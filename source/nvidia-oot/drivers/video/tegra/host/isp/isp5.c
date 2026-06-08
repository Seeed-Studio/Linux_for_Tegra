// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2017-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include <asm/ioctls.h>
/*
 * The host1x-next.h header must be included before the nvhost.h
 * header, as the nvhost.h header includes the host1x.h header,
 * which is mutually exclusive with host1x-next.h.
 */
#include <linux/host1x-next.h>
#include <linux/iommu.h>
#include <linux/clk.h>
#include <linux/reset.h>
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
#include <linux/reset.h>
#include <media/fusa-capture/capture-isp-channel.h>
#include <media/tegra_camera_platform.h>
#include <soc/tegra/camrtc-capture.h>
#include <soc/tegra/fuse-helper.h>
#include <linux/cdev.h>

#include "isp5.h"
#include <uapi/linux/nvhost_isp_ioctl.h>
#include <uapi/linux/nvhost_ioctl.h>

#define ISP_PPC		2
/* 20% overhead */
#define ISP_OVERHEAD	20

#define ISP_CLASS_ID 0x32

struct host_isp5 {
	struct platform_device *pdev;
	struct clk *clk;
	dma_addr_t syncpt_base;
	size_t syncpt_size;
	uint32_t syncpt_stride;
};

static int isp5_alloc_syncpt(struct platform_device *pdev,
			const char *name,
			uint32_t *syncpt_id)
{
	uint32_t id;
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct host1x_syncpt *sp = NULL;

	if (syncpt_id == NULL) {
		dev_err(&pdev->dev, "%s: null argument\n", __func__);
		return -EINVAL;
	}

	sp = host1x_syncpt_alloc(pdata->host1x, HOST1X_SYNCPT_CLIENT_MANAGED,
				name ? name : dev_name(&pdev->dev));
	if (!sp) {
		dev_err(&pdev->dev,
			"%s: allocation of requested sync point failed (name=%s, size=%d)\n",
					__func__, name ? name : "", GFP_KERNEL);
		return -ENOMEM;
	}

	id = host1x_syncpt_id(sp);
	if (id == 0) {
		dev_err(&pdev->dev, "%s: syncpt allocation failed\n", __func__);
		return -ENODEV;
	}

	*syncpt_id = id;

	return 0;
}

static void isp5_release_syncpt(struct platform_device *pdev, uint32_t id)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct host1x_syncpt *sp = NULL;

	dev_dbg(&pdev->dev, "%s: id=%u\n", __func__, id);

	sp = host1x_syncpt_get_by_id_noref(pdata->host1x, id);
	if (WARN_ON(!sp))
		return;

	host1x_syncpt_put(sp);
}

static void isp5_fast_forward_syncpt(struct platform_device *pdev, uint32_t id, uint32_t threshold)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct host1x_syncpt *sp = NULL;
	uint32_t cur;

	dev_dbg(&pdev->dev, "%s: id=%u -> thresh=%u\n", __func__, id, threshold);

	sp = host1x_syncpt_get_by_id_noref(pdata->host1x, id);
	if (WARN_ON(!sp))
		return;

	cur = host1x_syncpt_read(sp);
	while (cur++ != threshold)
		host1x_syncpt_incr(sp);

	host1x_syncpt_read(sp);
}

static int isp5_get_syncpt_gos_backing(struct platform_device *pdev,
			uint32_t id,
			dma_addr_t *syncpt_addr,
			uint32_t *gos_index,
			uint32_t *gos_offset)
{
	uint32_t index = GOS_INDEX_INVALID;
	uint32_t offset = 0;
	dma_addr_t addr;
	struct nvhost_device_data *pdata;
	struct host_isp5 *isp5;

	if (id == 0) {
		dev_err(&pdev->dev, "%s: syncpt id is invalid\n", __func__);
		return -EINVAL;
	}

	if (syncpt_addr == NULL || gos_index == NULL || gos_offset == NULL) {
		dev_err(&pdev->dev, "%s: null arguments\n", __func__);
		return -EINVAL;
	}

	pdata = platform_get_drvdata(pdev);
	isp5 = pdata->private_data;

	addr = isp5->syncpt_base + isp5->syncpt_stride * id;

	*syncpt_addr = addr;
	*gos_index = index;
	*gos_offset = offset;

	dev_dbg(&pdev->dev, "%s: id=%u addr=0x%llx gos_idx=%u gos_offset=%u\n",
		__func__, id, addr, index, offset);

	return 0;
}

