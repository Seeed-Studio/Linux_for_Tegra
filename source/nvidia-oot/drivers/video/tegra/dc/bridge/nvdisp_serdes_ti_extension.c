// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * NVDISP SERDES TI EXTENSION
 */

#include "nvdisp_serdes_common.h"
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/workqueue.h>

/* Function prototype declaration */
int ti_serdes_callback(struct nvdisp_serdes_priv *priv, enum nvdisp_serdes_op_type op_type);

/* Forward declarations for internal functions */
static int ti_serdes_pre_init(struct nvdisp_serdes_priv *priv);
static int ti_serdes_post_init(struct nvdisp_serdes_priv *priv);
static int ti_serdes_pre_deinit(struct nvdisp_serdes_priv *priv);
static int ti_serdes_post_deinit(struct nvdisp_serdes_priv *priv);
static int ti_serdes_pre_errb(struct nvdisp_serdes_priv *priv);
static int ti_serdes_post_errb(struct nvdisp_serdes_priv *priv);

/* Hardware-specific defines - adapt to your specific SerDes part */
#define SERDES_SLAVE_ADDR          0x40
#define LINK_STATUS_REG            0x641A    /* Example: register containing link status */
#define LINK_TRAINED_MASK          0xF0    /* Example: bitmask for link trained status */
#define LINK_TRAINED_VALUE         0xF0    /* Example: value indicating trained link */

/**
 * @brief Helper function to execute an opcode sequence
 *
 * @param priv Pointer to the private data structure
 * @param seq Pointer to the opcode sequence to execute
 * @return int 0 on success, negative error code on failure
 */
static int execute_opcode_sequence(struct nvdisp_serdes_priv *priv, struct opcode_sequence *seq)
{
    struct device *dev = &priv->client->dev;
    int pos = 0;
    int ret;

    if (!seq || seq->length == 0 || !seq->payload) {
        dev_err(dev, "Invalid sequence to execute\n");
        return -EINVAL;
    }

    /* Execute all opcodes in the sequence */
    while (pos < seq->length) {
        ret = dispatch_opcode(priv->client, priv, seq->payload, &pos, seq->length);
        if (ret < 0) {
            dev_err(dev, "dispatch_opcode failed (ret=%d) at pos=%d\n", ret, pos);
            return ret;
        }
    }

    return 0;
}

/**
 * @brief TI-specific context structure
 *
 * This structure contains all the TI-specific data
 * that needs to be maintained between callbacks.
 */
struct ti_serdes_context {
    /* Add your TI-specific fields here */
    int initialized;
    struct opcode_sequence post_lt_init_seq;  /* Post link training init sequence */
    struct delayed_work post_lt_work;         /* Work for post link training sequence */
    struct nvdisp_serdes_priv *priv;          /* Store priv pointer for work function */
    /* Add more fields as needed */
};

/**
 * @brief Work function to execute post link training sequence
 * 
 * @param work Pointer to the work structure
 */
static void ti_serdes_post_lt_work(struct work_struct *work)
{
    struct ti_serdes_context *ctx = container_of(to_delayed_work(work), 
                                                 struct ti_serdes_context, 
                                                 post_lt_work);
    struct nvdisp_serdes_priv *priv = ctx->priv;
    struct device *dev = &priv->client->dev;
    int ret;
    u8 lt_status;

    /* Check if link training is completed by reading LT status register */
    ret = nvdisp_serdes_read(priv, SERDES_SLAVE_ADDR, LINK_STATUS_REG, &lt_status);
    if (ret < 0) {
        dev_err(dev, "Failed to read LT status register\n");
        return;
    }

    /* Check LT completion bit (bit 4:7) */
    if (!(lt_status & LINK_TRAINED_VALUE)) {
        dev_info(dev, "Link training not completed yet, status: 0x%x, rescheduling work\n", lt_status);
        /* Reschedule work after 10ms */
        schedule_delayed_work(&ctx->post_lt_work, msecs_to_jiffies(10));
        return;
    }

    dev_info(dev, "Link training completed, executing post-lt sequence\n");

    /* Execute post link training init sequence */
    ret = execute_opcode_sequence(priv, &ctx->post_lt_init_seq);
    if (ret < 0) {
        dev_err(dev, "Failed to execute post link training init sequence\n");
        return;
    }

    dev_info(dev, "Post link training sequence executed successfully\n");
}

/**
 * @brief Main callback function for TI SerDes
 *
 * This function routes to the appropriate internal function based on operation type
 *
 * @param priv Pointer to the private data structure
 * @param op_type Operation type (PRE_INIT, POST_INIT, PRE_DEINIT, etc.)
 * @return int 0 on success, negative error code on failure
 */
