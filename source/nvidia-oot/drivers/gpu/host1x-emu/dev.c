/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include <nvidia/conftest.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#ifdef CONFIG_TEGRA_HOST1X_EMU_DBG_SYMBL
#include <linux/host1x-emu.h>
#include <linux/nvhost-emu.h>
#else
#include <linux/host1x-next.h>
#include <linux/nvhost.h>
#include <linux/nvhost_t194.h>
#endif

#if IS_ENABLED(CONFIG_ARM_DMA_USE_IOMMU)
#include <asm/dma-iommu.h>
#endif

#include <soc/tegra/common.h>

#include "dev.h"
#include "debug.h"
#include "hw/host1xEMU.h"
#include <linux/host1x-dispatch_type.h>

#define HOST1X_POOL_MSEC_PERIOD     70          /*70msec*/
#define HOST1X_SYNCPT_POOL_BASE(x)  (x*2+0)
#define HOST1X_SYNCPT_POOL_SIZE(x)  (x*2+1)

struct platform_device  *host1x_def_pdev;

static const struct host1x_info host1xEmu_info = {
    .nb_pts                 = 1024,
    .init                   = host1xEMU_init,
    .dma_mask               = DMA_BIT_MASK(40),
};

struct host1x_interface_ops  host1x_emu_api = {
    .host1x_fence_create            = HOST1X_EMU_EXPORT_SYMBOL_NAME(host1x_fence_create),
    .host1x_fence_extract           = HOST1X_EMU_EXPORT_SYMBOL_NAME(host1x_fence_extract),
    .host1x_fence_cancel            = HOST1X_EMU_EXPORT_SYMBOL_NAME(host1x_fence_cancel),
    .host1x_fence_get_node          = HOST1X_EMU_EXPORT_SYMBOL_NAME(host1x_fence_get_node),
    .host1x_get_dma_mask            = HOST1X_EMU_EXPORT_SYMBOL_NAME(host1x_get_dma_mask),
    .host1x_syncpt_get              = HOST1X_EMU_EXPORT_SYMBOL_NAME(host1x_syncpt_get),
    .host1x_syncpt_get_by_id        = HOST1X_EMU_EXPORT_SYMBOL_NAME(host1x_syncpt_get_by_id),
    .host1x_syncpt_get_by_id_noref  = HOST1X_EMU_EXPORT_SYMBOL_NAME(host1x_syncpt_get_by_id_noref),
    .host1x_syncpt_read             = HOST1X_EMU_EXPORT_SYMBOL_NAME(host1x_syncpt_read),
    .host1x_syncpt_read_min         = HOST1X_EMU_EXPORT_SYMBOL_NAME(host1x_syncpt_read_min),
    .host1x_syncpt_read_max         = HOST1X_EMU_EXPORT_SYMBOL_NAME(host1x_syncpt_read_max),
    .host1x_syncpt_incr             = HOST1X_EMU_EXPORT_SYMBOL_NAME(host1x_syncpt_incr),
    .host1x_syncpt_incr_max         = HOST1X_EMU_EXPORT_SYMBOL_NAME(host1x_syncpt_incr_max),
    .host1x_syncpt_alloc            = HOST1X_EMU_EXPORT_SYMBOL_NAME(host1x_syncpt_alloc),
    .host1x_syncpt_put              = HOST1X_EMU_EXPORT_SYMBOL_NAME(host1x_syncpt_put),
    .host1x_syncpt_id               = HOST1X_EMU_EXPORT_SYMBOL_NAME(host1x_syncpt_id),
    .host1x_syncpt_wait_ts          = HOST1X_EMU_EXPORT_SYMBOL_NAME(host1x_syncpt_wait_ts),
    .host1x_syncpt_wait             = HOST1X_EMU_EXPORT_SYMBOL_NAME(host1x_syncpt_wait),
	.host1x_syncpt_get_shim_info    = HOST1X_EMU_EXPORT_SYMBOL_NAME(host1x_syncpt_get_shim_info),

//nvhost.h Interface
    .host1x_writel                      = HOST1X_EMU_EXPORT_SYMBOL_NAME(host1x_writel),
    .nvhost_get_default_device          = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_get_default_device),
    .nvhost_get_host1x                  = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_get_host1x),
    .nvhost_client_device_get_resources = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_client_device_get_resources),
    .nvhost_client_device_init          = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_client_device_init),
    .nvhost_client_device_release       = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_client_device_release),
    .nvhost_get_syncpt_host_managed     = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_get_syncpt_host_managed),
    .nvhost_get_syncpt_client_managed   = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_get_syncpt_client_managed),
    .nvhost_get_syncpt_gpu_managed      = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_get_syncpt_gpu_managed),
    .nvhost_syncpt_put_ref_ext          = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_syncpt_put_ref_ext),
    .nvhost_syncpt_is_valid_pt_ext      = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_syncpt_is_valid_pt_ext),
    .nvhost_syncpt_is_expired_ext       = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_syncpt_is_expired_ext),
    .nvhost_syncpt_set_min_update       = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_syncpt_set_min_update),
    .nvhost_syncpt_read_ext_check       = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_syncpt_read_ext_check),
    .nvhost_syncpt_read_maxval          = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_syncpt_read_maxval),
    .nvhost_syncpt_incr_max_ext         = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_syncpt_incr_max_ext),
    .nvhost_syncpt_unit_interface_get_byte_offset_ext = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_syncpt_unit_interface_get_byte_offset_ext),
    .nvhost_syncpt_unit_interface_get_byte_offset   = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_syncpt_unit_interface_get_byte_offset),
    .nvhost_syncpt_unit_interface_get_aperture      = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_syncpt_unit_interface_get_aperture),
    .nvhost_syncpt_unit_interface_init      = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_syncpt_unit_interface_init),
    .nvhost_syncpt_unit_interface_deinit    = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_syncpt_unit_interface_deinit),
    .nvhost_syncpt_address              = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_syncpt_address),
    .nvhost_intr_register_notifier      = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_intr_register_notifier),
    .nvhost_module_init                 = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_module_init),
    .nvhost_module_deinit               = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_module_deinit),
    .nvhost_module_reset                = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_module_reset),
    .nvhost_module_busy                 = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_module_busy),
    .nvhost_module_idle                 = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_module_idle),
    .nvhost_module_idle_mult            = HOST1X_EMU_EXPORT_SYMBOL_NAME(nvhost_module_idle_mult),
};

