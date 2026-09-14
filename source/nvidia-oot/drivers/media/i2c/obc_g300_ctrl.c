/*
 * obc_g300_ctrl.c - Orbbec G300 camera driver
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
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/gpio.h>

#include <media/v4l2-ctrls.h>

#include <media/gmsl-link.h>
#include <media/obc_max9296.h>

#include <media/obc_g300_priv.h>

//-------------------------------------------------------------------------------------------
#define ORB_N_CONTROLS			8

//-------------------------------------------------------------------------------------------
#define ORB_CAMERA_CID          0x4000
#define ORB_CAMERA_CID_BASE	    (V4L2_CTRL_CLASS_CAMERA | ORB_CAMERA_CID)

#define ORBBEC_CAMERA_CID_SET_DATA				(ORB_CAMERA_CID_BASE+33)
#define ORBBEC_CAMERA_CID_GET_VERSION_DATA		(ORB_CAMERA_CID_BASE+34)
//#define ORBBEC_CAMERA_CID_GET_IMU_STOCK			(ORB_CAMERA_CID_BASE+35)
#define ORBBEC_CAMERA_CID_GET_IMU_DATA			(ORB_CAMERA_CID_BASE+36)
#define ORBBEC_CAMERA_CID_GET_IMU_FPS			(ORB_CAMERA_CID_BASE+37)
#define ORBBEC_CAMERA_CID_SET_DATA_LEN			(ORB_CAMERA_CID_BASE+38)
#define ORBBEC_CAMERA_CID_GET_DATA          	(ORB_CAMERA_CID_BASE+39)
#define ORBBEC_CAMERA_CID_PPS_TRIGGER		    (ORB_CAMERA_CID_BASE+40)
#define ORBBEC_CAMERA_CID_RESET_DEVICE_LINK		(ORB_CAMERA_CID_BASE+41)
#define ORBBEC_CAMERA_CID_GET_PID_SN			(ORB_CAMERA_CID_BASE+42)
#define ORBBEC_CAMERA_CID_GET_LINK_STATE		(ORB_CAMERA_CID_BASE+43)
#define ORBBEC_CAMERA_CID_RESET_DEVICE		    (ORB_CAMERA_CID_BASE+44)

#define G2R_IMU_STOCK_MAX_SIZE   				9

//----------------------------------------------------------
#define G2R_I2C_RESPONSE_HEARD_LEN				8
#define G2R_GET_FIRMWARE_DATA_CMD_LEN			8
#define G2R_GET_IMU_STOCK_CMD_LEN				10
#define G2R_GET_IMU_DATA_CMD_LEN				18
#define G2R_GET_FRAME_PROFILE_LEN_CMD_LEN		10
#define G2R_GET_FRAME_PROFILE_DATA_CMD_LEN		18
#define G2R_STREAM_START_CMD_LEN				10
#define G2R_STREAM_STOP_CMD_LEN					8
#define G2R_SET_PROPERTY_CMD_LEN				14
#define G2R_GET_PROPERTY_CMD_LEN				10
#define G2R_SET_IMU_CMD_LEN						14
#define G2R_GET_PU_CMD_LEN      		8
#define G2R_GET_PU_INIT_LEN     		30 //8+sizeof(ctpu_data)

//I2C command to read data length
#define G2R_GET_DATA_NOMAL_LEN	        10 
#define G2R_GET_VERSION_DATA_LEN	    172
#define G2R_RW_DATA_LEN	                256
#define G2R_GET_IMU_DATA_LEN	        24
#define G2R_GET_IMU_STOCK_LEN	        14 
#define G2R_GET_PROPERTY_DATA_LEN	    28
#define G2R_GET_FRAME_PROFILE_LEN_LEN	14 
#define G2R_GET_SN_DATA_LEN				24

//----------------------------------------------------------
//I2C command operation code
#define G2R_SET_PROPERTY_CODE		    2
#define G2R_GET_PROPERTY_CODE		    1
#define G2R_GET_FIRMWARE_DATA_CODE      3
#define G2R_GET_IMU_DATA_CODE	        30
#define G2R_GET_IMU_STOCK_CODE          29 
#define G2R_GET_FRAME_PROFILE_LEN_CORE  29
#define G2R_GET_FRAME_PROFILE_DATA_CORE 30
#define G2R_GET_CTPU_INIT_CODE          200
#define G2R_GET_CTPU_CODE               201
#define G2R_SET_CTPU_CODE               202

#define G2R_GET_PID_PRO_ID  			111
#define G2R_GET_SN_PRO_ID  				1035
#define G2R_GET_ASIC_SN_PRO_ID  		1063

//----------------------------------------------------------
#define CTPU_PROID_LEN					2
#define IR_CONTROL_SELECT               1
#define RGB_CONTROL_SELECT              0
#define CT_CONTROL_SELECT               0x00
#define PU_CONTROL_SELECT               0x80

//----------------------------------------------------------
#define CT_AE_MANUL_MODE				(1 << 0)
#define CT_AE_AUTO_MODE					(1 << 1)
#define CT_AE_SHUTTER_MODE				(1 << 2)
#define CT_AE_APERTURE_MODE				(1 << 3)

//----------------------------------------------------------
/* A.9.4. Camera Terminal Control Selectors */
#define CT_CONTROL_UNDEFINED                       0x00
#define CT_SCANNING_MODE_CONTROL                   0x01
#define CT_AE_MODE_CONTROL                         0x02
#define CT_AE_PRIORITY_CONTROL                     0x03
#define CT_EXPOSURE_TIME_ABSOLUTE_CONTROL          0x04
#define CT_EXPOSURE_TIME_RELATIVE_CONTROL          0x05
#define CT_FOCUS_ABSOLUTE_CONTROL                  0x06
#define CT_FOCUS_RELATIVE_CONTROL                  0x07
#define CT_FOCUS_AUTO_CONTROL                      0x08
#define CT_IRIS_ABSOLUTE_CONTROL                   0x09
#define CT_IRIS_RELATIVE_CONTROL                   0x0A
#define CT_ZOOM_ABSOLUTE_CONTROL                   0x0B
#define CT_ZOOM_RELATIVE_CONTROL                   0x0C
#define CT_PANTILT_ABSOLUTE_CONTROL                0x0D
#define CT_PANTILT_RELATIVE_CONTROL                0x0E
#define CT_ROLL_ABSOLUTE_CONTROL                   0x0F
#define CT_ROLL_RELATIVE_CONTROL                   0x10
#define CT_PRIVACY_CONTROL                         0x11
#define CT_FOCUS_SIMPLE_CONTROL                    0x12
#define CT_WINDOW_CONTROL                          0x13
#define CT_REGION_OF_INTEREST_CONTROL              0x14
#define CT_CONTROL_NUM              			   0x15

//----------------------------------------------------------
/* Processing Unit Control Selectors */
#define PU_CONTROL_UNDEFINED                       0x00
#define PU_BACKLIGHT_COMPENSATION_CONTROL          0x01
#define PU_BRIGHTNESS_CONTROL                      0x02
#define PU_CONTRAST_CONTROL                        0x03
#define PU_GAIN_CONTROL                            0x04
#define PU_POWER_LINE_FREQUENCY_CONTROL            0x05
#define PU_HUE_CONTROL                             0x06
#define PU_SATURATION_CONTROL                      0x07
#define PU_SHARPNESS_CONTROL                       0x08
#define PU_GAMMA_CONTROL                           0x09
#define PU_WHITE_BALANCE_TEMPERATURE_CONTROL       0x0A
#define PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL  0x0B
#define PU_WHITE_BALANCE_COMPONENT_CONTROL         0x0C
#define PU_WHITE_BALANCE_COMPONENT_AUTO_CONTROL    0x0D
#define PU_DIGITAL_MULTIPLIER_CONTROL              0x0E
#define PU_DIGITAL_MULTIPLIER_LIMIT_CONTROL        0x0F
#define PU_HUE_AUTO_CONTROL                        0x10
#define PU_ANALOG_VIDEO_STANDARD_CONTROL           0x11
#define PU_ANALOG_LOCK_STATUS_CONTROL              0x12
#define PU_CONTROL_NUM              			   0x13

//----------------------------------------------------------
#define OPCODE_OPEN_STREAM      100
#define OPCODE_CLOSE_STREAM     101

//-------------------------------------------------------------------------------------------
#define I2C_RETRY_TIME		10

//-------------------------------------------------------------------------------------------
struct ctpu_data {
    uint8_t len;//设置数据的长度一般是1/2/4
    uint8_t info;
    int32_t cur;
    int32_t max;
    int32_t min;
    int32_t def;
    int32_t step;
}__attribute__((__packed__));

struct xu_data {
    int32_t cur_xu;
    int32_t max_xu;
    int32_t min_xu;
    int32_t def_xu;
    int32_t step_xu;
};

//-------------------------------------------------------------------------------------------
uint8_t ct_ctl_len[CT_CONTROL_NUM] = {0};
uint8_t pu_ctl_len[PU_CONTROL_NUM] = {0};

static const u16 orb_imu_framerates[] = {0, 1, 3, 6, 12, 25, 50, 100, 200, 500, 1000, 2000, 
    4000, 8000, 16000, 32000, 400, 800};

static int get_data_len = 10;

int orb_cmd_index = 1;

//-------------------------------------------------------------------------------------------


