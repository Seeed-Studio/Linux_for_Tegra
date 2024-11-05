# SPDX-License-Identifier: GPL-2.0-only
# SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

LINUX_VERSION := $(shell expr $(VERSION) \* 256 + $(PATCHLEVEL))
LINUX_VERSION_6_10 := $(shell expr 6 \* 256 + 10)

ifndef CONFIG_TEGRA_SYSTEM_TYPE_ACK
ifeq ($(shell expr \( $(VERSION) \) \* 10000 + \( $(PATCHLEVEL) \) \* 100 + \( $(SUBLEVEL) \) \<= 60500), 1)
ifneq ($(NV_OOT_REALTEK_RTL8822CE_SKIP_BUILD),y)
obj-m += rtl8822ce/
endif
endif

ifneq ($(NV_OOT_REALTEK_RTL8852CE_SKIP_BUILD),y)
ifeq ($(shell test $(LINUX_VERSION) -lt $(LINUX_VERSION_6_10); echo $$?),0)
obj-m += rtl8852ce/
endif
endif
endif
