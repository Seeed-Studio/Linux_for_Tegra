# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2022-2023, NVIDIA CORPORATION.  All rights reserved.

ifndef CONFIG_TEGRA_SYSTEM_TYPE_ACK
obj-m += mods/
endif
obj-m += nvsciipc/
ifdef CONFIG_PCI
obj-m += tegra-pcie-dma-test.o
endif
obj-m += bluedroid_pm.o
obj-m += nvscic2c-pcie/
obj-m += ioctl_example.o
