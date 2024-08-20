/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024, NVIDIA CORPORATION. All rights reserved.
 */

#ifndef __CAM_FSYNC_H__
#define __CAM_FSYNC_H__

#include <linux/ioctl.h>
#include <linux/types.h>

/**
 * @brief CamFsync start arguments
 */
struct cam_sync_start_args {
	/** @brief The group id of the fsync signal generators to start.
	 *         Valid range: [@ref MIN_GROUP_ID_DEFINED_IN_DT, @ref MAX_GROUP_ID_DEFINED_IN_DT]
	 */
	__u32 group_id;
	/** @brief The start time in tsc ticks */
	__u64 start_tsc_ticks;
};

/**
 * @brief Arguments for generator reconfiguration.
 */
struct cam_sync_gen_reconfig_args {
	/**
	 * @brief Group ID of the generator to reconfigure.
	 *        Valid range: [@ref MIN_GROUP_ID_DEFINED_IN_DT, @ref MAX_GROUP_ID_DEFINED_IN_DT]
	 */
	__u32 group_id;
	/**
	 * @brief ID of the generator within the group to reconfigure.
	 *        Valid range: [@ref MIN_GENERATOR_ID_DEFINED_IN_DT, @ref MAX_GENERATOR_ID_DEFINED_IN_DT]
	 */
	__u32 generator_id;
	/**
	 * @brief The new frequency, in Hz.
	 *        Valid range: [> 0, 120]
	 */
	__u32 freqHz;
	/**
	 * @brief The new duty cycle, in whole percent.
	 *        Valid range: [> 0, < 100]
	 */
	__u32 dutyCycle;
	/**
	 * @brief The new relative offset, in milliseconds.
	 *        Valid range: [non-zero]
	 */
	__u32 offsetMs;
};

#define CAM_FSYNC_GRP_ABS_START_VAL \
	_IOW('T', 1, struct cam_sync_start_args)
#define CAM_FSYNC_GRP_STOP \
	_IOW('T', 2, uint32_t)
#define CAM_FSYNC_GEN_RECONFIGURE \
	_IOW('T', 3, struct cam_sync_gen_reconfig_args)

#endif
