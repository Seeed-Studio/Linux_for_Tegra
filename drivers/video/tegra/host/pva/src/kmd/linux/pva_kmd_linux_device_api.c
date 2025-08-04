// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#include <nvidia/conftest.h>

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/dma-fence.h>
#include <linux/dma-mapping.h>
#include <linux/host1x-next.h>
#include <linux/interrupt.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/version.h>

#include "pva_kmd_silicon_utils.h"
#include "pva_kmd_linux_device_api.h"

#define NVPVA_NUM_CDEV 1

uint32_t nvpva_get_syncpt_client_managed(struct platform_device *pdev,
					 const char *syncpt_name)
{
	struct nvpva_device_data *pdata = platform_get_drvdata(pdev);
	struct host1x_syncpt *sp;

	sp = host1x_syncpt_alloc(pdata->host1x, HOST1X_SYNCPT_CLIENT_MANAGED,
				 syncpt_name ? syncpt_name :
						     dev_name(&pdev->dev));
	if (!sp)
		return 0;

	return host1x_syncpt_id(sp);
}

void nvpva_syncpt_put_ref_ext(struct platform_device *pdev, uint32_t id)
{
	struct nvpva_device_data *pdata = platform_get_drvdata(pdev);
	struct host1x_syncpt *sp;

	sp = host1x_syncpt_get_by_id_noref(pdata->host1x, id);
	if (WARN_ON(!sp))
		return;

	host1x_syncpt_put(sp);
}

int nvpva_syncpt_read_ext_check(struct platform_device *pdev, uint32_t id,
				uint32_t *val)
{
	struct nvpva_device_data *pdata = platform_get_drvdata(pdev);
	struct host1x_syncpt *sp;

	sp = host1x_syncpt_get_by_id_noref(pdata->host1x, id);
	if (!sp)
		return -EINVAL;

	*val = host1x_syncpt_read(sp);
	return 0;
}

int nvpva_syncpt_unit_interface_init(struct platform_device *pdev)
{
	struct nvpva_device_data *pdata = platform_get_drvdata(pdev);
	struct nvpva_syncpt_interface *syncpt_if;
	phys_addr_t base;
	uint32_t stride, num_syncpts;
	int err;

	syncpt_if = devm_kzalloc(&pdev->dev, sizeof(*syncpt_if), GFP_KERNEL);
	if (!syncpt_if)
		return -ENOMEM;

	err = host1x_syncpt_get_shim_info(pdata->host1x, &base, &stride,
					  &num_syncpts);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to get shim info\n");
		return err;
	}
	syncpt_if->size = stride * num_syncpts;

	syncpt_if->page_size = stride;

	/* If IOMMU is enabled, map it into the device memory */
	if (iommu_get_domain_for_dev(&pdev->dev)) {
		syncpt_if->base =
			dma_map_resource(&pdev->dev, base, syncpt_if->size,
					 DMA_BIDIRECTIONAL,
					 DMA_ATTR_SKIP_CPU_SYNC);
		if (dma_mapping_error(&pdev->dev, syncpt_if->base))
			return -ENOMEM;
	} else {
		syncpt_if->base = base;
	}

	pdata->syncpt_unit_interface = syncpt_if;

	dev_info(&pdev->dev,
		 "syncpt_unit_base %llx syncpt_unit_size %zx size %x\n", base,
		 syncpt_if->size, syncpt_if->page_size);

	return 0;
}

void nvpva_syncpt_unit_interface_deinit(struct platform_device *pdev)
{
	struct nvpva_syncpt_interface *syncpt_if;
	struct nvpva_device_data *pdata;

	if (iommu_get_domain_for_dev(&pdev->dev)) {
		pdata = platform_get_drvdata(pdev);
		syncpt_if = pdata->syncpt_unit_interface;

		dma_unmap_resource(&pdev->dev, syncpt_if->base, syncpt_if->size,
				   DMA_BIDIRECTIONAL, DMA_ATTR_SKIP_CPU_SYNC);
	}
}

