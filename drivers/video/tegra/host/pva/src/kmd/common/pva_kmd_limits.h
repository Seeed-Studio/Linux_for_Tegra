/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
/**
 * @file pva_kmd_limits.h
 * @brief Platform-independent type limit definitions
 *
 * @details This header provides portable definitions for integer type limits
 * that work across different platforms (Linux kernel, QNX, Native x86, Simulation).
 *
 * Linux kernel does not have <stdint.h> but provides its own limit macros.
 * Other platforms (QNX, Native, SIM) use standard <stdint.h> definitions.
 *
 * This header abstracts these platform differences, allowing common code to use
 * consistent macro names across all platforms.
 */

#ifndef PVA_KMD_LIMITS_H
#define PVA_KMD_LIMITS_H

#ifdef __KERNEL__
/* Linux kernel build - use kernel-provided limit macros */
#include <linux/kernel.h>
/*
 * Linux kernel provides:
 * U8_MAX, U16_MAX, U32_MAX, U64_MAX for unsigned types
 * S8_MAX, S8_MIN, S16_MAX, S16_MIN, S32_MAX, S32_MIN, S64_MAX, S64_MIN for signed types
 */
#else
/* QNX/Native/SIM builds - use standard C definitions */
#include <stdint.h>
/* Map standard C names to kernel-style names for consistency */
#ifndef U8_MAX
#define U8_MAX UINT8_MAX
#endif
#ifndef U16_MAX
#define U16_MAX UINT16_MAX
#endif
#ifndef U32_MAX
#define U32_MAX UINT32_MAX
#endif
#ifndef U64_MAX
#define U64_MAX UINT64_MAX
#endif
#ifndef S8_MAX
#define S8_MAX INT8_MAX
#endif
#ifndef S8_MIN
#define S8_MIN INT8_MIN
#endif
#ifndef S16_MAX
#define S16_MAX INT16_MAX
#endif
#ifndef S16_MIN
#define S16_MIN INT16_MIN
#endif
#ifndef S32_MAX
#define S32_MAX INT32_MAX
#endif
#ifndef S32_MIN
#define S32_MIN INT32_MIN
#endif
#ifndef S64_MAX
#define S64_MAX INT64_MAX
#endif
#ifndef S64_MIN
#define S64_MIN INT64_MIN
#endif
#endif /* __KERNEL__ */

#endif /* PVA_KMD_LIMITS_H */
