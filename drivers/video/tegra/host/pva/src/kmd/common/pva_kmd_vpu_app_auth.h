/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_VPU_APP_AUTH_H
#define PVA_KMD_VPU_APP_AUTH_H

#include "pva_kmd_shim_vpu_app_auth.h"
#include "pva_kmd_mutex.h"

/**
 * @brief Maximum length of allowlist file path
 *
 * @details Maximum length allowed for the allowlist file path string.
 * This includes space for the null terminator. The allowlist file
 * contains authorized VPU executable hashes for security validation.
 * Value: 128
 */
#define ALLOWLIST_FILE_LEN 128U

/**
 * @brief Size of SHA256 digest in bytes
 *
 * @details Size of SHA256 hash digests used for VPU executable authentication.
 * SHA256 produces a 256-bit (32-byte) hash value that uniquely identifies
 * the content of VPU executables for security verification.
 * Value: 32
 */
#define NVPVA_SHA256_DIGEST_SIZE 32U

struct pva_kmd_device;

/**
 * @brief Hash vector structure for VPU executable authentication
 *
 * @details This structure stores information about a group of VPU executables
 * that share the same CRC32 hash value. It provides efficient lookup by first
 * comparing CRC32 values, then using the index to access the corresponding
 * SHA256 keys for detailed verification. This two-stage approach optimizes
 * the authentication process for large allowlists.
 */
struct vpu_hash_vector {
	/**
	 * @brief Number of SHA256 keys associated with this CRC32 hash
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t count;

	/**
	 * @brief Starting index into the SHA256 keys array
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t index;

	/**
	 * @brief CRC32 hash value for fast initial comparison
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t crc32_hash;
};

/**
 * @brief SHA256 key structure for secure authentication
 *
 * @details This structure stores a single SHA256 digest used for VPU executable
 * authentication. The 256-bit hash provides cryptographically strong verification
 * of executable integrity and authenticity, ensuring only authorized VPU
 * applications can be loaded and executed.
 */
struct shakey {
	/**
	 * @brief 256-bit (32-byte) SHA256 digest
	 *
	 * @details Array containing the SHA256 hash value for a VPU executable
	 */
	uint8_t sha_key[NVPVA_SHA256_DIGEST_SIZE];
};

/**
 * @brief Combined hash vector and key storage structure
 *
 * @details This structure maintains the complete allowlist data including both
 * CRC32 hash vectors for fast lookup and SHA256 keys for secure verification.
 * It provides efficient storage and access to authentication data loaded from
 * the allowlist file, supporting both performance optimization and security
 * requirements.
 */
struct vpu_hash_key_pair {
	/**
	 * @brief Total number of SHA256 keys in the allowlist
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t num_keys;

	/**
	 * @brief Pointer to array of SHA256 keys
	 * Valid value: non-null if num_keys > 0, null if num_keys == 0
	 */
	struct shakey *psha_key;

	/**
	 * @brief Total number of hash vectors in the allowlist
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t num_hashes;

	/**
	 * @brief Pointer to array of hash vectors
	 * Valid value: non-null if num_hashes > 0, null if num_hashes == 0
	 */
	struct vpu_hash_vector *pvpu_hash_vector;

	/**
	 * @brief Pointer to raw file data (platform-specific)
	 *
	 * @details Pointer to data loaded from allowlist file (QNX specific)
	 * Valid value: platform-dependent, may be null
	 */
	uint8_t *ptr_file_data;
};

/**
 * @brief VPU application authentication context
 *
 * @details This structure maintains all information related to VPU executable
 * authentication including allowlist data, enable state, and synchronization
 * primitives. It provides a complete authentication framework for verifying
 * VPU applications against an authorized allowlist before execution.
 */
struct pva_vpu_auth {
	/**
	 * @brief Pointer to hash-key pair data containing allowlist information
	 *
	 * @details Stores CRC32-SHA256 hash pairs for VPU executables
	 */
	struct vpu_hash_key_pair *vpu_hash_keys;

	/**
	 * @brief Mutex protecting allowlist operations
	 */
	pva_kmd_mutex_t allow_list_lock;

	/**
	 * @brief Flag indicating if authentication is enabled
	 * Valid values: true (enabled), false (disabled)
	 */
	bool pva_auth_enable;

	/**
	 * @brief Flag tracking if allowlist has been parsed
	 * Valid values: true (parsed), false (not parsed)
	 */
	bool pva_auth_allow_list_parsed;

