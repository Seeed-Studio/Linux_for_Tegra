/*
 * Copyright (c) 2021-2022, NVIDIA CORPORATION. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include <arch.h>
#include <arch_helpers.h>
#include <common/bl_common.h>
#include <common/debug.h>
#include <cortex_a78_ae.h>
#include <context.h>
#include <drivers/arm/gic600ae_fmu.h>
#include <drivers/delay_timer.h>
#include <lib/el3_runtime/context_mgmt.h>
#include <lib/psci/psci.h>

#include <mce.h>
#include <mce_private.h>
#include <memctrl_v2.h>
#include <plat/common/platform.h>
#include <psc_mailbox.h>
#include <se.h>
#include <t234_ari.h>
#include <tegra234_private.h>
#include <tegra_platform.h>
#include <tegra_private.h>

/* state id mask */
#define TEGRA234_STATE_ID_MASK		0xFU
/* constants to get power state's wake time */
#define TEGRA234_WAKE_TIME_MASK		0x0FFFFFF0U
#define TEGRA234_WAKE_TIME_SHIFT	4U
/* default core wake mask for CPU_SUSPEND
 * WAKE_REQUEST	2:2 	Per Core (GIC provides a wake request interface that
 *			is used to indicate when an interrupt is received that
 *			is required to wake a core from a low power state.)
 */
#define TEGRA234_CORE_WAKE_MASK		0x4U

extern bool tegra234_fake_system_suspend;

static struct t234_psci_percpu_data {
	uint32_t wake_time;
} __aligned(CACHE_WRITEBACK_GRANULE) t234_percpu_data[PLATFORM_CORE_COUNT];

int32_t tegra_soc_validate_power_state(uint32_t power_state,
					psci_power_state_t *req_state)
{
	uint8_t state_id = (uint8_t)psci_get_pstate_id(power_state) & TEGRA234_STATE_ID_MASK;
	uint32_t cpu = plat_my_core_pos();
	int32_t ret = PSCI_E_SUCCESS;

	/* save the core wake time (in TSC ticks)*/
	t234_percpu_data[cpu].wake_time = (power_state & TEGRA234_WAKE_TIME_MASK)
			<< TEGRA234_WAKE_TIME_SHIFT;

	/*
	 * Clean percpu_data[cpu] to DRAM. This needs to be done to ensure that
	 * the correct value is read in tegra_soc_pwr_domain_suspend(), which
	 * is called with caches disabled. It is possible to read a stale value
	 * from DRAM in that function, because the L2 cache is not flushed
	 * unless the cluster is entering CC7.
	 */
	clean_dcache_range((uint64_t)&t234_percpu_data[cpu],
			sizeof(t234_percpu_data[cpu]));

	/* Sanity check the requested state id */
	switch (state_id) {
	case PSTATE_ID_CORE_POWERDN:

		/* Core powerdown request */
		req_state->pwr_domain_state[MPIDR_AFFLVL0] = state_id;
		req_state->pwr_domain_state[MPIDR_AFFLVL1] = state_id;

		break;

	default:
		ERROR("%s: unsupported state id (%d)\n", __func__, state_id);
		ret = PSCI_E_INVALID_PARAMS;
		break;
	}

	return ret;
}

int32_t tegra_soc_cpu_standby(plat_local_state_t cpu_state)
{
	(void)cpu_state;

	return PSCI_E_SUCCESS;
}

