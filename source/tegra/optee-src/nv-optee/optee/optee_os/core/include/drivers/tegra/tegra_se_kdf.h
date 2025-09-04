/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES.
 */

#ifndef __TEGRA_SE_KDF_H__
#define __TEGRA_SE_KDF_H__

#include <drivers/tegra/tegra_se_keyslot.h>
#include <tee_api_types.h>

/*
 * SE AES ECB KDF - Derives root key from SE keyslot.
 *
 * derived_key		[out] pointer of the output buffer of the derived_key.
 * derived_key_len	[in] the length of the derived_key.
 * iv			[in] pointer of the initial vector.
 * iv_len		[in] the length of the initial vector.
 * keyslot		[in] SE keyslot.
 */

TEE_Result tegra_se_aes_ecb_kdf(uint8_t *derived_key, size_t derived_key_len,
				uint8_t *iv, size_t iv_len,
				uint32_t keyslot);

/*
 * A hardware-based NIST-SP 800-108 KDF;
 *  derives keys from the SE keyslot.
 *
 *  Use this function only during OP-TEE initialization at boot time
 *  (the device boot stage). To derive keys from a key buffer at runtime,
 *  use nist_sp_800_108_cmac_kdf().
 *
 * keyslot	[in] the SE keyslot where the key stores.
 * key_len	[in] length in bytes of the input key.
 * *context	[in] a pointer to a NIST-SP 800-108 context string.
 * context_len	[in] length of the context string.
 * *label	[in] a pointer to a NIST-SP 800-108 label string.
 * label_len	[in] length of the label string.
 * dk_len	[in] length of the derived key in bytes;
 *		     may be 16 (128 bits) or any multiple of 16.
 * *out_dk	[out] a pointer to the derived key. The function stores
 *		      its result in this location.
 */
TEE_Result tegra_se_nist_sp_800_108_cmac_kdf(se_aes_keyslot_t keyslot,
					     uint32_t key_len,
					     char const *context,
					     uint32_t context_len,
					     char const *label,
					     uint32_t label_len,
					     uint32_t dk_len,
					     uint8_t *out_dk);

#endif
