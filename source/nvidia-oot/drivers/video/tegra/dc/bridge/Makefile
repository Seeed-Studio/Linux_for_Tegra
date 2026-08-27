# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2022-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

obj-m += maxim_gmsl_dp_serializer.o
obj-m += maxim_gmsl_hdmi_serializer.o
obj-m += ti_fpdlink_dp_serializer.o

nvdisp_serdes-objs := nvdisp_serdes_common.o \
                   nvdisp_serdes_vendor_extension.o \
                   nvdisp_serdes_ti_extension.o \
                   nvdisp_serdes_maxim_extension.o
obj-m += nvdisp_serdes.o

