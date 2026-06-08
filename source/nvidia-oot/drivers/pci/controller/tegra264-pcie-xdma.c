// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025, NVIDIA CORPORATION. All rights reserved.

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/iopoll.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/tegra-pcie-dma.h>

#include "tegra-pcie-dma-irq.h"
#include "tegra264-pcie-xdma-osi.h"

/** Default number of descriptors used */
#define NUM_XDMA_DESC	4096

/** Calculates timeout based on size.
 *  Time in nano sec = size in bytes / (1000000 * 2).
 *    2Gbps is max speed for Gen 1 with 2.5GT/s at 8/10 encoding.
 *  Convert to milli seconds and add 1sec timeout
 */
#define GET_SYNC_TIMEOUT(s)	((((s) * 8UL) / 2000000) + 1000)

#define INCR_DESC(idx, i) ((idx) = ((idx) + (i)) % (ch->desc_sz))

struct xdma_chan {
	void *desc;
	void __iomem *remap_desc;
	struct tegra_pcie_dma_xfer_info *ring;
	dma_addr_t dma_iova;
	uint32_t desc_sz;
	/* descriptor size that is allocated for a channel */
	u64 xdma_desc_size;
	/** Index from where cleanup needs to be done */
	volatile uint32_t r_idx;
	/** Index from where descriptor update is needed */
	volatile uint32_t w_idx;
	struct mutex lock;
	wait_queue_head_t wq;
	tegra_pcie_dma_chan_type_t type;
	bool busy;
	/** This field is updated to abort or de-init to stop further xfer submits */
	tegra_pcie_dma_status_t st;
};

struct xdma_prv {
	u32 irq;
	char *irq_name;
	bool is_remote_dma;
	/** XDMA base address */
	void __iomem *xdma_base;
	/** XDMA base address size */
	uint32_t xdma_base_size;
	struct device *dev;
	struct device *pdev;
	struct xdma_chan tx[TEGRA_PCIE_DMA_WR_CHNL_NUM];
	struct xdma_chan rx[TEGRA_PCIE_DMA_RD_CHNL_NUM];
	/* BIT(0) - Write initialized, BIT(1) - Read initialized */
	uint32_t ch_init;
};

static inline void xdma_ll_ch_init(void __iomem *xdma_base, uint8_t ch, dma_addr_t ll_phy_addr,
				   bool rw_type, bool is_remote_dma)
{
	uint32_t val;

	val = XDMA_CHANNEL_CTRL_EN;
	if (rw_type)
		val |= XDMA_CHANNEL_CTRL_DMA_OPERATION;
	else
		val &= ~XDMA_CHANNEL_CTRL_DMA_OPERATION;
	/* All local channels mapped to same MSI and all remote mapped to same MSI */
	val |= (is_remote_dma << XDMA_CHANNEL_CTRL_MSI_CHANNEL_SHIFT);
	xdma_channel_wr(xdma_base, ch, val, XDMA_CHANNEL_CTRL);
}

static inline int xdma_ch_init(struct xdma_prv *prv, struct xdma_chan *ch, uint8_t c)
{
	if ((ch->desc_sz <= 1) || (ch->desc_sz & (ch->desc_sz - 1)))
		return -EINVAL;

	if (prv->is_remote_dma)
		memset_io(ch->remap_desc, 0, ch->xdma_desc_size);
	else
		memset(ch->desc, 0, ch->xdma_desc_size);

	ch->w_idx = 0;
	ch->r_idx = 0;
	ch->st = TEGRA_PCIE_DMA_SUCCESS;

	if (!ch->ring) {
		ch->ring = kcalloc(ch->desc_sz, sizeof(*ch->ring), GFP_KERNEL);
		if (!ch->ring)
			return -ENOMEM;
	}

	xdma_channel_wr(prv->xdma_base, c, ch->desc_sz, XDMA_CHANNEL_DESCRIPTOR_LIST_SIZE);

	xdma_channel_wr(prv->xdma_base, c, lower_32_bits(ch->dma_iova),
			XDMA_CHANNEL_DESCRIPTOR_LIST_POINTER_LOW);
	xdma_channel_wr(prv->xdma_base, c, upper_32_bits(ch->dma_iova),
			XDMA_CHANNEL_DESCRIPTOR_LIST_POINTER_HIGH);

	if (prv->is_remote_dma)
		xdma_channel_wr(prv->xdma_base, prv->is_remote_dma,
				XDMA_MSI_CHANNEL_CFG_INTR_DESTINATION, XDMA_MSI_CHANNEL_CFG_INTR);
	else
		xdma_channel_wr(prv->xdma_base, prv->is_remote_dma, 0, XDMA_MSI_CHANNEL_CFG_INTR);

	return 0;
}

