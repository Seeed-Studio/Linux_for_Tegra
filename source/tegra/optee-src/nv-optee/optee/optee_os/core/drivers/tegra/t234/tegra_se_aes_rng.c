// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2022-2024, NVIDIA CORPORATION & AFFILIATES.
 */

#include <string.h>
#include <stdlib.h>
#include <types_ext.h>
#include <tee_api_types.h>
#include <trace.h>
#include <initcall.h>
#include <rng_support.h>
#include <utee_defines.h>
#include <tegra_se_ccc.h>
#include <tegra_se_ccc_errno.h>
#include <tegra_driver_se.h>
#include <tegra_driver_srv_intf.h>

#if defined(CFG_TEGRA_SE_RNG1)
#include <tegra_driver_rng1.h>
#endif

static TEE_Result se_aes_encrypt(uint8_t *src, size_t src_len,
			uint8_t *dst, size_t dst_len, se_aes_op_mode mode,
			uint32_t keyslot, uint8_t *iv)
{
	status_t ret = NO_ERROR;

	if (src == NULL || src_len == 0 || dst == NULL || dst_len == 0)
		return TEE_ERROR_BAD_PARAMETERS;

	ret = tegra_se_ccc_aes_encrypt(src, src_len, dst, dst_len,
			mode, keyslot, TEGRA_SE_KEY_256_SIZE, iv);
	if (ret != NO_ERROR) {
		EMSG("%s failed with: %d", __func__, ret);
		return TEE_ERROR_GENERIC;
	}

        return TEE_SUCCESS;
}

static TEE_Result se_aes_ecb_kdf(uint8_t *derived_key, size_t derived_key_len,
				 uint8_t *iv, size_t iv_len,
				 uint32_t keyslot)
{
	if ((derived_key == NULL) || (iv == NULL)) {
		EMSG("%s: Invalid arguments.", __func__);
		return TEE_ERROR_BAD_PARAMETERS;
	}

	if ((derived_key_len != TEE_AES_BLOCK_SIZE) ||
	    (iv_len != TEE_AES_BLOCK_SIZE)) {
		EMSG("%s: Invalid size arguments.", __func__);
		return TEE_ERROR_BAD_PARAMETERS;
	}

	return se_aes_encrypt(iv, iv_len,
			derived_key, derived_key_len,
			SE_AES_OP_MODE_ECB, keyslot, NULL);
}

static TEE_Result se_nist_sp_800_108_cmac_kdf(se_aes_keyslot_t keyslot,
					      uint32_t key_len,
					      char const *context,
					      uint32_t context_len,
					      char const *label,
					      uint32_t label_len,
					      uint32_t dk_len,
					      uint8_t *out_dk)
{
	uint8_t counter[] = { 1 }, zero_byte[] = { 0 };
	uint32_t L[] = { TEE_U32_TO_BIG_ENDIAN(dk_len * 8) };
	uint8_t *message = NULL, *mptr;
	uint32_t msg_len, i, n;
	status_t ret = NO_ERROR;
	TEE_Result rc = TEE_SUCCESS;

	if ((key_len != TEGRA_SE_KEY_128_SIZE) &&
	    (key_len != TEGRA_SE_KEY_256_SIZE))
		return TEE_ERROR_BAD_PARAMETERS;

	if ((dk_len % TEE_AES_BLOCK_SIZE) != 0)
		return TEE_ERROR_BAD_PARAMETERS;

	if (!context || !label || !out_dk)
		return TEE_ERROR_BAD_PARAMETERS;

	/*
	 *  Regarding to NIST-SP 800-108
	 *  message = counter || label || 0 || context || L
	 *
	 *  A || B = The concatenation of binary strings A and B.
	 */
	msg_len = context_len + label_len + 2 + sizeof(L);
	message = calloc(1, msg_len);
	if (message == NULL)
		return TEE_ERROR_OUT_OF_MEMORY;

	/* Concatenate the messages */
	mptr = message;
	memcpy(mptr, counter, sizeof(counter));
	mptr += sizeof(counter);
	memcpy(mptr, label, label_len);
	mptr += label_len;
	memcpy(mptr, zero_byte, sizeof(zero_byte));
	mptr += sizeof(zero_byte);
	memcpy(mptr, context, context_len);
	mptr += context_len;
	memcpy(mptr, L, sizeof(L));

	/* SE AES-CMAC */
	/* n: iterations of the PRF count */
	n = dk_len / TEE_AES_BLOCK_SIZE;
	for (i = 0; i < n; i++) {
		/* Update the counter */
		message[0] = i + 1;

		ret = tegra_se_ccc_aes_encrypt(message, msg_len,
			(out_dk + (i * TEE_AES_BLOCK_SIZE)), TEE_AES_BLOCK_SIZE,
			SE_AES_OP_MODE_CMAC, keyslot, key_len, NULL);
		if (ret != NO_ERROR) {
			EMSG("CMAC-AES failed with: %d", ret);
			rc = TEE_ERROR_GENERIC;
			goto kdf_error;
		}
	}

kdf_error:
	free(message);
	return rc;
}

