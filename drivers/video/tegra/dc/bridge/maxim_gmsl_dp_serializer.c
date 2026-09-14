// SPDX-License-Identifier: GPL-2.0-only
/*
 * MAXIM DP Serializer driver for MAXIM GMSL Serializers
 *
 * Copyright (c) 2021-2023, NVIDIA CORPORATION.  All rights reserved.
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
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/of_gpio.h>
#include <linux/workqueue.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/version.h>

#define MAX_GMSL_DP_SER_REG_4			0x4
#define MAX_GMSL_DP_SER_REG_4_GMSL_A		(1 << 6)
#define MAX_GMSL_DP_SER_REG_4_GMSL_A_PAM4_VAL	(1 << 6)
#define MAX_GMSL_DP_SER_REG_4_GMSL_B		(1 << 7)
#define MAX_GMSL_DP_SER_REG_4_GMSL_B_PAM4_VAL	(1 << 7)

#define MAX_GMSL_DP_SER_REG_13			0xD

#define MAX_GMSL_DP_SER_CTRL3			0x13
#define MAX_GMSL_DP_SER_CTRL3_LOCK_MASK		(1 << 3)
#define MAX_GMSL_DP_SER_CTRL3_LOCK_VAL		(1 << 3)

#define MAX_GMSL_DP_SER_INTR2			0x1A
#define MAX_GMSL_DP_SER_REM_ERR_OEN_A_MASK	(1 << 4)
#define MAX_GMSL_DP_SER_REM_ERR_OEN_A_VAL	(1 << 4)
#define MAX_GMSL_DP_SER_REM_ERR_OEN_B_MASK	(1 << 5)
#define MAX_GMSL_DP_SER_REM_ERR_OEN_B_VAL	(1 << 5)

#define MAX_GMSL_DP_SER_INTR3			0x1B
#define MAX_GMSL_DP_SER_REM_ERR_FLAG_A		(1 << 4)
#define MAX_GMSL_DP_SER_REM_ERR_FLAG_B		(1 << 5)

#define MAX_GMSL_DP_SER_INTR8			0x20
#define MAX_GMSL_DP_SER_INTR8_MASK		(1 << 0)
#define MAX_GMSL_DP_SER_INTR8_VAL		0x1

#define MAX_GMSL_DP_SER_INTR9			0x21
#define MAX_GMSL_DP_SER_LOSS_OF_LOCK_FLAG	(1 << 0)

#define MAX_GMSL_DP_SER_LINK_CTRL_PHY_A		0x29
#define MAX_GMSL_DP_SER_LINK_CTRL_A_MASK	(1 << 0)

#define MAX_GMSL_DP_SER_LCTRL0_A			0x28
#define MAX_GMSL_DP_SER_LCTRL0_B			0x32
#define MAX_GMSL_DP_SER_LCTRL0_TX_RATE_MASK		(3 << 2)
#define MAX_GMSL_DP_SER_LCTRL0_TX_RATE_VAL_3GBPS	0x4
#define MAX_GMSL_DP_SER_LCTRL0_TX_RATE_VAL_6GBPS	0x8
#define MAX_GMSL_DP_SER_LCTRL0_TX_RATE_VAL_12GBPS	0xC

#define MAX_GMSL_DP_SER_LCTRL2_A		0x2A
#define MAX_GMSL_DP_SER_LCTRL2_B		0x34
#define MAX_GMSL_DP_SER_LCTRL2_LOCK_MASK	(1 << 0)
#define MAX_GMSL_DP_SER_LCTRL2_LOCK_VAL		0x1

#define MAX_GMSL_DP_SER_LINK_CTRL_PHY_B		0x33
#define MAX_GMSL_DP_SER_LINK_CTRL_B_MASK	(1 << 0)

#define MAX_GMSL_DP_SER_TX0_LINK_A	0x50
#define MAX_GMSL_DP_SER_TX0_LINK_B	0x60
#define MAX_GMSL_DP_SER_TX0_FEC_ENABLE_MASK		(1 << 1)
#define MAX_GMSL_DP_SER_TX0_FEC_ENABLE_VAL		0x2

#define MAX_GMSL_DP_SER_VID_TX_X		0x100
#define MAX_GMSL_DP_SER_VID_TX_Y		0x110
#define MAX_GMSL_DP_SER_VID_TX_Z		0x120
#define MAX_GMSL_DP_SER_VID_TX_U		0x130
#define MAX_GMSL_DP_SER_ENABLE_LINK_A		0x0
#define MAX_GMSL_DP_SER_ENABLE_LINK_B		0x1
#define MAX_GMSL_DP_SER_ENABLE_LINK_AB		0x2

#define MAX_GMSL_DP_SER_VID_TX_MASK		(1 << 0)
#define MAX_GMSL_DP_SER_VID_TX_LINK_MASK	(3 << 1)
#define MAX_GMSL_DP_SER_LINK_SEL_SHIFT_VAL	0x1

#define MAX_GMSL_DP_SER_PHY_EDP_0_CTRL0_B0	0x6064
#define MAX_GMSL_DP_SER_PHY_EDP_0_CTRL0_B1	0x6065
#define MAX_GMSL_DP_SER_PHY_EDP_1_CTRL0_B0	0x6164
#define MAX_GMSL_DP_SER_PHY_EDP_1_CTRL0_B1	0x6165
#define MAX_GMSL_DP_SER_PHY_EDP_2_CTRL0_B0	0x6264
#define MAX_GMSL_DP_SER_PHY_EDP_2_CTRL0_B1	0x6265
#define MAX_GMSL_DP_SER_PHY_EDP_3_CTRL0_B0	0x6364
#define MAX_GMSL_DP_SER_PHY_EDP_3_CTRL0_B1	0x6365

#define MAX_GMSL_DP_SER_DPRX_TRAIN		0x641A
#define MAX_GMSL_DP_SER_DPRX_TRAIN_STATE_MASK	(0xF << 4)
#define MAX_GMSL_DP_SER_DPRX_TRAIN_STATE_VAL	0xF0

#define MAX_GMSL_DP_SER_LINK_ENABLE		0x7000
#define MAX_GMSL_DP_SER_LINK_ENABLE_MASK	(1 << 0)

#define MAX_GMSL_DP_SER_MISC_CONFIG_B1		0x7019
#define MAX_GMSL_DP_SER_MISC_CONFIG_B1_MASK	(1 << 0)
#define MAX_GMSL_DP_SER_MISC_CONFIG_B1_VAL	0x1

#define MAX_GMSL_DP_SER_HPD_INTERRUPT_MASK		0x702D
#define MAX_GMSL_DP_SER_HPD_INTERRUPT_VAL		0x7C

#define MAX_GMSL_DP_SER_MAX_LINK_COUNT		0x7070
#define MAX_GMSL_DP_SER_MAX_LINK_RATE		0x7074

#define MAX_GMSL_DP_SER_LOCAL_EDID		0x7084

#define MAX_GMSL_DP_SER_I2C_SPEED_CAPABILITY		0x70A4
#define MAX_GMSL_DP_SER_I2C_SPEED_CAPABILITY_MASK	(0x3F << 0)
#define MAX_GMSL_DP_SER_I2C_SPEED_CAPABILITY_100KBPS	0x8

#define MAX_GMSL_DP_SER_MST_PAYLOAD_ID_0	0x7904
#define MAX_GMSL_DP_SER_MST_PAYLOAD_ID_1	0x7908
#define MAX_GMSL_DP_SER_MST_PAYLOAD_ID_2	0x790C
#define MAX_GMSL_DP_SER_MST_PAYLOAD_ID_3	0x7910

#define MAX_GMSL_DP_SER_TX3_0		0xA3
#define MAX_GMSL_DP_SER_TX3_1		0xA7
#define MAX_GMSL_DP_SER_TX3_2		0xAB
#define MAX_GMSL_DP_SER_TX3_3		0xAF

#define MAX_GMSL_DP_SER_INTERNAL_CRC_X		0x449
#define MAX_GMSL_DP_SER_INTERNAL_CRC_Y		0x549
#define MAX_GMSL_DP_SER_INTERNAL_CRC_Z		0x649
#define MAX_GMSL_DP_SER_INTERNAL_CRC_U		0x749

#define MAX_GMSL_DP_SER_INTERNAL_CRC_ENABLE		0x9
#define MAX_GMSL_DP_SER_INTERNAL_CRC_ERR_DET		0x4
#define MAX_GMSL_DP_SER_INTERNAL_CRC_ERR_INJ		0x10

/* XY Dual View Configuration Registers */
#define MAX_GMSL_DP_SER_ASYM_14_X		0X04CE
#define MAX_GMSL_DP_SER_ASYM_14_Y		0X05CE
#define MAX_GMSL_DP_SER_ASYM_15_X		0X04CF
#define MAX_GMSL_DP_SER_ASYM_15_Y		0X05CF
#define MAX_GMSL_DP_SER_ASYM_17_X		0X04D1
#define MAX_GMSL_DP_SER_ASYM_17_Y		0X05D1

