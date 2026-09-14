/**
 * max96712_mode_tbls.h - Deserializer mode tables
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
#ifndef __DESER_MAXI2C_TABLES__
#define __DESER_MAXI2C_TABLES__

/// Driver Version ///
#define DESER_DRIVER_VERSION_MAJOR 1
#define DESER_DRIVER_VERSION_MINOR 4
#define DESER_DRIVER_VERSION_PATCH 1


#define OUTPUT_STREAM 0xf1f0
#define SLEEP 0xf1f1
#define MAX96712_TABLE_END 0xff01

const int verbosity_level=0;

struct index_reg_8
{
	u16 addr;
	u16 val;
};

struct i2c_fingerprint
{
	u16 i2c_addr;
	u16 reg_addr;
	u16 val;
};

// MAX96712 i2c address
#define SOURCE_ID 0x01

// MAX96712 number of GMSL ports 
#define N_GMSL_PORTS 4
#define N_GMSL_TRANS 2
#define N_SER_PIPES 4
#define N_DSER_PIPES 8
#define N_DSER_I2C_BUS 2

#define SLEEP_TIME 100 /* In datasheet, min wait is 100ms after reboot */
			/* Measured min slip time is 40 ms */

#define GMSL_LINKS_EN_REG 	0x0006
#define GMSL_PIPES_01_REG 	0x00F0
#define GMSL_PIPES_23_REG 	0x00F1
#define GMSL_PIPES_45_REG 	0x00F2
#define GMSL_PIPES_67_REG 	0x00F3
#define GMSL_PIPES_ENABLE 	0x00F4
#define GMSL_LINKS_CC_REG 	0x0003
#define GMSL_CC_X_OVR_REG 	0x0007

#define GMSL_VID_ST_MAP 			0x090D
#define GMSL_12BIT_MODE 			0x2C
#define GMSL_10BIT_MODE 			0x2B

#define MAX9295_GMSL_LINK_RATE_CTRL 0x0001
#define MAX9295_GMSL_6GBPS_MODE 	0x08
#define VIDEO_LOCK_STATUS_REG 		0x1DC // Video lock status register are 0x1DC, 0x1FC, 0x21C.. 
#define MAX96712_GPIOA_ADDR 0x300
#define MAX96712_GPIOB_ADDR 0x337
#define MAX96712_GPIOC_ADDR 0x36D
#define MAX96712_GPIOD_ADDR 0x3A4
#define MAX96712_NB_MFP 16

#define PIPES_XZ_MASK 0x20

#define MFP3_REG 0x0309

#define ZED_ONE_SER_DFLT_ADDR 0x42

//values not used as registers in 96712
#define RIGHT_SENSOR_ADDR 0xff02
#define LEFT_SENSOR_ADDR 0xff03
#define SER_ADDR 0xff04

#define N_MAX_CAM 16  /* max number of connected sensors */
#define N_MAX_TOTAL_SER 32 /* number of ser in DT */

typedef void (*i2c_fingerprint_func)(struct i2c_fingerprint* table, const size_t size, const int ser_addr, const int left_sensor_addr, const int right_sensor_addr);
void get_zedx_alt_fingerprint(struct i2c_fingerprint* table, const size_t size, const int ser_addr, const int left_sensor_addr, const int right_sensor_addr);
void get_zedonegs_alt_fingerprint(struct i2c_fingerprint* table, const size_t size, const int ser_addr, const int left_sensor_addr, const int right_sensor_addr);
void get_zedone4k_alt_fingerprint(struct i2c_fingerprint* table, const size_t size, const int ser_addr, const int left_sensor_addr, const int right_sensor_addr);
void get_zedxhdr_alt_fingerprint(struct i2c_fingerprint* table, const size_t size, const int ser_addr, const int left_sensor_addr, const int right_sensor_addr);
void get_zedonehdr_alt_fingerprint(struct i2c_fingerprint* table, const size_t size, const int ser_addr, const int left_sensor_addr, const int right_sensor_addr);

/*
static u16 gmsl_pipes_table[] = {
	GMSL_PIPES_01_REG,
	GMSL_PIPES_01_REG,
	GMSL_PIPES_23_REG,
	GMSL_PIPES_23_REG,
	GMSL_PIPES_45_REG,
	GMSL_PIPES_45_REG,
	GMSL_PIPES_67_REG,
	GMSL_PIPES_67_REG,
};
*/

static struct i2c_fingerprint zedx_fingerprint[] = {
	{0x40, 0x0000, 0xC4}, // EP Serializer device ID -> special_case...
	{0x62, 0x000D, 0x95}, // Serializer device ID
	{0x10, 0x3000, 0x0A}, // Image sensor device ID1
	{0x10, 0x3001, 0x56}, // Image sensor device ID2
	{0x18, 0x3000, 0x0A}, // Image sensor device ID1
	{0x18, 0x3001, 0x56}, // Image sensor device ID2
	{MAX96712_TABLE_END, 0x00, 0x00}
};

