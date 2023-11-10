# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2022-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#
# Makefile for Hypervisor interface
#
ifdef CONFIG_TEGRA_VIRTUALIZATION
obj-m		+= tegra_hv.o
endif
obj-m		+= tegra_hv_pm_ctl.o
obj-m		+= hvc_sysfs.o
obj-m		+= ivc-cdev.o
obj-m		+= userspace_ivc_mempool.o
obj-m		+= tegra_hv_vcpu_yield.o

