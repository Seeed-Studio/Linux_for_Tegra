/*
 * Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <arch.h>
#include <arch_helpers.h>
#include <assert.h>
#include <common/debug.h>
#include <drivers/delay_timer.h>
#include <denver.h>
#include <lib/mmio.h>
#include <mce_private.h>
#include <platform_def.h>
#include <errno.h>
#include <t234_ari.h>
#include <tegra_private.h>

/******************************************************************************
 * Custom macros
 *****************************************************************************/
#define	ID_AFR0_EL1_CACHE_OPS_SHIFT	12U
#define	ID_AFR0_EL1_CACHE_OPS_MASK	0xFU

#define CORE_WARM_RESET_SHIFT		4U
#define CLUSTER_WARM_RESET_SHIFT	2U

#define CLUSTER_CONFIG_MASK		0x3FU

/******************************************************************************
 * Globals
 *****************************************************************************/
static uint32_t cluster_bitmap = U(0xDEADF00D);

/******************************************************************************
 * Register offsets for ARI request/results
 *****************************************************************************/
#define ARI_REQUEST			0x0U
#define ARI_REQUEST_EVENT_MASK		0x8U
#define ARI_STATUS			0x10U
#define ARI_REQUEST_DATA_LO		0x18U
#define ARI_REQUEST_DATA_HI		0x20U
#define ARI_RESPONSE_DATA_LO		0x28U
#define ARI_RESPONSE_DATA_HI		0x30U

/* Status values for the current request */
#define ARI_REQ_PENDING			1U
#define ARI_REQ_ONGOING			2U
/* Request status */
#define ARI_REQ_STATUS_MASK		0xFCU
#define ARI_REQ_NO_ERROR		0U
#define ARI_REQ_REQUEST_KILLED		1U
#define ARI_REQ_NS_ERROR		2U
#define ARI_REQ_EXECUTION_ERROR		0x3FU
/* Request control bits */
#define ARI_REQUEST_VALID_BIT		(1U << 8)
#define ARI_REQUEST_KILL_BIT		(1U << 9)
#define ARI_REQUEST_NS_BIT		(1U << 31)

/* default timeout (us) to wait for ARI completion */
#define ARI_MAX_RETRY_COUNT		U(2000000)

/******************************************************************************
 * ARI helper functions
 *****************************************************************************/
static inline uint32_t ari_read_32(uint32_t ari_base, uint32_t reg)
{
	return mmio_read_32((uint64_t)ari_base + (uint64_t)reg);
}

static inline void ari_write_32(uint32_t ari_base, uint32_t val, uint32_t reg)
{
	mmio_write_32((uint64_t)ari_base + (uint64_t)reg, val);
}

static inline uint32_t ari_get_response_low(uint32_t ari_base)
{
	return ari_read_32(ari_base, ARI_RESPONSE_DATA_LO);
}

static inline uint32_t ari_get_response_high(uint32_t ari_base)
{
	return ari_read_32(ari_base, ARI_RESPONSE_DATA_HI);
}

static inline void ari_clobber_response(uint32_t ari_base)
{
	ari_write_32(ari_base, 0, ARI_RESPONSE_DATA_LO);
	ari_write_32(ari_base, 0, ARI_RESPONSE_DATA_HI);
}

static void ari_request_send_command(uint32_t ari_base, uint32_t evt_mask,
		uint32_t req, uint32_t lo, uint32_t hi)
{
	ari_write_32(ari_base, lo, ARI_REQUEST_DATA_LO);
	ari_write_32(ari_base, hi, ARI_REQUEST_DATA_HI);
	ari_write_32(ari_base, evt_mask, ARI_REQUEST_EVENT_MASK);
	ari_write_32(ari_base, req | ARI_REQUEST_VALID_BIT, ARI_REQUEST);
}

