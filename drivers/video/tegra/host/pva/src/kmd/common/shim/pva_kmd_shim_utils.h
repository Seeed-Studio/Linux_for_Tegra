/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_SHIM_UTILS_H
#define PVA_KMD_SHIM_UTILS_H
#include "pva_api.h"

/**
 * @brief Allocate memory for KMD's private use.
 *
 *  Memory will be zero initialized.
 */
void *pva_kmd_zalloc(uint64_t size);

/**
 * @brief Free memory allocated by pva_kmd_zalloc.
 */
void pva_kmd_free(void *ptr);

/**
 * @brief Print a string.
 *
 * This function is used for logging errors, enabled even in safety environment.
 * For debug print, use pva_dbg_printf.
 *
 * @param str The string to print.
 */
void pva_kmd_print_str(const char *str);

/**
 * @brief Print a string followed by a 64-bit unsigned number.
 *
 * This function is used for logging errors, enabled even in safety environment.
 * For debug print, use pva_dbg_printf.
 *
 * @param str The string to print.
 * @param n The number to print.
 */
void pva_kmd_print_str_u64(const char *str, uint64_t n);

/**
 * @brief Print a string followed by a 32-bit unsigned number in hex format.
 *
 * This function is used for logging errors, enabled even in safety environment.
 * For debug print, use pva_dbg_printf.
 *
 * @param str The string to print.
 * @param n The number to print.
 */
void pva_kmd_print_str_hex32(const char *str, uint32_t n);

/**
 * @brief Fault KMD.
 *
 * Abort KMD due to critical unrecoverable error.
 */
void pva_kmd_fault(void) __attribute__((noreturn));

/**
 * @brief Sleep for some microseconds.
 *
 * @param us The number of microseconds to sleep.
 */
void pva_kmd_sleep_us(uint64_t us);

#if defined(__KERNEL__)
#include <linux/nospec.h>
#else
static inline uint32_t array_index_nospec(uint32_t index, uint32_t size)
{
	return index < size ? index : 0;
}
#endif

uint64_t pva_kmd_get_time_tsc(void);

#endif // PVA_KMD_SHIM_UTILS_H
