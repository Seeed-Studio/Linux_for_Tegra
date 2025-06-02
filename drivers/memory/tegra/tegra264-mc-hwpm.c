// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
 */

#define pr_fmt(fmt) "tegra264-mc-hwpm: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/export.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <uapi/linux/tegra-soc-hwpm-uapi.h>

#define MC_MCC_CTL_PERFMUX_OFFSET	0x8914
#define MC_MCC_DP_PERFMUX_OFFSET 	0x8918
#define MC_CBRIDGE_PERFMUX_OFFSET	0x891c
#define MSS_HUB_IB_PERFMUX_OFFSET	0x6f3c
#define MSS_HUB_CIF_PERFMUX_OFFSET	0x6f34
#define MSS_HUB_TU_PERFMUX_0		0x6f38

#define MAX_MC_CHANNELS 17	// Broadcast Channel + 16 MC Channels

static struct tegra_soc_hwpm_ip_ops hwpm_ip_ops;

struct tegra_mc_hwpm {
	struct device *dev;
	void __iomem **ch_regs;
	u32 no_ch;
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

static int tegra_mc_hwpm_reg_op(void *ip_dev,
	enum tegra_soc_hwpm_ip_reg_op reg_op,
	u32 inst_element_index, u64 reg_offset, u32 *reg_data)
{
	struct device *dev = (struct device *)ip_dev;
	struct tegra_mc_hwpm *mc;

	mc = dev_get_drvdata(dev);
	if (!mc) {
		pr_err("Invalid device\n");
		return -ENODEV;
	}

	if (inst_element_index >= mc->no_ch) {
		dev_err(dev, "Incorrect channel number: %u\n", inst_element_index);
		return -EINVAL;
	}

	if (reg_offset != MC_MCC_CTL_PERFMUX_OFFSET && reg_offset != MC_MCC_DP_PERFMUX_OFFSET &&
	    reg_offset != MC_CBRIDGE_PERFMUX_OFFSET && reg_offset != MSS_HUB_IB_PERFMUX_OFFSET &&
	    reg_offset != MSS_HUB_CIF_PERFMUX_OFFSET && reg_offset != MSS_HUB_TU_PERFMUX_0) {
		dev_err(dev, "SOC-HWPM requesting access to prohibited register");
		return -EPERM;
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
	{ .compatible = "nvidia,tegra-t264-mc-hwpm" },
	{ }
};
MODULE_DEVICE_TABLE(of, mc_hwpm_of_ids);

static int tegra_mc_hwpm_hwpm_probe(struct platform_device *pdev)
{
	struct tegra_mc_hwpm *mc;
	struct resource *res;
	u64 base_addr;
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

	for (i = 0; i < mc->no_ch; i++){
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res) {
			dev_err(mc->dev, "Missing MC channels in device tree\n");
			return -ENODEV;
		}

		mc->ch_regs[i] = devm_ioremap(mc->dev, res->start, resource_size(res));
		if (IS_ERR(mc->ch_regs[i]))
			return PTR_ERR(mc->ch_regs[i]);

		if (i == 0)
			base_addr = res->start;
	}

	hwpm_ip_ops.ip_dev = (void *)mc->dev;
	hwpm_ip_ops.resource_enum = TEGRA_SOC_HWPM_RESOURCE_MSS_CHANNEL;
	hwpm_ip_ops.ip_base_address = base_addr;
	hwpm_ip_ops.hwpm_ip_reg_op = &tegra_mc_hwpm_reg_op;
	tegra_soc_hwpm_ip_register(&hwpm_ip_ops);

	return 0;
}

static struct platform_driver mc_hwpm_driver = {
	.driver = {
		.name	= "tegra264-mc-hwpm",
		.of_match_table = of_match_ptr(mc_hwpm_of_ids),
		.owner	= THIS_MODULE,
	},

	.probe		= tegra_mc_hwpm_hwpm_probe,
};

static int __init tegra_mc_hwpm_init(void)
{
	int ret = platform_driver_register(&mc_hwpm_driver);

	if (ret) {
		pr_err("Failed to register MC-HWPM driver\n");
	}

	return ret;
}
module_init(tegra_mc_hwpm_init);

static void __exit tegra_mc_hwpm_exit(void)
{
	platform_driver_unregister(&mc_hwpm_driver);
}
module_exit(tegra_mc_hwpm_exit);

MODULE_AUTHOR("Ashish Mhetre <amhetre@nvidia.com>");
MODULE_DESCRIPTION("Tegra264 MC-HWPM driver");
MODULE_LICENSE("GPL v2");
