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

/*
 * Linux implementation for logging support.
 */

#ifndef NVDISPLAY_SERVER_OS_DCE_LOG_LINUX_H
#define NVDISPLAY_SERVER_OS_DCE_LOG_LINUX_H

struct tegra_dce;

enum dce_log_type {
    DCE_ERROR,
    DCE_WARNING,
    DCE_INFO,
    DCE_DEBUG,
};

__printf(5, 6)
void dce_log_msg(struct tegra_dce *d, const char *func_name, int line,
            enum dce_log_type type, const char *fmt, ...);

#endif /* NVDISPLAY_SERVER_OS_DCE_LOG_LINUX_H */
