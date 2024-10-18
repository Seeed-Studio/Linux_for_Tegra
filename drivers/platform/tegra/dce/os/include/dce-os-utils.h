/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

u8 dce_os_get_phys_stream_id(struct tegra_dce *d);

u8 dce_os_get_dce_stream_id(struct tegra_dce *d);

u8 dce_os_get_fw_vm_index(struct tegra_dce *d);

u8 dce_os_get_fw_carveout_id(struct tegra_dce *d);

bool dce_os_is_physical_id_valid(struct tegra_dce *d);

u32 dce_os_get_fw_dce_addr(struct tegra_dce *d);

u64 dce_os_get_fw_phy_addr(struct tegra_dce *d, struct dce_firmware *fw);

const char *dce_os_get_fw_name(struct tegra_dce *d);

#endif /* DCE_OS_UTILS_H */