int ti_serdes_callback(struct nvdisp_serdes_priv *priv, enum nvdisp_serdes_op_type op_type)
{
    struct device *dev = &priv->client->dev;

    switch (op_type) {
    case NVDISP_SERDES_OP_PRE_INIT:
        dev_dbg(dev, "Executing TI SerDes pre-initialization\n");
        return ti_serdes_pre_init(priv);

    case NVDISP_SERDES_OP_POST_INIT:
        dev_dbg(dev, "Executing TI SerDes post-initialization\n");
        return ti_serdes_post_init(priv);

    case NVDISP_SERDES_OP_PRE_DEINIT:
        dev_dbg(dev, "Executing TI SerDes pre-deinitialization\n");
        return ti_serdes_pre_deinit(priv);

    case NVDISP_SERDES_OP_POST_DEINIT:
        dev_dbg(dev, "Executing TI SerDes post-deinitialization\n");
        return ti_serdes_post_deinit(priv);

    case NVDISP_SERDES_OP_PRE_ERRB:
        dev_dbg(dev, "Executing TI SerDes pre-error handling\n");
        return ti_serdes_pre_errb(priv);

    case NVDISP_SERDES_OP_POST_ERRB:
        dev_dbg(dev, "Executing TI SerDes post-error handling\n");
        return ti_serdes_post_errb(priv);

    default:
        dev_err(dev, "Unknown operation type for TI SerDes: %d\n", op_type);
        return -EINVAL;
    }
}

/**
 * @brief TI pre-initialization function
 *
 * @param priv Pointer to the private data structure
 * @return int 0 on success, negative error code on failure
 */
static int ti_serdes_pre_init(struct nvdisp_serdes_priv *priv)
{
    struct device *dev = &priv->client->dev;

    dev_info(dev, "Executing TI SerDes pre-initialization\n");

    /* Add pre-initialization tasks here */

    return 0;
}

/**
 * @brief TI post-initialization function (was init)
 *
 * @param priv Pointer to the private data structure
 * @return int 0 on success, negative error code on failure
 */
static int ti_serdes_post_init(struct nvdisp_serdes_priv *priv)
{
    struct device *dev = &priv->client->dev;
    struct ti_serdes_context *ctx;
    struct device_node *ser = dev->of_node;
    struct device_node *display_serdes_config;
    int ret = 0;

    dev_info(dev, "Executing TI SerDes post-initialization\n");

    /* Allocate and initialize TI context */
    ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
    if (!ctx) {
        dev_err(dev, "Failed to allocate TI context\n");
        return -ENOMEM;
    }

    /* Store context in priv structure */
    priv->vendor_data = ctx;
    ctx->priv = priv;  /* Store priv pointer for work function */

    /* Get display-serdes-config node */
    display_serdes_config = of_find_node_by_name(ser, "display-serdes-config");
    if (!display_serdes_config) {
        dev_err(dev, "Cannot get display_serdes_config node\n");
        return -EINVAL;
    }

    /* Read post link training init sequence only once */
    if (ctx->post_lt_init_seq.length == 0) {
        ctx->post_lt_init_seq.name = "post-lt-init-seq";
        ret = read_opcode_props_sequence(priv->client, priv, display_serdes_config, &ctx->post_lt_init_seq);
        if (ret < 0) {
            dev_info(dev, "No post link training init sequence found\n");
            ctx->post_lt_init_seq.length = 0;
        }
    }

    /* Schedule work to execute post link training sequence */
    if (ctx->post_lt_init_seq.length > 0) {
        dev_info(dev, "Scheduling post link training sequence work\n");
        INIT_DELAYED_WORK(&ctx->post_lt_work, ti_serdes_post_lt_work);
        /* Reschedule work after 1ms */
        schedule_delayed_work(&ctx->post_lt_work, msecs_to_jiffies(1));
    }

    return 0;
}

/**
 * @brief TI pre-deinitialization function
 *
 * @param priv Pointer to the private data structure
 * @return int 0 on success, negative error code on failure
 */
static int ti_serdes_pre_deinit(struct nvdisp_serdes_priv *priv)
{
    struct device *dev = &priv->client->dev;

    dev_info(dev, "Executing TI SerDes pre-deinitialization\n");

    /* Add pre-deinitialization tasks here */

    return 0;
}

/**
 * @brief TI post-deinitialization function (was deinit)
 *
 * @param priv Pointer to the private data structure
 * @return int 0 on success, negative error code on failure
 */
static int ti_serdes_post_deinit(struct nvdisp_serdes_priv *priv)
{
    struct ti_serdes_context *ctx = priv->vendor_data;
    struct device *dev = &priv->client->dev;

    dev_info(dev, "Executing TI SerDes post-deinitialization\n");

    /* Clean up TI-specific data */
    if (ctx) {
        /* Cancel any pending work */
        cancel_delayed_work_sync(&ctx->post_lt_work);

        /* Free the allocated memory */
        kfree(ctx);
        priv->vendor_data = NULL;
    }

    return 0;
}

/**
 * @brief TI pre-error handling function
 *
 * @param priv Pointer to the private data structure
 * @return int 0 on success, negative error code on failure
 */
static int ti_serdes_pre_errb(struct nvdisp_serdes_priv *priv)
{
    struct device *dev = &priv->client->dev;

    dev_info(dev, "Executing TI SerDes pre-error handling\n");

    /* Add pre-error handling tasks here */

    return 0;
}

/**
 * @brief TI post-error handling function (was errb)
 *
 * @param priv Pointer to the private data structure
 * @return int 0 on success, negative error code on failure
 */
static int ti_serdes_post_errb(struct nvdisp_serdes_priv *priv)
{
    struct device *dev = &priv->client->dev;

    dev_info(dev, "Executing TI SerDes post-error handling\n");

    /* Add post-error handling tasks here */

    return 0;
}
