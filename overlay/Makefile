# SPDX-License-Identifier: GPL-2.0-only
# SPDX-FileCopyrightText: Copyright (c) 2023-2025, NVIDIA CORPORATION & AFFILIATES.  All rights reserved.

DTC_FLAGS += -@

old-dtb := $(dtb-y)
old-dtbo := $(dtbo-y)
dtb-y :=
dtbo-y :=
makefile-path := t23x/nv-public/overlay

dtbo-y += tegra-optee.dtbo
dtbo-y += tegra234-android.dtbo
# Build Kdump DTBOs for Android
ifeq ($(CONFIG_TEGRA_SYSTEM_TYPE_ACK),y)
dtbo-y += tegra234-android-kdump.dtbo
dtbo-y += tegra234-android-crash-kernel.dtbo
dtbo-y += tegra234-android-dummy-overlay.dtbo
endif
dtbo-y += tegra234-audio-overlay.dtbo
dtbo-y += tegra234-carveouts.dtbo
dtbo-y += tegra234-dcb-p3701-0000-hdmi.dtbo
dtbo-y += tegra234-dcb-p3767-0000-hdmi.dtbo
dtbo-y += tegra234-p3737-0000+p3701-0000-pcie-no-tra.dtbo
dtbo-y += tegra234-p3737-0000+p3701-0000-as-p3701-0004.dtbo
dtbo-y += tegra234-p3737-0000+p3701-0000-as-p3767-0000.dtbo
dtbo-y += tegra234-p3737-0000+p3701-0000-as-p3767-0001.dtbo
dtbo-y += tegra234-p3737-0000+p3701-0000-as-p3767-0003.dtbo
dtbo-y += tegra234-p3737-0000+p3701-0000-as-p3767-0004.dtbo
dtbo-y += tegra234-p3737-0000+p3701-0000-dynamic.dtbo
dtbo-y += tegra234-p3768-0000+p3767-0000-dynamic.dtbo
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
dtbo-y += tegra234-p3971-0000+p3701-0008-camera-dual-imx274-overlay.dtbo
dtbo-y += tegra234-p3971-0000+p3701-0008-camera-p3762-a00-overlay.dtbo
dtbo-y += tegra234-p3737-camera-dual-imx274-overlay.dtbo
dtbo-y += tegra234-p3737-camera-e3331-overlay.dtbo
dtbo-y += tegra234-p3737-camera-e3333-overlay.dtbo
dtbo-y += tegra234-p3737-camera-imx185-overlay.dtbo
dtbo-y += tegra234-p3737-camera-p3762-a00-overlay.dtbo
dtbo-y += tegra234-p3740-camera-p3783-a00-overlay.dtbo
dtbo-y += tegra234-p3767-camera-p3768-imx219-dual.dtbo
dtbo-y += tegra234-p3767-camera-p3768-imx477-dual.dtbo
dtbo-y += tegra234-p3767-camera-p3768-imx477-dual-4lane.dtbo
dtbo-y += tegra234-p3767-camera-p3768-imx477-imx219.dtbo
dtbo-y += tegra234-p3737-camera-eCAM130A-overlay.dtbo
dtbo-y += tegra234-p3737-camera-dual-hawk-ar0234-e3653-overlay.dtbo
dtbo-y += tegra234-p3737-camera-imx390-addr-0x21-overlay.dtbo
dtbo-y += tegra234-p3737-camera-imx390-overlay.dtbo
dtbo-y += tegra234-p3737-camera-p3762-a00-1Hawk-overlay.dtbo
dtbo-y += tegra234-p3737-camera-p3762-a00-2Hawk-overlay.dtbo
dtbo-y += tegra234-p3737-camera-p3762-a00-3Hawk-3Owl-overlay.dtbo
dtbo-y += tegra234-p3737-camera-p3762-a00-4Hawk-overlay.dtbo
dtbo-y += tegra234-p3737-camera-p3762-a00-4Owl-overlay.dtbo
dtbo-y += tegra234-p3767-camera-p3768-imx219-C.dtbo
dtbo-y += tegra234-p3767-camera-p3768-imx219-A.dtbo
dtbo-y += tegra234-p3767-camera-p3768-imx219-imx477.dtbo
dtbo-y += tegra234-p3740-0002+p3701-0008-hdr20.dtbo
dtbo-y += tegra234-p3740-0002+p3701-0008-m2ke.dtbo
dtbo-y += tegra234-p3740-0002+p3701-0008-m2kb.dtbo
dtbo-y += tegra234-p3767-camera-p3768-imx477-C.dtbo
dtbo-y += tegra234-p3767-camera-p3768-imx477-A.dtbo
dtbo-y += tegra234-p3740-0002-p3701-0008-csi.dtbo
dtbo-y += tegra234-stereolabs-zedx-j501-overlay.dtbo
dtbo-y += tegra234-stereolabs-zedx-j401-overlay.dtbo