/* Pipe X Superframe Registers, each next PIPE register at 0x100 offset */
#define MAX_GMSL_DP_SER_X_M_L			0X04C0
#define MAX_GMSL_DP_SER_X_M_M			0X04C1
#define MAX_GMSL_DP_SER_X_M_H			0X04C2
#define MAX_GMSL_DP_SER_X_N_L			0X04C3
#define MAX_GMSL_DP_SER_X_N_M			0X04C4
#define MAX_GMSL_DP_SER_X_N_H			0X04C5
#define MAX_GMSL_DP_SER_X_X_OFFSET_L		0X04C6
#define MAX_GMSL_DP_SER_X_X_OFFSET_H		0X04C7
#define MAX_GMSL_DP_SER_X_X_MAX_L		0X04C8
#define MAX_GMSL_DP_SER_X_X_MAX_H		0X04C9
#define MAX_GMSL_DP_SER_X_Y_MAX_L		0X04CA
#define MAX_GMSL_DP_SER_X_Y_MAX_H		0X04CB
#define MAX_GMSL_DP_SER_X_VS_DLY_L		0X04D8
#define MAX_GMSL_DP_SER_X_VS_DLY_M		0X04D9
#define MAX_GMSL_DP_SER_X_VS_DLY_H		0X04DA
#define MAX_GMSL_DP_SER_X_VS_HIGH_L		0X04DB
#define MAX_GMSL_DP_SER_X_VS_HIGH_M		0X04DC
#define MAX_GMSL_DP_SER_X_VS_HIGH_H		0X04DD
#define MAX_GMSL_DP_SER_X_VS_LOW_L		0X04DE
#define MAX_GMSL_DP_SER_X_VS_LOW_M		0X04DF
#define MAX_GMSL_DP_SER_X_VS_LOW_H		0X04E0
#define MAX_GMSL_DP_SER_X_HS_DLY_L		0X04E1
#define MAX_GMSL_DP_SER_X_HS_DLY_M		0X04E2
#define MAX_GMSL_DP_SER_X_HS_DLY_H		0X04E3
#define MAX_GMSL_DP_SER_X_HS_HIGH_L		0X04E4
#define MAX_GMSL_DP_SER_X_HS_HIGH_H		0X04E5
#define MAX_GMSL_DP_SER_X_HS_LOW_L		0X04E6
#define MAX_GMSL_DP_SER_X_HS_LOW_H		0X04E7
#define MAX_GMSL_DP_SER_X_HS_CNT_L		0X04E8
#define MAX_GMSL_DP_SER_X_HS_CNT_H		0X04E9
#define MAX_GMSL_DP_SER_X_HS_LLOW_L		0X04EA
#define MAX_GMSL_DP_SER_X_HS_LLOW_M		0X04EB
#define MAX_GMSL_DP_SER_X_HS_LLOW_H		0X04EC
#define MAX_GMSL_DP_SER_X_DE_DLY_L		0X04ED
#define MAX_GMSL_DP_SER_X_DE_DLY_M		0X04EE
#define MAX_GMSL_DP_SER_X_DE_DLY_H		0X04EF
#define MAX_GMSL_DP_SER_X_DE_HIGH_L		0X04F0
#define MAX_GMSL_DP_SER_X_DE_HIGH_H		0X04F1
#define MAX_GMSL_DP_SER_X_DE_LOW_L		0X04F2
#define MAX_GMSL_DP_SER_X_DE_LOW_H		0X04F3
#define MAX_GMSL_DP_SER_X_DE_CNT_L		0X04F4
#define MAX_GMSL_DP_SER_X_DE_CNT_H		0X04F5
#define MAX_GMSL_DP_SER_X_DE_LLOW_L		0X04F6
#define MAX_GMSL_DP_SER_X_DE_LLOW_M		0X04F7
#define MAX_GMSL_DP_SER_X_DE_LLOW_H		0X04F8
#define MAX_GMSL_DP_SER_X_LUT_TEMPLATE		0x04CD

#define MAX_GMSL_ARRAY_SIZE		4

#define MAX_GMSL_LINKS			2
#define MAX_GMSL_LINK_A			0
#define MAX_GMSL_LINK_B			1

#define MAX_GMSL_PIPE_X			0
#define MAX_GMSL_PIPE_Y			1
#define MAX_GMSL_PIPE_Z			2
#define MAX_GMSL_PIPE_U			3

#define MAX_GMSL_DP_SER_DUAL_VIEW_ZY_OFFSET	0x200
#define MAX_GMSL_DP_SER_PIPE_SUBIMAGE_OFFSET	0x100

struct max_gmsl_dp_ser_source {
	struct fwnode_handle *fwnode;
};

static const struct regmap_config max_gmsl_dp_ser_i2c_regmap = {
	.reg_bits = 16,
	.val_bits = 8,
};

struct max_gmsl_subimage_timing {
	bool enable;
	u32 superframe_group;
	u32 x_start;
	u32 h_active;
	u32 h_front_porch;
	u32 h_back_porch;
	u32 h_sync_len;
	u32 h_total;
	u32 y_start;
	u32 v_active;
	u32 v_front_porch;
	u32 v_back_porch;
	u32 v_sync_len;
	u32 v_total;
};

struct max_gmsl_subimage_attr {
	u32 m, n, x_offset, x_max, y_max;
	u32 vs_dly, vs_high, vs_low;
	u32 hs_dly, hs_high, hs_low, hs_cnt, hs_llow;
	u32 de_dly, de_high, de_low, de_cnt, de_llow;
	u8 lut_template;
};

struct superframe_info {
	struct max_gmsl_subimage_timing subimage_timing[MAX_GMSL_ARRAY_SIZE];
	struct max_gmsl_subimage_attr subimage_attr[MAX_GMSL_ARRAY_SIZE];
};

struct max_gmsl_dp_ser_priv {
	struct i2c_client *client;
	struct gpio_desc *gpiod_pwrdn;
	u8 dprx_lane_count;
	u8 dprx_link_rate;
	struct mutex mutex;
	struct regmap *regmap;
	int ser_errb;
	unsigned int ser_irq;
	bool enable_mst;
	u32 mst_payload_ids[MAX_GMSL_ARRAY_SIZE];
	u32 gmsl_stream_ids[MAX_GMSL_ARRAY_SIZE];
	u32 gmsl_link_select[MAX_GMSL_ARRAY_SIZE];
	bool link_is_enabled[MAX_GMSL_LINKS];
	u32 enable_gmsl3[MAX_GMSL_LINKS];
	u32 enable_gmsl_fec[MAX_GMSL_LINKS];
	struct superframe_info superframe;
};

static int max_gmsl_dp_ser_read(struct max_gmsl_dp_ser_priv *priv, int reg)
{
	int ret, val = 0;

	ret = regmap_read(priv->regmap, reg, &val);
	if (ret < 0)
		dev_err(&priv->client->dev,
			"%s: register 0x%02x read failed (%d)\n",
			__func__, reg, ret);
	return val;
}

static int max_gmsl_dp_ser_write(struct max_gmsl_dp_ser_priv *priv, u32 reg, u8 val)
{
	int ret;

	ret = regmap_write(priv->regmap, reg, val);
	if (ret < 0)
		dev_err(&priv->client->dev,
			"%s: register 0x%02x write failed (%d)\n",
			__func__, reg, ret);

	return ret;
}

