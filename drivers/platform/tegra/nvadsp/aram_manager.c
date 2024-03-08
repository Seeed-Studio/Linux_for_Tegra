// SPDX-License-Identifier: GPL-2.0-only
/**
 * Copyright (c) 2014-2023, NVIDIA CORPORATION. All rights reserved.
 */

#define pr_fmt(fmt) "%s : %d, " fmt, __func__, __LINE__

#include <linux/debugfs.h>
#include <linux/tegra_nvadsp.h>

#include "aram_manager.h"

void nvadsp_aram_print(void *aram_handle)
{
	mem_print(aram_handle);
}

void *nvadsp_aram_request(void *aram_handle, const char *name, size_t size)
{
	return mem_request(aram_handle, name, size);
}

bool nvadsp_aram_release(void *aram_handle, void *handle)
{
	return mem_release(aram_handle, handle);
}

unsigned long nvadsp_aram_get_address(void *handle)
{
	return mem_get_address(handle);
}

static int nvadsp_aram_dump(struct seq_file *s, void *data)
{
	struct nvadsp_drv_data *drv_data = data;

	mem_dump(drv_data->aram_handle, s);
	return 0;
}

static int nvadsp_aram_dump_open(struct inode *inode, struct file *file)
{
	return single_open(file, nvadsp_aram_dump, inode->i_private);
}

static const struct file_operations aram_dump_fops = {
	.open		= nvadsp_aram_dump_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

int nvadsp_aram_init(struct nvadsp_drv_data *drv_data,
		unsigned long addr, unsigned long size)
{
	drv_data->aram_handle = create_mem_manager("ARAM", addr, size);
	if (IS_ERR(drv_data->aram_handle)) {
		pr_err("ERROR: failed to create aram memory_manager");
		return PTR_ERR(drv_data->aram_handle);
	}

	if (debugfs_initialized()) {
		drv_data->aram_dump_debugfs_file = debugfs_create_file("aram_dump",
			S_IRUSR, drv_data->adsp_debugfs_root,
			drv_data, &aram_dump_fops);
		if (!drv_data->aram_dump_debugfs_file) {
			pr_err("ERROR: failed to create aram_dump debugfs");
			destroy_mem_manager(drv_data->aram_handle);
			return -ENOMEM;
		}
	}
	return 0;
}

void nvadsp_aram_exit(struct nvadsp_drv_data *drv_data)
{
	debugfs_remove(drv_data->aram_dump_debugfs_file);
	destroy_mem_manager(drv_data->aram_handle);
}

