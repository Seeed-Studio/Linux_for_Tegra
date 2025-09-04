/*
 * Copyright (c) 2021-2022, NVIDIA CORPORATION. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <arch.h>
#include <arch_helpers.h>
#include <assert.h>
#include <inttypes.h>

#include <common/bl_common.h>
#include <context.h>
#include <lib/el3_runtime/context_mgmt.h>
#include <common/debug.h>
#include <mce.h>
#include <mce_private.h>
#include <platform_def.h>
#include <stdbool.h>
#include <errno.h>
#include <t234_ari.h>
#include <tegra_def.h>
#include <tegra_platform.h>
#include <tegra_private.h>

/* Global variable to save the cluster bitmap */
static uint32_t cluster_present_bitmap = 0xDEADFEED;

/* Global variable to save the core present bitmap */
static uint32_t core_present_bitmap = 0xDEADFEED;

/* Global variable to save the unsupported platform list */
static uint32_t tegra_unsupported_platform_bitmap = U(0xDEADBEEF);

/* Table to hold the per-CPU ARI base address and function handlers */
static uint32_t mce_cfg_table[PLATFORM_CLUSTER_COUNT][PLATFORM_MAX_CPUS_PER_CLUSTER] = {
	{
		/* cluster 0 */
		(TEGRA_MMCRAB_BASE + MCE_ARI_APERTURE_0_OFFSET),
		(TEGRA_MMCRAB_BASE + MCE_ARI_APERTURE_1_OFFSET),
		(TEGRA_MMCRAB_BASE + MCE_ARI_APERTURE_2_OFFSET),
		(TEGRA_MMCRAB_BASE + MCE_ARI_APERTURE_3_OFFSET),
	},
	{
		/* cluster 1 */
		(TEGRA_MMCRAB_BASE + MCE_ARI_APERTURE_4_OFFSET),
		(TEGRA_MMCRAB_BASE + MCE_ARI_APERTURE_5_OFFSET),
		(TEGRA_MMCRAB_BASE + MCE_ARI_APERTURE_6_OFFSET),
		(TEGRA_MMCRAB_BASE + MCE_ARI_APERTURE_7_OFFSET),
	},
	{
		/* cluster 2 */
		(TEGRA_MMCRAB_BASE + MCE_ARI_APERTURE_8_OFFSET),
		(TEGRA_MMCRAB_BASE + MCE_ARI_APERTURE_9_OFFSET),
		(TEGRA_MMCRAB_BASE + MCE_ARI_APERTURE_10_OFFSET),
		(TEGRA_MMCRAB_BASE + MCE_ARI_APERTURE_11_OFFSET),
	}

};

static uint32_t mce_get_curr_cpu_ari_base(void)
{
	u_register_t mpidr = read_mpidr();
	u_register_t cpu_id, cluster_id;

	cluster_id = (mpidr >> (u_register_t)MPIDR_AFF2_SHIFT) &
		(u_register_t)MPIDR_AFFLVL_MASK;

	assert(cluster_id <= PLATFORM_CLUSTER_COUNT);

	cpu_id = (mpidr >> (u_register_t)MPIDR_AFF1_SHIFT) &
		(u_register_t)MPIDR_AFFLVL_MASK;

	assert(cpu_id <= PLATFORM_MAX_CPUS_PER_CLUSTER);

	return mce_cfg_table[cluster_id][cpu_id];
}

/* Handler to check if MCE firmware is supported */
static bool mce_firmware_not_supported(void)
{
	/* Check if this platform is supported */
	if (tegra_unsupported_platform_bitmap != U(0xDEADBEEF)) {
		return !(tegra_unsupported_platform_bitmap == 0U);
	}

	/*
	 * If the current platform is one of the unsupported ones,
	 * add it to the bitmap
	 */
	tegra_unsupported_platform_bitmap = tegra_platform_is_linsim() |
		(tegra_platform_is_virt_dev_kit() << 1U) |
		(tegra_platform_is_vsp() << 2U);

	return !(tegra_unsupported_platform_bitmap == 0U);
}

/******************************************************************************
 * Common handler for all MCE commands
 *****************************************************************************/
