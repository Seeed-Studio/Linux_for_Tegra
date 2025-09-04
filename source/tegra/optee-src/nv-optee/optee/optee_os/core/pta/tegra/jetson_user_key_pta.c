// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2021-2024, NVIDIA CORPORATION & AFFILIATES.
 */

#include <config.h>
#include <crypto/crypto.h>
#include <drivers/tegra/tegra_fuse.h>
#include <drivers/tegra/tegra_se_aes.h>
#include <drivers/tegra/tegra_se_kdf.h>
#include <drivers/tegra/tegra_se_rng.h>
#include <initcall.h>
#include <pta_jetson_user_key.h>
#include <kernel/panic.h>
#include <kernel/boot.h>
#include <kernel/dt.h>
#include <kernel/pseudo_ta.h>
#include <kernel/tee_common_otp.h>
#include <libfdt.h>
#include <stdio.h>
#include <string.h>
#include <trace.h>
#include <sys/queue.h>

#include <mm/core_memprot.h>
#include "jetson_decrypt_cpubl_payload.h"
#include "jetson_user_key_pta_compute_cmac.h"
#include "jetson_user_key_pta_ftpm.h"

#define MAX_EKB_RK			2
#define JETSON_USER_KEY_PTA_NAME	"jetson_user_key_pta.ta"

static vaddr_t ekb_base_addr;

/*
 * Random fixed vector for EKB.
 *
 * Note: This vector MUST match the 'fv' vector used for EKB binary
 * generation process.
 * ba d6 6e b4 48 49 83 68 4b 99 2f e5 4a 64 8b b8
 */
static uint8_t fv_for_ekb[] = {
	0xba, 0xd6, 0x6e, 0xb4, 0x48, 0x49, 0x83, 0x68,
	0x4b, 0x99, 0x2f, 0xe5, 0x4a, 0x64, 0x8b, 0xb8,
};

/*
 * Random fixed vector used to derive SSK_DK (Derived Key).
 *
 * e4 20 f5 8d 1d ea b5 24 c2 70 d8 d2 3e ca 45 e8
 */
static uint8_t fv_for_ssk_dk[] = {
	0xe4, 0x20, 0xf5, 0x8d, 0x1d, 0xea, 0xb5, 0x24,
	0xc2, 0x70, 0xd8, 0xd2, 0x3e, 0xca, 0x45, 0xe8,
};

/*
 * Root keys derived from SE keyslots.
 */
static uint8_t ekb_rks[MAX_EKB_RK][TEGRA_SE_KEY_128_SIZE];
static uint8_t ekb_rks_num = 0;
static uint8_t *ekb_rk_verified = NULL;
static uint8_t ssk_rk[TEGRA_SE_KEY_128_SIZE] = { 0 };
#if defined(PLATFORM_FLAVOR_t194)
static uint8_t demo_256_rk[TEGRA_SE_KEY_256_SIZE] = { 0 };
#endif

/*
 * Derived keys from NIST-SP 800-108.
 */
static uint8_t ekb_ek[TEGRA_SE_KEY_128_SIZE] = { 0 };
static uint8_t ekb_ak[TEGRA_SE_KEY_128_SIZE] = { 0 };

struct ekb_content_header {
	uint32_t enc_data_len;
	uint8_t magic[4];
	uint8_t reserved[8];
};

struct ekb_content {
	uint8_t ekb_cmac[TEGRA_SE_AES_BLOCK_SIZE];
	struct ekb_content_header content_header;
	uint8_t random_iv[TEGRA_SE_AES_IV_SIZE];
	uint8_t ekb_ciphertext[0];
};

typedef struct user_key {
	TAILQ_ENTRY(user_key) link;
	uint32_t type;
	uint32_t len;
	uint8_t *data;
} user_key_t;

static TAILQ_HEAD(user_keys, user_key) user_keys =
		TAILQ_HEAD_INITIALIZER(user_keys);

/*
 * HW unique key for tee_otp_get_hw_unique_key
 */
static struct tee_hw_unique_key *hw_unique_key = NULL;

/*
 * Jetson service flag:
 *    LUKS_SRV_FLAG: Turn on/off for CA to query passphrase
 *    PTA_SRV_FLAG: Indicate the TA is available or not
 */
static uint32_t jetson_service_flag = LUKS_SRV_FLAG |
				      PTA_SRV_FLAG;

/*
 * A software-based NIST-SP 800-108 KDF.
 *  derives keys from a key in a key buffer.
 *
 * *key		[in] input key for derivation.
 * key_len	[in] length in bytes of the input key.
 * *context	[in] a pointer to a NIST-SP 800-108 context string.
 * context_len	[in] length of the context string.
 * *label	[in] a pointer to a NIST-SP 800-108 label string.
 * label_len	[in] length of the label string.
 * dk_len	[in] length of the derived key in bytes;
 *		     may be 16 (128 bits) or any multiple of 16.
 * *out_dk 	[out] a pointer to the derived key. The function stores
 *		      its result in this location.
 */
