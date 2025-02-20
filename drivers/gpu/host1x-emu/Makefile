# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2024-2025 NVIDIA CORPORATION.  All rights reserved.

# Enable below flag for hypervisor build configuration
ifeq ($(NV_BUILD_CONFIGURATION_EMU_SYNCPT),1)
ccflags-y += -DHOST1X_EMU_HYPERVISOR
endif

#Enable Tasklet based fence scanning in SyncpointIncr
#ccflags-y += -DHOST1X_EMU_SYNC_INC_TASKLET

# Enable KThread based fence scanning
ccflags-y += -DHOST1X_EMU_KTHREAD_FENCE_SCAN

# Enable HRTimer based fence scanning
ccflags-y += -DHOST1X_EMU_HRTIMER_FENCE_SCAN

# Enable for verification in VDK. Below allow Emulated Syncpoint driver
# to co-exits with "Host1x" driver by exporting modified kernel symbol.
ccflags-y += -DCONFIG_TEGRA_HOST1X_EMU_DBG_SYMBL

ifeq ($(NV_BUILD_CONFIGURATION_EXPOSING_T26X), 1)
# Enable debug
CONFIG_TEGRA_HOST1X_EMU_SYNCPT_DEBUG := 1
endif

LINUXINCLUDE += -I$(srctree.nvidia-oot)/drivers/gpu/host1x-emu/include
LINUXINCLUDE += -I$(srctree.nvidia-oot)/drivers/gpu/host1x-fence/include

host1x-emu-y += \
	dev.o \
	poll.o \
	ioctl.o \
	fence.o \
	nvhost.o \
	syncpt.o \
	hw/host1xEMU.o

ifdef CONFIG_TEGRA_HOST1X_EMU_SYNCPT_DEBUG
host1x-emu-y += \
	debug.o

ccflags-y += -DHOST1X_EMU_SYNCPT_DEGUB
endif

obj-m := host1x-emu.o
