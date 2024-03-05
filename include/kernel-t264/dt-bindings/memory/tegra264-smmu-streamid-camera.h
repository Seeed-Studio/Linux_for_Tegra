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

#ifndef DT_BINDINGS_MEMORY_TEGRA264_SMMU_STREAMID_CAMERA_H
#define DT_BINDINGS_MEMORY_TEGRA264_SMMU_STREAMID_CAMERA_H

#include <dt-bindings/memory/tegra264-smmu-streamid.h>

#define TEGRA_SID_RCE_VM1    (TEGRA_SID_RCE | 1U)
#define TEGRA_SID_RCE_VM2    (TEGRA_SID_RCE | 2U)
#define TEGRA_SID_VI1_VM1    (TEGRA_SID_VI | 1U)
#define TEGRA_SID_VI1_VM2    (TEGRA_SID_VI | 2U)
#define TEGRA_SID_VI2_VM1    (TEGRA_SID_VI1 | 1U)
#define TEGRA_SID_VI2_VM2    (TEGRA_SID_VI1 | 2U)
#define TEGRA_SID_ISP1_VM1   (TEGRA_SID_ISP0 | 1U)
#define TEGRA_SID_ISP1_VM2   (TEGRA_SID_ISP0 | 2U)
#define TEGRA_SID_ISP2_VM1   (TEGRA_SID_ISP2 | 1U)
#define TEGRA_SID_ISP2_VM2   (TEGRA_SID_ISP2 | 2U)

#endif /* DT_BINDINGS_MEMORY_TEGRA264_SMMU_STREAMID_CAMERA_H */