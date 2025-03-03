// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved. */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/tegra-pcie-dma.h>

#include "tegra-pcie-dma-irq.h"
#include "tegra234-pcie-edma-osi.h"

#define DMA_WR_CHNL_NUM	4
#define DMA_RD_CHNL_NUM	2

/** Default number of descriptors used */
#define NUM_EDMA_DESC	4096

/* DMA base offset starts at 0x20000 from ATU_DMA base */
#define DMA_OFFSET	0x20000

/** Calculates timeout based on size.
 *  Time in nano sec = size in bytes / (1000000 * 2).
 *    2Gbps is max speed for Gen 1 with 2.5GT/s at 8/10 encoding.
 *  Convert to milli seconds and add 1sec timeout
 */
#define GET_SYNC_TIMEOUT(s)	((((s) * 8UL) / 2000000) + 1000)

#define INCR_DESC(idx, i) ((idx) = ((idx) + (i)) % (ch->desc_sz))

struct edma_chan {
	void *desc;
	void __iomem *remap_desc;
	struct tegra_pcie_dma_xfer_info *ring;
	dma_addr_t dma_iova;
	uint32_t desc_sz;
	/* descriptor size that is allocated for a channel */
	u64 edma_desc_size;
	/** Index from where cleanup needs to be done */
	volatile uint32_t r_idx;
	/** Index from where descriptor update is needed */
	volatile uint32_t w_idx;
	struct mutex lock;
	wait_queue_head_t wq;
	tegra_pcie_dma_chan_type_t type;
	u64 wcount;
	u64 rcount;
	bool busy;
	bool pcs;
	bool db_pos;
	/** This field is updated to abort or de-init to stop further xfer submits */
	tegra_pcie_dma_status_t st;
};

struct edma_prv {
	u32 irq;
	char *irq_name;
	bool is_remote_dma;
	uint16_t msi_data;
	uint64_t msi_addr;
	/** EDMA base address */
	void __iomem *edma_base;
	/** EDMA base address size */
	uint32_t edma_base_size;
	struct device *dev;
	struct edma_chan tx[DMA_WR_CHNL_NUM];
	struct edma_chan rx[DMA_RD_CHNL_NUM];
	/* BIT(0) - Write initialized, BIT(1) - Read initialized */
	uint32_t ch_init;
};

/** TODO: Define osi_ll_init strcuture and make this as OSI */
static inline void edma_ll_ch_init(void __iomem *edma_base, uint8_t ch,
				   dma_addr_t ll_phy_addr, bool rw_type,
				   bool is_remote_dma)
{
	uint32_t int_mask_val = OSI_BIT(ch);
	uint32_t val;
	/** configure write by default and overwrite if read */
	uint32_t int_mask = DMA_WRITE_INT_MASK_OFF;
	uint32_t ctrl1_offset = DMA_CH_CONTROL1_OFF_WRCH;
	uint32_t low_offset = DMA_LLP_LOW_OFF_WRCH;
	uint32_t high_offset = DMA_LLP_HIGH_OFF_WRCH;
	uint32_t lle_ccs = DMA_CH_CONTROL1_OFF_WRCH_LIE | DMA_CH_CONTROL1_OFF_WRCH_LLE |
			   DMA_CH_CONTROL1_OFF_WRCH_CCS;
	uint32_t rie = DMA_CH_CONTROL1_OFF_WRCH_RIE;
	uint32_t err_off = DMA_WRITE_LINKED_LIST_ERR_EN_OFF;
	uint32_t err_val = 0;

	if (!rw_type) {
		int_mask = DMA_READ_INT_MASK_OFF;
		low_offset = DMA_LLP_LOW_OFF_RDCH;
		high_offset = DMA_LLP_HIGH_OFF_RDCH;
		ctrl1_offset = DMA_CH_CONTROL1_OFF_RDCH;
		lle_ccs = DMA_CH_CONTROL1_OFF_RDCH_LIE | DMA_CH_CONTROL1_OFF_RDCH_LLE |
			  DMA_CH_CONTROL1_OFF_RDCH_CCS;
		rie = DMA_CH_CONTROL1_OFF_RDCH_RIE;
		err_off = DMA_READ_LINKED_LIST_ERR_EN_OFF;
	}
	/* Enable LIE or RIE for all write channels */
	val = dma_common_rd(edma_base, int_mask);
	err_val = dma_common_rd(edma_base, err_off);
	if (!is_remote_dma) {
		val &= ~int_mask_val;
		val &= ~(int_mask_val << 16);
		err_val |= OSI_BIT((16 + ch));
	} else {
		val |= int_mask_val;
		val |= (int_mask_val << 16);
		err_val |= OSI_BIT(ch);
	}
	dma_common_wr(edma_base, val, int_mask);
	dma_common_wr(edma_base, err_val, err_off);

	val = lle_ccs;
	/* Enable RIE for remote DMA */
	val |= (is_remote_dma ? rie : 0);
	dma_channel_wr(edma_base, ch, val, ctrl1_offset);
	dma_channel_wr(edma_base, ch, lower_32_bits(ll_phy_addr), low_offset);
	dma_channel_wr(edma_base, ch, upper_32_bits(ll_phy_addr), high_offset);
}

