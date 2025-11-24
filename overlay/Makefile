# SPDX-License-Identifier: GPL-2.0-only
# SPDX-FileCopyrightText: Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES.  All rights reserved.

DTC_FLAGS += -@

old-dtb := $(dtb-y)
old-dtbo := $(dtbo-y)
dtb-y :=
dtbo-y :=
makefile-path := t264/nv-public/overlay

dtbo-y += tegra264-ftpm.dtbo
dtbo-y += tegra264-p3971-camera-dual-hawk-ar0234-p3762-overlay.dtbo
dtbo-y += tegra264-p3971-camera-dual-imx274-overlay.dtbo
dtbo-y += tegra264-p3971-camera-eCAM130A-overlay.dtbo
dtbo-y += tegra264-p3971-camera-e3331-overlay.dtbo
dtbo-y += tegra264-p3971-camera-e3333-overlay.dtbo
dtbo-y += tegra264-p3971-camera-imx185-overlay.dtbo
dtbo-y += tegra264-p3971-camera-imx390-overlay.dtbo
dtbo-y += tegra264-p3971-camera-imx390-addr-0x21-overlay.dtbo
dtbo-y += tegra264-p3971-camera-p3762-a00-1Hawk-overlay.dtbo
dtbo-y += tegra264-p3971-camera-p3762-a00-2Hawk-overlay.dtbo
dtbo-y += tegra264-p3971-camera-p3762-a00-3Hawk-3Owl-overlay.dtbo
dtbo-y += tegra264-p3971-camera-p3762-a00-4Hawk-overlay.dtbo
dtbo-y += tegra264-p3971-camera-p3762-a00-4Owl-overlay.dtbo
dtbo-y += tegra264-p3971-camera-p3762-a00-overlay.dtbo
dtbo-y += tegra264-p3971-0000+p3834-xxxx-hdr40.dtbo
dtbo-y += tegra264-p3971-0000+p3834-xxxx-m2ke.dtbo
dtbo-y += tegra264-p3971-0000+p3834-xxxx-csi.dtbo
dtbo-y += tegra264-p3971-0000+p3834-xxxx-audio-adafruit-sph0645lm4h.dtbo
dtbo-y += tegra264-p3971-0000+p3834-xxxx-audio-adafruit-uda1334a.dtbo
dtbo-y += tegra264-p3971-0000+p3834-xxxx-audio-fe-pi.dtbo
dtbo-y += tegra264-p3971-0000+p3834-xxxx-audio-respeaker-4-mic-array.dtbo
dtbo-y += tegra264-p3971-0000+p3834-xxxx-audio-respeaker-4-mic-lin-array.dtbo

ifneq ($(dtb-y),)
dtb-y := $(addprefix $(makefile-path)/,$(dtb-y))
endif
ifneq ($(dtbo-y),)
dtbo-y := $(addprefix $(makefile-path)/,$(dtbo-y))
endif

dtb-y += $(old-dtb)
dtbo-y += $(old-dtbo)
