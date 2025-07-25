// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <nvidia/conftest.h>

#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/if_ether.h>
#include <linux/etherdevice.h>
#include <linux/notifier.h>
#include <linux/completion.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/dma-buf.h>
#include <linux/scatterlist.h>
#include <linux/err.h>
#include <linux/limits.h>

#include <linux/tegra-capture-ivc.h>
#include <soc/tegra/nvethernet-public.h>
#include <soc/tegra/camrtc-capture-messages.h>
#include <soc/tegra/camrtc-capture.h>

#include <media/fusa-capture/capture-coe.h>
#include <media/fusa-capture/capture-common.h>

#define ETHER_PACKET_HDR_SIZE	64U

/** Helper macros to get the lower and higher 32bits of 64bit address */
#define L32(data)       ((uint32_t)((data) & 0xFFFFFFFFU))
#define H32(data)       ((uint32_t)(((data) & 0xFFFFFFFF00000000UL) >> 32UL))

/** HW OWN bit for the Rx desciptor in MGBE */
#define RDES3_OWN	BIT(31)

/** Corresponds to max number of Virtual DMA channels in MGBE device HW */
#define MAX_HW_CHANS_PER_DEVICE	48U
/** How many capture channels can actually be opened on each MGBE device */
#define MAX_ACTIVE_CHANS_PER_DEVICE	8U
#define MAX_NUM_COE_DEVICES	4U
#define MAX_ACTIVE_COE_CHANNELS	(MAX_ACTIVE_CHANS_PER_DEVICE * MAX_NUM_COE_DEVICES)

/** Size of a single Rx descriptor */
#define MGBE_RXDESC_SIZE		16U
/** Size of a Packet Info descriptor */
#define MGBE_PKTINFO_DESC_SIZE		16U
/** Max size of a buffer to be used to store Ethernet packet header (stripped from data payload) */
#define COE_MAX_PKT_HEADER_SIZE		64U

/** Maximum number of Rx descriptors in a Rx ring for a single channel */
#define COE_MGBE_MAX_RXDESC_NUM		16384U

/** Buffer offset field in CoE header is 28 bits wide (bits 0-27) */
#define COE_MGBE_MAX_BUF_SIZE		(1U << 28U)

/** Mask for the Rx frame buffer address. Must be 4K aligned. */
#define COE_MGBE_RXFRAMEBUF_MASK	0x0000FFFFFFFFF000ULL

/**
 * @brief Invalid CoE channel ID; the channel is not initialized.
 */
#define CAPTURE_COE_CHANNEL_INVALID_ID	U32_C(0xFFFFFFFF)

#define CAPTURE_COE_CHAN_INVALID_HW_ID	U8_C(0xFF)

#define COE_CHAN_CAPTURE_QUEUE_LEN	16U
/** Max number of physical DMA channel for each Eth controller */
#define COE_MGBE_MAX_NUM_PDMA_CHANS	10U
#define COE_MGBE_PDMA_CHAN_INVALID	COE_MGBE_MAX_NUM_PDMA_CHANS

/** To indicate non-registered buffer slots */
#define COE_BUFFER_IDX_INVALID	(-1)

/** State associated with a physical DMA channel of an Eth controller */
struct coe_pdma_state {
	/* Virtual pointer to Eth packet info memory */
	void *rx_pktinfo;
	/** MGBE DMA mapping of a memory area for Rx packet info descriptors */
	struct sg_table pktinfo_mgbe_sgt;
	/* Rx packet info memory DMA address for RCE engine */
	dma_addr_t rx_pktinfo_dma_rce;
};

/** Rx descriptor shadow ring for MGBE */
struct mgbe_rx_desc {
	uint32_t rdes0;
	uint32_t rdes1;
	uint32_t rdes2;
	uint32_t rdes3;
};

struct coe_state {
	struct platform_device *pdev;

	struct device *rtcpu_dev;
	/* Platform device object for MGBE controller (not a netdevice) */
	struct device *mgbe_dev;
	/** An ID of a corresponding mgbe_dev */
	u32 mgbe_id;

	struct notifier_block netdev_nb;

	struct list_head channels;
	/* Number of Rx descriptors in a descriptors ring for each channel */
	u16 rx_ring_size;
	/* Number of Rx Packet Info descriptors */
	u16 rx_pktinfo_ring_size;

	/* Bitmap indicating which DMA channels of the device are used for camera */
	DECLARE_BITMAP(dmachans_map, MAX_HW_CHANS_PER_DEVICE);
	/** Track how VDMAs map to physical DMA (PDMA) */
	u8 vdma2pdma_map[MAX_HW_CHANS_PER_DEVICE];
	/* List entry in a global list of probed devices */
	struct list_head device_entry;

	/** State of PDMA channels */
	struct coe_pdma_state pdmas[COE_MGBE_MAX_NUM_PDMA_CHANS];

	/** Protect access to the state object */
	struct mutex access_lock;

	/** MGBE IRQ ID which must be handled by camera CPU */
	u8 mgbe_irq_id;
};

struct coe_capreq_state_inhw {
	struct capture_common_unpins unpins;
	/**< Capture number passed with coe_ioctl_data_capture_req, assigned by a user
	 * to track the capture number in userspace.
	 * Valid range: [0, UINT32_MAX].
	 */
	u32 user_capture_id;
};

struct coe_capreq_state_unreported {
	u32 capture_status;
	u32 user_capture_id;
	u64 eofTimestamp;
	u64 sofTimestamp;
	u32 errData;
};

/* State of a single CoE (Camera Over Ethernet) capture channel */
struct coe_channel_state {
	/* Device object for the channel, from device_create */
	struct device *dev;
	/* Pointer to a parent platform device */
	struct coe_state *parent;
	/* List entry to allow parent platform device to keep track of its channels */
	struct list_head list_entry;
	/* Network device servicing the channel (child of a &parent) */
	struct net_device *netdev;

	/* Serialize operations on the channel */
	struct mutex channel_lock;

	/* Ethernet engine HW DMA channel ID which services memory accesses for
	 * that CoE channel */
	u8 dma_chan;
	/* Minor device ID, as registered with kernel (under /dev/ path) */
	dev_t devt;
	/* Channel ID assigned by RCE */
	u32 rce_chan_id;

	u8 sensor_mac_addr[ETH_HLEN];

	/* Flag indicating whether the channel has been open()'ed by userspace */
	bool opened;

	/* Scratch space to store a response from RCE */
	struct CAPTURE_CONTROL_MSG rce_resp_msg;
	/* Serialize accessing RCE response rce_resp_msg */
	struct mutex rce_msg_lock;
	/* Indication that RCE has responded to a command and response data
	 * is avaialble in rce_resp_msg
	 */
	struct completion rce_resp_ready;
	/**< Completion for capture-control IVC response */
	struct completion capture_resp_ready;

	/* Virtual pointer to Rx descriptor ring memory */
	void *rx_desc_ring_va;
	/* Rx descriptor ring memory DMA address for MGBE engine */
	struct sg_table rx_desc_mgbe_sgt;
	/* Rx descriptor ring memory DMA address for RCE engine */
	dma_addr_t rx_desc_dma_rce;

	/* Virtual pointer to Eth packet header memory, for each Rx descriptor */
	void *rx_pkt_hdrs;
	/* Rx packet headers memory DMA address for MGBE engine */
	dma_addr_t rx_pkt_hdrs_dma_mgbe;

	/* Virtual pointer to 'Prefilled' Eth shadow Rx ring memory */
	void *rx_desc_shdw;
	/* Rx desc shadow ring address for RCE engine */
	dma_addr_t rx_desc_shdw_dma_rce;

	/** A PDMA channel which services this CoE channel */
	u8 pdma_id;

	/**< Surface buffer management table */
	struct capture_buffer_table *buf_ctx;
	/** Tracks buffers registered by userspace to be used for capture requests */
	int32_t registered_bufs[COE_BUFFER_IDX_MAX_NUM];

	/**< Queue of capture requests waiting for capture completion from RCE */
	struct coe_capreq_state_inhw capq_inhw[COE_CHAN_CAPTURE_QUEUE_LEN];
	/**< Protect capq_inhw access */
	struct mutex capq_inhw_lock;
	/**< number of elements in capq_inhw */
	u16 capq_inhw_pending;
	/**< Next write index in capq_inhw */
	u16 capq_inhw_wr;
	/**< Next read index in capq_inhw */
	u16 capq_inhw_rd;

	/**< Captures reported by RCE, waiting to be reported to an app */
	struct coe_capreq_state_unreported capq_appreport[COE_CHAN_CAPTURE_QUEUE_LEN];
	/**< Protect capq_appreport access */
	struct mutex capq_appreport_lock;
	/**< number of elements in capq_appreport */
	u16 capq_appreport_pending;
	/**< Next write index in capq_appreport */
	u16 capq_appreport_wr;
	/**< Next read index in capq_appreport */
	u16 capq_appreport_rd;
};

/**
 * @brief Set up CoE channel resources and request FW channel allocation in RCE.
 *
 * @param[in]	ptr	Pointer to a struct @ref coe_ioctl_data_capture_setup
 *
 * @returns	0 (success), neg. errno (failure)
 */
#define COE_IOCTL_CAPTURE_SETUP \
	_IOW('I', 1, struct coe_ioctl_data_capture_setup)