//-------------------------------------------------------------------------------------------
/*通过i2c 向orbbec写入数据,直接写数据不写寄存器地址
*orbbec_client：orbbec的i2c_client结构体。
*data：要写入的数据
*length：写长度
*返回值，错误，-1。成功，0  
*/
static int sensor_write_data(struct orb *state, void *data, u16 length)
{
    return state->deser_ops->write_sensor(state->dser_dev, state->dser_link, data, length);
}

/*通过i2c 向orbbec读入数据，直接读数据不写寄存器地址
*orbbec_client：orbbec的i2c_client结构体。
*data，保存读取得到的数据
*length，读长度
*返回值，错误，-1。成功，0
*/
static int sensor_read_data(struct orb *state, void *data, u16 length)
{
    return state->deser_ops->read_sensor(state->dser_dev, state->dser_link, data, length);
}

int sensor_stream_opt(struct orb *state, struct orbbec_set_stream_cmd *strcmd, int on)
{
    int ret;
    struct orbbec_cmd *cmd = kzalloc(sizeof(struct orbbec_cmd), GFP_KERNEL);
    if (!cmd) {
        dev_err(&state->client->dev, "%s(): Can't allocate memory \n", __func__);
        ret = -ENOMEM;
        return ret;
    }
    if(on) {
        cmd->header.len = G2R_STREAM_START_CMD_LEN;
        cmd->header.code = OPCODE_OPEN_STREAM;
    } else {
        cmd->header.len = G2R_STREAM_STOP_CMD_LEN;
        cmd->header.code = OPCODE_CLOSE_STREAM;
    }
    cmd->header.index = orb_cmd_index++;
    cmd->set_stream_cmd = *strcmd;

    ret = sensor_write_data(state, cmd, cmd->header.len);
    if(ret < 0) {
        printk(KERN_ERR "\n i2c write open stream cmd err \n");
    }

    kfree(cmd);

    return ret;
}

/*通过i2c 向orbbec读取设备信息
*state：orbbec的i2c_client结构体。
*data: 保存读取得到的数据
*cmd_len: 命令长度
*opcode: 操作符
*pro_id： pro_id
*data_len： 获取的数据长度
*返回值：错误，-1。成功，0
*/
static int orbbec_get_dev_info(struct orb *state, void * data, uint16_t cmd_len, uint16_t opcode, uint16_t pro_id, uint32_t data_len)
{
    int ret = 0, i = 0;
    struct orbbec_cmd *get_info_cmd;
    get_info_cmd = devm_kzalloc(&state->client->dev, sizeof(struct orbbec_cmd), GFP_KERNEL);
    if (!get_info_cmd) {
        dev_err(&state->client->dev, "%s(): Can't allocate memory \n", __func__);
        ret = -ENOMEM;
        return ret;
    }
    get_info_cmd->header.len = cmd_len;
    get_info_cmd->header.code = opcode;
    get_info_cmd->header.index = orb_cmd_index++;
    get_info_cmd->_data[0] = pro_id;
    get_info_cmd->_data[1] = pro_id >>8;

    ret = sensor_write_data(state, get_info_cmd, cmd_len);
    if(ret < 0) {
        printk(KERN_DEBUG "\n i2c write cmd err \n");
        goto err_11;
    }
    udelay(10);
retry:
    usleep_range(100,120);
    ret = sensor_read_data(state, get_info_cmd, data_len);
    if(get_info_cmd->header.len == 0 && get_info_cmd->header.code == 0 && get_info_cmd->header.index == 0 && i < I2C_RETRY_TIME){
        i++;
        dev_err(&state->client->dev, "get_dev_info data err retry ...\n");
        goto retry;
    }
    if(get_info_cmd->header.code != opcode){
        dev_err(&state->client->dev, "get_dev_info data err\n");
        goto err_11;
    }
    memcpy(data, get_info_cmd->msg_body1.data, data_len-G2R_I2C_RESPONSE_HEARD_LEN);
err_11:
    devm_kfree(&state->client->dev, get_info_cmd);
    return ret;
}

int orbbec_get_deviceinfo(struct orb *state, void *pid, void *sn, void *asic_sn)
{
    int ret = 0;
    ret |= orbbec_get_dev_info(state, pid, G2R_GET_PROPERTY_CMD_LEN, G2R_GET_PROPERTY_CODE, G2R_GET_PID_PRO_ID,G2R_GET_DATA_NOMAL_LEN);
    ret |= orbbec_get_dev_info(state, sn, G2R_GET_FIRMWARE_DATA_CMD_LEN, G2R_GET_FIRMWARE_DATA_CODE, G2R_GET_SN_PRO_ID,G2R_GET_SN_DATA_LEN);
    ret |= orbbec_get_dev_info(state, asic_sn, G2R_GET_FIRMWARE_DATA_CMD_LEN, G2R_GET_FIRMWARE_DATA_CODE, G2R_GET_ASIC_SN_PRO_ID,G2R_GET_SN_DATA_LEN);
    return ret;
}

/*通过i2c 向orbbec读取版本信息
*state：orbbec的i2c_client结构体。
*data，保存读取得到的数据
*返回值，错误，-1。成功，0
*/
static int orbbec_get_version(struct orb *state, void * data)
{
    int ret = 0;
    struct orbbec_cmd *get_version_cmd;
    get_version_cmd = devm_kzalloc(&state->client->dev, sizeof(struct orbbec_cmd), GFP_KERNEL);
    if (!get_version_cmd) {
        dev_err(&state->client->dev, "%s(): Can't allocate memory \n", __func__);
        ret = -ENOMEM;
        return ret;
    }
    get_version_cmd->header.len = G2R_GET_FIRMWARE_DATA_CMD_LEN;
    get_version_cmd->header.code = G2R_GET_FIRMWARE_DATA_CODE;
    get_version_cmd->header.index = orb_cmd_index++;
    get_version_cmd->_data[0] = 0xe8;
    get_version_cmd->_data[1] = 0x03;

    ret = sensor_write_data(state, get_version_cmd, G2R_GET_FIRMWARE_DATA_CMD_LEN);
    if(ret < 0) {
        printk(KERN_DEBUG "\n i2c write cmd err \n");
        goto err_10;
    }
    udelay(10);
    usleep_range(100,120);
    ret = sensor_read_data(state, get_version_cmd, G2R_GET_VERSION_DATA_LEN);
    memcpy(data, get_version_cmd, G2R_GET_VERSION_DATA_LEN);
err_10:
    devm_kfree(&state->client->dev, get_version_cmd);
    return ret;
}

static int orbbec_get_imu_stock(struct orb *state, int *data)
{
    int ret = 0, i = 0;
    u16 cmd_index;
    struct orbbec_cmd *get_imu_stock_cmd;
    get_imu_stock_cmd = devm_kzalloc(&state->client->dev, sizeof(struct orbbec_cmd), GFP_KERNEL);
    if (!get_imu_stock_cmd) {
        dev_err(&state->client->dev, "%s(): Can't allocate memory \n", __func__);
        ret = -ENOMEM;
        return ret;
    }
    get_imu_stock_cmd->header.len = G2R_GET_IMU_STOCK_CMD_LEN;
    get_imu_stock_cmd->header.code = G2R_GET_IMU_STOCK_CODE;
    get_imu_stock_cmd->header.index = orb_cmd_index++;
    get_imu_stock_cmd->_data[0] = 0xcd;
    get_imu_stock_cmd->_data[1] = 0x0f;

    cmd_index = get_imu_stock_cmd->header.index;
    ret = sensor_write_data(state, get_imu_stock_cmd, G2R_GET_IMU_STOCK_CMD_LEN);
    if(ret < 0) {
        printk(KERN_DEBUG "\n i2c write get_imu_stock cmd err \n");
        goto err_5;
    }
    //udelay(10);
    for(i = 0; i <= I2C_RETRY_TIME; i++) {
        usleep_range(100,120);
        ret = sensor_read_data(state, get_imu_stock_cmd, G2R_GET_IMU_STOCK_LEN);
        if(ret < 0) {
            printk(KERN_DEBUG "\n i2c read  get_imu_stock data err \n");
            goto err_5;
        }
        
        if(get_imu_stock_cmd->header.len != G2R_GET_IMU_STOCK_LEN || get_imu_stock_cmd->header.code != G2R_GET_IMU_STOCK_CODE || get_imu_stock_cmd->header.index != cmd_index) {
            dev_dbg(&state->client->dev, "retry get_imustock\n");
            continue;
        }
        else break;
    }
    memcpy(data, get_imu_stock_cmd->msg_body.data, 4);
    *data = *data/G2R_GET_IMU_DATA_LEN;
err_5:
    devm_kfree(&state->client->dev, get_imu_stock_cmd);
    return ret;
}

