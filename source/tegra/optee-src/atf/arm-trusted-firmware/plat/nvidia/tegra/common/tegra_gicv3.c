/*
 * Copyright (c) 2020-2022, NVIDIA Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <assert.h>

#include <common/bl_common.h>
#include <drivers/arm/gicv3.h>
#include <lib/utils.h>

#include <plat/common/platform.h>
#include <platform_def.h>
#include <tegra_private.h>
#include <tegra_def.h>

/* The GICv3 driver only needs to be initialized in EL3 */
static uintptr_t rdistif_base_addrs[PLATFORM_CORE_COUNT];

/* Default GICR base address to be used for GICR probe. */
static const uintptr_t gicr_base_addrs[2] = {
	TEGRA_GICR_BASE,	/* GICR Base address of the primary CPU */
	0U			/* Zero Termination */
};

/* List of zero terminated GICR frame addresses which CPUs will probe */
static const uintptr_t *tegra_gicr_frames = gicr_base_addrs;

/*
 * We save and restore the GICv3 context on CPU suspend and system suspend.
 */
static gicv3_redist_ctx_t rdist_ctx;
static gicv3_dist_ctx_t dist_ctx;

/* Pointers to platform specific structs to save the context */
static gicv3_redist_ctx_t *plat_redist_ctx;
static gicv3_dist_ctx_t *plat_dist_ctx;

/* Help function to register platform specific structure base address */
void tegra_gic_register_ctx_ptr(uintptr_t *redist_ptr, uintptr_t *dist_ptr)
{
	assert(redist_ptr != NULL);
	assert(dist_ptr != NULL);
	plat_redist_ctx = (gicv3_redist_ctx_t *)redist_ptr;
	plat_dist_ctx = (gicv3_dist_ctx_t *)dist_ptr;
}

/*
 * Override default implementation of 'arm_gicv3_distif_pre_save'
 * and 'arm_gicv3_distif_post_restore' functions.
 */
void arm_gicv3_distif_pre_save(unsigned int rdist_proc_num)
{
	; /* do nothing */
}

void arm_gicv3_distif_post_restore(unsigned int rdist_proc_num)
{
	; /* do nothing */
}

static unsigned int plat_tegra_mpidr_to_core_pos(unsigned long mpidr)
{
	return (unsigned int)plat_core_pos_by_mpidr(mpidr);
}

/*
 * By default, gicr_frames will be pointing to gicr_base_addrs. If
 * the platform supports a non-contiguous GICR frames (GICR frames located
 * at uneven offset), plat_arm_override_gicr_frames function can be used by
 * such platform to override the gicr_frames.
 */
void tegra_gicv3_override_gicr_frames(const uintptr_t *plat_gicr_frames)
{
	assert(plat_gicr_frames != NULL);
	tegra_gicr_frames = plat_gicr_frames;
}

/******************************************************************************
 * Tegra common helper to setup the GICv3 driver data.
 *****************************************************************************/
void tegra_gic_setup(const interrupt_prop_t *interrupt_props,
		     unsigned int interrupt_props_num)
{
	/*
	 * Tegra GIC configuration settings
	 */
	static gicv3_driver_data_t tegra_gic_data;

	/*
	 * Register Tegra GICv3 driver
	 */
	tegra_gic_data.gicd_base = TEGRA_GICD_BASE;
	tegra_gic_data.gicr_base = 0U;
	tegra_gic_data.rdistif_num = PLATFORM_CORE_COUNT;
	tegra_gic_data.rdistif_base_addrs = rdistif_base_addrs;
	tegra_gic_data.mpidr_to_core_pos = plat_tegra_mpidr_to_core_pos;
	tegra_gic_data.interrupt_props = interrupt_props;
	tegra_gic_data.interrupt_props_num = interrupt_props_num;
	gicv3_driver_init(&tegra_gic_data);

	if (gicv3_rdistif_probe(tegra_gicr_frames[0]) == -1) {
		ERROR("No GICR base frame found for Primary CPU\n");
		panic();
	}
}

