# SPDX-License-Identifier: GPL-2.0-only
# SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#
# Makefile for Virtual Storage Driver
#

LINUX_VERSION := $(shell expr $(VERSION) \* 256 + $(PATCHLEVEL))
LINUX_VERSION_6_11 := $(shell expr 6 \* 256 + 11)

# Use dummy driver for Kernel versions greater than or equal to Linux v6.11
ifeq ($(shell test $(LINUX_VERSION) -ge $(LINUX_VERSION_6_11); echo $$?),0)
tegra_vblk-y += tegra_hv_vblk_dummy.o
else
tegra_vblk-y += tegra_hv_vblk.o
tegra_vblk-y += tegra_hv_ioctl.o
tegra_vblk-y += tegra_hv_mmc.o
tegra_vblk-y += tegra_hv_scsi.o
tegra_vblk-y += tegra_hv_ufs.o
endif
obj-m += tegra_vblk.o
