// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#include <nvidia/conftest.h>

/* Linux headers */
#include <linux/atomic.h>
#include <linux/types.h>
#include <linux/iommu.h>
#include <linux/dma-buf.h>

/* PVA headers */
#include "pva_kmd_linux_device.h"

struct pva_kmd_linux_smmu_ctx {
	struct platform_device *pdev;
	uint32_t sid;
};

static const struct of_device_id pva_kmd_linux_smmu_context_of_match_table[] = {
	{ .compatible = "nvidia,pva-tegra186-iommu-context" },
	{ .compatible = "nvidia,pva-tegra264-iommu-context" },
	{},

};

/*
 * SMMU contexts available to PVA SW to support user applications
 *
 * Note that we reserve one SMMU context for use by PVA KMD to load FW from GSC
 * Probing of the reserved SMMU context is not handled in this file
 */
static struct pva_kmd_linux_smmu_ctx g_smmu_ctxs[PVA_MAX_NUM_SMMU_CONTEXTS];
atomic_t g_num_smmu_ctxs = ATOMIC_INIT(0);
atomic_t g_num_smmu_probing_done = ATOMIC_INIT(0);
bool g_smmu_probing_done = false;

static int pva_kmd_device_get_sid(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	uint32_t sid;

	if (fwspec == NULL) {
		dev_err(&pdev->dev, "SMMU fwspec is NULL");
		return -ENOENT;
	}
	if (fwspec->num_ids == 0) {
		dev_err(&pdev->dev, "SMMU fwspec has no IDs");
		return -EINVAL;
	}

	sid = fwspec->ids[0] & 0xffff;
	return (int)sid;
}

static int pva_kmd_linux_device_smmu_context_probe(struct platform_device *pdev)
{
	int idx;
	int new_idx;
	int sid_or_err;

	if (!iommu_get_domain_for_dev(&pdev->dev)) {
		dev_err(&pdev->dev,
			"iommu is not enabled for context device. aborting.");
		return -ENOSYS;
	}

	/*
	 * Probers for multiple PVA SMMU context devices might be executing
	 * this routine at once. To avoid race conditions, every prober must
	 * must first increment the number of contexts probed atomically. This
	 * way, each prober will find a unique place in the g_smmu_ctxs to store
	 * its SMMU context information.
	 */
	new_idx = atomic_add_return(1, &g_num_smmu_ctxs) - 1;
	if (new_idx < 0 || new_idx >= PVA_MAX_NUM_SMMU_CONTEXTS) {
		atomic_dec(&g_num_smmu_ctxs);
		dev_err(&pdev->dev, "Invalid number of SMMU contexts: %d",
			new_idx);
		return -EINVAL;
	}
	idx = new_idx;

	g_smmu_ctxs[idx].pdev = pdev;
	sid_or_err = pva_kmd_device_get_sid(pdev);
	if (sid_or_err < 0) {
		dev_err(&pdev->dev, "Failed to get SID: %d", sid_or_err);
		atomic_dec(&g_num_smmu_ctxs);
		return sid_or_err;
	}
	g_smmu_ctxs[idx].sid = (uint32_t)sid_or_err;

	atomic_add(1, &g_num_smmu_probing_done);

	dev_info(&pdev->dev, "initialized (streamid=%u)", g_smmu_ctxs[idx].sid);
	return 0;
}

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void __exit
pva_kmd_linux_smmu_context_remove_wrapper(struct platform_device *pdev)
{
}
#else
static int __exit
pva_kmd_linux_smmu_context_remove_wrapper(struct platform_device *pdev)
{
	return 0;
}
#endif

bool pva_kmd_linux_smmu_contexts_initialized(enum pva_chip_id chip_id)
{
	/* Note: Instead of checking if g_num_smmu_ctxs has reached its maximum
	 *	value here, we do it in the probe function. This is because
	 *	when PVA device probe calls this method, the prober of the last
	 *	SMMU context device might have incremented g_num_smmu_ctxs but
	 *	might still not have updated g_smmu_ctxs.
	 */
	int max_num_smmu_ctx = (chip_id == PVA_CHIP_T26X) ?
					     PVA_NUM_SMMU_CONTEXTS_T26X :
					     PVA_NUM_SMMU_CONTEXTS_T23X;

	(void)chip_id;
	// TODO: When multiple VMs are running, each VM will have less than
	//	PVA_MAX_NUM_SMMU_CONTEXTS contexts. Hence the following logic
	//	would be incorrect for such cases. Should be fixed.
	if (atomic_read(&g_num_smmu_probing_done) == (max_num_smmu_ctx - 1))
		g_smmu_probing_done = true;

	return g_smmu_probing_done;
}

void pva_kmd_linux_device_smmu_contexts_init(struct pva_kmd_device *pva_device)
{
	uint32_t sid_idx;
	struct device *dev;
	struct pva_kmd_linux_device_data *device_data =
		pva_kmd_linux_device_get_data(pva_device);

	if (!g_smmu_probing_done)
		FAULT("SMMU contexts init called before all contexts were probed");

	/* Configure SMMU contexts for unprivileged operations */
	/* PVA cluster will access these regions only using DMA */
	for (sid_idx = 0U;
	     sid_idx < safe_subu32(pva_device->hw_consts.n_smmu_contexts, 2U);
	     sid_idx++) {
		uint32_t smmu_ctx_idx = safe_addu32(sid_idx, 1U);
		struct pva_kmd_linux_smmu_ctx *smmu_ctx = &g_smmu_ctxs[sid_idx];

		dev = &smmu_ctx->pdev->dev;
		pva_device->stream_ids[smmu_ctx_idx] = smmu_ctx->sid;
		device_data->smmu_contexts[smmu_ctx_idx] = smmu_ctx->pdev;
		dma_set_mask_and_coherent(dev, DMA_BIT_MASK(39));
		//set max segment size to UINT_MAX to avoid creating scatterlist >= 4GB
		//during IOVA mapping, which will overflow the scatterlist length field,
		//causing IOVA leak
		dma_set_max_seg_size(dev, UINT_MAX);
	}

	/* Configure SMMU contexts for privileged operations */
	/* PVA cluster may access this region directly (without DMA) */
	/* The last one is PRIV SID */
	// TODO - if context devices are not enumerated in the order
	//      in which they appear in the device tree, the last
	//      ctx in g_smmu_ctxs may not be the last SID assigned to PVA
	//      Question: Is it necessary that priv SID is the last one?
	pva_device->stream_ids[0] = g_smmu_ctxs[sid_idx].sid;
	device_data->smmu_contexts[0] = g_smmu_ctxs[sid_idx].pdev;
	dev = &device_data->smmu_contexts[0]->dev;
	dma_set_mask_and_coherent(
		dev, DMA_BIT_MASK(31)); //only 2GB R5 space is accessible
	dma_set_max_seg_size(dev, UINT_MAX);
}

struct platform_driver pva_kmd_linux_smmu_context_driver = {
	.probe = pva_kmd_linux_device_smmu_context_probe,
	.remove = __exit_p(pva_kmd_linux_smmu_context_remove_wrapper),
	.driver = {
		.owner = THIS_MODULE,
		.name = "pva_iommu_context_dev",
#ifdef CONFIG_OF
		.of_match_table = pva_kmd_linux_smmu_context_of_match_table,
#endif
	},
};
