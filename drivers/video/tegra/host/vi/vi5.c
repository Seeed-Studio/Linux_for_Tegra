// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2017-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * VI5 driver
 */

#include <nvidia/conftest.h>

#include <asm/ioctls.h>
/*
 * The host1x-next.h header must be included before the nvhost.h
 * header, as the nvhost.h header includes the host1x.h header,
 * which is mutually exclusive with host1x-next.h.
 */
#include <linux/host1x-next.h>
#include <linux/iommu.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/interconnect.h>
#include <linux/module.h>
#include <linux/nvhost.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform/tegra/emc_bwmgr.h>
#include <linux/platform/tegra/latency_allowance.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <media/fusa-capture/capture-vi-channel.h>
#include <media/mc_common.h>
#include <media/tegra_camera_platform.h>
#include <media/vi.h>
#include <soc/tegra/camrtc-capture.h>
#include <soc/tegra/fuse.h>
#include <uapi/linux/nvhost_vi_ioctl.h>

#include "vi5.h"

/* HW capability, pixels per clock */
#define NUM_PPC		8
/* 15% bus protocol overhead */
/* + 5% SW overhead */
#define VI_OVERHEAD	20

#define VI_CLASS_ID 0x30

struct host_vi5 {
	struct platform_device *pdev;
	struct vi vi_common;
	struct icc_path *icc_write;

	/* Debugfs */
	struct vi5_debug {
		struct debugfs_regset32 ch0;
	} debug;

	dma_addr_t syncpt_base;
	size_t syncpt_size;
	uint32_t syncpt_stride;

	/* WAR: Adding a temp flags to avoid registering to V4L2 and
	 * tegra camera platform device.
	 */
	bool skip_v4l2_init;
};

static int vi5_init_debugfs(struct host_vi5 *vi5);
static void vi5_remove_debugfs(struct host_vi5 *vi5);

static int vi5_alloc_syncpt(struct platform_device *pdev,
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

int nvhost_vi5_aggregate_constraints(struct platform_device *dev,
				int clk_index,
				unsigned long floor_rate,
				unsigned long pixelrate,
				unsigned long bw_constraint)
{
	struct nvhost_device_data *pdata = nvhost_get_devdata(dev);

	if (!pdata) {
		dev_err(&dev->dev,
			"No platform data, fall back to default policy\n");
		return 0;
	}

	if (clk_index != 0)
		return 0;
	/*
	 * SCF and V4l2 send request using NVHOST_CLK through tegra_camera_platform,
	 * which is calculated in floor_rate.
	 */
	return floor_rate + (pixelrate / pdata->num_ppc);
}

static void vi5_release_syncpt(struct platform_device *pdev, uint32_t id)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct host1x_syncpt *sp = NULL;

	dev_dbg(&pdev->dev, "%s: id=%u\n", __func__, id);

	sp = host1x_syncpt_get_by_id_noref(pdata->host1x, id);
	if (WARN_ON(!sp))
		return;

	host1x_syncpt_put(sp);
}

static void vi5_fast_forward_syncpt(struct platform_device *pdev, uint32_t id, uint32_t threshold)
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

static void vi5_get_gos_table(struct platform_device *pdev, int *count,
			const dma_addr_t **table)
{
	if (count)
		*count = 0;
	*table = NULL;
}

static int vi5_get_syncpt_gos_backing(struct platform_device *pdev,
			uint32_t id,
			dma_addr_t *syncpt_addr,
			uint32_t *gos_index,
			uint32_t *gos_offset)
{
	uint32_t index = GOS_INDEX_INVALID;
	uint32_t offset = 0;
	dma_addr_t addr;
	struct nvhost_device_data *pdata;
	struct host_vi5 *vi5;

	if (id == 0) {
		dev_err(&pdev->dev, "%s: syncpt id is invalid\n", __func__);
		return -EINVAL;
	}

	if (syncpt_addr == NULL || gos_index == NULL || gos_offset == NULL) {
		dev_err(&pdev->dev, "%s: null arguments\n", __func__);
		return -EINVAL;
	}

	pdata = platform_get_drvdata(pdev);
	vi5 = pdata->private_data;

	addr = vi5->syncpt_base + vi5->syncpt_stride * id;

	*syncpt_addr = addr;
	*gos_index = index;
	*gos_offset = offset;

	dev_info(&pdev->dev, "%s: id=%u addr=0x%llx gos_idx=%u gos_offset=%u\n",
		__func__, id, addr, index, offset);

	return 0;
}

static struct vi_channel_drv_ops vi5_channel_drv_ops = {
	.alloc_syncpt = vi5_alloc_syncpt,
	.release_syncpt = vi5_release_syncpt,
	.fast_forward_syncpt = vi5_fast_forward_syncpt,
	.get_gos_table = vi5_get_gos_table,
	.get_syncpt_gos_backing = vi5_get_syncpt_gos_backing,
};