void get_zedx_alt_fingerprint(struct i2c_fingerprint* table, const size_t size,const int ser_addr, const int left_sensor_addr, const int right_sensor_addr){
	struct i2c_fingerprint zedx_alt_fingerprint[] = {
		{0x62, 				0x0000, ser_addr<<1}, // Set serializer addr 
		{ser_addr, 			0x000D, 0x95}, // Fix addr
		{0x10, 				0x301B, 0x50}, // Image sensor @ x10 unlock reg 
		{0x10, 				0x31FD, left_sensor_addr<<1}, // Image sensor @ x10 change addr 
		{left_sensor_addr, 0x301B, 0x58}, // Image sensor new addr relock reg 
		{0x18, 				0x301B, 0x50}, // Image sensor @ x18 unlock reg 
		{0x18, 				0x31FC, right_sensor_addr<<1}, // Image sensor @ x18 change addr
		{right_sensor_addr,0x301B, 0x58}, // Image sensor new addr relock reg 
		{MAX96712_TABLE_END,0x00, 	0x00}
	};

	if(size < sizeof(zedx_alt_fingerprint)){
		printk(KERN_ERR "zedx_alt_fingerprint: table size too small %zu < %zu\n", size, sizeof(zedx_alt_fingerprint));
	}

	memcpy(table, zedx_alt_fingerprint, sizeof(zedx_alt_fingerprint));
}

static struct i2c_fingerprint zedx_sensor_reset[]= {
	//Test reset addr
  	//	{0x63, 0x0000, 0x84}, /* Fsync pin is pulled up */
	{0x62, 0x02D4, 0x60}, /* Set sensor MFP to push pull */
	{0x62, 0x02D3, 0x80}, /* Power off sensor MFP */
	{0x62, 0x02D3, 0x90}, /* Power on sensor@x18 MFP */
	{0x62, 0x02D7, 0x60}, /* Set sensor MFP to push pull */
	{0x62, 0x02D6, 0x80}, /* Power off sensor MFP */
	{0x62, 0x02D6, 0x90}, /* Power on sensor MFP@x10 */
	{MAX96712_TABLE_END, 0x00, 0x00},
};

static struct index_reg_8 zedx_mappings[] = {
	/* First mapping register is the MIPI mapping,
	 * it depends on the buswidth of the deserializer *
	 * The address and the port might change*/
	{ 0x092D, 0x00}, // All mappings to controller 0 (port B/E)
	{ 0x0415, 0x39}, // 2500Mbps for zedx

	/* Then comes the input stream registers,
	 * only the address might change*/
	{ 0x090B, 0x07}, // Enable 3 mappings  Pipe 0 //video0
	{ 0x090D, 0x2B}, // Input RAW10, VC0
	{ 0x090F, 0x00}, // Input FS, VC0
	{ 0x0911, 0x01}, // Input FE, VC0

	/* Then comes the output stream registers,
	 * the address and the vc-id might change*/
	{OUTPUT_STREAM, 0x00},
	{ 0x090E, 0x2B}, // Output RAW10, VC0
	{ 0x0910, 0x00}, // Output FS, VC0
	{ 0x0912, 0x01}, // Output FE, VC0
	{MAX96712_TABLE_END, 0x00}
};

static struct i2c_fingerprint zedonegs_sensor_reset[]= {
	//{0x43, 0x0000, 0x84}
	{0x42, 0x02BF, 0x60}, /* Set sensor MFP to push pull */
	{0x42, 0x02BE, 0x80}, /* Power off sensor MFP */
	{0x42, 0x02BE, 0x90}, /* Power on sensor MFP */
	{MAX96712_TABLE_END, 0x00, 0x00}
};

static struct i2c_fingerprint zedonegs_fingerprint[] = {
	{0x42, 0x000D, 0x91}, /* Serializer device ID */
	{0x10, 0x3000, 0x0A}, // Image sensor device ID1
	{0x10, 0x3001, 0x56}, // Image sensor device ID2
	{MAX96712_TABLE_END, 0x00, 0x00}
};

void get_zedonegs_alt_fingerprint(struct i2c_fingerprint* table, const size_t size,const int ser_addr, const int left_sensor_addr, const int right_sensor_addr){
	struct i2c_fingerprint zedonegs_alt_fingerprint[] = {
		{0x42, 				0x0000, ser_addr<<1}, // Set serializer addr 
		{ser_addr, 			0x000D, 0x95}, // Fix addr
		{0x10, 				0x301B, 0x50}, // Image sensor @ x10 unlock reg 
		{0x10, 				0x31FD, right_sensor_addr<<1}, // Image sensor @ x10 change addr 
		{right_sensor_addr, 0x301B, 0x58}, // Image sensor new addr relock reg 
		{MAX96712_TABLE_END, 0x00, 0x00},
	};

	if(size < sizeof(zedonegs_alt_fingerprint)){
		printk(KERN_ERR "zedonegs_alt_fingerprint: table size too small %zu < %zu\n", size, sizeof(zedonegs_alt_fingerprint));
	}

	memcpy(table, zedonegs_alt_fingerprint, sizeof(zedonegs_alt_fingerprint));
}

static struct index_reg_8 zedonegs_mappings[] = {
	/* First mapping register is the MIPI mapping,
	 * it depends on the buswidth of the deserializer *
	 * The address and the port might change*/
	{ 0x092D, 0x00}, // All mappings to controller 0 (port B/E)
	{ 0x0415, 0x39}, // 2500Mbps for zedonegs