int nvpva_device_get_resources(struct platform_device *pdev)
{
	struct nvpva_device_data *pdata = platform_get_drvdata(pdev);
	int err;
	uint32_t i;

	for (i = 0; i < NVPVA_MODULE_MAX_IORESOURCE_MEM; i++) {
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

void nvpva_module_deinit(struct platform_device *pdev)
{
	struct nvpva_device_data *pdata = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);

	debugfs_remove_recursive(pdata->debugfs);
}

int nvpva_module_init(struct platform_device *pdev)
{
	struct nvpva_device_data *pdata = platform_get_drvdata(pdev);
	unsigned int i;
	int err;

	err = devm_clk_bulk_get_all(&pdev->dev, &pdata->clks);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to get clocks %d\n", err);
		return err;
	}
	pdata->num_clks = err;

	for (i = 0; i < pdata->num_clks; i++) {
		err = clk_set_rate(pdata->clks[i].clk, ULONG_MAX);
		if (err < 0) {
			dev_err(&pdev->dev, "failed to set clock rate!\n");
			return err;
		}
	}

	pdata->reset_control =
		devm_reset_control_get_exclusive_released(&pdev->dev, NULL);
	if (IS_ERR(pdata->reset_control)) {
		dev_err(&pdev->dev, "failed to get reset\n");
		return PTR_ERR(pdata->reset_control);
	}

	reset_control_acquire(pdata->reset_control);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to acquire reset: %d\n", err);
		return err;
	}

	err = clk_bulk_prepare_enable(pdata->num_clks, pdata->clks);
	if (err < 0) {
		reset_control_release(pdata->reset_control);
		dev_err(&pdev->dev, "failed to enabled clocks: %d\n", err);
		return err;
	}

	reset_control_reset(pdata->reset_control);
	clk_bulk_disable_unprepare(pdata->num_clks, pdata->clks);
	reset_control_release(pdata->reset_control);

	if (pdata->autosuspend_delay) {
		pm_runtime_set_autosuspend_delay(&pdev->dev,
						 pdata->autosuspend_delay);
		pm_runtime_use_autosuspend(&pdev->dev);
	}

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev))
		return -EOPNOTSUPP;

	pdata->debugfs = debugfs_create_dir(pdev->dev.of_node->name, NULL);

	return 0;
}

static struct device *nvpva_device_create(struct platform_device *pdev,
					  struct cdev *cdev,
					  const char *cdev_name, dev_t devno,
					  const struct file_operations *ops)
{
	struct nvpva_device_data *pdata = platform_get_drvdata(pdev);
	struct device *dev;
	int err;

#if defined(NV_CLASS_CREATE_HAS_NO_OWNER_ARG) /* Linux v6.4 */
	pdata->nvpva_class = class_create(pdev->dev.of_node->name);
#else
	pdata->nvpva_class = class_create(THIS_MODULE, pdev->dev.of_node->name);
#endif
	if (IS_ERR(pdata->nvpva_class)) {
		dev_err(&pdev->dev, "failed to create class\n");
		return ERR_CAST(pdata->nvpva_class);
	}

	cdev_init(cdev, ops);
	cdev->owner = THIS_MODULE;

	err = cdev_add(cdev, devno, 1);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to add cdev\n");
		class_destroy(pdata->nvpva_class);
		return ERR_PTR(err);
	}

	dev = device_create(pdata->nvpva_class, &pdev->dev, devno, NULL,
			    (pdev->id <= 0) ? "nvhost-%s%s" : "nvhost-%s%s.%d",
			    cdev_name, pdev->dev.of_node->name, pdev->id);

	if (IS_ERR(dev)) {
		dev_err(&pdev->dev, "failed to create %s device\n", cdev_name);
		class_destroy(pdata->nvpva_class);
		cdev_del(cdev);
	}

	return dev;
}

int nvpva_device_init(struct platform_device *pdev)
{
	struct nvpva_device_data *pdata = platform_get_drvdata(pdev);
	dev_t devno;
	int err;

	err = alloc_chrdev_region(&devno, 0, NVPVA_NUM_CDEV, "nvhost");
	if (err < 0) {
		dev_err(&pdev->dev, "failed to reserve chrdev region\n");
		return err;
	}

	pdata->ctrl_node = nvpva_device_create(pdev, &pdata->ctrl_cdev, "ctrl-",
					       devno, pdata->ctrl_ops);
	if (IS_ERR(pdata->ctrl_node))
		return PTR_ERR(pdata->ctrl_node);

	pdata->cdev_region = devno;

	return 0;
}

int nvpva_device_release(struct platform_device *pdev)
{
	struct nvpva_device_data *pdata = platform_get_drvdata(pdev);

	if (!IS_ERR_OR_NULL(pdata->ctrl_node)) {
		device_destroy(pdata->nvpva_class, pdata->ctrl_cdev.dev);
		cdev_del(&pdata->ctrl_cdev);
		class_destroy(pdata->nvpva_class);
	}

	unregister_chrdev_region(pdata->cdev_region, NVPVA_NUM_CDEV);

	return 0;
}
