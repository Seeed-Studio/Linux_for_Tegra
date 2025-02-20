# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2022-2023, NVIDIA CORPORATION.  All rights reserved.
#
# Tegra Host1x-fence Driver.

ccflags-y += -DCONFIG_TEGRA_HOST1X
ccflags-y += -Werror

LINUXINCLUDE += -I$(srctree.nvidia-oot)/drivers/gpu/host1x-fence/include

host1x-fence-objs += \
	dev.o \
	dispatch.o

obj-m += host1x-fence.o
