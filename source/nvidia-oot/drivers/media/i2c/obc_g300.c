/*
 * orb.c - Orbbec G300 camera driver
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
//#define DEBUG
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/media.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/videodev2.h>

#include <media/gmsl-link.h>
#include <media/obc_max9296.h>
#include <media/obc_max96712.h>

#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-mediabus.h>

#include <media/obc_g300_priv.h>

//-------------------------------------------------------------------------------------------
//#define ORB_DRIVER_NAME "ORB g300 camera driver"
#define ORB_DRIVER_NAME 		"g300"

//-------------------------------------------------------------------------------------------
enum orb_mux_pad {
    ORB_MUX_PAD_EXTERNAL,
    ORB_MUX_PAD_DEPTH,
    ORB_MUX_PAD_IR_L_T,
    ORB_MUX_PAD_IR_R_T,
    ORB_MUX_PAD_RGB,
    ORB_MUX_PAD_COUNT,
};

//orbbec stream_type 
enum stream_type {
    STREAM_TYPE_UNDEFINE = 0,
    STREAM_TYPE_GPM = 1,
    STREAM_TYPE_DEPTH = 2,
    STREAM_TYPE_MONO_L = 3,
    STREAM_TYPE_MONO_R = 4,
    STREAM_TYPE_RGB = 5,
    STREAM_TYPE_MAX = 6,
} stream_type_t;

enum orb_sensor_type {
    SENSOR_UNKNOWN = 0,
    SENSOR_IR      = 1,
    SENSOR_COLOR   = 2,
    SENSOR_DEPTH   = 3,
    SENSOR_ACCEL   = 4,
    SENSOR_GYRO    = 5,
    SENSOR_LEFT_IR = 6,
    SENSOR_RIGHT_IR = 7,
    SENSOR_TYPE_END = 8,
} orb_sensor_type_t;

enum pixel_fmt_e {
    PIXEL_FORMAT_UNDEFINED    = -1,
    PIXEL_FORMAT_PACKED_8BIT  = 8,
    PIXEL_FORMAT_PACKED_10BIT = 10,
    PIXEL_FORMAT_PACKED_12BIT = 12,
    PIXEL_FORMAT_PACKED_14BIT = 14,
    PIXEL_FORMAT_PACKED_16BIT = 16,
    PIXEL_FORMAT_YUV422 = 16,
    PIXEL_FORMAT_MJPEG = 8,
};

enum {
    ORB_ORBU,
    ORB_ASR,
    ORB_AWG,
};

struct orbbec_frame_profile_data {
    uint32_t orbbec_sensor_type;
    uint32_t orbbec_pixel_format;
    uint32_t width;
    uint32_t height;
    uint32_t max_rate;
};

//-------------------------------------------------------------------------------------------
static int resolution_map[17] = {
    256*144,
    320*180,
    320*240,
    424*240,
    480*270,
    640*360,
    640*400,
    640*480,
    848*100,
    848*480,
    967*16,
    960*540,
    1280*720,
    1280*800,
    1920*1080,
    1280*960,
    424*266,
};

struct orb_mbus_type_map {
    uint32_t map_mbus_code;
    uint8_t map_data_type;
};

static struct orb_mbus_type_map orb_mbus_code_map[25] = {
    {MEDIA_BUS_FMT_YUYV8_1X16, PIXEL_FORMAT_YUV422},   		/**< YUYV */
    {MEDIA_BUS_FMT_YUYV8_1X16, PIXEL_FORMAT_YUV422},		/**< YUY2 (same as YUYV)*/
    {MEDIA_BUS_FMT_UYVY8_1X16, PIXEL_FORMAT_YUV422},		/**< UYVY */
    {MEDIA_BUS_FMT_SGRBG12_1X12, PIXEL_FORMAT_PACKED_12BIT},	/**< SGRBG12_1X12 map to NV12 */
    {MEDIA_BUS_FMT_SGBRG12_1X12, PIXEL_FORMAT_PACKED_12BIT},	/**< SGBRG12_1X12 map to NV21 */
    {MEDIA_BUS_FMT_SBGGR8_1X8, PIXEL_FORMAT_MJPEG},					/**<SBGGR8_1X8 map to MJPG */
    {MEDIA_BUS_FMT_SBGGR12_1X12, PIXEL_FORMAT_PACKED_12BIT},	/**< SBGGR12_1X12 map to H.264 */
    {MEDIA_BUS_FMT_Y12_1X12, PIXEL_FORMAT_PACKED_12BIT},	/**< H.265 */
    {MEDIA_BUS_FMT_VYUY8_1X16, PIXEL_FORMAT_PACKED_16BIT},  /**< Y16,single channel 16bit Depth */
    {MEDIA_BUS_FMT_Y8_1X8, PIXEL_FORMAT_PACKED_8BIT},		/**< Y8, single channel 8bit Depth */
    {MEDIA_BUS_FMT_Y10_1X10, PIXEL_FORMAT_PACKED_10BIT},	/**< Y10, single channel 10bit Depth, Parse to Y16 by SDK */
    {MEDIA_BUS_FMT_Y12_1X12, PIXEL_FORMAT_PACKED_12BIT},	/**< Y11, single channel 11bit Depth, Parse to Y16 by SDK */
    {MEDIA_BUS_FMT_Y12_1X12, PIXEL_FORMAT_PACKED_12BIT},	/**< Y12, single channel 12bit Depth, Parse to Y16 by SDK */
    {MEDIA_BUS_FMT_UYVY8_1X16, PIXEL_FORMAT_YUV422},   		/**< No use GRAY (same as YUYV)*/
    {MEDIA_BUS_FMT_Y12_1X12, PIXEL_FORMAT_PACKED_12BIT},	/**< No use HEVC (same as H265)*/
    {MEDIA_BUS_FMT_Y12_1X12, PIXEL_FORMAT_PACKED_12BIT},	/**< No use I420 */
    {MEDIA_BUS_FMT_Y8_1X8, PIXEL_FORMAT_PACKED_8BIT},		/**< No use ACCEL */
    {MEDIA_BUS_FMT_Y8_1X8, PIXEL_FORMAT_PACKED_8BIT},		/**< No use GYRO */
    {MEDIA_BUS_FMT_Y8_1X8, PIXEL_FORMAT_PACKED_8BIT},		/**< No use  */
    {MEDIA_BUS_FMT_Y8_1X8, PIXEL_FORMAT_PACKED_8BIT},		/**< No use x-y-z 3D point */
    {MEDIA_BUS_FMT_Y8_1X8, PIXEL_FORMAT_PACKED_8BIT},		/**< No use RGB colored x-y-z 3d point */
    {MEDIA_BUS_FMT_Y8_1X8, PIXEL_FORMAT_PACKED_8BIT},		/**< No use RLE lostless encoded Depth, Parse to Y16 by SDK*/
    {MEDIA_BUS_FMT_RGB888_1X24, PIXEL_FORMAT_UNDEFINED},	/**< RGB888 */
    {MEDIA_BUS_FMT_RGB888_1X24,	PIXEL_FORMAT_UNDEFINED}, 	/**< No use BGR (same as RBG888)*/
    {MEDIA_BUS_FMT_Y14_1X14, PIXEL_FORMAT_UNDEFINED}		/**< Y14, single channel 14bit Depth, Parse to Y16 by SDK */
};