static const struct of_device_id host1x_of_match[] = {
    { .compatible = "nvidia,tegraEmu-host1x", .data = &host1xEmu_info, },
    { },
};
MODULE_DEVICE_TABLE(of, host1x_of_match);

static void *acpi_data[] = {
    (void*)&host1xEmu_info,   /*0x0*/
    NULL,
};

static struct acpi_device_id tegra_emu_syncpt_acpi_match[] = {
    {
        .id = "NVDA300A",
        .driver_data = 0,
    },
    { },
};
MODULE_DEVICE_TABLE(acpi, tegra_emu_syncpt_acpi_match);

void host1x_sync_writel(struct host1x *host1x, u32 v, u32 r)
{
    if (host1x->hv_syncpt_mem == true) {
        void __iomem *sync_mem = host1x->syncpt_va_apt;
        writel(v, (void __iomem*)((u8*)sync_mem + r));
    } else {
        unsigned int *sync_mem = (unsigned int*)((u8*)host1x->syncpt_va_apt + r);
        *sync_mem = v;
    }
}

u32 host1x_sync_readl(struct host1x *host1x, u32 r)
{
    if (host1x->hv_syncpt_mem == true) {
        void __iomem *sync_mem = host1x->syncpt_va_apt;
        return readl((void __iomem*)((u8*)sync_mem + r));
    } else {
        unsigned int *sync_mem = (unsigned int*)((u8*)host1x->syncpt_va_apt + r);
        return(*sync_mem);
    }
}

