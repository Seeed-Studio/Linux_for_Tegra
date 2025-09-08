# SPDX-License-Identifier: GPL-2.0-only
# SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

LINUX_VERSION := $(shell expr $(VERSION) \* 256 + $(PATCHLEVEL))
LINUX_VERSION_6_11 := $(shell expr 6 \* 256 + 11)

# MODS is currently broken for Linux v6.11 and later
ifeq ($(shell test $(LINUX_VERSION) -lt $(LINUX_VERSION_6_11); echo $$?),0)
ifeq ($(findstring ack_src,$(NV_BUILD_KERNEL_OPTIONS)),)
obj-m += mods/
endif
endif
obj-m += nvsciipc/
ifdef CONFIG_PCI
obj-m += tegra-pcie-dma-test.o
endif
obj-m += bluedroid_pm.o
obj-m += nvscic2c-pcie/
obj-m += ioctl_example.o

obj-m += obc_cam_sync.o
