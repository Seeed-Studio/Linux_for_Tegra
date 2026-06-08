# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES.  All rights reserved.

ufs-tegra-objs := ufs-tegra-common.o
ufs-tegra-objs += ufs-provision-dummy.o
obj-m += ufs-tegra.o

ifdef CONFIG_SCSI_UFSHCD_QUERY_EXPORT
ufs-tegra-provision-objs := ufs-tegra-common.o
ufs-tegra-provision-objs += ufs-provision.o
obj-m += ufs-tegra-provision.o
endif
