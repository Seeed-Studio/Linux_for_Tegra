// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2014-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/pm.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/io.h>
#include <linux/tegra_nvadsp.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <soc/tegra/fuse-helper.h>
#include <soc/tegra/virt/hv-ivc.h>
#include <linux/pm_runtime.h>
#include <linux/clk/tegra.h>
#include <linux/delay.h>
#include <asm/arch_timer.h>

#include "dev.h"
#include "hwmailbox.h"
#include "os.h"
#include "aram_manager.h"

#define MAX_DEV_STR_LEN    (20)

struct nvadsp_handle_node {
	char dev_str[MAX_DEV_STR_LEN];
	struct nvadsp_handle *nvadsp_handle;
	struct list_head list;
};

/* Global list of driver instance handles */
static LIST_HEAD(nvadsp_handle_list);
static DEFINE_MUTEX(nvadsp_handle_mutex);

/**
 * nvadsp_get_handle: Fetch driver instance using unique identfier
 *
 * @dev_str : Unique identifier string for the driver instance
 *             (trailing unique identifer in compatible DT property,
 *             e.g. "adsp", "adsp1", etc.)
 *
 * Return : Driver instance handle on success, else NULL
 */
struct nvadsp_handle *nvadsp_get_handle(const char *dev_str)
{
	struct nvadsp_handle_node *handle_node;
	struct nvadsp_handle *nvadsp_handle = NULL;
	uint32_t num_devs = 0;

	mutex_lock(&nvadsp_handle_mutex);

	if (list_empty(&nvadsp_handle_list))
		goto out;

	list_for_each_entry(handle_node, &nvadsp_handle_list, list) {
		num_devs++;

		if (!strcmp(dev_str, handle_node->dev_str)) {
			nvadsp_handle = handle_node->nvadsp_handle;
			goto out;
		}
	}

	if ((num_devs == 1) && !strcmp(dev_str, "")) {
		handle_node = list_first_entry(
					&nvadsp_handle_list,
					struct nvadsp_handle_node, list);
		nvadsp_handle = handle_node->nvadsp_handle;
	}

out:
	mutex_unlock(&nvadsp_handle_mutex);

	if (!nvadsp_handle)
		pr_err("Unable to find device %s in list\n", dev_str);

	return nvadsp_handle;
}
EXPORT_SYMBOL(nvadsp_get_handle);

#ifdef CONFIG_DEBUG_FS
static int adsp_debug_init(struct nvadsp_drv_data *drv_data,
				const char *dev_str)
{
	drv_data->adsp_debugfs_root = debugfs_create_dir(dev_str, NULL);
	if (!drv_data->adsp_debugfs_root)
		return -ENOMEM;
	return 0;
}
#endif /* CONFIG_DEBUG_FS */


#ifdef CONFIG_PM
static int nvadsp_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(pdev);
	int ret = -EINVAL;

	if (drv_data->runtime_resume)
		ret = drv_data->runtime_resume(dev);

	return ret;
}

static int nvadsp_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(pdev);
	int ret = -EINVAL;

	if (drv_data->runtime_suspend)
		ret = drv_data->runtime_suspend(dev);

	return ret;
}

static int nvadsp_runtime_idle(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(pdev);
	int ret = 0;

	if (drv_data->runtime_idle)
		ret = drv_data->runtime_idle(dev);

	return ret;
}
#endif /* CONFIG_PM */

#ifdef CONFIG_PM_SLEEP
static int nvadsp_suspend(struct device *dev)
{
	if (pm_runtime_status_suspended(dev))
		return 0;

	return nvadsp_runtime_suspend(dev);
}

static int nvadsp_resume(struct device *dev)
{
	if (pm_runtime_status_suspended(dev))
		return 0;

	return nvadsp_runtime_resume(dev);
}
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops nvadsp_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(nvadsp_suspend, nvadsp_resume)
	SET_RUNTIME_PM_OPS(nvadsp_runtime_suspend, nvadsp_runtime_resume,
			   nvadsp_runtime_idle)
};

uint64_t nvadsp_get_timestamp_counter(void)
{
	return __arch_counter_get_cntvct_stable();
}