//-------------------------------------------------------------------------------------------
static const u16 orb_framerate_10_30[] = {10, 15, 20, 30};

static const u16 orb_framerate_to_30[] = {5, 10, 15, 30};
static const u16 orb_framerate_to_60[] = {5, 10, 15, 30, 60};
static const u16 orb_framerate_to_90[] = {5, 10, 15, 30, 60, 90};
static const u16 orb_framerate_100[]= {100};

static struct orb_resolution orb_default_sizes[] = {
	{
		.width = 1280,
		.height = 800,
		.framerates = orb_framerate_to_30,
		.n_framerates = ARRAY_SIZE(orb_framerate_to_30),
	},
};

static int orb_default_formats_num = 0;
static struct orb_format orb_default_formats[] = {
	{
		.data_type = PIXEL_FORMAT_PACKED_16BIT,	// Z16 
		.mbus_code = MEDIA_BUS_FMT_UYVY8_1X16,
		.n_resolutions = 1,
		.resolutions = orb_default_sizes,
	}, 
};

//-------------------------------------------------------------------------------------------
static const u16 orb_framerate_30 = 30;

static struct v4l2_mbus_framefmt orb_mbus_framefmt_template = {
    .width = 0,
    .height = 0,
    .code = MEDIA_BUS_FMT_FIXED,
    .field = V4L2_FIELD_NONE,
    .colorspace = V4L2_COLORSPACE_DEFAULT,
    .ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT,
    .quantization = V4L2_QUANTIZATION_DEFAULT,
    .xfer_func = V4L2_XFER_FUNC_DEFAULT,
};

//-------------------------------------------------------------------------------------------
static int probe_cam_num = -1;
static bool pps_gpios_request = 0;

//-------------------------------------------------------------------------------------------


//-------------------------------------------------------------------------------------------
static int orb_fixed_configuration(struct orb *state)
{
    struct orb_sensor *sensor = &state->sensor;

    switch(state->cam_type) {
        case ORB_CAM_DEPTH:
        default:
            sensor->formats = state->orb_depth_formats;
            sensor->n_formats = state->orb_depth_formats_num;
            break;
        case ORB_CAM_COLOR:
            sensor->formats = state->orb_rgb_formats;
            sensor->n_formats = state->orb_rgb_formats_num;
            break;
        case ORB_CAM_IR_L:
            sensor->formats = state->orb_mono_formats;
            sensor->n_formats = state->orb_mono_formats_num;
            break;
        case ORB_CAM_IR_R:
            sensor->formats = state->orb_mono_formats;
            sensor->n_formats = state->orb_mono_formats_num;
            break;
    }
    if(sensor->n_formats == 0) {
        dev_err(&state->client->dev, "frame_profile err cam_type%d formats_num =0 \n", state->cam_type);
        sensor->formats = orb_default_formats;
        sensor->n_formats = orb_default_formats_num;
    }
    return 0;
}

static int orb_format_struct_init(struct orb *state){
	int i;
	for(i = 0; i < ORB_FORMARS_MAX; i++){
		state->orb_depth_formats[i].resolutions = state->orb_depth_sizes[i];
		state->orb_mono_formats[i].resolutions = state->orb_mono_sizes[i];
		state->orb_rgb_formats[i].resolutions = state->orb_rgb_sizes[i];
	}
	return 0;
}

