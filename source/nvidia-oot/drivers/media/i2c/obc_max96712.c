// SPDX-License-Identifier: GPL-2.0
/*
 * max96712.c - max96712 IO Expander driver
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

#include <media/obc_max96712.h>



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
#define MAX96712_DST_CSI_MODE_ADDR 0x8A0 //MIPI PHY Mode Select Register: 2x4 mode, 1x4 mode, 4x2 mode

#define MAX96712_PWDN_PHYS_ADDR  0x8A2  //MIPI PHY Enable Register: MIPI PHY 0~3, 0 = PHY in standby, 1 = PHY enabled

#define MAX96712_LANE_MAP1_ADDR  0x8A3 //MIPI PHY 0&1 Lane Mapping Register
#define MAX96712_LANE_MAP2_ADDR  0x8A4 //MIPI PHY 3&2 Lane Mapping Register
#define MAX96712_LANE_POLA1_ADDR 0x8A5 //MIPI PHY 0&1 Lane Polarity Register
#define MAX96712_LANE_POLA2_ADDR 0x8A6 //MIPI PHY 3&2 Lane Polarity Register

#define MAX96712_LANE_CTRL0_ADDR 0x90A //MIPI PHY 0 Lane Count / C-PHY enable Register
#define MAX96712_LANE_CTRL1_ADDR 0x94A //MIPI PHY 1 Lane Count / C-PHY enable Register
#define MAX96712_LANE_CTRL2_ADDR 0x98A //MIPI PHY 2 Lane Count / C-PHY enable Register
#define MAX96712_LANE_CTRL3_ADDR 0x9CA //MIPI PHY 3 Lane Count / C-PHY enable Register

#define MAX96712_PHY0_CLK_ADDR 	 0x415 //MIPI PHY 0 DPLL Freq Register
#define MAX96712_PHY1_CLK_ADDR 	 0x418 //MIPI PHY 1 DPLL Freq Register
#define MAX96712_PHY2_CLK_ADDR 	 0x41B //MIPI PHY 2 DPLL Freq Register
#define MAX96712_PHY3_CLK_ADDR 	 0x41E //MIPI PHY 3 DPLL Freq Register

#define MAX96712_CSI_OUT_EN      0x40B //CSI_OUT_EN: CSI output enabled, en:0x2, dis:0x0

/*-----------------------------------------------------------------------------------------------
 * 3. Video Pipe to MIPI Controller Mapping
 *----------------------------------------------------------------------------------------------*/
#define MAX96712_TX11_PIPE_X_EN_ADDR   0x90B //Mapping Enable Low Byte, 0~7
#define MAX96712_TX11_PIPE_X_EN_H_ADDR 0x90C //Mapping Enable High Byte, 8~15

#define MAX96712_PIPE_X_SRC_0_MAP_ADDR 0x90D //MAP_SRC_0 Register: VC+DT
#define MAX96712_PIPE_X_DST_0_MAP_ADDR 0x90E //MAP_DES_0 Register: VC+DT
#define MAX96712_PIPE_X_SRC_1_MAP_ADDR 0x90F //MAP_SRC_1 Register: VC+DT
#define MAX96712_PIPE_X_DST_1_MAP_ADDR 0x910 //MAP_DES_1 Register: VC+DT
#define MAX96712_PIPE_X_SRC_2_MAP_ADDR 0x911 //MAP_SRC_2 Register: VC+DT
#define MAX96712_PIPE_X_DST_2_MAP_ADDR 0x912 //MAP_DES_2 Register: VC+DT
#define MAX96712_PIPE_X_SRC_3_MAP_ADDR 0x913 //MAP_SRC_3 Register: VC+DT
#define MAX96712_PIPE_X_DST_3_MAP_ADDR 0x914 //MAP_DES_3 Register: VC+DT

#define MAX96712_TX45_PIPE_X_DST_CTRL_ADDR  0x92D //MAP Destination Controller Register: 0~3
#define MAX96712_TX45_PIPE_X_DST_CTRL2_ADDR 0x92E //MAP Destination Controller Register: 4~7

//--------------------------------------------------------------------
// others
#define MAX96712_PIPE_X_ST_SEL_ADDR 0x50 //RX packets with selected stream ID, add b7: RX_CRC_EN

#define MAX96712_PWR1_RESET_ALL_ADDR 0x13  //b6: 1->RESET_ALL, Chip reset

#define MAX96712_CTRL0_ADDR 0x17 //what?
#define MAX96712_CTRL3_ADDR 0x1A

#define MAX96712_LOCKED_MASK 0x08 //b3, GMSL2 link locked

/*-----------------------------------------------------------------------------------------------
 * 2. Video Pipe Selection
 *----------------------------------------------------------------------------------------------*/
#define MAX96712_PIPE_SEL_01_ADDR  0xF0 //Video Pipe Select & Stream ID Select Register: PHY A~D Stream 0~3 for Pipe 0&1
#define MAX96712_PIPE_SEL_23_ADDR  0xF1 //Video Pipe Select & Stream ID Select Register: PHY A~D Stream 0~3 for Pipe 2&3
#define MAX96712_PIPE_SEL_45_ADDR  0xF2 //Video Pipe Select & Stream ID Select Register: PHY A~D Stream 0~3 for Pipe 4&5
#define MAX96712_PIPE_SEL_67_ADDR  0xF3 //Video Pipe Select & Stream ID Select Register: PHY A~D Stream 0~3 for Pipe 6&7

#define MAX96712_PIPE_EN_ADDR  	  0xF4 //Video Pipe Enable Register: bits[7:0] for Pipe [7:0]

/*-----------------------------------------------------------------------------------------------
 * 1. Link Initialization
 *----------------------------------------------------------------------------------------------*/
#define MAX96712_LINK_MASK_ADDR     0x03 //Disable GMSL2 remote control channel link
#define MAX96712_LINK_EN_MODE_ADDR  0x06 //GMSL Link/PHY Enable b[3:0]A~D, Mode b[7:4] (0:GMSL1,1:GMSL2) Select Register

