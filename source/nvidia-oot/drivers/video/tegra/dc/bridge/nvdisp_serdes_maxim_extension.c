// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * NVDISP SERDES MAXIM EXTENSION
 */

#include "nvdisp_serdes_common.h"
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/workqueue.h>

/* Function prototype declaration */
int maxim_serdes_callback(struct nvdisp_serdes_priv *priv, enum nvdisp_serdes_op_type op_type);

/* Forward declarations for internal functions */
static int maxim_serdes_pre_init(struct nvdisp_serdes_priv *priv);
static int maxim_serdes_post_init(struct nvdisp_serdes_priv *priv);
static int maxim_serdes_pre_deinit(struct nvdisp_serdes_priv *priv);
static int maxim_serdes_post_deinit(struct nvdisp_serdes_priv *priv);
static int maxim_serdes_pre_errb(struct nvdisp_serdes_priv *priv);
static int maxim_serdes_post_errb(struct nvdisp_serdes_priv *priv);

/* Hardware-specific defines for Maxim SerDes */
#define MAXIM_SLAVE_ADDR          0x40    /* Example: Maxim SerDes I2C address */
#define MAXIM_LT_STATUS_REG       0x641A  /* Example: Maxim link training status register */
#define MAXIM_LT_STATUS_MASK      0xF0    /* Example: Mask for link training bit */
#define MAXIM_LT_COMPLETE_VALUE   0xF0    /* Example: Value indicating completed link training */

/* Add more Maxim-specific hardware defines as needed */

/**
 * @brief Maxim-specific context structure
 *
 * This structure contains Maxim-specific data that needs
 * to be maintained between callbacks.
 */
struct maxim_serdes_context {
    int initialized;
    struct opcode_sequence post_lt_init_seq;  /* Post link training init sequence */
    struct delayed_work post_lt_work;         /* Work for post link training sequence */
    struct nvdisp_serdes_priv *priv;          /* Store priv pointer for work function */

    /* Add Maxim-specific fields here */
    u8 chip_id;                               /* Maxim chip ID */
    u8 chip_rev;                              /* Maxim chip revision */
};

/**
 * @brief Work function to execute post link training sequence for Maxim
 *
 * @param work Pointer to the work structure
 */
static void maxim_serdes_post_lt_work(struct work_struct *work)
{
    struct maxim_serdes_context *ctx = container_of(to_delayed_work(work),
                                                   struct maxim_serdes_context,
                                                   post_lt_work);
    struct nvdisp_serdes_priv *priv = ctx->priv;
    struct device *dev = &priv->client->dev;
    int ret;
    u8 lt_status;

    /* Maxim-specific: Check if link training is completed */
    ret = nvdisp_serdes_read(priv, MAXIM_SLAVE_ADDR, MAXIM_LT_STATUS_REG, &lt_status);
    if (ret < 0) {
        dev_err(dev, "Failed to read Maxim LT status register\n");
        return;
    }

    /* Check LT completion bit using Maxim-specific bit patterns */
    if ((lt_status & MAXIM_LT_STATUS_MASK) != MAXIM_LT_COMPLETE_VALUE) {
        dev_info(dev, "Maxim link training not completed yet, status: 0x%x, rescheduling\n", lt_status);
        /* Reschedule work after 10ms */
        schedule_delayed_work(&ctx->post_lt_work, msecs_to_jiffies(10));
        return;
    }

    dev_info(dev, "Maxim link training completed, executing post-lt sequence\n");

    /* Execute post link training init sequence */
    if (ctx->post_lt_init_seq.length > 0) {
        int pos = 0;

        while (pos < ctx->post_lt_init_seq.length) {
            ret = dispatch_opcode(priv->client, priv, ctx->post_lt_init_seq.payload, 
                                  &pos, ctx->post_lt_init_seq.length);
            if (ret < 0) {
                dev_err(dev, "Maxim: dispatch_opcode failed (ret=%d) at pos=%d\n", ret, pos);
                return;
            }
        }
    }

    dev_info(dev, "Maxim post link training sequence executed successfully\n");
}

/**
 * @brief Main callback function for Maxim SerDes
 *
 * This function routes to the appropriate internal function based on operation type
 *
 * @param priv Pointer to the private data structure
 * @param op_type Operation type (PRE_INIT, POST_INIT, PRE_DEINIT, etc.)
 * @return int 0 on success, negative error code on failure
 */
