# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2022-2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#
# Display Controller Engine code.
#

GCOV_PROFILE := y

ifeq ($(CONFIG_TEGRA_OOT_MODULE),m)
CONFIG_TEGRA_DCE := m
else # CONFIG_TEGRA_OOT_MODULE
CONFIG_TEGRA_DCE := y
endif # CONFIG_TEGRA_OOT_MODULE

ccflags-y += -I$(srctree.nvidia-oot)/include
ccflags-y += -I$(srctree.nvidia-oot)/drivers/platform/tegra/dce/include
ccflags-y += -I$(srctree.nvidia-oot)/drivers/platform/tegra/dce/os/include
ccflags-y += -I$(srctree.nvidia-oot)/drivers/platform/tegra/dce/os/interface/include
ccflags-y += -I$(srctree.nvidia-oot)/drivers/platform/tegra/dce/os/include/linux
obj-$(CONFIG_TEGRA_DCE) += tegra-dce.o

tegra-dce-$(CONFIG_TEGRA_DCE) += dce-ast.o
tegra-dce-$(CONFIG_TEGRA_DCE) += dce-reset.o
tegra-dce-$(CONFIG_TEGRA_DCE) += dce-hsp-smb.o
tegra-dce-$(CONFIG_TEGRA_DCE) += dce-hsp-ss.o
tegra-dce-$(CONFIG_TEGRA_DCE) += dce-worker.o
tegra-dce-$(CONFIG_TEGRA_DCE) += dce-fsm.o
tegra-dce-$(CONFIG_TEGRA_DCE) += dce-init-deinit.o
tegra-dce-$(CONFIG_TEGRA_DCE) += dce-mailbox.o
tegra-dce-$(CONFIG_TEGRA_DCE) += dce-bootstrap.o
tegra-dce-$(CONFIG_TEGRA_DCE) += dce-admin.o
tegra-dce-$(CONFIG_TEGRA_DCE) += dce-ipc.o
tegra-dce-$(CONFIG_TEGRA_DCE) += dce-ipc-signal.o
tegra-dce-$(CONFIG_TEGRA_DCE) += dce-client-ipc.o
tegra-dce-$(CONFIG_TEGRA_DCE) += dce-module.o
tegra-dce-$(CONFIG_TEGRA_DCE) += dce-pm.o
tegra-dce-$(CONFIG_TEGRA_DCE) += dce-util-common.o
tegra-dce-$(CONFIG_TEGRA_DCE) += dce-hsp-smb-t264.o
tegra-dce-$(CONFIG_TEGRA_DCE) += dce-hsp-ss-t264.o

ifeq ($(CONFIG_DEBUG_FS),y)
tegra-dce-$(CONFIG_TEGRA_DCE) += dce-debug.o
tegra-dce-$(CONFIG_TEGRA_DCE) += dce-admin-debug.o
tegra-dce-$(CONFIG_TEGRA_DCE) += dce-debug-perf.o
endif
