// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * NVDISP SERDES driver for Display Serializers and DeSerializers
 *
 * Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
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
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/idr.h>

#ifdef CONFIG_TEGRA_EPL
#include <linux/tegra-epl.h>
#endif

#include "nvdisp_serdes_common.h"

/**
 * @brief EPL reporter ID for NVDISP SERDES.
 * This ID is used when reporting errors to FSI via EPL.
 */
#define NVDISP_SERDES_EPL_REPORTER_ID 0x8103
#define MAX_SERDES_INSTANCES 8

/**
 * @brief Structure for IOCTL data exchange between userspace and driver.
 * @seq_id: Input parameter specifying which sequence ID to execute
 */
struct nvdisp_serdes_ioctl_data {
	unsigned int seq_id; /* Input: Sequence ID to execute */
};

/**
 * @brief Global variables for character device management
 * @nvdisp_serdes_devt_base: Base device number for all serdes instances
 * @nvdisp_serdes_class: Device class for serdes character devices
 * @nvdisp_serdes_ida: ID allocator for managing minor numbers
 */
static dev_t nvdisp_serdes_devt_base;
static struct class *nvdisp_serdes_class;
static DEFINE_IDA(nvdisp_serdes_ida);

/**
 * @brief I2C opcode data structure sizes.
 * DO NOT MODIFY!!
 */
#define SIZE_PAYLOAD_I2C_WRITE_8_SINGLE                     (6U)
#define SIZE_PAYLOAD_I2C_WRITE_BLOCK                        (6U)
#define SIZE_PAYLOAD_I2C_READ_BLOCK_PRINT                   (6U)
#define SIZE_PAYLOAD_I2C_READ_8_SINGLE                      (4U)
#define SIZE_PAYLOAD_I2C_READ_8_SINGLE_PRINT                (4U)
#define SIZE_PAYLOAD_I2C_READ_8_SINGLE_WITH_POLLING         (14U)
#define SIZE_PAYLOAD_I2C_READ_8_SINGLE_WITH_GOLDEN_VALUE    (8U)
#define SIZE_PAYLOAD_I2C_READ_COMPARE_PRINT_CLEAR           (10U)
#define SIZE_PAYLOAD_I2C_UPDATE_SINGLE_BYTE                 (8U)

/**
 * @brief OS opcode data structure sizes.
 * DO NOT MODIFY!!
 */
#define SIZE_PAYLOAD_OS_DELAY_USEC                          (4U)

#define OPCODE_TERM                                         0xFF

/**
 * @brief Add speculative execution barrier to prevent Spectre-class attacks
 *
 * This macro prevents data leakage attacks during speculative execution
 * by adding memory barriers and instruction synchronization barriers.
 * Using macro ensures the barriers are inlined at the call site.
 */
#define SPECULATIVE_EXECUTION_BARRIER() do { \
	/* Data synchronization barrier - ensure all memory accesses complete */ \
	__asm__ volatile("dsb sy" : : : "memory"); \
	/* Instruction synchronization barrier - ensure all instructions complete */ \
	__asm__ volatile("isb" : : : "memory"); \
} while (false)

typedef int32_t (*opcode_dispatcher_t)(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload);

/**
 * @brief Opcode description.
 *
 */
typedef struct {
	/*
	 * A human-readable description of the opcode.
	 */
	char *const description;
	/*
	 * The opcodes unique identifier.
	 */
	uint8_t opcode;
	/*
	 * The size of the opcodes header.
	 */
	uint32_t payload_size;
	/*
	 * Dispatcher for the opcode.
	*/
	opcode_dispatcher_t dispatcher;
	/*
	 * True if opcode has variable-sized payload after header
	 */
	bool has_variable_payload;
} opcode_descriptor_t;

static int32_t op_i2c_write_byte(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload);
static int32_t op_i2c_write_block(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload);
static int32_t op_i2c_read_after_write_byte(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload);
static int32_t op_i2c_read_byte_and_poll(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload);
static int32_t op_i2c_read_byte_compare_print(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload);
static int32_t op_i2c_update_byte(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload);
static int32_t op_i2c_read_byte_and_print(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload);
static int32_t op_i2c_read_block_and_print(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload);
static int32_t op_i2c_read_byte(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload);
static int32_t op_delay_usec(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload);
static int32_t op_pwr_ctrl_on(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload);
static int32_t op_pwr_ctrl_off(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload);

opcode_descriptor_t opcode_desc_table[] = {
	/* Below OPCODEs are existing in CBA and used as it is in SerDes */
	{
		.description = "i2c write single byte",
		.opcode = 0x10,
		.payload_size = SIZE_PAYLOAD_I2C_WRITE_8_SINGLE,
		.dispatcher = op_i2c_write_byte,
		.has_variable_payload = false,
	},
	{
		.description = "i2c write block",
		.opcode = 0x28,
		.payload_size = SIZE_PAYLOAD_I2C_WRITE_BLOCK,
		.dispatcher = op_i2c_write_block,
		.has_variable_payload = true,
	},
	{
		.description = "i2c read after write single byte",
		.opcode = 0x11,
		.payload_size = SIZE_PAYLOAD_I2C_WRITE_8_SINGLE,
		.dispatcher = op_i2c_read_after_write_byte,
		.has_variable_payload = false,
	},
	{
		.description = "i2c read single byte",
		.opcode = 0x30,
		.payload_size = SIZE_PAYLOAD_I2C_READ_8_SINGLE,
		.dispatcher = op_i2c_read_byte,
		.has_variable_payload = false,
	},
	{
		.description = "i2c read single byte and poll for value",
		.opcode = 0x90,
		.payload_size = SIZE_PAYLOAD_I2C_READ_8_SINGLE_WITH_POLLING,
		.dispatcher = op_i2c_read_byte_and_poll,
		.has_variable_payload = false,
	},
	{
		.description = "delay in usec",
		.opcode = 0x50,
		.payload_size = SIZE_PAYLOAD_OS_DELAY_USEC,
		.dispatcher = op_delay_usec,
		.has_variable_payload = false,
	},
	{
		.description = "power control on",
		.opcode = 0x60,
		.payload_size = 0,
		.dispatcher = op_pwr_ctrl_on,
		.has_variable_payload = false,
	},
	{
		.description = "power control off",
		.opcode = 0x61,
		.payload_size = 0,
		.dispatcher = op_pwr_ctrl_off,
		.has_variable_payload = false,
	},
	{
		.description = "i2c read single byte and match masked value with golden value",
		.opcode = 0x80,
		.payload_size = SIZE_PAYLOAD_I2C_READ_8_SINGLE_WITH_GOLDEN_VALUE,
		.dispatcher = op_i2c_read_byte_compare_print,
		.has_variable_payload = false,
	},
	/* Below OPCODEs are new in SerDes */
	/* errb specific opcodes */
	{
		.description = "i2c update single byte with mask",
		.opcode = 0xA2,
		.payload_size = SIZE_PAYLOAD_I2C_UPDATE_SINGLE_BYTE,
		.dispatcher = op_i2c_update_byte,
		.has_variable_payload = false,
	},
	/* debug opcodes */
	{
		.description = "i2c read single byte and print",
		.opcode = 0xF0,
		.payload_size = SIZE_PAYLOAD_I2C_READ_8_SINGLE_PRINT,
		.dispatcher = op_i2c_read_byte_and_print,
		.has_variable_payload = false,
	},
	{
		.description = "i2c read block and print",
		.opcode = 0xF1,
		.payload_size = SIZE_PAYLOAD_I2C_READ_BLOCK_PRINT,
		.dispatcher = op_i2c_read_block_and_print,
		.has_variable_payload = false,
	},
};

#define NUM_OPCODES (sizeof(opcode_desc_table) / sizeof(opcode_desc_table[0]))

