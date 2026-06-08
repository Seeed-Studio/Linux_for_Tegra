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

#ifdef CONFIG_TEGRA_EPL
#include <linux/tegra-epl.h>
#endif

/**
 * @brief EPL reporter ID for NVDISP SERDES.
 * This ID is used when reporting errors to FSI via EPL.
 */
#define NVDISP_SERDES_EPL_REPORTER_ID 0x8103

/**
 * @brief I2C opcode data structure sizes.
 * DO NOT MODIFY!!
 */
#define SIZE_PAYLOAD_I2C_WRITE_8_SINGLE                     (6U)
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

struct opcode_sequence {
	u8 *payload;
	int32_t length;
	char *name;
};

struct nvdisp_serdes_priv {
	struct i2c_client *client;
	struct mutex mutex;
	struct regmap *regmap;
	int serdes_errb;
	int serdes_pwrdn;       /* GPIO number for power down pin */
	struct gpio_desc *gpiod_pwrdn;  /* GPIO descriptor for power pin */
	unsigned int ser_irq;
	struct opcode_sequence init_seq, deinit_seq, errb_seq;
};

/**
 * @brief Dispatch handler function definition.
 *
 */
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
} opcode_descriptor_t;

static int32_t op_i2c_write_byte(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload);
static int32_t op_i2c_read_after_write_byte(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload);
static int32_t op_i2c_read_byte_and_poll(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload);
static int32_t op_i2c_read_byte_compare_print(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload);
static int32_t op_i2c_update_byte(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload);
static int32_t op_i2c_read_byte_and_print(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload);
static int32_t op_i2c_read_byte(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload);
static int32_t op_delay_usec(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload);

opcode_descriptor_t opcode_desc_table[] = {
	/* Below OPCODEs are existing in CBA and used as it is in SerDes */
	{
		.description = "i2c write single byte",
		.opcode = 0x10,
		.payload_size = SIZE_PAYLOAD_I2C_WRITE_8_SINGLE,
		.dispatcher = op_i2c_write_byte,
	},
	{
		.description = "i2c read after write single byte",
		.opcode = 0x11,
		.payload_size = SIZE_PAYLOAD_I2C_WRITE_8_SINGLE,
		.dispatcher = op_i2c_read_after_write_byte,
	},
	{
		.description = "i2c read single byte",
		.opcode = 0x30,
		.payload_size = SIZE_PAYLOAD_I2C_READ_8_SINGLE,
		.dispatcher = op_i2c_read_byte,
	},
	{
		.description = "i2c read single byte and poll for value",
		.opcode = 0x90,
		.payload_size = SIZE_PAYLOAD_I2C_READ_8_SINGLE_WITH_POLLING,
		.dispatcher = op_i2c_read_byte_and_poll,
	},
	{
		.description = "delay in usec",
		.opcode = 0x50,
		.payload_size = SIZE_PAYLOAD_OS_DELAY_USEC,
		.dispatcher = op_delay_usec,
	},
	{
		.description = "i2c read single byte and match masked value with golden value",
		.opcode = 0x80,
		.payload_size = SIZE_PAYLOAD_I2C_READ_8_SINGLE_WITH_GOLDEN_VALUE,
		.dispatcher = op_i2c_read_byte_compare_print,
	},
	/* Below OPCODEs are new in SerDes */
	/* errb specific opcodes */
	{
		.description = "i2c update single byte with mask",
		.opcode = 0xA2,
		.payload_size = SIZE_PAYLOAD_I2C_UPDATE_SINGLE_BYTE,
		.dispatcher = op_i2c_update_byte,
	},
	/* debug opcodes */
	{
		.description = "i2c read single byte and print",
		.opcode = 0xF0,
		.payload_size = SIZE_PAYLOAD_I2C_READ_8_SINGLE_PRINT,
		.dispatcher = op_i2c_read_byte_and_print,
	},
};

#define NUM_OPCODES (sizeof(opcode_desc_table) / sizeof(opcode_desc_table[0]))

static int32_t nvdisp_serdes_read(struct nvdisp_serdes_priv *priv, u8 slave_addr, int reg, u8 *reg_val)
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

static int32_t nvdisp_serdes_write(struct nvdisp_serdes_priv *priv, u8 slave_addr, u32 reg, u8 val)
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

static int32_t nvdisp_serdes_update(struct nvdisp_serdes_priv *priv, u8 slave_addr, u32 reg, u32 mask, u8 val)
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

static int32_t dispatch_opcode(struct i2c_client *client, struct nvdisp_serdes_priv *priv, u8 *payload, int *curr_pos, int32_t length)
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

