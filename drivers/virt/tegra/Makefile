# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2022-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#
# Makefile for Hypervisor interface
#

LINUX_VERSION := $(shell expr $(VERSION) \* 256 + $(PATCHLEVEL))
LINUX_VERSION_6_2 := $(shell expr 6 \* 256 + 2)

# tegra_hv is currently broken for Linux v6.2 and so skip for Linux v6.2+
ifeq ($(shell test $(LINUX_VERSION) -lt $(LINUX_VERSION_6_2); echo $$?),0)
ifdef CONFIG_TEGRA_VIRTUALIZATION
obj-m		+= tegra_hv.o
endif
else
tegra_hv-y	= tegra-hv-dummy.o
obj-m		+= tegra_hv.o
endif
obj-m		+= tegra_hv_pm_ctl.o
obj-m		+= hvc_sysfs.o
obj-m		+= ivc-cdev.o
obj-m		+= userspace_ivc_mempool.o
obj-m		+= tegra_hv_vcpu_yield.o

