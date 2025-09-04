/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES.
 */

#ifndef __TEGRA_SE_KEYSLOT_H__
#define __TEGRA_SE_KEYSLOT_H__

#include <tee_api_types.h>

#define TEGRA_SE_AES_BLOCK_SIZE		16
#define TEGRA_SE_AES_IV_SIZE		16
#define TEGRA_SE_KEY_128_SIZE		16
#define TEGRA_SE_KEY_256_SIZE		32

#define TEGRA_SE_AES_QUAD_KEYS_128	0
#define TEGRA_SE_AES_QUAD_KEYS_256	1
#define TEGRA_SE_AES_QUAD_ORG_IV	2
#define TEGRA_SE_AES_QUAD_UPDTD_IV	3
#define TEGRA_SE_AES_QUAD_MAX		TEGRA_SE_AES_QUAD_UPDTD_IV

#if defined(PLATFORM_FLAVOR_t194)
typedef enum
{
	SE_AES_KEYSLOT_11 = 11,
	SE_AES_KEYSLOT_12 = 12,
	SE_AES_KEYSLOT_13 = 13,
	SE_AES_KEYSLOT_15 = 15,

	// SSK is loaded in keyslot 15
	SE_AES_KEYSLOT_SSK = SE_AES_KEYSLOT_15,

	// KEK fuse values are loaded in keyslots 11 through 13
	SE_AES_KEYSLOT_KEK1E_128B = SE_AES_KEYSLOT_13,
	SE_AES_KEYSLOT_KEK1G_128B = SE_AES_KEYSLOT_12,
	SE_AES_KEYSLOT_KEK2_128B = SE_AES_KEYSLOT_11,

	SE_AES_KEYSLOT_KEK256 = SE_AES_KEYSLOT_13,

	SE_AES_KEYSLOT_COUNT = 16,
} se_aes_keyslot_t;
#endif

#if defined(PLATFORM_FLAVOR_t234)
typedef enum
{
	SE_AES_KEYSLOT_11 = 11,
	SE_AES_KEYSLOT_12 = 12,

	// Fuse keys: OEM_K1 -> slot 12, OEM_K2 -> slot 11
	SE_AES_KEYSLOT_OEM_K1 = SE_AES_KEYSLOT_12,
	SE_AES_KEYSLOT_OEM_K2 = SE_AES_KEYSLOT_11,

	SE_AES_KEYSLOT_COUNT = 16,
} se_aes_keyslot_t;
#endif

/*
 * Write key into the AES keyslot.
 *
 * input_key		[in] the pointer of the input key buffer.
 * keylen		[in] the length of the input key.
 * key_quad_sel		[in] the quad selection of the key.
 * keyslot		[in] the target keyslot index.
 */
TEE_Result tegra_se_write_aes_keyslot(uint8_t *input_key, uint32_t keylen,
				      uint32_t key_quad_sel,
				      se_aes_keyslot_t keyslot);

/* Clear the AES keyslots */
TEE_Result tegra_se_clear_aes_keyslots(void);

#endif