/* static api to update given value */
static inline void max_gmsl_dp_ser_update(struct max_gmsl_dp_ser_priv *priv,
					  u32 reg, u32 mask, u8 val)
{
	u8 update_val;

	update_val = max_gmsl_dp_ser_read(priv, reg);
	update_val = ((update_val & (~mask)) | (val & mask));
	max_gmsl_dp_ser_write(priv, reg, update_val);
}

static void max_gmsl_dp_ser_mst_setup(struct max_gmsl_dp_ser_priv *priv)
{
	int i;
	static const int max_mst_payload_id_reg[] = {
		MAX_GMSL_DP_SER_MST_PAYLOAD_ID_0,
		MAX_GMSL_DP_SER_MST_PAYLOAD_ID_1,
		MAX_GMSL_DP_SER_MST_PAYLOAD_ID_2,
		MAX_GMSL_DP_SER_MST_PAYLOAD_ID_3,
	};
	static const int max_gmsl_stream_id_regs[] = {
		MAX_GMSL_DP_SER_TX3_0,
		MAX_GMSL_DP_SER_TX3_1,
		MAX_GMSL_DP_SER_TX3_2,
		MAX_GMSL_DP_SER_TX3_3,
	};

	/* enable MST by programming MISC_CONFIG_B1 reg  */
	max_gmsl_dp_ser_update(priv, MAX_GMSL_DP_SER_MISC_CONFIG_B1,
			       MAX_GMSL_DP_SER_MISC_CONFIG_B1_MASK,
			       MAX_GMSL_DP_SER_MISC_CONFIG_B1_VAL);

	/* program MST payload IDs */
	for (i = 0; i < ARRAY_SIZE(priv->mst_payload_ids); i++) {
		max_gmsl_dp_ser_write(priv, max_mst_payload_id_reg[i],
				      priv->mst_payload_ids[i]);
	}

	/* Program stream IDs */
	for (i = 0; i < ARRAY_SIZE(priv->gmsl_stream_ids); i++) {
		max_gmsl_dp_ser_write(priv, max_gmsl_stream_id_regs[i],
				      priv->gmsl_stream_ids[i]);
	}
}

static void max_gmsl_dp_ser_setup(struct max_gmsl_dp_ser_priv *priv)
{
	int i;
	u32 gmsl_link_select_value = 0;
	static const int max_gmsl_ser_vid_tx_regs[] = {
		MAX_GMSL_DP_SER_VID_TX_X,
		MAX_GMSL_DP_SER_VID_TX_Y,
		MAX_GMSL_DP_SER_VID_TX_Z,
		MAX_GMSL_DP_SER_VID_TX_U,
	};

	static const int max_gmsl_ser_tx0_link_regs[] = {
		MAX_GMSL_DP_SER_TX0_LINK_A,
		MAX_GMSL_DP_SER_TX0_LINK_B,
	};

	static const int max_gmsl_ser_lctrl0_regs[] = {
		MAX_GMSL_DP_SER_LCTRL0_A,
		MAX_GMSL_DP_SER_LCTRL0_B,
	};

	static const int max_gmsl_ser_reg4_mask[] = {
		MAX_GMSL_DP_SER_REG_4_GMSL_A,
		MAX_GMSL_DP_SER_REG_4_GMSL_B,
	};

	static const int max_gmsl_ser_reg4_value[] = {
		MAX_GMSL_DP_SER_REG_4_GMSL_A_PAM4_VAL,
		MAX_GMSL_DP_SER_REG_4_GMSL_B_PAM4_VAL,
	};

	/*
	 * Just enable "Loss of Training" and "Register control" events.
	 * Mask rest of event which can trigger HPD_IRQ.
	 */
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_HPD_INTERRUPT_MASK,
				MAX_GMSL_DP_SER_HPD_INTERRUPT_VAL);

	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_PHY_EDP_0_CTRL0_B0, 0x0f);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_PHY_EDP_0_CTRL0_B1, 0x0f);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_PHY_EDP_1_CTRL0_B0, 0x0f);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_PHY_EDP_1_CTRL0_B1, 0x0f);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_PHY_EDP_2_CTRL0_B0, 0x0f);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_PHY_EDP_2_CTRL0_B1, 0x0f);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_PHY_EDP_3_CTRL0_B0, 0x0f);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_PHY_EDP_3_CTRL0_B1, 0x0f);

	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_LOCAL_EDID, 0x1);

	/* Disable MST Mode */
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_MISC_CONFIG_B1, 0x0);

	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_MAX_LINK_RATE,
			      priv->dprx_link_rate);

	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_MAX_LINK_COUNT,
			      priv->dprx_lane_count);

	for (i = 0; i < MAX_GMSL_ARRAY_SIZE; i++) {
		gmsl_link_select_value = (priv->gmsl_link_select[i] <<
					  MAX_GMSL_DP_SER_LINK_SEL_SHIFT_VAL);
		max_gmsl_dp_ser_update(priv, max_gmsl_ser_vid_tx_regs[i],
				       MAX_GMSL_DP_SER_VID_TX_LINK_MASK,
				       gmsl_link_select_value);
	}

	max_gmsl_dp_ser_update(priv, MAX_GMSL_DP_SER_I2C_SPEED_CAPABILITY,
			       MAX_GMSL_DP_SER_I2C_SPEED_CAPABILITY_MASK,
			       MAX_GMSL_DP_SER_I2C_SPEED_CAPABILITY_100KBPS);

	for (i = 0; i < MAX_GMSL_LINKS; i++) {
		if (!priv->link_is_enabled[i])
			continue;

		if (priv->enable_gmsl_fec[i] || priv->enable_gmsl3[i]) {
			max_gmsl_dp_ser_update(priv, max_gmsl_ser_tx0_link_regs[i],
						MAX_GMSL_DP_SER_TX0_FEC_ENABLE_MASK,
						MAX_GMSL_DP_SER_TX0_FEC_ENABLE_VAL);
		}

		if (priv->enable_gmsl3[i]) {
			max_gmsl_dp_ser_update(priv, max_gmsl_ser_lctrl0_regs[i],
						MAX_GMSL_DP_SER_LCTRL0_TX_RATE_MASK,
						MAX_GMSL_DP_SER_LCTRL0_TX_RATE_VAL_12GBPS);

			max_gmsl_dp_ser_update(priv, MAX_GMSL_DP_SER_REG_4,
						max_gmsl_ser_reg4_mask[i],
						max_gmsl_ser_reg4_value[i]);
		}
	}

	if (priv->enable_mst)
		max_gmsl_dp_ser_mst_setup(priv);
}

static bool max_gmsl_dp_ser_check_dups(u32 *ids)
{
	int i = 0, j = 0;

	/* check if IDs are unique */
	for (i = 0; i < MAX_GMSL_ARRAY_SIZE; i++) {
		for (j = i + 1; j < MAX_GMSL_ARRAY_SIZE; j++) {
			if (ids[i] == ids[j])
				return false;
		}
	}

	return true;
}

static void max_gmsl_detect_internal_crc_error(struct max_gmsl_dp_ser_priv *priv,
				 struct device *dev)
{
	int i, ret = 0;

	static const int max_gmsl_internal_crc_regs[] = {
		MAX_GMSL_DP_SER_INTERNAL_CRC_X,
		MAX_GMSL_DP_SER_INTERNAL_CRC_Y,
		MAX_GMSL_DP_SER_INTERNAL_CRC_Z,
		MAX_GMSL_DP_SER_INTERNAL_CRC_U,
	};

	for (i = 0; i < MAX_GMSL_ARRAY_SIZE; i++) {
		ret = max_gmsl_dp_ser_read(priv, max_gmsl_internal_crc_regs[i]);
		/* Reading register will clear the detect bit */
		if ((ret & MAX_GMSL_DP_SER_INTERNAL_CRC_ERR_DET) != 0U) {
			dev_err(dev, "%s: INTERNAL CRC video error detected at pipe %d\n", __func__, i);
			if ((ret & MAX_GMSL_DP_SER_INTERNAL_CRC_ERR_INJ) != 0U) {
				/* CRC error is forcefuly injected, disable it */
				ret = ret & (~MAX_GMSL_DP_SER_INTERNAL_CRC_ERR_INJ);
				max_gmsl_dp_ser_write(priv, max_gmsl_internal_crc_regs[i], ret);
			}
		}
	}
}

