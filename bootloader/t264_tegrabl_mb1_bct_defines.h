/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#ifndef INCLUDED_TEGRABL_MB1_BCT_DEFINES_H
#define INCLUDED_TEGRABL_MB1_BCT_DEFINES_H

/** @brief Socket IDs */
#define SOCKET0							(0x00U)
#define SOCKET1							(0x01U)

/** @brief SOC SKUs */
#define SOC_SKU_INT						(0x00U)
#define SOC_SKU_SLT_PROD				(0xA3U)

/**
 * CCPLEX CPU cluster and SCF clock sources defined in BCT
 * @addtogroup CCPLEX_CLK_SOURCE_IDS
 * @{
 */
#define CCPLEX_CLK_SOURCE_OSC			(0x00U)
#define CCPLEX_CLK_SOURCE_PLLP			(0x01U)
#define CCPLEX_CLK_SOURCE_SPLL			(0x02U)
#define CCPLEX_CLK_SOURCE_PLLX			(0x02U)
#define CCPLEX_CLK_SOURCE_NAFLL			(0x03U)
#define CCPLEX_CLK_SOURCE_MAX			(0x04U)
/** @} */

/**
 * @brief DIVIDER setting used for CCPLEX CPU Cluster
 * @addtogroup CCPLEX_CLK_DIVIDERS
 * @{
 */
#define CCPLEX_CLK_DIVIDER_1P0			(0x00U)
#define CCPLEX_CLK_DIVIDER_1P5			(0x01U)
#define CCPLEX_CLK_DIVIDER_2P0			(0x02U)
#define CCPLEX_CLK_DIVIDER_2P5			(0x03U)
#define CCPLEX_CLK_DIVIDER_3P0			(0x04U)
#define CCPLEX_CLK_DIVIDER_3P5			(0x05U)
#define CCPLEX_CLK_DIVIDER_4P0			(0x06U)
#define CCPLEX_CLK_DIVIDER_4P5			(0x07U)
#define CCPLEX_CLK_DIVIDER_5P0			(0x08U)
#define CCPLEX_CLK_DIVIDER_5P5			(0x09U)
#define CCPLEX_CLK_DIVIDER_6P0			(0x0AU)
#define CCPLEX_CLK_DIVIDER_6P5			(0x0BU)
#define CCPLEX_CLK_DIVIDER_7P0			(0x0CU)
#define CCPLEX_CLK_DIVIDER_7P5			(0x0DU)
#define CCPLEX_CLK_DIVIDER_8P0			(0x0EU)
#define CCPLEX_CLK_DIVIDER_8P5			(0x0FU)
#define CCPLEX_CLK_DIVIDER_9P0			(0x10U)
#define CCPLEX_CLK_DIVIDER_9P5			(0x11U)
#define CCPLEX_CLK_DIVIDER_10P0			(0x12U)
#define CCPLEX_CLK_DIVIDER_10P5			(0x13U)
#define CCPLEX_CLK_DIVIDER_11P0			(0x14U)
#define CCPLEX_CLK_DIVIDER_11P5			(0x15U)
#define CCPLEX_CLK_DIVIDER_12P0			(0x16U)
#define CCPLEX_CLK_DIVIDER_12P5			(0x17U)
#define CCPLEX_CLK_DIVIDER_13P0			(0x18U)
#define CCPLEX_CLK_DIVIDER_13P5			(0x19U)
#define CCPLEX_CLK_DIVIDER_14P0			(0x1AU)
#define CCPLEX_CLK_DIVIDER_14P5			(0x1BU)
#define CCPLEX_CLK_DIVIDER_15P0			(0x1CU)
#define CCPLEX_CLK_DIVIDER_15P5			(0x1DU)
#define CCPLEX_CLK_DIVIDER_16P0			(0x1EU)
#define CCPLEX_CLK_DIVIDER_16P5			(0x1FU)
#define CCPLEX_CLK_DIVIDER_17P0			(0x20U)
#define CCPLEX_CLK_DIVIDER_17P5			(0x21U)
#define CCPLEX_CLK_DIVIDER_18P0			(0x22U)
#define CCPLEX_CLK_DIVIDER_18P5			(0x23U)
#define CCPLEX_CLK_DIVIDER_19P0			(0x24U)
#define CCPLEX_CLK_DIVIDER_19P5			(0x25U)
#define CCPLEX_CLK_DIVIDER_20P0			(0x26U)
#define CCPLEX_CLK_DIVIDER_20P5			(0x27U)
#define CCPLEX_CLK_DIVIDER_21P0			(0x28U)
#define CCPLEX_CLK_DIVIDER_21P5			(0x29U)
#define CCPLEX_CLK_DIVIDER_22P0			(0x2AU)
#define CCPLEX_CLK_DIVIDER_22P5			(0x2BU)
#define CCPLEX_CLK_DIVIDER_23P0			(0x2CU)
#define CCPLEX_CLK_DIVIDER_23P5			(0x2DU)
#define CCPLEX_CLK_DIVIDER_24P0			(0x2EU)
#define CCPLEX_CLK_DIVIDER_24P5			(0x2FU)
#define CCPLEX_CLK_DIVIDER_25P0			(0x30U)
#define CCPLEX_CLK_DIVIDER_25P5			(0x31U)
#define CCPLEX_CLK_DIVIDER_26P0			(0x32U)
#define CCPLEX_CLK_DIVIDER_26P5			(0x33U)
#define CCPLEX_CLK_DIVIDER_27P0			(0x34U)
#define CCPLEX_CLK_DIVIDER_27P5			(0x35U)
#define CCPLEX_CLK_DIVIDER_28P0			(0x36U)
#define CCPLEX_CLK_DIVIDER_28P5			(0x37U)
#define CCPLEX_CLK_DIVIDER_29P0			(0x38U)
#define CCPLEX_CLK_DIVIDER_29P5			(0x39U)
#define CCPLEX_CLK_DIVIDER_30P0			(0x3AU)
#define CCPLEX_CLK_DIVIDER_30P5			(0x3BU)
#define CCPLEX_CLK_DIVIDER_31P0			(0x3CU)
#define CCPLEX_CLK_DIVIDER_MASK			(0xFFU)
#define CCPLEX_CLK_DIVIDER_MIN 			(CCPLEX_CLK_DIVIDER_1P0)
#define CCPLEX_CLK_DIVIDER_MAX 			(CCPLEX_CLK_DIVIDER_31P0 + 1U)
/** @} */

#endif /* INCLUDED_TEGRABL_MB1_BCT_DEFINES_H */
