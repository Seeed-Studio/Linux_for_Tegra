/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES.
 */

#ifndef __TEGRA_SE_AES_H__
#define __TEGRA_SE_AES_H__

#include <tee_api_types.h>

/*
 * SE AES CBC - AES encryption function with CBC mode
 *
 * src			[in] source buffer of input key
 * src_len		[in] buffer length of input key
 * dst			[out] buffer of output buffer
 * keyslot		[in] SE keyslot.
 * iv			[in] initial vector
 */

TEE_Result tegra_se_aes_encrypt_cbc(uint8_t *src, size_t src_len,
					uint8_t *dst, uint32_t keyslot, uint8_t *iv);
#endif
