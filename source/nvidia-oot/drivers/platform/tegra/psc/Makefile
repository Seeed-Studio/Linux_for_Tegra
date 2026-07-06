# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2020-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

GCOV_PROFILE := y

ccflags-y += -Werror

obj-m += tegra23x_psc.o
tegra23x_psc-objs := tegra23x_psc_mailbox.o

tegra23x_psc-$(CONFIG_DEBUG_FS) += psc_debug.o