static uint32_t isp5_get_gos_table(struct platform_device *pdev,
			const dma_addr_t **table)
{
	uint32_t count = 0;
	*table = NULL;

	return count;
}

static int isp5_module_init(struct platform_device *pdev)
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

static void isp5_module_deinit(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);

	debugfs_remove_recursive(pdata->debugfs);
}

static struct isp_channel_drv_ops isp5_channel_drv_ops = {
	.alloc_syncpt = isp5_alloc_syncpt,
	.release_syncpt = isp5_release_syncpt,
	.fast_forward_syncpt = isp5_fast_forward_syncpt,
	.get_gos_table = isp5_get_gos_table,
	.get_syncpt_gos_backing = isp5_get_syncpt_gos_backing,
};

int isp5_priv_early_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nvhost_device_data *info;
	struct host_isp5 *isp5;
	int err = 0;

	info = (void *)of_device_get_match_data(dev);
	if (unlikely(info == NULL)) {
		dev_WARN(dev, "no platform data\n");
		return -ENODATA;
	}

	err = isp_channel_drv_fops_register(&isp5_channel_drv_ops);
	if (err) {
		goto error;
	}

	isp5 = devm_kzalloc(dev, sizeof(*isp5), GFP_KERNEL);
	if (!isp5)
		return -ENOMEM;

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
		return err;

	return 0;
}

static struct device *isp5_client_device_create(struct platform_device *pdev,
		struct cdev *cdev,
		const char *cdev_name,
		dev_t devno,
		const struct file_operations *ops)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct device *dev;
	int err;

#if	defined(NV_CLASS_CREATE_HAS_NO_OWNER_ARG) /* Linux v6.4	*/
	pdata->nvhost_class = class_create(pdev->dev.of_node->name);
#else
	pdata->nvhost_class = class_create(THIS_MODULE, pdev->dev.of_node->name);
#endif
	if (IS_ERR(pdata->nvhost_class)) {
		dev_err(&pdev->dev, "failed to create class\n");
		return ERR_CAST(pdata->nvhost_class);
	}

	cdev_init(cdev, ops);
	cdev->owner = THIS_MODULE;

	err = cdev_add(cdev, devno, 1);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to add cdev\n");
		class_destroy(pdata->nvhost_class);
		return ERR_PTR(err);
	}

	dev = device_create(pdata->nvhost_class, &pdev->dev, devno, NULL,
				(pdev->id <= 0) ? "nvhost-%s%s" : "nvhost-%s%s.%d",
				cdev_name, pdev->dev.of_node->name, pdev->id);

	if (IS_ERR(dev)) {
		dev_err(&pdev->dev, "failed to create %s device\n", cdev_name);
		class_destroy(pdata->nvhost_class);
		cdev_del(cdev);
	}

	return dev;
}

static int isp5_client_device_init(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	dev_t devno;
	int	err;

	err = alloc_chrdev_region(&devno, 0, 1, "nvhost");
	if (err < 0) {
		dev_err(&pdev->dev, "failed to reserve chrdev region\n");
		return err;
	}

	pdata->ctrl_node = isp5_client_device_create(pdev, &pdata->ctrl_cdev,
				"ctrl-", devno,
				pdata->ctrl_ops);
	if (IS_ERR(pdata->ctrl_node)) {
		unregister_chrdev_region(devno, 1);
		return PTR_ERR(pdata->ctrl_node);
	}

	pdata->cdev_region = devno;

	return 0;
}

static void	isp5_client_device_release(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);

	if (!IS_ERR_OR_NULL(pdata->ctrl_node)) {
		device_destroy(pdata->nvhost_class, pdata->ctrl_cdev.dev);
		cdev_del(&pdata->ctrl_cdev);
		class_destroy(pdata->nvhost_class);
	}

	unregister_chrdev_region(pdata->cdev_region, 1);
}

