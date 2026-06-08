// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2023, NVIDIA Corporation. All rights reserved.
 *
 * DS90UH983-Q1 DP to FPD-Link Serializer driver
 */

#include <nvidia/conftest.h>

#include <linux/device.h>
#include <linux/fwnode.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/of_gpio.h>
#include <linux/workqueue.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/version.h>

// Target I2C register address
#define TI_TARGET_ID_0                      0x70
#define TI_TARGET_ID_VAL                    0x58
#define TI_TARGET_ALIAS_ID_0                0x78
#define TI_TARGET_ALIAS_ID_VAL              0x2C
#define TI_TARGET_DEST_0                    0x88
#define TI_TARGET_DEST_VAL                  0x0
// Reset for SER and PLL
#define TI_RESET_CTL                        0x1
#define TI_RESET_CTL_VAL_SOFT_RESET_SER     0x1
#define TI_RESET_CTL_VAL_SOFT_RESET_PLL     0x30
// Indirect Page Access
#define TI_IND_ACC_CTL                  0x40
#define TI_IND_ACC_CTL_SEL_MASK         (0x7 << 2)
#define TI_IND_ACC_CTL_SEL_SHIFT        2
#define TI_IND_ACC_CTL_AUTO_INC_MASK    (0x1 << 1)
#define TI_IND_ACC_CTL_READ_MASK        (0x0 << 1)
// FPD Port page
#define TI_IND_ACC_PAGE_1               0x1
// FPD PLL page
#define TI_IND_ACC_PAGE_2               0x2
#define TI_IND_ACC_PAGE_4               0x4
#define TI_IND_ACC_PAGE_5               0x5
#define TI_IND_ACC_PAGE_9               0x9
#define TI_IND_ACC_PAGE_11              0xb
#define TI_IND_ACC_PAGE_12              0xc
#define TI_IND_ACC_PAGE_14              0xe
#define TI_IND_ACC_ADDR                 0x41
#define TI_IND_ACC_DATA                 0x42
// Page 0 - General
#define TI_GENERAL_CFG                          0x7
#define TI_GENERAL_CFG_SST_MST_MODE_STRP_MASK   (0x1 << 1)
#define TI_GENERAL_CFG_I2C_PASS_THROUGH_MASK    (0x1 << 3)
#define TI_GENERAL_CFG2                         0x2
#define TI_GENERAL_CFG2_VAL                     0xD1
#define TI_GENERAL_CFG2_DEFAULT                 0xDF
#define TI_GENERAL_CFG2_DPRX_EN                 (0x1 << 4)
#define TI_GENERAL_CFG2_CRC_ERROR_RESET_CLEAR   (0x1 << 5)
#define TI_FPD4_CFG                     0x5
#define TI_FPD4_CFG_FPD4_TX_MODE_DUAL   0x28
#define TI_FPD4_CFG_FPD4_TX_MODE_SINGLE 0x2C
#define TI_FPD4_CFG_FPD4_TX_MODE_IND    0x3C
#define TI_FPD4_PGCFG_VP0               0x29
#define TI_FPD4_PGCFG_DEFAULT           0x8
#define TI_FPD3_FIFO_CFG                0x5B
#define TI_FPD3_FIFO_CFG_VAL            0x23
#define TI_TX_PORT_SEL                  0x2D
#define TI_TX_PORT_SEL_VAL_PORT_0       0x1
#define TI_TX_PORT_SEL_VAL_PORT_1       0x12
// PAGE 2 PLL
#define TI_PLL_0                        0x1B
#define TI_PLL_1                        0x5B
#define TI_PLL_DISABLE_VAL              0x1
#define TI_PLL_DISABLE_SHIFT            0x3
#define TI_NDIV_7_0_CH0                 0x5
#define TI_NDIV_7_0_CH0_VAL             0x7D
#define TI_NDIV_15_8_CH0                0x6
#define TI_PDIV_CH0                     0x13
#define TI_PDIV_CH0_VAL                 0x80
#define TI_NDIV_7_0_CH1                 0x45
#define TI_NDIV_7_0_CH1_VAL             0x7D
#define TI_NDIV_15_8_CH1                0x46
#define TI_PDIV_CH1                     0x53
#define TI_PDIV_CH1_VAL                 0x80
#define TI_MASH_ORDER_CH0               0x4
#define TI_MASH_ORDER_CH0_VAL           0x1
#define TI_NUM_7_0_CH0                  0x1E
#define TI_NUM_7_0_CH0_VAL              0x0
#define TI_NUM_15_8_CH0                 0x1F
#define TI_NUM_15_8_CH0_VAL             0x0
#define TI_NUM_23_16_CH0                0x20
#define TI_NUM_23_16_CH0_VAL            0x0
#define TI_MASH_ORDER_CH1               0x44
#define TI_MASH_ORDER_CH1_VAL           0x1
#define TI_NUM_7_0_CH1                  0x5E
#define TI_NUM_7_0_CH1_VAL              0x0
#define TI_NUM_15_8_CH1                 0x5F
#define TI_NUM_15_8_CH1_VAL             0x0
#define TI_NUM_23_16_CH1                0x60
#define TI_NUM_23_16_CH1_VAL            0x0
#define TI_VCO_CH0                      0xE
#define TI_VCO_CH1                      0x4E
#define TI_VCO_CH_SEL_VCO_1             0xC3
#define TI_VCO_CH_SEL_VCO_2             0xC7
#define TI_VCO_CH_SEL_VCO_3             0xCB
#define TI_VCO_CH_SEL_VCO_4             0xCF
// APB bus registers
#define TI_APB_CTL                      0x48
#define TI_APB_CTL_EN                   0x1
#define TI_APB_ADDR0                    0x49
#define TI_APB_ADDR1                    0x4a
#define TI_APB_DATA0                    0x4b
#define TI_APB_DATA1                    0x4c
#define TI_APB_DATA2                    0x4d
#define TI_APB_DATA3                    0x4e
#define TI_APB_LINK_ENABLE              0x0
#define TI_APB_EMPTY_FILL               0x0
#define TI_APB_LINK_ENABLE_LOW          0x0
#define TI_APB_LINK_ENABLE_HIGH         0x1
#define TI_APB_MAX_LINK_RATE            0x74
#define TI_APB_MAX_LINK_RATE_MASK       0xFF
#define TI_APB_MAX_LINK_RATE_162        0x06 // 1.62 Gbps
#define TI_APB_MAX_LINK_RATE_270        0x0A // 2.7 Gbps
#define TI_APB_MAX_LINK_RATE_540        0x14 // 5.4 Gbps
#define TI_APB_MAX_LINK_RATE_810        0x1E // 8.1 Gbps
#define TI_APB_MAX_LANE_COUNT           0x70
#define TI_APB_MAX_LANE_COUNT_1         0x1
#define TI_APB_MAX_LANE_COUNT_2         0x2
#define TI_APB_MAX_LANE_COUNT_4         0x4
#define TI_APB_MISC_CONFIG                              0x18
#define TI_APB_MISC_CONFIG_MAX_DOWNSPREAD_CONFIG        (1 << 4)
#define TI_APB_MISC_CONFIG_DISABLE_INACTIVE_COUNT       (1 << 2)

