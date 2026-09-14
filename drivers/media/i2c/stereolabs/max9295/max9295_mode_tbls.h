/**
 * zedx_mode_tbls.h - zedx sensor mode tables
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
#ifndef __SL_MAX9295_I2C_TABLES__
#define __SL_MAX9295_I2C_TABLES__

/// Driver Version ///
#define MAX9295_DRIVER_VERSION_MAJOR 1
#define MAX9295_DRIVER_VERSION_MINOR 4
#define MAX9295_DRIVER_VERSION_PATCH 1

#define SERIALIZER_TABLE_END 0xFF01

#define MAX9295_ID_REG 0x000D
#define GMSL_VID_PIPE_Y 0x0316
#define GMSL_VID_PIPE_Z 0x0318
#define GMSL_12BIT_MODE   0x6C
#define GMSL_10BIT_MODE   0x6B
#define ZED_MONO_ACC_BASE_ADDR		0x18
#define ZED_MONO_GYRO_BASE_ADDR		0x68
#define ZED_STEREO_ACC_BASE_ADDR		0x19
#define ZED_STEREO_GYRO_BASE_ADDR		0x69

// write to this register in the table to do msleep(100)
#define SER_MAX9295_SLEEP_100 0xBEEF
#define ZEDX_ACC_BASE_ADDR		0x19
#define ZEDX_GYRO_BASE_ADDR		0x69

const int verbosity_level=0;
#define ZEDX_ACC_BIS_ADDR_A		0x3B
#define ZEDX_GYRO_BIS_ADDR_A	0x3C
#define ZEDX_ACC_BIS_ADDR_B		0x4B
#define ZEDX_GYRO_BIS_ADDR_B	0x4C


struct index_reg_8
{
	u16 source;
	u16 addr;
	u16 val;
};

// ZED-X serializer address
// 0x62 for ZEDX-120/50
// 0x40 for ZEDX-EP150
// [Stereolabs@dev] : --> modified by the ci/build script automatically
#define MAX9295D_ADDRESS_BASE 0x62
 
// Configure the zedonegs serializer to send the video over GMSL
static struct index_reg_8 ar0234_9295A_Ser_A[] = {
	{MAX9295D_ADDRESS_BASE, 0x0010, 0x21}, //92 reset path but not registers and auto conf
	{MAX9295D_ADDRESS_BASE, 0x0311, 0x10}, //92 start port b pipe x
	{MAX9295D_ADDRESS_BASE, 0x0308, 0x71}, //92 start mipi b , b get pipe x
	{MAX9295D_ADDRESS_BASE, 0x0314, 0x6B}, //92 datatype pipe x = 10 bits raw
	{MAX9295D_ADDRESS_BASE, 0x0002, 0xff}, //92 vid enable for all ports

	//Shutter trigger gpios: x10 address and synchronicity
	{MAX9295D_ADDRESS_BASE, 0x02D6, 0x04}, // MFP8 into gpio and into reception
	{MAX9295D_ADDRESS_BASE, 0x02D8, 0x10}, // MFP8 rx address is x10

	// Remap the eeprom i2c addresses. It has two addresses at x54 and x58
	// This is for the MAX9296 deserializer boards, that come with an eeprom
	// with the same address and on the same bus.
	// {MAX9295D_ADDRESS_BASE, 0x0042, 0xc0}, // When the ser receive at x58... eeprom i2c map eeporm have two i2c address
	// {MAX9295D_ADDRESS_BASE, 0x0043, 0xac}, // it remaps at x54
	// {MAX9295D_ADDRESS_BASE, 0x0044, 0xc2}, // When the ser receive at x59...
	// {MAX9295D_ADDRESS_BASE, 0x0045, 0xae}, // it remaps at x55

	{MAX9295D_ADDRESS_BASE, 0x02BF, 0x60}, /* Set sensor MFP to push pull */
	{MAX9295D_ADDRESS_BASE, 0x02BE, 0x90}, /* Power on sensor MFP */

	{MAX9295D_ADDRESS_BASE, 0x0010, 0x21}, /* Serializer path reset to update pipeline */

	{0x00, SERIALIZER_TABLE_END, 0x00}
};

