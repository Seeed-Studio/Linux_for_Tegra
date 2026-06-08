// SPDX-License-Identifier: MIT
/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <dce.h>
#include <dce-ipc.h>
#include <dce-os-utils.h>
#include <dce-os-trace.h>
#include <interface/dce-interface.h>
#include <interface/dce-ipc-header.h>

static struct dce_ipc_channel ivc_channels[DCE_IPC_CH_KMD_TYPE_MAX] = {
	[DCE_IPC_CH_KMD_TYPE_ADMIN] = {
		.flags = DCE_IPC_CHANNEL_VALID
			 | DCE_IPC_CHANNEL_MSG_HEADER,
		.ch_type = DCE_IPC_CH_KMD_TYPE_ADMIN,
		.ipc_type = DCE_IPC_TYPE_ADMIN,
		.signal = {
			.to_d = {
				.type = DCE_IPC_SIGNAL_MAILBOX,
				.sema_num = DCE_NUM_SEMA_REGS,
				.sema_bit = 0U,
				.form = {
					.mbox = {
						.mb_type = DCE_MAILBOX_ADMIN_INTERFACE,
						.mb_num = DCE_MBOX_TO_DCE_ADMIN,
					},
				},
				.signal = NULL,
				.next = NULL,
			},
			.from_d = {
				.type = DCE_IPC_SIGNAL_MAILBOX,
				.sema_num = DCE_NUM_SEMA_REGS,
				.sema_bit = 0U,
				.form = {
					.mbox = {
						.mb_type = DCE_MAILBOX_ADMIN_INTERFACE,
						.mb_num = DCE_MBOX_FROM_DCE_ADMIN,
					},
				},
				.signal = NULL,
				.next = NULL,
			},
		},
		.q_info = {
			.nframes = DCE_ADMIN_CMD_MAX_NFRAMES,
			.frame_sz = DCE_ADMIN_CMD_MAX_FSIZE,
		},
	},
	[DCE_IPC_CH_KMD_TYPE_RM] = {
		.flags = DCE_IPC_CHANNEL_VALID
			 | DCE_IPC_CHANNEL_MSG_HEADER,
		.ch_type = DCE_IPC_CH_KMD_TYPE_RM,
		.ipc_type = DCE_IPC_TYPE_DISPRM,
		.signal = {
			.to_d = {
				.type = DCE_IPC_SIGNAL_MAILBOX,
				.sema_num = DCE_NUM_SEMA_REGS,
				.sema_bit = 0U,
				.form = {
					.mbox = {
						.mb_type = DCE_MAILBOX_DISPRM_INTERFACE,
						.mb_num = DCE_MBOX_TO_DCE_RM,
					},
				},
				.signal = NULL,
				.next = NULL,
			},
			.from_d = {
				.type = DCE_IPC_SIGNAL_MAILBOX,
				.sema_num = DCE_NUM_SEMA_REGS,
				.sema_bit = 0U,
				.form = {
					.mbox = {
						.mb_type = DCE_MAILBOX_DISPRM_INTERFACE,
						.mb_num = DCE_MBOX_FROM_DCE_RM,
					},
				},
				.signal = NULL,
				.next = NULL,
			},
		},
		.q_info = {
			.nframes = DCE_DISPRM_CMD_MAX_NFRAMES,
			.frame_sz = DCE_DISPRM_CMD_MAX_FSIZE,
		},
	},
	[DCE_IPC_CH_KMD_TYPE_RM_NOTIFY] = {
		.flags = DCE_IPC_CHANNEL_VALID
			 | DCE_IPC_CHANNEL_MSG_HEADER,
		.ch_type = DCE_IPC_CH_KMD_TYPE_RM_NOTIFY,
		.ipc_type = DCE_IPC_TYPE_RM_NOTIFY,
		.signal = {
			.to_d = {
				.type = DCE_IPC_SIGNAL_MAILBOX,
				.sema_num = DCE_NUM_SEMA_REGS,
				.sema_bit = 0U,
				.form = {
					.mbox = {
						.mb_type = DCE_MAILBOX_DISPRM_NOTIFY_INTERFACE,
						.mb_num = DCE_MBOX_FROM_DCE_RM_EVENT_NOTIFY,
					},
				},
				.signal = NULL,
				.next = NULL,
			},
			.from_d = {
				.type = DCE_IPC_SIGNAL_MAILBOX,
				.sema_num = DCE_NUM_SEMA_REGS,
				.sema_bit = 0U,
				.form = {
					.mbox = {
						.mb_type = DCE_MAILBOX_DISPRM_NOTIFY_INTERFACE,
						.mb_num = DCE_MBOX_TO_DCE_RM_EVENT_NOTIFY,
					},
				},
				.signal = NULL,
				.next = NULL,
			},
		},
		.q_info = {
			.nframes = DCE_DISPRM_EVENT_NOTIFY_CMD_MAX_NFRAMES,
			.frame_sz = DCE_DISPRM_EVENT_NOTIFY_CMD_MAX_FSIZE,
		},
	},
};

