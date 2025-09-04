# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2020-2022, NVIDIA CORPORATION. All rights reserved.
#

global-incdirs-y += .
srcs-y += main.c

ifneq ("$(wildcard $(NV_OPTEE_DIR))","")
subdirs_ext-y += $(NV_OPTEE_DIR)/$(platform-dir)
endif