static int orbbec_get_frame_profile(struct orb *state){
	int ret = 0;
	int size = 0;
	int i = 0;
	int n = 0;
	int f = 0;
	int num = 0;
	int get_ir_frame_profile_flag = 0;
	struct orbbec_frame_profile_data *orbbec_frame_profile_t;
	struct orb_format *orbbec_format_t;
	struct orb_resolution *orbbec_resolutions_t;
	int *orb_formats_num; 
	int *orb_resolutions_num;
	orbbec_frame_profile_t = devm_kzalloc(&state->client->dev,10*sizeof(struct orbbec_frame_profile_data), GFP_KERNEL); 
	orb_format_struct_init(state);
	ret = orbbec_get_frame_profile_len(state, &size);
	if(size == 0) {
		dev_err(&state->client->dev, "get frame_profile size =0 retry \n");
		ret = orbbec_get_frame_profile_len(state, &size);
		if(size == 0) {
			dev_err(&state->client->dev, " err get frame_profile size =0  \n");
			ret = -1;
			goto err_6;
		}
	} 
	if(ret < 0 ) {
		dev_err(&state->client->dev, "get frame_profile size err \n");
		goto err_6;
	}
	size = size / sizeof(struct orbbec_frame_profile_data);
	dev_info(&state->client->dev, "frame_profile size = %d \n",size);
	while (i < size)
	{
		if((size - i) >= 10) {
			num = 10;
			ret = orbbec_get_frame_profile_data(state, orbbec_frame_profile_t, 
                        i*sizeof(struct orbbec_frame_profile_data), num*sizeof(struct orbbec_frame_profile_data));
            if(ret < 0 ) {
                dev_err(&state->client->dev, "orbbec_get_frame_profile_data err \n");
                continue;
            }
			i += 10;
		} else {
			num = size - i;
			ret = orbbec_get_frame_profile_data(state, orbbec_frame_profile_t, 
                        i*sizeof(struct orbbec_frame_profile_data), num*sizeof(struct orbbec_frame_profile_data));
			if(ret < 0 ) {
                dev_err(&state->client->dev, "orbbec_get_frame_profile_data err \n");
                continue;
            }
            i += size - i;
		}
		
		for (n = 0; n < num; n++) {
            struct orbbec_frame_profile_data *ptr = orbbec_frame_profile_t+n;
			dev_dbg(&state->client->dev, "n = %d, sensor_type = %d \n", n, ptr->orbbec_sensor_type);
			dev_dbg(&state->client->dev, "pixel_format = %d \n",ptr->orbbec_pixel_format);
			dev_dbg(&state->client->dev, "height = %d \n",ptr->height);
			dev_dbg(&state->client->dev, "width = %d \n",ptr->width);
			dev_dbg(&state->client->dev, "max_rate = %d \n",ptr->max_rate);
			
			if(ptr->orbbec_pixel_format > 25 ) {
				dev_err(&state->client->dev, "orbbec_pixel_format err \n");
				continue;
			}

			if(ptr->width > 2560 || ptr->height > 2560) {
				dev_err(&state->client->dev, "orbbec_resolution err \n");
				continue;
			}

			if(ptr->orbbec_sensor_type > 9) {
				dev_err(&state->client->dev, "orbbec_sensor_type err \n");
				continue;
			}

			if(ptr->max_rate > 200) {
				dev_err(&state->client->dev, "orbbec_max_rate err \n");
				continue;
			}

			switch (ptr->orbbec_sensor_type) {
				case SENSOR_IR:
				case SENSOR_LEFT_IR:
				case SENSOR_RIGHT_IR:
					orbbec_format_t = state->orb_mono_formats;
					orb_formats_num = &state->orb_mono_formats_num;
					if(get_ir_frame_profile_flag == 0)
                        get_ir_frame_profile_flag = ptr->orbbec_sensor_type;
					break;	
				case SENSOR_COLOR:
					orbbec_format_t = state->orb_rgb_formats;
					orb_formats_num = &state->orb_rgb_formats_num;
					break;
				case SENSOR_DEPTH:
					orbbec_format_t = state->orb_depth_formats;
					orb_formats_num = &state->orb_depth_formats_num;
					break;
				default :
					dev_err(&state->client->dev, "sensor_type err \n");
                    continue;
					break;
			}

			if(state->device_info.pid == 0x80b && orbbec_format_t == state->orb_rgb_formats ) {
				if(ptr->width == 1920 || ptr->width == 960 || ptr->width == 320)
                    continue;
			}
			if(orbbec_format_t == state->orb_mono_formats && get_ir_frame_profile_flag != ptr->orbbec_sensor_type)
                continue;

			if(*orb_formats_num == 0) {
				(orbbec_format_t+*orb_formats_num)->mbus_code = orb_mbus_code_map[ptr->orbbec_pixel_format].map_mbus_code;
				(orbbec_format_t+*orb_formats_num)->data_type = orb_mbus_code_map[ptr->orbbec_pixel_format].map_data_type;
				orbbec_resolutions_t = (orbbec_format_t+*orb_formats_num)->resolutions;
				orb_resolutions_num = &(orbbec_format_t+*orb_formats_num)->n_resolutions;
				*orb_formats_num=*orb_formats_num + 1; 
			} else {
				
				for(f = 0;f < *orb_formats_num;f++){
					if((orbbec_format_t+f)->mbus_code == orb_mbus_code_map[ptr->orbbec_pixel_format].map_mbus_code){
						orbbec_resolutions_t = (orbbec_format_t+f)->resolutions;
						orb_resolutions_num = &(orbbec_format_t+f)->n_resolutions;
						break;
					}
				}
				
				
				if(f == *orb_formats_num && (orbbec_format_t+f-1)->mbus_code != orb_mbus_code_map[ptr->orbbec_pixel_format].map_mbus_code){
					if(*orb_formats_num > (ORB_FORMARS_MAX-1)) {
						dev_err(&state->client->dev, "orb_formats_num is exceeded \n");
						continue;
					}
					(orbbec_format_t+*orb_formats_num)->mbus_code = orb_mbus_code_map[ptr->orbbec_pixel_format].map_mbus_code;
					(orbbec_format_t+*orb_formats_num)->data_type = orb_mbus_code_map[ptr->orbbec_pixel_format].map_data_type;
					orbbec_resolutions_t = (orbbec_format_t+*orb_formats_num)->resolutions;
					orb_resolutions_num = &(orbbec_format_t+*orb_formats_num)->n_resolutions;
					*orb_formats_num=*orb_formats_num + 1; 
				}
								
			}
			if(*orb_resolutions_num >= ORB_RESOLUTIONS_MAX) 
                continue;
			(orbbec_resolutions_t+*orb_resolutions_num)->width = (uint16_t)ptr->width;
			(orbbec_resolutions_t+*orb_resolutions_num)->height = (uint16_t)ptr->height;
			
			switch(ptr->max_rate){
				case 30:
					if(state->device_info.pid == 0xA13 || state->device_info.pid == 0x813){
						(orbbec_resolutions_t+*orb_resolutions_num)->framerates = orb_framerate_10_30;
					    (orbbec_resolutions_t+*orb_resolutions_num)->n_framerates = ARRAY_SIZE(orb_framerate_10_30);
					}else{
						(orbbec_resolutions_t+*orb_resolutions_num)->framerates = orb_framerate_to_30;
					    (orbbec_resolutions_t+*orb_resolutions_num)->n_framerates = ARRAY_SIZE(orb_framerate_to_30);
					}
					break;

				case 60:
					(orbbec_resolutions_t+*orb_resolutions_num)->framerates = orb_framerate_to_60;
					(orbbec_resolutions_t+*orb_resolutions_num)->n_framerates = ARRAY_SIZE(orb_framerate_to_60);
					break;

				case 90:
					(orbbec_resolutions_t+*orb_resolutions_num)->framerates = orb_framerate_to_90;
					(orbbec_resolutions_t+*orb_resolutions_num)->n_framerates = ARRAY_SIZE(orb_framerate_to_90);
					break;
				case 100:
					(orbbec_resolutions_t+*orb_resolutions_num)->framerates = orb_framerate_100;
					(orbbec_resolutions_t+*orb_resolutions_num)->n_framerates = ARRAY_SIZE(orb_framerate_100);
					break;
				default:
					dev_err(&state->client->dev, "max_rate err \n");
					(orbbec_resolutions_t+*orb_resolutions_num)->framerates = orb_framerate_to_30;
					(orbbec_resolutions_t+*orb_resolutions_num)->n_framerates = ARRAY_SIZE(orb_framerate_to_30);
					break;
			}
			*orb_resolutions_num = *orb_resolutions_num + 1;
		}

	}
	err_6:
	devm_kfree(&state->client->dev, orbbec_frame_profile_t);
	return ret;
}

/* Get readable sensor name */
static const char *orb_get_sensor_name(struct orb *state)
{
    static const char *sensor_name[] = {"DEPTH", "RGB", "IR_L", "IR_R"};
    return sensor_name[state->cam_type];
}

/* This is needed for .get_fmt()
 * and if streaming is started without .set_fmt() */
static void orb_sensor_format_init(struct orb_sensor *sensor)
{
    struct orb_format *fmt;
    struct v4l2_mbus_framefmt *ffmt;
    unsigned int i;

    if (sensor->config.format)
        return;

    dev_dbg(sensor->sd.dev, "%s()\n", __func__);

    ffmt = &sensor->format;
    *ffmt = orb_mbus_framefmt_template;
    /* Use the first format */
    fmt = sensor->formats;
    ffmt->code = fmt->mbus_code;
    /* and the first resolution */
    ffmt->width = fmt->resolutions->width;
    ffmt->height = fmt->resolutions->height;

    sensor->config.format = fmt;
    sensor->config.resolution = fmt->resolutions;
    /* Set default framerate to 30, or to 1st one if not supported */
    for (i = 0; i < fmt->resolutions->n_framerates;i++) {
        if (fmt->resolutions->framerates[i] == orb_framerate_30 /* fps */) {
            sensor->config.framerate = orb_framerate_30;
            return;
        }
    }
    sensor->config.framerate = fmt->resolutions->framerates[0];
}

