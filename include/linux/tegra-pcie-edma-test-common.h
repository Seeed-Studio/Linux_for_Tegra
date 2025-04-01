/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * PCIe DMA EPF Library for Tegra PCIe
 *
 * Copyright (C) 2022-2025 NVIDIA Corporation. All rights reserved.
 */

#ifndef TEGRA_PCIE_EDMA_TEST_COMMON_H
#define TEGRA_PCIE_EDMA_TEST_COMMON_H

#include <linux/pci-epf.h>
#include <linux/tegra-pcie-dma.h>

#define DMA_WRITE_DOORBELL_OFF		0x10
#define DMA_WRITE_DOORBELL_OFF_WR_STOP	BIT(31)

#define DMA_READ_DOORBELL_OFF		0x30

static inline void dma_common_wr(void __iomem *p, u32 val, u32 offset)
{
	writel(val, offset + p);
}

#define  TEGRA264_PCIE_DMA_MSI_CRC_VEC (TEGRA264_PCIE_DMA_MSI_REMOTE_VEC + 1U)

#define REMOTE_EDMA_TEST_EN	(edma->edma_ch & 0x80000000)
#define EDMA_ABORT_TEST_EN	(edma->edma_ch & 0x40000000)
#define EDMA_STOP_TEST_EN	(edma->edma_ch & 0x20000000)
#define EDMA_CRC_TEST_EN	(edma->edma_ch & 0x10000000)
#define EDMA_READ_TEST_EN	(edma->edma_ch & 0x08000000)
#define EDMA_SANITY_TEST_EN	(edma->edma_ch & 0x04000000)
#define EDMA_UNALIGN_SRC_TEST_EN	(edma->edma_ch & 0x02000000)
#define EDMA_UNALIGN_DST_TEST_EN	(edma->edma_ch & 0x01000000)
#define EDMA_UNALIGN_SRC_DST_TEST_EN	(edma->edma_ch & 0x00800000)
#define DMA_ABORT_SWITS_TEST_EN	(edma->edma_ch & 0x00400000)
#define DMA_STOP_SWITS_TEST_EN	(edma->edma_ch & 0x00200000)
#define IS_EDMA_CH_ENABLED(i)	(edma->edma_ch & ((BIT(i) << 4)))
#define IS_EDMA_CH_ASYNC(i)	(edma->edma_ch & BIT(i))
#define EDMA_PERF (edma->tsz / (diff / 1000))
#define EDMA_CPERF ((edma->tsz * (edma->nents / edma->nents_per_ch)) / (diff / 1000))

#define NUM_EDMA_DESC			4096

#define TEGRA234_PCIE_DMA_RD_CHNL_NUM	2

#define EDMA_PRIV_CH_OFF	32
#define EDMA_PRIV_LR_OFF	(EDMA_PRIV_CH_OFF + 2)
#define EDMA_PRIV_XF_OFF	(EDMA_PRIV_LR_OFF + 1)

/* Update DMA_DD_BUF_SIZE and DMA_LL_BUF_SIZE when changing BAR0_SIZE */
#define BAR0_SIZE               SZ_256M

/* Header includes RP/EP DMA addresses, EP MSI, LL, etc. */
#define BAR0_HEADER_OFFSET	0x0
#define BAR0_HEADER_SIZE	SZ_1M
#define DMA_LL_DEFAULT_SIZE	8

#define  BAR0_MSI_OFFSET        SZ_64K

/* DMA'able memory range */
#define BAR0_DMA_BUF_OFFSET	SZ_1M
#define BAR0_DMA_BUF_SIZE	(BAR0_SIZE - SZ_1M)

#define DEFAULT_STRESS_COUNT	10

#define MAX_DMA_ELE_SIZE	SZ_16M

/* DMA base offset starts at 0x20000 from ATU_DMA base */
#define DMA_OFFSET		0x20000

/* Outbound magic number */
#define PCIE_EP_OB_MAGIC	0xA5A519885A5A1984LU
#define PCIE_EP_OB_OFFSET	SZ_16K

struct sanity_data {
	u32 size;
	u32 src_offset;
	u32 dst_offset;
	u32 crc;
};

