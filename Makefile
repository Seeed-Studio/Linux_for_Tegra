# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2022-2023, NVIDIA CORPORATION.  All rights reserved.

LINUXINCLUDE += -I$(srctree.nvconftest)
LINUXINCLUDE += -I$(srctree.nvidia-oot)/include

subdir-ccflags-y += -Werror

LINUX_VERSION := $(shell expr $(VERSION) \* 256 + $(PATCHLEVEL))
LINUX_VERSION_6_2 := $(shell expr 6 \* 256 + 2)
LINUX_VERSION_6_3 := $(shell expr 6 \* 256 + 3)

# The Tegra IVC driver was updated to support iosys-map in Linux v6.2.
# For Linux v6.2 kernels, don't build any drivers that requires this.
ifeq ($(shell test $(LINUX_VERSION) -ge $(LINUX_VERSION_6_2); echo $$?),0)
export CONFIG_TEGRA_IVC_LEGACY_DISABLE=y
endif

ifeq ($(CONFIG_TEGRA_IVC_LEGACY_DISABLE),y)
subdir-ccflags-y += -DCONFIG_TEGRA_IVC_LEGACY_DISABLE
endif

# Legacy GPIO support is removed in Linux v6.3
ifeq ($(shell test $(LINUX_VERSION) -ge $(LINUX_VERSION_6_3); echo $$?),0)
export CONFIG_TEGRA_GPIO_LEGACY_DISABLE=y
endif

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
