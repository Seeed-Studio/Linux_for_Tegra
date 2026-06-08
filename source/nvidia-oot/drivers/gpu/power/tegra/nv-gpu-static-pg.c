// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include <linux/clk.h>
#include <linux/kobject.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <soc/tegra/bpmp.h>
#include <soc/tegra/bpmp-abi.h>

#define TEGRA264_STRAP_NV_FUSE_CTRL_OPT_GPU		1U
#define GPU_PG_MASK_PARAM_DEFAULT 0xFFFFFFFF

/* module parameter for T264 GPU PG mask */
uint gpu_pg_mask_param = GPU_PG_MASK_PARAM_DEFAULT;
module_param(gpu_pg_mask_param, uint, 0664);
MODULE_PARM_DESC(gpu_pg_mask_param, "T264 GPU GPC/TPC/FBP Power-Gating mask");

struct tegra_gpu_pg_profile {
	struct kobj_attribute attr;
	uint32_t gpu_pg_mask;

	/* lock to protect the gpu_pg_mask */
	struct mutex lock;
};

struct tegra_gpu_pg_profile_drv_data {
	struct kobject *gpu_static_pg_kobject;
	struct tegra_bpmp *gpu_pg_bpmp;
	struct tegra_gpu_pg_profile *gpu_pg_profile;
};

static bool is_gpu_pg_mask_param_set(void)
{
	return gpu_pg_mask_param != GPU_PG_MASK_PARAM_DEFAULT;
}

static ssize_t bpmp_set_gpu_pg_mask(struct tegra_bpmp *bpmp, uint32_t gpu_pg_mask)
{
	struct mrq_strap_request req = { 0 };
	struct tegra_bpmp_message msg;
	int ret = 0;

	memset(&req, 0, sizeof(req));
	req.cmd = STRAP_SET;
	req.id = TEGRA264_STRAP_NV_FUSE_CTRL_OPT_GPU;
	req.value = gpu_pg_mask;

	memset(&msg, 0, sizeof(struct tegra_bpmp_message));
	msg.mrq = MRQ_STRAP;
	msg.tx.data = &req;
	msg.tx.size = sizeof(struct mrq_strap_request);
	msg.rx.data = NULL;
	msg.rx.size = 0;
	ret = tegra_bpmp_transfer(bpmp, &msg);

	if (ret != 0)
		ret = -EIO;

	return ret;
}

static ssize_t gpu_pg_mask_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct tegra_gpu_pg_profile *gpu_pg_profile;

	gpu_pg_profile = container_of(attr, struct tegra_gpu_pg_profile, attr);

	return sprintf(buf, "%u\n", gpu_pg_profile->gpu_pg_mask);
}

static ssize_t gpu_pg_mask_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
			     size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct platform_device *pdev = to_platform_device(dev);
	struct tegra_gpu_pg_profile_drv_data *gpu_pg_profile_drv_data;
	struct tegra_gpu_pg_profile *gpu_pg_profile;
	uint32_t gpu_pg_mask;
	int ret = 0;

	gpu_pg_profile_drv_data = platform_get_drvdata(pdev);
	gpu_pg_profile = gpu_pg_profile_drv_data->gpu_pg_profile;

	ret = kstrtou32(buf, 0, &gpu_pg_mask);
	if (ret)
		return ret;

	mutex_lock(&gpu_pg_profile->lock);
	ret = bpmp_set_gpu_pg_mask(gpu_pg_profile_drv_data->gpu_pg_bpmp, gpu_pg_mask);
	if (ret) {
		pr_warn("Failed to send the BPMP MRQ for GPU PG mask\n");
		mutex_unlock(&gpu_pg_profile->lock);
		return ret;
	} else {
		gpu_pg_profile->gpu_pg_mask = gpu_pg_mask;
	}

	mutex_unlock(&gpu_pg_profile->lock);

	return count;
}