int nvadsp_set_bw(struct nvadsp_drv_data *drv_data, u32 efreq)
{
	int ret = -EINVAL;

	if (drv_data->bwmgr)
		ret = tegra_bwmgr_set_emc(drv_data->bwmgr, efreq * 1000,
					  TEGRA_BWMGR_SET_EMC_FLOOR);
	else if (drv_data->icc_path_handle)
		ret = icc_set_bw(drv_data->icc_path_handle, 0,
				(unsigned long)FREQ2ICC(efreq * 1000));
	if (ret)
		dev_err(&drv_data->pdev->dev,
			"failed to set emc freq rate:%d\n", ret);

	return ret;
}

static void nvadsp_bw_register(struct nvadsp_drv_data *drv_data)
{
	struct device *dev = &drv_data->pdev->dev;

	switch (__tegra_get_chip_id()) {
	case TEGRA194:
		drv_data->bwmgr = tegra_bwmgr_register(
				TEGRA_BWMGR_CLIENT_APE_ADSP);
		if (IS_ERR(drv_data->bwmgr)) {
			dev_err(dev, "unable to register bwmgr\n");
			drv_data->bwmgr = NULL;
		}
		break;
	case TEGRA234:
		if (!is_tegra_hypervisor_mode()) {
			/* Interconnect Support */
#ifdef CONFIG_ARCH_TEGRA_23x_SOC
			drv_data->icc_path_handle = icc_get(dev, TEGRA_ICC_APE,
							TEGRA_ICC_PRIMARY);
#endif
			if (IS_ERR(drv_data->icc_path_handle)) {
				dev_err(dev,
				"%s: Failed to register Interconnect err=%ld\n",
				__func__, PTR_ERR(drv_data->icc_path_handle));
				drv_data->icc_path_handle = NULL;
			}
		}
		break;
	default:
		/* None */
		break;
	}
}

static void nvadsp_bw_unregister(struct nvadsp_drv_data *drv_data)
{
	nvadsp_set_bw(drv_data, 0);

	if (drv_data->bwmgr) {
		tegra_bwmgr_unregister(drv_data->bwmgr);
		drv_data->bwmgr = NULL;
	}

	if (drv_data->icc_path_handle) {
		icc_put(drv_data->icc_path_handle);
		drv_data->icc_path_handle = NULL;
	}
}

static int nvadsp_parse_co_mem(struct platform_device *pdev)
{
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	struct device_node *node;
	int err = 0;

	node = of_parse_phandle(dev->of_node, "nvidia,adsp_co", 0);
	if (!node)
		return 0;

	if (!of_device_is_available(node))
		goto exit;

	err = of_address_to_resource(node, 0, &drv_data->co_mem);
	if (err) {
		dev_err(dev, "cannot get adsp CO memory (%d)\n", err);
		goto exit;
	}

	drv_data->adsp_mem[ADSP_OS_SIZE] = resource_size(&drv_data->co_mem);

exit:
	of_node_put(node);

	return err;
}

static void nvadsp_parse_clk_entries(struct platform_device *pdev)
{
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	u32 val32 = 0;

	/* Optional properties, should come from platform dt files */
	if (of_property_read_u32(dev->of_node, "nvidia,adsp_freq", &val32))
		dev_dbg(dev, "adsp_freq dt not found\n");
	else {
		drv_data->adsp_freq = val32;
		drv_data->adsp_freq_hz = val32 * 1000;
	}

	if (of_property_read_u32(dev->of_node, "nvidia,ape_freq", &val32))
		dev_dbg(dev, "ape_freq dt not found\n");
	else
		drv_data->ape_freq = val32;

	if (of_property_read_u32(dev->of_node, "nvidia,ape_emc_freq", &val32))
		dev_dbg(dev, "ape_emc_freq dt not found\n");
	else
		drv_data->ape_emc_freq = val32;

	drv_data->adsp_clk = devm_clk_get(dev, "cpu_clock");
	if (IS_ERR_OR_NULL(drv_data->adsp_clk))
		drv_data->adsp_clk = NULL;
}

