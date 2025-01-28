# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2022-2025, NVIDIA CORPORATION.  All rights reserved.

ifneq ($(filter y, $(CONFIG_ARCH_TEGRA_2x_SOC) $(CONFIG_ARCH_TEGRA_3x_SOC) \
	$(CONFIG_ARCH_TEGRA_114_SOC) $(CONFIG_ARCH_TEGRA_124_SOC) $(CONFIG_ARCH_TEGRA_132_SOC) \
	$(CONFIG_ARCH_TEGRA_210_SOC) $(CONFIG_ARCH_TEGRA_186_SOC)),)
export CONFIG_HOST1X_HAVE_SYNCPT_BASE=y
subdir-ccflags-y += -DCONFIG_HOST1X_HAVE_SYNCPT_BASE
endif

ifdef CONFIG_DRM
obj-m += drm/tegra/
endif
obj-m += host1x/
obj-m += host1x-fence/
obj-m += host1x-nvhost/