static int orbbec_get_imu_data(struct orb *state, void * data)
{
    int ret = 0, stock_size = 0, get_imu_date_len = 0, i = 0;
    u16 cmd_index;
    struct orbbec_cmd *get_imudata_cmd;
    get_imudata_cmd = devm_kzalloc(&state->client->dev, sizeof(struct orbbec_cmd), GFP_KERNEL);
    if (!get_imudata_cmd) {
        dev_err(&state->client->dev, "%s(): Can't allocate memory \n", __func__);
        ret = -ENOMEM;
        return ret;
    }

    ret = orbbec_get_imu_stock(state, &stock_size);
    if(ret < 0) {
        printk(KERN_DEBUG "\n orbbec_get_imu_stock err \n");
        goto err_9;
    }

    get_imu_date_len = G2R_GET_IMU_DATA_LEN*stock_size;
    get_imudata_cmd->header.len = G2R_GET_IMU_DATA_CMD_LEN;
    get_imudata_cmd->header.code = G2R_GET_IMU_DATA_CODE;
    get_imudata_cmd->header.index = orb_cmd_index++;
    get_imudata_cmd->_data[0] = 0xcd;
    get_imudata_cmd->_data[1] = 0x0f;
    get_imudata_cmd->_data[2] = 0x0;
    get_imudata_cmd->_data[3] = 0x0;
    get_imudata_cmd->_data[4] = 0x0;
    get_imudata_cmd->_data[5] = 0x0;
    get_imudata_cmd->_data[6] = 0x0;
    get_imudata_cmd->_data[7] = 0x0;
    get_imudata_cmd->_data[8] = get_imu_date_len;
    get_imudata_cmd->_data[9] = get_imu_date_len >> 8;
    get_imudata_cmd->_data[10] = get_imu_date_len >> 16;
    get_imudata_cmd->_data[11] = get_imu_date_len >> 24;

    cmd_index = get_imudata_cmd->header.index;

    if(stock_size == 0 || stock_size < 0 || stock_size > G2R_IMU_STOCK_MAX_SIZE) {
        get_imudata_cmd->header.len = G2R_I2C_RESPONSE_HEARD_LEN;
        get_imudata_cmd->_data[0] = 0;
        get_imudata_cmd->_data[1] = 0;
        get_imu_date_len = 0;
        dev_dbg(&state->client->dev, " stock_size = %d \n",stock_size);
        printk(KERN_DEBUG "\n orbbec stock size err\n");
    } else {
        ret = sensor_write_data(state, get_imudata_cmd, G2R_GET_IMU_DATA_CMD_LEN);
        if(ret < 0) {
            printk(KERN_DEBUG "\n i2c write get_imudata_cmd cmd err \n");
            goto err_9;
        }
        //udelay(10);
        for(i = 0; i <= I2C_RETRY_TIME; i++) {
            usleep_range(100,120);
            ret = sensor_read_data(state, get_imudata_cmd, G2R_I2C_RESPONSE_HEARD_LEN + get_imu_date_len);
            if(ret < 0) {
                printk(KERN_DEBUG "\n i2c write get_imudata_cmd data err \n");
                goto err_9;
            }

            if(get_imudata_cmd->header.len == 0 && get_imudata_cmd->header.code == 0 && get_imudata_cmd->header.index == 0) {
                dev_dbg(&state->client->dev, "retry get_imudata\n");
                continue;
            }
            else if(get_imudata_cmd->header.len != G2R_I2C_RESPONSE_HEARD_LEN + get_imu_date_len || get_imudata_cmd->header.code != G2R_GET_IMU_DATA_CODE || get_imudata_cmd->header.index != cmd_index) {
                get_imudata_cmd->header.len = G2R_I2C_RESPONSE_HEARD_LEN;
                get_imudata_cmd->_data[0] = 0;
                get_imudata_cmd->_data[1] = 0;
                get_imu_date_len = 0;
                dev_dbg(&state->client->dev, " get_imudata data err\n");
                break;
            } else {
                break;
            }
        }
    }
    
    memcpy(data, get_imudata_cmd, G2R_I2C_RESPONSE_HEARD_LEN + get_imu_date_len);
err_9:
    devm_kfree(&state->client->dev, get_imudata_cmd);
    return ret;
}

int orbbec_get_frame_profile_len(struct orb *state, void * data)
{
    int ret = 0;
    struct orbbec_cmd *get_frame_profile_len_cmd = kzalloc(sizeof(struct orbbec_cmd), GFP_KERNEL);
    if (!get_frame_profile_len_cmd) {
        dev_err(&state->client->dev, "%s(): Can't allocate memory\n", __func__);
        ret = -ENOMEM;
        return ret;
    }
    get_frame_profile_len_cmd->header.len = G2R_GET_FRAME_PROFILE_LEN_CMD_LEN;
    get_frame_profile_len_cmd->header.code = G2R_GET_FRAME_PROFILE_LEN_CORE;
    get_frame_profile_len_cmd->header.index = orb_cmd_index++;
    get_frame_profile_len_cmd->_data[0] = 0xc3;
    get_frame_profile_len_cmd->_data[1] = 0x0f;
    get_frame_profile_len_cmd->_data[2] = 0x00;
    get_frame_profile_len_cmd->_data[3] = 0x00;

    ret = sensor_write_data(state, get_frame_profile_len_cmd, G2R_GET_FRAME_PROFILE_LEN_CMD_LEN);
    if(ret < 0) {
        printk(KERN_DEBUG "\n i2c write cmd err \n");
        goto err_8;
    }
    //udelay(10);
    usleep_range(500, 800);
    ret = sensor_read_data(state, get_frame_profile_len_cmd, G2R_GET_FRAME_PROFILE_LEN_LEN);
    if(get_frame_profile_len_cmd->header.code != G2R_GET_FRAME_PROFILE_LEN_CORE) {
        ret = -2;
        goto err_8;
    }
    memcpy(data, get_frame_profile_len_cmd->msg_body.data, 4);

err_8:
    kfree(get_frame_profile_len_cmd);
    return ret;
}

int orbbec_get_frame_profile_data(struct orb *state, void * data, int offset, int length)
{
    int ret = 0;
    struct orbbec_cmd *get_frame_profile_data_cmd;
    get_frame_profile_data_cmd = kzalloc(sizeof(struct orbbec_cmd), GFP_KERNEL);
    if (!get_frame_profile_data_cmd) {
        dev_err(&state->client->dev, "%s(): Can't allocate memory\n", __func__);
        ret = -ENOMEM;
        return ret;
    }
    get_frame_profile_data_cmd->header.len = G2R_GET_FRAME_PROFILE_DATA_CMD_LEN;
    get_frame_profile_data_cmd->header.code = G2R_GET_FRAME_PROFILE_DATA_CORE;
    get_frame_profile_data_cmd->header.index = orb_cmd_index++;
    get_frame_profile_data_cmd->_data[0] = 0xc3;
    get_frame_profile_data_cmd->_data[1] = 0x0f;
    get_frame_profile_data_cmd->_data[2] = 0x00;
    get_frame_profile_data_cmd->_data[3] = 0x00;
    get_frame_profile_data_cmd->_data[4] = offset;
    get_frame_profile_data_cmd->_data[5] = offset >> 8;
    get_frame_profile_data_cmd->_data[6] = offset >> 16;
    get_frame_profile_data_cmd->_data[7] = offset >> 24;
    get_frame_profile_data_cmd->_data[8] = length;
    get_frame_profile_data_cmd->_data[9] = length >> 8;
    get_frame_profile_data_cmd->_data[10] = length >> 16;
    get_frame_profile_data_cmd->_data[11] = length >> 24;

    ret = sensor_write_data(state, get_frame_profile_data_cmd, G2R_GET_FRAME_PROFILE_DATA_CMD_LEN);
    if(ret < 0) {
        printk(KERN_DEBUG "\n i2c write cmd err \n");
        goto err_7;
    }
    //udelay(10);
    usleep_range(100, 120);
    ret = sensor_read_data(state, get_frame_profile_data_cmd, G2R_GET_DATA_NOMAL_LEN+length);
    if(get_frame_profile_data_cmd->header.code != G2R_GET_FRAME_PROFILE_DATA_CORE) {
        ret = -2;
        goto err_7;
    }
    memcpy(data,get_frame_profile_data_cmd->msg_body1.data, length);
err_7:
    kfree(get_frame_profile_data_cmd);
    return ret;
}

static int orbbec_set_exposure(struct orb *state, s32 data)
{
    int ret;
    int prop_id;
    struct orbbec_cmd *set_exposure_cmd;
    set_exposure_cmd = devm_kzalloc(&state->client->dev, sizeof(struct orbbec_cmd), GFP_KERNEL);
    if (!set_exposure_cmd) {
        dev_err(&state->client->dev, "%s(): Can't allocate memory \n", __func__);
        ret = -ENOMEM;
        return ret;
    }
    switch(state->cam_type) {
        case ORB_CAM_DEPTH: prop_id = 2017; break;
        case ORB_CAM_COLOR: prop_id = 2001; break;
        case ORB_CAM_IR_L:
        case ORB_CAM_IR_R:  prop_id = 2026; break;
        default: prop_id = 2017; break;
    }
    set_exposure_cmd->header.len = G2R_SET_PROPERTY_CMD_LEN;
    set_exposure_cmd->header.code = G2R_SET_PROPERTY_CODE;
    set_exposure_cmd->header.index = orb_cmd_index++;
    set_exposure_cmd->_data[0] = prop_id;
    set_exposure_cmd->_data[1] = prop_id >>8;
    set_exposure_cmd->_data[2] = prop_id >>16;
    set_exposure_cmd->_data[3] = prop_id >>24;
    set_exposure_cmd->_data[4] = data;
    set_exposure_cmd->_data[5] = data >>8;
    set_exposure_cmd->_data[6] = data >>16;
    set_exposure_cmd->_data[7] = data >>24;
    
    ret = sensor_write_data(state, set_exposure_cmd, G2R_SET_PROPERTY_CMD_LEN);
    if(ret < 0) {
        printk(KERN_DEBUG "\n i2c write cmd err \n");
        goto err_2;
    }
err_2:
    devm_kfree(&state->client->dev, set_exposure_cmd);
    return ret;
}

