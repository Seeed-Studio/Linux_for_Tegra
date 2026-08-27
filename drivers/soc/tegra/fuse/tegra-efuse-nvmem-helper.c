// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/device.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <soc/tegra/fuse.h>

#include "fuse.h"

struct tegra_efuse_nvmem_helper {
	struct device		*dev;
	struct nvmem_device	*ndev;
} efuse_helper;

int tegra_efuse_nvmem_readl(unsigned long offset, u32 *value)
{
	int num_bytes_read;

	if (IS_ERR_OR_NULL(efuse_helper.ndev))
		return -EPROBE_DEFER;

	num_bytes_read = nvmem_device_read(efuse_helper.ndev, offset, 4, value);
	if (num_bytes_read < 0)
		return num_bytes_read;

	return 0;
}

static int tegra_efuse_nvmem_helper_probe(struct platform_device *pdev)
{
	efuse_helper.dev = &pdev->dev;

	efuse_helper.ndev = devm_nvmem_device_get(&pdev->dev, "efuse");
	if (IS_ERR(efuse_helper.ndev))
		return dev_err_probe(&pdev->dev, PTR_ERR(efuse_helper.ndev),
				     "failed to get alias id\n");

	return 0;
}

static int tegra_efuse_nvmem_helper_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id tegra_efuse_nvmem_helper_match[] = {
	{ .compatible = "nvidia,efuse-nvmem-helper", },
	{ /* Sentinel. */ }
};
MODULE_DEVICE_TABLE(of, tegra_efuse_nvmem_helper_match);

static struct platform_driver tegra_efuse_nvmem_helper_driver = {
	.driver = {
		.name = "tegra-efuse-nvmem-helper",
		.of_match_table = tegra_efuse_nvmem_helper_match,
	},
	.probe = tegra_efuse_nvmem_helper_probe,
	.remove = tegra_efuse_nvmem_helper_remove,
};
module_platform_driver(tegra_efuse_nvmem_helper_driver);