/* No locking needed for enumeration methods */
static int orb_sensor_enum_mbus_code(struct v4l2_subdev *sd,
    #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 10)
        struct v4l2_subdev_pad_config *cfg,
    #else
        struct v4l2_subdev_state *v4l2_state,
    #endif
        struct v4l2_subdev_mbus_code_enum *mce)
{
    struct orb_sensor *sensor = container_of(sd, struct orb_sensor, sd.subdev);

    if (mce->pad)
        return -EINVAL;

    if (mce->index >= sensor->n_formats)
        return -EINVAL;

    mce->code = sensor->formats[mce->index].mbus_code;

    return 0;
}

static int orb_sensor_enum_frame_size(struct v4l2_subdev *sd,
    #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 10)
        struct v4l2_subdev_pad_config *cfg,
    #else
        struct v4l2_subdev_state *v4l2_state,
    #endif
        struct v4l2_subdev_frame_size_enum *fse)
{
    struct orb_sensor *sensor = container_of(sd, struct orb_sensor, sd.subdev);
    struct orb_format *fmt;
    unsigned int i;

    for (i = 0, fmt = sensor->formats; i < sensor->n_formats; i++, fmt++)
        if (fse->code == fmt->mbus_code)
            break;

    if (i == sensor->n_formats)
        return -EINVAL;

    if (fse->index >= fmt->n_resolutions)
        return -EINVAL;

    fse->min_width = fse->max_width = fmt->resolutions[fse->index].width;
    fse->min_height = fse->max_height = fmt->resolutions[fse->index].height;

    dev_dbg(sd->dev, "enum_frame_size, index=%d, n_res=%d, w=%d, h=%d\n", fse->index, fmt->n_resolutions, fse->min_width, fse->min_height);

    return 0;
}

static int orb_sensor_enum_frame_interval(struct v4l2_subdev *sd,
    #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 10)
        struct v4l2_subdev_pad_config *cfg,
    #else
        struct v4l2_subdev_state *v4l2_state,
    #endif
        struct v4l2_subdev_frame_interval_enum *fie)
{
    struct orb_sensor *sensor = container_of(sd, struct orb_sensor, sd.subdev);
    struct orb_format *fmt;
    struct orb_resolution *res;
    unsigned int i;

    for (i = 0, fmt = sensor->formats; i < sensor->n_formats; i++, fmt++)
        if (fie->code == fmt->mbus_code)
            break;

    if (i == sensor->n_formats)
        return -EINVAL;

    for (i = 0, res = fmt->resolutions; i < fmt->n_resolutions; i++, res++)
        if (res->width == fie->width && res->height == fie->height)
            break;

    if (i == fmt->n_resolutions)
        return -EINVAL;

    if (fie->index >= res->n_framerates)
        return -EINVAL;

    fie->interval.numerator = 1;
    fie->interval.denominator = res->framerates[fie->index];

    return 0;
}

static int orb_sensor_get_fmt(struct v4l2_subdev *sd,
    #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 10)
        struct v4l2_subdev_pad_config *cfg,
    #else
        struct v4l2_subdev_state *v4l2_state,
    #endif
        struct v4l2_subdev_format *fmt)
{
    struct orb_sensor *sensor = container_of(sd, struct orb_sensor, sd.subdev);
    struct orb *state = v4l2_get_subdevdata(sd);

    if (fmt->pad)
        return -EINVAL;

    mutex_lock(&state->lock);

    if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 10)
        fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
        fmt->format = *v4l2_subdev_get_try_format(sd, v4l2_state, fmt->pad);
#endif
    else
        fmt->format = sensor->format;

    mutex_unlock(&state->lock);

    dev_dbg(sd->dev, "%s(): pad %x, code %x, res %ux%u\n",
            __func__, fmt->pad, fmt->format.code,
            fmt->format.width, fmt->format.height);

    return 0;
}

/* Called with lock held */
static struct orb_format *orb_sensor_find_format(
        struct orb_sensor *sensor,
        struct v4l2_mbus_framefmt *ffmt,
        struct orb_resolution **best)
{
    struct orb_resolution *res;
    struct orb_format *fmt;
    unsigned long best_delta = ~0;
    unsigned int i;

    for (i = 0, fmt = sensor->formats; i < sensor->n_formats; i++, fmt++) {
        if (fmt->mbus_code == ffmt->code)
            break;
    }
    dev_dbg(sensor->sd.dev, "%s(): mbus_code = %x, code = %x \n",
            __func__, fmt->mbus_code, ffmt->code);

    if (i == sensor->n_formats)
        /* Not found, use default */
        fmt = sensor->formats;

    for (i = 0, res = fmt->resolutions; i < fmt->n_resolutions; i++, res++) {
        unsigned long delta = abs(ffmt->width * ffmt->height -
                res->width * res->height);
        if (delta < best_delta) {
            best_delta = delta;
            *best = res;
        }
    }

    ffmt->code = fmt->mbus_code;
    ffmt->width = (*best)->width;
    ffmt->height = (*best)->height;

    ffmt->field = V4L2_FIELD_NONE;
    /* Should we use V4L2_COLORSPACE_RAW for Y12I? */
    ffmt->colorspace = V4L2_COLORSPACE_SRGB;

    return fmt;
}

static int orb_sensor_set_fmt(struct v4l2_subdev *sd,
    #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 10)
        struct v4l2_subdev_pad_config *cfg,
    #else
        struct v4l2_subdev_state *v4l2_state,
    #endif
        struct v4l2_subdev_format *fmt)
{
    struct orb_sensor *sensor = container_of(sd, struct orb_sensor, sd.subdev);
    struct orb *state = v4l2_get_subdevdata(sd);
    struct v4l2_mbus_framefmt *mf = &fmt->format;
    //unsigned r;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 10)
    dev_dbg(sensor->sd.dev, "%s(): state %p, "
            "sensor %p, cfg %p, fmt %p, fmt->format %p\n",
            __func__, state, sensor, cfg, fmt,  &fmt->format);
#else
    dev_dbg(sensor->sd.dev, "%s(): state %p, "
            "sensor %p, cfg %p, fmt %p, fmt->format %p\n",
            __func__, state, sensor, v4l2_state, fmt,  &fmt->format);
#endif

    if (fmt->pad)
        return -EINVAL;

    mutex_lock(&state->lock);

    sensor->config.format = orb_sensor_find_format(sensor, mf,
                        &sensor->config.resolution);

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 10)
    if (cfg && fmt->which == V4L2_SUBDEV_FORMAT_TRY)
        *v4l2_subdev_get_try_format(&sensor->sd.subdev, cfg, fmt->pad) = *mf;
#else
    if (v4l2_state && fmt->which == V4L2_SUBDEV_FORMAT_TRY)
        *v4l2_subdev_get_try_format(&sensor->sd.subdev, v4l2_state, fmt->pad) = *mf;
#endif

    else
// FIXME: use this format in .s_stream()
        sensor->format = *mf;

    mutex_unlock(&state->lock);

    dev_dbg(sensor->sd.dev, "%s(): pad: %x, code: %x, %ux%u\n",
            __func__, fmt->pad, fmt->format.code,
            fmt->format.width, fmt->format.height);

    return 0;
}