static inline void xdma_hw_deinit(void *cookie, u32 ch)
{
	struct xdma_prv *prv = (struct xdma_prv *)cookie;
	int err;
	u32 val = 0, debug_reg_b4_poll;

	val = xdma_channel_rd(prv->xdma_base, ch, XDMA_CHANNEL_CTRL);
	val &= ~XDMA_CHANNEL_CTRL_EN;
	xdma_channel_wr(prv->xdma_base, ch, val, XDMA_CHANNEL_CTRL);

	err = readl_poll_timeout_atomic(prv->xdma_base + XDMA_CHANNEL_CTRL + (0x4 * ch), val,
			!(val & XDMA_CHANNEL_CTRL_EN), 1, 10000);
	if (err)
		dev_err(prv->dev, "failed to reset dma channel: %d st: 0x%x\n", ch, val);

	/* Store the value before polling to print post polling status for fail case */
	debug_reg_b4_poll = xdma_channel_rd(prv->xdma_base, ch, XDMA_CHANNEL_DEBUG_REGISTER_4);

	err = readl_poll_timeout_atomic(prv->xdma_base + XDMA_CHANNEL_DEBUG_REGISTER_4, val,
			(val & (XDMA_CHANNEL_DEBUG_REGISTER_4_INTR_ENGINE_MSI_CHAN_ISR_INPROG_FSM |
				XDMA_CHANNEL_DEBUG_REGISTER_4_INTR_ENGINE_MSI_CHAN_MSI_DISP_FSM |
				XDMA_CHANNEL_DEBUG_REGISTER_4_INTR_ENGINE_MSI_CHAN_INTR_MOD_FSM |
				XDMA_CHANNEL_DEBUG_REGISTER_4_INTR_ENGINE_MSI_CHAN_MSI_DISP_SHADOW_GEN_STATUS_MSI |
				XDMA_CHANNEL_DEBUG_REGISTER_4_INTR_ENGINE_MSI_CHAN_MSI_DISP_SHADOW_FUNC_ERR_DETECTED)) == 0, 1, 10000);
	if (err)
		dev_err(prv->dev, "failed to reset msi channel:%d. B4 poll st:0x%x Post poll st:0x%x\n",
			ch, debug_reg_b4_poll, val);
}

/** From OSI */
static inline u32 get_dma_idx_from_llp(struct xdma_prv *prv, u8 chan, struct xdma_chan *ch,
				       u32 type)
{
	u32 cur_idx;
	u64 cpl_addr;

	cur_idx = xdma_channel_rd(prv->xdma_base, chan,
				  XDMA_CHANNEL_STATUS_LAST_DESC_COMPLETED_INDEX);
	cur_idx &= XDMA_CHANNEL_STATUS_LAST_DESC_COMPLETED_INDEX_VALUE;

	cpl_addr = xdma_channel_rd(prv->xdma_base, chan,
				   XDMA_CHANNEL_STATUS_LAST_DESC_COMPLETED_ADDRESS_HI);
	cpl_addr <<= 32;
	cpl_addr |= xdma_channel_rd(prv->xdma_base, chan,
				    XDMA_CHANNEL_STATUS_LAST_DESC_COMPLETED_ADDRESS_LO);
	if (cpl_addr != 0x0)
		cur_idx++;

	return cur_idx % (ch->desc_sz);
}

