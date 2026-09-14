/* SPDX-License-Identifier: GPL-2.0-only
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef _EXPLORE_CEC_H
#define _EXPLORE_CEC_H

#include <uapi/misc/ep_cec.h>
#include <linux/miscdevice.h>
#include <linux/gpio/consumer.h>
#include <linux/kfifo.h>
#include <linux/mutex.h>
#include <linux/wait.h>

#define CEC_DEVICE_NAME			"explore_cec"
#define CEC_INT_ECF_MASK		0x02
#define CEC_MAX_LOGICAL_ADDR		0x0F
#define CEC_MAX_PHYSICAL_ADDR		0xFFFF

/* HIGH_LEVEL = 0, ECI_EN = 1, CCI_EN = 0 */
#define CEC_FEATURE_INIT_VALUE		0x02

/* Command Opcode */
#define CEC_CMD_TRANSMIT_MSG		0x01
#define CEC_CMD_SET_LOGICAL_ADDR	0x02
#define CEC_CMD_DELAY_MS		50

/* System Control Registers */
#define CEC_REG_SYSTEM_CTRL		0x0100
#define CEC_REG_RETRY_TIME		0x0101
#define CEC_REG_FEATURE_CTRL		0x0102

/* Feature Enable Registers */
#define CEC_REG_FEATURE_EN_0_7		0x0103
#define CEC_REG_FEATURE_EN_8_15		0x0104

/* Interrupt and Command Registers */
#define CEC_REG_INTR_FLAGS		0x0105
#define CEC_REG_CMD_CODE		0x0106

/* High Level Mode: Device Topology Registers */
#define CEC_REG_PHYS_ADDR_HIGH		0x0107
#define CEC_REG_PHYS_ADDR_LOW		0x0108

/* Firmware Revision */
#define CEC_REG_FW_REV_HIGH		0x0400
#define CEC_REG_FW_REV_LOW		0x0401

/* Command Parameter Buffer */
#define CEC_REG_CMD_START		0x0500
#define CEC_REG_CMD_SIZE		0x0501
#define CEC_REG_CMD_OPCODE		0x0502
#define CEC_REG_CMD_PARAM_0		0x0503
#define CEC_REG_CMD_PARAM_1		0x0504
#define CEC_REG_CMD_PARAM_2		0x0505
#define CEC_REG_CMD_PARAM_3		0x0506
#define CEC_REG_CMD_PARAM_4		0x0507
#define CEC_REG_CMD_PARAM_5		0x0508
#define CEC_REG_CMD_PARAM_6		0x0509
#define CEC_REG_CMD_PARAM_7		0x050A
#define CEC_REG_CMD_PARAM_8		0x050B
#define CEC_REG_CMD_PARAM_9		0x050C
#define CEC_REG_CMD_PARAM_10		0x050D
#define CEC_REG_CMD_PARAM_11		0x050E
#define CEC_REG_CMD_PARAM_12		0x050F
#define CEC_REG_CMD_PARAM_13		0x0510

/* Command Status */
#define CEC_REG_CMD_STATUS		0x0600

/* Event Status Buffer */
#define CEC_REG_EVT_START		0x0700
#define CEC_REG_EVT_SIZE		0x0701
#define CEC_REG_EVT_OPCODE		0x0702
#define CEC_REG_EVT_PARAM_0		0x0703
#define CEC_REG_EVT_PARAM_1		0x0704
#define CEC_REG_EVT_PARAM_2		0x0705
#define CEC_REG_EVT_PARAM_3		0x0706
#define CEC_REG_EVT_PARAM_4		0x0707
#define CEC_REG_EVT_PARAM_5		0x0708
#define CEC_REG_EVT_PARAM_6		0x0709
#define CEC_REG_EVT_PARAM_7		0x070A
#define CEC_REG_EVT_PARAM_8		0x070B
#define CEC_REG_EVT_PARAM_9		0x070C
#define CEC_REG_EVT_PARAM_10		0x070D
#define CEC_REG_EVT_PARAM_11		0x070E
#define CEC_REG_EVT_PARAM_12		0x070F
#define CEC_REG_EVT_PARAM_13		0x0710
#define CEC_REG_EVT_END			0x0711
#define CEC_REG_MAX			0x0711

/*
 * EP CEC only can buffer 21 messages at most, set the buffer size to 32 for
 * meeting the requirement of kfifo: aligning to power of 2.
 */
#define CEC_MSG_RING_BUFFER_MAX_COUNT	32

struct explore_cec {
	struct i2c_client *client;
	struct regmap *regmap;
	struct miscdevice misc_dev;
	struct gpio_desc *gpiod;

	unsigned int irq;
	u16 physical_addr;
	u8 logical_addr;

	DECLARE_KFIFO(cec_msg_fifo, struct ep_cec_message, CEC_MSG_RING_BUFFER_MAX_COUNT);
	struct mutex cec_lock;
	wait_queue_head_t cec_wait_queue;
};

#endif /* _EXPLORE_CEC_H */
