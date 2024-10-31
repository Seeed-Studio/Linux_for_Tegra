#
# SPDX-FileCopyrightText: Copyright (c) 2022-2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# nvhost-nvdla.ko
#

NVDLA_TOP := .

ifndef CONFIG_TEGRA_KLEAF_BUILD
include $(src)/Makefile.config.mk
else
include $(srctree.nvidia-oot)/drivers/video/tegra/host/nvdla/Makefile.config.mk
endif

NVDLA_COMMON_OBJS := $(addprefix $(NVDLA_TOP)/,$(NVDLA_COMMON_OBJS))

ccflags-y += -DNVDLA_HAVE_CONFIG_HW_PERFMON=1
ccflags-y += -DNVDLA_HAVE_CONFIG_AXI=0
ccflags-y += -DNVDLA_HAVE_CONFIG_SYNCPTFD=1

ifdef CONFIG_TEGRA_HSIERRRPTINJ
ccflags-y += -DNVDLA_HAVE_CONFIG_HSIERRINJ=1
endif

nvhost-nvdla-objs += \
		$(NVDLA_COMMON_OBJS) \
		$(NVDLA_TOP)/port/device/nvdla_device_host1x.o \
		$(NVDLA_TOP)/port/fw/nvdla_fw_flcn.o \
		$(NVDLA_TOP)/port/sync/nvdla_sync_syncpt.o

ifdef CONFIG_TEGRA_GRHOST
nvhost-nvdla-objs += $(NVDLA_TOP)/dla_channel.o
endif

obj-m += nvhost-nvdla.o
