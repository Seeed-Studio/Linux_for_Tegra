/* SPDX-License-Identifier: MIT */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef DCE_LOGGING_H
#define DCE_LOGGING_H

#include <interface/dce-admin-cmds.h>
#include <dce.h>

typedef uint8_t         dce_log_set_log_flags_t;

#define DCE_LOG_FL_CLEAR_BUFFER     ((dce_log_set_log_flags_t)1U)
#define DCE_LOG_FL_SET_IOVA_ADDR    ((dce_log_set_log_flags_t)2U)
#define DCE_LOG_FL_SET_LOG_LVL      ((dce_log_set_log_flags_t)4U)

typedef uint32_t        dce_log_set_log_lvl_t;

#define DCE_LOG_MAX_LOG_LVL			((dce_log_set_log_lvl_t)7U)
#define DCE_LOG_DEFAULT_LOG_LVL		((dce_log_set_log_lvl_t)0xFFFFFFFFU)

/**
 * @brief Allocate memory for DCE logging buffer
 */
int dce_log_buf_mem_init(struct tegra_dce *d);

/**
 * @brief Initialize set log struct while doing log buffer initialization
 */
int dce_log_init(struct tegra_dce *d, struct dce_ipc_message *msg);

/**
 * @brief Clear circular region of log buffer
 */
int dce_log_clear_buffer(struct tegra_dce *d, struct dce_ipc_message *msg);

/**
 * @brief Set log level for logging to log buffer
 */
int dce_log_set_log_level(struct tegra_dce *d, struct dce_ipc_message *msg,
					u32 log_lvl);

#endif /* DCE_LOGGING_H */
