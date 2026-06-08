/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2020-2024 NVIDIA CORPORATION & AFFILIATES. All Rights Reserved. */

#ifndef _LINUX_TEGRA_IVC_BUS_H
#define _LINUX_TEGRA_IVC_BUS_H

#include <nvidia/conftest.h>

#include <soc/tegra/ivc-priv.h>
#include <linux/types.h>
#include <linux/tegra-hsp-combo.h>

extern struct bus_type tegra_ivc_bus_type;
extern struct device_type tegra_ivc_bus_dev_type;
struct tegra_ivc_bus;
struct tegra_ivc_rpc_data;

struct tegra_ivc_bus *tegra_ivc_bus_create(struct device *dev,
	struct camrtc_hsp *camhsp);

void tegra_ivc_bus_ready(struct tegra_ivc_bus *bus, bool online);
void tegra_ivc_bus_destroy(struct tegra_ivc_bus *bus);
int tegra_ivc_bus_boot_sync(struct tegra_ivc_bus *bus,
	int (*iovm_setup)(struct device*, dma_addr_t));
void tegra_ivc_bus_notify(struct tegra_ivc_bus *bus, u16 group);

struct tegra_ivc_driver {
	struct device_driver driver;
	struct device_type *dev_type;
	union {
		const struct tegra_ivc_channel_ops *channel;
	} ops;
};

#if defined(NV_BUS_TYPE_STRUCT_MATCH_HAS_CONST_DRV_ARG)
static inline struct tegra_ivc_driver *to_tegra_ivc_driver(const struct device_driver *drv)
#else
static inline struct tegra_ivc_driver *to_tegra_ivc_driver(struct device_driver *drv)
#endif
{
	if (drv == NULL)
		return NULL;
	return container_of(drv, struct tegra_ivc_driver, driver);
}

int tegra_ivc_driver_register(struct tegra_ivc_driver *drv);
void tegra_ivc_driver_unregister(struct tegra_ivc_driver *drv);
#define tegra_ivc_module_driver(drv) \
	module_driver(drv, tegra_ivc_driver_register, \
			tegra_ivc_driver_unregister)

#define tegra_ivc_subsys_driver(__driver, __register, __unregister, ...) \
static int __init __driver##_init(void) \
{ \
	return __register(&(__driver), ##__VA_ARGS__); \
} \
subsys_initcall_sync(__driver##_init);

#define tegra_ivc_subsys_driver_default(__driver) \
tegra_ivc_subsys_driver(__driver, \
						tegra_ivc_driver_register, \
						tegra_ivc_driver_unregister)

/* IVC channel driver support */
extern struct device_type tegra_ivc_channel_type;

struct tegra_ivc_channel {
	struct tegra_ivc ivc;
	struct device dev;
	const struct tegra_ivc_channel_ops __rcu *ops;
	struct tegra_ivc_channel *next;
	struct mutex ivc_wr_lock;
	struct tegra_ivc_rpc_data *rpc_priv;
	atomic_t bus_resets;
	u16 group;
	bool is_ready;
};

static inline bool tegra_ivc_channel_online_check(
		struct tegra_ivc_channel *chan)
{
	atomic_set(&chan->bus_resets, 0);

	smp_wmb();
	smp_rmb();

	return chan->is_ready;
}

static inline bool tegra_ivc_channel_has_been_reset(
		struct tegra_ivc_channel *chan)
{
	smp_rmb();
	return atomic_read(&chan->bus_resets) != 0;
}

static inline void *tegra_ivc_channel_get_drvdata(
		struct tegra_ivc_channel *chan)
{
	return dev_get_drvdata(&chan->dev);
}

static inline void tegra_ivc_channel_set_drvdata(
		struct tegra_ivc_channel *chan, void *data)
{
	dev_set_drvdata(&chan->dev, data);
}

static inline struct tegra_ivc_channel *to_tegra_ivc_channel(
		struct device *dev)
{
	return container_of(dev, struct tegra_ivc_channel, dev);
}

static inline struct device *tegra_ivc_channel_to_camrtc_dev(
	struct tegra_ivc_channel *ch)
{
	if (unlikely(ch == NULL))
		return NULL;

	BUG_ON(ch->dev.parent == NULL);
	BUG_ON(ch->dev.parent->parent == NULL);

	return ch->dev.parent->parent;
}

int tegra_ivc_channel_runtime_get(struct tegra_ivc_channel *chan);
void tegra_ivc_channel_runtime_put(struct tegra_ivc_channel *chan);

struct tegra_ivc_channel_ops {
	int (*probe)(struct tegra_ivc_channel *);
	void (*ready)(struct tegra_ivc_channel *, bool online);
	void (*remove)(struct tegra_ivc_channel *);
	void (*notify)(struct tegra_ivc_channel *);
};

/* Legacy mailbox support */
struct tegra_ivc_mbox_msg {
	int length;
	void *data;
};

#endif