/*
 * This function is responsible for detecting ANY remote deserializer
 * errors. Note that the main error that we're interested in today is
 * any video line CRC error reported by the deserializer.
 */
static void max_gmsl_detect_remote_error(struct max_gmsl_dp_ser_priv *priv,
				 struct device *dev)
{
	int ret = 0;

	ret = max_gmsl_dp_ser_read(priv, MAX_GMSL_DP_SER_INTR3);

	if (priv->link_is_enabled[MAX_GMSL_LINK_A]) {
		if ((ret & MAX_GMSL_DP_SER_REM_ERR_FLAG_A) != 0)
			dev_err(dev, "%s: Remote deserializer error detected on Link A\n", __func__);
	}

	if (priv->link_is_enabled[MAX_GMSL_LINK_B]) {
		if ((ret & MAX_GMSL_DP_SER_REM_ERR_FLAG_B) != 0)
			dev_err(dev, "%s: Remote deserializer error detected on Link B\n", __func__);
	}
}

static irqreturn_t max_gsml_dp_ser_irq_handler(int irq, void *dev_id)
{
	struct max_gmsl_dp_ser_priv *priv = dev_id;
	int ret = 0;
	struct device *dev = &priv->client->dev;

	ret = max_gmsl_dp_ser_read(priv, MAX_GMSL_DP_SER_INTR9);
	if (ret & MAX_GMSL_DP_SER_LOSS_OF_LOCK_FLAG)
		dev_dbg(dev, "%s: Fault due to GMSL Link Loss\n", __func__);

	/* Detect internal CRC errors inside serializer */
	max_gmsl_detect_internal_crc_error(priv, dev);

	/* Detect remote error across GMSL link */
	max_gmsl_detect_remote_error(priv, dev);

	return IRQ_HANDLED;
}

static void max_gmsl_dp_ser_program_dual_view_for_superframe_group(struct device *dev, u32 pipe_id)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max_gmsl_dp_ser_priv *priv = i2c_get_clientdata(client);
	u32 offset = 0;

	/* Z-U registers offset shifted by 0x200 */
	if (pipe_id == MAX_GMSL_PIPE_Z)
		offset = MAX_GMSL_DP_SER_DUAL_VIEW_ZY_OFFSET;

	/*
	 * TODO: Get formula and sequence to calculate these regiters.
	 * As of now, just hard code these settings for symmetric dual view.
	 * These needs to be program in exact sequence and same register
	 * is modified with different values. As of now, data sheet does not
	 * have details for all fields of these registers.
	 */
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_ASYM_14_Y + offset, 0x37);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_ASYM_17_X + offset, 0xF8);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_ASYM_17_Y + offset, 0xF8);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_ASYM_15_X + offset, 0xBF);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_ASYM_15_Y + offset, 0xBF);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_ASYM_17_X + offset, 0xFC);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_ASYM_17_Y + offset, 0xFC);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_ASYM_14_X + offset, 0x2F);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_ASYM_14_X + offset, 0x0F);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_ASYM_14_Y + offset, 0x27);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_ASYM_14_Y + offset, 0x07);
}

static void max_gmsl_dp_ser_program_subimage_timings(struct device *dev, u32 pipe_id)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max_gmsl_dp_ser_priv *priv = i2c_get_clientdata(client);
	u32 offset = pipe_id * MAX_GMSL_DP_SER_PIPE_SUBIMAGE_OFFSET;

	// Asymmetric dual-view total pixel count per frame in sub-image
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_M_L + offset, (priv->superframe.subimage_attr[pipe_id].m & 0xFF));
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_M_M + offset, (priv->superframe.subimage_attr[pipe_id].m & 0xFF00) >> 8);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_M_H + offset, (priv->superframe.subimage_attr[pipe_id].m & 0xFF0000) >> 16);

	// Asymmetric dual-view total pixel count per frame for superframe
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_N_L + offset, (priv->superframe.subimage_attr[pipe_id].n & 0xFF));
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_N_M + offset, (priv->superframe.subimage_attr[pipe_id].n & 0xFF00) >> 8);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_N_H + offset, (priv->superframe.subimage_attr[pipe_id].n & 0xFF0000) >> 16);

	// Number of pixels to skip before writing when receiving a new line
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_X_OFFSET_L + offset, (priv->superframe.subimage_attr[pipe_id].x_offset & 0xFF));
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_X_OFFSET_H + offset, (priv->superframe.subimage_attr[pipe_id].x_offset & 0xFF00) >> 8);

	// Index of the last pixel of sub-image in the superframe
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_X_MAX_L + offset, (priv->superframe.subimage_attr[pipe_id].x_max & 0xFF));
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_X_MAX_H + offset, (priv->superframe.subimage_attr[pipe_id].x_max & 0xFF00) >> 8);

	// Index of the last line of sub-image in the superframe
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_Y_MAX_L + offset, (priv->superframe.subimage_attr[pipe_id].y_max & 0xFF));
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_Y_MAX_H + offset, (priv->superframe.subimage_attr[pipe_id].y_max & 0xFF00) >> 8);

	// VS delay in terms of PCLK cycles. The output VS is delayed by VS_DELAY cycles from the start.
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_VS_DLY_L + offset, (priv->superframe.subimage_attr[pipe_id].vs_dly & 0xFF));
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_VS_DLY_M + offset, (priv->superframe.subimage_attr[pipe_id].vs_dly & 0xFF00) >> 8);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_VS_DLY_H + offset, (priv->superframe.subimage_attr[pipe_id].vs_dly & 0xFF0000) >> 16);

	// VS high period in terms of PCLK
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_VS_HIGH_L + offset, (priv->superframe.subimage_attr[pipe_id].vs_high & 0xFF));
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_VS_HIGH_M + offset, (priv->superframe.subimage_attr[pipe_id].vs_high & 0xFF00) >> 8);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_VS_HIGH_H + offset, (priv->superframe.subimage_attr[pipe_id].vs_high & 0xFF0000) >> 16);

	// VS low period in terms of PCLK cycles
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_VS_LOW_L + offset, (priv->superframe.subimage_attr[pipe_id].vs_low & 0xFF));
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_VS_LOW_M + offset, (priv->superframe.subimage_attr[pipe_id].vs_low & 0xFF00) >> 8);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_VS_LOW_H + offset, (priv->superframe.subimage_attr[pipe_id].vs_low & 0xFF0000) >> 16);

	// HS delay in terms of PCLK cycles The output HS is delayed by HS_DELAY cycles from the start.
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_HS_DLY_L + offset, (priv->superframe.subimage_attr[pipe_id].hs_dly & 0xFF));
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_HS_DLY_M + offset, (priv->superframe.subimage_attr[pipe_id].hs_dly & 0xFF00) >> 8);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_HS_DLY_H + offset, (priv->superframe.subimage_attr[pipe_id].hs_dly & 0xFF0000) >> 16);

	// HS high period in terms of PCLK cycles
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_HS_HIGH_L + offset, (priv->superframe.subimage_attr[pipe_id].hs_high & 0xFF));
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_HS_HIGH_H + offset, (priv->superframe.subimage_attr[pipe_id].hs_high & 0xFF00) >> 8);

	// HS low period in terms of PCLK cycles
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_HS_LOW_L + offset, (priv->superframe.subimage_attr[pipe_id].hs_low & 0xFF));
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_HS_LOW_H + offset, (priv->superframe.subimage_attr[pipe_id].hs_low & 0xFF00) >> 8);

	// HS pulses per frame
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_HS_CNT_L + offset, (priv->superframe.subimage_attr[pipe_id].hs_cnt & 0xFF));
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_HS_CNT_H + offset, (priv->superframe.subimage_attr[pipe_id].hs_cnt & 0xFF00) >> 8);

	// HS long low period in terms of PCLK cycles
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_HS_LLOW_L + offset, (priv->superframe.subimage_attr[pipe_id].hs_llow & 0xFF));
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_HS_LLOW_M + offset, (priv->superframe.subimage_attr[pipe_id].hs_llow & 0xFF00) >> 8);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_HS_LLOW_H + offset, (priv->superframe.subimage_attr[pipe_id].hs_llow & 0xFF0000) >> 16);

	// DE delay in terms of PCLK cycles The output HS is delayed by HS_DELAY cycles from the start.
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_DE_DLY_L + offset, (priv->superframe.subimage_attr[pipe_id].de_dly & 0xFF));
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_DE_DLY_M + offset, (priv->superframe.subimage_attr[pipe_id].de_dly & 0xFF00) >> 8);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_DE_DLY_H + offset, (priv->superframe.subimage_attr[pipe_id].de_dly & 0xFF0000) >> 16);

	// DE high period in terms of PCLK cycles
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_DE_HIGH_L + offset, (priv->superframe.subimage_attr[pipe_id].de_high & 0xFF));
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_DE_HIGH_H + offset, (priv->superframe.subimage_attr[pipe_id].de_high & 0xFF00) >> 8);

	// DE low period in terms of PCLK cycles
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_DE_LOW_L + offset, (priv->superframe.subimage_attr[pipe_id].de_low & 0xFF));
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_DE_LOW_H + offset, (priv->superframe.subimage_attr[pipe_id].de_low & 0xFF00) >> 8);

	// DE pulses per frame
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_DE_CNT_L + offset, (priv->superframe.subimage_attr[pipe_id].de_cnt & 0xFF));
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_DE_CNT_H + offset, (priv->superframe.subimage_attr[pipe_id].de_cnt & 0xFF00) >> 8);

	// DE long low period in terms of PCLK cycles
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_DE_LLOW_L + offset, (priv->superframe.subimage_attr[pipe_id].de_llow & 0xFF));
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_DE_LLOW_M + offset, (priv->superframe.subimage_attr[pipe_id].de_llow & 0xFF00) >> 8);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_DE_LLOW_H + offset, (priv->superframe.subimage_attr[pipe_id].de_llow & 0xFF0000) >> 16);

	// LUT Template
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_X_LUT_TEMPLATE + offset, (priv->superframe.subimage_attr[pipe_id].lut_template));
}