/**
 * @brief Perform an operation on the buffer as specified in IOCTL
 * payload.
 *
 * @param[in]	ptr	Pointer to a struct @ref coe_ioctl_data_buffer_op
 * @returns	0 (success), neg. errno (failure)
 */
#define COE_IOCTL_BUFFER_OP \
	_IOW('I', 2, struct coe_ioctl_data_buffer_op)

/**
 * @brief Enqueue a capture request
 *
 * @param[in]	ptr	Pointer to a struct @ref coe_ioctl_data_capture_req
 * @returns	0 (success), neg. errno (failure)
 */
#define COE_IOCTL_CAPTURE_REQ \
	_IOW('I', 3, struct coe_ioctl_data_capture_req)

/**
 * Wait on the next completion of an enqueued frame, signalled by RCE.
 *
 * @note This call completes for the frame at the head of the FIFO queue, and is
 * not necessarily for the most recently enqueued capture request.
 *
 * @param[in,out]	ptr	Pointer to a struct @ref coe_ioctl_data_capture_status
 *
 * @returns	0 (success), neg. errno (failure)
 */
#define COE_IOCTL_CAPTURE_STATUS \
	_IOWR('I', 4, struct coe_ioctl_data_capture_status)

/**
 * @brief Get information about an open channel.
 *
 * @param[out]	ptr	Pointer to a struct @ref coe_ioctl_data_get_info
 * @returns	0 (success), neg. errno (failure)
 */
#define COE_IOCTL_GET_INFO \
	_IOR('I', 5, struct coe_ioctl_data_get_info)

/* List of all CoE platform devices which were successfully probed */
static LIST_HEAD(coe_device_list);
/* Lock to protect the list of CoE platform devices */
static DEFINE_MUTEX(coe_device_list_lock);

static struct class *coe_channel_class;
static int coe_channel_major;
static struct coe_channel_state coe_channels_arr[MAX_ACTIVE_COE_CHANNELS];
static DEFINE_MUTEX(coe_channels_arr_lock);

static inline struct coe_channel_state *coe_channel_arr_find_free(u32 * const arr_idx)
{
	u32 i;

	for (i = 0U; i < ARRAY_SIZE(coe_channels_arr); i++) {
		if (coe_channels_arr[i].dev == NULL) {
			*arr_idx = i;
			return &coe_channels_arr[i];
		}
	}

	return NULL;
}

/*
 * A callback to process RCE responses to commands issued through capture-control
 * IVC channel (struct CAPTURE_CONTROL_MSG).
 */
static void coe_rce_cmd_control_response_cb(const void *ivc_resp, const void *pcontext)
{
	const struct CAPTURE_CONTROL_MSG *r = ivc_resp;
	struct coe_channel_state * const ch = (struct coe_channel_state *)pcontext;

	switch (r->header.msg_id) {
	case CAPTURE_CHANNEL_SETUP_RESP:
	case CAPTURE_COE_CHANNEL_RESET_RESP:
	case CAPTURE_COE_CHANNEL_RELEASE_RESP:
		ch->rce_resp_msg = *r;
		complete(&ch->rce_resp_ready);
		break;
	default:
		dev_err(ch->dev, "unknown RCE control resp 0x%x",
			r->header.msg_id);
		break;
	}
}

static inline void coe_chan_buf_release(struct capture_buffer_table * const buf_ctx,
					struct coe_capreq_state_inhw * const buf)
{
	struct capture_common_unpins * const unpins = &buf->unpins;

	for (u32 i = 0U; i < unpins->num_unpins; i++) {
		if (buf_ctx != NULL && unpins->data[i] != NULL)
			put_mapping(buf_ctx, unpins->data[i]);
		unpins->data[i] = NULL;
	}
	unpins->num_unpins = 0U;
}

/*
 * A callback to process RCE responses to commands issued through capture
 * IVC channel (struct CAPTURE_MSG).
 */
static void coe_rce_cmd_capture_response_cb(const void *ivc_resp,
					    const void *pcontext)
{
	struct CAPTURE_MSG *msg = (struct CAPTURE_MSG *)ivc_resp;
	struct coe_channel_state * const ch = (struct coe_channel_state *)pcontext;

	if (ch == NULL || msg == NULL) {
		pr_err_ratelimited("Invalid RCE msg\n");
		return;
	}

	switch (msg->header.msg_id) {
	case CAPTURE_COE_STATUS_IND:
	{
		struct coe_capreq_state_unreported *unrep;
		u32 buf_idx;
		u32 capture_status;
		u32 user_capture_id;

		buf_idx = msg->capture_coe_status_ind.buffer_index;
		capture_status = msg->capture_coe_status_ind.capture_status;

		mutex_lock(&ch->capq_inhw_lock);

		if (ch->capq_inhw_pending == 0U) {
			mutex_unlock(&ch->capq_inhw_lock);
			return;
		}

		if (ch->capq_inhw_rd != buf_idx) {
			dev_warn_ratelimited(ch->dev, "Unexpected capture buf %u (expected %u)",
					    buf_idx, ch->capq_inhw_rd);
			mutex_unlock(&ch->capq_inhw_lock);
			return;
		}

		user_capture_id = ch->capq_inhw[buf_idx].user_capture_id;
		coe_chan_buf_release(ch->buf_ctx, &ch->capq_inhw[buf_idx]);

		ch->capq_inhw_pending--;
		ch->capq_inhw_rd = (ch->capq_inhw_rd + 1U) % ARRAY_SIZE(ch->capq_inhw);

		mutex_unlock(&ch->capq_inhw_lock);

		mutex_lock(&ch->capq_appreport_lock);

		if (ch->rce_chan_id == CAPTURE_COE_CHANNEL_INVALID_ID) {
			/* Channel was closed */
			mutex_unlock(&ch->capq_appreport_lock);
			return;
		}

		if (ch->capq_appreport_pending >= ARRAY_SIZE(ch->capq_appreport)) {
			dev_warn_ratelimited(ch->dev, "No space to report capture %u",
					     buf_idx);
			mutex_unlock(&ch->capq_appreport_lock);
			return;
		}

		unrep = &ch->capq_appreport[ch->capq_appreport_wr];
		unrep->capture_status = capture_status;
		unrep->user_capture_id = user_capture_id;
		unrep->eofTimestamp = msg->capture_coe_status_ind.timestamp_eof_ns;
		unrep->sofTimestamp = msg->capture_coe_status_ind.timestamp_sof_ns;
		unrep->errData = 0U;

		ch->capq_appreport_pending++;
		ch->capq_appreport_wr =
			(ch->capq_appreport_wr + 1U) % ARRAY_SIZE(ch->capq_appreport);

		mutex_unlock(&ch->capq_appreport_lock);

		complete(&ch->capture_resp_ready);

		break;
	}
	default:
		dev_err_ratelimited(ch->dev, "unknown RCE msg %u", msg->header.msg_id);
		break;
	}
}

static int coe_channel_open_on_rce(struct coe_channel_state *ch,
		uint8_t sensor_mac_addr[ETH_ALEN],
		uint8_t vlan_enable)
{
	struct CAPTURE_CONTROL_MSG control_desc;
	struct capture_coe_channel_config *config =
		&control_desc.channel_coe_setup_req.channel_config;
	struct CAPTURE_CONTROL_MSG const * const resp = &ch->rce_resp_msg;
	int ret;
	u32 transaction;
	unsigned long timeout = HZ;
	u32 rce_chan_id = CAPTURE_COE_CHANNEL_INVALID_ID;

	ret = tegra_capture_ivc_register_control_cb(&coe_rce_cmd_control_response_cb,
						    &transaction, ch);
	if (ret < 0) {
		dev_err(ch->dev, "failed to register control callback: %d\n", ret);
		return ret;
	}

	memset(&control_desc, 0, sizeof(control_desc));
	control_desc.header.msg_id = CAPTURE_COE_CHANNEL_SETUP_REQ;
	control_desc.header.transaction = transaction;

	config->mgbe_instance_id = ch->parent->mgbe_id;
	config->mgbe_irq_num = ch->parent->mgbe_irq_id;
	config->dma_chan = ch->dma_chan;
	config->pdma_chan_id = ch->pdma_id;
	memcpy(config->mac_addr, sensor_mac_addr, ETH_ALEN);

	config->rx_desc_ring_iova_mgbe = sg_dma_address(ch->rx_desc_mgbe_sgt.sgl);
	config->rx_desc_ring_iova_rce = ch->rx_desc_dma_rce;
	config->rx_desc_ring_mem_size = ch->parent->rx_ring_size * MGBE_RXDESC_SIZE;

	config->rx_desc_shdw_iova_rce = ch->rx_desc_shdw_dma_rce;

	config->rx_pkthdr_iova_mgbe = ch->rx_pkt_hdrs_dma_mgbe;
	config->rx_pkthdr_mem_size = ch->parent->rx_ring_size * COE_MAX_PKT_HEADER_SIZE;

	config->rx_pktinfo_iova_mgbe =
		sg_dma_address(ch->parent->pdmas[ch->pdma_id].pktinfo_mgbe_sgt.sgl);
	config->rx_pktinfo_iova_rce = ch->parent->pdmas[ch->pdma_id].rx_pktinfo_dma_rce;
	config->rx_pktinfo_mem_size = ch->parent->rx_pktinfo_ring_size * MGBE_PKTINFO_DESC_SIZE;

	config->vlan_enable = vlan_enable;
	config->rx_queue_depth = ARRAY_SIZE(ch->capq_inhw);

	mutex_lock(&ch->rce_msg_lock);

	ret = tegra_capture_ivc_control_submit(&control_desc, sizeof(control_desc));
	if (ret < 0) {
		dev_err(ch->dev, "IVC control submit failed\n");
		goto err;
	}

	timeout = wait_for_completion_timeout(&ch->rce_resp_ready, timeout);
	if (timeout <= 0) {
		dev_err(ch->dev, "capture control message timed out\n");
		ret = -ETIMEDOUT;

		goto err;
	}

	if (resp->header.msg_id != CAPTURE_CHANNEL_SETUP_RESP ||
	    resp->header.transaction != transaction) {
		dev_err(ch->dev, "%s: wrong msg id 0x%x transaction %u!\n", __func__,
				resp->header.msg_id, resp->header.transaction);
		ret = -EINVAL;
		goto err;
	};

	if (resp->channel_setup_resp.result != CAPTURE_OK) {
		dev_err(ch->dev, "%s: control failed, errno %d", __func__,
			resp->channel_setup_resp.result);
		ret = -EINVAL;
		goto err;
	}

	rce_chan_id = resp->channel_setup_resp.channel_id;

	mutex_unlock(&ch->rce_msg_lock);

	ret = tegra_capture_ivc_notify_chan_id(rce_chan_id, transaction);
	if (ret != 0) {
		dev_err(ch->dev, "failed to update control callback\n");
		tegra_capture_ivc_unregister_control_cb(transaction);
		return ret;
	}

	ret = tegra_capture_ivc_register_capture_cb(
			&coe_rce_cmd_capture_response_cb,
			rce_chan_id, ch);
	if (ret != 0) {
		dev_err(ch->dev, "failed to register capture callback\n");
		tegra_capture_ivc_unregister_control_cb(rce_chan_id);
		return ret;
	}

	ch->rce_chan_id = rce_chan_id;

	return 0;

err:
	mutex_unlock(&ch->rce_msg_lock);

	tegra_capture_ivc_unregister_control_cb(transaction);

	return ret;
}

