// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2014-2023, NVIDIA CORPORATION.  All rights reserved. */

#include "mods_internal.h"

#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/uaccess.h>

static struct dentry *mods_debugfs_dir;

#if defined(MODS_HAS_TEGRA) && defined(CONFIG_TEGRA_KFUSE)
#include <soc/tegra/kfuse.h>
#endif

#if defined(MODS_HAS_TEGRA) && defined(CONFIG_TEGRA_KFUSE)
static int mods_kfuse_show(struct seq_file *s, void *unused)
{
	unsigned int buf[KFUSE_DATA_SZ / 4];

	/* copy load kfuse into buffer - only needed for early Tegra parts */
	int ret = tegra_kfuse_read(buf, sizeof(buf));
	int i;

	if (ret)
		return ret;

	for (i = 0; i < KFUSE_DATA_SZ / 4; i += 4)
		seq_printf(s, "0x%08x 0x%08x 0x%08x 0x%08x\n",
			buf[i], buf[i+1], buf[i+2], buf[i+3]);

	return 0;
}

static int mods_kfuse_open(struct inode *inode, struct file *file)
{
	return single_open(file, mods_kfuse_show, inode->i_private);
}

static const struct file_operations mods_kfuse_fops = {
	.open		= mods_kfuse_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif /* MODS_HAS_TEGRA */

static int mods_debug_get(void *data, u64 *val)
{
	*val = (u64)(mods_get_debug_level() & DEBUG_ALL);
	return 0;
}
static int mods_debug_set(void *data, u64 val)
{
	mods_set_debug_level((int)val & DEBUG_ALL);
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(mods_debug_fops, mods_debug_get, mods_debug_set,
						"0x%08llx\n");

static int mods_mi_get(void *data, u64 *val)
{
	*val = (u64)mods_get_multi_instance();
	return 0;
}
static int mods_mi_set(void *data, u64 val)
{
	mods_set_multi_instance((int)val);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(mods_mi_fops, mods_mi_get, mods_mi_set, "%llu\n");

static int mods_ffa_get(void *data, u64 *val)
{
#if defined(MODS_HAS_ARM_FFA)
	*val = 1ULL;
#else
	*val = 0ULL;
#endif
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(mods_ffa_fops, mods_ffa_get, NULL, "%llu\n");

void mods_remove_debugfs(void)
{
	debugfs_remove_recursive(mods_debugfs_dir);
	mods_debugfs_dir = NULL;
}

int mods_create_debugfs(struct miscdevice *modsdev)
{
	struct dentry *retval;
	int err = 0;

	mods_debugfs_dir = debugfs_create_dir(dev_name(modsdev->this_device),
		NULL);
	if (IS_ERR(mods_debugfs_dir)) {
		err = -EIO;
		goto remove_out;
	}

	retval = debugfs_create_file("debug", 0644,
		mods_debugfs_dir, NULL, &mods_debug_fops);
	if (IS_ERR(retval)) {
		err = -EIO;
		goto remove_out;
	}

	retval = debugfs_create_file("multi_instance", 0644,
		mods_debugfs_dir, NULL, &mods_mi_fops);
	if (IS_ERR(retval)) {
		err = -EIO;
		goto remove_out;
	}

	retval = debugfs_create_file("ffa", 0444,
		mods_debugfs_dir, NULL, &mods_ffa_fops);
	if (IS_ERR(retval)) {
		err = -EIO;
		goto remove_out;
	}

#if defined(MODS_HAS_TEGRA) && defined(CONFIG_TEGRA_KFUSE)
	retval = debugfs_create_file("kfuse_data", 0444,
		mods_debugfs_dir, NULL, &mods_kfuse_fops);
	if (IS_ERR(retval)) {
		err = -EIO;
		goto remove_out;
	}
#endif

	return 0;
remove_out:
	dev_err(modsdev->this_device, "could not create debugfs\n");
	mods_remove_debugfs();
	return err;
}
