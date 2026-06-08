# SPDX-FileCopyrightText: Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: GPL-2.0-only

# CAN_LEDS has been marked as BROKEN since Linux v4.19 and
# was finally removed in Linux v5.19. Only compile support
# for LEDS if enabled in the kernel.
ifdef CONFIG_CAN_LEDS
subdir-ccflags-y += -DCONFIG_CAN_LEDS
endif

obj-m := mttcan.o

mttcan-y = native/m_ttcan_linux.o native/m_ttcan_sys.o hal/m_ttcan.o
mttcan-y += hal/m_ttcan_intr.o hal/m_ttcan_list.o hal/m_ttcan_ram.o
mttcan-y += hal/m_ttcan_tt.o