static inline void process_r_idx(struct xdma_chan *ch, tegra_pcie_dma_status_t st, u32 idx)
{
	u32 count = 0;
	struct xdma_hw_desc *dma_ll_virt;
	struct tegra_pcie_dma_xfer_info *ring;

	while ((ch->r_idx != idx) && (count < ch->desc_sz)) {
		count++;
		ring = &ch->ring[ch->r_idx];
		dma_ll_virt = (struct xdma_hw_desc *)ch->desc + ch->r_idx;
		INCR_DESC(ch->r_idx, 1);
		/* clear wm if any set */
		dma_ll_virt->attr_reg.ctrl_e.wm = 0;
		if (ch->type == TEGRA_PCIE_DMA_CHAN_XFER_ASYNC && ring->complete) {
			ring->complete(ring->priv, st);
			/* Clear ring callback and priv variables */
			ring->complete = NULL;
			ring->priv = NULL;
		}
	}
}

static inline void process_ch_irq(struct xdma_prv *prv, u32 chan, struct xdma_chan *ch, u32 type)
{
	u32 idx;

	idx = get_dma_idx_from_llp(prv, (u8)(chan & 0xFF), ch, type);

	if (ch->type == TEGRA_PCIE_DMA_CHAN_XFER_SYNC) {
		if (ch->busy) {
			ch->busy = false;
			wake_up(&ch->wq);
		} else {
			dev_info(prv->dev, "SYNC mode with chan %d busy not set r_idx %d, cur_idx %d, w_idx is %d\n",
				 chan, ch->r_idx, idx, ch->w_idx);
		}
	}

	if (ch->st == TEGRA_PCIE_DMA_ABORT) {
		dev_info(prv->dev, "Abort: ch %d at r_idx %d->idx %d, w_idx is %d\n", chan,
			 ch->r_idx, idx, ch->w_idx);
		if (ch->r_idx == idx)
			goto process_abort;
	}

	process_r_idx(ch, TEGRA_PCIE_DMA_SUCCESS, idx);

process_abort:
	if (ch->st == TEGRA_PCIE_DMA_ABORT)
		process_r_idx(ch, TEGRA_PCIE_DMA_ABORT, ch->w_idx);
}

irqreturn_t xdma_irq(int irq, void *cookie)
{
	/* Disable irq before wake thread handler */
	disable_irq_nosync((u32)(irq & INT_MAX));

	return IRQ_WAKE_THREAD;
}

static void tegra_pcie_xdma_reset_channel(struct xdma_prv *prv, struct xdma_chan *ch, int bit, u32 i)
{
	u32 mode_cnt[2] = {TEGRA_PCIE_DMA_WR_CHNL_NUM, TEGRA_PCIE_DMA_RD_CHNL_NUM};
	int channel_num = bit + (i * mode_cnt[0]);

	ch->st = TEGRA_PCIE_DMA_ABORT;
	xdma_hw_deinit(prv, channel_num);

	/* wait until existing xfer submit completed */
	mutex_lock(&ch->lock);
	mutex_unlock(&ch->lock);

	process_ch_irq(prv, channel_num, ch, i);

	xdma_ch_init(prv, ch, channel_num);

	xdma_ll_ch_init(prv->xdma_base, channel_num,
			ch->dma_iova, (i == 0), prv->is_remote_dma);
}

