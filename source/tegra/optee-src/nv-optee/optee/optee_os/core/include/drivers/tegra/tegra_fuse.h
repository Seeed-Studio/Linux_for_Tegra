/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2021-2024, NVIDIA CORPORATION & AFFILIATES.
 */

#ifndef __TEGRA_FUSE_H__
#define __TEGRA_FUSE_H__

#include <tee_api_types.h>

typedef struct tegra_fuse_ecid {
	uint32_t ecid[4];
} fuse_ecid_t;

#define FUSE_BOOT_SECURITY_INFO_0	0x268
#define BSI_OEM_KEY_VALID_MASK		0x200
#define FUSE_SECURITY_MODE_0		0x1a0
#define SECURITY_MODE_ENABLED_MASK	0x1

/*
 * Get an 128-bit ECID from fuse
 */
fuse_ecid_t* tegra_fuse_get_ecid(void);

/*
 * Get a 64-bit ECID from fuse
 * [out] val: the buffer saves the ECID
 */
TEE_Result tegra_fuse_get_64bit_ecid(uint64_t *val);

/*
 * Get Boot Security Info from fuse
 */
TEE_Result tegra_fuse_get_bsi(uint32_t *val);

/*
 * Get Security Mode from fuse
 */
TEE_Result tegra_fuse_get_sec_mode(uint32_t *val);

/*
 * Get the serial number from fuse ODMID and ODMINFO
 * [out]   sn:    the buffer saves the serial number
 * [inout] size:  the size of the buffer, will be updated by the function
 */
TEE_Result tegra_fuse_get_sn(uint8_t **sn, uint32_t *size);

#endif