int32_t nvdisp_serdes_read(struct nvdisp_serdes_priv *priv, u8 slave_addr, int reg, u8 *reg_val)
{
	int32_t ret, val = 0;
	struct i2c_client *i2c_client = priv->client;

	i2c_client->addr = slave_addr;
	ret = regmap_read(priv->regmap, reg, &val);
	if (ret < 0)
		dev_err(&priv->client->dev,
			"%s: register 0x%02x read failed (%d)\n",
			__func__, reg, ret);

	*reg_val = (u8)val;
	return ret;
}

int32_t nvdisp_serdes_write(struct nvdisp_serdes_priv *priv, u8 slave_addr, u32 reg, u8 val)
{
	int32_t ret;
	struct i2c_client *i2c_client = priv->client;

	i2c_client->addr = slave_addr;
	ret = regmap_write(priv->regmap, reg, val);
	if (ret < 0)
		dev_err(&priv->client->dev,
			"%s: register 0x%02x write failed (%d)\n",
			__func__, reg, ret);

	return ret;
}

int32_t nvdisp_serdes_update(struct nvdisp_serdes_priv *priv, u8 slave_addr, u32 reg, u32 mask, u8 val)
{
	u8 update_val;
	int32_t ret;

	ret = nvdisp_serdes_read(priv, slave_addr, reg, &update_val);
	if (ret < 0) {
		dev_err(&priv->client->dev,
			"%s: nvdisp_serdes_read 0x%02x failed (%d)\n",
			__func__, reg, ret);
		return ret;
	}

	update_val = ((update_val & (~mask)) | (val & mask));
	return nvdisp_serdes_write(priv, slave_addr, reg, update_val);
}

#define OPCODE_OFFSET_START                        0
#define OPCODE_OFFSET_I2C_SLAVE_ADDR_LOW           1
#define OPCODE_OFFSET_I2C_SLAVE_ADDR_HIGH          2
#define OPCODE_OFFSET_I2C_REG_ADDR_LOW             3
#define OPCODE_OFFSET_I2C_REG_ADDR_HIGH            4
#define OPCODE_OFFSET_I2C_REG_DATA                 5

static int32_t op_i2c_write_byte(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload)
{
	u8 slave_addr = payload[OPCODE_OFFSET_I2C_SLAVE_ADDR_LOW];
	u32 reg_addr = (payload[OPCODE_OFFSET_I2C_REG_ADDR_LOW]) | (payload[OPCODE_OFFSET_I2C_REG_ADDR_HIGH] << 0x8);

	return nvdisp_serdes_write(priv, slave_addr, reg_addr, payload[OPCODE_OFFSET_I2C_REG_DATA]);
}

#define OPCODE_OFFSET_I2C_BLOCK_LEN_LOW            5  /* Length low byte */
#define OPCODE_OFFSET_I2C_BLOCK_LEN_HIGH           6  /* Length high byte */

/**
 * @brief Write a block of data to an I2C device.
 *
 * This API does the following:
 * - Extract the write block opcode header from the sequence
 * - Write data block sequentially to the I2C device
 *
 * @param[in]      client       Pointer to I2C client
 * @param[in]      priv        Pointer to driver private data
 * @param[in]      payload     Pointer to opcode payload
 *
 * @retval 0       Successfully wrote the block of data
 * @retval -EINVAL Invalid parameters
 * @retval -EIO    I2C write operation failed
 */
static int32_t op_i2c_write_block(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload)
{
	u8 slave_addr = payload[OPCODE_OFFSET_I2C_SLAVE_ADDR_LOW];
	u32 reg_addr = (payload[OPCODE_OFFSET_I2C_REG_ADDR_LOW]) | (payload[OPCODE_OFFSET_I2C_REG_ADDR_HIGH] << 0x8);
	u16 data_len = (payload[OPCODE_OFFSET_I2C_BLOCK_LEN_LOW]) | (payload[OPCODE_OFFSET_I2C_BLOCK_LEN_HIGH] << 0x8);
	/* Data starts after the header */
	u8 *data = &payload[SIZE_PAYLOAD_I2C_WRITE_BLOCK + 1];  /* +1 to skip opcode */
	int32_t ret;
	int i;

	for (i = 0; i < data_len; i++) {
		ret = nvdisp_serdes_write(priv, slave_addr, reg_addr + i, data[i]);
		if (ret < 0) {
			dev_err(&client->dev, "%s: Write failed at offset %d (reg 0x%04x) with error %d\n",
				__func__, i, reg_addr + i, ret);
			return ret;
		}
	}

	return 0;
}

static int32_t op_i2c_read_after_write_byte(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload)
{
	u8 slave_addr = payload[OPCODE_OFFSET_I2C_SLAVE_ADDR_LOW];
	u32 reg_addr = (payload[OPCODE_OFFSET_I2C_REG_ADDR_LOW]) | (payload[OPCODE_OFFSET_I2C_REG_ADDR_HIGH] << 0x8);
	u8 reg_val;
	int32_t ret;

	ret = nvdisp_serdes_write(priv, slave_addr, reg_addr, payload[OPCODE_OFFSET_I2C_REG_DATA]);
	if (ret < 0) {
		dev_err(&priv->client->dev,
			"%s: nvdisp_serdes_write 0x%02x failed (%d)\n",
			__func__, reg_addr, ret);
		return ret;
	}

	/* Read back the value to verify write operation */
	ret = nvdisp_serdes_read(priv, slave_addr, reg_addr, &reg_val);
	if (ret < 0) {
		dev_err(&priv->client->dev,
			"%s: nvdisp_serdes_read 0x%02x failed (%d)\n",
			__func__, reg_addr, ret);
		return ret;
	}

	if (reg_val != payload[OPCODE_OFFSET_I2C_REG_DATA]) {
		dev_err(&priv->client->dev,
			"%s: readback verification failed. Expected 0x%02x, got 0x%02x\n",
			__func__, payload[OPCODE_OFFSET_I2C_REG_DATA], reg_val);
	}

	return 0;
}

#define OPCODE_90_OFFSET_POLL_COUNT               5
#define OPCODE_90_OFFSET_PADDING                  6
#define OPCODE_90_OFFSET_POLL_DELAY_0             7
#define OPCODE_90_OFFSET_POLL_DELAY_1             8
#define OPCODE_90_OFFSET_POLL_DELAY_2             9
#define OPCODE_90_OFFSET_POLL_DELAY_3             10
#define OPCODE_90_OFFSET_POLL_MASK_LOW            11
#define OPCODE_90_OFFSET_POLL_MASK_HIGH           12
#define OPCODE_90_OFFSET_POLL_VAL_LOW             13
#define OPCODE_90_OFFSET_POLL_VAL_HIGH            14

static int32_t op_i2c_read_byte_and_poll(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload)
{
	struct device *dev = &priv->client->dev;
	u16 reg_addr = (payload[OPCODE_OFFSET_I2C_REG_ADDR_LOW]) | (payload[OPCODE_OFFSET_I2C_REG_ADDR_HIGH] << 0x8);
	u8 slave_addr = payload[OPCODE_OFFSET_I2C_SLAVE_ADDR_LOW];
	u8 mask = payload[OPCODE_90_OFFSET_POLL_MASK_LOW];
	u8 expected_val = payload[OPCODE_90_OFFSET_POLL_VAL_LOW];
	u32 usec_delay = (payload[OPCODE_90_OFFSET_POLL_DELAY_3] << 24) | (payload[OPCODE_90_OFFSET_POLL_DELAY_2] << 16) | (payload[OPCODE_90_OFFSET_POLL_DELAY_1] << 8) | payload[OPCODE_90_OFFSET_POLL_DELAY_0];
	u8 poll_count = payload[OPCODE_90_OFFSET_POLL_COUNT];
	u8 iteration = 0;
	u8 reg_val;
	int32_t ret = 0;

	ret = nvdisp_serdes_read(priv, slave_addr, reg_addr, &reg_val);
	if (ret < 0) {
		dev_err(&priv->client->dev,
			"%s: nvdisp_serdes_read 0x%02x failed (%d)\n",
			__func__, reg_addr, ret);
		return ret;
	}

	for (iteration = 0; iteration < poll_count; iteration++) {
		if ((reg_val & mask) == expected_val) {
			/* expected value match */
			return 0;
		}
		/* expected value did not match, retry */
		udelay(usec_delay);
		ret = nvdisp_serdes_read(priv, slave_addr, reg_addr, &reg_val);
		if (ret < 0) {
			dev_err(&priv->client->dev,
				"%s: nvdisp_serdes_read 0x%02x failed (%d)\n",
				__func__, reg_addr, ret);
			return ret;
		}
	}

	dev_err(dev, "%s: expected value did not match\n", __func__);
	dev_err(dev, "%s: reg_addr = 0x%x, reg_val = 0x%x\n", __func__, reg_addr, reg_val);

	return ret;
}

