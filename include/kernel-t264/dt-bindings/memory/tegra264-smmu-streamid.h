/*
 * Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef DT_BINDINGS_MEMORY_TEGRA264_SMMU_STREAMID_H
#define DT_BINDINGS_MEMORY_TEGRA264_SMMU_STREAMID_H

#define TEGRA_SID_SHIFT  0x8U

#define TEGRA_SID_AON    (0x1U << TEGRA_SID_SHIFT)
#define TEGRA_SID_APE    (0x2U << TEGRA_SID_SHIFT)
#define TEGRA_SID_ETR    (0x3U << TEGRA_SID_SHIFT)
#define TEGRA_SID_BPMP   (0x4U << TEGRA_SID_SHIFT)
#define TEGRA_SID_DCE    (0x5U << TEGRA_SID_SHIFT)
#define TEGRA_SID_EQOS   (0x6U << TEGRA_SID_SHIFT)
#define TEGRA_SID_GPCDMA (0x8U << TEGRA_SID_SHIFT)
#define TEGRA_SID_DISP   (0x9U << TEGRA_SID_SHIFT)
#define TEGRA_SID_HDA    (0xAU << TEGRA_SID_SHIFT)
#define TEGRA_SID_HOST1X (0xBU << TEGRA_SID_SHIFT)
#define TEGRA_SID_ISP0   (0xCU << TEGRA_SID_SHIFT)
#define TEGRA_SID_ISP2   (0xDU << TEGRA_SID_SHIFT)
#define TEGRA_SID_PMA0   (0xEU << TEGRA_SID_SHIFT)
#define TEGRA_SID_FSI0   (0xFU << TEGRA_SID_SHIFT)
#define TEGRA_SID_FSI1   (0x10U << TEGRA_SID_SHIFT)
#define TEGRA_SID_PVA    (0x11U << TEGRA_SID_SHIFT)
#define TEGRA_SID_SDMMC0 (0x12U << TEGRA_SID_SHIFT)
#define TEGRA_SID_MGBE0  (0x13U << TEGRA_SID_SHIFT)
#define TEGRA_SID_MGBE1  (0x14U << TEGRA_SID_SHIFT)
#define TEGRA_SID_MGBE2  (0x15U << TEGRA_SID_SHIFT)
#define TEGRA_SID_MGBE3  (0x16U << TEGRA_SID_SHIFT)
#define TEGRA_SID_MSSSEQ (0x17U << TEGRA_SID_SHIFT)
#define TEGRA_SID_SE     (0x18U << TEGRA_SID_SHIFT)
#define TEGRA_SID_SEU1   (0x19U << TEGRA_SID_SHIFT)
#define TEGRA_SID_SEU2   (0x1AU << TEGRA_SID_SHIFT)
#define TEGRA_SID_SEU3   (0x1BU << TEGRA_SID_SHIFT)
#define TEGRA_SID_PSC    (0x1CU << TEGRA_SID_SHIFT)
#define TEGRA_SID_OESP   (0x23U << TEGRA_SID_SHIFT)
#define TEGRA_SID_SB     (0x24U << TEGRA_SID_SHIFT)
#define TEGRA_SID_XSPI0  (0x25U << TEGRA_SID_SHIFT)
#define TEGRA_SID_TSEC   (0x29U << TEGRA_SID_SHIFT)
#define TEGRA_SID_UFS    (0x2AU << TEGRA_SID_SHIFT)
#define TEGRA_SID_RCE    (0x2BU << TEGRA_SID_SHIFT)
#define TEGRA_SID_RCE1   (0x2CU << TEGRA_SID_SHIFT)
#define TEGRA_SID_VI     (0x2EU << TEGRA_SID_SHIFT)
#define TEGRA_SID_VI1    (0x2FU << TEGRA_SID_SHIFT)
#define TEGRA_SID_VIC    (0x30U << TEGRA_SID_SHIFT)
#define TEGRA_SID_XUSB_DEV  (0x32U << TEGRA_SID_SHIFT)
#define TEGRA_SID_XUSB_DEV1 (0x33U << TEGRA_SID_SHIFT)
#define TEGRA_SID_XUSB_DEV2 (0x34U << TEGRA_SID_SHIFT)
#define TEGRA_SID_XUSB_DEV3 (0x35U << TEGRA_SID_SHIFT)
#define TEGRA_SID_XUSB_DEV4 (0x36U << TEGRA_SID_SHIFT)
#define TEGRA_SID_XUSB_DEV5 (0x37U << TEGRA_SID_SHIFT)

#endif /* DT_BINDINGS_MEMORY_TEGRA264_SMMU_STREAMID_H */
