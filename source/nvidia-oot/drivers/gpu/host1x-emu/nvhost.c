/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include <nvidia/conftest.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/dma-fence.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/nvhost-emu.h>
#include <linux/host1x-emu.h>

#include "dev.h"

#define NVHOST_NUM_CDEV 1
extern struct platform_device  *host1x_def_pdev;

struct nvhost_syncpt_interface {
    dma_addr_t base;
    size_t size;
    uint32_t page_size;
};

struct nvhost_host1x_cb {
    struct dma_fence_cb cb;
    struct work_struct work;
    void (*notifier)(void *data);
    void *notifier_data;
};

static const struct of_device_id host1x_match[] = {
    { .compatible = "nvidia,tegraEmu-host1x", },
    {},
};

static struct device *nvhost_client_device_create(struct platform_device *pdev,
                          struct cdev *cdev,
                          const char *cdev_name,
                          dev_t devno,
                          const struct file_operations *ops)
{
    struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
    struct device *dev;
    int err;

#if defined(NV_CLASS_CREATE_HAS_NO_OWNER_ARG) /* Linux v6.4 */
    pdata->nvhost_class = class_create(pdev->dev.of_node->name);
#else
    pdata->nvhost_class = class_create(THIS_MODULE, pdev->dev.of_node->name);
#endif
    if (IS_ERR(pdata->nvhost_class)) {
        dev_err(&pdev->dev, "failed to create class\n");
        return ERR_CAST(pdata->nvhost_class);
    }

    cdev_init(cdev, ops);
    cdev->owner = THIS_MODULE;

    err = cdev_add(cdev, devno, 1);
    if (err < 0) {
        dev_err(&pdev->dev, "failed to add cdev\n");
        class_destroy(pdata->nvhost_class);
        return ERR_PTR(err);
    }

    dev = device_create(pdata->nvhost_class, &pdev->dev, devno, NULL,
                (pdev->id <= 0) ? "nvhost-%s%s" : "nvhost-%s%s.%d",
                cdev_name, pdev->dev.of_node->name, pdev->id);

    if (IS_ERR(dev)) {
        dev_err(&pdev->dev, "failed to create %s device\n", cdev_name);
        class_destroy(pdata->nvhost_class);
        cdev_del(cdev);
    }

    return dev;
}

static void nvhost_host1x_cb_func(struct dma_fence *f, struct dma_fence_cb *cb)
{
    struct nvhost_host1x_cb *host1x_cb;

    host1x_cb = container_of(cb, struct nvhost_host1x_cb, cb);
    schedule_work(&host1x_cb->work);
    dma_fence_put(f);
}

static void nvhost_intr_do_work(struct work_struct *work)
{
    struct nvhost_host1x_cb *host1x_cb;

    host1x_cb = container_of(work, struct nvhost_host1x_cb, work);
    host1x_cb->notifier(host1x_cb->notifier_data);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 5, 0))
    kfree_rcu_mightsleep(host1x_cb);
#else
    kfree_rcu(host1x_cb);
#endif
}

/*Exported Function*/
HOST1X_EMU_EXPORT_DECL(u32, host1x_readl(struct platform_device *pdev, u32 r))
{
    return 0;
}
HOST1X_EMU_EXPORT_SYMBOL(host1x_readl);

HOST1X_EMU_EXPORT_DECL(void, host1x_writel(struct platform_device *pdev, u32 r, u32 v))
{
    return;
}
HOST1X_EMU_EXPORT_SYMBOL(host1x_writel);

HOST1X_EMU_EXPORT_DECL(struct platform_device*, nvhost_get_default_device(void))
{
    struct device_node      *np;
    struct platform_device  *host1x_pdev;

    np = of_find_matching_node(NULL, host1x_match);
    if (np) {
        host1x_pdev = of_find_device_by_node(np);
        if (!host1x_pdev)
            return NULL;
    } else {
        if ((host1x_def_pdev != NULL) &&
                (ACPI_HANDLE(&host1x_def_pdev->dev))) {
            host1x_pdev = host1x_def_pdev;
        }
        else
            return NULL;
    }

    return host1x_pdev;
}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_get_default_device);