#define OPCODE_80_OFFSET_MASK                        5
#define OPCODE_80_OFFSET_MASK_PAD                    6
#define OPCODE_80_OFFSET_VAL                         7
#define OPCODE_80_OFFSET_VAL_PAD                     8

static int32_t op_i2c_read_byte_compare_print(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload)
{
	struct device *dev = &priv->client->dev;
	int32_t ret;
	u8 reg_val;
	u8 slave_addr = payload[OPCODE_OFFSET_I2C_SLAVE_ADDR_LOW];
	u16 reg_addr = (payload[OPCODE_OFFSET_I2C_REG_ADDR_LOW]) | (payload[OPCODE_OFFSET_I2C_REG_ADDR_HIGH] << 0x8);
	u8 mask = payload[OPCODE_80_OFFSET_MASK];
	u8 expected_val = payload[OPCODE_80_OFFSET_VAL];

	ret = nvdisp_serdes_read(priv, slave_addr, reg_addr, &reg_val);
	if (ret < 0) {
		dev_err(&priv->client->dev,
			"%s: nvdisp_serdes_read 0x%02x failed (%d)\n",
			__func__, reg_addr, ret);
		return ret;
	}

	if ((reg_val & mask) == expected_val) {
		dev_info(dev, "%s: expected value 0x%x is set at mask 0x%x at register addr 0x%x with value 0x%x\n",
		__func__, expected_val, mask, reg_addr, reg_val);
	}

	return ret;
}

#define OPCODE_A2_OFFSET_MASK                        5
#define OPCODE_A2_OFFSET_MASK_PAD                    6
#define OPCODE_A2_OFFSET_VAL                         7
#define OPCODE_A2_OFFSET_VAL_PAD                     8

static int32_t op_i2c_update_byte(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload)
{
	u8 slave_addr = payload[OPCODE_OFFSET_I2C_SLAVE_ADDR_LOW];
	u16 addr = (payload[OPCODE_OFFSET_I2C_REG_ADDR_LOW]) | (payload[OPCODE_OFFSET_I2C_REG_ADDR_HIGH] << 0x8);
	u8 mask = payload[OPCODE_A2_OFFSET_MASK];
	u8 val = payload[OPCODE_A2_OFFSET_VAL];

	return nvdisp_serdes_update(priv, slave_addr, (u32)addr, (u32)mask, val);
}

static int32_t op_i2c_read_byte_and_print(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload)
{
	struct device *dev = &priv->client->dev;
	u8 slave_addr = payload[OPCODE_OFFSET_I2C_SLAVE_ADDR_LOW];
	u16 reg_addr = (payload[OPCODE_OFFSET_I2C_REG_ADDR_LOW]) | (payload[OPCODE_OFFSET_I2C_REG_ADDR_HIGH] << 0x8);
	u8 reg_val;
	int32_t ret = 0;

	ret = nvdisp_serdes_read(priv, slave_addr, reg_addr, &reg_val);
	if (ret < 0) {
		dev_err(&priv->client->dev,
			"%s: nvdisp_serdes_read 0x%02x failed (%d)\n",
			__func__, reg_addr, ret);
		return ret;
	}

	dev_info(dev, "%s: - reg_addr = 0x%x,  reg_val = 0x%x\n", __func__, reg_addr, reg_val);
	return ret;
}

/**
 * @brief Read and print a block of data from an I2C device.
 *
 * This API does the following:
 * - Extract the read block opcode header from the sequence
 * - Read data block sequentially from the I2C device
 * - Print each read value with its register address
 *
 * @param[in]      client       Pointer to I2C client
 * @param[in]      priv        Pointer to driver private data
 * @param[in]      payload     Pointer to opcode payload
 *
 * @retval 0       Successfully read and printed the block of data
 * @retval -EINVAL Invalid parameters
 * @retval -EIO    I2C read operation failed
 */
static int32_t op_i2c_read_block_and_print(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload)
{
	struct device *dev = &priv->client->dev;
	u8 slave_addr = payload[OPCODE_OFFSET_I2C_SLAVE_ADDR_LOW];
	u32 reg_addr = (payload[OPCODE_OFFSET_I2C_REG_ADDR_LOW]) | (payload[OPCODE_OFFSET_I2C_REG_ADDR_HIGH] << 0x8);
	u16 data_len = (payload[OPCODE_OFFSET_I2C_BLOCK_LEN_LOW]) | (payload[OPCODE_OFFSET_I2C_BLOCK_LEN_HIGH] << 0x8);
	u8 reg_val;
	int32_t ret;
	int i;

	/* Print header */
	dev_info(dev, "%s: Reading %d bytes starting at reg_addr = 0x%x:\n", __func__, data_len, reg_addr);
	dev_info(dev, "%s: slave_addr = 0x%x\n", __func__, slave_addr);

	/* Read and print data block sequentially */
	for (i = 0; i < data_len; i++) {
		dev_info(dev, "%s: Reading from [0x%04x]\n", __func__, reg_addr + i);
		ret = nvdisp_serdes_read(priv, slave_addr, reg_addr + i, &reg_val);
		if (ret < 0) {
			dev_err(dev, "%s: Read failed at offset %d (reg 0x%04x) with error %d\n",
				__func__, i, reg_addr + i, ret);
			return ret;
		}
		dev_info(dev, "%s:   [0x%04x] = 0x%02x\n", __func__, reg_addr + i, reg_val);
	}

	return 0;
}

static int32_t op_i2c_read_byte(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload)
{
	u8 slave_addr = payload[OPCODE_OFFSET_I2C_SLAVE_ADDR_LOW];
	u16 reg_addr = (payload[OPCODE_OFFSET_I2C_REG_ADDR_LOW]) | (payload[OPCODE_OFFSET_I2C_REG_ADDR_HIGH] << 0x8);
	u8 reg_val;
	int32_t ret = 0;

	ret = nvdisp_serdes_read(priv, slave_addr, reg_addr, &reg_val);
	if (ret < 0) {
		dev_err(&priv->client->dev,
			"%s: nvdisp_serdes_read 0x%02x failed (%d)\n",
			__func__, reg_addr, ret);
		return ret;
	}

	return ret;
}

#define OPCODE_50_OFFSET_DELAY_1                        1
#define OPCODE_50_OFFSET_DELAY_2                        2
#define OPCODE_50_OFFSET_DELAY_3                        3
#define OPCODE_50_OFFSET_DELAY_4                        4

static int32_t op_delay_usec(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload)
{
	unsigned long min, max;

	min = (payload[OPCODE_50_OFFSET_DELAY_1] << 24) | (payload[OPCODE_50_OFFSET_DELAY_2] << 16) | (payload[OPCODE_50_OFFSET_DELAY_3] << 8) | payload[OPCODE_50_OFFSET_DELAY_4];
	/* max is calculated from min with fixed offset of 200 usec. TODO: need new opcode to define MIN value if required */
	max = min + 200;

	usleep_range(min, max);

	return 0;
}