static uint32_t ari_request_wait(uint32_t ari_base, uint32_t evt_mask,
		uint32_t req, uint32_t lo, uint32_t hi)
{
	uint32_t retries = ARI_MAX_RETRY_COUNT;
	uint32_t status;
	uint32_t ret = 0U;

	/*
	 * For each ARI command, the registers that are not used are listed
	 * as "Must be set to 0", MCE firmware enforce a check for it.
	 * Clear response lo/hi data before sending out command.
	 */
	ari_write_32(ari_base, 0, ARI_RESPONSE_DATA_LO);
	ari_write_32(ari_base, 0, ARI_RESPONSE_DATA_HI);

	/* program the request, event_mask, hi and lo registers */
	ari_request_send_command(ari_base, evt_mask, req, lo, hi);

	/*
	 * For commands that have an event trigger, we should stop
	 * ARI_STATUS polling, since MCE is waiting for SW to trigger
	 * the event.
	 */
	/* For shutdown/reboot commands, use function ari_request_send_command() */
	if (evt_mask == 0U) {
		/*
		 * Wait for the command response for not more than the timeout
		 */
		while (retries != 0U) {
			/* read the command status */
			status = ari_read_32(ari_base, ARI_STATUS);
			if ((status & (ARI_REQ_ONGOING | ARI_REQ_PENDING |
				 ARI_REQ_STATUS_MASK)) == 0U) {
				break;
			}

			if ((status & ARI_REQ_STATUS_MASK) != 0U) {
				ret = ((status & ARI_REQ_STATUS_MASK) >> 2);
				ERROR("ARI request get error: 0x%x\n", ret);
				return ret;
			}

			/* delay 1 us */
			udelay(1);

			/* decrement the retry count */
			retries--;
		}

		/* assert if the command timed out */
		if (retries == 0U) {
			ERROR("ARI request timed out: req %d\n", req);
			assert(retries != 0U);
		}
	}

	return 0;
}

/*
 * Reports the major and minor version of this interface.
 * ari_base: ari base for current CPU.
 * version: [63:32] Major version, [31:0] Minor version.
 */
uint64_t ari_get_version(uint32_t ari_base)
{
	uint64_t ret;

	/* clean the previous response state */
	ari_clobber_response(ari_base);

	ret = ari_request_wait(ari_base, 0U, (uint32_t)TEGRA_ARI_VERSION, 0, 0);

	if (ret == 0U) {
		ret = ari_get_response_low(ari_base);
		ret |= ((uint64_t)ari_get_response_high(ari_base) << 32);
	} else {
		ERROR("ARI request %s fail!\n", __func__);
	}

	return ret;
}

/*
 * return a bit-vector indicating which cores on the ccplex are enabled or
 * non-floorswept.
 * ari_base: ari base for current CPU.
 * num_cores: [15:0] bit-vector indicating which cores on the ccplex are enabled.
 */
uint32_t ari_num_cores(uint32_t ari_base)
{
	uint32_t ret;

	/* clean the previous response state */
	ari_clobber_response(ari_base);

	ret = ari_request_wait(ari_base, 0U, TEGRA_ARI_NUM_CORES, 0, 0);

	if (ret == 0U) {
		ret = ari_get_response_low(ari_base);
	} else {
		ERROR("ARI request %s fail!\n", __func__);
	}

	return (ret & 0xFFFFU);
}

/*
 * Return a bit-vector indicating which clusters on the ccplex are enabled or
 * non-floorswept.
 *
 * ari_base: ari base for current CPU.
 *
 * Return bit-vector indicating which clusters on the ccplex are enabled.
 */
uint32_t ari_cluster_present_map(uint32_t ari_base)
{
	uint32_t ret;

	if (cluster_bitmap != U(0xDEADF00D)) {
		return cluster_bitmap;
	}

	ret = ari_request_wait(ari_base, 0U, TEGRA_ARI_CLUSTER_CONFIG, 0U, 0U);
	if (ret == 0U) {
		cluster_bitmap = ari_get_response_low(ari_base);
		cluster_bitmap &= (CLUSTER_CONFIG_MASK);
	} else {
		ERROR("ARI request %s failed\n", __func__);
	}

	return cluster_bitmap;
}

#if DEBUG
/*
 * MCE SW will initiate the warm reset sequence to Warm Reset the requested
 * cluster. This ARI call is intended to be used in conjunction with
 * TEGRA_ARI_CORE_DEBUG_RECOVERY and TEGRA_ARI_DSU_DEBUG_RECOVERY to allow
 * the debugger to enable the DBGRM mode for the entire cluster.
 * ari_base: ari base for current CPU.
 * cluster_id: Cluster ID for the target cluster. Valid values are 0-2.
 * ret: error status, 0 indicate no error.
 */
static uint32_t ari_cluster_warm_reset(uint32_t ari_base, uint32_t cluster_id)
{
	uint32_t ret;

	assert(cluster_id < PLATFORM_CLUSTER_COUNT);

	/* clean the previous response state */
	ari_clobber_response(ari_base);

	ret = ari_request_wait(ari_base, 0U, TEGRA_ARI_CLUSTER_WARM_RESET,
		 cluster_id, 0);

	if (ret != 0U) {
		ERROR("ARI request %s fail!\n", __func__);
		if (ret == ARI_REQ_EXECUTION_ERROR) {
			ret = ari_get_response_low(ari_base);
		}
	}

	return ret;
}