irqreturn_t xdma_irq_handler(int irq, void *cookie)
{
	struct xdma_prv *prv = (struct xdma_prv *)cookie;
	int bit = 0, err;
	u32 val, debug_reg, i = 0;
	struct xdma_chan *chan[2] = {&prv->tx[0], &prv->rx[0]};
	struct xdma_chan *ch;
	u32 mode_cnt[2] = {TEGRA_PCIE_DMA_WR_CHNL_NUM, TEGRA_PCIE_DMA_RD_CHNL_NUM};

	/*
	 * In corner case, unexpected interrupt maybe possible when dma channel is
	 * resetting, set star_processing & end_processing irrespective of
	 * new_xfer_valid
	 */
	val = xdma_channel_rd(prv->xdma_base, prv->is_remote_dma,
			      XDMA_MSI_CHANNEL_PRIMARY_INTR_STATUS);
	val |= XDMA_MSI_CHANNEL_PRIMARY_INTR_STATUS_INTR_START_PROCESSING;
	xdma_channel_wr(prv->xdma_base, prv->is_remote_dma, val,
			XDMA_MSI_CHANNEL_PRIMARY_INTR_STATUS);

	for (i = 0; i < 2; i++) {
		if (!(prv->ch_init & OSI_BIT(i)))
			continue;

		for (bit = 0; bit < mode_cnt[i]; bit++) {
			u32 xfer_valid = val & (1 << (16 + bit + (i * mode_cnt[0])));
			u32 err_status = val & (1 << (8 + bit + (i * mode_cnt[0])));

			ch = chan[i] + bit;

			/* Ignore if no error or xfer valid set for the channel. */
			if (!xfer_valid && !err_status)
				continue;

			if (err_status) {
				u32 temp;

				temp = xdma_channel_rd(prv->xdma_base, bit + (i * mode_cnt[0]),
						       XDMA_CHANNEL_FUNC_ERROR_STATUS);

				dev_info(prv->dev, "MSI error %x seen for channel %d for mode %d\n",
					 temp, bit, i);

				tegra_pcie_xdma_reset_channel(prv, ch, bit, i);
			/*
			 * If both xfer_valid and err_status are set, error recovery process
			 * channel(process_ch_irq()), so we can skip else part when both xfer_valid
			 * and err_status are set.
			 */
			} else {
				process_ch_irq(prv, bit + (i * mode_cnt[0]), ch, i);
			}

			/* Polling for the MSI_DISP_FSM to be set to 0 */
			err = readl_poll_timeout_atomic(prv->xdma_base + XDMA_CHANNEL_DEBUG_REGISTER_4, debug_reg,
					(debug_reg & XDMA_CHANNEL_DEBUG_REGISTER_4_INTR_ENGINE_MSI_CHAN_MSI_DISP_FSM) == 0, 1, 100);
			if (err) {
				dev_err(prv->dev, "MSI ack is not received on channel: 0 st: 0x%x\n", debug_reg);
				tegra_pcie_xdma_reset_channel(prv, ch, bit, i);
			}
		}
	}

	val = xdma_channel_rd(prv->xdma_base, prv->is_remote_dma,
			      XDMA_MSI_CHANNEL_PRIMARY_INTR_STATUS);
	val |= XDMA_MSI_CHANNEL_PRIMARY_INTR_STATUS_INTR_END_PROCESSING;
	xdma_channel_wr(prv->xdma_base, prv->is_remote_dma, val,
			XDMA_MSI_CHANNEL_PRIMARY_INTR_STATUS);

	/* Must enable before exit */
	enable_irq((u32)(irq & INT_MAX));
	return IRQ_HANDLED;
}