// Configure the zedonegs serializer to send the video over GMSL
static struct index_reg_8 ar0234_9295A_Ser_B[] = {
	{MAX9295D_ADDRESS_BASE, 0x0010, 0x21}, // reset path but not registers and auto conf
	{MAX9295D_ADDRESS_BASE, 0x0311, 0x40}, // start pipe Z from port B
	{MAX9295D_ADDRESS_BASE, 0x0308, 0x74}, // pipe Z get port B
	{MAX9295D_ADDRESS_BASE, 0x0318, 0x6B}, // datatype pipe z = 10 bits raw
	{MAX9295D_ADDRESS_BASE, 0x0002, 0xff}, // vid enable for all ports

	//Shutter trigger gpios: x10 address and synchronicity
	{MAX9295D_ADDRESS_BASE, 0x02D6, 0x04}, // MFP8 into gpio and into reception
	{MAX9295D_ADDRESS_BASE, 0x02D8, 0x10}, // MFP8 rx address is x10

	// Remap the eeprom i2c addresses. It has two addresses at x54 and x58
	// This is for the MAX9296 deserializer boards, that come with an eeprom
	// with the same address and on the same bus.
	// {MAX9295D_ADDRESS_BASE, 0x0042, 0xB0}, // When the ser receive at x58... eeprom i2c map eeporm have two i2c address
	// {MAX9295D_ADDRESS_BASE, 0x0043, 0xAC}, // it remaps at x54
	// {MAX9295D_ADDRESS_BASE, 0x0044, 0xB2}, // When the ser receive at x59...
	// {MAX9295D_ADDRESS_BASE, 0x0045, 0xAE}, // it remaps at x55

	{MAX9295D_ADDRESS_BASE, 0x02BF, 0x60}, /* Set sensor MFP to push pull */
	{MAX9295D_ADDRESS_BASE, 0x02BE, 0x90}, /* Power on sensor MFP */

	{MAX9295D_ADDRESS_BASE, 0x0010, 0x21}, /* Serializer path reset to update pipeline */

	{0x00, SERIALIZER_TABLE_END, 0x00}
};

// Configure the zedx serializer to send the video over GMSL
static struct index_reg_8 ar0234_9295D_Ser_A[] = {
	{MAX9295D_ADDRESS_BASE, 0x0010, 0x21}, //92 reset path but not registers and auto conf
	{MAX9295D_ADDRESS_BASE, 0x0311, 0x21}, //92 start port a pipe x and port b pipe z
	{MAX9295D_ADDRESS_BASE, 0x0308, 0x7e}, //92 start mipi a and b , a get pipe x and b get pipe z

	{MAX9295D_ADDRESS_BASE, 0x0314, 0x6B}, //92 datatype pipe x = 10 bits raw
	{MAX9295D_ADDRESS_BASE, 0x0316, 0x6B}, //92 datatype pipe y = 10 bits raw
	{MAX9295D_ADDRESS_BASE, 0x0318, 0x6B}, //92 datatype pipe z = 10 bits raw
	{MAX9295D_ADDRESS_BASE, 0x031a, 0x6B}, //92 datatype pipe u = 10 bits raw
	{MAX9295D_ADDRESS_BASE, 0x0002, 0xff}, //92 vid enable for all ports
	
	//Shutter trigger gpios: x10 address and synchronicity
	{MAX9295D_ADDRESS_BASE, 0x02D9, 0x04}, // MFP9 into gpio and into reception
	{MAX9295D_ADDRESS_BASE, 0x02DB, 0x10}, // MFP9 rx address is x10
	{MAX9295D_ADDRESS_BASE, 0x02Dc, 0x04}, // MFP10 into gpio and into reception
	{MAX9295D_ADDRESS_BASE, 0x02De, 0x10}, // MFP10 rx address is x10

