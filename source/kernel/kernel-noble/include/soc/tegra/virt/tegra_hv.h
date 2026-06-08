/*
 * SPDX-License-Identifier: GPL-2.0-only
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef __TEGRA_HV_H__
#define __TEGRA_HV_H__

#include <soc/tegra/virt/syscalls.h>

#define SUPPORTS_TRAP_MSI_NOTIFICATION

#define IVC_INFO_PAGE_SIZE 65536

const struct ivc_info_page *tegra_hv_get_ivc_info(void);
int tegra_hv_get_vmid(void);

#endif /* __TEGRA_HV_H__ */