static opcode_descriptor_t *find_opcode(const u8 query_opcode)
{
	opcode_descriptor_t *op = NULL;
	int32_t i = 0;

	for (i = 0; i < NUM_OPCODES; i++) {
		if (query_opcode == opcode_desc_table[i].opcode) {
			op = &opcode_desc_table[i];
			break;
		}
	}

	return op;
}

int32_t dispatch_opcode(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload, int *curr_pos, int32_t length)
{
	const opcode_descriptor_t *opcode_desc = NULL;
	struct device *dev = &priv->client->dev;
	u8 opcode;
	int32_t ret, curr_pos_payload_size;

	if (length <= (*curr_pos)) {
		dev_err(dev, "%s: curr_pos(%d) is greater than the sequence length(%d)\n",
			__func__, *curr_pos, length);
		return -1;
	}

	opcode = payload[*curr_pos];
	opcode_desc = find_opcode(opcode);
	if (opcode_desc == NULL) {
		dev_err(dev, "%s: Unknown OPCODE - 0x%x, Add implementation as per OPCODE specification\n",
			__func__, opcode);
		return -1;
	}

	curr_pos_payload_size = *curr_pos + opcode_desc->payload_size + 1;

	/* For variable payload opcodes, just need to check max length */
	if (opcode_desc->has_variable_payload) {
		u16 data_len = (payload[*curr_pos + OPCODE_OFFSET_I2C_BLOCK_LEN_LOW]) |
			       (payload[*curr_pos + OPCODE_OFFSET_I2C_BLOCK_LEN_HIGH] << 0x8);

		/* Sanity check data length */
		if (data_len > NVDISP_SERDES_MAX_READ_WRITE) {
			dev_err(dev, "%s: Invalid data length %d (max %d) at pos %d\n",
				__func__, data_len, NVDISP_SERDES_MAX_READ_WRITE, *curr_pos);
			return -1;
		}
		curr_pos_payload_size += data_len;
	}

	if (length <= curr_pos_payload_size) {
		dev_err(dev, "%s: curr_pos_payload_size(%d) is greater than the sequence length(%d)\n",
			__func__, curr_pos_payload_size, length);
		return -1;
	}

	if (opcode_desc->dispatcher != NULL) {
		ret = opcode_desc->dispatcher(client, priv, &payload[*curr_pos]);
		if (ret < 0) {
			dev_err(dev, "%s: dispatcher return error (%d) for OPCODE 0x%x\n",
			__func__, ret, opcode);
			return -1;
		}
	} else {
		dev_info(dev, "%s: No dispatcher defined for OPCODE 0x%x, continue to next OPCODE\n",
			__func__, opcode);
	}

	/* increase curr_pos to terminator */
	*curr_pos = curr_pos_payload_size;

	if (payload[*curr_pos] != OPCODE_TERM) {
		dev_err(dev, "%s: Invalid sequence, OPCODE terminator not found = 0x%x\n", __func__, payload[*curr_pos]);
		return -1;
	}

	/* increase curr_pos to next opcode */
	*curr_pos = *curr_pos + 1;

	return 0;
}

int read_opcode_props_sequence(struct i2c_client *client,
									struct nvdisp_serdes_priv *priv,
									struct device_node *display_serdes_config,
									struct opcode_sequence *seq)
{
	struct device *dev = &priv->client->dev;

	seq->length = of_property_count_elems_of_size(display_serdes_config, seq->name, sizeof(u8));

	if (seq->length <= 0) {
		dev_info(dev, "%s: No elements in this %s sequence\n", __func__, seq->name);
		seq->length = 0;
		return -1;
	}

	seq->payload = devm_kzalloc(&client->dev, seq->length * sizeof(u8), GFP_KERNEL);
	if (seq->payload == NULL) {
		dev_err(dev, "%s: devm_kzalloc failed for %s\n", __func__, seq->name);
		return -ENOMEM;
	}

	return of_property_read_variable_u8_array(display_serdes_config, seq->name, seq->payload, 1, seq->length);
}


static int read_opcode_props(struct i2c_client *client,
					   struct nvdisp_serdes_priv *priv)
{
	struct device *dev = &priv->client->dev;
	struct device_node *ser = dev->of_node;
	const char *serdes_desc;
	const char *vendor_type = NULL;
	int err = 0;
	struct device_node *display_serdes_config;

	/* Read serdes-vendor-type property */
	err = of_property_read_string(ser, "serdes-vendor-type", &vendor_type);
	if (err < 0) {
		dev_info(dev, "serdes-vendor-type not specified\n");
		priv->vendor_type = NULL;
	} else {
		dev_info(dev, "SerDes vendor type: %s\n", vendor_type);
		priv->vendor_type = devm_kstrdup(dev, vendor_type, GFP_KERNEL);
		if (!priv->vendor_type)
			return -ENOMEM;
	}

	display_serdes_config = of_find_node_by_name(ser, "display-serdes-config");
	if (display_serdes_config == NULL) {
		dev_err(dev, "%s: can not get display_serdes_config node\n", __func__);
		return -1;
	}

	err = of_property_read_string(display_serdes_config, "description", &serdes_desc);
	if (err) {
		dev_info(dev, "%s: display-serdes-config description is absent = err = %d\n", __func__, err);
	} else {
		dev_info(dev, "%s: display-serdes-config description = %s\n", __func__, serdes_desc);
	}

	priv->init_seq.name = "init-seq";
	err = read_opcode_props_sequence(client, priv, display_serdes_config, &priv->init_seq);
	if (err < 0) {
		dev_err(dev, "%s: read_opcode_props_sequence failed (err=%d) for %s\n", __func__, err, priv->init_seq.name);
		return -1;
	}

	priv->deinit_seq.name = "deinit-seq";
	err = read_opcode_props_sequence(client, priv, display_serdes_config, &priv->deinit_seq);
	if (err < 0) {
		dev_info(dev, "%s: read_opcode_props_sequence failed (err=%d) for %s\n", __func__, err, priv->deinit_seq.name);
	}

	priv->errb_seq.name = "errb-seq";
	err = read_opcode_props_sequence(client, priv, display_serdes_config, &priv->errb_seq);
	if (err < 0) {
		dev_info(dev, "%s: read_opcode_props_sequence failed (err=%d) for %s\n", __func__, err, priv->errb_seq.name);
	}

	priv->devctl_seq.name = "devctl-seq";
	err = read_opcode_props_sequence(client, priv, display_serdes_config, &priv->devctl_seq);
	if (err < 0) {
		dev_info(dev, "%s: read_opcode_props_sequence failed (err=%d) for %s\n", __func__, err, priv->devctl_seq.name);
	}

	return 0;
}

static void nvdisp_serdes_epl_report_error(uint32_t error_code)
{
#ifdef CONFIG_TEGRA_EPL
	struct epl_error_report_frame error_report;
	u64 time;

	asm volatile("mrs %0, cntvct_el0" : "=r" (time));

	error_report.error_code = error_code;
	error_report.error_attribute = 0x0;
	error_report.reporter_id = NVDISP_SERDES_EPL_REPORTER_ID;
	error_report.timestamp = (u32) time;

	epl_report_error(error_report);
#endif
	return;
}

