/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef __LINUX_NVHOST_H
#define __LINUX_NVHOST_H

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/symbol-emu.h>

/**
 * TODO: Remove header after pre-silicon verification
 * This header is required till emulation driver verification is done on Tho-r
 * VDK platform. Since we are exporting new kernel symbol, header with declarion
 * of modified symbols is required
 *
 * Changed cannot be added in orignal Host1x driver header files ("linux/nvhost.h"
 * and "host1x-next.h") due to Host1x driver and other dependent drivers. Note in
 * Tho-r verification config both the driver will co-exist.
 *
 * Some data structure are also re-delacred in this header for case where client
 * driver wants to verify over emulation driver.
 */

struct nvhost_ctrl_sync_fence_info;
struct nvhost_fence;

#define NVHOST_MODULE_MAX_CLOCKS            8
#define NVHOST_MODULE_MAX_IORESOURCE_MEM    5

enum tegra_emc_request_type {
    TEGRA_SET_EMC_FLOOR,    /* lower bound */
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
    int     version;	/* ip version number of device */
    void __iomem    *aperture[NVHOST_MODULE_MAX_IORESOURCE_MEM];

    u32     moduleid;	/* Module id for user space API */

    /* interrupt ISR routine for falcon based engines */
    int (*flcn_isr)(struct platform_device *dev);
    int     irq;
    int     module_irq;	/* IRQ bit from general intr reg for module intr */
    bool    self_config_flcn_isr; /* skip setting up falcon interrupts */

    u32     class;		/* Device class */
    bool    keepalive;	/* Do not power gate when opened */
    bool    serialize;	/* Serialize submits in the channel */
    bool    push_work_done;	/* Push_op done into push buffer */
    bool    poweron_reset;  /* Reset the engine before powerup */
    char    *devfs_name;    /* Name in devfs */
    char    *devfs_name_family; /* Core of devfs name */

    char    *firmware_name; /* Name of firmware */
    bool    firmware_not_in_subdir; /* Firmware is not located in
                                                   chip subdirectory */

    bool    engine_can_cg;      /* True if CG is enabled */
    bool    can_powergate;      /* True if module can be power gated */
    int     autosuspend_delay;  /* Delay before power gated */
    struct nvhost_clock clocks[NVHOST_MODULE_MAX_CLOCKS];/* Clock names */

    int     num_clks;       /* Number of clocks opened for dev */
    struct clk_bulk_data *clks;
    struct mutex    lock;   /* Power management lock */

    int     num_channels;   /* Max num of channel supported */
    int     num_ppc;        /* Number of pixels per clock cycle */
    dev_t cdev_region;

    /* device node for ctrl block */
    struct class *nvhost_class;
    struct device *ctrl_node;
    struct cdev ctrl_cdev;
    const struct file_operations *ctrl_ops;    /* ctrl ops for the module */

    struct kobject clk_cap_kobj;
    struct kobj_attribute *clk_cap_attrs;
    struct dentry *debugfs;         /* debugfs directory */

    /* Marks if the device is booted when pm runtime is disabled */
    bool    booted;

