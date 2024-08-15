/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#ifndef NVDISPLAY_SERVER_OS_DCE_LOG_H
#define NVDISPLAY_SERVER_OS_DCE_LOG_H

#ifdef __KERNEL__
#include <linux-kmd/os-dce-log.h>
#elif defined(NVDISPLAY_SERVER_HVRTOS)
#include <hvrtos/os-dce-log.h>
#else
#error "OS Not Supported"
#endif

/**
 * dce_err - Print an error
 *
 * @d        - Pointer to tegra_dce.
 * @fmt      - A format string (printf style).
 * @arg...   - Arguments for the format string.
 *
 * Uncondtionally print an error message.
 */
#define dce_err(d, fmt, arg...)                    \
    dce_log_msg(d, __func__, __LINE__, DCE_ERROR, fmt, ##arg)

/**
 * dce_warn - Print a warning
 *
 * @d        - Pointer to tegra_dce.
 * @fmt      - A format string (printf style).
 * @arg...   - Arguments for the format string.
 *
 * Uncondtionally print a warming message.
 */
#define dce_warn(d, fmt, arg...)                    \
    dce_log_msg(d, __func__, __LINE__, DCE_WARNING, fmt, ##arg)

/**
 * dce_info - Print an info message
 *
 * @d        - Pointer to tegra_dce.
 * @fmt      - A format string (printf style).
 * @arg...   - Arguments for the format string.
 *
 * Unconditionally print an information message.
 */
#define dce_info(d, fmt, arg...)                    \
    dce_log_msg(d, __func__, __LINE__, DCE_INFO, fmt, ##arg)

/**
 * dce_debug - Print a debug message
 *
 * @d        - Pointer to tegra_dce.
 * @fmt      - A format string (printf style).
 * @arg...   - Arguments for the format string.
 *
 * print a debug message.
 */
#define dce_debug(d, fmt, arg...)                    \
    dce_log_msg(d, __func__, __LINE__, DCE_DEBUG, fmt, ##arg)

#endif /* NVDISPLAY_SERVER_OS_DCE_LOG_H */