static irqreturn_t nvdisp_serdes_irq_handler(int irq, void *dev_id)
{
	struct nvdisp_serdes_priv *priv = dev_id;
	struct device *dev = &priv->client->dev;
	struct i2c_client *client = to_i2c_client(dev);
	int ret;
	int pos;

	dev_dbg(dev, "%s: errb interrupt triggered\n", __func__);

	// Call vendor-specific pre-error handler
	ret = vendor_serdes_callback(priv, NVDISP_SERDES_OP_PRE_ERRB);
	if (ret < 0) {
		dev_err(dev, "Vendor SerDes pre-error handler failed (ret=%d)\n", ret);
		// Continue with sequence even if pre-callback fails
	}

	// Execute error handling sequence
	if (priv->errb_seq.length > 0) {
		pos = 0;
		while (pos < priv->errb_seq.length) {
			ret = dispatch_opcode(client, priv, priv->errb_seq.payload, &pos, priv->errb_seq.length);
			if (ret < 0) {
				dev_err(dev, "IRQ: dispatch_opcode(errb_seq) failed (ret=%d) at pos=%d\n",
					ret, pos);
				// Continue with post callback even if opcode dispatch fails
			}
		}
	}

	// Call vendor-specific post-error handler
	dev_info(dev, "Processing SERDES error\n");
	ret = vendor_serdes_callback(priv, NVDISP_SERDES_OP_POST_ERRB);
	if (ret < 0) {
		dev_err(dev, "Vendor SerDes post-error handler failed (ret=%d)\n", ret);
	}

	// Clear error status
	priv->serdes_errb = 0;

	// Report error resolution
	nvdisp_serdes_epl_report_error(0x0);

	return IRQ_HANDLED;
}

static int nvdisp_serdes_i2c_config(struct device *dev)
{
	struct nvdisp_serdes_priv *priv;
	struct i2c_client *client;
	struct device_node *ser = dev->of_node;
	int err;
	u32 val;

	/* update this approach when multiple devices on same i2c bus has different reg_bits and val_bits */
	static struct regmap_config nvdisp_serdes_i2c_regmap = {
		.reg_bits = 16,
		.val_bits = 8,
	};

	client = to_i2c_client(dev);
	priv = i2c_get_clientdata(client);

	err = of_property_read_u32(ser, "reg-bits", &val);
	if (err) {
		dev_dbg(dev, "%s: reg-bits property not found, using default\n",
			 __func__);
		nvdisp_serdes_i2c_regmap.reg_bits = 16;
	} else {
		nvdisp_serdes_i2c_regmap.reg_bits = val;
	}

	err = of_property_read_u32(ser, "val-bits", &val);
	if (err) {
		dev_dbg(dev, "%s: val-bits property not found, using default\n",
			 __func__);
		nvdisp_serdes_i2c_regmap.val_bits = 8;
	} else {
		nvdisp_serdes_i2c_regmap.val_bits = val;
	}

	priv->regmap = devm_regmap_init_i2c(client, &nvdisp_serdes_i2c_regmap);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	return 0;
}

static int nvdisp_serdes_deinit(struct device *dev)
{
	struct nvdisp_serdes_priv *priv;
	struct i2c_client *client;
	int ret;
	int pos;

	client = to_i2c_client(dev);
	priv = i2c_get_clientdata(client);

	// Call vendor-specific pre-deinit handler
	ret = vendor_serdes_callback(priv, NVDISP_SERDES_OP_PRE_DEINIT);
	if (ret < 0) {
		dev_err(dev, "Vendor SerDes pre-deinit handler failed (ret=%d)\n", ret);
		// Continue with sequence even if pre-callback fails
	}

	// Execute deinit sequence
	if (priv->deinit_seq.length > 0) {
		pos = 0;
		while (pos < priv->deinit_seq.length) {
			ret = dispatch_opcode(client, priv, priv->deinit_seq.payload, &pos, priv->deinit_seq.length);
			if (ret < 0) {
				dev_err(dev, "deinit: dispatch_opcode failed (ret=%d) at pos=%d\n",
						ret, pos);
				// Continue with post callback even if opcode dispatch fails
			}
		}
	}

	// Call vendor-specific post-deinit handler
	ret = vendor_serdes_callback(priv, NVDISP_SERDES_OP_POST_DEINIT);
	if (ret < 0) {
		dev_err(dev, "Vendor SerDes post-deinit handler failed (ret=%d)\n", ret);
	}

	return 0;
}

static int op_pwr_ctrl_on(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload)
{
	struct device *dev = &priv->client->dev;
	int ret = 0;

	if (priv->gpiod_pwrdn) {
		dev_info(dev, "%s: Setting nvdisp-serdes-pwrdn pin high to power on serializer\n", __func__);
		gpiod_set_value_cansleep(priv->gpiod_pwrdn, 1);
	} else if (gpio_is_valid(priv->serdes_pwrdn)) {
		dev_info(dev, "%s: Setting nvdisp-serdes-pwrdn pin high to power on serializer\n", __func__);
		gpio_set_value_cansleep(priv->serdes_pwrdn, 1);
	} else {
		dev_info(dev, "%s: No valid nvdisp-serdes-pwrdn pin available\n", __func__);
	}

	return ret;
}

static int op_pwr_ctrl_off(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload)
{
	struct device *dev = &priv->client->dev;
	int ret = 0;

	if (priv->gpiod_pwrdn) {
		dev_info(dev, "%s: Setting nvdisp-serdes-pwrdn pin low to power off ser\n", __func__);
		gpiod_set_value_cansleep(priv->gpiod_pwrdn, 0);
	} else if (gpio_is_valid(priv->serdes_pwrdn)) {
		dev_info(dev, "%s: Setting nvdisp-serdes-pwrdn pin low to power off ser\n", __func__);
		gpio_set_value_cansleep(priv->serdes_pwrdn, 0);
	} else {
		dev_info(dev, "%s: No valid nvdisp-serdes-pwrdn pin available\n", __func__);
	}

	return ret;
}

static int nvdisp_serdes_init(struct device *dev)
{
	struct nvdisp_serdes_priv *priv;
	struct i2c_client *client;
	int ret;
	int pos;

	client = to_i2c_client(dev);
	priv = i2c_get_clientdata(client);

	// Call vendor-specific pre-init handler
	ret = vendor_serdes_callback(priv, NVDISP_SERDES_OP_PRE_INIT);
	if (ret < 0) {
		dev_err(dev, "Vendor SerDes pre-init handler failed (ret=%d)\n", ret);
		return ret;
	}

	// Execute init sequence
	if (priv->init_seq.length > 0) {
		pos = 0;
		while (pos < priv->init_seq.length) {
			ret = dispatch_opcode(client, priv, priv->init_seq.payload, &pos, priv->init_seq.length);
			if (ret < 0) {
				dev_err(dev, "init: dispatch_opcode failed (ret=%d) at pos=%d\n",
						ret, pos);
				return ret;
			}
		}
	}

	// Call vendor-specific post-init handler
	ret = vendor_serdes_callback(priv, NVDISP_SERDES_OP_POST_INIT);
	if (ret < 0) {
		dev_err(dev, "Vendor SerDes post-init handler failed (ret=%d)\n", ret);
		return ret;
	}

	return 0;
}

#define NVDISP_SERDES_MAGIC 'S'
#define NVDISP_SERDES_EXECUTE_DEVCTL_SUB_SEQUENCE _IOWR(NVDISP_SERDES_MAGIC, 2, struct nvdisp_serdes_ioctl_data)

/*
 * Executes a specific sub-sequence within the devctl sequence buffer.
 * Returns 0 on success, negative error code on failure.
 * Updates priv->last_read_value and priv->last_read_valid based on reads.
 */
