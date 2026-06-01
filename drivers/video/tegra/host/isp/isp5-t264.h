/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES.  All rights reserved.
 */

/*
 * struct of_device_id initialization for ISP on T264
 */

//static const struct of_device_id tegra_isp5_of_match[] = {

	{
		.name = "isp0",
		.compatible = "nvidia,tegra264-isp",
		.data = &t264_isp0_info,
	},
	{
		.name = "isp1",
		.compatible = "nvidia,tegra264-isp",
		.data = &t264_isp1_info,
	},

//}
