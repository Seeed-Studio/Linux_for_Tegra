# SPDX-License-Identifier: GPL-2.0-only
# SPDX-FileCopyrightText: Copyright (c) 2022-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
LINUXINCLUDE += -I$(srctree.nvidia-oot)/drivers/media/platform/tegra

nvhost-nvcsi-objs  := nvcsi.o

ifneq ($(NV_BUILD_SYSTEM_TYPE),embedded-linux)
ifndef CONFIG_TEGRA_SYSTEM_TYPE_ACK
nvhost-nvcsi-objs += nvhost-csi.o
endif
endif

obj-m += nvhost-nvcsi.o