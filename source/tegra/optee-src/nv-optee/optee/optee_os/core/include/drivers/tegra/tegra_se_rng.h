/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES.
 */

#ifndef __TEGRA_SE_RNG_H__
#define __TEGRA_SE_RNG_H__

#include <tee_api_types.h>

/*
 * Get random bytes from the SE RNG module
 *
 * *data_buf	[out] the output of the random data buffer
 * data_len	[in] the length of the random bytes
 */
TEE_Result tegra_se_rng_get_random(void *data_buf, uint32_t data_len);

#endif
