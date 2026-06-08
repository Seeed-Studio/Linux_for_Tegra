// SPDX-License-Identifier: GPL-2.0-only
/**
 * Copyright (c) 2014-2024, NVIDIA CORPORATION. All rights reserved.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/tegra_nvadsp.h>
#include <linux/platform_device.h>
#include <uapi/misc/adsp_console_ioctl.h>

#include <linux/uaccess.h>

#include "dev.h"
#include "adsp_console_dbfs.h"

#define USE_RUN_APP_API

#define ACCESS_OK(addr, size) access_ok(addr, size)

static int adsp_app_ctx_add(struct nvadsp_cnsl *console, uint64_t ctx)
{
	int i;

	if (ctx == 0)
		return -EINVAL;

	for (i = 0; i < ADSP_APP_CTX_MAX; i++) {
		if (console->adsp_app_ctx_vals[i] == 0) {
			console->adsp_app_ctx_vals[i] = ctx;
			return 0;
		}
	}
	return -EINVAL;
}

static int adsp_app_ctx_check(struct nvadsp_cnsl *console, uint64_t ctx)
{
	int i;

	if (ctx == 0)
		return -EINVAL;

	for (i = 0; i < ADSP_APP_CTX_MAX; i++) {
		if (console->adsp_app_ctx_vals[i] == ctx)
			return 0;
	}
	return -EINVAL;
}


static void adsp_app_ctx_remove(struct nvadsp_cnsl *console, uint64_t ctx)
{
	int i;

	for (i = 0; i < ADSP_APP_CTX_MAX; i++) {
		if (console->adsp_app_ctx_vals[i] == ctx) {
			console->adsp_app_ctx_vals[i] = 0;
			return;
		}
	}
}

static int adsp_consol_open(struct inode *i, struct file *f)
{

	int ret;
	uint16_t snd_mbox_id = 30;
	struct nvadsp_cnsl *console = i->i_private;
	struct device *dev = console->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(pdev);
	struct nvadsp_handle *nvadsp_handle = &drv_data->nvadsp_handle;

	if (console->open_cnt)
		return -EBUSY;
	console->open_cnt++;
	ret = 0;
	f->private_data = console;
	if (!drv_data->adsp_os_running)
		goto exit_open;
	ret = nvadsp_handle->mbox_open(nvadsp_handle,
		&console->shl_snd_mbox, &snd_mbox_id,
		"adsp_send_cnsl", NULL, NULL);
	if (!ret)
		goto exit_open;
	pr_err("adsp_consol: Failed to init adsp_consol send mailbox");
	memset(&console->shl_snd_mbox, 0, sizeof(struct nvadsp_mbox));
	console->open_cnt--;
exit_open:
	return ret;
}
static int adsp_consol_close(struct inode *i, struct file *f)
{
	int ret = 0;
	struct nvadsp_cnsl *console = i->i_private;
	struct nvadsp_mbox *mbox = &console->shl_snd_mbox;
	struct device *dev = console->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(pdev);
	struct nvadsp_handle *nvadsp_handle = &drv_data->nvadsp_handle;

	console->open_cnt--;
	if (!drv_data->adsp_os_running || (0 == mbox->id))
		goto exit_close;
	ret = nvadsp_handle->mbox_close(nvadsp_handle, mbox);
	if (ret)
		pr_err("adsp_consol: Failed to close adsp_consol send mailbox)");
	memset(mbox, 0, sizeof(struct nvadsp_mbox));
exit_close:
	return ret;
}
static long
adsp_consol_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{

	int ret = 0;
	uint16_t *mid;
	uint16_t  mbxid = 0;
	uint32_t  data;
	uint64_t  ctx2;
	nvadsp_app_info_t *app_info;
	struct adsp_consol_run_app_arg_t app_args;
	struct nvadsp_cnsl *console = f->private_data;
	struct nvadsp_mbox *mbox;
	struct device *dev = console->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(pdev);
	struct nvadsp_handle *nvadsp_handle = &drv_data->nvadsp_handle;
	void __user *uarg = (void __user *)arg;

	if (_IOC_TYPE(cmd) != NV_ADSP_CONSOLE_MAGIC)
		return -EFAULT;

	if ((_IOC_NR(cmd) != _IOC_NR(ADSP_CNSL_LOAD)) &&
		(_IOC_NR(cmd) != _IOC_NR(ADSP_CNSL_RESUME)) &&
		(!drv_data->adsp_os_running)) {
		dev_info(dev, "adsp_consol: os not running.");
		return -EPERM;
	}

	if ((_IOC_NR(cmd) != _IOC_NR(ADSP_CNSL_LOAD)) &&
		(0 == console->shl_snd_mbox.id)) {
		dev_info(dev, "adsp_consol: Mailboxes not open.");
		return -EPERM;
	}

	switch (_IOC_NR(cmd)) {
	case _IOC_NR(ADSP_CNSL_LOAD):
		ret = 0;

		if (drv_data->adsp_os_running)
			break;
		mbxid = 30;
		mbox = &console->shl_snd_mbox;
		ret = nvadsp_handle->os_load(nvadsp_handle);
		if (ret) {
			dev_info(dev, "adsp_consol: Load OS Failed.");
			break;
		}
		ret = nvadsp_handle->os_start(nvadsp_handle);
		if (ret) {
			dev_info(dev, "adsp_consol: Start OS Failed.");
			break;
		}
		ret = nvadsp_handle->mbox_open(nvadsp_handle, mbox, &mbxid,
			"adsp_send_cnsl", NULL, NULL);
		if (!ret)
			break;
		pr_err("adsp_consol: Failed to init adsp_consol send mailbox");
		memset(mbox, 0, sizeof(struct nvadsp_mbox));
		break;
	case _IOC_NR(ADSP_CNSL_SUSPEND):
		ret = nvadsp_handle->os_suspend(nvadsp_handle);
		if (ret)
			dev_info(dev, "adsp_consol: OS Suspend Failed.");
		break;
	case _IOC_NR(ADSP_CNSL_STOP):
		nvadsp_handle->os_stop(nvadsp_handle);
		break;
	case _IOC_NR(ADSP_CNSL_RESUME):
		if (!drv_data->adsp_os_suspended) {
			dev_info(dev, "adsp_consol: OS is not suspended to perform resume.");
			break;
		}
		ret = nvadsp_handle->os_start(nvadsp_handle);
		if (ret)
			dev_info(dev, "adsp_consol: OS Resume Failed.");
		break;
	case _IOC_NR(ADSP_CNSL_RUN_APP):
		if (!ACCESS_OK(uarg, sizeof(struct adsp_consol_run_app_arg_t)))
			return -EACCES;
		ret = copy_from_user(&app_args, uarg,
			sizeof(app_args));
		if (ret) {
			ret = -EACCES;
			break;
		}

		dev_info(dev, "Core ID: %d\n", app_args.core_id);
		app_args.app_name[NVADSP_NAME_SZ_MAX] = '\0';

#ifdef USE_RUN_APP_API
		app_args.ctx2 = (uint64_t)nvadsp_handle->run_app(
			nvadsp_handle, NULL,
			app_args.app_name,
			(nvadsp_app_args_t *)&app_args.args[0],
			NULL, 0, app_args.core_id, true);
		if (!app_args.ctx2) {
			dev_info(dev, "adsp_consol: unable to run %s\n",
				app_args.app_name);
			return -EINVAL;
		}
		if (adsp_app_ctx_add(console, app_args.ctx2)) {
			dev_info(dev, "adsp_consol: unable to add %s ctx\n",
				app_args.app_name);
			return -EINVAL;
		}
#else
		app_args.ctx1 = (uint64_t)nvadsp_handle->app_load(
				nvadsp_handle, app_args.app_path,
				app_args.app_name);
		if (!app_args.ctx1) {
			dev_info(dev,
				"adsp_consol: dynamic app load failed %s\n",
				app_args.app_name);
			return -EINVAL;
		}
		if (adsp_app_ctx_add(console, app_args.ctx1)) {
			dev_info(dev, "adsp_consol: unable to add %s ctx\n",
				app_args.app_name);
			return -EINVAL;
		}

		dev_info(dev, "adsp_consol: calling nvadsp_app_init\n");
		app_args.ctx2 = (uint64_t)nvadsp_handle->app_init(
				nvadsp_handle, (void *)app_args.ctx1, NULL);
		if (!app_args.ctx2) {
			dev_info(dev,
				"adsp_consol: unable to initilize the app\n");
			return -EINVAL;
		}
		if (adsp_app_ctx_add(console, app_args.ctx2)) {
			dev_info(dev, "adsp_consol: unable to add %s ctx\n",
				app_args.app_name);
			return -EINVAL;
		}

		dev_info(dev, "adsp_consol: calling nvadsp_app_start\n");
		ret = nvadsp_handle->app_start(nvadsp_handle, (void *)app_args.ctx2);
		if (ret) {
			dev_info(dev, "adsp_consol: unable to start the app\n");
			break;
		}
#endif
		ret = copy_to_user((void __user *) arg, &app_args,
			sizeof(struct adsp_consol_run_app_arg_t));
		if (ret)
			ret = -EACCES;
		break;
	case _IOC_NR(ADSP_CNSL_STOP_APP):
		if (!ACCESS_OK(uarg, sizeof(struct adsp_consol_run_app_arg_t)))
			return -EACCES;
		ret = copy_from_user(&app_args, uarg,
			sizeof(app_args));
		if (ret) {
			ret = -EACCES;
			break;
		}
#ifdef USE_RUN_APP_API
		if (!app_args.ctx2) {
			ret = -EACCES;
			break;
		}
		if (adsp_app_ctx_check(console, app_args.ctx2)) {
			dev_info(dev, "adsp_consol: unable to check %s ctx\n",
				 app_args.app_name);
			return -EINVAL;
		}

		app_args.ctx1 = (uint64_t)
			((nvadsp_app_info_t *)app_args.ctx2)->handle;

		nvadsp_handle->exit_app(nvadsp_handle,
				(nvadsp_app_info_t *)app_args.ctx2, false);
		nvadsp_handle->app_unload(nvadsp_handle,
					(const void *)app_args.ctx1);

		adsp_app_ctx_remove(console, app_args.ctx2);
#else
		if ((!app_args.ctx2) || (!app_args.ctx1)) {
			ret = -EACCES;
			break;
		}

		if (adsp_app_ctx_check(console, app_args.ctx2) ||
		    adsp_app_ctx_check(console, app_args.ctx1)) {
			dev_info(dev, "adsp_consol: unable to check %s ctx\n",
				 app_args.app_name);
			return -EINVAL;
		}

		nvadsp_handle->app_deinit(nvadsp_handle,
					(void *)app_args.ctx2);
		nvadsp_handle->nvadsp_app_unload(nvadsp_handle,
						(void *)app_args.ctx1);

		adsp_app_ctx_remove(console, app_args.ctx2);
		adsp_app_ctx_remove(console, app_args.ctx1);
#endif

		break;
	case _IOC_NR(ADSP_CNSL_CLR_BUFFER):
		break;
	case _IOC_NR(ADSP_CNSL_OPN_MBX):
		if (!ACCESS_OK(uarg, sizeof(ctx2)))
			return -EACCES;
		ret = copy_from_user(&ctx2, uarg, sizeof(ctx2));
		if (ret) {
			ret = -EACCES;
			break;
		}
		if (adsp_app_ctx_check(console, ctx2)) {
			dev_info(dev, "adsp_consol: unable to check ctx\n");
			return -EINVAL;
		}

		app_info = (nvadsp_app_info_t *)ctx2;

		if (app_info && app_info->mem.shared) {
			mid = (short *)(app_info->mem.shared);
			dev_info(dev, "adsp_consol: open %x\n", *mid);
			mbxid = *mid;
		}

		ret = nvadsp_handle->mbox_open(nvadsp_handle,
			&console->app_mbox, &mbxid,
			"app_mbox", NULL, NULL);
		if (ret) {
			pr_err("adsp_consol: Failed to open app mailbox");
			ret = -EACCES;
		}
		break;
	case _IOC_NR(ADSP_CNSL_CLOSE_MBX):
		mbox = &console->app_mbox;
		while (!nvadsp_handle->mbox_recv(nvadsp_handle,
						mbox, &data, 0, 0))
			;
		ret = nvadsp_handle->mbox_close(nvadsp_handle, mbox);
		if (ret)
			break;
		memset(mbox, 0, sizeof(struct nvadsp_mbox));
		break;
	case _IOC_NR(ADSP_CNSL_PUT_MBX):
		if (!ACCESS_OK(uarg, sizeof(uint32_t)))
			return -EACCES;
		ret = copy_from_user(&data, uarg,
			sizeof(uint32_t));
		if (ret) {
			ret = -EACCES;
			break;
		}
		ret = nvadsp_handle->mbox_send(nvadsp_handle,
			&console->app_mbox, data,
			NVADSP_MBOX_SMSG, 0, 0);
		break;
	case _IOC_NR(ADSP_CNSL_GET_MBX):
		if (!ACCESS_OK(uarg, sizeof(uint32_t)))
			return -EACCES;
		ret = nvadsp_handle->mbox_recv(nvadsp_handle,
			&console->app_mbox, &data, 0, 0);
		if (ret)
			break;
		ret = copy_to_user(uarg, &data,
			sizeof(uint32_t));
		if (ret)
			ret = -EACCES;
		break;
	case _IOC_NR(ADSP_CNSL_PUT_DATA):
		if (!ACCESS_OK(uarg, sizeof(struct adsp_consol_run_app_arg_t)))
			return -EACCES;
		ret = copy_from_user(&data, uarg, sizeof(uint32_t));
		if (ret) {
			ret = -EACCES;
			break;
		}
		return nvadsp_handle->mbox_send(nvadsp_handle,
			&console->shl_snd_mbox, data,
			NVADSP_MBOX_SMSG, 0, 0);
		break;
	default:
		dev_info(dev, "adsp_consol: invalid command\n");
		return -EINVAL;
	}
	return ret;
}

static const struct file_operations adsp_console_operations = {
	.open = adsp_consol_open,
	.release = adsp_consol_close,
#ifdef CONFIG_COMPAT
	.compat_ioctl = adsp_consol_ioctl,
#endif
	.unlocked_ioctl = adsp_consol_ioctl
};

int
adsp_create_cnsl(struct dentry *adsp_debugfs_root, struct nvadsp_cnsl *cnsl)
{
	int ret = 0;

	struct device *dev = cnsl->dev;

	if (IS_ERR_OR_NULL(adsp_debugfs_root)) {
		ret = -ENOENT;
		goto err_out;
	}

	if (!debugfs_create_file("adsp_console", S_IRUSR,
		adsp_debugfs_root, cnsl,
		&adsp_console_operations)) {
		dev_err(dev,
			"unable to create adsp console debug fs file\n");
		ret = -ENOENT;
		goto err_out;
	}

	memset(&cnsl->app_mbox, 0, sizeof(cnsl->app_mbox));

err_out:
	return ret;
}