static int host1x_get_assigned_resources(struct host1x *host)
{
    int err;
    u32 vals[4];
    unsigned long   page_addr = 0;
    unsigned int    syncpt_pwr_2;
    struct device_node  *np = host->dev->of_node;

    err = of_property_read_u32_array(np, "nvidia,syncpoints", vals, 2);
    if (err == 0) {
        host->syncpt_base = vals[0];
        host->syncpt_end  = vals[0] + vals[1];
    } else {
        /**
         * In case of no syncpoint property defined, use pre-defined syncpoints
         * info.
         */
        host->syncpt_base  = 0;
        host->syncpt_end   = host->info->nb_pts;
        dev_err(host->dev, "Host1x-EMU: invalid/no nvidia,syncpoints property: %d\n", err);
    }

    err = of_property_read_u32_array(np, "nvidia,polling-interval", vals, 1);
    if (err == 0) {
        host->polling_intrval = vals[0];
    } else {
        host->polling_intrval = HOST1X_POOL_MSEC_PERIOD;
    }

#ifdef HOST1X_EMU_HRTIMER_FENCE_SCAN
	err = of_property_read_u32_array(np, "nvidia,hr-polling-interval", vals, 1);
	if (err == 0) {
		host->hr_polling_intrval = vals[0];
		if (host->hr_polling_intrval < 50)
			host->hr_polling_intrval = HRTIMER_TIMEOUT_NSEC;
	} else {
		host->hr_polling_intrval = HRTIMER_TIMEOUT_NSEC;
	}
	pr_info("Host1x-EMU: OS Scheduling resolution :%u\n", HZ);
	pr_info("Host1x-EMU: HRTimer Resolution :%unsec\n", MONOTONIC_RES_NSEC);
	pr_info("Host1x-EMU: HRTimer Polling Interval :%unsec\n", host->hr_polling_intrval);
#endif

    err = of_property_read_u32_array(np, "nvidia,syncpoints-mem", vals, 4);
    if (err == 0) {
        host->syncpt_phy_apt  = ((uint64_t)vals[0] << 32U) | ((uint64_t)vals[1]);
        host->syncpt_page_size = vals[2];
        host->syncpt_count     = vals[3];
        host->hv_syncpt_mem    = true;

        if ((host->syncpt_end + host->syncpt_base) > host->syncpt_count) {
            dev_err(host->dev,
                "Host1x-EMU: Invalid syncpoint property, Syncpoint excedes range: %d\n", -EINVAL );
            return -EINVAL ;
        }

        host->syncpt_va_apt = devm_ioremap(host->dev, host->syncpt_phy_apt,
                                    (host->syncpt_count*host->syncpt_page_size));
        if (IS_ERR(host->syncpt_va_apt)) {
            return PTR_ERR(host->syncpt_va_apt);
        }
    } else {
        host->hv_syncpt_mem = false;
        host->syncpt_count  = host->syncpt_end;

        syncpt_pwr_2 = order_base_2(host->syncpt_count);
        page_addr = __get_free_pages(GFP_KERNEL, syncpt_pwr_2);
        if (unlikely((void*)page_addr == NULL)) {
            dev_err(host->dev,
                "Host1x-EMU: Syncpoint Carveout allocation failed: %d\n", (-ENOMEM));
            return -ENOMEM;
        }
        host->syncpt_phy_apt   = __pa(page_addr);
        host->syncpt_va_apt    = (void*)page_addr;
        host->syncpt_page_size  = PAGE_SIZE;
        /*Resetting pool to zero value*/
        memset((void*)page_addr, 0, PAGE_SIZE << syncpt_pwr_2);
    }

#ifdef HOST1X_EMU_SYNCPT_DEGUB
    pr_info("Host1x-EMU: Syncpoint Physical Addr:%llx\n", host->syncpt_phy_apt);
    pr_info("Host1x-EMU: Syncpoint Page Size :%u\n", host->syncpt_page_size);
    pr_info("Host1x-EMU: Syncpoint Pooling Interval :%u\n", host->polling_intrval);
#endif

    pr_info("Host1x-EMU: Syncpoint-Base:%d Syncpoint-End:%d Syncpoint-Count:%d\n",
            host->syncpt_base, host->syncpt_end, host->syncpt_count);
    return 0;
}