static int max_gmsl_dp_ser_configure_superframe(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max_gmsl_dp_ser_priv *priv = i2c_get_clientdata(client);

	/*
	 * Currently Enforcing X-Y and/or Z-U Dual view.
	 * TODO: Add support for other combinations of pipe.
	 */
	if ((priv->superframe.subimage_timing[MAX_GMSL_PIPE_X].superframe_group != priv->superframe.subimage_timing[MAX_GMSL_PIPE_Y].superframe_group) ||
		(priv->superframe.subimage_timing[MAX_GMSL_PIPE_Z].superframe_group != priv->superframe.subimage_timing[MAX_GMSL_PIPE_U].superframe_group)) {
			dev_err(dev, "%s: Invalid superframe-group combination\n", __func__);
			return -1;
	}

	if (priv->superframe.subimage_timing[MAX_GMSL_PIPE_X].enable && priv->superframe.subimage_timing[MAX_GMSL_PIPE_Y].enable) {
		/* X and Y both enabled, program superframe registers */
		max_gmsl_dp_ser_program_dual_view_for_superframe_group(dev, MAX_GMSL_PIPE_X);
		max_gmsl_dp_ser_program_subimage_timings(dev, MAX_GMSL_PIPE_X);
		max_gmsl_dp_ser_program_subimage_timings(dev, MAX_GMSL_PIPE_Y);
	}

	if (priv->superframe.subimage_timing[MAX_GMSL_PIPE_Z].enable && priv->superframe.subimage_timing[MAX_GMSL_PIPE_U].enable) {
		/* Z and U both enabled, program superframe registers */
		max_gmsl_dp_ser_program_dual_view_for_superframe_group(dev, MAX_GMSL_PIPE_Z);
		max_gmsl_dp_ser_program_subimage_timings(dev, MAX_GMSL_PIPE_Z);
		max_gmsl_dp_ser_program_subimage_timings(dev, MAX_GMSL_PIPE_U);
	}

	return 0;
}

static int max_gmsl_dp_ser_init(struct device *dev)
{
	struct max_gmsl_dp_ser_priv *priv;
	struct i2c_client *client;

	client = to_i2c_client(dev);
	priv = i2c_get_clientdata(client);

	priv->gpiod_pwrdn = devm_gpiod_get_optional(&client->dev, "enable",
						    GPIOD_OUT_HIGH);
	if (IS_ERR(priv->gpiod_pwrdn)) {
		dev_err(dev, "%s: gpiopwrdn is not enabled\n", __func__);
		return PTR_ERR(priv->gpiod_pwrdn);
	}
	gpiod_set_consumer_name(priv->gpiod_pwrdn, "max_gmsl_dp_ser-pwrdn");

	/* Drive PWRDNB pin high to power up the serializer */
	gpiod_set_value_cansleep(priv->gpiod_pwrdn, 1);

	/* Wait ~4ms for powerup to complete */
	usleep_range(4000, 4200);

	/*
	 * Write RESET_LINK = 1 (for both Phy A, 0x29, and Phy B, 0x33)
	 * within 10ms
	 */
	max_gmsl_dp_ser_update(priv, MAX_GMSL_DP_SER_LINK_CTRL_PHY_A,
			       MAX_GMSL_DP_SER_LINK_CTRL_A_MASK, 0x1);
	max_gmsl_dp_ser_update(priv, MAX_GMSL_DP_SER_LINK_CTRL_PHY_B,
			       MAX_GMSL_DP_SER_LINK_CTRL_B_MASK, 0x1);

	/*
	 * Disable video output on the GMSL link by setting VID_TX_EN = 0
	 * for Pipe X, Y, Z and U
	 */
	max_gmsl_dp_ser_update(priv, MAX_GMSL_DP_SER_VID_TX_X,
			       MAX_GMSL_DP_SER_VID_TX_MASK, 0x0);
	max_gmsl_dp_ser_update(priv, MAX_GMSL_DP_SER_VID_TX_Y,
			       MAX_GMSL_DP_SER_VID_TX_MASK, 0x0);
	max_gmsl_dp_ser_update(priv, MAX_GMSL_DP_SER_VID_TX_Z,
			       MAX_GMSL_DP_SER_VID_TX_MASK, 0x0);
	max_gmsl_dp_ser_update(priv, MAX_GMSL_DP_SER_VID_TX_U,
			       MAX_GMSL_DP_SER_VID_TX_MASK, 0x0);

	/*
	 * Set LINK_ENABLE=0 (0x7000) to force the DP HPD
	 * pin low to hold off DP link training and
	 * SOC video
	 */
	max_gmsl_dp_ser_update(priv, MAX_GMSL_DP_SER_LINK_ENABLE,
			       MAX_GMSL_DP_SER_LINK_ENABLE_MASK, 0x0);

	max_gmsl_dp_ser_setup(priv);

	/*
	 * Write RESET_LINK = 0 (for both Phy A, 0x29, and Phy B, 0x33)
	 * to initiate the GMSL link lock process.
	 */
	if (priv->link_is_enabled[MAX_GMSL_LINK_A])
		max_gmsl_dp_ser_update(priv,
				       MAX_GMSL_DP_SER_LINK_CTRL_PHY_A,
				       MAX_GMSL_DP_SER_LINK_CTRL_A_MASK,
				       0x0);
	if (priv->link_is_enabled[MAX_GMSL_LINK_B])
		max_gmsl_dp_ser_update(priv,
				       MAX_GMSL_DP_SER_LINK_CTRL_PHY_B,
				       MAX_GMSL_DP_SER_LINK_CTRL_B_MASK,
				       0x0);

	/*
	 * Set LINK_ENABLE = 1 (0x7000) to enable SOC DP link training,
	 * enable SOC video output to the serializer.
	 */
	max_gmsl_dp_ser_update(priv, MAX_GMSL_DP_SER_LINK_ENABLE,
			       MAX_GMSL_DP_SER_LINK_ENABLE_MASK, 0x1);

	max_gmsl_dp_ser_read(priv, MAX_GMSL_DP_SER_INTR9);

	if (priv->link_is_enabled[MAX_GMSL_LINK_A])
		max_gmsl_dp_ser_update(priv, MAX_GMSL_DP_SER_INTR2,
				   MAX_GMSL_DP_SER_REM_ERR_OEN_A_MASK,
				   MAX_GMSL_DP_SER_REM_ERR_OEN_A_VAL);
	if (priv->link_is_enabled[MAX_GMSL_LINK_B])
		max_gmsl_dp_ser_update(priv, MAX_GMSL_DP_SER_INTR2,
				   MAX_GMSL_DP_SER_REM_ERR_OEN_B_MASK,
				   MAX_GMSL_DP_SER_REM_ERR_OEN_B_VAL);

	/* enable INTR8.LOSS_OF_LOCK_OEN */
	max_gmsl_dp_ser_update(priv, MAX_GMSL_DP_SER_INTR8,
			       MAX_GMSL_DP_SER_INTR8_MASK,
			       MAX_GMSL_DP_SER_INTR8_VAL);

	/* enable internal CRC after link training */
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_INTERNAL_CRC_X,
			       MAX_GMSL_DP_SER_INTERNAL_CRC_ENABLE);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_INTERNAL_CRC_Y,
			       MAX_GMSL_DP_SER_INTERNAL_CRC_ENABLE);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_INTERNAL_CRC_Z,
			       MAX_GMSL_DP_SER_INTERNAL_CRC_ENABLE);
	max_gmsl_dp_ser_write(priv, MAX_GMSL_DP_SER_INTERNAL_CRC_U,
			       MAX_GMSL_DP_SER_INTERNAL_CRC_ENABLE);

	/* configure superframe settings */
	max_gmsl_dp_ser_configure_superframe(dev);

	/* enable video output */
	max_gmsl_dp_ser_update(priv, MAX_GMSL_DP_SER_VID_TX_X,
			       MAX_GMSL_DP_SER_VID_TX_MASK, 0x1);
	max_gmsl_dp_ser_update(priv, MAX_GMSL_DP_SER_VID_TX_Y,
			       MAX_GMSL_DP_SER_VID_TX_MASK, 0x1);
	max_gmsl_dp_ser_update(priv, MAX_GMSL_DP_SER_VID_TX_Z,
			       MAX_GMSL_DP_SER_VID_TX_MASK, 0x1);
	max_gmsl_dp_ser_update(priv, MAX_GMSL_DP_SER_VID_TX_U,
			       MAX_GMSL_DP_SER_VID_TX_MASK, 0x1);

	return 0;
}