static int nvadsp_parse_dt(struct platform_device *pdev)
{
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	const char *adsp_elf;
	u32 *adsp_mem;
	int iter;

	adsp_mem = drv_data->adsp_mem;

	for (iter = 0; iter < ADSP_MEM_END; iter++) {
		if (of_property_read_u32_index(dev->of_node, "nvidia,adsp_mem",
			iter, &adsp_mem[iter])) {
			dev_err(dev, "adsp memory dt %d not found\n", iter);
			return -EINVAL;
		}
	}

	for (iter = 0; iter < ADSP_EVP_END; iter++) {
		if (of_property_read_u32_index(dev->of_node,
			"nvidia,adsp-evp-base",
			iter, &drv_data->evp_base[iter])) {
			break;
		}
	}

	for (iter = 0; iter < MAX_CLUSTER_MEM; iter++) {
		if (of_property_read_u64_index(dev->of_node,
			"nvidia,cluster_mem", (iter * MAX_CLUSTER_MEM),
			&drv_data->cluster_mem[iter].ccplex_addr))
			break;

		if (of_property_read_u64_index(dev->of_node,
			"nvidia,cluster_mem", (iter * MAX_CLUSTER_MEM) + 1,
			&drv_data->cluster_mem[iter].dsp_addr)) {
			dev_err(dev, "no DSP cluster mem found\n");
			return -ENODEV;
		}

		if (of_property_read_u64_index(dev->of_node,
			"nvidia,cluster_mem", (iter * MAX_CLUSTER_MEM) + 2,
			&drv_data->cluster_mem[iter].size)) {
			dev_err(dev, "DSP cluster mem size not specified\n");
			return -ENODEV;
		}
	}

	for (iter = 0; iter < MAX_DRAM_MAP; iter++) {
		if (of_property_read_u64_index(dev->of_node,
			"nvidia,dram_map", (iter * MAX_DRAM_MAP),
			&drv_data->dram_map[iter].addr))
			break;

		if (of_property_read_u64_index(dev->of_node,
			"nvidia,dram_map", (iter * MAX_DRAM_MAP) + 1,
			&drv_data->dram_map[iter].size)) {
			dev_err(dev,
			"Failed to get DRAM map with ID %d\n", iter);
			return -EINVAL;
		}
	}

	if (!of_property_read_string(dev->of_node,
				"nvidia,adsp_elf", &adsp_elf)) {
		if (strlen(adsp_elf) < MAX_FW_STR)
			strcpy(drv_data->adsp_elf, adsp_elf);
		else {
			dev_err(dev, "invalid string in nvidia,adsp_elf\n");
			return -EINVAL;
		}
	} else if (drv_data->chip_data->adsp_elf) {
		if (strlen(drv_data->chip_data->adsp_elf) < MAX_FW_STR)
			strcpy(drv_data->adsp_elf,
				drv_data->chip_data->adsp_elf);
		else {
			dev_err(dev, "invalid adsp_elf string in chip data");
			return -EINVAL;
		}
	} else
		strcpy(drv_data->adsp_elf, NVADSP_ELF);

	drv_data->adsp_unit_fpga = of_property_read_bool(dev->of_node,
				"nvidia,adsp_unit_fpga");

	drv_data->adsp_os_secload = of_property_read_bool(dev->of_node,
				"nvidia,adsp_os_secload");

	if (of_property_read_u32(dev->of_node, "nvidia,tegra_platform",
				&drv_data->tegra_platform))
		dev_dbg(dev, "tegra_platform dt not found\n");

	if (of_property_read_u32(dev->of_node, "nvidia,adsp_load_timeout",
				&drv_data->adsp_load_timeout))
		dev_dbg(dev, "adsp_load_timeout dt not found\n");

	nvadsp_parse_clk_entries(pdev);

	if (nvadsp_parse_co_mem(pdev))
		return -ENOMEM;

	drv_data->state.evp = devm_kzalloc(dev,
			drv_data->evp_base[ADSP_EVP_SIZE], GFP_KERNEL);
	if (!drv_data->state.evp)
		return -ENOMEM;

	return 0;
}

