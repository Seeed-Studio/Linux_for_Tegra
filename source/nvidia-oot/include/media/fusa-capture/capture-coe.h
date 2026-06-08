// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __FUSA_CAPTURE_COE_H__
#define __FUSA_CAPTURE_COE_H__

#if defined(__KERNEL__)
#include <linux/compiler.h>
#include <linux/types.h>
#else
#include <stdint.h>
#endif
#include <linux/ioctl.h>
#include <linux/if_ether.h>
#include <linux/if.h>

#define __COE_CAPTURE_ALIGN __aligned(8)

/**
 * @brief Maximum number of buffers indexes that can be registered with the CoE channel.
 */
#define COE_BUFFER_IDX_MAX_NUM	4U

/**
 * @brief CoE channel setup config (COE_IOCTL_CAPTURE_SETUP payload).
 *
 */
struct coe_ioctl_data_capture_setup {
	char if_name[IFNAMSIZ];  /**< Net interface through which the camera is accessible */
	uint8_t sensor_mac_addr[ETH_ALEN]; /**< Ethernet MAC address of a camera */
	uint8_t vlan_enable; /**< VLAN enable value. 1 - VLAN enabled, 0 - VLAN disabled */
	uint8_t reserved[1U];
} __COE_CAPTURE_ALIGN;

/**
 * @brief CoE channel buffer operation (COE_IOCTL_BUFFER_OP payload).
 *
 * Register/unregister a buffer with the CoE channel. Buffer index must be below
 * @ref COE_BUFFER_IDX_MAX_NUM.
 */
struct coe_ioctl_data_buffer_op {
	uint32_t mem; /**< handle to a buffer. */
	uint32_t flag; /**< Buffer @ref CAPTURE_BUFFER_OPS bitmask. */
	uint32_t buffer_idx; /**< Buffer index to identify the buffer for capture requests. */
	uint8_t reserved[4U]; /**< Reserved for future use. */
} __COE_CAPTURE_ALIGN;

/**
 * @brief Enqueue CoE capture request (COE_IOCTL_CAPTURE_REQ payload).
 *
 * Issue a capture request using a specified buffer mem_fd.
 * A buffer must previously have been registered with COE_IOCTL_BUFFER_OP.
 * mem_fd_offset specifies the offset from the beginning of the buffer memory from which
 * data should be received into. It can be used if an application makes a single large
 * allocation for all image memory, and then specifies separate offset within it for each
 * capture.
 * capture_number is used to track the capture number in userspace. The same capture_number
 * is returned by the driver in coe_ioctl_data_capture_status when capture is completed.
 */
struct coe_ioctl_data_capture_req {
	uint32_t buffer_idx; /**< Index of a buffer which is registered with COE_IOCTL_BUFFER_OP. */
	uint32_t buf_size; /**< capture image size in bytes */
	uint32_t mem_fd_offset; /**< offset from the beginning of a buffer */
	uint32_t capture_number; /**< capture number for a tracking by userspace */
} __COE_CAPTURE_ALIGN;

/**
 * @brief Wait on the next completion of an enqueued frame (COE_IOCTL_CAPTURE_STATUS payload).
 *
 * Wait for the next capture completion with the specified timeout.
 * The status of the capture will be returned via capture_status.
 *
 * @param[in]	timeout_ms		uint32_t timeout in [ms], 0 for indefinite wait.
 * @param[out]	capture_number	uint32_t capture number for which the status is returned.
 * @param[out]	capture_status	uint32_t capture status, Valid range: [ @ref CAPTURE_STATUS_UNKNOWN,
 *                                                                      @ref CAPTURE_STATUS_INVALID_CAP_SETTINGS]
 * @param[out]	errData		uint32_t extended error data.
 * @param[out]	sofTimestamp	uint64_t start-of-frame time stamp in nanoseconds.
 * @param[out]	eofTimestamp	uint64_t end-of-frame time stamp in nanoseconds.
 */
struct coe_ioctl_data_capture_status {
	uint32_t timeout_ms;     /**< Capture timeout in milliseconds. */
	uint32_t capture_number; /**< capture number passed with coe_ioctl_data_capture_req */
	uint32_t capture_status; /**< Capture status returned by the driver. */
	uint32_t errData; /**< Extended error data. */
	uint64_t sofTimestamp; /**< Start-of-frame time stamp in nanoseconds. */
	uint64_t eofTimestamp; /**< End-of-frame time stamp in nanoseconds. */
} __COE_CAPTURE_ALIGN;

/**
 * @brief Get info on CoE channel (COE_IOCTL_GET_INFO payload).
 */
struct coe_ioctl_data_get_info {
	uint8_t channel_number; /**< channel number value assigned by a driver */
	uint8_t reserved[7U];
} __COE_CAPTURE_ALIGN;

#endif /* __FUSA_CAPTURE_COE_H__ */