int32_t tegra_soc_pwr_domain_suspend(const psci_power_state_t *target_state)
{
	const plat_local_state_t *pwr_domain_state;
	uint8_t stateid_afflvl0, stateid_afflvl2;
	uint32_t val;
	uint32_t cpu = plat_my_core_pos();
	int32_t ret;

	/* get the state ID */
	pwr_domain_state = target_state->pwr_domain_state;
	stateid_afflvl0 = pwr_domain_state[MPIDR_AFFLVL0] &
		TEGRA234_STATE_ID_MASK;
	stateid_afflvl2 = pwr_domain_state[PLAT_MAX_PWR_LVL] &
		TEGRA234_STATE_ID_MASK;

	if (stateid_afflvl0 == PSTATE_ID_CORE_POWERDN) {

		/* Enter CPU powerdown */
		(void)mce_command_handler((uint64_t)MCE_CMD_ENTER_CSTATE,
					  t234_percpu_data[cpu].wake_time,
					  0U, 0U);

	} else if (stateid_afflvl2 == PSTATE_ID_SOC_POWERDN) {
		if (!tegra234_fake_system_suspend) {
			/*
			 * Before entering SC7 state, the ARM Trusted Firmware shall issue
	                 * a SC7 entry request to the PSC.
			 */
			tegra_psc_notify_sc7_entry();

			/*
			 * Suspend SE, RNG1 and PKA1 only on silcon and fpga,
			 * since VDK does not support atomic se ctx save
			 */
			if (tegra_platform_is_silicon() || tegra_platform_is_fpga()) {
				ret = tegra_se_suspend();
				assert(ret == 0);
			}

			/* Allow BPMP write access to AOTZRAM powergate controls */
			val = mmio_read_32(TEGRA_AON_FABRIC_FIREWALL_BASE + AON_FIREWALL_ARF_0_WRITE_CTL);
			val |= (WRITE_CTL_MSTRID_BPMP_FW_BIT | WRITE_CTL_MSTRID_TZ_BIT);
			mmio_write_32(TEGRA_AON_FABRIC_FIREWALL_BASE + AON_FIREWALL_ARF_0_WRITE_CTL,
				val);
			val = mmio_read_32(TEGRA_AON_FABRIC_FIREWALL_BASE + AON_FIREWALL_ARF_0_CTL);
			val |= ARF_CTL_OWNER_BPMP_FW;
			mmio_write_32(TEGRA_AON_FABRIC_FIREWALL_BASE + AON_FIREWALL_ARF_0_CTL,
				val);
		}

	} else {
		; /* do nothing */
	}

	return PSCI_E_SUCCESS;
}

/*******************************************************************************
 * Helper function to check if this is the last ON CPU in the cluster
 ******************************************************************************/
static bool tegra_last_on_cpu_in_cluster(const plat_local_state_t *states,
			uint32_t ncpu)
{
	plat_local_state_t target;
	bool last_on_cpu = true;
	uint32_t num_cpus = ncpu, pos = 0;

	do {
		target = states[pos];
		if (target != PLAT_MAX_OFF_STATE) {
			last_on_cpu = false;
		}
		--num_cpus;
		pos++;
	} while (num_cpus != 0U);

	return last_on_cpu;
}

/*******************************************************************************
 * Helper function to get target power state for the cluster
 ******************************************************************************/
static plat_local_state_t tegra_get_afflvl1_pwr_state(const plat_local_state_t *states,
			uint32_t ncpu)
{
	uint32_t core_pos = plat_my_core_pos() % PLATFORM_MAX_CPUS_PER_CLUSTER;
	plat_local_state_t target = states[core_pos];
	mce_cstate_info_t cstate_info = { 0 };

	/* CPU suspend */
	if (target == PSTATE_ID_CORE_POWERDN) {

		/* Program default wake mask */
		cstate_info.wake_mask = TEGRA234_CORE_WAKE_MASK;
		cstate_info.update_wake_mask = 1;
		mce_update_cstate_info(&cstate_info);
	}

	/* CPU off */
	if (target == PLAT_MAX_OFF_STATE) {

		/* Enable cluster powerdn from last CPU in the cluster */
		if (tegra_last_on_cpu_in_cluster(states, ncpu)) {

			/* Enable CC7 state and turn off wake mask */
			cstate_info.cluster = TEGRA_ARI_CSTATE_INFO_CLUSTER_CC7;
			mce_update_cstate_info(&cstate_info);

		} else {

			/* Turn off wake_mask */
			cstate_info.wake_mask = 0;
			cstate_info.update_wake_mask = 1;
			mce_update_cstate_info(&cstate_info);
			target = PSCI_LOCAL_STATE_RUN;
		}
	}

	return target;
}

/*******************************************************************************
 * Platform handler to calculate the proper target power level at the
 * specified affinity level
 ******************************************************************************/
