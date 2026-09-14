/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2022 NVIDIA CORPORATION.  All rights reserved.
 */

#ifndef _LINUX_TEGRA_I2C_RTCPU_H
#define _LINUX_TEGRA_I2C_RTCPU_H

#include <linux/i2c.h>
#include <media/camera_common.h>

struct tegra_i2c_rtcpu_sensor;

struct tegra_i2c_rtcpu_config {
	unsigned int reg_bytes;
};

/*
 * Sensor registration
 */

#ifdef CONFIG_I2C_TEGRA_CAMRTC

/* Find an I2C multi device, and register a sensor. */
struct tegra_i2c_rtcpu_sensor *tegra_i2c_rtcpu_register_sensor(
	struct i2c_client *client,
	const struct tegra_i2c_rtcpu_config *config);

#else

static inline struct tegra_i2c_rtcpu_sensor *tegra_i2c_rtcpu_register_sensor(
	struct i2c_client *client,
	const struct tegra_i2c_rtcpu_config *config)
{
	return NULL;
}

#endif

/*
 * I2C transfer
 */

#ifdef CONFIG_I2C_TEGRA_CAMRTC

/* Start or stop buffering of I2C transfer requests */
int tegra_i2c_rtcpu_aggregate(
	struct tegra_i2c_rtcpu_sensor *sensor,
	bool start);

/* Setting frame ID is available after aggregation started */
int tegra_i2c_rtcpu_set_frame_id(
	struct tegra_i2c_rtcpu_sensor *sensor,
	int frame_id);

/* Read one or more bytes from a sensor */
int tegra_i2c_rtcpu_read_reg8(
	struct tegra_i2c_rtcpu_sensor *sensor,
	unsigned int addr,
	u8 *data,
	unsigned int count);

/* Write one or more bytes to a sensor */
int tegra_i2c_rtcpu_write_reg8(
	struct tegra_i2c_rtcpu_sensor *sensor,
	unsigned int addr,
	const u8 *data,
	unsigned int count);

/* Write a table */
int tegra_i2c_rtcpu_write_table_8(
	struct tegra_i2c_rtcpu_sensor *sensor,
	const struct reg_8 table[],
	const struct reg_8 override_list[],
	int num_override_regs, u16 wait_ms_addr, u16 end_addr);

#else

#define tegra_i2c_rtcpu_aggregate(...) (0)
#define tegra_i2c_rtcpu_set_frame_id(...) (0)
#define tegra_i2c_rtcpu_read_reg8(...) (-ENODEV)
#define tegra_i2c_rtcpu_write_reg8(...) (-ENODEV)
#define tegra_i2c_rtcpu_write_table_8(...) (-ENODEV)

#endif

#endif /* _LINUX_TEGRA_I2C_RTCPU_H */
