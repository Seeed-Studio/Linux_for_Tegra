/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, NVIDIA Corporation.  All Rights Reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation.  Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */
#include <linux/debugfs.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/nvhost.h>

#include <uapi/linux/tegra-soc-hwpm-uapi.h>
#include "pva_kmd_linux.h"
#include "pva_kmd_linux_device.h"
#include "pva_kmd_debugfs.h"

static int pva_handle_fops(struct seq_file *s, void *data)
{
	return 0;
}

static int debugfs_node_open(struct inode *inode, struct file *file)
{
	int retval;
	struct pva_kmd_file_ops *fops = file_inode(file)->i_private;
	retval = single_open(file, pva_handle_fops, inode->i_private);
	if (retval != 0) {
		pva_kmd_log_err("debugfs_node_open single_open failed");
		goto out;
	}

	if (fops->open != NULL) {
		retval = fops->open(fops->pdev);
	}

out:
	return retval;
}

static int debugfs_node_release(struct inode *inode, struct file *file)
{
	int retval;
	struct pva_kmd_file_ops *fops = file_inode(file)->i_private;

	if (fops->release != NULL) {
		retval = fops->release(fops->pdev);
		if (retval != 0) {
			pva_kmd_log_err("debugfs_node_release release failed");
			goto out;
		}
	}

	retval = single_release(inode, file);

out:
	return retval;
}

static long int debugfs_node_read(struct file *file, char *data,
				  long unsigned int size, long long int *offset)
{
	int64_t retval;
	struct pva_kmd_file_ops *fops = file_inode(file)->i_private;
	retval = fops->read(fops->pdev, fops->file_data, data, *offset, size);

	return retval;
}

static long int debugfs_node_write(struct file *file, const char *data,
				   long unsigned int size,
				   long long int *offset)
{
	long int retval;
	struct pva_kmd_file_ops *fops = file_inode(file)->i_private;
	retval = fops->write(fops->pdev, fops->file_data, data, *offset, size);

	return retval;
}

static const struct file_operations pva_linux_debugfs_fops = {
	.open = debugfs_node_open,
	.read = debugfs_node_read,
	.write = debugfs_node_write,
	.release = debugfs_node_release,
};

void pva_kmd_debugfs_create_bool(struct pva_kmd_device *pva, const char *name,
				 bool *pdata)
{
	struct pva_kmd_linux_device_data *device_data =
		pva_kmd_linux_device_get_data(pva);
	struct nvhost_device_data *props = device_data->pva_device_properties;
	struct dentry *de = props->debugfs;

	debugfs_create_bool(name, 0644, de, pdata);
}
void pva_kmd_debugfs_create_u32(struct pva_kmd_device *pva, const char *name,
				uint32_t *pdata)
{
	struct pva_kmd_linux_device_data *device_data =
		pva_kmd_linux_device_get_data(pva);
	struct nvhost_device_data *props = device_data->pva_device_properties;
	struct dentry *de = props->debugfs;

	debugfs_create_u32(name, 0644, de, pdata);
}

void pva_kmd_debugfs_create_file(struct pva_kmd_device *pva, const char *name,
				 struct pva_kmd_file_ops *pvafops)
{
	struct pva_kmd_linux_device_data *device_data =
		pva_kmd_linux_device_get_data(pva);
	struct nvhost_device_data *props = device_data->pva_device_properties;
	struct dentry *de = props->debugfs;
	struct file_operations *fops =
		(struct file_operations *)&pva_linux_debugfs_fops;
	struct dentry *file;

	file = debugfs_create_file(name, 0644, de, pvafops, fops);
	ASSERT(file != NULL);
}

void pva_kmd_debugfs_remove_nodes(struct pva_kmd_device *pva)
{
	struct pva_kmd_linux_device_data *device_data =
		pva_kmd_linux_device_get_data(pva);
	struct nvhost_device_data *props = device_data->pva_device_properties;
	struct dentry *de = props->debugfs;

	debugfs_lookup_and_remove("stats_enable", de);
	debugfs_lookup_and_remove("vpu_debug", de);
	debugfs_lookup_and_remove("profile_level", de);
	debugfs_lookup_and_remove("vpu_stats", de);
}
