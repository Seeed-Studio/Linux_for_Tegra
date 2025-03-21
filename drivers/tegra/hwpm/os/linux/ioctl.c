// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/of_address.h>
#include <linux/dma-buf.h>
#include <uapi/linux/tegra-soc-hwpm-uapi.h>

#include <tegra_hwpm.h>
#include <tegra_hwpm_io.h>
#include <tegra_hwpm_ip.h>
#include <tegra_hwpm_log.h>
#include <tegra_hwpm_soc.h>
#include <tegra_hwpm_kmem.h>
#include <tegra_hwpm_common.h>
#include <tegra_hwpm_clk_rst.h>
#include <tegra_hwpm_mem_mgmt.h>

#include <os/linux/driver.h>
#include <os/linux/regops_utils.h>

static int tegra_hwpm_get_device_info_ioctl(struct tegra_soc_hwpm *hwpm,
	struct tegra_soc_hwpm_device_info *device_info)
{
	struct tegra_hwpm_os_linux *hwpm_linux = NULL;

	tegra_hwpm_fn(hwpm, " ");

	hwpm_linux = tegra_hwpm_os_linux_from_hwpm(hwpm);
	if (!hwpm_linux) {
		tegra_hwpm_err(NULL, "Invalid hwpm_linux struct");
		return -ENODEV;
	}

	device_info->chip = hwpm_linux->device_info.chip;
	device_info->chip_revision = hwpm_linux->device_info.chip_revision;
	device_info->revision = hwpm_linux->device_info.revision;
	device_info->platform = hwpm_linux->device_info.platform;

	tegra_hwpm_dbg(hwpm, hwpm_info | hwpm_dbg_device_info,
		"chip id 0x%x", device_info->chip);
	tegra_hwpm_dbg(hwpm, hwpm_info | hwpm_dbg_device_info,
		"chip_revision 0x%x", device_info->chip_revision);
	tegra_hwpm_dbg(hwpm, hwpm_info | hwpm_dbg_device_info,
		"revision 0x%x", device_info->revision);
	tegra_hwpm_dbg(hwpm, hwpm_info | hwpm_dbg_device_info,
		"platform 0x%x", device_info->platform);

	return 0;
}

static int tegra_hwpm_get_floorsweep_info_ioctl(struct tegra_soc_hwpm *hwpm,
	struct tegra_soc_hwpm_ip_floorsweep_info *fs_info)
{
	tegra_hwpm_fn(hwpm, " ");

	if (fs_info->num_queries > TEGRA_SOC_HWPM_IP_QUERIES_MAX) {
		tegra_hwpm_err(hwpm, "Number of queries exceed max limit of %u",
			TEGRA_SOC_HWPM_IP_QUERIES_MAX);
		return -EINVAL;
	}

	return tegra_hwpm_obtain_floorsweep_info(hwpm, fs_info);
}

static int tegra_hwpm_get_resource_info_ioctl(struct tegra_soc_hwpm *hwpm,
	struct tegra_soc_hwpm_resource_info *rsrc_info)
{
	tegra_hwpm_fn(hwpm, " ");

	if (rsrc_info->num_queries > TEGRA_SOC_HWPM_RESOURCE_QUERIES_MAX) {
		tegra_hwpm_err(hwpm, "Number of queries exceed max limit of %u",
			TEGRA_SOC_HWPM_RESOURCE_QUERIES_MAX);
		return -EINVAL;
	}

	return tegra_hwpm_obtain_resource_info(hwpm, rsrc_info);
}

static int tegra_hwpm_reserve_resource_ioctl(struct tegra_soc_hwpm *hwpm,
	struct tegra_soc_hwpm_reserve_resource *reserve_resource)
{
	u32 resource = reserve_resource->resource;
	int ret = 0;

	tegra_hwpm_fn(hwpm, " ");

	if (hwpm->bind_completed) {
		tegra_hwpm_err(hwpm, "The RESERVE_RESOURCE IOCTL can only be"
				" called before the BIND IOCTL.");
		return -EPERM;
	}

	if (resource >= TERGA_SOC_HWPM_NUM_RESOURCES) {
		tegra_hwpm_err(hwpm, "Requested resource %d is out of bounds.",
			resource);
		return -EINVAL;
	}

