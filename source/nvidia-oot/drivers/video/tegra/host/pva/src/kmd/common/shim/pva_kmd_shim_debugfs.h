/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_SHIM_DEBUGFS_H
#define PVA_KMD_SHIM_DEBUGFS_H
#include "pva_api.h"
#include "pva_kmd_tegra_stats.h"

/**
 * @brief Create a debugfs entry for a boolean variable.
 *
 * @details This function performs the following operations:
 * - Creates a debugfs file entry with the specified name
 * - Associates the entry with the provided boolean variable
 * - Enables reading and writing of the boolean value through debugfs
 * - Provides user space interface for runtime boolean configuration
 * - Uses platform-appropriate debugfs creation mechanisms
 *
 * The created debugfs entry allows user space applications to read
 * and modify the boolean variable value for debugging and testing
 * purposes. The entry is automatically cleaned up when the device
 * is removed or @ref pva_kmd_debugfs_remove_nodes() is called.
 *
 * @param[in, out] pva   Pointer to @ref pva_kmd_device structure
 *                       Valid value: non-null
 * @param[in] name       Name of the debugfs entry to create
 *                       Valid value: non-null, null-terminated string
 * @param[in, out] val   Pointer to boolean variable to expose
 *                       Valid value: non-null pointer to bool
 */
/* Shim function with platform-specific implementations (QNX, Linux, Native) */
void pva_kmd_debugfs_create_bool(struct pva_kmd_device *pva, const char *name,
				 bool *val);

/**
 * @brief Create a debugfs entry for a 32-bit unsigned integer variable.
 *
 * @details This function performs the following operations:
 * - Creates a debugfs file entry with the specified name
 * - Associates the entry with the provided 32-bit unsigned integer variable
 * - Enables reading and writing of the integer value through debugfs
 * - Provides user space interface for runtime integer configuration
 * - Uses platform-appropriate debugfs creation mechanisms
 *
 * The created debugfs entry allows user space applications to read
 * and modify the 32-bit unsigned integer variable value for debugging
 * and testing purposes. The entry is automatically cleaned up when
 * the device is removed or @ref pva_kmd_debugfs_remove_nodes() is called.
 *
 * @param[in, out] pva   Pointer to @ref pva_kmd_device structure
 *                       Valid value: non-null
 * @param[in] name       Name of the debugfs entry to create
 *                       Valid value: non-null, null-terminated string
 * @param[in, out] val   Pointer to 32-bit unsigned integer variable to expose
 *                       Valid value: non-null pointer to uint32_t
 */
/* Shim function with platform-specific implementations (QNX, Linux, Native) */
void pva_kmd_debugfs_create_u32(struct pva_kmd_device *pva, const char *name,
				uint32_t *val);

/**
 * @brief Create a debugfs file entry with custom file operations.
 *
 * @details This function performs the following operations:
 * - Creates a debugfs file entry with the specified name
 * - Associates the entry with custom file operation handlers
 * - Enables custom read/write/ioctl operations through debugfs
 * - Provides flexible user space interface for complex debugging operations
 * - Uses platform-appropriate debugfs creation mechanisms
 *
 * This function allows creation of sophisticated debugfs interfaces
 * with custom behavior defined by the file operations structure.
 * The custom operations enable complex debugging scenarios beyond
 * simple variable access.
 *
 * @param[in, out] pva   Pointer to @ref pva_kmd_device structure
 *                       Valid value: non-null
 * @param[in] name       Name of the debugfs entry to create
 *                       Valid value: non-null, null-terminated string
 * @param[in] fops       Pointer to custom file operations structure
 *                       Valid value: non-null pointer to @ref pva_kmd_file_ops
 *
 * @retval PVA_SUCCESS           Debugfs file created successfully
 * @retval PVA_NOMEM             Insufficient memory for debugfs entry
 * @retval PVA_INVAL             Invalid parameters provided
 * @retval PVA_INTERNAL          Debugfs creation failed
 */
/* Shim function with platform-specific implementations (QNX, Linux, Native) */
enum pva_error pva_kmd_debugfs_create_file(struct pva_kmd_device *pva,
					   const char *name,
					   struct pva_kmd_file_ops *fops);

/**
 * @brief Remove all debugfs nodes for the PVA device.
 *
 * @details This function performs the following operations:
 * - Removes all debugfs entries created for the PVA device
 * - Cleans up debugfs directory structure
 * - Releases debugfs-related resources
 * - Ensures proper cleanup during device removal
 * - Uses platform-appropriate debugfs cleanup mechanisms
 *
 * This function provides complete cleanup of debugfs resources
 * when the PVA device is being removed or during error cleanup.
 * It safely removes all debugfs entries created by previous
 * create operations.
 *
 * @param[in, out] pva   Pointer to @ref pva_kmd_device structure
 *                       Valid value: non-null
 */
/* Shim function with platform-specific implementations (QNX, Linux, Native) */
void pva_kmd_debugfs_remove_nodes(struct pva_kmd_device *pva);

