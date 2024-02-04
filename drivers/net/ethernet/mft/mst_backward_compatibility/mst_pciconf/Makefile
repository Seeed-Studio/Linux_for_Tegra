# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2024, NVIDIA CORPORATION.  All rights reserved.

mkfile_path := $(abspath $(lastword $(MAKEFILE_LIST)))
current_dir := $(shell dirname $(mkfile_path))
NNT_DRIVER_LOCATION := nnt_driver
NNT_DRIVER_ABS_LOCATION := $(current_dir)/$(NNT_DRIVER_LOCATION)

obj-m += mst_pciconf.o
ccflags-y := -I$(NNT_DRIVER_ABS_LOCATION)
mst_pciconf-y := $(NNT_DRIVER_LOCATION)/nnt_device.o $(NNT_DRIVER_LOCATION)/nnt_dma.o $(NNT_DRIVER_LOCATION)/nnt_pci_conf_access.o \
		 			$(NNT_DRIVER_LOCATION)/nnt_pci_conf_access_no_vsec.o $(NNT_DRIVER_LOCATION)/nnt_memory_access.o \
					$(NNT_DRIVER_LOCATION)/nnt_ioctl.o mst_pciconf_bc.o
