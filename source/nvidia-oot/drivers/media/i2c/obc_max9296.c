// SPDX-License-Identifier: GPL-2.0
/*
 * max9296a.c - max9296a IO Expander driver
 *
 * Copyright (c) 2016-2023, NVIDIA CORPORATION & AFFILIATES. All Rights Reserved.
 */

/* #define DEBUG */

#include <nvidia/conftest.h>

#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <media/camera_common.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>

#include <media/obc_max9296.h>



/*
 Programming Steps - Video Data Path
Configuring the forward video path from the GMSL2 quad deserializer consists of four basic programming steps:
 1. Link Initialization
 2. Video Pipe Selection
 3. Video Pipe to MIPI Controller Mapping
 4. MIPI PHY Settings
*/

/*-----------------------------------------------------------------------------------------------
 * 4. MIPI PHY Settings
 *----------------------------------------------------------------------------------------------*/
#define MAX9296_DST_CSI_MODE_ADDR 0x330 //MIPI PHY Mode Select Register: 2x4 mode, 1x4 mode, 4x2 mode

#define MAX9296_PWDN_PHYS_ADDR   0x332  //MIPI PHY Enable Register: MIPI PHY 0~3, 0 = PHY in standby, 1 = PHY enabled
#define ALLPHYS_NOSTDBY          0xF0 

#define MAX9296_LANE_MAP1_ADDR   0x333 //MIPI PHY 0&1 Lane Mapping Register
#define MAX9296_LANE_MAP2_ADDR   0x334 //MIPI PHY 3&2 Lane Mapping Register
#define MAX9296_LANE_POLA1_ADDR  0x335 //MIPI PHY 0&1 Lane Polarity Register
#define MAX9296_LANE_POLA2_ADDR  0x336 //MIPI PHY 3&2 Lane Polarity Register

#define MAX9296_LANE_CTRL0_ADDR  0x40A //MIPI PHY 0 Lane Count / C-PHY enable Register
#define MAX9296_LANE_CTRL1_ADDR  0x44A //MIPI PHY 1 Lane Count / C-PHY enable Register
#define MAX9296_LANE_CTRL2_ADDR  0x48A //MIPI PHY 2 Lane Count / C-PHY enable Register
#define MAX9296_LANE_CTRL3_ADDR  0x4CA //MIPI PHY 3 Lane Count / C-PHY enable Register

#define MAX9296_PHY0_CLK_ADDR 	 0x31D //MIPI PHY 0 DPLL Freq Register
#define MAX9296_PHY1_CLK_ADDR 	 0x320 //MIPI PHY 1 DPLL Freq Register
#define MAX9296_PHY2_CLK_ADDR 	 0x323 //MIPI PHY 2 DPLL Freq Register
#define MAX9296_PHY3_CLK_ADDR 	 0x326 //MIPI PHY 2 DPLL Freq Register

// #define MAX9296_CSI_OUT_EN      0x40B //CSI_OUT_EN: CSI output enabled, en:0x2, dis:0x0

/*-----------------------------------------------------------------------------------------------
 * 3. Video Pipe to MIPI Controller Mapping
 *----------------------------------------------------------------------------------------------*/
#define MAX9296_TX11_PIPE_X_EN_ADDR   0x40B //Mapping Enable Low Byte, 0~7
#define MAX9296_TX11_PIPE_X_EN_H_ADDR 0x40C //Mapping Enable High Byte, 8~15

#define MAX9296_PIPE_X_SRC_0_MAP_ADDR 0x40D //MAP_SRC_0 Register: VC+DT
#define MAX9296_PIPE_X_DST_0_MAP_ADDR 0x40E //MAP_DES_0 Register: VC+DT
#define MAX9296_PIPE_X_SRC_1_MAP_ADDR 0x40F //MAP_SRC_1 Register: VC+DT
#define MAX9296_PIPE_X_DST_1_MAP_ADDR 0x410 //MAP_DES_1 Register: VC+DT
#define MAX9296_PIPE_X_SRC_2_MAP_ADDR 0x411 //MAP_SRC_2 Register: VC+DT
#define MAX9296_PIPE_X_DST_2_MAP_ADDR 0x412 //MAP_DES_2 Register: VC+DT
#define MAX9296_PIPE_X_SRC_3_MAP_ADDR 0x413 //MAP_SRC_3 Register: VC+DT
#define MAX9296_PIPE_X_DST_3_MAP_ADDR 0x414 //MAP_DES_3 Register: VC+DT

#define MAX9296_TX45_PIPE_X_DST_CTRL_ADDR  0x42D //MAP Destination Controller Register: 0~3
#define MAX9296_TX45_PIPE_X_DST_CTRL2_ADDR 0x42E //MAP Destination Controller Register: 4~7

//--------------------------------------------------------------------
// others
#define MAX9296_PIPE_X_ST_SEL_ADDR  0x50 //RX packets with selected stream ID, add b7: RX_CRC_EN

#define MAX9296_CTRL0_ADDR 0x10   //get link status
#define CTRL0_RESET_ALL     0x80  //b[7]: 1->RESET_ALL, Chip reset
#define CTRL0_RESET_LINK    0x40  //b[6]: reset link
#define CTRL0_RESET_ONESHOT 0x20  //b[5]: reset oneshot, keep register
#define CTRL0_LINK_SPLITTER_MODE 0x3 //Enable plitter mode

#define MAX9296_CTRL3_ADDR 0x13 //get link status
#define CTRL3_LINK_LOCKED  0x08 //b[3], GMSL2 link locked

#define MAX9296_LINK_RESET_ADDR 0x10
/*-----------------------------------------------------------------------------------------------
 * data defines
 *----------------------------------------------------------------------------------------------*/
/* MIPI PHY Mode Select */
#define MAX9296_CSI_MODE_4X2 0x1  //4x2lanes
#define MAX9296_CSI_MODE_1X4 0x2
#define MAX9296_CSI_MODE_2X4 0x4  //2x4lanes

//----------------------------------------------------------
#define MAX9296_MAX_PIPES	4
#define MAX9296_MAX_LINKS   2