/**
 * @brief Copy data from user space to kernel space.
 *
 * @details This function performs the following operations:
 * - Safely copies data from user space memory to kernel space
 * - Validates user space memory accessibility
 * - Handles page faults and memory protection
 * - Prevents kernel crashes from invalid user space pointers
 * - Uses platform-appropriate user space access mechanisms
 *
 * This function provides safe user space memory access for debugfs
 * operations that need to copy data from user space applications
 * to kernel buffers. It includes proper error handling for invalid
 * user space addresses.
 *
 * @param[out] dst       Destination buffer in kernel space
 *                       Valid value: non-null, sufficient size
 * @param[in] src        Source buffer in user space
 *                       Valid value: user space address
 * @param[in] size       Number of bytes to copy
 *                       Valid range: [0 .. UINT64_MAX]
 *
 * @retval 0             All data copied successfully
 * @retval bytes_left    Number of bytes that could not be copied
 */
unsigned long pva_kmd_copy_data_from_user(void *dst, const void *src,
					  uint64_t size);

/**
 * @brief Copy data from kernel space to user space.
 *
 * @details This function performs the following operations:
 * - Safely copies data from kernel space memory to user space
 * - Validates user space memory accessibility for writing
 * - Handles page faults and memory protection
 * - Prevents kernel crashes from invalid user space pointers
 * - Uses platform-appropriate user space access mechanisms
 *
 * This function provides safe user space memory access for debugfs
 * operations that need to copy data from kernel buffers to user
 * space applications. It includes proper error handling for invalid
 * user space addresses.
 *
 * @param[out] to        Destination buffer in user space
 *                       Valid value: user space address
 * @param[in] from       Source buffer in kernel space
 *                       Valid value: non-null kernel address
 * @param[in] size       Number of bytes to copy
 *                       Valid range: [0 .. ULONG_MAX]
 *
 * @retval 0             All data copied successfully
 * @retval bytes_left    Number of bytes that could not be copied
 */
unsigned long pva_kmd_copy_data_to_user(void *to, const void *from,
					unsigned long size);

/**
 * @brief Convert string to long integer.
 *
 * @details This function performs the following operations:
 * - Parses the input string to extract a long integer value
 * - Supports different number bases (decimal, hexadecimal, octal)
 * - Handles leading whitespace and sign characters
 * - Provides platform-appropriate string to integer conversion
 * - Uses safe parsing mechanisms to prevent buffer overflows
 *
 * This function enables debugfs operations to parse integer values
 * from string input provided by user space applications. It supports
 * various number formats commonly used in debugging interfaces.
 *
 * @param[in] str        Null-terminated string to parse
 *                       Valid value: non-null, null-terminated string
 * @param[in] base       Number base for parsing
 *                       Valid values: 0 (auto-detect), 8 (octal),
 *                       10 (decimal), 16 (hexadecimal)
 *
 * @retval parsed_value  Long integer value parsed from string
 */
unsigned long pva_kmd_strtol(const char *str, int base);

/**
 * @brief Simulate entering SC7 power state.
 *
 * @details This function performs the following operations:
 * - Simulates the system entering SC7 (suspend-to-RAM) power state
 * - Triggers PVA power management suspend sequences
 * - Tests power state transition handling
 * - Provides debugging interface for power management validation
 * - Enables testing of suspend/resume functionality
 *
 * This function is used for testing and debugging power management
 * behavior without requiring actual system suspend operations.
 * It allows validation of PVA suspend handling in controlled
 * debugging scenarios.
 *
 * @param[in, out] pva   Pointer to @ref pva_kmd_device structure
 *                       Valid value: non-null
 *
 * @retval PVA_SUCCESS           SC7 entry simulation completed successfully
 * @retval PVA_INTERNAL          Device not in appropriate state for SC7
 * @retval PVA_TIMEDOUT          SC7 entry operation timed out
 * @retval PVA_INTERNAL          Hardware error during SC7 simulation
 */
enum pva_error pva_kmd_simulate_enter_sc7(struct pva_kmd_device *pva);

/**
 * @brief Simulate exiting SC7 power state.
 *
 * @details This function performs the following operations:
 * - Simulates the system exiting SC7 (suspend-to-RAM) power state
 * - Triggers PVA power management resume sequences
 * - Tests power state transition handling
 * - Provides debugging interface for power management validation
 * - Enables testing of suspend/resume functionality
 *
 * This function is used for testing and debugging power management
 * behavior without requiring actual system resume operations.
 * It allows validation of PVA resume handling in controlled
 * debugging scenarios.
 *
 * @param[in, out] pva   Pointer to @ref pva_kmd_device structure
 *                       Valid value: non-null
 *
 * @retval PVA_SUCCESS           SC7 exit simulation completed successfully
 * @retval PVA_INTERNAL          Device not in appropriate state for SC7 exit
 * @retval PVA_TIMEDOUT          SC7 exit operation timed out
 * @retval PVA_INTERNAL          Hardware error during SC7 simulation
 */
enum pva_error pva_kmd_simulate_exit_sc7(struct pva_kmd_device *pva);

#endif //PVA_KMD_SHIM_DEBUGFS_H