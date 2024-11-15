#
# SPDX-FileCopyrightText: Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
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

#
# axi-nvdla.ko
#
ccflags-y += -DNVDLA_HAVE_CONFIG_HW_PERFMON=0
ccflags-y += -DNVDLA_HAVE_CONFIG_AXI=1
ccflags-y += -DNVDLA_HAVE_CONFIG_SYNCPTFD=0
ccflags-y += -DNVDLA_HAVE_CONFIG_HSIERRINJ=0
ccflags-y += -DBUG_4942853=1

NVDLA_OBJS := \
	$(NVDLA_COMMON_OBJS) \
	port/device/nvdla_device_axi.o \
	port/fw/nvdla_fw_riscv.o \
	port/sync/nvdla_sync_stub.o

# Duplicate source file for forcing compilation.
NVDLA_OBJS := $(addprefix $(NVDLA_TOP)/,$(NVDLA_OBJS))
NVDLA_OBJS := $(patsubst %.o,%_$(NVDLA_MODULE_NAME).o,$(NVDLA_OBJS))

%_$(NVDLA_MODULE_NAME).c: %.c
	cp $^ $@

$(NVDLA_MODULE_NAME)-objs += $(NVDLA_OBJS)
obj-m += $(NVDLA_MODULE_NAME).o