HOST1X_EMU_EXPORT_DECL(struct host1x*, nvhost_get_host1x(struct platform_device *pdev))
{
    struct host1x 			*host1x;
    struct platform_device 	*host1x_pdev;

    host1x_pdev = HOST1X_EMU_EXPORT_CALL(nvhost_get_default_device());
    if (!host1x_pdev) {
        dev_dbg(&pdev->dev, "host1x device not available\n");
        return NULL;
    }

    host1x = platform_get_drvdata(host1x_pdev);
    if (!host1x) {
        dev_warn(&pdev->dev, "No platform data for host1x!\n");
        return NULL;
    }

    return host1x;
}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_get_host1x);

HOST1X_EMU_EXPORT_DECL(int, nvhost_client_device_get_resources(struct platform_device *pdev))
{
    struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
    int err;
    u32 i;

    pdata->host1x = HOST1X_EMU_EXPORT_CALL(nvhost_get_host1x(pdev));
    if (!pdata->host1x) {
        dev_warn(&pdev->dev, "No platform data for host1x!\n");
        return -ENODEV;
    }

    for (i = 0; i < pdev->num_resources; i++) {
        void __iomem *regs = NULL;
        struct resource *r;

        r = platform_get_resource(pdev, IORESOURCE_MEM, i);
        /* We've run out of mem resources */
        if (!r)
            break;

        regs = devm_ioremap_resource(&pdev->dev, r);
        if (IS_ERR(regs)) {
            err = PTR_ERR(regs);
            goto fail;
        }

        pdata->aperture[i] = regs;
    }

    return 0;

fail:
    dev_err(&pdev->dev, "failed to get register memory\n");

    return err;

}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_client_device_get_resources);

HOST1X_EMU_EXPORT_DECL(int, nvhost_client_device_init(struct platform_device *pdev))
{
    struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
    dev_t devno;
    int err;

    err = alloc_chrdev_region(&devno, 0, NVHOST_NUM_CDEV, "nvhost");
    if (err < 0) {
        dev_err(&pdev->dev, "failed to reserve chrdev region\n");
        return err;
    }

    pdata->ctrl_node = nvhost_client_device_create(pdev, &pdata->ctrl_cdev,
                               "ctrl-", devno,
                               pdata->ctrl_ops);
    if (IS_ERR(pdata->ctrl_node))
        return PTR_ERR(pdata->ctrl_node);

    pdata->cdev_region = devno;

    return 0;
}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_client_device_init);

HOST1X_EMU_EXPORT_DECL(int, nvhost_client_device_release(struct platform_device *pdev))
{
    struct nvhost_device_data *pdata = platform_get_drvdata(pdev);

    if (!IS_ERR_OR_NULL(pdata->ctrl_node)) {
        device_destroy(pdata->nvhost_class, pdata->ctrl_cdev.dev);
        cdev_del(&pdata->ctrl_cdev);
        class_destroy(pdata->nvhost_class);
    }

    unregister_chrdev_region(pdata->cdev_region, NVHOST_NUM_CDEV);

    return 0;
}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_client_device_release);

HOST1X_EMU_EXPORT_DECL(u32, nvhost_get_syncpt_host_managed(struct platform_device *pdev,
                   u32 param, const char *syncpt_name))
{
    struct host1x_syncpt 		*sp;
    struct nvhost_device_data 	*pdata = platform_get_drvdata(pdev);

    sp = HOST1X_EMU_EXPORT_CALL(host1x_syncpt_alloc(pdata->host1x, 0,
                             syncpt_name ? syncpt_name : dev_name(&pdev->dev)));
    if (!sp)
        return 0;

    return HOST1X_EMU_EXPORT_CALL(host1x_syncpt_id(sp));
}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_get_syncpt_host_managed);

HOST1X_EMU_EXPORT_DECL(u32, nvhost_get_syncpt_client_managed(struct platform_device *pdev,
                     const char *syncpt_name))
{
    struct host1x_syncpt *sp;
    struct nvhost_device_data *pdata = platform_get_drvdata(pdev);

    sp = HOST1X_EMU_EXPORT_CALL(host1x_syncpt_alloc(pdata->host1x,
        HOST1X_SYNCPT_CLIENT_MANAGED, syncpt_name ? syncpt_name : dev_name(&pdev->dev)));
    if (!sp)
        return 0;

    return HOST1X_EMU_EXPORT_CALL(host1x_syncpt_id(sp));
}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_get_syncpt_client_managed);