static int coe_chan_rce_capture_req(struct coe_channel_state * const ch,
				    u16 const buf_idx,
				    u64 const buf_mgbe_iova,
				    u32 const buf_len)
{
	struct CAPTURE_MSG rce_desc = {0U};
	int ret;

	rce_desc.header.msg_id = CAPTURE_COE_REQUEST;
	rce_desc.header.channel_id = ch->rce_chan_id;
	rce_desc.capture_coe_req.buffer_index = buf_idx;
	rce_desc.capture_coe_req.buf_mgbe_iova = buf_mgbe_iova;
	rce_desc.capture_coe_req.buf_len = buf_len;

	ret = tegra_capture_ivc_capture_submit(&rce_desc, sizeof(rce_desc));
	if (ret < 0) {
		dev_err(ch->dev, "IVC capture submit failed\n");
		return ret;
	}

	return 0;
}

static int coe_ioctl_handle_capture_req(struct coe_channel_state * const ch,
					const struct coe_ioctl_data_capture_req * const req)
{
	uint64_t mgbe_iova;
	uint64_t buf_max_size;
	uint32_t alloc_size_min;
	int ret;
	struct capture_common_unpins *unpins = NULL;
	int32_t mem_fd;

	if (req->buf_size == 0U || req->buf_size >= COE_MGBE_MAX_BUF_SIZE) {
		dev_err_ratelimited(ch->dev, "CAPTURE_REQ: bad buf size %u\n",
				    req->buf_size);
		return -EINVAL;
	}

	if (req->buffer_idx >= ARRAY_SIZE(ch->registered_bufs)) {
		dev_err_ratelimited(ch->dev, "CAPTURE_REQ: bad buf index %u\n",
				    req->buffer_idx);
		return -EINVAL;
	}

	mem_fd = ch->registered_bufs[req->buffer_idx];
	if (mem_fd < 0) {
		dev_err_ratelimited(ch->dev, "CAPTURE_REQ: buf not registered %u\n",
				    req->buffer_idx);
		return -EBADFD;
	}

	mutex_lock(&ch->capq_inhw_lock);

	if (ch->capq_inhw_pending >= ARRAY_SIZE(ch->capq_inhw)) {
		dev_warn_ratelimited(ch->dev, "CAPTURE_REQ: Rx queue is full\n");
		ret = -EAGAIN;
		goto error;
	}

	if (ch->rce_chan_id == CAPTURE_COE_CHANNEL_INVALID_ID) {
		dev_warn_ratelimited(ch->dev, "CAPTURE_REQ: chan not opened\n");
		ret = -ENOTCONN;
		goto error;
	}

	unpins = &ch->capq_inhw[ch->capq_inhw_wr].unpins;
	ret = capture_common_pin_and_get_iova(ch->buf_ctx,
					      (uint32_t)mem_fd,
					      req->mem_fd_offset,
					      &mgbe_iova,
					      &buf_max_size,
					      unpins);

	if (ret) {
		dev_err(ch->dev, "get buf iova failed: %d\n", ret);
		goto error;
	}

	if ((mgbe_iova & ~COE_MGBE_RXFRAMEBUF_MASK) != 0U) {
		dev_err(ch->dev, "CAPTURE_REQ: bad buf iova 0x%llx\n", mgbe_iova);
		ret = -ERANGE;
		goto error;
	}

	/* Hardware can limit memory access within a range of powers of two only.
	 * Make sure DMA buffer allocation is large enough to at least cover the memory
	 * up to the next closest power of two boundary to eliminate a risk of a malformed
	 * incoming network packet triggerring invalid memory access.
	 */
#define L4T7463
#ifndef L4T7463
	alloc_size_min = roundup_pow_of_two(req->buf_size);
#else
	alloc_size_min = req->buf_size;
#endif

	if (alloc_size_min > buf_max_size) {
		dev_err(ch->dev, "CAPTURE_REQ: capture too long %u\n", req->buf_size);
		ret = -ENOSPC;
		goto error;
	}

	ret = coe_chan_rce_capture_req(ch, ch->capq_inhw_wr, mgbe_iova, req->buf_size);
	if (ret)
		goto error;

	ch->capq_inhw[ch->capq_inhw_wr].user_capture_id = req->capture_number;
	ch->capq_inhw_pending++;
	ch->capq_inhw_wr = (ch->capq_inhw_wr + 1U) % ARRAY_SIZE(ch->capq_inhw);

	mutex_unlock(&ch->capq_inhw_lock);

	return 0;

error:
	if (unpins && unpins->num_unpins != 0) {
		u32 i;

		for (i = 0U; i < unpins->num_unpins; i++) {
			if (ch->buf_ctx != NULL && unpins->data[i] != NULL)
				put_mapping(ch->buf_ctx, unpins->data[i]);
		}
		(void)memset(unpins, 0U, sizeof(*unpins));
	}

	mutex_unlock(&ch->capq_inhw_lock);

	return ret;
}

static int coe_ioctl_handle_capture_status(struct coe_channel_state * const ch,
					   struct coe_ioctl_data_capture_status * const req)
{
	int ret;
	const s32 timeout_ms = (s32)req->timeout_ms;

	if (ch->rce_chan_id == CAPTURE_COE_CHANNEL_INVALID_ID) {
		dev_err(ch->dev, "CAPTURE_STATUS: chan not opened\n");
		return -ENOTCONN;
	}

	dev_dbg_ratelimited(ch->dev, "CAPTURE_STATUS num=%u timeout:%d ms\n",
			    req->capture_number, timeout_ms);

	/* negative timeout means wait forever */
	if (timeout_ms < 0) {
		ret = wait_for_completion_interruptible(&ch->capture_resp_ready);
		if (ret == -ERESTARTSYS) {
			dev_dbg_ratelimited(ch->dev, "capture status interrupted\n");
			return -ETIMEDOUT;
		}
	} else {
		ret = wait_for_completion_timeout(
				&ch->capture_resp_ready,
				msecs_to_jiffies(timeout_ms));
		if (ret == 0) {
			dev_dbg_ratelimited(ch->dev, "capture status timed out\n");
			return -ETIMEDOUT;
		}
	}

	if (ret < 0) {
		dev_err_ratelimited(ch->dev, "wait for capture status failed\n");
		return ret;
	}

	mutex_lock(&ch->capq_appreport_lock);

	if (ch->capq_appreport_pending == 0) {
		dev_warn_ratelimited(ch->dev, "No captures pending\n");
		mutex_unlock(&ch->capq_appreport_lock);
		return -ENODATA;
	}

	req->capture_status = ch->capq_appreport[ch->capq_appreport_rd].capture_status;
	req->capture_number = ch->capq_appreport[ch->capq_appreport_rd].user_capture_id;
	req->eofTimestamp = ch->capq_appreport[ch->capq_appreport_rd].eofTimestamp;
	req->sofTimestamp = ch->capq_appreport[ch->capq_appreport_rd].sofTimestamp;
	req->errData = ch->capq_appreport[ch->capq_appreport_rd].errData;
	ch->capq_appreport_pending--;
	ch->capq_appreport_rd = (ch->capq_appreport_rd + 1U) % ARRAY_SIZE(ch->capq_appreport);

