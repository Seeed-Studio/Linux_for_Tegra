# SPDX-License-Identifier: GPL-2.0-only
# SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#
# Makefile for Hypervisor interface
#
ifeq ($(CONFIG_LOCALVERSION), -prod)
ccflags-y += -DCONFIG_BUILD_PROD
endif

ifneq ($(NV_OOT_TEGRA_HV_SKIP_BUILD),y)
obj-m		+= tegra_hv.o
else
obj-m		+= tegra_hv.o
tegra_hv-objs := tegra_hv_dummy.o
endif
obj-m		+= tegra_hv_pm_ctl.o
ifneq ($(NV_OOT_HVC_SYSFS_SKIP_BUILD),y)
obj-m		+= hvc_sysfs.o
endif
obj-m		+= ivc-cdev.o
obj-m		+= userspace_ivc_mempool.o
obj-m		+= tegra_hv_vcpu_yield.o