static int host1x_get_syncpt_pools(struct host1x *host)
{
    struct device_node *np = host->dev->of_node;
    int ret;
    int i;

    if (ACPI_HANDLE(host->dev)) {
        /*
         * Set number of R/W pool as 1 for ACPI clients
         * TODO: Make this based on host->info which is set
         * based on platform.
         */
        ret = 1;
    } else {
        ret = of_property_count_strings(np, "nvidia,syncpoint-pool-names");
        if (ret < 0) {
            /* No pools defined, only read only pool*/
            dev_err(host->dev, "Host1x-EMU: Invalid nvidia,syncpoint-pool-names property: %d\n", ret);
            ret = 0;
        }
    }
    host->num_pools = ret;

    /*
     * Adding 1 here for RO-pool, which is used for all RO-syncpoint for this VM.
     * By default all syncpoint are assigned to RO-Pool.
     *
     * Further pool initialization and syncpoint initialization will re-assign
     * R/W syncpoint to appropiate pool based on calibration data.
     *
     * Note: RO-Pool variable "sp_base/sp_end" are not updated to correct syncpoint
     *       range after all pool/syncpoint initialization due to following reason
     *       1. Variable are not used after pool/syncpoint object initialization
     *       2. Two Variable are not sufficient to represent fragmented RO range
     *          Ex |---RO Sync Range1--|--RW range--|---RO Sync Range2--|
     */
    host->ro_pool_id = host->num_pools;
    host->pools = devm_kcalloc(host->dev, host->num_pools + 1,
                        sizeof(struct host1x_syncpt_pool), GFP_KERNEL);
    if (!host->pools) {
        dev_err(host->dev, "Host1x-EMU: Failed allocating pool memory\n");
        return -ENOMEM;
    }
    host->pools[host->ro_pool_id].sp_base = 0;
    host->pools[host->ro_pool_id].sp_end  = host->syncpt_count;

    /* Return if only read only pools*/
    if (host->num_pools == 0) {
        return 0;
    }

    for (i = 0; i < host->num_pools; i++) {
        struct host1x_syncpt_pool *pool = &host->pools[i];

        if (ACPI_HANDLE(host->dev)) {
            /*
             * TODO: Make this based on host->info which is set
             * based on platform.
             */
            pool->sp_base = 0;
            pool->sp_end  = host->syncpt_count;
        } else {
            ret = of_property_read_string_index(np, "nvidia,syncpoint-pool-names", i, &pool->name);
            if (ret) {
                dev_err(host->dev, "Host1x-EMU: Invalid nvidia,syncpoint-pool-names property: %d\n", ret);
                return ret;
            }

            ret = of_property_read_u32_index(np, "nvidia,syncpoint-pools", HOST1X_SYNCPT_POOL_BASE(i), &pool->sp_base);
            if (!ret) {
                ret = of_property_read_u32_index(np, "nvidia,syncpoint-pools", HOST1X_SYNCPT_POOL_SIZE(i), &pool->sp_end);
                if (ret) {
                    dev_err(host->dev, "Host1x-EMU: Invalid nvidia,syncpoint-pools property: %d\n", ret);
                    return ret;
                }
            } else {
                dev_err(host->dev, "Host1x-EMU: Error in read, invalid nvidia,syncpoint-pools property: %d\n", ret);
                return ret;
            }
        }

        pool->sp_end = pool->sp_base + pool->sp_end;
        if (pool->sp_end > host->syncpt_count) {
            pool->sp_end = host->syncpt_count;
        }
    }
    return 0;
}

