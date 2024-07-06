# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2022-2024, NVIDIA CORPORATION.  All rights reserved.

ifeq ($(CONFIG_TEGRA_KLEAF_BUILD),y)
include $(abspath $(shell dirname $(lastword $(MAKEFILE_LIST))))/Makefile.kleaf
endif

# Include kernel specific config
ifneq ($(kernel_name),)
kernel_config := $(abspath $(shell dirname $(lastword $(MAKEFILE_LIST))))/configs/Makefile.config.$(kernel_name)
ifneq ($(wildcard $(kernel_config)),)
include $(kernel_config)
endif
endif

# Include system specific config
ifneq ($(system_type),)
system_config := $(abspath $(shell dirname $(lastword $(MAKEFILE_LIST))))/configs/Makefile.config.$(system_type)
ifneq ($(wildcard $(system_config)),)
include $(system_config)
endif
endif

LINUXINCLUDE += -I$(srctree.nvconftest)
LINUXINCLUDE += -I$(srctree.nvidia-oot)/include

subdir-ccflags-y += -Werror
subdir-ccflags-y += -Wmissing-prototypes

ifeq ($(CONFIG_TEGRA_VIRTUALIZATION),y)
subdir-ccflags-y += -DCONFIG_TEGRA_VIRTUALIZATION
endif

ifeq ($(CONFIG_TEGRA_SYSTEM_TYPE_ACK),y)
subdir-ccflags-y += -DCONFIG_TEGRA_SYSTEM_TYPE_ACK
subdir-ccflags-y += -Wno-sometimes-uninitialized
subdir-ccflags-y += -Wno-parentheses-equality
subdir-ccflags-y += -Wno-enum-conversion
subdir-ccflags-y += -Wno-implicit-fallthrough
endif

obj-m += drivers/

ifdef CONFIG_SND_SOC
obj-m += sound/soc/tegra/
obj-m += sound/tegra-safety-audio/
obj-m += sound/soc/tegra-virt-alt/
endif
