# SPDX-License-Identifier: GPL-2.0

# Set config when build as OOT module.
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
