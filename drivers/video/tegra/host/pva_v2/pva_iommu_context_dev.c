// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * PVA Application Specific Virtual Memory
 */

#include <nvidia/conftest.h>

#include <linux/dma-mapping.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/iommu.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/version.h>
#include <linux/dma-buf.h>
#include <linux/nvhost.h>
#include <linux/platform_device.h>

#include "pva_iommu_context_dev.h"
#include "pva.h"

#define NVPVA_CNTXT_DEV_NAME_LEN_T23X	(29U)
#define NVPVA_CNTXT_DEVICE_CNT		(8U)

#ifdef CONFIG_TEGRA_T26X_GRHOST_PVA
#include "pva_iommu_context_dev_t264.h"
#else
#define NVPVA_CNTXT_DEV_NAME_LEN NVPVA_CNTXT_DEV_NAME_LEN_T23X
#define NVPVA_CNTXT_DEVICE_CNT_T264 NVPVA_CNTXT_DEVICE_CNT
#endif

static u32 cntxt_dev_count;
static char *dev_names[] = {
	"pva0_niso1_ctx0",
	"pva0_niso1_ctx1",
	"pva0_niso1_ctx2",
	"pva0_niso1_ctx3",
	"pva0_niso1_ctx4",
	"pva0_niso1_ctx5",
	"pva0_niso1_ctx6",
	"pva0_niso1_ctx7",
	"pva0_niso1_ctx8",
};

#define PVA_HW_DONT_CARE	(0)

struct nvpva_ctx_device_data {
	u32	version;
	u32	pva_cntxt_dev_cnt;
	u32	pva_cntxt_dev_name_len;
	u32	aux_dev_idx;
};

static struct nvpva_ctx_device_data told_ctx_dev_info = {
	.version		= PVA_HW_GEN2,
	.pva_cntxt_dev_cnt	= 8,
	.pva_cntxt_dev_name_len	= 29,
	.aux_dev_idx 		= 7,
};

static struct nvpva_ctx_device_data t264_ctx_dev_info = {
	.version = PVA_HW_GEN3,
	.pva_cntxt_dev_cnt	= 9,
	.pva_cntxt_dev_name_len	= 31,
	.aux_dev_idx 		= 8,
};

static u32 pva_cntxt_dev_cnt[4] = {0, 0, 8, 9};

static struct of_device_id pva_iommu_context_dev_of_match[] = {
	{
		.compatible = "nvidia,pva-tegra186-iommu-context",
		.data = (struct nvpva_ctx_device_data *)&told_ctx_dev_info
	},
	{
		.compatible = "nvidia,pva-tegra264-iommu-context",
		.data = (struct nvpva_ctx_device_data *)&t264_ctx_dev_info
	},
	{},
};

struct pva_iommu_ctx {
	struct platform_device		*pdev;
	struct list_head		list;
	struct device_dma_parameters	dma_parms;
	u32				ref_count;
	bool				allocated;
	bool				shared;
};

static LIST_HEAD(pva_iommu_ctx_list);
static DEFINE_MUTEX(pva_iommu_ctx_list_mutex);
static u32 pva_cntxt_dev_name_len = 0;
static u32 aux_dev_idx = 0;
static u32 aux_dev_idex_name_len = 0;

bool is_cntxt_initialized(const int hw_gen)
{
	return (cntxt_dev_count == pva_cntxt_dev_cnt[hw_gen]);
}

int nvpva_iommu_context_dev_get_sids(int *hwids, int *count, const int hw_gen)
{
	struct pva_iommu_ctx *ctx;
	int err = 0;
	int i;

	*count = 0;
	mutex_lock(&pva_iommu_ctx_list_mutex);

	for (i = 0; i < pva_cntxt_dev_cnt[hw_gen]; i++) {
		list_for_each_entry(ctx, &pva_iommu_ctx_list, list) {
			if (strnstr(ctx->pdev->name, dev_names[i],
			 pva_cntxt_dev_name_len) != NULL) {
				hwids[*count] = nvpva_get_device_hwid(ctx->pdev, 0);
				if (hwids[*count] < 0) {
					err = hwids[*count];
					break;
				}

				++(*count);
				if (*count >= pva_cntxt_dev_cnt[hw_gen])
					break;
			}
		}
	}

	mutex_unlock(&pva_iommu_ctx_list_mutex);

	return err;
}

