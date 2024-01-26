# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2022-2024, NVIDIA CORPORATION.  All rights reserved.

host1x-y = \
	bus.o \
	syncpt.o \
	dev.o \
	intr.o \
	cdma.o \
	channel.o \
	job.o \
	debug.o \
	mipi.o \
	fence.o \
	actmon.o \
	hw/host1x01.o \
	hw/host1x02.o \
	hw/host1x04.o \
	hw/host1x05.o \
	hw/host1x06.o \
	hw/host1x07.o \
	hw/host1x08.o

host1x-$(CONFIG_IOMMU_API) += \
	context.o

obj-m := host1x.o