static int _dce_ipc_wait(struct tegra_dce *d, u32 w_type, u32 ch_type)
{
	int ret = 0;
	struct dce_ipc_channel *ch;

	if (ch_type >= DCE_IPC_CH_KMD_TYPE_MAX) {
		dce_os_err(d, "Invalid Channel Type : [%d]", ch_type);
		return -EINVAL;
	}

	ch = d->d_ipc.ch[ch_type];
	if (ch == NULL) {
		dce_os_err(d, "Invalid Channel Data for type : [%d]", ch_type);
		ret = -EINVAL;
		goto out;
	}

	ch->w_type = w_type;

	dce_os_mutex_unlock(&ch->lock);

	if (ch_type == DCE_IPC_TYPE_ADMIN)
		ret = dce_admin_ipc_wait(d);
	else
		ret = dce_client_ipc_wait(d, ch_type);

	dce_os_mutex_lock(&ch->lock);

	ch->w_type = DCE_IPC_WAIT_TYPE_INVALID;

out:
	return ret;
}

u32 dce_ipc_get_cur_wait_type(struct tegra_dce *d, u32 ch_type)
{
	uint32_t w_type;
	struct dce_ipc_channel *ch;

	if (ch_type >= DCE_IPC_CH_KMD_TYPE_MAX) {
		dce_os_err(d, "Invalid Channel Type : [%d]", ch_type);
		return -EINVAL;
	}

	ch = d->d_ipc.ch[ch_type];
	if (ch == NULL) {
		dce_os_err(d, "Invalid Channel Data for type : [%d]", ch_type);
		return -EINVAL;
	}

	dce_os_mutex_lock(&ch->lock);

	w_type = ch->w_type;

	dce_os_mutex_unlock(&ch->lock);

	return w_type;
}

/**
 * dce_ipc_channel_init_unlocked - Initializes the underlying IPC channel to
 *				be used for all bi-directional messaging.
 * @d : Pointer to struct tegra_dce.
 * @type : Type of interface for which this channel is needed.
 *
 * Note: This function is not thread safe and should be called only once
 *       during initialization.
 *
 * Return : 0 if successful.
 */
