// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES.
 */
#ifndef JETSON_DECRYPT_CPUBL_PAYLOAD_H
#define JETSON_DECRYPT_CPUBL_PAYLOAD_H

#include <tee_api_types.h>

#define JETSON_CPUBL_PAYLOAD_DECRYPTION_PRE_INIT	0
#define JETSON_CPUBL_PAYLOAD_DECRYPTION_INIT		1
#define JETSON_CPUBL_PAYLOAD_DECRYPTION_UPDATE		2
#define JETSON_CPUBL_PAYLOAD_DECRYPTION_FINAL		3

/* common macro */
#define BCH_HEADER_MAGIC				"NVDA"
#define BCH_HEADER_MAGIC_OFFSET				0U
#define BCH_HEADER_MAGIC_SIZE				4U

#if defined(PLATFORM_FLAVOR_t234)
/* T234 BCH Header macro */
#define T234_BCH_HEADER_SIZE				8192U
#define T234_BCH_HEADER_AAD_OFFSET			5120U
#define T234_BCH_HEADER_AAD_SIZE			80U
#define T234_BCH_HEADER_BINARY_LEN_OFFSET		5124U
#define T234_BCH_HEADER_BINARY_LEN_SIZE			4U
#define T234_BCH_HEADER_VER_OFFSET			5136U
#define T234_BCH_HEADER_VER_SIZE			4U
#define T234_BCH_HEADER_DERIVE_STR_OFFSET		5168U
#define T234_BCH_HEADER_DERIVE_STR_SIZE			16U
#define T234_BCH_HEADER_IV_OFFSET			5188U
#define T234_BCH_HEADER_IV_SIZE				12U
#define T234_BCH_HEADER_TAG_OFFSET			5200U
#define T234_BCH_HEADER_TAG_SIZE			16U
#endif

#if defined(PLATFORM_FLAVOR_t194)
/* T194 BCH Header macro */
#define T194_BCH_HEADER_SIZE				4096U
#define T194_BCH_HEADER_BINARY_LEN_OFFSET		2996U
#define T194_BCH_HEADER_BINARY_LEN_SIZE			4U
#define T194_BCH_HEADER_IV_SIZE				16U
#define T194_BCH_HEADER_DIGEST_OFFSET			3040U
#define T194_BCH_HEADER_DIGEST_LEN			32U
#endif

struct dec_ctx {
	void *ctx;
	uint32_t dec_phase;
	uint32_t payload_size;
	uint32_t key_len;
	uint8_t key[TEGRA_SE_KEY_256_SIZE];
#if defined(PLATFORM_FLAVOR_t194)
	void *hash_ctx;
	uint8_t iv_in_hdr[T194_BCH_HEADER_IV_SIZE];
	uint8_t hash_in_hdr[T194_BCH_HEADER_DIGEST_LEN];
#endif
#if defined(PLATFORM_FLAVOR_t234)
	uint8_t iv_in_hdr[T234_BCH_HEADER_IV_SIZE];
	uint8_t aad_in_hdr[T234_BCH_HEADER_AAD_SIZE];
	uint8_t tag_in_hdr[T234_BCH_HEADER_TAG_SIZE];
#endif
};

TEE_Result jetson_decrypt_cpubl_payload_process_params(struct dec_ctx *dec_ctx,
		uint8_t *key, uint32_t key_len,
		void *img, size_t *img_size);

TEE_Result jetson_decrypt_cpubl_payload_init(struct dec_ctx *dec_ctx);

TEE_Result jetson_decrypt_cpubl_payload_update(struct dec_ctx *dec_ctx,
		const uint8_t *src_data, uint32_t src_len,
		uint8_t *dst_data, size_t *dst_len);

TEE_Result jetson_decrypt_cpubl_payload_final(struct dec_ctx *dec_ctx,
		const uint8_t *src_data, uint32_t src_len,
		uint8_t *dst_data, size_t *dst_len);
#endif /* JETSON_DECRYPT_CPUBL_PAYLOAD_H */