#define TI_APB_VIDEO_INPUT_RESET            0x54
#define TI_APB_VIDEO_INPUT_RESET_TRIGGER    0x1

#define TI_LINK_LAYER_CTL                   0x0
#define TI_LINK_LAYER_CTL_EN_NEW_TSLOT1     0x8
#define TI_LINK_LAYER_CTL_LINK_LAYER_1_EN   0x4
#define TI_LINK_LAYER_CTL_EN_NEW_TSLOT0     0x2
#define TI_LINK_LAYER_CTL_LINK_LAYER_0_EN   0x1

#define TI_LINK0_STREAM_EN                  0x1
#define TI_LINK1_STREAM_EN                  0x11
#define TI_LINK_STREAM_EN0                  0x1
#define TI_LINK_STREAM_EN1                  0x2
#define TI_LINK_STREAM_EN2                  0x4
#define TI_LINK_STREAM_EN3                  0x8
#define TI_LINK_STREAM_EN4                  0x10
#define TI_LINK0_SLOT_REQ0                  0x6
#define TI_LINK1_SLOT_REQ0                  0x16
#define TI_VP_WIDTH0                        0x20
#define TI_VID_PROC_CFG_VP0                 0x1
#define TI_VID_PROC_CFG_VP0_DEFAULT         0xA8
#define TI_DP_H_ACTIVE0_VP0                 0x2
#define TI_DP_H_ACTIVE1_VP0                 0x3
#define TI_VID_H_ACTIVE0_VP0                0x10
#define TI_VID_H_ACTIVE1_VP0                0x11
#define TI_VID_H_BACK0_VP0                  0x12
#define TI_VID_H_BACK1_VP0                  0x13
#define TI_VID_H_WIDTH0_VP0                 0x14
#define TI_VID_H_WIDTH1_VP0                 0x15
#define TI_VID_H_TOTAL0_VP0                 0x16
#define TI_VID_H_TOTAL1_VP0                 0x17
#define TI_VID_V_ACTIVE0_VP0                0x18
#define TI_VID_V_ACTIVE1_VP0                0x19
#define TI_VID_V_BACK0_VP0                  0x1A
#define TI_VID_V_BACK1_VP0                  0x1B
#define TI_VID_V_WIDTH0_VP0                 0x1C
#define TI_VID_V_WIDTH1_VP0                 0x1D
#define TI_VID_V_FRONT0_VP0                 0x1F
#define TI_VID_V_FRONT1_VP0                 0x1E
#define TI_VID_PROC_CFG2_VP0                0x27
#define TI_PCLK_GEN_M_0_VP0                 0x23
#define TI_PCLK_GEN_M_1_VP0                 0x24
#define TI_PCLK_GEN_N_VP0                   0x25

#define TI_VP_CONFIG_REG                    0x43
#define TI_VP_CONFIG_REG_VID_STREAM_1       0x0
#define TI_VP_CONFIG_REG_VID_STREAM_2       0x1
#define TI_VP_CONFIG_REG_VID_STREAM_3       0x2
#define TI_VP_CONFIG_REG_VID_STREAM_4       0x3
#define TI_VP_ENABLE_REG                    0x44
#define TI_VP_ENABLE_REG_VP0_ENABLE         0x1
#define TI_VP_ENABLE_REG_VP1_ENABLE         0x2
#define TI_VP_ENABLE_REG_VP2_ENABLE         0x4
#define TI_VP_ENABLE_REG_VP3_ENABLE         0x8

#define TI_HDCP_TX_ID0                      0xF0
#define TI_HDCP_TX_ID0_VAL                  0x5F
#define TI_HDCP_TX_ID1                      0xF1
#define TI_HDCP_TX_ID1_VAL                  0x55
#define TI_HDCP_TX_ID2                      0xF2
#define TI_HDCP_TX_ID2_VAL_1                0x42
#define TI_HDCP_TX_ID2_VAL_2                0x48
#define TI_HDCP_TX_ID3                      0xF3
#define TI_HDCP_TX_ID3_VAL                  0x39
#define TI_HDCP_TX_ID4                      0xF4
#define TI_HDCP_TX_ID4_VAL                  0x38
#define TI_HDCP_TX_ID5                      0xF5
#define TI_HDCP_TX_ID5_VAL                  0x33
#define TI_HDCP_TX_ID6                      0xF6
#define TI_HDCP_TX_ID6_VAL                  0x30

// Custom Macros
#define TI_FPD_LINK_COUNT                   0x2

