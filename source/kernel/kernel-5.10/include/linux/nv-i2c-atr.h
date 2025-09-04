
/* SPDX-License-Identifier: GPL-2.0 */
/*
 * drivers/i2c/i2c-atr.h -- I2C Address Translator
 *
 * Copyright (c) 2019 Luca Ceresoli <luca@lucaceresoli.net>
 *
 * Based on i2c-mux.h
 */

#ifndef _LINUX_nv_i2c_atr_H
#define _LINUX_nv_i2c_atr_H


#include <linux/i2c.h>
#include <linux/mutex.h>

struct nv_i2c_atr;

/**
 * struct nv_i2c_atr_ops - Callbacks from ATR to the device driver.
 * @select:        Ask the driver to select a child bus (optional)
 * @deselect:      Ask the driver to deselect a child bus (optional)
 * @attach_client: Notify the driver of a new device connected on a child
 *                 bus. The driver must choose an I2C alias, configure the
 *                 hardware to use it and return it in `alias_id`.
 * @detach_client: Notify the driver of a device getting disconnected. The
 *                 driver must configure the hardware to stop using the
 *                 alias.
 *
 * All these functions return 0 on success, a negative error code otherwise.
 */
struct nv_i2c_atr_ops {
	int  (*select)(struct nv_i2c_atr *atr, u32 chan_id);
	int  (*deselect)(struct nv_i2c_atr *atr, u32 chan_id);
	int  (*attach_client)(struct nv_i2c_atr *atr, u32 chan_id,
			      const struct i2c_board_info *info,
			      const struct i2c_client *client,
			      u16 *alias_id);
	void (*detach_client)(struct nv_i2c_atr *atr, u32 chan_id,
			      const struct i2c_client *client);
};

/*
 * Helper to add I2C ATR features to a device driver.
 */
struct nv_i2c_atr {
	/* private: internal use only */

	struct i2c_adapter *parent;
	struct device *dev;
	const struct nv_i2c_atr_ops *ops;

	void *priv;

	struct i2c_algorithm algo;
	struct mutex lock;
	int max_adapters;


	struct notifier_block i2c_nb;
	struct i2c_adapter *adapter[0];
};

struct nv_i2c_atr *nv_i2c_atr_new(struct i2c_adapter *parent, struct device *dev,
			    const struct nv_i2c_atr_ops *ops, int max_adapters);
void nv_i2c_atr_delete(struct nv_i2c_atr *atr);

static inline void nv_i2c_atr_set_clientdata(struct nv_i2c_atr *atr, void *data)
{
	atr->priv = data;
}

static inline void *nv_i2c_atr_get_clientdata(struct nv_i2c_atr *atr)
{
	return atr->priv;
}

int nv_i2c_atr_add_adapter(struct nv_i2c_atr *atr, u32 chan_id);
void nv_i2c_atr_del_adapter(struct nv_i2c_atr *atr, u32 chan_id);

#endif /* _LINUX_nv_i2c_atr_H */