int32_t mce_command_handler(uint64_t cmd, uint64_t arg0, uint64_t arg1,
			uint64_t arg2)
{
	int32_t ret = 0;
	uint32_t cpu_ari_base;

	/* get the CPU's ARI base address */
	cpu_ari_base = mce_get_curr_cpu_ari_base();

	switch (cmd) {
	case (uint64_t)MCE_CMD_ENTER_CSTATE:
		ret = ari_enter_cstate(cpu_ari_base, (uint32_t)arg0);
		if (ret < 0) {
			ERROR("%s: enter_cstate failed(%d)\n", __func__, ret);
		}

		break;

	case (uint64_t)MCE_CMD_IS_SC7_ALLOWED:
		ret = ari_is_sc7_allowed((uint32_t)cpu_ari_base);
		if (ret < 0) {
			ERROR("%s: is_sc7_allowed failed(%d)\n", __func__, ret);
		}

		break;

	case (uint64_t)MCE_CMD_ONLINE_CORE:
		ret = ari_online_core(cpu_ari_base, (uint32_t)arg0);
		if (ret < 0) {
			ERROR("%s: online_core failed(%d)\n", __func__, ret);
		}

		break;

	case (uint64_t)MCE_CMD_ROC_FLUSH_CACHE:
		ret = ari_ccplex_cache_clean_and_invalidate(cpu_ari_base);
		if (ret < 0) {
			ERROR("%s: flush cache failed(%d)\n", __func__, ret);
		}

		break;

	case (uint64_t)MCE_CMD_ROC_CLEAN_CACHE:
		ret = ari_ccplex_cache_clean(cpu_ari_base);
		if (ret < 0) {
			ERROR("%s: clean cache failed(%d)\n", __func__, ret);
		}

		break;

#if ENABLE_CHIP_VERIFICATION_HARNESS
	case (uint64_t)MCE_CMD_ENABLE_LATIC:
		/*
		 * This call is not for production use. The constant value,
		 * 0xFFFF0000, is specific to allowing for enabling LATIC on
		 * pre-production parts for the chip verification harness.
		 *
		 * Enabling LATIC allows S/W to read the MINI ISPs in the
		 * CCPLEX. The ISMs are used for various measurements relevant
		 * to particular locations in the Silicon. They are small
		 * counters which can be polled to determine how fast a
		 * particular location in the Silicon is.
		 */
		ari_ccplex_latic_on(cpu_ari_base);

		break;
#endif

	default:
		ERROR("unknown MCE command (%" PRIu64 ")\n", cmd);
		ret = -EINVAL;
		break;
	}

	return ret;
}

/******************************************************************************
 * Handler to update carveout values for Video Memory Carveout region
 *****************************************************************************/
int32_t mce_update_gsc_videomem(void)
{
	int32_t ret = 0;

	/*
	 * MCE firmware is not running on simulation platforms.
	 */
	if (mce_firmware_not_supported()) {
		ret = -EINVAL;
	} else {
		ret = ari_update_ccplex_gsc(mce_get_curr_cpu_ari_base(),
			 TEGRA_ARI_CARVEOUT_VPR);
	}

	return ret;
}

/******************************************************************************
 * Handler to update carveout values for TZDRAM aperture
 *****************************************************************************/
int32_t mce_update_gsc_tzdram(void)
{
	/* This function is not required for T23x.
	 * Return 0 to avoid compile error.
	 */
	return 0;
}

/******************************************************************************
 * Handler to issue the UPDATE_CSTATE_INFO request
 *****************************************************************************/
void mce_update_cstate_info(const mce_cstate_info_t *cstate)
{
	/* issue the UPDATE_CSTATE_INFO request */
	ari_update_cstate_info(mce_get_curr_cpu_ari_base(), cstate->cluster,
		 cstate->system, cstate->wake_mask, cstate->update_wake_mask);
}

/******************************************************************************
 * Handler to read the MCE firmware version and check if it is compatible
 * with interface header the BL3-1 was compiled against
 *****************************************************************************/