static int orbbec_get_exposure(struct orb *state, void * data)
{
    int ret;
    int prop_id;
    struct orbbec_cmd *get_exposure_cmd;
    get_exposure_cmd = devm_kzalloc(&state->client->dev, sizeof(struct orbbec_cmd), GFP_KERNEL);
    if (!get_exposure_cmd) {
        dev_err(&state->client->dev, "%s(): Can't allocate memory \n", __func__);
        ret = -ENOMEM;
        return ret;
    }
    switch(state->cam_type) {
        case ORB_CAM_DEPTH: prop_id = 2017; break;
        case ORB_CAM_COLOR: prop_id = 2001; break;
        case ORB_CAM_IR_L:
        case ORB_CAM_IR_R:  prop_id = 2026; break;
        default: prop_id = 2017; break;
    }
    get_exposure_cmd->header.len = G2R_GET_PROPERTY_CMD_LEN;
    get_exposure_cmd->header.code = G2R_GET_PROPERTY_CODE;
    get_exposure_cmd->header.index = orb_cmd_index++;
    get_exposure_cmd->_data[0] = prop_id;
    get_exposure_cmd->_data[1] = prop_id >>8;
    get_exposure_cmd->_data[2] = prop_id >>16;
    get_exposure_cmd->_data[3] = prop_id >>24;
    
    ret = sensor_write_data(state, get_exposure_cmd, G2R_GET_PROPERTY_CMD_LEN);
    if(ret < 0) {
        printk(KERN_DEBUG "\n i2c write cmd err \n");
        goto err_1;
    }
    usleep_range(100,120);
    ret = sensor_read_data(state, get_exposure_cmd, sizeof(struct orbbec_header)+6);
    memcpy(data, get_exposure_cmd->msg_body1.data, 4);
err_1:
    devm_kfree(&state->client->dev, get_exposure_cmd);
    return ret;
}

static int orbbec_get_exposure_init(struct orb *state, void * data)
{
    int ret;
    int prop_id;
    struct orbbec_cmd *get_exposure_cmd;
    get_exposure_cmd = devm_kzalloc(&state->client->dev, sizeof(struct orbbec_cmd), GFP_KERNEL);
    if (!get_exposure_cmd) {
        dev_err(&state->client->dev, "%s(): Can't allocate memory \n", __func__);
        ret = -ENOMEM;
        return ret;
    }
    switch(state->cam_type) {
        case ORB_CAM_DEPTH: prop_id = 2017; break;
        case ORB_CAM_COLOR: prop_id = 2001; break;
        case ORB_CAM_IR_L:
        case ORB_CAM_IR_R:  prop_id = 2026; break;
        default: prop_id = 2017; break;
    }
    get_exposure_cmd->header.len = G2R_GET_PROPERTY_CMD_LEN;
    get_exposure_cmd->header.code = G2R_GET_PROPERTY_CODE;
    get_exposure_cmd->header.index = orb_cmd_index++;
    get_exposure_cmd->_data[0] = prop_id;
    get_exposure_cmd->_data[1] = prop_id>>8;
    get_exposure_cmd->_data[2] = prop_id>>16;
    get_exposure_cmd->_data[3] = prop_id>>24;
    
    ret = sensor_write_data(state, get_exposure_cmd, G2R_GET_PROPERTY_CMD_LEN);
    if(ret < 0) {
        printk(KERN_DEBUG "\n i2c write cmd err \n");
        goto err_1;
    }
    usleep_range(100,120);
    ret = sensor_read_data(state, get_exposure_cmd, G2R_GET_PROPERTY_DATA_LEN);
    memcpy(data, get_exposure_cmd->msg_body1.data, 20);
err_1:
    devm_kfree(&state->client->dev, get_exposure_cmd);
    return ret;
}

static int orbbec_get_ctpu_init(struct orb *state, uint8_t ctpu_select, uint8_t irrgb_select, uint8_t ctpu_id, void * data)
{
    int ret;
    struct orbbec_cmd *get_pu_cmd;
    get_pu_cmd = devm_kzalloc(&state->client->dev, sizeof(struct orbbec_cmd), GFP_KERNEL);
    if (!get_pu_cmd) {
        dev_err(&state->client->dev, "%s(): Can't allocate memory \n", __func__);
        ret = -ENOMEM;
        return ret;
    }
    get_pu_cmd->header.len = G2R_GET_PU_CMD_LEN;
    get_pu_cmd->header.code = G2R_GET_CTPU_INIT_CODE;
    get_pu_cmd->header.index = orb_cmd_index++;
    get_pu_cmd->_data[0] = ctpu_select+ctpu_id;
    get_pu_cmd->_data[1] = irrgb_select;

    ret = sensor_write_data(state, get_pu_cmd, G2R_GET_PU_CMD_LEN);
    if(ret < 0) {
        printk(KERN_DEBUG "\n i2c write cmd err \n");
        return ret;
    }
    usleep_range(100,120);
    ret = sensor_read_data(state, get_pu_cmd, G2R_GET_PU_INIT_LEN);
    memcpy(data, get_pu_cmd->msg_body1.data, sizeof(struct ctpu_data));
    devm_kfree(&state->client->dev, get_pu_cmd);
    return ret;
}

static int orbbec_get_ctpu(struct orb *state, uint8_t ctpu_select, uint8_t irrgb_select, uint8_t ctpu_id, void * data, uint8_t len)
{
    int ret;
    struct orbbec_cmd *get_pu_cmd;
    get_pu_cmd = devm_kzalloc(&state->client->dev, sizeof(struct orbbec_cmd), GFP_KERNEL);
    if (!get_pu_cmd) {
        dev_err(&state->client->dev, "%s(): Can't allocate memory \n", __func__);
        ret = -ENOMEM;
        return ret;
    }
    get_pu_cmd->header.len = G2R_GET_PU_CMD_LEN;
    get_pu_cmd->header.code = G2R_GET_CTPU_CODE;
    get_pu_cmd->header.index = orb_cmd_index++;
    get_pu_cmd->_data[0] = ctpu_select+ctpu_id;
    get_pu_cmd->_data[1] = irrgb_select;

    ret = sensor_write_data(state, get_pu_cmd, G2R_GET_PU_CMD_LEN);
    if(ret < 0) {
        printk(KERN_DEBUG "\n i2c write cmd err \n");
        return ret;
    }
    usleep_range(100,120);
    ret = sensor_read_data(state, get_pu_cmd, G2R_I2C_RESPONSE_HEARD_LEN+len);
    memcpy(data, get_pu_cmd->msg_body1.data, len);
    devm_kfree(&state->client->dev, get_pu_cmd);
    return ret;
}

static int orbbec_set_autoexposure(struct orb *state, s32 data)
{
    int ret;
    struct orbbec_cmd *set_autoexposure_cmd;
    set_autoexposure_cmd = devm_kzalloc(&state->client->dev, sizeof(struct orbbec_cmd), GFP_KERNEL);
    if (!set_autoexposure_cmd) {
        dev_err(&state->client->dev, "%s(): Can't allocate memory \n", __func__);
        ret = -ENOMEM;
        return ret;
    }
    set_autoexposure_cmd->header.len = G2R_SET_PROPERTY_CMD_LEN;
    set_autoexposure_cmd->header.code = G2R_SET_PROPERTY_CODE;
    set_autoexposure_cmd->header.index = orb_cmd_index++;
    set_autoexposure_cmd->_data[0] = 0xE0;
    set_autoexposure_cmd->_data[1] = 0x07;
    set_autoexposure_cmd->_data[2] = 0x00;
    set_autoexposure_cmd->_data[3] = 0x00;
    if(data == V4L2_EXPOSURE_AUTO) {
        set_autoexposure_cmd->_data[4] = 1;
        set_autoexposure_cmd->_data[5] = 0;
    } else if(data == V4L2_EXPOSURE_MANUAL) {
        set_autoexposure_cmd->_data[4] = 0;
        set_autoexposure_cmd->_data[5] = 0;
    }

    ret = sensor_write_data(state, set_autoexposure_cmd, G2R_SET_PROPERTY_CMD_LEN);
    if(ret < 0) {
        printk(KERN_DEBUG "\n i2c write cmd err \n");
        goto err_3;
    }
err_3:
    devm_kfree(&state->client->dev, set_autoexposure_cmd);
    return ret;
}

static int orbbec_get_autoexposure(struct orb *state, void * data)
{
    int ret;
    struct orbbec_cmd *get_autoexposure_cmd;
    get_autoexposure_cmd = devm_kzalloc(&state->client->dev, sizeof(struct orbbec_cmd), GFP_KERNEL);
    if (!get_autoexposure_cmd) {
        dev_err(&state->client->dev, "%s(): Can't allocate memory \n", __func__);
        ret = -ENOMEM;
        return ret;
    }
    get_autoexposure_cmd->header.len = G2R_GET_PROPERTY_CMD_LEN;
    get_autoexposure_cmd->header.code = G2R_GET_PROPERTY_CODE;
    get_autoexposure_cmd->header.index = orb_cmd_index++;
    get_autoexposure_cmd->_data[0] = 0xE0;
    get_autoexposure_cmd->_data[1] = 0x07;
    get_autoexposure_cmd->_data[2] = 0x00;
    get_autoexposure_cmd->_data[3] = 0x00;


    ret = sensor_write_data(state, get_autoexposure_cmd, G2R_GET_PROPERTY_CMD_LEN);
    if(ret < 0) {
        printk(KERN_DEBUG "\n i2c write cmd err \n");
        return ret;
    }
    usleep_range(100,120);
    ret = sensor_read_data(state, get_autoexposure_cmd, G2R_GET_PROPERTY_DATA_LEN);
    memcpy(data, get_autoexposure_cmd->msg_body1.data, 2);
    devm_kfree(&state->client->dev, get_autoexposure_cmd);
    return ret;
}

