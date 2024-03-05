/*
 * Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _DT_BINDINGS_CLOCK_TEGRA264_CLOCK_H
#define _DT_BINDINGS_CLOCK_TEGRA264_CLOCK_H

#include <dt-bindings/clock/tegra264-clk.h>

#if (TEGRA_BPMP_FW_DT_VERSION >= DT_VERSION_2)
#define bpmp_clks SOC_BPMP_LABEL
#endif

#endif