static TEE_Result se_write_aes_keyslot(uint8_t *input_key, uint32_t keylen,
				       uint32_t key_quad_sel,
				       se_aes_keyslot_t keyslot)
{
	status_t ret = NO_ERROR;

	if ((keylen != TEGRA_SE_KEY_128_SIZE) &&
	    (keylen != TEGRA_SE_KEY_256_SIZE)) {
		EMSG("%s: Invalid key size.", __func__);
		return TEE_ERROR_BAD_PARAMETERS;
	}

	if ((key_quad_sel != TEGRA_SE_AES_QUAD_KEYS_128) &&
	    (key_quad_sel != TEGRA_SE_AES_QUAD_KEYS_256) &&
	    (key_quad_sel != TEGRA_SE_AES_QUAD_ORG_IV)) {
		EMSG("%s: Invalid key QUAD selection.", __func__);
		return TEE_ERROR_BAD_PARAMETERS;
	}

	/* TODO:
	 * For T234 keyslot setting, algo is mandatory.
	 * On T194, the aes slot writting is only used to pass SBK to CPU-BL.
	 * So need to decide:
	 *     1. Does UEFI need SBK to be present in a keyslot?
	 *     2. Which encryption algo is used for kernel encryption?
	 * Need to change here when there is a conclusion.
	 */
	ret = tegra_se_ccc_set_aes_keyslot(keyslot, input_key, keylen, SE_AES_OP_MODE_CBC);
	if (ret != NO_ERROR) {
		EMSG("%s failed with: %d", __func__, ret);
		return TEE_ERROR_GENERIC;
	}

	return TEE_SUCCESS;
}

static TEE_Result se_clear_aes_keyslots(void)
{
	status_t ret = NO_ERROR;
	uint32_t i;

	for (i = SE_AES_KEYSLOT_11; i < SE_AES_KEYSLOT_COUNT; i++) {
		if (i == SE_AES_KEYSLOT_11 + 2) {
			/* Skip keyslot #13 since we don't use it */
			continue;
		}

		ret = tegra_se_ccc_clear_aes_keyslot(i);
		if (ret != NO_ERROR) {
			EMSG("%s failed with: %d", __func__, ret);
			return TEE_ERROR_GENERIC;
		}
	}

	return TEE_SUCCESS;
}

#if defined(CFG_TEGRA_SE_RNG1)
static TEE_Result se_rng1_get_random(void *data, uint32_t data_len)
{
	status_t ret = NO_ERROR;

	ret = tegra_se_ccc_aes_genrnd(data, data_len);
	if (ret != NO_ERROR) {
		EMSG("%s failed with: %d", __func__, ret);
		return TEE_ERROR_GENERIC;
	}

	return TEE_SUCCESS;
}
#endif