HOST1X_EMU_EXPORT_DECL(u32, nvhost_get_syncpt_gpu_managed(struct platform_device *pdev,
                     const char *syncpt_name))
{
    return HOST1X_EMU_EXPORT_CALL(nvhost_get_syncpt_client_managed(pdev, syncpt_name));
}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_get_syncpt_gpu_managed);

HOST1X_EMU_EXPORT_DECL(void, nvhost_syncpt_put_ref_ext(struct platform_device *pdev, u32 id))
{
    struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
    struct host1x_syncpt *sp;

    sp = HOST1X_EMU_EXPORT_CALL(host1x_syncpt_get_by_id_noref(pdata->host1x, id));
    if (WARN_ON(!sp))
        return;

    HOST1X_EMU_EXPORT_CALL(host1x_syncpt_put(sp));
}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_syncpt_put_ref_ext);

HOST1X_EMU_EXPORT_DECL(bool, nvhost_syncpt_is_valid_pt_ext(struct platform_device *pdev, u32 id))
{
    struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
    struct host1x_syncpt *sp;

    if (!pdata || !pdata->host1x)
        return -ENODEV;

    sp = HOST1X_EMU_EXPORT_CALL(host1x_syncpt_get_by_id_noref(pdata->host1x, id));

    return sp ? true : false;
}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_syncpt_is_valid_pt_ext);

HOST1X_EMU_EXPORT_DECL(int, nvhost_syncpt_is_expired_ext(struct platform_device *pdev, u32 id,
                 u32 thresh))
{
    struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
    struct host1x_syncpt *sp;

    sp = HOST1X_EMU_EXPORT_CALL(host1x_syncpt_get_by_id_noref(pdata->host1x, id));
    if (WARN_ON(!sp))
        return true;

    if (HOST1X_EMU_EXPORT_CALL(host1x_syncpt_wait(sp, thresh, 0, NULL)))
        return false;

    return true;
}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_syncpt_is_expired_ext);

HOST1X_EMU_EXPORT_DECL(void, nvhost_syncpt_set_min_update(struct platform_device *pdev, u32 id, u32 val))
{
    struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
    struct host1x_syncpt *sp;
    u32 cur;

    /**
     * TODO: Use host1x_syncpt_get_by_id(), otherwise anyone can update
     * syncpoint without allocating
     */
    //sp = HOST1X_EMU_EXPORT_CALL(host1x_syncpt_get_by_id(pdata->host1x, id));
    sp = HOST1X_EMU_EXPORT_CALL(host1x_syncpt_get_by_id_noref(pdata->host1x, id));
    if (WARN_ON(!sp))
        return;

    cur = HOST1X_EMU_EXPORT_CALL(host1x_syncpt_read(sp));

    while (cur++ != val)
        HOST1X_EMU_EXPORT_CALL(host1x_syncpt_incr(sp));
    //HOST1X_EMU_EXPORT_CALL(host1x_syncpt_put(sp));

    HOST1X_EMU_EXPORT_CALL(host1x_syncpt_read(sp));
}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_syncpt_set_min_update);

HOST1X_EMU_EXPORT_DECL(int, nvhost_syncpt_read_ext_check(struct platform_device *pdev, u32 id, u32 *val))
{
    struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
    struct host1x_syncpt *sp;

    sp = HOST1X_EMU_EXPORT_CALL(host1x_syncpt_get_by_id_noref(pdata->host1x, id));
    if (!sp)
        return -EINVAL;

    *val = HOST1X_EMU_EXPORT_CALL(host1x_syncpt_read(sp));
    return 0;
}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_syncpt_read_ext_check);

HOST1X_EMU_EXPORT_DECL(u32, nvhost_syncpt_read_maxval(struct platform_device *pdev, u32 id))
{
    struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
    struct host1x_syncpt *sp;

    sp = HOST1X_EMU_EXPORT_CALL(host1x_syncpt_get_by_id_noref(pdata->host1x, id));
    if (WARN_ON(!sp))
        return 0;

    return HOST1X_EMU_EXPORT_CALL(host1x_syncpt_read_max(sp));
}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_syncpt_read_maxval);

