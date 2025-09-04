// -----------------------------------------------------------------------
//
//   Copyright (c) 2019-2021 NVIDIA Corporation - All Rights Reserved
//
//   This source module contains confidential and proprietary information
//   of NVIDIA Corporation.  It is not to be disclosed or used except
//   in accordance with applicable agreements.  This copyright notice does
//   not evidence any actual or intended publication of such source code.
//
// -----------------------------------------------------------------------
#ifndef T234_ARI_H
#define T234_ARI_H
/*  ARI Version numbers */
#define TEGRA_ARI_VERSION_MAJOR  8UL
#define TEGRA_ARI_VERSION_MINOR  1UL

/*
 *  ARI Request IDs
 *
 *   TODO: RENUMBER range before finalization
 *   NOTE: for documentation purposes, only documenting gaps
 *   in ranges, to indicate that we know about the missing ids
 *
 *   Require NO LAB Locks
 *   range from 0 - 31
 */
#define TEGRA_ARI_VERSION                      0UL
#define TEGRA_ARI_ECHO                         1UL
#define TEGRA_ARI_NUM_CORES                    2UL
#define TEGRA_ARI_CSTATE_STAT_QUERY            3UL
#define TEGRA_ARI_CLUSTER_CONFIG               4UL
/* Undefined                                   5 - 28 */
/*
 * Debug Only ARIs at the end of the NO LAB Lock Range
 */
#define TEGRA_ARI_CORE_DEBUG_RECOVERY          29UL
#define TEGRA_ARI_DSU_DEBUG_RECOVERY           30UL
#define TEGRA_ARI_CLUSTER_WARM_RESET           31UL
/*
 *   Require CORE LAB Lock -- obtained by MTM from ARI
 *   range from 32 - 63
 */
/* UNDEFINED                                   32 */
/* UNDEFINED                                   33 */
#define TEGRA_ARI_ONLINE_CORE                  34UL
#define TEGRA_ARI_ENTER_CSTATE                 35UL
#define TEGRA_ARI_PRETRIGGER_ONLINE_IST        36UL
#define TEGRA_ARI_TRIGGER_ONLINE_IST           37UL
#define TEGRA_ARI_ONLINE_IST_STATUS            38UL
#define TEGRA_ARI_ONLINE_IST_DMA_STATUS        39UL
/*
 *   Require CLUSTER and CORE LAB Lock -- obtained by MTM from ARI
 *   range from 64 - 95
 */
/* UNDEFINED                                   64 */
#define TEGRA_ARI_NVFREQ_REQ                   65UL
#define TEGRA_ARI_NVFREQ_FEEDBACK              66UL
#define TEGRA_ARI_CLUSTER_ATCLKEN              67UL
/*
 *   Require CCPLEX, CLUSTER and CORE LAB Lock -- obtained by MTM from ARI
 *   range from 96 - 127
 */
#define TEGRA_ARI_CCPLEX_CACHE_CONTROL         96UL
#define TEGRA_ARI_CCPLEX_CACHE_CLEAN           97UL
/* UNDEFINED                                   98 */
#define TEGRA_ARI_CCPLEX_LATIC_ON              99UL
#define TEGRA_ARI_UPDATE_CROSSOVER             100UL
#define TEGRA_ARI_VALIDATE_ONLINE_IST          101UL
#define TEGRA_ARI_CCPLEX_SHUTDOWN              102UL
/* UNDEFINED                                   103 */
#define TEGRA_ARI_CSTATE_INFO                  104UL
#define TEGRA_ARI_IS_SC7_ALLOWED               105UL
/* UNDEFINED                                   106 */
/* UNDEFINED                                   107 */
#define TEGRA_ARI_SECURITY_CONFIG              108UL
#define TEGRA_ARI_UPDATE_CCPLEX_CARVEOUTS      109UL
#define TEGRA_ARI_DDA_CONTROL                  110UL
#define TEGRA_ARI_PERFMON                      111UL
#define TEGRA_ARI_DEBUG_CONFIG                 112UL
#define TEGRA_ARI_CCPLEX_ERROR_RECOVERY_RESET  114UL

/* EVENT MASKS */
#define TEGRA_ARI_EVENT_CORE_OFF_MASK      (1UL<<0)
#define TEGRA_ARI_EVENT_CORE_OFF_EMU_MASK  (1UL<<1)
#define TEGRA_ARI_EVENT_WAKE_REQUEST_MASK  (1UL<<2)