static int max_gmsl_dp_ser_parse_mst_props(struct i2c_client *client,
					   struct max_gmsl_dp_ser_priv *priv)
{
	struct device *dev = &priv->client->dev;
	struct device_node *ser = dev->of_node;
	int err = 0;
	bool ret;

	priv->enable_mst = of_property_read_bool(ser,
						 "enable-mst");
	if (priv->enable_mst)
		dev_info(dev, "%s: MST mode enabled\n", __func__);
	else
		dev_info(dev, "%s: MST mode not enabled\n", __func__);

	if (priv->enable_mst) {
		err = of_property_read_variable_u32_array(ser,
							 "mst-payload-ids",
							 priv->mst_payload_ids, 1,
							 ARRAY_SIZE(priv->mst_payload_ids));

		if (err < 0) {
			dev_info(dev,
				 "%s: MST Payload prop not found or invalid\n",
				 __func__);
			return -EINVAL;
		}

		ret = max_gmsl_dp_ser_check_dups(priv->mst_payload_ids);
		if (!ret) {
			dev_err(dev, "%s: payload IDs are not unique\n",
				__func__);
			return -EINVAL;
		}

		err = of_property_read_variable_u32_array(ser,
							 "gmsl-stream-ids",
							 priv->gmsl_stream_ids, 1,
							 ARRAY_SIZE(priv->gmsl_stream_ids));
		if (err < 0) {
			dev_info(dev,
				 "%s: GMSL Stream ID property not found or invalid\n",
				 __func__);
			return -EINVAL;
		}

		ret = max_gmsl_dp_ser_check_dups(priv->gmsl_stream_ids);
		if (!ret) {
			dev_err(dev, "%s: stream IDs are not unique\n",
				__func__);
			return -EINVAL;
		}
	}

	return 0;
}

static int max_gmsl_dp_ser_parse_gmsl_props(struct i2c_client *client,
					   struct max_gmsl_dp_ser_priv *priv)
{
	struct device *dev = &priv->client->dev;
	struct device_node *ser = dev->of_node;
	int err = 0;

	err = of_property_read_variable_u32_array(ser, "enable-gmsl3",
						 priv->enable_gmsl3, 1,
						 ARRAY_SIZE(priv->enable_gmsl3));
	if (err < 0) {
		if (err == -EINVAL) {
			dev_info(dev,
				 "%s: enable-gmsl3 property not found\n",
				 __func__);
		} else {
			dev_err(dev,
				 "%s: enable-gmsl3 property does not have valid data\n",
				 __func__);
			return -EINVAL;
		}
	}

	err = of_property_read_variable_u32_array(ser, "enable-gmsl-fec",
						 priv->enable_gmsl_fec, 1,
						 ARRAY_SIZE(priv->enable_gmsl_fec));
	if (err < 0) {
		if (err == -EINVAL) {
			dev_info(dev,
				 "%s: enable-gmsl-fec property not found\n",
				 __func__);
		} else {
			dev_err(dev,
				 "%s: enable-gmsl-fec property does not have valid data\n",
				 __func__);
			return -EINVAL;
		}
	}

	return 0;
}

static int max_gmsl_dp_ser_calc_superframe_pipe_attr(struct i2c_client *client,
					   struct max_gmsl_dp_ser_priv *priv,
					   int pipe_id)
{
	struct device *dev = &priv->client->dev;

	if (pipe_id < 0 || pipe_id >= MAX_GMSL_ARRAY_SIZE) {
		dev_err(dev, "%s: max_gmsl_dp_ser_calc_superframe_pipe_attr - Invalid pipe_id: %d\n",
			 __func__, pipe_id);
		return -1;
	}

	//Asymmetric dual-view total pixel count per frame in sub-image
	priv->superframe.subimage_attr[pipe_id].m = priv->superframe.subimage_timing[pipe_id].h_total * priv->superframe.subimage_timing[pipe_id].v_total;

	//Asymmetric dual-view total pixel count per frame for superframe
	// TODO: Get formula for this value
	priv->superframe.subimage_attr[pipe_id].n = 461000;

	//Number of pixels to skip before writing when receiving a new line
	priv->superframe.subimage_attr[pipe_id].x_offset = priv->superframe.subimage_timing[pipe_id].x_start;

	//Index of the last pixel of sub-image in the superframe
	priv->superframe.subimage_attr[pipe_id].x_max = priv->superframe.subimage_timing[pipe_id].h_active + priv->superframe.subimage_attr[pipe_id].x_offset;

	//Index of the last line of sub-image in the superframe
	priv->superframe.subimage_attr[pipe_id].y_max = priv->superframe.subimage_timing[pipe_id].v_active;

	//VS delay in terms of PCLK cycles. The output VS is delayed by VS_DELAY cycles from the start.
	priv->superframe.subimage_attr[pipe_id].vs_dly = priv->superframe.subimage_timing[pipe_id].h_total * (priv->superframe.subimage_timing[pipe_id].v_active + priv->superframe.subimage_timing[pipe_id].v_front_porch);

	//VS high period in terms of PCLK
	priv->superframe.subimage_attr[pipe_id].vs_high = priv->superframe.subimage_timing[pipe_id].h_total * priv->superframe.subimage_timing[pipe_id].v_sync_len;

	//VS low period in terms of PCLK cycles
	//TODO Get formula for this value
	priv->superframe.subimage_attr[pipe_id].vs_low = 36000;

	//HS delay in terms of PCLK cycles The output HS is delayed by HS_DELAY cycles from the start.
	priv->superframe.subimage_attr[pipe_id].hs_dly = 0;

	//HS high period in terms of PCLK cycles
	priv->superframe.subimage_attr[pipe_id].hs_high = priv->superframe.subimage_timing[pipe_id].h_sync_len;

	//HS low period in terms of PCLK cycles
	priv->superframe.subimage_attr[pipe_id].hs_low = priv->superframe.subimage_timing[pipe_id].h_active + priv->superframe.subimage_timing[pipe_id].h_front_porch + priv->superframe.subimage_timing[pipe_id].h_back_porch;

	//HS pulses per frame
	priv->superframe.subimage_attr[pipe_id].hs_cnt = priv->superframe.subimage_timing[pipe_id].v_total;

	//HS long low period in terms of PCLK cycles
	priv->superframe.subimage_attr[pipe_id].hs_llow = 0;

	//DE delay in terms of PCLK cycles The output DE is delayed by DE_DELAY cycles from the start.
	priv->superframe.subimage_attr[pipe_id].de_dly = priv->superframe.subimage_timing[pipe_id].h_back_porch + priv->superframe.subimage_timing[pipe_id].h_sync_len;

	//DE high period in terms of PCLK cycles
	priv->superframe.subimage_attr[pipe_id].de_high = priv->superframe.subimage_timing[pipe_id].h_active;

	//DE low period in terms of PCLK cycles
	priv->superframe.subimage_attr[pipe_id].de_low = priv->superframe.subimage_timing[pipe_id].h_front_porch + priv->superframe.subimage_timing[pipe_id].h_sync_len + priv->superframe.subimage_timing[pipe_id].h_back_porch;

	//Active lines per frame
	priv->superframe.subimage_attr[pipe_id].de_cnt = priv->superframe.subimage_timing[pipe_id].v_active;

	//DE long low period in terms of PCLK cycles
	//TODO Get formula for this value
	priv->superframe.subimage_attr[pipe_id].de_llow = 61944;

	//LUT template
	//TODO Get formula for this value
	priv->superframe.subimage_attr[pipe_id].lut_template = 20;

	return 0;
}

