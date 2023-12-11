// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2023 NVIDIA CORPORATION.  All rights reserved.

#define pr_fmt(fmt) "mc: " fmt
#include <linux/module.h>
#include <linux/export.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <soc/tegra/mc-t26x.h>

#define MC_SECURITY_CARVEOUT_BASE 0x9404
#define MC_CARVEOUT_NEXT 0xa0
#define MC_SECURITY_CARVEOUT_LITE_BASE 0xa804
#define MC_CARVEOUT_LITE_NEXT 0x60
#define MC_CARVEOUT_BASE_HI 0x4
#define MC_SECURITY_CARVEOUT_SIZE_128KB 0x8

static void __iomem *mcb_base;

static inline u32 mc_readl(unsigned long offset)
{
	return readl_relaxed(mcb_base + offset);
}

int tegra264_mc_get_carveout_info(unsigned int id, phys_addr_t *base, u64 *size)
{
	u32 offset;

	if (id < 1 || id > 42)
		return -EINVAL;
	if (id < 32)
		offset = MC_SECURITY_CARVEOUT_BASE + MC_CARVEOUT_NEXT * (id - 1);
	else
		offset = MC_SECURITY_CARVEOUT_LITE_BASE + MC_CARVEOUT_LITE_NEXT * (id - 32);
	*base = mc_readl(offset + 0x0);
#ifdef CONFIG_PHYS_ADDR_T_64BIT
	*base |= (phys_addr_t)mc_readl(offset + MC_CARVEOUT_BASE_HI) << 32;
#endif
	if (size)
		*size = (u64)mc_readl(offset + MC_SECURITY_CARVEOUT_SIZE_128KB) << 17;
	return 0;
}
EXPORT_SYMBOL(tegra264_mc_get_carveout_info);

const struct of_device_id tegra_mc_of_ids[] = {
	{ .compatible = "nvidia,tegra-t26x-mc" },
	{ }
};
MODULE_DEVICE_TABLE(of, tegra_mc_of_ids);

static int tegra_mc_probe(struct platform_device *pdev)
{
	mcb_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR_OR_NULL(mcb_base))
		return PTR_ERR(mcb_base);
	return 0;
}

static int tegra_mc_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver mc_driver = {
	.driver = {
		.name	= "nv-tegra-t26x-mc",
		.of_match_table = tegra_mc_of_ids,
		.owner	= THIS_MODULE,
	},
	.remove		= tegra_mc_remove,
};

module_platform_driver_probe(mc_driver, tegra_mc_probe);

MODULE_AUTHOR("Ashish Mhetre <amhetre@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra Memory Controller driver");
MODULE_LICENSE("GPL v2");
