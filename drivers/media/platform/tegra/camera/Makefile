# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2022-2024, NVIDIA CORPORATION.  All rights reserved.

ifdef CONFIG_MEDIA_SUPPORT
ifndef CONFIG_TEGRA_SYSTEM_TYPE_ACK
LINUXINCLUDE += -I$(srctree.nvidia-oot)/drivers/video/tegra/camera
LINUXINCLUDE += -I$(srctree.nvidia-oot)/drivers/media/platform/tegra
LINUXINCLUDE += -DCONFIG_TEGRA_HOST1X
LINUXINCLUDE += -Werror

tegra-camera-objs := regmap_util.o
tegra-camera-objs += camera_common.o
tegra-camera-objs += camera_gpio.o
tegra-camera-objs += sensor_common.o
tegra-camera-objs += camera_version_utils.o
tegra-camera-objs += nvcamera_log.o
tegra-camera-objs += tegracam_v4l2.o
tegra-camera-objs += tegracam_core.o
tegra-camera-objs += tegracam_ctrls.o
tegra-camera-objs += tegracam_utils.o
tegra-camera-objs += vi/vi5_fops.o
tegra-camera-objs += vi/mc_common.o
tegra-camera-objs += vi/graph.o
tegra-camera-objs += vi/channel.o
tegra-camera-objs += vi/core.o
tegra-camera-objs += csi/csi.o
tegra-camera-objs += nvcsi/csi5_fops.o
tegra-camera-objs += fusa-capture/capture-vi.o
tegra-camera-objs += fusa-capture/capture-common.o
tegra-camera-objs += fusa-capture/capture-vi-channel.o
tegra-camera-objs += fusa-capture/capture-isp-channel.o
tegra-camera-objs += fusa-capture/capture-isp.o
obj-m += tegra-camera.o
obj-m += tests/
endif
endif
