# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#
# Always On Sensor Processing Engine code.
#
GCOV_PROFILE := y

LINUXINCLUDE += -I$(srctree.nvidia-oot)/drivers/platform/tegra/aon/include

ccflags-y += -Werror

obj-m += tegra234-aon.o
obj-m += tegra-aon-ivc-echo.o

tegra234-aon-objs += \
    tegra-aon-hsp.o \
    ../tegra-ivc.o \
    tegra-aon-mail.o \
    tegra-aon-module.o \
    aon-hsp-mbox-client.o

ifeq ($(CONFIG_DEBUG_FS), y)
tegra234-aon-objs += tegra-aon-debug.o
endif