#define MAX96712_LINK_AB_RATE_ADDR  0x10 //GMSL Link/PHY A&B Rate Select Register
#define MAX96712_LINK_CD_RATE_ADDR  0x11 //GMSL Link/PHY C&D Rate Select Register

#define MAX96712_LINK_RESET_ADDR    0x18 //GMSL Link Reset Register: Bits [7:4]: Link reset for link D/C/B/A,  Bits [3:0]: One-shot link reset for link D/C/B/A


/*-----------------------------------------------------------------------------------------------
 * data defines
 *----------------------------------------------------------------------------------------------*/
/* MIPI PHY Mode Select */
#define MAX96712_CSI_MODE_4X2 0x1
#define MAX96712_CSI_MODE_1X4 0x2
#define MAX96712_CSI_MODE_2X4 0x4
#define MAX96712_CSI_MODE_1X4A_2X2 0x8
#define MAX96712_CSI_MODE_1X4B_2X2 0x10

/* MIPI PHY Lane Mapping */
#define MAX96712_LANE_MAP1_4X2 0x44 
#define MAX96712_LANE_MAP2_4X2 0x44
#define MAX96712_LANE_MAP1_2X4 0x4E
#define MAX96712_LANE_MAP2_2X4 0xE4

#define MAX96712_LANE_CTRL_MAP(num_lanes) \
	(((num_lanes) << 6) & 0xF0)

#define MAX96712_MAX_PIPES 8
#define MAX96712_MAX_LINKS 4

#define SERI_DEFAULT_ADDR 0x40 // >>1 = 0x40

#define TO_SERI_ADDR(A)	(priv->seri_start_addr + (A))
#define TO_SNR_ADDR(A)	(priv->snr_proxy_addr + (A))

//--------------------------------------------------------------------------------------
struct index_reg_8 {
	u8 source;
	u16 addr;
	u8 val;
};