	// reset camera 0
	//	{MAX9295D_ADDRESS_BASE, 0x02d3, 0x90}, // MFP7 -> enable reset pin, powerup.
	//	{MAX9295D_ADDRESS_BASE, 0x02d4, 0x60}, // MFP7 -> rx address is 0

	// reset camera 1
	//	{MAX9295D_ADDRESS_BASE, 0x02d6, 0x90}, // MFP8 -> enable reset pin, powerup.
	//	{MAX9295D_ADDRESS_BASE, 0x02d7, 0x60}, // MFP8 -> rx address is 0

	// Remap the eeprom i2c addresses. It has two addresses at x54 and x58
	// This is for the MAX9296 deserializer boards, that come with an eeprom
	// with the same address and on the same bus.
	// {MAX9295D_ADDRESS_BASE, 0x0042, ACC_TMP_ADDR}, // When the ser receive at x58... eeprom i2c map eeporm have two i2c address
	// {MAX9295D_ADDRESS_BASE, 0x0043, ZEDX_ACC_BASE_ADDR*2}, // it remaps at x54
	// {MAX9295D_ADDRESS_BASE, 0x0044, GYRO_TMP_ADDR}, // When the ser receive at x59...
	// {MAX9295D_ADDRESS_BASE, 0x0045, ZEDX_GYRO_BASE_ADDR*2}, // it remaps at x55

	{MAX9295D_ADDRESS_BASE, 0x0010, 0x21}, /* Serializer path reset to update pipeline */

	{0x00, SERIALIZER_TABLE_END, 0x00}
};

// Configure the zedx serializer to send the video over GMSL
static struct index_reg_8 ar0234_9295D_Ser_B[] = {
	{MAX9295D_ADDRESS_BASE, 0x0010, 0x21}, //92 reset path but not registers and auto conf
	{MAX9295D_ADDRESS_BASE, 0x0311, 0x84}, //92 start port a pipe Y and port b pipe U
	{MAX9295D_ADDRESS_BASE, 0x0308, 0x7b}, //92 start mipi a and b , a get pipe x and b get pipe z

	{MAX9295D_ADDRESS_BASE, 0x0314, 0x6B}, //92 datatype pipe x = 10 bits raw
	{MAX9295D_ADDRESS_BASE, 0x0316, 0x6B}, //92 datatype pipe y = 10 bits raw
	{MAX9295D_ADDRESS_BASE, 0x0318, 0x6B}, //92 datatype pipe z = 10 bits raw
	{MAX9295D_ADDRESS_BASE, 0x031a, 0x6B}, //92 datatype pipe u = 10 bits raw
	{MAX9295D_ADDRESS_BASE, 0x0002, 0xff}, //92 vid enable for all ports
	
	//Shutter trigger gpios: x10 address and synchronicity
	{MAX9295D_ADDRESS_BASE, 0x02D9, 0x04}, // MFP9 into gpio and into reception
	{MAX9295D_ADDRESS_BASE, 0x02DB, 0x10}, // MFP9 rx address is x10
	{MAX9295D_ADDRESS_BASE, 0x02Dc, 0x04}, // MFP10 into gpio and into reception
	{MAX9295D_ADDRESS_BASE, 0x02De, 0x10}, // MFP10 rx address is x10

	// reset camera 0
//	{MAX9295D_ADDRESS_BASE, 0x02d3, 0x90}, // MFP7 -> enable reset pin, powerup.
//	{MAX9295D_ADDRESS_BASE, 0x02d4, 0x60}, // MFP7 -> rx address is 0

	// reset camera 1
//	{MAX9295D_ADDRESS_BASE, 0x02d6, 0x90}, // MFP8 -> enable reset pin, powerup.
//	{MAX9295D_ADDRESS_BASE, 0x02d7, 0x60}, // MFP8 -> rx address is 0

