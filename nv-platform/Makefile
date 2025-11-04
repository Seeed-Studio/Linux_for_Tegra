# SPDX-License-Identifier: GPL-2.0-only
# SPDX-FileCopyrightText: Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES.  All rights reserved.

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
dtb-y += tegra264-p3740-0002+p3834-0008-qspi-nv-safety.dtb
dtb-y += tegra264-p3971-0080+p3834-0000-qspi-nvme.dtb
dtb-y += tegra264-p4071-0000+p3834-0000-nv.dtb
dtb-y += tegra264-p4071-0000+p3834-0008-nv.dtb
dtb-y += tegra264-p4071-0008+p3834-0008-nv.dtb
dtb-y += tegra264-p4071-0008+p3834-0008-nv-safety.dtb
dtbo-y += tegra264-p3740-0002+p3834-0008-pcie-disable-in-uefi-dynamic.dtbo
dtbo-y += tegra264-p3971-0000+p3834-xxxx-dynamic.dtbo
dtbo-y += tegra264-p4071-0000+p3834-xxxx-dynamic.dtbo
dtbo-y += tegra264-android.dtbo
dtbo-y += tegra264-ufs-provision.dtbo

ifneq ($(dtb-y),)
dtb-y := $(addprefix $(makefile-path)/,$(dtb-y))
endif
ifneq ($(dtbo-y),)
dtbo-y := $(addprefix $(makefile-path)/,$(dtbo-y))
endif

dtb-y += $(old-dtb)
dtbo-y += $(old-dtbo)
