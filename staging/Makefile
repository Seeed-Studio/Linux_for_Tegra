# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES.  All rights reserved.

DTC_FLAGS += -@

old-dtb := $(dtb-y)
old-dtbo := $(dtbo-y)
dtb-y :=
dtbo-y :=
makefile-path := t23x/nv-public/staging

dtb-y += tegra234-p3737-0000+p3701-0004.dtb
dtb-y += tegra234-p3737-0000+p3701-0005.dtb
dtb-y += tegra234-p3737-0000+p3701-0008.dtb
dtb-y += tegra234-p3768-0000+p3767-0001.dtb
dtb-y += tegra234-p3768-0000+p3767-0003.dtb
dtb-y += tegra234-p3768-0000+p3767-0004.dtb
dtb-y += tegra234-p3768-0000+p3767-0005.dtb

ifneq ($(dtb-y),)
dtb-y := $(addprefix $(makefile-path)/,$(dtb-y))
endif
ifneq ($(dtbo-y),)
dtbo-y := $(addprefix $(makefile-path)/,$(dtbo-y))
endif

dtb-y += $(old-dtb)
dtbo-y += $(old-dtbo)