static TEE_Result nist_sp_800_108_cmac_kdf(uint8_t *key,
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
	void *cmac_ctx = NULL;
	TEE_Result rc = TEE_SUCCESS;

	if ((key_len != TEGRA_SE_KEY_128_SIZE) &&
	    (key_len != TEGRA_SE_KEY_256_SIZE))
		return TEE_ERROR_BAD_PARAMETERS;

	if ((dk_len % TEE_AES_BLOCK_SIZE) != 0)
		return TEE_ERROR_BAD_PARAMETERS;

	if (!key || !context || !label || !out_dk)
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

	/* AES-CMAC */
	rc = crypto_mac_alloc_ctx(&cmac_ctx, TEE_ALG_AES_CMAC);
	if (rc)
		goto kdf_error;

	/* n: iterations of the PRF count */
	n = dk_len / TEE_AES_BLOCK_SIZE;

	for (i = 0; i < n; i++) {
		/* Update the counter */
		message[0] = i + 1;

		crypto_mac_init(cmac_ctx, key, key_len);
		crypto_mac_update(cmac_ctx, message, msg_len);
		crypto_mac_final(cmac_ctx, (out_dk + (i * TEE_AES_BLOCK_SIZE)),
				 TEE_AES_BLOCK_SIZE);
	}

	crypto_mac_free_ctx(cmac_ctx);

kdf_error:
	free(message);
	return rc;
}

static TEE_Result ekb_extraction_process(vaddr_t ekb_addr)
{
	TEE_Result rc = TEE_SUCCESS;
	struct ekb_content *ekb = (struct ekb_content *)ekb_addr;
	struct ekb_content_header *content_header = &ekb->content_header;
	uint8_t *magic = content_header->magic;
	void *decrypted_ekb_content;
	void *crypt_ctx = NULL;
	uint8_t ekb_cmac_verify[TEGRA_SE_AES_BLOCK_SIZE];
	struct user_key *ekb_key = NULL;
	uint8_t *p = NULL;
	int i = 0;

	if (magic[0] != 'E' || magic[1] != 'E' || magic[2] != 'K' || magic[3] != 'B') {
		EMSG("Bad parameter: eks image not correct\n");
		return TEE_ERROR_BAD_PARAMETERS;
	}

	for (i = 0; i < ekb_rks_num; i++) {
		memset(ekb_ek, 0, sizeof(ekb_ek));
		memset(ekb_ak, 0, sizeof(ekb_ak));
		memset(ekb_cmac_verify, 0, sizeof(ekb_cmac_verify));
		crypt_ctx = NULL;
		ekb_key = NULL;

		/* Derive EKB_EK */
		rc = nist_sp_800_108_cmac_kdf(ekb_rks[i], TEGRA_SE_KEY_128_SIZE,
					      "ekb", strlen("ekb"),
					      "encryption", strlen("encryption"),
					      TEGRA_SE_KEY_128_SIZE, ekb_ek);
		if (rc) {
			DMSG("Derive EKB_EK from #%d EKB_RK failed: 0x%x.", i, rc);
			continue;
		}

		/* Derive EKB_AK */
		rc = nist_sp_800_108_cmac_kdf(ekb_rks[i], TEGRA_SE_KEY_128_SIZE,
					      "ekb", strlen("ekb"),
					      "authentication", strlen("authentication"),
					      TEGRA_SE_KEY_128_SIZE, ekb_ak);
		if (rc) {
			DMSG("Derive EKB_AK from #%d EKB_RK failed: 0x%x.", i, rc);
			continue;
		}

		/* Calculate the EKB CMAC */
		rc = crypto_mac_alloc_ctx(&crypt_ctx, TEE_ALG_AES_CMAC);
		if (rc)
			continue;

		crypto_mac_init(crypt_ctx, ekb_ak, sizeof(ekb_ak));
		crypto_mac_update(crypt_ctx, (uint8_t *)content_header,
				  sizeof(struct ekb_content_header) + sizeof(ekb->random_iv)
				  + content_header->enc_data_len);
		crypto_mac_final(crypt_ctx, ekb_cmac_verify, TEGRA_SE_AES_BLOCK_SIZE);
		crypto_mac_free_ctx(crypt_ctx);

		/* Verify the CMAC */
		if (memcmp(ekb->ekb_cmac, ekb_cmac_verify, TEGRA_SE_AES_BLOCK_SIZE)) {
			DMSG("EKB signature mismatch when using #%d EKB_RK.", i);
			continue;
		}

		/* Decrypt the EKB ciphertext */
		rc = crypto_cipher_alloc_ctx(&crypt_ctx, TEE_ALG_AES_CBC_NOPAD);
		if (rc)
			continue;

		decrypted_ekb_content = malloc(content_header->enc_data_len);
		if (!decrypted_ekb_content) {
			crypto_cipher_free_ctx(crypt_ctx);
			continue;
		}

		crypto_cipher_init(crypt_ctx, TEE_MODE_DECRYPT,
				   ekb_ek, TEGRA_SE_KEY_128_SIZE,
				   NULL, 0,
				   ekb->random_iv, TEGRA_SE_AES_BLOCK_SIZE);
		crypto_cipher_update(crypt_ctx, TEE_MODE_DECRYPT, false,
				     ekb->ekb_ciphertext,
				     content_header->enc_data_len,
				     (uint8_t*)decrypted_ekb_content);
		crypto_cipher_final(crypt_ctx);
		crypto_cipher_free_ctx(crypt_ctx);

		p = decrypted_ekb_content;
		/* Parse the EKB keys */
		while (true) {
			ekb_key = malloc(sizeof(struct user_key));
			if (!ekb_key) {
				rc = TEE_ERROR_OUT_OF_MEMORY;
				break;
			}

			memcpy(&ekb_key->type, p, sizeof(ekb_key->type));
			p = p + sizeof(ekb_key->type);
			memcpy(&ekb_key->len, p, sizeof(ekb_key->len));
			if ((ekb_key->type == 0) && (ekb_key->len == 0)) {
				free(ekb_key);
				break;
			}
			p = p + sizeof(ekb_key->len);
			ekb_key->data = malloc(ekb_key->len);
			if (!(ekb_key->data)) {
				free(ekb_key);
				rc = TEE_ERROR_OUT_OF_MEMORY;
				break;
			}
			memcpy(ekb_key->data, p, ekb_key->len);
			p += ekb_key->len;
			TAILQ_INSERT_TAIL(&user_keys, ekb_key, link);
		}

		free(decrypted_ekb_content);
		if (rc == TEE_SUCCESS) {
			DMSG("Verified #%d EKB_RK works. Skip other EKB_RK keys.", i);
			ekb_rk_verified = ekb_rks[i];
			break;
		}
	}

	if (i == ekb_rks_num) {
		EMSG("Tried all EKB_RKs but still can't extract the EKB image.");
		return TEE_ERROR_SECURITY;
	}

	return TEE_SUCCESS;
}