static inline void edma_hw_init(void *cookie, bool rw_type)
{
	struct edma_prv *prv = (struct edma_prv *)cookie;
	uint32_t msi_data;
	u32 eng_off[2] = {DMA_WRITE_ENGINE_EN_OFF, DMA_READ_ENGINE_EN_OFF};

	if (prv->ch_init & OSI_BIT(rw_type))
		dma_common_wr(prv->edma_base, WRITE_ENABLE, eng_off[rw_type]);

	/* Program MSI addr & data for remote DMA use case */
	if (prv->is_remote_dma) {
		msi_data = prv->msi_data;
		msi_data |= (msi_data << 16);

		dma_common_wr(prv->edma_base, lower_32_bits(prv->msi_addr),
			      DMA_WRITE_DONE_IMWR_LOW_OFF);
		dma_common_wr(prv->edma_base, upper_32_bits(prv->msi_addr),
			      DMA_WRITE_DONE_IMWR_HIGH_OFF);
		dma_common_wr(prv->edma_base, lower_32_bits(prv->msi_addr),
			      DMA_WRITE_ABORT_IMWR_LOW_OFF);
		dma_common_wr(prv->edma_base, upper_32_bits(prv->msi_addr),
			      DMA_WRITE_ABORT_IMWR_HIGH_OFF);
		dma_common_wr(prv->edma_base, msi_data,
			      DMA_WRITE_CH01_IMWR_DATA_OFF);
		dma_common_wr(prv->edma_base, msi_data,
			      DMA_WRITE_CH23_IMWR_DATA_OFF);

		dma_common_wr(prv->edma_base, lower_32_bits(prv->msi_addr),
			      DMA_READ_DONE_IMWR_LOW_OFF);
		dma_common_wr(prv->edma_base, upper_32_bits(prv->msi_addr),
			      DMA_READ_DONE_IMWR_HIGH_OFF);
		dma_common_wr(prv->edma_base, lower_32_bits(prv->msi_addr),
			      DMA_READ_ABORT_IMWR_LOW_OFF);
		dma_common_wr(prv->edma_base, upper_32_bits(prv->msi_addr),
			      DMA_READ_ABORT_IMWR_HIGH_OFF);
		dma_common_wr(prv->edma_base, msi_data,
			      DMA_READ_CH01_IMWR_DATA_OFF);
	}
}

