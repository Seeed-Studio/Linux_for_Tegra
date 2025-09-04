// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2021-2024, NVIDIA CORPORATION & AFFILIATES.
 */

#include <drivers/tegra/tegra_se_aes.h>
#include <drivers/tegra/tegra_se_kdf.h>
#include <drivers/tegra/tegra_se_keyslot.h>
#include <drivers/tegra/tegra_se_rng.h>
#include <drivers/tegra/tegra_fuse.h>
#include <initcall.h>
#include <stdlib.h>
#include <tegra_driver_srv_intf.h>

static tegra_drv_srv_intf_t *tegra_drv_srv_intf = NULL;

TEE_Result tegra_se_aes_ecb_kdf(uint8_t *derived_key, size_t derived_key_len,
				uint8_t *iv, size_t iv_len,
				uint32_t keyslot)
{
	if (!tegra_drv_srv_intf || !tegra_drv_srv_intf->aes_ecb_kdf)
		return TEE_ERROR_NOT_SUPPORTED;

	return tegra_drv_srv_intf->aes_ecb_kdf(derived_key, derived_key_len,
					       iv, iv_len,
					       keyslot);
}

TEE_Result tegra_se_aes_encrypt_cbc(uint8_t *src, size_t src_len,
					uint8_t *dst, uint32_t keyslot, uint8_t *iv)
{
	if (!tegra_drv_srv_intf || !tegra_drv_srv_intf->aes_encrypt)
		return TEE_ERROR_NOT_SUPPORTED;

	return tegra_drv_srv_intf->aes_encrypt(src, src_len, dst, src_len,
			SE_AES_OP_MODE_CBC, keyslot, iv);
}

TEE_Result tegra_se_nist_sp_800_108_cmac_kdf(se_aes_keyslot_t keyslot,
					     uint32_t key_len,
					     char const *context,
					     uint32_t context_len,
					     char const *label,
					     uint32_t label_len,
					     uint32_t dk_len,
					     uint8_t *out_dk)
{
	if (!tegra_drv_srv_intf ||
	    !tegra_drv_srv_intf->nist_sp_800_108_cmac_kdf)
		return TEE_ERROR_NOT_SUPPORTED;

	return tegra_drv_srv_intf->nist_sp_800_108_cmac_kdf(keyslot, key_len,
							    context,
							    context_len,
							    label,
							    label_len,
							    dk_len,
							    out_dk);
}

TEE_Result tegra_se_write_aes_keyslot(uint8_t *input_key, uint32_t keylen,
				      uint32_t key_quad_sel,
				      se_aes_keyslot_t keyslot)
{
	if (!tegra_drv_srv_intf || !tegra_drv_srv_intf->write_aes_keyslot)
		return TEE_ERROR_NOT_SUPPORTED;

	return tegra_drv_srv_intf->write_aes_keyslot(input_key, keylen,
						     key_quad_sel,
						     keyslot);
}

TEE_Result tegra_se_clear_aes_keyslots(void)
{
	if (!tegra_drv_srv_intf || !tegra_drv_srv_intf->clear_aes_keyslots)
		return TEE_ERROR_NOT_SUPPORTED;

	return tegra_drv_srv_intf->clear_aes_keyslots();
}

TEE_Result tegra_se_rng_get_random(void *data_buf, uint32_t data_len)
{
	if (!tegra_drv_srv_intf || !tegra_drv_srv_intf->rng_get_random)
		return TEE_ERROR_NOT_SUPPORTED;

	return tegra_drv_srv_intf->rng_get_random(data_buf, data_len);
}

fuse_ecid_t* tegra_fuse_get_ecid(void)
{
	if (!tegra_drv_srv_intf || !tegra_drv_srv_intf->get_ecid)
		return NULL;

	return tegra_drv_srv_intf->get_ecid();
}

TEE_Result tegra_fuse_get_64bit_ecid(uint64_t *val)
{
	if (!tegra_drv_srv_intf || !tegra_drv_srv_intf->get_64bit_ecid)
		return TEE_ERROR_NOT_SUPPORTED;

	return tegra_drv_srv_intf->get_64bit_ecid(val);
}

TEE_Result tegra_fuse_get_bsi(uint32_t *val)
{
	if (!tegra_drv_srv_intf || !tegra_drv_srv_intf->get_bsi)
		return TEE_ERROR_NOT_SUPPORTED;

	return tegra_drv_srv_intf->get_bsi(val);
}

TEE_Result tegra_fuse_get_sec_mode(uint32_t *val)
{
	if (!tegra_drv_srv_intf || !tegra_drv_srv_intf->get_sec_mode)
		return TEE_ERROR_NOT_SUPPORTED;

	return tegra_drv_srv_intf->get_sec_mode(val);
}


TEE_Result tegra_fuse_get_sn(uint8_t **sn, uint32_t *size)
{
	if (!tegra_drv_srv_intf || !tegra_drv_srv_intf->get_sn)
		return TEE_ERROR_NOT_SUPPORTED;

	return tegra_drv_srv_intf->get_sn(sn, size);
}

tegra_drv_srv_intf_t *tegra_drv_srv_intf_get(void)
{
	return tegra_drv_srv_intf;
}

static TEE_Result tegra_drv_srv_intf_init(void)
{
	tegra_drv_srv_intf = calloc(1, sizeof(tegra_drv_srv_intf_t));
	if (tegra_drv_srv_intf == NULL)
		return TEE_ERROR_OUT_OF_MEMORY;

	return TEE_SUCCESS;
}

early_init_late(tegra_drv_srv_intf_init);