	mutex_unlock(&ch->capq_appreport_lock);

	return 0;
}

/**
 * Calculate total size of contiguous DMA memory in scatterlist
 * @sgl: scatterlist to examine
 * @nents: number of entries in scatterlist
 *
 * Contiguous means that for every entry in scatterlist,
 * sg_dma_address(sg) + sg_dma_len(sg) of current entry must be equal to
 * sg_dma_address(sg) of the next element.
 *
 * Returns: size of contiguous memory region starting from first entry,
 *          0 if scatterlist is empty or invalid
 */
static size_t coe_calc_contiguous_dma_size(struct scatterlist *sgl, unsigned int nents)
{
	struct scatterlist *sg;
	size_t total_size = 0;
	dma_addr_t next_addr;
	unsigned int i;

	if (!sgl || nents == 0)
		return 0;

	for_each_sg(sgl, sg, nents, i) {
		if (i > 0 && sg_dma_address(sg) != next_addr)
			break;

		total_size += sg_dma_len(sg);
		next_addr = sg_dma_address(sg) + sg_dma_len(sg);
	}

	return total_size;
}

static void coe_unmap_and_free_dma_buf(
	struct coe_state * const s,
	size_t size,
	void *va,
	dma_addr_t dma_handle,
	struct sg_table *sgt)
{
	if (sgt->sgl) {
		dma_unmap_sg(s->mgbe_dev, sgt->sgl, sgt->orig_nents, DMA_BIDIRECTIONAL);
		sg_free_table(sgt);
	}

	if (va)
		dma_free_coherent(s->rtcpu_dev, size, va, dma_handle);
}

static void *coe_alloc_and_map_dma_buf(
	struct coe_state * const s,
	size_t size,
	dma_addr_t *dma_handle,
	struct sg_table *sgt)
{
	void *va;
	int ret;
	size_t real_size;

	va = dma_alloc_coherent(s->rtcpu_dev, size, dma_handle, GFP_KERNEL | __GFP_ZERO);
	if (!va)
		return ERR_PTR(-ENOMEM);

	ret = dma_get_sgtable(s->rtcpu_dev, sgt, va, *dma_handle, size);
	if (ret < 0) {
		dev_err(&s->pdev->dev, "Failed to get SGT ret=%d\n", ret);
		goto err_free_dma;
	}

	ret = dma_map_sg(s->mgbe_dev, sgt->sgl, sgt->orig_nents, DMA_BIDIRECTIONAL);
	if (ret <= 0) {
		dev_err(&s->pdev->dev, "Failed to map SG table ret=%d\n", ret);
		ret = ret ? : -EFAULT;
		goto err_free_sgt;
	}
	sgt->nents = ret;

	real_size = coe_calc_contiguous_dma_size(sgt->sgl, sgt->nents);
	if (real_size < size) {
		dev_err(&s->pdev->dev, "buffer not contiguous\n");
		ret = -ENOMEM;
		goto err_unmap_sg;
	}

	return va;

err_unmap_sg:
	dma_unmap_sg(s->mgbe_dev, sgt->sgl, sgt->orig_nents, DMA_BIDIRECTIONAL);
err_free_sgt:
	sg_free_table(sgt);
err_free_dma:
	dma_free_coherent(s->rtcpu_dev, size, va, *dma_handle);
	return ERR_PTR(ret);
}

static void coe_chan_rxring_release(struct coe_channel_state * const ch)
{
	size_t rx_ring_alloc_size = ch->parent->rx_ring_size * MGBE_RXDESC_SIZE;

	coe_unmap_and_free_dma_buf(ch->parent,
				   rx_ring_alloc_size,
				   ch->rx_desc_ring_va, ch->rx_desc_dma_rce,
				   &ch->rx_desc_mgbe_sgt);
	ch->rx_desc_ring_va = NULL;
}

static int coe_ioctl_handle_buffer_op(struct coe_channel_state * const ch,
				      const struct coe_ioctl_data_buffer_op * const req)
{
	int ret;
	const bool is_adding = req->flag & BUFFER_ADD;
	int32_t memfd;

	if (req->buffer_idx >= ARRAY_SIZE(ch->registered_bufs)) {
		dev_err(ch->dev, "BUFFER_OP: invalid index %u\n", req->buffer_idx);
		return -EINVAL;
	}

	mutex_lock(&ch->channel_lock);

	if (ch->rce_chan_id == CAPTURE_COE_CHANNEL_INVALID_ID) {
		dev_err(ch->dev, "BUFFER_OP: chan not opened\n");
		ret = -ENOTCONN;
		goto unlock_and_return;
	}

	if (is_adding) {
		if (req->mem > S32_MAX) {
			dev_err(ch->dev, "BUFFER_OP: invalid buf %u\n", req->mem);
			ret = -EINVAL;
			goto unlock_and_return;
		}

		if (ch->registered_bufs[req->buffer_idx] >= 0) {
			dev_err(ch->dev, "BUFFER_OP: buffer idx busy %u\n",
				req->buffer_idx);
			ret = -EBUSY;
			goto unlock_and_return;
		}

		memfd = req->mem;
	} else {
		memfd = ch->registered_bufs[req->buffer_idx];
		if (memfd < 0) {
			dev_err(ch->dev, "BUFFER_OP: buffer idx not registered %u\n",
				req->buffer_idx);
			ret = -EBADFD;
			goto unlock_and_return;
		}
	}

	ret = capture_buffer_request(ch->buf_ctx, memfd, req->flag);
	if (ret < 0) {
		dev_err(ch->dev, "BUFFER_OP: failed flag=0x%x idx=%u: %d\n",
			req->flag, req->buffer_idx, ret);
		goto unlock_and_return;
	}

	// Update buffer state on success
	ch->registered_bufs[req->buffer_idx] =
		is_adding ? (int32_t)req->mem : COE_BUFFER_IDX_INVALID;

	dev_dbg(ch->dev, "BUFFER_OP: OK flag=0x%x idx=%u\n",
		req->flag, req->buffer_idx);

unlock_and_return:
	mutex_unlock(&ch->channel_lock);
	return ret;
}

static int coe_ioctl_handle_setup_channel(struct coe_channel_state * const ch,
			       struct coe_ioctl_data_capture_setup *setup)
{
	struct nvether_coe_cfg g_coe_cfg;
	struct nvether_per_coe_cfg per_coe_cfg;
	struct net_device *ndev;
	struct mgbe_rx_desc *rx_desc_shdw_ring;
	struct coe_state *parent;
	struct device *find_dev = NULL;
	uint32_t dma_chan;
	u8 pdma_chan;
	int ret;

	if (ch->rce_chan_id != CAPTURE_COE_CHANNEL_INVALID_ID ||
		ch->buf_ctx != NULL) {
		dev_err(ch->dev, "Chan already opened\n");
		return -EBUSY;
	}

	if (MINOR(ch->devt) >= ARRAY_SIZE(coe_channels_arr)) {
		dev_err(ch->dev, "Bad chan Minor\n");
		return -EFAULT;
	}

	mutex_lock(&coe_device_list_lock);
	list_for_each_entry(parent, &coe_device_list, device_entry) {
		find_dev = device_find_child_by_name(parent->mgbe_dev,
						     setup->if_name);
		if (find_dev != NULL)
			break;

	}
	mutex_unlock(&coe_device_list_lock);

	if (find_dev == NULL) {
		dev_err(ch->dev, "Can't find netdev %s\n", setup->if_name);
		return -ENODEV;
	}

	ndev = to_net_dev(find_dev);

	/* Check if the network interface is UP */
	if (!netif_running(ndev)) {
		dev_err(ch->dev, "Network interface %s is not UP\n",
			netdev_name(ndev));
		put_device(find_dev);
		return -ENETDOWN;
	}

	dma_chan = find_first_bit(parent->dmachans_map, MAX_HW_CHANS_PER_DEVICE);
	if (dma_chan >= MAX_HW_CHANS_PER_DEVICE) {
		dev_err(&parent->pdev->dev,
			"No DMA chans left %s\n", setup->if_name);
		put_device(find_dev);
		return -ENOENT;
	}

	pdma_chan = parent->vdma2pdma_map[dma_chan];
	if ((pdma_chan >= ARRAY_SIZE(parent->pdmas)) ||
	    (parent->pdmas[pdma_chan].rx_pktinfo == NULL)) {
		dev_err(&parent->pdev->dev, "Bad PDMA chan %u\n", pdma_chan);
		put_device(find_dev);
		return -EFAULT;
	}

	ret = device_move(ch->dev, &parent->pdev->dev, DPM_ORDER_NONE);
	if (ret) {
		dev_err(ch->dev, "Can't move state\n");
		put_device(find_dev);
		return ret;
	}

	ch->parent = parent;
	/* Store netdev reference - it will be released in coe_channel_close() */
	ch->netdev = ndev;
	ch->dma_chan = dma_chan;
	ch->pdma_id = pdma_chan;
	clear_bit(dma_chan, parent->dmachans_map);
	list_add(&ch->list_entry, &parent->channels);
	reinit_completion(&ch->capture_resp_ready);