static int max_gmsl_dp_ser_parse_superframe_pipe_props(struct i2c_client *client,
					   struct max_gmsl_dp_ser_priv *priv,
					   struct device_node *pipe_node,
					   int pipe_id)
{
	struct device *dev = &priv->client->dev;
	int err = 0;
	u32 val = 0;
	struct device_node *timings_handle;

	err = of_property_read_u32(pipe_node, "superframe-group", &val);
	if (err) {
		dev_err(dev, "%s: - superframe-group property not found\n",
			 __func__);
		return err;
	}

	if (pipe_id < 0 || pipe_id >= MAX_GMSL_ARRAY_SIZE) {
		dev_err(dev, "%s: - Invalid pipe_id: %d\n",
			 __func__, pipe_id);
		return -1;
	}

	priv->superframe.subimage_timing[pipe_id].superframe_group = val;

	err = of_property_read_u32(pipe_node, "timings-phandle", &val);
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

	err = of_property_read_u32(timings_handle, "x", &val);
	if (err) {
		dev_err(dev, "%s: - x property not found\n",
			 __func__);
		return err;
	}
	priv->superframe.subimage_timing[pipe_id].x_start = val;

	err = of_property_read_u32(timings_handle, "width", &val);
	if (err) {
		dev_err(dev, "%s: - width property not found\n",
			 __func__);
		return err;
	}
	priv->superframe.subimage_timing[pipe_id].h_active = val;

	err = of_property_read_u32(timings_handle, "hfront-porch", &val);
	if (err) {
		dev_err(dev, "%s: - hfront-porch property not found\n",
			 __func__);
		return err;
	}
	priv->superframe.subimage_timing[pipe_id].h_front_porch = val;

	err = of_property_read_u32(timings_handle, "hback-porch", &val);
	if (err) {
		dev_err(dev, "%s: - hback-porch property not found\n",
			 __func__);
		return err;
	}
	priv->superframe.subimage_timing[pipe_id].h_back_porch = val;

	err = of_property_read_u32(timings_handle, "hsync-len", &val);
	if (err) {
		dev_err(dev, "%s: - hsync-len property not found\n",
			 __func__);
		return err;
	}
	priv->superframe.subimage_timing[pipe_id].h_sync_len = val;

	priv->superframe.subimage_timing[pipe_id].h_total = priv->superframe.subimage_timing[pipe_id].h_active + priv->superframe.subimage_timing[pipe_id].h_front_porch + priv->superframe.subimage_timing[pipe_id].h_back_porch + priv->superframe.subimage_timing[pipe_id].h_sync_len;

	err = of_property_read_u32(timings_handle, "y", &val);
	if (err) {
		dev_err(dev, "%s: - y property not found\n",
			 __func__);
		return err;
	}
	priv->superframe.subimage_timing[pipe_id].y_start = val;

	err = of_property_read_u32(timings_handle, "height", &val);
	if (err) {
		dev_err(dev, "%s: - height property not found\n",
			 __func__);
		return err;
	}
	priv->superframe.subimage_timing[pipe_id].v_active = val;

	err = of_property_read_u32(timings_handle, "vfront-porch", &val);
	if (err) {
		dev_err(dev, "%s: - vfront-porch property not found\n",
			 __func__);
		return err;
	}
	priv->superframe.subimage_timing[pipe_id].v_front_porch = val;

	err = of_property_read_u32(timings_handle, "vback-porch", &val);
	if (err) {
		dev_err(dev, "%s: - vback-porch property not found\n",
			 __func__);
		return err;
	}
	priv->superframe.subimage_timing[pipe_id].v_back_porch = val;

	err = of_property_read_u32(timings_handle, "vsync-len", &val);
	if (err) {
		dev_err(dev, "%s: - vsync-len property not found\n",
			 __func__);
		return err;
	}
	priv->superframe.subimage_timing[pipe_id].v_sync_len = val;

	priv->superframe.subimage_timing[pipe_id].v_total = priv->superframe.subimage_timing[pipe_id].v_active + priv->superframe.subimage_timing[pipe_id].v_front_porch + priv->superframe.subimage_timing[pipe_id].v_back_porch + priv->superframe.subimage_timing[pipe_id].v_sync_len;

	return 0;
}

static int max_gmsl_dp_ser_parse_superframe_props(struct i2c_client *client,
					   struct max_gmsl_dp_ser_priv *priv)
{
	struct device *dev = &priv->client->dev;
	struct device_node *ser = dev->of_node;
	int err, i = 0;
	int ret = 0;
	struct device_node *superframe_video_timings, *pipe_node;
	char pipe_name[MAX_GMSL_ARRAY_SIZE][7] = { "pipe-x", "pipe-y", "pipe-z", "pipe-u"};

	/* check if superframe timings present in device tree */
	superframe_video_timings = of_find_node_by_name(ser, "superframe-video-timings");
	if (superframe_video_timings == NULL) {
		dev_info(dev, "%s: - superframe-video-timings property not found\n",
			 __func__);
		goto fail;
	}

	/* get superframe subimage properties for each pipe */
	for (i = 0; i < MAX_GMSL_ARRAY_SIZE; i++) {
		pipe_node = of_find_node_by_name(superframe_video_timings, pipe_name[i]);
		if (pipe_node) {
			err = max_gmsl_dp_ser_parse_superframe_pipe_props(client, priv, pipe_node, i);
			if (!err) {
				/* pipe have valid properties, calculate attributes */
				priv->superframe.subimage_timing[i].enable = true;
				err = max_gmsl_dp_ser_calc_superframe_pipe_attr(client, priv, i);
				if (err) {
					dev_err(dev, "%s: - %s Invalid Pipe Id for the property \n", __func__, pipe_name[i]);
					ret = -1;
					goto fail;
				}
			} else {
				dev_err(dev, "%s: - %s property found but properties are invalid\n", __func__, pipe_name[i]);
				ret = -1;
				goto fail;
			}
		} else {
			dev_info(dev, "%s: - %s property not found\n", __func__, pipe_name[i]);
		}
	}

fail:
    return ret;
}

static int max_gmsl_dp_ser_parse_dt_props(struct i2c_client *client,
					   struct max_gmsl_dp_ser_priv *priv)
{
	struct device *dev = &priv->client->dev;
	int err = 0;

