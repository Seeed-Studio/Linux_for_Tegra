/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 */

#ifndef _NVVSE_COMMS_LIB_PRIV_H_
#define _NVVSE_COMMS_LIB_PRIV_H_

#include <linux/errno.h>
#include <linux/types.h>
#include <soc/tegra/virt/hv-ivc.h>
#include <linux/platform_device.h>

struct comms_handle_priv {
	uint32_t comm_id;
	struct device *dev;
	int read_size;
	struct tegra_hv_ivc_cookie *ivck;
	struct tegra_virtual_se_ivc_msg_t *ivc_msg;
	struct tegra_virtual_se_ivc_msg_t *ivc_resp_msg;
};

#endif /* _NVVSE_COMMS_LIB_PRIV_H_ */