//----------------------------------------------------------
#define SERI_DEFAULT_I2C_ADDRESS    0x40

//----------------------------------------------------------
#define TO_SERI_ADDR(A)	(priv->seri_start_addr + (A))
#define TO_SNR_ADDR(A)	(priv->snr_proxy_addr + (A))

//--------------------------------------------------------------------------------------
struct index_reg_8 {
	u8 source;
	u16 addr;
	u8 val;
};

struct max9296a {
	struct mutex lock;
	struct i2c_client *i2c_client;
	struct regmap *regmap;
	u8 csi_mode;
	u8 links_map;
	u8 links_init_flag;
	u8 pipes_map;
	u8 index;
	u8 deser_addr; //deser address
	u8 seri_start_addr; //serializer start address
	u8 snr_proxy_addr;  // sensor proxy address
	u8 snr_real_addr;   // sensor real address
	int reset_gpio;
	int fsync_mfp_x;
	int pps_mfp_x;
};

//--------------------------------------------------------------------------------------
static int _max9296a_write_reg_with_addr(struct device *dev, u8 i2c_addr, u16 addr, u8 val)
{
	struct max9296a *priv = dev_get_drvdata(dev);
	struct i2c_client *i2c_client;
	int err;

	i2c_client = priv->i2c_client;

	i2c_client->addr = i2c_addr;
	err = regmap_write(priv->regmap, addr, val);
	i2c_client->addr = priv->deser_addr;
	if (err)
		dev_err(dev, "%s err: 0x%02x, 0x%04x = 0x%02x\n", __func__, i2c_addr, addr, val);

	usleep_range(100, 110);

	dev_dbg(dev, "[%s] wr reg: 0x%02x, 0x%04x = 0x%02x\n", (i2c_addr == priv->deser_addr) ? "dser" : "seri", i2c_addr, addr, val);

	return err;
}

int max9296a_write_reg_with_addr(struct device *dev, u8 i2c_addr, u16 addr, u8 val)
{
	struct max9296a *priv = dev_get_drvdata(dev);
	int ret;

	if (priv == NULL)
		return -1;

	mutex_lock(&priv->lock);
	ret = _max9296a_write_reg_with_addr(dev, i2c_addr, addr, val);
	mutex_unlock(&priv->lock);

	return ret;
}
EXPORT_SYMBOL(max9296a_write_reg_with_addr);

int max9296a_write_tab_with_addr(struct device *dev, u8 i2c_addr, struct max_reg_pair tab[], u16 size)
{
	struct max9296a *priv = dev_get_drvdata(dev);
	struct i2c_client *i2c_client;
	int i, err;

	if (priv == NULL)
		return -1;

	mutex_lock(&priv->lock);

	i2c_client = priv->i2c_client;

	i2c_client->addr = i2c_addr;
	for (i = 0; i < size; i++) {
		err = regmap_write(priv->regmap, tab[i].addr, tab[i].val);
		if (err) {
			dev_err(dev, "%s: error, 0x%02x --> 0x%04x = 0x%02x\n", __func__, i2c_addr, tab[i].addr, tab[i].val);
			break;
		}
		usleep_range(100, 110);
		dev_dbg(dev, "[%s] wr tab: 0x%02x, 0x%04x = 0x%02x\n", (i2c_addr == priv->deser_addr) ? "dser" : "seri", i2c_addr, tab[i].addr, tab[i].val);
	}
	i2c_client->addr = priv->deser_addr;

	mutex_unlock(&priv->lock);

	return err;
}
EXPORT_SYMBOL(max9296a_write_tab_with_addr);

int max9296a_read_reg_with_addr(struct device *dev, u8 i2c_addr, u16 addr, u8 *val)
{
	struct max9296a *priv = dev_get_drvdata(dev);
	struct i2c_client *i2c_client;
	unsigned int value = 0;
	int err;

	if (priv == NULL)
		return -1;

	mutex_lock(&priv->lock);

	i2c_client = priv->i2c_client;

	i2c_client->addr = i2c_addr;
	err = regmap_read(priv->regmap, addr, &value);
	i2c_client->addr = priv->deser_addr;
	if (err)
		dev_err(dev, "%s: 0x%02x, 0x%04x = 0x%02x\n", __func__, i2c_addr, addr, value);

	*val = value;

	usleep_range(100, 110);

	mutex_unlock(&priv->lock);

	dev_dbg(dev, "[cwd]dser rd reg: 0x%04x, 0x%02x = 0x%02x\n", i2c_addr, addr, value);

	return err;
}
EXPORT_SYMBOL(max9296a_read_reg_with_addr);

int max9296a_write_seri_reg(struct device *dev, u8 link_id, u16 addr, u8 val)
{
	struct max9296a *priv = dev_get_drvdata(dev);
	return max9296a_write_reg_with_addr(dev, TO_SERI_ADDR(link_id), addr, val);
}
EXPORT_SYMBOL(max9296a_write_seri_reg);

int max9296a_write_seri_tab(struct device *dev, u8 link_id, struct max_reg_pair buf[], u16 size)
{
	struct max9296a *priv = dev_get_drvdata(dev);
	return max9296a_write_tab_with_addr(dev, TO_SERI_ADDR(link_id), buf, size);
}
EXPORT_SYMBOL(max9296a_write_seri_tab);

int max9296a_write_sensor(struct device *dev, u8 link_id, void *data, u16 length)
{
	struct max9296a *priv = dev_get_drvdata(dev);
	struct i2c_msg send_msg = {
		.addr = TO_SNR_ADDR(link_id),
		.flags = 0,
		.buf = (uint8_t*)data,
		.len = length,
	};
	int error;

	mutex_lock(&priv->lock);
	error = i2c_transfer(priv->i2c_client->adapter, &send_msg, 1);
	mutex_unlock(&priv->lock);
	if (error != 1) {
		dev_err(dev, "\n i2c_transfer error \n");
		return -1;
	}

	// dev_info(dev, "[snr] i2c wr: %x, %d\n", send_msg.addr, length);

	return 0;
}
EXPORT_SYMBOL(max9296a_write_sensor);

