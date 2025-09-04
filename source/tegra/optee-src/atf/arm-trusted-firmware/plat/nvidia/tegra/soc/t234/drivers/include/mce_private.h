/*
 * Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __MCE_PRIVATE_H__
#define __MCE_PRIVATE_H__

#include <stdbool.h>
#include <tegra_def.h>

/*******************************************************************************
 * Macros to prepare CSTATE info request
 ******************************************************************************/
/* Description of the parameters for UPDATE_CSTATE_INFO request */
#define CLUSTER_CSTATE_MASK			0x7U
#define CLUSTER_CSTATE_SHIFT			0X0U
#define SYSTEM_CSTATE_MASK			0xFU
#define SYSTEM_CSTATE_SHIFT			16U

/*******************************************************************************
 * Core ID mask (bits 3:0 in the online request)
 ******************************************************************************/
#define MCE_CORE_ID_MASK			0xFU

/*******************************************************************************
 * C-state statistics macros
 ******************************************************************************/
#define MCE_STAT_ID_SHIFT			16U

/*******************************************************************************
 * Uncore PERFMON ARI macros
 ******************************************************************************/
#define UNCORE_PERFMON_CMD_READ			U(0)
#define UNCORE_PERFMON_CMD_WRITE		U(1)

#define UNCORE_PERFMON_CMD_MASK			U(0xFF)
#define UNCORE_PERFMON_UNIT_GRP_MASK		U(0xF)
#define UNCORE_PERFMON_SELECTOR_MASK		U(0xF)
#define UNCORE_PERFMON_REG_MASK			U(0xFF)
#define UNCORE_PERFMON_CTR_MASK			U(0xFF)
#define UNCORE_PERFMON_RESP_STATUS_MASK		U(0xFF)

/*******************************************************************************
 * CLUSTER_CONFIG ARI macros
 ******************************************************************************/
#define CLUSTER_MODE_SHIFT(cluster)		((cluster) << 1U)
#define CLUSTER_MODE_MASK			0x3U
#define CLUSTER_MODE_DISABLED			0U
#define CLUSTER_MODE_SPLIT			1U
#define CLUSTER_MODE_HYBRID			2U
#define CLUSTER_MODE_LOCK			3U

/* declarations for ARI handler functions */
/* Function from NS world */
uint64_t ari_get_version(uint32_t ari_base);
uint32_t ari_num_cores(uint32_t ari_base);
uint32_t ari_cluster_present_map(uint32_t ari_base);
uint32_t ari_ccplex_cache_clean(uint32_t ari_base);
uint32_t ari_ccplex_cache_clean_and_invalidate(uint32_t ari_base);
/* Function for secure world */
#if DEBUG
uint32_t ari_core_debug_recovery(uint32_t ari_base, uint32_t core_id, bool core_warm_reset);
uint32_t ari_dsu_debug_recovery(uint32_t ari_base, uint32_t cluster_id, bool cluster_warm_reset);
uint32_t ari_ccplex_latic_on(uint32_t ari_base);
#endif
uint32_t ari_online_core(uint32_t ari_base, uint32_t core);
uint32_t ari_enter_cstate(uint32_t ari_base, uint32_t wake_time);
uint32_t ari_trigger_online_ist(uint32_t ari_base, uint32_t slice_id);
uint32_t ari_update_cstate_info(uint32_t ari_base, uint32_t cluster,
	 uint32_t system, uint32_t wake_mask, uint8_t update_wake_mas);
bool ari_is_sc7_allowed(uint32_t ari_base);
void ari_enable_strict_checking_mode(uint32_t ari_base);
int32_t ari_update_ccplex_gsc(uint32_t ari_base, uint32_t gsc_idx);

void ari_system_shutdown(uint32_t ari_base);
void ari_system_reboot(uint32_t ari_base);

/* MCE helper functions */
void mce_clean_and_invalidate_caches(void);
void mce_enable_strict_checking(void);
uint32_t mce_enter_debug_recovery_mode(uint32_t core_id);
void mce_system_shutdown(void);
void mce_system_reboot(void);
uint32_t mce_num_cores(void);
bool mce_is_cluster_present(unsigned int id);
bool mce_is_cluster_in_lock_mode(unsigned int id);
bool mce_is_cluster_in_split_mode(unsigned int id);
bool mce_is_cluster_in_hybrid_mode(unsigned int id);

#endif /* __MCE_PRIVATE_H__ */
