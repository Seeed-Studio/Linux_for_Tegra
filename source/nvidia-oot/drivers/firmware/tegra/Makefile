# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2022-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#
# Makefile for Extended IVC Driver and BPMP driver
#

ifneq ($(NV_OOT_IVC_EXT_SKIP_BUILD),y)
obj-m		+= ivc_ext.o
else
obj-m		+= ivc_ext.o
ivc_ext-objs := ivc_ext_dummy.o
endif

ifneq ($(NV_OOT_TEGRA_BPMP_SKIP_BUILD),y)
tegra_bpmp-y	+= ../../clk/tegra/clk-bpmp.o
tegra_bpmp-y	+= ../../reset/tegra/reset-bpmp.o
tegra_bpmp-y	+= ../../soc/tegra/powergate-bpmp.o
tegra_bpmp-$(CONFIG_DEBUG_FS)	+= bpmp-debugfs.o
tegra_bpmp-y	+= bpmp-tegra186-hv.o
obj-m 		+= tegra_bpmp.o
else
obj-m 		+= tegra_bpmp.o
tegra_bpmp-objs	+= tegra_bpmp_dummy.o
endif
