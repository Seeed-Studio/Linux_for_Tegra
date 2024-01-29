// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2017-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <asm/ioctls.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/nvhost.h>
#include <media/fusa-capture/capture-isp-channel.h>
#include <media/tegra_camera_platform.h>
#include <soc/tegra/camrtc-capture.h>
#include <soc/tegra/fuse-helper.h>

#include "isp5.h"
#include "capture/capture-support.h"
#include <uapi/linux/nvhost_isp_ioctl.h>

#define ISP_PPC		2
/* 20% overhead */
#define ISP_OVERHEAD	20

#define ISP_CLASS_ID 0x32

struct host_isp5 {
	struct platform_device *pdev;
	struct platform_device *isp_thi;
	struct clk *clk;
};

static int isp5_alloc_syncpt(struct platform_device *pdev,
			const char *name,
			uint32_t *syncpt_id)
{
	struct nvhost_device_data *info = platform_get_drvdata(pdev);
	struct host_isp5 *isp5 = info->private_data;

	return capture_alloc_syncpt(isp5->isp_thi, name, syncpt_id);
}

static void isp5_release_syncpt(struct platform_device *pdev, uint32_t id)
{
	struct nvhost_device_data *info = platform_get_drvdata(pdev);
	struct host_isp5 *isp5 = info->private_data;

	capture_release_syncpt(isp5->isp_thi, id);
}

static int isp5_get_syncpt_gos_backing(struct platform_device *pdev,
			uint32_t id,
			dma_addr_t *syncpt_addr,
			uint32_t *gos_index,
			uint32_t *gos_offset)
{
	struct nvhost_device_data *info = platform_get_drvdata(pdev);
	struct host_isp5 *isp5 = info->private_data;

	return capture_get_syncpt_gos_backing(isp5->isp_thi, id,
				syncpt_addr, gos_index, gos_offset);

}

static uint32_t isp5_get_gos_table(struct platform_device *pdev,
			const dma_addr_t **table)
{
	struct nvhost_device_data *info = platform_get_drvdata(pdev);
	struct host_isp5 *isp5 = info->private_data;
	uint32_t count;

	capture_get_gos_table(isp5->isp_thi, &count, table);

	return count;
}

static struct isp_channel_drv_ops isp5_channel_drv_ops = {
	.alloc_syncpt = isp5_alloc_syncpt,
	.release_syncpt = isp5_release_syncpt,
	.get_gos_table = isp5_get_gos_table,
	.get_syncpt_gos_backing = isp5_get_syncpt_gos_backing,
};

int isp5_priv_early_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nvhost_device_data *info;
	struct device_node *thi_np;
	struct platform_device *thi = NULL;
	struct host_isp5 *isp5;
	int err = 0;

	info = (void *)of_device_get_match_data(dev);
	if (unlikely(info == NULL)) {
		dev_WARN(dev, "no platform data\n");
		return -ENODATA;
	}

	thi_np = of_parse_phandle(dev->of_node, "nvidia,isp-falcon-device", 0);
	if (thi_np == NULL) {
		dev_WARN(dev, "missing %s handle\n",
			"nvidia,isp-falcon-device");
		err = -ENODEV;
		goto error;
	}

	thi = of_find_device_by_node(thi_np);
	of_node_put(thi_np);

	if (thi == NULL) {
		err = -ENODEV;
		goto error;
	}

	if (thi->dev.driver == NULL) {
		platform_device_put(thi);
		return -EPROBE_DEFER;
	}

	isp5 = devm_kzalloc(dev, sizeof(*isp5), GFP_KERNEL);
	if (!isp5)
		return -ENOMEM;

	isp5->isp_thi = thi;
	isp5->pdev = pdev;
	info->pdev = pdev;
	mutex_init(&info->lock);
	platform_set_drvdata(pdev, info);
	info->private_data = isp5;

	/* A bit was stolen */
	(void) dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(39));

#ifdef CONFIG_DMABUF_DEFERRED_UNMAPPING
	if (dma_buf_defer_unmapping(dev, true) < 0)
		dev_warn(dev, "Failed to set deferred dma buffer unmapping\n");
#endif

	return 0;

error:
	info->private_data = NULL;
	if (err != -EPROBE_DEFER)
		dev_err(&pdev->dev, "probe failed: %d\n", err);
	return err;
}

static int isp5_set_rate(struct tegra_camera_dev_info *cdev_info, unsigned long rate)
{
	struct nvhost_device_data *info = platform_get_drvdata(cdev_info->pdev);
	struct host_isp5 *isp5 = info->private_data;

	return clk_set_rate(isp5->clk, rate);
}

static struct tegra_camera_dev_ops isp5_cdev_ops = {
	.set_rate = isp5_set_rate,
};

