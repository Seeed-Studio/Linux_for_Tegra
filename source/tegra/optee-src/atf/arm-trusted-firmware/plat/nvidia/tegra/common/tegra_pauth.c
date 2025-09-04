/*
 * Copyright (c) 2021, NVIDIA Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <arch_helpers.h>
#include <cdefs.h>
#include <stdint.h>

#define TEGRA_APKEY_FIXED_PATTERN		U(0x55AAAA5555AAAA55)

/*
 * This is only a toy implementation to generate a fixed 128-bit key
 * from sp, x30 and fix values.
 *
 * A production system must re-implement this function to generate
 * keys from a reliable randomness source.
 */
uint128_t plat_init_apkey(void)
{
	uint64_t return_addr = (uint64_t)__builtin_return_address(0U);
	uint64_t frame_addr = (uint64_t)__builtin_frame_address(0U);
	uint64_t fixed_pattern = TEGRA_APKEY_FIXED_PATTERN;

	/* Generate 128-bit key */
	uint64_t key_lo = (return_addr << 13) ^ frame_addr ^ fixed_pattern;
	uint64_t key_hi = (frame_addr << 15) ^ return_addr ^ fixed_pattern;

	return ((uint128_t)(key_hi) << 64) | key_lo;
}
