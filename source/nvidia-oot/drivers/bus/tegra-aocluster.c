// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * Tegra AOCLUSTER Bus Driver
 */

#include <nvidia/conftest.h>

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

static int tegra_aocluster_probe(struct platform_device *pdev)
{
	if (!pdev->dev.of_node)
		return -EINVAL;

	/* ANY CLOCKS OR RESETS NEEDED FOR AOCLUSTER MAY BE ADDED HERE */

	of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);

	dev_info(&pdev->dev, "Tegra AOCLUSTER bus registered\n");

	return 0;
}

static int tegra_aocluster_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id tegra_aocluster_of_match[] = {
	{ .compatible = "nvidia,tegra264-aocluster", },
	{ }
};
MODULE_DEVICE_TABLE(of, tegra_aocluster_of_match);

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void tegra_aocluster_remove_wrapper(struct platform_device *pdev)
{
	tegra_aocluster_remove(pdev);
}
#else
static int tegra_aocluster_remove_wrapper(struct platform_device *pdev)
{
	return tegra_aocluster_remove(pdev);
}
#endif

static struct platform_driver tegra_aocluster_driver = {
	.probe = tegra_aocluster_probe,
	.remove = tegra_aocluster_remove_wrapper,
	.driver = {
		.name = "tegra-aocluster",
		.of_match_table = tegra_aocluster_of_match,
	},
};
module_platform_driver(tegra_aocluster_driver);

MODULE_DESCRIPTION("NVIDIA Tegra AOCLUSTER Bus Driver");
MODULE_AUTHOR("Viswanath L <viswanathl@nvidia.com>");
MODULE_LICENSE("GPL v2");