plat_local_state_t tegra_soc_get_target_pwr_state(uint32_t lvl,
					     const plat_local_state_t *states,
					     uint32_t ncpu)
{
	plat_local_state_t target = PSCI_LOCAL_STATE_RUN;
	uint32_t cpu = plat_my_core_pos();

	/* System Suspend */
	if ((lvl == (uint32_t)MPIDR_AFFLVL2) && (states[cpu] == PSTATE_ID_SOC_POWERDN)) {
		target = PSTATE_ID_SOC_POWERDN;
	}

	/* CPU off, CPU suspend */
	if (lvl == (uint32_t)MPIDR_AFFLVL1) {
		target = tegra_get_afflvl1_pwr_state(states, ncpu);
	}

	/* target cluster/system state */
	return target;
}

int32_t tegra_soc_pwr_domain_suspend_pwrdown_early(const psci_power_state_t *target_state)
{
	return PSCI_E_NOT_SUPPORTED;
}

int32_t tegra_soc_pwr_domain_power_down_wfi(const psci_power_state_t *target_state)
{
	uint32_t rmr_el3;
	uint8_t stateid_afflvl2;
	uint32_t val;
	mce_cstate_info_t cstate_info = { 0 };

	/* get the state ID */
	stateid_afflvl2 = target_state->pwr_domain_state[PLAT_MAX_PWR_LVL] &
		TEGRA234_STATE_ID_MASK;

	/*
	 * If we are in fake system suspend mode, ensure we start doing
	 * procedures that help in looping back towards system suspend exit
	 * instead of calling WFI by requesting a warm reset.
	 */
	if (stateid_afflvl2 == PSTATE_ID_SOC_POWERDN) {
		if (tegra234_fake_system_suspend) {
			rmr_el3 = read_rmr_el3();
			write_rmr_el3(rmr_el3 | RMR_WARM_RESET_CPU);
			wfi();
			panic();
		}

		/*
		 * Before executing wfi instruction to enter SC7 state,
		 * the ARM Trusted Firmware shall confirm that the PSC
		 * has completed its SC7 entry sequence.
		 */
		tegra_psc_wait_for_ack();

		/* SCF flush - Clean and invalidate caches */
		mce_clean_and_invalidate_caches();

		/* Prepare for system suspend */
		cstate_info.cluster = PSTATE_ID_CORE_POWERDN;
		cstate_info.system = PSTATE_ID_CORE_POWERDN;
		mce_update_cstate_info(&cstate_info);

		do {
			val = (uint32_t)mce_command_handler(
					(uint32_t)MCE_CMD_IS_SC7_ALLOWED,
					0U, 0U,	0U);
		} while (val == 0U);

		/* Instruct the MCE to enter system suspend state */
		(void)mce_command_handler((uint64_t)MCE_CMD_ENTER_CSTATE,
				MCE_CORE_SLEEP_TIME_INFINITE, 0U, 0U);
	}

	return PSCI_E_SUCCESS;
}

int32_t tegra_soc_pwr_domain_on(u_register_t mpidr)
{
	uint64_t target_cpu = plat_core_pos_by_mpidr(mpidr);

	if (target_cpu >= PLATFORM_CORE_COUNT) {
		ERROR("%s: unsupported CPU (0x%x)\n", __func__ , (uint32_t)target_cpu);
		return PSCI_E_NOT_PRESENT;
	}

	(void)mce_command_handler((uint64_t)MCE_CMD_ONLINE_CORE, target_cpu, 0U, 0U);

	return PSCI_E_SUCCESS;
}

/*******************************************************************************
 * Enable write access to cluster PMU register
 ******************************************************************************/
static void tegra_enable_cluster_pmu_writes(void)
{
	uint64_t actlr_elx;

	/**
	 * ARM DSU PMU driver in linux kernel requires write access to CLUSTERPM*
	 * registers. These registers are protected by CLUSTERPMUEN bit in ACTLR
	 * register. Linux kernel is in EL2, so ACTLR_EL3.CLUSTERPMUEN bit needs to
	 * be set, which will allow writes in EL2/EL1secure.
	 */

	/* Enable write in EL2 */
	actlr_elx = read_actlr_el3();
	actlr_elx |= CORTEX_A78_AE_ACTLR_CLUSTERPMUEN_BIT;
	write_actlr_el3(actlr_elx);
}