static int execute_devctl_sequence(struct i2c_client *client, unsigned int devctl_seq_id)
{
	struct nvdisp_serdes_priv *priv = i2c_get_clientdata(client);
	struct device *dev = priv->chardev ? priv->chardev : &client->dev;
	int ret = 0;
	int devctl_pos = 0;
	int no_of_opcodes_to_skip = 0;
	int no_of_opcodes_to_dispatch = -1; // Initialize to indicate not found yet
	int devctl_sub_seq_start;
	bool sequence_found = false;

	dev_dbg(dev, "%s: executing devctl sequence ID %u\n", __func__, devctl_seq_id);
	if (priv->devctl_seq.length == 0) {
		dev_info(dev, "%s: devctl sequence is empty\n", __func__);
		return -ENOENT; // Indicate sequence cannot be found
	}

	// Find the requested devctl sub-sequence
	while (devctl_pos < priv->devctl_seq.length) {
		if (devctl_pos + 1 >= priv->devctl_seq.length) {
			dev_err(dev, "%s: Malformed sequence - missing length at pos %d\n", __func__, devctl_pos);
			return -EINVAL;
		}

		SPECULATIVE_EXECUTION_BARRIER();

		if (devctl_seq_id == priv->devctl_seq.payload[devctl_pos]) {

			SPECULATIVE_EXECUTION_BARRIER();

			no_of_opcodes_to_dispatch = priv->devctl_seq.payload[devctl_pos + 1];
			devctl_pos += 2; // Move past ID and length
			dev_info(dev, "%s: Found sequence ID %u, dispatching %d opcodes starting at pos %d\n",
				 __func__, devctl_seq_id, no_of_opcodes_to_dispatch, devctl_pos);
			sequence_found = true;
			break; // Found the sequence, exit search loop
		}
		// --- Skip this sub-sequence ---
		no_of_opcodes_to_skip = priv->devctl_seq.payload[devctl_pos + 1];
		dev_dbg(dev, "%s: Skipping sequence ID %u (%d opcodes)\n", __func__,
			priv->devctl_seq.payload[devctl_pos], no_of_opcodes_to_skip);
		devctl_pos += 2; // Move past ID and length

		// Calculate the position jump without dispatching
		while (no_of_opcodes_to_skip > 0 && devctl_pos < priv->devctl_seq.length) {
			int next_pos;
			const opcode_descriptor_t *opcode_desc = find_opcode(priv->devctl_seq.payload[devctl_pos]);
			if (opcode_desc == NULL) {
				dev_err(dev, "%s: Unknown OPCODE 0x%x encountered while skipping at pos %d\n",
					__func__, priv->devctl_seq.payload[devctl_pos], devctl_pos);
				return -EINVAL; // Error during skipping indicates malformed sequence
			}
			next_pos = devctl_pos + opcode_desc->payload_size + 1; // Position of terminator
			if (next_pos >= priv->devctl_seq.length || priv->devctl_seq.payload[next_pos] != OPCODE_TERM) {
				dev_err(dev, "%s: Malformed sequence - missing terminator while skipping at pos %d\n",
					__func__, devctl_pos);
				return -EINVAL;
			}
			devctl_pos = next_pos + 1; // Move to the next opcode
			no_of_opcodes_to_skip--;
		}
		// --- End skip logic ---

		if (no_of_opcodes_to_skip > 0 && devctl_pos >= priv->devctl_seq.length) {
			// Reached end unexpectedly while skipping
			dev_err(dev, "%s: Reached end of sequence while skipping for ID %u\n", __func__, devctl_seq_id);
			return -EINVAL; // Malformed sequence
		}
	}

	// --- Check if the sequence was found ---
	if (!sequence_found) {
		dev_err(dev, "%s: devctl sequence ID %u not found\n", __func__, devctl_seq_id);
		return -ENOENT;
	}

	if (no_of_opcodes_to_dispatch <= 0) {
		dev_info(dev, "%s: Sequence ID %u has zero opcodes to dispatch.\n", __func__, devctl_seq_id);
		// Return success, but indicate no read value is valid
		return 0;
	}

	dev_info(dev, "%s: Starting dispatch for sequence ID %u at pos %d\n", __func__, devctl_seq_id, devctl_pos);
	devctl_sub_seq_start = devctl_pos;
	/* opcode dispatcher code */
	while (devctl_sub_seq_start < priv->devctl_seq.length && no_of_opcodes_to_dispatch > 0) {
		// Use passthrough=true to actually execute the opcode
		ret = dispatch_opcode(client, priv, priv->devctl_seq.payload, &devctl_sub_seq_start, priv->devctl_seq.length);
		if (ret != 0) {
			dev_err(dev, "%s: dispatch_opcode failed (%d) at pos %d for sequence ID %u\n",
				__func__, ret, devctl_sub_seq_start, devctl_seq_id);
			return ret; // Propagate the error from dispatch_opcode
		}
		no_of_opcodes_to_dispatch--;
	}

	if (no_of_opcodes_to_dispatch > 0) {
		dev_warn(dev, "%s: Sequence ID %u finished unexpectedly, %d opcodes remaining (Sequence length issue?)\n",
			 __func__, devctl_seq_id, no_of_opcodes_to_dispatch);
		// Indicate potential issue, but maybe the executed part was successful
		return -EIO; // Return I/O error to indicate incomplete execution
	}

	dev_info(dev, "%s: Finished dispatch for sequence ID %u\n", __func__, devctl_seq_id);
	// Return 0 for success. The last read value is in priv->last_read_value
	return 0;
}

/**
 * @brief Perform bulk register reads
 */
int32_t nvdisp_serdes_i2c_xfer(struct nvdisp_serdes_priv *priv,
			      struct nvdisp_serdes_i2c_xfer *xfer)
{
	int32_t ret = 0;
	u16 i;
	struct device *dev = &priv->client->dev;

	if (!priv || !xfer) {
		dev_err(dev, "%s: Invalid parameters\n", __func__);
		return -EINVAL;
	}

	/* Validate sizes */
	if (xfer->write_size > NVDISP_SERDES_MAX_READ_WRITE ||
	    xfer->read_size > NVDISP_SERDES_MAX_READ_WRITE) {
		dev_err(dev, "%s: Invalid transfer sizes (write=%d, read=%d)\n",
		       __func__, xfer->write_size, xfer->read_size);
		return -EINVAL;
	}

	/* Process write operations first */
	for (i = 0; i < xfer->write_size; i++) {
		ret = nvdisp_serdes_write(priv, xfer->slave_addr, xfer->write_regs[i], xfer->write_vals[i]);
		if (ret < 0) {
			dev_err(dev, "%s: Write failed for reg 0x%04x (err=%d)\n",
			       __func__, xfer->write_regs[i], ret);
			xfer->write_status[i] = -ret;
		} else {
			xfer->write_status[i] = 0;  /* Success */
		}
	}

	/* Process read operations */
	for (i = 0; i < xfer->read_size; i++) {
		ret = nvdisp_serdes_read(priv, xfer->slave_addr, xfer->read_regs[i], &xfer->read_vals[i]);
		if (ret < 0) {
			dev_err(dev, "%s: Read failed for reg 0x%04x (err=%d)\n",
			       __func__, xfer->read_regs[i], ret);
			xfer->read_status[i] = -ret;
		} else {
			xfer->read_status[i] = 0;  /* Success */
		}
	}

	return 0;  /* Return success even if some operations failed - check individual statuses */
}