/******************************************************************************
 * Tegra common helper to initialize the GICv3 only driver.
 *****************************************************************************/
void tegra_gic_init(void)
{
	gicv3_distif_init();
	gicv3_rdistif_init(plat_my_core_pos());
	gicv3_cpuif_enable(plat_my_core_pos());
}

/******************************************************************************
 * Tegra common helper to disable the GICv3 CPU interface
 *****************************************************************************/
void tegra_gic_cpuif_deactivate(void)
{
	gicv3_cpuif_disable(plat_my_core_pos());
}

/******************************************************************************
 * Tegra common helper to initialize the per cpu distributor interface
 * in GICv3
 *****************************************************************************/
void tegra_gic_pcpu_init(void)
{
	int result;
	const uintptr_t *gicr_frames = tegra_gicr_frames;

	do {
		result = gicv3_rdistif_probe(*gicr_frames);

		/* If the probe is successful, no need to proceed further */
		if (result == 0) {
			break;
		}

		gicr_frames++;
	} while (*gicr_frames != 0U);

	if (result == -1) {
		ERROR("No GICR base frame found for CPU 0x%lx\n", read_mpidr());
		panic();
	}

	gicv3_rdistif_init(plat_my_core_pos());
	gicv3_cpuif_enable(plat_my_core_pos());
}

/******************************************************************************
 * Tegra helper to save & restore the GICv3 on resume from CPU or system suspend
 *****************************************************************************/
void tegra_gic_save(uint32_t pstate_id)
{
	gicv3_redist_ctx_t *local_rdist_base;
	gicv3_dist_ctx_t *local_dist_base;

	/*
	 * Check the platform redist and dist ctx,
	 * if they are not NULL, use platform specific
	 * structures to do the save.
	 */
	local_rdist_base = (plat_redist_ctx == NULL) ? &rdist_ctx : plat_redist_ctx;
	local_dist_base = (plat_dist_ctx == NULL) ? &dist_ctx : plat_dist_ctx;

	if (pstate_id == PSTATE_ID_SOC_POWERDN) {
		/*
		 * Save the GIC Redistributors and ITS contexts before the
		 * Distributor context. we only need to save the context of the CPU
		 * that is issuing the CPU or SYSTEM SUSPEND call, i.e. the current CPU.
		 */
		gicv3_rdistif_save(plat_my_core_pos(), local_rdist_base);

		/* Save the GIC Distributor context */
		gicv3_distif_save(local_dist_base);
	}

	/*
	 * From here, all the components of the GIC can be safely powered down
	 * as long as there is an alternate way to handle wakeup interrupt
	 * sources.
	 */
}

void tegra_gic_restore(uint32_t pstate_id)
{
	gicv3_redist_ctx_t *local_rdist_base;
	gicv3_dist_ctx_t *local_dist_base;

	/*
	 * Check the platform redist and dist ctx,
	 * if they are not NULL, use platform specific
	 * structures to do the restore.
	 */
	local_rdist_base = (plat_redist_ctx == NULL) ? &rdist_ctx : plat_redist_ctx;
	local_dist_base = (plat_dist_ctx == NULL) ? &dist_ctx : plat_dist_ctx;

	if (pstate_id == PSTATE_ID_SOC_POWERDN) {
		/* Restore the GIC Distributor context */
		gicv3_distif_init_restore(local_dist_base);

		/*
		 * Restore the GIC Redistributor and ITS contexts after the
		 * Distributor context. we only need to restore the context of the CPU
		 * that is issuing the CPU or SYSTEM SUSPEND call, i.e. the current CPU.
		 */
		gicv3_rdistif_init_restore(plat_my_core_pos(), local_rdist_base);
	}

	/* Re-enable the GIC CPU interface*/
	gicv3_cpuif_enable(plat_my_core_pos());
}
