// SPDX-License-Identifier: GPL-2.0-only
/**
 * Copyright (c) 2015-2024, NVIDIA CORPORATION. All rights reserved.
 */

#include <linux/version.h>
#include <soc/tegra/fuse.h>
#include <soc/tegra/virt/hv-ivc.h>
#include <linux/platform_device.h>
#include <linux/tegra_nvadsp.h>
#include <linux/tegra-hsp.h>

#include "dev.h"
#include "hwmailbox.h"
#include "os.h"
#include "dev-t18x.h"

#if IS_ENABLED(CONFIG_TEGRA_HSP)
static void nvadsp_dbell_handler(void *data)
{
	struct platform_device *pdev = data;
	struct device *dev = &pdev->dev;

	dev_info(dev, "APE DBELL handler\n");
}
#endif


/* Function to return the ADMA page number (0 indexed) used by guest */
static int tegra_adma_query_dma_page(void)
{
	struct device_node *np = NULL;
	int adma_page = 0, ret = 0, i = 0;

	static const char *compatible[] = {
		"nvidia,tegra210-adma",
		"nvidia,tegra210-adma-hv",
		"nvidia,tegra186-adma",
		"nvidia,tegra194-adma-hv",
	};

	for (i = 0; i < ARRAY_SIZE(compatible); i++) {
		np = of_find_compatible_node(NULL, NULL, compatible[i]);
		if (np == NULL)
			continue;

		/*
		 * In DT, "adma-page" property is 1 indexed
		 * If property is present, update return value to be 0 indexed
		 * If property is absent, return default value as page 0
		 */
		ret = of_property_read_u32(np, "adma-page", &adma_page);
		if (ret == 0)
			adma_page = adma_page - 1;

		break;
	}

	pr_info("%s: adma-page %d\n", __func__, adma_page);
	return adma_page;
}

int nvadsp_os_t18x_init(struct platform_device *pdev)
{
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(pdev);
	int ret = 0, adma_ch_page, val = 0;

	if (is_tegra_hypervisor_mode()) {

		adma_ch_page = tegra_adma_query_dma_page();

		/* Set ADSP to do decompression again  */
		val = ADSP_CONFIG_DECOMPRESS_EN << ADSP_CONFIG_DECOMPRESS_SHIFT;

		/* Set ADSP to know its virtualized configuration  */
		val = val | (ADSP_CONFIG_VIRT_EN << ADSP_CONFIG_VIRT_SHIFT);

		/* Encode DMA Page Bits with DMA page information  */
		val = val | (adma_ch_page << ADSP_CONFIG_DMA_PAGE_SHIFT);

		/* Write to HWMBOX5 */
		hwmbox_writel(drv_data, val,
			drv_data->chip_data->adsp_os_config_hwmbox);

		/* Clear HWMBOX0 for ADSP Guest reset handling */
		hwmbox_writel(drv_data, 0,
			drv_data->chip_data->hwmb.hwmbox0_reg);

		return 0;
	}

#if IS_ENABLED(CONFIG_TEGRA_HSP)
	ret = tegra_hsp_db_add_handler(HSP_MASTER_APE,
				       nvadsp_dbell_handler, pdev);
	if (ret)
		dev_err(&pdev->dev,
			"failed to add HSP_MASTER_APE DB handler\n");
#endif

	return ret;
}