int max9296a_read_sensor(struct device *dev, u8 link_id, void *data, u16 length)
{
	struct max9296a *priv = dev_get_drvdata(dev);
	struct i2c_msg recv_msg = {
		.addr = TO_SNR_ADDR(link_id),
		.flags = I2C_M_RD,
		.buf = data,
		.len = length,
	};
	int error;

	mutex_lock(&priv->lock);
	error = i2c_transfer(priv->i2c_client->adapter, &recv_msg, 1);
	// dev_info(dev, "orb: i2c read: %x, %d\n", client->addr, length);
	mutex_unlock(&priv->lock);
	if (error != 1) {
		dev_err(dev, "\n i2c_read_orbbec error \n");
		return -1;
	}

	return 0;
}
EXPORT_SYMBOL(max9296a_read_sensor);

static int max9296a_write_reg(struct device *dev, u16 addr, u8 val)
{
	struct max9296a *priv = dev_get_drvdata(dev);

	int err = regmap_write(priv->regmap, addr, val);
	if (err)
		dev_err(dev, "%s err: 0x%04x = 0x%02x\n", __func__, addr, val);

	/* delay before next i2c command as required for SERDES link */
	usleep_range(100, 110);

	dev_dbg(dev, "[dser] 0x%02x i2c wr: 0x%04x = 0x%02x\n", priv->i2c_client->addr, addr, val);

	return err;
}

static int max9296a_read_reg(struct device *dev, u16 addr, u8 *val)
{
	struct max9296a *priv = dev_get_drvdata(dev);
	unsigned int value;

	int err = regmap_read(priv->regmap, addr, &value);
	if (err)
		dev_err(dev, "%s err: 0x%04x\n", __func__, addr);

	*val = value&0xff;

	// delay before next i2c command as required for SERDES link 
	usleep_range(100, 110);

	dev_dbg(dev, "[dser] 0x%02x  i2c rd: 0x%04x = 0x%02x\n", priv->i2c_client->addr, addr, value);

	return err;
}

static int max9296a_set_registers(struct device *dev, struct max_reg_pair *map, u16 count)
{
	int err = 0;
	u16 i;

	for (i = 0; i < count; i++) {
		err = max9296a_write_reg(dev, map[i].addr, map[i].val);
		if (err != 0)
			break;
	}

	return err;
}

#if 0
static int max9296a_write_table(struct device *dev, const struct index_reg_8 table[], u16 size)
{
	int ret, retry = 5, i = 0;

	for (i = 0; i < size; i++) {
		ret = _max9296a_write_reg_with_addr(dev, table[i].source, table[i].addr, table[i].val);
		if (ret) {
			retry--;
			if (retry == 0)
				return -1;
			dev_warn(dev, "max9296a wr tab: retry = %d\n", retry);
			msleep(20);
			continue;
		}
		if (0x0000 == table[i].addr || 0x0010 == table[i].addr)
			msleep(200);
		retry = 5;
	}
	return 0;
}
#endif

static int max9295d_set_src_id(struct device *dev, int index)
{
    u8 tx_src_id[] = {
        0x6B, 0x10, //what?
        0x73, 0x11, //what?

        0x7B, 0x30, //CFGI INFOFR
        0x83, 0x30, //CFGL SPI
        0x93, 0x30, //CFGL GPIO
        0x9B, 0x30, 
        0xA3, 0x30, //CFGL IIC_X
        0xAB, 0x30, //CFGL IIC_Y
        0x8B, 0x30, //CFGC CC
    };

    int i, err;
	struct max9296a *priv = dev_get_drvdata(dev);

    for (i = 0; i < ARRAY_SIZE(tx_src_id); i += 2) {
        /* update address overrides */
		tx_src_id[i+1] += index;
		err = _max9296a_write_reg_with_addr(dev, TO_SERI_ADDR(index), tx_src_id[i], tx_src_id[i+1]);
        if(err)
            break;
    }

    dev_info(dev, "%s: done, err=%d\n", __func__, err);

    return err;
}

u8 max9296a_get_link_map(struct device *dev)
{
	struct max9296a *priv = dev_get_drvdata(dev);
	return priv->links_map;
}
EXPORT_SYMBOL(max9296a_get_link_map);

u8 max9296a_get_link_init_flag(struct device *dev)
{
	struct max9296a *priv = dev_get_drvdata(dev);
	return priv->links_init_flag;
}
EXPORT_SYMBOL(max9296a_get_link_init_flag);

void max9296a_set_link_init_flag(struct device *dev, u8 link)
{
	struct max9296a *priv = dev_get_drvdata(dev);
	priv->links_init_flag &= ~(1 << link);
}
EXPORT_SYMBOL(max9296a_set_link_init_flag);

#if 1
#if 0
/* Video channel is locked and outputting valid video data */
u8 max9296a_check_video_channel_lock(struct device *dev)
{
	struct max9296a *priv = dev_get_drvdata(dev);
	u16 addr_tab[8] = {0x01DC, 0x01FC, 0x021C, 0x023C}; //, 0x025C, 0x027C, 0x029C, 0x02BC}; //pipe 0~7
	int i, ret;
	u8 val = 0, ch_lock = 0;

	mutex_lock(&priv->lock);
	for (i = 0; i < MAX9296_MAX_PIPES; i++) {
		val = 0;
		ret = max9296a_read_reg(dev, addr_tab[i], &val); //pipe 0~7
		if(ret)
			break;
		if(val&0x1) //bit0
			ch_lock |= (1<<i);
	}
    mutex_unlock(&priv->lock);

	if(ret)
		ch_lock = 0;

	dev_info(dev, "check channel lock: 0x%x\n", ch_lock);

	return ch_lock;
}
EXPORT_SYMBOL(max9296a_check_video_channel_lock);
#endif