	ret = tegra_hwpm_reserve_resource(hwpm,
		tegra_hwpm_translate_soc_hwpm_resource(hwpm, resource));
	if (ret < 0) {
		tegra_hwpm_err(hwpm, "Failed to reserve resource %d", resource);
	}

	return ret;
}

static int tegra_hwpm_alloc_pma_stream_ioctl(struct tegra_soc_hwpm *hwpm,
	struct tegra_soc_hwpm_alloc_pma_stream *alloc_pma_stream)
{
	int ret = 0;

	tegra_hwpm_fn(hwpm, " ");

	if (hwpm->bind_completed) {
		tegra_hwpm_err(hwpm, "The ALLOC_PMA_STREAM IOCTL can only be"
				" called before the BIND IOCTL.");
		return -EPERM;
	}

	if (alloc_pma_stream->stream_buf_size == 0) {
		tegra_hwpm_err(hwpm, "stream_buf_size is 0");
		return -EINVAL;
	}
	if (alloc_pma_stream->stream_buf_fd == 0) {
		tegra_hwpm_err(hwpm, "Invalid stream_buf_fd");
		return -EINVAL;
	}
	if (alloc_pma_stream->mem_bytes_buf_fd == 0) {
		tegra_hwpm_err(hwpm, "Invalid mem_bytes_buf_fd");
		return -EINVAL;
	}

	ret = tegra_hwpm_map_stream_buffer(hwpm, alloc_pma_stream);
	if (ret < 0) {
		tegra_hwpm_err(hwpm, "Failed to map stream buffer");
	}

	return ret;
}

static int tegra_hwpm_bind_ioctl(struct tegra_soc_hwpm *hwpm)
{
	int ret = 0;

	tegra_hwpm_fn(hwpm, " ");

	ret = tegra_hwpm_bind_resources(hwpm);
	if (ret != 0) {
		tegra_hwpm_err(hwpm, "Failed to bind resources");
	} else {
		hwpm->bind_completed = true;
	}

	return ret;
}

static int tegra_hwpm_query_allowlist_ioctl(struct tegra_soc_hwpm *hwpm,
	struct tegra_soc_hwpm_query_allowlist *query_allowlist)
{
	int ret = 0;

	tegra_hwpm_fn(hwpm, " ");

	if (!hwpm->bind_completed) {
		tegra_hwpm_err(hwpm,
			"The QUERY_ALLOWLIST IOCTL can only be called"
			" after the BIND IOCTL.");
		return -EPERM;
	}

	if (hwpm->alist_map == NULL) {
		ret = tegra_hwpm_alloc_alist_map(hwpm);
		if (ret != 0) {
			tegra_hwpm_err(hwpm,
				"Couldn't allocate allowlist map structure");
			return ret;
		}
	}

	if (query_allowlist->allowlist == NULL) {
		/* Userspace is querying allowlist size only */
		ret = tegra_hwpm_get_allowlist_size(hwpm);
		if (ret != 0) {
			tegra_hwpm_err(hwpm, "failed to get alist_size");
			return ret;
		}
		query_allowlist->allowlist_size =
			hwpm->alist_map->full_alist_size;
	} else {
		/* Concatenate allowlists and return */
		ret = tegra_hwpm_map_update_allowlist(hwpm, query_allowlist);
		if (ret != 0) {
			tegra_hwpm_err(hwpm, "Failed to update full alist");
			return ret;
		}
	}
	return 0;
}

static int tegra_hwpm_exec_reg_ops_ioctl(struct tegra_soc_hwpm *hwpm,
	struct tegra_soc_hwpm_exec_reg_ops *exec_reg_ops)
{
	tegra_hwpm_fn(hwpm, " ");

	if (!hwpm->bind_completed) {
		tegra_hwpm_err(hwpm, "The EXEC_REG_OPS IOCTL can only be called"
				   " after the BIND IOCTL.");
		return -EPERM;
	}

	return tegra_hwpm_exec_regops(hwpm, exec_reg_ops);
}

static int tegra_hwpm_update_get_put_ioctl(struct tegra_soc_hwpm *hwpm,
	struct tegra_soc_hwpm_update_get_put *update_get_put)
{
	tegra_hwpm_fn(hwpm, " ");

	if (!hwpm->bind_completed) {
		tegra_hwpm_err(hwpm,
			"The UPDATE_GET_PUT IOCTL can only be called"
			" after the BIND IOCTL.");
		return -EPERM;
	}

