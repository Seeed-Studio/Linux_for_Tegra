/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_VPU_APP_AUTH_H
#define PVA_KMD_VPU_APP_AUTH_H

#include "pva_kmd_shim_vpu_app_auth.h"
#include "pva_kmd_mutex.h"

/**
 * Maximum length of allowlist file path
 */
#define ALLOWLIST_FILE_LEN 128U

/**
 * Size of sha256 keys in bytes.
 */
#define NVPVA_SHA256_DIGEST_SIZE 32U

struct pva_kmd_device;
/**
 * Array of all VPU Hash'es
 */
struct vpu_hash_vector {
	/*! Number of Keys for this crc32_hash */
	uint32_t count;
	/*! Starting Index into Keys Array */
	uint32_t index;
	/*! CRC32 hash value */
	uint32_t crc32_hash;
};

/**
 * Stores sha256 key
 */
struct shakey {
	/** 256-bit (32 Bytes) SHA Key */
	uint8_t sha_key[NVPVA_SHA256_DIGEST_SIZE];
};

/**
 * Stores Hash Vector and Keys vector
 */
struct vpu_hash_key_pair {
	/*! Total number of Keys in binary file */
	uint32_t num_keys;
	/*! pointer to SHA keys Array. */
	struct shakey *psha_key;
	/*! Total number of Hashes in binary file */
	uint32_t num_hashes;
	/*! pointer to Array of Hash'es */
	struct vpu_hash_vector *pvpu_hash_vector;
	/*! pointer to data loaded from file (QNX Specific)*/
	uint8_t *ptr_file_data;
};

/**
 * Stores all the information related to pva vpu elf authentication.
 */
struct pva_vpu_auth {
	/** Stores crc32-sha256 of ELFs */
	struct vpu_hash_key_pair *vpu_hash_keys;
	pva_kmd_mutex_t allow_list_lock;
	/** Flag to check if allowlist is enabled */
	bool pva_auth_enable;
	/** Flag to track if the allow list is already parsed */
	bool pva_auth_allow_list_parsed;
	/** Stores the path to allowlist binary file. */
	char pva_auth_allowlist_path[ALLOWLIST_FILE_LEN + 1U];
};

enum pva_error pva_kmd_init_vpu_app_auth(struct pva_kmd_device *pva, bool ena);
void pva_kmd_deinit_vpu_app_auth(struct pva_kmd_device *pva);

enum pva_error pva_kmd_verify_exectuable_hash(struct pva_kmd_device *pva,
					      const uint8_t *dataptr,
					      size_t size);

enum pva_error pva_kmd_allowlist_parse(struct pva_kmd_device *pva);

#endif