struct ti_fpdlink_dp_ser_source {
	struct fwnode_handle *fwnode;
};

static const struct regmap_config ti_fpdlink_dp_ser_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
};

struct view_port {
	u16 h_active;
	u16 h_back_porch;
	u16 h_width;
	u16 h_total;
	u16 v_active;
	u16 v_back_porch;
	u16 v_width;
	u16 v_front_porch;
};

struct ti_fpdlink_dp_ser_priv {
	struct i2c_client *client;
	struct gpio_desc *gpiod_pwrdn;
	struct regmap *regmap;
	u8 dprx_lane_count;
	u8 dprx_link_rate;
	struct view_port vp;
	bool link_0_select;
	bool link_1_select;
};

static int ti_i2c_read(struct ti_fpdlink_dp_ser_priv *priv, u8 reg)
{
	int ret = 0;
	int val = 0;

	ret = regmap_read(priv->regmap, reg, &val);
	if (ret < 0)
		dev_err(&priv->client->dev,
			"%s: register 0x%02x read failed (%d)\n",
			__func__, reg, ret);

	return val;
}

static int ti_i2c_write(struct ti_fpdlink_dp_ser_priv *priv, u8 reg, int val)
{
	int ret = 0;

	ret = regmap_write(priv->regmap, reg, val);
	if (ret < 0)
		dev_err(&priv->client->dev,
			"%s: register 0x%02x write failed (%d)\n",
			__func__, reg, ret);

	return ret;
}

static int ti_page_indirect_i2c_write(struct ti_fpdlink_dp_ser_priv *priv,
	u32 page, u32 buffer[][2], u32 length)
{
	int i, ret = 0;

	// set page at i2c indirect register 0x40
	ret = ti_i2c_write(priv, TI_IND_ACC_CTL, page);
	if (ret < 0)
		goto fail;

	for (i = 0; i < length; i++) {
		// write address at i2c indirect register 0x41
		ret = ti_i2c_write(priv, TI_IND_ACC_ADDR, buffer[i][0]);
		if (ret < 0)
			goto fail;

		// write data at i2c indirect register 0x42
		ret = ti_i2c_write(priv, TI_IND_ACC_DATA, buffer[i][1]);
		if (ret < 0)
			goto fail;
	}

fail:
	if (ret < 0)
		dev_err(&priv->client->dev, "%s: failed (%d)\n", __func__, ret);

	return ret;
}

static void ti_fpdlink_dp_apb_write(struct ti_fpdlink_dp_ser_priv *priv,
		u8 apb_addr[2], u8 apb_data[4])
{
	// addr 0
	ti_i2c_write(priv, TI_APB_ADDR0, apb_addr[0]);

	// addr 1
	ti_i2c_write(priv, TI_APB_ADDR1, apb_addr[1]);

	// data 0
	ti_i2c_write(priv, TI_APB_DATA0, apb_data[0]);

	// data 1
	ti_i2c_write(priv, TI_APB_DATA1, apb_data[1]);

	// data 2
	ti_i2c_write(priv, TI_APB_DATA2, apb_data[2]);

	// data 3
	ti_i2c_write(priv, TI_APB_DATA3, apb_data[3]);
}

static void ti_program_pll_port_0(struct ti_fpdlink_dp_ser_priv *priv)
{
	u32 page = 0;
	u32 buf[20][2];

	ti_i2c_write(priv, TI_TX_PORT_SEL, TI_TX_PORT_SEL_VAL_PORT_0);

	// Set FPD4 on port 0
	{
		page = TI_IND_ACC_PAGE_9 << TI_IND_ACC_CTL_SEL_SHIFT;
		// Register offset not present in datasheet but present in reference script
		buf[0][0] = 0x84;
		buf[0][1] = 0x2;
		ti_page_indirect_i2c_write(priv, page, buf, 1);
	}

	// Program PLL page - port0
	{
		page = TI_IND_ACC_PAGE_2 << TI_IND_ACC_CTL_SEL_SHIFT;
		buf[0][0] = TI_NDIV_7_0_CH0;
		buf[0][1] = TI_NDIV_7_0_CH0_VAL;
		buf[1][0] = TI_PDIV_CH0;
		buf[1][1] = TI_PDIV_CH0_VAL;
		ti_page_indirect_i2c_write(priv, page, buf, 2);
	}

	// Select port 0 and set sampling rate
	{
		ti_i2c_write(priv, TI_TX_PORT_SEL, TI_TX_PORT_SEL_VAL_PORT_0);
		// Register offset not present in datasheet but present in reference script
		ti_i2c_write(priv, 0x6a, 0x4a);
		ti_i2c_write(priv, 0x6e, 0x80);
	}

	// Set halfrate mode
	{
		ti_i2c_write(priv, TI_GENERAL_CFG2, TI_GENERAL_CFG2_VAL);
	}

	// PLLs
	{
		page = TI_IND_ACC_PAGE_2 << TI_IND_ACC_CTL_SEL_SHIFT;
		// Zero Out PLL
		{
			buf[0][0] = TI_MASH_ORDER_CH0;
			buf[0][1] = TI_MASH_ORDER_CH0_VAL;
			buf[1][0] = TI_NUM_7_0_CH0;
			buf[1][1] = TI_NUM_7_0_CH0_VAL;
			buf[2][0] = TI_NUM_15_8_CH0;
			buf[2][1] = TI_NUM_15_8_CH0_VAL;
			buf[3][0] = TI_NUM_23_16_CH0;
			buf[3][1] = TI_NUM_23_16_CH0_VAL;
			ti_page_indirect_i2c_write(priv, page, buf, 4);
		}
		// Configure VCO
		{
			buf[0][0] = TI_VCO_CH0;
			buf[0][1] = TI_VCO_CH_SEL_VCO_2;
			ti_page_indirect_i2c_write(priv, page, buf, 1);
			// soft reset PLL
			ti_i2c_write(priv, TI_RESET_CTL, TI_RESET_CTL_VAL_SOFT_RESET_PLL);
		}
		// Enable PLL
		{
			// Below registers not given in data sheet.
			buf[0][0] = 0x1b;
			buf[0][1] = 0x0;
			ti_page_indirect_i2c_write(priv, page, buf, 1);
		}
	}

}

