/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2009-2024 NVIDIA CORPORATION & AFFILIATES. All Rights Reserved. */

#ifndef __LINUX_NVHOST_H
#define __LINUX_NVHOST_H

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/host1x.h>
#include <linux/platform_device.h>

struct nvhost_ctrl_sync_fence_info;
struct nvhost_fence;
struct nvhost_job;

#define NVHOST_MODULE_MAX_CLOCKS		8
#define NVHOST_MODULE_MAX_IORESOURCE_MEM	5

struct nvhost_notification {
	struct {			/* 0000- */
		__u32 nanoseconds[2];	/* nanoseconds since Jan. 1, 1970 */
	} time_stamp;			/* -0007 */
	__u32 info32;	/* info returned depends on method 0008-000b */
	__u16 info16;	/* info returned depends on method 000c-000d */
	__u16 status;	/* user sets bit 15, NV sets status 000e-000f */
};

struct nvhost_gating_register {
	u64 addr;
	u32 prod;
	u32 disable;
};

enum tegra_emc_request_type {
	TEGRA_SET_EMC_FLOOR,		/* lower bound */
};

struct nvhost_clock {
	char *name;
	unsigned long default_rate;
	u32 moduleid;
	enum tegra_emc_request_type request_type;
	bool disable_scaling;
	unsigned long devfreq_rate;
};

struct nvhost_vm_hwid {
	u64 addr;
	bool dynamic;
	u32 shift;
};

/*
 * Defines HW and SW class identifiers.
 *
 * This is module ID mapping between userspace and kernelspace.
 * The values of enum entries' are referred from NvRmModuleID enum defined
 * in below userspace file:
 * $TOP/vendor/nvidia/tegra/core/include/nvrm_module.h
 * Please make sure each entry below has same value as set in above file.
 */
enum nvhost_module_identifier {
	/* Specifies external memory (DDR RAM, etc) */
	NVHOST_MODULE_ID_EXTERNAL_MEMORY_CONTROLLER = 75,
};

enum nvhost_resource_policy {
	RESOURCE_PER_DEVICE = 0,
	RESOURCE_PER_CHANNEL_INSTANCE,
};

struct nvhost_device_data {
	int		version;	/* ip version number of device */
	void __iomem	*aperture[NVHOST_MODULE_MAX_IORESOURCE_MEM];

	u32		moduleid;	/* Module id for user space API */

	/* interrupt ISR routine for falcon based engines */
	int (*flcn_isr)(struct platform_device *dev);
	int irq;
	int module_irq;	/* IRQ bit from general intr reg for module intr */
	bool self_config_flcn_isr; /* skip setting up falcon interrupts */

	u32		class;		/* Device class */
	bool		keepalive;	/* Do not power gate when opened */
	bool		serialize;	/* Serialize submits in the channel */
	bool		push_work_done;	/* Push_op done into push buffer */
	bool		poweron_reset;	/* Reset the engine before powerup */
	char		*devfs_name;	/* Name in devfs */
	char		*devfs_name_family; /* Core of devfs name */

	char		*firmware_name;	/* Name of firmware */
	bool		firmware_not_in_subdir; /* Firmware is not located in
                                                   chip subdirectory */

	bool		engine_can_cg;	/* True if CG is enabled */
	bool		can_powergate;	/* True if module can be power gated */
	int		autosuspend_delay;/* Delay before power gated */
	struct nvhost_clock clocks[NVHOST_MODULE_MAX_CLOCKS];/* Clock names */

	/* Clock gating registers */
	struct nvhost_gating_register *engine_cg_regs;

	int		num_clks;	/* Number of clocks opened for dev */
	struct clk_bulk_data *clks;
	struct mutex	lock;		/* Power management lock */

	int		num_channels;	/* Max num of channel supported */
	int		num_ppc;	/* Number of pixels per clock cycle */
	dev_t cdev_region;

	/* device node for ctrl block */
	struct class *nvhost_class;
	struct device *ctrl_node;
	struct cdev ctrl_cdev;
	const struct file_operations *ctrl_ops;    /* ctrl ops for the module */

	struct kobject clk_cap_kobj;
	struct kobj_attribute *clk_cap_attrs;
	struct dentry *debugfs;		/* debugfs directory */

	/* Marks if the device is booted when pm runtime is disabled */
	bool				booted;

	void *private_data;		/* private platform data */
	void *falcon_data;		/* store the falcon info */
	struct platform_device *pdev;	/* owner platform_device */
	struct host1x *host1x;		/* host1x device */

	/* Finalize power on. Can be used for context restore. */
	int (*finalize_poweron)(struct platform_device *dev);

	/* Preparing for power off. Used for context save. */
	int (*prepare_poweroff)(struct platform_device *dev);

	/* paring for power off. Used for context save. */
	int (*aggregate_constraints)(struct platform_device *dev,
				     int clk_index,
				     unsigned long floor_rate,
				     unsigned long pixel_rate,
				     unsigned long bw_rate);

	/* Used to add platform specific masks on reloc address */
	dma_addr_t (*get_reloc_phys_addr)(dma_addr_t phys_addr, u32 reloc_type);

	/* engine specific init functions */
	int (*pre_virt_init)(struct platform_device *pdev);
	int (*post_virt_init)(struct platform_device *pdev);

