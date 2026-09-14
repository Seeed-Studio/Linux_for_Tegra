/*
 * sl_zedx.c - Stereolabs ar0234 sensor driver
 *
 * Copyright (c) 2022-2023, Stereolabs.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// #define DEBUG 1
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/mutex.h>

#include <linux/seq_file.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>

#include <media/tegracam_core.h>
#include <media/v4l2-ctrls.h>
#include "zedx_mode_tbls.h"
#include "../include/parse_folder.h"
#include "../include/info_sysfs.h"

#define STRINGIFY0(s)
#define STRINGIFY(s) STRINGIFY0(s)
#define xstr(s) str(s)
#define str(s) #s
#define DRV_STR_VERSION xstr(ZEDX_DRIVER_VERSION_MAJOR)"."xstr(ZEDX_DRIVER_VERSION_MINOR)"."xstr(ZEDX_DRIVER_VERSION_PATCH)

#define MAX_RADIAL_COEFFICIENTS 6
#define MAX_TANGENTIAL_COEFFICIENTS 2
#define MAX_FISHEYE_COEFFICIENTS 6

#define CAMERA_MAX_SN_LENGTH            32
#define MAX_RLS_COLOR_CHANNELS          4
#define MAX_RLS_BREAKPOINTS             6

extern int fps_set_Dser(int channel, s64 val);
/* to guarantee that the ser is loaded before the sensor */
extern int set_bitrate_Ser(int channel, u8 val);
extern int dser_enable_gmsl_link(int channel,int zedx_id);
extern int dser_get_sibling_on_phy(int channel, unsigned int caller_cam_addr);
extern int dser_open_all_gmsl_link(int channel);
extern int ser_translate_addr(int zedx_id, unsigned int src_addr, unsigned int dest_addr);
extern int ser_reset_translate(int zedx_id);

int custom_s_ctrl(struct v4l2_ctrl *ctrl);
extern int ser_get_acc_addr(int zedx_id);
extern int ser_get_gyro_addr(int zedx_id);

#define AR0234_MIN_GAIN (1)
#define AR0234_MAX_GAIN (8)
#define AR0234_MAX_GAIN_REG (0x40)
#define AR0234_DEFAULT_FRAME_LENGTH (0x30D)
#define AR0234_DEFAULT_FPS (60)
#define AR0234_MIN_LINE_LENGTH_PCK (0x300)
#define AR0234_COARSE_TIME_SHS1_ADDR 0x3012
#define AR0234_ANALOG_GAIN 0x3060
#define AR0234_DEFAULT_RES (0x04D0)
#define AR0234_BINNED_RES  (0x0268)
#define AR0234_DEFAULT_EXP  (5)
#define ZED_CAMERA_CID_EEPROM_DATA (TEGRA_CAMERA_CID_BASE+300)

/**
 * Structure definitions
 */

/**
 * struct of_device_id* - Link the driver to the dts.
 * @compatible: String that includes the name of the
 *				company and the name of the device.
 *
 * To add support for your ZED-X, the dts compatibility
 * must match "stereolabs,zedx".
 */
const struct of_device_id zedx_of_match[] = {
	{.compatible = "stereolabs,zedx"},
	{},
};

/**
 * macro MODULE_DEVICE_TABLE - Allow hotplug of the device
 * Exposes the vendor/device id for the compiler.
 */
MODULE_DEVICE_TABLE(of, zedx_of_match);

/**
 * List of control defined in tegra-v4l2-camera.h
 */
static const u32 ctrl_cid_list[] = {
	TEGRA_CAMERA_CID_GAIN,
	TEGRA_CAMERA_CID_EXPOSURE,
	TEGRA_CAMERA_CID_EXPOSURE_SHORT,
	TEGRA_CAMERA_CID_FRAME_RATE,
	TEGRA_CAMERA_CID_EEPROM_DATA,
	TEGRA_CAMERA_CID_HDR_EN,
	TEGRA_CAMERA_CID_SENSOR_MODE_ID,
	TEGRA_CAMERA_CID_STEREO_EEPROM,
};

static const struct v4l2_ctrl_ops custom_ctrl_ops = {
	.s_ctrl = custom_s_ctrl,
};

static struct v4l2_ctrl_config cfg_list[] = {
	{
		.ops = &custom_ctrl_ops,
		.id = ZED_CAMERA_CID_EEPROM_DATA,
		.name = "ZED EEPROM data",
		.type = V4L2_CTRL_TYPE_STRING,
		.min = 1024/2,
		.max = (1024/2)+1,
		.step = 1,
	},
};

/**
 * struct fisheye_lens_distorion_coeff - Coefficients for the distortion model.
 * @coeff_count: uint 32 - Number of radial coefficients.
 * @k: float* - Table of coefficients.
 * @mapping_type: uint 32 - 0 -> equidistant, 1 -> equisolid,
 *							2 -> orthographic, 3 -> stereographic
 *
 * For lens correction when a wide FOV is used.
 */
typedef struct
{
	u32 coeff_count;
	float k[MAX_FISHEYE_COEFFICIENTS];
	u32 mapping_type;
} fisheye_lens_distortion_coeff;

/**
 * struct polynomial_lens_distorion_coeff - Coefficients for the distortion model.
 * @radial_coeff_count: uint 32 - Number of radial coefficients.
 * @k: float* - Table of radial coefficients.
 * @tangential_coeff_count: uint 32 - Number of tangential coefficients.
 * @p: float* - Table of tangential coefficients.
 *
 * For lens correction when a wide FOV is used.
 */
typedef struct
{
	u32 radial_coeff_count;
	float k[MAX_RADIAL_COEFFICIENTS];
	u32 tangential_coeff_count;
	float p[MAX_TANGENTIAL_COEFFICIENTS];
} polynomial_lens_distortion_coeff;

/**
 * struct camera_intrinsics - Camera calibration settings.
 * @width: uint 32 - Width of the image in pixel.
 * @height: uint32 - Height of the image in pixel.
 * @fx: float - focal length accross the x axis in pixel.
 * @fy: float - focal length accross the y axis in pixel.
 * @tangential_coeff_count: uint 32 - Number of tangential coefficients.
 * @skew: float - Skew of the sensor.
 * @cx: float - x coordinate of the optical center in pixels.
 * @cy: float - y coordinate of the optical center in pixels.
 * @distortion_type: uint32 - type of distortion, depending on the lens.
 *					0: pinhole, assuming polynomial distortion
 *					1: fisheye, assuming fisheye distortion)
 *					2: ocam (omini-directional)
 * @distortion_coefficients: union of structures - Distortion coefficients
 *							 corresponding to the lens. See the structs above
 *							 for more details.
 *
 * Camera and lens intrinsic calibration settings.
 */
typedef struct
{
	u32 width, height;
	float fx, fy;
	float skew;
	float cx, cy;
	u32 distortion_type;
	union distortion_coefficients
	{
		polynomial_lens_distortion_coeff poly;
		fisheye_lens_distortion_coeff fisheye;
	} dist_coeff;
} camera_intrinsics;

/**
 * struct camera_extrinsics - Camera and IMU rotation and translation parameters.
 * @rx: float - Rotation parameter expressed in Rodrigues notation:
 *		angle = sqrt(rx^2+ry^2+rz^2), unit axis = s = [rx,ry,rz]/angle.
 * @ry: float - Rotation parameter expressed in Rodrigues notation.
 * @rz: float - Rotation parameter expressed in Rodrigues notation.
 * @tx: float - Translation parameter.
 * @ty: float - Translation parameter.
 * @tz: float - Translation parameter.
 *
 * All rotations and translations are done with respect to the same reference point.
 */
typedef struct
{
	float rx, ry, rz;
	float tx, ty, tz;
} camera_extrinsics;

/**
 * struct imu_params - IMU calibration settings.
 * @linear_acceleration_bias: float[3] - 3D vector containing the accelerometer bias.
 * 							  Must be added to the accelerometer readings.
 * @angular_velocity_bias: float[3] - 3D vector containing the gyroscope bias.
 * 						   Must be added to the gyroscope readings.
 * @gravity_acceleration: float[3] - Gravity acceleration when the camera is horizontal.
 * @extr: camera_extrinsincs - Rotation and transltation parameters.
 * @update_rate: float
 * @linear_acceleration_noise_density: float - Accelerometer noise parameter
 * @linear_acceleration_random_walk: float - Accelerometer noise parameter
 * @angular_velocity_noise_density: float - Gyroscope noise parameter
 * @angular_velocity_random_walk: float - Gyroscope noise parameter
 *
 * IMU calibration settings.
 */
typedef struct
{
	float linear_acceleration_bias[3];
	float angular_velocity_bias[3];
	float gravity_acceleration[3];
	camera_extrinsics extr;
} imu_params_v1;

typedef struct {
	/*
	 * Noise model parameters
	 */
#if !IS_ENABLED(CONFIG_TEGRA_L4T_351)
	float update_rate;
	float linear_acceleration_noise_density;
	float linear_acceleration_random_walk;
	float angular_velocity_noise_density;
	float angular_velocity_random_walk;
#endif
} imu_params_noise_m;

typedef struct {
	imu_params_v1 imu_data_v1;
	imu_params_noise_m nm;
} imu_params_v2;

#if !IS_ENABLED(CONFIG_TEGRA_L4T_351)
/**
 * radial_lsc_params - Radial LSC parameters for image correction
 *
 * @image_height: Image height
 * @image_width: Image width
 * @n_channels: Number of color channels
 * @rls_x0: X-coordinate of center point for each color channel
 * @rls_y0: Y-coordinate of center point for each color channel
 * @ekxx: Ellipse xx parameter for each color channel
 * @ekxy: Ellipse xy parameter for each color channel
 * @ekyx: Ellipse yx parameter for each color channel
 * @ekyy: Ellipse yy parameter for each color channel
 * @rls_n_points: Number of breakpoints in LSC radial transfer function
 * @rls_rad_tf_x: LSC radial transfer function X for each color channel and breakpoint
 * @rls_rad_tf_y: LSC radial transfer function Y for each color channel and breakpoint
 * @rls_rad_tf_slope: LSC radial transfer function slope for each color channel and breakpoint
 * @r_scale: rScale parameter
 *
 * Structure containing parameters for radial LSC (Lens Shading Correction)
 * for image correction purposes.
 */
