# Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-2-Clause

global-incdirs-y += include
cflags-y += -I$(TA_DEV_KIT_DIR)/host_include
srcs-y += ftpm_helper_ta.c
