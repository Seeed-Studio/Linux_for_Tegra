# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2022-2024, NVIDIA CORPORATION.  All rights reserved.

ifndef CONFIG_TEGRA_SYSTEM_TYPE_ACK
ifeq ($(shell expr \( $(VERSION) \) \* 10000 + \( $(PATCHLEVEL) \) \* 100 + \( $(SUBLEVEL) \) \<= 60500), 1)
obj-m += rtl8822ce/
endif
endif