int vi5_priv_early_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nvhost_device_data *info;
	struct host_vi5 *vi5;
	int err = 0;

	info = (void *)of_device_get_match_data(dev);
	if (unlikely(info == NULL)) {
		dev_WARN(dev, "no platform data\n");
		return -ENODATA;
	}

	err = vi_channel_drv_fops_register(&vi5_channel_drv_ops);
	if (err) {
		dev_warn(&pdev->dev, "syncpt fops register failed, defer probe\n");
		goto error;
	}

	vi5 = (struct host_vi5 *) devm_kzalloc(dev, sizeof(*vi5), GFP_KERNEL);
	if (!vi5) {
		err = -ENOMEM;
		goto error;
	}

	vi5->skip_v4l2_init = of_property_read_bool(dev->of_node,
					"nvidia,skip-v4l2-init");
	vi5->pdev = pdev;
	info->pdev = pdev;
	mutex_init(&info->lock);
	platform_set_drvdata(pdev, info);
	info->private_data = vi5;

	(void) dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(39));

#ifdef CONFIG_DMABUF_DEFERRED_UNMAPPING
	if (dma_buf_defer_unmapping(dev, true) < 0)
		dev_warn(dev, "Failed to set deferred dma buffer unmapping\n");
#endif

	return 0;

error:
	if (err != -EPROBE_DEFER)
		dev_err(&pdev->dev, "probe failed: %d\n", err);

	info->private_data = NULL;

	return err;
}

int vi5_priv_late_probe(struct platform_device *pdev)
{
	struct tegra_camera_dev_info vi_info;
	struct nvhost_device_data *info = platform_get_drvdata(pdev);
	struct host_vi5 *vi5 = info->private_data;
	int err;

	memset(&vi_info, 0, sizeof(vi_info));
	vi_info.pdev = pdev;
	vi_info.hw_type = HWTYPE_VI;
	vi_info.ppc = NUM_PPC;
	vi_info.overhead = VI_OVERHEAD;

	err = tegra_camera_device_register(&vi_info, vi5);
	if (err)
		return err;

	vi5_init_debugfs(vi5);

	return 0;
}

static int vi5_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nvhost_device_data *pdata;
	struct host_vi5 *vi5;
	int err;
	phys_addr_t base;
	uint32_t stride;
	uint32_t num_syncpts;

	dev_dbg(dev, "%s: probe %s\n", __func__, pdev->name);

	err = vi5_priv_early_probe(pdev);
	if (err)
		goto error;

	pdata = platform_get_drvdata(pdev);
	vi5 = pdata->private_data;

	vi5->icc_write = devm_of_icc_get(dev, "write");
	if (IS_ERR(vi5->icc_write))
		return dev_err_probe(&pdev->dev, PTR_ERR(vi5->icc_write),
				     "failed to get icc write handle\n");

	pdata->host1x = dev_get_drvdata(pdev->dev.parent);
	if (!pdata->host1x) {
		err = -ENODEV;
		dev_err(&pdev->dev, "Error getting host1x data\n");
		goto error;
	}

	err = nvhost_module_init(pdev);
	if (err)
		goto error;

	dev_info(&pdev->dev, "%s: client init done\n", __func__);

	err = host1x_syncpt_get_shim_info(pdata->host1x, &base, &stride, &num_syncpts);
	if (err) {
		dev_err(&pdev->dev, "Failed to get shim info\n");
		goto deinit;
	}

	vi5->syncpt_stride = stride;
	vi5->syncpt_size = stride * num_syncpts;

	if (iommu_get_domain_for_dev(&pdev->dev)) {
		vi5->syncpt_base = dma_map_resource(&pdev->dev, base,
						vi5->syncpt_size, DMA_BIDIRECTIONAL,
						DMA_ATTR_SKIP_CPU_SYNC);
		if (dma_mapping_error(&pdev->dev, vi5->syncpt_base)) {
			err = -ENOMEM;
			goto error;
		}
	} else {
		vi5->syncpt_base = base;
	}

	err = vi5_priv_late_probe(pdev);
	if (err)
		goto deinit;

	return 0;

deinit:
	nvhost_module_deinit(pdev);
error:
	if (err != -EPROBE_DEFER)
		dev_err(dev, "probe failed: %d\n", err);
	return err;
}

static int vi5_remove(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct host_vi5 *vi5 = pdata->private_data;

	tegra_camera_device_unregister(vi5);

	vi5_remove_debugfs(vi5);
	return 0;
}

static struct nvhost_device_data t19_vi5_info = {
	.devfs_name		= "vi",
	.moduleid		= 2, //NVHOST_MODULE_VI,
	.clocks = {
		{"vi", UINT_MAX},
		{"emc", 0,
		 NVHOST_MODULE_ID_EXTERNAL_MEMORY_CONTROLLER,
		 TEGRA_SET_EMC_FLOOR, false, UINT_MAX}
	},
	.num_ppc		= 8,
	.aggregate_constraints	= nvhost_vi5_aggregate_constraints,
	.pre_virt_init		= vi5_priv_early_probe,
	.post_virt_init		= vi5_priv_late_probe,
	.bwmgr_client_id	= TEGRA_BWMGR_CLIENT_VI,
};