# Seeed camera and GMSL overlays
dtbo-y += tegra234-p3767-camera-p3768-imx219-dual-seeed.dtbo
dtbo-y += tegra234-p3767-camera-p3768-imx219-quad-seeed.dtbo
dtbo-y += tegra234-p3767-camera-p3768-imx477-dual-seeed.dtbo
dtbo-y += tegra234-p3767-camera-p3768-imx477-quad-seeed.dtbo
dtbo-y += tegra234-p3767-camera-p3768-imx219-imx477-quad-seeed.dtbo
dtbo-y += tegra234-p3737-camera-g300-fg96-overlay.dtbo
dtbo-y += tegra234-p3737-camera-g300-nomtd-fg96-overlay.dtbo
dtbo-y += tegra234-p3737-camera-g300-leopard-overlay.dtbo
dtbo-y += tegra234-p3767-camera-p3768-g300-fg96-overlay.dtbo
dtbo-y += tegra234-p3737-camera-g300-mic-fg-8g-overlay.dtbo
dtbo-y += tegra234-seeed-gmsl1x4-3g-overlay.dtbo
dtbo-y += tegra234-seeed-gmsl1x4-6g-overlay.dtbo
dtbo-y += tegra234-seeed-gmsl2x4-3g-overlay.dtbo
dtbo-y += tegra234-seeed-gmsl2x1x4-3g-overlay.dtbo
dtbo-y += tegra234-seeed-gmsl2x1x4-6g-overlay.dtbo
dtbo-y += tegra234-seeed-gmsl-recomputer-robo-3g-overlay.dtbo
dtbo-y += tegra234-seeed-gmsl-recomputer-robo-6g-overlay.dtbo

# Board-specific overlay: inject #define based on CONFIG variable
DTS_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
DTS_FILE := $(DTS_DIR)/tegra234-seeed-gmsl2x4-orbbec-335lg-overlay.dts

ifneq ($(CONFIG),)
$(info CONFIG raw value: '$(CONFIG)')
CONFIG_UPPERCASE := $(shell echo $(CONFIG) | tr 'a-z-' 'A-Z_')
$(info Injecting: #define CONFIG_$(CONFIG_UPPERCASE))
$(shell sed -i 's|/\* BOARD_CONFIG_MARKER.*\*/|#define CONFIG_$(CONFIG_UPPERCASE)|' "$(DTS_FILE)")
else
$(info CONFIG not set, using default configuration)
$(shell sed -i 's|^#define CONFIG_RECOMPUTER.*|/* BOARD_CONFIG_MARKER - Do not modify, auto-generated by Makefile */|' "$(DTS_FILE)")
endif

dtbo-y += tegra234-seeed-orbbec-335lg-overlay.dtbo
dtbo-y += tegra234-seeed-gmsl2x4-orbbec-335lg-overlay.dtbo

ifneq ($(dtb-y),)
dtb-y := $(addprefix $(makefile-path)/,$(dtb-y))
endif
ifneq ($(dtbo-y),)
dtbo-y := $(addprefix $(makefile-path)/,$(dtbo-y))
endif

dtb-y += $(old-dtb)
dtbo-y += $(old-dtbo)
