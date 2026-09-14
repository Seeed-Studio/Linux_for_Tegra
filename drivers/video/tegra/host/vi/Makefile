# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2022-2023, NVIDIA CORPORATION.  All rights reserved.

LINUXINCLUDE += -DCONFIG_TEGRA_HOST1X

nvhost-vi5-objs := vi5.o
ifndef CONFIG_TEGRA_SYSTEM_TYPE_ACK
obj-m += nvhost-vi5.o
endif