static void ti_program_pll_port_1(struct ti_fpdlink_dp_ser_priv *priv)
{
	u32 page = 0;
	u32 buf[20][2];

	ti_i2c_write(priv, TI_TX_PORT_SEL, TI_TX_PORT_SEL_VAL_PORT_1);

	// Set FPD4 on port 1
	{
		page = TI_IND_ACC_PAGE_9 << TI_IND_ACC_CTL_SEL_SHIFT;
		// Register offset not present in datasheet but present in reference script
		buf[0][0] = 0x94;
		buf[0][1] = 0x2;
		ti_page_indirect_i2c_write(priv, page, buf, 1);
	}

	// Program PLL page - port1
	{
		page = TI_IND_ACC_PAGE_2 << TI_IND_ACC_CTL_SEL_SHIFT;
		buf[0][0] = TI_NDIV_7_0_CH1;
		buf[0][1] = TI_NDIV_7_0_CH1_VAL;
		buf[1][0] = TI_PDIV_CH1;
		buf[1][1] = TI_PDIV_CH1_VAL;
		ti_page_indirect_i2c_write(priv, page, buf, 2);
	}

	// Select port 1  and set sampling rate
	{
		ti_i2c_write(priv, TI_TX_PORT_SEL, TI_TX_PORT_SEL_VAL_PORT_1);
		// Register offset not present in datasheet but present in reference script
		ti_i2c_write(priv, 0x6a, 0x4a);
		ti_i2c_write(priv, 0x6e, 0x80);
	}

	// Set halfrate mode
	{
		ti_i2c_write(priv, TI_GENERAL_CFG2, TI_GENERAL_CFG2_VAL);
	}

	{
		page = TI_IND_ACC_PAGE_2 << TI_IND_ACC_CTL_SEL_SHIFT;
		// Zero Out PLL
		{
			buf[0][0] = TI_MASH_ORDER_CH1;
			buf[0][1] = TI_MASH_ORDER_CH1_VAL;
			buf[1][0] = TI_NUM_7_0_CH1;
			buf[1][1] = TI_NUM_7_0_CH1_VAL;
			buf[2][0] = TI_NUM_15_8_CH1;
			buf[2][1] = TI_NUM_15_8_CH1_VAL;
			buf[3][0] = TI_NUM_23_16_CH1;
			buf[3][1] = TI_NUM_23_16_CH1_VAL;
			ti_page_indirect_i2c_write(priv, page, buf, 4);
		}
		// Configure VCO
		{
			buf[0][0] = TI_VCO_CH1;
			buf[0][1] = TI_VCO_CH_SEL_VCO_2;
			ti_page_indirect_i2c_write(priv, page, buf, 1);
			// soft reset PLL
			ti_i2c_write(priv, TI_RESET_CTL, TI_RESET_CTL_VAL_SOFT_RESET_PLL);
		}
		// Enable PLL
		{
			// Below registers not given in data sheet.
			buf[0][0] = 0x5b;
			buf[0][1] = 0x0;
			ti_page_indirect_i2c_write(priv, page, buf, 2);
		}
	}

}
/* Set FPD4 and program PLL for both ports */
static void ti_program_pll(struct ti_fpdlink_dp_ser_priv *priv)
{
	u32 page = 0;
	u32 buf[20][2];

	// Disable PLL - PLL0 and PLL1
	{
		page = TI_IND_ACC_PAGE_2 << TI_IND_ACC_CTL_SEL_SHIFT;
		buf[0][0] = TI_PLL_0;
		buf[0][1] = TI_PLL_DISABLE_VAL << TI_PLL_DISABLE_SHIFT;
		buf[1][0] = TI_PLL_1;
		buf[1][1] = TI_PLL_DISABLE_VAL << TI_PLL_DISABLE_SHIFT;
		ti_page_indirect_i2c_write(priv, page, buf, 2);
	}

	// DISABLE FPD3 FIFO pass through, Enable FPD4 independent mode.
	{
		// These are default values from data sheet.
		ti_i2c_write(priv, TI_FPD3_FIFO_CFG, TI_FPD3_FIFO_CFG_VAL);
		ti_i2c_write(priv, TI_FPD4_CFG, TI_FPD4_CFG_FPD4_TX_MODE_IND);
		ti_i2c_write(priv, TI_GENERAL_CFG2, TI_GENERAL_CFG2_VAL);
	}

	if (priv->link_0_select)
		ti_program_pll_port_0(priv);

	if (priv->link_1_select)
		ti_program_pll_port_1(priv);

	// soft reset Ser
	ti_i2c_write(priv, TI_RESET_CTL, TI_RESET_CTL_VAL_SOFT_RESET_SER);

	// Enable I2C Passthrough
	{
		int pass_thr_val = ti_i2c_read(priv, TI_GENERAL_CFG);
		int pass_thr_reg = pass_thr_val | TI_GENERAL_CFG_I2C_PASS_THROUGH_MASK;

		ti_i2c_write(priv, 0x7, pass_thr_reg);
	}
}