typedef struct {
	u16 image_height;
	u16 image_width;
	u8 n_channels;
	float rls_x0[MAX_RLS_COLOR_CHANNELS];
	float rls_y0[MAX_RLS_COLOR_CHANNELS];
	double ekxx[MAX_RLS_COLOR_CHANNELS];
	double ekxy[MAX_RLS_COLOR_CHANNELS];
	double ekyx[MAX_RLS_COLOR_CHANNELS];
	double ekyy[MAX_RLS_COLOR_CHANNELS];
	u8 rls_n_points;
	float rls_rad_tf_x[MAX_RLS_COLOR_CHANNELS][MAX_RLS_BREAKPOINTS];
	float rls_rad_tf_y[MAX_RLS_COLOR_CHANNELS][MAX_RLS_BREAKPOINTS];
	float rls_rad_tf_slope[MAX_RLS_COLOR_CHANNELS][MAX_RLS_BREAKPOINTS];
	u8 r_scale;
} radial_lsc_params;
#endif

/**
 * struct NvCamSyncSensorCalibData - Camera calibration settings.
 * @cam_intr: struct camera_intrinsics - Intrinsics calibration parameters, see above.
 * @cam_extr: struct camera_extrinsics - Extrinsics calibration parameters, see above.
 * @imu_present: uint 8 - IMU availability. 0 means no IMU is present.
 * @imu: struct imu_params - IMU calibration settings.
 * @serial_number: u8* - table containing the ZED serial number.
 * @rls: radial_lsc_params - Radial lens shading correction parameters
 *				for the optics.
 *
 * Camera calibration settings.
 */
typedef struct
{
	camera_intrinsics cam_intr;
	camera_extrinsics cam_extr;
	u8 imu_present;
	imu_params_v2 imu;
#if !IS_ENABLED(CONFIG_TEGRA_L4T_351)
	u8 serial_number[CAMERA_MAX_SN_LENGTH];
	radial_lsc_params rls;
#endif
} NvCamSyncSensorCalibData;

/**
 * struct LiEeprom_Content_Struct - Structure representing the EEPROM content.
 * @version: uint 32 - EEPROM layout version.
 * @factory_data: uint 32 - Factory flag to set when factory flashed.
 * 				  It needs to be reset to 0 when the user modifies it.
 * @left_cam_intr: struct camera_intrinsics - Left camera intrinsics calibration parameters,
 * 				   see above.
 * @right_cam_intr: struct camera_intrinsics - Right camera intrinsics calibration parameters,
 * 				   see above.
 * @cam_extr: struct camera_extrinsics - Extrinsics calibration parameters
 * 			  for the cameras, see above.
 * @imu_present: uint 32 - IMU availability. 0 means no IMU is present.
 * @imu: struct imu_params - IMU calibration settings.
 * @serial_number: u8* - u8 table of containing the serial number of the ZED X.
 * @left_rls: radial_lsc_params - Radial lens shading correction parameters for
 *			the left camera sensor.
 * @right_rls: radial_lsc_params - Radial lens shading correction parameters for
 *			the right camera sensor.
 *
 * Calibration parameters of the ZED-X. Parsed using the EEPROM content.
 */
typedef struct
{
	u32 version;
	u32 factory_data;
	camera_intrinsics left_cam_intr;
	camera_intrinsics right_cam_intr;
	camera_extrinsics cam_extr;
	u32 imu_present;
	imu_params_v1 imu;
#if !IS_ENABLED(CONFIG_TEGRA_L4T_351)
	u8 serial_number[CAMERA_MAX_SN_LENGTH];
	imu_params_noise_m nm;
	radial_lsc_params left_rls;
	radial_lsc_params right_rls;
#endif
} LiEeprom_Content_Struct;

/**
 * atomic_t master_frame_length - For multiple sensor synchronisation.
 * @master_frame_length: atomic_t - duration of the frame exposure in number of sensor
 * 			  lines.
 *
 * This atomic variable allows a sensor bound to a slave deserializer to be synchronized
 * with a sensor bound to a master deserializer. Both sensors need to be linked by a
 * synchronisation GPIO. See the <sync_mode> entry in the deserializer dts for more informations.
 */
atomic_t master_frame_length = ATOMIC_INIT(0);
/**
 * struct zedx - ZED-X private data.
 * @eeprom: struct camera_common_eeprom_data * - EEPROM i2c interface details.
 *			Here EEPROM_NUM_BLOCKS = 2, hence the EEPROM i2c addresses are x54 and x55.
 * @eeprom_buf: uint 8 - The EEPROM is accessed once, emptying or filling all its memory
 * 				using this buffer.
 * @i2c_client: struct i2c_client * - i2c adapter of the zedx.
 * @id: struct i2c_device_id * - i2c adapter id.
 * @subdev: struct v4l2_subdev * - To register the zedx as a v4l2 device.
 * @frame_length: uint 32 - Exposure duration of a frame.
 * @s_data: struct camera_common_data * - Nvidia camera common data for v4l2 support.
 * @tc_dev: struct tegracam_device * - Nvidia camera data for tegra and argus.
 * @channel: uint 32 - camera id.
 * @master: boolean. Indicates if camera is set as master (first sensor)
 * @sync_sensor_index: uint 32 - Sync source for the camera.
 * @EepromCalib: NvCamSyncSensorCalibData - EEPROM data structure.
 * @serial_number: Decimal value of the serial number.
 *
 * ZED-X private data for this driver. One structure is initialized by camera sensor,
 * so two per ZED-X.
 */
struct zedx
{
	struct camera_common_eeprom_data eeprom[AR0234_EEPROM_NUM_BLOCKS];
	u8 eeprom_buf[AR0234_EEPROM_SIZE];
	struct i2c_client *i2c_client;
	const struct i2c_device_id *id;
	struct v4l2_subdev *subdev;
	struct v4l2_ctrl_handler ctrl_handler;
	int num_ctrls;
	struct v4l2_ctrl* ctrls[1];
	s64 fps;
	u32 pix_clk;
	u32 line_length_pck;
	u32 total_frame_length;
	u16 frame_length_line;
	struct camera_common_data *s_data;
	struct tegracam_device *tc_dev;
	u32 channel;
	bool master;
	bool sync_slave;
	u32 sync_sensor_index;
	int zedx_id;
	int gmsl_port;
	u8 i2c_cc;
	NvCamSyncSensorCalibData EepromCalib;
	unsigned long serial_number;
	struct info_sysfs zed_sysfs;
	s8 probe_counter;
	bool ioctl_updated;
	bool streaming;
	unsigned int stream_users;
	bool auto_started;   /* this sensor was auto-started by a sibling on shared PHY */
	uint8_t gyro_addr;
	uint8_t acc_addr;
};

/**
 * This allow the left and right cameras to refer to one another internally.
 * In particular, the goal is to share the serial number between 2 sensors,
 * because only the left camera has access to the eeprom.
 */
static s8 zedx_probe_count = 0;
static struct zedx *zedx_array[N_ZEDX];
static DEFINE_MUTEX(zedx_stream_lock);
static bool auto_start_sibling = true;
module_param(auto_start_sibling, bool, 0644);
MODULE_PARM_DESC(auto_start_sibling,
	"Auto-start a shared-PHY sibling sensor when only one source streams");

static struct zedx *zedx_find_sibling(struct zedx *priv, int sib_cam_addr);

/**
 * const struct regmap_config - ar0234 register mapping configuration.
 * @reg_bits: uint 32 - 16 bits register addresses.
 * @val_bits: uint 32 - 16 bits register contents.
 * @cache_type: enum - Supported cache type.
 *
 * Register configuration of the ar0234 camera sensor.
 */
static const struct regmap_config sensor_regmap_config = {
	.reg_bits = 16,
	.val_bits = 16,
	.cache_type = REGCACHE_RBTREE,
};

/* Miscellaneous functions*/

/**
 * zedx_get_coarse_time_regs_shs1() - Prepare an integration time reading.
 * @regs: A structure to write the register address and value.
 * @coarse_time: A 16 bits mask coressponding to the integration time of the sensor.
 *
 * Prepare the register mask and address to get the integration time of the ar0234
 * sensor as multiples of line_length_pck_.
 *
 * Context: Inlined function, can sleep.
 * Return: Void.
 */
static inline void zedx_get_coarse_time_regs_shs1(ar0234_reg *regs,
		u16 coarse_time)
{
	regs->addr = AR0234_COARSE_TIME_SHS1_ADDR;
	regs->val = (coarse_time)&0xffff;
}

/**
 * zedx_get_gain_reg() - Prepare a gain reading.
 * @regs: A structure to write the register address and value.
 * @coarse_time: A 16 bits mask corresponding to the gain of the sensor.
 *
 * Prepare the register mask and address to get the gain of the ar0234 sensor.
 *
 * Context: Inlined function, can sleep.
 * Return: Void.
 */
static inline void zedx_get_gain_reg(ar0234_reg *regs,
		u16 gain)
{
	regs->addr = AR0234_ANALOG_GAIN;
	regs->val = (gain)&0xffff;
}

/**
 * zedx_set_line_length_px_clk() - set the line length for the sensor.
 * @regs: A structure to write the register address and value.
 * @px_clk_per_line: The number of clock period per sensor line.
 *
 * This function sets the number of Pixel Clock period per sensor line. This
 * way the exposure time can be set precisely.
 *
 * Context: Inlined function, can sleep.
 * Return: Void.
 */
static inline void zedx_set_line_length_px_clk(ar0234_reg *regs, u16 px_clk_per_line)
{
	regs->addr = 0x300C;
	regs->val = (px_clk_per_line)&0xffff;
}

 /**
 * zedx_read_reg() - Read in the registers of the ar0234 sensor.
 * @s_data: Camera data, for the regmapping.
 * @addr: Address of the register to write.
 * @val: Value to write in the register
 *
 * Read 16 bits in a register of the ar0234 sensor.
 *
 * Context: Non critical function, can sleep.
 * Return: 0 in case of success and a negative errno in case of error.
 */
