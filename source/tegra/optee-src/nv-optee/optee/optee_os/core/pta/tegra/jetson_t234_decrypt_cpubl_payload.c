// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES.
 */

#include <stdio.h>
#include <string.h>
#include "utee_defines.h"
#include "crypto/crypto.h"
#include "drivers/tegra/tegra_se_keyslot.h"
#include "jetson_decrypt_cpubl_payload.h"

/*
 * A software-based NIST-SP 800-108 KDF.
 * derives keys from a key in a key buffer.
 *
 * key		[in] input key for derivation.
 * key_len	[in] length in bytes of the input key.
 * context	[in] a pointer to a NIST-SP 800-108 context string.
 * context_len	[in] length in bytes of the contexct.
 * label	[in] a pointer to a NIST-SP 800-108 label string.
 * label_len	[in] length in bytes of the label.
 * dk_len	[in] length of the derived key in bytes;
 *		     may be 16 (128 bits) or any multiple of 16.
 * out_dk 	[out] a pointer to the derived key. The function stores
 *		      its result in this location.
 */
static TEE_Result nist_sp_800_108_hmac_kdf(uint8_t *key,
					   uint32_t key_len,
					   uint8_t *context,
					   uint32_t context_len,
					   uint8_t *label,
					   uint32_t label_len,
					   uint32_t dk_len,
					   uint8_t *out_dk)
{
	TEE_Result res = TEE_SUCCESS;
	uint8_t zero_byte[] = { 0U };
	uint8_t zero_context[] = { 0U };
	uint8_t *p, tmp_buf[TEE_SHA256_HASH_SIZE] = { 0U };
	uint32_t tmp_len;
	uint32_t counter[] = { TEE_U32_TO_BIG_ENDIAN( 1 ) };
	uint32_t L[] = { TEE_U32_TO_BIG_ENDIAN(dk_len * 8) };
	uint8_t *message = NULL;
	uint8_t *mptr;
	uint32_t msg_len;
	void *hmac_ctx = NULL;

	if ((key_len != TEGRA_SE_KEY_128_SIZE) &&
			(key_len != TEGRA_SE_KEY_256_SIZE))
		return TEE_ERROR_BAD_PARAMETERS;

	if ((dk_len % TEE_AES_BLOCK_SIZE) != 0)
		return TEE_ERROR_BAD_PARAMETERS;

	if (!key || !label || !out_dk)
		return TEE_ERROR_BAD_PARAMETERS;

	if (!context_len){
		context = zero_context;
		context_len = sizeof(zero_context);
	}

	/*
	 *  Regarding to NIST-SP 800-108
	 *  message = counter || label || 0 || context || L
	 *
	 *  A || B = The concatenation of binary strings A and B.
	 */
	msg_len = sizeof(counter) + context_len + label_len + sizeof(L) +
		sizeof(zero_byte);
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

	/* HMAC-SHA256 */
	res = crypto_mac_alloc_ctx(&hmac_ctx, TEE_ALG_HMAC_SHA256);
	if (res)
		goto kdf_error;

	tmp_len = dk_len;
	p = out_dk;

	while (tmp_len > 0) {
		crypto_mac_init(hmac_ctx, key, key_len);
		crypto_mac_update(hmac_ctx, message, msg_len);

		if (tmp_len >= TEE_SHA256_HASH_SIZE) {
			crypto_mac_final(hmac_ctx, p, TEE_SHA256_HASH_SIZE);
			tmp_len -= TEE_SHA256_HASH_SIZE;
			p += TEE_SHA256_HASH_SIZE;
		} else {
			crypto_mac_final(hmac_ctx, tmp_buf,
					 TEE_SHA256_HASH_SIZE);
			memcpy(p, tmp_buf, tmp_len);
			break;
		}

		/* Update the counter for next use */
		message[0] += 1;
	}

	crypto_mac_free_ctx(hmac_ctx);

kdf_error:
	free(message);
	return res;
}

static TEE_Result get_one_and_zeros_padding(uint8_t *input,
					    uint32_t input_len,
					    size_t *data_len)
{
	uint32_t i;
	unsigned char done = 0, prev_done, bad;

	if (NULL == input || NULL == data_len)
		return TEE_ERROR_BAD_PARAMETERS;

	if ((input_len % TEGRA_SE_AES_BLOCK_SIZE) != 0)
		return TEE_ERROR_BAD_PARAMETERS;

	bad = 0x80;
	*data_len = 0;
	for (i = input_len; i > 0; i--) {
		prev_done = done;
		done |= (input[i - 1] != 0);
		*data_len |= (i - 1) * (done != prev_done);
		bad ^= input[i - 1] * (done != prev_done);
	}

	if (bad != 0)
		*data_len = input_len;

	return TEE_SUCCESS;
}