static inline int edma_ch_init(struct edma_prv *prv, struct edma_chan *ch)
{
	struct edma_dblock *db;
	dma_addr_t addr;
	uint32_t j;

	if ((ch->desc_sz <= 1) || (ch->desc_sz & (ch->desc_sz - 1)))
		return -EINVAL;

	if (prv->is_remote_dma)
		memset_io(ch->remap_desc, 0,
			  (sizeof(struct edma_dblock)) * ((ch->desc_sz / 2) + 1));
	else
		memset(ch->desc, 0, (sizeof(struct edma_dblock)) * ((ch->desc_sz / 2) + 1));

	db = (struct edma_dblock *)ch->desc + ((ch->desc_sz / 2) - 1);
	db->llp.sar_low = lower_32_bits(ch->dma_iova);
	db->llp.sar_high = upper_32_bits(ch->dma_iova);
	db->llp.ctrl_reg.ctrl_e.llp = 1;
	db->llp.ctrl_reg.ctrl_e.tcb = 1;
	for (j = 0; j < (ch->desc_sz / 2 - 1); j++) {
		db = (struct edma_dblock *)ch->desc + j;
		addr = ch->dma_iova + (sizeof(struct edma_dblock) * (j + 1));
		db->llp.sar_low = lower_32_bits(addr);
		db->llp.sar_high = upper_32_bits(addr);
		db->llp.ctrl_reg.ctrl_e.llp = 1;
	}
	ch->wcount = 0;
	ch->rcount = 0;
	ch->w_idx = 0;
	ch->r_idx = 0;
	ch->pcs = 1;
	ch->st = TEGRA_PCIE_DMA_SUCCESS;

	if (!ch->ring) {
		ch->ring = kcalloc(ch->desc_sz, sizeof(*ch->ring), GFP_KERNEL);
		if (!ch->ring)
			return -ENOMEM;
	}

	return 0;
}

static inline void edma_hw_deinit(void *cookie, bool rw_type)
{
	struct edma_prv *prv = (struct edma_prv *)cookie;
	int i;
	u32 eng_off[2] = {DMA_WRITE_ENGINE_EN_OFF, DMA_READ_ENGINE_EN_OFF};
	u32 ctrl_off[2] = {DMA_CH_CONTROL1_OFF_WRCH, DMA_CH_CONTROL1_OFF_RDCH};
	u32 mode_cnt[2] = {DMA_WR_CHNL_NUM, DMA_RD_CHNL_NUM};

	if (prv->ch_init & OSI_BIT(rw_type)) {
		dma_common_wr(prv->edma_base, 0, eng_off[rw_type]);
		for (i = 0; i < mode_cnt[rw_type]; i++)
			dma_channel_wr(prv->edma_base, i, 0, ctrl_off[rw_type]);
	}
}

/** From OSI */
static inline u32 get_dma_idx_from_llp(struct edma_prv *prv, u8 chan, struct edma_chan *ch,
				       u32 type)
{
	u64 cur_iova;
	u64 high_iova, tmp_iova;
	u32 cur_idx;
	u32 llp_low_off[2] = {DMA_LLP_LOW_OFF_WRCH, DMA_LLP_LOW_OFF_RDCH};
	u32 llp_high_off[2] = {DMA_LLP_HIGH_OFF_WRCH, DMA_LLP_HIGH_OFF_RDCH};
	u64 block_idx, idx_in_block;

	/*
	 * Read current element address in DMA_LLP register which pending
	 * DMA request and validate for spill over.
	 */
	high_iova = dma_channel_rd(prv->edma_base, chan, llp_high_off[type]);
	cur_iova = (high_iova << 32);
	cur_iova |= dma_channel_rd(prv->edma_base, chan, llp_low_off[type]);
	tmp_iova = dma_channel_rd(prv->edma_base, chan, llp_high_off[type]);
	if (tmp_iova > high_iova) {
		/* Take latest reading of low offset and use it with new high offset */
		cur_iova = dma_channel_rd(prv->edma_base, chan, llp_low_off[type]);
		cur_iova |= (tmp_iova << 32);
	}

	if ((cur_iova >= ch->dma_iova) && ((cur_iova - ch->dma_iova) < ch->edma_desc_size)) {
		/* Compute DMA desc index */
		block_idx = ((cur_iova - ch->dma_iova) / sizeof(struct edma_dblock));
		idx_in_block = (cur_iova & (sizeof(struct edma_dblock) - 1)) /
			       sizeof(struct edma_hw_desc);
		cur_idx = (u32)(((block_idx * 2UL) + idx_in_block) & UINT_MAX);
	} else {
		ch->st = TEGRA_PCIE_DMA_ABORT;
		/* Set Read index to ensure all callbacks gets error return */
		cur_idx = (ch->r_idx - 1U) % ch->desc_sz;
	}

	return cur_idx % (ch->desc_sz);
}