static int nvadsp_probe(struct platform_device *pdev)
{
	struct nvadsp_drv_data *drv_data;
	struct device *dev = &pdev->dev;
	struct resource *res = NULL;
	void __iomem *base = NULL;
	uint32_t aram_addr, aram_size;
	int irq_iter, iter, irq_num;
	const char *compat, *dev_str;
	struct nvadsp_handle_node *handle_node;
	int ret = 0;

	mutex_lock(&nvadsp_handle_mutex);

	ret = of_property_read_string(dev->of_node, "compatible", &compat);
	if (ret)
		goto out;

	/**
	 * Trailing part of compatible string after the
	 * last '-' is taken as unique device string
	 */
	dev_str = strrchr(compat, '-');
	if (strlen(dev_str) < 2) {
		dev_err(dev, "Unable to extract device string\n");
		ret = -EINVAL;
		goto out;
	}
	dev_str += 1;

	if (strlen(dev_str) >= MAX_DEV_STR_LEN) {
		dev_err(dev, "Device string %s out of bound\n", dev_str);
		ret = -EINVAL;
		goto out;
	}

	list_for_each_entry(handle_node, &nvadsp_handle_list, list) {
		if (!strcmp(dev_str, handle_node->dev_str)) {
			dev_err(dev, "Device %s already probed\n", dev_str);
			ret = -EEXIST;
			goto out;
		}
	}

	dev_info(dev, "in probe()...%s\n", dev_str);

	drv_data = devm_kzalloc(dev, sizeof(*drv_data),
				GFP_KERNEL);
	if (!drv_data) {
		dev_err(&pdev->dev, "Failed to allocate driver data");
		ret = -ENOMEM;
		goto out;
	}

	platform_set_drvdata(pdev, drv_data);
	drv_data->pdev = pdev;
	drv_data->chip_data = of_device_get_match_data(dev);

	ret = nvadsp_parse_dt(pdev);
	if (ret)
		goto out;

#ifdef CONFIG_PM
	ret = nvadsp_pm_init(pdev);
	if (ret) {
		dev_err(dev, "Failed in pm init");
		goto out;
	}
#endif

#ifdef CONFIG_DEBUG_FS
	if (adsp_debug_init(drv_data, dev_str))
		dev_err(dev,
			"unable to create %s debug fs directory\n", dev_str);
#endif

	drv_data->base_regs =
		devm_kzalloc(dev, sizeof(void *) * APE_MAX_REG,
							GFP_KERNEL);
	if (!drv_data->base_regs) {
		dev_err(dev, "Failed to allocate regs");
		ret = -ENOMEM;
		goto out;
	}

	for (iter = 0; iter < APE_MAX_REG; iter++) {
		if ((iter == AMC) && drv_data->chip_data->amc_not_avlbl)
			continue;

		res = platform_get_resource(pdev, IORESOURCE_MEM, iter);
		if (!res) {
			dev_err(dev,
			"Failed to get resource with ID %d\n",
							iter);
			ret = -EINVAL;
			goto out;
		}

		base = devm_ioremap_resource(dev, res);
		if (IS_ERR(base)) {
			dev_err(dev, "Failed to iomap resource reg[%d]\n",
				iter);
			ret = PTR_ERR(base);
			goto out;
		}
		drv_data->base_regs[iter] = base;
		nvadsp_add_load_mappings(drv_data,
					res->start, (void __force *)base,
					resource_size(res));
	}

	drv_data->base_regs_saved = drv_data->base_regs;

	for (irq_iter = 0; irq_iter < drv_data->chip_data->num_irqs; irq_iter++) {
		if ((irq_iter == WFI_VIRQ) && drv_data->chip_data->no_wfi_irq)
			continue;

		irq_num = platform_get_irq(pdev, irq_iter);
		if (irq_num < 0) {
			dev_err(dev, "Failed to get irq number for index %d\n",
				irq_iter);
			ret = -EINVAL;
			goto out;
		}
		drv_data->agic_irqs[irq_iter] = irq_num;
	}

#ifdef CONFIG_PM
	pm_runtime_enable(dev);

	ret = pm_runtime_get_sync(dev);
	if (ret < 0)
		goto out;
#endif
	ret = nvadsp_hwmbox_init(pdev);
	if (ret)
		goto err;

	ret = nvadsp_mbox_init(pdev);
	if (ret)
		goto err;

#ifdef CONFIG_TEGRA_ADSP_ACTMON
	ret = ape_actmon_probe(pdev);
	if (ret)
		goto err;
#endif

	ret = nvadsp_os_probe(pdev);
	if (ret)
		goto err;

	ret = nvadsp_dev_init(pdev);
	if (ret) {
		dev_err(dev, "Failed initialize resets\n");
		goto err;
	}

	ret = nvadsp_app_module_probe(pdev);
	if (ret)
		goto err;

	aram_addr = drv_data->adsp_mem[ARAM_ALIAS_0_ADDR];
	aram_size = drv_data->adsp_mem[ARAM_ALIAS_0_SIZE];
	if (aram_size) {
		ret = nvadsp_aram_init(drv_data, aram_addr, aram_size);
		if (ret)
			dev_err(dev, "Failed to init aram\n");
	}

	nvadsp_bw_register(drv_data);

	if (!drv_data->adsp_os_secload) {
		ret = nvadsp_acast_init(pdev);
		if (ret)
			goto err;
	}

	handle_node = kzalloc(sizeof(struct nvadsp_handle_node),
				GFP_KERNEL);
	if (!handle_node) {
		dev_err(dev, "Failed to allocate node mem for %s\n", dev_str);
		ret = -ENOMEM;
		goto err;
	}

	/* Add probed device into list of handles */
	strcpy(handle_node->dev_str, dev_str);
	handle_node->nvadsp_handle = (struct nvadsp_handle *)drv_data;
	INIT_LIST_HEAD(&handle_node->list);
	list_add_tail(&handle_node->list, &nvadsp_handle_list);

err:
#ifdef CONFIG_PM
	ret = pm_runtime_put_sync(dev);
	if (ret < 0)
		dev_err(dev, "pm_runtime_put_sync failed\n");
#endif
out:
	mutex_unlock(&nvadsp_handle_mutex);

	return ret;
}

