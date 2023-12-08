/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All Rights Reserved. */
/*
 * virtual_i2c_mux.c - virtual i2c mux driver for P3762 & P3783 GMSL boards.
 */

#include <nvidia/conftest.h>

#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/module.h>
#include <linux/mux/consumer.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/version.h>

#define DESER_A				(0)
#define ENABLE_IMU			(0xFE)
#define ENABLE_ALL_CC			(0xAA)
#define DESER_ADDR			(0x52)
#define DESER_CC_REG			(0x0003)

extern int max96712_write_reg_Dser(int slaveAddr,int channel,
                u16 addr, u8 val);
static int virtual_i2c_mux_select(struct i2c_mux_core *muxc, u32 chan)
{
	int ret = 0;

	/* Do select 1st channel, to access IMUs from 1st Hawk */
	if (!chan) {
		ret = max96712_write_reg_Dser(DESER_ADDR, DESER_A, DESER_CC_REG, ENABLE_IMU);
		if (ret)
			pr_err("%s: Failed to do i2c address trans for IMUs\n",__func__);

	}
	return ret;
}

static int virtual_i2c_mux_deselect(struct i2c_mux_core *muxc, u32 chan)
{
	int ret = 0;

	/* Enable all control channels */
	if (!chan) {
		ret = max96712_write_reg_Dser(DESER_ADDR, DESER_A, DESER_CC_REG, ENABLE_ALL_CC);
		if (ret)
			pr_err("%s: Failed to do i2c address trans for IMUs\n",__func__);
	}
	return ret;
}

#if defined(NV_I2C_DRIVER_STRUCT_PROBE_WITHOUT_I2C_DEVICE_ID_ARG) /* Linux 6.3 */
static int virtual_i2c_mux_probe(struct i2c_client *client)
#else
static int virtual_i2c_mux_probe(struct i2c_client *client,
                             const struct i2c_device_id *id)
#endif
{
	struct device *dev = &client->dev;
	struct device_node *np = dev->of_node;
	struct device_node *child;
	struct i2c_mux_core *muxc;
	struct i2c_adapter *parent;
	int children;
	int ret;
	u32 chan;

	dev_info(dev, "probing virtual i2c-mux.\n");
	if (!np)
		return -ENODEV;

	parent = client->adapter;
	if (IS_ERR(parent))
		return dev_err_probe(dev, PTR_ERR(parent),
				"failed to get i2c parent adapter\n");

	children = of_get_child_count(np);
	dev_info(dev, "No of children = %d\n",children);

	muxc = i2c_mux_alloc(parent, dev, children, 0, I2C_MUX_LOCKED,
			virtual_i2c_mux_select, virtual_i2c_mux_deselect);
	if (!muxc) {
		ret = -ENOMEM;
		goto err_parent;
	}
	i2c_set_clientdata(client, muxc);

	for (chan = 0; chan < children; chan++) {
		pr_info("%s:  chan = %d\n",__func__, chan);
		ret = i2c_mux_add_adapter(muxc, 0, chan, 0);
		if (ret)
			goto err_children;
	}

	dev_info(dev, "Probde is successful!!! \n");
	dev_info(dev, "%d-port mux on %s adapter\n", children, parent->name);

	return 0;

err_children:
	of_node_put(child);
	i2c_mux_del_adapters(muxc);
err_parent:
	i2c_put_adapter(parent);

	return ret;
}

#if (KERNEL_VERSION(6, 1, 0) > LINUX_VERSION_CODE)
static int virtual_i2c_mux_remove(struct i2c_client *client)
#else
static void virtual_i2c_mux_remove(struct i2c_client *client)
#endif
{
	struct i2c_mux_core *muxc = i2c_get_clientdata(client);

	i2c_mux_del_adapters(muxc);
	i2c_put_adapter(muxc->parent);
#if (KERNEL_VERSION(6, 1, 0) > LINUX_VERSION_CODE)
	return 0;
#endif
}

static const struct of_device_id virtual_i2c_mux_of_match[] = {
    { .compatible = "nvidia,virtual-i2c-mux", },
    {},
};
MODULE_DEVICE_TABLE(of, virtual_i2c_mux_of_match);

static const struct i2c_device_id virt_i2c_mux_id[] = {
    { "virtual-i2c-mux", 0 },
    { },
};
MODULE_DEVICE_TABLE(i2c, virt_i2c_mux_id);

static struct i2c_driver virtual_i2c_mux_driver = {
	.probe	= virtual_i2c_mux_probe,
	.remove	= virtual_i2c_mux_remove,
	.id_table = virt_i2c_mux_id,
	.driver	= {
		.name	= "virtual-i2c-mux",
        	.owner = THIS_MODULE,
        	.of_match_table = of_match_ptr(virtual_i2c_mux_of_match),
	},
};

module_i2c_driver(virtual_i2c_mux_driver);

MODULE_DESCRIPTION("Virtual I2C multiplexer driver");
MODULE_AUTHOR("Praveen AC <pac@nvidia.com>");
MODULE_LICENSE("GPL v2");
