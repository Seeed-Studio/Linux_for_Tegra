/*
 * obc_g300_priv.h - Orbbec G300 camera driver
 *
 * Copyright (c) 2023-2025, ORBBEC CORPORATION.  All rights reserved.
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
#ifndef _OBC_G300_PRIV_H_
#define _OBC_G300_PRIV_H_


#define CONFIG_TEGRA_CAMERA_PLATFORM  1

//---------------------------------------------------------------------------
#define MAX_I2C_PACKET_SIZE 	240

#define ORB_FORMARS_MAX     	3
#define ORB_RESOLUTIONS_MAX    	15

struct orbbec_header {
	u16 len;
    u16 code;
    u16 index;
};

struct orbbec_msg_body {
	uint16_t res;
	uint16_t prop_ver;
    uint8_t  data[MAX_I2C_PACKET_SIZE - 4];
};

struct orbbec_msg_body1 {
	uint16_t res;
    uint8_t  data[MAX_I2C_PACKET_SIZE - 2];
};

struct orbbec_set_imu_cmd {
	uint16_t prop_code0;
	uint16_t prop_code1;
	uint16_t value0;
	uint16_t value1;
};

struct orbbec_set_stream_cmd {
	uint8_t stream_type;
	uint8_t res;
	uint8_t format;
	uint8_t fps;
};

struct orbbec_cmd {
	struct orbbec_header header;
	union {
		uint8_t _data[MAX_I2C_PACKET_SIZE];
        struct orbbec_msg_body msg_body;
		struct orbbec_msg_body1 msg_body1;
		struct orbbec_set_stream_cmd set_stream_cmd;
		struct orbbec_set_imu_cmd set_imu_cmd;
	};
};

struct orbbec_device_info {
	uint16_t vid;
	uint16_t pid;
	uint8_t  sn[16];
    uint8_t  asic_sn[16];
	uint16_t video_type;
	uint16_t sub_num;
	uint32_t cam_num;
};

//---------------------------------------------------------------------------
typedef enum {
	ORB_CAM_DEPTH,
	ORB_CAM_COLOR,
	ORB_CAM_IR_L,
	ORB_CAM_IR_R,
	ORB_CAM_TYPE_COUNT,
} orb_cam_type_e;

#ifdef CONFIG_TEGRA_CAMERA_PLATFORM
#include <media/camera_common.h>
#define orb_mux_subdev camera_common_data
#else
struct orb_mux_subdev {
	struct v4l2_subdev subdev;
};
#endif

struct orb_resolution {
	u16 width;
	u16 height;
	u8 n_framerates;
	const u16 *framerates;
};

struct orb_format {
	unsigned int n_resolutions;
	struct orb_resolution *resolutions;
	u32 mbus_code;
	u8 data_type;
};

struct orb_sensor {
	struct orb_mux_subdev sd;
	struct media_pad pad;
	struct v4l2_mbus_framefmt format;
	struct {
		struct orb_format *format;
		struct orb_resolution *resolution;
		u16 framerate;
	} config;
	bool streaming;
	/*struct orb_vchan *vchan;*/
	struct orb_format *formats;
	unsigned int n_formats;
	s16 pipe_id;
};


struct orb_ctrls {
	struct v4l2_ctrl_handler handler;
	// struct {
		struct v4l2_ctrl *log;
		struct v4l2_ctrl *fw_version;
		struct v4l2_ctrl *gvd;
		struct v4l2_ctrl *get_depth_calib;
		struct v4l2_ctrl *set_depth_calib;
		struct v4l2_ctrl *get_coeff_calib;
		struct v4l2_ctrl *set_coeff_calib;
		struct v4l2_ctrl *ae_roi_get;
		struct v4l2_ctrl *ae_roi_set;
		struct v4l2_ctrl *ae_setpoint_get;
		struct v4l2_ctrl *ae_setpoint_set;
		struct v4l2_ctrl *erb;
		struct v4l2_ctrl *ewb;
		struct v4l2_ctrl *hwmc;
		struct v4l2_ctrl *auto_exp;
		struct v4l2_ctrl *exposure;
		/* in ORB manual gain only works with manual exposure */
		struct v4l2_ctrl *gain;
		struct v4l2_ctrl *backlight;
		struct v4l2_ctrl *brightness;
		struct v4l2_ctrl *contrast;
		struct v4l2_ctrl *gamma;
		struct v4l2_ctrl *hue;
		struct v4l2_ctrl *saturation;
		struct v4l2_ctrl *sharpness;
		struct v4l2_ctrl *whitebalance;
		struct v4l2_ctrl *autowhitebalance;
		struct v4l2_ctrl *powerlinefreq;
		struct v4l2_ctrl *aepriority;
		//struct v4l2_ctrl *focus_absolute;

