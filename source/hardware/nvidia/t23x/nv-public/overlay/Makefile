# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES.  All rights reserved.

DTC_FLAGS += -@

old-dtb := $(dtb-y)
old-dtbo := $(dtbo-y)
dtb-y :=
dtbo-y :=
makefile-path := t23x/nv-public/overlay

dtbo-y += tegra-optee.dtbo
dtbo-y += tegra234-audio-overlay.dtbo
dtbo-y += tegra234-carveouts.dtbo
dtbo-y += tegra234-jetson.dtbo
dtbo-y += tegra234-p3737-0000+p3701-0000.dtbo
dtbo-y += tegra234-p3740-0002+p3701-0008.dtbo
dtbo-y += tegra234-p3740-0002+p3701-0008-safety.dtbo
dtbo-y += tegra234-p3768-0000+p3767-0000.dtbo
dtbo-y += tegra234-p3737-0000+p3701-0000-audio-adafruit-uda1334a.dtbo
dtbo-y += tegra234-p3737-0000+p3701-0000-audio-adafruit-sph0645lm4h.dtbo
dtbo-y += tegra234-p3737-0000+p3701-0000-audio-fe-pi.dtbo
dtbo-y += tegra234-p3737-0000+p3701-0000-audio-respeaker-4-mic-array.dtbo
dtbo-y += tegra234-p3737-0000+p3701-0000-audio-respeaker-4-mic-lin-array.dtbo
dtbo-y += tegra234-p3737-0000+p3701-0000-csi.dtbo
dtbo-y += tegra234-p3737-0000+p3701-0000-hdr40.dtbo
dtbo-y += tegra234-p3737-0000+p3701-0000-m2ke.dtbo
dtbo-y += tegra234-p3767-0000+p3509-a02-audio-adafruit-sph0645lm4h.dtbo
dtbo-y += tegra234-p3767-0000+p3509-a02-audio-adafruit-uda1334a.dtbo
dtbo-y += tegra234-p3767-0000+p3509-a02-audio-fe-pi.dtbo
dtbo-y += tegra234-p3767-0000+p3509-a02-audio-respeaker-4-mic-array.dtbo
dtbo-y += tegra234-p3767-0000+p3509-a02-audio-respeaker-4-mic-lin-array.dtbo
dtbo-y += tegra234-p3767-0000+p3509-a02-csi.dtbo
dtbo-y += tegra234-p3767-0000+p3509-a02-hdr40.dtbo
dtbo-y += tegra234-p3767-0000+p3509-a02-m2ke.dtbo
dtbo-y += tegra234-p3767-0000+p3768-0000-csi.dtbo
dtbo-y += tegra234-p3740-0002+p3701-0008-hdr20.dtbo
dtbo-y += tegra234-p3740-0002+p3701-0008-m2ke.dtbo
dtbo-y += tegra234-p3740-0002+p3701-0008-m2kb.dtbo
dtbo-y += tegra234-p3737-camera-dual-imx274-overlay.dtbo
dtbo-y += tegra234-p3737-camera-e3331-overlay.dtbo
dtbo-y += tegra234-p3737-camera-e3333-overlay.dtbo
dtbo-y += tegra234-p3737-camera-imx185-overlay.dtbo
dtbo-y += tegra234-p3767-camera-p3768-imx219-dual.dtbo
dtbo-y += tegra234-p3767-camera-p3768-imx477-dual.dtbo
dtbo-y += tegra234-p3767-camera-p3768-imx477-dual-4lane.dtbo
dtbo-y += tegra234-p3767-camera-p3768-imx477-imx219.dtbo

ifneq ($(dtb-y),)
dtb-y := $(addprefix $(makefile-path)/,$(dtb-y))
endif
ifneq ($(dtbo-y),)
dtbo-y := $(addprefix $(makefile-path)/,$(dtbo-y))
endif

dtb-y += $(old-dtb)
dtbo-y += $(old-dtbo)
