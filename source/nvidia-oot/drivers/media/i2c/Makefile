# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2023-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

subdir-ccflags-y += -Werror

obj-m += max9295.o
obj-m += max9296.o

## orbbec max9296/max96712+ max9295d + G300
obj-m += obc_max9296.o
obj-m += obc_max96712.o
subdir-ccflags-y += -DCONFIG_TEGRA_CAMERA_PLATFORM
obj-m += g300.o
g300-objs := obc_g300_seri.o obc_g300_ctrl.o obc_g300.o

ifeq ($(findstring ack_src,$(NV_BUILD_KERNEL_OPTIONS)),)
obj-m += max96712.o

ifdef CONFIG_MEDIA_SUPPORT
obj-m += ar1335_common.o
obj-m += lt6911uxc.o
obj-m += nv_imx185.o
obj-m += nv_imx219.o
obj-m += nv_imx274.o
obj-m += nv_imx318.o
obj-m += nv_imx390.o
obj-m += nv_imx477.o
obj-m += nv_ov5693.o
obj-m += nv_ar0234.o
obj-m += nv_hawk_owl.o
obj-m += max929x.o
obj-m += nv_cam.o
obj-m += maxim-serdes/
endif

obj-m += pca9570.o
obj-m += virtual_i2c_mux.o
endif