/* First 1MB of BAR0 is reserved for control data */
struct pcie_epf_bar {
	/* RP system memory allocated for EP DMA operations */
	u64 rp_phy_addr;
	/* EP system memory allocated as BAR */
	u64 ep_phy_addr;
	/* MSI data for RP -> EP interrupts */
	u32 msi_data[TEGRA_PCIE_DMA_WR_CHNL_NUM + TEGRA_PCIE_DMA_RD_CHNL_NUM];
	struct sanity_data wr_data[TEGRA_PCIE_DMA_WR_CHNL_NUM];
	struct sanity_data rd_data[TEGRA_PCIE_DMA_RD_CHNL_NUM];
};

struct edmalib_common {
	struct device *fdev;
	struct device *cdev;
	void (*raise_irq)(void *p);
	void *priv;
	struct pcie_epf_bar *epf_bar;
	void *src_virt;
	void __iomem *dma_virt;
	u32 dma_size;
	dma_addr_t src_dma_addr;
	dma_addr_t dst_dma_addr;
	dma_addr_t bar_phy;
	u32 stress_count;
	void *cookie;
	struct device_node *of_node;
	wait_queue_head_t wr_wq[TEGRA_PCIE_DMA_WR_CHNL_NUM];
	unsigned long wr_busy;
	unsigned long rd_busy;
	ktime_t edma_start_time[TEGRA_PCIE_DMA_WR_CHNL_NUM];
	u64 tsz;
	u32 edma_ch;
	u32 prev_edma_ch;
	u32 nents;
	struct tegra_pcie_dma_desc *ll_desc;
	u64 priv_iter[TEGRA_PCIE_DMA_WR_CHNL_NUM];
	struct tegra_pcie_dma_remote_info remote;
	u32 nents_per_ch;
	u32 st_as_ch;
	u32 ls_as_ch;
	u64 msi_addr;
	u32 msi_data;
	u32 msi_irq;
	nvpcie_dma_soc_t chip_id;
};

static struct edmalib_common *l_edma;

static void edma_final_complete(void *priv, tegra_pcie_dma_status_t status)
{
	struct edmalib_common *edma = l_edma;
	u64 cb = *(u64 *)priv;
	u32 ch = (cb >> EDMA_PRIV_CH_OFF) & 0x3;
	tegra_pcie_dma_xfer_type_t xfer_type = (cb >> EDMA_PRIV_XF_OFF) & 0x1;
	char *xfer_str[2] = {"WR", "RD"};
	u32 l_r = (cb >> EDMA_PRIV_LR_OFF) & 0x1;
	char *l_r_str[2] = {"local", "remote"};
	u64 diff = ktime_to_ns(ktime_get()) - ktime_to_ns(edma->edma_start_time[ch]);
	u64 cdiff = ktime_to_ns(ktime_get()) - ktime_to_ns(edma->edma_start_time[edma->st_as_ch]);

	cb = cb & 0xFFFFFFFF;
	/* TODO support abort test case for T264 */
	if (edma->chip_id == NVPCIE_DMA_SOC_T234) {
		if (EDMA_ABORT_TEST_EN && status == TEGRA_PCIE_DMA_SUCCESS)
			dma_common_wr(edma->dma_virt, DMA_WRITE_DOORBELL_OFF_WR_STOP | (ch + 1),
				      DMA_WRITE_DOORBELL_OFF);
	}

	dev_info(edma->fdev, "%s: %s-%s-Async complete for chan %d with status %d. Total desc %llu of Sz %d Bytes done in time %llu nsec. Perf is %llu Mbps\n",
		 __func__, xfer_str[xfer_type], l_r_str[l_r], ch, status, edma->nents_per_ch*(cb+1),
		 edma->dma_size, diff, EDMA_PERF);

	if (ch == edma->ls_as_ch)
		dev_info(edma->fdev, "%s: All Async channels. Cumulative Perf %llu Mbps, time %llu nsec\n",
			 __func__, EDMA_CPERF, cdiff);
}

static void edma_complete(void *priv, tegra_pcie_dma_status_t status)
{
	struct edmalib_common *edma = l_edma;
	u64 cb = *(u64 *)priv;
	u32 ch = (cb >> EDMA_PRIV_CH_OFF) & 0x3;

	if (BIT(ch) & edma->wr_busy) {
		edma->wr_busy &= ~(BIT(ch));
		wake_up(&edma->wr_wq[ch]);
	}

	if (status == 0)
		dev_dbg(edma->fdev, "%s: status %d, cb %llu\n", __func__, status, cb);
}