void mce_verify_firmware_version(void)
{
	uint64_t version;
	uint32_t major, minor;

	/*
	 * MCE firmware is not running on simulation platforms.
	 */
	if (mce_firmware_not_supported()) {
		return;
	}

	/*
	 * Read the MCE firmware version and extract the major and minor
	 * version fields
	 */
	version = ari_get_version(mce_get_curr_cpu_ari_base());
	major = (uint32_t)version;
	minor = (uint32_t)(version >> 32);

	INFO("MCE Version - HW=%d:%d, SW=%ld:%ld\n", major, minor,
		TEGRA_ARI_VERSION_MAJOR, TEGRA_ARI_VERSION_MINOR);

	/*
	 * Verify that the MCE firmware version and the interface header
	 * match
	 */
	if (major != (uint32_t)TEGRA_ARI_VERSION_MAJOR) {
		ERROR("MCE major version mismatch\n");
		panic();
	}

	if (minor < (uint32_t)TEGRA_ARI_VERSION_MINOR) {
		ERROR("MCE minor version mismatch\n");
		panic();
	}
}

/******************************************************************************
 * Handler to issue SCF flush - Clean and invalidate caches
 *****************************************************************************/
void mce_clean_and_invalidate_caches(void)
{
	int32_t ret = 0;

	/*
	 * MCE firmware is not running on simulation platforms.
	 */
	if (mce_firmware_not_supported()) {
		return;
	}

	ret = ari_ccplex_cache_clean_and_invalidate(mce_get_curr_cpu_ari_base());
	if (ret < 0) {
		ERROR("%s: flush cache_trbits failed(%d)\n", __func__,
			ret);
	}
}

#if ENABLE_STRICT_CHECKING_MODE
/******************************************************************************
 * Handler to enable the strict checking mode
 *****************************************************************************/
void mce_enable_strict_checking(void)
{
	uint64_t sctlr = read_sctlr_el3();
	int32_t ret = 0;

	if (tegra_platform_is_silicon() || tegra_platform_is_fpga()) {
		/*
		 * Step1: TZ-DRAM and TZRAM should be setup before the MMU is
		 * enabled.
		 *
		 * The common code makes sure that TZDRAM/TZRAM are already
		 * enabled before calling into this handler. If this is not the
		 * case, the following sequence must be executed before moving
		 * on to step 2.
		 *
		 * tlbialle1is();
		 * tlbialle3is();
		 * dsbsy();
		 *
		 */
		if ((sctlr & (uint64_t)SCTLR_M_BIT) == (uint64_t)SCTLR_M_BIT) {
			tlbialle1is();
			tlbialle3is();
			dsbsy();
		}

		/*
		 * Step2: SCF flush - Clean and invalidate caches and clear the
		 * TR-bits
		 */
		ret = ari_ccplex_cache_clean_and_invalidate(mce_get_curr_cpu_ari_base());
		if (ret < 0) {
			ERROR("%s: flush cache_trbits failed(%d)\n", __func__,
				ret);
			return;
		}

		/*
		 * Step3: Issue the SECURITY_CONFIG request to MCE to enable
		 * strict checking mode.
		 */
		ari_enable_strict_checking_mode(mce_get_curr_cpu_ari_base());
	}
}
#endif

#if DEBUG
/*
 * attempt to transition the specified core into the DEBUG_RECOVERY Mode (DBGRM)
 * core_id: Core ID of Target Core to put into DBRGM
 * ret: [1:0]
 * 	 0 – Success. P-Channel returned ACCEPT.
 *	 1 – Deny. P-Channel returned Deny.
 *	 2 – Busy. P-Channel for core is currently in a handshake.
 *	 3 – Off. DSU is unpowered and cannot accept P-Channel requests.
 */
uint32_t mce_enter_debug_recovery_mode(uint32_t core_id)
{
	uint32_t ret;
	uint32_t ari_base = mce_get_curr_cpu_ari_base();

	/* transition the specified core into the DEBUG_RECOVERY Mode */
	ret = ari_core_debug_recovery(ari_base, core_id, false);
	if (ret == 0U) {
		/* transition the specified DSU into the DEBUG_RECOVERY
		 * and Warm Reset the requested cluster
		 */
		ret = ari_dsu_debug_recovery(ari_base,
		 (core_id / PLATFORM_MAX_CPUS_PER_CLUSTER), true);
	}

	return ret;
}
#endif

/******************************************************************************
 * Handler to power down the entire system
 *****************************************************************************/
void mce_system_shutdown(void)
{
	ari_system_shutdown(mce_get_curr_cpu_ari_base());
}

/******************************************************************************
 * Handler to reboot the entire system
 *****************************************************************************/
