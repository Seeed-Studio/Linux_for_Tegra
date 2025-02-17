// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2017-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include "nvcsi-t194.h"

#include <uapi/linux/nvhost_nvcsi_ioctl.h>
#include <linux/tegra-camera-rtcpu.h>

#include <asm/ioctls.h>
#include <linux/host1x-next.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/nvhost.h>

#include <media/mc_common.h>
#include <media/tegra_camera_platform.h>
#include "camera/nvcsi/csi5_fops.h"

#include "media/csi.h"

/* PG rate based on max ISP throughput */
#define PG_CLK_RATE	102000000
/* width of interface between VI and CSI */
#define CSI_BUS_WIDTH	64
/* number of lanes per brick */
#define NUM_LANES	4

#define PHY_OFFSET			0x10000U
#define CIL_A_SW_RESET			0x11024U
#define CIL_B_SW_RESET			0x110b0U
#define CSIA				(1 << 20)
#define CSIH				(1 << 27)

static struct tegra_csi_device *mc_csi;
struct t194_nvcsi {
	struct platform_device *pdev;
	struct tegra_csi_device csi;
	struct dentry *dir;
	struct clk *clk;
};

struct nvhost_device_data t19_nvcsi_info = {
	.moduleid		= 14, //NVHOST_MODULE_NVCSI,
	.clocks			= {
		{"nvcsi", 400000000},
	},
	.devfs_name		= "nvcsi",
	.autosuspend_delay      = 500,
	.can_powergate = true,
};

static const struct of_device_id tegra194_nvcsi_of_match[] = {
	{
		.compatible = "nvidia,tegra194-nvcsi",
		.data = &t19_nvcsi_info,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, tegra194_nvcsi_of_match);

struct t194_nvcsi_file_private {
	struct platform_device *pdev;
};

static long t194_nvcsi_ioctl(struct file *file, unsigned int cmd,
			unsigned long arg)
{
	return -ENOIOCTLCMD;
}

static int t194_nvcsi_open(struct inode *inode, struct file *file)
{
	struct nvhost_device_data *pdata = container_of(inode->i_cdev,
					struct nvhost_device_data, ctrl_cdev);
	struct platform_device *pdev = pdata->pdev;
	struct t194_nvcsi_file_private *filepriv;

	filepriv = kzalloc(sizeof(*filepriv), GFP_KERNEL);
	if (unlikely(filepriv == NULL))
		return -ENOMEM;

	filepriv->pdev = pdev;

	file->private_data = filepriv;

	return nonseekable_open(inode, file);
}

static int t194_nvcsi_release(struct inode *inode, struct file *file)
{
	struct t194_nvcsi_file_private *filepriv = file->private_data;

	kfree(filepriv);

	return 0;
}

static int t194_nvcsi_module_init(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	unsigned int i;
	int err;

	err = devm_clk_bulk_get_all(&pdev->dev, &pdata->clks);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to get clocks %d\n", err);
		return err;
	}
	pdata->num_clks = err;

	for (i = 0; i < pdata->num_clks; i++) {
		err = clk_set_rate(pdata->clks[i].clk, ULONG_MAX);
		if (err < 0) {
			dev_err(&pdev->dev, "failed to set clock rate!\n");
			return err;
		}
	}

	pdata->reset_control = devm_reset_control_get_exclusive_released(
					&pdev->dev, NULL);
	if (IS_ERR(pdata->reset_control)) {
		dev_err(&pdev->dev, "failed to get reset\n");
		return PTR_ERR(pdata->reset_control);
	}

	reset_control_acquire(pdata->reset_control);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to acquire reset: %d\n", err);
		return err;
	}

	err = clk_bulk_prepare_enable(pdata->num_clks, pdata->clks);
	if (err < 0) {
		reset_control_release(pdata->reset_control);
		dev_err(&pdev->dev, "failed to enabled clocks: %d\n", err);
		return err;
	}

	reset_control_reset(pdata->reset_control);
	clk_bulk_disable_unprepare(pdata->num_clks, pdata->clks);
	reset_control_release(pdata->reset_control);

	if (pdata->autosuspend_delay) {
		pm_runtime_set_autosuspend_delay(&pdev->dev,
			pdata->autosuspend_delay);
		pm_runtime_use_autosuspend(&pdev->dev);
	}

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev))
		return -EOPNOTSUPP;

	pdata->debugfs = debugfs_create_dir(pdev->dev.of_node->name,
					    NULL);

	return 0;

}

