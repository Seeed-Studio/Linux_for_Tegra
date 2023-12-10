# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

GCOV_PROFILE := y

LINUX_VERSION := $(shell expr $(VERSION) \* 256 + $(PATCHLEVEL))
LINUX_VERSION_6_3 := $(shell expr 6 \* 256 + 3)

ifneq ($(CONFIG_TEGRA_GPIO_LEGACY_DISABLE),y)
obj-m	+= isc_gpio.o
endif
obj-m	+= isc_mgr.o
obj-m	+= isc_dev.o
obj-m	+= isc_pwm.o