static inline int zedx_read_reg(struct zedx *priv,
		u16 addr, u16 *val)
{
	int err = 0;
	u32 reg_val = 0;

	if (val == NULL)
	{
		dev_err(&priv->i2c_client->dev, "%s: u16 *val is not initialized...\n", __func__);
		return -1;
	}

	err = regmap_read(priv->s_data->regmap, addr, &reg_val);
	*val = (u16)reg_val & 0xFFFF;

	return err;
}

/**
 * zedx_write_reg() - Write in the registers of the ar0234 sensor.
 * @priv: Camera data, for the regmapping.
 * @addr: Address of the register to write.
 * @val: Value to write in the register
 *
 * Write 16 bits in a register of the ar0234 sensor.
 *
 * Context: Non critical function, can sleep.
 * Return: 0 in case of success and a negative errno in case of error.
 */
static int zedx_write_reg(struct zedx *priv,
						  u16 addr, u16 val)
{
	int err;

	err = regmap_write(priv->s_data->regmap, addr, val);
#ifdef DEBUG
	if (err)
		dev_dbg(&priv->i2c_client->dev,
				"%s:i2c write failed: dev. 0x%x, reg. 0x%x, val. 0x%x - %d\n",
				__func__, priv->i2c_client->addr, addr, val,err);
#endif
	return err;
}

/**
 * zedx_write_table() - Write in the registers.
 * @priv: Driver data.
 * @table: Table of registers to write in the deserializer.
 *
 * Write a table of 16 bits registers sequentially to the serializer
 * or deserializer. The timings are important.
 * For each register, it tries two times before returning with an error.
 *
 * Context: Non critical function, can sleep.
 * Return: 0 in case of success and a negative errno in case of error.
 */
static int zedx_write_table(struct zedx *priv,
							const struct index_reg_8 table[])
{
#ifdef DEBUG
	struct tegracam_device *tc_dev = priv->tc_dev;
	struct device *dev = tc_dev->dev;
#endif
	int i = 0, j = 0;
	int ret = 0;
	int retry = 5;

	/* While we haven't reach the end of the table */
	while (table[i].source != 0x00)
	{
		/* While we haven't tried to write the register 'retry' times */
		for (j = 0; j < retry; j++)
		{
			/* We try to write the register. */

			if (table[i].addr == AR0234_TABLE_WAIT_MS)
			{
				msleep(table[i].val);
				i++;
				break;
			}
			ret = zedx_write_reg(priv, table[i].addr, table[i].val);
			if (ret)
			{
				if (j == retry - 1)
					return ret;
#ifdef DEBUG
				dev_dbg(dev, "%s: try %d - %d\n", __func__, j, ret);
#endif
				msleep(4);
				continue;
			}
			else
			{
				if (0x301a == table[i].addr || 0x3060 == table[i].addr)
					msleep(80); /* minimum 40 ms -> double for comfort */
			}
			break;
		}

		i++;
	}
	return 0;
}

/**
 * zedx_power_on() - Power on the Deserializer.
 * @s_data: Structure to the camera GPIOs.
 *
 * Turn on the camera common power rail.
 *
 * Context: Non critical function, can sleep.
 * Return: 0 in case of success and a negative errno in case of error.
 */
static int zedx_power_on(struct camera_common_data *s_data)
{
	int err = 0;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;

	if (verbosity_level>=1)
		dev_dbg(dev, "%s: power on\n", __func__);

	if (pdata && pdata->power_on)
	{
		err = pdata->power_on(pw);
		if (err)
			dev_err(dev, "%s failed.\n", __func__);
		else
			pw->state = SWITCH_ON;
		return err;
	}

	pw->state = SWITCH_ON;

	return 0;
}

/**
 * zedx_power_off() - Power off the deserializer.
 * @s_data: Structure to the camera GPIOs.
 *
 * For tegracam structure
 *
 * Context: Non critical function, can sleep.
 * Return: Always 0.
 */
static int zedx_power_off(struct camera_common_data *s_data)
{
	return 0;
}

/**
 * zedx_power_get() - Get the power state.
 * @tc_dev: tegracam_device driver structure.
 *
 * Get the power status of the deserializer and the frame clock.
 *
 * Context: Non critical function, can sleep.
 * Return: 0 in case of success and a negative errno in case of error.
 */
static int zedx_power_get(struct tegracam_device *tc_dev)
{
	struct device *dev = tc_dev->dev;
	struct camera_common_data *s_data = tc_dev->s_data;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	const char *mclk_name;
	const char *parentclk_name;
	struct clk *parent;
	int err = 0;

	mclk_name = pdata->mclk_name ? pdata->mclk_name : "cam_mclk1";
	pw->mclk = devm_clk_get(dev, mclk_name);
	if (IS_ERR(pw->mclk))
	{
		dev_err(dev, "unable to get clock %s\n", mclk_name);
		return PTR_ERR(pw->mclk);
	}

	parentclk_name = pdata->parentclk_name;
	if (parentclk_name)
	{
		parent = devm_clk_get(dev, parentclk_name);
		if (IS_ERR(parent))
		{
			dev_err(dev, "unable to get parent clock %s",
					parentclk_name);
		}
		else
			clk_set_parent(pw->mclk, parent);
	}

	pw->state = SWITCH_OFF;

	return err;
}


/**
 * zedx_power_put() - Check the power rail of the camera.
 * @tc_dev: tegracam_device driver structure.
 *
 * Check that the camera_common_power_rail structure is initialized.
 *
 * Context: Non critical function, can sleep.
 * Return: 0 in case of success and a negative errno in case of error.
 */
static int zedx_power_put(struct tegracam_device *tc_dev)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct camera_common_power_rail *pw = s_data->power;

	if (unlikely(!pw))
		return -EFAULT;

	return 0;
}

/* tegracam driver interface functions */

/**
 * zedx_set_group_hold - Dummy function
 * @tc_dev: tegracam_device driver structure.
 * @val: boolean to set the group hold.
 *
 * Does nothing: for tegracam driver only.
 *
 * Context: Non critical function, can sleep.
 * Return: 0.
 */
static int zedx_set_group_hold(struct tegracam_device *tc_dev, bool val)
{
	return 0;
}

static struct zedx *zedx_get_stereo_sibling(struct zedx *priv)
{
	int sib_addr;
	struct zedx *sib;

	if (!priv->i2c_client || !priv->serial_number ||
	    (priv->sync_sensor_index != 1 && priv->sync_sensor_index != 2))
		return NULL;

	sib_addr = dser_get_sibling_on_phy((int)priv->channel,
					   (unsigned int)priv->i2c_client->addr);
	sib = zedx_find_sibling(priv, sib_addr);
	if (!sib || !sib->serial_number ||
	    sib->serial_number != priv->serial_number ||
	    sib->sync_sensor_index == priv->sync_sensor_index)
		return NULL;

	return sib;
}

static int zedx_write_gain_value(struct zedx *priv, s64 val)
{
	ar0234_reg reg_list[1];
	u16 gain = (u16)val;
	u16 gain_reg = 0;

	/* From the datasheet, gain_reg = 0brYYYyyyyrXXXxxxx,
	 * where r is reserved, X is for context A and Y the context B.
	 * The low or high case indicate fine or coarse gain bit.
	 * The coarse gain is given by 2**(0bAAA).
	 * The recommended minimum gain is 1.684. */
	if (val <= 168)
		gain_reg = 0x0D;
	else if (val > 168 && val < 200)
		gain_reg = (32 * (1000 - (100000 / gain))) / 1000;
	else if (val < 400 && val >= 200) {
		gain = gain / 2;
		gain_reg = (16 * (1000 - (100000 / gain))) / 1000 * 2;
		gain_reg = gain_reg + 0x10;
	} else if (val < 800 && val >= 400) {
		gain = gain / 4;
		gain_reg = (32 * (1000 - (100000 / gain))) / 1000;
		gain_reg = gain_reg + 0x20;
	} else if (val < 1600 && val >= 800) {
		gain = gain / 8;
		gain_reg = (16 * (1000 - (100000 / gain))) / 1000 * 2;
		gain_reg = gain_reg + 0x30;
	} else if (val >= 1600) {
		gain_reg = 0x40;
	}

	if (gain > AR0234_MAX_GAIN_REG)
		gain = AR0234_MAX_GAIN_REG;
	zedx_get_gain_reg(reg_list, gain_reg);

	return zedx_write_reg(priv, reg_list[0].addr, reg_list[0].val);
}

/**
 * zedx_set_gain - Set the gain of the camera sensor.
 * @tc_dev: tegracam_device driver structure.
 * @val: value of the sensor gain.
 *
 * Encode val in 16 bits and writes it in the sensor gain register
 * of the ar0234.
 *
 * Context: Non critical function, can sleep.
 * Return: 0 in case of success and a negative errno in case of error.
 */
static int zedx_set_gain(struct tegracam_device *tc_dev, s64 val)
{
	struct zedx *priv = (struct zedx *)tegracam_get_privdata(tc_dev);
	struct zedx *target = zedx_get_stereo_sibling(priv);
	struct device *dev = tc_dev->dev;
	int err;

	if (!target)
		target = priv;

	err = zedx_write_gain_value(target, val);
	if (err)
	{
		dev_info(dev, "%s: GAIN control error\n", __func__);
		return err;
	}

	return err;
}

static int zedx_write_exposure_value(struct zedx *priv, s64 val)
{
	ar0234_reg reg_list[1];
	u64 coarse_time;
	u32 max_coarse_time;
	u32 shs1;

	max_coarse_time = priv->total_frame_length - 1;
	coarse_time = priv->pix_clk * val / priv->line_length_pck / 1000000;

	if (coarse_time > max_coarse_time)
		coarse_time = (u64)max_coarse_time;

	shs1 = (u32)coarse_time;
	if (shs1 < 2)
		shs1 = 2;

	zedx_get_coarse_time_regs_shs1(reg_list, shs1);
	return zedx_write_reg(priv, reg_list[0].addr, reg_list[0].val);
}

/**
 * zedx_set_exposure - Set the frame exposure of the camera sensor.
 * @tc_dev: tegracam_device driver structure.
 * @val: value of the frame exposure duration in nanoseconds.
 *
 * Set the coarse frame exposure duration of the ar0234 sensor in nanoseconds.
 *
 * Context: Non critical function, can sleep.
 * Return: 0 in case of success and a negative errno in case of error.
 */
