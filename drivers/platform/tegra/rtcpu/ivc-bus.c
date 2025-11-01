// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#if defined(NV_TEGRA_IVC_USE_IVC_EXT_DRIVER)
#include <soc/tegra/ivc_ext.h>
#else
#include <soc/tegra/ivc-priv.h>
#endif
#include <linux/tegra-ivc-bus.h>
#include <linux/bitops.h>
#include <linux/version.h>
#include "soc/tegra/camrtc-channels.h"

#if defined(NV_TEGRA_IVC_STRUCT_HAS_IOSYS_MAP)
#include <linux/iosys-map.h>
#endif

#define NV(p) "nvidia," #p

#define CAMRTC_IVC_CONFIG_SIZE	4096

struct tegra_ivc_region {
	uintptr_t base;
	size_t size;
	dma_addr_t iova;
	size_t config_size;
	size_t ivc_size;
};

struct tegra_ivc_bus {
	struct device dev;
	struct tegra_ivc_channel *chans;
	unsigned num_regions;
	struct tegra_ivc_region regions[];
};

/**
 * @brief Notification callback for IVC channel ring events
 *
 * This function is called when an IVC ring event occurs. It retrieves the channel
 * context and notifies the HSP group.
 * - Gets the channel context using @ref container_of()
 * - Notifies the HSP group using @ref camrtc_hsp_group_ring()
 *
 * @param[in] ivc   Pointer to the IVC channel
 *                  Valid value: non-NULL
 * @param[in] data  Pointer to the HSP context
 *                  Valid value: non-NULL
 */
static void tegra_ivc_channel_ring(struct tegra_ivc *ivc, void *data)
{
	struct tegra_ivc_channel *chan =
		container_of(ivc, struct tegra_ivc_channel, ivc);
	struct camrtc_hsp *camhsp = (struct camrtc_hsp *) data;

	camrtc_hsp_group_ring(camhsp, chan->group);
}

struct device_type tegra_ivc_channel_type = {
	.name = "tegra-ivc-channel",
};
EXPORT_SYMBOL(tegra_ivc_channel_type);

/**
 * @brief Gets a runtime PM reference for an IVC channel
 *
 * This function gets a runtime PM reference for the specified IVC channel.
 * It ensures the channel is powered on and ready for use.
 * - Validates the channel pointer using @ref BUG_ON()
 * - Gets a runtime PM reference using @ref pm_runtime_get_sync()
 *
 * @param[in] ch  Pointer to the IVC channel
 *                Valid value: non-NULL
 *
 * @retval (int)  return value from @ref pm_runtime_get_sync()
 */
int tegra_ivc_channel_runtime_get(struct tegra_ivc_channel *ch)
{
	BUG_ON(ch == NULL);

	return pm_runtime_get_sync(&ch->dev);
}
EXPORT_SYMBOL(tegra_ivc_channel_runtime_get);

/**
 * @brief Releases a runtime PM reference for an IVC channel
 *
 * This function releases a runtime PM reference for the specified IVC channel.
 * It allows the channel to be powered down when not in use.
 * - Validates the channel pointer using @ref BUG_ON()
 * - Releases the runtime PM reference using @ref pm_runtime_put()
 *
 * @param[in] ch  Pointer to the IVC channel
 *                Valid value: non-NULL
 */
void tegra_ivc_channel_runtime_put(struct tegra_ivc_channel *ch)
{
	BUG_ON(ch == NULL);

	pm_runtime_put(&ch->dev);
}
EXPORT_SYMBOL(tegra_ivc_channel_runtime_put);

/**
 * @brief Releases resources associated with an IVC channel device
 *
 * This function is called when an IVC channel device is being destroyed.
 * It performs cleanup of allocated resources.
 * - Gets the channel context using @ref container_of()
 * - Releases the device node using @ref of_node_put()
 * - Frees the channel memory using @ref kfree()
 *
 * @param[in] dev  Pointer to the device being released
 *                 Valid value: non-NULL
 */
static void tegra_ivc_channel_release(struct device *dev)
{
	struct tegra_ivc_channel *chan =
		container_of(dev, struct tegra_ivc_channel, dev);

	of_node_put(dev->of_node);
	kfree(chan);
}