static inline void process_r_idx(struct edma_chan *ch, tegra_pcie_dma_status_t st, u32 idx)
{
	u32 count = 0;
	struct edma_hw_desc *dma_ll_virt;
	struct edma_dblock *db;
	struct tegra_pcie_dma_xfer_info *ring;

	while ((ch->r_idx != idx) && (count < ch->desc_sz)) {
		count++;
		ring = &ch->ring[ch->r_idx];
		db = (struct edma_dblock *)ch->desc + ch->r_idx / 2;
		dma_ll_virt = &db->desc[ch->r_idx % 2];
		INCR_DESC(ch->r_idx, 1);
		ch->rcount++;
		/* clear lie and rie if any set */
		dma_ll_virt->ctrl_reg.ctrl_e.lie = 0;
		dma_ll_virt->ctrl_reg.ctrl_e.rie = 0;
		if (ch->type == TEGRA_PCIE_DMA_CHAN_XFER_ASYNC && ring->complete) {
			ring->complete(ring->priv, st);
			/* Clear ring callback and priv variables */
			ring->complete = NULL;
			ring->priv = NULL;
		}
	}
}

static inline void process_ch_irq(struct edma_prv *prv, u32 chan, struct edma_chan *ch,
				  u32 type)
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

irqreturn_t edma_irq(int irq, void *cookie)
{
	/* Disable irq before wake thread handler */
	disable_irq_nosync((u32)(irq & INT_MAX));

	return IRQ_WAKE_THREAD;
}

static void edma_check_and_ring_db(struct edma_prv *prv, u8 bit, u32 ctrl_off, u32 db_off)
{
	u32 reg = dma_channel_rd(prv->edma_base, bit, ctrl_off);

#define CS_POS		5U
#define CS_MASK		(OSI_BIT(5) | OSI_BIT(6))
#define CS_RUNNING	2U
	reg &= CS_MASK;
	reg >>= CS_POS;
	/*
	 * There are pending descriptors to process, but
	 * EDMA HW is not running. Ring the doorbell again
	 * to ensure that descriptors are processed.
	 */
	if (reg != CS_RUNNING)
		dma_common_wr(prv->edma_base, bit, db_off);
}

irqreturn_t edma_irq_handler(int irq, void *cookie)
{
	struct edma_prv *prv = (struct edma_prv *)cookie;
	int bit = 0;
	u32 val, i = 0;
	struct edma_chan *chan[2] = {&prv->tx[0], &prv->rx[0]};
	struct edma_chan *ch;
	u32 int_status_off[2] = {DMA_WRITE_INT_STATUS_OFF, DMA_READ_INT_STATUS_OFF};
	u32 int_clear_off[2] = {DMA_WRITE_INT_CLEAR_OFF, DMA_READ_INT_CLEAR_OFF};
	u32 mode_cnt[2] = {DMA_WR_CHNL_NUM, DMA_RD_CHNL_NUM};
	u32 ctrl_off[2] = {DMA_CH_CONTROL1_OFF_WRCH, DMA_CH_CONTROL1_OFF_RDCH};
	u32 db_off[2] = {DMA_WRITE_DOORBELL_OFF, DMA_READ_DOORBELL_OFF};

	for (i = 0; i < 2; i++) {
		if (!(prv->ch_init & OSI_BIT(i)))
			continue;

		val = dma_common_rd(prv->edma_base, int_status_off[i]);
		if ((val & OSI_GENMASK(31, 16)) != 0U) {
			/**
			 * If ABORT, immediately update state for all channels as aborted.
			 * This setting stop further SW queuing
			 */
			dev_info(prv->dev, "Abort int status 0x%x", val);
			for (bit = 0; bit < mode_cnt[i]; bit++) {
				ch = chan[i] + bit;
				ch->st = TEGRA_PCIE_DMA_ABORT;
			}

			edma_hw_deinit(prv, !!i);

			/** Perform abort handling */
			for (bit = 0; bit < mode_cnt[i]; bit++) {
				ch = chan[i] + bit;
				if (!ch->ring)
					continue;

				/* Clear ABORT and DONE interrupt, as abort handles both */
				dma_common_wr(prv->edma_base, OSI_BIT(16 + bit) | OSI_BIT(bit),
					      int_clear_off[i]);
				/** wait until exisitng xfer submit completed */
				mutex_lock(&ch->lock);
				mutex_unlock(&ch->lock);

				process_ch_irq(prv, bit, ch, i);

				edma_ch_init(prv, ch);
				edma_ll_ch_init(prv->edma_base, (u8)(bit & 0xFF), ch->dma_iova,
						(i == 0), prv->is_remote_dma);
			}

			edma_hw_init(prv, !!i);
		} else {
			for (bit = 0; bit < mode_cnt[i]; bit++) {
				ch = chan[i] + bit;
				if (OSI_BIT(bit) & val) {
					dma_common_wr(prv->edma_base, OSI_BIT(bit),
						      int_clear_off[i]);
					process_ch_irq(prv, bit, ch, i);
					/* Check if there are pending descriptors */
					if (ch->w_idx != ch->r_idx)
						edma_check_and_ring_db(prv, (u8)(bit & 0xFF),
								       ctrl_off[i], db_off[i]);
				}
			}
		}
	}

	/* Must enable before exit */
	enable_irq((u32)(irq & INT_MAX));
	return IRQ_HANDLED;
}