	/* Then comes the input stream registers,
	 * only the address might change*/
	{ 0x090B, 0x07}, // Enable 3 mappings  Pipe 0 //video0
	{ 0x090D, 0x2B}, // Input RAW10, VC0
	{ 0x090F, 0x00}, // Input FS, VC0
	{ 0x0911, 0x01}, // Input FE, VC0

	/* Then comes the output stream registers,
	 * the address and the vc-id might change*/
	{OUTPUT_STREAM, 0x00},
	{ 0x090E, 0x2B}, // Output RAW10, VC0
	{ 0x0910, 0x00}, // Output FS, VC0
	{ 0x0912, 0x01}, // Output FE, VC0
	{MAX96712_TABLE_END, 0x00}
};

static struct i2c_fingerprint zedxhdr_sensor_reset[]= {
//	{0x61, 0x0000, 0xC0},

	{0x60, 0x02D3, 0x80}, /* FSYNC sensor 2 down */
	{0x60, 0x02D6, 0x80}, /* FSYNC sensor 2 down */

	{0x60, 0x02D9, 0x80}, /* FSYNC sensor 1 down */
	{0x60, 0x02DC, 0x80}, /* FSYNC sensor 2 down */

	{MAX96712_TABLE_END, 0x00, 0x00}
};

static struct i2c_fingerprint zedxhdr_fingerprint[] = {
	{0x60, 0x000D, 0x95}, /* Serializer device ID */
	//{0x1A, 0x8A54, 0x1A}, // Image sensor device i2c address 
	{MAX96712_TABLE_END, 0x00, 0x00}
};

void get_zedxhdr_alt_fingerprint(struct i2c_fingerprint* table, const size_t size, const int ser_addr,const int left_sensor_addr,const int right_sensor_addr){
	struct i2c_fingerprint zedxhdr_alt_fingerprint[] = {
		{0x60, 				0x0000, ser_addr<<1}, // Set serializer addr 
		{ser_addr, 			0x000D, 0x95}, // Fix serializer addr

		{ser_addr, 0x02D6, 0x90}, /* Power on sensor MFP */
		{SLEEP, 0x00, 0x00},

		{0x1a, 				0x8A54, right_sensor_addr}, // Write sensor new addr

		{ser_addr, 0x02D6, 0x80}, /* Power off sensor MFP */
		{ser_addr, 0x02D3, 0x90}, /* Power on sensor MFP */
		{SLEEP, 0x00, 0x00},

		{0x1a, 				0x8A54, left_sensor_addr}, // Write sensor new addr

		{ser_addr, 0x02D3, 0x80}, /* Power off sensor MFP */

		{ser_addr, 0x02D9, 0x90}, /* FSYNC sensor 1 up */
		{ser_addr, 0x02DC, 0x90}, /* FSYNC sensor 2 up */
		{SLEEP, 0x00, 0x00},

		{ser_addr, 0x02D6, 0x90}, /* Power on sensor MFP */
		{ser_addr, 0x02D3, 0x90}, /* Power on sensor MFP */

		{MAX96712_TABLE_END, 0x00, 0x00},
	};

	memcpy(table, zedxhdr_alt_fingerprint, sizeof(zedxhdr_alt_fingerprint));
}

static struct index_reg_8 zedxhdr_mappings[] = {
	/* First mapping register is the MIPI mapping,
	 * it depends on the buswidth of the deserializer *
	 * The address and the port might change*/
	{ 0x092D, 0x00}, // All mappings to controller 0 (port B/E)
	{ 0x0415, 0x34}, // 2000Mbps for zedxhdr

	/* Then comes the input stream registers,
	 * only the address might change*/
	{ 0x090B, 0x07}, // Enable 3 mappings  Pipe 0 //video0
	{ 0x090D, 0x1E}, // Input RAW10, VC0
	{ 0x090F, 0x00}, // Input FS, VC0
	{ 0x0911, 0x01}, // Input FE, VC0

	/* Then comes the output stream registers,
	 * the address and the vc-id might change*/
	{OUTPUT_STREAM, 0x00},
	{ 0x090E, 0x1E}, // Output RAW10, VC0
	{ 0x0910, 0x00}, // Output FS, VC0
	{ 0x0912, 0x01}, // Output FE, VC0
	{MAX96712_TABLE_END, 0x00}
};

static struct i2c_fingerprint zedonehdr_sensor_reset[]= {
  	//	{0x40, 0x0000, 0x84}, /* Fsync pin is pulled up */
	{0x42, 0x02BE, 0x90}, /* Power on sensor MFP */
	{SLEEP, 0x00, 0x00},
	{SLEEP, 0x00, 0x00},
	{SLEEP, 0x00, 0x00},

  	{0x42, 0x02D6, 0x80}, /* Fsync pin is pulled down -> set addr to 0x1a */
	{0x42, 0x02BF, 0x60}, /* Set sensor MFP to push pull */
	{SLEEP, 0x00, 0x00},

	{0x42, 0x02BE, 0x80}, /* Power off sensor MFP */
	{SLEEP, 0x00, 0x00},
	{SLEEP, 0x00, 0x00},
	
	{0x42, 0x02BE, 0x90}, /* Power on sensor MFP */
	{MAX96712_TABLE_END, 0x00, 0x00}
};

