# SPDX-License-Identifier: GPL-2.0-only
# SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

# MODS is currently broken for Linux v6.11 and later
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
obj-m += tegra-cec/
