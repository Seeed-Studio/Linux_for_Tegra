// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 */

#include <nvidia/conftest.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of.h>
#include "soc_plugin_os_hal.h"
#include "soc-plugin-priv.h"
#include "soc_t26x.h"

#define MAX_IVC_Q_PRIORITY				2U
#define TEGRA_IVC_ID_OFFSET				0U
#define TEGRA_SE_ENGINE_DOMAIN_OFFSET		1U
#define TEGRA_SE_PORT_OFFSET				2U
#define TEGRA_SE_ENGINE_NUMBER_OFFSET		3U
#define TEGRA_VIRTUALIZED_INSTANCE_OFFSET			4U
#define TEGRA_IVC_PRIORITY_OFFSET			5U
#define TEGRA_MAX_BUFFER_SIZE				6U
#define TEGRA_CHANNEL_GROUPID_OFFSET			7U
#define TEGRA_SID_OFFSET				8U
#define TEGRA_IVCCFG_ARRAY_LEN				13U

static struct t26x_plugin_data {
	soc_plugin_t26x_dt_data channels[MAX_NUM_IVC];
	uint32_t num_channels;
} g_t26x_plugin_data;

Soc_Plugin_Hal_Status soc_populate_dt_data_from_node(const void *nvdt_node)
{
	struct device_node *np = (struct device_node *)nvdt_node;
	uint32_t ivc_cnt, cnt;
	uint32_t se_comm_id;
	uint32_t se_domain;
	uint32_t se_domain_instance;
	uint32_t se_port;
	uint32_t virtualized_instance;
	uint32_t max_buffer_size;
	uint32_t sid;
	Soc_Plugin_Hal_Status status = SOC_PLUGIN_HAL_SUCCESS;
	int32_t err = 0;

	if (!np) {
		pr_err("Invalid device node\n");
		status = SOC_PLUGIN_HAL_FAILED;
		goto exit;
	}

	/* read ivccfg from dts */
	err = of_property_read_u32_index(np, "nvidia,ivccfg_cnt", 0, &ivc_cnt);
	if (err) {
		NVVSE_ERR("%s Error: failed to read ivc_cnt. err %u\n", __func__, err);
		status = SOC_PLUGIN_HAL_FAILED;
		goto exit;
	}

	if (ivc_cnt > MAX_NUM_IVC) {
		NVVSE_ERR("%s Error: invalid ivc_cnt. err %u\n", __func__, ivc_cnt);
		status = SOC_PLUGIN_HAL_FAILED;
		goto exit;
	} else {
		/* check if the total number of IVCs exceeds the maximum supported limit
		 * without running into integer overflow while performing the check.
		 */
		if (g_t26x_plugin_data.num_channels > (MAX_NUM_IVC - ivc_cnt)) {
			NVVSE_ERR("%s Error: ivc count exceeds the maximum supported limit of %u\n",
				__func__, MAX_NUM_IVC);
			status = SOC_PLUGIN_HAL_FAILED;
			goto exit;
		}
	}

	for (cnt = 0; cnt < ivc_cnt; cnt++) {
		uint32_t index = g_t26x_plugin_data.num_channels;

		err = of_property_read_u32_index(np, "nvidia,ivccfg", (cnt * TEGRA_IVCCFG_ARRAY_LEN)
						+ TEGRA_IVC_ID_OFFSET, &se_comm_id);
		if (err) {
			NVVSE_ERR("%s Error: failed to read se_comm_id. err %d\n", __func__, err);
			status = SOC_PLUGIN_HAL_FAILED;
			goto exit;
		}
		g_t26x_plugin_data.channels[index].se_comm_id = se_comm_id;

		err = of_property_read_u32_index(np, "nvidia,ivccfg", (cnt * TEGRA_IVCCFG_ARRAY_LEN)
						+ TEGRA_SE_ENGINE_DOMAIN_OFFSET, &se_domain);
		if (err) {
			NVVSE_ERR("%s Error: invalid queue se_engine_domain. err %d\n", __func__, err);
			status = SOC_PLUGIN_HAL_FAILED;
			goto exit;
		}
		g_t26x_plugin_data.channels[index].se_domain = se_domain;

		err = of_property_read_u32_index(np, "nvidia,ivccfg", (cnt * TEGRA_IVCCFG_ARRAY_LEN)
						+ TEGRA_SE_ENGINE_NUMBER_OFFSET, &se_domain_instance);
		if (err) {
			NVVSE_ERR(
			"%s Error: invalid queue se_engine_domain_instanceId. err %d\n",
			__func__, err);
			status = SOC_PLUGIN_HAL_FAILED;
			goto exit;
		}
		g_t26x_plugin_data.channels[index].se_domain_instance = se_domain_instance;

		err = of_property_read_u32_index(np, "nvidia,ivccfg", (cnt * TEGRA_IVCCFG_ARRAY_LEN)
						+ TEGRA_SE_PORT_OFFSET, &se_port);
		if (err) {
			NVVSE_ERR("%s Error: invalid se port. err %d\n", __func__, err);
			status = SOC_PLUGIN_HAL_FAILED;
			goto exit;
		}
		g_t26x_plugin_data.channels[index].se_port = se_port;

		err = of_property_read_u32_index(np, "nvidia,ivccfg", (cnt * TEGRA_IVCCFG_ARRAY_LEN)
						+ TEGRA_VIRTUALIZED_INSTANCE_OFFSET, &virtualized_instance);
		if (err) {
			NVVSE_ERR(
			"%s Error: invalid queue virtualized_instance_number. err %d\n",
			__func__, err);
			status = SOC_PLUGIN_HAL_FAILED;
			goto exit;
		}
		g_t26x_plugin_data.channels[index].virtualized_instance = virtualized_instance;

		err = of_property_read_u32_index(np, "nvidia,ivccfg", (cnt * TEGRA_IVCCFG_ARRAY_LEN)
						+ TEGRA_MAX_BUFFER_SIZE, &max_buffer_size);
		if (err) {
			NVVSE_ERR("%s Error: invalid max buffer size. err %d\n", __func__, err);
			status = SOC_PLUGIN_HAL_FAILED;
			goto exit;
		}
		g_t26x_plugin_data.channels[index].mapped_buffer_size = max_buffer_size;

		err = of_property_read_u32_index(np, "nvidia,ivccfg", (cnt * TEGRA_IVCCFG_ARRAY_LEN)
						+ TEGRA_SID_OFFSET, &sid);
		if (err) {
			NVVSE_ERR("%s Error: invalid sid. err %d\n", __func__, err);
			status = SOC_PLUGIN_HAL_FAILED;
			goto exit;
		}
		g_t26x_plugin_data.channels[index].stream_id = sid;

		status = soc_plugin_validate_dt_data(g_t26x_plugin_data.channels[index]);
		if (status != SOC_PLUGIN_HAL_SUCCESS) {
			NVVSE_ERR("%s Error: invalid dt data. err %d\n", __func__, status);
			status = SOC_PLUGIN_HAL_FAILED;
			goto exit;
		}

		g_t26x_plugin_data.num_channels++;
	}

	status = SOC_PLUGIN_HAL_SUCCESS;
exit:
	return status;
}

