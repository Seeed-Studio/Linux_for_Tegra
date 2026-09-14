// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * NVDISP SERDES driver header
 *
 * Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
 */

#ifndef __NVDISP_SERDES_H__
#define __NVDISP_SERDES_H__

#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/cdev.h>

#define NVDISP_SERDES_MAX_READ_WRITE 512  /* Support large init sequences */

/**
 * @brief Structure for I2C transfer request
 */
struct nvdisp_serdes_i2c_xfer {
    u8 slave_addr;           /* I2C slave address */
    u16 write_size;          /* Number of write operations */
    u16 read_size;           /* Number of read operations */

    /* Write operation buffers */
    u16 write_regs[NVDISP_SERDES_MAX_READ_WRITE];    /* Write register addresses */
    u8 write_vals[NVDISP_SERDES_MAX_READ_WRITE];     /* Values to write */
    u8 write_status[NVDISP_SERDES_MAX_READ_WRITE];   /* Status for each write */

    /* Read operation buffers */
    u16 read_regs[NVDISP_SERDES_MAX_READ_WRITE];     /* Read register addresses */
    u8 read_vals[NVDISP_SERDES_MAX_READ_WRITE];      /* Read values */
    u8 read_status[NVDISP_SERDES_MAX_READ_WRITE];    /* Status for each read */
};

/**
 * @brief Operation types for vendor callbacks
 */
enum nvdisp_serdes_op_type {
    NVDISP_SERDES_OP_PRE_INIT,   /* Pre-initialization operation */
    NVDISP_SERDES_OP_POST_INIT,  /* Post-initialization operation (was INIT) */
    NVDISP_SERDES_OP_PRE_DEINIT, /* Pre-deinitialization operation */
    NVDISP_SERDES_OP_POST_DEINIT,/* Post-deinitialization operation (was DEINIT) */
    NVDISP_SERDES_OP_PRE_ERRB,   /* Pre-error handling operation */
    NVDISP_SERDES_OP_POST_ERRB   /* Post-error handling operation (was ERRB) */
};

/**
 * @brief Opcode sequence structure
 */
struct opcode_sequence {
    u8 *payload;
    int32_t length;
    char *name;
};

/**
 * @brief Private data structure for the NVDISP SERDES driver
 */
struct nvdisp_serdes_priv {
    struct i2c_client *client;
    struct mutex mutex;
    struct regmap *regmap;
    int serdes_errb;
    unsigned int ser_irq;
    int serdes_pwrdn;       /* GPIO number for power down pin */
    struct gpio_desc *gpiod_pwrdn;  /* GPIO descriptor for power pin */
    struct opcode_sequence init_seq;      /* Initialization sequence */
    struct opcode_sequence deinit_seq;    /* Deinitialization sequence */
    struct opcode_sequence errb_seq;      /* Error handling sequence */
    struct opcode_sequence devctl_seq;    /* Device control sequence */

	struct cdev cdev;
	dev_t devt;
	struct device *chardev;
	char instance_name[20];

    /* Vendor type string from device tree, this will activate the vendor specific callback */
    char *vendor_type;

    /* Vendor-specific data which can store the context of the vendor serdes driver */
    void *vendor_data;
};

/**
 * @brief Function prototypes for vendor extension callbacks
 */
int vendor_serdes_callback(struct nvdisp_serdes_priv *priv, enum nvdisp_serdes_op_type op_type);

/* Function prototypes for opcode sequence reading */
int read_opcode_props_sequence(struct i2c_client *client,
                             struct nvdisp_serdes_priv *priv,
                             struct device_node *display_serdes_config,
                             struct opcode_sequence *seq);

int32_t dispatch_opcode(struct i2c_client *client,
                       struct nvdisp_serdes_priv *priv,
                       u8 *payload,
                       int *curr_pos,
                       int32_t length);

int32_t nvdisp_serdes_read(struct nvdisp_serdes_priv *priv,
                          u8 slave_addr,
                          int reg,
                          u8 *reg_val);

int32_t nvdisp_serdes_write(struct nvdisp_serdes_priv *priv,
                          u8 slave_addr,
                          u32 reg,
                          u8 val);

int32_t nvdisp_serdes_update(struct nvdisp_serdes_priv *priv,
                            u8 slave_addr,
                            u32 reg,
                            u32 mask,
                            u8 val);

/**
 * @brief Execute I2C transfer operations
 * @param priv Driver private data
 * @param xfer Transfer request structure
 * @return 0 on success, negative error code on failure
 */
int32_t nvdisp_serdes_i2c_xfer(struct nvdisp_serdes_priv *priv,
				struct nvdisp_serdes_i2c_xfer *xfer);

/* IOCTL commands */
#define NVDISP_SERDES_MAGIC 'S'
#define NVDISP_SERDES_I2C_XFER _IOWR(NVDISP_SERDES_MAGIC, 3, struct nvdisp_serdes_i2c_xfer)

#endif /* __NVDISP_SERDES_H__ */
