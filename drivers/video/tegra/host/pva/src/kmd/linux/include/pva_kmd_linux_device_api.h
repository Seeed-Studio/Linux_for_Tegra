/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_LINUX_DEVICE_API_H
#define PVA_KMD_LINUX_DEVICE_API_H

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/host1x.h>
#include <linux/platform_device.h>

/* Include common constants and types */
#include "pva_constants.h"
#include "pva_kmd_constants.h"
#include "../pva_kmd_linux_isr.h"

/* Forward declarations */
struct pva_kmd_device;

#define NVPVA_MODULE_MAX_IORESOURCE_MEM 2

struct nvpva_syncpt_interface {
	dma_addr_t base;
	size_t size;
	uint32_t page_size;
};

/**
 * @brief Unified PVA device structure containing all platform and common data
 *
 * @details This structure unifies all device management data to eliminate
 * indirection layers and improve performance. It combines hardware resources,
 * Linux-specific platform data, and common KMD device state in a single
 * cohesive structure.
 */
struct nvpva_device_data {
	/* === Core Platform Resources === */
	/** Platform device pointer */
	struct platform_device *pdev;
	/** Pointer to common PVA KMD device structure */
	struct pva_kmd_device *pva_kmd_dev;

	/* === Hardware Register Mapping === */
	/** Array of mapped register apertures */
	void __iomem *aperture[NVPVA_MODULE_MAX_IORESOURCE_MEM];

	/* === Clock and Power Management === */
	/** Array of clocks used by this device */
	struct clk_bulk_data *clks;
	/** Number of clocks */
	int num_clks;
	/** Reset control for this device */
	struct reset_control *reset_control;
	/** Autosuspend delay in milliseconds */
	int autosuspend_delay;
	/** Power management lock */
	struct mutex lock;

	/* === Host1x Integration === */
	/** Host1x device pointer */
	struct host1x *host1x;
	/** Syncpoint unit interface */
	struct nvpva_syncpt_interface *syncpt_unit_interface;

	/* === Character Device Management === */
	/** Device class for sysfs */
	struct class *nvpva_class;
	/** Control device node */
	struct device *ctrl_node;
	/** Control character device */
	struct cdev ctrl_cdev;
	/** Character device region */
	dev_t cdev_region;
	/** Control operations for the module */
	const struct file_operations *ctrl_ops;

	/* === Debug and Sysfs === */
	/** Debugfs directory */
	struct dentry *debugfs;
	/** Kobject for clock cap sysfs entries */
	struct kobject clk_cap_kobj;
	/** Clock cap attributes */
	struct kobj_attribute *clk_cap_attrs;

	/* === SMMU Context Management === */
	/** SMMU context platform devices */
	struct platform_device *smmu_contexts[PVA_MAX_NUM_SMMU_CONTEXTS];
	/** ISR data for interrupt lines */
	struct pva_kmd_isr_data isr[PVA_KMD_INTR_LINE_COUNT];

	/* === Device Configuration === */
	/** IP version number */
	int version;
	/** Device class ID */
	uint32_t class;
	/** Firmware filename */
	char *firmware_name;

	/* === Device State Flags === */
	/** Device is booted when PM runtime is disabled */
	bool booted;
	/** Module can be power gated */
	bool can_powergate;
	/** Serialize submits in the channel */
	bool serialize;
	/** Push_op done into push buffer */
	bool push_work_done;
	/** Reset the engine before powerup */
	bool poweron_reset;
	/** Resource policy flag */
	bool resource_policy;

	/* === Optional Fields === */
	/** Max number of channels supported */
	int num_channels;
	/** Name in devfs */
	char *devfs_name;
	/** Core of devfs name */
	char *devfs_name_family;

	/* === Callback Functions === */
	/** Finalize power on callback */
	int (*finalize_poweron)(struct platform_device *dev);
	/** Prepare poweroff callback */
	int (*prepare_poweroff)(struct platform_device *dev);
	/** Get reloc physical address callback */
	dma_addr_t (*get_reloc_phys_addr)(dma_addr_t phys_addr, u32 reloc_type);
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