static TEE_Result hwkey_derivation_process(void)
{
	TEE_Result rc = TEE_SUCCESS;
	size_t i = 0;
#if defined(PLATFORM_FLAVOR_t194)
	se_aes_keyslot_t fuse_key_for_ekb[] = { SE_AES_KEYSLOT_KEK2_128B };
	se_aes_keyslot_t fuse_key_for_ssk = SE_AES_KEYSLOT_SSK;
#endif
#if defined(PLATFORM_FLAVOR_t234)
	se_aes_keyslot_t fuse_key_for_ekb[] = { SE_AES_KEYSLOT_OEM_K1, SE_AES_KEYSLOT_OEM_K2 };
	se_aes_keyslot_t fuse_key_for_ssk = SE_AES_KEYSLOT_OEM_K2;
#endif
	static_assert(ARRAY_SIZE(fuse_key_for_ekb) <= MAX_EKB_RK);

	memset(ekb_rks, 0, sizeof(ekb_rks));
	for (i = 0; i < ARRAY_SIZE(fuse_key_for_ekb); i++) {
		/*
		 * Derive root keys by performing AES-ECB encryption with the fixed
		 * vector (fv) and keys in SE keyslots.
		 */
		rc = tegra_se_aes_ecb_kdf(ekb_rks[ekb_rks_num], sizeof(ekb_rks[ekb_rks_num]),
					  fv_for_ekb, sizeof(fv_for_ekb),
					  fuse_key_for_ekb[i]);
		if (rc == TEE_SUCCESS) {
			DMSG("Derived EKB_RK from #%lu fuse key succeeded", i);
			ekb_rks_num++;
		} else {
			IMSG("Derive EKB_RK from #%lu fuse key failed, ignored.", i);
			rc = TEE_SUCCESS;
		}
	}

	if (ekb_rks_num == 0) {
		EMSG("%s: Failed to derive EKB root key from all fuse keys(%x)", __func__, rc);
		goto key_derivation_fail;
	}

	rc = tegra_se_aes_ecb_kdf(ssk_rk, sizeof(ssk_rk),
				  fv_for_ssk_dk, sizeof(fv_for_ssk_dk),
				  fuse_key_for_ssk);
	if (rc != TEE_SUCCESS) {
		EMSG("%s: Failed to derive SSK root key (%x)", __func__, rc);
		/*
		 * Failing to derive SSK is not a critical issue, to not halt OP-TEE boots,
		 * reset the rc to SUCCESS so that OP-TEE can keep booting.
		 */
		rc = TEE_SUCCESS;
	}

#if defined(PLATFORM_FLAVOR_t194)
	/*
	 * Derive 256-bit root key from KEK256 SE keyslot.
	 *
	 * To support this, you need to modify the BR BCT file
	 * e.g. "tegra194-br-bct-sdmmc.cfg" (or "tegra194-br-bct-qspi.cfg").
	 * Add "BctKEKKeySelect = 1" into the BR BCT file.
	 * The BootRom will check the BCT to load KEK0 and KEK1 as a single
	 * 256-bit fuse into KEK256 SE keyslot.
	 */
	rc = tegra_se_nist_sp_800_108_cmac_kdf(SE_AES_KEYSLOT_KEK256,
					       TEGRA_SE_KEY_256_SIZE,
					       "Derived 256-bit root key",
					       strlen("Derived 256-bit root key"),
					       "256-bit key",
					       strlen("256-bit key"),
					       TEGRA_SE_KEY_256_SIZE,
					       demo_256_rk);
	if (rc != TEE_SUCCESS)
		EMSG("%s: Failed to derive 256-bit root key (%x).", __func__,
		     rc);
#endif

key_derivation_fail:
	return rc;
}