static int orbbec_set_ctpu(struct orb *state,uint8_t ctpu_select, uint8_t irrgb_select, uint8_t ctpu_id, int value, uint8_t len)
{
    int ret;
    struct orbbec_cmd *set_pu_cmd;
    set_pu_cmd = devm_kzalloc(&state->client->dev, sizeof(struct orbbec_cmd), GFP_KERNEL);
    if (!set_pu_cmd) {
        dev_err(&state->client->dev, "%s(): Can't allocate memory \n", __func__);
        ret = -ENOMEM;
        return ret;
    }
    set_pu_cmd->header.len = sizeof(struct orbbec_header)+CTPU_PROID_LEN+len;
    set_pu_cmd->header.code = G2R_SET_CTPU_CODE;
    set_pu_cmd->header.index = orb_cmd_index++;
    set_pu_cmd->_data[0] = ctpu_select+ctpu_id;
    set_pu_cmd->_data[1] = irrgb_select;
    set_pu_cmd->_data[2] = value;
    set_pu_cmd->_data[3] = value >> 8;
    set_pu_cmd->_data[4] = value >> 16;
    set_pu_cmd->_data[5] = value >> 24;

    ret = sensor_write_data(state, set_pu_cmd, sizeof(struct orbbec_header)+CTPU_PROID_LEN+len);
    if(ret < 0) {
        printk(KERN_DEBUG "\n i2c write cmd err \n");
        return ret;
    }
    devm_kfree(&state->client->dev, set_pu_cmd);
    return ret;
}

static int orb_s_ctrl(struct v4l2_ctrl *ctrl)
{
    struct orb *state = container_of(ctrl->handler, struct orb, ctrls.handler);
    struct v4l2_subdev *sd = &state->sensor.sd.subdev;
    struct orbbec_cmd *get_cmd = NULL;
    int ret = 0;

    mutex_lock(&state->lock);

    get_cmd = devm_kzalloc(&state->client->dev, sizeof(struct orbbec_cmd), GFP_KERNEL);
    dev_dbg(&state->client->dev, "%s(): ctrl: %s \n", __func__, ctrl->name);

    v4l2_dbg(3, 1, sd, "ctrl: %s, value: %d\n", ctrl->name, ctrl->val);

    switch (ctrl->id) {
    case V4L2_CID_GAIN:
        if (state->cam_type != ORB_CAM_COLOR) {
            orbbec_set_ctpu(state, PU_CONTROL_SELECT, IR_CONTROL_SELECT, PU_GAIN_CONTROL, ctrl->val, pu_ctl_len[PU_GAIN_CONTROL]);
        } else {
            orbbec_set_ctpu(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_GAIN_CONTROL, ctrl->val, pu_ctl_len[PU_GAIN_CONTROL]);
        }
        break;
    case V4L2_CID_BACKLIGHT_COMPENSATION:
        orbbec_set_ctpu(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_BACKLIGHT_COMPENSATION_CONTROL, ctrl->val, pu_ctl_len[PU_GAIN_CONTROL]);
        break;

    case V4L2_CID_BRIGHTNESS:
        orbbec_set_ctpu(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_BRIGHTNESS_CONTROL, ctrl->val, pu_ctl_len[PU_BRIGHTNESS_CONTROL]);
        break;	
    
    case V4L2_CID_CONTRAST:
        orbbec_set_ctpu(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_CONTRAST_CONTROL, ctrl->val, pu_ctl_len[PU_CONTRAST_CONTROL]);
        break;
        
    case V4L2_CID_EXPOSURE_AUTO:
        if (state->cam_type == ORB_CAM_COLOR) {
            if(ctrl->val == V4L2_EXPOSURE_AUTO) {
                orbbec_set_ctpu(state, CT_CONTROL_SELECT, RGB_CONTROL_SELECT, CT_AE_MODE_CONTROL, CT_AE_AUTO_MODE, ct_ctl_len[CT_AE_MODE_CONTROL]);
            } else if(ctrl->val == V4L2_EXPOSURE_MANUAL) {
                orbbec_set_ctpu(state, CT_CONTROL_SELECT, RGB_CONTROL_SELECT, CT_AE_MODE_CONTROL, CT_AE_MANUL_MODE, ct_ctl_len[CT_AE_MODE_CONTROL]);
            }
        }
        if (state->cam_type == ORB_CAM_IR_L || state->cam_type == ORB_CAM_IR_R) {
            if(ctrl->val == V4L2_EXPOSURE_AUTO) {
                orbbec_set_ctpu(state, CT_CONTROL_SELECT, IR_CONTROL_SELECT, CT_AE_MODE_CONTROL, CT_AE_AUTO_MODE, ct_ctl_len[CT_AE_MODE_CONTROL]);
            } else if(ctrl->val == V4L2_EXPOSURE_MANUAL) {
                orbbec_set_ctpu(state, CT_CONTROL_SELECT, IR_CONTROL_SELECT, CT_AE_MODE_CONTROL, CT_AE_MANUL_MODE, ct_ctl_len[CT_AE_MODE_CONTROL]);
            }
        }
        if (state->cam_type == ORB_CAM_DEPTH) {
            ret = orbbec_set_autoexposure(state, ctrl->val);
        }
        break;

    case V4L2_CID_EXPOSURE_ABSOLUTE:
        if (state->cam_type != ORB_CAM_COLOR) {
            ret = orbbec_set_exposure(state, ctrl->val);
        } else {
            orbbec_set_ctpu(state, CT_CONTROL_SELECT, RGB_CONTROL_SELECT, CT_EXPOSURE_TIME_ABSOLUTE_CONTROL, ctrl->val, ct_ctl_len[CT_EXPOSURE_TIME_ABSOLUTE_CONTROL]);	
        }
        break;

    case V4L2_CID_GAMMA:
        orbbec_set_ctpu(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_GAMMA_CONTROL, ctrl->val, pu_ctl_len[PU_GAMMA_CONTROL]);	
        break;

    case V4L2_CID_HUE:
        orbbec_set_ctpu(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_HUE_CONTROL, ctrl->val, pu_ctl_len[PU_HUE_CONTROL]);	
        break;

    case V4L2_CID_SATURATION:
        orbbec_set_ctpu(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_SATURATION_CONTROL, ctrl->val, pu_ctl_len[PU_SATURATION_CONTROL]);	
        break;

    case V4L2_CID_SHARPNESS:
        orbbec_set_ctpu(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_SHARPNESS_CONTROL, ctrl->val, pu_ctl_len[PU_SHARPNESS_CONTROL]);	
        break;

    case V4L2_CID_WHITE_BALANCE_TEMPERATURE:
        orbbec_set_ctpu(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_WHITE_BALANCE_TEMPERATURE_CONTROL, ctrl->val, pu_ctl_len[PU_WHITE_BALANCE_TEMPERATURE_CONTROL]);	
        break;

    case V4L2_CID_AUTO_WHITE_BALANCE:
        orbbec_set_ctpu(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL, ctrl->val, pu_ctl_len[PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL]);	
        break;

    case V4L2_CID_POWER_LINE_FREQUENCY:
        orbbec_set_ctpu(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_POWER_LINE_FREQUENCY_CONTROL, ctrl->val, pu_ctl_len[PU_POWER_LINE_FREQUENCY_CONTROL]);	
        break;
    
    case V4L2_CID_EXPOSURE_AUTO_PRIORITY:
        orbbec_set_ctpu(state, CT_CONTROL_SELECT, RGB_CONTROL_SELECT, CT_AE_PRIORITY_CONTROL, ctrl->val, ct_ctl_len[CT_AE_PRIORITY_CONTROL]);	
        break;

    case V4L2_CID_FOCUS_ABSOLUTE:
        orbbec_set_ctpu(state, CT_CONTROL_SELECT, RGB_CONTROL_SELECT, CT_FOCUS_ABSOLUTE_CONTROL, ctrl->val, ct_ctl_len[CT_FOCUS_ABSOLUTE_CONTROL]);	
        break;

    case ORBBEC_CAMERA_CID_SET_DATA:
        dev_dbg(&state->client->dev,"%s(): G2R set size \n",__func__);
        if (ctrl->p_new.p_u8) {
            memcpy(get_cmd, ctrl->p_new.p_u8, sizeof(struct orbbec_header)+8);
            dev_dbg(&state->client->dev, "%s(): G2R set opcode=%d, proid=%d\n",
                    __func__, get_cmd->header.code, get_cmd->set_imu_cmd.prop_code0);
            //save imu_fps
            if(get_cmd->header.code == 2 && get_cmd->set_imu_cmd.prop_code0 == 2021) {
                state->imu_fps = orb_imu_framerates[get_cmd->set_imu_cmd.value0];
                dev_dbg(&state->client->dev,"%s(): imu_fps=%d \n", __func__, state->imu_fps);
            }  
            ret = sensor_write_data(state, (u8*)ctrl->p_new.p_u8, get_cmd->header.len);
        }
        break;

    case ORBBEC_CAMERA_CID_SET_DATA_LEN:
        if (ctrl->val) {
            get_data_len = ctrl->val;
            dev_dbg(&state->client->dev, "%s(): G2R set get data len= %d\n", __func__, get_data_len);
        }
        break;
    
    case ORBBEC_CAMERA_CID_RESET_DEVICE_LINK:
        dev_dbg(&state->client->dev,"%s(): G2R reset device link\n", __func__);
        ret = orbbec_reset_device_link(state, 0);
        msleep (3000);
        ret = orbbec_reset_device_link(state, 1);
        msleep (3000);
        dev_dbg(&state->client->dev,"%s(): G2R reset device link finish\n", __func__);
        break;

    case ORBBEC_CAMERA_CID_PPS_TRIGGER:
        dev_dbg(&state->client->dev,"%s(): G2R set pps trigger\n", __func__);
        gpio_set_value(state->pps_gpios, 1);
        msleep (1);
        gpio_set_value(state->pps_gpios, 0);
        break;

    case ORBBEC_CAMERA_CID_RESET_DEVICE:
        dev_dbg(&state->client->dev,"%s(): G2R reset device\n", __func__);
        ret = max9295d_set_orbbec_off(state);
        msleep (3000);
        ret = max9295d_set_orbbec_on(state);
        msleep (3000);
        dev_dbg(&state->client->dev,"%s(): G2R reset device finish\n", __func__);
        break;

    }

    devm_kfree(&state->client->dev, get_cmd);
    mutex_unlock(&state->lock);

    return ret;
}