HOST1X_EMU_EXPORT_DECL(u32, nvhost_syncpt_incr_max_ext(struct platform_device *pdev, u32 id, u32 incrs))
{
    struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
    struct host1x_syncpt *sp;

    sp = HOST1X_EMU_EXPORT_CALL(host1x_syncpt_get_by_id_noref(pdata->host1x, id));
    if (WARN_ON(!sp))
        return 0;

    return HOST1X_EMU_EXPORT_CALL(host1x_syncpt_incr_max(sp, incrs));
}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_syncpt_incr_max_ext);

HOST1X_EMU_EXPORT_DECL(u32, nvhost_syncpt_unit_interface_get_byte_offset_ext(struct platform_device *pdev,
                             u32 syncpt_id))
{
	struct platform_device *host1x_pdev;
	struct host1x *host1x;

	host1x_pdev = HOST1X_EMU_EXPORT_CALL(nvhost_get_default_device());
	if (WARN_ON(!host1x_pdev))
		return 0;

	host1x = platform_get_drvdata(host1x_pdev);
	if (!host1x) {
		pr_info("No platform data for host1x!\n");
		return 0;
	}

	if (syncpt_id >= host1x->syncpt_count) {
		pr_info("Invalid syncpoint ID!\n");
		return 0;
	}

	return syncpt_id * host1x->syncpt_page_size;
}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_syncpt_unit_interface_get_byte_offset_ext);

HOST1X_EMU_EXPORT_DECL(u32, nvhost_syncpt_unit_interface_get_byte_offset(u32 syncpt_id))
{
    struct platform_device *host1x_pdev;
    struct host1x *host1x;

    host1x_pdev = HOST1X_EMU_EXPORT_CALL(nvhost_get_default_device());
    if (WARN_ON(!host1x_pdev))
        return 0;

    host1x = platform_get_drvdata(host1x_pdev);
    if (!host1x) {
        pr_info("No platform data for host1x!\n");
        return 0;
    }

    if (syncpt_id >= host1x->syncpt_count) {
        pr_info("Invalid syncpoint ID!\n");
        return 0;
    }
    return (syncpt_id * host1x->syncpt_page_size);
}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_syncpt_unit_interface_get_byte_offset);

HOST1X_EMU_EXPORT_DECL(int, nvhost_syncpt_unit_interface_get_aperture(struct platform_device *pdev,
                          u64 *base, size_t *size))
{
	struct platform_device *host1x_pdev;
	struct host1x *host1x;

	host1x_pdev = HOST1X_EMU_EXPORT_CALL(nvhost_get_default_device());
	if (WARN_ON(!host1x_pdev))
		return 0;

	host1x = platform_get_drvdata(host1x_pdev);
	if (!host1x) {
		pr_info("No platform data for host1x!\n");
		return 0;
	}

	*base = host1x->syncpt_phy_apt;
	*size = host1x->syncpt_page_size * host1x->syncpt_count;
	return 0;
}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_syncpt_unit_interface_get_aperture);

HOST1X_EMU_EXPORT_DECL(int, nvhost_syncpt_unit_interface_init(struct platform_device *pdev))
{
    struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
    struct nvhost_syncpt_interface *syncpt_if;


    syncpt_if = devm_kzalloc(&pdev->dev, sizeof(*syncpt_if), GFP_KERNEL);
    if (!syncpt_if)
        return -ENOMEM;

    if (pdata->host1x == NULL) {
        dev_info(&pdev->dev,"Client device resource not initialized\n");
        return -ENODEV;
    }

    syncpt_if->base = (dma_addr_t)(pdata->host1x->syncpt_phy_apt);
    syncpt_if->size = pdata->host1x->syncpt_page_size * pdata->host1x->syncpt_count;
    syncpt_if->page_size = pdata->host1x->syncpt_page_size;

    /* If IOMMU is enabled, map it into the device memory */
    if (iommu_get_domain_for_dev(&pdev->dev)) {
        syncpt_if->base = dma_map_resource(&pdev->dev, pdata->host1x->syncpt_phy_apt,
                                            syncpt_if->size,
                                            DMA_BIDIRECTIONAL,
                                            DMA_ATTR_SKIP_CPU_SYNC);
        if (dma_mapping_error(&pdev->dev, syncpt_if->base))
            return -ENOMEM;
    }

    pdata->syncpt_unit_interface = syncpt_if;
    dev_info(&pdev->dev, "syncpt_unit_base %llx syncpt_unit_size %zx size %x\n",
                pdata->host1x->syncpt_phy_apt,
                syncpt_if->size,
                syncpt_if->page_size);
    return 0;
}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_syncpt_unit_interface_init);

