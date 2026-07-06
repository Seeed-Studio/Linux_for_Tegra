/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 */

#ifndef _NVVSE_LINUX_COMMON_H_
#define _NVVSE_LINUX_COMMON_H_

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/types.h>

#define NVVSE_ERR(...) pr_err("nvvse " __VA_ARGS__)
#define NVVSE_INFO(...) pr_info("nvvse " __VA_ARGS__)
#define NVOS_COV_WHITELIST(...)

#define EOK 0

#ifndef UINT64_MAX
#define UINT64_MAX 0xffffffffffffffffU
#endif

#ifndef UINT32_MAX
#define UINT32_MAX 0xffffffffU
#endif

/* WAR to define MAX_NUM_BUF_TSEC_BATCH as it is used in QNX NVVSE driver */
#define MAX_NUM_BUF_TSEC_BATCH 1

/* WAR to define ERR codes as it is used in QNX NVVSE driver */
#define ERR_INT30_C_ADDU64 0x00000000
#define ERR_INT30_C_ADDU32 0x00000001
#define ERR_INT30_C_MULTU32 0x00000002
#define ERR_INT30_C_MULTU64 0x00000003
#define ERR_INT30_C_SUBU32 0x00000004

/* WAR to define tegra_soc_chip_id as it is used in QNX NVVSE driver */
typedef enum {
	TEGRA_SOC_CHIP_ID_1 = 0,
	TEGRA_SOC_CHIP_ID_2 = 1,
} tegra_soc_chip_id;

/* Trigger a kernel warning to indicate an error */
static inline void NVVSE_EXIT(uint32_t err_code)
{
	NVVSE_ERR("Irrecoverable error encountered! error code: %d\n", err_code);
	WARN_ON_ONCE(false);
}

#endif /* _NVVSE_LINUX_COMMON_H_ */