    void *private_data;             /* private platform data */
    void *falcon_data;              /* store the falcon info */
    struct platform_device *pdev;   /* owner platform_device */
    struct host1x *host1x;          /* host1x device */

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

extern const struct dev_pm_ops nvhost_module_pm_ops;

HOST1X_EMU_EXPORT_DECL(u32, host1x_readl(struct platform_device *pdev, u32 r));

HOST1X_EMU_EXPORT_DECL(void, host1x_writel(struct platform_device *pdev, u32 r, u32 v));

/* public api to return platform_device ptr to the default host1x instance */
HOST1X_EMU_EXPORT_DECL(struct platform_device*, nvhost_get_default_device(void));

/* common runtime pm and power domain APIs */
HOST1X_EMU_EXPORT_DECL(int, nvhost_module_init(struct platform_device *ndev));

HOST1X_EMU_EXPORT_DECL(void, nvhost_module_deinit(struct platform_device *dev));

HOST1X_EMU_EXPORT_DECL(void, nvhost_module_reset(struct platform_device *dev, bool reboot));

HOST1X_EMU_EXPORT_DECL(int, nvhost_module_busy(struct platform_device *dev));

HOST1X_EMU_EXPORT_DECL(void, nvhost_module_idle(struct platform_device *dev));

HOST1X_EMU_EXPORT_DECL(void, nvhost_module_idle_mult(struct platform_device *pdev, int refs));

/* common device management APIs */
HOST1X_EMU_EXPORT_DECL(int, nvhost_client_device_get_resources(struct platform_device *dev));

HOST1X_EMU_EXPORT_DECL(int, nvhost_client_device_release(struct platform_device *dev));

HOST1X_EMU_EXPORT_DECL(int, nvhost_client_device_init(struct platform_device *dev));

/* public host1x sync-point management APIs */
HOST1X_EMU_EXPORT_DECL(u32, nvhost_get_syncpt_host_managed(struct platform_device *pdev,
                   u32 param, const char *syncpt_name));

HOST1X_EMU_EXPORT_DECL(u32, nvhost_get_syncpt_client_managed(struct platform_device *pdev,
                     const char *syncpt_name));

HOST1X_EMU_EXPORT_DECL(u32, nvhost_get_syncpt_gpu_managed(struct platform_device *pdev,
                  const char *syncpt_name));

HOST1X_EMU_EXPORT_DECL(void, nvhost_syncpt_put_ref_ext(struct platform_device *pdev, u32 id));

HOST1X_EMU_EXPORT_DECL(bool, nvhost_syncpt_is_valid_pt_ext(struct platform_device *dev, u32 id));

HOST1X_EMU_EXPORT_DECL(int, nvhost_syncpt_is_expired_ext(struct platform_device *dev, u32 id,
                 u32 thresh));

HOST1X_EMU_EXPORT_DECL(void, nvhost_syncpt_set_min_update(struct platform_device *pdev, u32 id, u32 val));

HOST1X_EMU_EXPORT_DECL(int, nvhost_syncpt_read_ext_check(struct platform_device *dev, u32 id, u32 *val));

HOST1X_EMU_EXPORT_DECL(u32, nvhost_syncpt_read_maxval(struct platform_device *dev, u32 id));

HOST1X_EMU_EXPORT_DECL(u32, nvhost_syncpt_incr_max_ext(struct platform_device *dev, u32 id, u32 incrs));

HOST1X_EMU_EXPORT_DECL(int, nvhost_syncpt_unit_interface_init(struct platform_device *pdev));

HOST1X_EMU_EXPORT_DECL(void, nvhost_syncpt_unit_interface_deinit(struct platform_device *pdev));

HOST1X_EMU_EXPORT_DECL(dma_addr_t, nvhost_syncpt_address(struct platform_device *engine_pdev, u32 id));

/* public host1x interrupt management APIs */
HOST1X_EMU_EXPORT_DECL(int, nvhost_intr_register_notifier(struct platform_device *pdev,
                  u32 id, u32 thresh,
                  void (*callback)(void *),
                  void *private_data));

/* public host1x sync-point management APIs */
HOST1X_EMU_EXPORT_DECL(struct host1x*, nvhost_get_host1x(struct platform_device *pdev));

HOST1X_EMU_EXPORT_DECL(int, nvhost_syncpt_unit_interface_get_aperture(
                struct platform_device *host_pdev,
                phys_addr_t *base,
                size_t *size));

HOST1X_EMU_EXPORT_DECL(u32, nvhost_syncpt_unit_interface_get_byte_offset(u32 syncpt_id));

HOST1X_EMU_EXPORT_DECL(u32, nvhost_syncpt_unit_interface_get_byte_offset_ext(
                struct platform_device *host_pdev,
                u32 syncpt_id));
#endif