/* Override the weak tee_otp_get_hw_unique_key */
TEE_Result tee_otp_get_hw_unique_key(struct tee_hw_unique_key *hwkey)
{
	bool gen_unique_key_fail = false;

	if (hw_unique_key == NULL) {
		fuse_ecid_t *ecid;
		char ecid_str[36];
		TEE_Result rc;

		hw_unique_key = calloc(1, sizeof(struct tee_hw_unique_key));
		ecid = tegra_fuse_get_ecid();

		if ((hw_unique_key != NULL) && (ecid != NULL)) {
			snprintf(ecid_str, sizeof(ecid_str), "%08x%08x%08x%08x",
				 ecid->ecid[3], ecid->ecid[2], ecid->ecid[1],
				 ecid->ecid[0]);
			rc = nist_sp_800_108_cmac_kdf(ekb_rk_verified,
						      TEGRA_SE_KEY_128_SIZE,
						      ecid_str,
						      strlen(ecid_str),
						      "tee-hw-unique-key",
						      strlen("tee-hw-unique-key"),
						      HW_UNIQUE_KEY_LENGTH,
						      hw_unique_key->data);
			if (rc)
				gen_unique_key_fail = true;
		} else {
			gen_unique_key_fail = true;
		}
	}

	if (gen_unique_key_fail) {
		memset(&hwkey->data[0], 0, sizeof(hwkey->data));
		if (hw_unique_key != NULL) {
			free(hw_unique_key);
			hw_unique_key = NULL;
		}
	} else {
		memcpy(hwkey->data, hw_unique_key->data, HW_UNIQUE_KEY_LENGTH);
	}

	return TEE_SUCCESS;
}

/*
 * Get keys from ekb without check.
 *
 * NOTE:
 * Since this function can get any key from ekb without checking,
 * the caller should take the responsibility of checking the key type.
 */
static TEE_Result jetson_pta_get_key_by_type(struct user_key **ekb_key,
					 uint32_t key_type)
{
	struct user_key *tmp_key;

	TAILQ_FOREACH(tmp_key, &user_keys, link)
		if (tmp_key->type == key_type) {
			*ekb_key = tmp_key;
			return TEE_SUCCESS;
		}

	return TEE_ERROR_ITEM_NOT_FOUND;
}

static TEE_Result jetson_user_key_pta_get_user_key(uint8_t **key,
						   uint32_t *key_len,
						   uint32_t key_type)
{
	TEE_Result rc = TEE_SUCCESS;
	struct user_key *ekb_key = NULL;

	if (!key || !key_len)
		return TEE_ERROR_BAD_PARAMETERS;

	if (key_type >= EKB_USER_KEY_NUM_MAX)
		return TEE_ERROR_BAD_PARAMETERS;

	rc = jetson_pta_get_key_by_type(&ekb_key, key_type);
	if (rc)
		return rc;

	*key = ekb_key->data;
	*key_len = ekb_key->len;

	return rc;
}

