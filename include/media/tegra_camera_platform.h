/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (C) 2015-2023 NVIDIA CORPORATION.  All rights reserved.
 */

#ifndef _TEGRA_CAMERA_PLATFORM_H_
#define _TEGRA_CAMERA_PLATFORM_H_

#include <uapi/media/tegra_camera_platform.h>

/* avoid overflows */
#define DEFAULT_PG_CLK_RATE (UINT_MAX - 1)

/**
 * enum tegra_camera_hw_type - camera hw engines
 */
enum tegra_camera_hw_type {
	HWTYPE_NONE = 0,
	HWTYPE_CSI,
	HWTYPE_SLVSEC,
	HWTYPE_VI,
	HWTYPE_ISPA,
	HWTYPE_ISPB,
	HWTYPE_MAX,
};

/**
 * enum tegra_camera_sensor_type - camera sensor types
 */
enum tegra_camera_sensor_type {
	SENSORTYPE_NONE = 0,
	SENSORTYPE_DPHY,
	SENSORTYPE_CPHY,
	SENSORTYPE_SLVSEC,
	SENSORTYPE_VIRTUAL,
	/* HDMI-IN or other inputs */
	SENSORTYPE_OTHER,
	SENSORTYPE_MAX,
};

struct tegra_camera_dev_info;

/**
 * struct tegra_camera_dev_ops - camera device operations
 * @set_rate: set the device clock rate
 */
struct tegra_camera_dev_ops {
	int (*set_rate)(struct tegra_camera_dev_info *cdev_info, unsigned long rate);
};

/**
 * struct tegra_camera_dev_info - camera devices information
 * @priv: a unique identifier assigned during registration
 * @hw_type: type of HW engine as defined by the enum above
 * @bus_width: csi bus width for clock calculation
 * @overhead: hw/ sw overhead considered while calculations
 * @ppc: HW capability, pixels per clock
 * @bw: calculated bw for this node
 * @use_max: populated by hw engine to decide it's clocking policy
 * @memory_latency: latency allowed for memory freq scaling
 * @pdev: pointer to platform_data
 * @sensor_type: type of sensor as defined by the enum above
 * @pixel_rate: pixel rate coming out of the sensor
 * @pixel_bit_depth: bits per pixel
 * @bpp: bytes per pixel
 * @stream_on: stream enabled on the channel
 * @device_node: list node
 * @ops: operation callbacks of the camera device
 */
struct tegra_camera_dev_info {
	void *priv;
	u32 hw_type;
	u32 bus_width;
	u32 overhead;
	u64 lane_speed;
	u32 lane_num;
	u32 ppc;
	u64 pg_clk_rate;
	u64 bw;
	bool use_max;
	u32 memory_latency;
	struct platform_device *pdev;
	u32 sensor_type;
	u64 pixel_rate;
	u32 pixel_bit_depth;
	u32 bpp;
	bool stream_on;
	struct list_head device_node;
	const struct tegra_camera_dev_ops *ops;
};

int tegra_camera_update_isobw(void);
int tegra_camera_emc_clk_enable(void);
int tegra_camera_emc_clk_disable(void);
int tegra_camera_device_register(struct tegra_camera_dev_info *cdev_info,
					void *priv);
int tegra_camera_device_unregister(void *priv);
int tegra_camera_get_device_list_entry(const u32 hw_type, const void *priv,
		struct tegra_camera_dev_info *cdev_info);
int tegra_camera_get_device_list_stats(u32 *n_sensors, u32 *n_hwtypes);
int tegra_camera_update_clknbw(void *priv, bool stream_on);

#endif
