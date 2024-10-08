// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef TEGRA_PCIE_DMA_SANITY_HELPERS_H
#define TEGRA_PCIE_DMA_SANITY_HELPERS_H

#include <linux/tegra-pcie-dma.h>

#define TEGRA264_PCIE_DMA_MSI_CRC_VEC (TEGRA264_PCIE_DMA_MSI_REMOTE_VEC + 1U)

#define EDMA_PERF (edma->dma_size * edma->nents * 8UL / (diff / 1000))

#define NUM_EDMA_DESC			16

/* Update DMA_DD_BUF_SIZE and DMA_LL_BUF_SIZE when changing BAR0_SIZE */
#define BAR0_SIZE               SZ_256M

/* DMA'able memory range */
#define BAR0_HEADER_SIZE	SZ_1M
#define BAR0_DMA_BUF_OFFSET	BAR0_HEADER_SIZE
#define BAR0_DMA_BUF_SIZE	(BAR0_SIZE - BAR0_DMA_BUF_OFFSET)

/* First 1MB of BAR0 is reserved for control data */
struct pcie_epf_bar {
	/* RP system memory allocated for EP DMA operations */
	u64 rp_phy_addr;
	/* EP system memory allocated as BAR */
	u64 ep_phy_addr;
};

struct edmalib_sanity {
	void *priv;
	struct tegra_pcie_dma_desc *ll_desc;
	void *cookie;

	void *src_virt;
	void *dst_virt;

	dma_addr_t src_dma_addr;
	dma_addr_t dst_dma_addr;

	struct device *fdev;
	struct device *cdev;

	u64 priv_iter;
	nvpcie_dma_soc_t chip_id;
	ktime_t edma_start_time;

	u64 msi_addr;
	u32 msi_data;
	u32 msi_irq;

	/* Testing parameters */
	u32 dma_size;
	tegra_pcie_dma_chan_type_t edma_ch_type;
	u32 nents;
	u32 stress_count;
	bool deinit_dma;

	/* Channel parameters */
	u32 success_xfer_count;
	u32 abort_timeout_nomem_xfer_count;
	u32 other_xfer_fail_count;
	struct completion channel_completion;
};
static struct edmalib_sanity *l_edma;

/**
 *  @brief This function does the CPU comparison for a local READ on the EP
 *
 *  @param[in] edma contains all info about the edma test
 *				(including source addr, destination addr, size of transfer)
 *  @param[in] priv contains information about iteration number (for address offset)
 *
 * @return
 * - count of incorrectly transferred bits
 */
static u64 cpu_verification(struct edmalib_sanity *edma, void *priv)
{
	u64 cb = *(u64 *)priv;
	/* Iteration number which helps with the src and dst addr */
	u64 it_num = cb & 0xFFFF;
	u64 total_fails = 0, i, j, total_size = edma->dma_size * edma->nents, tmp_diff;
	u64 *tmp_src = edma->src_virt + (it_num * total_size);
	u64 *tmp_dst = edma->dst_virt + (it_num * total_size);

	/* Loop to check bits using CPU */
	for (i = 0; i < total_size; i += sizeof(u64)) {
		if ((*tmp_src) != (*tmp_dst)) {
			tmp_diff = (*tmp_src) ^ (*tmp_dst);
			for (j = 0; j < 64; j++) {
				total_fails += tmp_diff & 1;
				tmp_diff >>= 1;
			}
		}
		/* Updating the pointer */
		tmp_src += 1;
		tmp_dst += 1;
	}
	return total_fails;
}

/**
 * @brief This function prints the perf of the iteration and the CPU verification result
 *
 * @param[in] edma - contains all info about the edma test, status - returns status of DMA engine
 */
static void edma_sanity_complete(void *priv, tegra_pcie_dma_status_t status)
{
	struct edmalib_sanity *edma = l_edma;
	u64 cb = *(u64 *)priv;
	/* Iteration number which helps decide if the callback status counts to be printed */
	u64 it_num = cb & 0xFFFF;
	/* Time taken for the transfer */
	u64 diff = ktime_to_ns(ktime_get()) - ktime_to_ns(edma->edma_start_time);
	u64 total_fail_bits = 0;

	/* Updating counts of the status for the callbacks */
	if (status == TEGRA_PCIE_DMA_SUCCESS)
		edma->success_xfer_count++;
	else {
		if ((status == TEGRA_PCIE_DMA_ABORT) ||
				(status == TEGRA_PCIE_DMA_FAIL_TIMEOUT) ||
				(status == TEGRA_PCIE_DMA_FAIL_NOMEM)) {
			edma->abort_timeout_nomem_xfer_count++;
		} else
			edma->other_xfer_fail_count++;
	}

	/* CPU verification */
	total_fail_bits = cpu_verification(edma, priv);
	/* Print the performance and other stats of the iteration */
	dev_info(edma->fdev, "%s: WR-Async iteration %lld | Channel 0 | %d desc of Sz %uKB each | Bit-error-count: %llu | Perf: %llu Mbps | Time-taken: %llu ns |\n",
			__func__, it_num, edma->nents, edma->dma_size / SZ_1K,
			total_fail_bits, EDMA_PERF, diff);

	/* Printing the statistics of status to callbacks if last iteration */
	if (it_num == (edma->stress_count - 1))
		dev_info(edma->fdev, "%s: successes=%d | abort/timeout/nomem=%d | other failiures=%d |\n",
				__func__, edma->success_xfer_count,
				edma->abort_timeout_nomem_xfer_count, edma->other_xfer_fail_count);

	/* Complete the iteration */
	complete(&edma->channel_completion);
}
/**
 * @brief This function performs the testing itself. Switch on 1 channel in SYNC/ASYNC mode
 *
 * @param[in] edma - contains all info about the edma test
 *
 * @retVal EDMA_XFER_SUCCESS, EDMA_XFER_FAIL_INVAL_INPUTS, EDMA_XFER_FAIL_NOMEM
 * EDMA_XFER_FAIL_TIMEOUT, EDMA_XFER_ABORT, EDMA_XFER_DEINIT
 */
