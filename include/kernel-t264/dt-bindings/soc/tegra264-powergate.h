/*
 * Copyright (c) 2022, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef BPMP_ABI_MACH_T264_POWERGATE_T264_H
#define BPMP_ABI_MACH_T264_POWERGATE_T264_H

/**
 * @file
 * @defgroup bpmp_pdomain_ids Power Domain ID's
 * This is a list of power domain IDs provided by the firmware.
 * @{
 */
#define TEGRA264_POWER_DOMAIN_DISP	1U
#define TEGRA264_POWER_DOMAIN_AUD	2U
#define TEGRA264_POWER_DOMAIN_AUDCMN	3U
#define TEGRA264_POWER_DOMAIN_PCIE_C1	4U
#define TEGRA264_POWER_DOMAIN_PCIE_C2	5U
#define TEGRA264_POWER_DOMAIN_PCIE_C3	6U
#define TEGRA264_POWER_DOMAIN_PCIE_C4	7U
#define TEGRA264_POWER_DOMAIN_PCIE_C5	8U
#define TEGRA264_POWER_DOMAIN_PCIE_C6	9U
#define TEGRA264_POWER_DOMAIN_XUSB_SS	10U
#define TEGRA264_POWER_DOMAIN_XUSB_DEV	11U
#define TEGRA264_POWER_DOMAIN_XUSB_HOST	12U
#define TEGRA264_POWER_DOMAIN_MGBE0	13U
#define TEGRA264_POWER_DOMAIN_MGBE1	14U
#define TEGRA264_POWER_DOMAIN_MGBE2	15U
#define TEGRA264_POWER_DOMAIN_MGBE3	16U
#define TEGRA264_POWER_DOMAIN_VI	17U
#define TEGRA264_POWER_DOMAIN_VIC	18U
#define TEGRA264_POWER_DOMAIN_ISP0	19U
#define TEGRA264_POWER_DOMAIN_ISP1	20U
#define TEGRA264_POWER_DOMAIN_PVA0	21U
#define TEGRA264_POWER_DOMAIN_GPU	22U

#define TEGRA264_POWER_DOMAIN_MAX	22U
/** @} */

#endif
