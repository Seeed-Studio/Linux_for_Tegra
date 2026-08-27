/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES.  All rights reserved.
 */

/*
 * struct of_device_id initialization for capture support on T264
 */

//static struct of_device_id capture_support_of_match[] = {

	{
		.name = "isp-thi",
		.compatible = "nvidia,tegra264-isp-thi",
		.data = &t264_isp0_thi_info,
	},
	{
		.name = "isp1-thi",
		.compatible = "nvidia,tegra264-isp-thi",
		.data = &t264_isp1_thi_info,
	},
//}