static int orb_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
    struct orb *state = container_of(ctrl->handler, struct orb,
            ctrls.handler);
    int16_t temp;
    int ret = 0, err = 0;
    u16 reg;

    dev_dbg(&state->client->dev, "%s(): ctrl: %s \n",
        __func__, ctrl->name);
    mutex_lock(&state->lock);

    switch (ctrl->id) {
    case V4L2_CID_GAIN:
        if (state->cam_type != ORB_CAM_COLOR) {
            orbbec_get_ctpu(state, PU_CONTROL_SELECT, IR_CONTROL_SELECT, PU_GAIN_CONTROL, &ctrl->val, pu_ctl_len[PU_GAIN_CONTROL]);
        } else {
            orbbec_get_ctpu(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_GAIN_CONTROL, &ctrl->val, pu_ctl_len[PU_GAIN_CONTROL]);
        }	
        break;

    case V4L2_CID_BACKLIGHT_COMPENSATION:
        orbbec_get_ctpu(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_BACKLIGHT_COMPENSATION_CONTROL, &ctrl->val, pu_ctl_len[PU_BACKLIGHT_COMPENSATION_CONTROL]);
        break;

    case V4L2_CID_BRIGHTNESS:
        orbbec_get_ctpu(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_BRIGHTNESS_CONTROL, &temp, pu_ctl_len[PU_BRIGHTNESS_CONTROL]);
        ctrl->val = temp;
        break;	
    
    case V4L2_CID_CONTRAST:
        orbbec_get_ctpu(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_CONTRAST_CONTROL, &ctrl->val, pu_ctl_len[PU_CONTRAST_CONTROL]);
        break;	
    case V4L2_CID_EXPOSURE_AUTO:
        if(state->cam_type == ORB_CAM_DEPTH) {
            orbbec_get_autoexposure(state, &reg);
            if (reg == 1) ctrl->val = V4L2_EXPOSURE_AUTO;
            else if (reg == 0) ctrl->val = V4L2_EXPOSURE_MANUAL;
        } else {
            if(state->cam_type == ORB_CAM_COLOR) {
                orbbec_get_ctpu(state, CT_CONTROL_SELECT, RGB_CONTROL_SELECT, CT_AE_MODE_CONTROL, &ctrl->val, ct_ctl_len[CT_AE_MODE_CONTROL]);
            } else {
                orbbec_get_ctpu(state, CT_CONTROL_SELECT, IR_CONTROL_SELECT, CT_AE_MODE_CONTROL, &ctrl->val, ct_ctl_len[CT_AE_MODE_CONTROL]);
            }
            if(ctrl->val == CT_AE_AUTO_MODE || ctrl->val == CT_AE_APERTURE_MODE)
                ctrl->val = V4L2_EXPOSURE_AUTO;
            else if(ctrl->val == CT_AE_MANUL_MODE || ctrl->val == CT_AE_SHUTTER_MODE)
                ctrl->val = V4L2_EXPOSURE_MANUAL;
        }
        break;

    case V4L2_CID_EXPOSURE_ABSOLUTE:
        if (state->cam_type != ORB_CAM_COLOR) {
            ret = orbbec_get_exposure(state, &ctrl->val);
        } else {
            ret = orbbec_get_ctpu(state, CT_CONTROL_SELECT, RGB_CONTROL_SELECT, CT_EXPOSURE_TIME_ABSOLUTE_CONTROL, &ctrl->val, ct_ctl_len[CT_EXPOSURE_TIME_ABSOLUTE_CONTROL]);
        }
        break;

    case V4L2_CID_GAMMA:
        orbbec_get_ctpu(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_GAMMA_CONTROL, &ctrl->val, pu_ctl_len[PU_GAMMA_CONTROL]);	
        break;

    case V4L2_CID_HUE:
        orbbec_get_ctpu(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_HUE_CONTROL, &temp, pu_ctl_len[PU_HUE_CONTROL]);
        ctrl->val = temp;
        break;

    case V4L2_CID_SATURATION:
        orbbec_get_ctpu(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_SATURATION_CONTROL, &ctrl->val, pu_ctl_len[PU_SATURATION_CONTROL]);	
        break;

    case V4L2_CID_SHARPNESS:
        orbbec_get_ctpu(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_SHARPNESS_CONTROL, &ctrl->val, pu_ctl_len[PU_SHARPNESS_CONTROL]);	
        break;

    case V4L2_CID_WHITE_BALANCE_TEMPERATURE:
        orbbec_get_ctpu(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_WHITE_BALANCE_TEMPERATURE_CONTROL, &ctrl->val, pu_ctl_len[PU_WHITE_BALANCE_TEMPERATURE_CONTROL]);	
        break;

    case V4L2_CID_AUTO_WHITE_BALANCE:
        orbbec_get_ctpu(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL, &ctrl->val, pu_ctl_len[PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL]);	
        break;

    case V4L2_CID_POWER_LINE_FREQUENCY:
        orbbec_get_ctpu(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_POWER_LINE_FREQUENCY_CONTROL, &ctrl->val, pu_ctl_len[PU_POWER_LINE_FREQUENCY_CONTROL]);	
        break;

    case V4L2_CID_EXPOSURE_AUTO_PRIORITY:
        orbbec_get_ctpu(state, CT_CONTROL_SELECT, RGB_CONTROL_SELECT, CT_AE_PRIORITY_CONTROL, &ctrl->val, ct_ctl_len[CT_AE_PRIORITY_CONTROL]);	
        break;

    case V4L2_CID_FOCUS_ABSOLUTE:
        orbbec_get_ctpu(state, CT_CONTROL_SELECT, RGB_CONTROL_SELECT, CT_FOCUS_ABSOLUTE_CONTROL, &ctrl->val, ct_ctl_len[CT_FOCUS_ABSOLUTE_CONTROL]);	
        break;

    case ORBBEC_CAMERA_CID_GET_VERSION_DATA:
        if (ctrl->p_new.p_u8) {
            ret = orbbec_get_version(state, ctrl->p_new.p_u8);	
        }
        break;

    case ORBBEC_CAMERA_CID_GET_IMU_FPS:
        if (ctrl->p_new.p_u32) {
            *ctrl->p_new.p_u32 = state->imu_fps;
        }
        break;

    case ORBBEC_CAMERA_CID_GET_IMU_DATA:
        if (ctrl->p_new.p_u8) {
            ret = orbbec_get_imu_data(state, ctrl->p_new.p_u8);		
        }
        break;
        
    case ORBBEC_CAMERA_CID_GET_DATA:
        dev_dbg(&state->client->dev,"%s(): G2R getdata\n",__func__);
        if (ctrl->p_new.p_u8) {
            ret = sensor_read_data(state, (u8*)ctrl->p_new.p_u8 , get_data_len);
        }
        break;

    case ORBBEC_CAMERA_CID_GET_PID_SN:
        dev_dbg(&state->client->dev,"%s(): G2R getpidsn\n",__func__);
        ret = orbbec_get_deviceinfo(state, &state->device_info.pid, &state->device_info.sn, &state->device_info.asic_sn);
        if (ret) {
            dev_err(&state->client->dev,"orbbec_get_deviceinfo err\n");
            return ret;
        }
        if (ctrl->p_new.p_u8) {
            memcpy(ctrl->p_new.p_u8,&state->device_info, sizeof(struct orbbec_device_info));
        }
        break;

    case ORBBEC_CAMERA_CID_GET_LINK_STATE:
        /*
        dev_dbg(&state->client->dev,"%s(): G2R get link state\n",__func__);
        err = max9295d_set_orbbec_on(state);
        if (err) {
            dev_err(&state->client->dev, "%s, failed to max9295d set_orbbec_on\n", __func__);
            ctrl->val = 0;
        } else {
            ctrl->val = 1;
        }
        */
        err = state->deser_ops->get_link_state(state->dser_dev, state->dser_link, &ctrl->val);
        break;
    }
    mutex_unlock(&state->lock);

    return ret;
}