static struct i2c_fingerprint zedonehdr_fingerprint[] = {
	{0x42, 0x000D, 0x91}, /* Serializer device ID */
	//{0x1A, 0x8A54, 0x1A}, // Image sensor device i2c address 
	{MAX96712_TABLE_END, 0x00, 0x00}
};

void get_zedonehdr_alt_fingerprint(struct i2c_fingerprint* table, const size_t size,const int ser_addr, const int left_sensor_addr, const int right_sensor_addr){
	struct i2c_fingerprint zedonepro_alt_fingerprint[] = {
		{0x42, 		0x0000, ser_addr<<1}, // Set serializer addr 
		{ser_addr, 	0x000D, 0x95}, // Fix serializer addr
		{SLEEP, 0x00, 0x00},
		{SLEEP, 0x00, 0x00},

		{0x1a, 		0x8A54, right_sensor_addr}, // Write sensor new addr

		{ser_addr, 	0x02be, 0x80}, /* Power off sensor MFP */
		{ser_addr,	0x02D6, 0x90}, /* Fsync pin is pulled up */
		{SLEEP, 	0x00, 	0x00},
		{SLEEP, 	0x00, 	0x00},
		{ser_addr, 	0x02be, 0x90}, /* Power on sensor MFP */
		{MAX96712_TABLE_END, 0x00, 0x00},
	};

	if(size < sizeof(zedonepro_alt_fingerprint)){
		printk(KERN_ERR "zedonepro_alt_fingerprint: table size too small %zu < %zu\n", size, sizeof(zedonepro_alt_fingerprint));
	}

	memcpy(table, zedonepro_alt_fingerprint, sizeof(zedonepro_alt_fingerprint));
}

static struct index_reg_8 zedonehdr_mappings[] = {
	/* First mapping register is the MIPI mapping,
	 * it depends on the buswidth of the deserializer *
	 * The address and the port might change*/
	{ 0x092D, 0x00}, // All mappings to controller 0 (port B/E)
	//{ 0x0415, 0x2A}, // 1600Mbps/lane on port D
	{ 0x0415, 0x34}, // 1600Mbps/lane on port D

	/* Then comes the input stream registers,
	 * only the address might change*/
	{ 0x090B, 0x07}, // Enable 3 mappings  Pipe 0 //video0
	{ 0x090D, 0x1E}, // Input YUV8, VC0
	{ 0x090F, 0x00}, // Input FS, VC0
	{ 0x0911, 0x01}, // Input FE, VC0

	/* Then comes the output stream registers,
	 * the address and the vc-id might change*/
	{OUTPUT_STREAM, 0x00},
	{ 0x090E, 0x1E}, // Output YUV8, VC0
	{ 0x0910, 0x00}, // Output FS, VC0
	{ 0x0912, 0x01}, // Output FE, VC0
	{MAX96712_TABLE_END, 0x00}
};

static struct i2c_fingerprint zedone4k_sensor_reset[]= {
	//	{0x44, 0x0000, 0x84}, /* Reset the serializer */
	{0x42, 0x02BF, 0x60}, /* Set sensor MFP to push pull */
	{0x42, 0x02BE, 0x80}, /* Power off sensor MFP */
	{0x42, 0x02BE, 0x90}, /* Power on sensor MFP */
	{MAX96712_TABLE_END, 0x00, 0x00}
};

static struct i2c_fingerprint zedone4k_fingerprint[] = {
	{0x42, 0x000D, 0x91}, /* Serializer device ID */
	{0x1A, 0x3028, 0xCA}, /* VMAX_MSB register of the IMX678 */
	{0x1A, 0x302C, 0x4C}, /* HMAX_MSB register of the IMX678 */
	{MAX96712_TABLE_END, 0x00, 0x00}
};

void get_zedone4k_alt_fingerprint(struct i2c_fingerprint* table, const size_t size, const int ser_addr, const int left_sensor_addr, const int right_sensor_addr){
	struct i2c_fingerprint zedone4k_alt_fingerprint[] = {
		{0x42, 0x0000, ser_addr<<1}, /* SER address becomes 0x44  */
		{MAX96712_TABLE_END, 0x00, 0x00},
	};

	if(size < sizeof(zedone4k_alt_fingerprint)){
		printk(KERN_ERR "zedonepro_alt_fingerprint: table size too small %zu < %zu\n", size, sizeof(zedone4k_alt_fingerprint));
	}

	memcpy(table, zedone4k_alt_fingerprint, sizeof(zedone4k_alt_fingerprint));
}

static struct index_reg_8 zedone4k_mappings[] = {
	/* First mapping register is the MIPI mapping,
	 * it depends on the buswidth of the deserializer *
	 * The address and the port might change*/
	{ 0x092D, 0x00}, // All mappings to controller 0 (port A)
	{ 0x0415, 0x2f}, // 1500Mbps for zedone4k

	/* Then comes the input stream registers,
	 * only the address might change*/
	{ 0x090B, 0x07}, // Enable 3 mappings  Pipe 0 //video0
	{ 0x090D, 0x2C}, // Input RAW10, VC0
	{ 0x090F, 0x00}, // Input RAW10, VC0
	{ 0x0911, 0x01}, // Input FS, VC0

