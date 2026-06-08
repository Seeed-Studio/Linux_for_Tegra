# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2022-2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

tegra-bootloader-debug-objs := tegra_bootloader_debug.o
obj-m += tegra-bootloader-debug.o

obj-m += tegra-cactmon-mc-all.o
ifdef CONFIG_PM_DEVFREQ
obj-m += tegra-bpmp-bwmgr.o
endif
obj-m += tegra-fsicom.o

obj-m += mce/
ifdef CONFIG_PERF_EVENTS
obj-m += uncore_pmu/
endif
obj-m += mc-hwpm.o
obj-m += mc-utils/
obj-m += dce/
obj-m += psc/
ifndef CONFIG_TEGRA_SYSTEM_TYPE_ACK
obj-m += rtcpu/
endif
obj-m += aon/
obj-m += tegra-uss-io-proxy.o
obj-m += nvadsp/
obj-m += tegra-hv-xhci-debug.o
obj-m += tegra-hv-xhci.o
