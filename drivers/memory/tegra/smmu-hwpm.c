// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024, NVIDIA CORPORATION.  All rights reserved.

#define pr_fmt(fmt) "smmu-hwpm: " fmt

#include <nvidia/conftest.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/export.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <uapi/linux/tegra-soc-hwpm-uapi.h>

#define SMMU_TCU_PERFMUX_0_OFFSET	0x5000

static struct tegra_soc_hwpm_ip_ops hwpm_ip_ops;

struct smmu_hwpm {
	struct device *dev;
	void __iomem *base;
};

static u32 smmu_readl(struct smmu_hwpm *smmu, u32 reg)
{
	return readl(smmu->base + reg);
}

static void smmu_writel(struct smmu_hwpm *smmu, u32 val, u32 reg)
{
	writel(val, smmu->base + reg);
}

static int smmu_hwpm_reg_op(void *ip_dev,
	enum tegra_soc_hwpm_ip_reg_op reg_op,
	u32 inst_element_index, u64 reg_offset, u32 *reg_data)
{
	struct device *dev = (struct device *)ip_dev;
	struct smmu_hwpm *smmu;

	smmu = dev_get_drvdata(dev);
	if (!smmu) {
		pr_err("Invalid device\n");
		return -ENODEV;
	}

	if ((u32)reg_offset != SMMU_TCU_PERFMUX_0_OFFSET) {
		dev_err(smmu->dev, "SOC-HWPM requesting access to prohibited register");
		return -EPERM;
	}

	if (reg_op == TEGRA_SOC_HWPM_IP_REG_OP_READ) {
		*reg_data = smmu_readl(smmu, (u32)reg_offset);
	} else if (reg_op == TEGRA_SOC_HWPM_IP_REG_OP_WRITE) {
		smmu_writel(smmu, *reg_data, (u32)reg_offset);
	} else {
		dev_err(smmu->dev, "Invalid operation\n");
		return -EINVAL;
	}

	return 0;
}

static const struct of_device_id smmu_hwpm_of_ids[] = {
	{ .compatible = "nvidia,t264-smmu-hwpm" },
	{ }
};
MODULE_DEVICE_TABLE(of, smmu_hwpm_of_ids);

static int smmu_hwpm_probe(struct platform_device *pdev)
{
	struct smmu_hwpm *smmu;
	struct resource *res;
	u64 base_addr;

	smmu = devm_kzalloc(&pdev->dev, sizeof(*smmu), GFP_KERNEL);
	if (!smmu)
		return -ENOMEM;

	platform_set_drvdata(pdev, smmu);
	smmu->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(smmu->dev, "Missing SMMU aperture in DT\n");
		return -ENODEV;
	}

	smmu->base = devm_ioremap(smmu->dev, res->start, resource_size(res));
	if (IS_ERR_OR_NULL(smmu->base))
		return PTR_ERR(smmu->base);

	base_addr = res->start;

	hwpm_ip_ops.ip_dev = (void *)smmu->dev;
	hwpm_ip_ops.resource_enum = TEGRA_SOC_HWPM_RESOURCE_SMMU;
	hwpm_ip_ops.ip_base_address = base_addr;
	hwpm_ip_ops.hwpm_ip_reg_op = &smmu_hwpm_reg_op;
	tegra_soc_hwpm_ip_register(&hwpm_ip_ops);

	return 0;
}

static void smmu_hwpm_remove(struct platform_device *pdev)
{
	tegra_soc_hwpm_ip_unregister(&hwpm_ip_ops);
}

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void smmu_hwpm_remove_wrapper(struct platform_device *pdev)
{
	smmu_hwpm_remove(pdev);
}
#else
static int smmu_hwpm_remove_wrapper(struct platform_device *pdev)
{
	smmu_hwpm_remove(pdev);
	return 0;
}
#endif

static struct platform_driver smmu_hwpm_driver = {
	.driver = {
		.name	= "tegra264-smmu-hwpm",
		.of_match_table = smmu_hwpm_of_ids,
		.owner	= THIS_MODULE,
	},

	.probe		= smmu_hwpm_probe,
	.remove		= smmu_hwpm_remove_wrapper,
};

static int __init smmu_hwpm_init(void)
{
	return platform_driver_register(&smmu_hwpm_driver);
}
module_init(smmu_hwpm_init);

static void __exit smmu_hwpm_exit(void)
{
	platform_driver_unregister(&smmu_hwpm_driver);
}
module_exit(smmu_hwpm_exit);

MODULE_AUTHOR("Ashish Mhetre <amhetre@nvidia.com>");
MODULE_DESCRIPTION("Tegra264 SMMU-HWPM driver");
MODULE_LICENSE("GPL v2");