static struct mutex serdes_lock__;

//---------------------------------------------------------------------------------
static int orb_is_link(struct orb *state)
{
    u8 link_sta = state->deser_ops->get_link_map(state->dser_dev);
    return ((0x1<<state->dser_link) & link_sta) ? 1 : 0;
}


/* seri_pipe_id = dstvc_id */
static int orb_setup_pipeline(struct orb *state, u8 data_type1, u8 data_type2,
    s16 dser_pipe_id, u8 src_vc_id, u8 dst_vc_id)
{
    int ret = 0;
    u8 pipe_lock = 0;
    u8 seri_pipe_id = dst_vc_id;

    dev_info(&state->client->dev, 
            "set pipe, port:%d, pipe:%d, dt1: 0x%x, dt2: 0x%x, src_vc: %u, dst_vc: %u\n",
            state->dser_link, dser_pipe_id, data_type1, data_type2, src_vc_id, dst_vc_id);

    ret |= max9295d_set_pipe(state, seri_pipe_id, data_type1, data_type2, src_vc_id);
    ret |= state->deser_ops->set_pipe(state->dser_dev, state->dser_link, dser_pipe_id, data_type1, data_type2, src_vc_id, dst_vc_id);
    // ret |= max9295d_pipe_en(state, dst_vc_id, 1);

    msleep(200);
    pipe_lock = state->deser_ops->check_pipe_lock(state->dser_dev);
    dev_info(&state->client->dev, "%s pipe lock: 0x%x\n", __func__, pipe_lock);

    if (ret)
        dev_info(&state->client->dev, "failed to setup pipe\n");

    return ret;
}

static int orb_gmsl_serdes_setup(struct orb *state)
{
    struct device *dev;
    int err;

    if (!state || !state->dser_dev || !state->client)
        return -EINVAL;

    dev = &state->client->dev;

    mutex_lock(&serdes_lock__);

    dev_dbg(dev, "Setup SERDES addressing and control pipeline\n");

    err = max9295d_init_settings(state);
    if(err)
        goto error;

    state->deser_ops->set_link_init_flag(state->dser_dev, state->dser_link);

    if(state->deser_ops->get_link_init_flag(state->dser_dev) == 0){
        state->deser_ops->init_tx_gpio(state->dser_dev);
    }

    dev_info(dev, "%s success\n", __func__);

error:
    mutex_unlock(&serdes_lock__);
    return err;
}
//---------------------------------------------------------------------------------

int orbbec_reset_device_link(struct orb *state, s32 value)
{
    int ret = 0;
    struct i2c_client *c = state->client;
    
    if(!state->dser_dev)
        return -1;
    
    if(value == 0) {
        ret = max9295d_set_orbbec_off(state);
        if(state->cam_type == ORB_CAM_DEPTH) {
            ret = state->deser_ops->reset_dev(state->dser_dev);
            if (ret)
                dev_warn(&c->dev,  "failed in 9296 reset control\n");
        }
    } else {
        if(state->cam_type == ORB_CAM_DEPTH) {

            ret = orb_gmsl_serdes_setup(state);
            if (ret) {
                dev_err(&c->dev, "%s gmsl serdes setup failed\n", __func__);
                return ret;
            }

            // ret = max9295d_init_settings(state);
            // if (ret) {
            // 	dev_err(&c->dev, "%s, failed to init max9295d settings\n", __func__);
            // 	return ret;
            // }

            ret = state->deser_ops->init(state->dser_dev);
            if (ret) {
                dev_err(&c->dev, "%s, failed to init Deser chip settings\n", __func__);
                return ret;
            }
        }

        ret = max9295d_set_orbbec_on(state);
        if (ret) {
            dev_err(&c->dev, "%s, failed to max9295d set_orbbec_on\n", __func__);
            return ret;
        }
    }

    return 0;
}

/* Video ops */
static int orb_mux_g_frame_interval(struct v4l2_subdev *sd,
        struct v4l2_subdev_frame_interval *fi)
{
    struct orb *state = container_of(sd, struct orb, sensor.sd.subdev);
    struct orb_sensor *sensor = &state->sensor;

    if (NULL == sd || NULL == fi)
        return -EINVAL;

    fi->interval.numerator = 1;
    fi->interval.denominator = sensor->config.framerate;

    dev_dbg(sd->dev, "%s(): %s %u\n", __func__, sd->name,
            fi->interval.denominator);

    return 0;
}

static u16 __orb_probe_framerate(const struct orb_resolution *res, u16 target)
{
    int i;
    u16 framerate;

    for (i = 0; i < res->n_framerates; i++) {
        framerate = res->framerates[i];
        if (target <= framerate)
            return framerate;
    }

    return res->framerates[res->n_framerates - 1];
}

static int orb_mux_s_frame_interval(struct v4l2_subdev *sd,
        struct v4l2_subdev_frame_interval *fi)
{
    struct orb *state = container_of(sd, struct orb, sensor.sd.subdev);
    struct orb_sensor *sensor = &state->sensor;
    u16 framerate = 1;

    if (NULL == sd || NULL == fi || fi->interval.numerator == 0) {
        dev_err(sd->dev, "%s(): %p, %p %d\n", __func__, sd, fi, fi ? fi->interval.numerator : -1);
        return -EINVAL;
    }
    framerate = fi->interval.denominator / fi->interval.numerator;
    framerate = __orb_probe_framerate(sensor->config.resolution, framerate);
    sensor->config.framerate = framerate;
    fi->interval.numerator = 1;
    fi->interval.denominator = framerate;

    dev_dbg(sd->dev, "%s(): %s %u\n", __func__, sd->name, framerate);

    return 0;
}

