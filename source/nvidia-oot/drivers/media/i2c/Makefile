# SPDX-FileCopyrightText: Copyright (c) 2023-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: GPL-2.0-only
#
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
#
# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.

subdir-ccflags-y += -Werror
ifneq ($(NV_BUILD_SYSTEM_TYPE),embedded-linux)
obj-m += max9295.o
obj-m += max9296.o
## orbbec max9296/max96712+ max9295d + G300
obj-m += obc_max9296.o
obj-m += obc_max96712.o
subdir-ccflags-y += -DCONFIG_TEGRA_CAMERA_PLATFORM
obj-m += g300.o
g300-objs := obc_g300_seri.o obc_g300_ctrl.o obc_g300.o
ifndef CONFIG_TEGRA_SYSTEM_TYPE_ACK
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
endif
