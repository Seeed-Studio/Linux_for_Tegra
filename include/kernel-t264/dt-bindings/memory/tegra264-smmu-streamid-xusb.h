/*
 * Copyright (c) 2024, NVIDIA CORPORATION.  All rights reserved.
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

#ifndef __TEGRA264_SMMU_STREAMID_USB_H__
#define __TEGRA264_SMMU_STREAMID_USB_H__

#include "tegra264-mc.h"

/* SIDs for PF */
#define TEGRA_SID_XUSB_PF_HS_PORTS      (TEGRA_SID_XUSB_DEV | 0U)
#define TEGRA_SID_XUSB_PF_SS_PORT1      (TEGRA_SID_XUSB_DEV1 | 0U)
#define TEGRA_SID_XUSB_PF_SS_PORT2      (TEGRA_SID_XUSB_DEV2 | 0U)
#define TEGRA_SID_XUSB_PF_SS_PORT3      (TEGRA_SID_XUSB_DEV3 | 0U)
#define TEGRA_SID_XUSB_PF_SS_PORT4      (TEGRA_SID_XUSB_DEV4 | 0U)
/* SIDs for VF1 */
#define TEGRA_SID_XUSB_VF1_HS_PORTS     (TEGRA_SID_XUSB_DEV | 1U)
#define TEGRA_SID_XUSB_VF1_SS_PORT1     (TEGRA_SID_XUSB_DEV1 | 1U)
#define TEGRA_SID_XUSB_VF1_SS_PORT2     (TEGRA_SID_XUSB_DEV2 | 1U)
#define TEGRA_SID_XUSB_VF1_SS_PORT3     (TEGRA_SID_XUSB_DEV3 | 1U)
#define TEGRA_SID_XUSB_VF1_SS_PORT4     (TEGRA_SID_XUSB_DEV4 | 1U)
/* SIDs for VF2 */
#define TEGRA_SID_XUSB_VF2_HS_PORTS     (TEGRA_SID_XUSB_DEV | 2U)
#define TEGRA_SID_XUSB_VF2_SS_PORT1     (TEGRA_SID_XUSB_DEV1 | 2U)
#define TEGRA_SID_XUSB_VF2_SS_PORT2     (TEGRA_SID_XUSB_DEV2 | 2U)
#define TEGRA_SID_XUSB_VF2_SS_PORT3     (TEGRA_SID_XUSB_DEV3 | 2U)
#define TEGRA_SID_XUSB_VF2_SS_PORT4     (TEGRA_SID_XUSB_DEV4 | 2U)
/* SIDs for VF3 */
#define TEGRA_SID_XUSB_VF3_HS_PORTS     (TEGRA_SID_XUSB_DEV | 3U)
#define TEGRA_SID_XUSB_VF3_SS_PORT1     (TEGRA_SID_XUSB_DEV1 | 3U)
#define TEGRA_SID_XUSB_VF3_SS_PORT2     (TEGRA_SID_XUSB_DEV2 | 3U)
#define TEGRA_SID_XUSB_VF3_SS_PORT3     (TEGRA_SID_XUSB_DEV3 | 3U)
#define TEGRA_SID_XUSB_VF3_SS_PORT4     (TEGRA_SID_XUSB_DEV4 | 3U)
/* SIDs for VF4 */
#define TEGRA_SID_XUSB_VF4_HS_PORTS     (TEGRA_SID_XUSB_DEV | 4U)
#define TEGRA_SID_XUSB_VF4_SS_PORT1     (TEGRA_SID_XUSB_DEV1 | 4U)
#define TEGRA_SID_XUSB_VF4_SS_PORT2     (TEGRA_SID_XUSB_DEV2 | 4U)
#define TEGRA_SID_XUSB_VF4_SS_PORT3     (TEGRA_SID_XUSB_DEV3 | 4U)
#define TEGRA_SID_XUSB_VF4_SS_PORT4     (TEGRA_SID_XUSB_DEV4 | 4U)
/* SIDs for VF5 */
#define TEGRA_SID_XUSB_VF5_HS_PORTS     (TEGRA_SID_XUSB_DEV | 5U)
#define TEGRA_SID_XUSB_VF5_SS_PORT1     (TEGRA_SID_XUSB_DEV1 | 5U)
#define TEGRA_SID_XUSB_VF5_SS_PORT2     (TEGRA_SID_XUSB_DEV2 | 5U)
#define TEGRA_SID_XUSB_VF5_SS_PORT3     (TEGRA_SID_XUSB_DEV3 | 5U)
#define TEGRA_SID_XUSB_VF5_SS_PORT4     (TEGRA_SID_XUSB_DEV4 | 5U)
/* SIDs for VF6 */
#define TEGRA_SID_XUSB_VF6_HS_PORTS     (TEGRA_SID_XUSB_DEV | 6U)
#define TEGRA_SID_XUSB_VF6_SS_PORT1     (TEGRA_SID_XUSB_DEV1 | 6U)
#define TEGRA_SID_XUSB_VF6_SS_PORT2     (TEGRA_SID_XUSB_DEV2 | 6U)
#define TEGRA_SID_XUSB_VF6_SS_PORT3     (TEGRA_SID_XUSB_DEV3 | 6U)
#define TEGRA_SID_XUSB_VF6_SS_PORT4     (TEGRA_SID_XUSB_DEV4 | 6U)
/* SIDs for VF7 */
#define TEGRA_SID_XUSB_VF7_HS_PORTS     (TEGRA_SID_XUSB_DEV | 7U)
#define TEGRA_SID_XUSB_VF7_SS_PORT1     (TEGRA_SID_XUSB_DEV1 | 7U)
#define TEGRA_SID_XUSB_VF7_SS_PORT2     (TEGRA_SID_XUSB_DEV2 | 7U)
#define TEGRA_SID_XUSB_VF7_SS_PORT3     (TEGRA_SID_XUSB_DEV3 | 7U)
#define TEGRA_SID_XUSB_VF7_SS_PORT4     (TEGRA_SID_XUSB_DEV4 | 7U)
/* SIDs for DEV */
#define TEGRA_SID_XUSB_DEV_MODE         (TEGRA_SID_XUSB_DEV5 | 8U)

#endif /* __TEGRA264_SMMU_STREAMID_USB_H__ */