u8 max9296a_check_pipe_lock(struct device *dev)
{
	struct max9296a *priv = dev_get_drvdata(dev);
	int i, ret;
	u16 addr_tb[] = {0x108, 0x11A, 0x12C, 0x13E}; //, 0x150, 0x168, 0x17A, 0x18C}; //pipe 0~3
	u8 val = 0, pipe_lock = 0;

	mutex_lock(&priv->lock);
	for (i = 0; i < MAX9296_MAX_PIPES; i++) {
		val = 0;
		ret = max9296a_read_reg(dev, addr_tb[i], &val); //video pipeline locked 0~3
		if(ret)
			break;
		if(val&0x40) //bit6
			pipe_lock |= (1<<i);
	}
    mutex_unlock(&priv->lock);

	if(ret)
		pipe_lock = -1;

	dev_info(dev, "check pipe lock map: 0x%02x\n", pipe_lock);

	return pipe_lock;
}
EXPORT_SYMBOL(max9296a_check_pipe_lock);
#endif

#define MAX9296_CSI_OUT_EN_ADDR  0x313
#define CSI_OUT_EN  0x02
#define CSI_OUT_DIS 0x00

/* transform serializer i2c address */
static int max9296a_seri_i2c_addr_trans(struct device *dev)
{
	struct max9296a *priv = dev_get_drvdata(dev);
	int i, ret;
	u8 links = 0;

	mutex_lock(&priv->lock);

	for (i = 0; i < MAX9296_MAX_LINKS; i++) { //link A~D
		ret = max9296a_write_reg(dev, MAX9296_CTRL0_ADDR, 1<<i); //select link A / B
		if(ret)
			break;
		ret = max9296a_write_reg(dev, MAX9296_CTRL0_ADDR, (1<<i)|CTRL0_RESET_ONESHOT);
		if(ret)
			break;

		msleep(200);

		ret = _max9296a_write_reg_with_addr(dev, SERI_DEFAULT_I2C_ADDRESS, 0x00, TO_SERI_ADDR(i)<<1);
		if(ret) {
			ret = _max9296a_write_reg_with_addr(dev,  TO_SERI_ADDR(i), 0x00, TO_SERI_ADDR(i)<<1);
			if(ret)
				continue;
		}

		max9295d_set_src_id(dev, i);
		links |= 1<<i;
		dev_info(dev, "transf seri i2c addr, 0x%02x, 0x40 -> 0x%02x\n", i, TO_SERI_ADDR(i));
	}
	priv->links_map = links;
	priv->links_init_flag = links;
	if(links == 0x3) {
		max9296a_write_reg(dev, MAX9296_CTRL0_ADDR, CTRL0_RESET_ONESHOT|CTRL0_LINK_SPLITTER_MODE);
		dev_info(dev, "spiliter mode enable \n");
		msleep(100);
	}
 	else {
		max9296a_write_reg(dev, MAX9296_CTRL0_ADDR, 0x31);  //autolink mode
		msleep(100);
	}

	mutex_unlock(&priv->lock);

	return 0; //ret;
}


#define ORBBEC_POWER_GPIOA_ADDR 0x2D6
#define ORBBEC_POWER_GPIOB_ADDR 0x2D7

static int max9296a_set_sensor_on(struct device *dev, int link_id)
{
	struct max9296a *priv = dev_get_drvdata(dev);
	int ret = _max9296a_write_reg_with_addr(dev, TO_SERI_ADDR(link_id), ORBBEC_POWER_GPIOA_ADDR, 0x00);
	if (ret)
		dev_err(dev, "%s:set_sensor_on fail\n", __func__);

	return ret;
}

static int max9296a_set_sensor_off(struct device *dev, int link_id)
{
	struct max9296a *priv = dev_get_drvdata(dev);
	int ret = _max9296a_write_reg_with_addr(dev, TO_SERI_ADDR(link_id), ORBBEC_POWER_GPIOA_ADDR, 0x10);
	if (ret)
		dev_err(dev, "%s:set_sensor_off fail\n", __func__);

	return ret;
}

/* set sensor proxy address */
static int max9296a_set_snr_proxy_addr(struct device *dev)
{
	struct max9296a *priv = dev_get_drvdata(dev);
	int i, err = 0;
    if(priv->links_map == 0) return err;
	mutex_lock(&priv->lock);
	for (i = 0; i < MAX9296_MAX_LINKS; i++) { //link A~D
		if(priv->links_map & (1<<i)) {
			err  = _max9296a_write_reg_with_addr(dev, TO_SERI_ADDR(i), 0x0042, (priv->snr_proxy_addr + i)<<1);
			err += _max9296a_write_reg_with_addr(dev, TO_SERI_ADDR(i), 0x0043, priv->snr_real_addr<<1);
			if(err) {
				dev_err(dev, "max9296a set_snr_proxy_addr failed %d\n", err);
				break;
			}
			err = max9296a_set_sensor_off(dev, i); //power off
			if(err) {
				dev_err(dev, "max9296a set_sensor_off failed %d\n", err);
				break;
			}
			msleep(800); //At least 800 milliseconds
		}
	}
	mutex_unlock(&priv->lock);
#if 1
	msleep_range(3000);

	/* power on */
	mutex_lock(&priv->lock);
	for (i = 0; i < MAX9296_MAX_LINKS; i++) { //link A~D
		if(priv->links_map & (1<<i)) {
			err = max9296a_set_sensor_on(dev, i);
			if(err) {
				dev_err(dev, "max9296a set_sensor_on failed %d\n", err);
				break;
			}
			msleep_range(100);
		}
	}
	mutex_unlock(&priv->lock);

	msleep_range(3000);
#endif
	return err;
}

