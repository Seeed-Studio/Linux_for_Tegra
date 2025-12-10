/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_SHIM_UTILS_H
#define PVA_KMD_SHIM_UTILS_H
#include "pva_api.h"
struct pva_kmd_context;

/**
 * @brief Allocate memory for KMD's private use.
 *
 * @details This function performs the following operations:
 * - Allocates memory of the specified size for KMD internal use
 * - Initializes all allocated memory to zero
 * - Uses platform-appropriate memory allocation mechanisms
 * - Returns a pointer to the allocated memory region
 * - Ensures memory is properly aligned for the target platform
 *
 * The allocated memory is guaranteed to be zero-initialized and must
 * be freed using @ref pva_kmd_free() to prevent memory leaks. The
 * allocation is suitable for KMD internal data structures and buffers.
 *
 * @param[in] size  Size of memory to allocate in bytes
 *                  Valid range: [1 .. UINT64_MAX]
 *
 * @retval non-null  Pointer to allocated and zero-initialized memory
 * @retval NULL      Memory allocation failed
 */
void *pva_kmd_zalloc(uint64_t size);

/**
 * @brief Free memory allocated by pva_kmd_zalloc.
 *
 * @details This function performs the following operations:
 * - Frees memory previously allocated by @ref pva_kmd_zalloc()
 * - Uses platform-appropriate memory deallocation mechanisms
 * - Safely handles NULL pointer input without side effects
 * - Ensures proper cleanup of memory resources
 *
 * This function must be used to free memory allocated by
 * @ref pva_kmd_zalloc() to prevent memory leaks. It is safe to
 * pass NULL as the pointer argument.
 *
 * @param[in] ptr  Pointer to memory to free, or NULL
 *                 Valid value: pointer returned by @ref pva_kmd_zalloc()
 *                 or NULL
 */
void pva_kmd_free(void *ptr);

/**
 * @brief Log an error message string.
 *
 * Logs an error message string. This function is always enabled, including in safety environments.
 *
 * @details This function performs the following operations:
 * - Outputs the specified string to the system log or console
 * - Provides error logging capability in safety-critical environments
 * - Uses platform-appropriate output mechanisms
 * - Ensures output is visible in production and safety builds
 * - Handles null-terminated string input safely
 *
 * This function is designed for critical error logging and is enabled
 * even in safety environments where debug output may be disabled.
 * For general debug output, use @ref pva_dbg_printf() instead.
 *
 * @param[in] str  Null-terminated string to print
 *                 Valid value: non-null, null-terminated string
 */
void pva_kmd_log_err(const char *str);

/**
 * @brief Log an error message string with a 64-bit unsigned integer.
 *
 * Logs an error message string followed by a 64-bit unsigned integer value. This function is always enabled,
 * including in safety environments.
 *
 * @details This function performs the following operations:
 * - Outputs the specified string followed by the numeric value
 * - Formats the number in decimal representation
 * - Provides error logging with numeric context
 * - Uses platform-appropriate output mechanisms
 * - Ensures output is visible in production and safety builds
 *
 * This function is designed for critical error logging with numeric
 * context and is enabled even in safety environments. For general
 * debug output, use @ref pva_dbg_printf() instead.
 *
 * @param[in] str  Null-terminated string to print
 *                 Valid value: non-null, null-terminated string
 * @param[in] n    64-bit unsigned number to print after the string
 *                 Valid range: [0 .. UINT64_MAX]
 */
void pva_kmd_log_err_u64(const char *str, uint64_t n);

/**
 * @brief Log an error message string with a 32-bit unsigned integer in hexadecimal format.
 *
 * Logs an error message string followed by a 32-bit unsigned integer value in hexadecimal format.
 * This function is always enabled, including in safety environments.
 *
 * @details This function performs the following operations:
 * - Outputs the specified string followed by the numeric value
 * - Formats the number in hexadecimal representation
 * - Provides error logging with hex numeric context
 * - Uses platform-appropriate output mechanisms
 * - Ensures output is visible in production and safety builds
 *
 * This function is designed for critical error logging with hexadecimal
 * numeric context, useful for displaying addresses, register values,
 * and other hex-formatted data. It is enabled even in safety environments.
 *
 * @param[in] str  Null-terminated string to print
 *                 Valid value: non-null, null-terminated string
 * @param[in] n    32-bit unsigned number to print in hex format
 *                 Valid range: [0 .. UINT32_MAX]
 */
