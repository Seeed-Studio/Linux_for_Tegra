/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_PLAT_FAULTS_H
#define PVA_PLAT_FAULTS_H

#include "pva_kmd_shim_utils.h"

/**
 * @brief Assert macro for runtime condition checking with automatic location reporting
 *
 * @details This macro performs the following operations:
 * - Evaluates the provided boolean expression at runtime
 * - If the expression is false, triggers assertion failure handling
 * - Automatically captures the source file name and line number
 * - Logs assertion failure with location information using @ref pva_kmd_print_str_u64()
 * - Calls @ref pva_kmd_fault() to initiate platform-specific fault handling
 * - Provides debugging information for identifying the exact assertion location
 *
 * The macro is used throughout the PVA KMD code to validate assumptions
 * and catch programming errors during development and testing. When an
 * assertion fails, the system will log the failure and potentially halt
 * execution depending on platform-specific fault handling behavior.
 *
 * @param x Boolean expression to evaluate
 *          Valid values: Any expression that evaluates to true or false
 */
#define ASSERT(x)                                                              \
	if (!(x)) {                                                            \
		pva_kmd_log_err_u64("PVA KMD ASSERT at " __FILE__, __LINE__);  \
		pva_kmd_fault();                                               \
	}

/**
 * @brief Fault macro for triggering critical error handling with custom message
 *
 * @details This macro performs the following operations:
 * - Immediately triggers fault handling without condition checking
 * - Automatically captures the source file name and line number
 * - Logs fault occurrence with location using @ref pva_kmd_print_str_u64()
 * - Prints the custom error message using @ref pva_kmd_print_str()
 * - Calls @ref pva_kmd_fault() to initiate platform-specific fault handling
 * - Provides both location and context information for debugging
 *
 * This macro is used to handle critical error conditions that require
 * immediate system attention and potential shutdown. The custom message
 * should describe the specific error condition that triggered the fault.
 * The while(0) construct ensures the macro can be used safely in all
 * syntactic contexts.
 *
 * @param msg Pointer to null-terminated string describing the fault condition
 *            Valid value: non-null string literal or variable
 */
#define FAULT(msg)                                                             \
	do {                                                                   \
		pva_kmd_log_err_u64("PVA KMD FAULT at " __FILE__, __LINE__);   \
		pva_kmd_log_err(msg);                                          \
		pva_kmd_fault();                                               \
	} while (false)

/**
 * @brief Assert macro with explicit location information for remote error reporting
 *
 * @details This macro performs the following operations:
 * - Evaluates the provided boolean expression at runtime
 * - If the expression is false, triggers assertion failure with custom location
 * - Uses the provided file name and line number instead of current location
 * - Logs the custom error location using @ref pva_kmd_print_str_u64()
 * - Prints the provided file name using @ref pva_kmd_print_str()
 * - Calls @ref pva_kmd_fault() to initiate platform-specific fault handling
 * - Enables assertion reporting from different source locations
 *
 * This macro is useful for scenarios where the assertion failure should
 * be reported as originating from a different source location than where
 * the macro is called. This can be helpful for generic error handling
 * functions that want to report the original error location.
 *
 * @param x        Boolean expression to evaluate
 *                 Valid values: Any expression that evaluates to true or false
 * @param err_file Pointer to null-terminated string containing source file name
 *                 Valid value: non-null string
 * @param err_line Line number where the error occurred
 *                 Valid range: [1 .. UINT64_MAX]
 */
#define ASSERT_WITH_LOC(x, err_file, err_line)                                 \
	if (!(x)) {                                                            \
		pva_kmd_log_err_u64("Error at line", err_line);                \
		pva_kmd_log_err(err_file);                                     \
		pva_kmd_log_err("PVA KMD ASSERT");                             \
		pva_kmd_fault();                                               \
	}

#endif