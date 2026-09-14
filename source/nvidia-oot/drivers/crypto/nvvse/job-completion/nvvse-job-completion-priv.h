/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 */

#ifndef _NVVSE_JOB_COMPLETION_PRIV_H_
#define _NVVSE_JOB_COMPLETION_PRIV_H_

#include <linux/platform_device.h>
#include <linux/host1x.h>
#include <linux/of_device.h>
#include "virt_se_interface_resp_tegra.h"

struct se_job_completion_device_handle_priv {
	/** Holds the device pointer */
	struct device *dev;
	/** Holds the host1x pointer */
	struct host1x *host1x;
};

struct se_job_completion_wait_handle_priv {
	/** Holds the device pointer */
	struct device *dev;
	/** Holds the host1x pointer */
	struct host1x *host1x;
};

#endif /* _NVVSE_JOB_COMPLETION_PRIV_H_ */