static void ti_program_dp_config(struct ti_fpdlink_dp_ser_priv *priv)
{
	u32 page = 0;
	u32 buf[20][2];
	u8 apb_addr[2];
	u8 apb_data[4];

	// Select ser link layer
	if (priv->link_0_select) {
		page = TI_IND_ACC_PAGE_11 << TI_IND_ACC_CTL_SEL_SHIFT;
		buf[0][0] = TI_LINK_LAYER_CTL;
		buf[0][1] = TI_LINK_LAYER_CTL_EN_NEW_TSLOT0 | TI_LINK_LAYER_CTL_LINK_LAYER_0_EN;
		ti_page_indirect_i2c_write(priv, page, buf, 1);
		// select write to port 0
		ti_i2c_write(priv, TI_TX_PORT_SEL, TI_TX_PORT_SEL_VAL_PORT_0);
	}

	if (priv->link_1_select) {
		page = TI_IND_ACC_PAGE_11 << TI_IND_ACC_CTL_SEL_SHIFT;
		buf[0][0] = TI_LINK_LAYER_CTL;
		buf[0][1] = TI_LINK_LAYER_CTL_EN_NEW_TSLOT0 | TI_LINK_LAYER_CTL_LINK_LAYER_1_EN;
		ti_page_indirect_i2c_write(priv, page, buf, 1);
		// select write to port 1
		ti_i2c_write(priv, TI_TX_PORT_SEL, TI_TX_PORT_SEL_VAL_PORT_1);
	}

	// Set DP config
	{
		// Enable APB interface
		ti_i2c_write(priv, TI_APB_CTL, TI_APB_CTL_EN);

		// Force HPD low
		{
			memset(apb_addr, 0, 2);
			memset(apb_data, 0, 4);
			apb_addr[0] = TI_APB_LINK_ENABLE;
			apb_data[0] = TI_APB_LINK_ENABLE_LOW;
			ti_fpdlink_dp_apb_write(priv, apb_addr, apb_data);
		}

		// Set max advertised link rate.
		{
			memset(apb_addr, 0, 2);
			memset(apb_data, 0, 4);
			apb_addr[0] = TI_APB_MAX_LINK_RATE;
			apb_data[0] = priv->dprx_link_rate;
			ti_fpdlink_dp_apb_write(priv, apb_addr, apb_data);
		}

		// Set max advertised lane count.
		{
			memset(apb_addr, 0, 2);
			memset(apb_data, 0, 4);
			apb_addr[0] = TI_APB_MAX_LANE_COUNT;
			apb_data[0] = priv->dprx_lane_count;
			ti_fpdlink_dp_apb_write(priv, apb_addr, apb_data);
		}

		// Request min VOD swing of 0x02
		{
			memset(apb_addr, 0, 2);
			memset(apb_data, 0, 4);
			// below register not given in data sheet
			apb_addr[0] = 0x14;
			apb_addr[1] = 2;
			apb_data[0] = 2;
			ti_fpdlink_dp_apb_write(priv, apb_addr, apb_data);
		}

		// Set SST/MST mode and DP/eDP Mode. Add support for MST in future.
		{
			// This is set to default value
			memset(apb_addr, 0, 2);
			memset(apb_data, 0, 4);
			apb_addr[0] = TI_APB_MISC_CONFIG;
			apb_data[0] = TI_APB_MISC_CONFIG_MAX_DOWNSPREAD_CONFIG
						| TI_APB_MISC_CONFIG_DISABLE_INACTIVE_COUNT;
			ti_fpdlink_dp_apb_write(priv, apb_addr, apb_data);
		}

		// Disable line reset for VS0
		{
			// below register not given in data sheet
			memset(apb_addr, 0, 2);
			memset(apb_data, 0, 4);
			apb_addr[0] = 0xc;
			apb_addr[1] = 0xa;
			apb_data[0] = 1;
			ti_fpdlink_dp_apb_write(priv, apb_addr, apb_data);
		}

		// Force HPD high to trigger link training
		{
			memset(apb_addr, 0, 2);
			memset(apb_data, 0, 4);
			apb_addr[0] = TI_APB_LINK_ENABLE;
			apb_data[0] = TI_APB_LINK_ENABLE_HIGH;
			ti_fpdlink_dp_apb_write(priv, apb_addr, apb_data);
		}
	}
}