	// Remap the eeprom i2c addresses. It has two addresses at x54 and x58
	// This is for the MAX9296 deserializer boards, that come with an eeprom
	// with the same address and on the same bus.
	// {MAX9295D_ADDRESS_BASE, 0x0042, ACC_TMP_ADDR}, // When the ser receive at x58... eeprom i2c map eeporm have two i2c address
	// {MAX9295D_ADDRESS_BASE, 0x0043, ZEDX_ACC_BASE_ADDR*2}, // it remaps at x54
	// {MAX9295D_ADDRESS_BASE, 0x0044, GYRO_TMP_ADDR}, // When the ser receive at x59...
	// {MAX9295D_ADDRESS_BASE, 0x0045, ZEDX_GYRO_BASE_ADDR*2}, // it remaps at x55

	{MAX9295D_ADDRESS_BASE, 0x0010, 0x21}, /* Serializer path reset to update pipeline */

	{0x00, SERIALIZER_TABLE_END, 0x00}
};

// Configure the zed one uhd serializer to send the video over GMSL
static struct index_reg_8 imx678_9295A_Ser_A[] = {
	// Delay 100mS    }
	//{0x44, 0x0010, 0x81}, // Apply Reset Oneshot for changes
	// Delay 100mS    }
	// Serializer MIPI} CSI-2 PHY settings
	{0x44, 0x02BE, 0x00}, // Drive output low (sensor disabled)
	{0x44, 0x02BF, 0xA0}, // Output is push-pull

	{0x44, 0x02be, 0x80}, // set MFP0 low to reset sensor

	{0x44, 0x0330, 0x10}, // Set SER to 1x4 mode (phy_config = 0)
	{0x44, 0x0332, 0xE4}, // Verify lane map is at its default (phy1_lane_map = 4'hE, phy2_lane_map = 4'h4 )
	{0x44, 0x0333, 0xe4}, // Additional lane map
	{0x44, 0x0331, 0x33}, // Set 4 lanes for serializer (ctrl1_num_lanes = 3)
	{0x44, 0x0311, 0x30}, // Start video from both port A and port B.

	{0x44, 0x0308, 0x63}, // Enable info lines. Additional start bits for Port A and B. Use data from port B for all pipelines.
	{0x44, 0x0314, 0x22}, // Route 16bit DCG (DT = 0x30) to VIDEO_X (Bit 6 enable)
	{0x44, 0x0316, 0x6C}, // Route 12bit RAW (DT = 0x2C) to VIDEO_Y (Bit 6 enable)
	{0x44, 0x0318, 0x22}, // Route EMBEDDED8 to VIDEO_Z (Bit 6 enable)
	{0x44, 0x031A, 0x22}, // Unused VIDEO_U
	{0x44, 0x0002, 0x33}, // Make sure all pipelines start transmission (VID_TX_EN_X/Y/Z/U = 1)
	{0x44, 0x02BE, 0x10}, // Drive output high (sensor enabled)

	// Sh4tter trigger gpios: x10 address and synchronicity
	{0x44, 0x02D6, 0x04}, // MFP8 into gpio and into reception
	{0x44, 0x02D8, 0x10}, // MFP8 rx address is x10
						  /// For4MAX9296 only, modify MFP src for FSIN
	
	{0x44, 0x0010, 0x21}, // Apply Reset Oneshot for changes

	{0x00, SERIALIZER_TABLE_END, 0x00}
};

static struct index_reg_8 imx678_9295A_Ser_B[] = {
	// Delay 100mS    }
	//{0x44, 0x0010, 0x81}, // Apply Reset Oneshot for changes
	// Delay 100mS    }
	// Serializer MIPI} CSI-2 PHY settings
	{0x44, 0x02BE, 0x00}, // Drive output low (sensor disabled)
	{0x44, 0x02BF, 0xA0}, // Output is push-pull

	{0x44, 0x02be, 0x80}, // set MFP0 low to reset sensor

	{0x44, 0x0330, 0x10}, // Set SER to 1x4 mode (phy_config = 0)
	{0x44, 0x0332, 0xE4}, // Verify lane map is at its default (phy1_lane_map = 4'hE, phy2_lane_map = 4'h4 )
	{0x44, 0x0333, 0xe4}, // Additional lane map
	{0x44, 0x0331, 0x33}, // Set 4 lanes for serializer (ctrl1_num_lanes = 3)
	{0x44, 0x0311, 0xC0}, // Start video from both port A and port B.