	/* Then comes the output stream registers,
	 * the address and the vc-id might change*/
	{OUTPUT_STREAM, 0x00},
	{ 0x090E, 0x2C}, // Output RAW10, VC0
	{ 0x0910, 0x00}, // Output FS, VC0
	{ 0x0912, 0x01}, // Output FE, VC0
	{MAX96712_TABLE_END, 0x00}
};

// Reset only the necessary registers when the deserializer has already been probed.
static struct index_reg_8 max96712_fast_reset[] = {
	{0x0003, 0xFF}, // Disable the control channel to all GMSL links
	{0x0007, 0x00}, // Disable the control channel crossover
					// Between port 0 and 1
	{0x0003, 0xAA}, // Enable the control channel 0 on all GMSL links
	{0x0006, 0xFF}, // Enable all GMSL links
	{0x00F0, 0x62}, // Reset the piping of MIPI PHY 0 and 1
	{0x00F1, 0xEA}, // Reset the piping of MIPI PHY 2 and 3
	{MAX96712_TABLE_END, 0x00}
};

// Link status registers
static struct index_reg_8 max96712_link_regs[] = {
	{0x001A, 0x00}, // GMSL Link A status register
	{0x000A, 0x00}, // GMSL Link B status register
	{0x000B, 0x00}, // GMSL Link C status register
	{0x000C, 0x00}, // GMSL Link D status register
	{MAX96712_TABLE_END, 0x00}
};


static struct index_reg_8 max96712_7_fps[] = {
	{0x04A5, 0xD0}, // frame rate
	{0x04A6, 0xdc}, // frame rate
	{0x04A7, 0x32}, // frame rate
	{MAX96712_TABLE_END, 0x00}
};

static struct index_reg_8 max96712_15_fps[] = {
	{0x04A5, 0x68}, // frame rate
	{0x04A6, 0x6e}, // frame rate
	{0x04A7, 0x19}, // frame rate
	{MAX96712_TABLE_END, 0x00}
};

static struct index_reg_8 max96712_25_fps[] = {
	{0x04A5, 0x3E}, // frame rate
	{0x04A6, 0x42}, // frame rate
	{0x04A7, 0x0F}, // frame rate
	{MAX96712_TABLE_END, 0x00}
};

static struct index_reg_8 max96712_30_fps[] = {
	{0x04A5, 0x34}, // frame rate
	{0x04A6, 0xB7}, // frame rate
	{0x04A7, 0x0C}, // frame rate
	{MAX96712_TABLE_END, 0x00}
};

static struct index_reg_8 max96712_60_fps[] = {
	{ 0x04A5, 0x9A}, // frame rate
	{ 0x04A6, 0x5B}, // frame rate
	{ 0x04A7, 0x06}, // frame rate
	{ MAX96712_TABLE_END, 0x00}
};

static struct index_reg_8 max96712_120_fps[] = {
	{0x04A5, 0xCD}, // frame rate
	{0x04A6, 0x2D}, // frame rate
	{0x04A7, 0x03}, // frame rate
	{MAX96712_TABLE_END, 0x00}
};

// TODO: More comments about the registers
// Initialization that is camera independent.
// Rename into max96712_2x4_init_table.
// Create max96712_4x2_init_table.
static struct index_reg_8 max96712_init_table[] = {
	// Full reset, registers and paths
	{0x0013, 0x40},
	// The following lines comes from the datasheet
	// and are setting up the max96712. We don't have
	// any documentation regarding them.
	{0x1458, 0x28},
	{0x1459, 0x68},
	{0x1558, 0x28},
	{0x1559, 0x68},
	{0x1658, 0x28},
	{0x1659, 0x68},
	{0x1758, 0x28},
	{0x1759, 0x68},

	//{MAX96712_ADDRESS, 0x0018, 0x0F}, // Reset everything except the registers
	{0x0001, 0xcc}, // I2C0 and I2C1 are both connected to control channel // I2C2 is disabled

	// MIPI PHY configuration
	{0x08A0, 0x01}, // CSI output is 4x2
	{0x08A3, 0x44}, // 4x2 lane mapping
	{0x08A4, 0x44}, // 4x2 lane mapping

	{ 0x0415, 0x30}, //
	{ 0x0418, 0x30}, // 1600Mbps/lane on port D
	{ 0x041B, 0x30}, // 1600Mbps/lane on port E
	{ 0x041E, 0x30},

	{ 0x090A, 0x40}, // 2 lanes on port C
	{ 0x094A, 0x40}, // 2 lanes on port D
	{ 0x098A, 0x40}, // 2 lanes on port E
	{ 0x09CA, 0x40}, // 2 lanes on port F

	// // MFP2
	// {0x0005, 0x80}, // Disable ERRB to MFP3
	// {0x0309, 0x01}, // Disable output drivers
	// {0x030A, 0xA0}, // Enable pull-down

	{ 0x04AF, 0xC0}, // AUTO_FS_LINKS = 0, FS_USE_XTAL = 1, FS_LINK_[3:0] = 0
	{ 0x04A0, 0x24}, // Internal Frame sync mode, manual frame sync, Fsync on MFP10

	{ 0x04A2, 0x00}, // Turn off auto master link selection
	{ 0x04AA, 0x00}, // OVLP window = 0
	{ 0x04AB, 0x00},