static const struct of_device_id of_nv_gpu_static_pg_match[] = {
	{ .compatible = "nvidia,gpu-static-pg", },
	{},
};
MODULE_DEVICE_TABLE(of, of_nv_gpu_static_pg_match);

static int gpu_static_pg_init(struct platform_device *pdev)
{
	struct tegra_gpu_pg_profile_drv_data *gpu_pg_profile_drv_data = platform_get_drvdata(pdev);
	struct tegra_gpu_pg_profile *gpu_pg_profile;
	int ret = 0;

	/* Allocate memory for gpu_pg_profile */
	gpu_pg_profile = devm_kzalloc(&pdev->dev, sizeof(*gpu_pg_profile), GFP_KERNEL);
	if (!gpu_pg_profile) {
		dev_err(&pdev->dev, "Failed to allocate gpu_pg_profile!\n");
		return -ENOMEM;
	}

	mutex_init(&gpu_pg_profile->lock);

	/* Send the GPU PG mask to BPMP if module param is set */
	mutex_lock(&gpu_pg_profile->lock);

	if (is_gpu_pg_mask_param_set()) {
		dev_info(&pdev->dev, "Sending GPU PG mask via BPMP MRQ, "
			"GPU PG mask param = %u.\n", gpu_pg_mask_param);
		ret = bpmp_set_gpu_pg_mask(gpu_pg_profile_drv_data->gpu_pg_bpmp, gpu_pg_mask_param);
		if (ret) {
			dev_err(&pdev->dev, "Failed to send the BPMP MRQ for GPU PG mask param.\n");
			mutex_unlock(&gpu_pg_profile->lock);
			ret = -EINVAL;
			goto err_free_gpu_pg_profile;
		} else {
			gpu_pg_profile->gpu_pg_mask = gpu_pg_mask_param;
		}
	}
	mutex_unlock(&gpu_pg_profile->lock);

	gpu_pg_profile_drv_data->gpu_pg_profile = gpu_pg_profile;

	/* Create sysfs interface for gpu pg mask */
	gpu_pg_profile->attr.attr.name = kstrdup_const("gpu_pg_mask", GFP_KERNEL);
	if (!gpu_pg_profile->attr.attr.name) {
		dev_warn(&pdev->dev, "Couldn't allocate memory for gpu_pg_mask\n");
		ret = -ENOMEM;
		goto err_free_gpu_pg_profile;
	}

	sysfs_attr_init(&gpu_pg_profile->attr.attr);
	gpu_pg_profile->attr.attr.mode = 0664;
	gpu_pg_profile->attr.show = gpu_pg_mask_show;
	gpu_pg_profile->attr.store = gpu_pg_mask_store;
	if (sysfs_create_file(gpu_pg_profile_drv_data->gpu_static_pg_kobject,
		&(gpu_pg_profile->attr.attr))) {
		dev_warn(&pdev->dev, "Couldn't create gpu_pg_mask sysfs\n");

		kfree_const(gpu_pg_profile->attr.attr.name);
		ret = -EINVAL;
		goto err_free_gpu_pg_profile;
	}

	return 0;

err_free_gpu_pg_profile:
	mutex_destroy(&gpu_pg_profile->lock);
	devm_kfree(&pdev->dev, gpu_pg_profile);
	gpu_pg_profile = NULL;

	return ret;
}

static void gpu_static_pg_deinit(struct platform_device *pdev)
{
	struct tegra_gpu_pg_profile_drv_data *gpu_pg_profile_drv_data;
	struct tegra_gpu_pg_profile *gpu_pg_profile;

	gpu_pg_profile_drv_data = platform_get_drvdata(pdev);
	gpu_pg_profile = gpu_pg_profile_drv_data->gpu_pg_profile;

	if (!gpu_pg_profile)
		return;

	sysfs_remove_file(gpu_pg_profile_drv_data->gpu_static_pg_kobject, &(gpu_pg_profile->attr.attr));

	/* release gpu_pg_profile if it is allocated */
	if (gpu_pg_profile->attr.attr.name)
		kfree_const(gpu_pg_profile->attr.attr.name);

	mutex_destroy(&gpu_pg_profile->lock);

	if (gpu_pg_profile)
		devm_kfree(&pdev->dev, gpu_pg_profile);

	gpu_pg_profile = NULL;
}

