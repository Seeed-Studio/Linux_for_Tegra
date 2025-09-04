/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2021-2024, NVIDIA CORPORATION & AFFILIATES.
 */

#ifndef __TEGRA_DRIVER_SRV_INTF_H__
#define __TEGRA_DRIVER_SRV_INTF_H__

#include <drivers/tegra/tegra_fuse.h>
#include <drivers/tegra/tegra_se_keyslot.h>
#include <tegra_driver_common.h>
#include <tegra_driver_se.h>
#include <tee_api_types.h>

typedef struct tegra_driver_srv_intf {
	/* KDF interface */
	TEE_Result (*aes_ecb_kdf)(uint8_t *derived_key, size_t derived_key_len,
				  uint8_t *iv, size_t iv_len,
				  uint32_t keyslot);
	TEE_Result (*nist_sp_800_108_cmac_kdf)(se_aes_keyslot_t keyslot,
					       uint32_t key_len,
					       char const *context,
					       uint32_t context_len,
					       char const *label,
					       uint32_t label_len,
					       uint32_t dk_len,
					       uint8_t *out_dk);
	/* keyslot interface */
	TEE_Result (*write_aes_keyslot)(uint8_t *input_key, uint32_t keylen,
					uint32_t key_quad_sel,
					se_aes_keyslot_t keyslot);
	TEE_Result (*clear_aes_keyslots)(void);
	/* RNG interface */
	TEE_Result (*rng_get_random)(void *data_buf, uint32_t data_len);
	/* Fuse ECID interface */
	fuse_ecid_t* (*get_ecid)(void);
	TEE_Result (*get_64bit_ecid)(uint64_t *val);
	/* Fuse BSI interface */
	TEE_Result (*get_bsi)(uint32_t *val);
	/* Fuse security mode interface */
	TEE_Result (*get_sec_mode)(uint32_t *val);
	/* Fuse get SN interface */
	TEE_Result (*get_sn)(uint8_t **sn, uint32_t *size);
	/* AES encryption interface */
	TEE_Result (*aes_encrypt)(uint8_t *src, size_t src_len,
					uint8_t *dst, size_t dst_len, se_aes_op_mode mode,
					uint32_t keyslot, uint8_t *iv);
} tegra_drv_srv_intf_t;

/* Initialize SE service interface */
tegra_drv_srv_intf_t *tegra_drv_srv_intf_get(void);

#endif