	return tegra_hwpm_update_mem_bytes(hwpm, update_get_put);
}

static int tegra_hwpm_credit_program_ioctl(struct tegra_soc_hwpm *hwpm,
	struct tegra_soc_hwpm_exec_credit_program *credit_info)
{
	tegra_hwpm_fn(hwpm, " ");

	if (!hwpm->bind_completed) {
		tegra_hwpm_err(hwpm,
				"The Credit Programming can be called only"
				" after completion of BIND IOCTL");
		return -EPERM;
	}

	return tegra_hwpm_credit_program(hwpm, credit_info);
}

static int tegra_hwpm_setup_trigger_ioctl(struct tegra_soc_hwpm *hwpm,
	struct tegra_soc_hwpm_setup_trigger *setup_trigger)
{
	tegra_hwpm_fn(hwpm, " ");

	if (!hwpm->bind_completed) {
		tegra_hwpm_err(hwpm,
			"Setup trigger programming can be called only"
			" after completion of BIND IOCTL");
		return -EPERM;
	}

	return tegra_hwpm_setup_trigger(hwpm, setup_trigger);
}

static long tegra_hwpm_ioctl(struct file *file,
				 unsigned int cmd,
				 unsigned long arg)
{
	int ret = 0;
	struct tegra_soc_hwpm *hwpm = NULL;
	u8 *buf = NULL;

	if ((_IOC_TYPE(cmd) != TEGRA_SOC_HWPM_IOC_MAGIC) ||
		(_IOC_NR(cmd) >= TERGA_SOC_HWPM_NUM_IOCTLS) ||
		(_IOC_SIZE(cmd) > TEGRA_SOC_HWPM_MAX_ARG_SIZE)) {
		tegra_hwpm_err(hwpm, "Invalid IOCTL call");
		ret = -EINVAL;
		goto fail;
	}

	if (!file) {
		tegra_hwpm_err(hwpm, "Invalid file");
		ret = -ENODEV;
		goto fail;
	}

	hwpm = file->private_data;
	if (!hwpm) {
		tegra_hwpm_err(hwpm, "Invalid hwpm struct");
		ret = -ENODEV;
		goto fail;
	}

	tegra_hwpm_fn(hwpm, " ");

	if (!hwpm->device_opened) {
		tegra_hwpm_err(hwpm, "Device open failed, can't process IOCTL");
		ret = -ENODEV;
		goto fail;
	}

	if (!(_IOC_DIR(cmd) & _IOC_NONE)) {
		buf = tegra_hwpm_kzalloc(hwpm, TEGRA_SOC_HWPM_MAX_ARG_SIZE);
		if (!buf) {
			tegra_hwpm_err(hwpm, "Kernel buf allocation failed");
			ret = -ENOMEM;
			goto fail;
		}
	}

	if (_IOC_DIR(cmd) & _IOC_WRITE) {
		if (copy_from_user(buf, (void __user *)arg, _IOC_SIZE(cmd))) {
			tegra_hwpm_err(hwpm, "Copy data from userspace failed");
			ret = -EFAULT;
			goto fail;
		}
	}

	switch (cmd) {
	case TEGRA_CTRL_CMD_SOC_HWPM_DEVICE_INFO:
		ret = tegra_hwpm_get_device_info_ioctl(hwpm,
			(struct tegra_soc_hwpm_device_info *)buf);
		break;
	case TEGRA_CTRL_CMD_SOC_HWPM_IP_FLOORSWEEP_INFO:
		ret = tegra_hwpm_get_floorsweep_info_ioctl(hwpm,
			(struct tegra_soc_hwpm_ip_floorsweep_info *)buf);
		break;
	case TEGRA_CTRL_CMD_SOC_HWPM_RESOURCE_INFO:
		ret = tegra_hwpm_get_resource_info_ioctl(hwpm,
			(struct tegra_soc_hwpm_resource_info *)buf);
		break;
	case TEGRA_CTRL_CMD_SOC_HWPM_RESERVE_RESOURCE:
		ret = tegra_hwpm_reserve_resource_ioctl(hwpm,
			(struct tegra_soc_hwpm_reserve_resource *)buf);
		break;
	case TEGRA_CTRL_CMD_SOC_HWPM_ALLOC_PMA_STREAM:
		ret = tegra_hwpm_alloc_pma_stream_ioctl(hwpm,
			(struct tegra_soc_hwpm_alloc_pma_stream *)buf);
		break;
	case TEGRA_CTRL_CMD_BIND:
		ret = tegra_hwpm_bind_ioctl(hwpm);
		break;
	case TEGRA_CTRL_CMD_SOC_HWPM_QUERY_ALLOWLIST:
		ret = tegra_hwpm_query_allowlist_ioctl(hwpm,
			(struct tegra_soc_hwpm_query_allowlist *)buf);
		break;
	case TEGRA_CTRL_CMD_SOC_HWPM_EXEC_REG_OPS:
		ret = tegra_hwpm_exec_reg_ops_ioctl(hwpm,
			(struct tegra_soc_hwpm_exec_reg_ops *)buf);
		break;
	case TEGRA_CTRL_CMD_SOC_HWPM_UPDATE_GET_PUT:
		ret = tegra_hwpm_update_get_put_ioctl(hwpm,
			(struct tegra_soc_hwpm_update_get_put *)buf);
		break;
	case TEGRA_CTRL_CMD_SOC_HWPM_CREDIT_PROGRAM:
		ret = tegra_hwpm_credit_program_ioctl(hwpm,
			(struct tegra_soc_hwpm_exec_credit_program *)buf);
		break;
	case TEGRA_CTRL_CMD_SOC_HWPM_SETUP_TRIGGER:
		ret = tegra_hwpm_setup_trigger_ioctl(hwpm,
			(struct tegra_soc_hwpm_setup_trigger *)buf);
		break;
	default:
		tegra_hwpm_err(hwpm, "Unknown IOCTL command");
		ret = -ENOTTY;
		goto fail;
	}

	if ((ret == 0) && (_IOC_DIR(cmd) & _IOC_READ)) {
		if (copy_to_user((void __user *)arg, buf, _IOC_SIZE(cmd))) {
			tegra_hwpm_err(hwpm, "Copy buffer to user failed");
			ret = -EFAULT;
			goto fail;
		}
	}

fail:
	if (buf) {
		tegra_hwpm_kfree(hwpm, buf);
	}

	if (ret < 0) {
		tegra_hwpm_err(hwpm, "IOCTL cmd %d failed(%d)!", cmd, ret);
	} else {
		tegra_hwpm_dbg(hwpm, hwpm_info,
			"IOCTL cmd %d completed successfully!", cmd);
	}
	return ret;
}

