# SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: GPL-2.0-only

obj-m += realtek/

# Build only for ANDROID Kernel
ifeq ($(CONFIG_TEGRA_SYSTEM_TYPE_ACK),y)
# Build only for ANDROID-U 14 Kernel
ifneq (,$(findstring ack_6.1,$(NV_BUILD_KERNEL_OPTIONS)))
obj-m += synaptics/
endif
endif