static int __max9296a_set_pipe(struct device *dev, u8 link_id, u8 pipe_id, 
				u8 data_type1, u8 data_type2, u8 src_vc_id, u8 dst_vc_id)
{
	struct max9296a *priv = dev_get_drvdata(dev);
	int i, err;
	u8 mapping2csi = 0x55, mappings_en = 0x0F;

	struct max_reg_pair map_pipe_control[] = {
		/* Enable 4 mappings for Pipe X */
		{MAX9296_TX11_PIPE_X_EN_ADDR,   0x0F}, //0x40B
		/* Map data_type1 on vc_id */
		{MAX9296_PIPE_X_SRC_0_MAP_ADDR, 0x1E}, //0x40D
		{MAX9296_PIPE_X_DST_0_MAP_ADDR, 0x1E},
		/* Map frame_start on vc_id */
		{MAX9296_PIPE_X_SRC_1_MAP_ADDR, 0x00}, //0x40F
		{MAX9296_PIPE_X_DST_1_MAP_ADDR, 0x00},
		/* Map frame end on vc_id */
		{MAX9296_PIPE_X_SRC_2_MAP_ADDR, 0x01}, //0x411
		{MAX9296_PIPE_X_DST_2_MAP_ADDR, 0x01},
		/* Map data_type2 on vc_id */
		{MAX9296_PIPE_X_SRC_3_MAP_ADDR, 0x12}, //0x413
		{MAX9296_PIPE_X_DST_3_MAP_ADDR, 0x12},
		/* All mappings to PHY1 (master for port A) */
		{MAX9296_TX45_PIPE_X_DST_CTRL_ADDR, 0x55}, //0x42D
		/* Disable “Heartbeat” Mode, I's use for DSI */
		{0x0100, 0x23}, //0x23, SEQ_MISS_EN=0, LINE_CRC_EN=0, DIS_PKT_DET=1
	};

	for (i = 0; i < 10; i++)
		map_pipe_control[i].addr += 0x40 * pipe_id;
	map_pipe_control[10].addr += 0x12 * pipe_id;

	if(priv->csi_mode == MAX9296_CSI_MODE_2X4)
		mapping2csi = (link_id > 1) ? 0xAA : 0x55;
	else
		mapping2csi = 0x55*link_id;

	if(data_type2 == 0) {
		mapping2csi &= 0x3f;
		mappings_en = 0x07;
	}

	map_pipe_control[0].val = mappings_en;
	map_pipe_control[1].val = (src_vc_id << 6) | data_type1;
	map_pipe_control[2].val = (dst_vc_id << 6) | data_type1;
	map_pipe_control[3].val = (src_vc_id << 6) | 0x00;
	map_pipe_control[4].val = (dst_vc_id << 6) | 0x00;
	map_pipe_control[5].val = (src_vc_id << 6) | 0x01;
	map_pipe_control[6].val = (dst_vc_id << 6) | 0x01;
	map_pipe_control[7].val = (src_vc_id << 6) | data_type2;
	map_pipe_control[8].val = (dst_vc_id << 6) | data_type2;
	map_pipe_control[9].val = mapping2csi;
	map_pipe_control[10].val = 0x23;

	dev_dbg(dev, "set pipe start\n");

	err = max9296a_set_registers(dev, map_pipe_control, ARRAY_SIZE(map_pipe_control));

	dev_dbg(dev, "set pipe %s!\n", err ? "failed" : "success");

	return err;
}

int max9296a_set_pipe(struct device *dev, u8 link_id, u8 pipe_id,
		     u8 data_type1, u8 data_type2, u8 src_vc_id, u8 dst_vc_id)
{
	struct max9296a *priv = dev_get_drvdata(dev);
	int err = 0;
	// u8 pipe_lock = 0;

	if (pipe_id > (MAX9296_MAX_PIPES - 1)) {
		dev_err(dev, "%s, input pipe_id: %d exceed max9296a max pipes\n", __func__, pipe_id);
		return -EINVAL;
	}

	dev_info(dev, "%s pipe_id:%d, dt1:0x%x, dt2:0x%x, src_vc:0x%x, dst_vc:0x%x\n",
		__func__, pipe_id, data_type1, data_type2, src_vc_id, dst_vc_id);

	mutex_lock(&priv->lock);

	err = __max9296a_set_pipe(dev, link_id, pipe_id, data_type1, data_type2, src_vc_id, dst_vc_id);

	mutex_unlock(&priv->lock);

	// msleep(10);
	// pipe_lock = max9296a_check_pipe_lock(dev);
	// dev_info(dev, "%s pipe lock: 0x%x\n", __func__, pipe_lock);

	return err;
}
EXPORT_SYMBOL(max9296a_set_pipe);

int max9296a_init_settings(struct device *dev)
{
	struct max9296a *priv = dev_get_drvdata(dev);
	int err = 0;

	struct max_reg_pair map_phy_opt_2x4[] = //2 x 4 Lane
	{
		// {0x1458, 0x28}, // PHY A Optimization
		// {0x1459, 0x68}, // PHY A Optimization
		// {0x1558, 0x28}, // PHY B Optimization
		// {0x1559, 0x68}, // PHY B Optimization
		
		/* MIPI D-PHY Config */
		// {MAX9296_DST_CSI_MODE_ADDR, (MAX9296_CSI_MODE_2X4 | 0x20)}, //0x8A0
		// {MAX9296_LANE_MAP1_ADDR, 0xE4}, //0x8A3, Default 4x2 lane mapping
		// {MAX9296_LANE_MAP2_ADDR, 0xE4}, //0x8A4, Default 4x2 lane mapping

		/* 4 lanes on port A,  0x40 -> 2 lanes, 0xC0 -> 4 lanes */
		// {MAX9296_LANE_CTRL0_ADDR, 0xC0}, //0x90A, MIPI PHY0
		{MAX9296_LANE_CTRL1_ADDR, 0xD0}, //0x94A, MIPI PHY1, as Master controller for A 4lane
		// {MAX9296_LANE_CTRL2_ADDR, 0xC0}, //0x98A, MIPI PHY2, as Master controller for B 4lane
		// {MAX9296_LANE_CTRL3_ADDR, 0xC0}, //0x9CA, MIPI PHY3

		/* 1500Mbps/lane on port A */
		// {MAX9296_PHY0_CLK_ADDR, 0x2F}, //0x415, MIPI PHY0
		{MAX9296_PHY1_CLK_ADDR, 0x2F}, //0x418, MIPI PHY1, this
		// {MAX9296_PHY2_CLK_ADDR, 0x2F}, //0x41B, MIPI PHY2, this
		// {MAX9296_PHY3_CLK_ADDR, 0x2F}, //0x41E, MIPI PHY3

		/* Do not un-double 8bpp (Un-double 8bpp data) */
		//{0x031C, 0xF0}, //Pipe0~3, Process BPP = 8 as 16-bit color
		//{0x041F, 0xF0}, //Enable 8-bit write alternate map to RAMs for pipeline X~U
	
		/* 0x02: ALT_MEM_MAP8, 0x10: ALT2_MEM_MAP8 */
		// {0x0433, 0x10}, //MIPI TX0
		{0x0473, 0x10}, //0x10, MIPI TX1
		// {0x04B3, 0x10}, //0x10, MIPI TX2
		// {0x04F3, 0x10}, //MIPI TX3

		{MAX9296_PWDN_PHYS_ADDR, ALLPHYS_NOSTDBY}, //0x8A2, All PHYs not in standby
		{MAX9296_CSI_OUT_EN_ADDR, CSI_OUT_EN},  // (CSI_OUT_EN): CSI output enabled
	};

	mutex_lock(&priv->lock);

	if(priv->csi_mode == MAX9296_CSI_MODE_2X4)
		err |= max9296a_set_registers(dev, map_phy_opt_2x4, ARRAY_SIZE(map_phy_opt_2x4));

	mutex_unlock(&priv->lock);

	if (err == 0)
		dev_info(dev, "%s done\n", __func__);
	else
		dev_err(dev, "%s failed, err %d\n", __func__, err);

	return err;
}
EXPORT_SYMBOL(max9296a_init_settings);