	{0x44, 0x0308, 0x6C}, // Enable info lines. Additional start bits for Port A and B. Use data from port B for all pipelines.

	{0x44, 0x0314, 0x22}, // Route 16bit DCG (DT = 0x30) to VIDEO_X (Bit 6 enable)
	{0x44, 0x0316, 0x22}, // Route 12bit RAW (DT = 0x2C) to VIDEO_Y (Bit 6 enable)
	{0x44, 0x0318, 0x6C}, // Route EMBEDDED8 to VIDEO_Z (Bit 6 enable)
	{0x44, 0x031A, 0x22}, // Unused VIDEO_U
	{0x44, 0x0002, 0xC3}, // Make sure all pipelines start transmission (VID_TX_EN_X/Y/Z/U = 1)
	{0x44, 0x02BE, 0x10}, // Drive output high (sensor enabled)

	// Sh4tter trigger gpios: x10 address and synchronicity
	{0x44, 0x02D6, 0x04}, // MFP8 into gpio and into reception
	{0x44, 0x02D8, 0x10}, // MFP8 rx address is x10
						  /// For4MAX9296 only, modify MFP src for FSIN
	
	{0x44, 0x0010, 0x21}, // Apply Reset Oneshot for changes

	{0x00, SERIALIZER_TABLE_END, 0x00}
};

static struct index_reg_8 isx031_9295D_Ser_A[] = {
	{MAX9295D_ADDRESS_BASE, 0x0010, 0x21}, //92 reset path but not registers and auto conf
	{MAX9295D_ADDRESS_BASE, 0x0330, 0x06}, //
	{MAX9295D_ADDRESS_BASE, 0x0331, 0x33}, //
	{MAX9295D_ADDRESS_BASE, 0x0332, 0x4E}, //
	{MAX9295D_ADDRESS_BASE, 0x0333, 0xE4}, //

	{MAX9295D_ADDRESS_BASE, 0x0311, 0x21}, //92 start port a pipe x and port b pipe z
	{MAX9295D_ADDRESS_BASE, 0x0308, 0x7E}, //92 start mipi a and b , a get pipe x and b get pipe z
	{MAX9295D_ADDRESS_BASE, 0x0314, 0x5E}, //92 datatype pipe x = 10 bits raw
	{MAX9295D_ADDRESS_BASE, 0x0316, 0x5E}, //92 datatype pipe x = 10 bits raw
	{MAX9295D_ADDRESS_BASE, 0x0318, 0x5E}, //92 datatype pipe z = 10 bits raw
	{MAX9295D_ADDRESS_BASE, 0x031a, 0x5E}, //92 datatype pipe z = 10 bits raw
	{MAX9295D_ADDRESS_BASE, 0x0002, 0xff}, //92 vid enable for all ports

	//Shutter trigger gpios: 9 and 10 with 0x10 address and synchronicity
	{MAX9295D_ADDRESS_BASE, 0x02D9, 0x04}, // MFP9 into gpio and into reception
	{MAX9295D_ADDRESS_BASE, 0x02DB, 0x10}, // MFP9 rx address is x10
	{MAX9295D_ADDRESS_BASE, 0x02Dc, 0x04}, // MFP10 into gpio and into reception
	{MAX9295D_ADDRESS_BASE, 0x02De, 0x10}, // MFP10 rx address is x10
	
	// cut camera 0
//	{MAX9295D_ADDRESS_BASE, 0x02d3, 0x80}, // MFP7 -> enable reset pin, powerup.
//	{MAX9295D_ADDRESS_BASE, 0x02d4, 0x60}, // MFP7 -> rx address is 0
//
//	// cut camera 1
//	{MAX9295D_ADDRESS_BASE, 0x02d6, 0x80}, // MFP8 -> enable reset pin, powerup.
//	{MAX9295D_ADDRESS_BASE, 0x02d7, 0x60}, // MFP8 -> rx address is 0