static int zedx_set_exposure(struct tegracam_device *tc_dev, s64 val)
{
	struct zedx *priv = (struct zedx *)tegracam_get_privdata(tc_dev);
	struct zedx *target = zedx_get_stereo_sibling(priv);
	int err;
	u32 max_coarse_time;

	if (!target)
		target = priv;

	err = zedx_write_exposure_value(target, val);

	if (err)
		dev_dbg(&priv->i2c_client->dev,"%s: set coarse time error\n", __func__);

	if (verbosity_level)
	{
		max_coarse_time = priv->total_frame_length-1 ;
		dev_info(&priv->i2c_client->dev,
				"%s: max_coarse_time value is %d\n",
				__func__, max_coarse_time);
		dev_info(&priv->i2c_client->dev,
				"%s: expo value is %lld\n",
				__func__, val);
		dev_info(&priv->i2c_client->dev,
				"%s: line length in pixel clock value is %d\n",
				__func__, priv->line_length_pck);
	}

	return err;
}

/**
 * zedx_set_frame_rate - Set the frame rate of the camera sensor.
 * @tc_dev: tegracam_device driver structure.
 * @val: value of the frame rate power 10^6.
 *
 * Set the frame rate at 30, 60 or 120 fps. For 30 FPS, the line length in
 * number of pixel clocks need to be adjusted.
 *
 * Context: Non critical function, can sleep.
 * Return: 0 in case of success and a negative errno in case of error.
 */
static int zedx_set_frame_rate(struct tegracam_device *tc_dev, s64 val)
{
	struct zedx *priv = (struct zedx *)tegracam_get_privdata(tc_dev);
	struct device *dev = tc_dev->dev;
	u32 fps_val = 0, vertical_blanking = 0;
	u16 row_start = 0, row_stop = 0;
	u8 bin_coeff = 0;
	ar0234_reg reg_list[1];
	int err;

	if (val <= 0) {
		dev_warn(dev, "%s: invalid fps %lld, using 30 fps\n",
			 __func__, val);
		val = 30000000;
	}

	err = (int)fps_set_Dser(priv->channel, val);

	if (!priv->sync_slave)
	{
		if (err == 0xEEEE)
		{
			if (verbosity_level)
				dev_warn(dev, "%s: Unsupported value\n", __func__);

			return 0;
		}
		if (err == 0xFFFF)
		{
			dev_warn(dev, "%s: Deserializer write error\n", __func__);
			return -1;
		}
	}

	if (priv->sync_slave)
	{
		priv->frame_length_line = (u32) atomic_read(&master_frame_length);
		if (verbosity_level)
			dev_info(dev, "%s: serializer in slave mode, FLL = %d\n", __func__, priv->frame_length_line);
	}

	/* To differentiate the 1200p or 600p binned mode */
	bin_coeff = (priv->frame_length_line == AR0234_BINNED_RES) ? 2 : 1;

	/* Transform val in true FPS to avoid using a u64*/
	fps_val = (u32)(val/1000000);

	/* Compute the new line length in terms of pixel clocks (LLPCK).
	 * The time to sample a line is equal to LLPCK/PCK
	 * where PCK is the pixel clock.
	 * This calcul is based on the resolution and the FPS value requested
	 * and it is valid accross all combinations supported by this driver.
	 * Minimum LLPCK is 612 and maximum LLPCK depends on the maximum exposure
	 * time (fps) and resolution (binned or not binned). */
	priv->line_length_pck = AR0234_MIN_LINE_LENGTH_PCK*(u32)bin_coeff;
	priv->line_length_pck = priv->line_length_pck*AR0234_DEFAULT_FPS/fps_val;

	zedx_set_line_length_px_clk(reg_list, priv->line_length_pck);

	err = zedx_write_reg(priv, reg_list[0].addr,
		reg_list[0].val);

	zedx_read_reg(priv,AR0234_ROW_START, &row_start);

	zedx_read_reg(priv,AR0234_ROW_STOP, &row_stop);

	/* Compute the vertical blanking in term of pixel clocks.
	 * It is given by (FLL+5-N_rows)*LLPCK in the datasheet.
	 * Here, the bin_coeff denotes the 600p binning case,
	 * where n_rows is divided by 2. */
	vertical_blanking = (priv->frame_length_line+5-(row_stop-row_start+1)/bin_coeff)*(priv->line_length_pck);

	/* Total frame length in number of lines (F in the datasheet).
	 * F*LLPCK*PCK should be slightly lower than 1/fps to guarrantee
	 * that the exposure is over before the next trigger */
	priv->total_frame_length = vertical_blanking/priv->line_length_pck+(row_stop-row_start+1)/bin_coeff;

	priv->fps = val;

	if (verbosity_level)
	{
		dev_info(dev, "%s: requested fps value is %lld \n", __func__, val);

		dev_info(dev, "%s: wrote %d in the line length pck reg 0x300C \n",
				__func__, priv->line_length_pck);

		dev_info(dev, "%s: total frame length in number of lines is %d \n",
				__func__, priv->total_frame_length);

		dev_info(dev, "%s: vertical blanking in pixel clock is %d \n", __func__, vertical_blanking);
	}

	return 0;
}

static s64 zedx_get_frame_rate_ctrl(struct tegracam_device *tc_dev)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct v4l2_ctrl *ctrl = NULL;
	struct v4l2_ctrl_handler *hdl;
	s64 fps = 0;

	if (s_data->ctrl_handler) {
		hdl = s_data->ctrl_handler;
		ctrl = v4l2_ctrl_find(hdl, TEGRA_CAMERA_CID_FRAME_RATE);
	}

	if (!ctrl && s_data->tegracam_ctrl_hdl) {
		hdl = &s_data->tegracam_ctrl_hdl->ctrl_handler;
		ctrl = v4l2_ctrl_find(hdl, TEGRA_CAMERA_CID_FRAME_RATE);
	}

	if (ctrl) {
		if (ctrl->p_new.p_s64 && *ctrl->p_new.p_s64 > 0)
			fps = *ctrl->p_new.p_s64;
		else if (ctrl->p_cur.p_s64 && *ctrl->p_cur.p_s64 > 0)
			fps = *ctrl->p_cur.p_s64;
	}

	if (fps <= 0 && s_data->mode_prop_idx >= 0 &&
	    s_data->mode_prop_idx < s_data->sensor_props.num_modes)
		fps = s_data->sensor_props
			.sensor_modes[s_data->mode_prop_idx]
			.control_properties.default_framerate;

	if (fps <= 0)
		fps = 30000000;

	return fps;
}


/**
 * zedx_fill_string_ctrl - Fill the eeprom buffer.
 * @tc_dev: tegracam_device driver structure.
 * @v4l2_ctrl: video for linux driver structure.
 *
 * Fill the eeprom buffer memory 16 bits at a time. The eeprom
 * is written later by the eeprom driver (avoiding any bottleneck).
 *
 * Context: Non critical function, can sleep.
 * Return: 0 in case of success and a negative errno in case of error.
 */
static int zedx_fill_string_ctrl(struct tegracam_device *tc_dev,
		struct v4l2_ctrl *ctrl)
{
	struct zedx *priv = tc_dev->priv;
	int i, ret;

	switch (ctrl->id) {
		case TEGRA_CAMERA_CID_EEPROM_DATA:
			for (i = 0; i < AR0234_EEPROM_SIZE; i++) {
				ret = sprintf(&ctrl->p_new.p_char[i*2], "%c",
						priv->eeprom_buf[i]);
				if (ret < 0)
					return -EINVAL;
			}
			break;
		default:
			return -EINVAL;
	}
	ctrl->p_cur.p_char = ctrl->p_new.p_char;

	return 0;
}

/**
 * zedx_fill_eeprom - Fill the calibration structures.
 * @tc_dev: tegracam_device driver structure.
 * @v4l2_ctrl: video for linux driver structure.
 *
 * Copy the content of the eeprom in the driver calibration structures.
 *
 * Context: Non critical function, can sleep.
 * Return: 0 in case of success and a negative errno in case of error.
 */
static int zedx_fill_eeprom(struct tegracam_device *tc_dev,
		struct v4l2_ctrl *ctrl)
{
#if !IS_ENABLED(CONFIG_TEGRA_L4T_351)
	struct zedx *priv = tc_dev->priv;
	LiEeprom_Content_Struct tmp;
	int i = 0;
	int index = 0;
	char buf_serialNumber[32] = {0};
	u8 buf[32] = {0};
	
	memcpy(buf, &priv->eeprom_buf[256], 32);

	for (i = 0; i < 32; i++) {
		index += scnprintf(&buf_serialNumber[index],32-index,"%02x",buf[i]);
	}

	switch (ctrl->id)
	{
		case TEGRA_CAMERA_CID_STEREO_EEPROM:
			memset(&(priv->EepromCalib), 0, sizeof(NvCamSyncSensorCalibData));
			memset(ctrl->p_new.p, 0, sizeof(NvCamSyncSensorCalibData));
			memcpy(&tmp, priv->eeprom_buf, sizeof(LiEeprom_Content_Struct));

			//TODO -- use zedx eeprom data to match //
			/*if (priv->sync_sensor_index == 1) {
				priv->EepromCalib.cam_intr =  tmp.left_cam_intr;
			} else if (priv->sync_sensor_index == 2) {
				priv->EepromCalib.cam_intr =  tmp.right_cam_intr;
			} else {
				priv->EepromCalib.cam_intr =  tmp.left_cam_intr;
			}
			priv->EepromCalib.cam_extr = tmp.cam_extr;
			priv->EepromCalib.imu_present = tmp.imu_present;
			priv->EepromCalib.imu = tmp.imu;
			memcpy(priv->EepromCalib.serial_number, tmp.serial_number,
					CAMERA_MAX_SN_LENGTH);

			if (priv->sync_sensor_index == 1)
				priv->EepromCalib.rls = tmp.left_rls;
			else if (priv->sync_sensor_index == 2)
				priv->EepromCalib.rls = tmp.right_rls;
			else
				priv->EepromCalib.rls = tmp.left_rls;*/	 
			memcpy(priv->EepromCalib.serial_number,buf_serialNumber,32);
			priv->EepromCalib.cam_intr.distortion_type = 0;
			memcpy(ctrl->p_new.p, (u8 *)&(priv->EepromCalib),
					sizeof(NvCamSyncSensorCalibData));
			break;
		default:
			return -EINVAL;
	}