int max9296a_init_tx_gpio(struct device *dev)
{
	struct max9296a *priv = dev_get_drvdata(dev);
	int err = 0;

	struct max_reg_pair map_disable_uart1[] = {
		{0x0003, 0x40}, // LOCK_CFG[7]: 0->GMSL2 link locked, 1->GMSL2 link locked and MIPI output started; UART_1_EN[4]:0->disable, 1->enable
		{0x0B08, 0x20},
	};
	struct max_reg_pair map_fsync_trigger[] = {
		/* MFP10 SYNC IN, Tx*/
		{0x02CE, 0x83}, // RES_CFG[7]: 0->40K, 1->1M; GPIO_RX_EN[2]; GPIO_TX_EN[1], GPIO_OUT[4]; GPIO_IN[3]; GPIO_OUT_DIS[0]: 1->disable
		{0x02CF, 0xA2}, // PULL_UPDN_SEL[7:6]: 0->no pullup, 1->Pullup, 2->Pulldown; OUT_TYPE[5]: 1->Push-pull, 0->Open-drain; GPIO_TX_ID[4:0]
	};

	struct max_reg_pair map_pps_trigger[] = {
		/* MFP9 pps, TX */
		{0x02CB, 0x83}, // RES_CFG[7]: 0->40K, 1->1M; GPIO_RX_EN[2]; GPIO_TX_EN[1], GPIO_OUT[4]; GPIO_IN[3]; 
		{0x02CC, 0x1D}, // PULL_UPDN_SEL[7:6]: 0->no pullup, 1->Pullup, 2->Pulldown; OUT_TYPE[5]: 1->Push-pull, 0->Open-drain; GPIO_TX_ID[4:0]
	};

	mutex_lock(&priv->lock);

	// FSYNC Trigger
	if(priv->fsync_mfp_x == 5 || priv->fsync_mfp_x == 6 || priv->pps_mfp_x == 5 || priv->pps_mfp_x == 6)
		err |= max9296a_set_registers(dev, map_disable_uart1,
				     ARRAY_SIZE(map_disable_uart1));

	map_fsync_trigger[0].addr = 0x2B0 + priv->fsync_mfp_x * 3;
	map_fsync_trigger[1].addr = map_fsync_trigger[0].addr + 1;
	err |= max9296a_set_registers(dev, map_fsync_trigger,
				     ARRAY_SIZE(map_fsync_trigger));
	
	// PPS Trigger
	map_pps_trigger[0].addr = 0x2B0 + priv->pps_mfp_x * 3;
	map_pps_trigger[1].addr = map_pps_trigger[0].addr + 1;
	err |= max9296a_set_registers(dev, map_pps_trigger,
				     ARRAY_SIZE(map_pps_trigger));

	mutex_unlock(&priv->lock);

	if (err == 0)
		dev_info(dev, "%s done\n", __func__);
	else
		dev_err(dev, "%s failed, err %d\n", __func__, err);

	return err;
}
EXPORT_SYMBOL(max9296a_init_tx_gpio);

int max9296a_reset_dev(struct device *dev)
{
	struct max9296a *priv = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&priv->lock);
	ret = max9296a_write_reg(dev, MAX9296_CTRL0_ADDR, CTRL0_RESET_ALL); //reset all
	mutex_unlock(&priv->lock);

	msleep(100); /* delay to settle reset */

	return ret;
}
EXPORT_SYMBOL(max9296a_reset_dev);

int max9296a_get_link_state(struct device *dev, u8 link_id, int *value)
{
	struct max9296a *priv = dev_get_drvdata(dev);
	int ret;
	u8 val = 0;

	mutex_lock(&priv->lock);
	ret = max9296a_read_reg(dev, MAX9296_CTRL3_ADDR, &val);
	if (ret)
		dev_err(dev, "%s:failed\n", __func__);
	*value = (val & CTRL3_LINK_LOCKED) ? 1 : 0; //b[3]: gmsl2  link locked, for linkA & B
	mutex_unlock(&priv->lock);

	dev_info(dev, "max9296a register 0x%04x= 0x%02x\n", MAX9296_CTRL3_ADDR, val);

	return ret;
}
EXPORT_SYMBOL(max9296a_get_link_state);


int max9296a_get_available_pipe_id(struct device *dev, int dst_vc_id)
{
	struct max9296a *priv = dev_get_drvdata(dev);
	int pipe_id = -1;

	mutex_lock(&priv->lock);

#if 0
	for (int i = 0; i < MAX9296_MAX_PIPES; i++) {
		if(!(priv->pipes_map & (1<<i))) {
			priv->pipes_map |= (1<<i);
			pipe_id = i;
			break;
		}
	}
#else
	if(!(priv->pipes_map & (1<<dst_vc_id))) {
		priv->pipes_map |= (1<<dst_vc_id);
		pipe_id = dst_vc_id;
	}
#endif

	mutex_unlock(&priv->lock);

	dev_info(dev, "request pipe id: map=0x%02x, id=%d\n", priv->pipes_map, pipe_id);

	return pipe_id;
}
EXPORT_SYMBOL(max9296a_get_available_pipe_id);

