# SPDX-License-Identifier: GPL-2.0-only
# SPDX-FileCopyrightText: Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES.  All rights reserved.
 
DTC_FLAGS += -@

old-dtb := $(dtb-y)
old-dtbo := $(dtbo-y)
dtb-y :=
dtbo-y :=
makefile-path := t264/nv-public/nv-platform

dtb-y += tegra264-p3834-0005+p3971-0050-qspi-nvme.dtb
dtb-y += tegra264-p3834-0008+p3971-0080-qspi-nvme.dtb
dtb-y += tegra264-p3834-0008+p3971-0089-qspi-ufs.dtb

ifneq ($(dtb-y),)
dtb-y := $(addprefix $(makefile-path)/,$(dtb-y))
endif
ifneq ($(dtbo-y),)
dtbo-y := $(addprefix $(makefile-path)/,$(dtbo-y))
endif

dtb-y += $(old-dtb)
dtbo-y += $(old-dtbo)
