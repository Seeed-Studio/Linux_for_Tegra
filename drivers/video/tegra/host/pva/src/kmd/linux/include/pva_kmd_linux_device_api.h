/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_LINUX_DEVICE_API_H
#define PVA_KMD_LINUX_DEVICE_API_H

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/host1x.h>
#include <linux/platform_device.h>

#define NVPVA_MODULE_MAX_IORESOURCE_MEM 2

struct nvpva_syncpt_interface {
	dma_addr_t base;
	size_t size;
	uint32_t page_size;
};

struct nvpva_device_data {
	void __iomem *aperture[NVPVA_MODULE_MAX_IORESOURCE_MEM];
	struct mutex lock; /* Power management lock */
	struct clk_bulk_data *clks;
	const struct file_operations *ctrl_ops; /* ctrl ops for the module */
	struct dentry *debugfs; /* debugfs directory */
	struct host1x *host1x; /* host1x device */
	struct platform_device *pdev; /* owner platform_device */
	/* reset control for this device */
	struct reset_control *reset_control;
	struct nvpva_syncpt_interface *syncpt_unit_interface;
	/* device node for ctrl block */
	struct class *nvpva_class;
	struct device *ctrl_node;
	struct cdev ctrl_cdev;
	/* private platform data */
	void *private_data;

	/* Finalize power on. Can be used for context restore. */
	int (*finalize_poweron)(struct platform_device *dev);
	/* Preparing for power off. Used for context save. */
	int (*prepare_poweroff)(struct platform_device *dev);
	/* Used to add platform specific masks on reloc address */
	dma_addr_t (*get_reloc_phys_addr)(dma_addr_t phys_addr, u32 reloc_type);

	int version; /* ip version number of device */
	uint32_t class; /* Device class */
	int autosuspend_delay; /* Delay before power gated */
	int num_clks; /* Number of clocks opened for dev */
	int num_channels; /* Max num of channel supported */
	dev_t cdev_region;
	bool resource_policy;
	/* Marks if the device is booted when pm runtime is disabled */
	bool booted;
	bool can_powergate; /* True if module can be power gated */
	bool serialize; /* Serialize submits in the channel */
	bool push_work_done; /* Push_op done into push buffer */
	bool poweron_reset; /* Reset the engine before powerup */

	/* kobject to hold clk_cap sysfs entries */
	struct kobject clk_cap_kobj;
	struct kobj_attribute *clk_cap_attrs;

	char *devfs_name; /* Name in devfs */
	char *devfs_name_family; /* Core of devfs name */
	char *firmware_name; /* Name of firmware */
};

static inline struct nvpva_device_data *
nvpva_get_devdata(struct platform_device *pdev)
{
	return (struct nvpva_device_data *)platform_get_drvdata(pdev);
}

static inline struct host1x *
nvpva_device_to_host1x(struct platform_device *pdev)
{
	return dev_get_drvdata(pdev->dev.parent);
}

/* common runtime pm and power domain APIs */
int nvpva_module_init(struct platform_device *ndev);
void nvpva_module_deinit(struct platform_device *dev);

/* common device management APIs */
int nvpva_device_get_resources(struct platform_device *dev);
int nvpva_device_release(struct platform_device *dev);
int nvpva_device_init(struct platform_device *dev);

/* public host1x sync-point management APIs */
u32 nvpva_get_syncpt_client_managed(struct platform_device *pdev,
				    const char *syncpt_name);
void nvpva_syncpt_put_ref_ext(struct platform_device *pdev, u32 id);
int nvpva_syncpt_read_ext_check(struct platform_device *dev, u32 id, u32 *val);

int nvpva_syncpt_unit_interface_init(struct platform_device *pdev);
void nvpva_syncpt_unit_interface_deinit(struct platform_device *pdev);

u32 nvpva_syncpt_unit_interface_get_byte_offset_ext(u32 syncpt_id);

#endif