static int read_opcode_props_sequence(struct i2c_client *client,
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
	int err = 0;
	struct device_node *display_serdes_config;

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
	int32_t errb_pos = 0;
	int ret;

	dev_dbg(dev, "%s: errb interrupt triggered\n", __func__);

	if (priv->errb_seq.length == 0) {
		dev_info(dev, "%s: errb sequence is empty\n", __func__);
		goto done;
	}

	/* opcode dispatcher code */
	while (errb_pos < priv->errb_seq.length) {
		ret = dispatch_opcode(priv->client, priv, priv->errb_seq.payload,
						&errb_pos, priv->errb_seq.length);
		if (ret < 0) {
			dev_err(dev, "%s: dispatch_opcode failed (%d) at errb_pos = %d\n",
			__func__, ret, errb_pos);
			break;
		}
	}

	nvdisp_serdes_epl_report_error(0x0);
done:
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
	int ret = 0;
	int deinit_pos = 0;

	client = to_i2c_client(dev);
	priv = i2c_get_clientdata(client);

	if (priv->deinit_seq.length == 0) {
		dev_info(dev, "%s: deinit sequence is empty\n", __func__);
		goto done;
	}

	/* opcode dispatcher code */
	while (deinit_pos < priv->deinit_seq.length) {
		ret = dispatch_opcode(client, priv, priv->deinit_seq.payload, &deinit_pos, priv->deinit_seq.length);
		if (ret != 0) {
			dev_err(dev, "%s: dispatch_opcode failed (%d) at deinit_pos = %d\n",
			__func__, ret, deinit_pos);
			return ret;
		}
	}

done:
	return ret;
}

static int nvdisp_serdes_power_on(struct nvdisp_serdes_priv *priv)
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

static int nvdisp_serdes_init(struct device *dev)
{
	struct nvdisp_serdes_priv *priv;
	struct i2c_client *client;
	int ret = 0;
	int init_pos = 0;

	client = to_i2c_client(dev);
	priv = i2c_get_clientdata(client);

	/* First power on the serializer */
	ret = nvdisp_serdes_power_on(priv);
	if (ret != 0) {
		dev_err(dev, "%s: Failed to power on serializer\n", __func__);
		return ret;
	}

	/* opcode dispatcher code */
	while (init_pos < priv->init_seq.length) {
		ret = dispatch_opcode(client, priv, priv->init_seq.payload, &init_pos, priv->init_seq.length);
		if (ret != 0) {
			dev_err(dev, "%s: dispatch_opcode failed at init_pos = %d\n",
			__func__, init_pos);
			return ret;
		}
	}

	return ret;
}

static int nvdisp_serdes_probe(struct i2c_client *client)
{
	struct nvdisp_serdes_priv *priv;
	struct device *dev;
	struct device_node *ser = client->dev.of_node;
	int ret;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;

	priv->client = client;
	i2c_set_clientdata(client, priv);
	priv->gpiod_pwrdn = NULL;  /* Initialize to NULL for safety */

	dev = &priv->client->dev;

	ret = read_opcode_props(client, priv);
	if (ret != 0) {
		dev_err(dev, "%s: error parsing in device tree node\n", __func__);
		return -EFAULT;
	}

	ret = nvdisp_serdes_i2c_config(&client->dev);
	if (ret < 0) {
		dev_err(dev, "%s: nvdisp serdes boot failed\n", __func__);
		return -EFAULT;
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
		dev_err(dev, "%s: nvdisp serdes init failed\n", __func__);
		return -EFAULT;
	}

	priv->serdes_errb = of_get_named_gpio(ser, "nvdisp-serdes-errb", 0);

	ret = devm_gpio_request_one(&client->dev, priv->serdes_errb,
				    GPIOF_IN, "GPIO_ERRB_NVDISP_SERDES");
	if (ret < 0) {
		dev_err(dev, "%s: devm_gpio_request_one for nvdisp-serdes-errb failed ret: %d\n",
			__func__, ret);
		return ret;
	}

	if (gpio_is_valid(priv->serdes_errb)) {
		dev_dbg(dev, "%s: serdes_errb gpio is valid, requesting threaded irq\n", __func__);
		priv->ser_irq = gpio_to_irq(priv->serdes_errb);
		dev_set_name(dev, "%s.%s", dev_name(dev), "nvdisp_serdes");
		ret = request_threaded_irq(priv->ser_irq, NULL,
					   nvdisp_serdes_irq_handler,
					   IRQF_TRIGGER_FALLING
					   | IRQF_ONESHOT, dev_name(dev), priv);
		if (ret < 0) {
			dev_err(dev, "%s: Unable to register IRQ handler ret: %d\n",
				__func__, ret);
			return ret;
		}
	}

	return ret;
}

#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
static int nvdisp_serdes_remove(struct i2c_client *client)
#else
static void nvdisp_serdes_remove(struct i2c_client *client)
#endif
{
#if defined(NV_I2C_DRIVER_STRUCT_REMOVE_RETURN_TYPE_INT) /* Linux 6.1 */
	return 0;
#endif
}

#ifdef CONFIG_PM
static int nvdisp_serdes_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct nvdisp_serdes_priv *priv = i2c_get_clientdata(client);
	int ret = 0;

	ret = nvdisp_serdes_deinit(&client->dev);
	if (ret < 0) {
		dev_err(&priv->client->dev, "%s: nvdisp_serdes_deinit failed\n", __func__);
	}
	return ret;
}

static int nvdisp_serdes_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct nvdisp_serdes_priv *priv = i2c_get_clientdata(client);
	int ret = 0;

	ret = nvdisp_serdes_init(&client->dev);
	if (ret < 0) {
		dev_err(&priv->client->dev, "%s: nvdisp_serdes_init failed\n", __func__);
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
};

module_i2c_driver(nvdisp_serdes_i2c_driver);

MODULE_DESCRIPTION("NvDisplay SerDes Driver");
MODULE_AUTHOR("Prafull Suryawanshi");
MODULE_LICENSE("GPL");
