// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * NVDISP SERDES VENDOR EXTENSION ROUTER
 */

#include "nvdisp_serdes_common.h"
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/string.h>

/* TI SerDes extension callback */
extern int ti_serdes_callback(struct nvdisp_serdes_priv *priv, enum nvdisp_serdes_op_type op_type);

/* Maxim SerDes extension callback */
extern int maxim_serdes_callback(struct nvdisp_serdes_priv *priv, enum nvdisp_serdes_op_type op_type);

/**
 * @brief Vendor callback - Routes to appropriate vendor implementation based on vendor_type
 *
 * @param priv Pointer to the private data structure
 * @param op_type Operation type (PRE_INIT, POST_INIT, PRE_DEINIT, etc.)
 * @return int 0 on success, negative error code on failure
 */
int vendor_serdes_callback(struct nvdisp_serdes_priv *priv, enum nvdisp_serdes_op_type op_type)
{
    struct device *dev = &priv->client->dev;
    const char *op_name;

    /* Get operation name for logging */
    switch (op_type) {
    case NVDISP_SERDES_OP_PRE_INIT:
        op_name = "pre-initialization";
        break;
    case NVDISP_SERDES_OP_POST_INIT:
        op_name = "post-initialization";
        break;
    case NVDISP_SERDES_OP_PRE_DEINIT:
        op_name = "pre-deinitialization";
        break;
    case NVDISP_SERDES_OP_POST_DEINIT:
        op_name = "post-deinitialization";
        break;
    case NVDISP_SERDES_OP_PRE_ERRB:
        op_name = "pre-error handling";
        break;
    case NVDISP_SERDES_OP_POST_ERRB:
        op_name = "post-error handling";
        break;
    default:
        dev_err(dev, "Unknown operation type: %d\n", op_type);
        return -EINVAL;
    }

    /* Check vendor type and route to appropriate implementation */
    if (priv->vendor_type != NULL) {
        if (strcmp(priv->vendor_type, "ti") == 0) {
            dev_info(dev, "Routing to TI SerDes %s\n", op_name);
            return ti_serdes_callback(priv, op_type);
        }
        if (strcmp(priv->vendor_type, "maxim") == 0) {
            dev_info(dev, "Routing to Maxim SerDes %s\n", op_name);
            return maxim_serdes_callback(priv, op_type);
        }
        /* Add more vendor checks here as needed */

        dev_err(dev, "Unsupported SerDes vendor type: %s\n", priv->vendor_type);
        return -EINVAL;
    }

    /* No vendor type specified, return success */
    dev_info(dev, "No SerDes vendor type specified, skipping vendor-specific %s\n", op_name);
    return 0;
}