/* debugfs to perform eDMA lib transfers and do CRC check */
static int edmalib_common_test(struct edmalib_common *edma)
{
	struct tegra_pcie_dma_desc *ll_desc = edma->ll_desc;
	dma_addr_t src_dma_addr = edma->src_dma_addr;
	dma_addr_t dst_dma_addr = edma->dst_dma_addr;
	u32 nents = edma->nents, num_chans = 0, nents_per_ch = 0, nent_id = 0, chan_count;
	u32 i, j, k, max_size, num_descriptors;
	u32 db_off;
	tegra_pcie_dma_status_t ret;
	struct tegra_pcie_dma_init_info info = {};
	struct tegra_pcie_dma_chans_info *chan_info;
	struct tegra_pcie_dma_xfer_info tx_info = {};
	u64 diff;
	tegra_pcie_dma_xfer_type_t xfer_type;
	char *xfer_str[2] = {"WR", "RD"};
	u32 l_r;
	char *l_r_str[2] = {"local", "remote"};
	struct pcie_epf_bar *epf_bar = edma->epf_bar;
	u32 crc;

	if (!edma->stress_count) {
		tegra_pcie_dma_deinit(&edma->cookie);
		edma->cookie = NULL;
		return 0;
	}

	l_edma = edma;

	if (DMA_ABORT_SWITS_TEST_EN || DMA_STOP_SWITS_TEST_EN) {
		edma->edma_ch &= ~0xFF;
		/* All channels in ASYNC, where chan 2 async gets aborted */
		edma->edma_ch |= 0x11;
	}
	if (EDMA_ABORT_TEST_EN || EDMA_STOP_TEST_EN) {
		edma->edma_ch &= ~0xFF;
		/* All channels in ASYNC, where chan 2 async gets aborted */
		edma->edma_ch |= 0xFF;
	}

	if (EDMA_CRC_TEST_EN) {
		/* 4 channels in sync mode */
		edma->edma_ch = (0x10000000 | 0xF0);
		/* Single SZ_4K packet on each channel, so total SZ_16K of data */
		edma->stress_count  = 1;
		edma->dma_size = SZ_4K;
		edma->nents = nents = 4;
		epf_bar->wr_data[0].size = edma->dma_size * edma->nents;
	}

	if (EDMA_UNALIGN_SRC_TEST_EN) {
		/* 4 channels in sync mode */
		edma->edma_ch &= ~0xFF;
		edma->edma_ch |= (0x02000000 | 0x10000000 | 0x10);
		/* Single SZ_4K packet on each channel, so total SZ_16K of data */
		edma->stress_count  = 1;
		edma->dma_size = SZ_4K;
		edma->nents = nents = 4;
		epf_bar->wr_data[0].size = edma->dma_size * edma->nents;
		src_dma_addr += 11;
		epf_bar->wr_data[0].dst_offset = 0;
		epf_bar->wr_data[0].src_offset = 11;
	}

	if (EDMA_UNALIGN_DST_TEST_EN) {
		/* 4 channels in sync mode */
		edma->edma_ch &= ~0xFF;
		edma->edma_ch |= (0x01000000 | 0x10000000 | 0x10);
		/* Single SZ_4K packet on each channel, so total SZ_16K of data */
		edma->stress_count  = 1;
		edma->dma_size = SZ_4K;
		edma->nents = nents = 4;
		epf_bar->wr_data[0].size = edma->dma_size * edma->nents;
		dst_dma_addr += 7;
		epf_bar->wr_data[0].src_offset = 0;
		epf_bar->wr_data[0].dst_offset = 7;
	}

	if (EDMA_UNALIGN_SRC_DST_TEST_EN) {
		/* 4 channels in sync mode */
		edma->edma_ch &= ~0xFF;
		edma->edma_ch |= (0x00800000 | 0x10000000 | 0x10);
		/* Single SZ_4K packet on each channel, so total SZ_16K of data */
		edma->stress_count  = 1;
		edma->dma_size = SZ_4K;
		edma->nents = nents = 4;
		epf_bar->wr_data[0].size = edma->dma_size * edma->nents;
		src_dma_addr += 7;
		dst_dma_addr += 13;
		epf_bar->wr_data[0].src_offset = 7;
		epf_bar->wr_data[0].dst_offset = 13;
	}

	if (EDMA_SANITY_TEST_EN) {
		edma->dma_size = SZ_1K;
		edma->nents = nents = 128;
		edma->stress_count  = 2;
	}

	if (edma->cookie && edma->prev_edma_ch != edma->edma_ch) {
		dev_info(edma->fdev, "edma_ch changed from 0x%x != 0x%x, deinit\n",
			edma->prev_edma_ch, edma->edma_ch);
		tegra_pcie_dma_deinit(&edma->cookie);
		edma->st_as_ch = -1;
		edma->cookie = NULL;
	}

	info.dev = edma->cdev;
	info.soc = edma->chip_id;

	if (REMOTE_EDMA_TEST_EN) {
		num_descriptors = 1024;
		info.rx[0].desc_phy_base = edma->bar_phy + SZ_128K;
		info.rx[0].desc_iova = epf_bar->ep_phy_addr + SZ_128K;
		info.rx[1].desc_phy_base = edma->bar_phy + SZ_256K;
		info.rx[1].desc_iova = epf_bar->ep_phy_addr + SZ_128K;
		info.rx[2].desc_phy_base = edma->bar_phy + SZ_256K + SZ_128K;
		info.rx[2].desc_iova = epf_bar->ep_phy_addr + SZ_256K + SZ_128K;
		info.rx[3].desc_phy_base = edma->bar_phy + SZ_512K;
		info.rx[3].desc_iova = epf_bar->ep_phy_addr + SZ_512K;
		info.remote = &edma->remote;
		info.msi_irq = edma->msi_irq;
		info.msi_data = edma->msi_data;
		info.msi_addr = edma->msi_addr;
		if (edma->chip_id == NVPCIE_DMA_SOC_T234)
			chan_count = TEGRA234_PCIE_DMA_RD_CHNL_NUM;
		else
			chan_count = TEGRA_PCIE_DMA_RD_CHNL_NUM;
		chan_info = &info.rx[0];
		xfer_type = TEGRA_PCIE_DMA_READ;
		/* TODO support abort test case for T264 */
		if (edma->chip_id == NVPCIE_DMA_SOC_T234)
			db_off = DMA_WRITE_DOORBELL_OFF;
		l_r = 1;
	} else {
		chan_count = TEGRA_PCIE_DMA_WR_CHNL_NUM;
		num_descriptors = 4096;
		chan_info = &info.tx[0];
		xfer_type = TEGRA_PCIE_DMA_WRITE;
		/* TODO support abort test case for T264 */
		if (edma->chip_id == NVPCIE_DMA_SOC_T234)
			db_off = DMA_READ_DOORBELL_OFF;
		l_r = 0;
		info.msi_irq = edma->msi_irq;
		info.msi_data = edma->msi_data;
		info.msi_addr = edma->msi_addr;
	}

	if (EDMA_READ_TEST_EN) {
		if (edma->chip_id == NVPCIE_DMA_SOC_T234)
			chan_count = TEGRA234_PCIE_DMA_RD_CHNL_NUM;
		else
			chan_count = TEGRA_PCIE_DMA_RD_CHNL_NUM;
		num_descriptors = 4096;
		chan_info = &info.rx[0];
		xfer_type = TEGRA_PCIE_DMA_READ;
		/* TODO support abort test case for T264 */
		if (edma->chip_id == NVPCIE_DMA_SOC_T234)
			db_off = DMA_READ_DOORBELL_OFF;
		l_r = 1;
	}

	for (i = 0; i < chan_count; i++) {
		struct tegra_pcie_dma_chans_info *ch = chan_info + i;

		ch->ch_type = IS_EDMA_CH_ASYNC(i) ? TEGRA_PCIE_DMA_CHAN_XFER_ASYNC :
				TEGRA_PCIE_DMA_CHAN_XFER_SYNC;
		if (IS_EDMA_CH_ENABLED(i)) {
			if (edma->st_as_ch == -1)
				edma->st_as_ch = i;
			edma->ls_as_ch = i;
			ch->num_descriptors = num_descriptors;
			num_chans++;
		} else
			ch->num_descriptors = 0;
	}

	max_size = (BAR0_DMA_BUF_SIZE - BAR0_DMA_BUF_OFFSET) / 2;
	if (((edma->dma_size * nents) > max_size) || (nents > NUM_EDMA_DESC)) {
		dev_err(edma->fdev, "%s: max dma size including all nents(%d), max_nents(%d), dma_size(%d) should be <= 0x%x\n",
			__func__, nents, NUM_EDMA_DESC, edma->dma_size, max_size);
		return 0;
	}

	if (num_chans != 0)
		nents_per_ch = nents / num_chans;

	if (nents_per_ch == 0) {
		dev_err(edma->fdev, "%s: nents(%d) < enabled channels(%d)\n",
			__func__, nents, num_chans);
		return 0;
	}

	for (j = 0; j < nents; j++) {
		if (EDMA_READ_TEST_EN) {
			ll_desc->dst = src_dma_addr + (j * edma->dma_size);
			ll_desc->src = dst_dma_addr + (j * edma->dma_size);
		} else {
			ll_desc->src = src_dma_addr + (j * edma->dma_size);
			ll_desc->dst = dst_dma_addr + (j * edma->dma_size);
		}
		ll_desc->sz = edma->dma_size;
		ll_desc++;
	}
	ll_desc = edma->ll_desc;

	edma->tsz = (u64)edma->stress_count * (nents_per_ch) * (u64)edma->dma_size * 8UL;

	if (!edma->cookie || ((edma->prev_edma_ch & 0xFF) != (edma->edma_ch & 0xFF))) {
		dev_info(edma->fdev, "%s: re-init edma lib prev_ch(%x) != current chans(%x); edma cookie:%p\n",
			 __func__, edma->prev_edma_ch, edma->edma_ch, edma->cookie);
		ret = tegra_pcie_dma_initialize(&info, &edma->cookie);
		if (ret != TEGRA_PCIE_DMA_SUCCESS) {
			dev_info(edma->fdev, "%s: tegra_pcie_dma_initialize() fail: %d\n",
				 __func__, ret);
			return -1;
		}
		edma->prev_edma_ch = edma->edma_ch;

		if (edma->chip_id == NVPCIE_DMA_SOC_T264) {
			ret = tegra_pcie_dma_set_msi(edma->cookie, edma->msi_addr, edma->msi_data);
			if (ret != TEGRA_PCIE_DMA_SUCCESS) {
				dev_info(edma->fdev, "%s: tegra_pcie_dma_set_msi() fail: %d\n",
					 __func__, ret);
				return -1;
			}
		}
	}

	edma->nents_per_ch = nents_per_ch;

	/* generate random bytes to transfer */
	if (EDMA_SANITY_TEST_EN) {
		for (j = 0; j < num_descriptors; j++)
			memset((u8 *)edma->src_virt + (j * SZ_1K), j, SZ_1K);
	} else {
		get_random_bytes(edma->src_virt, edma->dma_size * nents_per_ch);
	}
	dev_info(edma->fdev, "%s: EDMA LIB %s started for %d chans, size %d Bytes, iterations: %d of descriptors %d\n",
		 __func__, xfer_str[xfer_type], num_chans, edma->dma_size, edma->stress_count,
		 nents_per_ch);

	/* LL DMA with size epfnv->dma_size per desc */
	for (i = 0; i < chan_count; i++) {
		int ch = i;
		struct tegra_pcie_dma_chans_info *ch_info = chan_info + i;

		if (ch_info->num_descriptors == 0)
			continue;

		edma->edma_start_time[i] = ktime_get();
		tx_info.desc = &ll_desc[nent_id++ * nents_per_ch];
		for (k = 0; k < edma->stress_count; k++) {
			tx_info.channel_num = ch;
			tx_info.type = xfer_type;
			tx_info.nents = nents_per_ch;
			if (ch_info->ch_type == TEGRA_PCIE_DMA_CHAN_XFER_ASYNC) {
				if (k == edma->stress_count - 1)
					tx_info.complete = edma_final_complete;
				else
					tx_info.complete = edma_complete;
			}
			edma->priv_iter[ch] = k | (((u64)xfer_type) << EDMA_PRIV_XF_OFF) |
						(((u64)l_r) << EDMA_PRIV_LR_OFF) |
						(((u64)ch) << EDMA_PRIV_CH_OFF);
			tx_info.priv = &edma->priv_iter[ch];
			/* Set second desc as 0 to trigger DECERR for T264 */
			if (DMA_ABORT_SWITS_TEST_EN && (k == 1) &&
				(edma->chip_id == NVPCIE_DMA_SOC_T264)) {
				dev_info(edma->fdev, "Configuring Src DMA as 0 to trigger DECERR\n");
				tx_info.desc[0].src = 0;
			}
			ret = tegra_pcie_dma_submit_xfer(edma->cookie, &tx_info);
			if (ret == TEGRA_PCIE_DMA_FAIL_NOMEM) {
				/** Retry after 20 msec */
				dev_dbg(edma->fdev, "%s: TEGRA_PCIE_DMA_FAIL_NOMEM stress count %d on channel %d iter %d\n",
					__func__, edma->stress_count, i, k);
				ret = wait_event_timeout(edma->wr_wq[i],
							 !(edma->wr_busy & (1 << i)),
							 msecs_to_jiffies(500));
				/* Do a more sleep to avoid repeated wait and wake calls */
				msleep(100);
				if (ret == 0) {
					dev_err(edma->fdev, "%s: %d timedout\n", __func__, i);
					ret = -ETIMEDOUT;
					goto fail;
				}
				k--;
				continue;
			} else if ((ret != TEGRA_PCIE_DMA_SUCCESS) &&
				   (ret != TEGRA_PCIE_DMA_FAIL_NOMEM)) {
				dev_err(edma->fdev, "%s: LL %d, SZ: %u B CH: %d failed. %d at iter %d ret: %d\n",
					__func__, xfer_type, edma->dma_size, ch, ret, k, ret);
				if (EDMA_STOP_TEST_EN) {
					break;
				} else if (EDMA_ABORT_TEST_EN) {
					msleep(5000);
					break;
				}
				goto fail;
			}
			dev_dbg(edma->fdev, "%s: LL EDMA LIB %d, SZ: %u B CH: %d iter %d\n",
				__func__, xfer_type, edma->dma_size, ch, i);
		}
		if (DMA_STOP_SWITS_TEST_EN) {
			bool stop_status;

			stop_status = tegra_pcie_dma_stop(edma->cookie);
			dev_info(edma->fdev, "%s: EDMA LIB, status of stop DMA is %d", __func__,
				 stop_status);
		}
		if (i == 2) {
			if (EDMA_ABORT_TEST_EN) {
				msleep(edma->stress_count);
				/* TODO support abort test case for T264 */
				if (edma->chip_id == NVPCIE_DMA_SOC_T234)
					dma_common_wr(edma->dma_virt,
						      DMA_WRITE_DOORBELL_OFF_WR_STOP, db_off);
			} else if (EDMA_STOP_TEST_EN) {
				bool stop_status;

				msleep(edma->stress_count);
				stop_status = tegra_pcie_dma_stop(edma->cookie);
				dev_info(edma->fdev, "%s: EDMA LIB, status of stop DMA is %d",
					 __func__, stop_status);
			}
		}
		diff = ktime_to_ns(ktime_get()) - ktime_to_ns(edma->edma_start_time[i]);
		if (ch_info->ch_type == TEGRA_PCIE_DMA_CHAN_XFER_SYNC) {
			if (ret == TEGRA_PCIE_DMA_SUCCESS)
				dev_info(edma->fdev, "%s: EDMA LIB %s-%s-SYNC done for %d iter on channel %d. Total Size %llu bytes, time %llu nsec. Perf is %llu Mbps\n",
					 __func__, xfer_str[xfer_type], l_r_str[l_r],
					 edma->stress_count, i, edma->tsz, diff, EDMA_PERF);
		}
	}


	if (EDMA_SANITY_TEST_EN)
		edma->raise_irq(edma->priv);

	if (EDMA_CRC_TEST_EN && !REMOTE_EDMA_TEST_EN) {
		edma->raise_irq(edma->priv);
		crc = crc32_le(~0, edma->src_virt + epf_bar->wr_data[0].src_offset,
			       epf_bar->wr_data[0].size);
		msleep(100);
		if (crc != epf_bar->wr_data[0].crc)
			dev_err(edma->fdev, "CRC check failed, LCRC: 0x%x RCRC: 0x%x\n",
				crc, epf_bar->wr_data[0].crc);
		else
			dev_err(edma->fdev, "CRC check pass\n");
	}

	dev_info(edma->fdev, "%s: return success, coockie %p\n", __func__, edma->cookie);
	return 0;
fail:
	if (ret != TEGRA_PCIE_DMA_DEINIT) {
		tegra_pcie_dma_deinit(&edma->cookie);
		edma->cookie = NULL;
	}
	return -1;
}

#endif /* TEGRA_PCIE_EDMA_TEST_COMMON_H */