////////////////////////////////////////////////////
//
// Defines for ARI Parameters
//
//  TEGRA_ARI_VERSION                          NONE
//  TEGRA_ARI_ECHO                             NONE
//  TEGRA_ARI_NUM_CORES                        NONE
//  TEGRA_ARI_CLUSTER_CONFIG
#define TEGRA_ARI_CLUSTER0_CONFIG_FLOORSWEPT 0UL
#define TEGRA_ARI_CLUSTER0_CONFIG_SPLIT      1UL
#define TEGRA_ARI_CLUSTER0_CONFIG_HYBRID     2UL
#define TEGRA_ARI_CLUSTER0_CONFIG_LOCK       3UL
#define TEGRA_ARI_CLUSTER1_CONFIG_FLOORSWEPT (0UL<<2)
#define TEGRA_ARI_CLUSTER1_CONFIG_SPLIT      (1UL<<2)
#define TEGRA_ARI_CLUSTER1_CONFIG_HYBRID     (2UL<<2)
#define TEGRA_ARI_CLUSTER1_CONFIG_LOCK       (3UL<<2)
#define TEGRA_ARI_CLUSTER2_CONFIG_FLOORSWEPT (0UL<<4)
#define TEGRA_ARI_CLUSTER2_CONFIG_SPLIT      (1UL<<4)
#define TEGRA_ARI_CLUSTER2_CONFIG_HYBRID     (2UL<<4)
#define TEGRA_ARI_CLUSTER2_CONFIG_LOCK       (3UL<<4)
//  TEGRA_ARI_CSTATE_STAT_QUERY
#define TEGRA_ARI_STAT_QUERY_SC7_ENTRIES        1UL
#define TEGRA_ARI_STAT_QUERY_CC7_ENTRIES        6UL
#define TEGRA_ARI_STAT_QUERY_C7_ENTRIES         14UL
#define TEGRA_ARI_STAT_QUERY_SC7_ENTRY_TIME_SUM 60UL
#define TEGRA_ARI_STAT_QUERY_CC7_ENTRY_TIME_SUM 61UL
#define TEGRA_ARI_STAT_QUERY_C7_ENTRY_TIME_SUM  64UL
#define TEGRA_ARI_STAT_QUERY_SC7_EXIT_TIME_SUM  70UL
#define TEGRA_ARI_STAT_QUERY_CC7_EXIT_TIME_SUM  71UL
#define TEGRA_ARI_STAT_QUERY_C7_EXIT_TIME_SUM   74UL

//  TEGRA_ARI_CORE_DEBUG_RECOVERY
#define TEGRA_ARI_CORE_DEBUG_RECOVERY_RCWR_NONE 0UL
#define TEGRA_ARI_CORE_DEBUG_RECOVERY_RCWR_WARM_RESET (1UL<<4)

#define TEGRA_ARI_CORE_DEBUG_RECOVERY_RESULT_SUCCESS 0UL
#define TEGRA_ARI_CORE_DEBUG_RECOVERY_RESULT_DENIED  1UL
#define TEGRA_ARI_CORE_DEBUG_RECOVERY_RESULT_BUSY    2UL
#define TEGRA_ARI_CORE_DEBUG_RECOVERY_RESULT_OFF     3UL

//  TEGRA_ARI_DSU_DEBUG_RECOVERY
#define TEGRA_ARI_DSU_DEBUG_RECOVERY_RCWR_NONE 0UL
#define TEGRA_ARI_DSU_DEBUG_RECOVERY_RCWR_WARM_RESET (1UL<<2)

#define TEGRA_ARI_DSU_DEBUG_RECOVERY_RESULT_SUCCESS 0UL
#define TEGRA_ARI_DSU_DEBUG_RECOVERY_RESULT_DENIED  1UL
#define TEGRA_ARI_DSU_DEBUG_RECOVERY_RESULT_BUSY    2UL
#define TEGRA_ARI_DSU_DEBUG_RECOVERY_RESULT_OFF     3UL

//  TEGRA_ARI_CLUSTER_WARM_RESET               NONE
//  TEGRA_ARI_ONLINE_CORE                      NONE
//  TEGRA_ARI_ENTER_CSTATE
#define TEGRA_ARI_ENTER_CSTATE_WAKE_TIME_NO_WAKE (0xffffffffUL)

//  TEGRA_ARI_TRIGGER_ONLINE_IST
#define TEGRA_ARI_TRIGGER_ONLINE_IST_SELECT_PLL_NAFLL 0UL
#define TEGRA_ARI_TRIGGER_ONLINE_IST_SELECT_PLL_PLLX1 1UL

