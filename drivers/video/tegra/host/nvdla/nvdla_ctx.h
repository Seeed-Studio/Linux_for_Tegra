/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * NVDLA context driver
 */

#ifndef __NVDLA_CTX_H__
#define __NVDLA_CTX_H__

#include "port/nvdla_host_wrapper.h"

/**
 * nvdla_flcn_ctx_tx_isr() falcon context MNOC interrupt handler
 *
 * @pdev        Pointer for platform device
 *
 * Return       0 on success otherwise negative
 *
 * This function called from nvhost falcon subsystem on receiving falcon
 * interrupt for a MNOC response from a context, like INT_ON_COMPLETE,
 * INT_ON_ERR, DLA_DEBUG etc.
 */
int nvdla_flcn_ctx_tx_isr(struct platform_device *pdev);

#if defined(NVDLA_HAVE_CONFIG_AXI) && (NVDLA_HAVE_CONFIG_AXI == 1)
extern struct nvhost_device_data t25x_nvdla0_ctx0_info;
extern struct nvhost_device_data t25x_nvdla0_ctx1_info;
extern struct nvhost_device_data t25x_nvdla0_ctx2_info;
extern struct nvhost_device_data t25x_nvdla0_ctx3_info;
#endif // NVDLA_HAVE_CONFIG_AXI
extern struct platform_driver nvdla_ctx_driver;

#endif /* End of __NVDLA_CTX_H__ */
