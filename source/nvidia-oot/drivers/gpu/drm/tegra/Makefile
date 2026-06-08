# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2022-2025, NVIDIA CORPORATION.  All rights reserved.

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

ifeq ($(CONFIG_DRM_TEGRA_HAVE_DISPLAY),y)
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

ifneq ($(CONFIG_TEGRA_DRM_NATIVE_DIS),y)
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

tegra-drm-$(CONFIG_DRM_FBDEV_EMULATION) += fbdev.o

obj-m := tegra-drm.o