static struct nvhost_device_data t23x_vi0_info = {
	.devfs_name		= "vi0",
	.moduleid		= 2, //NVHOST_MODULE_VI,
	.clocks = {
		{"vi", UINT_MAX},
	},
	.num_ppc		= 8,
	.aggregate_constraints	= nvhost_vi5_aggregate_constraints,
	.pre_virt_init		= vi5_priv_early_probe,
	.post_virt_init		= vi5_priv_late_probe,
	.class			= VI_CLASS_ID,
};

static struct nvhost_device_data t23x_vi1_info = {
	.devfs_name		= "vi1",
	.moduleid		= 3, //NVHOST_MODULE_VI2,
	.clocks = {
		{"vi", UINT_MAX},
	},
	.num_ppc		= 8,
	.aggregate_constraints	= nvhost_vi5_aggregate_constraints,
	.pre_virt_init		= vi5_priv_early_probe,
	.post_virt_init		= vi5_priv_late_probe,
	.class			= VI_CLASS_ID,
};

static const struct of_device_id tegra_vi5_of_match[] = {
	{
		.name = "vi",
		.compatible = "nvidia,tegra194-vi",
		.data = &t19_vi5_info,
	},
	{
		.name = "vi0",
		.compatible = "nvidia,tegra234-vi",
		.data = &t23x_vi0_info,
	},
	{
		.name = "vi1",
		.compatible = "nvidia,tegra234-vi",
		.data = &t23x_vi1_info,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_vi5_of_match);

static int vi_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct nvhost_device_data *info = platform_get_drvdata(pdev);
	struct host_vi5 *vi5 = info->private_data;
	int err;

	if (nvhost_module_pm_ops.runtime_suspend != NULL) {
		err = nvhost_module_pm_ops.runtime_suspend(dev);
		if (!err && vi5->icc_write) {
			err = icc_set_bw(vi5->icc_write, 0, 0);
			if (err)
				dev_warn(dev,
					 "failed to set icc_write bw: %d\n", err);
			return 0;
		}
		return err;
	}

	return -EOPNOTSUPP;
}

static int vi_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct host_vi5 *vi5 = pdata->private_data;
	int err;

	if (nvhost_module_pm_ops.runtime_resume != NULL) {
		err = nvhost_module_pm_ops.runtime_resume(dev);
		if (!err && vi5->icc_write) {
			err = icc_set_bw(vi5->icc_write, 0, UINT_MAX);
			if (err)
				dev_warn(dev,
					 "failed to set icc_write bw: %d\n", err);
			return 0;
		}
		return err;
	}

	return -EOPNOTSUPP;
}

const struct dev_pm_ops vi_pm_ops = {
	SET_RUNTIME_PM_OPS(vi_runtime_suspend, vi_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void vi5_remove_wrapper(struct platform_device *pdev)
{
	vi5_remove(pdev);
}
#else
static int vi5_remove_wrapper(struct platform_device *pdev)
{
	return vi5_remove(pdev);
}
#endif

static struct platform_driver vi5_driver = {
	.probe = vi5_probe,
	.remove = vi5_remove_wrapper,
	.driver = {
		.owner = THIS_MODULE,
		.name = "tegra194-vi5",
#ifdef CONFIG_OF
		.of_match_table = tegra_vi5_of_match,
#endif
#ifdef CONFIG_PM
		.pm = &vi_pm_ops,
#endif
	},
};

module_platform_driver(vi5_driver);

/* === Debugfs ========================================================== */

static int vi5_init_debugfs(struct host_vi5 *vi5)
{
	static const struct debugfs_reg32 vi5_ch_regs[] = {
		{ .name = "protocol_version", 0x00 },
		{ .name = "perforce_changelist", 0x4 },
		{ .name = "build_timestamp", 0x8 },
		{ .name = "channel_count", 0x80 },
	};
	struct nvhost_device_data *pdata = platform_get_drvdata(vi5->pdev);

	if (pdata->aperture[0]) {
		struct dentry *dir = pdata->debugfs;
		struct vi5_debug *debug = &vi5->debug;

		debug->ch0.base = pdata->aperture[0];
		debug->ch0.regs = vi5_ch_regs;
		debug->ch0.nregs = ARRAY_SIZE(vi5_ch_regs);
		debugfs_create_regset32("ch0", 0444, dir, &debug->ch0);
	} else {
		dev_info(&vi5->pdev->dev, "Unable to create debugfs entry\n");
	}

	return 0;
}

static void vi5_remove_debugfs(struct host_vi5 *vi5)
{
}
MODULE_LICENSE("GPL");