static const struct v4l2_ctrl_ops orb_ctrl_ops = {
    .s_ctrl	= orb_s_ctrl,
    .g_volatile_ctrl = orb_g_volatile_ctrl,
};

static const struct v4l2_ctrl_config g2r_ctrl_get_imu_fps = {
    .ops = &orb_ctrl_ops,
    .id = ORBBEC_CAMERA_CID_GET_IMU_FPS,
    .name = "GET_IMU_FPS",
    .type = V4L2_CTRL_TYPE_U32,
    .dims = {1},
    .elem_size = sizeof(u32),
    .min = 0,
    .max = 0xFFFFFFFF,
    .def = 0,
    .step = 1,
    .flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_READ_ONLY,
};	
    
static const struct v4l2_ctrl_config g2r_ctrl_set_data = {
    .ops = &orb_ctrl_ops,
    .id = ORBBEC_CAMERA_CID_SET_DATA,
    .name = "G2R_W",
    .type = V4L2_CTRL_TYPE_U8,
    .dims = {G2R_RW_DATA_LEN},
    .elem_size = sizeof(u8),
    .min = 0,
    .max = 0xFFFFFFFF,
    .def = 240,
    .step = 1,
    .flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE,
};	

static const struct v4l2_ctrl_config g2r_ctrl_set_datalen = {
    .ops = &orb_ctrl_ops,
    .id = ORBBEC_CAMERA_CID_SET_DATA_LEN,
    .name = "SET G2R_R LEN",
    .type = V4L2_CTRL_TYPE_INTEGER,
    .min = 0,
    .max = 256,
    .step = 1,
    .def = 256,
    .flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE,
};	

static const struct v4l2_ctrl_config g2r_ctrl_reset_device_link = {
    .ops = &orb_ctrl_ops,
    .id = ORBBEC_CAMERA_CID_RESET_DEVICE_LINK,
    .name = "G2R_RESET_DEVICE_LINK",
    .type = V4L2_CTRL_TYPE_BOOLEAN,
    .min = 0,
    .max = 1,
    .step = 1,
    .def = 1,
    .flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE,
};

static const struct v4l2_ctrl_config g2r_ctrl_reset_device = {
    .ops = &orb_ctrl_ops,
    .id = ORBBEC_CAMERA_CID_RESET_DEVICE,
    .name = "G2R_RESET_DEVICE",
    .type = V4L2_CTRL_TYPE_BOOLEAN,
    .min = 0,
    .max = 1,
    .step = 1,
    .def = 1,
    .flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE,
};

static const struct v4l2_ctrl_config g2r_ctrl_get_version_data = {
    .ops = &orb_ctrl_ops,
    .id = ORBBEC_CAMERA_CID_GET_VERSION_DATA,
    .name = "G2R_GET_VERSION",
    .type = V4L2_CTRL_TYPE_U8,
    .dims = {G2R_GET_VERSION_DATA_LEN},
    .elem_size = sizeof(u8),
    .min = 0,
    .max = 0xFFFFFFFF,
    .def = 240,
    .step = 1,
    .flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_READ_ONLY,
};

static const struct v4l2_ctrl_config g2r_ctrl_get_data = {
    .ops = &orb_ctrl_ops,
    .id = ORBBEC_CAMERA_CID_GET_DATA,
    .name = "G2R_R",
    .type = V4L2_CTRL_TYPE_U8,
    .dims = {G2R_RW_DATA_LEN},
    .elem_size = sizeof(u8),
    .min = 0,
    .max = 0xFFFFFFFF,
    .def = 1,
    .step = 1,
    .flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_READ_ONLY,
};
static const struct v4l2_ctrl_config g2r_ctrl_get_pid_sn = {
    .ops = &orb_ctrl_ops,
    .id = ORBBEC_CAMERA_CID_GET_PID_SN,
    .name = "G2R_R_PID_SN",
    .type = V4L2_CTRL_TYPE_U8,
    .dims = {sizeof(struct orbbec_device_info)},
    .elem_size = sizeof(u8),
    .min = 0,
    .max = 0xFFFFFFFF,
    .def = 1,
    .step = 1,
    .flags = V4L2_CTRL_FLAG_VOLATILE |V4L2_CTRL_FLAG_READ_ONLY,
};

static const struct v4l2_ctrl_config g2r_ctrl_get_imu_data = {
    .ops = &orb_ctrl_ops,
    .id = ORBBEC_CAMERA_CID_GET_IMU_DATA,
    .name = "G2R_GET_IMU_DATA",
    .type = V4L2_CTRL_TYPE_U8,
    .dims = {G2R_I2C_RESPONSE_HEARD_LEN + G2R_GET_IMU_DATA_LEN * G2R_IMU_STOCK_MAX_SIZE},
    .elem_size = sizeof(u8),
    .min = 0,
    .max = 0xFFFFFFFF,
    .def = 240,
    .step = 1,
    .flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_READ_ONLY,
};

static const struct v4l2_ctrl_config g2r_ctrl_pps_trigger = {
    .ops = &orb_ctrl_ops,
    .id = ORBBEC_CAMERA_CID_PPS_TRIGGER,
    .name = "G2R_PPS_TRIGGER",
    .type = V4L2_CTRL_TYPE_BOOLEAN,
    .min = 0,
    .max = 1,
    .step = 1,
    .def = 1,
    .flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE,
};

static const struct v4l2_ctrl_config g2r_ctrl_get_link_state = {
    .ops = &orb_ctrl_ops,
    .id = ORBBEC_CAMERA_CID_GET_LINK_STATE,
    .name = "GET LINK STATE",
    .type = V4L2_CTRL_TYPE_INTEGER,
    .min = 0,
    .max = 1,
    .step = 1,
    .def = 1,
    .flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_READ_ONLY,
};	


static int orb_set_flags(uint8_t info)
{
    switch (info) {
    case 3:
        return V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
    case 2:
        return V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE | V4L2_CTRL_FLAG_WRITE_ONLY;
    case 1:
        return V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE | V4L2_CTRL_FLAG_READ_ONLY;
    default:
        return V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
    }
}

