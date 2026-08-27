// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#define pr_fmt(fmt) "mc-hwpm: " fmt

#include <nvidia/conftest.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/export.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

#include <uapi/linux/tegra-soc-hwpm-uapi.h>

#include <soc/tegra/fuse.h>

/* Broadcast Channel + 16 MC Channels */
#define MAX_MC_CHANNELS 17
#define FUSE_EMC_DISABLE_OFFSET	0x8c0

static struct tegra_soc_hwpm_ip_ops hwpm_ip_ops;

struct tegra_mc_hwpm {
	struct device *dev;
	void __iomem **ch_regs;
	u32 no_ch;
	u64 base_addr;
};

/**
 * ch_no == 0 = Broadcast Channel
 * ch_no == 1 = MC0
 * ch_no == 2 = MC1
 * .
 * .
 * ch_no == 16 = MC15
 */
static u32 mc_readl(struct tegra_mc_hwpm *mc, u32 ch_no, u32 reg)
{
	return readl(mc->ch_regs[ch_no] + reg);
}

static void mc_writel(struct tegra_mc_hwpm *mc, u32 ch_no, u32 val, u32 reg)
{
	writel(val, mc->ch_regs[ch_no] + reg);
}

static bool is_channel_enabled(u32 ch)
{
	u32 fuse_val = 0U;
	int err = 0;

	err = tegra_fuse_readl(FUSE_EMC_DISABLE_OFFSET, &fuse_val);
	if (err != 0) {
		pr_err("Failed to read EMC FUSE\n");
		return false;
	}

	if ((fuse_val & BIT(ch)) == 0)
		return true;

	return false;
}

static int tegra_mc_hwpm_reg_op(void *ip_dev,
	enum tegra_soc_hwpm_ip_reg_op reg_op,
	u32 inst_element_index, u64 reg_offset, u32 *reg_data)
{
	struct device *dev = (struct device *)ip_dev;
	struct tegra_mc_hwpm *mc;

	mc = dev_get_drvdata(dev);
	if (!mc) {
		pr_err("tegra-mc-hwpm: Invalid device\n");
		return -ENODEV;
	}

	if (inst_element_index >= mc->no_ch) {
		dev_err(mc->dev, "Incorrect channel number: %u\n", inst_element_index);
		return -EINVAL;
	}

	if (inst_element_index > 0) {
		if (!is_channel_enabled(inst_element_index - 1)) {
			dev_err(mc->dev, "MC Channel %u is not enabled\n", inst_element_index);
			return -ENODEV;
		}
	}
	if (reg_op == TEGRA_SOC_HWPM_IP_REG_OP_READ) {
		*reg_data = mc_readl(mc, inst_element_index, (u32)reg_offset);
	} else if (reg_op == TEGRA_SOC_HWPM_IP_REG_OP_WRITE) {
		mc_writel(mc, inst_element_index, *reg_data, (u32)reg_offset);
	} else {
		dev_err(mc->dev, "Invalid operation\n");
		return -EINVAL;
	}

	return 0;
}

static const struct of_device_id mc_hwpm_of_ids[] = {
	{ .compatible = "nvidia,tegra-t23x-mc-hwpm" },
	{ }
};
MODULE_DEVICE_TABLE(of, mc_hwpm_of_ids);

static int tegra_mc_hwpm_hwpm_probe(struct platform_device *pdev)
{
	struct tegra_mc_hwpm *mc;
	struct resource *res;
	u32 i;

	mc = devm_kzalloc(&pdev->dev, sizeof(*mc), GFP_KERNEL);
	if (!mc)
		return -ENOMEM;

	platform_set_drvdata(pdev, mc);
	mc->dev = &pdev->dev;

	mc->no_ch = MAX_MC_CHANNELS;
	mc->ch_regs = devm_kcalloc(mc->dev, mc->no_ch, sizeof(*mc->ch_regs),
				   GFP_KERNEL);
	if (!mc->ch_regs)
		return -ENOMEM;

	for (i = 0; i < mc->no_ch; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res) {
			dev_err(mc->dev, "Missing MC channels in device tree\n");
			return -ENODEV;
		}

		mc->ch_regs[i] = devm_ioremap(mc->dev, res->start, resource_size(res));
		if (IS_ERR(mc->ch_regs[i])) {
			dev_err(mc->dev, "Failed to ioremap MC channels aperture\n");
			return PTR_ERR(mc->ch_regs[i]);
		}

		if (i == 0)
			mc->base_addr = res->start;
	}

	hwpm_ip_ops.ip_dev = (void *)mc->dev;
	hwpm_ip_ops.resource_enum = TEGRA_SOC_HWPM_RESOURCE_MSS_CHANNEL;
	hwpm_ip_ops.ip_base_address = mc->base_addr;
	hwpm_ip_ops.hwpm_ip_reg_op = &tegra_mc_hwpm_reg_op;
	tegra_soc_hwpm_ip_register(&hwpm_ip_ops);

	return 0;
}

static int tegra_mc_hwpm_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tegra_mc_hwpm *mc;

	mc = dev_get_drvdata(dev);
	if (!mc) {
		pr_err("tegra-mc-hwpm: Invalid device\n");
		return -ENODEV;
	}

	hwpm_ip_ops.ip_dev = (void *)mc->dev;
	hwpm_ip_ops.resource_enum = TEGRA_SOC_HWPM_RESOURCE_MSS_CHANNEL;
	hwpm_ip_ops.ip_base_address = mc->base_addr;
	hwpm_ip_ops.hwpm_ip_reg_op = NULL;
	tegra_soc_hwpm_ip_unregister(&hwpm_ip_ops);

	return 0;
}

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void tegra_mc_hwpm_remove_wrapper(struct platform_device *pdev)
{
	tegra_mc_hwpm_remove(pdev);
}
#else
static int tegra_mc_hwpm_remove_wrapper(struct platform_device *pdev)
{
	return tegra_mc_hwpm_remove(pdev);
}
#endif

static struct platform_driver mc_hwpm_driver = {
	.driver = {
		.name	= "tegra-mc-hwpm",
		.of_match_table = mc_hwpm_of_ids,
		.owner	= THIS_MODULE,
	},

	.probe		= tegra_mc_hwpm_hwpm_probe,
	.remove		= tegra_mc_hwpm_remove_wrapper,
};

static int __init tegra_mc_hwpm_init(void)
{
	return platform_driver_register(&mc_hwpm_driver);
}
module_init(tegra_mc_hwpm_init);

static void __exit tegra_mc_hwpm_exit(void)
{
	platform_driver_unregister(&mc_hwpm_driver);
}
module_exit(tegra_mc_hwpm_exit);

MODULE_AUTHOR("Puneet Saxena <puneets@nvidia.com>");
MODULE_AUTHOR("Ashish Mhetre <amhetre@nvidia.com>");
MODULE_DESCRIPTION("MC Hardware Performace Monitor Interface driver");
MODULE_LICENSE("GPL v2");