static int orb_mux_s_stream(struct v4l2_subdev *sd, int on)
{
    struct orb *state = container_of(sd, struct orb, sensor.sd.subdev);
    struct orb_sensor *sensor;
    int ret = 0 ,i = 0 ,res_product = 0;
    int restore_val = 0;
    uint8_t stream_id = 0, stream_fmt = 0, stream_fmt_index = 0, stream_res = 0, md_fmt = 0;
    uint8_t data_type1, data_type2, src_vc_id, dst_vc_id;
    struct orbbec_set_stream_cmd stream_cmd = {0, 0, 0, 0};

    sensor = &state->sensor;

    switch(state->cam_type) {
        case ORB_CAM_DEPTH:
            stream_id = STREAM_TYPE_DEPTH;
            break;
        case ORB_CAM_COLOR:
            stream_id = STREAM_TYPE_RGB;
            break;
        case ORB_CAM_IR_L:
            stream_id = STREAM_TYPE_MONO_L;
            break;
        case ORB_CAM_IR_R:
            stream_id = STREAM_TYPE_MONO_R;
            break;
        default:
            dev_err(&state->client->dev, "Invail camera type!\n");
            return -EINVAL;
    }
    md_fmt = GMSL_CSI_DT_EMBED;

    stream_fmt_index = sensor->config.format->data_type;
    switch (stream_fmt_index) {
        case PIXEL_FORMAT_PACKED_8BIT:
            stream_fmt = GMSL_CSI_DT_RAW_8;
            break;
        case PIXEL_FORMAT_PACKED_10BIT:
            stream_fmt = GMSL_CSI_DT_RAW_10;
            break;
        case PIXEL_FORMAT_PACKED_12BIT:
            stream_fmt = GMSL_CSI_DT_RAW_12;
            break;
        case PIXEL_FORMAT_PACKED_14BIT:
            stream_fmt = GMSL_CSI_DT_RAW_14;
            break;
        case PIXEL_FORMAT_YUV422:
            stream_fmt = GMSL_CSI_DT_YUV422_8;
            break;
        default:
            dev_err(&state->client->dev, "Invail format");
            return -EINVAL;
    }
    src_vc_id = state->src_vc;
    dst_vc_id = state->dst_vc;

    dev_info(&state->client->dev, "s_stream for stream %s, vc:%d, on:%d, format:%d ,fps:%d\n",
            state->sensor.sd.subdev.name, dst_vc_id, on, stream_fmt_index, sensor->config.framerate);

    restore_val = state->sensor.streaming;
    state->sensor.streaming = on;

    if (on) {
        /*  sensor->pipe_id == dst_vc_id */
        sensor->pipe_id = state->deser_ops->get_available_pipe(state->dser_dev, ((state->dser_link / 2) * 4) + dst_vc_id);
        if (sensor->pipe_id < 0) {
            dev_err(&state->client->dev, "No free pipe in Deser chip\n");
            ret = 0;
            goto restore_s_state;
        }

        data_type1 = stream_fmt;
        if(state->embedded_metadata_height)
            data_type2 = md_fmt;
        else
            data_type2 = 0;

        ret = orb_setup_pipeline(state, data_type1, data_type2, sensor->pipe_id, src_vc_id, dst_vc_id);
        if (ret < 0) {
            dev_err(&state->client->dev, "setup_pipeline err\n");
            goto restore_s_state;
        }

        res_product = sensor->config.resolution->width * sensor->config.resolution->height;
        for(i = 0; i < ARRAY_SIZE(resolution_map); i++) {
            if(res_product == resolution_map[i]) {
                stream_res = i;
                break;
            }
        }

        if(i == ARRAY_SIZE(resolution_map) && res_product != resolution_map[i-1])
            stream_res = -1;

        stream_cmd.stream_type = stream_id;
        stream_cmd.format = stream_fmt_index;
        stream_cmd.res = stream_res;
        stream_cmd.fps = sensor->config.framerate;
        ret = sensor_stream_opt(state, &stream_cmd, 1); //stream on
        if(ret < 0) {
            printk(KERN_ERR "\n i2c write open stream cmd err \n");
            goto restore_s_state;
        }

        msleep(300);

        state->deser_ops->reset_oneshot(state->dser_dev);

    } else {

        stream_cmd.stream_type = stream_id;
        ret = sensor_stream_opt(state, &stream_cmd, 0); //stream off
        if(ret < 0){
            printk(KERN_ERR "\n i2c write close stream cmd err \n");
        }

        msleep(50);
        
        if(max9295d_release_pipe(state, sensor->pipe_id) < 0)
        dev_warn(&state->client->dev, "release max9295 pipe failed\n");
        
        if (state->deser_ops->release_pipe(state->dser_dev, state->dser_link, sensor->pipe_id) < 0)
        dev_warn(&state->client->dev, "release Deser chip pipe failed\n");
        
        sensor->pipe_id = -1;
        msleep(300);
    }

    return 0;

restore_s_state:
    if (on && sensor->pipe_id >= 0) {
        if (state->deser_ops->release_pipe(state->dser_dev, state->dser_link, sensor->pipe_id) < 0)
            dev_warn(&state->client->dev, "release pipe failed\n");
        sensor->pipe_id = -1;
    }

    dev_err(&state->client->dev, "%s stream toggle failed! %x \n", orb_get_sensor_name(state) ,restore_val);

    state->sensor.streaming = restore_val;

    return ret;
}

static const struct v4l2_subdev_pad_ops orb_mux_pad_ops = {
    .enum_mbus_code		= orb_sensor_enum_mbus_code,
    .enum_frame_size	= orb_sensor_enum_frame_size,
    .enum_frame_interval	= orb_sensor_enum_frame_interval,
    .get_fmt		= orb_sensor_get_fmt,
    .set_fmt		= orb_sensor_set_fmt,
};

static const struct v4l2_subdev_core_ops orb_mux_core_ops = {
    //.s_power = orb_mux_set_power,
    .log_status = v4l2_ctrl_subdev_log_status,
};

static const struct v4l2_subdev_video_ops orb_mux_video_ops = {
    .g_frame_interval	= orb_mux_g_frame_interval,
    .s_frame_interval	= orb_mux_s_frame_interval,
    .s_stream		= orb_mux_s_stream,
};

static const struct v4l2_subdev_ops orb_mux_subdev_ops = {
    .core = &orb_mux_core_ops,
    .pad = &orb_mux_pad_ops,
    .video = &orb_mux_video_ops,
};

static const struct orb_deser_ops max9296_deser_ops = {
    .init	= max9296a_init_settings,
    .get_link_map = max9296a_get_link_map,
    .get_link_init_flag = max9296a_get_link_init_flag,
    .set_link_init_flag = max9296a_set_link_init_flag,
    .init_tx_gpio = max9296a_init_tx_gpio,
    .get_link_state = max9296a_get_link_state,
	.get_available_pipe = max9296a_get_available_pipe_id,
	.set_pipe = max9296a_set_pipe,
    .check_pipe_lock = max9296a_check_pipe_lock,
	.release_pipe = max9296a_release_pipe,
	.power_on = max9296a_power_on,
	.power_off = max9296a_power_off,
	.reset_dev = max9296a_reset_dev,
    .write_seri_reg = max9296a_write_seri_reg,
    .write_seri_tab = max9296a_write_seri_tab,
    .write_sensor = max9296a_write_sensor,
    .read_sensor = max9296a_read_sensor,
    .reset_oneshot = max9296a_reset_oneshot,
};

static const struct orb_deser_ops max96712_deser_ops = {
    .init	= max96712_init_settings,
    .get_link_map = max96712_get_link_map,
    .get_link_init_flag = max96712_get_link_init_flag,
    .set_link_init_flag = max96712_set_link_init_flag,
    .init_tx_gpio = max96712_init_tx_gpio,
    .get_link_state = max96712_get_link_state,
	.get_available_pipe = max96712_get_available_pipe_id,
	.set_pipe = max96712_set_pipe,
    .check_pipe_lock = max96712_check_pipe_lock,
	.release_pipe = max96712_release_pipe,
	.power_on = max96712_power_on,
	.power_off = max96712_power_off,
	.reset_dev = max96712_reset_dev,
    .write_seri_reg = max96712_write_seri_reg,
    .write_seri_tab = max96712_write_seri_tab,
    .write_sensor = max96712_write_sensor,
    .read_sensor = max96712_read_sensor,
    .reset_oneshot = max96712_reset_oneshot,
};

