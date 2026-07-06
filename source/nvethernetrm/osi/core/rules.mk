###################################
# SPDX-FileCopyrightText: Copyright (c) 2019-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: MIT
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#
###############################################################################

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

ifeq ($(NV_L4T_BUILD),1)
NVETHERNETRM := $(TEGRA_TOP)/kernel/ootm/nvethernetrm
else
NVETHERNETRM := $(TEGRA_TOP)/nvethernetrm
endif

include $(NVETHERNETRM)/include/config.tmk

GLOBAL_INCLUDES += \
	$(NVETHERNETRM)/ \
	$(NVETHERNETRM)/include/ \
	$(NVETHERNETRM)/osi/common/include/ \
	$(NVETHERNETRM)/osi/core/ \
	$(NVETHERNETRM)/osi/dma/ \
	$(NVETHERNETRM)/osi/nvmacsecrm/ \
	$(NVETHERNETRM)/osi/nvxpcsrm/ \
	$(TEGRA_TOP)/fsi-internal/fw/include \

MODULE_SRCS += \
	$(NVETHERNETRM)/osi/core/osi_core.c \
	$(NVETHERNETRM)/osi/core/osi_hal.c \
	$(NVETHERNETRM)/osi/core/eqos_core.c \
	$(NVETHERNETRM)/osi/core/ivc_core.c \
	$(NVETHERNETRM)/osi/core/eqos_mmc.c \
	$(NVETHERNETRM)/osi/core/vlan_filter.c \
	$(NVETHERNETRM)/osi/core/frp.c \
	$(NVETHERNETRM)/osi/core/core_common.c \
	$(NVETHERNETRM)/osi/core/xpcs.c \
	$(NVETHERNETRM)/osi/core/mgbe_mmc.c \
	$(NVETHERNETRM)/osi/core/mgbe_core.c \
	$(NVETHERNETRM)/osi/core/common_macsec.c \
	$(NVETHERNETRM)/osi/core/debug.c

MODULE_COMPILEFLAGS += -Wno-format
MODULE_COMPILEFLAGS += -DFSI_EQOS_SUPPORT
MODULE_COMPILEFLAGS += -DOSI_DEBUG
MODULE_COMPILEFLAGS += -mgeneral-regs-only
#MODULE_COMPILEFLAGS += -DOSI_STRIPPED_LIB
#MODULE_COMPILEFLAGS += -DLOG_OSI
include make/module.mk
