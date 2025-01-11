# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2022-2023, NVIDIA CORPORATION.  All rights reserved.

ifeq ($(NV_BUILD_CONFIGURATION_IS_SAFETY),1)
ccflags-y += -DCONFIG_TEGRA_HWPM_CAM_SAFETY
else ifeq ($(CONFIG_TEGRA_LINUX_PROD),1)
ccflags-y += -DCONFIG_TEGRA_HWPM_CAM_PROD
else ifneq ($(CONFIG_ARCH_TEGRA),y)
ccflags-y += -DCONFIG_TEGRA_HWPM_CAM_NON_TEGRA
else
ccflags-y += -DCONFIG_TEGRA_HWPM_CAM
endif

nvhost-capture-objs = capture-support.o

obj-m += nvhost-capture.o