	ctrl->p_cur.p = ctrl->p_new.p;
#endif
	return 0;
}

/**
 * struct tegracam_ctrl_ops - Structure of control functions.
 * @numctrls: number of control function pointers.
 * @ctrl_cid_list: List of uint 32 control IDs.
 * @string_ctrl_size: Size of the calibration EEPROM.
 * @compound_ctrl_size: Size of the driver calibration data struct.
 * @set_gain: Gain setting function.
 * @set_exposure: Exposure setting function.
 * @set_exposure_short: Exposure setting function.
 * @set_frame_rate: Frame rate setting function.
 * @set_group_hold: Doing Nothing for now.
 * @fill_string_ctrl: Control over the EEPROM, to fill it.
 * @fill_compound_ctrl: Fill the calibration structures from the EEPROM.
 *
 * Structure containing all the control operation function required by
 * NVidia tegracam driver.
 */
static struct tegracam_ctrl_ops zedx_ctrl_ops = {
	.numctrls = ARRAY_SIZE(ctrl_cid_list),
	.ctrl_cid_list = ctrl_cid_list,
	.string_ctrl_size = {AR0234_EEPROM_STR_SIZE},
	.compound_ctrl_size = {sizeof(NvCamSyncSensorCalibData)},
	.set_gain = zedx_set_gain,
	.set_exposure = zedx_set_exposure,
	.set_exposure_short = zedx_set_exposure,
	.set_frame_rate = zedx_set_frame_rate,
	.set_group_hold = zedx_set_group_hold,
	.fill_string_ctrl = zedx_fill_string_ctrl,
	.fill_compound_ctrl = zedx_fill_eeprom,
};

/**
 * zedx_parse_dt - Device tree parser.
 * @tc_dev: tegracam_device driver structure.
 *
 * Copy the content of the device tree. Mainly initializing
 * the GPIOs, especially the master clock to trigger the frame capture.
 *
 * Context: Non critical function, can sleep.
 * Return: A struct camera_common_pdata pointer in case of success
 *         and a NULL pointer in case of error.
 */
static struct camera_common_pdata *zedx_parse_dt(struct tegracam_device *tc_dev)
{
	struct device *dev = tc_dev->dev;
	struct device_node *node = dev->of_node;
	struct camera_common_pdata *board_priv_pdata;
	const struct of_device_id *match;
	int err;

	if (!node)
		return NULL;

	match = of_match_device(zedx_of_match, dev);
	if (!match)
	{
		dev_err(dev, "Failed to find a matching device tree ID\n");
		return NULL;
	}

	board_priv_pdata = devm_kzalloc(dev, sizeof(*board_priv_pdata), GFP_KERNEL);

	err = of_property_read_string(node, "mclk",
			&board_priv_pdata->mclk_name);
	if (err)
		dev_err(dev, "mclk not in DT\n");


	board_priv_pdata->has_eeprom =
		of_property_read_bool(node, "has-eeprom");

	return board_priv_pdata;
}

/**
 * zedx_set_mode - Set the camera mode.
 * @tc_dev: tegracam_device driver structure.
 *
 * Read the camera mode in the device tree and sets it.
 * For instance it can set the camera in 1080p 30 fps (mode 0).
 *
 * Context: Non critical function, can sleep.
 * Return: 0 in case of success, and a negative errno in case of error.
 */
static int zedx_set_mode(struct tegracam_device *tc_dev)
{
	struct zedx *priv = (struct zedx *)tegracam_get_privdata(tc_dev);
	struct camera_common_data *s_data = tc_dev->s_data;
	struct device *dev = tc_dev->dev;
	const struct of_device_id *match;
	u16 vt_pix_clk_div,vt_sys_clk_div,pll_multiplier,pre_pll_clk_div;
	int err;

	match = of_match_device(zedx_of_match, dev);
	if (!match)
	{
		dev_err(dev, "Failed to find matching dt id\n");
		return -EINVAL;
	}

	dev_dbg(dev, "%s: setting the device mode\n", __func__);

	if (priv->streaming && priv->auto_started) {
		err = zedx_write_table(priv, mode_table[AR0234_MODE_STOP_STREAM]);
		if (err)
			return err;
		priv->streaming = false;
		priv->auto_started = false;
		priv->stream_users = 0;
	} else if (priv->streaming) {
		dev_dbg(dev, "%s: stream already active, skip mode reprogramming\n",
			__func__);
		return 0;
	}

	err = zedx_write_table(priv, mode_table[AR0234_MODE_STOP_STREAM]);
	if (err)
		return err;

	if (s_data->mode_prop_idx < 0)
		return -EINVAL;

	//dev_dbg(dev, "%s: mode index:%d\n", __func__, s_data->mode_prop_idx);
	err = zedx_write_table(priv, mode_table[s_data->mode_prop_idx]);
	if (err)
		return err;

	if (!priv->sync_slave)
	{
		zedx_read_reg(priv, AR0234_FLL, &priv->frame_length_line);

		atomic_set(&master_frame_length,(int) priv->frame_length_line);

		if (verbosity_level)
			dev_info(dev, "%s: serializer in master mode\n", __func__);

	}

	zedx_read_reg(priv,AR0234_PLL_MULTIPLIER,&pll_multiplier);
	zedx_read_reg(priv,AR0234_PRE_PLL_CLK_DIV,&pre_pll_clk_div);
	zedx_read_reg(priv,AR0234_VT_SYS_CLK_DIV,&vt_sys_clk_div);
	zedx_read_reg(priv,AR0234_VT_PIX_CLK_DIV,&vt_pix_clk_div);

	priv->pix_clk = EXTCLK*pll_multiplier;

	priv->pix_clk = priv->pix_clk/(pre_pll_clk_div*vt_sys_clk_div*vt_pix_clk_div);

	priv->fps = zedx_get_frame_rate_ctrl(tc_dev);
	err = zedx_set_frame_rate(tc_dev, priv->fps);
	if (err)
		return err;

	return 0;
}

/* Find the sibling zedx priv on the same channel whose i2c addr matches
 * sib_cam_addr (returned by dser_get_sibling_on_phy). Used to auto-start/stop a
 * sibling sensor sharing the same CSI PHY so a single-sensor grab still produces
 * frames (MAX96712 PHY output requires all routed VCs active). */
static struct zedx *zedx_find_sibling(struct zedx *priv, int sib_cam_addr)
{
	int i;

	if (sib_cam_addr < 0 || !priv->i2c_client)
		return NULL;
	for (i = 0; i < N_ZEDX; i++) {
		struct zedx *sib = zedx_array[i];
		if (!sib)
			break;
		if (sib != priv &&
		    sib->channel == priv->channel &&
		    sib->i2c_client &&
		    (unsigned int)sib->i2c_client->addr == (unsigned int)sib_cam_addr)
			return sib;
	}
	return NULL;
}

/**
 * zedx_start_streaming - Start the frame capture.
 * @tc_dev: tegracam_device driver structure.
 *
 * Put the camera into streaming mode.
 *
 * Context: Non critical function, can sleep.
 * Return: 0 in case of success, and a negative errno in case of error.
 */
static int zedx_start_streaming(struct tegracam_device *tc_dev)
{
	struct zedx *priv = (struct zedx *)tegracam_get_privdata(tc_dev);
	int err;

	dev_dbg(tc_dev->dev, "%s\n", __func__);

	mutex_lock(&zedx_stream_lock);

	if (priv->streaming) {
		if (priv->auto_started) {
			err = zedx_write_table(priv, mode_table[AR0234_MODE_STOP_STREAM]);
			if (err && err != -EREMOTEIO) {
				mutex_unlock(&zedx_stream_lock);
				return err;
			}
			err = zedx_write_table(priv, mode_table[AR0234_MODE_START_STREAM]);
			if (err) {
				priv->streaming = false;
				priv->stream_users = 0;
				priv->auto_started = false;
				mutex_unlock(&zedx_stream_lock);
				return err;
			}
			dev_info(tc_dev->dev,
				 "%s: restarted auto-started sensor for userspace capture\n",
				 __func__);
		}
		priv->stream_users++;
		priv->auto_started = false;
		mutex_unlock(&zedx_stream_lock);
		return 0;
	}

	err = zedx_write_table(priv, mode_table[AR0234_MODE_START_STREAM]);
	if (err) {
		mutex_unlock(&zedx_stream_lock);
		return err;
	}
	priv->streaming = true;
	priv->stream_users = 1;
	priv->auto_started = false;

	/* Auto-start sibling sensor sharing the same CSI PHY (stereo L/R or shared-PHY
	 * mono). Its frames flow on its own VC and are dropped (no VI captures them). */
	if (auto_start_sibling && priv->i2c_client) {
		int sib_addr = dser_get_sibling_on_phy((int)priv->channel,
						       (unsigned int)priv->i2c_client->addr);
		struct zedx *sib = zedx_find_sibling(priv, sib_addr);
		if (sib && !sib->streaming) {
			int serr;
			int sib_mode = 0;

			if (tc_dev->s_data && tc_dev->s_data->mode_prop_idx >= 0)
				sib_mode = tc_dev->s_data->mode_prop_idx;

			/* Ensure sibling has a valid mode loaded: it may never have been STREAMON'd
			 * by userspace, so zedx_set_mode may not have run for it. */
			if (sib->tc_dev && sib->tc_dev->s_data)
				sib->tc_dev->s_data->mode_prop_idx = sib_mode;
			if (sib->tc_dev)
				zedx_set_mode(sib->tc_dev);
			serr = zedx_write_table(sib, mode_table[AR0234_MODE_START_STREAM]);
			if (!serr) {
				sib->streaming = true;
				sib->stream_users = 0;
				sib->auto_started = true;
				dev_info(tc_dev->dev,
					 "%s: auto-started sibling sensor on shared PHY\n", __func__);
			} else {
				dev_warn(tc_dev->dev,
					 "%s: failed to auto-start sibling (%d)\n", __func__, serr);
			}
		}
	}
	mutex_unlock(&zedx_stream_lock);
	return 0;
}

