# SPDX-License-Identifier: GPL-2.0-only
# SPDX-FileCopyrightText: Copyright (c) 2023-2024, NVIDIA CORPORATION & AFFILIATES.  All rights reserved.

DTC_FLAGS += -@

old-dtb := $(dtb-y)
old-dtbo := $(dtbo-y)
dtb-y :=
dtbo-y :=
makefile-path := t23x/nv-public/nv-platform

dtb-y += tegra234-p3737-0000+p3701-0000-nv.dtb
dtb-y += tegra234-p3737-0000+p3701-0004-nv.dtb
dtb-y += tegra234-p3737-0000+p3701-0005-nv.dtb
dtb-y += tegra234-p3737-0000+p3701-0008-nv.dtb
dtb-y += tegra234-p3740-0002+p3701-0008-nv.dtb
dtb-y += tegra234-p3740-0002+p3701-0008-nv-safety.dtb
dtb-y += tegra234-p3768-0000+p3767-0000-nv.dtb
dtb-y += tegra234-p3768-0000+p3767-0000-nv-px1.dtb
dtb-y += tegra234-p3768-0000+p3767-0000-nv-taylor-high.dtb
dtb-y += tegra234-p3768-0000+p3767-0000-nv-taylor-low.dtb
dtb-y += tegra234-p3768-0000+p3767-0001-nv.dtb
dtb-y += tegra234-p3768-0000+p3767-0003-nv.dtb
dtb-y += tegra234-p3768-0000+p3767-0003-nv-super.dtb
dtb-y += tegra234-p3768-0000+p3767-0004-nv.dtb
dtb-y += tegra234-p3768-0000+p3767-0004-nv-super.dtb
dtb-y += tegra234-p3768-0000+p3767-0005-nv.dtb
dtb-y += tegra234-p3768-0000+p3767-0005-nv-super.dtb

ifneq ($(dtb-y),)
dtb-y := $(addprefix $(makefile-path)/,$(dtb-y))
endif
ifneq ($(dtbo-y),)
dtbo-y := $(addprefix $(makefile-path)/,$(dtbo-y))
endif

dtb-y += $(old-dtb)
dtbo-y += $(old-dtbo)