	//Power gpio of the isx031
//	{0x62, 0x02BE, 0x90}, // MFP0 into a driven gpo at value 1
//	{0x62, 0x02BF, 0x60}, // MFP0 resistance is pullup in pushpull

	{MAX9295D_ADDRESS_BASE, 0x02BE, 0x80}, // MFP0 to low to have IMU adress @0x19/69 (Note : 0x90 changes adress to secondary)

	{0x62, 0x0010, 0x21}, // Reset path to update the pipeline

	{0x00, SERIALIZER_TABLE_END, 0x00}
};

static struct index_reg_8 isx031_9295D_Ser_B[] = {
	{MAX9295D_ADDRESS_BASE, 0x0010, 0x21}, //92 reset path but not registers and auto conf
	{MAX9295D_ADDRESS_BASE, 0x0330, 0x06}, //
	{MAX9295D_ADDRESS_BASE, 0x0331, 0x33}, //
	{MAX9295D_ADDRESS_BASE, 0x0332, 0x4E}, //
	{MAX9295D_ADDRESS_BASE, 0x0333, 0xE4}, //

	{MAX9295D_ADDRESS_BASE, 0x0311, 0x84}, //92 start port a pipe z and port b pipe u
	{MAX9295D_ADDRESS_BASE, 0x0308, 0x7B}, //92 start mipi a and b , a get pipe z and b get pipe u
	{MAX9295D_ADDRESS_BASE, 0x0314, 0x5E}, //92 datatype pipe z = 10 bits raw
	{MAX9295D_ADDRESS_BASE, 0x0316, 0x5E}, //92 datatype pipe z = 10 bits raw
	{MAX9295D_ADDRESS_BASE, 0x0318, 0x5E}, //92 datatype pipe u = 10 bits raw
	{MAX9295D_ADDRESS_BASE, 0x031a, 0x5E}, //92 datatype pipe u = 10 bits raw
	{MAX9295D_ADDRESS_BASE, 0x0002, 0xff}, //92 vid enable for all ports

	//Shutter trigger gpios: 9 and 10 with 0x10 address and synchronicity
	{MAX9295D_ADDRESS_BASE, 0x02D9, 0x04}, // MFP9 into gpio and into reception
	{MAX9295D_ADDRESS_BASE, 0x02DB, 0x10}, // MFP9 rx address is x10
	{MAX9295D_ADDRESS_BASE, 0x02Dc, 0x04}, // MFP10 into gpio and into reception
	{MAX9295D_ADDRESS_BASE, 0x02De, 0x10}, // MFP10 rx address is x10
	
	{MAX9295D_ADDRESS_BASE, 0x02BE, 0x80}, // MFP0 to low to have IMU adress @0x19/69 (Note : 0x90 changes adress to secondary)
	// cut camera 0
//	{MAX9295D_ADDRESS_BASE, 0x02d3, 0x80}, // MFP7 -> enable reset pin, powerup.
//	{MAX9295D_ADDRESS_BASE, 0x02d4, 0x60}, // MFP7 -> rx address is 0
//
//	// cut camera 1
//	{MAX9295D_ADDRESS_BASE, 0x02d6, 0x80}, // MFP8 -> enable reset pin, powerup.
//	{MAX9295D_ADDRESS_BASE, 0x02d7, 0x60}, // MFP8 -> rx address is 0

	//Power gpio of the isx031
//	{0x62, 0x02BE, 0x90}, // MFP0 into a driven gpo at value 1
//	{0x62, 0x02BF, 0x60}, // MFP0 resistance is pullup in pushpull

	{0x62, 0x0010, 0x21}, // Reset path to update the pipeline

	{0x00, SERIALIZER_TABLE_END, 0x00}
};

static struct index_reg_8 isx031_9295A_Ser_A[] = {
	//{0x42, 0x0010, 0x91}, // Reset path to update the pipeline
	{0x42, 0x0330, 0x00}, //
	{0x42, 0x0331, 0x30}, //
	{0x42, 0x0332, 0xE0}, //
	{0x42, 0x0333, 0x04}, //

