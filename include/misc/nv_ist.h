/*
 * Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef _MISC_NV_IST_H
#define _MISC_NV_IST_H

#if IS_ENABLED(CONFIG_ARM_PSCI_FW)

bool is_ist_enabled(unsigned int cpu);
void cpu_enter_ist(unsigned int cpu);

#else /* ! CONFIG_ARM_PSCI_FW */

static inline bool is_ist_enabled(unsigned int cpu) { return false; }
static inline void cpu_enter_ist(unsigned int cpu) { }

#endif /* CONFIG_ARM_PSCI_FW */

#endif /* _MISC_NV_IST_H */
