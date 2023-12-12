# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2022-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

ccflags-y += -Wframe-larger-than=2048

obj-m += capture-ivc.o
obj-m += ivc-bus.o
obj-m += camchar.o
obj-m += camera-diagnostics.o
obj-m += rtcpu-debug.o
obj-m += hsp-mailbox-client.o

tegra-camera-rtcpu-objs := clk-group.o \
			   device-group.o \
			   reset-group.o \
			   rtcpu-monitor.o \
	                   tegra-camera-rtcpu-base.o \
			   tegra-rtcpu-trace.o

obj-m += tegra-camera-rtcpu.o
