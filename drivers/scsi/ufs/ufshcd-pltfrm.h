/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2015-2023, The Linux Foundation. All rights reserved. */
/* SPDX-FileCopyrightText: Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES.  All rights reserved. */
/*
 * This file is derived from the Linux kernel's drivers/ufs/host/ufshcd-pltfrm.h
 * header file, but only the necessary prototype used by the Tegra UFS driver
 * is copied. This function was first introduced in Linux v4.4 and was modified
 * in Linux v6.1 to make the 'vops' argument const. Hence, this prototype is
 * compatible with kernels from Linux v6.1 upto Linux v6.15. Linux kernels newer
 * than v6.15 need to be verified.
 */

#ifndef UFSHCD_PLTFRM_H_
#define UFSHCD_PLTFRM_H_

#include <ufs/ufshcd.h>

int ufshcd_pltfrm_init(struct platform_device *pdev,
		       const struct ufs_hba_variant_ops *vops);

#endif /* UFSHCD_PLTFRM_H_ */