int orb_ctrl_init(struct orb *state)
{
    const struct v4l2_ctrl_ops *ops = &orb_ctrl_ops;
    struct orb_ctrls *ctrls = &state->ctrls;
    struct v4l2_ctrl_handler *hdl = &ctrls->handler;
    struct v4l2_subdev *sd = &state->sensor.sd.subdev;
    struct ctpu_data *ctpu_data_t = devm_kzalloc(&state->client->dev, sizeof(struct ctpu_data), GFP_KERNEL);
    struct xu_data *xu_data_t = devm_kzalloc(&state->client->dev, sizeof(struct xu_data), GFP_KERNEL);
    int ret, sel;

    ret = v4l2_ctrl_handler_init(hdl, ORB_N_CONTROLS);
    if (ret < 0) {
        v4l2_err(sd, "cannot init ctrl handler (%d)\n", ret);
        goto err_ctl;
    }

    /* Total gain */
    if (state->cam_type == ORB_CAM_COLOR)
        sel = RGB_CONTROL_SELECT;
    else
        sel = IR_CONTROL_SELECT;
    orbbec_get_ctpu_init(state, PU_CONTROL_SELECT, sel, PU_GAIN_CONTROL, ctpu_data_t);
    pu_ctl_len[PU_GAIN_CONTROL] = ctpu_data_t->len;
    ctrls->gain = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_GAIN,
                    ctpu_data_t->min, ctpu_data_t->max, ctpu_data_t->step, ctpu_data_t->def);
    if (ctrls->gain)
        ctrls->gain->flags = orb_set_flags(ctpu_data_t->info);

    orbbec_get_ctpu_init(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_BACKLIGHT_COMPENSATION_CONTROL, ctpu_data_t);
    pu_ctl_len[PU_BACKLIGHT_COMPENSATION_CONTROL] = ctpu_data_t->len;
    ctrls->backlight = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_BACKLIGHT_COMPENSATION,
                    ctpu_data_t->min, ctpu_data_t->max, ctpu_data_t->step, ctpu_data_t->def);
    if (ctrls->backlight)
        ctrls->backlight->flags = orb_set_flags(ctpu_data_t->info);

    orbbec_get_ctpu_init(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_BRIGHTNESS_CONTROL, ctpu_data_t);
    pu_ctl_len[PU_BRIGHTNESS_CONTROL] = ctpu_data_t->len;
    ctrls->brightness = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_BRIGHTNESS,
                    ctpu_data_t->min, ctpu_data_t->max, ctpu_data_t->step, ctpu_data_t->def);
    if (ctrls->brightness)
        ctrls->brightness->flags = orb_set_flags(ctpu_data_t->info);

    orbbec_get_ctpu_init(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_CONTRAST_CONTROL, ctpu_data_t);
    pu_ctl_len[PU_CONTRAST_CONTROL] = ctpu_data_t->len;
    ctrls->contrast = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_CONTRAST,
                    ctpu_data_t->min, ctpu_data_t->max, ctpu_data_t->step, ctpu_data_t->def);
    if (ctrls->contrast)
        ctrls->contrast->flags = orb_set_flags(ctpu_data_t->info);

    orbbec_get_ctpu_init(state, CT_CONTROL_SELECT, RGB_CONTROL_SELECT, CT_AE_MODE_CONTROL, ctpu_data_t);
    ct_ctl_len[CT_AE_MODE_CONTROL] = ctpu_data_t->len;
    ctrls->auto_exp = v4l2_ctrl_new_std_menu(hdl, ops, V4L2_CID_EXPOSURE_AUTO,
            V4L2_EXPOSURE_MANUAL, ~((1 << V4L2_EXPOSURE_AUTO) | (1 << V4L2_EXPOSURE_MANUAL)), V4L2_EXPOSURE_AUTO);
    if (ctrls->auto_exp)
        ctrls->auto_exp->flags |= V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;

    /* Exposure time: V4L2_CID_EXPOSURE_ABSOLUTE default unit: 100 us. */
    if (state->cam_type != ORB_CAM_COLOR) {
        orbbec_get_exposure_init(state,xu_data_t);
        ctrls->exposure = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_EXPOSURE_ABSOLUTE,
                    xu_data_t->min_xu, xu_data_t->max_xu, xu_data_t->step_xu, xu_data_t->def_xu);
    } else {
        orbbec_get_ctpu_init(state, CT_CONTROL_SELECT, RGB_CONTROL_SELECT, CT_EXPOSURE_TIME_ABSOLUTE_CONTROL, ctpu_data_t);
        ct_ctl_len[CT_EXPOSURE_TIME_ABSOLUTE_CONTROL] = ctpu_data_t->len;
        ctrls->exposure = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_EXPOSURE_ABSOLUTE,
                    ctpu_data_t->min, ctpu_data_t->max, ctpu_data_t->step, ctpu_data_t->def);
    }
    if (ctrls->exposure) {
        ctrls->exposure->flags |= V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
        /* override default int type to u32 to match SKU & UVC */
        //ctrls->exposure->type = V4L2_CTRL_TYPE_U32;
    }

    orbbec_get_ctpu_init(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_GAMMA_CONTROL, ctpu_data_t);
    pu_ctl_len[PU_GAMMA_CONTROL] = ctpu_data_t->len;
    ctrls->gamma = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_GAMMA,
                    ctpu_data_t->min, ctpu_data_t->max, ctpu_data_t->step, ctpu_data_t->def);
    if (ctrls->gamma)
        ctrls->gamma->flags = orb_set_flags(ctpu_data_t->info);

    orbbec_get_ctpu_init(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_HUE_CONTROL, ctpu_data_t);
    pu_ctl_len[PU_HUE_CONTROL] = ctpu_data_t->len;
    ctrls->hue = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_HUE,
                    ctpu_data_t->min, ctpu_data_t->max, ctpu_data_t->step, ctpu_data_t->def);
    if (ctrls->hue)
        ctrls->hue->flags = orb_set_flags(ctpu_data_t->info);

    orbbec_get_ctpu_init(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_SATURATION_CONTROL, ctpu_data_t);
    pu_ctl_len[PU_SATURATION_CONTROL] = ctpu_data_t->len;
    ctrls->saturation = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_SATURATION,
                    ctpu_data_t->min, ctpu_data_t->max, ctpu_data_t->step, ctpu_data_t->def);
    if (ctrls->saturation)
        ctrls->saturation->flags = orb_set_flags(ctpu_data_t->info);

    orbbec_get_ctpu_init(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_SHARPNESS_CONTROL, ctpu_data_t);
    pu_ctl_len[PU_SHARPNESS_CONTROL] = ctpu_data_t->len;
    ctrls->sharpness = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_SHARPNESS,
                    ctpu_data_t->min, ctpu_data_t->max, ctpu_data_t->step, ctpu_data_t->def);
    if (ctrls->sharpness)
        ctrls->sharpness->flags = orb_set_flags(ctpu_data_t->info);

    orbbec_get_ctpu_init(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_WHITE_BALANCE_TEMPERATURE_CONTROL, ctpu_data_t);
    pu_ctl_len[PU_WHITE_BALANCE_TEMPERATURE_CONTROL] = ctpu_data_t->len;
    ctrls->whitebalance = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_WHITE_BALANCE_TEMPERATURE,
                    ctpu_data_t->min, ctpu_data_t->max, ctpu_data_t->step, ctpu_data_t->def);
    if (ctrls->whitebalance)
        ctrls->whitebalance->flags = orb_set_flags(ctpu_data_t->info);

    orbbec_get_ctpu_init(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL, ctpu_data_t);
    pu_ctl_len[PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL] = ctpu_data_t->len;
    ctrls->autowhitebalance = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_AUTO_WHITE_BALANCE,
                    ctpu_data_t->min, ctpu_data_t->max, ctpu_data_t->step, ctpu_data_t->def);
    if (ctrls->autowhitebalance)
        ctrls->autowhitebalance->flags = orb_set_flags(ctpu_data_t->info);

    orbbec_get_ctpu_init(state, PU_CONTROL_SELECT, RGB_CONTROL_SELECT, PU_POWER_LINE_FREQUENCY_CONTROL, ctpu_data_t);
    pu_ctl_len[PU_POWER_LINE_FREQUENCY_CONTROL] = ctpu_data_t->len;
    ctrls->powerlinefreq = v4l2_ctrl_new_std_menu(hdl, ops, V4L2_CID_POWER_LINE_FREQUENCY,
                    ctpu_data_t->max, ~((1 << V4L2_CID_POWER_LINE_FREQUENCY_AUTO) |
                    (1 << V4L2_CID_POWER_LINE_FREQUENCY_60HZ) | (1 << V4L2_CID_POWER_LINE_FREQUENCY_50HZ) | 
                    (1 << V4L2_CID_POWER_LINE_FREQUENCY_DISABLED)), ctpu_data_t->def);
    if (ctrls->powerlinefreq)
        ctrls->powerlinefreq->flags = orb_set_flags(ctpu_data_t->info);
    
    orbbec_get_ctpu_init(state, CT_CONTROL_SELECT, RGB_CONTROL_SELECT, CT_AE_PRIORITY_CONTROL, ctpu_data_t);
    ct_ctl_len[CT_AE_PRIORITY_CONTROL] = ctpu_data_t->len;
    ctrls->aepriority = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_EXPOSURE_AUTO_PRIORITY,
                    ctpu_data_t->min, ctpu_data_t->max, ctpu_data_t->step, ctpu_data_t->def);
    if (ctrls->aepriority)
        ctrls->aepriority->flags = orb_set_flags(ctpu_data_t->info);

    /*
    orbbec_get_ctpu_init(state, CT_CONTROL_SELECT, RGB_CONTROL_SELECT, CT_FOCUS_ABSOLUTE_CONTROL, ctpu_data_t);
    ct_ctl_len[CT_FOCUS_ABSOLUTE_CONTROL] = ctpu_data_t->len;
    ctrls->focus_absolute = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FOCUS_ABSOLUTE,
                    ctpu_data_t->min, ctpu_data_t->max, ctpu_data_t->step, ctpu_data_t->def);
    if (ctrls->focus_absolute)
        ctrls->focus_absolute->flags = orb_set_flags(ctpu_data_t->info);
    */

    if (hdl->error) {
        v4l2_err(sd, "error creating controls (%d)\n", hdl->error);
        ret = hdl->error;
        v4l2_ctrl_handler_free(hdl);
        goto err_ctl;
    }
    /*
    //During the initialization of the V4L2 device, the setup control will be set to the default values of the current control parameters.
    ret = v4l2_ctrl_handler_setup(hdl);
    if (ret < 0) {
        dev_err(&state->client->dev,
            "failed to set default values for controls\n");
        v4l2_ctrl_handler_free(hdl);
        return ret;
    }
    */
    ctrls->set_date = v4l2_ctrl_new_custom(hdl,&g2r_ctrl_set_data, NULL);
    ctrls->set_datelen = v4l2_ctrl_new_custom(hdl,&g2r_ctrl_set_datalen, NULL);
    ctrls->reset_device_link = v4l2_ctrl_new_custom(hdl,&g2r_ctrl_reset_device_link, NULL);
    ctrls->reset_device = v4l2_ctrl_new_custom(hdl,&g2r_ctrl_reset_device, NULL);
    ctrls->pps_trigger = v4l2_ctrl_new_custom(hdl,&g2r_ctrl_pps_trigger, NULL);
    ctrls->get_link_state = v4l2_ctrl_new_custom(hdl,&g2r_ctrl_get_link_state, NULL);
    ctrls->get_date = v4l2_ctrl_new_custom(hdl,&g2r_ctrl_get_data, NULL);
    ctrls->get_pid_sn = v4l2_ctrl_new_custom(hdl,&g2r_ctrl_get_pid_sn, NULL);
    ctrls->get_imu_fps = v4l2_ctrl_new_custom(hdl,&g2r_ctrl_get_imu_fps, NULL);
    ctrls->get_version = v4l2_ctrl_new_custom(hdl,&g2r_ctrl_get_version_data , NULL);
    ctrls->get_imu_data = v4l2_ctrl_new_custom(hdl,&g2r_ctrl_get_imu_data, NULL);

    state->sensor.sd.subdev.ctrl_handler = hdl;

err_ctl:
    devm_kfree(&state->client->dev, xu_data_t);
    devm_kfree(&state->client->dev, ctpu_data_t);

    return 0;
}
