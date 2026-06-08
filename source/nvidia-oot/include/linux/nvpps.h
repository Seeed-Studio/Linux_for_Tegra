/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef __NVPPS_H__
#define __NVPPS_H__

/*
 * Get PTP time
 * Clients may call the API every and anytime PTP time is needed.
 * If PTP time source is not registered, returns -EINVAL
 *
 * This API is available irrespective of nvpps dt availablity
 * When nvpps dt node is not present, interface name will
 * default to "eth0".
 */
int nvpps_get_ptp_ts(void *ts);

#endif /* __NVPPS_H__ */