		struct v4l2_ctrl *set_date;
		struct v4l2_ctrl *reset_device_link;
		struct v4l2_ctrl *reset_device;
		struct v4l2_ctrl *pps_trigger;
		struct v4l2_ctrl *set_datelen;
		struct v4l2_ctrl *get_date;
		struct v4l2_ctrl *get_pid_sn;
		struct v4l2_ctrl *get_version;
		struct v4l2_ctrl *get_imu_fps;
		//struct v4l2_ctrl *get_imu_stock;
		struct v4l2_ctrl *get_imu_data;
		struct v4l2_ctrl *get_link_state;
	// };
};

struct max_reg_pair {
	u16 addr;
	u8 val;
};

struct orb_deser_ops {
	int (*init)(struct device *dev);
	u8 (*get_link_map)(struct device *dev);
	u8 (*get_link_init_flag)(struct device *dev);
	int (*init_tx_gpio)(struct device *dev);
	void (*set_link_init_flag)(struct device *dev, u8 link);
	int (*get_link_state)(struct device *dev, u8 link_id, int *value);
	int (*get_available_pipe)(struct device *dev, int dst_vc_id);
	int (*set_pipe)(struct device *dev, u8 link_id, u8 pipe_id,
		    		 u8 data_type1, u8 data_type2, u8 src_vc_id, u8 dst_vc_id);
	u8 (*check_pipe_lock)(struct device *dev);
	int (*release_pipe)(struct device *dev, u8 link_id, u8 pipe_id);
	int (*power_on)(struct device *dev);
	void (*power_off)(struct device *dev);
	int (*reset_dev)(struct device *dev);
	int (*write_seri_reg)(struct device *dev, u8 link_id, u16 addr, u8 val);
	int (*write_seri_tab)(struct device *dev, u8 link_id, struct max_reg_pair buf[], u16 size);
	int (*write_sensor)(struct device *dev, u8 link_id, void *data, u16 length);
	int (*read_sensor)(struct device *dev, u8 link_id, void *data, u16 length);
	int (*reset_oneshot)(struct device *dev);
};

struct orb {
	struct orb_sensor sensor;
	struct orb_resolution orb_depth_sizes[ORB_FORMARS_MAX][ORB_RESOLUTIONS_MAX];
	struct orb_resolution orb_mono_sizes[ORB_FORMARS_MAX][ORB_RESOLUTIONS_MAX];
	struct orb_resolution orb_rgb_sizes[ORB_FORMARS_MAX][ORB_RESOLUTIONS_MAX];
	struct orb_resolution orb_default_sizes[ORB_FORMARS_MAX][ORB_RESOLUTIONS_MAX];
	struct orb_format orb_depth_formats[ORB_FORMARS_MAX];
	struct orb_format orb_default_formats[ORB_FORMARS_MAX];
	struct orb_format orb_mono_formats[ORB_FORMARS_MAX];
	struct orb_format orb_rgb_formats[ORB_FORMARS_MAX];
	struct orb_ctrls ctrls;
	struct i2c_client *client;
	/* All below pointers are used for writing, cannot be const */
	struct mutex lock;
	struct regmap *regmap;
	struct regulator *vcc;
	//const struct orb_variant *variant;
	struct device *dser_dev;
	const struct orb_deser_ops *deser_ops;

	struct orbbec_device_info device_info;
	u8 src_vc; //source vc id, form max9295d
	u8 dst_vc; //destination vc id, to max9296
	u8 dser_link; //max9296 link port A,B,C,D
	// u8 dev_real_addr;  //g3xx sensor real address, 0x66
	// u8 dev_proxy_addr; //sensor proxy address, 0x1a/0x1b -> 0x66
	orb_cam_type_e cam_type;
	u16 imu_fps;
	u8 embedded_metadata_height;
	int pps_gpios;
	int orb_depth_formats_num;
	int orb_default_formats_num;
	int orb_mono_formats_num;
	int orb_rgb_formats_num;
};

//---------------------------------------------------------------------------
int max9295d_pipe_en(struct orb *state, u8 pipe_id, u8 en);
int max9295d_set_pipe(struct orb *state, u8 pipe_id, u8 data_type1, u8 data_type2, u8 src_vc_id);
int max9295d_release_pipe(struct orb *state, u8 pipe_id);
int max9295d_init_settings(struct orb *state);
int max9295d_set_orbbec_on(struct orb *state);
int max9295d_set_orbbec_off(struct orb *state);

int orbbec_reset_device_link(struct orb *state, s32 value);

//---------------------------------------------------------------------------
int orbbec_get_frame_profile_data(struct orb *state, void * data, int offset, int length);
int orbbec_get_frame_profile_len(struct orb *state, void * data);
int orbbec_get_deviceinfo(struct orb *state, void *pid, void *sn, void *asic_sn);
int sensor_stream_opt(struct orb *state, struct orbbec_set_stream_cmd *strcmd, int on);

int orb_ctrl_init(struct orb *state);

#endif