# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2022-2024, NVIDIA CORPORATION.  All rights reserved.

obj-m += nvidia/

ifdef CONFIG_PCI
# Build mft, bluefield3 modules only when mlnx ofed driver is built
ifeq ($(NV_BUILD_MODULE_DRV_MLNX_MST),1)
obj-m += mft/
endif
obj-m += marvell/
obj-m += microchip/
obj-m += realtek/
endif