struct platform_device
*nvpva_iommu_context_dev_allocate(char *identifier, size_t len, bool shared)
{
	struct pva_iommu_ctx *ctx;
	struct pva_iommu_ctx *ctx_new = NULL;

	mutex_lock(&pva_iommu_ctx_list_mutex);

	if (identifier == NULL) {
		list_for_each_entry(ctx, &pva_iommu_ctx_list, list)
			if (!ctx->allocated && !ctx_new)
				ctx_new = ctx;
		if (!ctx_new && shared)
			list_for_each_entry(ctx, &pva_iommu_ctx_list, list)
				if ((!ctx->allocated || ctx->shared) && !ctx_new)
					ctx_new = ctx;
	} else {
		list_for_each_entry(ctx, &pva_iommu_ctx_list, list)
			if (!ctx_new
			    && (strncmp(ctx->pdev->name, identifier, len) == 0))
				ctx_new = ctx;

		if (ctx_new && !shared && ctx_new->allocated)
			ctx_new = NULL;

		if (ctx_new && shared && (ctx_new->allocated && !ctx_new->shared))
			ctx_new = NULL;
	}

	if (ctx_new) {
#ifdef CONFIG_NVMAP
		/*
		 * Ensure that all stashed mappings are removed from this context device
		 * before this context device gets reassigned to some other process
		 */
		dma_buf_release_stash(&ctx_new->pdev->dev);
#endif
		ctx_new->allocated = true;
		ctx_new->shared = shared;
		ctx_new->ref_count += 1;
		mutex_unlock(&pva_iommu_ctx_list_mutex);
		return ctx_new->pdev;
	}

	mutex_unlock(&pva_iommu_ctx_list_mutex);

	return NULL;
}

void nvpva_iommu_context_dev_release(struct platform_device *pdev)
{
	struct pva_iommu_ctx *ctx;

	if (pdev == NULL)
		return;

	ctx = platform_get_drvdata(pdev);
	mutex_lock(&pva_iommu_ctx_list_mutex);
	ctx->ref_count -= 1;
	if (ctx->ref_count == 0) {
		ctx->allocated = false;
		ctx->shared = false;
	}

	mutex_unlock(&pva_iommu_ctx_list_mutex);
}

static int pva_iommu_context_dev_probe(struct platform_device *pdev)
{
	struct pva_iommu_ctx *ctx;
	struct device *dev = &pdev->dev;
	struct nvpva_ctx_device_data *pdata;
	const struct of_device_id *match;
	int err = 0;

	match = of_match_device(pva_iommu_context_dev_of_match, dev);
	if (!match) {
		dev_err(dev, "no match for pva ctx dev dev\n");
		err = -ENODATA;
		goto err_get_pdata;
	}

	pdata = (struct nvpva_ctx_device_data *)match->data;
	WARN_ON(!pdata);
	if (!pdata) {
		dev_info(dev, "no platform data\n");
		err = -ENODATA;
		goto err_get_pdata;
	}

	aux_dev_idx = pdata->aux_dev_idx;
	aux_dev_idex_name_len = pdata->pva_cntxt_dev_name_len;
	pva_cntxt_dev_name_len = pdata->pva_cntxt_dev_name_len;

	if (!iommu_get_domain_for_dev(dev)) {
		dev_err(dev,
			"iommu is not enabled for context device. aborting.");
		return -ENOSYS;
	}

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		dev_err(dev,
			   "%s: could not allocate iommu ctx\n", __func__);
		return -ENOMEM;
	}

	if (strnstr(pdev->name, dev_names[aux_dev_idx], aux_dev_idex_name_len ) != NULL)
		dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	else
		dma_set_mask_and_coherent(dev, DMA_BIT_MASK(39));

	INIT_LIST_HEAD(&ctx->list);
	ctx->pdev = pdev;

	mutex_lock(&pva_iommu_ctx_list_mutex);
	list_add_tail(&ctx->list, &pva_iommu_ctx_list);
	cntxt_dev_count++;
	mutex_unlock(&pva_iommu_ctx_list_mutex);

	platform_set_drvdata(pdev, ctx);

	pdev->dev.dma_parms = &ctx->dma_parms;
	dma_set_max_seg_size(dev, UINT_MAX);

#ifdef CONFIG_NVMAP
	/* flag required to handle stashings in context devices */
	pdev->dev.context_dev = true;
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 0, 0)
	dev_info(dev, "initialized (streamid=%d, iommu=%s)",
		 nvpva_get_device_hwid(pdev, 0),
		 dev_name(pdev->dev.iommu->iommu_dev->dev));
#else
	dev_info(dev, "initialized (streamid=%d)",
		 nvpva_get_device_hwid(pdev, 0));
#endif

err_get_pdata:

	return err;
}

static int __exit pva_iommu_context_dev_remove(struct platform_device *pdev)
{
	struct pva_iommu_ctx *ctx = platform_get_drvdata(pdev);

	mutex_lock(&pva_iommu_ctx_list_mutex);
	list_del(&ctx->list);
	mutex_unlock(&pva_iommu_ctx_list_mutex);

	return 0;
}

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void __exit pva_iommu_context_dev_remove_wrapper(struct platform_device *pdev)
{
	pva_iommu_context_dev_remove(pdev);
}
#else
static int __exit pva_iommu_context_dev_remove_wrapper(struct platform_device *pdev)
{
	return pva_iommu_context_dev_remove(pdev);
}
#endif

struct platform_driver nvpva_iommu_context_dev_driver = {
	.probe = pva_iommu_context_dev_probe,
	.remove = __exit_p(pva_iommu_context_dev_remove_wrapper),
	.driver = {
		.owner = THIS_MODULE,
		.name = "pva_iommu_context_dev",
#ifdef CONFIG_OF
		.of_match_table = pva_iommu_context_dev_of_match,
#endif
	},
};