static void t194_nvcsi_module_deinit(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);

	debugfs_remove_recursive(pdata->debugfs);
}


const struct file_operations tegra194_nvcsi_ctrl_ops = {
	.owner = THIS_MODULE,
#if defined(NV_NO_LLSEEK_PRESENT)
	.llseek = no_llseek,
#endif
	.unlocked_ioctl = t194_nvcsi_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = t194_nvcsi_ioctl,
#endif
	.open = t194_nvcsi_open,
	.release = t194_nvcsi_release,
};

int t194_nvcsi_early_probe(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata;
	struct t194_nvcsi *nvcsi;

	pdata = (void *)of_device_get_match_data(&pdev->dev);
	if (unlikely(pdata == NULL)) {
		dev_WARN(&pdev->dev, "no platform data\n");
		return -ENODATA;
	}

	nvcsi = devm_kzalloc(&pdev->dev, sizeof(*nvcsi), GFP_KERNEL);
	if (!nvcsi)
		return -ENOMEM;

	pdata->pdev = pdev;
	nvcsi->pdev = pdev;
	mutex_init(&pdata->lock);
	platform_set_drvdata(pdev, pdata);
	mc_csi = &nvcsi->csi;

	pdata->private_data = nvcsi;

	return 0;
}

static int t194_nvcsi_set_rate(struct tegra_camera_dev_info *cdev_info, unsigned long rate)
{
	struct nvhost_device_data *info = platform_get_drvdata(cdev_info->pdev);
	struct t194_nvcsi *nvcsi = info->private_data;

	return clk_set_rate(nvcsi->clk, rate);
}

static struct tegra_camera_dev_ops t194_nvcsi_cdev_ops = {
	.set_rate = t194_nvcsi_set_rate,
};

int t194_nvcsi_late_probe(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct t194_nvcsi *nvcsi = pdata->private_data;
	struct tegra_camera_dev_info csi_info;
	int err;

	memset(&csi_info, 0, sizeof(csi_info));
	csi_info.pdev = pdev;
	csi_info.hw_type = HWTYPE_CSI;
	csi_info.use_max = true;
	csi_info.bus_width = CSI_BUS_WIDTH;
	csi_info.lane_num = NUM_LANES;
	csi_info.pg_clk_rate = PG_CLK_RATE;
	csi_info.ops = &t194_nvcsi_cdev_ops;

	err = tegra_camera_device_register(&csi_info, nvcsi);
	if (err)
		return err;

	nvcsi->pdev = pdev;
	nvcsi->csi.fops = &csi5_fops;
	err = tegra_csi_media_controller_init(&nvcsi->csi, pdev);

	return 0;
}

static int t194_nvcsi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nvhost_device_data *pdata;
	struct t194_nvcsi *nvcsi;
	int err;

	err = t194_nvcsi_early_probe(pdev);
	if (err)
		return err;

	pdata = platform_get_drvdata(pdev);

	nvcsi = pdata->private_data;

	nvcsi->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(nvcsi->clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		return PTR_ERR(nvcsi->clk);
	}

	err = t194_nvcsi_module_init(pdev);
	if (err)
		return err;

	err = t194_nvcsi_late_probe(pdev);
	if (err)
		goto deinit;

	return 0;

deinit:
	t194_nvcsi_module_deinit(pdev);
	return err;
}

static int __exit t194_nvcsi_remove(struct platform_device *dev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(dev);
	struct t194_nvcsi *nvcsi = pdata->private_data;

	tegra_camera_device_unregister(nvcsi);
	mc_csi = NULL;
	tegra_csi_media_controller_remove(&nvcsi->csi);

	return 0;
}

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void t194_nvcsi_remove_wrapper(struct platform_device *pdev)
{
	t194_nvcsi_remove(pdev);
}
#else
static int t194_nvcsi_remove_wrapper(struct platform_device *pdev)
{
	return t194_nvcsi_remove(pdev);
}
#endif

static struct platform_driver t194_nvcsi_driver = {
	.probe = t194_nvcsi_probe,
	.remove = __exit_p(t194_nvcsi_remove_wrapper),
	.driver = {
		.owner = THIS_MODULE,
		.name = "t194-nvcsi",
#ifdef CONFIG_OF
		.of_match_table = tegra194_nvcsi_of_match,
#endif
#ifdef CONFIG_PM
		.pm = &nvhost_module_pm_ops,
#endif
	},
};

module_platform_driver(t194_nvcsi_driver);
MODULE_LICENSE("GPL");
