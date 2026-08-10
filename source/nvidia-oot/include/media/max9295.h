/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

/**
 * @file
 * <b>MAX9295 API: For Maxim Integrated MAX9295 serializer</b>
 *
 * @b Description: Defines elements used to set up and use a
 *  Maxim Integrated MAX9295 serializer.
 */

#ifndef __MAX9295_H__
#define __MAX9295_H__

#include <media/gmsl-link.h>
/**
 * \defgroup max9295 MAX9295 serializer driver
 *
 * Controls the MAX9295 serializer module.
 *
 * @ingroup serdes_group
 * @{
 */


/**
 * @brief  Powers on a serializer device and performs the I2C overrides
 * for sensor and serializer devices.
 *
 * The I2C overrides include setting proxy I2C slave addresses for the devices.
 *
 * Before the client calls this function it must ensure that
 * the deserializer device is in link_ex exclusive link mode
 * by calling the deserializer driver's max9296_setup_link() function.
 *
 * @param  [in]  dev            The serializer device handle.
 *
 * @return  0 for success, or -1 otherwise.
 */
int max9295_setup_control(struct device *dev);

/**
 * Reverts I2C overrides and resets a serializer device.
 *
 * @param  [in]  dev            The serializer device handle.
 *
 * @return  0 for success, or -1 otherwise.
 */
int max9295_reset_control(struct device *dev);

/**
 * @brief  Pairs a sensor device with a serializer device.
 *
 * To be called by sensor client driver.
 *
 * @param  [in]  dev            The deserializer device handle.
 * @param  [in]  g_ctx          The @ref gmsl_link_ctx structure handle.
 *
 * @return  0 for success, or -1 otherwise.
 */
int max9295_sdev_pair(struct device *dev, struct gmsl_link_ctx *g_ctx);

/**
 * @brief Unpairs a sensor device from a serializer device.
 *
 * To be called by sensor client driver.
 *
 * @param  [in]  dev            The serializer device handle.
 * @param  [in]  s_dev          The sensor device handle.
 *
 * @return  0 for success, or -1 otherwise.
 */
int max9295_sdev_unpair(struct device *dev, struct device *s_dev);

/**
 * Sets up the serializer device's internal pipeline for a specified
 * sensor/serializer pair.
 *
 * @param  [in]  dev            The serializer device handle.
 *
 * @return  0 for success, or -1 otherwise.
 */
int max9295_setup_streaming(struct device *dev);

/** @} */

#endif  /* __MAX9295_H__ */
