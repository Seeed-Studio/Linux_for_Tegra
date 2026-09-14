/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef __NVPPS_H__
#define __NVPPS_H__

/**
 * @defgroup timesync_user_api_group NvPPS user APIs/IOCTL
 * @{
 */

/**
 * @brief Read PTP Time from primary PTP network interface
 *
 * @param[out] ts Pointer to variable to hold the ptp time
 *
 * @return
 * - 0 for success
 * - -ve value in error case
 *
 * Behaviour of API and user expectations:
 * - This API is expected to be used by in kernel clients of NvPPS module
 * - This API returns HW PTP time from primary PTP network interface
 * - Hardware PTP timestamp represents the actual PTP clock time in Primary PTP interface
 *
 * @pre
 * - The NvPPS driver should be initialized and active
 * - PTP client daemon should be running on primary interfaces.
 *   This is needed because only after starting PTP client daemon on the primary PTP interface
 *   the tegra network driver registers its interface to read PTP time from primary PTP interface
 *   If this IOCTL is excersized before starting the PTP client daemon, then the value returned in PTP
 *   timestamp field will be set to 0 and an error msg is shown in the kernel dmesg log.
 *
 * @post None
 *
 * @usage
 * - Allowed context for the Kernel API call
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: Yes
 *   - Async/Sync: Sync
 * - API Group
 *   - Init: Yes
 *   - Runtime: Yes
 *   - De-Init: Yes
 *
 */

int nvpps_get_ptp_ts(void *ts);

/**
 * @}
 */
#endif /* __NVPPS_H__ */