int dce_ipc_channel_init_unlocked(struct tegra_dce *d, u32 ch_type)
{
	u32 q_sz;
	u32 msg_sz;
	int ret = 0;
	struct dce_ipc_region *r;
	struct dce_ipc_channel *ch;
	struct dce_ipc_queue_info *q_info;

	if (ch_type >= DCE_IPC_CH_KMD_TYPE_MAX) {
		dce_os_err(d, "Invalid ivc channel ch_type : [%d]", ch_type);
		ret = -EINVAL;
		goto out;
	}

	ch = &ivc_channels[ch_type];
	if (!ch) {
		dce_os_err(d, "Invalid ivc channel for this ch_type : [%d]",
			ch_type);
		ret = -ENOMEM;
		goto out;
	}

	ret = dce_os_mutex_init(&ch->lock);
	if (ret) {
		dce_os_err(d, "dce lock initialization failed for mailbox");
		goto out;
	}

	if ((ch->flags & DCE_IPC_CHANNEL_VALID) == 0U) {
		dce_os_info(d, "Invalid Channel State [0x%x] for ch_type [%d]",
		ch->flags, ch_type);
		goto out_lock_destroy;
	}

	ch->d = d;

	ret = dce_ipc_init_signaling(d, ch);
	if (ret) {
		dce_os_err(d, "Signaling init failed");
		goto out_lock_destroy;
	}

	q_info = &ch->q_info;
	msg_sz = dce_os_ivc_align(q_info->frame_sz);
	q_sz = dce_os_ivc_total_queue_size(msg_sz * q_info->nframes);

	r = &d->d_ipc.region;
	if (!r->base) {
		ret = -ENOMEM;
		goto out_lock_destroy;
	}

	ret = dce_os_ivc_init(&ch->d_ivc,
			(char *)r->base + r->s_offset, (char *)r->base + r->s_offset + q_sz,
			r->iova + r->s_offset, r->iova + r->s_offset + q_sz,
			q_info->nframes, msg_sz);
	if (ret) {
		dce_os_err(d, "IVC creation failed");
		goto out_lock_destroy;
	}

	ch->flags |= DCE_IPC_CHANNEL_INITIALIZED;

	q_info->rx_iova = r->iova + r->s_offset;
	q_info->tx_iova = r->iova + r->s_offset + q_sz;

	dce_os_trace_ivc_channel_init_complete(d, ch);

	d->d_ipc.ch[ch_type] = ch;
	r->s_offset += (2 * q_sz);

out_lock_destroy:
	if (ret)
		dce_os_mutex_destroy(&ch->lock);
out:
	return ret;
}

/**
 * dce_ipc_channel_deinit_unlocked - Releases resources for a ivc channel
 *
 * @d : Pointer to tegra_dce struct.
 * @id : Channel Id.
 *
 * Note: This function is not thread safe and should be called only once
 *       during de-initialization.
 */
void dce_ipc_channel_deinit_unlocked(struct tegra_dce *d, u32 ch_type)
{
	struct dce_ipc_channel *ch = d->d_ipc.ch[ch_type];

	if (ch == NULL || (ch->flags & DCE_IPC_CHANNEL_INITIALIZED) == 0U) {
		dce_os_info(d, "Invalid IVC Channel [%d]", ch_type);
		return;
	}

	dce_os_mutex_lock(&ch->lock);

	dce_ipc_deinit_signaling(d, ch);

	ch->flags &= ~DCE_IPC_CHANNEL_INITIALIZED;
	ch->flags &= ~DCE_IPC_CHANNEL_SYNCED;

	d->d_ipc.ch[ch_type] = NULL;

	dce_os_mutex_unlock(&ch->lock);

	dce_os_mutex_destroy(&ch->lock);

}

/**
 * dce_ipc_get_dce_from_ch_unlocked - Get DCE struct from IPC ch type.
 *
 * @ch_type : DCE IPC channel type.
 *
 * Return : Pointer to tegra dce struct.
 *
 * Note: We do not need to acquire a channel lock in this function
 * as it only retrieves the tegra_dce struct for the channel.
 * This tegra_dce struct member is initialized and uninitialized in
 * dce_ipc_channel_init_unlocked() and dce_ipc_channel_deinit_unlocked().
 */
struct tegra_dce *dce_ipc_get_dce_from_ch_unlocked(u32 ch_type)
{
	struct tegra_dce *d = NULL;
	struct dce_ipc_channel *ch = NULL;

	if (ch_type >= DCE_IPC_CH_KMD_TYPE_MAX)
		goto out;

	ch = &ivc_channels[ch_type];

	d = ch->d;

out:
	return d;
}

/**
 * dce_ipc_channel_ready - Checks if channel is ready to use
 *
 * @d : Pointer to tegra_dce struct.
 * @id : Channel Id.
 *
 * Return : true if channel ready to use.
 */
bool dce_ipc_channel_is_ready(struct tegra_dce *d, u32 ch_type)
{
	bool is_est;

	struct dce_ipc_channel *ch = d->d_ipc.ch[ch_type];

	dce_os_mutex_lock(&ch->lock);

	is_est = (dce_os_ivc_notified(&ch->d_ivc) ? false : true);

	ch->signal.notify(d, &ch->signal.to_d);

	dce_os_mutex_unlock(&ch->lock);

	return is_est;
}