	{0x42, 0x0311, 0x10}, // Start video pipe X from port B
	{0x42, 0x0308, 0x61}, // Enable line-start informations + enable csi port B + pipe X to port B
	{0x42, 0x0314, 0x5E}, // Datatype pipe x = yuv422 8 bits 
	{0x42, 0x0316, 0x5E}, // Datatype pipe x = yuv422 8 bits 
	{0x42, 0x0318, 0x5E}, // Datatype pipe x = yuv422 8 bits 
	{0x42, 0x031a, 0x5E}, // Datatype pipe x = yuv422 8 bits 
	{0x42, 0x0002, 0xf3}, // All video transmit channel enabled

	//Shutter trigger gpios: x10 address and synchronicity
	{0x42, 0x02D6, 0x04}, // MFP8 into gpio and into reception
	{0x42, 0x02D8, 0x10}, // MFP8 rx address is x10
	
	{0x42, 0x0010, 0x21}, // Reset path to update the pipeline
	{0x00, SERIALIZER_TABLE_END, 0x00}
};

static struct index_reg_8 isx031_9295A_Ser_B[] = {
	//{0x42, 0x0010, 0x91}, // Reset path to update the pipeline
	{0x42, 0x0330, 0x00}, //
	{0x42, 0x0331, 0x30}, //
	{0x42, 0x0332, 0xE0}, //
	{0x42, 0x0333, 0x04}, //

	{0x42, 0x0311, 0x40}, // Start video pipe X from port B
	{0x42, 0x0308, 0x64}, // Enable line-start informations + enable csi port B + pipe X to port B
	{0x42, 0x0314, 0x5E}, // Datatype pipe x = yuv422 8 bits 
	{0x42, 0x0316, 0x5E}, // Datatype pipe x = yuv422 8 bits 
	{0x42, 0x0318, 0x5E}, // Datatype pipe x = yuv422 8 bits 
	{0x42, 0x031a, 0x5E}, // Datatype pipe x = yuv422 8 bits 
	{0x42, 0x0002, 0xf3}, // All video transmit channel enabled

	//Shutter trigger gpios: x10 address and synchronicity
	{0x42, 0x02D6, 0x04}, // MFP8 into gpio and into reception
	{0x42, 0x02D8, 0x10}, // MFP8 rx address is x10
	
	{0x42, 0x0010, 0x21}, // Reset path to update the pipeline

	{0x00, SERIALIZER_TABLE_END, 0x00}
};

enum
{
	AR0234_9295D_SER,
	AR0234_9295A_SER,
	IMX678_9295A_SER,
	ISX031_9295A_SER,
	ISX031_9295D_SER,
};

static struct index_reg_8 *mode_table_A[] = {
	[AR0234_9295D_SER] = ar0234_9295D_Ser_A,
	[AR0234_9295A_SER] = ar0234_9295A_Ser_A,
	[IMX678_9295A_SER] = imx678_9295A_Ser_A,
	[ISX031_9295A_SER] = isx031_9295A_Ser_A,
	[ISX031_9295D_SER] = isx031_9295D_Ser_A,
};

static struct index_reg_8 *mode_table_B[] = {
	[AR0234_9295D_SER] = ar0234_9295D_Ser_B,
	[AR0234_9295A_SER] = ar0234_9295A_Ser_B,
	[IMX678_9295A_SER] = imx678_9295A_Ser_B,
	[ISX031_9295A_SER] = isx031_9295A_Ser_B,
	[ISX031_9295D_SER] = isx031_9295D_Ser_B,
};

typedef enum
{
	ZEDX = 0,
	ZEDONEGS,
	ZEDONE4K,
	ZEDONEHDR,
	ZEDXHDR,
	/* Don't add a camera type bellow N_CAM_TYPE, it will not be parsed*/
	N_CAM_TYPE,
}CamType;

#endif /* __ZEDX_I2C_TABLES__ */
