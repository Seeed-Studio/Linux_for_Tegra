# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2022-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#
# Makefile for Extended IVC Driver and BPMP driver
#

obj-m		+= ivc_ext.o
tegra_bpmp-y	+= ../../clk/tegra/clk-bpmp.o
tegra_bpmp-y	+= ../../reset/tegra/reset-bpmp.o
tegra_bpmp-y	+= ../../soc/tegra/powergate-bpmp.o
tegra_bpmp-$(CONFIG_DEBUG_FS)	+= bpmp-debugfs.o
tegra_bpmp-y	+= bpmp-tegra186-hv.o
obj-m 		+= tegra_bpmp.o