	err = max_gmsl_dp_ser_parse_mst_props(client, priv);
	if (err != 0) {
		dev_err(dev, "%s: error parsing MST props\n", __func__);
		return -EFAULT;
	}

	err = max_gmsl_dp_ser_parse_gmsl_props(client, priv);
	if (err != 0) {
		dev_err(dev, "%s: error parsing GMSL props\n", __func__);
		return -EFAULT;
	}

	err = max_gmsl_dp_ser_parse_superframe_props(client, priv);
	if (err != 0) {
		dev_err(dev, "%s: error parsing Super frame \n", __func__);
		return -EFAULT;
	}

	return err;
}

static int max_gmsl_dp_ser_parse_dt(struct i2c_client *client,
				    struct max_gmsl_dp_ser_priv *priv)
{
	struct device *dev = &priv->client->dev;
	struct device_node *ser = dev->of_node;
	int err = 0, i;
	u32 val = 0;

	dev_info(dev, "%s: parsing serializer device tree:\n", __func__);

	err = of_property_read_u32(ser, "dprx-lane-count", &val);
	if (err) {
		if (err == -EINVAL) {
			dev_info(dev, "%s: - dprx-lane-count property not found\n",
				 __func__);
			/* default value: 4 */
			priv->dprx_lane_count = 4;
			dev_info(dev, "%s: dprx-lane-count set to default val: 4\n",
				 __func__);
		} else {
			return err;
		}
	} else {
		/* set dprx-lane-count */
		priv->dprx_lane_count = val;
		dev_info(dev, "%s: - dprx-lane-count %i\n", __func__, val);
	}

	err = of_property_read_u32(ser, "dprx-link-rate", &val);
	if (err) {
		if (err == -EINVAL) {
			dev_info(dev, "%s: - dprx-link-rate property not found\n",
				 __func__);
			/* default value: 0x1E */
			priv->dprx_link_rate = 0x1E;
			dev_info(dev, "%s: dprx-link-rate set to default val: 0x1E\n",
				 __func__);
		} else {
			return err;
		}
	} else {
		/* set dprx-link-rate*/
		priv->dprx_link_rate = val;
		dev_info(dev, "%s: - dprx-link-rate %i\n", __func__, val);
	}

	err = of_property_read_variable_u32_array(ser, "gmsl-link-select",
						 priv->gmsl_link_select, 1,
						 ARRAY_SIZE(priv->gmsl_link_select));
	if (err < 0) {
		dev_info(dev,
			 "%s: GMSL Link select property not found or invalid\n",
			 __func__);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(priv->gmsl_link_select); i++) {
		if ((priv->gmsl_link_select[i] == MAX_GMSL_DP_SER_ENABLE_LINK_A)
		    || (priv->gmsl_link_select[i] ==
		    MAX_GMSL_DP_SER_ENABLE_LINK_AB)) {
			priv->link_is_enabled[MAX_GMSL_LINK_A] = true;
		} else if ((priv->gmsl_link_select[i] == MAX_GMSL_DP_SER_ENABLE_LINK_B)
		    || (priv->gmsl_link_select[i] ==
		    MAX_GMSL_DP_SER_ENABLE_LINK_AB)) {
			priv->link_is_enabled[MAX_GMSL_LINK_B] = true;
		} else {
			dev_info(dev,
				 "%s: GMSL Link select values are invalid\n",
				 __func__);
			return -EINVAL;
		}
	}

	err = max_gmsl_dp_ser_parse_dt_props(client, priv);
	if (err != 0) {
		dev_err(dev, "%s: error parsing DT props\n", __func__);
		return -EFAULT;
	}

	return 0;
}

static int max_gmsl_dp_ser_probe(struct i2c_client *client)
{
	struct max_gmsl_dp_ser_priv *priv;
	struct device *dev;
	struct device_node *ser = client->dev.of_node;
	int ret;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;

	mutex_init(&priv->mutex);

	priv->client = client;
	i2c_set_clientdata(client, priv);

	priv->regmap = devm_regmap_init_i2c(client, &max_gmsl_dp_ser_i2c_regmap);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	dev = &priv->client->dev;

	ret = max_gmsl_dp_ser_read(priv, MAX_GMSL_DP_SER_REG_13);
	if (ret != 0) {
		dev_info(dev, "%s: MAXIM Serializer detected\n", __func__);
	} else {
		dev_err(dev, "%s: MAXIM Serializer Not detected\n", __func__);
		return -ENODEV;
	}

	ret = max_gmsl_dp_ser_parse_dt(client, priv);
	if (ret < 0) {
		dev_err(dev, "%s: error parsing device tree\n", __func__);
		return -EFAULT;
	}

	ret = max_gmsl_dp_ser_init(&client->dev);
	if (ret < 0) {
		dev_err(dev, "%s: dp serializer init failed\n", __func__);
		return -EFAULT;
	}

	priv->ser_errb = of_get_named_gpio(ser, "ser-errb", 0);

	ret = devm_gpio_request_one(&client->dev, priv->ser_errb,
				    GPIOF_IN, "GPIO_MAXIM_SER");
	if (ret < 0) {
		dev_err(dev, "%s: GPIO request failed\n ret: %d",
			__func__, ret);
		return ret;
	}

	if (gpio_is_valid(priv->ser_errb)) {
		priv->ser_irq = gpio_to_irq(priv->ser_errb);
		ret = request_threaded_irq(priv->ser_irq, NULL,
					   max_gsml_dp_ser_irq_handler,
					   IRQF_TRIGGER_FALLING
					   | IRQF_ONESHOT, "SER", priv);
		if (ret < 0) {
			dev_err(dev, "%s: Unable to register IRQ handler ret: %d\n",
				__func__, ret);
			return ret;
		}

	}
	return ret;
}

#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
static int max_gmsl_dp_ser_remove(struct i2c_client *client)
#else
static void max_gmsl_dp_ser_remove(struct i2c_client *client)
#endif
{
	struct max_gmsl_dp_ser_priv *priv = i2c_get_clientdata(client);

	i2c_unregister_device(client);
	gpiod_set_value_cansleep(priv->gpiod_pwrdn, 0);

#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
	return 0;
#endif
}

#ifdef CONFIG_PM
static int max_gmsl_dp_ser_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max_gmsl_dp_ser_priv *priv = i2c_get_clientdata(client);

	/* Drive PWRDNB pin low to power down the serializer */
	gpiod_set_value_cansleep(priv->gpiod_pwrdn, 0);
	return 0;
}

static int max_gmsl_dp_ser_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max_gmsl_dp_ser_priv *priv = i2c_get_clientdata(client);
	int ret = 0;

	/*
	  Drive PWRDNB pin high to power up the serializer
	  and initialize all registes
	 */
	ret = max_gmsl_dp_ser_init(&client->dev);
	if (ret < 0) {
		dev_err(&priv->client->dev, "%s: dp serializer init failed\n", __func__);
	}
	return ret;
}

const struct dev_pm_ops max_gmsl_dp_ser_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(
		max_gmsl_dp_ser_suspend, max_gmsl_dp_ser_resume)
};

#endif

static const struct of_device_id max_gmsl_dp_ser_dt_ids[] = {
	{ .compatible = "maxim,max_gmsl_dp_ser" },
	{},
};
MODULE_DEVICE_TABLE(of, max_gmsl_dp_ser_dt_ids);

static struct i2c_driver max_gmsl_dp_ser_i2c_driver = {
	.driver	= {
		.name		= "max_gmsl_dp_ser",
		.of_match_table	= of_match_ptr(max_gmsl_dp_ser_dt_ids),
#ifdef CONFIG_PM
		.pm	= &max_gmsl_dp_ser_pm_ops,
#endif
	},
#if defined(NV_I2C_DRIVER_STRUCT_HAS_PROBE_NEW) /* Dropped on Linux 6.6 */
	.probe_new	= max_gmsl_dp_ser_probe,
#else
	.probe		= max_gmsl_dp_ser_probe,
#endif
	.remove		= max_gmsl_dp_ser_remove,
};

module_i2c_driver(max_gmsl_dp_ser_i2c_driver);

MODULE_DESCRIPTION("Maxim DP GMSL Serializer Driver");
MODULE_AUTHOR("Vishwaroop");
MODULE_LICENSE("GPL");