	{ 0x04A5, 0x35}, // 30Hz FSYNC
	{ 0x04A6, 0xB7},
	{ 0x04A7, 0x0C},
	{ 0x04B1, 0x80}, // FSYNC TX ID is x10

	// Deskew configuration
	{ 0x0903, 0x80}, // Auto initial deskew ON for controller 0
	{ 0x0943, 0x80}, // Auto initial deskew ON for controller 1
	{ 0x0983, 0x80}, // Auto initial deskew ON for controller 2
	{ 0x09c3, 0x80}, // Auto initial deskew ON for controller 3

	{ 0x0904, 0x91}, // Periodic deskew ON, deskew packets every 2 frames, width = 2K UI
	{ 0x0944, 0x91}, // Periodic deskew ON, deskew packets every 2 frames, width = 2K UI
	{ 0x0984, 0x91}, // Periodic deskew ON, deskew packets every 2 frames, width = 2K UI
	{ 0x09c4, 0x91}, // Periodic deskew ON, deskew packets every 2 frames, width = 2K UI

	{ MAX96712_TABLE_END, 0x00},
};

static struct index_reg_8 max96712_2x4_init_table[] = {
	// Full reset, registers and paths
	{0x0013, 0x40},
	// The following lines comes from the datasheet
	// and are setting up the max96712. We don't have
	// any documentation regarding them.
	{0x1458, 0x28},
	{0x1459, 0x68},
	{0x1558, 0x28},
	{0x1559, 0x68},
	{0x1658, 0x28},
	{0x1659, 0x68},
	{0x1758, 0x28},
	{0x1759, 0x68},

	//{MAX96712_ADDRESS, 0x0018, 0x0F}, // Reset everything except the registers
	{0x0001, 0xcc}, // I2C0 and I2C1 are both connected to control channel // I2C2 is disabled

	// MIPI PHY configuration
	{0x08A0, 0x24}, // CSI output is 2x4 with dual 4-lane PHY mode
	{0x08A2, 0xF0}, // Enable 2x4 output PHYs
	{0x08A3, 0xE4}, // 4x2 lane mapping
	{0x08A4, 0xE4}, // 4x2 lane mapping

	{ 0x0415, 0x28},
	{ 0x0418, 0x2A}, // 2x4 master PHY A output rate
	{ 0x041B, 0x2A}, // 2x4 master PHY B output rate
	{ 0x041E, 0x28},

	{ 0x090A, 0xC0}, // 0 lanes on port C
	{ 0x094A, 0xC0}, // TODO: D0 4 lanes on port D
	{ 0x098A, 0xC0}, // TODO: D0 4 lanes on port E
	{ 0x09CA, 0xC0}, // 0 lanes on port F

	/*
	{0x0316, 0x2b},
	{0x0330, 0x04}, // Set MIPI Phy Mode: 2x(1x4) mode
	{0x0333, 0x4E}, // lane maps - all 4 ports mapped straight
	{0x0334, 0xE4}, // Additional lane map
	{0x040A, 0x00}, // lane count - 0 lanes striping on controller 0 (Port A slave in 2x1x4 mode).
	{0x044A, 0xd0}, // lane count - 4 lanes striping on controller 1 (Port A master in 2x1x4 mode).
	{0x048A, 0xd0}, // lane count - 4 lanes striping on controller 2 (Port B master in 2x1x4 mode).
	{0x04CA, 0x00}, // lane count - 0 lanes striping on controller 3 (Port B slave in 2x1x4 mode).
	{0x031D, 0x2C}, // MIPI clock rate - 1.5Gbps from controller 0 clock (Port A slave in 2x1x4 mode).
	{0x0320, 0x2C}, // MIPI clock rate - 1.5Gbps from controller 1 clock (Port A master in 2x1x4 mode).
	{0x0323, 0x2C}, // MIPI clock rate - 1.5Gbps from controller 2 clock (Port B master in 2x1x4 mode).
	{0x0326, 0x2C}, // MIPI clock rate - 1.5Gbps from controller 2 clock (Port B slave in 2x1x4 mode).
	{0x0050, 0x00}, // Route data from stream 0 to pipe X
	{0x0051, 0x01}, // Route data from stream 0 to pipe Y MAXIM INTEGRATED CONFIDENTIAL
	{0x0052, 0x02}, // Route data from stream 0 to pipe Z
	{0x0053, 0x03}, // Route data from stream 0 to pipe U
	*/
#if 0
	{0x040B,0x00}, //  (CSI_OUT_EN): CSI output disabled

	{0x0006,0xF1}, // (Default)  (LINK_EN_A): Enabled |  (LINK_EN_B): Disabled |  (LINK_EN_C): Disabled |  (LINK_EN_D): Disabled
	{0x0003,0xFE}, // (Default)  (GMSL Link A I2C Port 0): Enabled |  (GMSL Link B I2C Port 0): Disabled |  (GMSL Link C I2C Port 0): Disabled |  (GMSL Link D I2C Port 0): Disabled

	{0x00F0,0x60}, // (Default)  (Pipe 0 GMSL2 PHY): A |  (Pipe 0 Input Pipe): X
	{0x00F4,0x01}, // (Default)  (Video Pipe 0): Enabled |  (Video Pipe 1): Disabled |  (Video Pipe 2): Disabled |  (Video Pipe 3): Disabled | (Default)  (Video Pipe 4): Disabled | (Default)  (Video Pipe 5): Disabled | (Default)  (Video Pipe 6): Disabled | (Default)  (Video Pipe 7): Disabled

