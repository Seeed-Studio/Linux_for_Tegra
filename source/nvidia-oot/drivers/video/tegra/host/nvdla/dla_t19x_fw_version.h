/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2023, NVIDIA Corporation.  All rights reserved.
 *
 * NVDLA OS Interface
 */

#ifndef DLA_T19X_FW_VERSION_H
#define DLA_T19X_FW_VERSION_H

#define FIRMWARE_T19X_VERSION_MAJOR		((uint32_t)0x1U)
#define FIRMWARE_T19X_VERSION_MINOR		((uint32_t)0x2U)
#define FIRMWARE_T19X_VERSION_SUBMINOR		((uint32_t)0x0U)

static inline uint32_t dla_t19x_fw_version(void)
{
	return (((FIRMWARE_T19X_VERSION_MAJOR & 0xffU) << 16) |
			((FIRMWARE_T19X_VERSION_MINOR & 0xffU) << 8) |
			((FIRMWARE_T19X_VERSION_SUBMINOR & 0xffU)));
}

#endif /* End of DLA_T19X_FW_VERSION_H */
