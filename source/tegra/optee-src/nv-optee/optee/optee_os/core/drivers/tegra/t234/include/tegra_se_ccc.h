/*
 * SPDX-FileCopyrightText: Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#ifndef __TEGRA_SE_CCC_H__
#define __TEGRA_SE_CCC_H__

#include <tegra_driver_se.h>

typedef int status_t;
typedef enum optee_tegra_se_cdev_id_e {
	OPTEE_TEGRA_SE_CDEV_SE0  = 0, /**< SE0 device [0] */
	OPTEE_TEGRA_SE_CDEV_PKA1 = 1, /**< PKA device [1] */
	OPTEE_TEGRA_SE_CDEV_RNG1 = 2, /**< RNG1 device [2] */
} optee_tegra_se_cdev_id_t;

status_t tegra_init_crypto_devices(void);
status_t tegra_se_map_device(optee_tegra_se_cdev_id_t device_id, void **base_va);
status_t tegra_se_ccc_configure_se0(vaddr_t base);
status_t tegra_se_ccc_configure_rng1(vaddr_t base);
status_t tegra_se_ccc_aes_encrypt(uint8_t *src, size_t src_len,
			uint8_t *dst, size_t dst_len, se_aes_op_mode mode,
			uint32_t keyslot, uint32_t key_len, uint8_t *iv);
status_t tegra_se_ccc_set_aes_keyslot(uint32_t keyslot, uint8_t *key,
			uint32_t key_len, se_aes_op_mode mode);
status_t tegra_se_ccc_clear_aes_keyslot(uint32_t keyslot);
status_t tegra_se_ccc_aes_genrnd(uint8_t *output, uint32_t output_len);

#if defined(CFG_TEGRA_SE_USE_TEST_KEYS)
status_t tegra_se_ccc_set_test_oem_keys(const uint8_t *oem_k1, const uint8_t *oem_k2);
#endif

#endif
