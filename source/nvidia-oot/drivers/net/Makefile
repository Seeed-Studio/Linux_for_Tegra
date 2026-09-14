# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2022-2025, NVIDIA CORPORATION.  All rights reserved.

obj-m += ethernet/
obj-m += tegra_hv_net.o
ifdef CONFIG_VSOCKETS
obj-m += nv_vsock_transport.o
endif
ifdef CONFIG_CAN
obj-m += can/
endif
ifdef CONFIG_WLAN
obj-m += wireless/
endif
