# SPDX-License-Identifier: GPL-2.0-only
# SPDX-FileCopyrightText: Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES.  All rights reserved.

DTC_FLAGS += -@

old-dtb := $(dtb-y)
old-dtbo := $(dtbo-y)
dtb-y :=
dtbo-y :=
makefile-path := t264/nv-public/nv-platform

dtb-y += tegra264-p3971-0050+p3834-0005-qspi-nvme.dtb
dtb-y += tegra264-p3971-0080+p3834-0008-qspi-nvme.dtb
dtb-y += tegra264-p3971-0089+p3834-0008-qspi-ufs.dtb
dtb-y += tegra264-p3740-0002+p3834-0008-qspi-nvme.dtb
dtbo-y += tegra264-p3971-0050+p3834-0005-qspi-nvme-dynamic.dtbo
dtbo-y += tegra264-p3971-0080+p3834-0008-qspi-nvme-dynamic.dtbo
dtbo-y += tegra264-p3971-0089+p3834-0008-qspi-ufs-dynamic.dtbo
dtbo-y += tegra264-android.dtbo

ifneq ($(dtb-y),)
dtb-y := $(addprefix $(makefile-path)/,$(dtb-y))
endif
ifneq ($(dtbo-y),)
dtbo-y := $(addprefix $(makefile-path)/,$(dtbo-y))
endif

dtb-y += $(old-dtb)
dtbo-y += $(old-dtbo)