int isp5_priv_late_probe(struct platform_device *pdev)
{
	struct tegra_camera_dev_info isp_info;
	struct nvhost_device_data *info = platform_get_drvdata(pdev);
	struct host_isp5 *isp5 = info->private_data;
	int err;

	memset(&isp_info, 0, sizeof(isp_info));
	isp_info.overhead = ISP_OVERHEAD;
	isp_info.ppc = ISP_PPC;
	isp_info.hw_type = HWTYPE_ISPA;
	isp_info.pdev = pdev;
	isp_info.ops = &isp5_cdev_ops;

	err = tegra_camera_device_register(&isp_info, isp5);
	if (err)
		goto device_release;

	err = isp_channel_drv_register(pdev, &isp5_channel_drv_ops);
	if (err)
		goto device_release;

	return 0;

device_release:
	nvhost_client_device_release(pdev);

	return err;
}

static int isp5_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nvhost_device_data *pdata;
	struct host_isp5 *isp5;
	int err = 0;

	err = isp5_priv_early_probe(pdev);
	if (err)
		goto error;

	pdata = platform_get_drvdata(pdev);
	isp5 = pdata->private_data;

	isp5->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(isp5->clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		return PTR_ERR(isp5->clk);
	}

	err = nvhost_client_device_get_resources(pdev);
	if (err)
		goto put_thi;

	err = nvhost_module_init(pdev);
	if (err)
		goto put_thi;

	err = nvhost_client_device_init(pdev);
	if (err) {
		nvhost_module_deinit(pdev);
		goto put_thi;
	}

	err = isp5_priv_late_probe(pdev);
	if (err)
		goto put_thi;

	return 0;

put_thi:
	platform_device_put(isp5->isp_thi);
error:
	if (err != -EPROBE_DEFER)
		dev_err(&pdev->dev, "probe failed: %d\n", err);
	return err;
}

static long isp_ioctl(struct file *file,
		unsigned int cmd, unsigned long arg)
{
	struct t194_isp5_file_private *filepriv = file->private_data;
	struct platform_device *pdev = filepriv->pdev;

	if (_IOC_TYPE(cmd) != NVHOST_ISP_IOCTL_MAGIC)
		return -EFAULT;

	switch (_IOC_NR(cmd)) {
	case _IOC_NR(NVHOST_ISP_IOCTL_SET_ISP_LA_BW): {
		/* No BW control needed. Return without error. */
		return 0;
	}
	default:
		dev_err(&pdev->dev,
		"%s: Unknown ISP ioctl.\n", __func__);
		return -EINVAL;
	}
	return 0;
}

static int isp_open(struct inode *inode, struct file *file)
{
	struct nvhost_device_data *pdata = container_of(inode->i_cdev,
					struct nvhost_device_data, ctrl_cdev);
	struct platform_device *pdev = pdata->pdev;
	struct t194_isp5_file_private *filepriv;

	filepriv = kzalloc(sizeof(*filepriv), GFP_KERNEL);
	if (unlikely(filepriv == NULL))
		return -ENOMEM;

	filepriv->pdev = pdev;

	file->private_data = filepriv;

	return nonseekable_open(inode, file);
}

static int isp_release(struct inode *inode, struct file *file)
{
	struct t194_isp5_file_private *filepriv = file->private_data;

	kfree(filepriv);

	return 0;
}

const struct file_operations tegra194_isp5_ctrl_ops = {
	.owner = THIS_MODULE,
	.open = isp_open,
	.unlocked_ioctl = isp_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = isp_ioctl,
#endif
	.release = isp_release,
};

static int isp5_remove(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct host_isp5 *isp5 = (struct host_isp5 *)pdata->private_data;

	tegra_camera_device_unregister(isp5);

	isp_channel_drv_unregister(&pdev->dev);

	platform_device_put(isp5->isp_thi);

	return 0;
}

struct nvhost_device_data t19_isp5_info = {
	.devfs_name		= "isp",
	.moduleid		= 4, //NVHOST_MODULE_ISP,
	.clocks			= {
		{"isp", UINT_MAX},
	},
	.ctrl_ops		= &tegra194_isp5_ctrl_ops,
	.pre_virt_init		= isp5_priv_early_probe,
	.post_virt_init		= isp5_priv_late_probe,
	.autosuspend_delay      = 500,
	.can_powergate = true,
	.class			= ISP_CLASS_ID,
};

static const struct of_device_id tegra_isp5_of_match[] = {
	{
		.compatible = "nvidia,tegra194-isp",
		.data = &t19_isp5_info,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_isp5_of_match);

static struct platform_driver isp5_driver = {
	.probe = isp5_probe,
	.remove = isp5_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "tegra194-isp5",
#ifdef CONFIG_OF
		.of_match_table = tegra_isp5_of_match,
#endif
#ifdef CONFIG_PM
		.pm = &nvhost_module_pm_ops,
#endif
	},
};

static int __init capture_isp_init(void)
{
	int err;

	err = isp_channel_drv_init();
	if (err)
		return err;

	err = platform_driver_register(&isp5_driver);
	if (err) {
		isp_channel_drv_exit();
		return err;
	}

	return 0;
}
static void __exit capture_isp_exit(void)
{
	isp_channel_drv_exit();
	platform_driver_unregister(&isp5_driver);
}

module_init(capture_isp_init);
module_exit(capture_isp_exit);
MODULE_LICENSE("GPL");