static int tegra_hwpm_open(struct inode *inode, struct file *filp)
{
	int ret = 0;
	unsigned int minor;
	struct tegra_soc_hwpm *hwpm = NULL;
	struct tegra_hwpm_os_linux *hwpm_linux = NULL;

	if (!inode) {
		tegra_hwpm_err(NULL, "Invalid inode");
		return -EINVAL;
	}

	if (!filp) {
		tegra_hwpm_err(NULL, "Invalid file");
		return -EINVAL;
	}

	minor = iminor(inode);
	if (minor > 0) {
		tegra_hwpm_err(NULL, "Incorrect minor number");
		return -EBADFD;
	}

	hwpm_linux =
		container_of(inode->i_cdev, struct tegra_hwpm_os_linux, cdev);
	if (!hwpm_linux) {
		tegra_hwpm_err(NULL, "Invalid hwpm_linux struct");
		return -EINVAL;
	}
	hwpm = &hwpm_linux->hwpm;
	filp->private_data = hwpm;

	tegra_hwpm_fn(hwpm, " ");

	if (hwpm->soft_reset_unstable) {
		tegra_hwpm_err(hwpm, "HWPM subsystem is unstable. Please perform system-wide reset.");
		return -ENOTRECOVERABLE;
	}

	/* Initialize driver on first open call only */
	if (!atomic_add_unless(&hwpm_linux->usage_count.var, 1U, 1U)) {
		return -EAGAIN;
	}

	if (hwpm->active_chip->clk_rst_set_rate_enable) {
		ret = hwpm->active_chip->clk_rst_set_rate_enable(hwpm_linux);
		if (ret != 0) {
			goto fail;
		}
	}

	ret = tegra_hwpm_setup_hw(hwpm);
	if (ret < 0) {
		tegra_hwpm_err(hwpm, "Failed to setup hw");
		goto fail;
	}

	ret = tegra_hwpm_check_status(hwpm);
	if (ret < 0) {
		tegra_hwpm_err(hwpm, "HW not ready for profiling session");
		goto fail;
	}

	ret = tegra_hwpm_setup_sw(hwpm);
	if (ret < 0) {
		tegra_hwpm_err(hwpm, "Failed to setup sw");
		goto fail;
	}

	hwpm->device_opened = true;

	return 0;
fail:
	ret = tegra_hwpm_release_hw(hwpm);
	if (ret < 0) {
		tegra_hwpm_err(hwpm, "Failed to release hw");
	}

	tegra_hwpm_err(hwpm, "%s failed", __func__);
	return ret;
}

