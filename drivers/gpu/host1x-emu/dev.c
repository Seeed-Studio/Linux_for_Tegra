/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include <nvidia/conftest.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#if IS_ENABLED(CONFIG_ARM_DMA_USE_IOMMU)
#include <asm/dma-iommu.h>
#endif

#include <soc/tegra/common.h>

#include "dev.h"
#include "debug.h"
#include "hw/host1xEMU.h"

#define HOST1X_POOL_MSEC_PERIOD     70          /*70msec*/
#define HOST1X_SYNCPT_POOL_BASE(x)  (x*2+0)
#define HOST1X_SYNCPT_POOL_SIZE(x)  (x*2+1)

static const struct host1x_info host1xEmu_info = {
    .nb_pts                 = 1024,
    .init                   = host1xEMU_init,
    .dma_mask               = DMA_BIT_MASK(40),
};

static const struct of_device_id host1x_of_match[] = {
    { .compatible = "nvidia,tegraEmu-host1x", .data = &host1xEmu_info, },
    { },
};
MODULE_DEVICE_TABLE(of, host1x_of_match);

void host1x_sync_writel(struct host1x *host1x, u32 v, u32 r)
{
#ifdef HOST1X_EMU_HYPERVISOR
    void __iomem *sync_mem = host1x->syncpt_va_apt;
    writel(v, (void __iomem*)((u8*)sync_mem + r));
#else
    unsigned int *sync_mem = (unsigned int*)((u8*)host1x->syncpt_va_apt + r);
    *sync_mem = v;
#endif
}

u32 host1x_sync_readl(struct host1x *host1x, u32 r)
{
#ifdef HOST1X_EMU_HYPERVISOR
    void __iomem *sync_mem = host1x->syncpt_va_apt;
    return readl((void __iomem*)((u8*)sync_mem + r));
#else
    unsigned int *sync_mem = (unsigned int*)((u8*)host1x->syncpt_va_apt + r);
    return(*sync_mem);
#endif
}

static int host1x_get_assigned_resources(struct host1x *host)
{
    int err;
    u32 vals[4];
#ifndef HOST1X_EMU_HYPERVISOR
    unsigned long   page_addr = 0;
    unsigned int    syncpt_pwr_2;
#endif
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

#ifdef HOST1X_EMU_HYPERVISOR
    err = of_property_read_u32_array(np, "nvidia,syncpoints-mem", vals, 4);
    if (err == 0) {
        host->syncpt_phy_apt  = ((uint64_t)vals[0] << 32U) | ((uint64_t)vals[1]);
        host->syncpt_page_size = vals[2];
        host->syncpt_count     = vals[3];
#ifdef HOST1X_EMU_SYNCPT_DEGUB
        /**
         * TODO: Remove debug prints
         */
        pr_info("Host1x-EMU: Syncpoint Physical Addr:%llx\n", host->syncpt_phy_apt);
        pr_info("Host1x-EMU: Syncpoint Page Size :%u\n", vals[2]);
        pr_info("Host1x-EMU: Syncpoint Count :%u\n", vals[3]);
        pr_info("Host1x-EMU: Syncpoint Pooling Interval :%u\n", host->polling_intrval);
        pr_info("Host1x-EMU: OS Scheduling resolution :%u\n", HZ);
#endif
    } else {
        dev_err(host->dev,
            "Host1x-EMU:invalid nvidia,syncpoints-mem property: %d\n", err);
        return err;
    }

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
#else
    /**
     * TODO: Check if we can set this from DT.
     * Currently for native OS using static value for number of syncpoint
     */
    host->syncpt_count = host->info->nb_pts;
    if ((host->syncpt_end + host->syncpt_base) > host->syncpt_count) {
        dev_err(host->dev,
            "Host1x-EMU: Invalid syncpoint property, Syncpoint excedes range: %d\n", -EINVAL );
        return -EINVAL;
    }

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

    ret = of_property_count_strings(np, "nvidia,syncpoint-pool-names");
    if (ret < 0) {
        /* No pools defined, only read only pool*/
        dev_err(host->dev, "Host1x-EMU: Invalid nvidia,syncpoint-pool-names property: %d\n", ret);
        ret = 0;
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
    if (!host)
        return -ENOMEM;

    host->info = of_device_get_match_data(&pdev->dev);
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

    err = devm_of_platform_populate(&pdev->dev);
    if (err < 0) {
        pr_info("Host1x-EMU: Failed to populate device from DT\n");
        goto deinit_debugfs;
    }

    /* Start pool polling thread*/
    host1x_poll_start(host);

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
