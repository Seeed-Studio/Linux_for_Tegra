#
# SPDX-FileCopyrightText: Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: GPL-2.0-only
#

GCOV_PROFILE := y

ifdef CONFIG_TEGRA_GRHOST
ccflags-y += -DCONFIG_TEGRA_NVDLA_CHANNEL
endif

ccflags-y += -Werror
ccflags-y += -DCONFIG_TEGRA_HOST1X

NVDLA_COMMON_OBJS := \
		nvdla.o \
		nvdla_buffer.o \
		nvdla_ioctl.o \
		dla_queue.o \
		nvdla_queue.o \
		nvdla_debug.o