/**
 * @brief Creates and initializes a new IVC channel
 *
 * This function creates and initializes a new IVC channel with the specified parameters.
 * It performs the following operations:
 * - Validates the bus, channel node, region, and camhsp pointers
 * - Sets the device name using @ref dev_set_name()
 * - Initializes device properties and runtime PM using @ref pm_runtime_no_callbacks() and @ref pm_runtime_enable()
 * - Reads channel configuration from device tree using @ref of_property_read_string(), @ref of_property_read_u32(), and @ref fls()
 * - Check for overflows in size calculations using @ref __builtin_add_overflow() and @ref __builtin_mul_overflow()
 * - Calculates total queue size using @ref tegra_ivc_total_queue_size()
 * - Checks if buffers exceed IVC region using @ref __builtin_add_overflow()
 * - Initializes IVC communication using @ref tegra_ivc_init()
 * - Reset IVC communication using @ref tegra_ivc_reset()
 * - Add device using @ref device_add()
 *
 * @param[in] bus       Pointer to the IVC bus
 *                      Valid value: non-NULL
 * @param[in] ch_node   Pointer to the device tree node
 *                      Valid value: non-NULL
 * @param[in] region    Pointer to the IVC region
 *                      Valid value: non-NULL
 * @param[in] camhsp    Pointer to the camera HSP context
 *                      Valid value: non-NULL
 *
 * @retval (struct tegra_ivc_channel *) Pointer to the created IVC channel on success
 * @retval ERR_PTR(-ENOMEM) If memory allocation fails
 * @retval ERR_PTR(-ENOSPC) If buffers exceed IVC region
 * @retval ERR_PTR(-EOVERFLOW) If size calculations overflow
 * @retval ERR_PTR(-EIO) If IVC initialization fails
 */
static struct tegra_ivc_channel *tegra_ivc_channel_create(
		struct tegra_ivc_bus *bus, struct device_node *ch_node,
		struct tegra_ivc_region *region,
		struct camrtc_hsp *camhsp)
{
	struct camrtc_tlv_ivc_setup *tlv;
#if defined(NV_TEGRA_IVC_STRUCT_HAS_IOSYS_MAP)
	struct iosys_map rx_map, tx_map;
#endif
	struct {
		u32 rx;
		u32 tx;
	} start, end;
	u32 version, channel_group, nframes, frame_size, queue_size;
	const char *service;
	int ret;
	u32 mul = 0U, sum = 0U;
	dma_addr_t rx_phys = 0U, tx_phys = 0U;

	struct tegra_ivc_channel *chan = kzalloc(sizeof(*chan), GFP_KERNEL);
	if (unlikely(chan == NULL))
		return ERR_PTR(-ENOMEM);

	chan->dev.parent = &bus->dev;
	chan->dev.type = &tegra_ivc_channel_type;
	chan->dev.bus = &tegra_ivc_bus_type;
	chan->dev.of_node = of_node_get(ch_node);
	chan->dev.release = tegra_ivc_channel_release;
	dev_set_name(&chan->dev, "%s:%s", dev_name(&bus->dev),
			kbasename(ch_node->full_name));
	device_initialize(&chan->dev);
	pm_runtime_no_callbacks(&chan->dev);
	pm_runtime_enable(&chan->dev);

	ret = of_property_read_string(ch_node, NV(service), &service);
	if (ret) {
		dev_err(&chan->dev, "missing <%s> property\n",
			NV(service));
		goto error;
	}

	ret = of_property_read_u32(ch_node, NV(version), &version);
	if (ret)
		version = 0;

	ret = of_property_read_u32(ch_node, NV(group), &channel_group);
	if (ret) {
		dev_err(&chan->dev, "missing <%s> property\n", NV(group));
		goto error;
	}

	/* We have 15 channel group bits available */
	if ((channel_group & 0x7FFFU) != channel_group) {
		dev_err(&chan->dev, "invalid property %s = 0x%x\n",
			NV(group), channel_group);
		goto error;
	}

	ret = of_property_read_u32(ch_node, NV(frame-count), &nframes);
	if (ret || !nframes) {
		dev_err(&chan->dev, "missing <%s> property\n",
			NV(frame-count));
		goto error;
	}
	nframes = 1 << fls(nframes - 1); /* Round up to a power of two */

	ret = of_property_read_u32(ch_node, NV(frame-size), &frame_size);
	if (ret || !frame_size) {
		dev_err(&chan->dev, "missing <%s> property\n", NV(frame-size));
		goto error;
	}

	if (__builtin_add_overflow(region->config_size, sizeof(*tlv), &sum)) {
		dev_err(&chan->dev, "IVC config size overflow\n");
		ret = -EOVERFLOW;
		goto error;
	}

	if (sum > CAMRTC_IVC_CONFIG_SIZE) {
		dev_err(&chan->dev, "IVC config size exceeded\n");
		ret = -ENOSPC;
		goto error;
	}

	if (__builtin_mul_overflow(nframes, frame_size, &mul)) {
		dev_err(&chan->dev, "IVC frame size overflow\n");
		ret = -EOVERFLOW;
		goto error;
	}
	queue_size = tegra_ivc_total_queue_size(mul);

	if (__builtin_mul_overflow(2U, queue_size, &mul)) {
		dev_err(&chan->dev, "IVC queue size overflow\n");
		ret = -EOVERFLOW;
		goto error;
	}