static ssize_t tegra_hwpm_read(struct file *file,
				   char __user *ubuf,
				   size_t count,
				   loff_t *offp)
{
	return 0;
}

/* FIXME: Fix double release bug */
static int tegra_hwpm_release(struct inode *inode, struct file *filp)
{
	int ret = 0, err = 0;
	struct tegra_hwpm_os_linux *hwpm_linux = NULL;
	struct tegra_soc_hwpm *hwpm = NULL;

	if (!inode) {
		tegra_hwpm_err(NULL, "Invalid inode");
		return -EINVAL;
	}
	if (!filp) {
		tegra_hwpm_err(NULL, "Invalid file");
		return -EINVAL;
	}

	hwpm_linux =
		container_of(inode->i_cdev, struct tegra_hwpm_os_linux, cdev);
	if (!hwpm_linux) {
		tegra_hwpm_err(NULL, "Invalid hwpm_linux struct");
		return -EINVAL;
	}
	hwpm = &hwpm_linux->hwpm;

	tegra_hwpm_fn(hwpm, " ");

	if (hwpm->device_opened == false) {
		/* Device was not opened, do nothing */
		return 0;
	}

	/* Reset the HWPM state via soft reset before releasing resources */
	if (hwpm->bind_completed && hwpm->active_chip->soft_reset) {
		int soft_reset_ret = hwpm->active_chip->soft_reset(hwpm);
		if (soft_reset_ret != 0) {
			tegra_hwpm_err(hwpm,
				       "Failed to perform soft reset. Error: %d",
				       soft_reset_ret);
			if (soft_reset_ret == -ENOTRECOVERABLE) {
				hwpm->soft_reset_unstable = true;
				tegra_hwpm_err(hwpm,
					"HWPM subsystem has become unstable. "
					"Only recovery scheme is to perform System-wide reset.");
			}
		}
	}

	ret = tegra_hwpm_disable_triggers(hwpm);
	if (ret != 0) {
		tegra_hwpm_err(hwpm, "Failed to disable PMA triggers");
		err = ret;
	}

	/* Disable and release reserved IPs */
	ret = tegra_hwpm_release_resources(hwpm);
	if (ret != 0) {
		tegra_hwpm_err(hwpm, "Failed to release IP apertures");
		err = ret;
		goto fail;
	}

	/* Clear MEM_BYTES pipeline */
	ret = tegra_hwpm_clear_mem_pipeline(hwpm);
	if (ret != 0) {
		tegra_hwpm_err(hwpm, "Failed to clear MEM_BYTES pipeline");
		err = ret;
		goto fail;
	}

	tegra_hwpm_release_alist_map(hwpm);
	tegra_hwpm_release_mem_mgmt(hwpm);

	ret = tegra_hwpm_release_hw(hwpm);
	if (ret != 0) {
		tegra_hwpm_err(hwpm, "Failed to release hw");
		err = ret;
		goto fail;
	}

	if (hwpm->active_chip->clk_rst_disable) {
		ret = hwpm->active_chip->clk_rst_disable(hwpm_linux);
		if (ret != 0) {
			tegra_hwpm_err(hwpm, "Failed to release clock");
			err = ret;
			goto fail;
		}
	}

	/* De-init driver on last close call only */
	if (!atomic_dec_and_test(&hwpm_linux->usage_count.var)) {
		return 0;
	}

	hwpm->device_opened = false;
fail:
	return err;
}

/* File ops for device node */
const struct file_operations tegra_hwpm_ops = {
	.owner = THIS_MODULE,
	.open = tegra_hwpm_open,
	.read = tegra_hwpm_read,
	.release = tegra_hwpm_release,
	.unlocked_ioctl = tegra_hwpm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = tegra_hwpm_ioctl,
#endif
};
