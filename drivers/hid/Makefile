# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2025, NVIDIA CORPORATION.  All rights reserved.

ccflags-y += -I $(srctree)/drivers/hid

ifdef CONFIG_USB_HID
obj-m += hid-jarvis-remote.o
hid-jarvis-remote-objs := hid-atv-jarvis.o sbcdec.o
endif