	if (__builtin_add_overflow(region->ivc_size, mul, &sum)) {
		dev_err(&chan->dev, "IVC size overflow\n");
		ret = -EOVERFLOW;
		goto error;
	}

	if (sum > region->size) {
		dev_err(&chan->dev, "buffers exceed IVC region\n");
		ret = -ENOSPC;
		goto error;
	}

	start.rx = region->ivc_size;
	region->ivc_size += queue_size;
	end.rx = region->ivc_size;

	start.tx = end.rx;
	region->ivc_size += queue_size;
	end.tx = region->ivc_size;

	if (__builtin_add_overflow(region->iova, start.rx, &rx_phys)) {
		dev_err(&chan->dev, "rx phys overflow\n");
		ret = -EOVERFLOW;
		goto error;
	}

	if (__builtin_add_overflow(region->iova, start.tx, &tx_phys)) {
		dev_err(&chan->dev, "tx phys overflow\n");
		ret = -EOVERFLOW;
		goto error;
	}

#if defined(NV_TEGRA_IVC_STRUCT_HAS_IOSYS_MAP)
	iosys_map_set_vaddr(&rx_map, (void *)(region->base + start.rx));
	iosys_map_set_vaddr(&tx_map, (void *)(region->base + start.tx));

	/* Init IVC */
	ret = tegra_ivc_init(&chan->ivc,
			NULL,
			&rx_map,
			rx_phys,
			&tx_map,
			tx_phys,
			nframes, frame_size,
			tegra_ivc_channel_ring,
			(void *)camhsp);
#else
	/* Init IVC */
	ret = tegra_ivc_init(&chan->ivc,
			NULL,
			(void *)(region->base + start.rx),
			rx_phys,
			(void *)(region->base + start.tx),
			tx_phys,
			nframes, frame_size,
			tegra_ivc_channel_ring,
			(void *)camhsp);
#endif
	if (ret) {
		dev_err(&chan->dev, "IVC initialization error: %d\n", ret);
		goto error;
	}

	chan->group = channel_group;

#if defined(NV_TEGRA_IVC_USE_IVC_EXT_DRIVER)
	tegra_ivc_channel_reset(&chan->ivc);
#else
	tegra_ivc_reset(&chan->ivc);
#endif

	/* Fill channel descriptor */
	tlv = (struct camrtc_tlv_ivc_setup *)
		(region->base + region->config_size);

	tlv->tag = CAMRTC_TAG_IVC_SETUP;
	tlv->len = sizeof(*tlv);
	if (__builtin_add_overflow(region->iova, start.rx, &tlv->rx_iova)) {
		dev_err(&chan->dev, "IVC setup overflow\n");
		ret = -EOVERFLOW;
		goto error;
	}

	tlv->rx_frame_size = frame_size;
	tlv->rx_nframes = nframes;
	if (__builtin_add_overflow(region->iova, start.tx, &tlv->tx_iova)) {
		dev_err(&chan->dev, "IVC setup overflow\n");
		ret = -EOVERFLOW;
		goto error;
	}

	tlv->tx_frame_size = frame_size;
	tlv->tx_nframes = nframes;
	tlv->channel_group = channel_group;
	tlv->ivc_version = version;
	if (strscpy(tlv->ivc_service, service, sizeof(tlv->ivc_service)) < 0)
		dev_warn(&chan->dev, "service name <%s> too long\n", service);

	region->config_size += sizeof(*tlv);
	(++tlv)->tag = 0; /* terminator */

	dev_info(&chan->dev,
		"%s: ver=%u grp=%u RX[%ux%u]=0x%x-0x%x TX[%ux%u]=0x%x-0x%x\n",
		ch_node->name, version, channel_group,
		nframes, frame_size, start.rx, end.rx,
		nframes, frame_size, start.tx, end.tx);

	ret = device_add(&chan->dev);
	if (ret) {
		dev_err(&chan->dev, "channel device error: %d\n", ret);
		goto error;
	}

	return chan;
error:
	put_device(&chan->dev);
	return ERR_PTR(ret);
}

/**
 * @brief Notifies a channel of an IVC event
 *
 * This function handles notification of IVC events for a specific channel.
 * It performs the following operations:
 * - Checks if the channel has been notified using @ref tegra_ivc_channel_notified() or @ref tegra_ivc_notified()
 * - Verifies if the channel is ready using @ref chan->is_ready
 * - Uses RCU locking to safely access channel operations using @ref rcu_read_lock() and @ref rcu_dereference()
 * - Calls the channel's notify callback if available using @ref ops->notify()
 * - Unlocks the RCU using @ref rcu_read_unlock()
 *
 * @param[in] chan  Pointer to the IVC channel
 *                  Valid value: non-NULL
 */
