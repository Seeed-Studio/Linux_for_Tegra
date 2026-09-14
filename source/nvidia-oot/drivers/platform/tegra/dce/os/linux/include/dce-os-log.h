/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef DCE_OS_LOG_H
#define DCE_OS_LOG_H

struct tegra_dce;

/**
 * TODOs:
 * 1) Change below dce_os_*() macros to functions so that we can
 *	  have a common header file for all OSs.
 * 2) We also need to abstract below __printf() macro to make
 *    this a common header.
 */
enum dce_os_log_type {
	DCE_OS_ERROR,
	DCE_OS_WARNING,
	DCE_OS_INFO,
	DCE_OS_DEBUG,
};

/*
 * Each OS must implement these functions. They handle the OS specific nuances
 * of printing data to a UART, log, whatever.
 */
__printf(5, 6)
void dce_os_log_msg(struct tegra_dce *d, const char *func_name, int line,
			enum dce_os_log_type type, const char *fmt, ...);

/**
 * dce_os_err - Print an error
 *
 * @d        - Pointer to tegra_dce.
 * @fmt      - A format string (printf style).
 * @arg...   - Arguments for the format string.
 *
 * Uncondtionally print an error message.
 */
#define dce_os_err(d, fmt, arg...)					\
	dce_os_log_msg(d, __func__, __LINE__, DCE_OS_ERROR, fmt, ##arg)

/**
 * dce_os_warn - Print a warning
 *
 * @d        - Pointer to tegra_dce.
 * @fmt      - A format string (printf style).
 * @arg...   - Arguments for the format string.
 *
 * Uncondtionally print a warming message.
 */
#define dce_os_warn(d, fmt, arg...)					\
	dce_os_log_msg(d, __func__, __LINE__, DCE_OS_WARNING, fmt, ##arg)

/**
 * dce_os_info - Print an info message
 *
 * @d        - Pointer to tegra_dce.
 * @fmt      - A format string (printf style).
 * @arg...   - Arguments for the format string.
 *
 * Unconditionally print an information message.
 */
#define dce_os_info(d, fmt, arg...)					\
	dce_os_log_msg(d, __func__, __LINE__, DCE_OS_INFO, fmt, ##arg)

/**
 * dce_os_debug - Print a debug message
 *
 * @d        - Pointer to tegra_dce.
 * @fmt      - A format string (printf style).
 * @arg...   - Arguments for the format string.
 *
 * print a debug message.
 */
#define dce_os_debug(d, fmt, arg...)					\
	dce_os_log_msg(d, __func__, __LINE__, DCE_OS_DEBUG, fmt, ##arg)

#endif /* DCE_OS_LOG_H */
