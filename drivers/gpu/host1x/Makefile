# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2022-2025, NVIDIA CORPORATION.  All rights reserved.

host1x-y = \
	bus.o \
	syncpt.o \
	dev.o \
	intr.o \
	cdma.o \
	channel.o \
	job.o \
	debug.o \
	fence.o


ifneq ($(filter y, $(CONFIG_ARCH_TEGRA_2x_SOC) $(CONFIG_ARCH_TEGRA_3x_SOC)),)
host1x-y += hw/host1x01.o
endif
host1x-$(CONFIG_ARCH_TEGRA_114_SOC) += hw/host1x02.o
host1x-$(CONFIG_ARCH_TEGRA_124_SOC) += hw/host1x04.o
host1x-$(CONFIG_ARCH_TEGRA_210_SOC) += hw/host1x05.o
host1x-$(CONFIG_ARCH_TEGRA_186_SOC) += hw/host1x06.o
host1x-$(CONFIG_ARCH_TEGRA_194_SOC) += hw/host1x07.o
host1x-$(CONFIG_ARCH_TEGRA_234_SOC) += hw/host1x08.o
host1x-$(CONFIG_ARCH_TEGRA_264_SOC) += hw/host1x09.o

host1x-$(CONFIG_DRM_TEGRA_HAVE_DISPLAY) += mipi.o
host1x-$(CONFIG_PM_DEVFREQ) += actmon.o
host1x-$(CONFIG_IOMMU_API) += context.o

obj-m := host1x.o