static int host1x_probe(struct platform_device *pdev)
{
    int err;
    struct host1x *host;

    host = devm_kzalloc(&pdev->dev, sizeof(*host), GFP_KERNEL);
    if (!host) {
        return -ENOMEM;
    }

    if (ACPI_HANDLE(&pdev->dev)) {
        const struct acpi_device_id *match;

        match = acpi_match_device(tegra_emu_syncpt_acpi_match, &pdev->dev);
        if (match == NULL) {
            return -ENODEV;
        }
        host1x_def_pdev = pdev;
        host->info = (struct host1x_info *)acpi_data[match->driver_data];
    }else {
        host->info = of_device_get_match_data(&pdev->dev);
    }

    if (host->info == NULL) {
        dev_err(&pdev->dev, "Host1x-EMU: platform match data not found\n");
        return -EINVAL;
    }
    host->dev  = &pdev->dev;

    /* set common host1x device data */
    platform_set_drvdata(pdev, host);

    host->dev->dma_parms = &host->dma_parms;
    dma_set_max_seg_size(host->dev, UINT_MAX);

    if (host->info->init) {
        err = host->info->init(host);
        if (err)
            return err;
    }

    err = host1x_get_assigned_resources(host);
    if (err)
        return err;

    err = host1x_get_syncpt_pools(host);
    if (err)
        return err;

    err = host1x_syncpt_init(host);
    if (err) {
        dev_err(&pdev->dev, "Host1x-EMU: failed to initialize syncpts\n");
        return err;
    }

    err = host1x_poll_init(host);
    if (err) {
        dev_err(&pdev->dev, "Host1x-EMU: failed to initialize interrupts\n");
        goto deinit_syncpt;
    }

    pm_runtime_enable(&pdev->dev);
    /* the driver's code isn't ready yet for the dynamic RPM */
    err = pm_runtime_resume_and_get(&pdev->dev);
    if (err) {
        goto pm_disable;
    }

    host1x_user_init(host);

#ifdef HOST1X_EMU_SYNCPT_DEGUB
    host1x_debug_init(host);
#endif

    if (host->dev->of_node) {
        err = devm_of_platform_populate(&pdev->dev);
        if (err < 0) {
            pr_info("Host1x-EMU: Failed to populate device from DT\n");
            goto deinit_debugfs;
        }
    }

    /* Start pool polling thread*/
    host1x_poll_start(host);

    /* Register Host1x-EMU Interface*/
    host1x_wrapper_register_interface(&host1x_emu_api);

    pr_info("Host1x-EMU: Probe Done\n");
    return 0;

deinit_debugfs:
#ifdef HOST1X_EMU_SYNCPT_DEGUB
    host1x_debug_deinit(host);
#endif
    pm_runtime_put_sync_suspend(&pdev->dev);
pm_disable:
    pm_runtime_disable(&pdev->dev);
deinit_syncpt:
    host1x_syncpt_deinit(host);
    return err;
}

static int host1x_remove(struct platform_device *pdev)
{
    struct host1x *host = platform_get_drvdata(pdev);

#ifdef HOST1X_EMU_SYNCPT_DEGUB
    host1x_debug_deinit(host);
#endif
    pm_runtime_force_suspend(&pdev->dev);
    host1x_syncpt_deinit(host);
    return 0;
}

static int __maybe_unused host1x_runtime_suspend(struct device *dev)
{
    struct host1x *host = dev_get_drvdata(dev);

    host1x_poll_stop(host);
    host1x_syncpt_save(host);
    return 0;
}

static int __maybe_unused host1x_runtime_resume(struct device *dev)
{
    struct host1x *host = dev_get_drvdata(dev);

    host1x_syncpt_restore(host);
    host1x_poll_start(host);
    return 0;
}

static const struct dev_pm_ops host1x_pm_ops = {
    SET_RUNTIME_PM_OPS(NULL, NULL, NULL)
    SET_SYSTEM_SLEEP_PM_OPS(host1x_runtime_suspend, host1x_runtime_resume)
};

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void host1x_remove_wrapper(struct platform_device *pdev)
{
    host1x_remove(pdev);
}
#else
static int host1x_remove_wrapper(struct platform_device *pdev)
{
    return host1x_remove(pdev);
}
#endif

static struct platform_driver tegra_host1x_driver = {
    .driver = {
        .name = "tegra-host1x-emu",
        .of_match_table = host1x_of_match,
        .acpi_match_table = ACPI_PTR(tegra_emu_syncpt_acpi_match),
        .pm = &host1x_pm_ops,
    },
    .probe  = host1x_probe,
    .remove = host1x_remove_wrapper,
};

static struct platform_driver * const drivers[] = {
    &tegra_host1x_driver,
};

static int __init tegra_host1x_init(void)
{
    int err;

    err = platform_register_drivers(drivers, ARRAY_SIZE(drivers));
    return err;
}
module_init(tegra_host1x_init);

static void __exit tegra_host1x_exit(void)
{
    platform_unregister_drivers(drivers, ARRAY_SIZE(drivers));
}
module_exit(tegra_host1x_exit);

MODULE_AUTHOR("Amitabh Dutta <amitabhd@nvidia.com>");
MODULE_AUTHOR("Amitabh Dutta <amitabhd@nvidia.com>");
MODULE_DESCRIPTION("Emulated Host1x Syncpoint Driver");
MODULE_LICENSE("GPL");
