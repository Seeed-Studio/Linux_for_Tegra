# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2022, NVIDIA CORPORATION.  All rights reserved.

obj-m += spi-aurix-tegra.o
ifneq ($(NV_OOT_SPI_TEGRA210_QUAD_SKIP_BUILD),y)
obj-m += spi-tegra210-quad.o
endif
obj-m += spi-tegra124-slave.o
