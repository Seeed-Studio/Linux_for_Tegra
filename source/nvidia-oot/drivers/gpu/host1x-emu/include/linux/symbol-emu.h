// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#ifndef __LINUX_HOST1X_SYMMBOL_EMU_H
#define __LINUX_HOST1X_SYMMBOL_EMU_H

#ifdef CONFIG_TEGRA_HOST1X_EMU_DBG_SYMBL
#define HOST1X_EMU_EXPORT_CALL(...)         Emu_ ## __VA_ARGS__
#define HOST1X_EMU_EXPORT_DECL(ret, ...)    ret Emu_ ## __VA_ARGS__
#define HOST1X_EMU_EXPORT_SYMBOL(f)         EXPORT_SYMBOL(Emu_## f)
#define HOST1X_EMU_EXPORT_SYMBOL_NAME(f)    Emu_## f
#else
#define HOST1X_EMU_EXPORT_CALL(...)         __VA_ARGS__
#define HOST1X_EMU_EXPORT_DECL(ret, ...)    ret __VA_ARGS__
#define HOST1X_EMU_EXPORT_SYMBOL(f)         EXPORT_SYMBOL(f)
#define HOST1X_EMU_EXPORT_SYMBOL_NAME(f)    f
#endif

#endif /*__LINUX_HOST1X_SYMMBOL_EMU_H*/
