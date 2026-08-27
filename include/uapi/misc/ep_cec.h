/* SPDX-License-Identifier: GPL-2.0-only
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */
#ifndef __UAPI_EP_CEC_H
#define __UAPI_EP_CEC_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define EP_CEC_MESSAGE_SIZE		18
#define CEC_REG_DUMP_SIZE		2048

struct ep_cec_message {
	__u8 data[EP_CEC_MESSAGE_SIZE];
};

#define EP_CEC_IOCTL_MAGIC 'C'
/*
 * EP_CEC_IOCTL_DUMP_REG:
 *
 * Dump the contents of the CEC registers.
 * The caller must provide a buffer of size CEC_REG_DUMP_SIZE.
 *
 * Use 65 as the ioctl command number since 1-64 are reserved by Tegra CEC driver.
 */
#define EP_CEC_IOCTL_DUMP_REG		_IOR(EP_CEC_IOCTL_MAGIC, 65, char[CEC_REG_DUMP_SIZE])

#endif /* __UAPI_EP_CEC_H */
