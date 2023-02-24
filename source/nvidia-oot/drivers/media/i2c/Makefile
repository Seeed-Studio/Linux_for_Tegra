# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

subdir-ccflags-y += -Werror

obj-m += max9295.o
obj-m += max9296.o
obj-m += max96712.o
ifeq ($(findstring ack_src,$(NV_BUILD_KERNEL_OPTIONS)),)
obj-m += ar1335_common.o
obj-m += lt6911uxc.o
obj-m += nv_imx185.o
obj-m += nv_imx219.o
obj-m += nv_imx274.o
obj-m += nv_imx318.o
obj-m += nv_imx390.o
obj-m += nv_ov5693.o
obj-m += nv_ar0234.o
obj-m += pca9570.o
endif