void mce_system_reboot(void)
{
	ari_system_reboot(mce_get_curr_cpu_ari_base());
}

/******************************************************************************
 * Return number of CPU cores
 *****************************************************************************/
uint32_t mce_num_cores(void)
{
	uint32_t num_cores = 1U;

	if (mce_firmware_not_supported())
		return num_cores;

	/* Issue ARI if bitmap is uninitialized */
	if (core_present_bitmap == 0xDEADFEED) {
		core_present_bitmap = ari_num_cores(mce_get_curr_cpu_ari_base());
	}

	for (int i = 1; i < PLATFORM_CORE_COUNT; i++) {
		if (core_present_bitmap & BIT(i))
			num_cores++;
	}

	return num_cores;
}

/******************************************************************************
 * Return true if a cluster is present
 *****************************************************************************/
bool mce_is_cluster_present(unsigned int id)
{
	unsigned int mode;

	/* Sanity check cluster id */
	if (id >= PLATFORM_CLUSTER_COUNT) {
		return false;
	}

	/* Cluster 0 is always present */
	if (mce_firmware_not_supported() && (id == 0U)) {
		return true;
	}

	/* Without MCE support we cannot assume the presence of other clusters */
	if (mce_firmware_not_supported() && (id != 0U)) {
		return false;
	}

	/* Issue ARI if the bitmap is uninitialized */
	if (cluster_present_bitmap == 0xDEADFEED) {
		cluster_present_bitmap = ari_cluster_present_map(mce_get_curr_cpu_ari_base());
	}

	/* Extract the mode */
	mode = (cluster_present_bitmap >> CLUSTER_MODE_SHIFT(id)) & CLUSTER_MODE_MASK;

	/* Any mode other than "disabled" means the cluster is present */
	return (mode != CLUSTER_MODE_DISABLED);
}

/******************************************************************************
 * Return true if a cluster is configured to run in lock mode
 *****************************************************************************/
bool mce_is_cluster_in_lock_mode(unsigned int id)
{
	unsigned int mode;

	/* Sanity checks */
	if ((id >= PLATFORM_CLUSTER_COUNT) || mce_firmware_not_supported()) {
		return false;
	}

	/* Issue ARI if the bitmap is uninitialized */
	if (cluster_present_bitmap == 0xDEADFEED) {
		cluster_present_bitmap = ari_cluster_present_map(mce_get_curr_cpu_ari_base());
	}

	/* Extract the mode */
	mode = (cluster_present_bitmap >> CLUSTER_MODE_SHIFT(id)) & CLUSTER_MODE_MASK;

	return (mode != CLUSTER_MODE_LOCK);
}

/******************************************************************************
 * Return true if a cluster is configured to run in split mode
 *****************************************************************************/
bool mce_is_cluster_in_split_mode(unsigned int id)
{
	unsigned int mode;

	/* Sanity checks */
	if ((id >= PLATFORM_CLUSTER_COUNT) || mce_firmware_not_supported()) {
		return false;
	}

	/* Issue ARI if the bitmap is uninitialized */
	if (cluster_present_bitmap == 0xDEADFEED) {
		cluster_present_bitmap = ari_cluster_present_map(mce_get_curr_cpu_ari_base());
	}

	/* Extract the mode */
	mode = (cluster_present_bitmap >> CLUSTER_MODE_SHIFT(id)) & CLUSTER_MODE_MASK;

	return (mode != CLUSTER_MODE_SPLIT);
}

/******************************************************************************
 * Return true if a cluster is configured to run in hybrid mode
 *****************************************************************************/
bool mce_is_cluster_in_hybrid_mode(unsigned int id)
{
	unsigned int mode;

	/* Sanity checks */
	if ((id >= PLATFORM_CLUSTER_COUNT) || mce_firmware_not_supported()) {
		return false;
	}

	/* Issue ARI if the bitmap is uninitialized */
	if (cluster_present_bitmap == 0xDEADFEED) {
		cluster_present_bitmap = ari_cluster_present_map(mce_get_curr_cpu_ari_base());
	}

	/* Extract the mode */
	mode = (cluster_present_bitmap >> CLUSTER_MODE_SHIFT(id)) & CLUSTER_MODE_MASK;

	return (mode != CLUSTER_MODE_HYBRID);
}