//  TEGRA_ARI_ONLINE_IST_STATUS
#define TEGRA_ARI_ONLINE_IST_STATUS_RESET       0UL
#define TEGRA_ARI_ONLINE_IST_STATUS_TEST_PASS   1UL
#define TEGRA_ARI_ONLINE_IST_STATUS_TEST_FAIL   2UL

//  TEGRA_ARI_ONLINE_IST_DMA_STATUS
#define TEGRA_ARI_ONLINE_IST_DMA_STATUS_PENDING_TRANSACTIONS_FALSE 0UL
#define TEGRA_ARI_ONLINE_IST_DMA_STATUS_PENDING_TRANSACTIONS_TRUE  1UL

//  TEGRA_ARI_NVFREQ_REQ
#define TEGRA_ARI_NVFREQ_REQ_CORE_NDIV_WRITE_EN (1UL<<15)
#define TEGRA_ARI_NVFREQ_REQ_DSU_NDIV_WRITE_EN  (1UL<<15)

//  TEGRA_ARI_NVFREQ_FEEDBACK                  NONE
//  TEGRA_ARI_CLUSTER_ATCLKEN
#define TEGRA_ARI_CLUSTER_ATCLKEN_DISABLE        0UL
#define TEGRA_ARI_CLUSTER_ATCLKEN_ENABLE         1UL
#define TEGRA_ARI_CLUSTER_ATCLKEN_RESULT_SUCCESS 0UL
#define TEGRA_ARI_CLUSTER_ATCLKEN_RESULT_FAILURE 1UL
#define TEGRA_ARI_CLUSTER_ATCLKEN_RESULT_NONE    2UL

//  TEGRA_ARI_CCPLEX_CACHE_CONTROL
#define TEGRA_ARI_CCPLEX_CACHE_CONTROL_WRITE_EN (1UL<<15)

//  TEGRA_ARI_CCPLEX_CACHE_CLEAN
#define TEGRA_ARI_CCPLEX_CACHE_CLEAN_NO_INVAL 0UL
#define TEGRA_ARI_CCPLEX_CACHE_CLEAN_INVAL    1UL

//  TEGRA_ARI_CCPLEX_LATIC_ON                  NONE
//  TEGRA_ARI_UPDATE_CROSSOVER
#define TEGRA_ARI_CROSSOVER_C7_LOWER_BOUND  0UL
#define TEGRA_ARI_CROSSOVER_CC7_LOWER_BOUND 1UL
//  TEGRA_ARI_VALIDATE_ONLINE_IST
#define TEGRA_ARI_VALIDATE_OIST_CLUSTER0_SUCCESS    0UL
#define TEGRA_ARI_VALIDATE_OIST_CLUSTER0_INELIGIBLE 1UL
#define TEGRA_ARI_VALIDATE_OIST_CLUSTER0_FAILED     2UL
#define TEGRA_ARI_VALIDATE_OIST_CLUSTER1_SUCCESS    (0UL<<2)
#define TEGRA_ARI_VALIDATE_OIST_CLUSTER1_INELIGIBLE (1UL<<2)
#define TEGRA_ARI_VALIDATE_OIST_CLUSTER1_FAILED     (2UL<<2)
#define TEGRA_ARI_VALIDATE_OIST_CLUSTER2_SUCCESS    (0UL<<4)
#define TEGRA_ARI_VALIDATE_OIST_CLUSTER2_INELIGIBLE (1UL<<4)
#define TEGRA_ARI_VALIDATE_OIST_CLUSTER2_FAILED     (2UL<<4)
//  TEGRA_ARI_CCPLEX_SHUTDOWN
#define TEGRA_ARI_CCPLEX_SHUTDOWN_POWER_OFF 0UL
#define TEGRA_ARI_CCPLEX_SHUTDOWN_REBOOT    1UL

//  TEGRA_ARI_CSTATE_INFO
#define TEGRA_ARI_CSTATE_INFO_CLUSTER_CC1      1UL
#define TEGRA_ARI_CSTATE_INFO_CLUSTER_CC7      7UL
#define TEGRA_ARI_CSTATE_INFO_SYSTEM_SC0       (0UL<<16)
#define TEGRA_ARI_CSTATE_INFO_SYSTEM_SC7       (7UL<<16)
#define TEGRA_ARI_CSTATE_INFO_UPDATE_CLUSTER   (1UL<<7)
#define TEGRA_ARI_CSTATE_INFO_UPDATE_SYSTEM    (1UL<<23)
#define TEGRA_ARI_CSTATE_INFO_UPDATE_WAKE_MASK (1UL<<31)

