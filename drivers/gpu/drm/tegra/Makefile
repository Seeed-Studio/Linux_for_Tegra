# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2022-2024, NVIDIA CORPORATION.  All rights reserved.

ccflags-$(CONFIG_DRM_TEGRA_DEBUG) += -DDEBUG
ccflags-y += -I$(srctree.nvidia-oot)/drivers/gpu/drm/tegra/include
ccflags-y += -I$(srctree.hwpm)/include

tegra-drm-y := \
	drm.o \
	uapi.o \
	submit.o \
	firewall.o \
	gem.o \
	fb.o \
	virt.o

ifneq ($(filter y, $(CONFIG_ARCH_TEGRA_2x_SOC) $(CONFIG_ARCH_TEGRA_3x_SOC) \
	$(CONFIG_ARCH_TEGRA_114_SOC) $(CONFIG_ARCH_TEGRA_124_SOC) $(CONFIG_ARCH_TEGRA_132_SOC) \
	$(CONFIG_ARCH_TEGRA_210_SOC) $(CONFIG_ARCH_TEGRA_186_SOC) $(CONFIG_ARCH_TEGRA_194_SOC)),)
tegra-drm-y += \
	dp.o \
	hub.o \
	plane.o \
	dc.o \
	output.o \
	rgb.o \
	hda.o \
	hdmi.o \
	mipi-phy.o \
	dsi.o \
	sor.o \
	dpaux.o \
	gr2d.o \
	gr3d.o
endif

ifeq ($(CONFIG_TEGRA_DRM_NATIVE_DIS),y)
ccflags-y += -DCONFIG_TEGRA_DRM_NATIVE_DIS
else
tegra-drm-y += \
	falcon.o \
	hwpm.o \
	vic.o \
	nvdec.o \
	nvenc.o \
	nvjpg.o \
	riscv.o \
	util.o \
	ofa.o
endif

tegra-drm-y += trace.o

obj-m := tegra-drm.o
