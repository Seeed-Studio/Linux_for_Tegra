# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES.  All rights reserved.

DTC_FLAGS += -@

old-dtb := $(dtb-y)
old-dtbo := $(dtbo-y)
dtb-y :=
dtbo-y :=
makefile-path := t23x/nv-public/overlay

dtbo-y += tegra234-audio-overlay.dtbo
dtbo-y += tegra234-carveouts.dtbo
dtbo-y += tegra234-jetson.dtbo
dtbo-y += tegra234-p3737-0000+p3701-0000.dtbo
dtbo-y += tegra234-p3740-0002+p3701-0008.dtbo
dtbo-y += tegra234-p3768-0000+p3767-0000.dtbo

ifneq ($(dtb-y),)
dtb-y := $(addprefix $(makefile-path)/,$(dtb-y))
endif
ifneq ($(dtbo-y),)
dtbo-y := $(addprefix $(makefile-path)/,$(dtbo-y))
endif

dtb-y += $(old-dtb)
dtbo-y += $(old-dtbo)
