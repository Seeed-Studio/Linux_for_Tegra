# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: GPL-2.0-only

obj-m := mttcan.o

mttcan-y = native/m_ttcan_linux.o native/m_ttcan_sys.o hal/m_ttcan.o
mttcan-y += hal/m_ttcan_intr.o hal/m_ttcan_list.o hal/m_ttcan_ram.o
mttcan-y += hal/m_ttcan_tt.o