	ch->rx_desc_ring_va = coe_alloc_and_map_dma_buf(ch->parent,
					 ch->parent->rx_ring_size * MGBE_RXDESC_SIZE,
					 &ch->rx_desc_dma_rce,
					 &ch->rx_desc_mgbe_sgt);
	if (IS_ERR(ch->rx_desc_ring_va)) {
		dev_err(ch->dev, "Failed to alloc Rx ring\n");
		ret = PTR_ERR(ch->rx_desc_ring_va);
		ch->rx_desc_ring_va = NULL;
		goto err_list_del;
	}

	ch->buf_ctx = create_buffer_table(ch->parent->mgbe_dev);
	if (ch->buf_ctx == NULL) {
		dev_err(ch->dev, "Failed to alloc buffers table\n");
		ret = -ENOMEM;
		goto err_unmap_ring;
	}

	g_coe_cfg.coe_enable = COE_ENABLE;

	if (setup->vlan_enable == COE_VLAN_ENABLE) {
		g_coe_cfg.vlan_enable = COE_VLAN_ENABLE;
		g_coe_cfg.coe_hdr_offset = COE_MACSEC_HDR_OFFSET;
	} else {
		g_coe_cfg.vlan_enable = COE_VLAN_DISABLE;
		g_coe_cfg.coe_hdr_offset = COE_MACSEC_HDR_VLAN_DISABLE_OFFSET;
	}

	ret = nvether_coe_config(ndev, &g_coe_cfg);
	if (ret != 0) {
		dev_err(ch->dev, "COE config failed for ch %u\n", ch->dma_chan);
		return ret;
	}

	per_coe_cfg.lc1 = COE_MACSEC_SFT_LC1;
	per_coe_cfg.lc2 = COE_MACSEC_SFT_LC2;
	ret = nvether_coe_chan_config(ndev, ch->dma_chan, &per_coe_cfg);
	if (ret != 0) {
		dev_err(ch->dev, "Failed to setup line counters %u\n", ch->dma_chan);
		return ret;
	}

	ether_addr_copy(ch->sensor_mac_addr, setup->sensor_mac_addr);

	ch->rx_pkt_hdrs = dma_alloc_coherent(ch->parent->mgbe_dev,
		ch->parent->rx_ring_size * COE_MAX_PKT_HEADER_SIZE,
		&ch->rx_pkt_hdrs_dma_mgbe,
		GFP_KERNEL | __GFP_ZERO);
	if (ch->rx_pkt_hdrs == NULL) {
		dev_err(ch->dev, "Rx pkt headers alloc failed\n");
		ret = -ENOMEM;
		goto err_destroy_buf_table;
	}
	ch->rx_desc_shdw = dma_alloc_coherent(ch->parent->rtcpu_dev,
					      ch->parent->rx_ring_size * MGBE_RXDESC_SIZE,
					      &ch->rx_desc_shdw_dma_rce,
					      GFP_KERNEL);
	if (ch->rx_desc_shdw == NULL) {
		dev_err(ch->dev, "Rx desc shadow ring alloc failed\n");
		ret = -ENOMEM;
		goto err_free_pkt_hdrs;
	}

	/* Pre-fill the shadow Rx desc ring with the header buffers */
	rx_desc_shdw_ring = (struct mgbe_rx_desc *) ch->rx_desc_shdw;
	for (uint32_t i = 0; i < ch->parent->rx_ring_size; i++) {
		rx_desc_shdw_ring[i].rdes0 = L32(ch->rx_pkt_hdrs_dma_mgbe + (i * ETHER_PACKET_HDR_SIZE));
		rx_desc_shdw_ring[i].rdes1 = H32(ch->rx_pkt_hdrs_dma_mgbe + (i * ETHER_PACKET_HDR_SIZE));
		rx_desc_shdw_ring[i].rdes2 = 0U;
		rx_desc_shdw_ring[i].rdes3 = 0U;
		rx_desc_shdw_ring[i].rdes3 |= RDES3_OWN;
	}

	ret = coe_channel_open_on_rce(ch, setup->sensor_mac_addr, setup->vlan_enable);
	if (ret)
		goto err_free_rx_desc_shdw;

	dev_info(&parent->pdev->dev, "CoE chan added %s dmachan=%u num_desc=%u\n",
		 netdev_name(ndev), ch->dma_chan, ch->parent->rx_ring_size);

	return 0;

err_free_rx_desc_shdw:
	dma_free_coherent(ch->parent->rtcpu_dev,
			  ch->parent->rx_ring_size * MGBE_RXDESC_SIZE,
			  ch->rx_desc_shdw,
			  ch->rx_desc_shdw_dma_rce);
	ch->rx_desc_shdw = NULL;
err_free_pkt_hdrs:
	dma_free_coherent(ch->parent->mgbe_dev,
			  ch->parent->rx_ring_size * COE_MAX_PKT_HEADER_SIZE,
			  ch->rx_pkt_hdrs, ch->rx_pkt_hdrs_dma_mgbe);
	ch->rx_pkt_hdrs = NULL;
err_destroy_buf_table:
	destroy_buffer_table(ch->buf_ctx);
	ch->buf_ctx = NULL;
err_unmap_ring:
	coe_chan_rxring_release(ch);
err_list_del:
	list_del(&ch->list_entry);
	set_bit(ch->dma_chan, ch->parent->dmachans_map);
	ch->parent = NULL;
	ch->netdev = NULL;
	ch->dma_chan = CAPTURE_COE_CHAN_INVALID_HW_ID;
	put_device(find_dev);
	return ret;
}

static long coe_fop_channel_ioctl(
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct coe_channel_state *ch = file->private_data;
	void __user *ptr = (void __user *)arg;
	long ret;

	if (ch == NULL || ch->dev == NULL) {
		pr_err("CoE IOCTL invalid channel\n");
		return -EINVAL;
	}

	if (_IOC_NR(cmd) != _IOC_NR(COE_IOCTL_CAPTURE_SETUP)) {
		if (ch->parent == NULL || ch->netdev == NULL) {
			dev_err(ch->dev, "CoE channel is not set up\n");
			return -ENOTCONN;
		}
	}

	switch (_IOC_NR(cmd)) {
	case _IOC_NR(COE_IOCTL_CAPTURE_SETUP):
	{
		struct coe_ioctl_data_capture_setup setup;

		if (copy_from_user(&setup, ptr, sizeof(setup))) {
			return -EFAULT;
		}

		ret = coe_ioctl_handle_setup_channel(ch, &setup);
		if (ret != 0)
			return ret;
		break;
	}
	case _IOC_NR(COE_IOCTL_BUFFER_OP):
	{
		struct coe_ioctl_data_buffer_op req;

		ret = copy_from_user(&req, ptr, sizeof(req));
		if (ret != 0)
			return ret;

		ret = coe_ioctl_handle_buffer_op(ch, &req);
		break;
	}
	case _IOC_NR(COE_IOCTL_CAPTURE_REQ):
	{
		struct coe_ioctl_data_capture_req req;

		ret = copy_from_user(&req, ptr, sizeof(req));
		if (ret != 0)
			return ret;

		ret = coe_ioctl_handle_capture_req(ch, &req);
		break;
	}
	case _IOC_NR(COE_IOCTL_CAPTURE_STATUS):
	{
		struct coe_ioctl_data_capture_status req;

		ret = copy_from_user(&req, ptr, sizeof(req));
		if (ret != 0)
			return ret;

		ret = coe_ioctl_handle_capture_status(ch, &req);
		if (ret < 0) {
			dev_err(ch->dev, "CoE capture status failed: %ld\n",
				ret);
			return ret;
		}

		ret = copy_to_user(ptr, &req, sizeof(req));
		if (ret != 0)
			return ret;

		break;
	}
	case _IOC_NR(COE_IOCTL_GET_INFO):
	{
		struct coe_ioctl_data_get_info ret_info = {0U};

		if (ch->dma_chan == CAPTURE_COE_CHAN_INVALID_HW_ID) {
			dev_err(ch->dev, "CoE chan HW ID not set yet\n");
			return -EAGAIN;
		}

		ret_info.channel_number = ch->dma_chan;
		ret = copy_to_user(ptr, &ret_info, sizeof(ret_info));
		if (ret != 0)
			return ret;
		break;
	}
	default:
		dev_err(ch->dev, "Unknown IOCTL 0x%x\n", _IOC_NR(cmd));
		ret = -EIO;
		break;
	}

	return ret;
}

static int coe_fop_channel_open(
	struct inode *inode,
	struct file *file)
{
	struct coe_channel_state *ch;
	unsigned int chan_id = iminor(inode);
	int ret;

	if (chan_id >= ARRAY_SIZE(coe_channels_arr)) {
		pr_err("CoE: open chan invalid minor %u\n", chan_id);
		return -ENXIO;
	}

	if (mutex_lock_interruptible(&coe_channels_arr_lock))
		return -ERESTARTSYS;

	ch = &coe_channels_arr[chan_id];

	if (ch->devt != inode->i_rdev) {
		pr_err("CoE: open chan mismatch devt %u!=%u\n",
			ch->devt, inode->i_rdev);
		ret = -ENXIO;
		goto mutex_unlock;
	}

	if (ch->dev == NULL) {
		pr_err("CoE: open chan bad state\n");
		ret = -EFAULT;
		goto mutex_unlock;
	}

	if (ch->opened) {
		dev_dbg(ch->dev, "CoE channel is busy\n");
		ret = -EBUSY;
		goto mutex_unlock;
	}

