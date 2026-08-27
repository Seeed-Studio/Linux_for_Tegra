/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Tegra Graphics Host NVCSI
 *
 * Copyright (c) 2015-2022 NVIDIA Corporation.  All rights reserved.
 */

#ifndef __NVHOST_NVCSI_H__
#define __NVHOST_NVCSI_H__

#define CFG_ERR_STATUS2VI_MASK_VC3			(0x1 << 24)
#define CFG_ERR_STATUS2VI_MASK_VC2			(0x1 << 16)
#define CFG_ERR_STATUS2VI_MASK_VC1			(0x1 << 8)
#define CFG_ERR_STATUS2VI_MASK_VC0			(0x1 << 0)

extern const struct file_operations tegra_nvcsi_ctrl_ops;

int nvcsi_finalize_poweron(struct platform_device *pdev);
int nvcsi_prepare_poweroff(struct platform_device *pdev);

int nvcsi_cil_sw_reset(int lanes, int enable);
struct tegra_csi_device *tegra_get_mc_csi(void);
#endif