	{0x090B,0x07}, //  (MAP_EN_L Pipe 0): 0x7
	{0x090C,0x00}, // (Default)  (MAP_EN_H Pipe 0): 0x0
	{0x090D,0x2C}, //  (MAP_SRC_0 Pipe 0 DT): 0x2C | (Default)  (MAP_SRC_0 Pipe 0 VC): 0x0
	{0x090E,0x2C}, //  (MAP_DST_0 Pipe 0 DT): 0x2C | (Default)  (MAP_DST_0 Pipe 0 VC): 0x0
	{0x090F,0x00}, // (Default)  (MAP_SRC_1 Pipe 0 DT): 0x0 | (Default)  (MAP_SRC_1 Pipe 0 VC): 0x0
	{0x0910,0x00}, // (Default)  (MAP_DST_1 Pipe 0 DT): 0x0 | (Default)  (MAP_DST_1 Pipe 0 VC): 0x0
	{0x0911,0x01}, //  (MAP_SRC_2 Pipe 0 DT): 0x1 | (Default)  (MAP_SRC_2 Pipe 0 VC): 0x0
	{0x0912,0x01}, //  (MAP_DST_2 Pipe 0 DT): 0x1 | (Default)  (MAP_DST_2 Pipe 0 VC): 0x0
	{0x092D,0x15}, //  (MAP_DPHY_DST_0 Pipe 0): 0x1 |  (MAP_DPHY_DST_1 Pipe 0): 0x1 |  (MAP_DPHY_DST_2 Pipe 0): 0x1

	{0x0973,0x01}, //  (ALT_MEM_MAP12 CTRL1): Alternate memory map enabled

	{0x08A0,0x04}, // (Default)  (Port Configuration): 2 (1x4)
	{0x094A,0xC0}, // (Default)  (Port A - Lane Count): 4
	{0x08A3,0xE4}, //  (Lane Map - PHY0 D0): Lane 0 |  (Lane Map - PHY0 D1): Lane 1 |  (Lane Map - PHY1 D0): Lane 2 |  (Lane Map - PHY1 D1): Lane 3
	{0x08A5,0x00}, // (Default)  (Polarity - PHY0 Lane 0): Normal | (Default)  (Polarity - PHY0 Lane 1): Normal | (Default)  (Polarity - PHY1 Lane 0): Normal | (Default)  (Polarity - PHY1 Lane 1): Normal | (Default)  (Polarity - PHY1 Clock Lane): Normal
	{0x1D00,0xF4}, //  (config_soft_rst_n - PHY1): 0x0

	{0x1C00,0xF4}, // (Default)
	{0x1D00,0xF4}, // (Default)
	{0x0418,0x26},
	{0x0415,0x26},
	{0x1C00,0xF5}, //  | (Default)  (config_soft_rst_n - PHY1): 0x1
	{0x1D00,0xF5}, //  | (Default)  (config_soft_rst_n - PHY1): 0x1
	{0x08A2,0x34}, //  (phy_Stdby_2): Put PHY2 in standby mode |  (phy_Stdby_3): Put PHY3 in standby mode
	{0x040B,0x02}, //  (CSI_OUT_EN): CSI output enabled
#endif

	{ MAX96712_TABLE_END, 0x00}
};

static int get_max96712_slave_mode_table(int mfp_trig_in, struct index_reg_8* table, const size_t size){
	int err = 0;
	// Every 16 registers, a register is inserted, this means we have to add this small increment
	// to the base offset depending on the mfp_trig_in value.
	int offset_linkA = (mfp_trig_in > 4) ? (mfp_trig_in > 9) ? (mfp_trig_in > 14) ? 
						MAX96712_GPIOA_ADDR+3 : MAX96712_GPIOA_ADDR+2 : MAX96712_GPIOA_ADDR+1 : MAX96712_GPIOA_ADDR;
	int offset_linkB = (mfp_trig_in > 2) ? (mfp_trig_in > 7) ? (mfp_trig_in > 12) ? 
						MAX96712_GPIOB_ADDR+3 : MAX96712_GPIOB_ADDR+2 : MAX96712_GPIOB_ADDR+1 : MAX96712_GPIOB_ADDR;
	int offset_linkC = (mfp_trig_in > 0) ? (mfp_trig_in > 5) ? (mfp_trig_in > 10) ? (mfp_trig_in > 15) ? 
						MAX96712_GPIOC_ADDR+4 : MAX96712_GPIOC_ADDR+3 : MAX96712_GPIOC_ADDR+2 : MAX96712_GPIOC_ADDR+1 : MAX96712_GPIOC_ADDR;
	int offset_linkD = (mfp_trig_in > 3) ? (mfp_trig_in > 8) ? (mfp_trig_in > 13) ? 
						MAX96712_GPIOD_ADDR+3 : MAX96712_GPIOD_ADDR+2 : MAX96712_GPIOD_ADDR+1 : MAX96712_GPIOD_ADDR;

	struct index_reg_8 slave_mode_table[] = {
		{0x04A0,0x08}, // External Fsync mode
		{0x04AF,0x9F}, // GMSL 2 type Fsync shared on every links 

		// mfp_trig_in = Trig_In ( default MFP10)
		{ offset_linkA+(3*mfp_trig_in), 0xc3}, // Link A: Output driver disabled, gmsl transmission enabled 
		{ offset_linkA+(3*mfp_trig_in)+1, 0x10}, // Link A: gmsl transmission address = 0x10	
		{ offset_linkB+(3*mfp_trig_in), 0x30}, // Link B: gmsl transmission enabled, gmsl transmission address = 0x10
		{ offset_linkC+(3*mfp_trig_in), 0x30}, // Link C: gmsl transmission enabled, gmsl transmission address = 0x10
		{ offset_linkD+(3*mfp_trig_in), 0x30}, // Link D: gmsl transmission enabled, gmsl transmission address = 0x10
		{MAX96712_TABLE_END, 0x00},
	};

	if(mfp_trig_in < 0 || mfp_trig_in > MAX96712_NB_MFP){
		return -EINVAL;
	}

	if(size < sizeof(slave_mode_table)){
		return -ENOMEM;
	}

	memcpy(table, slave_mode_table, sizeof(slave_mode_table));

	return err;
}

