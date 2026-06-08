# SPDX-License-Identifier: GPL-2.0-only
# SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
tegra-bpmp-y			= bpmp.o
tegra-bpmp-$(CONFIG_ARCH_TEGRA_210_SOC)	+= bpmp-tegra210.o
tegra-bpmp-$(CONFIG_ARCH_TEGRA_186_SOC)	+= bpmp-tegra186.o
tegra-bpmp-$(CONFIG_ARCH_TEGRA_194_SOC)	+= bpmp-tegra186.o
tegra-bpmp-$(CONFIG_ARCH_TEGRA_234_SOC)	+= bpmp-tegra186.o
tegra-bpmp-$(CONFIG_TEGRA_HV_DRIVER)	+= bpmp-tegra194-hv.o
tegra-bpmp-$(CONFIG_DEBUG_FS)	+= bpmp-debugfs.o
obj-$(CONFIG_TEGRA_BPMP)	+= tegra-bpmp.o
obj-$(CONFIG_TEGRA_IVC)		+= ivc.o