int nvvse_soc_plugin_probe(struct platform_device *pdev)
{
	NVVSE_INFO("t26x soc plugin: start probe\n");

	NVVSE_INFO("t26x soc plugin: end probe\n");

	return 0;
}

int nvvse_soc_plugin_remove(struct platform_device *pdev)
{
	return 0;
}

void nvvse_soc_plugin_shutdown(struct platform_device *pdev)
{
}

static const struct of_device_id nvvse_soc_plugin_of_match[] = {
	{},
};
MODULE_DEVICE_TABLE(of, nvvse_soc_plugin_of_match);

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void nvvse_soc_plugin_remove_wrapper(struct platform_device *pdev)
{
	int err = 0;

	err = nvvse_soc_plugin_remove(pdev);
	if (err != 0)
		NVVSE_ERR("%s failed\n", __func__);
}
#else
static int nvvse_soc_plugin_remove_wrapper(struct platform_device *pdev)
{
	int err = 0;

	err = nvvse_soc_plugin_remove(pdev);
	if (err != 0)
		NVVSE_ERR("%s failed\n", __func__);

	return err;
}
#endif

static struct platform_driver nvvse_soc_plugin_driver = {
	.probe = nvvse_soc_plugin_probe,
	.remove = nvvse_soc_plugin_remove_wrapper,
	.shutdown = nvvse_soc_plugin_shutdown,
	.driver = {
		.name = "nvvse_soc_plugin_t26x",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(nvvse_soc_plugin_of_match),
	},
};

static int __init nvvse_soc_plugin_init(void)
{
	return platform_driver_register(&nvvse_soc_plugin_driver);
}

static void __exit nvvse_soc_plugin_exit(void)
{
	platform_driver_unregister(&nvvse_soc_plugin_driver);
}

module_init(nvvse_soc_plugin_init);
module_exit(nvvse_soc_plugin_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("NVIDIA Corporation");
MODULE_DESCRIPTION("NVIDIA Virtual Secure Engine NVVSE SOC T26x Plugin");