static TEE_Result jetson_pta_get_ekb_key(uint32_t ptypes,
					 TEE_Param params[TEE_NUM_PARAMS])
{
	uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT,
					  TEE_PARAM_TYPE_MEMREF_OUTPUT,
					  TEE_PARAM_TYPE_NONE,
					  TEE_PARAM_TYPE_NONE);
	TEE_Result rc = TEE_SUCCESS;
	uint32_t key_type = params[0].value.a;
	uint8_t *key = NULL;
	uint32_t key_len = 0;

	if (exp_pt != ptypes)
		return TEE_ERROR_BAD_PARAMETERS;

	rc = jetson_user_key_pta_get_user_key(&key, &key_len, key_type);
	if (rc)
		return rc;

	if (key_len <= params[1].memref.size) {
		memcpy(params[1].memref.buffer, key, key_len);
		params[1].memref.size = key_len;
	} else {
		return TEE_ERROR_SHORT_BUFFER;
	}

	return rc;
}

static TEE_Result jetson_pta_get_random(uint32_t ptypes,
					TEE_Param params[TEE_NUM_PARAMS])
{
	uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
					  TEE_PARAM_TYPE_NONE,
					  TEE_PARAM_TYPE_NONE,
					  TEE_PARAM_TYPE_NONE);
	TEE_Result rc = TEE_SUCCESS;

	if (exp_pt != ptypes)
		return TEE_ERROR_BAD_PARAMETERS;

	rc = tegra_se_rng_get_random(params[0].memref.buffer,
				     params[0].memref.size);
	if (rc != TEE_SUCCESS) {
		memset(params[0].memref.buffer, 0, params[0].memref.size);
		params[0].memref.size = 0;
	}

	return rc;
}

static TEE_Result jetson_pta_gen_unique_key_by_ekb(uint32_t ptypes,
					TEE_Param params[TEE_NUM_PARAMS])
{
	uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT,
					  TEE_PARAM_TYPE_MEMREF_INPUT,
					  TEE_PARAM_TYPE_MEMREF_OUTPUT,
					  TEE_PARAM_TYPE_NONE);
	TEE_Result rc = TEE_SUCCESS;
	fuse_ecid_t *ecid;
	char ecid_str[36];
	uint8_t *key;
	uint32_t key_len;
	uint32_t key_type = params[0].value.a;

	if ((exp_pt != ptypes) ||
	    (params[1].memref.size == 0)) {
		return TEE_ERROR_BAD_PARAMETERS;
	}

	rc = jetson_user_key_pta_get_user_key(&key, &key_len, key_type);
	if (rc)
		return rc;

	ecid = tegra_fuse_get_ecid();
	snprintf(ecid_str, sizeof(ecid_str), "%08x%08x%08x%08x",
		 ecid->ecid[3], ecid->ecid[2], ecid->ecid[1],
		 ecid->ecid[0]);

	rc = nist_sp_800_108_cmac_kdf(key, key_len,
				      ecid_str,
				      strlen(ecid_str),
				      params[1].memref.buffer,
				      params[1].memref.size,
				      params[2].memref.size,
				      params[2].memref.buffer);

	return rc;
}

static TEE_Result jetson_pta_gen_key(uint32_t ptypes,
				     TEE_Param params[TEE_NUM_PARAMS])
{
	uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
					  TEE_PARAM_TYPE_MEMREF_INPUT,
					  TEE_PARAM_TYPE_MEMREF_INPUT,
					  TEE_PARAM_TYPE_MEMREF_OUTPUT);
	TEE_Result rc = TEE_SUCCESS;

	if ((exp_pt != ptypes) ||
	    (params[1].memref.size == 0) ||
	    (params[2].memref.size == 0)) {
		return TEE_ERROR_BAD_PARAMETERS;
	}

	rc = nist_sp_800_108_cmac_kdf(params[0].memref.buffer,
				      params[0].memref.size,
				      params[1].memref.buffer,
				      params[1].memref.size,
				      params[2].memref.buffer,
				      params[2].memref.size,
				      params[3].memref.size,
				      params[3].memref.buffer);

	return rc;
}

static TEE_Result jetson_pta_get_flag(uint32_t ptypes,
				      TEE_Param params[TEE_NUM_PARAMS])
{
	uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT,
					  TEE_PARAM_TYPE_VALUE_OUTPUT,
					  TEE_PARAM_TYPE_NONE,
					  TEE_PARAM_TYPE_NONE);
	TEE_Result rc = TEE_SUCCESS;

	if (exp_pt != ptypes)
		return TEE_ERROR_BAD_PARAMETERS;

	switch (params[0].value.a) {
	case LUKS_SRV_FLAG:
		break;
	default:
		return TEE_ERROR_ACCESS_DENIED;
	}

	params[1].value.a = jetson_service_flag & params[0].value.a;

	return rc;
}

