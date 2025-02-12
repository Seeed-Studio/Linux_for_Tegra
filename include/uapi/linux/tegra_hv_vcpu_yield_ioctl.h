/* SPDX-License-Identifier: (GPL-2.0 WITH Linux-syscall-note)
 *
 * Copyright (c) 2022-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef TEGRA_HV_VCPU_YIELD_IOCTL_H
#define TEGRA_HV_VCPU_YIELD_IOCTL_H

#include <linux/ioctl.h>

#define VCPU_YIELD_IOC_MAGIC    'Y'
#define VCPU_YIELD_START_CMDID  1

/* Control data for VCPU Yield Start ioctl */
struct vcpu_yield_start_ctl {
	/* max time in micro seconds for which vcpu will be yielded */
	uint32_t timeout_us;
};

#define VCPU_YIELD_START_IOCTL _IOW(VCPU_YIELD_IOC_MAGIC, \
		VCPU_YIELD_START_CMDID, struct vcpu_yield_start_ctl)

#endif /* #ifndef TEGRA_HV_VCPU_YIELD_IOCTL_H */