void *tegra234_pcie_edma_initialize(struct tegra_pcie_dma_init_info *info, void *priv_dma)
{
	struct edma_prv *prv;
	struct resource *dma_res;
	int32_t ret, i, j, l_irq = 0;
	struct edma_chan *ch = NULL;
	struct edma_chan *chan[2];
	u32 mode_cnt[2] = {DMA_WR_CHNL_NUM, DMA_RD_CHNL_NUM};
	struct tegra_pcie_dma_chans_info *chan_info[2];
	struct tegra_pcie_dma_chans_info *ch_info;
	struct platform_device *pdev;
	struct device_node *np;
	struct pci_host_bridge *bridge;
	struct pci_bus *bus;
	struct pci_dev *pci_dev;
	struct device *dev;

	prv = kzalloc(sizeof(*prv), GFP_KERNEL);
	if (!prv) {
		pr_err("Failed to allocate memory for edma_prv\n");
		return NULL;
	}

	chan[0] = &prv->tx[0];
	chan[1] = &prv->rx[0];
	chan_info[0] = &info->tx[0];
	chan_info[1] = &info->rx[0];

	if (!info->dev) {
		pr_err("%s: dev pointer is NULL\n", __func__);
		goto free_priv;
	}
	prv->dev = info->dev;
	np = prv->dev->of_node;

	if (info->remote != NULL) {
		if (info->msi_irq > INT_MAX) {
			pr_err("%s: msi_irq is out of range\n", __func__);
			goto free_priv;
		}
		prv->irq = (int)info->msi_irq;
		prv->msi_data = info->msi_data;
		prv->msi_addr = info->msi_addr;
		prv->is_remote_dma = true;

		prv->edma_base = devm_ioremap(prv->dev, info->remote->dma_phy_base,
					      info->remote->dma_size);
		if (IS_ERR(prv->edma_base)) {
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

		dma_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "atu_dma");
		if (!dma_res) {
			dev_err(prv->dev, "missing atu_dma resource in DT\n");
			goto put_dev;
		}

		prv->edma_base = devm_ioremap(prv->dev, dma_res->start + DMA_OFFSET,
					      resource_size(dma_res) - DMA_OFFSET);
		if (IS_ERR(prv->edma_base)) {
			dev_err(prv->dev, "dma region map failed.\n");
			goto put_dev;
		}

		l_irq = platform_get_irq_byname(pdev, "intr");
		if (l_irq <= 0) {
			dev_err(prv->dev, "failed to get intr interrupt\n");
			goto put_dev;
		} else {
			prv->irq = (u32)l_irq;
		}
	} else {
		pr_err("Neither device node nor edma remote available");
		goto free_priv;
	}

	for (j = 0; j < 2; j++) {
		for (i = 0; i < mode_cnt[j]; i++) {
			ch_info = chan_info[j] + i;
			ch = chan[j] + i;

			if (ch_info->num_descriptors == 0)
				continue;

			ch->type = ch_info->ch_type;
			ch->desc_sz = ch_info->num_descriptors;
			ch->edma_desc_size = (sizeof(struct edma_dblock)) * ((ch->desc_sz / 2) + 1);

			if (prv->is_remote_dma) {
				ch->dma_iova = ch_info->desc_iova;
				ch->remap_desc = devm_ioremap(prv->dev, ch_info->desc_phy_base,
							      (sizeof(struct edma_dblock)) *
							      ((ch->desc_sz / 2) + 1));
				ch->desc = (__force void *)ch->remap_desc;
				if (!ch->desc) {
					dev_err(prv->dev, "desc region map failed, phy: 0x%llx\n",
						ch_info->desc_phy_base);
					goto dma_iounmap;
				}
			} else {
				ch->desc = dma_alloc_coherent(prv->dev, ch->edma_desc_size,
							      &ch->dma_iova, GFP_KERNEL);
				if (!ch->desc) {
					dev_err(prv->dev, "Cannot allocate required descriptos(%d) of size (%lu) for channel:%d type: %d\n",
						ch->desc_sz,
						(sizeof(struct edma_hw_desc) * ch->desc_sz), i, j);
					goto dma_iounmap;
				}
			}

			prv->ch_init |= OSI_BIT(j);

			if (edma_ch_init(prv, ch) < 0)
				goto free_dma_desc;

			edma_ll_ch_init(prv->edma_base, (u8)(i & 0xFF), ch->dma_iova, (j == 0),
					prv->is_remote_dma);
		}
	}

	prv->irq_name = kasprintf(GFP_KERNEL, "%s_edma_lib", dev_name(prv->dev));
	if (!prv->irq_name)
		goto free_ring;

	ret = request_threaded_irq(prv->irq, tegra_pcie_dma_irq, tegra_pcie_dma_irq_handler,
				   IRQF_SHARED, prv->irq_name, priv_dma);
	if (ret < 0) {
		dev_err(prv->dev, "failed to request \"intr\" irq\n");
		goto free_irq_name;
	}

	for (i = 0; i < DMA_WR_CHNL_NUM; i++) {
		mutex_init(&prv->tx[i].lock);
		init_waitqueue_head(&prv->tx[i].wq);
	}

	for (i = 0; i < DMA_RD_CHNL_NUM; i++) {
		mutex_init(&prv->rx[i].lock);
		init_waitqueue_head(&prv->rx[i].wq);
	}

	edma_hw_init(prv, false);
	edma_hw_init(prv, true);
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
				dma_free_coherent(prv->dev, ch->edma_desc_size,
						  ch->desc, ch->dma_iova);
		}
	}