static int edmalib_sanity_tester(struct edmalib_sanity *edma)
{
	u32 j, k, max_size;
	tegra_pcie_dma_status_t ret;
	struct tegra_pcie_dma_init_info info = {};
	struct tegra_pcie_dma_chans_info *chan_info;
	struct tegra_pcie_dma_xfer_info tx_info = {};
	struct tegra_pcie_dma_desc *ll_desc = edma->ll_desc;

	l_edma = edma;

	info.dev = edma->cdev;
	info.soc = edma->chip_id;

	chan_info = &info.tx[0];

	info.msi_irq = edma->msi_irq;
	info.msi_data = edma->msi_data;
	info.msi_addr = edma->msi_addr;

	/* Setting up the channels */
	chan_info->num_descriptors = 16;
	chan_info->ch_type = edma->edma_ch_type;

	/* The src/dst addresses should not exceed the accessible memory */
	max_size = (BAR0_DMA_BUF_SIZE - BAR0_DMA_BUF_OFFSET);
	if (((edma->dma_size * edma->nents) > max_size)) {
		dev_err(edma->fdev, "%s: max dma size including all nents(%d), max_nents(%d), dma_size(%d) should be <= 0x%x\n",
				__func__, edma->nents, NUM_EDMA_DESC, edma->dma_size, max_size);
		return 0;
	}

	if (!edma->cookie) {
		ret = tegra_pcie_dma_initialize(&info, &edma->cookie);
		if (ret != TEGRA_PCIE_DMA_SUCCESS) {
			dev_err(edma->fdev, "%s: tegra_pcie_dma_initialize() fail: %d\n",
					__func__, ret);
			return -1;
		}
		/* Since this chip uses MSI interrupts for DMA: */
		if (edma->chip_id == NVPCIE_DMA_SOC_T264) {
			ret = tegra_pcie_dma_set_msi(edma->cookie, edma->msi_addr, edma->msi_data);
			if (ret != TEGRA_PCIE_DMA_SUCCESS) {
				dev_err(edma->fdev, "%s: tegra_pcie_dma_set_msi() fail: %d\n",
						__func__, ret);
				return -1;
			}
		}
	}

	/* generate random bytes to transfer */
	get_random_bytes(edma->src_virt, edma->dma_size * edma->nents * edma->stress_count);
	dev_info(edma->fdev, "%s: EDMA LIB WR started for %d chans, size %d Bytes, iterations: %d of descriptors %d\n",
			__func__, 1, edma->dma_size, edma->stress_count,
			edma->nents);

	/* Initialize the completion variable */
	init_completion(&edma->channel_completion);

	/* Refresh channel parameters */
	edma->success_xfer_count = 0;
	edma->abort_timeout_nomem_xfer_count = 0;
	edma->other_xfer_fail_count = 0;

	tx_info.desc = edma->ll_desc;
	for (k = 0; k < edma->stress_count; k++) {
		/* Populate the src and dst addresses, transfer size */
		for (j = 0; j < edma->nents; j++) {
			/* Update by j dma_size and k tsz */
			ll_desc->src = edma->src_dma_addr + ((j + (k * edma->nents)) *
					edma->dma_size);
			ll_desc->dst = edma->dst_dma_addr + ((j + (k * edma->nents)) *
					edma->dma_size);
			ll_desc->sz = edma->dma_size;
			ll_desc++;
		}
		ll_desc = edma->ll_desc;

		tx_info.channel_num = 0;
		tx_info.type = TEGRA_PCIE_DMA_WRITE;
		tx_info.nents = edma->nents;
		tx_info.complete = edma_sanity_complete;

		edma->priv_iter = k;
		tx_info.priv = &edma->priv_iter;

		edma->edma_start_time = ktime_get();
		ret = tegra_pcie_dma_submit_xfer(edma->cookie, &tx_info);
		wait_for_completion_timeout(&(edma->channel_completion),
				msecs_to_jiffies(edma->dma_size
					* 1000));
		if (ret) {
			dev_err(edma->fdev, "%s: Submission error at iteration = %d | error code: %d |\n",
					__func__, k, ret);
			return ret;
		}
	}
	if (edma->deinit_dma) {
		tegra_pcie_dma_deinit(&edma->cookie);
		edma->cookie = NULL;
	}
	return 0;
}
#endif