/*
 * attempt to transition the specified core into the DEBUG_RECOVERY Mode (DBGRM)
 * ari_base: ari base for current CPU.
 * core_id: Core ID of Target Core to put into DBRGM
 * core_warm_reset: Request Cluster Warm Reset
 * 	 0 – No Cluster Warm Reset
 * 	 1 – Warm Reset Cluster after successful transition of core to DBGRM
 * ret: [1:0]
 * 	 0 – Success. P-Channel returned ACCEPT.
 *	 1 – Deny. P-Channel returned Deny.
 *	 2 – Busy. P-Channel for core is currently in a handshake.
 *	 3 – Off. DSU is unpowered and cannot accept P-Channel requests.
 * if the ARI Request register Status field has been set to 0x3f,
 * the ret value will be
 * ret: [31:0]
 *       error code indicate information about the error
 */
uint32_t ari_core_debug_recovery(uint32_t ari_base, uint32_t core_id,
	 bool core_warm_reset)
{
	uint32_t request, ret;

	assert(core_id <
		 (PLATFORM_CLUSTER_COUNT * PLATFORM_MAX_CPUS_PER_CLUSTER));

	request = ((uint32_t)core_warm_reset << CORE_WARM_RESET_SHIFT)
			 | core_id;

	/* clean the previous response state */
	ari_clobber_response(ari_base);

	ret = ari_request_wait(ari_base, 0U, TEGRA_ARI_CORE_DEBUG_RECOVERY,
		 request, 0);

	if (ret == 0U) {
		ret = ari_get_response_low(ari_base);
		if (core_warm_reset && (ret == 0)) {
			ret = ari_cluster_warm_reset(ari_base,
			 (core_id / PLATFORM_MAX_CPUS_PER_CLUSTER));
		}
	} else {
		ERROR("ARI request %s fail!\n", __func__);
		if (ret == ARI_REQ_EXECUTION_ERROR) {
			ret = ari_get_response_low(ari_base);
		}
	}

	return ret;
}

/*
 * attempt to transition the specified DSU into the DEBUG_RECOVERY Mode (DBGRM).
 * ari_base: ari base for current CPU.
 * cluster_id: Cluster ID for the target DSU. Valid values are 0-2.
 * cluster_warm_reset: Request Cluster Warm Reset
 * 	 0 – No Cluster Warm Reset
 * 	 1 – Warm Reset Cluster after successful transition of core to DBGRM
 * ret: [1:0]
 * 	 0 – Success. P-Channel returned ACCEPT.
 *	 1 – Deny. P-Channel returned Deny.
 *	 2 – Busy. P-Channel for core is currently in a handshake.
 *	 3 – Off. DSU is unpowered and cannot accept P-Channel requests.
 * if the ARI Request register Status field has been set to 0x3f,
 * the ret value will be
 * ret: [31:0]
 *       error code indicate information about the error
 */
uint32_t ari_dsu_debug_recovery(uint32_t ari_base, uint32_t cluster_id,
	 bool cluster_warm_reset)
{
	uint32_t request, ret;

	assert(cluster_id < PLATFORM_CLUSTER_COUNT);

	request = ((uint32_t)cluster_warm_reset << CLUSTER_WARM_RESET_SHIFT)
			 | cluster_id;

	/* clean the previous response state */
	ari_clobber_response(ari_base);

	ret = ari_request_wait(ari_base, 0U, TEGRA_ARI_DSU_DEBUG_RECOVERY,
		 request, 0);

	if (ret == 0U) {
		ret = ari_get_response_low(ari_base);
		if (cluster_warm_reset && (ret == 0)) {
			ret = ari_cluster_warm_reset(ari_base, cluster_id);
		}
	} else {
		ERROR("ARI request %s fail!\n", __func__);
		if (ret == ARI_REQ_EXECUTION_ERROR) {
			ret = ari_get_response_low(ari_base);
		}
	}

	return ret;
}

/*
 * MCE SW will enable the CCLA for internal debugging. This will cause an MCE
 * assert if used on a production fused part.
 * ari_base: ari base for current CPU.
 * ret: error status, 0 indicate no error.
 */
uint32_t ari_ccplex_latic_on(uint32_t ari_base)
{

	/* clean the previous response state */
	ari_clobber_response(ari_base);

	return ari_request_wait(ari_base, 0U, TEGRA_ARI_CCPLEX_LATIC_ON, 0, 0);

}
#endif