HOST1X_EMU_EXPORT_DECL(void, nvhost_syncpt_unit_interface_deinit(struct platform_device *pdev))
{
    struct nvhost_syncpt_interface *syncpt_if;
    struct nvhost_device_data *pdata;

    pdata = platform_get_drvdata(pdev);
    if (pdata == NULL) {
        return;
    }
    syncpt_if = pdata->syncpt_unit_interface;

    if (syncpt_if != NULL) {
        if (iommu_get_domain_for_dev(&pdev->dev)) {
            dma_unmap_resource(&pdev->dev, syncpt_if->base, syncpt_if->size,
                                DMA_BIDIRECTIONAL, DMA_ATTR_SKIP_CPU_SYNC);
        }
        devm_kfree(&pdev->dev, syncpt_if);
    }
}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_syncpt_unit_interface_deinit);

HOST1X_EMU_EXPORT_DECL(dma_addr_t, nvhost_syncpt_address(struct platform_device *pdev, u32 id))
{
        struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
        struct nvhost_syncpt_interface *syncpt_if = pdata->syncpt_unit_interface;

        if (syncpt_if == NULL) {
            dev_info(&pdev->dev,"Syncpoint unit interfac not initialized\n");
            return (dma_addr_t)NULL;
        }

        if (id >= pdata->host1x->syncpt_count) {
            dev_info(&pdev->dev,"Invalid Syncpoint ID\n");
            return (dma_addr_t)NULL;
        }
        return syncpt_if->base + syncpt_if->page_size * id;
}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_syncpt_address);

HOST1X_EMU_EXPORT_DECL(int, nvhost_intr_register_notifier(struct platform_device *pdev,
                  u32 id, u32 thresh,
                  void (*callback)(void *data),
                  void *private_data))
{
    int err;
    struct dma_fence *fence;
    struct host1x_syncpt *sp;
    struct nvhost_host1x_cb *cb;
    struct nvhost_device_data *pdata = platform_get_drvdata(pdev);

    sp = HOST1X_EMU_EXPORT_CALL(host1x_syncpt_get_by_id_noref(pdata->host1x, id));
    if (!sp)
        return -EINVAL;

    fence = HOST1X_EMU_EXPORT_CALL(host1x_fence_create(sp, thresh, true));
    if (IS_ERR(fence)) {
        pr_err("error %d during construction of fence!", (int)PTR_ERR(fence));
        return PTR_ERR(fence);
    }

    cb = kzalloc(sizeof(*cb), GFP_KERNEL);
    if (!cb) {
        dma_fence_put(fence);
        return -ENOMEM;
    }

    INIT_WORK(&cb->work, nvhost_intr_do_work);
    cb->notifier      = callback;
    cb->notifier_data = private_data;

    err = dma_fence_add_callback(fence, &cb->cb, nvhost_host1x_cb_func);
    if (err < 0) {
        dma_fence_put(fence);
        kfree(cb);
    }

    return err;
}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_intr_register_notifier);

HOST1X_EMU_EXPORT_DECL(int, nvhost_module_init(struct platform_device *pdev))
{
    return -EOPNOTSUPP;
}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_module_init);

HOST1X_EMU_EXPORT_DECL(void, nvhost_module_deinit(struct platform_device *pdev))
{
    return;
}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_module_deinit);

HOST1X_EMU_EXPORT_DECL(void, nvhost_module_reset(struct platform_device *pdev, bool reboot))
{
    return;
}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_module_reset);

HOST1X_EMU_EXPORT_DECL(int, nvhost_module_busy(struct platform_device *dev))
{
    return -EOPNOTSUPP;
}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_module_busy);

HOST1X_EMU_EXPORT_DECL(inline void, nvhost_module_idle(struct platform_device *pdev))
{
    return;
}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_module_idle);

HOST1X_EMU_EXPORT_DECL(void, nvhost_module_idle_mult(struct platform_device *pdev, int refs))
{
    return;
}
HOST1X_EMU_EXPORT_SYMBOL(nvhost_module_idle_mult);
