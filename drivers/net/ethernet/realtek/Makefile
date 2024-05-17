# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2024, NVIDIA CORPORATION.  All rights reserved.

obj-m += r8168/

ifeq ($(shell expr \( $(VERSION) \) \* 10000 + \( $(PATCHLEVEL) \) \* 100 + \( $(SUBLEVEL) \) \<= 60400), 1)
obj-m += r8126/
endif
