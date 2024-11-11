/* SPDX-License-Identifier: LicenseRef-NvidiaProprietary */
/* SPDX-FileCopyrightText: Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES.  All rights reserved.
 *
 * NVDLA NVHOST-WRAPPER
 */

#ifndef __NVDLA_HOST_WRAPPER_H__
#define __NVDLA_HOST_WRAPPER_H__

#if defined(NVDLA_HAVE_CONFIG_AXI) && (NVDLA_HAVE_CONFIG_AXI == 1)
#include <linux/nvhost-emu.h>

struct nvhost_notification {
	struct {            /* 0000- */
		__u32 nanoseconds[2];   /* nanoseconds since Jan. 1, 1970 */
	} time_stamp;           /* -0007 */
	__u32 info32;   /* info returned depends on method 0008-000b */
	__u16 info16;   /* info returned depends on method 000c-000d */
	__u16 status;   /* user sets bit 15, NV sets status 000e-000f */
};

#else
#include <linux/nvhost.h>
#endif /* NVDLA_HAVE_CONFIG_AXI */

#endif /*__NVDLA_HOST_WRAPPER_H__ */
