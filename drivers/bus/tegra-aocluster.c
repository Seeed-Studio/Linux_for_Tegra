// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * Tegra AOCLUSTER Bus Driver
 */

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
	{ }
};
MODULE_DEVICE_TABLE(of, tegra_aocluster_of_match);

static struct platform_driver tegra_aocluster_driver = {
	.probe = tegra_aocluster_probe,
	.remove = tegra_aocluster_remove,
	.driver = {
		.name = "tegra-aocluster",
		.of_match_table = tegra_aocluster_of_match,
	},
};
module_platform_driver(tegra_aocluster_driver);

MODULE_DESCRIPTION("NVIDIA Tegra AOCLUSTER Bus Driver");
MODULE_AUTHOR("Viswanath L <viswanathl@nvidia.com>");
MODULE_LICENSE("GPL v2");