static TEE_Result jetson_pta_set_flag(uint32_t ptypes,
				      TEE_Param params[TEE_NUM_PARAMS])
{
	uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT,
					  TEE_PARAM_TYPE_NONE,
					  TEE_PARAM_TYPE_NONE,
					  TEE_PARAM_TYPE_NONE);
	TEE_Result rc = TEE_SUCCESS;

	if (exp_pt != ptypes)
		return TEE_ERROR_BAD_PARAMETERS;

	switch (params[0].value.a) {
	case LUKS_SRV_FLAG:
		break;
	default:
		return TEE_ERROR_ACCESS_DENIED;
	}
	if (params[0].value.b)
		jetson_service_flag |= params[0].value.a;
	else
		jetson_service_flag &= ~params[0].value.a;

	return rc;
}

static TEE_Result jetson_pta_is_user_key_exists(uint32_t ptypes,
				      TEE_Param params[TEE_NUM_PARAMS])
{
	uint32_t key_type;
	uint8_t *key;
	uint32_t key_len;
	uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT,
					  TEE_PARAM_TYPE_VALUE_OUTPUT,
					  TEE_PARAM_TYPE_NONE,
					  TEE_PARAM_TYPE_NONE);
	if (exp_pt != ptypes)
		return TEE_ERROR_BAD_PARAMETERS;

	key_type = params[0].value.a;

	/* default key does not exist */
	params[1].value.a = 0;

	if (jetson_user_key_pta_get_user_key(&key, &key_len, key_type))
		goto out;

	if ((key_len != TEGRA_SE_KEY_128_SIZE)
			&& (key_len != TEGRA_SE_KEY_256_SIZE))
		goto out;

	/* key is exists */
	params[1].value.a = 1;

out:
	return TEE_SUCCESS;
}

static TEE_Result jetson_pta_decrypt_cpubl_payload(uint32_t ptypes,
				      TEE_Param params[TEE_NUM_PARAMS])
{
	TEE_Result rc = TEE_SUCCESS;
	uint32_t key_type = EKB_USER_KEY_KERNEL_ENCRYPTION;
	uint8_t *key;
	uint32_t key_len;
	static struct dec_ctx ctx = {0};
	uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INOUT,
					  TEE_PARAM_TYPE_VALUE_INPUT,
					  TEE_PARAM_TYPE_NONE,
					  TEE_PARAM_TYPE_NONE);
	if ((exp_pt != ptypes) ||
	    (params[0].memref.buffer == NULL) ||
	    (params[0].memref.size == 0))
		return TEE_ERROR_BAD_PARAMETERS;

	switch (params[1].value.a) {
	case JETSON_CPUBL_PAYLOAD_DECRYPTION_INIT:
		if (ctx.dec_phase != JETSON_CPUBL_PAYLOAD_DECRYPTION_PRE_INIT) {
			rc = TEE_ERROR_BAD_STATE;
			goto error;
		}

		rc = jetson_user_key_pta_get_user_key(&key, &key_len, key_type);
		if (rc)
			goto error;

		rc = jetson_decrypt_cpubl_payload_process_params(&ctx,
				key, key_len,
				(void *)params[0].memref.buffer, &params[0].memref.size);
		if (rc)
			goto error;

		rc = jetson_decrypt_cpubl_payload_init(&ctx);
		if (rc)
			goto error;

		ctx.dec_phase = JETSON_CPUBL_PAYLOAD_DECRYPTION_INIT;

		break;

	case JETSON_CPUBL_PAYLOAD_DECRYPTION_UPDATE:
		if (ctx.dec_phase != JETSON_CPUBL_PAYLOAD_DECRYPTION_INIT
				&& ctx.dec_phase != JETSON_CPUBL_PAYLOAD_DECRYPTION_UPDATE) {
			rc = TEE_ERROR_BAD_STATE;
			goto error;
		}

		rc = jetson_decrypt_cpubl_payload_update(&ctx,
				(void *)params[0].memref.buffer, params[0].memref.size,
				(void *)params[0].memref.buffer, &params[0].memref.size);
		if (rc)
			goto error;

		ctx.dec_phase = JETSON_CPUBL_PAYLOAD_DECRYPTION_UPDATE;

		break;

	case JETSON_CPUBL_PAYLOAD_DECRYPTION_FINAL:
		if (ctx.dec_phase != JETSON_CPUBL_PAYLOAD_DECRYPTION_INIT
				&& ctx.dec_phase != JETSON_CPUBL_PAYLOAD_DECRYPTION_UPDATE) {
			rc = TEE_ERROR_BAD_STATE;
			goto error;
		}

		rc = jetson_decrypt_cpubl_payload_final(&ctx,
				(void *)params[0].memref.buffer, params[0].memref.size,
				(void *)params[0].memref.buffer, &params[0].memref.size);

		memset(&ctx, 0, sizeof(struct dec_ctx));

		break;

	default:
		rc = TEE_ERROR_BAD_PARAMETERS;
	}

	return rc;

error:
	memset(&ctx, 0, sizeof(struct dec_ctx));
	return rc;
}