	/* Information related to engine-side synchronization */
	void *syncpt_unit_interface;

	u64 transcfg_addr;
	u32 transcfg_val;
	struct nvhost_vm_hwid vm_regs[13];

	/* Should we map channel at submit time? */
	bool resource_policy;
	/* Should we enable context isolation for this device? */
	bool isolate_contexts;

	/* reset control for this device */
	struct reset_control *reset_control;

	/* icc client id for emc requests */
	int icc_id;

	/* icc_path handle handle */
	struct icc_path *icc_path_handle;

	/* bandwidth manager client id for emc requests */
	int bwmgr_client_id;
};

static inline
struct nvhost_device_data *nvhost_get_devdata(struct platform_device *pdev)
{
	return (struct nvhost_device_data *)platform_get_drvdata(pdev);
}

int flcn_intr_init(struct platform_device *pdev);
int flcn_reload_fw(struct platform_device *pdev);
int nvhost_flcn_prepare_poweroff(struct platform_device *pdev);
int nvhost_flcn_finalize_poweron(struct platform_device *dev);

/* public api to return platform_device ptr to the default host1x instance */
struct platform_device *nvhost_get_default_device(void);

/* common runtime pm and power domain APIs */
int nvhost_module_init(struct platform_device *ndev);
void nvhost_module_deinit(struct platform_device *dev);
void nvhost_module_reset(struct platform_device *dev, bool reboot);
void nvhost_module_idle(struct platform_device *dev);
void nvhost_module_idle_mult(struct platform_device *pdev, int refs);
int nvhost_module_busy(struct platform_device *dev);
extern const struct dev_pm_ops nvhost_module_pm_ops;

void host1x_writel(struct platform_device *dev, u32 r, u32 v);
u32 host1x_readl(struct platform_device *dev, u32 r);

/* common device management APIs */
int nvhost_client_device_get_resources(struct platform_device *dev);
int nvhost_client_device_release(struct platform_device *dev);
int nvhost_client_device_init(struct platform_device *dev);

/* public host1x sync-point management APIs */
u32 nvhost_get_syncpt_host_managed(struct platform_device *pdev,
				   u32 param, const char *syncpt_name);
u32 nvhost_get_syncpt_client_managed(struct platform_device *pdev,
				     const char *syncpt_name);
u32 nvhost_get_syncpt_gpu_managed(struct platform_device *pdev,
				  const char *syncpt_name);
void nvhost_syncpt_put_ref_ext(struct platform_device *pdev, u32 id);
bool nvhost_syncpt_is_valid_pt_ext(struct platform_device *dev, u32 id);
void nvhost_syncpt_set_minval(struct platform_device *dev, u32 id, u32 val);
void nvhost_syncpt_set_min_update(struct platform_device *pdev, u32 id, u32 val);
int nvhost_syncpt_read_ext_check(struct platform_device *dev, u32 id, u32 *val);
u32 nvhost_syncpt_read_maxval(struct platform_device *dev, u32 id);
u32 nvhost_syncpt_incr_max_ext(struct platform_device *dev, u32 id, u32 incrs);
int nvhost_syncpt_is_expired_ext(struct platform_device *dev, u32 id,
				 u32 thresh);
dma_addr_t nvhost_syncpt_address(struct platform_device *engine_pdev, u32 id);
int nvhost_syncpt_unit_interface_init(struct platform_device *pdev);
void nvhost_syncpt_unit_interface_deinit(struct platform_device *pdev);

/* public host1x interrupt management APIs */
int nvhost_intr_register_notifier(struct platform_device *pdev,
				  u32 id, u32 thresh,
				  void (*callback)(void *),
				  void *private_data);

/* public host1x sync-point management APIs */
struct host1x *nvhost_get_host1x(struct platform_device *pdev);

static inline int nvhost_module_set_rate(struct platform_device *dev, void *priv,
			   unsigned long constraint, int index,
			   unsigned long attr)
{
	return 0;
}

static inline int nvhost_module_add_client(struct platform_device *dev, void *priv)
{
	return 0;
}

static inline void nvhost_module_remove_client(struct platform_device *dev, void *priv) { }

static inline void nvhost_debug_dump_device(struct platform_device *pdev)
{
}

static inline int nvhost_fence_create_fd(
		struct platform_device *pdev,
		struct nvhost_ctrl_sync_fence_info *pts,
		u32 num_pts,
		const char *name,
		s32 *fence_fd)
{
	return -EOPNOTSUPP;
}

static inline int nvhost_fence_foreach_pt(
	struct nvhost_fence *fence,
	int (*iter)(struct nvhost_ctrl_sync_fence_info, void *),
	void *data)
{
	return -EOPNOTSUPP;
}

static inline void nvhost_job_put(struct nvhost_job *job) {}

static inline struct nvhost_fence *nvhost_fence_get(int fd)
{
	return NULL;
}

static inline void nvhost_fence_put(struct nvhost_fence *fence) {}

static inline dma_addr_t nvhost_t194_get_reloc_phys_addr(dma_addr_t phys_addr,
							 u32 reloc_type)
{
	return 0;
}

static inline dma_addr_t nvhost_t23x_get_reloc_phys_addr(dma_addr_t phys_addr,
							 u32 reloc_type)
{
	return 0;
}

#endif