int max9296a_release_pipe(struct device *dev, u8 link_id, u8 pipe_id)
{
#if 1
	struct max9296a *priv = dev_get_drvdata(dev);

	if (link_id >= MAX9296_MAX_LINKS || pipe_id < 0 || pipe_id >= MAX9296_MAX_PIPES)
		return -EINVAL;

	// mutex_lock(&priv->lock);

	// max9296a_write_reg(dev, MAX9296_CTRL0_ADDR, CTRL0_RESET_ONESHOT); //link reset oneshot

	priv->pipes_map &= ~(1<<pipe_id);

	// mutex_unlock(&priv->lock);

	dev_info(dev, "release pipe id, 0x%02x\n", priv->pipes_map);
#endif

	return 0;
}
EXPORT_SYMBOL(max9296a_release_pipe);

int max9296a_reset_oneshot(struct device *dev)
{
	struct max9296a *priv = dev_get_drvdata(dev);
	int err = 0;

	mutex_lock(&priv->lock);

	dev_info(dev, "%s not implemented\n", __func__);
	// err = max9296a_write_reg(dev, MAX9296_LINK_RESET_ADDR, 0x21); //0x10 -> link-A oneshot reset
	// msleep(100);
	// err = max9296a_write_reg(dev, MAX9296_LINK_RESET_ADDR, 0x22); //0x10 -> link-B oneshot reset
	// msleep(100);
	// err = max9296a_write_reg(dev, MAX9296_LINK_RESET_ADDR, 0x23); //0x10 -> all link oneshot reset
	// msleep(100);

	mutex_unlock(&priv->lock);

	return err;
}
EXPORT_SYMBOL(max9296a_reset_oneshot);

static int max9296a_stats_show(struct seq_file *s, void *data)
{
	return 0;
}

static int max9296a_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, max9296a_stats_show, inode->i_private);
}

static ssize_t max9296a_debugfs_write(struct file *s,
				const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct max9296a *priv = ((struct seq_file *)s->private_data)->private;
	struct i2c_client *i2c_client = priv->i2c_client;

	char buf[255];
	int buf_size;
	// u8 val = 0;

	if (!user_buf || count <= 1)
		return -EFAULT;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	if (buf[0] == 'd') {
		dev_info(&i2c_client->dev, "%s, set daymode\n", __func__);
		// max9296a_read_reg(&i2c_client->dev, 0x0010, &val);
		return count;
	}

	if (buf[0] == 'n') {
		dev_info(&i2c_client->dev, "%s, set nightmode\n", __func__);
		return count;
	}

	return count;
}


static const struct file_operations max9296a_debugfs_fops = {
	.open = max9296a_debugfs_open,
	.read = seq_read,
	.write = max9296a_debugfs_write,
	.llseek = seq_lseek,
	.release = single_release,
};

int max9296a_power_on(struct device *dev)
{
	struct max9296a *priv = dev_get_drvdata(dev);

	if (priv->reset_gpio > 0) {

		gpio_direction_output(priv->reset_gpio, 1);

		if(gpio_cansleep(priv->reset_gpio))
			gpio_set_value_cansleep(priv->reset_gpio, 0);
		else
			gpio_set_value(priv->reset_gpio, 0);
	
		usleep_range(30, 50);

		if(gpio_cansleep(priv->reset_gpio))
			gpio_set_value_cansleep(priv->reset_gpio, 1);
		else
			gpio_set_value(priv->reset_gpio, 1);

		msleep(100);
		dev_info(dev, "%s\n", __func__);
	}

	return 0;
}
EXPORT_SYMBOL(max9296a_power_on);

void max9296a_power_off(struct device *dev)
{
	struct max9296a *priv = dev_get_drvdata(dev);

	if (priv->reset_gpio > 0) {
		if(gpio_cansleep(priv->reset_gpio))
			gpio_set_value_cansleep(priv->reset_gpio, 0);
		else
			gpio_set_value(priv->reset_gpio, 0);
		dev_info(dev, "%s\n", __func__);
	}
}
EXPORT_SYMBOL(max9296a_power_off);

static int max9296a_debugfs_init(const char *dir_name,
				struct dentry **d_entry,
				struct dentry **f_entry,
				struct max9296a *priv)
{
	struct dentry  *dp, *fp;
	char dev_name[20];
	struct i2c_client *i2c_client = priv->i2c_client;
	int err = 0;

	err = snprintf(dev_name, sizeof(dev_name), "max9296a_%d", priv->index);
	if (err < 0)
		return -EINVAL;

	dp = debugfs_create_dir(dev_name, NULL);
	if (dp == NULL) {
		dev_err(&i2c_client->dev, "%s: debugfs create dir failed\n", __func__);
		return -ENOMEM;
	}

	fp = debugfs_create_file("max9296a", 0644, dp, priv, &max9296a_debugfs_fops);
	if (!fp) {
		dev_err(&i2c_client->dev, "%s: debugfs create file failed\n", __func__);
		debugfs_remove_recursive(dp);
		return -ENOMEM;
	}

	if (d_entry)
		*d_entry = dp;
	if (f_entry)
		*f_entry = fp;

	return 0;
}

static const struct of_device_id max9296a_of_match[] = {
	{ .compatible = "maxim,obc_max9296", },
	{ },
};
MODULE_DEVICE_TABLE(of, max9296a_of_match);