dma_iounmap:
	devm_iounmap(prv->dev, prv->edma_base);
put_dev:
	if (!prv->is_remote_dma)
		put_device(prv->dev);
free_priv:
	kfree(prv);
	return NULL;
}

tegra_pcie_dma_status_t tegra234_pcie_edma_submit_xfer(void *cookie,
						       struct tegra_pcie_dma_xfer_info *tx_info)
{
	struct edma_prv *prv = (struct edma_prv *)cookie;
	struct edma_chan *ch;
	struct edma_hw_desc *dma_ll_virt = NULL;
	struct edma_dblock *db;
	int i;
	u64 total_sz = 0;
	tegra_pcie_dma_status_t st = TEGRA_PCIE_DMA_SUCCESS;
	u32 avail, to_ms;
	struct tegra_pcie_dma_xfer_info *ring;
	u32 int_status_off[2] = {DMA_WRITE_INT_STATUS_OFF, DMA_READ_INT_STATUS_OFF};
	u32 doorbell_off[2] = {DMA_WRITE_DOORBELL_OFF, DMA_READ_DOORBELL_OFF};
	u32 mode_cnt[2] = {DMA_WR_CHNL_NUM, DMA_RD_CHNL_NUM};
	bool pcs, final_pcs = false;
	long ret, to_jif;