static int orb_mux_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
    dev_dbg(sd->dev, "%s(): %s (%p)\n", __func__, sd->name, fh);
    return 0;
}

static int orb_mux_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
    dev_dbg(sd->dev, "%s(): %s (%p)\n", __func__, sd->name, fh);
    return 0;
}

static const struct v4l2_subdev_internal_ops orb_mux_internal_ops = {
    .open = orb_mux_open,
    .close = orb_mux_close,
    // .registered = orb_mux_registered,
    // .unregistered = orb_mux_unregistered,
};

// static int orb_mux_register(struct i2c_client *c, struct orb *state)
// {
// 	return v4l2_async_register_subdev(&state->sensor.sd.subdev);
// }

static int orb_v4l_init(struct i2c_client *c, struct orb *state)
{
    struct v4l2_subdev *sd = &state->sensor.sd.subdev;
    struct media_entity *entity = &state->sensor.sd.subdev.entity;
    struct media_pad *pad = &state->sensor.pad;
    dev_t *dev_num = &state->client->dev.devt;
    const char *name_tb[] = {"depth", "color", "ir_l", "ir_r"};
    int ret;

    orb_fixed_configuration(state);
    orb_sensor_format_init(&state->sensor);

    v4l2_i2c_subdev_init(sd, c, &orb_mux_subdev_ops);
    // See tegracam_v4l2.c tegracam_v4l2subdev_register()
    // Set owner to NULL so we can unload the driver module
    sd->owner = NULL;
    sd->internal_ops = &orb_mux_internal_ops;  //The internal operation function set for V4L2 sub-devices:open、close、registered、unregistered
    sd->grp_id = *dev_num;
    v4l2_set_subdevdata(sd, state);
    snprintf(sd->name, sizeof(sd->name), "G300 %s %d-%04x", name_tb[state->cam_type], i2c_adapter_id(c->adapter), c->addr);

    sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

    pad->flags = MEDIA_PAD_FL_SOURCE;
    entity->obj_type = MEDIA_ENTITY_TYPE_V4L2_SUBDEV;
    entity->function = MEDIA_ENT_F_CAM_SENSOR;

    ret = media_entity_pads_init(entity, 1, pad);
    if (ret < 0)
        return ret;

    ret = orb_ctrl_init(state);
    if (ret < 0)
        goto e_entity;

#ifdef CONFIG_TEGRA_CAMERA_PLATFORM
    state->sensor.sd.dev = &c->dev;
    ret = camera_common_initialize(&state->sensor.sd, "g300");
    if (ret) {
        dev_err(&c->dev, "Failed to initialize g300, %d\n", ret);
        goto e_ctrl;
    }
#endif

    ret = v4l2_async_register_subdev(&state->sensor.sd.subdev);
    if (ret < 0)
        goto e_cam_com;

    return 0;

e_cam_com:
#ifdef CONFIG_TEGRA_CAMERA_PLATFORM
    camera_common_cleanup(&state->sensor.sd);
e_ctrl:
    v4l2_ctrl_handler_free(sd->ctrl_handler);
#endif
e_entity:
    media_entity_cleanup(entity);

    return ret;
}

static void orb_mux_remove(struct orb *state)
{
#ifdef CONFIG_TEGRA_CAMERA_PLATFORM
    camera_common_cleanup(&state->sensor.sd);
#endif
    v4l2_async_unregister_subdev(&state->sensor.sd.subdev);
    v4l2_ctrl_handler_free(state->sensor.sd.subdev.ctrl_handler);
    media_entity_cleanup(&state->sensor.sd.subdev.entity);
}

static int orb_prase_dt(struct orb *state)
{
    struct device *dev = &state->client->dev;
    struct device_node *node = dev->of_node;
    struct device_node *dser_node;
    struct i2c_client *dser_i2c = NULL;
    struct device_node *mode;
    const char *str;
    int value = 0xFFFF;
    int err = 0;

    dser_node = of_parse_phandle(node, "maxim,gmsl-dser-device", 0);
    if (dser_node == NULL) {
        dser_node = of_parse_phandle(node, "nvidia,gmsl-dser-device", 0);
        if (dser_node == NULL) {
            dev_err(dev, "missing %s handle\n", "[maxim|nvidia],gmsl-dser-device");
            goto error;
        }
    }

    dser_i2c = of_find_i2c_device_by_node(dser_node);
    of_node_put(dser_node);

    if (dser_i2c == NULL) {
        err = -EPROBE_DEFER;
        goto error;
    }
    if (dser_i2c->dev.driver == NULL) {
        dev_err(dev, "missing deserializer driver\n");
        goto error;
    }

    state->dser_dev = &dser_i2c->dev;

	if (of_device_is_compatible(dser_node, "maxim,obc_max9296"))
		state->deser_ops = &max9296_deser_ops;
	else if (of_device_is_compatible(dser_node, "maxim,obc_max96712"))
		state->deser_ops = &max96712_deser_ops;
	else 
        state->deser_ops = &max9296_deser_ops;

    err = of_property_read_string(node, "dser-link-port", &str);
    if (err < 0) {
        dev_err(dev, "No serdes-csi-link found\n");
        goto error;
    }
    state->dser_link = str[0] - 'a';

    err = of_property_read_u32(node, "st-vc", &value);
    if (err < 0) {
        dev_err(dev, "No st-vc info\n");
        goto error;
    }
    state->src_vc = value;

    err = of_property_read_u32(node, "vc-id", &value);
    if (err < 0) {
        dev_err(dev, "No vc-id info\n");
        goto error;
    }
    state->dst_vc = value;

    mode = of_get_child_by_name(node, "mode0");
    if (mode == NULL) {
        dev_err(dev, "missing mode0 device node\n");
        err = -EINVAL;
    }

    /* embedded_metadata_height is optional */
    err = of_property_read_string(mode, "embedded_metadata_height", &str);
    if (err)
        state->embedded_metadata_height = 0;
    else
        state->embedded_metadata_height = str[0] - '0';

    state->pps_gpios = of_get_named_gpio(node, "pps-gpios", 0);
    if (state->pps_gpios < 0) {
        dev_err(dev, "pps-gpios not found %d\n", err);
    } else {
        if (!pps_gpios_request){
            err = gpio_request(state->pps_gpios, "pps_gpios");
            if (err < 0) {
                dev_err(dev, "Failed to request GPIO %d\n", err);
            }
            pps_gpios_request = 1;
        }

        err = gpio_direction_output(state->pps_gpios, 0);
        if (err < 0) {
            dev_err(dev, "Failed to set GPIO direction %d\n", err);
            gpio_free(state->pps_gpios);
            pps_gpios_request = 0;
            goto error;
        }
    }

    err = of_property_read_string(node, "cam-type",	&str);

    if (err || !strncmp(str, "Depth", strlen("Depth"))){
        state->cam_type = ORB_CAM_DEPTH;
        state->device_info.video_type = ORB_MUX_PAD_DEPTH;
    }
    else if (!strncmp(str, "RGB", strlen("RGB"))) {
        state->cam_type = ORB_CAM_COLOR;
        state->device_info.video_type = ORB_MUX_PAD_RGB;
    }
    else if (!strncmp(str, "IR_L", strlen("IR_L"))) {
        state->cam_type = ORB_CAM_IR_L;
        state->device_info.video_type = ORB_MUX_PAD_IR_L_T;
    }
    else if (!strncmp(str, "IR_R", strlen("IR_R"))) {
        state->cam_type = ORB_CAM_IR_R;
        state->device_info.video_type = ORB_MUX_PAD_IR_R_T;
    }

    state->device_info.vid = 0x2bc5;
    err = of_property_read_u32(node, "orbbec_cam_num", &state->device_info.cam_num);
    if (err < 0) {
        dev_err(dev, "orbbec_cam_num not found\n");
        goto error;
    }

    dev_info(dev, "g300 devicetree parse success\n");

error:
    return err;
}

