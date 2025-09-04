/*
 * Copyright (c) 2018, ARM Limited and Contributors. All rights reserved.
 * Copyright (c) 2020-2022, NVIDIA Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef TEGRA_GIC_H
#define TEGRA_GIC_H

#include <common/interrupt_props.h>

/*******************************************************************************
 * Per-CPU struct describing FIQ state to be stored
 ******************************************************************************/
typedef struct pcpu_fiq_state {
	uint64_t elr_el3;
	uint64_t spsr_el3;
} pcpu_fiq_state_t;

/*******************************************************************************
 * Function declarations
 ******************************************************************************/
void tegra_gic_cpuif_deactivate(void);
void tegra_gic_init(void);
void tegra_gic_pcpu_init(void);
void tegra_gic_setup(const interrupt_prop_t *interrupt_props,
		     unsigned int interrupt_props_num);
void tegra_gic_save(uint32_t pstate_id);
void tegra_gic_restore(uint32_t pstate_id);
void tegra_gic_register_ctx_ptr(uintptr_t *redist_ptr, uintptr_t *dist_ptr);

/* Tegra GICv3 function declarations */
void tegra_gicv3_override_gicr_frames(const uintptr_t *plat_gicr_frames);

#endif /* TEGRA_GIC_H */