	if (!prv || !tx_info || tx_info->nents == 0 || !tx_info->desc ||
	    (tx_info->type < TEGRA_PCIE_DMA_WRITE || tx_info->type > TEGRA_PCIE_DMA_READ) ||
	    tx_info->channel_num >= mode_cnt[tx_info->type])
		return TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS;

	ch = (tx_info->type == TEGRA_PCIE_DMA_WRITE) ? &prv->tx[tx_info->channel_num] :
						     &prv->rx[tx_info->channel_num];

	if (!ch->desc_sz)
		return TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS;

	if ((tx_info->complete == NULL) && (ch->type == TEGRA_PCIE_DMA_CHAN_XFER_ASYNC))
		return TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS;

	/* Get hold of the hardware - locking */
	mutex_lock(&ch->lock);

	/* Channel busy flag should be updated before channel status check */
	ch->busy = true;

	if (ch->st != TEGRA_PCIE_DMA_SUCCESS) {
		st = ch->st;
		goto unlock;
	}

	avail = (ch->r_idx - ch->w_idx - 1U) & (ch->desc_sz - 1U);
	if (tx_info->nents > avail) {
		dev_dbg(prv->dev, "Descriptors full. w_idx %d. r_idx %d, avail %d, req %d\n",
			ch->w_idx, ch->r_idx, avail, tx_info->nents);
		st = TEGRA_PCIE_DMA_FAIL_NOMEM;
		goto unlock;
	}

	dev_dbg(prv->dev, "xmit for %d nents at %d widx and %d ridx\n",
		tx_info->nents, ch->w_idx, ch->r_idx);
	db = (struct edma_dblock *)ch->desc + (ch->w_idx / 2);
	for (i = 0; i < tx_info->nents; i++) {
		dma_ll_virt = &db->desc[ch->db_pos];
		dma_ll_virt->size = tx_info->desc[i].sz;
		/* calculate number of packets and add those many headers */
		total_sz +=  (u64)(((tx_info->desc[i].sz / ch->desc_sz) + 1) * 30ULL);
		total_sz += tx_info->desc[i].sz;
		dma_ll_virt->sar_low = lower_32_bits(tx_info->desc[i].src);
		dma_ll_virt->sar_high = upper_32_bits(tx_info->desc[i].src);
		dma_ll_virt->dar_low = lower_32_bits(tx_info->desc[i].dst);
		dma_ll_virt->dar_high = upper_32_bits(tx_info->desc[i].dst);
		/* Set LIE or RIE in last element */
		if (i == tx_info->nents - 1) {
			dma_ll_virt->ctrl_reg.ctrl_e.lie = 1;
			dma_ll_virt->ctrl_reg.ctrl_e.rie = !!prv->is_remote_dma;
			final_pcs = ch->pcs;
		} else {
			/* CB should be updated last in the descriptor */
			dma_ll_virt->ctrl_reg.ctrl_e.cb = ch->pcs;
		}
		ch->db_pos = !ch->db_pos;
		avail = ch->w_idx;
		ch->w_idx++;
		if (!ch->db_pos) {
			ch->wcount = 0;
			db->llp.ctrl_reg.ctrl_e.cb = ch->pcs;
			if (ch->w_idx == ch->desc_sz) {
				ch->pcs = !ch->pcs;
				db->llp.ctrl_reg.ctrl_e.cb = ch->pcs;
				dev_dbg(prv->dev, "Toggled pcs at w_idx %d\n", ch->w_idx);
				ch->w_idx = 0;
			}
			db = (struct edma_dblock *)ch->desc + (ch->w_idx / 2);
		}
	}

	ring = &ch->ring[avail];
	ring->priv = tx_info->priv;
	ring->complete = tx_info->complete;

	/* Update CB post SW ring update to order callback and transfer updates */
	dma_ll_virt->ctrl_reg.ctrl_e.cb = final_pcs;