static int gpu_static_pg_probe(struct platform_device *pdev)
{
	struct tegra_gpu_pg_profile_drv_data *gpu_pg_profile_drv_data;
	struct kobject *gpu_static_pg_kobject;
	struct tegra_bpmp *bpmp;
	int ret = 0;

	gpu_pg_profile_drv_data = \
		devm_kzalloc(&pdev->dev, sizeof(struct tegra_gpu_pg_profile_drv_data), GFP_KERNEL);

	if (!gpu_pg_profile_drv_data) {
		dev_err(&pdev->dev, "Failed to allocate gpu_pg_profile_drv_data!\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, gpu_pg_profile_drv_data);

	/* Get the corresponding BPMP instance */
	bpmp = tegra_bpmp_get(&pdev->dev);
	if (IS_ERR(bpmp)) {
		dev_err(&pdev->dev, "Failed to get BPMP instance!\n");
		devm_kfree(&pdev->dev, gpu_pg_profile_drv_data);
		gpu_pg_profile_drv_data = NULL;
		return PTR_ERR(bpmp);
	}
	gpu_pg_profile_drv_data->gpu_pg_bpmp = bpmp;

	gpu_static_pg_kobject = kobject_create_and_add("gpu_static_pg", kernel_kobj);
	if (!gpu_static_pg_kobject) {
		dev_err(&pdev->dev, "Failed to create gpu_static_pg sysfs!\n");
		ret = -ENOMEM;
		goto put_bpmp;
	}
	gpu_pg_profile_drv_data->gpu_static_pg_kobject = gpu_static_pg_kobject;

	/* Initialize tegra_gpu_pg_profile and corresponding sysfs */
	ret = gpu_static_pg_init(pdev);
	if (ret) {
		dev_warn(&pdev->dev, "Failed to initialize GPU PG mask!\n");
		goto put_kobject;
	}


	return ret;

put_kobject:
	kobject_put(gpu_static_pg_kobject);
	gpu_static_pg_kobject = NULL;

put_bpmp:
	tegra_bpmp_put(bpmp);
	devm_kfree(&pdev->dev, gpu_pg_profile_drv_data);
	gpu_pg_profile_drv_data = NULL;

	return ret;
}

static int gpu_static_pg_remove(struct platform_device *pdev)
{
	struct tegra_gpu_pg_profile_drv_data *gpu_pg_profile_drv_data = platform_get_drvdata(pdev);

	gpu_static_pg_deinit(pdev);

	kobject_put(gpu_pg_profile_drv_data->gpu_static_pg_kobject);
	tegra_bpmp_put(gpu_pg_profile_drv_data->gpu_pg_bpmp);
	devm_kfree(&pdev->dev, gpu_pg_profile_drv_data);
	gpu_pg_profile_drv_data = NULL;

	return 0;
}

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void gpu_static_pg_remove_wrapper(struct platform_device *pdev)
{
	gpu_static_pg_remove(pdev);
}
#else
static int gpu_static_pg_remove_wrapper(struct platform_device *pdev)
{
	return gpu_static_pg_remove(pdev);
}
#endif

static struct platform_driver nv_gpu_static_pg_driver = {
	.probe = gpu_static_pg_probe,
	.remove = gpu_static_pg_remove_wrapper,
	.driver = {
		.name = "nv-gpu-static-pg",
		.of_match_table = of_nv_gpu_static_pg_match,
	},
};

module_platform_driver(nv_gpu_static_pg_driver);

MODULE_AUTHOR("Shao-Chun Kao <shaochunk@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA GPU static power-gating driver which receives the GPU PG mask from the user");
MODULE_LICENSE("GPL v2");
