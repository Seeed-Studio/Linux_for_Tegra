/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES.
 */

#ifndef __TEGRA_DRIVER_SE_H__
#define __TEGRA_DRIVER_SE_H__

#include <tee_api_types.h>
#include <types_ext.h>

#if defined(PLATFORM_FLAVOR_t194)
#define TEGRA_SE_BASE   0x03ac0000
#define TEGRA_SE_SIZE   0x2000
#endif

#if defined(PLATFORM_FLAVOR_t234)
#define TEGRA_SE_BASE   0x03b50000
#define TEGRA_SE_SIZE   0x30000
#endif

/* Security Engine Operation Modes */
typedef enum
{
	SE_AES_OP_MODE_CBC,     /* Cipher Block Chaining (CBC) mode */
	SE_AES_OP_MODE_CMAC,    /* Cipher-based MAC (CMAC) mode */
	SE_AES_OP_MODE_ECB,     /* Electronic Codebook (ECB) mode */
} se_aes_op_mode;

TEE_Result tegra_se_map_regs(vaddr_t *va);

#endif
