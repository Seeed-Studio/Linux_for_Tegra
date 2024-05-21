/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef BPMP_ABI_MACH_T264_THERMAL_H
#define BPMP_ABI_MACH_T264_THERMAL_H

/**
 * @file
 * @defgroup bpmp_thermal_ids Thermal Zone ID's
 * @{
 */

#define TEGRA264_THERMAL_ZONE_TJ_MAX		0U
#define TEGRA264_THERMAL_ZONE_TJ_MIN		1U
#define TEGRA264_THERMAL_ZONE_GPU_AVG		2U
#define TEGRA264_THERMAL_ZONE_CPU_AVG		3U
#define TEGRA264_THERMAL_ZONE_SOC_012_AVG	4U /* Powered by Vdd_SoC */
#define TEGRA264_THERMAL_ZONE_SOC_45_AVG	5U /* Powered by Vdd_MSS */
#define TEGRA264_THERMAL_ZONE_SOC_3_AVG		6U /* Powered by Uphy_Vdd */
#define TEGRA264_THERMAL_ZONE_GPU_MAX		7U
#define TEGRA264_THERMAL_ZONE_CPU_MAX		8U
#define TEGRA264_THERMAL_ZONE_SOC_012_MAX	9U /* Powered by Vdd_SoC */
#define TEGRA264_THERMAL_ZONE_SOC_345_MAX	10U /* Powered by Uphy_Vdd and Vdd_MSS */
#define TEGRA264_THERMAL_ZONE_TJ_AVG		11U

#define TEGRA264_THERMAL_ZONE_COUNT		12U

/** @} */

/* Deprecated */

#define TEGRA264_THERMAL_ZONE_SECURE0		0U
#define TEGRA264_THERMAL_ZONE_SECURE1		1U
#define TEGRA264_THERMAL_ZONE_SECURE2		2U
#define TEGRA264_THERMAL_ZONE_SECURE3		3U
#define TEGRA264_THERMAL_ZONE_SECURE4		4U
#define TEGRA264_THERMAL_ZONE_SECURE5		5U
#define TEGRA264_THERMAL_ZONE_GENERAL0		6U
#define TEGRA264_THERMAL_ZONE_GENERAL1		7U
#define TEGRA264_THERMAL_ZONE_GENERAL2		8U
#define TEGRA264_THERMAL_ZONE_GENERAL3		9U
#define TEGRA264_THERMAL_ZONE_GENERAL4		10U
#define TEGRA264_THERMAL_ZONE_GENERAL5		11U

#endif /* BPMP_ABI_MACH_T264_THERMAL_H */
