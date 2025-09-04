/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES.
 */

#include <config.h>
#include <drivers/tegra/tegra_se_kdf.h>
#include <drivers/tegra/tegra_se_keyslot.h>
#include <initcall.h>
#include <io.h>
#include <kernel/boot.h>
#include <stdlib.h>
#include <string.h>
#include <tegra_se_mgnt.h>
#include <tegra_driver_srv_intf.h>
#include <tegra_driver_se.h>
#include <utee_defines.h>

#define WORD_SIZE	sizeof(uint32_t)
#define WORD_SIZE_MASK	0x3
#define QUAD_NUM_BYTES	16
#define QUAD_NUM_WORDS	4

#define SE_KEYTABLE_SLOT_SHIFT		4
#define SE_KEYTABLE_SLOT(x)		(x << SE_KEYTABLE_SLOT_SHIFT)
#define SE_KEYTABLE_QUAD_SHIFT		2
#define SE_KEYTABLE_WORD_SHIFT		0
#define SE_KEYTABLE_QUAD(x)		(x << SE_KEYTABLE_QUAD_SHIFT)
#define SE_KEYTABLE_WORD(x)		(x << SE_KEYTABLE_WORD_SHIFT)

#define SE_KEYTABLE_DATA0_REG_OFFSET	0x2c0 // SE0_AES0_CRYPTO_KEYTABLE_DATA_0
#define SE_KEYTABLE_REG_OFFSET		0x2bc // SE0_AES0_CRYPTO_KEYTABLE_ADDR_0

struct tegra_se_cmac_context {
	se_aes_keyslot_t	keyslot;	/* SE key slot */
	uint32_t		keylen;		/* key lenght */
	uint8_t			k1[TEGRA_SE_AES_BLOCK_SIZE];	/* key 1 */
	uint8_t			k2[TEGRA_SE_AES_BLOCK_SIZE];	/* key 2 */
	void			*data;		/* data for CMAC operation */
	uint32_t		dlen;		/* data length */
};
typedef struct tegra_se_cmac_context se_cmac_ctx;

static vaddr_t se_va_base = 0;

/* Forward declaration */
static TEE_Result write_aes_keyslot(void *p_data, size_t data_len,
				    uint32_t key_quad_sel,
				    uint32_t key_slot_index);

