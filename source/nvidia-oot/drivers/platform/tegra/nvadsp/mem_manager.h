/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (c) 2014-2023, NVIDIA CORPORATION. All rights reserved.
 */

#ifndef __TEGRA_NVADSP_MEM_MANAGER_H
#define __TEGRA_NVADSP_MEM_MANAGER_H

#include <linux/sizes.h>

#define NAME_SIZE SZ_16

struct mem_chunk {
	struct list_head node;
	char name[NAME_SIZE];
	unsigned long address;
	unsigned long size;
};

struct mem_manager_info {
	struct list_head *alloc_list;
	struct list_head *free_list;
	char name[NAME_SIZE];
	unsigned long start_address;
	unsigned long size;
	spinlock_t lock;
};

void *create_mem_manager(const char *name, unsigned long start_address,
	unsigned long size);
void destroy_mem_manager(void *mem_handle);

void *mem_request(void *mem_handle, const char *name, size_t size);
bool mem_release(void *mem_handle, void *handle);

unsigned long mem_get_address(void *handle);

void mem_print(void *mem_handle);
void mem_dump(void *mem_handle, struct seq_file *s);

#endif /* __TEGRA_NVADSP_MEM_MANAGER_H */
