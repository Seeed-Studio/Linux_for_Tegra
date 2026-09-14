/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES.*/
/* All rights reserved. */

#ifndef __MAX929X_H__
#define __MAX929X_H__

/* TI FPD Link III 954 deser I2C address */
#define TI954_ADDR  (0x30)
/* TI FPD Link III 953 ser I2C address */
#define TI953_ADDR  (0x18)
/* TI 953 alias address */
#define TI953_CAM1_ADDR (0x29)
#define TI953_CAM2_ADDR (0X2A)

#define MAX9295_DEV_ADDR 0x00

#define SENSOR_ADDR (0x1a)
/* CAM alias address */
#define CAM1_SENSOR_ADDR (0x1b)
#define CAM2_SENSOR_ADDR (0x1c)

#define TI954_RESET_ADDR (0x01)
#define TI954_RESET_VAL  (0x02)

#define AFDRV_I2C_ADDR (0x3E)
/*AF ctrl*/
#define AFDRV1_I2C_ADDR  (0x21)
#define AFDRV2_I2C_ADDR  (0x20)

#define EEPROM_I2C_ADDR (0x50)
/*eeprom ctrl*/
#define EEPROM1_I2C_ADDR (0x51)
#define EEPROM2_I2C_ADDR (0x52)

struct max929x_reg {
	u8 slave_addr;
	u16 reg;
	u8 val;
};

/* Serializer slave addresses */
#define SER_SLAVE1 0x40
#define SER_SLAVE2 0x62

/* Deserializer slave addresses */
#define DESER_SLAVE 0x48

/*
 * MAX9296 i2c addr 0x90(8bits) 0x48(7bits)
 * (MAX9296 link A) MAX9295 i2c addr 0xc4(8bits) 0x62(7bits)
 */
static struct max929x_reg max929x_Double_Dser_Ser_init[] = {
	/* set MFP0 low to reset sensor */
	{0x62, 0x02be, 0x80},
	/* Set SER to 1x4 mode (phy_config = 0) */
	{0x62, 0x0330, 0x00},
	{0x62, 0x0332, 0xe4},
	/* Additional lane map */
	{0x62, 0x0333, 0xe4},
	/* Set 4 lanes for serializer (ctrl1_num_lanes = 3) */
	{0x62, 0x0331, 0x31},
	/* Start video from both port A and port B. */
	{0x62, 0x0311, 0x20},
	/*
	 * Enable info lines. Additional start bits for Port A and B.
	 * Use data from port B for all pipelines
	 */
	{0x62, 0x0308, 0x62},
	/* Route 16bit DCG (DT = 0x30) to VIDEO_X (Bit 6 enable) */
	{0x62, 0x0314, 0x22},
	/* Route 12bit RAW (DT = 0x2C) to VIDEO_Y (Bit 6 enable) */
	{0x62, 0x0316, 0x6c},
	/* Route EMBEDDED8 to VIDEO_Z (Bit 6 enable) */
	{0x62, 0x0318, 0x22},
	/* Unused VIDEO_U  */
	{0x62, 0x031A, 0x22},
	/*
	 * Make sure all pipelines start transmission
	 * (VID_TX_EN_X/Y/Z/U = 1)
	 */
	{0x62, 0x0002, 0x22},
	/* Set MIPI Phy Mode: 2x(1x4) mode */
	{0x48, 0x0330, 0x04},
	/* lane maps - all 4 ports mapped straight */
	{0x48, 0x0333, 0x4E},
	/* Additional lane map */
	{0x48, 0x0334, 0xE4},
	/*
	 * lane count - 0 lanes stripping on controller 0
	 * (Port A slave in 2x1x4 mode).
	 */
	{0x48, 0x040A, 0x00},
	/*
	 * lane count - 4 lanes stripping on controller 1
	 * (Port A master in 2x1x4 mode).
	 */
	{0x48, 0x044A, 0xd0},
	/*
	 * lane count - 4 lanes stripping on controller 2
	 * (Port B master in 2x1x4 mode).
	 */
	{0x48, 0x048A, 0xd0},
	/*
	 * lane count - 0 lanes stripping on controller 3
	 * (Port B slave in 2x1x4 mode).
	 */
	{0x48, 0x04CA, 0x00},
	/*
	 * MIPI clock rate - 1.5Gbps from controller 0 clock
	 * (Port A slave in 2x1x4 mode).
	 */
	{0x48, 0x031D, 0x2f},
	/*
	 * MIPI clock rate - 1.5Gbps from controller 1 clock
	 * (Port A master in 2x1x4 mode).
	 */
	{0x48, 0x0320, 0x2f},
	/*
	 * MIPI clock rate - 1.5Gbps from controller 2 clock
	 * (Port B master in 2x1x4 mode).
	 */
	{0x48, 0x0323, 0x2f},
	/*
	 * MIPI clock rate - 1.5Gbps from controller 2 clock
	 * (Port B slave in 2x1x4 mode).
	 */
	{0x48, 0x0326, 0x2f},
	/* Route data from stream 0 to pipe X */
	{0x48, 0x0050, 0x00},
	/* Route data from stream 0 to pipe Y */
	{0x48, 0x0051, 0x01},
	/* Route data from stream 0 to pipe Z */
	{0x48, 0x0052, 0x02},
	/* Route data from stream 0 to pipe U */
	{0x48, 0x0053, 0x03},
	/* Enable all PHYS. */
	{0x48, 0x0332, 0xF0},
	/* Enable sensor power down pin. Put imager in,Active mode */
	{0x62, 0x02be, 0x90},
	/* Output RCLK to sensor. */
	{0x62, 0x03F1, 0x89},
	/* MFP8 for FSIN */
	{0x62, 0x02D8, 0x10},
	{0x62, 0x02D6, 0x04},
	/* need disable pixel clk out inb order to use MFP1 */
	{0x48, 0x0005, 0x00},
	/* GPIO TX compensation */
	{0x48, 0x02B3, 0x83},
	{0x48, 0x02B4, 0x10},
};

#endif
