#
# SPDX-FileCopyrightText: Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: GPL-2.0-only
#

NVDLA_MODULE_NAME=axi-nvdla

NVDLA_TOP := ..

ifndef CONFIG_TEGRA_KLEAF_BUILD
include $(src)/../Makefile.config.mk
else
include $(srctree.nvidia-oot)/drivers/video/tegra/host/nvdla/Makefile.config.mk
ccflags-y += -I$(srctree.nvidia-oot)/drivers/video/tegra/host/nvdla
ccflags-y += -I$(srctree.nvidia-oot)/drivers/video/tegra/host/nvdla/port/fw
endif

#required to get the nvhost_device struture definitions
LINUXINCLUDE += -I$(srctree.nvidia-oot)/drivers/gpu/host1x-emu/include
LINUXINCLUDE += -I$(srctree.nvidia-oot)/drivers/gpu/host1x-fence/include

#
# axi-nvdla.ko
#
ccflags-y += -DNVDLA_HAVE_CONFIG_HW_PERFMON=0
ccflags-y += -DNVDLA_HAVE_CONFIG_AXI=1
ccflags-y += -DNVDLA_HAVE_CONFIG_SYNCPTFD=0
ccflags-y += -DNVDLA_HAVE_CONFIG_HSIERRINJ=0
ccflags-y += -DNVDLA_HAVE_CONFIG_FIREWALL=0
ccflags-y += -DNVDLA_HAVE_CONFIG_FWSUSPEND=1
ccflags-y += -DBUG_4942853=1
ccflags-y += -DBUG_4960393=1
ccflags-y += -DBUG_4972382=1
ccflags-y += -DBUG_5054810=1

NVDLA_OBJS := \
	$(NVDLA_COMMON_OBJS) \
	port/device/nvdla_device_axi.o \
	port/fw/nvdla_fw_riscv.o \
	port/pm/nvdla_pm_hfrp.o \
	port/pm/nvdla_pm_hfrp_cmd_v1_2.o \
	port/sync/nvdla_sync_syncpt_emu.o

# Duplicate source file for forcing compilation.
NVDLA_OBJS := $(addprefix $(NVDLA_TOP)/,$(NVDLA_OBJS))
NVDLA_OBJS := $(patsubst %.o,%_$(NVDLA_MODULE_NAME).o,$(NVDLA_OBJS))

%_$(NVDLA_MODULE_NAME).c: %.c
	cp $^ $@

$(NVDLA_MODULE_NAME)-objs += $(NVDLA_OBJS)
obj-m += $(NVDLA_MODULE_NAME).o
