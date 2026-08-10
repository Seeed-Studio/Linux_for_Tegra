/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2021-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_SHA256_H
#define PVA_KMD_SHA256_H

#include "pva_api_types.h"

/**
 * @brief Utility macro to cast value to uint32_t
 *
 * @details Convenience macro for casting values to 32-bit unsigned integers.
 * Used throughout SHA256 implementation for type safety and clarity.
 */
#define U32(x) ((uint32_t)(x))

/**
 * @brief SHA256 hash computation context structure
 *
 * @details This structure maintains the state for incremental SHA256 hash
 * computation. It stores the bit length of processed data and the current
 * hash state array. The union for bitlen accommodates both size_t (for
 * efficiency on RISC-V and other architectures) and uint32_t (for Coverity
 * analysis) representations of the bit length.
 */
struct sha256_ctx {
	/**
	 * @brief Bit length of processed data
	 *
	 * @details Union to handle bit length efficiently while satisfying static analysis.
	 * While we don't exceed 2^32 bit (2^29 byte) length for input buffers,
	 * size_t is more efficient on at least RISC-V. This union structure
	 * is needed to make Coverity static analysis happy.
	 */
	union {
		/** @brief Bit length as size_t for efficiency */
		size_t bitlen;
		/** @brief Bit length as uint32_t for static analysis */
		uint32_t bitlen_low;
	};

	/**
	 * @brief SHA256 hash state array
	 *
	 * @details Array of 8 32-bit words representing the current SHA256 hash state.
	 * These values are updated during hash computation and contain the final
	 * hash result when computation is complete.
	 */
	uint32_t state[8];
};

/**
 * @brief Initialize SHA256 context for hash computation
 *
 * @details This function performs the following operations:
 * - Initializes the SHA256 context structure to known initial state
 * - Sets the bit length counter to zero
 * - Loads the SHA256 initial hash values into the state array
 * - Prepares the context for incremental hash computation
 * - Ensures context is in valid state for subsequent operations
 *
 * This function must be called before any hash data can be processed
 * using @ref sha256_update() or @ref sha256_finalize().
 *
 * @param[out] ctx  Pointer to @ref sha256_ctx context structure to initialize
 *                  Valid value: non-null
 */
void sha256_init(struct sha256_ctx *ctx);

/**
 * @brief Process full blocks of data for SHA256 hash computation
 *
 * @details This function performs the following operations:
 * - Processes complete 64-byte blocks of input data
 * - Updates the SHA256 state with the processed block data
 * - Maintains bit length counter for the total processed data
 * - Can be called repeatedly with chunks of the message to be hashed
 * - Optimized for processing large amounts of data efficiently
 * - Handles block-aligned data for optimal performance
 *
 * This function processes data in 64-byte chunks and can be called multiple
 * times to hash large inputs incrementally. The input length should be a
 * multiple of 64 bytes for optimal operation.
 *
 * @param[in, out] ctx   Pointer to @ref sha256_ctx context structure
 *                       Valid value: non-null, must be initialized
 * @param[in] data       Pointer to input data block to be hashed
 *                       Valid value: non-null
 * @param[in] len        Length of data in bytes (should be multiple of 64)
 *                       Valid range: [0 .. SIZE_MAX]
 */
void sha256_update(struct sha256_ctx *ctx, const void *data, size_t len);

/**
 * @brief Finalize SHA256 hash computation and produce result
 *
 * @details This function performs the following operations:
 * - Processes any remaining input data less than 64 bytes
 * - Applies SHA256 padding according to the algorithm specification
 * - Includes the total bit length in the final padding block
 * - Completes the hash computation and produces the final result
 * - Stores the 256-bit hash result in the output array
 * - Handles final block processing with proper padding
 *
 * This function must be called after all data has been processed with
 * @ref sha256_update() to complete the hash computation. The input_size
 * parameter must be less than 64 bytes (remaining data after block processing).
 *
 * @param[in, out] ctx        Pointer to @ref sha256_ctx context structure
 *                            Valid value: non-null, must be initialized
 * @param[in] input           Pointer to remaining data block to be hashed
 *                            Valid value: non-null if input_size > 0,
 *                            may be null if input_size == 0
 * @param[in] input_size      Size of remaining data block in bytes
 *                            Valid range: [0 .. 63]
 * @param[out] out            Array to store the computed SHA256 hash result
 *                            Valid value: non-null, must have space for 8 uint32_t values
 */
void sha256_finalize(struct sha256_ctx *ctx, const void *input,
		     size_t input_size, uint32_t out[8]);

/**
 * @brief Copy SHA256 context state from one context to another
 *
 * @details This function performs the following operations:
 * - Copies the complete state from input context to output context
 * - Transfers bit length counter and hash state array
 * - Creates an independent copy that can be used separately
 * - Enables branched hash computation from a common base
 * - Preserves the state for continued computation on both contexts
 * - Useful for scenarios requiring multiple hash computations from same base
 *
 * This function allows creating checkpoints in hash computation or
 * branching hash computation to produce multiple related hashes efficiently.
 *
 * @param[in] ctx_in   Pointer to source @ref sha256_ctx structure to copy from
 *                     Valid value: non-null, must be initialized
 * @param[out] ctx_out Pointer to destination @ref sha256_ctx structure to copy to
 *                     Valid value: non-null
 */
void sha256_copy(const struct sha256_ctx *ctx_in, struct sha256_ctx *ctx_out);

#endif /* PVA_SHA256_H */