	/* desc write should not go OOO wrt DMA DB ring */
	wmb();
	/* use smb_wmb to synchronize across cores */
	smp_wmb();

	/* Read back CB and check with CCS to ensure that descriptor update is reflected. */
	pcs = dma_ll_virt->ctrl_reg.ctrl_e.cb;

	if (pcs != (dma_channel_rd(prv->edma_base, tx_info->channel_num, DMA_CH_CONTROL1_OFF_WRCH) &
				   OSI_BIT(8)))
		dev_dbg(prv->dev, "read pcs != CCS failed. But expected sometimes: %d\n", pcs);

	/* Ensure that all reads and writes are ordered */
	mb();
	/* Ensure that all reads and writes are ordered */
	smp_mb();

	dma_common_wr(prv->edma_base, tx_info->channel_num, doorbell_off[tx_info->type]);

	if (ch->type == TEGRA_PCIE_DMA_CHAN_XFER_SYNC) {
		total_sz = GET_SYNC_TIMEOUT(total_sz);
		to_ms = (total_sz > UINT_MAX) ? UINT_MAX : (u32)(total_sz & UINT_MAX);
		to_jif = msecs_to_jiffies(to_ms) > LONG_MAX ?
			 LONG_MAX : msecs_to_jiffies(to_ms) & LONG_MAX;
		ret = wait_event_timeout(ch->wq, !ch->busy, to_jif);
		if (ret == 0) {
			/* dummy print to avoid misra-c voilations */
			dev_dbg(prv->dev, "read back pcs: %d\n", pcs);
			dev_err(prv->dev, "%s: timeout at %d ch, w_idx(%d), r_idx(%d)\n",
				__func__, tx_info->channel_num, ch->w_idx, ch->r_idx);
			dev_err(prv->dev, "%s: int status 0x%x", __func__,
				dma_common_rd(prv->edma_base, int_status_off[tx_info->type]));
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

static void edma_stop(struct edma_prv *prv, tegra_pcie_dma_status_t st)
{
	struct edma_chan *chan[2], *ch;
	int i, j;
	u32 mode_cnt[2] = {DMA_WR_CHNL_NUM, DMA_RD_CHNL_NUM};
	bool sync_irq = false;

	chan[0] = &prv->tx[0];
	chan[1] = &prv->rx[0];

	/* wake up xfer function waiting on dma completion in sync mode */
	for (j = 0; j < 2; j++) {
		for (i = 0; i < mode_cnt[j]; i++) {
			ch = chan[j] + i;
			ch->st = st;
			if ((ch->type == TEGRA_PCIE_DMA_CHAN_XFER_SYNC) && ch->busy) {
				ch->busy = false;
				wake_up(&ch->wq);
			}
			/** wait until exisitng xfer submit completed */
			mutex_lock(&ch->lock);
			mutex_unlock(&ch->lock);
			if (ch->w_idx != ch->r_idx)
				sync_irq = true;
		}
	}

	edma_hw_deinit(prv, false);
	edma_hw_deinit(prv, true);

	if (sync_irq)
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

bool tegra234_pcie_edma_stop(void *cookie)
{
	struct edma_prv *prv = (struct edma_prv *)cookie;

	if (cookie == NULL)
		return false;

	edma_stop(prv, TEGRA_PCIE_DMA_ABORT);
	return true;
}

void tegra234_pcie_edma_deinit(void *cookie, void *priv_dma)
{
	struct edma_prv *prv = (struct edma_prv *)cookie;
	struct edma_chan *chan[2], *ch;
	int i, j;
	u32 mode_cnt[2] = {DMA_WR_CHNL_NUM, DMA_RD_CHNL_NUM};

	if (cookie == NULL)
		return;

	edma_stop(prv, TEGRA_PCIE_DMA_DEINIT);

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
				dma_free_coherent(prv->dev, ch->edma_desc_size,
						  ch->desc, ch->dma_iova);
			kfree(ch->ring);
		}
	}

	devm_iounmap(prv->dev, prv->edma_base);
	if (!prv->is_remote_dma)
		put_device(prv->dev);
	kfree(prv);
}

MODULE_LICENSE("GPL v2");