	file->private_data = ch;
	ch->opened = true;

	for (uint32_t i = 0U; i < ARRAY_SIZE(ch->registered_bufs); i++)
		ch->registered_bufs[i] = COE_BUFFER_IDX_INVALID;

	ret = nonseekable_open(inode, file);

mutex_unlock:
	mutex_unlock(&coe_channels_arr_lock);
	return ret;
}

static int coe_channel_reset_rce(struct coe_channel_state *ch)
{
	struct CAPTURE_CONTROL_MSG control_desc;
	struct CAPTURE_CONTROL_MSG const * const resp = &ch->rce_resp_msg;
	///@todo A capture reset barrier ind message is also needed
	///      This would be similar to how both VI and ISP handle reset
	int ret;
	unsigned long timeout = HZ;

	if (ch->rce_chan_id == CAPTURE_COE_CHANNEL_INVALID_ID) {
		dev_dbg(ch->dev, "%s: CoE channel not set up\n", __func__);
		return 0;
	}

	dev_info(ch->dev, "Reset CoE chan rce %u, rce_chan_id %u\n",
			MINOR(ch->devt), ch->rce_chan_id);

	memset(&control_desc, 0, sizeof(control_desc));
	control_desc.header.msg_id = CAPTURE_COE_CHANNEL_RESET_REQ;
	control_desc.header.channel_id = ch->rce_chan_id;

	mutex_lock(&ch->rce_msg_lock);

	ret = tegra_capture_ivc_control_submit(&control_desc, sizeof(control_desc));
	if (ret < 0) {
		dev_info(ch->dev, "IVC control submit failed\n");
		goto mutex_unlock;
	}

	timeout = wait_for_completion_timeout(&ch->rce_resp_ready, timeout);
	if (timeout <= 0) {
		dev_info(ch->dev, "capture control message timed out\n");
		ret = -ETIMEDOUT;
		goto mutex_unlock;
	}

	if (resp->header.msg_id != CAPTURE_COE_CHANNEL_RESET_RESP) {
		dev_info(ch->dev, "%s: wrong msg id 0x%x\n", __func__, resp->header.msg_id);
		ret = -EINVAL;
		goto mutex_unlock;
	};

	if (resp->channel_coe_reset_resp.result != CAPTURE_OK) {
		dev_info(ch->dev, "%s: control failed, errno %d", __func__,
			resp->channel_coe_reset_resp.result);
		ret = -EINVAL;
		goto mutex_unlock;
	}

mutex_unlock:
	mutex_unlock(&ch->rce_msg_lock);

	return ret;
}

///@todo refactor reset and release to use common code to send IVC
static int coe_channel_release_rce(struct coe_channel_state *ch)
{
	struct CAPTURE_CONTROL_MSG control_desc;
	struct CAPTURE_CONTROL_MSG const * const resp = &ch->rce_resp_msg;
	int ret;
	unsigned long timeout = HZ;

	if (ch->rce_chan_id == CAPTURE_COE_CHANNEL_INVALID_ID) {
		dev_dbg(ch->dev, "%s: CoE channel not set up\n", __func__);
		return 0;
	}

	dev_info(ch->dev, "Release CoE chan rce %u, rce_chan_id %u\n",
		 MINOR(ch->devt), ch->rce_chan_id);

	memset(&control_desc, 0, sizeof(control_desc));
	control_desc.header.msg_id = CAPTURE_COE_CHANNEL_RELEASE_REQ;
	control_desc.header.channel_id = ch->rce_chan_id;

	mutex_lock(&ch->rce_msg_lock);

	ret = tegra_capture_ivc_control_submit(&control_desc, sizeof(control_desc));
	if (ret < 0) {
		dev_info(ch->dev, "IVC control submit failed\n");
		goto mutex_unlock;
	}

	timeout = wait_for_completion_timeout(&ch->rce_resp_ready, timeout);
	if (timeout <= 0) {
		dev_info(ch->dev, "capture control message timed out\n");
		ret = -ETIMEDOUT;
		goto mutex_unlock;
	}

	if (resp->header.msg_id != CAPTURE_COE_CHANNEL_RELEASE_RESP) {
		dev_info(ch->dev, "%s: wrong msg id 0x%x\n", __func__, resp->header.msg_id);
		ret = -EINVAL;
		goto mutex_unlock;
	};

	if (resp->channel_coe_release_resp.result != CAPTURE_OK) {
		dev_info(ch->dev, "%s: control failed, errno %d", __func__,
			resp->channel_coe_reset_resp.result);
		ret = -EINVAL;
		goto mutex_unlock;
	}

mutex_unlock:
	mutex_unlock(&ch->rce_msg_lock);

	return ret;
}

static int coe_channel_close(struct coe_channel_state *ch)
{
	if (!ch->opened)
		return 0;

	dev_info(ch->dev, "Closing CoE chan %u\n", MINOR(ch->devt));

	mutex_lock(&ch->channel_lock);
	mutex_lock(&ch->capq_inhw_lock);

	coe_channel_reset_rce(ch);

	for (u32 buf_idx = 0U; buf_idx < ARRAY_SIZE(ch->capq_inhw); buf_idx++) {
		coe_chan_buf_release(ch->buf_ctx, &ch->capq_inhw[buf_idx]);
	}

	ch->capq_inhw_pending = 0U;
	ch->capq_inhw_wr = 0U;
	ch->capq_inhw_rd = 0U;

	coe_channel_release_rce(ch);

	if (ch->rce_chan_id != CAPTURE_COE_CHANNEL_INVALID_ID) {
		tegra_capture_ivc_unregister_capture_cb(ch->rce_chan_id);
		tegra_capture_ivc_unregister_control_cb(ch->rce_chan_id);

		ch->rce_chan_id = CAPTURE_COE_CHANNEL_INVALID_ID;
	}

	mutex_unlock(&ch->capq_inhw_lock);

	mutex_lock(&ch->capq_appreport_lock);

	for (u32 buf_idx = 0U; buf_idx < ARRAY_SIZE(ch->capq_appreport); buf_idx++) {
		ch->capq_appreport[buf_idx].capture_status = CAPTURE_STATUS_UNKNOWN;
	}

	ch->capq_appreport_pending = 0U;
	ch->capq_appreport_rd = 0U;
	ch->capq_appreport_wr = 0U;
	complete_all(&ch->capture_resp_ready);

	mutex_unlock(&ch->capq_appreport_lock);

	for (uint32_t i = 0U; i < ARRAY_SIZE(ch->registered_bufs); i++) {
		ch->registered_bufs[i] = COE_BUFFER_IDX_INVALID;
		/* Any buffers which were not unregistered by userspace will
		 * be unmapped and released by destroying ch->buf_ctx next
		 */
	}

	if (ch->buf_ctx != NULL) {
		destroy_buffer_table(ch->buf_ctx);
		ch->buf_ctx = NULL;
	}

	if (ch->netdev) {
		put_device(&ch->netdev->dev);
		ch->netdev = NULL;
	}

	if (ch->parent) {
		if (ch->rx_pkt_hdrs != NULL) {
			dma_free_coherent(ch->parent->mgbe_dev,
					  ch->parent->rx_ring_size * COE_MAX_PKT_HEADER_SIZE,
					  ch->rx_pkt_hdrs, ch->rx_pkt_hdrs_dma_mgbe);
			ch->rx_pkt_hdrs = NULL;
		}

		if (ch->rx_desc_shdw != NULL) {
			dma_free_coherent(ch->parent->rtcpu_dev,
					  ch->parent->rx_ring_size * MGBE_RXDESC_SIZE,
					  ch->rx_desc_shdw, ch->rx_desc_shdw_dma_rce);
			ch->rx_desc_shdw = NULL;
		}

		coe_chan_rxring_release(ch);
		device_move(ch->dev, NULL, DPM_ORDER_NONE);
		set_bit(ch->dma_chan, ch->parent->dmachans_map);
		list_del(&ch->list_entry);
		ch->parent = NULL;
		ch->dma_chan = CAPTURE_COE_CHAN_INVALID_HW_ID;
	}

	ch->opened = false;
	mutex_unlock(&ch->channel_lock);

	return 0;
}

static int coe_fop_channel_release(
	struct inode *inode,
	struct file *file)
{
	struct coe_channel_state *ch = file->private_data;

	file->private_data = NULL;

	if (ch == NULL || ch->dev == NULL) {
		return 0;
	}

	dev_info(ch->dev, "%s\n", __func__);

	return coe_channel_close(ch);
}

static const struct file_operations coe_channel_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.unlocked_ioctl = coe_fop_channel_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = coe_fop_channel_ioctl,
#endif
	.open = coe_fop_channel_open,
	.release = coe_fop_channel_release,
};

static void coe_netdev_event_handle(struct coe_state * const s,
		unsigned long event, struct net_device *event_dev)
{
	dev_info(&s->pdev->dev, "netdev event %lu dev %s\n",
		event, netdev_name(event_dev));

	switch (event) {
	case NETDEV_UP:
		/* TODO can do sensor discovery here */
		break;
	case NETDEV_DOWN:
		break;
	default:
		break;
	}
}