void pva_kmd_log_err_hex32(const char *str, uint32_t n);

/**
 * @brief Log an info message string.
 *
 * Logs an info message string. This function is always enabled, including in safety environments.
 *
 * @param[in] str Null-terminated string to log as an info message.
 */
void pva_kmd_log_info(const char *str);

/**
 * @brief Log an info message string with a 64-bit unsigned integer.
 *
 * Logs an info message string followed by a 64-bit unsigned integer value. This function is always enabled,
 * including in safety environments.
 *
 * @param[in] str Null-terminated string to log as an info message.
 * @param[in] n   64-bit unsigned integer value to log after the string.
 */
void pva_kmd_log_info_u64(const char *str, uint64_t n);

/**
 * @brief Print a string message.
 *
 * Prints a string message. Used for tracepoints and debugging output.
 *
 * @param[in] str Null-terminated string to print.
 */
void pva_kmd_print_str(const char *str);

/**
 * @brief Fault KMD.
 *
 * @details This function performs the following operations:
 * - Immediately terminates KMD execution due to critical error
 * - Provides a mechanism for handling unrecoverable error conditions
 * - Ensures system safety by halting operation when corruption is detected
 * - Logs critical error information before termination
 * - Uses platform-appropriate termination mechanisms
 *
 * This function is used when the KMD encounters a critical, unrecoverable
 * error that requires immediate termination to prevent system corruption
 * or unsafe operation. It should only be used for the most severe error
 * conditions where continuing execution would be unsafe.
 *
 * This function never returns and will terminate the calling process
 * or kernel module immediately.
 */
void pva_kmd_fault(void) __attribute__((noreturn));

/**
 * @brief Sleep for some microseconds.
 *
 * @details This function performs the following operations:
 * - Suspends execution of the calling thread for the specified duration
 * - Uses platform-appropriate sleep mechanisms
 * - Provides microsecond-precision delay capabilities
 * - Allows other threads to execute during the sleep period
 * - Handles sleep interruption appropriately for the platform
 *
 * This function enables precise timing delays for hardware sequencing,
 * polling operations, and other timing-sensitive operations in the KMD.
 * The actual sleep duration may be slightly longer than requested due
 * to system scheduling and timer resolution.
 *
 * @param[in] us  Number of microseconds to sleep
 *                Valid range: [0 .. UINT64_MAX]
 */
void pva_kmd_sleep_us(uint64_t us);

#if defined(__KERNEL__)
#include <linux/nospec.h>
#else
/**
 * @brief Bounds check array index to prevent speculative execution attacks.
 *
 * @details This function performs the following operations:
 * - Validates that the index is within the specified array bounds
 * - Returns the index if it is valid (less than size)
 * - Returns 0 if the index is out of bounds
 * - Provides protection against speculative execution side-channel attacks
 * - Implements bounds checking for user space environments
 *
 * This function is used to safely validate array indices before accessing
 * array elements, preventing potential security vulnerabilities from
 * speculative execution. It provides a user space implementation when
 * the kernel's @ref array_index_nospec() is not available.
 *
 * @param[in] index  Array index to validate
 *                   Valid range: [0 .. UINT32_MAX]
 * @param[in] size   Size of the array being accessed
 *                   Valid range: [1 .. UINT32_MAX]
 *
 * @retval index  If index is less than size (valid index)
 * @retval 0      If index is greater than or equal to size (invalid index)
 */
static inline uint32_t array_index_nospec(uint32_t index, uint32_t size)
{
	return index < size ? index : 0U;
}
#endif

/**
 * @brief Get current time using Time Stamp Counter.
 *
 * @details This function performs the following operations:
 * - Reads the current value of the platform's Time Stamp Counter (TSC)
 * - Provides high-resolution timing information for performance measurement
 * - Uses platform-appropriate timing mechanisms
 * - Returns a monotonic timestamp value
 * - Enables precise timing measurements and profiling
 *
 * The TSC provides a high-resolution, low-overhead method for obtaining
 * timing information. The returned value is platform-specific and should
 * be used for relative timing measurements rather than absolute time.
 *
 * @retval timestamp  Current TSC value representing elapsed time
 */
uint64_t pva_kmd_get_time_tsc(void);

/* Shim function with platform-specific implementations (QNX, Linux, Native) */
bool pva_kmd_is_ops_allowed(struct pva_kmd_context *ctx, uint32_t opcode);

#endif // PVA_KMD_SHIM_UTILS_H