	/**
	 * @brief Path to allowlist binary file
	 *
	 * @details Stores the filesystem path to the allowlist binary file
	 * containing authorized VPU executable hashes
	 */
	char pva_auth_allowlist_path[ALLOWLIST_FILE_LEN + 1U];
};

/**
 * @brief Initialize VPU application authentication system
 *
 * @details This function performs the following operations:
 * - Initializes the VPU authentication context structure
 * - Sets up mutex protection for allowlist operations
 * - Configures authentication enable state based on parameter
 * - Prepares the system for allowlist loading and parsing
 * - Allocates necessary resources for authentication operations
 * - Establishes security policy for VPU executable verification
 *
 * The authentication system provides security enforcement for VPU applications
 * by verifying executable hashes against an authorized allowlist before
 * permitting execution on the VPU hardware.
 *
 * @param[in] pva  Pointer to @ref pva_kmd_device structure
 *                 Valid value: non-null, must be initialized
 * @param[in] ena  Enable flag for authentication system
 *                 Valid values: true (enable), false (disable)
 *
 * @retval PVA_SUCCESS              Authentication system initialized successfully
 * @retval PVA_NOMEM                  Failed to allocate authentication resources
 * @retval PVA_INTERNAL               Failed to initialize synchronization mutex
 */
enum pva_error pva_kmd_init_vpu_app_auth(struct pva_kmd_device *pva, bool ena);

/**
 * @brief Deinitialize VPU application authentication system
 *
 * @details This function performs the following operations:
 * - Cleans up allowlist data and associated memory allocations
 * - Destroys mutex protection for allowlist operations
 * - Frees hash-key pair data structures
 * - Releases platform-specific file data allocations
 * - Invalidates authentication context for the device
 * - Ensures proper cleanup of all authentication resources
 *
 * This function should be called during device shutdown to ensure
 * proper cleanup of all authentication-related resources and prevent
 * memory leaks.
 *
 * @param[in] pva  Pointer to @ref pva_kmd_device structure
 *                 Valid value: non-null, must have been initialized with authentication
 */
void pva_kmd_deinit_vpu_app_auth(struct pva_kmd_device *pva);

/**
 * @brief Verify VPU executable hash against allowlist
 *
 * @details This function performs the following operations:
 * - Calculates CRC32 hash of the provided executable data
 * - Searches hash vectors for matching CRC32 values
 * - Computes SHA256 digest of the executable for secure verification
 * - Compares SHA256 hash against authorized keys in allowlist
 * - Returns verification result indicating authentication status
 * - Logs authentication attempts and results for security auditing
 *
 * The verification process uses a two-stage approach: fast CRC32 filtering
 * followed by cryptographically secure SHA256 verification. This ensures
 * both performance and security for executable authentication.
 *
 * @param[in] pva      Pointer to @ref pva_kmd_device structure
 *                     Valid value: non-null, must be initialized with authentication
 * @param[in] dataptr  Pointer to VPU executable data to verify
 *                     Valid value: non-null
 * @param[in] size     Size of executable data in bytes
 *                     Valid range: [1 .. SIZE_MAX]
 *
 * @retval PVA_SUCCESS                  Executable hash verified successfully
 * @retval PVA_EACCES                   Executable not found in allowlist
 * @retval PVA_NOT_IMPL                 Authentication system not enabled
 * @retval PVA_NOENT                    Allowlist not loaded or parsed
 * @retval PVA_INTERNAL                 Failed to compute executable hash
 */
enum pva_error pva_kmd_verify_exectuable_hash(struct pva_kmd_device *pva,
					      const uint8_t *dataptr,
					      size_t size);

/**
 * @brief Parse allowlist file and load authentication data
 *
 * @details This function performs the following operations:
 * - Opens and reads the allowlist file from the configured path
 * - Validates the allowlist file format and structure
 * - Parses hash vectors and SHA256 keys from the file data
 * - Allocates memory for hash-key pair storage
 * - Populates authentication data structures for runtime use
 * - Establishes efficient lookup tables for hash verification
 * - Marks allowlist as parsed and ready for authentication
 *
 * The allowlist file contains authorized VPU executable hashes in a binary
 * format optimized for efficient parsing and lookup during authentication.
 * The parsed data enables fast verification of VPU applications.
 *
 * @param[in] pva  Pointer to @ref pva_kmd_device structure
 *                 Valid value: non-null, must be initialized with authentication
 *
 * @retval PVA_SUCCESS                  Allowlist parsed successfully
 * @retval PVA_NOENT                    Allowlist file not found at configured path
 * @retval PVA_INTERNAL                 Failed to read allowlist file
 * @retval PVA_INVAL                    Allowlist file format is invalid
 * @retval PVA_NOMEM                    Failed to allocate memory for allowlist data
 * @retval PVA_BAD_PARAMETER_ERROR      Allowlist contains no valid entries
 */
enum pva_error pva_kmd_allowlist_parse(struct pva_kmd_device *pva);

#endif