# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2022-2024, NVIDIA CORPORATION.  All rights reserved.


LINUX_VERSION := $(shell expr $(VERSION) \* 256 + $(PATCHLEVEL))
LINUX_VERSION_5_14 := $(shell expr 5 \* 256 + 14)

# Do not build driver for K5.14
ifneq ($(shell test $(LINUX_VERSION) -eq $(LINUX_VERSION_5_14); echo $$?),0)
ifndef CONFIG_TEGRA_SYSTEM_TYPE_ACK
ifeq ($(shell expr \( $(VERSION) \) \* 10000 + \( $(PATCHLEVEL) \) \* 100 + \( $(SUBLEVEL) \) \<= 60500), 1)
obj-m += rtl8822ce/
endif
endif
endif
