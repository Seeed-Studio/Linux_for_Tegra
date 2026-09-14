# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2022-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.


ifdef CONFIG_V4L2_ASYNC
subdir-ccflags-y += -DCONFIG_V4L2_ASYNC
endif

ifdef CONFIG_V4L2_FWNODE
subdir-ccflags-y += -DCONFIG_V4L2_FWNODE
endif

ifdef CONFIG_VIDEOBUF2_DMA_CONTIG
subdir-ccflags-y += -DCONFIG_VIDEOBUF2_DMA_CONTIG
endif

ifdef CONFIG_I2C
obj-m += i2c/
obj-m += platform/
endif