TEE_Result jetson_decrypt_cpubl_payload_process_params(struct dec_ctx *dec_ctx,
						       uint8_t *key,
						       uint32_t key_len,
						       void *img,
						       size_t *img_size)
{
	TEE_Result res;
	uint8_t *bch;

	if (!dec_ctx)
		return TEE_ERROR_BAD_PARAMETERS;

	if (!key || (key_len == 0))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!img || !img_size)
		return TEE_ERROR_BAD_PARAMETERS;

	if (*img_size < T234_BCH_HEADER_SIZE)
		return TEE_ERROR_BAD_PARAMETERS;

	bch = img;

	if (memcmp(bch + BCH_HEADER_MAGIC_OFFSET, BCH_HEADER_MAGIC,
		   BCH_HEADER_MAGIC_SIZE))
		return TEE_ERROR_BAD_PARAMETERS;

	memcpy(&(dec_ctx->payload_size),
	       bch + T234_BCH_HEADER_BINARY_LEN_OFFSET,
	       T234_BCH_HEADER_BINARY_LEN_SIZE);
	memcpy(dec_ctx->aad_in_hdr, bch + T234_BCH_HEADER_AAD_OFFSET,
	       T234_BCH_HEADER_AAD_SIZE);
	memcpy(dec_ctx->iv_in_hdr, bch + T234_BCH_HEADER_IV_OFFSET,
	       T234_BCH_HEADER_IV_SIZE);
	memcpy(dec_ctx->tag_in_hdr, bch + T234_BCH_HEADER_TAG_OFFSET,
	       T234_BCH_HEADER_TAG_SIZE);
	dec_ctx->key_len = TEGRA_SE_KEY_256_SIZE;

	res = nist_sp_800_108_hmac_kdf(key,
				       key_len,
				       bch + T234_BCH_HEADER_VER_OFFSET,
				       T234_BCH_HEADER_VER_SIZE,
				       bch + T234_BCH_HEADER_DERIVE_STR_OFFSET,
				       T234_BCH_HEADER_DERIVE_STR_SIZE,
				       dec_ctx->key_len,
				       dec_ctx->key);
	if (res != TEE_SUCCESS)
		EMSG("Derive DK failed with res(%x)", res);

	return res;
}

TEE_Result jetson_decrypt_cpubl_payload_init(struct dec_ctx *dec_ctx)
{
	TEE_Result res = TEE_ERROR_GENERIC;

	if (!dec_ctx)
		return TEE_ERROR_BAD_PARAMETERS;

	res = crypto_authenc_alloc_ctx(&(dec_ctx->ctx), TEE_ALG_AES_GCM);
	if (res != TEE_SUCCESS) {
		EMSG("crypto_authenc_alloc_ctx failed with res(%x)", res);
		goto out;
	}

	res = crypto_authenc_init(dec_ctx->ctx,
				  TEE_MODE_DECRYPT,
				  dec_ctx->key,
				  dec_ctx->key_len,
				  dec_ctx->iv_in_hdr,
				  T234_BCH_HEADER_IV_SIZE,
				  T234_BCH_HEADER_TAG_SIZE,
				  T234_BCH_HEADER_AAD_SIZE,
				  dec_ctx->payload_size);
	if (res != TEE_SUCCESS) {
		EMSG("crypto_authenc_init failed with res(%x)", res);
		goto out;
	}

	res = crypto_authenc_update_aad(dec_ctx->ctx,
					TEE_MODE_DECRYPT,
					dec_ctx->aad_in_hdr,
					T234_BCH_HEADER_AAD_SIZE);
	if (res != TEE_SUCCESS)
		EMSG("crypto_authenc_update_aad failed with res(%x)", res);

out:
	if ((res != TEE_SUCCESS) && (dec_ctx->ctx != NULL))
		crypto_authenc_free_ctx(dec_ctx->ctx);

	return res;
}

TEE_Result jetson_decrypt_cpubl_payload_update(struct dec_ctx *dec_ctx,
					       const uint8_t *src_data,
					       uint32_t src_len,
					       uint8_t *dst_data,
					       size_t *dst_len)
{
	TEE_Result res = TEE_ERROR_GENERIC;

	if (!dec_ctx)
		return TEE_ERROR_BAD_PARAMETERS;

	if (!src_data || src_len <= 0)
		return TEE_ERROR_BAD_PARAMETERS;

	if (!dst_data || !dst_len)
		return TEE_ERROR_BAD_PARAMETERS;

	res = crypto_authenc_update_payload(dec_ctx->ctx, TEE_MODE_DECRYPT,
					    src_data, src_len,
					    dst_data, dst_len);
	if (res != TEE_SUCCESS)
		EMSG("crypto_authenc_update_payload failed with res(%x)", res);

	if ((res != TEE_SUCCESS) && (dec_ctx->ctx != NULL))
		crypto_authenc_free_ctx(dec_ctx->ctx);

	return res;
}


TEE_Result jetson_decrypt_cpubl_payload_final(struct dec_ctx *dec_ctx,
					      const uint8_t *src_data,
					      uint32_t src_len,
					      uint8_t *dst_data,
					      size_t *dst_len)
{
	TEE_Result res = TEE_ERROR_GENERIC;
	size_t final_len;

	if (!dec_ctx)
		return TEE_ERROR_BAD_PARAMETERS;

	if (!src_data || src_len <= 0)
		return TEE_ERROR_BAD_PARAMETERS;

	if (!dst_data || !dst_len)
		return TEE_ERROR_BAD_PARAMETERS;

	final_len = *dst_len;

	res = crypto_authenc_dec_final(dec_ctx->ctx,
				       src_data, src_len,
				       dst_data, &final_len,
				       dec_ctx->tag_in_hdr,
				       T234_BCH_HEADER_TAG_SIZE);
	if (res != TEE_SUCCESS) {
		EMSG("crypto_authenc_dec_final failed with res(%x)", res);
		goto out;
	}

	res = get_one_and_zeros_padding(dst_data, final_len, dst_len);
	if (res != TEE_SUCCESS)
		EMSG("unpadding failed with res(%x)", res);

out:
	if (dec_ctx->ctx)
		crypto_authenc_free_ctx(dec_ctx->ctx);

	return res;
}
