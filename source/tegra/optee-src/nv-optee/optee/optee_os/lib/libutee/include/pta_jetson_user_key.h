/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2021-2024, NVIDIA CORPORATION & AFFILIATES.
 */

#ifndef __JETSON_USER_KEY_PTA_H__
#define __JETSON_USER_KEY_PTA_H__

/*
 * Each trusted app UUID should have a unique UUID that is
 * generated from a UUID generator such as
 * https://www.uuidgenerator.net/
 *
 * UUID : {e9e156e8-e161-4c8a-91a9-0bba5e247ee8}
 */
#define JETSON_USER_KEY_UUID \
		{ 0xe9e156e8, 0xe161, 0x4c8a, \
			{0x91, 0xa9, 0x0b, 0xba, 0x5e, 0x24, 0x7e, 0xe8} }

/*
 * JETSON_USER_KEY_CMD_GET_EKB_KEY - Query the EKB key
 * param[0] in (value) a: EKB key index
 * param[1] out (memref) key buffer, key size
 * param[2] unused
 * param[3] unused
 */
#define JETSON_USER_KEY_CMD_GET_EKB_KEY			0

/*
 * EKB user symmetric keys.
 */
typedef enum {
	EKB_USER_KEY_KERNEL_ENCRYPTION = 1,
	EKB_USER_KEY_DISK_ENCRYPTION,
	EKB_USER_KEY_DEVICE_ID_CERT,
	EKB_USER_KEY_UEFI_VAR_AUTH,
	EKB_USER_KEY_NUM_MAX,
} ekb_key_index_t;

/*
 * JETSON_USER_KEY_CMD_GET_RANDOM - Get random bytes from RNG
 * param[0] out (memref) RNG data, RNG size
 * param[1] unused
 * param[2] unused
 * param[3] unused
 */
#define JETSON_USER_KEY_CMD_GET_RANDOM			1

/*
 * JETSON_USER_KEY_CMD_GEN_UNIQUE_KEY_BY_EKB - Generate a unique key by EKB key
 * param[0] in (value) a: EKB key index
 * param[1] in (memref) label string, length
 * param[2] out (memref) key, size
 * param[3] unused
 */
#define JETSON_USER_KEY_CMD_GEN_UNIQUE_KEY_BY_EKB	2

/*
 * JETSON_USER_KEY_CMD_GEN_KEY - Generate a key by a input key
 * param[0] in (memref) input key, size
 * param[1] in (memref) context string, length
 * param[2] in (memref) label string, length
 * param[3] out (memref) output key, size
 */
#define JETSON_USER_KEY_CMD_GEN_KEY			3

#define LUKS_SRV_FLAG					(1U << 0)
#define PTA_SRV_FLAG					(1U << 31)

/*
 * JETSON_USER_KEY_CMD_GET_FLAG - Get the service flag
 * param[0] in (value) a: flag
 * param[1] out (value) a: flag status
 * param[2] unused
 * param[3] unused
 */
#define JETSON_USER_KEY_CMD_GET_FLAG			4

/*
 * JETSON_USER_KEY_CMD_SET_FLAG - Set the service flag
 * param[0] in (value) a: flag, b: flag status
 * param[1] unused
 * param[2] unused
 * param[3] unused
 */
#define JETSON_USER_KEY_CMD_SET_FLAG			5

/*
 * JETSON_USER_KEY_CMD_IS_KEY_EXISTS - Check if key is exists
 * param[0] in (value), A: key type
 * param[1] out (value), A: indicates if key is exists
 * param[2] unused
 * param[3] unused
 */
#define JETSON_USER_KEY_CMD_IS_KEY_EXISTS		6

/*
 * JETSON_USER_KEY_CMD_DECRYPT_CPUBL_PAYLOAD - Decrypt CPUBL payload(Kernel/Kernel-dtb)
 * param[0] inout (memref): encrypt/decrypt image&size
 * param[1] input (value): A: decryption phase: init/update/final
 * param[2] unused
 * param[3] unused
 */
#define JETSON_USER_KEY_CMD_DECRYPT_CPUBL_PAYLOAD	7

#endif