static TEE_Result invoke_command(void *psess __unused,
				 uint32_t cmd, uint32_t ptypes,
				 TEE_Param params[TEE_NUM_PARAMS])
{
	if (!(jetson_service_flag & PTA_SRV_FLAG))
		return TEE_ERROR_ACCESS_DENIED;

	switch(cmd) {
	case JETSON_USER_KEY_CMD_GET_EKB_KEY:
		return jetson_pta_get_ekb_key(ptypes, params);
	case JETSON_USER_KEY_CMD_GET_RANDOM:
		return jetson_pta_get_random(ptypes, params);
	case JETSON_USER_KEY_CMD_GEN_UNIQUE_KEY_BY_EKB:
		return jetson_pta_gen_unique_key_by_ekb(ptypes, params);
	case JETSON_USER_KEY_CMD_GEN_KEY:
		return jetson_pta_gen_key(ptypes, params);
	case JETSON_USER_KEY_CMD_GET_FLAG:
		return jetson_pta_get_flag(ptypes, params);
	case JETSON_USER_KEY_CMD_SET_FLAG:
		return jetson_pta_set_flag(ptypes, params);
	case JETSON_USER_KEY_CMD_IS_KEY_EXISTS:
		return jetson_pta_is_user_key_exists(ptypes, params);
	case JETSON_USER_KEY_CMD_DECRYPT_CPUBL_PAYLOAD:
		return jetson_pta_decrypt_cpubl_payload(ptypes, params);
	default:
		return TEE_ERROR_NOT_SUPPORTED;
	}
}

static TEE_Result open_session(uint32_t param_types __unused,
			       TEE_Param params[TEE_NUM_PARAMS] __unused,
			       void **sess_ctx __unused)
{
	struct ts_session *s = NULL;

	/* Check that we're called from a user TA */
	s = ts_get_calling_session();
	if (!s || !is_user_ta_ctx(s->ctx))
		return TEE_ERROR_ACCESS_DENIED;

	return TEE_SUCCESS;
}

pseudo_ta_register(.uuid = JETSON_USER_KEY_UUID,
		   .name = JETSON_USER_KEY_PTA_NAME,
		   .flags = PTA_DEFAULT_FLAGS,
		   .open_session_entry_point = open_session,
		   .invoke_command_entry_point = invoke_command);

static const char *const jetson_ekb_dt_match_table[] = {
	"jetson-ekb-blob",
};

TEE_Result jetson_user_key_pta_query_ftpm_prop(uint32_t ftpm_ekb_id,
					       uint8_t **data,
					       uint32_t *len)
{
	TEE_Result rc = TEE_SUCCESS;
	struct user_key *ekb_key = NULL;

	if ((ftpm_ekb_id < EKB_FTPM_SN) || (ftpm_ekb_id >= EKB_FTPM_ID_MAX))
		return TEE_ERROR_ITEM_NOT_FOUND;

	rc = jetson_pta_get_key_by_type(&ekb_key, ftpm_ekb_id);
	if (rc == TEE_SUCCESS) {
		*data = ekb_key->data;
		*len = ekb_key->len;
	}

	return rc;
}

static TEE_Result get_oem_key_valid(bool *valid)
{
	TEE_Result rc = TEE_SUCCESS;
	uint32_t bsi = 0;

	if (!valid)
		return TEE_ERROR_BAD_PARAMETERS;

	rc = tegra_fuse_get_bsi(&bsi);
	if (rc) {
		EMSG("Read fuse BSI failed: 0x%x", rc);
		return rc;
	}

	if ((bsi & BSI_OEM_KEY_VALID_MASK) == 0)
		*valid = false;
	else
		*valid = true;

	return rc;
}

static TEE_Result get_sec_mode_enabled(bool *enabled)
{
	TEE_Result rc = TEE_SUCCESS;
	uint32_t sec_mode = 0;

	if (!enabled)
		return TEE_ERROR_BAD_PARAMETERS;

	rc = tegra_fuse_get_sec_mode(&sec_mode);
	if (rc) {
		EMSG("Read fuse security mode failed: 0x%x", rc);
		return rc;
	}

	if ((sec_mode & SECURITY_MODE_ENABLED_MASK) == 0)
		*enabled = false;
	else
		*enabled = true;

	return rc;
}

