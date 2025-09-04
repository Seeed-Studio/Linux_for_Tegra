// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES.
 */

#include <types_ext.h>
#include <tee_api_types.h>
#include <trace.h>
#include <io.h>
#include <string.h>
#include <initcall.h>
#include <assert.h>
#include <kernel/tee_common_otp.h>
#include <tee/tee_fs.h>
#include <drivers/tegra/tegra_fuse.h>
#include <drivers/tegra/tegra_se_aes.h>
#include <drivers/tegra/tegra_se_keyslot.h>

static bool tegra_rpmb_key_is_ready = false;

static uint8_t rollbackkeyiv[TEGRA_SE_AES_IV_SIZE] = {
	'n', 'v', '-', 's', 't', 'o', 'r', 'a',
	'g', 'e', '-', 'd', 'u', 'm', 'm', 'y'
};

static uint8_t rollbackkeysrc[TEGRA_SE_AES_BLOCK_SIZE * 2] = {
	0x81, 0x2A, 0x01, 0x43, 0x6B, 0x7C, 0x19, 0xAA,
	0xFF, 0x22, 0x38, 0x82, 0x0A, 0x67, 0x74, 0x08,
	0x30, 0x06, 0xCA, 0x11, 0x41, 0x49, 0x80, 0xED,
	0xE7, 0xBB, 0x61, 0x01, 0x2F, 0x56, 0x9D, 0xD3
};

static uint8_t rollbackkey[TEGRA_SE_AES_BLOCK_SIZE * 2] = { 0 };

/* Override the weak tee_otp_get_rpmb_key */
TEE_Result tee_otp_get_rpmb_key(uint8_t *key, size_t len)
{
	if (key == NULL)
		return TEE_ERROR_BAD_PARAMETERS;
	if (len != TEGRA_SE_AES_BLOCK_SIZE * 2) {
		EMSG("Invalid RPMB key length");
		return TEE_ERROR_BAD_PARAMETERS;
	}

	if (tegra_rpmb_key_is_ready) {
		memcpy(key, rollbackkey, len);
		return TEE_SUCCESS;
	} else {
		EMSG("Failed to get RPMB key");
		return TEE_ERROR_NO_DATA;
	}
}

bool plat_rpmb_key_is_ready(void)
{
	return tegra_rpmb_key_is_ready;
}

static TEE_Result tegra_rpmb_init(void)
{
	TEE_Result rc = TEE_SUCCESS;
	se_aes_keyslot_t fuse_key_for_rpmb = SE_AES_KEYSLOT_OEM_K1;
	uint32_t bsi = 0;
	fuse_ecid_t *ecid = NULL;
	static_assert(sizeof(fuse_ecid_t) <= TEGRA_SE_AES_IV_SIZE);

	rc = tegra_fuse_get_bsi(&bsi);
	if (rc != TEE_SUCCESS) {
		EMSG("Read fuse BSI failed: 0x%x", rc);
		goto out;
	}

	if ((bsi & BSI_OEM_KEY_VALID_MASK) == 0) {
		IMSG("OEM key valid is not set. RPMB key generation is skipped.");
		goto out;
	}

	ecid = tegra_fuse_get_ecid();
	if (ecid == NULL) {
		rc = TEE_ERROR_GENERIC;
		EMSG("%s: Failed to get ecid", __func__);
		goto out;
	}
	memcpy(rollbackkeyiv, ecid->ecid, sizeof(fuse_ecid_t));

	rc = tegra_se_aes_encrypt_cbc(rollbackkeysrc, sizeof(rollbackkeysrc),
				      rollbackkey, fuse_key_for_rpmb, rollbackkeyiv);
	if (rc != TEE_SUCCESS) {
		EMSG("%s: Failed to derive rollback key (%x)", __func__, rc);
		/*
		 * Do not return an error which will halt optee booting
		 * We have "tegra_rpmb_key_is_ready" to tell optee if the RPMB key is ready or not
		 */
		rc = TEE_SUCCESS;
	} else {
		IMSG("Tegra RPMB key generation succeeded.");
		tegra_rpmb_key_is_ready = true;
	}

out:
	return rc;
}
driver_init(tegra_rpmb_init);