static void tegra_ivc_channel_notify(struct tegra_ivc_channel *chan)
{
	const struct tegra_ivc_channel_ops *ops;

#if defined(NV_TEGRA_IVC_USE_IVC_EXT_DRIVER)
	if (tegra_ivc_channel_notified(&chan->ivc) != 0)
#else
	if (tegra_ivc_notified(&chan->ivc) != 0)
#endif
		return;

	if (!chan->is_ready)
		return;

	rcu_read_lock();
	ops = rcu_dereference(chan->ops);

	if (ops != NULL && ops->notify != NULL)
		ops->notify(chan);
	rcu_read_unlock();
}

/**
 * @brief Notifies all channels in a group of an IVC event
 *
 * This function iterates through all channels in the IVC bus and notifies
 * those belonging to the specified group of an IVC event.
 * - Iterates through all channels using a linked list
 * - For each channel in the specified group, calls @ref tegra_ivc_channel_notify()
 *
 * @param[in] bus    Pointer to the IVC bus
 *                   Valid value: non-NULL
 * @param[in] group  Group identifier to notify
 *                   Valid value: any 16-bit value
 */
void tegra_ivc_bus_notify(struct tegra_ivc_bus *bus, u16 group)
{
	struct tegra_ivc_channel *chan;

	for (chan = bus->chans; chan != NULL; chan = chan->next) {
		if ((chan->group & group) != 0)
			tegra_ivc_channel_notify(chan);
	}
}
EXPORT_SYMBOL(tegra_ivc_bus_notify);

struct device_type tegra_ivc_bus_dev_type = {
	.name = "tegra-ivc-bus",
};
EXPORT_SYMBOL(tegra_ivc_bus_dev_type);

/**
 * @brief Releases resources associated with an IVC bus device
 *
 * This function is called when an IVC bus device is being destroyed.
 * It performs cleanup of allocated resources.
 * - Gets the bus context using @ref container_of()
 * - Releases the device node using @ref of_node_put()
 * - Frees DMA memory for each region using @ref dma_free_coherent()
 * - Frees the bus memory using @ref kfree()
 *
 * @param[in] dev  Pointer to the device being released
 *                 Valid value: non-NULL
 */
static void tegra_ivc_bus_release(struct device *dev)
{
	struct tegra_ivc_bus *bus =
		container_of(dev, struct tegra_ivc_bus, dev);
	int i;

	of_node_put(dev->of_node);

	for (i = 0; i < bus->num_regions; i++) {
		if (!bus->regions[i].base)
			continue;

		dma_free_coherent(dev->parent, bus->regions[i].size,
				(void *)bus->regions[i].base,
				bus->regions[i].iova);
	}

	kfree(bus);
}

/**
 * @brief Matches an IVC bus device with a driver
 *
 * This function determines if a driver can handle a specific IVC bus device.
 * It performs the following operations:
 * - Gets the IVC driver using @ref to_tegra_ivc_driver()
 * - Checks if device type matches driver type
 * - Uses @ref of_driver_match_device() for device tree matching
 *
 * @param[in] dev  Pointer to the device to match
 *                 Valid value: non-NULL
 * @param[in] drv  Pointer to the driver to match
 *                 Valid value: non-NULL
 *
 * @retval 1  If the driver matches the device
 * @retval 0  If the driver does not match the device
 */
#if defined(NV_BUS_TYPE_STRUCT_MATCH_HAS_CONST_DRV_ARG)
static int tegra_ivc_bus_match(struct device *dev, const struct device_driver *drv)
#else
static int tegra_ivc_bus_match(struct device *dev, struct device_driver *drv)
#endif
{
	struct tegra_ivc_driver *ivcdrv = to_tegra_ivc_driver(drv);

	if (dev->type != ivcdrv->dev_type)
		return 0;
	return of_driver_match_device(dev, drv);
}

/**
 * @brief Stops and cleans up all channels in an IVC bus
 *
 * This function stops all IVC channels associated with a bus and releases their resources.
 * It performs the following operations:
 * - Iterates through all channels in the linked list
 * - Disables runtime PM for each channel using @ref pm_runtime_disable()
 * - Unregisters each channel device using @ref device_unregister()
 * - Updates the linked list as channels are removed
 *
 * @param[in] bus  Pointer to the IVC bus
 *                 Valid value: non-NULL
 */
static void tegra_ivc_bus_stop(struct tegra_ivc_bus *bus)
{
	while (bus->chans != NULL) {
		struct tegra_ivc_channel *chan = bus->chans;

		bus->chans = chan->next;
		pm_runtime_disable(&chan->dev);
		device_unregister(&chan->dev);
	}
}

/**
 * @brief Starts all channels in an IVC bus
 *
 * This function initializes and starts all IVC channels associated with a bus.
 * It performs the following operations:
 * - Iterates through device tree nodes to find channel specifications
 * - Checks if each channel is enabled in the device tree
 * - Creates each channel using @ref tegra_ivc_channel_create()
 * - Adds each channel to the linked list of channels in the bus
 * - Cleans up on error using @ref tegra_ivc_bus_stop()
 *
 * @param[in] bus     Pointer to the IVC bus
 *                    Valid value: non-NULL
 * @param[in] camhsp  Pointer to the camera HSP context
 *                    Valid value: non-NULL
 *
 * @retval 0        On successful startup
 * @retval (int)   Error code from @ref tegra_ivc_channel_create()
 */
