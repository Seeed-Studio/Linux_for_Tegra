# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2022-2025, NVIDIA CORPORATION.  All rights reserved.

obj-m += tegra-pcie-edma.o
obj-m += pcie-tegra-vf.o

# Set config when build as OOT module.
ifneq ($(NV_OOT_PCIE_T264_SOC_SKIP_BUILD),y)
ifeq ($(CONFIG_TEGRA_OOT_MODULE),m)
CONFIG_PCIE_TEGRA264 := m
CONFIG_PCIE_TEGRA264_EP := m
endif

obj-$(CONFIG_PCIE_TEGRA264) += pcie-tegra264.o
obj-$(CONFIG_PCIE_TEGRA264_EP) += pcie-tegra264-ep.o

ifeq ($(CONFIG_TEGRA_OOT_MODULE),m)
obj-m += tegra-pcie-dma-lib.o

tegra-pcie-dma-lib-objs:= tegra-pcie-dma.o \
			  tegra234-pcie-edma.o \
			  tegra264-pcie-xdma.o
endif
else
obj-m += pcie-tegra264.o
pcie-tegra264-objs := pcie-tegra-soc-dummy.o
endif
