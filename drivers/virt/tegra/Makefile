# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2022-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#
# Makefile for Hypervisor interface
#
ifdef CONFIG_TEGRA_VIRTUALIZATION
ifneq ($(NV_OOT_TEGRA_HV_SKIP_BUILD),y)
obj-m		+= tegra_hv.o
else
obj-m		+= tegra_hv.o
tegra_hv-objs := tegra_hv_dummy.o
endif
endif
obj-m		+= tegra_hv_pm_ctl.o
obj-m		+= hvc_sysfs.o
obj-m		+= ivc-cdev.o
obj-m		+= userspace_ivc_mempool.o
obj-m		+= tegra_hv_vcpu_yield.o