void *tegra264_pcie_xdma_initialize(struct tegra_pcie_dma_init_info *info, void *priv_dma)
{
	struct xdma_prv *prv;
	struct resource *dma_res;
	int32_t ret;
	int32_t i, j;
	struct xdma_chan *ch = NULL;
	struct xdma_chan *chan[2];
	u32 mode_cnt[2] = {TEGRA_PCIE_DMA_WR_CHNL_NUM, TEGRA_PCIE_DMA_RD_CHNL_NUM};
	struct tegra_pcie_dma_chans_info *chan_info[2];
	struct tegra_pcie_dma_chans_info *ch_info;
	struct platform_device *pdev;
	struct device_node *np;
	struct pci_host_bridge *bridge;
	struct pci_bus *bus;
	struct pci_dev *pci_dev;
	struct device *dev;

	prv = kzalloc(sizeof(*prv), GFP_KERNEL);
	if (!prv)
		return NULL;

	chan[0] = &prv->tx[0];
	chan[1] = &prv->rx[0];
	chan_info[0] = &info->tx[0];
	chan_info[1] = &info->rx[0];

	if (!info->dev) {
		pr_err("%s: dev pointer is NULL\n", __func__);
		goto free_priv;
	}
	prv->dev = info->dev;

	if (info->remote != NULL) {
		if (info->msi_irq > INT_MAX) {
			pr_err("%s: msi_irq is out of range\n", __func__);
			goto free_priv;
		}
		prv->irq = (int)info->msi_irq;
		prv->is_remote_dma = true;

		prv->xdma_base = devm_ioremap(prv->dev, info->remote->dma_phy_base,
					      info->remote->dma_size);
		if (IS_ERR(prv->xdma_base)) {
			dev_err(prv->dev, "dma region map failed.\n");
			goto free_priv;
		}
	} else if (prv->dev != NULL) {
		prv->is_remote_dma = false;

		if (dev_is_pci(prv->dev)) {
			pci_dev = to_pci_dev(prv->dev);
			bus = pci_dev->bus;
			if (bus == NULL) {
				dev_err(prv->dev, "PCI bus is NULL\n");
				goto free_priv;
			}
			bridge = pci_find_host_bridge(bus);
			dev = &bridge->dev;
			if (dev->parent == NULL) {
				dev_err(prv->dev, "PCI bridge parent is NULL\n");
				goto free_priv;
			}
			np = dev->parent->of_node;
		} else {
			np = prv->dev->of_node;
		}

		if (np == NULL) {
			dev_err(prv->dev, "Device of_node is NULL\n");
			goto free_priv;
		}

		pdev = of_find_device_by_node(np);
		if (!pdev) {
			pr_err("Unable to retrieve pdev node\n");
			goto free_priv;
		}
		prv->dev = &pdev->dev;
		prv->pdev = info->dev;

		dma_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "xdma");
		if (!dma_res) {
			dev_err(prv->dev, "missing xdma resource in DT\n");
			goto put_dev;
		}

		prv->xdma_base = devm_ioremap(prv->dev, dma_res->start, resource_size(dma_res));
		if (IS_ERR(prv->xdma_base)) {
			dev_err(prv->dev, "dma region map failed.\n");
			goto put_dev;
		}
		prv->irq = (int)info->msi_irq;
	} else {
		pr_err("Neither device node nor xdma remote available");
		goto free_priv;
	}

	for (j = 0; j < 2; j++) {
		for (i = 0; i < mode_cnt[j]; i++) {
			ch_info = chan_info[j] + i;
			ch = chan[j] + i;

			if (ch_info->num_descriptors == 0)
				continue;

			if (ch_info->num_descriptors > SZ_32K) {
				dev_err(prv->dev, "desc size cannot be greater than SZ_32K, ch: %d type: %d cur size: %ux\n",
					i, j, ch_info->num_descriptors);
				goto dma_iounmap;
			}

			ch->type = ch_info->ch_type;
			ch->desc_sz = ch_info->num_descriptors;
			ch->xdma_desc_size = sizeof(struct xdma_hw_desc) * ch->desc_sz;

			if (prv->is_remote_dma) {
				ch->dma_iova = ch_info->desc_iova;
				ch->remap_desc = devm_ioremap(prv->dev, ch_info->desc_phy_base,
							      ch->xdma_desc_size);
				ch->desc = (__force void *)ch->remap_desc;
				if (!ch->desc) {
					dev_err(prv->dev, "desc region map failed, phy: 0x%llx\n",
						ch_info->desc_phy_base);
					goto dma_iounmap;
				}
			} else {
				ch->desc = dma_alloc_coherent(prv->pdev, ch->xdma_desc_size,
							      &ch->dma_iova, GFP_KERNEL);
				if (!ch->desc) {
					dev_err(prv->dev, "Cannot allocate required descriptos(%d) of size (%llu) for channel:%d type: %d\n",
						ch->desc_sz, ch->xdma_desc_size, i, j);
					goto dma_iounmap;
				}
			}

			prv->ch_init |= OSI_BIT(j);

			if (xdma_ch_init(prv, ch, (u8)((i + (mode_cnt[0] * j)) & 0xFF)) < 0)
				goto free_dma_desc;

			xdma_ll_ch_init(prv->xdma_base, (u8)((i + (mode_cnt[0] * j)) & 0xFF),
					ch->dma_iova, (j == 0), prv->is_remote_dma);
		}
	}

	if (prv->ch_init == 0U) {
		dev_err(prv->dev, "No channel enabled to initialize\n");
		goto free_ring;
	}

	prv->irq_name = kasprintf(GFP_KERNEL, "%s_xdma_lib", dev_name(prv->dev));
	if (!prv->irq_name)
		goto free_ring;

	ret = request_threaded_irq(prv->irq, tegra_pcie_dma_irq, tegra_pcie_dma_irq_handler,
				   IRQF_SHARED, prv->irq_name, priv_dma);
	if (ret < 0) {
		dev_err(prv->dev, "failed to request irq: %d err: %d\n", prv->irq, ret);
		goto free_irq_name;
	}

	for (i = 0; i < TEGRA_PCIE_DMA_WR_CHNL_NUM; i++) {
		mutex_init(&prv->tx[i].lock);
		init_waitqueue_head(&prv->tx[i].wq);
	}

	for (i = 0; i < TEGRA_PCIE_DMA_RD_CHNL_NUM; i++) {
		mutex_init(&prv->rx[i].lock);
		init_waitqueue_head(&prv->rx[i].wq);
	}
	dev_info(prv->dev, "%s: success", __func__);

	return prv;