//  TEGRA_ARI_IS_SC7_ALLOWED
#define TEGRA_ARI_IS_SC7_ALLOWED_FALSE 0UL
#define TEGRA_ARI_IS_SC7_ALLOWED_TRUE  1UL

//  TEGRA_ARI_SECURITY_CONFIG
#define TEGRA_ARI_SECURITY_CONFIG_STRICT_EN_DISABLE 0UL
#define TEGRA_ARI_SECURITY_CONFIG_STRICT_EN_ENABLE  1UL
#define TEGRA_ARI_SECURITY_CONFIG_STRICT_LOCK       (1UL<<1)
#define TEGRA_ARI_SECURITY_CONFIG_WRITE_EN          (1UL<<2)

//  TEGRA_ARI_UPDATE_CCPLEX_CARVEOUTS
#define TEGRA_ARI_CARVEOUT_ALL           0UL
#define TEGRA_ARI_CARVEOUT_GSC_NONE      0xFFFFUL
#define TEGRA_ARI_CARVEOUT_GSC_IST      31UL
#define TEGRA_ARI_CARVEOUT_SBS          (1UL<<16)
#define TEGRA_ARI_CARVEOUT_VPR          (1UL<<17)
//  TEGRA_ARI_DDA_CONTROL
#define DDA_SNOC_MCF                     0U
#define DDA_MCF_ORD1                     1U
#define DDA_MCF_ORD2                     2U
#define DDA_MCF_ORD3                     3U
#define DDA_MCF_ISO                      4U
#define DDA_MCF_SISO                     5U
#define DDA_MCF_NISO                     6U
#define DDA_MCF_NISO_REMOTE              7U
#define DDA_L3CTRL_ISO                   8U
#define DDA_L3CTRL_SISO                  9U
#define DDA_L3CTRL_NISO                 10U
#define DDA_L3CTRL_NISO_REMOTE          11U
#define DDA_L3CTRL_L3FILL               12U
#define DDA_L3CTRL_L3WR                 13U
#define DDA_L3CTRL_RSP_L3RD_DMA         14U
#define DDA_L3CTRL_RSP_MCFRD_DMA        15U
#define DDA_L3CTRL_GLOBAL               16U
#define DDA_L3CTRL_LL                   17U
#define DDA_L3CTRL_L3D                  18U
#define DDA_L3CTRL_FCM_RD               19U
#define DDA_L3CTRL_FCM_WR               20U
#define DDA_SNOC_GLOBAL_CTRL            21U
#define DDA_SNOC_CLIENT_REQ_CTRL        22U
#define DDA_SNOC_CLIENT_REPLENTISH_CTRL 23U

#define TEGRA_ARI_DDA_CONTROL_DDA_REG_WRITE_EN (1UL<<15)

//  TEGRA_ARI_PERFMON                          NONE
//  TEGRA_ARI_DEBUG_CONFIG
#define TEGRA_ARI_DEBUG_CONFIG_NOTIFY_ON_DCLS_FAULT_CP0     (0UL<<2)
#define TEGRA_ARI_DEBUG_CONFIG_NOTIFY_ON_DCLS_FAULT_CP1     (1UL<<2)
#define TEGRA_ARI_DEBUG_CONFIG_NOTIFY_ON_DCLS_FAULT_CP0CP1  (2UL<<2)
#define TEGRA_ARI_DEBUG_CONFIG_NOTIFY_ON_DCLS_FAULT_DISABLE (3UL<<2)

//  TEGRA_ARI_CCPLEX_ERROR_RECOVERY_RESET      NONE
// Primary ERROR CODES - valid in RSP_LO
#define TEGRA_ARI_ERROR_SW            1UL
// not defined                        2UL
// not defined                        3UL
// not defined                        4UL
// not defined                        5UL
// not defined                        6UL
#define TEGRA_ARI_ERROR_RESERVED      7UL
#define TEGRA_ARI_ERROR_OUT_OF_RANGE  8UL
#define TEGRA_ARI_ERROR_RESTRICTED    9UL
#define TEGRA_ARI_ERROR_NONSECURE    10UL
// Secondary ERROR CODES - valid in RSP_HI
// none currently defined
#endif /* T234_ARI_H */