/**
 * zedx_stop_streaming - Stop the frame capture.
 * @tc_dev: tegracam_device driver structure.
 *
 * Stop the camera streaming.
 *
 * Context: Non critical function, can sleep.
 * Return: 0 in case of success, and a negative errno in case of error.
 */
static int zedx_stop_streaming(struct tegracam_device *tc_dev)
{
	struct zedx *priv = (struct zedx *)tegracam_get_privdata(tc_dev);
	struct zedx *sib = NULL;
	int err = 0;
	int sib_addr;

	dev_dbg(tc_dev->dev, "%s\n", __func__);

	mutex_lock(&zedx_stream_lock);

	if (priv->stream_users > 0)
		priv->stream_users--;

	if (priv->stream_users > 0) {
		mutex_unlock(&zedx_stream_lock);
		return 0;
	}

	if (priv->i2c_client) {
		sib_addr = dser_get_sibling_on_phy((int)priv->channel,
						   (unsigned int)priv->i2c_client->addr);
		sib = zedx_find_sibling(priv, sib_addr);
	}

	if (sib && sib->stream_users > 0) {
		priv->auto_started = true;
		mutex_unlock(&zedx_stream_lock);
		return 0;
	}

	if (!priv->streaming) {
		priv->auto_started = false;
		mutex_unlock(&zedx_stream_lock);
		return 0;
	}

	err = zedx_write_table(priv, mode_table[AR0234_MODE_STOP_STREAM]);
	// Write table will return -EREMOTEIO if the i2c device is disconnected
	// This return does not seem well handled by the application so we ignore it here
	if(err == -EREMOTEIO){
		dev_dbg(tc_dev->dev, "%s: i2c device disconnected, ignoring stop streaming error\n", __func__);
		priv->streaming = false;
		priv->auto_started = false;
		priv->stream_users = 0;
		mutex_unlock(&zedx_stream_lock);
		return 0;
	}

	if (err){
		dev_err(tc_dev->dev, "%s: failed to stop streaming %d\n", __func__, err);
		mutex_unlock(&zedx_stream_lock);
		return err;
	}

	priv->streaming = false;
	priv->stream_users = 0;
	priv->auto_started = false;

	/* If we auto-started a sibling (and the user never opened it), stop it now. */
	if (sib && sib->auto_started && sib->streaming && sib->stream_users == 0) {
		zedx_write_table(sib, mode_table[AR0234_MODE_STOP_STREAM]);
		sib->streaming = false;
		sib->stream_users = 0;
		sib->auto_started = false;
		dev_info(tc_dev->dev, "%s: stopped auto-started sibling sensor\n", __func__);
	}

	mutex_unlock(&zedx_stream_lock);
	return 0;
}

/**
 * struct camera_common_sensor_ops - Tegra camera operation functions
 * @numfrmfmts: Number of camera modes.
 * @frmfmt_table: Camera modes table.
 * @power_on: Deserializer power on function.
 * @power_off: Deserializer power off function.
 * @parse_dt: Device tree parser, get the frame trigger and other gpios.
 * @power_get: Get the Deserializer power state.
 * @power_put: Put the Deserializer in a certain power mode.
 * @set_mode: Set the streaming mode.
 * @start_streaming: Start the camera stream.
 * @stop_streaming: Stop the camera stream.
 *
 * Basic camera operation functions.
 */
static struct camera_common_sensor_ops zedx_common_ops = {
	.numfrmfmts = ARRAY_SIZE(ar0234_frmfmt),
	.frmfmt_table = ar0234_frmfmt,
	.power_on = zedx_power_on,
	.power_off = zedx_power_off,
	.parse_dt = zedx_parse_dt,
	.power_get = zedx_power_get,
	.power_put = zedx_power_put,
	.set_mode = zedx_set_mode,
	.start_streaming = zedx_start_streaming,
	.stop_streaming = zedx_stop_streaming,
};

/* Driver operations */

/**
 * zedx_open() - Open the camera device.
 * @sd: v4l2 subdevice data.
 * @fh: v4l2 subdevice file header.
 *
 * This function is called whenever a user accesses the camera.
 *
 * Context: Can sleep.
 * Return: 0 in case of success and a negative errno in case of unregistered i2c device.
 */
static int zedx_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (client == NULL)
		return -EADDRNOTAVAIL;
	dev_dbg(&client->dev, "%s: Accessing the camera\n", __func__);

	return 0;
}

static int zedx_eeprom_device_release(struct zedx *priv)
{
	int i;

	for (i = 0; i < AR0234_EEPROM_NUM_BLOCKS; i++) {
		if (priv->eeprom[i].i2c_client != NULL) {
			i2c_unregister_device(priv->eeprom[i].i2c_client);
			priv->eeprom[i].i2c_client = NULL;
		}
	}

	return 0;
}

static int zedx_get_eeprom_info(struct zedx *priv)
{
	int i;
	unsigned long sn_ = 0; 
	for (i = 3; i >= 0; i--)
	{
		sn_ = (sn_ << 8) | priv->eeprom_buf[AR0234_EEPROM_BLOCK_SIZE+i];
	}
	priv->serial_number = sn_;
	priv->zed_sysfs.model_id = priv->eeprom_buf[AR0234_EEPROM_BLOCK_SIZE+5];
	priv->zed_sysfs.awb = priv->eeprom_buf[AR0234_EEPROM_BLOCK_SIZE+6];

	return 0;
}

int custom_s_ctrl(struct v4l2_ctrl *ctrl){
	struct zedx *priv = container_of(ctrl->handler,
		struct zedx, ctrl_handler);
	char tmp[3]={};
	int err =0; 
	int i =0;
	uint8_t tmp_eeprom_buff[AR0234_EEPROM_SIZE/2] = {};

	switch (ctrl->id) {
	case ZED_CAMERA_CID_EEPROM_DATA:
		for(i=0;i<AR0234_EEPROM_SIZE/2;i++){ // convert hex string value to uint8_t "7b" > 123
			if((i*2) >= strlen(ctrl->p_new.p_char)){
				dev_err(&priv->i2c_client->dev,"Exceeding Cntl buffer size\r\n");
				priv->ioctl_updated = false;
				return err;
			}

			memcpy(tmp,&ctrl->p_new.p_char[i*2],2);

			err = kstrtou8(tmp,16,&tmp_eeprom_buff[i]);

			if (err){
				dev_err(&priv->i2c_client->dev,"Writing IOCTL failed\r\n");
				priv->ioctl_updated = false;
				return err;
			}

			if(tmp_eeprom_buff[i] != priv->eeprom_buf[(AR0234_EEPROM_SIZE/2)+i]){
				dev_dbg(&priv->i2c_client->dev,"IOCTL UPDATED\r\n");
				priv->ioctl_updated = true;
			}
		}

		if(err)
			return err;
		
		memcpy(&priv->eeprom_buf[AR0234_EEPROM_SIZE/2],&tmp_eeprom_buff,sizeof(tmp_eeprom_buff));
		break;
		default:
			pr_err("%s: Unknown ctrl id %x.\n", __func__,ctrl->id);
			return -EINVAL;
	}

	return 0;
}

// This function is executed during tegracam_v4l2subdev_register(), 
// we use it to add a new custom control to write/read to the eeprom 
static int subdev_register(struct v4l2_subdev *sd){
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_common_data *s_data = to_camera_common_data(&client->dev);
	struct zedx *priv = (struct zedx *)s_data->priv;
	struct v4l2_ctrl *ctrl;
	int err = 0;
	int i, j, num_ctrls;

	num_ctrls = ARRAY_SIZE(cfg_list);
	v4l2_ctrl_handler_init(&priv->ctrl_handler, num_ctrls);

	for (i = 0; i < num_ctrls; i++) {
		ctrl = v4l2_ctrl_new_custom(&priv->ctrl_handler,
			&cfg_list[i], NULL);
		if (ctrl == NULL) {
			dev_err(&client->dev, "Failed to init %s ctrl\n",
				cfg_list[i].name);
			continue;
		}
		
		if (priv->ctrl_handler.error) {
			dev_err(&client->dev, "Failed to init controls: %d\n", priv->ctrl_handler.error);
			return priv->ctrl_handler.error;
		}

		if(ctrl->id == ZED_CAMERA_CID_EEPROM_DATA){ // Initialize eeprom ctrl string value
			char tmp [((AR0234_EEPROM_SIZE/2)*2)+1] = {};
			int index = 0;

			for (j = 0; j < AR0234_EEPROM_SIZE/2; j++) { // We make only the 2nd 256 bytes accessible
				index += scnprintf(&tmp[index],sizeof(tmp)-index,"%02x",priv->eeprom_buf[(AR0234_EEPROM_SIZE/2)+j]);
			}

			v4l2_ctrl_s_ctrl_string(ctrl,tmp);
		}

		priv->ctrls[i] = ctrl;
	}

	priv->num_ctrls = num_ctrls;

	// L4T 32.7: kernel version is 4.9
	#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
		err = v4l2_ctrl_add_handler(
			sd->ctrl_handler,
			&priv->ctrl_handler,
			NULL);
	#else
		err = v4l2_ctrl_add_handler(
			sd->ctrl_handler,
			&priv->ctrl_handler,
			NULL,0);
	#endif

	if (err){
		v4l2_ctrl_handler_free(&priv->ctrl_handler);
		return err;
	}

	return 0;
}

/**
 * struct v4l2_subdev_internal_ops - v4l2 operations for the device.
 * @open: opening function.
 *
 * Only to register the v4l2 device. the opening function only prints a message.
 */
static const struct v4l2_subdev_internal_ops zedx_subdev_internal_ops = {
	.open = zedx_open,
	.registered = subdev_register,
};