static void ti_program_viewport_timing(struct ti_fpdlink_dp_ser_priv *priv)
{
	u32 page = 0;
	u32 buf[20][2];
	u8 apb_addr[2];
	u8 apb_data[4];

	// Program VP config
	{
		//Set VP_SRC_SELECT to Stream 0 for SST Mode and program timings
		page = TI_IND_ACC_PAGE_12 << TI_IND_ACC_CTL_SEL_SHIFT;
		page = page | TI_IND_ACC_CTL_AUTO_INC_MASK;
		// set page address with autoincrement writes.
		// ie. If first register is set to 0x2 (using 0x41) then first write will be to 0x2,
		// next write will 0x3 and so on.
		ti_i2c_write(priv, TI_IND_ACC_CTL, page);
		ti_i2c_write(priv, TI_IND_ACC_ADDR, TI_VID_PROC_CFG_VP0);
		ti_i2c_write(priv, TI_IND_ACC_DATA, TI_VID_PROC_CFG_VP0_DEFAULT);
		// VID H Active
		ti_i2c_write(priv, TI_IND_ACC_ADDR, TI_DP_H_ACTIVE0_VP0);
		ti_i2c_write(priv, TI_IND_ACC_DATA, priv->vp.h_active & 0xFF);
		ti_i2c_write(priv, TI_IND_ACC_DATA, (priv->vp.h_active & 0xFF00) >> 8);
		//Horizontal Active - VID_H_ACTIVE0_VP0
		ti_i2c_write(priv, TI_IND_ACC_ADDR, TI_VID_H_ACTIVE0_VP0);
		ti_i2c_write(priv, TI_IND_ACC_DATA, priv->vp.h_active & 0xFF);
		ti_i2c_write(priv, TI_IND_ACC_DATA, (priv->vp.h_active & 0xFF00) >> 8);
		//Horizontal Back Porch - VID_H_BACK0_VP0
		ti_i2c_write(priv, TI_IND_ACC_DATA, priv->vp.h_back_porch & 0xFF);
		ti_i2c_write(priv, TI_IND_ACC_DATA, (priv->vp.h_back_porch & 0xFF00) >> 8);
		//Horizontal Sync - VID_H_WIDTH0_VP0
		ti_i2c_write(priv, TI_IND_ACC_DATA, priv->vp.h_width & 0xFF);
		ti_i2c_write(priv, TI_IND_ACC_DATA, (priv->vp.h_width & 0xFF00) >> 8);
		//Horizontal Total - VID_H_TOTAL0_VP0
		ti_i2c_write(priv, TI_IND_ACC_DATA, priv->vp.h_total & 0xFF);
		ti_i2c_write(priv, TI_IND_ACC_DATA, (priv->vp.h_total & 0xFF00) >> 8);
		//Vertical Active - VID_V_ACTIVE0_VP0
		ti_i2c_write(priv, TI_IND_ACC_DATA, priv->vp.v_active & 0xFF);
		ti_i2c_write(priv, TI_IND_ACC_DATA, (priv->vp.v_active & 0xFF00) >> 8);
		//Vertical Back Porch - VID_V_BACK0_VP0
		ti_i2c_write(priv, TI_IND_ACC_DATA, priv->vp.v_back_porch & 0xFF);
		ti_i2c_write(priv, TI_IND_ACC_DATA, (priv->vp.v_back_porch & 0xFF00) >> 8);
		//Vertical Sync - VID_V_WIDTH0_VP0
		ti_i2c_write(priv, TI_IND_ACC_DATA, priv->vp.v_width & 0xFF);
		ti_i2c_write(priv, TI_IND_ACC_DATA, (priv->vp.v_width & 0xFF00) >> 8);
		//Vertical Front Porch - VID_V_FRONT0_VP0
		ti_i2c_write(priv, TI_IND_ACC_DATA, priv->vp.v_front_porch & 0xFF);
		ti_i2c_write(priv, TI_IND_ACC_DATA, (priv->vp.v_front_porch & 0xFF00) >> 8);
		//HSYNC Polarity = +, VSYNC Polarity = +, - VID_PROC_CFG2_VP0
		ti_i2c_write(priv, TI_IND_ACC_ADDR, TI_VID_PROC_CFG2_VP0);
		ti_i2c_write(priv, TI_IND_ACC_DATA, 0x0);
		//M/N Register - M,M and N value
		ti_i2c_write(priv, TI_IND_ACC_ADDR, TI_PCLK_GEN_M_0_VP0);
		ti_i2c_write(priv, TI_IND_ACC_DATA, 0x14);
		ti_i2c_write(priv, TI_IND_ACC_DATA, 0xe);
		ti_i2c_write(priv, TI_IND_ACC_DATA, 0xf);
	}

	// Enable VP
	{
		//Set number of VPs used = 1
		ti_i2c_write(priv, TI_VP_CONFIG_REG, TI_VP_CONFIG_REG_VID_STREAM_1);
		//Enable video processors
		ti_i2c_write(priv, TI_VP_ENABLE_REG, TI_VP_ENABLE_REG_VP0_ENABLE);
	}

	// Video Input Reset
	{
		memset(apb_addr, 0, 2);
		memset(apb_data, 0, 4);
		apb_addr[0] = TI_APB_VIDEO_INPUT_RESET;
		apb_data[0] = TI_APB_VIDEO_INPUT_RESET_TRIGGER;
		ti_fpdlink_dp_apb_write(priv, apb_addr, apb_data);
	}

	{
		// Set color depth to 24 bpp for VP0
		page = TI_IND_ACC_PAGE_12  << TI_IND_ACC_CTL_SEL_SHIFT;
		buf[0][0] = TI_FPD4_PGCFG_VP0;
		buf[0][1] = TI_FPD4_PGCFG_DEFAULT;
		ti_page_indirect_i2c_write(priv, page, buf, 1);
		// Soft Reset PLL
		ti_i2c_write(priv, TI_RESET_CTL, TI_RESET_CTL_VAL_SOFT_RESET_PLL);
	}

	// Configure TX Link Layer 0 - stream 0 enable, time slot 0, vp bpp, enable
	if (priv->link_0_select) {
		page = TI_IND_ACC_PAGE_11 << TI_IND_ACC_CTL_SEL_SHIFT;
		buf[0][0] = TI_LINK0_STREAM_EN;
		buf[0][1] = TI_LINK_STREAM_EN0;
		buf[1][0] = TI_LINK0_SLOT_REQ0;
		buf[1][1] = 0x3c; // fixed value, find out why?
		buf[2][0] = TI_VP_WIDTH0;
		buf[2][1] = 0x55; // default set to 24 bit. Need to fetch from DTS?
		buf[3][0] = TI_LINK_LAYER_CTL;
		buf[3][1] = TI_LINK_LAYER_CTL_EN_NEW_TSLOT0 | TI_LINK_LAYER_CTL_LINK_LAYER_0_EN;
		ti_page_indirect_i2c_write(priv, page, buf, 4);
	}

	if (priv->link_1_select) {
		page = TI_IND_ACC_PAGE_11 << TI_IND_ACC_CTL_SEL_SHIFT;
		buf[0][0] = TI_LINK1_STREAM_EN;
		buf[0][1] = TI_LINK_STREAM_EN0;
		buf[1][0] = TI_LINK1_SLOT_REQ0;
		buf[1][1] = 0x3c; // fixed value, find out why?
		buf[2][0] = TI_VP_WIDTH0;
		buf[2][1] = 0x55; // default set to 24 bit. Need to fetch from DTS?
		buf[3][0] = TI_LINK_LAYER_CTL;
		buf[3][1] = TI_LINK_LAYER_CTL_EN_NEW_TSLOT0 | TI_LINK_LAYER_CTL_LINK_LAYER_1_EN;
		ti_page_indirect_i2c_write(priv, page, buf, 4);
	}
}

