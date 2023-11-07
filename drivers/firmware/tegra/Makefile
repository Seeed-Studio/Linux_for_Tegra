# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2022-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#
# Makefile for Extended IVC Driver and BPMP driver
#

obj-m		+= ivc_ext.o
ifneq ($(CONFIG_TEGRA_IVC_LEGACY_DISABLE),y)
tegra_bpmp-y	+= ../../clk/tegra/clk-bpmp.o
tegra_bpmp-y	+= ../../reset/tegra/reset-bpmp.o
tegra_bpmp-y	+= ../../soc/tegra/powergate-bpmp.o
tegra_bpmp-$(CONFIG_DEBUG_FS)	+= bpmp-debugfs.o
tegra_bpmp-y	+= bpmp-tegra186-hv.o
obj-m 		+= tegra_bpmp.o
else
# Develop the dummy driver for packaging script temporarily
tegra_bpmp-y	= tegra-bpmp-dummy.o
obj-m		+= tegra_bpmp.o
endif
