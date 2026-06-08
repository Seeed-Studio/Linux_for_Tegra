/* SPDX-License-Identifier: MIT */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#ifndef DCE_OS_UTILS_H
#define DCE_OS_UTILS_H

#include <dce-os-types.h>

/**
 * Do not add any more util functions to this file.
 * We should add OS util functions to respective OS module files.
 */

struct tegra_dce;

void dce_os_writel(struct tegra_dce *d, u32 r, u32 v);

u32 dce_os_readl(struct tegra_dce *d, u32 r);

void dce_os_writel_check(struct tegra_dce *d, u32 r, u32 v);

bool dce_os_io_exists(struct tegra_dce *d);

bool dce_os_io_valid_reg(struct tegra_dce *d, u32 r);

struct dce_firmware *dce_os_request_firmware(struct tegra_dce *d,
					  const char *fw_name);

void dce_os_release_fw(struct tegra_dce *d, struct dce_firmware *fw);

void *dce_os_kzalloc(struct tegra_dce *d, size_t size,  bool dma_flag);

void dce_os_kfree(struct tegra_dce *d, void *addr);

unsigned long dce_os_get_nxt_pow_of_2(unsigned long *addr, u8 nbits);

void dce_os_usleep_range(unsigned long min, unsigned long max);

void dce_os_bitmap_set(unsigned long *map,
				  unsigned int start, unsigned int len);

void dce_os_bitmap_clear(unsigned long *map,
				    unsigned int start, unsigned int len);

u8 dce_os_get_dce_stream_id(struct tegra_dce *d);

int dce_os_init_log_buffer(struct tegra_dce *d);

void dce_os_deinit_log_buffer(struct tegra_dce *d);

#endif /* DCE_OS_UTILS_H */