/*
 * Initiate an SCF level Cache Clean
 * ari_base: ari base for current CPU.
 * ret: error status, 0 indicate no error.
 */
uint32_t ari_ccplex_cache_clean(uint32_t ari_base)
{
	/* clean the previous response state */
	ari_clobber_response(ari_base);

	return ari_request_wait(ari_base, 0U, TEGRA_ARI_CCPLEX_CACHE_CLEAN,
		 TEGRA_ARI_CCPLEX_CACHE_CLEAN_NO_INVAL, 0);
}

/*
 * Initiate an SCF level Cache invalidate and Clean
 * ari_base: ari base for current CPU.
 * ret: error status, 0 indicate no error.
 */
uint32_t ari_ccplex_cache_clean_and_invalidate(uint32_t ari_base)
{
	/* clean the previous response state */
	ari_clobber_response(ari_base);

	return ari_request_wait(ari_base, 0U, TEGRA_ARI_CCPLEX_CACHE_CLEAN,
		 TEGRA_ARI_CCPLEX_CACHE_CLEAN_INVAL, 0);
}

/*
 * This function will request for the BPMP to power down the CCPLEX.
 * This can be used to either reboot or power off the system.
 * ari_base: ari base for current CPU.
 * type: 0 — Power Off SOC
 *	 1 — Reboot SOC
 */
static void ari_ccplex_shutdown(uint32_t ari_base, uint32_t type)
{
	/* sanity check for type */
	assert(type <= TEGRA_ARI_CCPLEX_SHUTDOWN_REBOOT);

	/* clean the previous response state */
	ari_clobber_response(ari_base);

	ari_request_send_command(ari_base, 0U, TEGRA_ARI_CCPLEX_SHUTDOWN,
		 type, 0);
}

/*
 * This request allows updating of CLUSTER_CSTATE, CCPLEX_CSTATE and
 * SYSTEM_CSTATE values.
 * ari_base: ari base for current CPU.
 * cluster: cluster CSTATE
 *	    1 — Auto-CC1 (reset value)
 *	    7 — CC7
 * update_cluster: 0 — Ignore value in Cluster CSTATE field
 *		   1 — Value in Cluster CSTATE field is valid and
 *		     should be updated
 * system: system CSTATE
 *	   0 << 16 — SC0 (reset value)
 *	   7 << 16 — SC7
 * update_system:  0 — Ignore value in system CSTATE field
 *		   1 — Value in system CSTATE field is valid and
 *		     should be updated
 * wake_mask: Mask of events to wake from low power states.
 * update_wake_mask: 0 — Ignore value in Wake Mask field
 *		     1 — Value in Wake Mask Field is valid and should be updated
 * ret: error status, 0 indicate no error.
 */
uint32_t ari_update_cstate_info(uint32_t ari_base, uint32_t cluster,
	 uint32_t system, uint32_t wake_mask, uint8_t update_wake_mask)
{
	uint32_t val = 0;

	/* update CLUSTER_CSTATE? */
	if (cluster != 0U) {
		val |= (cluster & CLUSTER_CSTATE_MASK) |
				TEGRA_ARI_CSTATE_INFO_UPDATE_CLUSTER;
	}

	/* update SYSTEM_CSTATE? */
	if (system != 0U) {
		val |= ((system & SYSTEM_CSTATE_MASK) << SYSTEM_CSTATE_SHIFT) |
				TEGRA_ARI_CSTATE_INFO_UPDATE_SYSTEM ;
	}

	/* update wake mask value? */
	if (update_wake_mask != 0U) {
		val |= TEGRA_ARI_CSTATE_INFO_UPDATE_WAKE_MASK;
	}

	/* clean the previous response state */
	ari_clobber_response(ari_base);

	/* set the updated cstate info */
	return ari_request_wait(ari_base, 0U, TEGRA_ARI_CSTATE_INFO,
				val, wake_mask);
}

/*
 * Return a non-zero value if the CCPLEX is able to enter SC7
 * ari_base: ari base for current CPU.
 * result: 0 — System is not allowed to enter SC7 and may reenter running state.
 *	   1 – System may enter SC7 and is committed to proceed to enter SC7.
 */
bool ari_is_sc7_allowed(uint32_t ari_base)
{
	bool result;
	uint32_t ret;

	/* clean the previous response state */
	ari_clobber_response(ari_base);

	/* issue command to check if SC7 is allowed */
	ret = ari_request_wait(ari_base, 0U, TEGRA_ARI_IS_SC7_ALLOWED, 0, 0);
	if (ret != 0) {
		ERROR("%s: failed (%d)\n", __func__, ret);
		result = false;
	} else {
		/* 1 = SC7 allowed, 0 = SC7 not allowed */
		result = (bool)ari_get_response_low(ari_base);
	}

	return result;
}