TEE_Result jetson_user_key_pta_uefi_vars_auth(uint8_t *vars, uint32_t vars_len)
{
	TEE_Result rc = TEE_SUCCESS;
	uint32_t key_type = EKB_USER_KEY_UEFI_VAR_AUTH;
	uint8_t *key;
	uint32_t key_len = 16;
	void *cmac_ctx = NULL;
	bool key_valid;
	bool sec_enabled;
	/* test UEFI variable auth key: d9f7b49e3b6264985f1326f541bb43c9 */
	uint8_t uefi_vars_auth_key_test[] = {
		0xd9, 0xf7, 0xb4, 0x9e, 0x3b, 0x62, 0x64, 0x98,
		0x5f, 0x13, 0x26, 0xf5, 0x41, 0xbb, 0x43, 0xc9
	};

	if (!vars || vars_len < TEE_AES_BLOCK_SIZE)
		return TEE_ERROR_BAD_PARAMETERS;

	/* Get UEFI variables authentication key */
	rc = jetson_user_key_pta_get_user_key(&key, &key_len, key_type);
	if (rc) {
		if ((get_oem_key_valid(&key_valid) != TEE_SUCCESS) ||
				(get_sec_mode_enabled(&sec_enabled) != TEE_SUCCESS)) {
			EMSG("Failed to get oem key valid or get security mode !");
			goto error;
		} else {
			if (key_valid || sec_enabled) {
				EMSG("UEFI variable auth key not set !");
				goto error;
			} else {
				IMSG("WARNING: Test UEFI variable auth "
						"key is being used !");
				IMSG("WARNING: UEFI variable protection "
						"is not fully enabled !");
				key = uefi_vars_auth_key_test;
			}
		}
	}

	/* Calculate AES-CMAC for uefi variables */
	rc = crypto_mac_alloc_ctx(&cmac_ctx, TEE_ALG_AES_CMAC);
	if (rc)
		goto error;

	rc = crypto_mac_init(cmac_ctx, key, key_len);
	if (rc)
		goto error;

	rc = crypto_mac_update(cmac_ctx, vars, vars_len);
	if (rc)
		goto error;

	rc = crypto_mac_final(cmac_ctx, vars, TEE_AES_BLOCK_SIZE);
	if (rc)
		goto error;

error:
	if (cmac_ctx)
		crypto_mac_free_ctx(cmac_ctx);

	return rc;
}

static TEE_Result jetson_ekb_dt_init(void)
{
	void *fdt = NULL;
	int node = -1;
	uint32_t i = 0;
	paddr_t pbase;
	ssize_t sz;

	fdt = get_dt();
	if (!fdt) {
		EMSG("%s: DTB is not present.", __func__);
		return TEE_ERROR_ITEM_NOT_FOUND;
	}

	for (i = 0; i < ARRAY_SIZE(jetson_ekb_dt_match_table); i++) {
		node = fdt_node_offset_by_compatible(fdt, 0,
					jetson_ekb_dt_match_table[i]);
		if (node >= 0)
			break;
	}

	if (node < 0) {
		EMSG("%s: DT not found (%x).", __func__, node);
		return TEE_ERROR_ITEM_NOT_FOUND;
	}

	pbase = fdt_reg_base_address(fdt, node);
	if (pbase == DT_INFO_INVALID_REG)
		return TEE_ERROR_GENERIC;

	sz = fdt_reg_size(fdt, node);
	if (sz < 0)
		return TEE_ERROR_GENERIC;

	if (!core_mmu_add_mapping(MEM_AREA_RAM_SEC, pbase, sz)) {
		EMSG("Failed to map %zu bytes at PA 0x%"PRIxPA,
		     (size_t)sz, pbase);
		return TEE_ERROR_GENERIC;
	}

	ekb_base_addr = (vaddr_t)phys_to_virt(pbase, MEM_AREA_RAM_SEC, sz);
	if (!ekb_base_addr) {
		EMSG("Failed to get VA for PA 0x%"PRIxPA, pbase);
		return TEE_ERROR_GENERIC;
	}

	return TEE_SUCCESS;
}

static TEE_Result jetson_user_key_pta_init(void)
{
	TEE_Result rc = TEE_SUCCESS;
	struct user_key *ekb_key = NULL;

	if (!IS_ENABLED(CFG_DT)) {
		rc = TEE_ERROR_NOT_SUPPORTED;
		goto pta_init_fail;
	}

	rc = jetson_ekb_dt_init();
	if (rc)
		goto pta_init_fail;

	/* Derive keys from SE keyslots. */
	rc = hwkey_derivation_process();
	if(rc)
		goto pta_init_fail;

	/* EKB extraction */
	rc = ekb_extraction_process(ekb_base_addr);
	if (rc)
		goto pta_init_fail;

pta_init_fail:
	if (rc) {
		jetson_service_flag &= !PTA_SRV_FLAG;

		TAILQ_FOREACH(ekb_key, &user_keys, link) {
			if (ekb_key && ekb_key->data)
				free(ekb_key->data);
			free(ekb_key);
		}

		EMSG("%s: Failed (%x).", __func__, rc);
#if defined(CFG_JETSON_MANDATORY_EKB)
		panic();
#endif
	}

	return rc;
}

service_init_late(jetson_user_key_pta_init);
