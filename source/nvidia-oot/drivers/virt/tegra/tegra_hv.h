/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef __TEGRA_HV_H__
#define __TEGRA_HV_H__

#include <soc/tegra/virt/syscalls.h>

#define SUPPORTS_TRAP_MSI_NOTIFICATION

#define IVC_INFO_PAGE_SIZE 65536

/** @brief Maximum Guest VM count */
#define MAX_NUM_GUESTS		16U
/** @brief The maximum number of IVC queues supported by the PCT. */
#define PCT_MAX_NUM_IVC_QUEUES	512U
/** @brief The maximum number of mempools supported by the PCT. */
#define PCT_MAX_NUM_MEMPOOLS	120U

const struct ivc_info_page *tegra_hv_get_ivc_info(void);
int tegra_hv_get_vmid(void);

#endif /* __TEGRA_HV_H__ */
