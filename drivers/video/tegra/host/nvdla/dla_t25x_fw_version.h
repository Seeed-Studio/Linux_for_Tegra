/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2020-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * NVDLA OS Interface
 */

#ifndef DLA_T25X_FW_VERSION_H
#define DLA_T25X_FW_VERSION_H

#define FIRMWARE_T25X_VERSION_MAJOR		((uint32_t)0x3U)
#define FIRMWARE_T25X_VERSION_MINOR		((uint32_t)0x0U)
#define FIRMWARE_T25X_VERSION_SUBMINOR		((uint32_t)0x0U)

static inline uint32_t dla_t25x_fw_version(void)
{
	return (((FIRMWARE_T25X_VERSION_MAJOR & 0xffU) << 16) |
			((FIRMWARE_T25X_VERSION_MINOR & 0xffU) << 8) |
			((FIRMWARE_T25X_VERSION_SUBMINOR & 0xffU)));
}

#endif /* End of DLA_T23X_FW_VERSION_H */
