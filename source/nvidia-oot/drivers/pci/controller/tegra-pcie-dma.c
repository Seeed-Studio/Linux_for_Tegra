// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved. */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/tegra-pcie-dma.h>

#include "tegra-pcie-dma-irq.h"
#include "tegra234-pcie-edma-osi.h"
#include "tegra264-pcie-xdma-osi.h"

struct tegra_pcie_dma_priv {
	/** Store SoC specific private data pointer. */
	void *soc_cookie;
	nvpcie_dma_soc_t soc;
};

irqreturn_t tegra_pcie_dma_irq(int irq, void *cookie)
{
	struct tegra_pcie_dma_priv *prv = (struct tegra_pcie_dma_priv *)cookie;
	irqreturn_t ret;

	if (prv->soc == NVPCIE_DMA_SOC_T234)
		ret = edma_irq(irq, prv->soc_cookie);
	else if (prv->soc == NVPCIE_DMA_SOC_T264)
		ret = xdma_irq(irq, prv->soc_cookie);
	else
		ret = IRQ_WAKE_THREAD;

	return ret;
}

irqreturn_t tegra_pcie_dma_irq_handler(int irq, void *cookie)
{
	struct tegra_pcie_dma_priv *prv = (struct tegra_pcie_dma_priv *)cookie;
	irqreturn_t ret;

	if (prv->soc == NVPCIE_DMA_SOC_T234)
		ret = edma_irq_handler(irq, prv->soc_cookie);
	else if (prv->soc == NVPCIE_DMA_SOC_T264)
		ret = xdma_irq_handler(irq, prv->soc_cookie);
	else
		ret = IRQ_HANDLED;

	return ret;
}

tegra_pcie_dma_status_t tegra_pcie_dma_initialize(struct tegra_pcie_dma_init_info *info,
						  void **cookie)
{
	struct tegra_pcie_dma_priv *prv;

	if (!info || !cookie) {
		pr_err("%s: NULL info or cookie param\n", __func__);
		return TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS;
	}

	prv = kzalloc(sizeof(*prv), GFP_KERNEL);
	if (!prv) {
		pr_err("Failed to allocate memory for dma_prv\n");
		return TEGRA_PCIE_DMA_FAIL_NOMEM;
	}

	prv->soc = info->soc;
	if (info->soc == NVPCIE_DMA_SOC_T234) {
		prv->soc_cookie = tegra234_pcie_edma_initialize(info, prv);
		if (!prv->soc_cookie) {
			kfree(prv);
			return TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS;
		}
		*cookie = prv;
		return TEGRA_PCIE_DMA_SUCCESS;
	} else if (info->soc == NVPCIE_DMA_SOC_T264) {
		prv->soc_cookie = tegra264_pcie_xdma_initialize(info, prv);
		if (!prv->soc_cookie) {
			kfree(prv);
			return TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS;
		}
		*cookie = prv;
		return TEGRA_PCIE_DMA_SUCCESS;
	} else {
		pr_err("%s: invalid soc id: %d\n", __func__, info->soc);
		kfree(prv);
		return TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS;
	}
}
EXPORT_SYMBOL_GPL(tegra_pcie_dma_initialize);
tegra_pcie_dma_status_t tegra_pcie_dma_set_msi(void *cookie, u64 msi_addr, u32 msi_data)
{
	struct tegra_pcie_dma_priv *prv = (struct tegra_pcie_dma_priv *)cookie;

	if (!cookie) {
		pr_err("%s: cookie is null\n", __func__);
		return TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS;
	}

	if (prv->soc == NVPCIE_DMA_SOC_T234) {
		/** This is not supported for T234 SoC. */
		return TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS;
	} else if (prv->soc == NVPCIE_DMA_SOC_T264) {
		return tegra264_pcie_xdma_set_msi(prv->soc_cookie, msi_addr, msi_data);
	} else {
		pr_err("%s: invalid soc id: %d\n", __func__, prv->soc);
		return TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS;
	}
}
EXPORT_SYMBOL_GPL(tegra_pcie_dma_set_msi);

tegra_pcie_dma_status_t tegra_pcie_dma_submit_xfer(void *cookie,
						   struct tegra_pcie_dma_xfer_info *tx_info)
{
	struct tegra_pcie_dma_priv *prv = (struct tegra_pcie_dma_priv *)cookie;

	if (!cookie) {
		pr_err("%s: cookie is null\n", __func__);
		return TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS;
	}

	if (prv->soc == NVPCIE_DMA_SOC_T234) {
		return tegra234_pcie_edma_submit_xfer(prv->soc_cookie, tx_info);
	} else if (prv->soc == NVPCIE_DMA_SOC_T264) {
		return tegra264_pcie_xdma_submit_xfer(prv->soc_cookie, tx_info);
	} else {
		pr_err("%s: invalid soc id: %d\n", __func__, prv->soc);
		return TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS;
	}
}
EXPORT_SYMBOL_GPL(tegra_pcie_dma_submit_xfer);

bool tegra_pcie_dma_stop(void *cookie)
{
	struct tegra_pcie_dma_priv *prv = (struct tegra_pcie_dma_priv *)cookie;
	bool st = false;

	if (!cookie) {
		pr_err("%s: cookie is null\n", __func__);
		goto end;
	}

	if (prv->soc == NVPCIE_DMA_SOC_T234) {
		st = tegra234_pcie_edma_stop(prv->soc_cookie);
	} else if (prv->soc == NVPCIE_DMA_SOC_T264) {
		st = tegra264_pcie_xdma_stop(prv->soc_cookie);
	} else {
		pr_err("%s: invalid soc id: %d\n", __func__, prv->soc);
	}

end:
	return st;
}
EXPORT_SYMBOL_GPL(tegra_pcie_dma_stop);

tegra_pcie_dma_status_t tegra_pcie_dma_deinit(void **cookie)
{
	struct tegra_pcie_dma_priv *prv;

	if (!cookie) {
		pr_err("%s: cookie is null\n", __func__);
		return TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS;
	}

	prv = (struct tegra_pcie_dma_priv *)(*cookie);
	if (!prv) {
		pr_err("%s: prv pointer is null\n", __func__);
		return TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS;
	}
	*cookie = NULL;
	if (prv->soc == NVPCIE_DMA_SOC_T234) {
		tegra234_pcie_edma_deinit(prv->soc_cookie, prv);
		kfree(prv);
		return TEGRA_PCIE_DMA_SUCCESS;
	} else if (prv->soc == NVPCIE_DMA_SOC_T264) {
		tegra264_pcie_xdma_deinit(prv->soc_cookie, prv);
		kfree(prv);
		return TEGRA_PCIE_DMA_SUCCESS;
	} else {
		pr_err("%s: invalid soc id: %d\n", __func__, prv->soc);
		return TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS;
	}
}
EXPORT_SYMBOL_GPL(tegra_pcie_dma_deinit);

MODULE_LICENSE("GPL v2");