static int zedx_eeprom_device_init(struct zedx *priv)
{
	struct camera_common_pdata *pdata =  priv->s_data->pdata;
	struct device *dev = priv->s_data->dev;
	char *dev_name = "eeprom_zedx";
	static struct regmap_config eeprom_regmap_config = {
		.reg_bits = 8,
		.val_bits = 8
	};
	int i;
	int err;

	if (!pdata->has_eeprom)
		return -EINVAL;

	for (i = 0; i < AR0234_EEPROM_NUM_BLOCKS; i++) {

		priv->eeprom[i].adap = i2c_get_adapter(
				priv->i2c_client->adapter->nr);
		memset(&priv->eeprom[i].brd, 0, sizeof(priv->eeprom[i].brd));
		strncpy(priv->eeprom[i].brd.type, dev_name,
				sizeof(priv->eeprom[i].brd.type));

		/* assign the EEPROM addrs which is read from DT */
		priv->eeprom[i].brd.addr = priv->zed_sysfs.eeprom_id_addr + i;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
		priv->eeprom[i].i2c_client = i2c_new_device(
				priv->eeprom[i].adap, &priv->eeprom[i].brd);
#else
		priv->eeprom[i].i2c_client = i2c_new_client_device(
				priv->eeprom[i].adap, &priv->eeprom[i].brd);
#endif

		i2c_put_adapter(priv->eeprom[i].adap);

		if (IS_ERR_OR_NULL(priv->eeprom[i].i2c_client)) {
			dev_dbg(dev, "%s: Failed to probe EEPROM at addr = 0x%x \n",
					__func__, priv->eeprom[i].brd.addr);
			return -1;
		}
		priv->eeprom[i].regmap = devm_regmap_init_i2c(
				priv->eeprom[i].i2c_client, &eeprom_regmap_config);
		if (IS_ERR(priv->eeprom[i].regmap)) {
			err = PTR_ERR(priv->eeprom[i].regmap);
			zedx_eeprom_device_release(priv);
			return err;
		}
	}
	return 0;
}

static int zedx_read_eeprom(struct zedx *priv)
{
#ifdef DEBUG
	struct camera_common_data *s_data = priv->s_data;
	struct device *dev = s_data->dev;
#endif
	int err, i;

	for (i = 0; i < AR0234_EEPROM_NUM_BLOCKS; i++) {
		err = regmap_bulk_read(priv->eeprom[i].regmap, 0,
				&priv->eeprom_buf[i * AR0234_EEPROM_BLOCK_SIZE],
				AR0234_EEPROM_BLOCK_SIZE);
		if (err) {
			return err;
		}
	}
#ifdef DEBUG
	for (i = AR0234_EEPROM_BLOCK_SIZE; i < AR0234_EEPROM_BLOCK_SIZE*2; i+=8) {
		dev_dbg(dev, "%s: %02x %02x %02x %02x %02x %02x %02x %02x",__func__,
			priv->eeprom_buf[i+0], priv->eeprom_buf[i+1], priv->eeprom_buf[i+2],
			priv->eeprom_buf[i+3], priv->eeprom_buf[i+4], priv->eeprom_buf[i+5],
			priv->eeprom_buf[i+6], priv->eeprom_buf[i+7]);
	}
#endif
	err = zedx_get_eeprom_info(priv);
	if (err)
		return err;

	return 0;
}

/**
 * zedx_board_setup() - Electrical init of the zedx camera.
 * @priv: Driver data structure.
 *
 * This functions is called when probing the device. Its role is to
 * initialize the eeprom, enable the frame trigger master clock,
 * power on the serializer, deserializer and read the eeprom.
 *
 * Context: Can sleep.
 * Return: 0 in case of success, and a negative errno otherwise.
 */
static int zedx_board_setup(struct zedx *priv)
{
	struct camera_common_data *s_data = priv->s_data;
	struct device *dev = s_data->dev;
	bool eeprom_ctrl = 0;
	int err = 0;

	if (verbosity_level>=1)
		dev_dbg(dev, "%s++\n", __func__);

	priv->gmsl_port = dser_enable_gmsl_link(priv->channel,priv->zedx_id);
	if(priv->gmsl_port < 0){
		dev_err(dev,"Error %d setting gmsl link\n", priv->gmsl_port);
		return priv->gmsl_port;
	}

	msleep(100);

	/* eeprom interface */
	err = zedx_eeprom_device_init(priv);
	if (err && s_data->pdata->has_eeprom)
		dev_warn(dev,
				"Failed to allocate eeprom reg map: %d\n", err);
	eeprom_ctrl = !err;
	err = camera_common_mclk_enable(s_data);
	if (err)
	{
		dev_err(dev,"Error %d turning on mclk\n", err);
		return err;
	}

	/* eeprom interface */
	err = zedx_power_on(s_data);
	if (err)
	{
		camera_common_mclk_disable(s_data);
		dev_err(dev,"Error %d during power on sensor\n", err);
		return err;
	}

	if (eeprom_ctrl) {
		err = zedx_read_eeprom(priv);
		if (err) {
			dev_err(dev, "Error %d reading eeprom\n", err);
			zedx_power_off(s_data);
			camera_common_mclk_disable(s_data);
		}
	}

	err = dser_open_all_gmsl_link(priv->channel);
	msleep(100);

    return err | !eeprom_ctrl;
}

static int zedx_probe_ar0234(struct zedx *priv)
{
    struct i2c_client *client = priv->i2c_client;
    struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct tegracam_device *tc_dev;
	const char *video_name;
    int err;

	tc_dev = devm_kzalloc(dev,
						  sizeof(struct tegracam_device), GFP_KERNEL);
	if (!tc_dev)
		return -ENOMEM;

	err = of_property_read_string(node, "devnode", &video_name);
	if (err){
		dev_err(dev, "Property devnode is missing from the device tree\n");
		return err;
	}

	err = of_property_read_u32(node, "sync_sensor_index",
							   &priv->sync_sensor_index);
	if (err)
	{
		dev_err(dev, "sync name index not in DT\n");
		return -EINVAL;
	}

	tc_dev->client = client;
	tc_dev->dev = dev;
	if(video_name != NULL){
		strncpy(tc_dev->name, video_name, sizeof(tc_dev->name));
	}
	tc_dev->dev_regmap_config = &sensor_regmap_config;
	tc_dev->sensor_ops = &zedx_common_ops;
	tc_dev->v4l2sd_internal_ops = &zedx_subdev_internal_ops;
	tc_dev->tcctrl_ops = &zedx_ctrl_ops;

	err = tegracam_device_register(tc_dev);
	if (err)
	{
		dev_err(dev, "tegra camera driver registration failed\n");
		return err;
	}
	priv->tc_dev = tc_dev;
	priv->s_data = tc_dev->s_data;
	priv->subdev = &tc_dev->s_data->subdev;
	tegracam_set_privdata(tc_dev, (void *)priv);

	/* might need to stop stream at power up */
	err = zedx_write_table(priv, mode_table[AR0234_MODE_STOP_STREAM]);
	if (err)
	{
		tegracam_device_unregister(tc_dev);
		return -EINVAL;
	}

	err = of_property_read_u32(node, "eeprom-addr", &priv->zed_sysfs.eeprom_id_addr);
	if (err || !priv->s_data->pdata->has_eeprom)
	{
		dev_err(dev, "%s: has-eeprom or eeprom-addr is not in dts\n", __func__);
		tegracam_device_unregister(tc_dev);
		return err;
	}

	err = zedx_board_setup(priv);
	zedx_eeprom_device_release(priv);
	if (err < 0)
	{
		dev_err(dev, "board setup failed\n");
		tegracam_device_unregister(tc_dev);
		return err;
	}
	else if (err == 1)
	{
		if (IS_ERR_OR_NULL(zedx_array[zedx_probe_count-1]))
			dev_warn(dev, "%s: You need an EEPROM for your first device entry\n", __func__);

		else if (priv->sync_sensor_index !=
			zedx_array[zedx_probe_count-1]->sync_sensor_index)
		{
			dev_dbg(dev, "%s: use serial number of the first sensor\n", __func__);
			priv->serial_number = zedx_array[zedx_probe_count-1]->serial_number;
			priv->zed_sysfs.model_id = zedx_array[zedx_probe_count-1]->zed_sysfs.model_id;
			priv->zed_sysfs.awb = zedx_array[zedx_probe_count-1]->zed_sysfs.awb;
			memcpy(priv->eeprom_buf , zedx_array[zedx_probe_count-1]->eeprom_buf, 512);
		}
	}

	err = tegracam_v4l2subdev_register(tc_dev, true);
	if (err)
	{
		dev_err(dev, "tegra camera subdev registration failed\n");
		zedx_eeprom_device_release(priv);
		tegracam_device_unregister(tc_dev);
		return err;
	}

	dev_info(&client->dev, "ZED-X sensor initialisation done\n");

	return 0;
}

static int duplicate_priv_info(struct zedx *priv) {
	priv->zed_sysfs.tc_dev = priv->tc_dev;
	priv->zed_sysfs.serial_number = priv->serial_number;
	priv->zed_sysfs.sync_sensor_index = priv->sync_sensor_index;
	priv->zed_sysfs.name = priv->s_data->subdev.name;
	priv->zed_sysfs.gmsl_port =  priv->gmsl_port;
	priv->zed_sysfs.channel =  priv->channel;
	priv->zed_sysfs.video_lock =  0;
	priv->zed_sysfs.status =  0;
	priv->zed_sysfs.zedx_id =  priv->zedx_id;
	priv->zed_sysfs.acc_addr = priv->acc_addr;
	priv->zed_sysfs.gyro_addr = priv->gyro_addr;

	return 0;
}

static int zedx_i2c_read(struct i2c_client *client, u16 reg, u16 *val)
{
    struct i2c_msg msgs[2];
    u8 reg_buf[2];
    u8 data[2];
    int ret;

    reg_buf[0] = reg >> 8;
    reg_buf[1] = reg & 0xff;

    /* Write register address */
    msgs[0].addr  = client->addr;
    msgs[0].flags = 0;
    msgs[0].len   = 2;
    msgs[0].buf   = reg_buf;

    /* Read 16-bit value */
    msgs[1].addr  = client->addr;
    msgs[1].flags = I2C_M_RD;
    msgs[1].len   = 2;
    msgs[1].buf   = data;

    ret = i2c_transfer(client->adapter, msgs, 2);
    if (ret != 2)
        return ret < 0 ? ret : -EIO;

    /* Most sensors are big-endian on the wire */
    *val = (data[0] << 8) | data[1];

    return 0;
};