free_irq_name:
	kfree(prv->irq_name);
free_ring:
	for (j = 0; j < 2; j++) {
		for (i = 0; i < mode_cnt[j]; i++) {
			ch = chan[j] + i;
			kfree(ch->ring);
		}
	}
free_dma_desc:
	for (j = 0; j < 2; j++) {
		for (i = 0; i < mode_cnt[j]; i++) {
			ch = chan[j] + i;
			if (prv->is_remote_dma && ch->desc)
				devm_iounmap(prv->dev, ch->remap_desc);
			else if (ch->desc)
				dma_free_coherent(prv->dev, ch->xdma_desc_size, ch->desc,
						  ch->dma_iova);
		}
	}
dma_iounmap:
	devm_iounmap(prv->dev, prv->xdma_base);
put_dev:
	if (!prv->is_remote_dma)
		put_device(prv->dev);
free_priv:
	kfree(prv);
	return NULL;
}

tegra_pcie_dma_status_t tegra264_pcie_xdma_set_msi(void *cookie, u64 msi_addr, u32 msi_data)
{
	struct xdma_prv *prv = (struct xdma_prv *)cookie;
	u32 val;

	if (!prv) {
		pr_err("%s inval inputs\n", __func__);
		return TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS;
	}

	val = xdma_common_rd(prv->xdma_base, XDMA_MSI_CFG_MASK);
	val &= ~XDMA_MSI_CFG_MASK_STATUS_MSI;
	xdma_common_wr(prv->xdma_base, val, XDMA_MSI_CFG_MASK);

	if (prv->is_remote_dma == false) {
		xdma_common_wr(prv->xdma_base, lower_32_bits(msi_addr),
			       XDMA_MSI_CFG_LOCAL_ADDRESS_LO);
		xdma_common_wr(prv->xdma_base, upper_32_bits(msi_addr),
			       XDMA_MSI_CFG_LOCAL_ADDRESS_HI);
	} else {
		xdma_common_wr(prv->xdma_base, lower_32_bits(msi_addr),
			       XDMA_MSI_CFG_REMOTE_ADDRESS_LO);
		xdma_common_wr(prv->xdma_base, upper_32_bits(msi_addr),
			       XDMA_MSI_CFG_REMOTE_ADDRESS_HI);
	}

	xdma_channel_wr(prv->xdma_base, prv->is_remote_dma, msi_data, XDMA_MSI_CHANNEL_CFG_INTR_ID);

	return TEGRA_PCIE_DMA_SUCCESS;
}

tegra_pcie_dma_status_t tegra264_pcie_xdma_submit_xfer(void *cookie,
						       struct tegra_pcie_dma_xfer_info *tx_info)
{
	struct xdma_prv *prv = (struct xdma_prv *)cookie;
	struct xdma_chan *ch;
	struct xdma_hw_desc *dma_ll_virt = NULL;
	int i;
	u64 total_sz = 0;
	tegra_pcie_dma_status_t st = TEGRA_PCIE_DMA_SUCCESS;
	u32 avail, to_ms, val;
	struct tegra_pcie_dma_xfer_info *ring;
	u32 mode_cnt[2] = {TEGRA_PCIE_DMA_WR_CHNL_NUM, TEGRA_PCIE_DMA_RD_CHNL_NUM};
	long ret, to_jif;