static int tegra_ivc_bus_start(struct tegra_ivc_bus *bus,
	struct camrtc_hsp *camhsp)
{
	struct device_node *dn = bus->dev.parent->of_node;
	struct of_phandle_args reg_spec;
	const char *status;
	int i, ret;

	for (i = 0;
		of_parse_phandle_with_fixed_args(dn, NV(ivc-channels), 3,
							i, &reg_spec) == 0;
		i++) {
		struct device_node *ch_node;

		for_each_child_of_node(reg_spec.np, ch_node) {
			struct tegra_ivc_channel *chan;

			ret = of_property_read_string(ch_node,
					"status", &status);

			if (ret == 0) {
				ret = strcmp(status, "disabled");

				if (ret == 0)
					continue;
			}

			chan = tegra_ivc_channel_create(bus, ch_node,
							&bus->regions[i],
							camhsp);
			if (IS_ERR(chan)) {
				ret = PTR_ERR(chan);
				of_node_put(ch_node);
				goto error;
			}

			chan->next = bus->chans;
			bus->chans = chan;
		}
	}

	return 0;
error:
	tegra_ivc_bus_stop(bus);
	return ret;
}

/**
 * @brief Synchronizes IVC bus during RTCPU boot or PM resume
 *
 * This function is called during RTCPU boot to synchronize or re-synchronize
 * the IVC bus in the case of PM resume. It sets up the IOVM for each
 * IVC region.
 * - Checks if the bus is valid using @ref IS_ERR_OR_NULL()
 * - Iterates through all regions in the bus
 * - Calls the provided IOVM setup function for each region
 *
 * @param[in] bus         Pointer to the IVC bus
 *                        Valid value: any value including NULL or error pointer
 * @param[in] iovm_setup  Function pointer to set up IOVM mappings
 *                        Valid value: non-NULL function pointer
 *
 * @retval 0      On successful synchronization or if bus is NULL/error
 * @retval -EIO   If IOVM setup fails
 */
int tegra_ivc_bus_boot_sync(struct tegra_ivc_bus *bus,
	int (*iovm_setup)(struct device*, dma_addr_t))
{
	int i;

	if (IS_ERR_OR_NULL(bus))
		return 0;

	for (i = 0; i < bus->num_regions; i++) {
		int ret = (*iovm_setup)(bus->dev.parent,
				bus->regions[i].iova);
		if (ret != 0) {
			dev_info(&bus->dev, "IOVM setup error: %d\n", ret);
			return -EIO;
		}
	}

	return 0;
}
EXPORT_SYMBOL(tegra_ivc_bus_boot_sync);

/**
 * @brief Probes an IVC bus device
 *
 * This function is called when a device is added to the IVC bus.
 * It initializes the device and calls the driver's probe function.
 * Currently, it only supports IVC channel devices.
 * - Checks if the device is an IVC channel using its type
 * - Gets the driver and channel from the device
 * - Initializes the channel's mutex
 * - Calls the driver's probe function if available
 * - Sets up the RCU-protected operations pointer
 *
 * @param[in] dev  Pointer to the device to probe
 *                 Valid value: non-NULL
 *
 * @retval 0        On successful probe
 * @retval -ENXIO   If the device is not supported
 * @retval (int)    Error code from driver's probe function
 */
static int tegra_ivc_bus_probe(struct device *dev)
{
	int ret = -ENXIO;

	if (dev->type == &tegra_ivc_channel_type) {
		struct tegra_ivc_driver *drv = to_tegra_ivc_driver(dev->driver);
		struct tegra_ivc_channel *chan = to_tegra_ivc_channel(dev);
		const struct tegra_ivc_channel_ops *ops = drv->ops.channel;

		mutex_init(&chan->ivc_wr_lock);

		BUG_ON(ops == NULL);
		if (ops->probe != NULL) {
			ret = ops->probe(chan);
			if (ret)
				return ret;
		}

		rcu_assign_pointer(chan->ops, ops);
		ret = 0;

	}

	return ret;
}

/**
 * @brief Removes an IVC bus device
 *
 * This function is called when a device is removed from the IVC bus.
 * It cleans up the device and calls the driver's remove function.
 * Currently, it only supports IVC channel devices.
 * - Checks if the device is an IVC channel using its type
 * - Gets the driver and channel from the device
 * - Safely removes the RCU-protected operations pointer
 * - Calls the driver's remove function if available
 *
 * @param[in] dev  Pointer to the device to remove
 *                 Valid value: non-NULL
 *
 * @retval 0  On successful removal (for Linux v5.15+ only)
 */
