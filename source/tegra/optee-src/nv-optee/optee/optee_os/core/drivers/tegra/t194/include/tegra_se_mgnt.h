/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES.
 */

#ifndef __TEGRA_SE_MGNT_H__
#define __TEGRA_SE_MGNT_H__

#include <tee_api_types.h>
#include <types_ext.h>
#include <tegra_driver_common.h>
#include <tegra_driver_se.h>

/*
 * Acquires SE hardware mutex -
 *  This function should ALWAYS be called BEFORE interacting with SE.
 *
 * se_base	[in] the virtual address of the SE base addr.
 */
TEE_Result se_mutex_acquire(vaddr_t se_base);

/*
 * Releases SE hardware mutex -
 *  This function should ALWAYS be called AFTER interacting with SE.
 *
 * se_base	[in] the virtual address of the SE base addr.
 */
void se_mutex_release(vaddr_t se_base);

/*
 * Get the configuration of SE AES config register.
 *
 * mode		[in] the SE AES operation mode.
 * keylen	[in] the key length.
 */
uint32_t se_aes_get_config(se_aes_op_mode mode, uint32_t keylen);

/*
 * Get the configuration of the AES crypto config register.
 *
 * mode		[in] the SE AES operation mode.
 * keylen	[in] the key length.
 * orig_iv	[in] if this is original initial vector.
 */
uint32_t se_aes_get_crypto_config(se_aes_op_mode mode, uint32_t keyslot,
				  bool org_iv);

/*
 * Start the SE AES operation.
 *
 * se_base		[in] the virtual address of the SE base addr.
 * config		[in] the SE AES config.
 * crypto_config	[in] the SE AEC crypto config.
 * src			[in] the pointer of the source buffer.
 * src_len		[in] the length of the source buffer.
 * dst			[out] the pointer of the destination buffer.
 * dst_len		[in] the length of the destination buffer.
 */
TEE_Result se_aes_start_operation(vaddr_t se_base,
				  uint32_t config, uint32_t crypto_config,
				  uint8_t *src, size_t src_len,
				  uint8_t *dst, size_t dst_len);

#endif
