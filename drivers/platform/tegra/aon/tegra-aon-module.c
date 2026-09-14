// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2020-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <nvidia/conftest.h>

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/sizes.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>

#include <aon.h>

#define IPCBUF_SIZE		2097152

static int tegra_aon_init_dev_data(struct platform_device *pdev)
{
	struct tegra_aon *aon;
	struct device *dev = &pdev->dev;
	struct device_node *dn = dev->of_node;
	int ret = 0;

	aon = devm_kzalloc(dev, sizeof(*aon), GFP_KERNEL);
	if (!aon) {
		ret = -ENOMEM;
		goto exit;
	}
	platform_set_drvdata(pdev, aon);

	if (dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32))) {
		dev_err(&pdev->dev, "setting DMA MASK failed!\n");
	}
	aon->dev = dev;

	aon->regs = of_iomap(dn, 0);
	if (!aon->regs) {
		dev_err(&pdev->dev, "Cannot map AON register space\n");
		ret = -ENOMEM;
		goto exit;
	}

exit:
	return ret;
}

static int  tegra_aon_setup_ipc_carveout(struct tegra_aon *aon)
{
	struct device_node *dn = aon->dev->of_node;
	int ret = 0;

	aon->ipcbuf = dmam_alloc_coherent(aon->dev,
					  IPCBUF_SIZE,
					  &aon->ipcbuf_dma,
					  GFP_KERNEL | __GFP_ZERO);
	if (!aon->ipcbuf) {
		dev_err(aon->dev, "failed to allocate IPC memory\n");
		ret = -ENOMEM;
		goto exit;

	}
	aon->ipcbuf_size = IPCBUF_SIZE;

	ret = of_property_read_u32(dn, NV("ivc-rx-ss"), &aon->ivc_rx_ss);
	if (ret) {
		dev_err(aon->dev, "missing <%s> property\n", NV("ivc-rx-ss"));
		goto exit;
	}

	ret = of_property_read_u32(dn, NV("ivc-tx-ss"), &aon->ivc_tx_ss);
	if (ret) {
		dev_err(aon->dev, "missing <%s> property\n", NV("ivc-tx-ss"));
		goto exit;
	}

	ret = of_property_read_u32(dn, NV("ivc-carveout-base-ss"),
				   &aon->ivc_carveout_base_ss);
	if (ret) {
		dev_err(aon->dev, "missing <%s> property\n",
			NV("ivc-carveout-base-ss"));
		goto exit;
	}

	ret = of_property_read_u32(dn, NV("ivc-carveout-size-ss"),
				   &aon->ivc_carveout_size_ss);
	if (ret) {
		dev_err(aon->dev, "missing <%s> property\n",
			NV("ivc-carveout-size-ss"));
		goto exit;
	}

exit:
	return ret;
}

static int tegra_aon_probe(struct platform_device *pdev)
{
	struct tegra_aon *aon = NULL;
	struct device *dev = &pdev->dev;
	int ret = 0;

	ret = tegra_aon_init_dev_data(pdev);
	if (ret) {
		dev_err(dev, "failed to init device data err = %d\n", ret);
		goto exit;
	}

	aon = platform_get_drvdata(pdev);
	ret = tegra_aon_setup_ipc_carveout(aon);
	if (ret) {
		dev_err(dev, "failed to setup ipc carveout err = %d\n", ret);
		goto exit;
	}

	ret = tegra_aon_mail_init(aon);
	if (ret) {
		dev_err(dev, "failed to init mail err = %d\n", ret);
		goto exit;
	}

	ret = tegra_aon_debugfs_create(aon);
	if (ret) {
		dev_err(dev, "failed to create debugfs err = %d\n", ret);
		goto exit;
	}

	ret = tegra_aon_ipc_init(aon);
	if (ret) {
		dev_err(dev, "failed to init ipc err = %d\n", ret);
		goto exit;
	}

	dev_info(aon->dev, "init done\n");

exit:
	return ret;
}

static int tegra_aon_remove(struct platform_device *pdev)
{
	struct tegra_aon *aon = platform_get_drvdata(pdev);

	if (!IS_ERR_OR_NULL(aon)) {
		tegra_aon_debugfs_remove(aon);
		tegra_aon_mail_deinit(aon);
	}

	return 0;
}

static const struct of_device_id tegra_aon_of_match[] = {
	{
		.compatible = NV("tegra234-aon"),
	},
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_aon_of_match);

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void tegra_aon_remove_wrapper(struct platform_device *pdev)
{
	tegra_aon_remove(pdev);
}
#else
static int tegra_aon_remove_wrapper(struct platform_device *pdev)
{
	return tegra_aon_remove(pdev);
}
#endif

static struct platform_driver tegra234_aon_driver = {
	.driver = {
		.name	= "tegra234-aon",
		.of_match_table = tegra_aon_of_match,
	},
	.probe = tegra_aon_probe,
	.remove = tegra_aon_remove_wrapper,
};
module_platform_driver(tegra234_aon_driver);

MODULE_DESCRIPTION("Tegra SPE driver");
MODULE_AUTHOR("akhumbum@nvidia.com");
MODULE_LICENSE("GPL v2");