#if defined(NV_BUS_TYPE_STRUCT_REMOVE_HAS_INT_RETURN_TYPE) /* Linux v5.15 */
static int tegra_ivc_bus_remove(struct device *dev)
#else
static void tegra_ivc_bus_remove(struct device *dev)
#endif
{
	if (dev->type == &tegra_ivc_channel_type) {
		struct tegra_ivc_driver *drv = to_tegra_ivc_driver(dev->driver);
		struct tegra_ivc_channel *chan = to_tegra_ivc_channel(dev);
		const struct tegra_ivc_channel_ops *ops = drv->ops.channel;

		if (rcu_access_pointer(chan->ops) != ops)
			dev_warn(dev, "dev ops does not match!\n");
		RCU_INIT_POINTER(chan->ops, NULL);
		synchronize_rcu();

		if (ops->remove != NULL)
			ops->remove(chan);

	}

#if defined(NV_BUS_TYPE_STRUCT_REMOVE_HAS_INT_RETURN_TYPE) /* Linux v5.15 */
	return 0;
#endif
}

/**
 * @brief Sets the ready state for a child device in the IVC bus
 *
 * This function is called for each child device in the IVC bus to set its ready state.
 * It notifies the driver about the readiness of the device.
 * - Gets the driver for the device using @ref to_tegra_ivc_driver()
 * - Processes ready state from the data parameter
 * - If the device is an IVC channel, gets the channel using @ref to_tegra_ivc_channel()
 * - Updates the ready flag and resets counter using @ref atomic_inc() and @ref smp_wmb()
 * - If the driver is not NULL, reads the RCU-protected operations pointer using @ref rcu_read_lock() and @ref rcu_dereference()
 * - Calls the driver's ready callback if available using @ref ops->ready()
 * - Releases the RCU read lock using @ref rcu_read_unlock()
 *
 * @param[in] dev   Pointer to the child device
 *                  Valid value: non-NULL
 * @param[in] data  Pointer to the ready state data (boolean)
 *                  Valid value: NULL (interpreted as true) or pointer to boolean
 *
 * @retval 0  Always returns success
 */
static int tegra_ivc_bus_ready_child(struct device *dev, void *data)
{
	struct tegra_ivc_driver *drv = to_tegra_ivc_driver(dev->driver);
	bool is_ready = (data != NULL) ? *(bool *)data : true;

	if (dev->type == &tegra_ivc_channel_type) {
		struct tegra_ivc_channel *chan = to_tegra_ivc_channel(dev);
		const struct tegra_ivc_channel_ops *ops;

		chan->is_ready = is_ready;
		if (!is_ready)
			atomic_inc(&chan->bus_resets);
		smp_wmb();

		if (drv != NULL) {
			rcu_read_lock();
			ops = rcu_dereference(chan->ops);
			if (ops == NULL) {
				dev_warn(dev, "channel ops not set\n");
				rcu_read_unlock();
				return 0;
			}
			if (ops->ready != NULL)
				ops->ready(chan, is_ready);
			rcu_read_unlock();
		} else {
			dev_warn(dev, "ivc channel driver missing\n");
		}
	}

	return 0;
}

struct bus_type tegra_ivc_bus_type = {
	.name	= "tegra-ivc-bus",
	.match	= tegra_ivc_bus_match,
	.probe	= tegra_ivc_bus_probe,
	.remove	= tegra_ivc_bus_remove,
};
EXPORT_SYMBOL(tegra_ivc_bus_type);

/**
 * @brief Registers an IVC driver with the IVC bus
 *
 * This function registers a driver with the IVC bus system.
 * It allows the driver to handle IVC channel devices.
 * - Calls @ref driver_register() to register the driver with the kernel
 *
 * @param[in] drv  Pointer to the IVC driver to register
 *                 Valid value: non-NULL
 *
 * @retval (int)    return value from @ref driver_register()
 */
int tegra_ivc_driver_register(struct tegra_ivc_driver *drv)
{
	return driver_register(&drv->driver);
}
EXPORT_SYMBOL(tegra_ivc_driver_register);

/**
 * @brief Unregisters an IVC driver from the IVC bus
 *
 * This function unregisters a driver from the IVC bus system.
 * It removes the driver's ability to handle IVC channel devices.
 * - Calls @ref driver_unregister() to unregister the driver from the kernel
 *
 * @param[in] drv  Pointer to the IVC driver to unregister
 *                 Valid value: non-NULL
 */
void tegra_ivc_driver_unregister(struct tegra_ivc_driver *drv)
{
	return driver_unregister(&drv->driver);
}
EXPORT_SYMBOL(tegra_ivc_driver_unregister);

