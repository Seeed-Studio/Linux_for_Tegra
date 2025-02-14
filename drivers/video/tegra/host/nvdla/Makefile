#
# SPDX-FileCopyrightText: Copyright (c) 2022-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: GPL-2.0-only
#
# nvhost-nvdla.ko
#

ifndef CONFIG_TEGRA_KLEAF_BUILD
include $(src)/Makefile.config.mk
else
include $(srctree.nvidia-oot)/drivers/video/tegra/host/nvdla/Makefile.config.mk
endif

ccflags-y += -DNVDLA_HAVE_CONFIG_HW_PERFMON=1
ccflags-y += -DNVDLA_HAVE_CONFIG_AXI=0
ccflags-y += -DNVDLA_HAVE_CONFIG_SYNCPTFD=1

ifdef CONFIG_TEGRA_HSIERRRPTINJ
ccflags-y += -DNVDLA_HAVE_CONFIG_HSIERRINJ=1
endif

nvhost-nvdla-objs += \
		$(NVDLA_COMMON_OBJS) \
		port/device/nvdla_device_host1x.o \
		port/fw/nvdla_fw_flcn.o \
		port/pm/nvdla_pm_stub.o \
		port/sync/nvdla_sync_syncpt.o \
		port/fw/nvdla_falcon.o

ifdef CONFIG_TEGRA_GRHOST
nvhost-nvdla-objs += dla_channel.o
endif

obj-m += nvhost-nvdla.o

ifndef CONFIG_TEGRA_SYSTEM_TYPE_ACK
ifeq ($(NV_BUILD_CONFIGURATION_EXPOSING_T26X), 1)
obj-m += axi/
endif
endif
