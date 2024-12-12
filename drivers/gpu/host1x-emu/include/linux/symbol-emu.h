// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#ifndef __HOST1X_EMU_SYMMBOL_H
#define __HOST1X_EMU_SYMMBOL_H

#ifdef CONFIG_TEGRA_HOST1X_EMU_DBG_SYMBL
#define HOST1X_EMU_EXPORT_CALL(...)         Dbg_ ## __VA_ARGS__
#define HOST1X_EMU_EXPORT_DECL(ret, ...)    ret Dbg_ ## __VA_ARGS__
#define HOST1X_EMU_EXPORT_SYMBOL(f)         EXPORT_SYMBOL( Dbg_## f)
#else
#define HOST1X_EMU_EXPORT_CALL(...)         __VA_ARGS__
#define HOST1X_EMU_EXPORT_DECL(ret, ...)    ret __VA_ARGS__
#define HOST1X_EMU_EXPORT_SYMBOL(f)         EXPORT_SYMBOL(f)
#endif

#ifndef CONFIG_TEGRA_HOST1X_EMU_DBG_SYMBL
HOST1X_EMU_EXPORT_DECL(void, host1x_syncpt_fence_scan(struct host1x_syncpt *sp));
#endif /*CONFIG_TEGRA_HOST1X_EMU_DBG_SYMBL*/

#endif /*__HOST1X_EMU_SYMMBOL_H*/