/**
 * dce_ipc_channel_synced - Checks if channel is in synced state
 *
 * @d : Pointer to tegra_dce struct.
 * @id : Channel Id.
 *
 * Return : true if channel is in synced state.
 */
bool dce_ipc_channel_is_synced(struct tegra_dce *d, u32 ch_type)
{
	bool ret;

	struct dce_ipc_channel *ch = d->d_ipc.ch[ch_type];

	dce_os_mutex_lock(&ch->lock);

	ret = (ch->flags & DCE_IPC_CHANNEL_SYNCED) ? true : false;

	dce_os_mutex_unlock(&ch->lock);

	return ret;
}

/**
 * dce_ipc_channel_reset - Resets the channel and completes
 *				the handshake with the remote.
 *
 * @d : Pointer to tegra_dce struct.
 * @id : Channel Id.
 *
 * Return : void
 */
void dce_ipc_channel_reset(struct tegra_dce *d, u32 ch_type)
{
	struct dce_ipc_channel *ch = d->d_ipc.ch[ch_type];

	dce_os_mutex_lock(&ch->lock);

	dce_os_ivc_reset(&ch->d_ivc);

	dce_os_trace_ivc_channel_reset_triggered(d, ch);

	ch->flags &= ~DCE_IPC_CHANNEL_SYNCED;

	ch->signal.notify(d, &ch->signal.to_d);

	dce_os_mutex_unlock(&ch->lock);

	do {
		if (dce_ipc_channel_is_ready(d, ch_type) == true)
			break;
		dce_os_usleep_range(10, 20);
	} while (true);

	dce_os_mutex_lock(&ch->lock);

	ch->flags |= DCE_IPC_CHANNEL_SYNCED;

	dce_os_trace_ivc_channel_reset_complete(d, ch);

	dce_os_mutex_unlock(&ch->lock);
}

/**
 * dce_ipc_send_message - Sends messages over ipc.
 *
 * @d : Pointer to tegra_dce struct.
 * @id : Channel Id.
 * @data : Pointer to the data to be written.
 * @size : Size of the data to be written.
 *
 * Return : 0 if successful.
 */
int dce_ipc_send_message(struct tegra_dce *d, u32 ch_type,
		const void *data, size_t size)
{
	int ret = 0;
	struct dce_ipc_channel *ch
		= d->d_ipc.ch[ch_type];

	dce_os_mutex_lock(&ch->lock);

	dce_os_trace_ivc_send_req_received(d, ch);

	ret = dce_os_ivc_get_next_write_frame(&ch->d_ivc, &ch->obuff);
	if (ret) {
		dce_os_err(ch->d, "Error getting next free buf to write");
		goto out;
	}

	ret = dce_os_ivc_write_channel(ch, data, size);
	if (ret) {
		dce_os_err(ch->d, "Error writing to channel");
		goto out;
	}

	ch->signal.notify(d, &ch->signal.to_d);

	dce_os_trace_ivc_send_complete(d, ch);

out:
	dce_os_mutex_unlock(&ch->lock);

	return ret;
}

/**
 * dce_ipc_read_message - Reads messages over ipc.
 *
 * @d : Pointer to tegra_dce struct.
 * @id : Channel Id.
 * @data : Pointer to the data to be read.
 * @size : Size of the data to be read.
 *
 * Return : 0 if successful.
 */
int dce_ipc_read_message(struct tegra_dce *d, u32 ch_type,
		void *data, size_t size)
{
	int ret = 0;
	struct dce_ipc_channel *ch = d->d_ipc.ch[ch_type];

	dce_os_mutex_lock(&ch->lock);

	dce_os_trace_ivc_receive_req_received(d, ch);

	ret = dce_os_ivc_get_next_read_frame(&ch->d_ivc, &ch->ibuff);
	if (ret) {
		dce_os_debug(ch->d, "No Msg to read");
		goto out;
	}

	ret = dce_os_ivc_read_channel(ch, data, size);
	if (ret) {
		dce_os_err(ch->d, "Error reading from channel");
		goto out;
	}

	dce_os_trace_ivc_receive_req_complete(d, ch);

out:
	dce_os_mutex_unlock(&ch->lock);
	return ret;
}