int32_t tegra_soc_pwr_domain_on_finish(const psci_power_state_t *target_state)
{
	uint8_t stateid_afflvl2 = target_state->pwr_domain_state[PLAT_MAX_PWR_LVL];
	uint8_t stateid_afflvl0 = target_state->pwr_domain_state[MPIDR_AFFLVL0];
	mce_cstate_info_t cstate_info = { 0 };
	uint32_t val = 0U;
	uint32_t __unused cluster_id = ((uint32_t)read_mpidr() >>
				MPIDR_AFF2_SHIFT) & MPIDR_AFFLVL_MASK;
	uint32_t __unused smen;

#if RAS_EXTENSION
	/* Enable RAS handling for the cluster specific nodes */
	tegra234_ras_init_my_cluster();
#endif

	/*
	 * Program the following bits to '1'.
	 *
	 * Bit 0: Enable interleaving normal Non-cacheable transactions between
	 * master interfaces like Cacheable transactions.
	 * Bit 14: Enables sending WriteEvict transactions on the ACE or CHI
	 * master for UniqueClean evictions.
	 */
	val = read_clusterectlr_el1();
	write_clusterectlr_el1(val | DSU_WRITEEVICT_BIT | DSU_NC_CTRL_BIT);

	/*
	 * Reset power state info for CPUs when onlining. We set
	 * deepest power when offlining or suspending a core, so
	 * reset the power state when the CPU powers back on.
	 */
	if (tegra_platform_is_silicon() || tegra_platform_is_fpga()) {
		cstate_info.cluster = TEGRA_ARI_CSTATE_INFO_CLUSTER_CC1;
		mce_update_cstate_info(&cstate_info);
	}

	/*
	 * Enable write access to cluster PMU register if ODM_PRODUCTION
	 * fuse is 0.
	 */
	if (mmio_read_32(TEGRA_FUSE_BASE + SECURITY_MODE) == 0U) {
		tegra_enable_cluster_pmu_writes();
	}

	/*
	 * Check if we are exiting from deep sleep and restore SE
	 * context if we are.
	 */
	if (stateid_afflvl2 == PSTATE_ID_SOC_POWERDN) {

#if ENABLE_STRICT_CHECKING_MODE
		/*
		 * Enable strict checking after programming the GSC for
		 * enabling TZSRAM and TZDRAM
		 */
		mce_enable_strict_checking();
#endif

		if (!tegra234_fake_system_suspend) {
			/* Resume SE, RNG1 and PKA1 */
			tegra_se_resume();
		}

#if RAS_EXTENSION
		/* Enable RAS handling for all the common nodes */
		tegra234_ras_init_common();
#endif
		NOTICE("%s: exited SC7 successfully. Entering normal world.\n", __func__);
	}

#if GICV3_SUPPORT_GIC600AE_FMU
	if (tegra_platform_is_silicon() || tegra_platform_is_fpga()) {
		/* disable the GICFMU SMID 0xB after cluster power on */
		if (stateid_afflvl0 != PSTATE_ID_CORE_POWERDN) {
			smen = ((FMU_BLK_PPI0 + cluster_id) << FMU_SMEN_BLK_SHIFT) |
				(0xb << FMU_SMEN_SMID_SHIFT);
			gic_fmu_write_smen(TEGRA_GICFMU_BASE, smen);
		}
	}
#endif

	return PSCI_E_SUCCESS;
}

int32_t tegra_soc_pwr_domain_off(const psci_power_state_t *target_state)
{
	(void)target_state;

	/* Turn off CPU */
	(void)mce_command_handler((uint64_t)MCE_CMD_ENTER_CSTATE,
			MCE_CORE_SLEEP_TIME_INFINITE, 0U, 0U);

	return PSCI_E_SUCCESS;
}

__dead2 void tegra_soc_prepare_system_off(void)
{
	/* System power off */
	mce_system_shutdown();

	wfi();

	/* wait for the system to power down */
	for (;;) {
		;
	}
}

int32_t tegra_soc_prepare_system_reset(void)
{
	/* System reboot */
	mce_system_reboot();

	return PSCI_E_SUCCESS;
}