static int nvadsp_remove(struct platform_device *pdev)
{
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(pdev);
	uint32_t aram_size =  drv_data->adsp_mem[ARAM_ALIAS_0_SIZE];
	struct nvadsp_handle_node *handle_node;

	nvadsp_bw_unregister(drv_data);

	if (aram_size)
		nvadsp_aram_exit(drv_data);

#ifdef CONFIG_PM
	pm_runtime_disable(&pdev->dev);

	if (!pm_runtime_status_suspended(&pdev->dev))
		nvadsp_runtime_suspend(&pdev->dev);
#endif

	mutex_lock(&nvadsp_handle_mutex);

	list_for_each_entry(handle_node, &nvadsp_handle_list, list) {
		if (handle_node->nvadsp_handle ==
				(struct nvadsp_handle *)drv_data) {
			list_del(&handle_node->list);
			kfree(handle_node);
		}
	}

	mutex_unlock(&nvadsp_handle_mutex);

	return 0;
}

/**
 * List of compatibles and associated chip data
 */
extern struct nvadsp_chipdata tegrat18x_adsp_chipdata;
extern struct nvadsp_chipdata tegra239_adsp_chipdata;
extern struct nvadsp_chipdata tegra264_adsp0_chipdata;
extern struct nvadsp_chipdata tegra264_adsp1_chipdata;
extern struct nvadsp_chipdata tegra264_aon_chipdata;
static const struct of_device_id nvadsp_of_match[] = {
	{
		.compatible = "nvidia,tegra18x-adsp",
		.data = &tegrat18x_adsp_chipdata,
	}, {
		.compatible = "nvidia,tegra239-adsp",
		.data = &tegra239_adsp_chipdata,
	}, {
		.compatible = "nvidia,tegra264-adsp",
		.data = &tegra264_adsp0_chipdata,
	}, {
		.compatible = "nvidia,tegra264-adsp1",
		.data = &tegra264_adsp1_chipdata,
	}, {
		.compatible = "nvidia,tegra264-aon",
		.data = &tegra264_aon_chipdata,
	}, {
	},
};
MODULE_DEVICE_TABLE(of, nvadsp_of_match);

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void nvadsp_remove_wrapper(struct platform_device *pdev)
{
	nvadsp_remove(pdev);
}
#else
static int nvadsp_remove_wrapper(struct platform_device *pdev)
{
	return nvadsp_remove(pdev);
}
#endif

static struct platform_driver nvadsp_driver __refdata = {
	.driver	= {
		.name	= "nvadsp",
		.owner	= THIS_MODULE,
		.pm	= &nvadsp_pm_ops,
		.of_match_table = of_match_ptr(nvadsp_of_match),
	},
	.probe		= nvadsp_probe,
	.remove		= nvadsp_remove_wrapper,
};
module_platform_driver(nvadsp_driver);

MODULE_AUTHOR("NVIDIA");
MODULE_DESCRIPTION("Tegra Host ADSP Driver");
MODULE_VERSION("6.0");
MODULE_LICENSE("GPL v2");