struct max96712 {
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
static int _max96712_write_reg_with_addr(struct device *dev, u8 i2c_addr, u16 addr, u8 val)
{
	struct max96712 *priv = dev_get_drvdata(dev);
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

int max96712_write_reg_with_addr(struct device *dev, u8 i2c_addr, u16 addr, u8 val)
{
	struct max96712 *priv = dev_get_drvdata(dev);
	int ret;

	if (priv == NULL)
		return -1;

	mutex_lock(&priv->lock);
	ret = _max96712_write_reg_with_addr(dev, i2c_addr, addr, val);
	mutex_unlock(&priv->lock);

	return ret;
}
EXPORT_SYMBOL(max96712_write_reg_with_addr);

int max96712_write_tab_with_addr(struct device *dev, u8 i2c_addr, struct max_reg_pair tab[], u16 size)
{
	struct max96712 *priv = dev_get_drvdata(dev);
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
EXPORT_SYMBOL(max96712_write_tab_with_addr);

int max96712_read_reg_with_addr(struct device *dev, u8 i2c_addr, u16 addr, u8 *val)
{
	struct max96712 *priv = dev_get_drvdata(dev);
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

	dev_dbg(dev, "dser rd reg: 0x%02x, 0x%04x = 0x%02x\n", i2c_addr, addr, value);

	return err;
}
EXPORT_SYMBOL(max96712_read_reg_with_addr);

int max96712_write_seri_reg(struct device *dev, u8 link_id, u16 addr, u8 val)
{
	struct max96712 *priv = dev_get_drvdata(dev);
	return max96712_write_reg_with_addr(dev, TO_SERI_ADDR(link_id), addr, val);
}
EXPORT_SYMBOL(max96712_write_seri_reg);

int max96712_write_seri_tab(struct device *dev, u8 link_id, struct max_reg_pair buf[], u16 size)
{
	struct max96712 *priv = dev_get_drvdata(dev);
	return max96712_write_tab_with_addr(dev, TO_SERI_ADDR(link_id), buf, size);
}
EXPORT_SYMBOL(max96712_write_seri_tab);

int max96712_write_sensor(struct device *dev, u8 link_id, void *data, u16 length)
{
	struct max96712 *priv = dev_get_drvdata(dev);
	struct i2c_msg send_msg = {
		.addr = TO_SNR_ADDR(link_id),
		.flags = 0,
		.buf = (uint8_t*)data,
		.len = length,
	};
	int error;
	u16 *header = data;

	mutex_lock(&priv->lock);
	error = i2c_transfer(priv->i2c_client->adapter, &send_msg, 1);
	mutex_unlock(&priv->lock);
	if (error != 1) {
		dev_err(dev, "\nsensor i2c tx error, code=%d, len=%d, index=%d\n", header[1], header[0], header[2]);
		return -1;
	}

	dev_dbg(dev, "[snr] i2c wr: 0x%02x, %d\n", send_msg.addr, length);

	return 0;
}
EXPORT_SYMBOL(max96712_write_sensor);

int max96712_read_sensor(struct device *dev, u8 link_id, void *data, u16 length)
{
	struct max96712 *priv = dev_get_drvdata(dev);
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
EXPORT_SYMBOL(max96712_read_sensor);

static int max96712_write_reg(struct device *dev, u16 addr, u8 val)
{
	struct max96712 *priv = dev_get_drvdata(dev);

	int err = regmap_write(priv->regmap, addr, val);
	if (err)
		dev_err(dev, "%s err: 0x%04x = 0x%02x\n", __func__, addr, val);

	/* delay before next i2c command as required for SERDES link */
	usleep_range(100, 110);

	dev_dbg(dev, "[dser] i2c wr: 0x%04x = 0x%02x\n", addr, val);

	return err;
}

static int max96712_read_reg(struct device *dev, u16 addr, u8 *val)
{
	struct max96712 *priv = dev_get_drvdata(dev);
	unsigned int value;

	int err = regmap_read(priv->regmap, addr, &value);
	if (err)
		dev_err(dev, "%s err: 0x%04x\n", __func__, addr);

	*val = value&0xff;

	// delay before next i2c command as required for SERDES link 
	usleep_range(100, 110);

	dev_dbg(dev, "[dser] i2c rd: 0x%04x = 0x%02x\n", addr, value);

	return err;
}

static int max96712_set_registers(struct device *dev, struct max_reg_pair *map, u16 count)
{
	int err = 0;
	u16 i;

	for (i = 0; i < count; i++) {
		err = max96712_write_reg(dev, map[i].addr, map[i].val);
		if (err != 0)
			break;
	}

	return err;
}

//--------------------------------------------------------------------------------------
static int max96712_read_link_lock(struct device *dev, int index)
{
	u16 addr_tab[4] = {0x1A, 0x0A, 0x0B, 0x0C}; // link A, B, C, D
	u8 val = 0;
	int ret = max96712_read_reg(dev, addr_tab[index], &val); // link A, B, C, D
	if(ret) {
		dev_info(dev, "read link reg err, %d\n", ret);
		return -1;
	}
	return (val&0x8) ? 1 : 0; //bit3
}

static int max96712_check_link_lock(struct device *dev)
{
	int i, ret;
	u8 links = 0;

	for (i = 0; i < MAX96712_MAX_LINKS; i++) {
		ret = max96712_read_link_lock(dev, i);// link A, B, C, D
		if(ret < 0)
			return ret;
		else if(ret == 1)
			links |= (1<<i);
	}
	dev_info(dev, "check link status: 0x%02x\n", links);
	return links;
}

u8 max96712_get_link_map(struct device *dev)
{
	struct max96712 *priv = dev_get_drvdata(dev);
	return priv->links_map;
}
EXPORT_SYMBOL(max96712_get_link_map);

u8 max96712_get_link_init_flag(struct device *dev)
{
	struct max96712 *priv = dev_get_drvdata(dev);
	return priv->links_init_flag;
}
EXPORT_SYMBOL(max96712_get_link_init_flag);

void max96712_set_link_init_flag(struct device *dev, u8 link)
{
	struct max96712 *priv = dev_get_drvdata(dev);
	priv->links_init_flag &= ~(1 << link);
}
EXPORT_SYMBOL(max96712_set_link_init_flag);

#if 1
#if 0
/* Video channel is locked and outputting valid video data */
u8 max96712_check_video_channel_lock(struct device *dev)
{
	struct max96712 *priv = dev_get_drvdata(dev);
	u16 addr_tab[8] = {0x01DC, 0x01FC, 0x021C, 0x023C, 0x025C, 0x027C, 0x029C, 0x02BC}; //pipe 0~7
	int i, ret;
	u8 val = 0, ch_lock = 0;

	mutex_lock(&priv->lock);
	for (i = 0; i < 8; i++) {
		val = 0;
		ret = max96712_read_reg(dev, addr_tab[i], &val); //pipe 0~7
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
EXPORT_SYMBOL(max96712_check_video_channel_lock);
#endif

u8 max96712_check_pipe_lock(struct device *dev)
{
	struct max96712 *priv = dev_get_drvdata(dev);
	int i, ret;
	u16 addr_tb[]={0x108, 0x11A, 0x12C, 0x13E, 0x150, 0x168, 0x17A, 0x18C}; //pipe 0~7
	u8 val = 0, pipe_lock = 0;

	mutex_lock(&priv->lock);
	for (i = 0; i < MAX96712_MAX_PIPES; i++) {
		val = 0;
		ret = max96712_read_reg(dev, addr_tb[i], &val); //video pipeline locked 0~7
		if(ret)
			break;
		if(val&0x40) //bit6
			pipe_lock |= (1<<i);
	}
    mutex_unlock(&priv->lock);

	if(ret)
		pipe_lock = -1;

	dev_info(dev, "check pipe lock: 0x%02x\n", pipe_lock);

	return pipe_lock;
}
EXPORT_SYMBOL(max96712_check_pipe_lock);
#endif

/* transform serializer i2c address */
static int max96712_seri_i2c_addr_trans(struct device *dev)
{
	struct max96712 *priv = dev_get_drvdata(dev);
	int i, ret, links;
	u16 mask = 0xF0;

	mutex_lock(&priv->lock);

	links = max96712_check_link_lock(dev);
	if(links <= 0) {
		dev_err(dev, "max96712_check_link_lock failed %d\n", ret);
		ret = -1;
		goto err;
	}
	priv->links_map = links;
	priv->links_init_flag = links;
	ret = _max96712_write_reg_with_addr(dev, priv->deser_addr, 0x40B, 0x00); //CSI_OUT_EN = 0, CSI output disabled
	if (ret)
		goto err;
	ret = _max96712_write_reg_with_addr(dev, priv->deser_addr, 0x18, 0x0F); //reset link A~D oneshot
	if (ret)
		goto err;
	msleep(120);

	for (i = 0; i < MAX96712_MAX_LINKS; i++) { //link A~D
		if(links & (1<<i)) {
			mask |= (1<<i);
			/* max96712 */
			ret = _max96712_write_reg_with_addr(dev, priv->deser_addr, 0x06, mask); //enable link A~D
			if (ret)
				goto err;
			msleep(120);

			/* max9295d */
			ret = _max96712_write_reg_with_addr(dev, SERI_DEFAULT_ADDR, 0x0002, 0); //disable all pipe out
			ret = _max96712_write_reg_with_addr(dev, SERI_DEFAULT_ADDR, 0x0000, TO_SERI_ADDR(i)<<1); //transform serializer i2c address

			dev_info(dev, "transf seri i2c addr, 0x%02x, 0x40 -> 0x%02x\n", mask, TO_SERI_ADDR(i));
		}
	}

#if 1
	ret = _max96712_write_reg_with_addr(dev, priv->deser_addr, 0x40B, 0x02); //CSI_OUT_EN = 1, CSI output enabled
	if (ret)
		goto err;

	ret = _max96712_write_reg_with_addr(dev, priv->deser_addr, 0x06, mask); //re enable link A~D
	if (ret)
		goto err;
#endif

err:
	mutex_unlock(&priv->lock);

	return ret;
}


#define ORBBEC_POWER_GPIOA_ADDR 0x2D6
#define ORBBEC_POWER_GPIOB_ADDR 0x2D7

static int max96712_set_sensor_on(struct device *dev, int link_id)
{
	struct max96712 *priv = dev_get_drvdata(dev);
	int ret = _max96712_write_reg_with_addr(dev, TO_SERI_ADDR(link_id), ORBBEC_POWER_GPIOA_ADDR, 0x00);
	if (ret)
		dev_err(dev, "%s:set_sensor_on fail\n", __func__);
	return ret;
}

static int max96712_set_sensor_off(struct device *dev, int link_id)
{
	struct max96712 *priv = dev_get_drvdata(dev);
	int ret = _max96712_write_reg_with_addr(dev, TO_SERI_ADDR(link_id), ORBBEC_POWER_GPIOA_ADDR, 0x10);
	if (ret)
		dev_err(dev, "%s:set_sensor_off fail\n", __func__);
	return ret;
}

/* set sensor proxy address */
static int max96712_set_snr_proxy_addr(struct device *dev)
{
	struct max96712 *priv = dev_get_drvdata(dev);
	int i, err = 0;

	mutex_lock(&priv->lock);

	for (i = 0; i < MAX96712_MAX_LINKS; i++) { //link A~D
		if(priv->links_map & (1<<i)) {
			err  = _max96712_write_reg_with_addr(dev, TO_SERI_ADDR(i), 0x0042, (priv->snr_proxy_addr + i)<<1);
			err += _max96712_write_reg_with_addr(dev, TO_SERI_ADDR(i), 0x0043, priv->snr_real_addr<<1);
			if(err) {
				dev_err(dev, "max96712 set_snr_proxy_addr failed %d\n", err);
				break;
			}
			err = max96712_set_sensor_off(dev, i); //power off
			if(err) {
				dev_err(dev, "max96712 set_sensor_on failed %d\n", err);
				break;
			}
			msleep(10);
		}
	}
	mutex_unlock(&priv->lock);
#if 1
	msleep(3000);

	/* power on */
	mutex_lock(&priv->lock);
	for (i = 0; i < MAX96712_MAX_LINKS; i++) { //link A~D
		if(priv->links_map & (1<<i)) {
			err = max96712_set_sensor_on(dev, i);
			if(err) {
				dev_err(dev, "max96712 set_sensor_on failed %d\n", err);
				break;
			}
			msleep(100);
		}
	}
	mutex_unlock(&priv->lock);

	msleep(3000);
#endif
	return err;
}

static int __max96712_set_pipe(struct device *dev, u8 link_id, u8 pipe_id, 
				u8 data_type1, u8 data_type2, u8 src_vc_id, u8 dst_vc_id)
{
	struct max96712 *priv = dev_get_drvdata(dev);
	int i, err;
	u8 mapping2csi, mappings_en = 0x0F;

	struct max_reg_pair map_pipe_control[] = {
		/* Enable 4 mappings for Pipe X */
		{MAX96712_TX11_PIPE_X_EN_ADDR,   0x0F}, //0x90B
		/* Map data_type1 on vc_id */
		{MAX96712_PIPE_X_SRC_0_MAP_ADDR, 0x1E}, //0x90D
		{MAX96712_PIPE_X_DST_0_MAP_ADDR, 0x1E},
		/* Map frame_start on vc_id */
		{MAX96712_PIPE_X_SRC_1_MAP_ADDR, 0x00}, //0x90F
		{MAX96712_PIPE_X_DST_1_MAP_ADDR, 0x00},
		/* Map frame end on vc_id */
		{MAX96712_PIPE_X_SRC_2_MAP_ADDR, 0x01}, //0x911
		{MAX96712_PIPE_X_DST_2_MAP_ADDR, 0x01},
		/* Map data_type2 on vc_id */
		{MAX96712_PIPE_X_SRC_3_MAP_ADDR, 0x12}, //0x913
		{MAX96712_PIPE_X_DST_3_MAP_ADDR, 0x12},
		/* All mappings to PHY1 (master for port A) */
		{MAX96712_TX45_PIPE_X_DST_CTRL_ADDR, 0x55}, //0x92D
		/* Disable “Heartbeat” Mode, I's use for DSI */
		{0x0100, 0x23}, //0x23, SEQ_MISS_EN=0, LINE_CRC_EN=0, DIS_PKT_DET=1
		{0x0106, 0x0A}, //LIM_HEART
	};

	for (i = 0; i < 10; i++)
		map_pipe_control[i].addr += 0x40 * pipe_id;
	for (; i < 12; i++)
		map_pipe_control[i].addr += (pipe_id < 5) ? (0x12 * pipe_id) : (0x12 * pipe_id + 6);

	if(priv->csi_mode == MAX96712_CSI_MODE_2X4)
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

	dev_info(dev, "set pipe start\n");

	err = max96712_set_registers(dev, map_pipe_control, ARRAY_SIZE(map_pipe_control));

	dev_info(dev, "set pipe %s!\n", err ? "failed" : "success");

	return err;
}

/*
   link_id: 0~3 -> deser link A ~ D
   pipe_id: 0~7 -> deser pipe
   seri_pipe_id: 0~3 ->seri pipe
*/
int max96712_link_pipe_bind(struct device *dev, u8 link_id, u8 pipe_id, u8 seri_pipe_id)
{
#if 1
	struct max96712 *priv = dev_get_drvdata(dev);
	const u8 pipe_sel_tb[4] = {
		MAX96712_PIPE_SEL_01_ADDR,
		MAX96712_PIPE_SEL_23_ADDR,
		MAX96712_PIPE_SEL_45_ADDR,
		MAX96712_PIPE_SEL_67_ADDR
	};
	int err;
	u8 addr, val, temp;

	dev_info(dev, "%s bind pipe, deser link=%d, deser id=%d, seri id=%d\n", __func__, link_id, pipe_id, seri_pipe_id);

	addr = pipe_sel_tb[pipe_id/2];

	mutex_lock(&priv->lock);

	err = max96712_read_reg(dev, addr, &val);
	if(err) {
		dev_err(dev, "%s read reg failed, 0x%04x\n", __func__, addr);
		mutex_unlock(&priv->lock);
		return -1;
	}

	temp = (link_id<<2) | seri_pipe_id;
	if (pipe_id & 1) {
		val = (val & 0x0f) | (temp<<4);
	} else {
		val = (val & 0xf0) | temp;
	}

	err = max96712_write_reg(dev, addr, val);
	if(err == 0)
		dev_info(dev, "%s bind pipe success\n", __func__);

	dev_info(dev, "%s oneshot reset all link\n", __func__);
	max96712_write_reg(dev, MAX96712_LINK_RESET_ADDR, 0x0f); //0x18, 0x0f -> all link oneshot reset
	msleep(100);
	
	max96712_write_reg(dev, MAX96712_PIPE_EN_ADDR, priv->pipes_map); //0xF4, pipe en

	mutex_unlock(&priv->lock);

	return err;
#else
	return 0;
#endif
}
EXPORT_SYMBOL(max96712_link_pipe_bind);

int max96712_set_pipe(struct device *dev, u8 link_id, u8 pipe_id,
		     u8 data_type1, u8 data_type2, u8 src_vc_id, u8 dst_vc_id)
{
	struct max96712 *priv = dev_get_drvdata(dev);
	int err = 0;
	// u8 pipe_lock = 0;

	if (pipe_id > (MAX96712_MAX_PIPES - 1)) {
		dev_err(dev, "%s, input pipe_id: %d exceed max96712 max pipes\n", __func__, pipe_id);
		return -EINVAL;
	}

	dev_info(dev, "%s pipe_id:%d, dt1:%u, dt2:%u, src_vc:%u, dst_vc:%u\n",
		__func__, pipe_id, data_type1, data_type2, src_vc_id, dst_vc_id);

	mutex_lock(&priv->lock);

	err = __max96712_set_pipe(dev, link_id, pipe_id, data_type1, data_type2, src_vc_id, dst_vc_id);

	mutex_unlock(&priv->lock);

	err |= max96712_link_pipe_bind(dev, link_id, pipe_id, dst_vc_id);

	return err;
}
EXPORT_SYMBOL(max96712_set_pipe);

int max96712_init_settings(struct device *dev)
{
	struct max96712 *priv = dev_get_drvdata(dev);
	int err = 0;

	struct max_reg_pair map_pipe_opt[] = {
	#if 0
		{0x1458, 0x28}, // PHY A Optimization
		{0x1459, 0x68}, // PHY A Optimization
		{0x1558, 0x28}, // PHY B Optimization
		{0x1559, 0x68}, // PHY B Optimization
		{0x1658, 0x28}, // PHY C Optimization
		{0x1659, 0x68}, // PHY C Optimization
		{0x1758, 0x28}, // PHY D Optimization
		{0x1759, 0x68}, // PHY D Optimization
    #endif
		/* ----------Video pipes configure ----------------------*/
		{MAX96712_PIPE_SEL_01_ADDR, 0x10}, //0xF0, Link A ID 0 to pipe 0 , Link A ID 1 to pipe 1
		{MAX96712_PIPE_SEL_23_ADDR, 0x32}, //0xF1, Link A ID 2 to pipe 2 , Link A ID 3 to pipe 3
		// {MAX96712_PIPE_SEL_45_ADDR, 0x54}, //0x10}, //0xF0, Link A ID 0 to pipe 0 , Link A ID 1 to pipe 1
		// {MAX96712_PIPE_SEL_67_ADDR, 0x76}, //0x32}, //0xF1, Link A ID 2 to pipe 2 , Link A ID 3 to pipe 3
		{MAX96712_PIPE_SEL_45_ADDR, 0xDC}, //0xF2, Link C ID 0 to pipe 4 , Link C ID 1 to pipe 5
		{MAX96712_PIPE_SEL_67_ADDR, 0xFE}, //0xF3, Link C ID 2 to pipe 6 , Link C ID 3 to pipe 7

		{MAX96712_PIPE_EN_ADDR, 0x00}, //0xF4, Enable pipe 0-3, 0xFF

		/* ------------Disable heartbeat mode --------------------*/
	#if 0
		{0x0100, 0x23},
		{0x0112, 0x23},
		{0x0124, 0x23},
		{0x0136, 0x23},

		{0x0106, 0x0A},
		{0x0118, 0x0A},
		{0x012A, 0x0A},
		{0x013C, 0x0A},
	#endif
	};

	struct max_reg_pair map_phy_opt_2x4[] = 
	{
		/* MIPI D-PHY Config */
		{MAX96712_DST_CSI_MODE_ADDR, (MAX96712_CSI_MODE_2X4 | 0x20)}, //0x8A0
		{MAX96712_LANE_MAP1_ADDR, 0xE4}, //0x8A3, Default 4x2 lane mapping
		{MAX96712_LANE_MAP2_ADDR, 0xE4}, //0x8A4, Default 4x2 lane mapping

		/* 4 lanes on port A,  0x40 -> 2 lanes, 0xC0 -> 4 lanes */
		// {MAX96712_LANE_CTRL0_ADDR, 0xC0}, //0x90A, MIPI PHY0
		{MAX96712_LANE_CTRL1_ADDR, 0xC0}, //0x94A, MIPI PHY1, as Master controller for A 4lane
		{MAX96712_LANE_CTRL2_ADDR, 0xC0}, //0x98A, MIPI PHY2, as Master controller for B 4lane
		// {MAX96712_LANE_CTRL3_ADDR, 0xC0}, //0x9CA, MIPI PHY3

		/* Put DPLL for ctrl 1 and 2 in reset (config_soft_rst_n = 0) before changing MIPI lane rates */
		// {0x1C00, 0xF4}, //PHY0
		{0x1D00, 0xF4}, //PHY1
		{0x1E00, 0xF4}, //PHY2
		// {0x1F00, 0xF4}, //PHY3

		/* 1500Mbps/lane on port A */
		// {MAX96712_PHY0_CLK_ADDR, 0x2F}, //0x415, MIPI PHY0
		{MAX96712_PHY1_CLK_ADDR, 0x2F}, //0x418, MIPI PHY1, this
		{MAX96712_PHY2_CLK_ADDR, 0x2F}, //0x41B, MIPI PHY2, this
		// {MAX96712_PHY3_CLK_ADDR, 0x2F}, //0x41E, MIPI PHY3

		/* Release DPLL reset (config_soft_rst_n = 1) */
		// {0x1C00, 0xF5}, //PHY0
		{0x1D00, 0xF5}, //PHY1
		{0x1E00, 0xF5}, //PHY2
		// {0x1F00, 0xF5}, //PHY3

		/* Do not un-double 8bpp (Un-double 8bpp data) */
		//{0x0414, 0xF0}, //Pipe0~3, Process BPP = 8 as 16-bit color
		//{0x0417, 0xF0}, //Enable 8-bit write alternate map to RAMs for pipeline X~U
		//{0x0434, 0xF0}, //Pipe4~7, Process BPP = 8 as 16-bit color
		//{0x0437, 0xF0}, //Enable 8-bit write alternate map to RAMs for pipeline X~U
	
		/* 0x02: ALT_MEM_MAP8, 0x10: ALT2_MEM_MAP8 */
		// {0x0933, 0x10}, //MIPI TX0
		{0x0973, 0x10}, //0x10, MIPI TX1
		{0x09B3, 0x10}, //0x10, MIPI TX2
		// {0x09F3, 0x10}, //MIPI TX3

		{MAX96712_PWDN_PHYS_ADDR, 0xF0}, //0x8A2
		{0x40B, 0x02},  // (CSI_OUT_EN): CSI output enabled
	};
	struct max_reg_pair map_phy_opt_4x2[] = {
		/* MIPI D-PHY Config */
		{MAX96712_DST_CSI_MODE_ADDR, MAX96712_CSI_MODE_4X2}, //0x8A0
		{MAX96712_LANE_MAP1_ADDR, 0x44}, //0x8A3, Default 4x2 lane mapping
		{MAX96712_LANE_MAP2_ADDR, 0x44}, //0x8A4, Default 4x2 lane mapping

		/* 4 lanes on port A,  0x40 -> 2 lanes, 0xC0 -> 4 lanes */
		{MAX96712_LANE_CTRL0_ADDR, 0x40}, //0x90A, MIPI PHY0
		{MAX96712_LANE_CTRL1_ADDR, 0x40}, //0x94A, MIPI PHY1, as Master controller for A 4lane
		{MAX96712_LANE_CTRL2_ADDR, 0x40}, //0x98A, MIPI PHY2, as Master controller for B 4lane
		{MAX96712_LANE_CTRL3_ADDR, 0x40}, //0x9CA, MIPI PHY3

		/* Put DPLL for ctrl 1 and 2 in reset (config_soft_rst_n = 0) before changing MIPI lane rates */
		{0x1C00, 0xF4}, //PHY0
		{0x1D00, 0xF4}, //PHY1
		{0x1E00, 0xF4}, //PHY2
		{0x1F00, 0xF4}, //PHY3

		/* 1500Mbps/lane on port A */
		{MAX96712_PHY0_CLK_ADDR, 0x2F}, //0x415, MIPI PHY0
		{MAX96712_PHY1_CLK_ADDR, 0x2F}, //0x418, MIPI PHY1, this
		{MAX96712_PHY2_CLK_ADDR, 0x2F}, //0x41B, MIPI PHY2, this
		{MAX96712_PHY3_CLK_ADDR, 0x2F}, //0x41E, MIPI PHY3

		/* Release DPLL reset (config_soft_rst_n = 1) */
		{0x1C00, 0xF5}, //PHY0
		{0x1D00, 0xF5}, //PHY1
		{0x1E00, 0xF5}, //PHY2
		{0x1F00, 0xF5}, //PHY3

		/* Do not un-double 8bpp (Un-double 8bpp data) */
		// {0x0414, 0xF0}, //Pipe0~3, Process BPP = 8 as 16-bit color
		// {0x0417, 0xF0}, //Enable 8-bit write alternate map to RAMs for pipeline X~U
		// {0x0434, 0xF0}, //Pipe4~7, Process BPP = 8 as 16-bit color
		// {0x0437, 0xF0}, //Enable 8-bit write alternate map to RAMs for pipeline X~U
	
		/* 0x02: ALT_MEM_MAP8, 0x10: ALT2_MEM_MAP8 */
		{0x0933, 0x10}, //MIPI TX0
		{0x0973, 0x10}, //0x10, MIPI TX1
		{0x09B3, 0x10}, //0x10, MIPI TX2
		{0x09F3, 0x10}, //MIPI TX3

		{MAX96712_PWDN_PHYS_ADDR, 0xF0}, //0x8A2, 0xF4
		{0x40B, 0x02},  // (CSI_OUT_EN): CSI output enabled
	};

	dev_info(dev, "%s start\n", __func__);

	mutex_lock(&priv->lock);

	err |= max96712_set_registers(dev, map_pipe_opt, ARRAY_SIZE(map_pipe_opt));

	if(priv->csi_mode == MAX96712_CSI_MODE_2X4)
		err |= max96712_set_registers(dev, map_phy_opt_2x4, ARRAY_SIZE(map_phy_opt_2x4));
	else
		err |= max96712_set_registers(dev, map_phy_opt_4x2, ARRAY_SIZE(map_phy_opt_4x2));

	mutex_unlock(&priv->lock);

	if (err == 0)
		dev_info(dev, "%s done\n", __func__);
	else
		dev_err(dev, "%s failed, err %d\n", __func__, err);

	return err;
}
EXPORT_SYMBOL(max96712_init_settings);

int max96712_init_tx_gpio(struct device *dev)
{
	struct max96712 *priv = dev_get_drvdata(dev);
	int err = 0;

	struct max_reg_pair map_fsync_trigger[] = {
		/* MFP2 fsync out */
		/* Set Internal FSYNC off, GPIO is used for FSYNC, type = GMSL2 */
		{0x04A0, 0x08},
		{0x04AF, 0x9F},
		/* Config MAX96712/722 MFP2 to receive external FSYNC signal for each link */
		{0x0306, 0x83}, // RES_CFG[7]: 0->40K, 1->1M; GPIO_RX_EN[2]; GPIO_TX_EN[1], GPIO_OUT[4]; GPIO_IN[3]; GPIO_OUT_DIS[0]: 1->disable
		{0x0307, 0xA2}, // PULL_UPDN_SEL[7:6]: 0->no pullup, 1->Pullup, 2->Pulldown; OUT_TYPE[5]: 1->Push-pull, 0->Open-drain; GPIO_TX_ID[4:0]
		{0x033D, 0x22},
		{0x0374, 0x22},
		{0x03AA, 0x22},
	};
#if 0
	struct max_reg_pair map_pps_trigger[] = {
		/* MFP9 pps, TX */
		{0x031C, 0x83}, // RES_CFG[7]: 0->40K, 1->1M; GPIO_RX_EN[2]; GPIO_TX_EN[1], GPIO_OUT[4]; GPIO_IN[3]; 
		{0x031D, 0x1D}, // PULL_UPDN_SEL[7:6]: 0->no pullup, 1->Pullup, 2->Pulldown; OUT_TYPE[5]: 1->Push-pull, 0->Open-drain; GPIO_TX_ID[4:0]
		{0x0354, 0x3D},
		{0x038A, 0x3D},
		{0x03C1, 0x3D},
	};
#endif
	dev_info(dev, "%s start\n", __func__);

	mutex_lock(&priv->lock);

	map_fsync_trigger[2].addr = 0x300 + priv->fsync_mfp_x * 3 + priv->fsync_mfp_x / 5;
	map_fsync_trigger[3].addr = map_fsync_trigger[2].addr + 1;
	map_fsync_trigger[4].addr = 0x337 + priv->fsync_mfp_x * 3 + (priv->fsync_mfp_x + 2) / 5;
	map_fsync_trigger[5].addr = 0x36D + priv->fsync_mfp_x * 3 + (priv->fsync_mfp_x + 4) / 5;
	map_fsync_trigger[6].addr = 0x3A4 + priv->fsync_mfp_x * 3 + (priv->fsync_mfp_x + 1) / 5;
	err |= max96712_set_registers(dev, map_fsync_trigger, ARRAY_SIZE(map_fsync_trigger));

#if 0
	// PPS Trigger
	map_pps_trigger[0].addr = 0x300 + priv->pps_mfp_x * 3 + priv->fsync_mfp_x / 5;
	map_pps_trigger[1].addr = map_pps_trigger[0].addr + 1;
	map_pps_trigger[2].addr = 0x337 + priv->pps_mfp_x * 3 + (priv->fsync_mfp_x + 2) / 5;
	map_pps_trigger[3].addr = 0x36D + priv->pps_mfp_x * 3 + (priv->fsync_mfp_x + 4) / 5;
	map_pps_trigger[4].addr = 0x3A4 + priv->pps_mfp_x * 3 + (priv->fsync_mfp_x + 1) / 5;
	err |= max96712_set_registers(dev, map_pps_trigger, ARRAY_SIZE(map_pps_trigger));
#endif

	mutex_unlock(&priv->lock);

	if (err == 0)
		dev_info(dev, "%s done\n", __func__);
	else
		dev_err(dev, "%s failed, err %d\n", __func__, err);

	return err;
}
EXPORT_SYMBOL(max96712_init_tx_gpio);

int max96712_reset_dev(struct device *dev)
{
	struct max96712 *priv = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&priv->lock);
	ret = max96712_write_reg(dev, MAX96712_PWR1_RESET_ALL_ADDR, 0x40); //reset all
	if(ret)
		dev_err(dev, "reset deseri chip failed, %d\n", ret);
	mutex_unlock(&priv->lock);

	msleep(100); /* delay to settle reset */

	return ret;
}
EXPORT_SYMBOL(max96712_reset_dev);

int max96712_get_link_state(struct device *dev, u8 link_id, int *value)
{
	struct max96712 *priv = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&priv->lock);
	ret = max96712_read_link_lock(dev, link_id);
	if(ret >= 0)
		*value = ret;
	mutex_unlock(&priv->lock);

	dev_info(dev, "%s, %d\n", __func__, ret);

	return ret;
}
EXPORT_SYMBOL(max96712_get_link_state);


int max96712_get_available_pipe_id(struct device *dev, int dst_vc_id)
{
	struct max96712 *priv = dev_get_drvdata(dev);
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

	dev_info(dev, "request pipe id, %x\n", dst_vc_id);

	return pipe_id;
}
EXPORT_SYMBOL(max96712_get_available_pipe_id);

int max96712_release_pipe(struct device *dev, u8 link_id, u8 pipe_id)
{
#if 1
	struct max96712 *priv = dev_get_drvdata(dev);

	if (link_id > 3 || pipe_id < 0 || pipe_id >= MAX96712_MAX_PIPES)
		return -EINVAL;

	mutex_lock(&priv->lock);

	max96712_write_reg(dev, MAX96712_LINK_RESET_ADDR, 1<<link_id); //link reset oneshot

	priv->pipes_map &= ~(1<<pipe_id);
	max96712_write_reg(dev, MAX96712_PIPE_EN_ADDR, priv->pipes_map); //pipe disable

	mutex_unlock(&priv->lock);

	dev_info(dev, "release pipe id, 0x%02x\n", priv->pipes_map);
#endif

	return 0;
}
EXPORT_SYMBOL(max96712_release_pipe);

int max96712_reset_oneshot(struct device *dev)
{
	struct max96712 *priv = dev_get_drvdata(dev);
	int err = 0;

	mutex_lock(&priv->lock);

	dev_info(dev, "%s oneshot reset all link.\n", __func__);
	err = max96712_write_reg(dev, MAX96712_LINK_RESET_ADDR, 0x0f); //0x18, 0x0f -> all link oneshot reset
	msleep(100);

	mutex_unlock(&priv->lock);

	return err;
}
EXPORT_SYMBOL(max96712_reset_oneshot);

static int max96712_stats_show(struct seq_file *s, void *data)
{
	return 0;
}

static int max96712_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, max96712_stats_show, inode->i_private);
}

static ssize_t max96712_debugfs_write(struct file *s,
				const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct max96712 *priv = ((struct seq_file *)s->private_data)->private;
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
		// max96712_read_reg(&i2c_client->dev, 0x0010, &val);
		return count;
	}

	if (buf[0] == 'n') {
		dev_info(&i2c_client->dev, "%s, set nightmode\n", __func__);
		return count;
	}

	return count;
}


static const struct file_operations max96712_debugfs_fops = {
	.open = max96712_debugfs_open,
	.read = seq_read,
	.write = max96712_debugfs_write,
	.llseek = seq_lseek,
	.release = single_release,
};

int max96712_power_on(struct device *dev)
{
	struct max96712 *priv = dev_get_drvdata(dev);

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
EXPORT_SYMBOL(max96712_power_on);

void max96712_power_off(struct device *dev)
{
	struct max96712 *priv = dev_get_drvdata(dev);

	if (priv->reset_gpio > 0) {
		if(gpio_cansleep(priv->reset_gpio))
			gpio_set_value_cansleep(priv->reset_gpio, 0);
		else
			gpio_set_value(priv->reset_gpio, 0);
		dev_info(dev, "%s\n", __func__);
	}
}
EXPORT_SYMBOL(max96712_power_off);

static int max96712_debugfs_init(const char *dir_name,
				struct dentry **d_entry,
				struct dentry **f_entry,
				struct max96712 *priv)
{
	struct dentry  *dp, *fp;
	char dev_name[20];
	struct i2c_client *i2c_client = priv->i2c_client;
	int err = 0;

	err = snprintf(dev_name, sizeof(dev_name), "max96712_%d", priv->index);
	if (err < 0)
		return -EINVAL;

	dp = debugfs_create_dir(dev_name, NULL);
	if (dp == NULL) {
		dev_err(&i2c_client->dev, "%s: debugfs create dir failed\n", __func__);
		return -ENOMEM;
	}

	fp = debugfs_create_file("max96712", 0644, dp, priv, &max96712_debugfs_fops);
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

static const struct of_device_id max96712_of_match[] = {
	{ .compatible = "maxim,obc_max96712", },
	{ },
};
MODULE_DEVICE_TABLE(of, max96712_of_match);

static int max96712_parse_dt(struct max96712 *priv, struct i2c_client *client)
{
	struct device_node *node = client->dev.of_node;
	const struct of_device_id *match;
	const char *str_value;
	int value, err = 0;

	if (!node)
		return -EINVAL;

	match = of_match_device(max96712_of_match, &client->dev);
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

	if (!strcmp(str_value, "2x4")) {
		priv->csi_mode = MAX96712_CSI_MODE_2X4;
	} else if (!strcmp(str_value, "4x2")) {
		priv->csi_mode = MAX96712_CSI_MODE_4X2;
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

	priv->deser_addr = client->addr;
	err = of_property_read_u32(node, "fsync_mfp_index", &value);
	if (err < 0) {
		priv->fsync_mfp_x = 2;
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

static struct regmap_config max96712_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.cache_type = REGCACHE_NONE, // REGCACHE_RBTREE,
};

#if defined(NV_I2C_DRIVER_STRUCT_PROBE_WITHOUT_I2C_DEVICE_ID_ARG) /* Linux 6.3 */
static int max96712_probe(struct i2c_client *client)
#else
static int max96712_probe(struct i2c_client *client, const struct i2c_device_id *id)
#endif
{
	struct max96712 *priv;
	int err = 0;

	dev_info(&client->dev, "%s: enter\n", __func__);

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	priv->i2c_client = client;
	priv->regmap = devm_regmap_init_i2c(priv->i2c_client, &max96712_regmap_config);
	if (IS_ERR(priv->regmap)) {
		dev_err(&client->dev, "regmap init i2c failed: %ld\n", PTR_ERR(priv->regmap));
		return -ENODEV;
	}
	dev_set_drvdata(&client->dev, priv);

	err = max96712_parse_dt(priv, client);   //parse devicetree
	if (err) {
		dev_err(&client->dev, "parse devicetree error, %d\n", err);
		return -EFAULT;
	}

	mutex_init(&priv->lock);

	max96712_power_off(&client->dev);
	msleep(400);
	err = max96712_power_on(&client->dev);
	if (err) {
		dev_err(&client->dev, "power on failed, %d\n", err);
		return err;
	}
	msleep(200);

	err = max96712_debugfs_init(NULL, NULL, NULL, priv);
	if (err)
		return err;

	/* Serializer i2c address trans */
	err = max96712_seri_i2c_addr_trans(&client->dev);
	if(err) {
		dev_err(&client->dev, "Serializer i2c address change failed, %d\n", err);
		return 0; //err;
	}

	err = max96712_init_settings(&client->dev);
	if(err) {
		dev_err(&client->dev, "max96712 init settings failed, %d\n", err);
		return 0; //err;
	}

	err = max96712_set_snr_proxy_addr(&client->dev);
	if(err) {
		dev_err(&client->dev, "max96712 set_snr_proxy_addr failed, %d\n", err);
		return 0; //err;
	}

	/*set daymode by fault*/
	dev_info(&client->dev, "%s: success\n", __func__);

	return 0; //err;
}

#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
static int max96712_remove(struct i2c_client *client)
#else
static void max96712_remove(struct i2c_client *client)
#endif
{
	struct max96712 *priv;

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

static const struct i2c_device_id max96712_id[] = {
	{ "obc_max96712", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, max96712_id);

static struct i2c_driver max96712_i2c_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "obc_max96712",
		.of_match_table = of_match_ptr(max96712_of_match),
	},
	.probe = max96712_probe,
	.remove = max96712_remove,
	.id_table = max96712_id,
};

module_i2c_driver(max96712_i2c_driver);

MODULE_DESCRIPTION("IO Expander driver max96712");
MODULE_AUTHOR("NVIDIA Corporation");
MODULE_LICENSE("GPL v2");