/*
 * Wake an offlined logical core. Note that a core is offlined by entering
 * a C-state where the WAKE_MASK is all 0.
 * ari_base: ari base for current CPU.
 * core: MPIDR Linear Core ID of the core to bring online.
 * ret: error status, 0 indicate no error.
 */
uint32_t ari_online_core(uint32_t ari_base, uint32_t core)
{
	assert(core <
		 (PLATFORM_CLUSTER_COUNT * PLATFORM_MAX_CPUS_PER_CLUSTER));

	/* clean the previous response state */
	ari_clobber_response(ari_base);

	/* get a core online */
	return ari_request_wait(ari_base, 0U, TEGRA_ARI_ONLINE_CORE, core, 0U);
}

/*
 * MC GSC (General Security Carveout) register values are expected to be
 * changed by TrustZone ARM code after boot.
 * ari_base: ari base for current CPU.
 * gsc_idx: The enumerated GSC value to be copied from the SOC into the CCPLEX
 *          !! only support GSC_VPR now !!
 * ret: error status, 0 indicate no error.
 */
int32_t ari_update_ccplex_gsc(uint32_t ari_base, uint32_t gsc_idx)
{
	/* sanity check GSC ID */
	if (gsc_idx != TEGRA_ARI_CARVEOUT_VPR) {
		return -EINVAL;
	}

	/* clean the previous response state */
	ari_clobber_response(ari_base);

	/*
	 * The MCE code will read the GSC carveout value, corrseponding
	 * to the ID, from the MC registers and update the internal GSC
	 * registers of the CCPLEX.
	 */
	return ari_request_wait(ari_base, 0U,
		 TEGRA_ARI_UPDATE_CCPLEX_CARVEOUTS, gsc_idx, 0U);
}


/*
 * Set the power state for a core
 * ari_base: ari base for current CPU.
 * wake_time: Time in TSC ticks until the core is expected to get a wake event.
 *	      0 indicates the core is expected to wake immediately.
 *	      0xffffffff indicates that the core is not expected
 *	      to get a wake event.
 * ret: error status, 0 indicate no error.
 */
uint32_t ari_enter_cstate(uint32_t ari_base, uint32_t wake_time)
{
/*
 * should have C1 state according CCPLEX_Power_Management_GFD
 * but only wake_time as input parameter in MCE_ISS.
 */

	/* time (TSC ticks) until the core is expected to get a wake event */

	/* clean the previous response state */
	ari_clobber_response(ari_base);

	/* send the command and do not wait for the ARI to complete*/
	ari_request_send_command(ari_base, TEGRA_ARI_EVENT_CORE_OFF_MASK,
		TEGRA_ARI_ENTER_CSTATE, wake_time, 0);

	return 0;
}

#if ENABLE_STRICT_CHECKING_MODE
/*
 * Enable strict checking mode and verify the result.
 * ari_base: ari base for current CPU.
 */
void ari_enable_strict_checking_mode(uint32_t ari_base)
{
	uint32_t params;
	uint32_t ret = 0U;

	/* Clean the previous response state */
	ari_clobber_response(ari_base);

	/* Enable the strict checking mode
	 * Lock the strict enable bit.
	 * Once the bit is set, write to strict_en bit will be ignored
	 */
	params = TEGRA_ARI_SECURITY_CONFIG_STRICT_EN_ENABLE
		 | TEGRA_ARI_SECURITY_CONFIG_STRICT_LOCK
		 | TEGRA_ARI_SECURITY_CONFIG_WRITE_EN;
	ret = ari_request_wait(ari_base, 0, TEGRA_ARI_SECURITY_CONFIG,
		 params, 0);
	if (ret == 0) {
		params = ari_get_response_low(ari_base);
	}

	assert(params == (TEGRA_ARI_SECURITY_CONFIG_STRICT_EN_ENABLE
		 | TEGRA_ARI_SECURITY_CONFIG_STRICT_LOCK));
}
#endif

/*
 * Request a reboot
 */
void ari_system_reboot(uint32_t ari_base)
{
	ari_ccplex_shutdown(ari_base, TEGRA_ARI_CCPLEX_SHUTDOWN_REBOOT);
}

/*
 * Request a shutdown
 */
void ari_system_shutdown(uint32_t ari_base)
{
	ari_ccplex_shutdown(ari_base, TEGRA_ARI_CCPLEX_SHUTDOWN_POWER_OFF);
}