/* This function is called by tegra_init_crypto_devices to map devices */
status_t tegra_se_map_device(optee_tegra_se_cdev_id_t device_id, void **base_va)
{
	TEE_Result rc = TEE_SUCCESS;
	status_t ret = NO_ERROR;
	vaddr_t va = 0;

	if (base_va == NULL) {
		EMSG("tegra_se_map_device: base_va is NULL.");
		*base_va = NULL;
		return ERR_INVALID_ARGS;
	}

	switch (device_id) {
	case OPTEE_TEGRA_SE_CDEV_SE0:
		rc = tegra_se_map_regs(&va);
		if (rc != TEE_SUCCESS) {
			EMSG("Mapping SE0 regs failed: 0x%x", rc);
			*base_va = NULL;
			ret = ERR_GENERIC;
			goto out;
		}
		ret = tegra_se_ccc_configure_se0(va);
		if (ret != NO_ERROR) {
			EMSG("Configure SE0 failed: %d", ret);
			*base_va = NULL;
			goto out;
		}
		*base_va = (void *)va;
		break;
#if defined(CFG_TEGRA_SE_RNG1)
	case OPTEE_TEGRA_SE_CDEV_RNG1:
		/*
		 * Random numbers can be generated via SE engine as well,
		 * just mapping the RNG1 here too in case we need to use NVRNG
		 * in the future.
		 */
		rc = tegra_rng1_map_regs(&va);
		if (rc != TEE_SUCCESS) {
			EMSG("Mapping RNG1 regs failed: 0x%x", rc);
			*base_va = NULL;
			ret = ERR_GENERIC;
			goto out;
		}
		ret = tegra_se_ccc_configure_rng1(va);
		if (ret != NO_ERROR) {
			EMSG("Configure RNG1 failed: %d", ret);
			*base_va = NULL;
			goto out;
		}
		*base_va = (void *)va;
		break;
#endif
	default:
		/* CCC will disable the unsupported devices accordingly */
		DMSG("Mapping unsupported SE device: %d", device_id);
		*base_va = NULL;
		break;
	}

out:
	return ret;
}

static TEE_Result tegra_se_init(void)
{
	status_t ret;
	tegra_drv_srv_intf_t *drv_srv_intf;

#if defined(CFG_TEGRA_SE_USE_TEST_KEYS)
	/* OEM_K1/OEM_K2: 0000000000000000000000000000000000000000000000000000000000000000 */
	const uint8_t test_oem_key[] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};

	TEE_Result err = TEE_SUCCESS;
	uint32_t bsi = 0;
#endif

	ret = tegra_init_crypto_devices();
	if (ret != NO_ERROR) {
		return TEE_ERROR_GENERIC;
	}

#if defined(CFG_TEGRA_SE_USE_TEST_KEYS)
	err = tegra_fuse_get_bsi(&bsi);
	if (err != TEE_SUCCESS) {
		EMSG("Read fuse BSI failed: 0x%x", err);
		return TEE_ERROR_GENERIC;
	}

	if ((bsi & BSI_OEM_KEY_VALID_MASK) == 0) {
		ret = tegra_se_ccc_set_test_oem_keys(test_oem_key, test_oem_key);
		if (ret != NO_ERROR) {
			EMSG("Setting test OEM keys failed: %d", ret);
			return TEE_ERROR_GENERIC;
		}
		IMSG("Test OEM keys are being used. This is insecure for shipping products!");
	}
#endif

	drv_srv_intf = tegra_drv_srv_intf_get();
	if (drv_srv_intf != NULL) {
		drv_srv_intf->aes_encrypt = se_aes_encrypt;
		drv_srv_intf->aes_ecb_kdf = se_aes_ecb_kdf;
		drv_srv_intf->nist_sp_800_108_cmac_kdf = se_nist_sp_800_108_cmac_kdf;
		drv_srv_intf->write_aes_keyslot = se_write_aes_keyslot;
		drv_srv_intf->clear_aes_keyslots = se_clear_aes_keyslots;
#if defined(CFG_TEGRA_SE_RNG1)
		drv_srv_intf->rng_get_random = se_rng1_get_random;
#endif
	} else {
		return TEE_ERROR_NOT_SUPPORTED;
	}

	return TEE_SUCCESS;
}
service_init_late(tegra_se_init);
