/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021-2023, NVIDIA Corporation.  All rights reserved.
 */

#ifndef PVA_SHA256_H
#define PVA_SHA256_H

#define U32(x)	((uint32_t)(x))

struct sha256_ctx_s {
	/*
	 * On bitlen:
	 *
	 * While we don't exceed 2^32 bit (2^29 byte) length for the input buffer,
	 * size_t is more efficient at least on RISC-V. This particular
	 * structure is needed to make Coverity happy.
	 */
	union {
		size_t bitlen;
		uint32_t bitlen_low;
	};
	uint32_t state[8];
};

/**
 * Initializes struct sha256_ctx_s
 *
 * \param[in] ctx  pointer of struct sha256_ctx_s context
 *
 * \return  void
 */
void sha256_init(struct sha256_ctx_s *ctx);

/**
 * \brief
 * Hash full blocks, in units of 64 bytes
 * can be called repeatedly with chunks of the message
 * to be hashed (len bytes at data).
 *
 * \param[in] ctx  pointer of struct sha256_ctx_s context
 * \param[in] data  pointer to the data block to be hashed
 * \param[in] len  length (in units of 64 bytes) of the data to be hashed.
 *
 * \return  void
 */
void
pva_sha256_update(struct sha256_ctx_s *ctx,
		  const void *data,
		  size_t len);

/**
 * \brief
 * Finalize the hash and keep the calcualted hash in out.
 * Required: input_size < 64. Call pva_sha256_update() first otherwise.
 *
 * \param[in] ctx  pointer of struct sha256_ctx_s context
 * \param[in] input pointer to the data block
 * (left over from \ref pva_sha256_update) to be hashed
 * \param[in] input_size size of the data block to hashed
 * (left over from \ref pva_sha256_update to be hashed)
 * \param[out] out places the calcuated sha256 key in out.
 *
 * \return void
 */
void
sha256_finalize(struct sha256_ctx_s *ctx,
		const void *input,
		size_t input_size,
		uint32_t out[8]);

/**
 * \brief
 * copy state information to ctx_out from ctx_in
 * \param[in] ctx_in input struct sha256_ctx_s
 * \param[out] ctx_out output struct sha256_ctx_s
 * \return void
 */
void sha256_copy(const struct sha256_ctx_s *ctx_in,
		 struct sha256_ctx_s *ctx_out);

#endif   /* PVA_SHA256_H */