/**
 * @brief Parses IVC regions from device tree
 *
 * This function parses IVC region specifications from the device tree.
 * It allocates memory for each region and initializes region parameters.
 * - Iterates through device tree nodes to find channel specifications
 * - Validates region specifications
 * - Counts frames and calculates sizes
 * - Allocates DMA-coherent memory for each region
 * - Initializes region parameters
 *
 * @param[in] bus       Pointer to the IVC bus
 *                      Valid value: non-NULL
 * @param[in] dev_node  Pointer to the device tree node
 *                      Valid value: non-NULL
 *
 * @retval 0        On successful parsing
 * @retval -EINVAL  If region specification is invalid
 * @retval -ENOMEM  If memory allocation fails
 */
static int tegra_ivc_bus_parse_regions(struct tegra_ivc_bus *bus,
					struct device_node *dev_node)
{
	struct of_phandle_args reg_spec;
	int i;
	u32 mul = 0U, sum = 0U;

	/* Parse out all regions in a node */
	for (i = 0;
		of_parse_phandle_with_fixed_args(dev_node, NV(ivc-channels), 3,
							i, &reg_spec) == 0;
		i++) {
		struct device_node *ch_node;
		struct tegra_ivc_region *region = &bus->regions[i];
		u32 nframes, frame_size, size = CAMRTC_IVC_CONFIG_SIZE;
		int ret = -ENODEV;

		if (reg_spec.args_count < 3) {
			of_node_put(reg_spec.np);
			dev_err(&bus->dev, "invalid region specification\n");
			return -EINVAL;
		}

		for_each_child_of_node(reg_spec.np, ch_node) {
			ret = of_property_read_u32(ch_node, NV(frame-count),
						&nframes);
			if (ret || !nframes) {
				dev_err(&bus->dev, "missing <%s> property\n",
					NV(frame-count));
				break;
			}
			/* Round up to a power of two */
			nframes = 1 << fls(nframes - 1);

			ret = of_property_read_u32(ch_node, NV(frame-size),
						&frame_size);
			if (ret || !frame_size) {
				dev_err(&bus->dev, "missing <%s> property\n",
					NV(frame-size));
				break;
			}

			if (__builtin_mul_overflow(nframes, frame_size, &mul)) {
				dev_err(&bus->dev, "IVC frame size overflow\n");
				break;
			}

			if (__builtin_mul_overflow(2U, tegra_ivc_total_queue_size(mul), &mul)) {
				dev_err(&bus->dev, "IVC queue size overflow\n");
				break;
			}

			if (__builtin_add_overflow(size, mul, &size)) {
				dev_err(&bus->dev, "IVC size overflow\n");
				break;
			}
		}
		of_node_put(reg_spec.np);

		if (ret)
			return ret;

		region->base =
			(uintptr_t)dma_alloc_coherent(bus->dev.parent,
						size, &region->iova,
						GFP_KERNEL | __GFP_ZERO);
		if (!region->base)
			return -ENOMEM;

		region->size = size;
		region->config_size = 0;
		region->ivc_size = CAMRTC_IVC_CONFIG_SIZE;

		(void)(__builtin_add_overflow(region->iova, size, &sum));
		(void)(__builtin_sub_overflow(sum, 1U, &sum));
		dev_info(&bus->dev, "region %u: iova=0x%x-0x%x size=%u\n",
			i, (u32)region->iova, sum,
			size);
	}

	return 0;
}

/**
 * @brief Counts the number of IVC regions in a device tree node
 *
 * This function counts the number of IVC regions specified in a device tree node.
 * It uses @ref of_parse_phandle_with_fixed_args() to iterate through all ivc-channels.
 *
 * @param[in] dev_node  Pointer to the device tree node
 *                      Valid value: non-NULL
 *
 * @retval (unsigned)  The number of IVC regions found
 */
static unsigned tegra_ivc_bus_count_regions(const struct device_node *dev_node)
{
	unsigned i;

	for (i = 0; of_parse_phandle_with_fixed_args(dev_node,
			NV(ivc-channels), 3, i, NULL) == 0; i++)
		;

	return i;
}

/**
 * @brief Creates and initializes an IVC bus
 *
 * This function creates and initializes an IVC bus with the specified parameters.
 * It performs the following operations:
 * - Counts the number of regions using @ref tegra_ivc_bus_count_regions()
 * - Allocates memory for the bus and regions using @ref kzalloc()
 * - Initializes bus properties and device parameters
 * - Parses regions using @ref tegra_ivc_bus_parse_regions()
 * - Adds the device using @ref device_add()
 * - Starts the bus using @ref tegra_ivc_bus_start()
 *
 * @param[in] dev     Pointer to the parent device
 *                    Valid value: non-NULL
 * @param[in] camhsp  Pointer to the camera HSP context
 *                    Valid value: non-NULL
 *
 * @retval struct tegra_ivc_bus*  Pointer to the created IVC bus on success
 * @retval ERR_PTR(-ENOMEM)       If memory allocation fails
 * @retval ERR_PTR(ret)           Error code from one of the called functions
 */
