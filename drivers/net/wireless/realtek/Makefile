# SPDX-License-Identifier: GPL-2.0-only
# SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

ifndef CONFIG_TEGRA_SYSTEM_TYPE_ACK
ifeq ($(shell expr \( $(VERSION) \) \* 10000 + \( $(PATCHLEVEL) \) \* 100 + \( $(SUBLEVEL) \) \<= 60500), 1)
ifneq ($(NV_OOT_REALTEK_RTL8822CE_SKIP_BUILD),y)
obj-m += rtl8822ce/
endif
endif
endif
