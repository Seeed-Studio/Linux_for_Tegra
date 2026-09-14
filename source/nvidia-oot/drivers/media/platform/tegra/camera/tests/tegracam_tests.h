/* SPDX-License-Identifier: GPL-2.0 */
/*
 * tegracam_tests - tegra camera kernel tests
 *
 * Copyright (c) 2018-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 */
#ifndef __TEGRACAM_TESTS_H__
#define __TEGRACAM_TESTS_H__

/*
 * Tegra Camera Kernel Tests
 */
int sensor_verify_dt(struct device_node *node, const u32 tvcf_version);

#endif // __TEGRACAM_TESTS_H__