static struct index_reg_8 max96712_csi_b[] = {
	{ 0x092D, 0x2A}, // All mappings to controller 2 (port E)
	{ 0x096D, 0x2A}, // All mappings to controller 2 (port E)
	{ 0x09AD, 0x2A}, // All mappings to controller 2 (port E)
	{ 0x09ED, 0x2A}, // All mappings to controller 2 (port E)
	{ MAX96712_TABLE_END, 0x00}
};

enum
{
	ZEDX,
	ZEDONEGS,
	ZEDONE4K,
	ZEDONEHDR,
	ZEDXHDR,
	/* Don't add a camera type bellow N_CAM_TYPE, it will not be parsed*/
	N_CAM_TYPE,
};

static u8 cam_pipes[] = {
	[ZEDX] = 0x3,
	[ZEDONEGS] = 0x1,
	[ZEDONE4K] = 0x2,
	[ZEDONEHDR] = 0x1,
	[ZEDXHDR] = 0x3,
};

static const char *camera_names[] = {
	[ZEDX] = "zedx",
	[ZEDONEGS] = "zedonegs",
	[ZEDONE4K] = "zedone4k",
	[ZEDONEHDR] = "zedonehdr",
	[ZEDXHDR] = "zedxhdr",
};


static struct index_reg_8 *pipeline_table[] = {
	[ZEDX] = zedx_mappings,
	[ZEDONEGS] = zedonegs_mappings,
	[ZEDONE4K] = zedone4k_mappings,
	[ZEDONEHDR] = zedonehdr_mappings,
	[ZEDXHDR] = zedxhdr_mappings,
};

static struct i2c_fingerprint *fingerprint_table[] = {
	[ZEDX] = zedx_fingerprint,
	[ZEDONEGS] = zedonegs_fingerprint,
	[ZEDONE4K] = zedone4k_fingerprint,
	[ZEDONEHDR] = zedonehdr_fingerprint,
	[ZEDXHDR] = zedxhdr_fingerprint,
};

static i2c_fingerprint_func get_fingerprint_alt_table[] = {
	[ZEDX] = get_zedx_alt_fingerprint,
	[ZEDONEGS] = get_zedonegs_alt_fingerprint,
	[ZEDONE4K] = get_zedone4k_alt_fingerprint,
	[ZEDONEHDR] = get_zedonehdr_alt_fingerprint,
	[ZEDXHDR] = get_zedxhdr_alt_fingerprint,
};

static struct i2c_fingerprint *reset_table[] = {
	[ZEDX] = zedx_sensor_reset,
	[ZEDONEGS] = zedonegs_sensor_reset,
	[ZEDONE4K] = zedone4k_sensor_reset,
	[ZEDONEHDR] = zedonehdr_sensor_reset,
	[ZEDXHDR] = zedxhdr_sensor_reset,
};

enum
{
	MAX96712_INIT,
	MAX96712_INIT_2x4,
	MAX96712_FST_RST,
	MAX96712_LINK_REGS,
	MAX96712_7_FPS,
	MAX96712_15_FPS,
	MAX96712_25_FPS,
	MAX96712_30_FPS,
	MAX96712_60_FPS,
	MAX96712_120_FPS,
	MAX96712_CSI_B,
};

static struct index_reg_8 *mode_table[] = {
	[MAX96712_INIT] = max96712_init_table,
	[MAX96712_INIT_2x4] = max96712_2x4_init_table,
	[MAX96712_FST_RST] = max96712_fast_reset,
	[MAX96712_LINK_REGS] = max96712_link_regs,
	[MAX96712_7_FPS] = max96712_7_fps,
	[MAX96712_15_FPS] = max96712_15_fps,
	[MAX96712_25_FPS] = max96712_25_fps,
	[MAX96712_30_FPS] = max96712_30_fps,
	[MAX96712_60_FPS] = max96712_60_fps,
	[MAX96712_120_FPS] = max96712_120_fps,
	[MAX96712_CSI_B] = max96712_csi_b,
};

#endif /* __DESER_I2C_TABLES__ */