static long nvdisp_serdes_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct nvdisp_serdes_priv *priv = file->private_data;
	struct device *dev = NULL;
	struct nvdisp_serdes_ioctl_data ioctl_data;
	int ret = 0;

	if ((priv == NULL) || (priv->client == NULL)) {
		pr_err("nvdisp_serdes: %s called with invalid private data\n", __func__);
		return -ENODEV;
	}

	dev = priv->chardev ? priv->chardev : &priv->client->dev; // Prefer chardev for logging if available

	mutex_lock(&priv->mutex);

	switch (cmd) {
	case NVDISP_SERDES_I2C_XFER: {
		struct nvdisp_serdes_i2c_xfer *xfer;

		xfer = devm_kzalloc(dev, sizeof(*xfer), GFP_KERNEL);
		if (!xfer) {
			ret = -ENOMEM;
			goto unlock_out;
		}

		if (copy_from_user(xfer, (void __user *)arg, sizeof(*xfer))) {
			dev_err(dev, "%s: Failed to copy I2C transfer request from user\n", __func__);
			ret = -EFAULT;
			goto free_xfer;
		}

		ret = nvdisp_serdes_i2c_xfer(priv, xfer);
		if (ret < 0) {
			dev_err(dev, "%s: I2C transfer operation failed: %d\n", __func__, ret);
			goto free_xfer;
		}

		if (copy_to_user((void __user *)arg, xfer, sizeof(*xfer))) {
			dev_err(dev, "%s: Failed to copy I2C transfer response to user\n", __func__);
			ret = -EFAULT;
		}

free_xfer:
		devm_kfree(dev, xfer);
		break;
	}

	case NVDISP_SERDES_EXECUTE_DEVCTL_SUB_SEQUENCE:
		// Copy data from user space (seq_id is input)
		if (copy_from_user(&ioctl_data, (void __user *)arg, sizeof(ioctl_data))) {
			ret = -EFAULT;
			goto unlock_out;
		}

		dev_info(dev, "%s: Received ioctl to execute devctl sequence ID %u\n", __func__, ioctl_data.seq_id);

		if (ioctl_data.seq_id == 0) {
			dev_err(dev, "%s: Invalid sequence ID: 0\n", __func__);
			ret = -EINVAL;
			goto unlock_out;
		}

		// Execute the sequence. Return value indicates success/failure.
		// Last read value (if any) is stored in priv->last_read_value.
		ret = execute_devctl_sequence(priv->client, ioctl_data.seq_id);
		if (ret < 0) {
			dev_err(dev, "%s: execute_devctl_sequence failed for ID %u with error %d\n", __func__, ioctl_data.seq_id, ret);
			// Keep the error code in 'ret' to return to userspace
		} else {
			// Execution was successful
			dev_info(dev, "%s: Sequence ID %u executed successfully\n",
				 __func__, ioctl_data.seq_id);
			// Return success (ret is already 0)
		}
		break; // NVDISP_SERDES_EXECUTE_DEVCTL_SUB_SEQUENCE case end

	default:
		dev_warn(dev, "%s: Received unknown ioctl command: 0x%x\n", __func__, cmd);
		ret = -ENOTTY;
		break;
	}

unlock_out:
	mutex_unlock(&priv->mutex);
	return ret; // Return 0 on success, negative error code otherwise
}

static int nvdisp_serdes_open(struct inode *inode, struct file *file)
{
	struct nvdisp_serdes_priv *priv = container_of(inode->i_cdev, struct nvdisp_serdes_priv, cdev);
	file->private_data = priv;

	dev_dbg(priv->chardev, "%s: open\n", __func__);
	return 0;
}

static int nvdisp_serdes_release(struct inode *inode, struct file *file)
{
	struct nvdisp_serdes_priv *priv = file->private_data;
	dev_dbg(priv->chardev, "%s: release\n", __func__);
	file->private_data = NULL;
	return 0;
}

static const struct file_operations nvdisp_serdes_fops = {
	.owner = THIS_MODULE,
	.open = nvdisp_serdes_open,
	.release = nvdisp_serdes_release,
	.unlocked_ioctl = nvdisp_serdes_ioctl,
};

static int nvdisp_serdes_probe(struct i2c_client *client)
{
	struct nvdisp_serdes_priv *priv;
	struct device *dev = &client->dev;
	struct device_node *ser = client->dev.of_node;
	struct device_node *display_serdes_config;
	const char *serdes_instance_prop;
	char instance_char;
	int ret;
	int minor;

	minor = ida_alloc_max(&nvdisp_serdes_ida, MAX_SERDES_INSTANCES - 1, GFP_KERNEL);
	if (minor < 0) {
		dev_err(dev, "Failed to allocate minor number: %d\n", minor);
		return minor;
	}

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (priv == NULL) {
		ret = -ENOMEM;
		goto err_free_ida;
	}

	priv->gpiod_pwrdn = NULL;  /* Initialize to NULL for safety */
	priv->client = client;
	i2c_set_clientdata(client, priv);
	mutex_init(&priv->mutex);
	priv->devt = MKDEV(MAJOR(nvdisp_serdes_devt_base), minor);

	display_serdes_config = of_find_node_by_name(ser, "display-serdes-config");
	if (display_serdes_config == NULL) {
		dev_err(dev, "Cannot get display-serdes-config node\n");
		ret = -ENODEV;
		goto err_free_ida;
	}

	ret = of_property_read_string(ser, "serdes_instance", &serdes_instance_prop);
	if (ret < 0 || sscanf(serdes_instance_prop, "SER %c", &instance_char) != 1) {
		dev_warn(dev, "serdes_instance property invalid or not found, using minor %d\n", minor);
		snprintf(priv->instance_name, sizeof(priv->instance_name),
			 "nvdisp_serdes%d", minor);
	} else {
		snprintf(priv->instance_name, sizeof(priv->instance_name),
			 "nvdisp_serdes_%c", tolower(instance_char));
	}
	of_node_put(display_serdes_config);

	dev_info(dev, "Probing instance %s (minor %d)\n", priv->instance_name, minor);

	ret = read_opcode_props(client, priv);
	if (ret != 0) {
		dev_err(dev, "Error parsing opcode properties from device tree\n");
		goto err_free_ida;
	}

	ret = nvdisp_serdes_i2c_config(&client->dev);
	if (ret < 0) {
		dev_err(dev, "I2C config failed: %d\n", ret);
		goto err_free_ida;
	}

	/* Get the PWRDN GPIO pin */
	priv->serdes_pwrdn = of_get_named_gpio(ser, "nvdisp-serdes-pwrdn", 0);
	if (!gpio_is_valid(priv->serdes_pwrdn)) {
		dev_err(dev, "%s: nvdisp-serdes-pwrdn GPIO not valid\n", __func__);
		/* Continue even if GPIO is not valid - don't return error */
	} else {
		/* Request the GPIO pin with initial state LOW (powered down) */
		ret = devm_gpio_request_one(&client->dev, priv->serdes_pwrdn,
				      GPIOF_OUT_INIT_LOW, "GPIO_PWRDN_NVDISP_SERDES");
		if (ret < 0) {
			dev_err(dev, "%s: devm_gpio_request_one for nvdisp-serdes-pwrdn failed ret: %d\n",
				__func__, ret);
			/* Continue even if GPIO request fails - don't return error */
		} else {
			/* Create GPIO descriptor for later use */
			priv->gpiod_pwrdn = gpio_to_desc(priv->serdes_pwrdn);
			if (!priv->gpiod_pwrdn) {
				dev_err(dev, "%s: Failed to get GPIO descriptor for nvdisp-serdes-pwrdn\n",
				__func__);
			}
		}
	}

	ret = nvdisp_serdes_init(&client->dev);
	if (ret < 0) {
		dev_err(dev, "Hardware init failed: %d\n", ret);
		goto err_free_ida;
	}

	cdev_init(&priv->cdev, &nvdisp_serdes_fops);
	priv->cdev.owner = THIS_MODULE;
	ret = cdev_add(&priv->cdev, priv->devt, 1);
	if (ret < 0) {
		dev_err(dev, "Failed to add character device %s: %d\n", priv->instance_name, ret);
		goto err_deinit;
	}

	priv->chardev = device_create(nvdisp_serdes_class, dev, /* parent */
				    priv->devt, priv, /* drvdata */
				    "%s", priv->instance_name);
	if (IS_ERR(priv->chardev)) {
		ret = PTR_ERR(priv->chardev);
		dev_err(dev, "Failed to create device node /dev/%s: %d\n", priv->instance_name, ret);
		priv->chardev = NULL;
		goto err_del_cdev;
	}
	dev_info(priv->chardev, "Character device created\n");

	priv->serdes_errb = of_get_named_gpio(ser, "nvdisp-serdes-errb", 0);
	if (!gpio_is_valid(priv->serdes_errb)) {
		dev_info(priv->chardev, "No valid 'nvdisp-serdes-errb' GPIO found.\n");
		priv->ser_irq = -1;
	} else {
		ret = devm_gpio_request_one(&client->dev, priv->serdes_errb,
					    GPIOF_IN, "GPIO_ERRB_NVDISP_SERDES");
		if (ret < 0) {
			dev_err(priv->chardev, "devm_gpio_request_one for errb failed: %d\n", ret);
			goto err_destroy_device;
		}

		priv->ser_irq = gpio_to_irq(priv->serdes_errb);
		if (priv->ser_irq < 0) {
			ret = priv->ser_irq;
			dev_err(priv->chardev, "gpio_to_irq failed for errb: %d\n", ret);
			goto err_destroy_device;
		}

		ret = request_threaded_irq(priv->ser_irq, NULL,
					   nvdisp_serdes_irq_handler,
					   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					   priv->instance_name,
					   priv);
		if (ret < 0) {
			dev_err(priv->chardev, "Unable to register IRQ handler: %d\n", ret);
			priv->ser_irq = -1;
			goto err_destroy_device;
		}
		dev_info(priv->chardev, "Registered IRQ %d for errb GPIO %d\n", priv->ser_irq, priv->serdes_errb);
	}

	dev_info(priv->chardev, "Probe successful for %s\n", priv->instance_name);
	return 0;

err_destroy_device:
	device_destroy(nvdisp_serdes_class, priv->devt);
err_del_cdev:
	cdev_del(&priv->cdev);
err_deinit:
	if (nvdisp_serdes_init(&client->dev) == 0)
		nvdisp_serdes_deinit(&client->dev);
err_free_ida:
	ida_free(&nvdisp_serdes_ida, minor);
	dev_err(dev, "Probe failed for %s with error %d\n",
		(priv && priv->instance_name[0]) ? priv->instance_name : "unknown instance", ret);
	return ret;
}

