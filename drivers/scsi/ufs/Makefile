# SPDX-License-Identifier: GPL-2.0-only
# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

ufs-tegra-objs := ufs-tegra-common.o
ufs-tegra-objs += ufs-provision-dummy.o
obj-m += ufs-tegra.o

ifndef CONFIG_TEGRA_SYSTEM_TYPE_ACK
ufs-tegra-provision-objs := ufs-tegra-common.o
ufs-tegra-provision-objs += ufs-provision.o
obj-m += ufs-tegra-provision.o
endif