static TEE_Result se_aes_encrypt(uint8_t *src, size_t src_len,
					uint8_t *dst, size_t dst_len, se_aes_op_mode mode,
					uint32_t keyslot, uint8_t *iv)
{
	TEE_Result rc = TEE_SUCCESS;
	uint32_t crypto_cfg_value = 0, se_cfg_value = 0;

	se_cfg_value = se_aes_get_config(mode, TEGRA_SE_KEY_128_SIZE);
	crypto_cfg_value = se_aes_get_crypto_config(mode,
						    keyslot, true);

	/* Accquire SE mutex */
	rc = se_mutex_acquire(se_va_base);
	if (rc != TEE_SUCCESS) {
		EMSG("%s: Failed to accquire SE (%x)", __func__, rc);
		return rc;
	}

	if (iv)
		write_aes_keyslot(iv, TEGRA_SE_AES_IV_SIZE, TEGRA_SE_AES_QUAD_ORG_IV,
				keyslot);

	rc = se_aes_start_operation(se_va_base,
				se_cfg_value, crypto_cfg_value,
				src, src_len, dst, dst_len);

	/* Release SE mutex */
	se_mutex_release(se_va_base);

	return rc;
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

static inline void write_aes_keytable_word(vaddr_t se_base, uint32_t offset,
					   uint32_t val)
{
	io_write32(se_base + SE_KEYTABLE_REG_OFFSET, offset);
	io_write32(se_base + SE_KEYTABLE_DATA0_REG_OFFSET, val);
}

static TEE_Result write_aes_keyslot(void *p_data, size_t data_len,
				    uint32_t key_quad_sel,
				    uint32_t key_slot_index)
{
	uint32_t num_words, word_end;
	uint32_t data_size;
	uint32_t i = 0, val = 0;
	uint32_t *p_key;
	uint8_t quad = 0, pkt = 0;

	if (!p_data || !data_len) {
		EMSG("%s: Invalid key data or length.", __func__);
		return TEE_ERROR_BAD_PARAMETERS;
	}

	p_key = p_data;

	if (key_slot_index >= SE_AES_KEYSLOT_COUNT) {
		EMSG("%s: Invalid SE keyslot.", __func__);
		return TEE_ERROR_BAD_PARAMETERS;
	}

	if ((data_len & WORD_SIZE_MASK) != 0) {
		EMSG("%s: Key length %ld is not a multiple of word size.",
		     __func__, data_len);
		return TEE_ERROR_BAD_PARAMETERS;
	}

	num_words = data_len / WORD_SIZE;

	/* Boundary check */
	word_end = SE_KEYTABLE_QUAD(key_quad_sel) + SE_KEYTABLE_WORD(num_words);
	if (word_end > SE_KEYTABLE_SLOT(1)) {
		EMSG("%s: Key range exceeds current key slot by %u words.",
		     __func__, word_end - SE_KEYTABLE_SLOT(1));
		return TEE_ERROR_BAD_PARAMETERS;
	}

	quad = key_quad_sel;
	if (key_quad_sel == TEGRA_SE_AES_QUAD_KEYS_256)
		quad = TEGRA_SE_AES_QUAD_KEYS_128;

	/* Write data to the key table */
	data_size = QUAD_NUM_BYTES;

	do {
		pkt = SE_KEYTABLE_SLOT(key_slot_index) |
		      SE_KEYTABLE_QUAD(quad);

		for (i = 0; i < data_size; i += 4, data_len -= 4) {
			val = pkt | SE_KEYTABLE_WORD(i/WORD_SIZE);
			write_aes_keytable_word(se_va_base, val, *p_key++);
		}

		data_size = data_len;
		quad = TEGRA_SE_AES_QUAD_KEYS_256;
	} while(data_len);

	return TEE_SUCCESS;
}

static TEE_Result clear_aes_keyslot(uint32_t keyslot, uint32_t slot_type)
{
	uint8_t zero_key[] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	uint32_t keylen;

	switch (slot_type) {
	case TEGRA_SE_AES_QUAD_KEYS_256:
		keylen = TEGRA_SE_KEY_256_SIZE;
		break;
	default:
		keylen = TEGRA_SE_KEY_128_SIZE;
		break;
	};

	return write_aes_keyslot(zero_key, keylen, slot_type, keyslot);
}

static TEE_Result se_write_aes_keyslot(uint8_t *input_key, uint32_t keylen,
				       uint32_t key_quad_sel,
				       se_aes_keyslot_t keyslot)
{
	TEE_Result rc = TEE_SUCCESS;

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

	/* Accquire SE mutex */
	rc = se_mutex_acquire(se_va_base);
	if (rc != TEE_SUCCESS) {
		EMSG("%s: Failed to accquire SE (%x)", __func__, rc);
		return rc;
	}

	rc = write_aes_keyslot(input_key, keylen, key_quad_sel, keyslot);

	/* Release SE mutex */
	se_mutex_release(se_va_base);

	return rc;
}

static TEE_Result se_clear_aes_keyslots(void)
{
	TEE_Result rc = TEE_SUCCESS;
	uint32_t i;

	/* Accquire SE mutex */
	rc = se_mutex_acquire(se_va_base);
	if (rc != TEE_SUCCESS) {
		EMSG("%s: Failed to accquire SE (%x)", __func__, rc);
		return rc;
	}

	for (i=0; i < TEGRA_SE_AES_QUAD_MAX; i++) {
		clear_aes_keyslot(SE_AES_KEYSLOT_11, i);
		clear_aes_keyslot(SE_AES_KEYSLOT_12, i);
		clear_aes_keyslot(SE_AES_KEYSLOT_13, i);

		/* If the SSK is used in Kernel, don't clear it. */
		clear_aes_keyslot(SE_AES_KEYSLOT_15, i);
	}

	/* Release SE mutex */
	se_mutex_release(se_va_base);

	return rc;
}

static void make_sub_key(uint8_t *subkey, uint8_t *in, uint32_t size)
{
	uint8_t msb;
	uint32_t i;

	msb = in[0] >> 7;

	/* Left shift one bit */
	subkey[0] = in[0] << 1;
	for (i = 1; i < size; i++) {
		subkey[i - 1] |= in[i] >> 7;
		subkey[i] = in[i] << 1;
	}

	if (msb)
		subkey[size - 1] ^= 0x87;
}

static se_cmac_ctx *se_cmac_new(void)
{
	se_cmac_ctx *se_cmac;

	se_cmac = calloc(1, sizeof(se_cmac_ctx));
	if (se_cmac == NULL)
		EMSG("%s: Calloc failed.", __func__);

	return se_cmac;
}

static void se_cmac_free(se_cmac_ctx *se_cmac)
{
	if (!se_cmac)
		return;

	free(se_cmac);
}

static TEE_Result se_cmac_init(se_cmac_ctx *se_cmac, se_aes_keyslot_t keyslot,
			       uint32_t keylen)
{
	TEE_Result rc = TEE_SUCCESS;
	uint32_t config, crypto_config;
	uint8_t *pbuf;

	if (se_cmac == NULL) {
		EMSG("%s: Invalid SE CMAC context.", __func__);
		return TEE_ERROR_BAD_PARAMETERS;
	}

	if ((keylen != TEGRA_SE_KEY_128_SIZE) &&
	    (keylen != TEGRA_SE_KEY_256_SIZE)) {
		EMSG("%s: Invalid key size.", __func__);
		return TEE_ERROR_BAD_PARAMETERS;
	}

	pbuf = calloc(1, TEGRA_SE_AES_BLOCK_SIZE);
	if (!pbuf) {
		EMSG("%s: Calloc failed.", __func__);
		return TEE_ERROR_OUT_OF_MEMORY;
	}

	se_cmac->keyslot = keyslot;
	se_cmac->keylen = keylen;

	/*
	 * Load the key
	 *
	 * 1. In case of using fuse key, the key should be pre-loaded into
	 *    the dedicated keyslot before using Tegra SE AES-CMAC APIs.
	 *
	 * 2. In case of using user-defined key, please pre-load the key
	 *    into the keyslot you want to use before using Tegra SE AES-CMAC
	 *    APIs.
	 */

	/* Accquire SE mutex */
	rc = se_mutex_acquire(se_va_base);
	if (rc != TEE_SUCCESS) {
		EMSG("%s: Failed to accquire SE (%x)", __func__, rc);
		goto se_init_fail;
	}

	/* Load zero IV */
	write_aes_keyslot(pbuf, TEGRA_SE_AES_IV_SIZE, TEGRA_SE_AES_QUAD_ORG_IV,
			  keyslot);

	config = se_aes_get_config(SE_AES_OP_MODE_CBC, keylen);
	crypto_config = se_aes_get_crypto_config(SE_AES_OP_MODE_CBC, keyslot, true);
	rc = se_aes_start_operation(se_va_base, config, crypto_config,
				    pbuf, TEGRA_SE_AES_BLOCK_SIZE,
				    pbuf, TEGRA_SE_AES_BLOCK_SIZE);
	if (rc != TEE_SUCCESS) {
		EMSG("%s: Failed to start SE operation.", __func__);
		goto se_cmac_init_fail;
	}

	make_sub_key(se_cmac->k1, pbuf, TEGRA_SE_AES_BLOCK_SIZE);
	make_sub_key(se_cmac->k2, se_cmac->k1, TEGRA_SE_AES_BLOCK_SIZE);

se_cmac_init_fail:
	/* Release SE mutex */
	se_mutex_release(se_va_base);
se_init_fail:
	free(pbuf);

	return rc;
}

static TEE_Result se_cmac_update(se_cmac_ctx *se_cmac, void *data,
				 uint32_t dlen)
{
	if ((!se_cmac) || (!data) || (!dlen)) {
		EMSG("%s: Invalid argument.", __func__);
		return TEE_ERROR_BAD_PARAMETERS;
	}

	se_cmac->data = data;
	se_cmac->dlen = dlen;

	return TEE_SUCCESS;
}

static TEE_Result se_cmac_final(se_cmac_ctx *se_cmac, void *out,
				uint32_t *outlen)
{
	TEE_Result rc = TEE_SUCCESS;
	uint32_t config, crypto_config;
	uint32_t blocks_to_process, last_block_bytes = 0, i;
	bool padding_needed = false, use_orig_iv = true;
	uint8_t *iv, *buf, *last_block;

	if ((!se_cmac) || (!out) || (!outlen)) {
		EMSG("%s: Invalid argument.", __func__);
		return TEE_ERROR_BAD_PARAMETERS;
	}

	iv = calloc(1, TEGRA_SE_AES_BLOCK_SIZE);
	buf = calloc(1, TEGRA_SE_AES_BLOCK_SIZE);
	last_block = calloc(1, TEGRA_SE_AES_BLOCK_SIZE);
	if ((!iv) || (!buf) || (!last_block)) {
		EMSG("%s: Calloc failed.", __func__);
		return TEE_ERROR_OUT_OF_MEMORY;
	}

	*outlen = TEGRA_SE_AES_BLOCK_SIZE;

	blocks_to_process = se_cmac->dlen / TEGRA_SE_AES_BLOCK_SIZE;

	/* Number of bytes less than block size */
	if ((se_cmac->dlen % TEGRA_SE_AES_BLOCK_SIZE) || !blocks_to_process) {
		padding_needed = true;
		last_block_bytes = se_cmac->dlen % TEGRA_SE_AES_BLOCK_SIZE;
	} else {
		blocks_to_process--;
		last_block_bytes = TEGRA_SE_AES_BLOCK_SIZE;
	}

	/* Accquire SE mutex */
	rc = se_mutex_acquire(se_va_base);
	if (rc != TEE_SUCCESS) {
		EMSG("%s: Failed to accquire SE (%x)", __func__, rc);
		goto se_fail;
	}

	/* Processing all blocks except last block */
	if (blocks_to_process) {
		uint32_t total = blocks_to_process * TEGRA_SE_AES_BLOCK_SIZE;

		/* Write zero IV */
		write_aes_keyslot(iv, TEGRA_SE_AES_IV_SIZE,
				  TEGRA_SE_AES_QUAD_ORG_IV, se_cmac->keyslot);

		config = se_aes_get_config(SE_AES_OP_MODE_CMAC,
					   se_cmac->keylen);
		crypto_config = se_aes_get_crypto_config(SE_AES_OP_MODE_CMAC,
							 se_cmac->keyslot,
							 use_orig_iv);
		rc = se_aes_start_operation(se_va_base, config, crypto_config,
					    se_cmac->data, total,
					    buf, TEGRA_SE_AES_BLOCK_SIZE);
		if (rc != TEE_SUCCESS) {
			EMSG("%s: Failed to start SE operation.", __func__);
			goto err_out;
		}

		use_orig_iv = false;
	}

	memcpy(last_block,
	       ((uint8_t *)se_cmac->data + se_cmac->dlen - last_block_bytes),
	       last_block_bytes);

	/* Processing last block */
	if (padding_needed) {
		last_block[last_block_bytes] = 0x80;

		/* XOR with K2 */
		for (i = 0; i < TEGRA_SE_AES_BLOCK_SIZE; i++)
			last_block[i] ^= se_cmac->k2[i];
	} else {
		/* XOR with K1 */
		for (i = 0; i < TEGRA_SE_AES_BLOCK_SIZE; i++)
			last_block[i] ^= se_cmac->k1[i];
	}

	if (use_orig_iv)
		write_aes_keyslot(iv, TEGRA_SE_AES_IV_SIZE,
				  TEGRA_SE_AES_QUAD_ORG_IV, se_cmac->keyslot);
	else
		write_aes_keyslot(buf, TEGRA_SE_AES_IV_SIZE,
				  TEGRA_SE_AES_QUAD_UPDTD_IV, se_cmac->keyslot);

	config = se_aes_get_config(SE_AES_OP_MODE_CMAC, se_cmac->keylen);
	crypto_config = se_aes_get_crypto_config(SE_AES_OP_MODE_CMAC,
						 se_cmac->keyslot,
						 use_orig_iv);
	rc = se_aes_start_operation(se_va_base, config, crypto_config,
				    last_block, TEGRA_SE_AES_BLOCK_SIZE,
				    out, TEGRA_SE_AES_BLOCK_SIZE);
	if (rc != TEE_SUCCESS)
		EMSG("%s: Failed to start SE operation.", __func__);

err_out:
	/* Release SE mutex */
	se_mutex_release(se_va_base);
se_fail:
	free(iv);
	free(buf);
	free(last_block);

	return rc;
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
	uint32_t msg_len, i, n, cmac_len;
	se_cmac_ctx *cmac_ctx = NULL;
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
	cmac_ctx = se_cmac_new();
	if (cmac_ctx == NULL) {
		rc = TEE_ERROR_OUT_OF_MEMORY;
		goto kdf_error;
	}

	/* n: iterations of the PRF count */
	n = dk_len / TEE_AES_BLOCK_SIZE;

	if (key_len == TEGRA_SE_KEY_256_SIZE)
		se_cmac_init(cmac_ctx, keyslot, TEGRA_SE_KEY_256_SIZE);
	else
		se_cmac_init(cmac_ctx, keyslot, TEGRA_SE_KEY_128_SIZE);

	for (i = 0; i < n; i++) {
		/* Update the counter */
		message[0] = i + 1;

		se_cmac_update(cmac_ctx, message, msg_len);
		se_cmac_final(cmac_ctx, (out_dk + (i * TEE_AES_BLOCK_SIZE)),
			      &cmac_len);
	}

	se_cmac_free(cmac_ctx);

kdf_error:
	free(message);
	return rc;
}

static TEE_Result tegra_se_init(void)
{
	TEE_Result rc;

	if (IS_ENABLED(CFG_DT))
		rc = tegra_se_map_regs(&se_va_base);
	else
		rc = iomap_pa2va(TEGRA_SE_BASE, TEGRA_SE_SIZE, &se_va_base);

	if (rc == TEE_SUCCESS) {
		tegra_drv_srv_intf_t *drv_srv_intf;

		drv_srv_intf = tegra_drv_srv_intf_get();
		if (drv_srv_intf != NULL) {
			drv_srv_intf->aes_ecb_kdf = se_aes_ecb_kdf;
			drv_srv_intf->aes_encrypt = se_aes_encrypt;
			drv_srv_intf->nist_sp_800_108_cmac_kdf =
						se_nist_sp_800_108_cmac_kdf;
			drv_srv_intf->write_aes_keyslot = se_write_aes_keyslot;
			drv_srv_intf->clear_aes_keyslots =
						se_clear_aes_keyslots;
		} else {
			return TEE_ERROR_NOT_SUPPORTED;
		}
	}

	return rc;
}

service_init(tegra_se_init);