static int orb_probe(struct i2c_client *c, const struct i2c_device_id *id)
{
    struct orb *state = devm_kzalloc(&c->dev, sizeof(*state), GFP_KERNEL);
    int ret = 0;
    int size = 0;
    int i = 0;

    if (!state)
        return -ENOMEM;
    mutex_init(&state->lock);

    state->client = c;
    dev_info(&c->dev, "Probing new driver for orbbec camera\n");

    state->vcc = devm_regulator_get(&c->dev, "vcc");
    if (IS_ERR(state->vcc)) {
        ret = PTR_ERR(state->vcc);
        dev_warn(&c->dev, "failed %d to get vcc regulator\n", ret);
        return ret;
    }

    if (state->vcc) {
        ret = regulator_enable(state->vcc); 
        if (ret < 0) {
            dev_warn(&c->dev, "failed %d to enable the vcc regulator\n", ret);
            return ret;
        }
    }

    ret = orb_prase_dt(state);
    if (ret) {
        dev_err(&c->dev, "g300 device-tree parse failed\n");
        goto e_regulator;
    }

    if(orb_is_link(state) == 0) {
        dev_err(&c->dev, "camera num %d no found serialer dev\n", state->device_info.cam_num);
        goto e_regulator;
    }

    /* Each device is called only once */
    if(probe_cam_num != state->device_info.cam_num) {

        probe_cam_num = state->device_info.cam_num;

        ret = orb_gmsl_serdes_setup(state);
        if (ret) {
            dev_err(&c->dev, "orbbec gmsl serdes setup failed, %d\n", ret);
            goto e_regulator;
        }

        /* reset Orbbec cammera */
    #if 0
        // ret = max9295d_set_orbbec_off(state);
        msleep(3000);
        ret = max9295d_set_orbbec_on(state);
        if (ret) {
            dev_err(&c->dev, "%s, failed to max9295d set_orbbec_on\n", __func__);
            goto e_regulator;
        }
        msleep (3000); //delay 3s after reboot
    #endif
        for (i = 0; i < 10; i++) {
            ret = orbbec_get_frame_profile_len(state, &size);
            if(ret == 0 && size > 0) {
                dev_info(&c->dev, "communicate with G300\n");
                break;
            }
            msleep(100);
        }
        if (ret < 0 || size <= 0) {
            dev_err(&c->dev, "%s(): cannot communicate with G300: %d, %d\n", __func__, ret, size);
            goto e_regulator;
        }

        dev_info(&c->dev, "orbbec gmsl serdes setup success\n");
    }

    /* All video streams will be called  */
    orbbec_get_deviceinfo(state, &state->device_info.pid, &state->device_info.sn, &state->device_info.asic_sn);

    ret = orbbec_get_frame_profile(state);
    if(ret < 0) {
        dev_err(&c->dev, "%s(): orbbec_get_frame_profile err: %d\n" , __func__, ret);
        goto e_regulator;
    }

    ret = orb_v4l_init(c, state);
    if (ret < 0) {
        dev_err(&c->dev, "orb_v4l_init failed, %d\n", ret);
        goto e_regulator;
    }

    /* wait video pipeline bound finish */
    while(state->sensor.sd.subdev.devnode == NULL)
        msleep (50);
    state->device_info.sub_num = state->sensor.sd.subdev.devnode->num;
    if(state->cam_type == ORB_CAM_DEPTH)
        dev_info(&c->dev, "success probe orbbec camera num %d\n", state->device_info.cam_num);
    return 0;

e_regulator:
    if (state->vcc)
        regulator_disable(state->vcc);
    return ret;
}

static int orb_remove(struct i2c_client *c)
{	
    struct orb *state = NULL;
    if (c != NULL) {
        struct v4l2_subdev *sd = i2c_get_clientdata(c);
        state = container_of(sd, struct orb, sensor.sd.subdev);
        //state= v4l2_get_subdevdata(sd);
        if(state != NULL && sd != NULL){
            int ret;
            if (state->cam_type == ORB_CAM_DEPTH) {
                mutex_lock(&serdes_lock__);

                // ret = max9295d_reset_control(state->dser_dev);
                // if (ret)
                // 	dev_warn(&c->dev,"failed in 9295 reset control\n");

                ret = state->deser_ops->reset_dev(state->dser_dev);
                if (ret)
                    dev_warn(&c->dev,  "failed in deser chip reset control\n");
                
                state->deser_ops->power_off(state->dser_dev);

                mutex_unlock(&serdes_lock__);
            }
            
            dev_info(&c->dev, "G300 remove %s\n", orb_get_sensor_name(state));
            if (state->vcc)
                regulator_disable(state->vcc);
            if (pps_gpios_request) {
                gpio_free(state->pps_gpios);
                pps_gpios_request = 0;
            }
            mutex_destroy(&state->lock);
            orb_mux_remove(state);
        }
    
    }
    
    return 0;
}

static const struct i2c_device_id orb_id[] = {
    { ORB_DRIVER_NAME, ORB_ORBU },
    { },
};
MODULE_DEVICE_TABLE(i2c, orb_id);

static const struct of_device_id g300_of_match[] = {
    { .compatible = "orbbec,g300", },
    { },
};
MODULE_DEVICE_TABLE(of, g300_of_match);

static struct i2c_driver orb_i2c_driver = {
    .driver = {
        .owner = THIS_MODULE,
        .name = ORB_DRIVER_NAME,
        .of_match_table = of_match_ptr(g300_of_match),
    },
    .probe		= orb_probe,
    .remove		= orb_remove,
    .id_table	= orb_id,
};

module_i2c_driver(orb_i2c_driver);

MODULE_DESCRIPTION("Orbbec G300 Camera Driver");
MODULE_AUTHOR("chenwendong <xuanyuan@orbbec.com>");
MODULE_AUTHOR("yezhenhao <yanxiao@orbbec.com>");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.2.03(JP6.2)");