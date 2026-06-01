# SPDX-License-Identifier: GPL-2.0
# SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
subdir-ccflags-y	= -Werror
ifneq ($(findstring debug,$(NV_BUILD_KERNEL_OPTIONS)),)
subdir-ccflags-y += -DEP_CEC_DEBUG
endif

obj-m += ep_cec.o
