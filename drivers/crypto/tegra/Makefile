// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

ccflags-y += -I$(srctree.nvidia)/drivers/gpu/host1x/include

tegra-se-objs := tegra-se-key.o tegra-se-main.o

tegra-se-y += tegra-se-aes.o
tegra-se-y += tegra-se-hash.o
tegra-se-y += tegra-se-sm4.o

obj-m += tegra-se.o
obj-m += tegra-se-kds.o
