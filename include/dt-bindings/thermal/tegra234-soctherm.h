/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef BPMP_ABI_MACH_T234_SOCTHERM_H
#define BPMP_ABI_MACH_T234_SOCTHERM_H

/**
 * @file
 * @defgroup bpmp_soctherm_ids Soctherm ID's
 * @{
 *   @defgroup bpmp_soctherm_throt_ids Throttle Identifiers
 *   @defgroup bpmp_soctherm_edp_oc_ids EDP/OC Identifiers
 *   @defgroup bpmp_soctherm_throt_modes Throttle Modes
 * @}
 */

/**
 * @addtogroup bpmp_soctherm_throt_ids
 * @{
 */
#define TEGRA234_SOCTHERM_THROT_NONE		0U
#define TEGRA234_SOCTHERM_THROT_LITE		1U
#define TEGRA234_SOCTHERM_THROT_MED		2U
#define TEGRA234_SOCTHERM_THROT_HEAVY		4U
/** @} */

/**
 * @addtogroup bpmp_soctherm_edp_oc_ids
 * @{
 */
#define TEGRA234_SOCTHERM_EDP_OC1		0U
#define TEGRA234_SOCTHERM_EDP_OC2		1U
#define TEGRA234_SOCTHERM_EDP_OC3		2U
#define TEGRA234_SOCTHERM_EDP_OC_CNT		3U
/** @} */

/**
 * @addtogroup bpmp_soctherm_throt_modes
 * @{
 */
#define TEGRA234_SOCTHERM_EDP_OC_MODE_BRIEF	2U
/** @} */

#endif