static int ti_fpdlink_dp_ser_init(struct device *dev)
{
	struct ti_fpdlink_dp_ser_priv *priv;
	struct i2c_client *client;
	int ret = 0;

	client = to_i2c_client(dev);
	priv = i2c_get_clientdata(client);

	priv->gpiod_pwrdn = devm_gpiod_get_optional(&client->dev, "enable",
						    GPIOD_OUT_HIGH);
	if (IS_ERR(priv->gpiod_pwrdn)) {
		dev_err(dev, "%s: gpiopwrdn is not enabled\n", __func__);
		return PTR_ERR(priv->gpiod_pwrdn);
	}
	gpiod_set_consumer_name(priv->gpiod_pwrdn, "ti_fpdlink_dp_ser-pwrdn");

	/* Drive PWRDNB pin high to power up the serializer */
	gpiod_set_value_cansleep(priv->gpiod_pwrdn, 1);

	/* Wait ~20ms for link to establish after power up */
	usleep_range(20000, 21000);

	// Init addresses
	ti_i2c_write(priv, TI_TARGET_ID_0, TI_TARGET_ID_VAL);
	ti_i2c_write(priv, TI_TARGET_ALIAS_ID_0, TI_TARGET_ALIAS_ID_VAL);
	ti_i2c_write(priv, TI_TARGET_DEST_0, TI_TARGET_DEST_VAL);

	ti_program_pll(priv);

	ti_program_dp_config(priv);

	ti_program_viewport_timing(priv);

	return ret;
}

static int ti_fpdlink_dp_ser_parse_dt_link_config(struct i2c_client *client,
				struct ti_fpdlink_dp_ser_priv *priv)
{
	struct device *dev = &priv->client->dev;
	struct device_node *ser = dev->of_node;
	int err = 0;
	u32 fpd_link_select[TI_FPD_LINK_COUNT] = { 0 };

	err = of_property_read_variable_u32_array(ser, "fpd-link-select",
						 fpd_link_select, 1,
						 ARRAY_SIZE(fpd_link_select));
	if (err < 0) {
		dev_err(dev,
			 "%s: fpd-link-select property not found or invalid\n",
			 __func__);
		return -EINVAL;
	}

	if (fpd_link_select[0] == 1)
		priv->link_0_select = true;

	if (fpd_link_select[1] == 1)
		priv->link_1_select = true;

	if (priv->link_0_select && priv->link_1_select) {
		dev_err(dev,
			 "%s: fpd-link-select: both link cannot be selected\n",
			 __func__);
		return -EINVAL;
	}

	if (!(priv->link_0_select || priv->link_1_select)) {
		dev_err(dev,
			 "%s: fpd-link-select: No link is selected, enabling link 0\n",
			 __func__);
		priv->link_0_select = true;
	}

	dev_info(dev,
		"%s: fpd-link-select link_0_select = %d, link_1_select = %d\n",
		__func__, priv->link_0_select, priv->link_1_select);

	return err;
}

static int ti_fpdlink_dp_ser_parse_dt_dp_config(struct i2c_client *client,
				struct ti_fpdlink_dp_ser_priv *priv)
{
	struct device *dev = &priv->client->dev;
	struct device_node *ser = dev->of_node;
	int err = 0;
	u32 val = 0;

	dev_info(dev, "%s: parsing serializer device tree:\n", __func__);

	err = of_property_read_u32(ser, "dprx-lane-count", &val);
	if (err) {
		if (err == -EINVAL) {
			dev_err(dev, "%s: - dprx-lane-count property not found\n",
				 __func__);
			/* default value: 4 */
			priv->dprx_lane_count = 4;
			dev_err(dev, "%s: dprx-lane-count set to default val: 4\n",
				 __func__);
		} else {
			return err;
		}
	} else {
		/* set dprx-lane-count */
		if ((val == TI_APB_MAX_LANE_COUNT_1) ||
			(val == TI_APB_MAX_LANE_COUNT_2) ||
			(val == TI_APB_MAX_LANE_COUNT_4)) {
			priv->dprx_lane_count = val;
		} else {
			dev_err(dev, "%s: - invalid lane-count %i from DTS\n",
				 __func__, val);
			return err;
		}
	}

	err = of_property_read_u32(ser, "dprx-link-rate", &val);
	if (err) {
		if (err == -EINVAL) {
			dev_err(dev, "%s: - dprx-link-rate property not found\n",
				 __func__);
			/* default value: 0x14 which is 5.4 Gbps */
			priv->dprx_link_rate = TI_APB_MAX_LINK_RATE_540;
			dev_err(dev, "%s: dprx-link-rate set to default val: 0x14\n",
				 __func__);
		} else {
			return err;
		}
	} else {
		/* set dprx-link-rate*/
		if ((val == TI_APB_MAX_LINK_RATE_162) ||
			(val == TI_APB_MAX_LINK_RATE_270) ||
			(val == TI_APB_MAX_LINK_RATE_540) ||
			(val == TI_APB_MAX_LINK_RATE_810)) {
			priv->dprx_link_rate = val;
			dev_info(dev, "%s: - dprx-link-rate %i\n", __func__, val);
		} else {
			dev_err(dev, "%s: - invalid link-count %i from DTS\n",
				 __func__, val);
			return err;
		}
	}

	return 0;
}

static int ti_fpdlink_dp_ser_parse_dt_viewport_config(struct i2c_client *client,
				struct ti_fpdlink_dp_ser_priv *priv)
{
	struct device *dev = &priv->client->dev;
	struct device_node *ser = dev->of_node;
	int err = 0;
	u32 val = 0;
	struct device_node *timings_handle;

	err = of_property_read_u32(ser, "timings-phandle", &val);
	if (err) {
		dev_err(dev, "%s: - timings-phandle property not found\n",
			 __func__);
		return err;
	}