static void nvdisp_serdes_shutdown(struct i2c_client *client)
{
	struct nvdisp_serdes_priv *priv = i2c_get_clientdata(client);
	int ret;

	dev_info(priv->chardev ? priv->chardev : &client->dev, "Shutting down instance %s\n",
		 priv->instance_name);

	ret = nvdisp_serdes_deinit(&client->dev);
	if (ret < 0) {
		dev_crit(priv->chardev ? priv->chardev : &client->dev,
			"nvdisp_serdes_deinit failed during shutdown: %d\n", ret);
	}

}

#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
static int nvdisp_serdes_remove(struct i2c_client *client)
#else
static void nvdisp_serdes_remove(struct i2c_client *client)
#endif
{
	struct nvdisp_serdes_priv *priv = i2c_get_clientdata(client);
	int minor = MINOR(priv->devt);

	dev_info(priv->chardev ? priv->chardev : &client->dev, "Removing instance %s (minor %d)\n",
		 priv->instance_name, minor);

	if (priv->ser_irq >= 0) {
		free_irq(priv->ser_irq, priv);
	}

	if (priv->chardev) {
		device_destroy(nvdisp_serdes_class, priv->devt);
	}

	cdev_del(&priv->cdev);

	nvdisp_serdes_deinit(&client->dev);

	ida_free(&nvdisp_serdes_ida, minor);

#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
	return 0;
#endif
}

#ifdef CONFIG_PM
static int nvdisp_serdes_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct nvdisp_serdes_priv *priv = i2c_get_clientdata(client);
	struct device *logdev = priv->chardev ? priv->chardev : dev;
	int ret = 0;

	dev_info(logdev, "%s: suspending\n", __func__);
	ret = nvdisp_serdes_deinit(&client->dev);
	if (ret < 0) {
		dev_err(logdev, "%s: nvdisp_serdes_deinit failed: %d\n", __func__, ret);
	}
	return ret;
}

static int nvdisp_serdes_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct nvdisp_serdes_priv *priv = i2c_get_clientdata(client);
	struct device *logdev = priv->chardev ? priv->chardev : dev;
	int ret = 0;

	dev_info(logdev, "%s: resuming\n", __func__);
	ret = nvdisp_serdes_init(&client->dev);
	if (ret < 0) {
		dev_err(logdev, "%s: nvdisp_serdes_init failed: %d\n", __func__, ret);
	}
	return ret;
}

const struct dev_pm_ops nvdisp_serdes_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(
		nvdisp_serdes_suspend, nvdisp_serdes_resume)
};
#endif

static const struct of_device_id nvdisp_serdes_dt_ids[] = {
	{ .compatible = "nvidia,nvdisp_serdes" },
	{},
};
MODULE_DEVICE_TABLE(of, nvdisp_serdes_dt_ids);

static struct i2c_driver nvdisp_serdes_i2c_driver = {
	.driver	= {
		.name		= "nvdisp_serdes",
		.of_match_table	= of_match_ptr(nvdisp_serdes_dt_ids),
#ifdef CONFIG_PM
		.pm	= &nvdisp_serdes_pm_ops,
#endif
	},
#if defined(NV_I2C_DRIVER_STRUCT_HAS_PROBE_NEW) /* Dropped on Linux 6.6 */
	.probe_new	= nvdisp_serdes_probe,
#else
	.probe		= nvdisp_serdes_probe,
#endif
	.remove		= nvdisp_serdes_remove,
	.shutdown	= nvdisp_serdes_shutdown,
};

static int __init nvdisp_serdes_module_init(void)
{
	int ret;

	pr_info("nvdisp_serdes: Loading module...\n");

	ret = alloc_chrdev_region(&nvdisp_serdes_devt_base, 0, MAX_SERDES_INSTANCES, "nvdisp_serdes");
	if (ret < 0) {
		pr_err("nvdisp_serdes: Failed to allocate chrdev region: %d\n", ret);
		return ret;
	}
	pr_info("nvdisp_serdes: Allocated device region Major %d\n", MAJOR(nvdisp_serdes_devt_base));

#if defined(NV_CLASS_CREATE_HAS_NO_OWNER_ARG)
	nvdisp_serdes_class = class_create("nvdisp_serdes");
#else
	nvdisp_serdes_class = class_create(THIS_MODULE, "nvdisp_serdes");
#endif
	if (IS_ERR(nvdisp_serdes_class)) {
		ret = PTR_ERR(nvdisp_serdes_class);
		pr_err("nvdisp_serdes: Failed to create device class: %d\n", ret);
		unregister_chrdev_region(nvdisp_serdes_devt_base, MAX_SERDES_INSTANCES);
		return ret;
	}
	pr_info("nvdisp_serdes: Created device class\n");

	ret = i2c_add_driver(&nvdisp_serdes_i2c_driver);
	if (ret) {
		pr_err("nvdisp_serdes: Failed to register i2c driver: %d\n", ret);
		class_destroy(nvdisp_serdes_class);
		unregister_chrdev_region(nvdisp_serdes_devt_base, MAX_SERDES_INSTANCES);
		return ret;
	}
	pr_info("nvdisp_serdes: Registered I2C driver\n");

	return 0;
}

static void __exit nvdisp_serdes_module_exit(void)
{
	pr_info("nvdisp_serdes: Unloading module...\n");
	i2c_del_driver(&nvdisp_serdes_i2c_driver);
	class_destroy(nvdisp_serdes_class);
	unregister_chrdev_region(nvdisp_serdes_devt_base, MAX_SERDES_INSTANCES);
	ida_destroy(&nvdisp_serdes_ida);
	pr_info("nvdisp_serdes: Module unloaded.\n");
}

module_init(nvdisp_serdes_module_init);
module_exit(nvdisp_serdes_module_exit);

MODULE_DESCRIPTION("NvDisplay SerDes Driver");
MODULE_AUTHOR("Prafull Suryawanshi");
MODULE_LICENSE("GPL");