struct tegra_ivc_bus *tegra_ivc_bus_create(struct device *dev,
	struct camrtc_hsp *camhsp)
{
	struct tegra_ivc_bus *bus;
	unsigned num;
	int ret;

	num = tegra_ivc_bus_count_regions(dev->of_node);

	bus = kzalloc(sizeof(*bus) + num * sizeof(*bus->regions), GFP_KERNEL);
	if (unlikely(bus == NULL))
		return ERR_PTR(-ENOMEM);

	bus->num_regions = num;
	bus->dev.parent = dev;
	bus->dev.type = &tegra_ivc_bus_dev_type;
	bus->dev.bus = &tegra_ivc_bus_type;
	bus->dev.of_node = of_get_child_by_name(dev->of_node, "hsp");
	bus->dev.release = tegra_ivc_bus_release;
	dev_set_name(&bus->dev, "%s:ivc-bus", dev_name(dev));
	device_initialize(&bus->dev);
	pm_runtime_no_callbacks(&bus->dev);
	pm_runtime_enable(&bus->dev);

	ret = tegra_ivc_bus_parse_regions(bus, dev->of_node);
	if (ret) {
		dev_err(&bus->dev, "IVC regions setup failed: %d\n", ret);
		goto error;
	}

	ret = device_add(&bus->dev);
	if (ret) {
		dev_err(&bus->dev, "IVC instance error: %d\n", ret);
		goto error;
	}

	ret = tegra_ivc_bus_start(bus, camhsp);
	if (ret) {
		dev_err(&bus->dev, "bus start failed: %d\n", ret);
		goto error;
	}

	return bus;

error:
	put_device(&bus->dev);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(tegra_ivc_bus_create);

/**
 * @brief Communicates RTCPU UP/DOWN state to IVC devices
 *
 * This function communicates the RTCPU UP/DOWN state to IVC devices.
 * It notifies all child devices of the bus about the RTCPU state.
 * - Checks if the bus is valid using @ref IS_ERR_OR_NULL()
 * - Calls @ref device_for_each_child() with @ref tegra_ivc_bus_ready_child()
 * - If online, notifies all channels using @ref tegra_ivc_bus_notify()
 *
 * @param[in] bus     Pointer to the IVC bus
 *                    Valid value: any value including NULL or error pointer
 * @param[in] online  Boolean indicating if RTCPU is online (true) or offline (false)
 *                    Valid value: true or false
 */
void tegra_ivc_bus_ready(struct tegra_ivc_bus *bus, bool online)
{
	if (IS_ERR_OR_NULL(bus))
		return;

	device_for_each_child(&bus->dev, &online, tegra_ivc_bus_ready_child);

	if (online)
		tegra_ivc_bus_notify(bus, 0xFFFFU);
}
EXPORT_SYMBOL(tegra_ivc_bus_ready);

/**
 * @brief Destroys an IVC bus
 *
 * This function destroys an IVC bus and releases all associated resources.
 * It performs the following operations:
 * - Checks if the bus is valid using @ref IS_ERR_OR_NULL() and returns if invalid
 * - Disables runtime PM for the device using @ref pm_runtime_disable()
 * - Stops all channels using @ref tegra_ivc_bus_stop()
 * - Unregisters the device using @ref device_unregister()
 *
 * @param[in] bus  Pointer to the IVC bus
 *                 Valid value: any value including NULL or error pointer
 */
void tegra_ivc_bus_destroy(struct tegra_ivc_bus *bus)
{
	if (IS_ERR_OR_NULL(bus))
		return;

	pm_runtime_disable(&bus->dev);
	tegra_ivc_bus_stop(bus);
	device_unregister(&bus->dev);
}
EXPORT_SYMBOL(tegra_ivc_bus_destroy);

/**
 * @brief Initializes the Tegra IVC bus subsystem
 *
 * This function initializes the IVC bus subsystem by registering the IVC bus type using
 * @ref bus_register()
 * @pre It is called during module initialization.
 *
 * @retval (int)    return value from @ref bus_register()
 */
static __init int tegra_ivc_bus_init(void)
{
	return bus_register(&tegra_ivc_bus_type);
}

/**
 * @brief Cleans up the Tegra IVC bus subsystem
 *
 * This function performs cleanup of the IVC bus subsystem by unregistering the IVC bus type using @ref bus_unregister()
 * @pre It is called during module exit.
 */
static __exit void tegra_ivc_bus_exit(void)
{
	bus_unregister(&tegra_ivc_bus_type);
}

module_init(tegra_ivc_bus_init);
module_exit(tegra_ivc_bus_exit);
MODULE_AUTHOR("Remi Denis-Courmont <remid@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra IVC generic bus driver");
MODULE_LICENSE("GPL v2");