int maxim_serdes_callback(struct nvdisp_serdes_priv *priv, enum nvdisp_serdes_op_type op_type)
{
    struct device *dev = &priv->client->dev;

    switch (op_type) {
    case NVDISP_SERDES_OP_PRE_INIT:
        dev_dbg(dev, "Executing Maxim SerDes pre-initialization\n");
        return maxim_serdes_pre_init(priv);

    case NVDISP_SERDES_OP_POST_INIT:
        dev_dbg(dev, "Executing Maxim SerDes post-initialization\n");
        return maxim_serdes_post_init(priv);

    case NVDISP_SERDES_OP_PRE_DEINIT:
        dev_dbg(dev, "Executing Maxim SerDes pre-deinitialization\n");
        return maxim_serdes_pre_deinit(priv);

    case NVDISP_SERDES_OP_POST_DEINIT:
        dev_dbg(dev, "Executing Maxim SerDes post-deinitialization\n");
        return maxim_serdes_post_deinit(priv);

    case NVDISP_SERDES_OP_PRE_ERRB:
        dev_dbg(dev, "Executing Maxim SerDes pre-error handling\n");
        return maxim_serdes_pre_errb(priv);

    case NVDISP_SERDES_OP_POST_ERRB:
        dev_dbg(dev, "Executing Maxim SerDes post-error handling\n");
        return maxim_serdes_post_errb(priv);

    default:
        dev_err(dev, "Unknown operation type for Maxim SerDes: %d\n", op_type);
        return -EINVAL;
    }
}

/**
 * @brief Maxim pre-initialization function
 *
 * @param priv Pointer to the private data structure
 * @return int 0 on success, negative error code on failure
 */
static int maxim_serdes_pre_init(struct nvdisp_serdes_priv *priv)
{
    struct device *dev = &priv->client->dev;

    dev_info(dev, "Executing Maxim SerDes pre-initialization\n");

    /* Add pre-initialization tasks here */

    return 0;
}

/**
 * @brief Maxim post-initialization function (was init)
 *
 * @param priv Pointer to the private data structure
 * @return int 0 on success, negative error code on failure
 */
static int maxim_serdes_post_init(struct nvdisp_serdes_priv *priv)
{
    struct device *dev = &priv->client->dev;
    struct maxim_serdes_context *ctx;
    struct device_node *ser = dev->of_node;
    struct device_node *display_serdes_config;
    int ret = 0;

    dev_info(dev, "Executing Maxim SerDes post-initialization\n");

    /* Allocate and initialize Maxim context */
    ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
    if (!ctx) {
        dev_err(dev, "Failed to allocate Maxim context\n");
        return -ENOMEM;
    }

    /* Store context in priv structure */
    priv->vendor_data = ctx;
    ctx->priv = priv;

    /* Get display-serdes-config node */
    display_serdes_config = of_find_node_by_name(ser, "display-serdes-config");
    if (!display_serdes_config) {
        dev_err(dev, "Cannot get display_serdes_config node\n");
        return -EINVAL;
    }

    /* Read Maxim-specific post link training init sequence */
    ctx->post_lt_init_seq.name = "post-lt-init-seq";
    ret = read_opcode_props_sequence(priv->client, priv, display_serdes_config, 
                                   &ctx->post_lt_init_seq);
    if (ret < 0) {
        dev_info(dev, "No post link training init sequence found for Maxim\n");
        ctx->post_lt_init_seq.length = 0;
    }

    /* Schedule work to execute post link training sequence */
    if (ctx->post_lt_init_seq.length > 0) {
        dev_info(dev, "Scheduling Maxim post link training sequence work\n");
        INIT_DELAYED_WORK(&ctx->post_lt_work, maxim_serdes_post_lt_work);
        schedule_delayed_work(&ctx->post_lt_work, msecs_to_jiffies(1));
    }

    return 0;
}

/**
 * @brief Maxim pre-deinitialization function
 *
 * @param priv Pointer to the private data structure
 * @return int 0 on success, negative error code on failure
 */
static int maxim_serdes_pre_deinit(struct nvdisp_serdes_priv *priv)
{
    struct device *dev = &priv->client->dev;

    dev_info(dev, "Executing Maxim SerDes pre-deinitialization\n");

    /* Add pre-deinitialization tasks here */

    return 0;
}

/**
 * @brief Maxim post-deinitialization function (was deinit)
 *
 * @param priv Pointer to the private data structure
 * @return int 0 on success, negative error code on failure
 */
static int maxim_serdes_post_deinit(struct nvdisp_serdes_priv *priv)
{
    struct maxim_serdes_context *ctx = priv->vendor_data;
    struct device *dev = &priv->client->dev;

    dev_info(dev, "Executing Maxim SerDes post-deinitialization\n");

    /* Clean up Maxim-specific data */
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
 * @brief Maxim pre-error handling function
 *
 * @param priv Pointer to the private data structure
 * @return int 0 on success, negative error code on failure
 */
static int maxim_serdes_pre_errb(struct nvdisp_serdes_priv *priv)
{
    struct device *dev = &priv->client->dev;

    dev_info(dev, "Executing Maxim SerDes pre-error handling\n");

    /* Add pre-error handling tasks here */

    return 0;
}

/**
 * @brief Maxim post-error handling function (was errb)
 *
 * @param priv Pointer to the private data structure
 * @return int 0 on success, negative error code on failure
 */
static int maxim_serdes_post_errb(struct nvdisp_serdes_priv *priv)
{
    struct device *dev = &priv->client->dev;

    dev_info(dev, "Executing Maxim SerDes post-error handling\n");

    /* Add post-error handling tasks here */

    return 0;
}
