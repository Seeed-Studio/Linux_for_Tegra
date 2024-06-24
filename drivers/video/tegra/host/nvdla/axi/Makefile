#
# SPDX-FileCopyrightText: Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#

NVDLA_TOP := ../
include $(src)/../Makefile.config.mk

NVDLA_COMMON_OBJS := $(addprefix $(NVDLA_TOP)/,$(NVDLA_COMMON_OBJS))

#
# axi-nvdla.ko
#
ccflags-y += -DNVDLA_HAVE_CONFIG_HW_PERFMON=0
ccflags-y += -DNVDLA_HAVE_CONFIG_AXI=1
ccflags-y += -DNVDLA_HAVE_CONFIG_SYNCPTFD=0
ccflags-y += -DNVDLA_HAVE_CONFIG_HSIERRINJ=0

axi-nvdla-objs += \
		$(NVDLA_COMMON_OBJS) \
		$(NVDLA_TOP)/port/device/nvdla_device_axi.o \
		$(NVDLA_TOP)/port/fw/nvdla_fw_riscv.o \
		$(NVDLA_TOP)/port/sync/nvdla_sync_stub.o

obj-m += axi-nvdla.o