static int rtcpu_coe_netdev_event(struct notifier_block *this,
			     unsigned long event, void *ptr)
{
	struct net_device *event_dev;
	struct coe_state * const s =
			container_of(this, struct coe_state, netdev_nb);

	if (ptr == NULL)
		return NOTIFY_DONE;

	event_dev = netdev_notifier_info_to_dev(ptr);
	if (event_dev == NULL)
		return NOTIFY_DONE;

	if (s->mgbe_dev == event_dev->dev.parent)
		coe_netdev_event_handle(s, event, event_dev);

	return NOTIFY_DONE;
}

static struct device *camrtc_coe_get_linked_device(
	const struct device *dev, char const *name, int index)
{
	struct device_node *np;
	struct platform_device *pdev;

	np = of_parse_phandle(dev->of_node, name, index);
	if (np == NULL)
		return NULL;

	pdev = of_find_device_by_node(np);
	of_node_put(np);

	if (pdev == NULL) {
		dev_warn(dev, "%s[%u] node has no device\n", name, index);
		return NULL;
	}

	return &pdev->dev;
}

static int coe_parse_dt_pdma_info(struct coe_state * const s)
{
	struct device_node *vm_node;
	struct device_node *temp;
	u32 num_of_pdma;
	int ret;
	unsigned int node = 0;

	vm_node = of_parse_phandle(s->mgbe_dev->of_node,
				   "nvidia,vm-vdma-config", 0);
	if (vm_node == NULL) {
		dev_err(&s->pdev->dev, "failed to found VDMA configuration\n");
		return -ENOMEM;
	}

	ret = of_property_read_u32(vm_node, "nvidia,pdma-num", &num_of_pdma);
	if (ret != 0) {
		dev_err(&s->pdev->dev, "failed to get number of PDMA (%d)\n",
			ret);
		dev_info(&s->pdev->dev, "Using number of PDMA as 1\n");
		num_of_pdma = 1U;
	}

	if (num_of_pdma > COE_MGBE_MAX_NUM_PDMA_CHANS) {
		dev_err(&s->pdev->dev, "Invalid Num. of PDMA's %u\n", num_of_pdma);
		return -EINVAL;
	}

	ret = of_get_child_count(vm_node);
	if (ret != (int)num_of_pdma) {
		dev_err(&s->pdev->dev,
			"Mismatch in num_of_pdma and VDMA config DT nodes\n");
		return ret;
	}

	for_each_child_of_node(vm_node, temp) {
		u32 pdma_chan;
		u32 num_vdma_chans;
		u32 vdma_chans[MAX_HW_CHANS_PER_DEVICE];

		if (node == num_of_pdma)
			break;

		ret = of_property_read_u32(temp, "nvidia,pdma-chan", &pdma_chan);
		if (ret != 0) {
			dev_err(&s->pdev->dev, "failed to read PDMA ID\n");
			return ret;
		}

		if (pdma_chan >= ARRAY_SIZE(s->pdmas)) {
			dev_err(&s->pdev->dev, "Invalid PDMA ID %u\n", pdma_chan);
			return -EINVAL;
		}

		ret = of_property_read_u32(temp, "nvidia,num-vdma-channels", &num_vdma_chans);
		if (ret != 0) {
			dev_err(&s->pdev->dev,
				"failed to read number of VDMA channels\n");
			return ret;
		}

		if (num_vdma_chans >= ARRAY_SIZE(vdma_chans)) {
			dev_err(&s->pdev->dev, "Invalid num of VDMAs %u\n", num_vdma_chans);
			return -EINVAL;
		}

		ret = of_property_read_u32_array(temp, "nvidia,vdma-channels",
						 vdma_chans, num_vdma_chans);
		if (ret != 0) {
			dev_err(&s->pdev->dev, "failed to get VDMA channels\n");
			return ret;
		}

		for (u32 i = 0U; i < num_vdma_chans; i++) {
			if (vdma_chans[i] >= ARRAY_SIZE(s->vdma2pdma_map)) {
				dev_err(&s->pdev->dev, "Bad VDMA ID %u\n", vdma_chans[i]);
				return -EINVAL;
			}

			s->vdma2pdma_map[vdma_chans[i]] = pdma_chan;
		}
	}

	return 0;
}

static int32_t coe_mgbe_parse_dt_dmachans(struct coe_state * const s,
					  u32 * const vm_chans,
					  size_t max_num_chans)
{
	struct device_node *vm_node;
	struct device_node *temp;
	u32 vm_irq_id = 0U;
	int ret = 0;
	u32 num_vm_chans;

	vm_node = of_parse_phandle(s->mgbe_dev->of_node,
				   "nvidia,vm-irq-config", 0);
	if (vm_node == NULL) {
		dev_err(&s->pdev->dev, "failed to found VM IRQ config\n");
		return -ENOMEM;
	}

	for_each_child_of_node(vm_node, temp) {
		bool isCoE;

		isCoE = of_property_read_bool(temp, "nvidia,camera-over-eth");
		if (!isCoE) {
			continue;
		}

		ret = of_property_read_u32(temp, "nvidia,vm-num", &vm_irq_id);
		if (ret != 0) {
			dev_err(&s->pdev->dev, "failed to read VM Number\n");
			break;
		}

		ret = of_property_read_u32(temp, "nvidia,num-vm-channels",
					&num_vm_chans);
		if (ret != 0) {
			dev_err(&s->pdev->dev,
				"failed to read number of VM channels\n");
			break;
		}

		if (num_vm_chans > max_num_chans) {
			dev_warn(&s->pdev->dev, "Too many CoE channels\n");
			ret = -E2BIG;
			break;
		}

		ret = of_property_read_u32_array(temp, "nvidia,vm-channels",
						 vm_chans, num_vm_chans);
		if (ret != 0) {
			dev_err(&s->pdev->dev, "failed to get VM channels\n");
			break;
		}

		s->mgbe_irq_id = vm_irq_id;
		ret = num_vm_chans;
		break;
	}

	return ret;
}

static void coe_destroy_channels(struct platform_device *pdev)
{
	struct coe_channel_state *ch;
	u32 ch_id;

	mutex_lock(&coe_channels_arr_lock);
	for (ch_id = 0U; ch_id < ARRAY_SIZE(coe_channels_arr); ch_id++) {
		ch = &coe_channels_arr[ch_id];

		/*
		 * Find all channel devices that are children of the platform
		 * device being removed and destroy them. This cleans up all
		 * channels, whether they were opened or not.
		 */
		if (ch->dev != NULL && ch->dev->parent == &pdev->dev) {
			coe_channel_close(ch);
			device_destroy(coe_channel_class, ch->devt);
			ch->dev = NULL;
			ch->devt = 0U;
		}
	}
	mutex_unlock(&coe_channels_arr_lock);
}

/**
 * Deallocate resources for all enabled Physical DMA channels
 * @s: CoE state
 */
static void coe_pdma_dealloc_resources(struct coe_state * const s)
{
	for (u32 pdma_id = 0U; pdma_id < ARRAY_SIZE(s->pdmas); pdma_id++) {
		struct coe_pdma_state * const pdma = &s->pdmas[pdma_id];
		const size_t ring_size =
			s->rx_pktinfo_ring_size * MGBE_PKTINFO_DESC_SIZE;

		if (pdma->rx_pktinfo == NULL)
			continue;

		coe_unmap_and_free_dma_buf(s,
					   ring_size,
					   pdma->rx_pktinfo,
					   pdma->rx_pktinfo_dma_rce,
					   &pdma->pktinfo_mgbe_sgt);
		pdma->rx_pktinfo = NULL;
	}
}

/**
 * Allocate resources for all enabled Physical DMA channels
 * @s: CoE state
 * @pdmachans_map: Bitmap indicating which PDMA channels of the device are active
 *
 * Returns: 0 on success, negative error code on failure
 */
static int coe_pdma_alloc_resources(struct coe_state * const s,
				    const unsigned long * const pdmachans_map)
{
	const size_t ring_size = s->rx_pktinfo_ring_size * MGBE_PKTINFO_DESC_SIZE;
	void *va;

	/* Initialize addresses for all enabled Physical DMA channels */
	for (u32 pdma_id = 0U; pdma_id < ARRAY_SIZE(s->pdmas); pdma_id++) {
		struct coe_pdma_state * const pdma = &s->pdmas[pdma_id];

		if (!test_bit(pdma_id, pdmachans_map))
			continue;

		va = coe_alloc_and_map_dma_buf(s,
					      ring_size,
					      &pdma->rx_pktinfo_dma_rce,
					      &pdma->pktinfo_mgbe_sgt);
		if (IS_ERR(va)) {
			dev_err(&s->pdev->dev, "Pktinfo alloc failed PDMA%u\n",
				pdma_id);
			return PTR_ERR(va);
		}
		pdma->rx_pktinfo = va;
	}

	return 0;
}