static int isp5_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nvhost_device_data *pdata;
	struct host_isp5 *isp5;
	int err = 0;
	phys_addr_t base;
	uint32_t stride;
	uint32_t num_syncpts;

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

	pdata->host1x = dev_get_drvdata(pdev->dev.parent);
	if (!pdata->host1x) {
		err = -ENODEV;
		dev_err(&pdev->dev, "Error getting host1x data\n");
		goto error;
	}

	err = isp5_module_init(pdev);
	if (err)
		goto deinit;

	err = isp5_client_device_init(pdev);
	if (err)
		goto deinit;

	err = host1x_syncpt_get_shim_info(pdata->host1x, &base, &stride, &num_syncpts);
	if (err) {
		dev_err(&pdev->dev, "Failed to get shim info\n");
		goto release_client;
	}

	isp5->syncpt_stride = stride;
	isp5->syncpt_size = stride * num_syncpts;

	if (iommu_get_domain_for_dev(&pdev->dev)) {
		isp5->syncpt_base = dma_map_resource(&pdev->dev, base,
						isp5->syncpt_size, DMA_BIDIRECTIONAL,
						DMA_ATTR_SKIP_CPU_SYNC);
		if (dma_mapping_error(&pdev->dev, isp5->syncpt_base)) {
			err = -ENOMEM;
			goto release_client;
		}
	} else {
		isp5->syncpt_base = base;
	}

	err = isp5_priv_late_probe(pdev);
	if (err)
		goto release_client;

	return 0;

release_client:
	isp5_client_device_release(pdev);
deinit:
	isp5_module_deinit(pdev);
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

const struct file_operations isp_ctrl_ops = {
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
	isp5_client_device_release(pdev);
	isp_channel_drv_unregister(&pdev->dev);

	return 0;
}

struct nvhost_device_data t19_isp5_info = {
	.devfs_name		= "isp",
	.moduleid		= 4, //NVHOST_MODULE_ISP,
	.clocks			= {
		{"isp", UINT_MAX},
	},
	.ctrl_ops		= &isp_ctrl_ops,
	.pre_virt_init		= isp5_priv_early_probe,
	.post_virt_init		= isp5_priv_late_probe,
	.autosuspend_delay      = 500,
	.can_powergate = true,
	.class			= ISP_CLASS_ID,
};

struct nvhost_device_data t264_isp_info = {
	.devfs_name             = "isp",
	.moduleid               = NVHOST_MODULE_ISP,
	.clocks                 = {
		{"isp", UINT_MAX},
	},
	.ctrl_ops		= &isp_ctrl_ops,
	.pre_virt_init          = isp5_priv_early_probe,
	.post_virt_init         = isp5_priv_late_probe,
	.can_powergate = false,
	.class                  = ISP_CLASS_ID,
};

struct nvhost_device_data t264_isp1_info = {
	.devfs_name             = "isp1",
	.moduleid               = NVHOST_MODULE_ISPB,
	.clocks                 = {
		{"isp1", UINT_MAX},
	},
	.ctrl_ops		= &isp_ctrl_ops,
	.pre_virt_init          = isp5_priv_early_probe,
	.post_virt_init         = isp5_priv_late_probe,
	.can_powergate = false,
	.class                  = ISP_CLASS_ID,
};

static const struct of_device_id tegra_isp5_of_match[] = {
	{
		.name = "isp",
		.compatible = "nvidia,tegra264-isp",
		.data = &t264_isp_info,
	},
	{
		.name = "isp1",
		.compatible = "nvidia,tegra264-isp",
		.data = &t264_isp1_info,
	},
	{
		.compatible = "nvidia,tegra194-isp",
		.data = &t19_isp5_info,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_isp5_of_match);

static int isp_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct nvhost_device_data *info = platform_get_drvdata(pdev);

	clk_bulk_disable_unprepare(info->num_clks, info->clks);

	return 0;
}

static int isp_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	int err;

	err = clk_bulk_prepare_enable(pdata->num_clks, pdata->clks);
	if (err) {
		dev_warn(dev, "failed to enable clocks: %d\n", err);
		return err;
	}

	return 0;
}

const struct dev_pm_ops isp_pm_ops = {
	SET_RUNTIME_PM_OPS(isp_runtime_suspend, isp_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void isp5_remove_wrapper(struct platform_device *pdev)
{
	isp5_remove(pdev);
}
#else
static int isp5_remove_wrapper(struct platform_device *pdev)
{
	return isp5_remove(pdev);
}
#endif

static struct platform_driver isp5_driver = {
	.probe = isp5_probe,
	.remove = isp5_remove_wrapper,
	.driver = {
		.owner = THIS_MODULE,
		.name = "tegra194-isp5",
#ifdef CONFIG_OF
		.of_match_table = tegra_isp5_of_match,
#endif
#ifdef CONFIG_PM
		.pm = &isp_pm_ops,
#endif
	},
};

module_platform_driver(isp5_driver);

MODULE_LICENSE("GPL");