/**
 * dce_ipc_send_message_sync - Sends messages on a channel
 *				synchronously and waits for an ack.
 *
 * @d : Pointer to tegra_dce struct.
 * @id : Channel Id.
 * @msg : Pointer to the message to be sent/received.
 *
 * Return : 0 if successful
 */
int dce_ipc_send_message_sync(struct tegra_dce *d, u32 ch_type,
				struct dce_ipc_message *msg)
{
	int ret = 0;
	struct dce_ipc_channel *ch = d->d_ipc.ch[ch_type];

	ret = dce_ipc_send_message(d, ch_type, msg->tx.data, msg->tx.size);
	if (ret) {
		dce_os_err(ch->d, "Error in sending message to DCE");
		goto done;
	}

	dce_os_mutex_lock(&ch->lock);
	ret = _dce_ipc_wait(ch->d, DCE_IPC_WAIT_TYPE_RPC, ch_type);
	dce_os_mutex_unlock(&ch->lock);
	if (ret) {
		dce_os_err(ch->d, "Error in waiting for ack");
		goto done;
	}

	dce_os_trace_ivc_wait_complete(d, ch);

	ret = dce_ipc_read_message(d, ch_type, msg->rx.data, msg->rx.size);
	if (ret) {
		dce_os_err(ch->d, "Error in reading DCE msg for ch_type [%d]",
			ch_type);
		goto done;
	}
done:
	return ret;
}

/**
 * dce_ipc_get_channel_info - Provides information about frames details
 *
 * @d : Pointer to tegra_dce struct.
 * @ch_index : Channel Index.
 * @q_info : Pointer to struct dce_ipc_queue_info
 *
 * Return : 0 if successful
 */
int dce_ipc_get_channel_info(struct tegra_dce *d,
		struct dce_ipc_queue_info *q_info, u32 ch_index)
{
	struct dce_ipc_channel *ch = d->d_ipc.ch[ch_index];

	if (ch == NULL)
		return -ENOMEM;

	dce_os_mutex_lock(&ch->lock);

	memcpy(q_info, &ch->q_info, sizeof(ch->q_info));

	dce_os_mutex_unlock(&ch->lock);

	return 0;
}

/**
 * dce_ipc_get_region_iova_info - Provides iova details for ipc region
 *
 * @d : Pointer to tegra_dce struct.
 * @iova : Iova start address.
 * @size : Iova size
 *
 * Return : 0 if successful
 */
int dce_ipc_get_region_iova_info(struct tegra_dce *d, u64 *iova, u32 *size)
{
	struct dce_ipc_region *r = &d->d_ipc.region;

	if (!r->base)
		return -ENOMEM;

	*iova = r->iova;
	*size = r->size;

	return 0;
}

/*
 * dce_ipc_is_data_available - Check if IVC channel has new data
 *				avaialble for reading.
 *
 * @d : Pointer to tegra_dce struct
 * @id : Channel Index
 *
 * Return : true if the worker thread needs to wake up
 */
bool dce_ipc_is_data_available(struct tegra_dce *d, u32 ch_type)
{
	bool ret = false;
	struct dce_ipc_channel *ch = d->d_ipc.ch[ch_type];

	dce_os_mutex_lock(&ch->lock);

	ret = dce_os_ivc_is_data_available(ch);

	dce_os_mutex_unlock(&ch->lock);

	return ret;
}

/*
 * dce_ipc_get_ipc_type - Returns the ipc_type for the channel.
 *
 * @d : Pointer to tegra_dce struct
 * @id : Channel Index
 *
 * Return : True if the worker thread needs to wake up
 */
uint32_t dce_ipc_get_ipc_type(struct tegra_dce *d, u32 ch_type)
{
	uint32_t ipc_type;
	struct dce_ipc_channel *ch = d->d_ipc.ch[ch_type];

	dce_os_mutex_lock(&ch->lock);

	ipc_type = ch->ipc_type;

	dce_os_mutex_unlock(&ch->lock);

	return ipc_type;
}