static int camrtc_coe_probe(struct platform_device *pdev)
{
	struct coe_state *s;
	struct device *dev = &pdev->dev;
	int ret;
	u32 dma_chans_arr[MAX_HW_CHANS_PER_DEVICE];
	int num_coe_channels;
	const struct coe_state *check_state;
	/* Bitmap indicating which PDMA channels of the device are used for camera */
	DECLARE_BITMAP(pdmachans_map, COE_MGBE_MAX_NUM_PDMA_CHANS);

	dev_dbg(dev, "tegra-camrtc-capture-coe probe\n");

	s = devm_kzalloc(dev, sizeof(*s), GFP_KERNEL);
	if (s == NULL)
		return -ENOMEM;

	s->rtcpu_dev = camrtc_coe_get_linked_device(dev,
			"nvidia,cam_controller", 0U);
	if (s->rtcpu_dev == NULL) {
		dev_err(dev, "No CoE controller found\n");
		return -ENOENT;
	}

	s->mgbe_dev = camrtc_coe_get_linked_device(dev,
					"nvidia,eth_controller", 0U);
	if (s->mgbe_dev == NULL) {
		ret = -ENOENT;
		goto err_put_rtcpu;
	}

	num_coe_channels = coe_mgbe_parse_dt_dmachans(s, dma_chans_arr,
						      ARRAY_SIZE(dma_chans_arr));
	if (num_coe_channels < 0) {
		ret = num_coe_channels;
		goto err_put_devices;
	}

	platform_set_drvdata(pdev, s);
	INIT_LIST_HEAD(&s->channels);
	INIT_LIST_HEAD(&s->device_entry);
	mutex_init(&s->access_lock);
	s->pdev = pdev;

	s->netdev_nb.notifier_call = rtcpu_coe_netdev_event;
	ret = register_netdevice_notifier(&s->netdev_nb);
	if (ret != 0) {
		dev_err(dev, "CoE failed to register notifier\n");
		goto err_put_devices;
	}

	/* TODO take from DT?  */
	s->rx_ring_size = 16384U;
	s->rx_pktinfo_ring_size = 4096U; /* Can only be 256, 512, 2048 or 4096 */

	ret = of_property_read_u32(s->mgbe_dev->of_node,
				   "nvidia,instance_id", &s->mgbe_id);
	if (ret != 0) {
		dev_info(dev,
			"DT instance_id missing, setting default to MGBE0\n");
		s->mgbe_id = 0U;
	}

	if (s->rx_ring_size > COE_MGBE_MAX_RXDESC_NUM) {
		dev_err(dev, "Invalid Rx ring size %u\n", s->rx_ring_size);
		ret = -ENOSPC;
		goto err_unregister_notifier;
	}

	if (s->rx_pktinfo_ring_size != 256U &&
	    s->rx_pktinfo_ring_size != 512U &&
	    s->rx_pktinfo_ring_size != 2048U &&
	    s->rx_pktinfo_ring_size != 4096U) {
		dev_err(dev, "Invalid pktinfo ring size %u\n", s->rx_pktinfo_ring_size);
		ret = -ENOSPC;
		goto err_unregister_notifier;
	}

	if (s->mgbe_id >= MAX_NUM_COE_DEVICES) {
		dev_err(dev, "Invalid MGBE ID %u\n", s->mgbe_id);
		ret = -EBADFD;
		goto err_unregister_notifier;
	}

	mutex_lock(&coe_device_list_lock);
	list_for_each_entry(check_state, &coe_device_list, device_entry) {
		if (s->mgbe_id == check_state->mgbe_id) {
			mutex_unlock(&coe_device_list_lock);
			dev_err(dev, "Device already exists for mgbe_id=%u\n",
				s->mgbe_id);
			ret = -EEXIST;
			goto err_unregister_notifier;
		}
	}
	list_add(&s->device_entry, &coe_device_list);
	mutex_unlock(&coe_device_list_lock);

	ret = coe_parse_dt_pdma_info(s);
	if (ret)
		goto err_del_from_list;

	for (u32 ch = 0U; ch < num_coe_channels; ch++) {
		u32 arr_idx;
		struct coe_channel_state *chan;

		mutex_lock(&coe_channels_arr_lock);

		chan = coe_channel_arr_find_free(&arr_idx);
		if (chan == NULL) {
			dev_err(dev, "No free channel slots ch=%u\n", ch);
			mutex_unlock(&coe_channels_arr_lock);
			ret = -ENOMEM;
			goto err_destroy_channels;
		}

		chan->devt = MKDEV(coe_channel_major, arr_idx);
		chan->dev = device_create(coe_channel_class, dev, chan->devt, NULL,
					"coe-chan-%u", arr_idx);
		if (IS_ERR(chan->dev)) {
			ret = PTR_ERR(chan->dev);
			chan->dev = NULL;
			mutex_unlock(&coe_channels_arr_lock);
			goto err_destroy_channels;
		}

		mutex_unlock(&coe_channels_arr_lock);

		INIT_LIST_HEAD(&chan->list_entry);
		mutex_init(&chan->rce_msg_lock);
		mutex_init(&chan->channel_lock);
		mutex_init(&chan->capq_inhw_lock);
		mutex_init(&chan->capq_appreport_lock);
		init_completion(&chan->rce_resp_ready);
		init_completion(&chan->capture_resp_ready);
		chan->rce_chan_id = CAPTURE_COE_CHANNEL_INVALID_ID;
		chan->pdma_id = COE_MGBE_PDMA_CHAN_INVALID;
		chan->dma_chan = CAPTURE_COE_CHAN_INVALID_HW_ID;

		set_bit(dma_chans_arr[ch], s->dmachans_map);
		set_bit(s->vdma2pdma_map[dma_chans_arr[ch]], pdmachans_map);

		dev_info(&s->pdev->dev, "Ch%u->PDMA%u\n",
			 dma_chans_arr[ch], s->vdma2pdma_map[dma_chans_arr[ch]]);
	}

	ret = coe_pdma_alloc_resources(s, pdmachans_map);
	if (ret)
		goto err_destroy_channels;

	dev_info(dev, "Camera Over Eth controller %s num_chans=%u IRQ=%u\n",
		 dev_name(s->mgbe_dev), num_coe_channels, s->mgbe_irq_id);

	return 0;

err_destroy_channels:
	coe_pdma_dealloc_resources(s);
	coe_destroy_channels(pdev);
err_del_from_list:
	mutex_lock(&coe_device_list_lock);
	list_del(&s->device_entry);
	mutex_unlock(&coe_device_list_lock);
err_unregister_notifier:
	unregister_netdevice_notifier(&s->netdev_nb);
err_put_devices:
	put_device(s->mgbe_dev);
err_put_rtcpu:
	put_device(s->rtcpu_dev);
	return ret;
}

static int camrtc_coe_remove(struct platform_device *pdev)
{
	struct coe_state * const s = platform_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "tegra-camrtc-capture-coe remove\n");

	unregister_netdevice_notifier(&s->netdev_nb);
	coe_destroy_channels(pdev);

	mutex_lock(&coe_device_list_lock);
	list_del(&s->device_entry);
	mutex_unlock(&coe_device_list_lock);

	coe_pdma_dealloc_resources(s);

	if (s->mgbe_dev != NULL) {
		put_device(s->mgbe_dev);
		s->mgbe_dev = NULL;
	}

	if (s->rtcpu_dev != NULL) {
		put_device(s->rtcpu_dev);
		s->rtcpu_dev = NULL;
	}

	return 0;
}

static const struct of_device_id camrtc_coe_of_match[] = {
	{ .compatible = "nvidia,tegra-camrtc-capture-coe" },
	{},
};
MODULE_DEVICE_TABLE(of, camrtc_coe_of_match);

static struct platform_driver capture_coe_driver = {
	.probe = camrtc_coe_probe,
	.remove = camrtc_coe_remove,
	.driver = {
		.name = "camrtc-coe",
		.owner = THIS_MODULE,
		.of_match_table = camrtc_coe_of_match,
	},
};

static int __init capture_coe_init(void)
{
	int err;

#if defined(NV_CLASS_CREATE_HAS_NO_OWNER_ARG) /* Linux v6.4 */
	coe_channel_class = class_create("capture-coe-channel");
#else
	coe_channel_class = class_create(THIS_MODULE, "capture-coe-channel");
#endif
	if (IS_ERR(coe_channel_class))
		return PTR_ERR(coe_channel_class);

	coe_channel_major = register_chrdev(0, "capture-coe-channel",
					    &coe_channel_fops);
	if (coe_channel_major < 0) {
		class_destroy(coe_channel_class);
		return coe_channel_major;
	}

	err = platform_driver_register(&capture_coe_driver);
	if (err) {
		unregister_chrdev(coe_channel_major, "capture-coe-channel");
		class_destroy(coe_channel_class);
		return err;
	}

	return 0;
}

static void __exit capture_coe_exit(void)
{
	platform_driver_unregister(&capture_coe_driver);

	/* Clean up any remaining channel devices in the global array */
	mutex_lock(&coe_channels_arr_lock);
	for (u32 ch_id = 0U; ch_id < ARRAY_SIZE(coe_channels_arr); ch_id++) {
		struct coe_channel_state * const ch = &coe_channels_arr[ch_id];

		if (ch->dev != NULL) {
			device_destroy(coe_channel_class, ch->devt);
			ch->dev = NULL;
			ch->devt = 0U;
		}
		/* Reset all channel state for clean module reload */
		memset(ch, 0, sizeof(*ch));
	}
	mutex_unlock(&coe_channels_arr_lock);

	unregister_chrdev(coe_channel_major, "capture-coe-channel");
	class_destroy(coe_channel_class);
}

module_init(capture_coe_init);
module_exit(capture_coe_exit);

MODULE_AUTHOR("Igor Mitsyanko <imitsyanko@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra Camera Over Ethernet driver");
MODULE_LICENSE("GPL v2");
