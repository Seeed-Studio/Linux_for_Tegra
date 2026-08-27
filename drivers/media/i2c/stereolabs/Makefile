# SPDX-License-Identifier: GPL-2.0
# Stereolabs ZED X / ZED X One GS GMSL camera drivers.
# Built as part of nvidia-oot; tegra-camera symbol dependencies are resolved
# within this same tree, so no KBUILD_EXTRA_SYMBOLS is needed.

export CONFIG_SL_DESER_MAX96712=m
export CONFIG_SL_SER_MAX9295=m
export CONFIG_SL_VIDEO_ZEDX=m

subdir-ccflags-y += -Werror
subdir-ccflags-y += -Wmissing-prototypes
subdir-ccflags-y += -DCONFIG_SL_DESER_MAX96712

obj-m += zedx/
obj-m += max96712/
obj-m += max9295/