	if (!prv || !tx_info || tx_info->nents == 0 || !tx_info->desc ||
	    (tx_info->type < TEGRA_PCIE_DMA_WRITE || tx_info->type > TEGRA_PCIE_DMA_READ) ||
	    tx_info->channel_num >= mode_cnt[tx_info->type]) {
		pr_err("%s inval inputs prv: 0x%px tx_info: 0x%px\n", __func__, prv, tx_info);
		return TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS;
	}

	ch = (tx_info->type == TEGRA_PCIE_DMA_WRITE) ? &prv->tx[tx_info->channel_num] :
	     &prv->rx[tx_info->channel_num];

	if (!ch->desc_sz) {
		dev_err(prv->dev, "%s inval inputs desc_sz desc_sz: %u\n", __func__, ch->desc_sz);
		return TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS;
	}

	if ((tx_info->complete == NULL) && (ch->type == TEGRA_PCIE_DMA_CHAN_XFER_ASYNC)) {
		dev_err(prv->dev, "%s inval inputs complete\n", __func__);
		return TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS;
	}

	/* Get hold of the hardware - locking */
	mutex_lock(&ch->lock);

	/* Channel busy flag should be updated before channel status check */
	ch->busy = true;

	if (ch->st != TEGRA_PCIE_DMA_SUCCESS) {
		st = ch->st;
		dev_err(prv->dev, "%s inval inputs st: %d\n", __func__, ch->st);
		goto unlock;
	}

	avail = (ch->r_idx - ch->w_idx - 1U) & (ch->desc_sz - 1U);
	if (tx_info->nents > avail) {
		dev_err(prv->dev, "Descriptors full. w_idx %d. r_idx %d, avail %d, req %d\n",
			ch->w_idx, ch->r_idx, avail, tx_info->nents);
		st = TEGRA_PCIE_DMA_FAIL_NOMEM;
		goto unlock;
	}

	for (i = 0; i < tx_info->nents; i++) {
		dma_ll_virt = (struct xdma_hw_desc *)ch->desc + ch->w_idx;
		dma_ll_virt->size = tx_info->desc[i].sz;
		/* calculate number of packets and add those many headers */
		total_sz +=  (u64)(((tx_info->desc[i].sz / ch->desc_sz) + 1) * 30ULL);
		total_sz += tx_info->desc[i].sz;
		dma_ll_virt->sar_low = lower_32_bits(tx_info->desc[i].src);
		dma_ll_virt->sar_high = upper_32_bits(tx_info->desc[i].src);
		dma_ll_virt->dar_low = lower_32_bits(tx_info->desc[i].dst);
		dma_ll_virt->dar_high = upper_32_bits(tx_info->desc[i].dst);
		/* Set watermark in last element */
		if (i == tx_info->nents - 1)
			dma_ll_virt->attr_reg.ctrl_e.wm = 1;
		avail = ch->w_idx;
		INCR_DESC(ch->w_idx, 1);
	}

	ring = &ch->ring[avail];
	ring->priv = tx_info->priv;
	ring->complete = tx_info->complete;

	/* desc write should not go OOO wrt DMA DB ring */
	wmb();
	/* use smb_wmb to synchronize across cores */
	smp_wmb();

	val = (tx_info->nents << XDMA_CHANNEL_TRANSFER_DOORBELL_NUM_DESC_ADDED_SHIFT) &
	      XDMA_CHANNEL_TRANSFER_DOORBELL_NUM_DESC_ADDED;
	val |= XDMA_CHANNEL_TRANSFER_DOORBELL_DOORBELL;
	xdma_channel_wr(prv->xdma_base, tx_info->channel_num + (tx_info->type * mode_cnt[0]), val,
			XDMA_CHANNEL_TRANSFER_DOORBELL);

