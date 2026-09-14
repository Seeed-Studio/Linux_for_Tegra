/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 */

#ifndef _COMMS_LIB_HAL_OS_H_
#define _COMMS_LIB_HAL_OS_H_

/**
 * @brief Defines the Linux specific attributes of the comms device.
 *
 * @param dev The device.
 * @param comms_id The comms ID.
 */
struct CommsDeviceAttribute {
	/* Pointer to the device handle */
	struct device *dev;
	/* SE Comms(IVC) ID */
	uint32_t comms_id;
};

#endif // _COMMS_LIB_HAL_OS_H_
