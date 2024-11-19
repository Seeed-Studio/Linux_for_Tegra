# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2022-2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#
# Makefile for NvSciIpc driver
#

ccflags-y += -I$(PWD)

ifneq ($(NV_CUSTOMER_BUILD), 1)
ifdef TEGRA_TOP
  ccflags-y += -I$(TEGRA_TOP)/gpu/drv/drivers/nvsci/inc/internal
endif
ifdef PWD
  ccflags-y += -I$(PWD)/gpu/drv/drivers/nvsci/inc/internal
endif
else
ccflags-y += -I$(TEGRA_TOP)/prebuilt/generic-aaos-pdk/gpu/drv/drivers/nvsci/inc/internal
endif

obj-m += nvsciipc.o
