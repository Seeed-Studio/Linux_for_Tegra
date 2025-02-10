/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
 *
 * Host1x Application Specific Virtual Memory
 */

#ifndef IOMMU_CONTEXT_DEV_H
#define IOMMU_CONTEXT_DEV_H

struct platform_device
*nvpva_iommu_context_dev_allocate(char *identifier, size_t len, bool shared);
void nvpva_iommu_context_dev_release(struct platform_device *pdev);
int nvpva_iommu_context_dev_get_sids(int *hwids, int *count, const int hw_gen);
bool is_cntxt_initialized(const int hw_gen);

#endif