	timings_handle = of_find_node_by_phandle(val);
	if (timings_handle == NULL) {
		dev_err(dev, "%s: - can not get timings node\n",
			 __func__);
		return -1;
	}

	err = of_property_read_u32(timings_handle, "hactive", &val);
	if (err) {
		dev_err(dev, "%s: - hactive property not found\n",
			 __func__);
		return err;
	}
	priv->vp.h_active = val;
	dev_info(dev, "%s: - hactive - %i\n", __func__, val);

	err = of_property_read_u32(timings_handle, "hback-porch", &val);
	if (err) {
		dev_err(dev, "%s: - hback-porch property not found\n",
			 __func__);
		return err;
	}
	priv->vp.h_back_porch = val;
	dev_info(dev, "%s: - hback-porch - %i\n", __func__, val);

	err = of_property_read_u32(timings_handle, "hsync-len", &val);
	if (err) {
		dev_err(dev, "%s: - hsync-len property not found\n",
			 __func__);
		return err;
	}
	priv->vp.h_width = val;
	dev_info(dev, "%s: - hsync-len - %i\n", __func__, val);

	err = of_property_read_u32(timings_handle, "hfront-porch", &val);
	if (err) {
		dev_err(dev, "%s: - hfront-porch property not found\n",
			 __func__);
		return err;
	}
	priv->vp.h_total = val + priv->vp.h_width + priv->vp.h_back_porch + priv->vp.h_active;
	dev_info(dev, "%s: - htotal - %i\n", __func__, priv->vp.h_total);

	err = of_property_read_u32(timings_handle, "vactive", &val);
	if (err) {
		dev_err(dev, "%s: - vactive property not found\n",
			 __func__);
		return err;
	}
	priv->vp.v_active = val;
	dev_info(dev, "%s: - vactive - %i\n", __func__, val);

	err = of_property_read_u32(timings_handle, "vback-porch", &val);
	if (err) {
		dev_err(dev, "%s: - vback-porch property not found\n",
			 __func__);
		return err;
	}
	priv->vp.v_back_porch = val;
	dev_info(dev, "%s: - vback-porch - %i\n", __func__, val);

	err = of_property_read_u32(timings_handle, "vsync-len", &val);
	if (err) {
		dev_err(dev, "%s: - vsync-len property not found\n",
			 __func__);
		return err;
	}
	priv->vp.v_width = val;
	dev_info(dev, "%s: - v-width - %i\n", __func__, val);

	err = of_property_read_u32(timings_handle, "vfront-porch", &val);
	if (err) {
		dev_err(dev, "%s: - v-front-porch property not found\n",
			 __func__);
		return err;
	}
	priv->vp.v_front_porch = val;
	dev_info(dev, "%s: - v-front-porch - %i\n", __func__, val);

	return 0;
}

static int ti_fpdlink_dp_ser_probe(struct i2c_client *client)
{
	struct ti_fpdlink_dp_ser_priv *priv;
	struct device *dev;
	int ret = 0;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;

	priv->client = client;
	i2c_set_clientdata(client, priv);

	priv->regmap = devm_regmap_init_i2c(client, &ti_fpdlink_dp_ser_i2c_regmap);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	dev = &priv->client->dev;

	ret = ti_fpdlink_dp_ser_parse_dt_link_config(client, priv);
	if (ret < 0) {
		dev_err(dev, "%s: error parsing device tree for link config\n", __func__);
		return -EFAULT;
	}

	ret = ti_fpdlink_dp_ser_parse_dt_dp_config(client, priv);
	if (ret < 0) {
		dev_err(dev, "%s: error parsing device tree for dp config\n", __func__);
		return -EFAULT;
	}

	ret = ti_fpdlink_dp_ser_parse_dt_viewport_config(client, priv);
	if (ret < 0) {
		dev_err(dev, "%s: error parsing device tree for vp config\n", __func__);
		return -EFAULT;
	}

	dev_err(dev, "%s: TI Serializer initialization started\n", __func__);
	ret = ti_fpdlink_dp_ser_init(&client->dev);
	if (ret < 0) {
		dev_err(dev, "%s: TI Serializer init failed\n", __func__);
		return -EFAULT;
	}
	dev_err(dev, "%s: TI Serializer initialization completed\n", __func__);

	return ret;
}

#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
static int ti_fpdlink_dp_ser_remove(struct i2c_client *client)
#else
static void ti_fpdlink_dp_ser_remove(struct i2c_client *client)
#endif
{
	struct ti_fpdlink_dp_ser_priv *priv = i2c_get_clientdata(client);

	i2c_unregister_device(client);
	gpiod_set_value_cansleep(priv->gpiod_pwrdn, 0);

#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
	return 0;
#endif
}

static const struct of_device_id ti_fpdlink_dp_ser_dt_ids[] = {
	{ .compatible = "ti,ti_fpdlink_dp_ser" },
	{},
};
MODULE_DEVICE_TABLE(of, ti_fpdlink_dp_ser_dt_ids);

static struct i2c_driver ti_fpdlink_dp_ser_i2c_driver = {
	.driver	= {
		.name		= "ti_fpdlink_dp_ser",
		.of_match_table	= of_match_ptr(ti_fpdlink_dp_ser_dt_ids),
	},
#if defined(NV_I2C_DRIVER_STRUCT_HAS_PROBE_NEW) /* Dropped on Linux 6.6 */
	.probe_new	= ti_fpdlink_dp_ser_probe,
#else
	.probe		= ti_fpdlink_dp_ser_probe,
#endif
	.remove		= ti_fpdlink_dp_ser_remove,
};

module_i2c_driver(ti_fpdlink_dp_ser_i2c_driver);

MODULE_DESCRIPTION("TI DP FPDLINK Serializer Driver");
MODULE_AUTHOR("NVIDIA CORP");
MODULE_LICENSE("GPL");