/**
 * zedx_probe() - Initialize the zedx camera.
 * @client: i2c adapter structure to fill.
 * @id: i2c device id.
 *
 * This functions is called when loading the module. Its main role
 * is to setup the serializer and the deserializer of the zedx. In
 * addition, it links a bunch of device driver structures, register
 * a tegracam device and a v4l2 device.
 *
 * Context: Can sleep.
 * Return: 0 in case of success, and a negative errno otherwise.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0)
static int zedx_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
#else
static int zedx_probe(struct i2c_client *client)
#endif
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct zedx *priv;
	const char *str = NULL;
	int err;
	u16 val;

	if (!IS_ENABLED(CONFIG_OF) || !node)
		return -EINVAL;
	
	priv = devm_kzalloc(dev, sizeof(struct zedx), GFP_KERNEL);

	if (!priv)
	{
		dev_err(dev, "unable to allocate memory!\n");
		return -ENOMEM;
	}

    priv->i2c_client = client;

	err = zedx_i2c_read(client, AR0234_ID_REG, &val);
	if(err || val != AR0234_ID_VAL)
		return -ENODEV;

	err = of_property_read_string(node, "channel", &str);
	if (err)
    {
        dev_err(dev, "%s: channel not found in dts\n",__func__);
        return -EINVAL;
    }

	priv->channel = str[0] - 'a';

    if (priv->channel < 0 || priv->channel > 3)
    {
        dev_err(dev, "%s: channel %d doesn't exist\n", __func__, priv->channel);
        return -EINVAL;
    }


	err = of_property_read_string(node, "zedx-id", &str);
	if (err)
    {
        dev_err(dev, "%s: zedx-id not found in dts\n",__func__);
        return -EINVAL;
    }

	err = kstrtoint(str,10,&priv->zedx_id);

	//If dummy entry, don't continue probe - no verbose
    if (priv->zedx_id < 0) {
        return -ENODEV;
	}

	dev_info(dev, "Driver Version : v%d.%d.%d\n",ZEDX_DRIVER_VERSION_MAJOR,ZEDX_DRIVER_VERSION_MINOR,ZEDX_DRIVER_VERSION_PATCH);
	
	priv->master = false;
	priv->ioctl_updated = false;

	err = zedx_probe_ar0234(priv);
	if (err)
		return err;
	

	dev_info(dev, "%s: Serial Number : %lu",__func__,priv->serial_number);

	/* sysfs entry for this device */
	priv->zed_sysfs.tc_dev = priv->tc_dev;

	priv->zed_sysfs.serial_number = priv->serial_number;

	priv->acc_addr = ser_get_acc_addr(priv->zedx_id);
	if(priv->acc_addr < 0){
		dev_err(dev, "%s: Acc i2c addr not found %d\n", __func__,
			priv->acc_addr);
		return priv->acc_addr;
	}

	priv->gyro_addr = ser_get_gyro_addr(priv->zedx_id);
	if(priv->gyro_addr < 0){
		dev_err(dev, "%s: Gyro i2c addr not found %d\n", __func__,
			priv->gyro_addr);
		return priv->gyro_addr;
	}

	/* we try to find the /dev/videoX entry associated to this sensor */
	priv->zed_sysfs.video_id = find_video_device_by_subdev_name(priv->s_data->subdev.v4l2_dev, priv->s_data->subdev.name, &priv->zed_sysfs.parent_kobj);

	if (priv->zed_sysfs.video_id<0){
		tegracam_v4l2subdev_unregister(priv->tc_dev);
		zedx_eeprom_device_release(priv);
		tegracam_device_unregister(priv->tc_dev);
		dev_err(dev, "%s: Video device id not found\n", __func__);
		return priv->zed_sysfs.video_id;
	}

	/* we create the sysfs info entry */
	if (kobject_init_and_add(&priv->zed_sysfs.info_kobj, &zed_info_kobj_type, &priv->zed_sysfs.parent_kobj, "zed_info")<0) {
		tegracam_v4l2subdev_unregister(priv->tc_dev);
		zedx_eeprom_device_release(priv);
		tegracam_device_unregister(priv->tc_dev);
		kobject_put(&priv->zed_sysfs.info_kobj);
		return -ENOMEM;
	}

	/* we duplicate some information of priv in priv->sys_info*/
	duplicate_priv_info(priv);

	priv->probe_counter = zedx_probe_count;
	zedx_array[priv->probe_counter] = priv;
	zedx_probe_count++;
	
    dev_info(dev, "%s: success\n", __func__);

	return 0;
}

static void zedx_shutdown(struct i2c_client *client){
	struct camera_common_data *s_data = NULL;
	struct zedx *priv = NULL;
	int err = 0;
	int i = 0;
	unsigned long sn_ioctl = 0;
	unsigned long sn_eeprom = 0;
	u8 buff[4] = {};

	s_data = to_camera_common_data(&client->dev);
	priv = (struct zedx *)s_data->priv;

	if(!priv->ioctl_updated){
		dev_dbg(&priv->i2c_client->dev,"IOCTL !updated\r\n");
		return;
	}

	priv->ioctl_updated = false;

	/* eeprom interface */
	err = zedx_eeprom_device_init(priv);
	if (err)
		dev_info(&priv->i2c_client->dev, "Failed to allocate eeprom reg map: %d\n", err);

	if(priv->eeprom[1].regmap == NULL){
		dev_info(&priv->i2c_client->dev,"Regmap eeprom not set\r\n");
		err = zedx_eeprom_device_release(priv);
		return;
	}

	err = dser_enable_gmsl_link(priv->channel,priv->zedx_id);
	if(err < 0){
		dev_info(&priv->i2c_client->dev,"Failed to enable gmsl link %d\r\n",err);
		err = zedx_eeprom_device_release(priv);
		return;
	}

	msleep(100);

	err = regmap_bulk_read(priv->eeprom[1].regmap, 0, &buff, 4);
	if(err){
		dev_info(&priv->i2c_client->dev,"Failed to read eeprom to save calibrations\r\n");
		err = zedx_eeprom_device_release(priv);
		return;
	}

	for (i = 3; i >= 0; i--){
		sn_eeprom = (sn_eeprom << 8) | buff[i];
		sn_ioctl = (sn_ioctl << 8) | priv->eeprom_buf[AR0234_EEPROM_BLOCK_SIZE+i];
	}

	dev_dbg(&priv->i2c_client->dev,"Sn ioctl/eeprom: %ld %ld %ld\r\n",sn_ioctl,sn_eeprom, priv->serial_number);
	for (i = 0; i < 4; i++){
		dev_dbg(&priv->i2c_client->dev,"ioctl/eeprom: %02x/%02x\r\n",priv->eeprom_buf[AR0234_EEPROM_BLOCK_SIZE+i],buff[i]);
	}

	if(sn_eeprom != sn_ioctl){
		dev_info(&priv->i2c_client->dev,"Sn eeprom does not match\r\n");
		err = zedx_eeprom_device_release(priv);
		err = dser_open_all_gmsl_link(priv->channel);
		return;
	}

	for(i = 0;i<16;++i){ // eeprom can be written only 16 bytes at a time
		err = regmap_bulk_write(priv->eeprom[1].regmap,16 * i, &priv->eeprom_buf[AR0234_EEPROM_BLOCK_SIZE + 16*i], 16); // We write only to the 2nd 256 bytes
		msleep(20);
	}

	err = zedx_eeprom_device_release(priv);
	if(err)
		dev_info(&priv->i2c_client->dev,"Failed to release eeprom %d\r\n",err);

	err = dser_open_all_gmsl_link(priv->channel);
	msleep(100);

	dev_dbg(&priv->i2c_client->dev,"Eeprom successfully written\r\n");
}

/**
 * zedx_remove() - Remove the zedx camera.
 * @client: i2c adapter structure to remove.
 *
 * This functions is called when discharging the module. Its main role
 * is to unregister the v4l2 and tegracam devices. Otherwise, the memory
 * is freed automatically thanks to the devmkzalloc() used in the probing
 * function.
 *
 * Context: Can sleep.
 * Return: 0 in case of success.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0)
static int zedx_remove(struct i2c_client *client)
#else
static void zedx_remove(struct i2c_client *client)
#endif
{
	struct device *dev = &client->dev;
	struct camera_common_data *s_data = NULL;
	struct zedx *priv = NULL;

	s_data = to_camera_common_data(&client->dev);
	priv = (struct zedx *)s_data->priv;

	zedx_shutdown(client);

	kobject_put(&priv->zed_sysfs.info_kobj);

	if (priv->tc_dev){
		tegracam_v4l2subdev_unregister(priv->tc_dev);
		tegracam_device_unregister(priv->tc_dev);
	}

	zedx_eeprom_device_release(priv);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
	v4l2_ctrl_handler_free(&priv->ctrl_handler);
#endif

	zedx_probe_count--;
	if (zedx_probe_count < 0)
		dev_alert(&client->dev,"%s: zedx_probe_count < 0\n", __func__);

	dev_dbg(dev, " ZED-X sensor successfully removed\n");

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0)
	return 0;
#endif
}

/**
 * struct i2c_device_id - Name and ID of the i2c device.
 *  The id should be the same as the one in the dts module part.
 */
static const struct i2c_device_id zedx_id[] = {
	{"zedx", 0},
	{}};

MODULE_DEVICE_TABLE(i2c, zedx_id);

/**
 * struct i2c_driver - i2c driver functions
 * Here the name should be the same as the first part of the V4L2 devname
 * and the first part of the badge in the dts.
 * For more infos, please refer to nvidia's documentation
 */
static struct i2c_driver zedx_i2c_driver = {
	.driver = {
		.name = "zedx",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(zedx_of_match),
		/* Drivers are loaded synchronously by default,
		 * but let's not risk anything. We need this feature
		 * to give the right serial number to stereocams sensors. */
		.probe_type = PROBE_FORCE_SYNCHRONOUS,
	},
	.probe = zedx_probe,
	.remove = zedx_remove,
	.shutdown = zedx_shutdown,
	.id_table = zedx_id,
};

/* Register the i2c driver in the kernel */
module_i2c_driver(zedx_i2c_driver);

MODULE_DESCRIPTION("Media Controller driver for Stereolabs ZED-X");
MODULE_AUTHOR("STEREOLABS <support@stereolabs.com>");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_STR_VERSION);