static int max9296a_parse_dt(struct max9296a *priv, struct i2c_client *client)
{
	struct device_node *node = client->dev.of_node;
	const struct of_device_id *match;
	const char *str_value;
	int value, err = 0;

	if (!node)
		return -EINVAL;

	match = of_match_device(max9296a_of_match, &client->dev);
	if (!match) {
		dev_err(&client->dev, "Failed to find matching dt id\n");
		return -EFAULT;
	}

	err = of_property_read_u32(node, "index", &value);
	if (err)
		dev_err(&client->dev, "index not found\n");

	if(value >= 0 && value < 4)
		priv->index = value;
	else
		priv->index = 0;

	dev_dbg(&client->dev, "%s: index %d\n", __func__, priv->index);

	err = of_property_read_string(node, "csi-mode", &str_value);
	if (err < 0) {
		dev_err(&client->dev, "csi-mode property not found\n");
		return err;
	}

	priv->deser_addr = client->addr;
	if (!strcmp(str_value, "2x4")) {
		priv->csi_mode = MAX9296_CSI_MODE_2X4;
	} else if (!strcmp(str_value, "4x2")) {
		priv->csi_mode = MAX9296_CSI_MODE_4X2;
	} else {
		dev_err(&client->dev, "invalid csi mode\n");
		return -EINVAL;
	}

	err = of_property_read_u32(node, "seri-addr", &value);
	if (err)
		dev_err(&client->dev, "seri-addr not found\n");
	priv->seri_start_addr = value;

	err = of_property_read_u32(node, "proxy-addr", &value);
	if (err)
		dev_err(&client->dev, "proxy-addr not found\n");
	priv->snr_proxy_addr = value;

	err = of_property_read_u32(node, "real-addr", &value);
	if (err)
		dev_err(&client->dev, "real-addr not found\n");
	priv->snr_real_addr = value;

	err = of_property_read_u32(node, "fsync_mfp_index", &value);
	if (err < 0) {
		priv->fsync_mfp_x = 10;
		dev_err(&client->dev, "No fsync_mfp_index info\n");
	} else {
		priv->fsync_mfp_x = value;
	}

	err = of_property_read_u32(node, "pps_mfp_index", &value);
	if (err < 0) {
		priv->pps_mfp_x = 9;
		dev_err(&client->dev, "No pps_mfp_index info\n");
	} else {
		priv->pps_mfp_x = value;
	}

	priv->reset_gpio = of_get_named_gpio(node, "reset-gpios", 0);
	if (priv->reset_gpio < 0)
		dev_err(&client->dev, "reset-gpios not found %d\n", err);

	return 0;
}

static struct regmap_config max9296a_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.cache_type = REGCACHE_NONE, // REGCACHE_RBTREE,
};

#if defined(NV_I2C_DRIVER_STRUCT_PROBE_WITHOUT_I2C_DEVICE_ID_ARG) /* Linux 6.3 */
static int max9296a_probe(struct i2c_client *client)
#else
static int max9296a_probe(struct i2c_client *client, const struct i2c_device_id *id)
#endif
{
	struct max9296a *priv;
	struct device_node *node = client->dev.of_node;
	int err = 0;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	priv->i2c_client = client;
	priv->regmap = devm_regmap_init_i2c(priv->i2c_client, &max9296a_regmap_config);
	if (IS_ERR(priv->regmap)) {
		dev_err(&client->dev, "regmap init failed: %ld\n", PTR_ERR(priv->regmap));
		return -ENODEV;
	}
	dev_set_drvdata(&client->dev, priv);

	err = max9296a_parse_dt(priv, client);   //解析设备树
	if (err) {
		dev_err(&client->dev, "parse devicetree error, %d\n", err);
		return -EFAULT;
	}

	mutex_init(&priv->lock);

	max9296a_power_off(&client->dev);
	msleep_range(100);
	err = max9296a_power_on(&client->dev);
	if (err) {
		dev_err(&client->dev, "power on failed, %d\n", err);
		return err;
	}
	msleep_range(200);

	if (of_get_property(node, "is-fg96-2ch", NULL)) {
		//Enable POC
		max9296a_write_reg(&client->dev, 0x0005, 0x80);
		max9296a_write_reg(&client->dev, 0x02BC, 0x80); //Enable MFP4 Output low
		max9296a_write_reg(&client->dev, 0x02BD, 0x84); //Enable MFP4 Output Pulldown 
		
	}

	if (of_get_property(node, "is-fg96-8ch-v2.1", NULL)) {
		//Enable POC
		max9296a_write_reg(&client->dev, 0x0005, 0x80);
		max9296a_write_reg(&client->dev, 0x02BC, 0x90); //Enable MFP4 Output low
	}
	
	err = max9296a_debugfs_init(NULL, NULL, NULL, priv);
	if (err)
		return err;

	/* Serializer i2c address trans */
	err = max9296a_seri_i2c_addr_trans(&client->dev);
	if(err) {
		dev_err(&client->dev, "Serializer i2c address change failed, %d\n", err);
		return 0; //err;
	}

	err = max9296a_init_settings(&client->dev);
	if(err) {
		dev_err(&client->dev, "max9296a init settings failed, %d\n", err);
		return 0; //err;
	}

	err = max9296a_set_snr_proxy_addr(&client->dev);
	if(err) {
		dev_err(&client->dev, "max9296a set_snr_proxy_addr failed, %d\n", err);
		return 0; //err;
	}

	/*set daymode by fault*/
	dev_info(&client->dev, "%s: success\n", __func__);

	return 0; //err;
}

#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
static int max9296a_remove(struct i2c_client *client)
#else
static void max9296a_remove(struct i2c_client *client)
#endif
{
	struct max9296a *priv;

	if (client != NULL) {
		priv = dev_get_drvdata(&client->dev);
		mutex_destroy(&priv->lock);
		//i2c_unregister_device(client);
		client = NULL;
	}
#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
	return 0;
#endif
}

static const struct i2c_device_id max9296a_id[] = {
	{ "obc_max9296", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, max9296a_id);

static struct i2c_driver max9296a_i2c_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "obc_max9296",
		.of_match_table = of_match_ptr(max9296a_of_match),
	},
	.probe = max9296a_probe,
	.remove = max9296a_remove,
	.id_table = max9296a_id,
};

module_i2c_driver(max9296a_i2c_driver);

MODULE_DESCRIPTION("IO Expander driver max9296a");
MODULE_AUTHOR("NVIDIA Corporation");
MODULE_LICENSE("GPL v2");