	if (ch->type == TEGRA_PCIE_DMA_CHAN_XFER_SYNC) {
		total_sz = GET_SYNC_TIMEOUT(total_sz);
		to_ms = (total_sz > UINT_MAX) ? UINT_MAX : (u32)(total_sz & UINT_MAX);
		to_jif = msecs_to_jiffies(to_ms) > LONG_MAX ?
			 LONG_MAX : msecs_to_jiffies(to_ms) & LONG_MAX;
		ret = wait_event_timeout(ch->wq, !ch->busy, to_jif);
		if (ret == 0) {
			dev_err(prv->dev, "%s: timeout at %d ch, w_idx(%d), r_idx(%d)\n",
				__func__, tx_info->channel_num, ch->w_idx, ch->r_idx);
			st = TEGRA_PCIE_DMA_FAIL_TIMEOUT;
			goto unlock;
		} else {
			st = ch->st;
		}
		dev_dbg(prv->dev, "xmit done for %d nents at %d widx and %d ridx\n",
			tx_info->nents, ch->w_idx, ch->r_idx);
	}
unlock:
	/* release hardware - unlocking */
	mutex_unlock(&ch->lock);

	return st;
}

static void xdma_stop(struct xdma_prv *prv, tegra_pcie_dma_status_t st)
{
	struct xdma_chan *chan[2], *ch;
	int i, j;
	u32 mode_cnt[2] = {TEGRA_PCIE_DMA_WR_CHNL_NUM, TEGRA_PCIE_DMA_RD_CHNL_NUM};

	chan[0] = &prv->tx[0];
	chan[1] = &prv->rx[0];

	/* wake up xfer function waiting on dma completion in sync mode */
	for (j = 0; j < 2; j++) {
		for (i = 0; i < mode_cnt[j]; i++) {
			ch = chan[j] + i;
			if (ch->desc_sz == 0)
				continue;
			ch->st = st;
			if ((ch->type == TEGRA_PCIE_DMA_CHAN_XFER_SYNC) && ch->busy) {
				ch->busy = false;
				wake_up(&ch->wq);
			}
			/** wait until exisitng xfer submit completed */
			mutex_lock(&ch->lock);
			mutex_unlock(&ch->lock);
			xdma_hw_deinit(prv, i + j * TEGRA_PCIE_DMA_WR_CHNL_NUM);
		}
	}

	synchronize_irq(prv->irq);

	for (j = 0; j < 2; j++) {
		for (i = 0; i < mode_cnt[j]; i++) {
			ch = chan[j] + i;
			if (ch->desc_sz == 0)
				continue;

			process_r_idx(ch, st, ch->w_idx);
		}
	}
}

bool tegra264_pcie_xdma_stop(void *cookie)
{
	struct xdma_prv *prv = (struct xdma_prv *)cookie;

	if (cookie == NULL)
		return false;

	xdma_stop(prv, TEGRA_PCIE_DMA_ABORT);

	return true;
}

void tegra264_pcie_xdma_deinit(void *cookie, void *priv_dma)
{
	struct xdma_prv *prv = (struct xdma_prv *)cookie;
	struct xdma_chan *chan[2], *ch;
	int i, j;
	u32 mode_cnt[2] = {TEGRA_PCIE_DMA_WR_CHNL_NUM, TEGRA_PCIE_DMA_RD_CHNL_NUM};

	if (cookie == NULL)
		return;

	xdma_stop(prv, TEGRA_PCIE_DMA_DEINIT);

	free_irq(prv->irq, priv_dma);
	kfree(prv->irq_name);

	chan[0] = &prv->tx[0];
	chan[1] = &prv->rx[0];
	for (j = 0; j < 2; j++) {
		for (i = 0; i < mode_cnt[j]; i++) {
			ch = chan[j] + i;

			if (prv->is_remote_dma && ch->desc)
				devm_iounmap(prv->dev, ch->remap_desc);
			else if (ch->desc)
				dma_free_coherent(prv->dev, ch->xdma_desc_size,
						  ch->desc, ch->dma_iova);
			kfree(ch->ring);
		}
	}

	devm_iounmap(prv->dev, prv->xdma_base);
	if (!prv->is_remote_dma)
		put_device(prv->dev);
	kfree(prv);
}

MODULE_LICENSE("GPL v2");
