// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2014-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <nvidia/conftest.h>
#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/nvmap.h>
#include <linux/version.h>
#include <linux/kmemleak.h>
#include <linux/io.h>
#include <linux/nvmap_t19x.h>
#include <linux/platform_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/sched/clock.h>
#include <linux/cma.h>
#include <linux/dma-map-ops.h>

#include "include/linux/nvmap_exports.h"
#include "nvmap_dev.h"
#include "nvmap_alloc.h"
#include "nvmap_handle.h"
#include "nvmap_dev_int.h"

#include <soc/tegra/virt/hv-ivc.h>
#include <soc/tegra/virt/syscalls.h>

static struct device tegra_generic_dev;

static struct device tegra_vpr_dev;
static struct device tegra_vpr1_dev;

static struct device tegra_generic_cma_dev;
static struct device tegra_vpr_cma_dev;

static struct platform_device *pdev;
extern ulong nvmap_init_time;

static const struct of_device_id nvmap_of_ids[] = {
	{ .compatible = "nvidia,carveouts" },
        { }
};
MODULE_DEVICE_TABLE(of, nvmap_of_ids);

static struct acpi_device_id nvmap_acpi_ids[] = {
	{
		.id = "NVDA300B",
		.driver_data = 0,
	},
	{ },
};
MODULE_DEVICE_TABLE(acpi, nvmap_acpi_ids);

static struct nvmap_platform_carveout nvmap_carveouts[] = {
	[0] = {
		.name		= "generic-0",
		.usage_mask	= NVMAP_HEAP_CARVEOUT_GENERIC,
		.base		= 0,
		.size		= 0,
		.dma_dev	= &tegra_generic_dev,
		.cma_dev	= &tegra_generic_cma_dev,
		.numa_node_id = 0,
	},
	[1] = {
		.name		= "vpr",
		.usage_mask	= NVMAP_HEAP_CARVEOUT_VPR,
		.base		= 0,
		.size		= 0,
		.dma_dev	= &tegra_vpr_dev,
		.cma_dev	= &tegra_vpr_cma_dev,
		.enable_static_dma_map = true,
		.numa_node_id = 0,
	},
	[2] = {
		.name		= "vpr1",
		.usage_mask	= NVMAP_HEAP_CARVEOUT_VPR,
		.base		= 0,
		.size		= 0,
		.dma_dev	= &tegra_vpr1_dev,
		.enable_static_dma_map = true,
		.numa_node_id = 1,
	},
	[3] = {
		.name		= "vidmem",
		.usage_mask	= NVMAP_HEAP_CARVEOUT_VIDMEM,
		.base		= 0,
		.size		= 0,
		.disable_dynamic_dma_map = true,
		.no_cpu_access = true,
		.numa_node_id = 0,
	},
	[4] = {
		.name		= "fsi",
		.usage_mask	= NVMAP_HEAP_CARVEOUT_FSI,
		.base		= 0,
		.size		= 0,
		.numa_node_id = 0,
	},
	[5] = {
		.name		= "vimem",
		.usage_mask	= NVMAP_HEAP_CARVEOUT_VI,
		.base		= 0,
		.size		= 0,
		.numa_node_id = 0,
	},
	/* Need uninitialized entries for IVM carveouts */
	[6] = {
		.name		= NULL,
		.usage_mask	= NVMAP_HEAP_CARVEOUT_IVM,
		.numa_node_id = 0,
	},
	[7] = {
		.name		= NULL,
		.usage_mask	= NVMAP_HEAP_CARVEOUT_IVM,
		.numa_node_id = 0,
	},
	[8] = {
		.name		= NULL,
		.usage_mask	= NVMAP_HEAP_CARVEOUT_IVM,
		.numa_node_id = 0,
	},
	[9] = {
		.name		= NULL,
		.usage_mask	= NVMAP_HEAP_CARVEOUT_IVM,
		.numa_node_id = 0,
	},
};

static struct nvmap_platform_data nvmap_data = {
	.carveouts	= nvmap_carveouts,
	.nr_carveouts	= 6,
};

static struct nvmap_platform_carveout *nvmap_get_carveout_pdata(const char *name)
{
	struct nvmap_platform_carveout *co;
	for (co = nvmap_carveouts;
	     co < nvmap_carveouts + ARRAY_SIZE(nvmap_carveouts); co++) {
		int i = min_t(int, strcspn(name, "_"), strcspn(name, "-"));
		/* handle IVM carveouts */
		if ((co->usage_mask == NVMAP_HEAP_CARVEOUT_IVM) &&  !co->name)
			goto found;

		if (strncmp(co->name, name, i))
			continue;
found:
		co->dma_dev = co->dma_dev ? co->dma_dev : &co->dev;
		return co;
	}
	pr_err("not enough space for all nvmap carveouts\n");
	return NULL;
}

static int __init nvmap_populate_ivm_carveout(struct device *dev)
{
	char *name;
	const __be32 *prop;
	int ret = 0;
	struct nvmap_platform_carveout *co;
	struct of_phandle_iterator it;
	struct tegra_hv_ivm_cookie *ivm;
	unsigned long long id;
	unsigned int guestid, result;
	bool is_vi_heap;

	if (!of_phandle_iterator_init(&it, dev->of_node, "memory-region", NULL, 0)) {
		while (!of_phandle_iterator_next(&it) && it.node) {
			is_vi_heap = false;
			if (of_device_is_available(it.node)) {
				if (of_device_is_compatible(it.node, "nvidia,ivm_carveout") > 0) {
					co = nvmap_get_carveout_pdata("nvidia,ivm_carveout");
				} else if (of_device_is_compatible(it.node,
					"nvidia,vi_carveout") > 0) {
					co = nvmap_get_carveout_pdata("vimem");
					is_vi_heap = true;
				} else
					continue;

				if (!co) {
					ret = -ENOMEM;
					goto err;
				}

				if (hyp_read_gid(&guestid)) {
					pr_err("failed to read gid\n");
					ret = -EINVAL;
					goto err;
				}

				prop = of_get_property(it.node, "ivm", NULL);
				if (!prop) {
					pr_err("failed to read ivm property\n");
					ret = -EINVAL;
					goto err;
				}

				id = of_read_number(prop + 1, 1);
				if (id > UINT_MAX) {
					ret = -EINVAL;
					goto err;
				}
				ivm = tegra_hv_mempool_reserve(id);
				if (IS_ERR_OR_NULL(ivm)) {
					pr_err("failed to reserve IVM memory pool %llu\n", id);
					ret = -ENOMEM;
					goto err;
				}
				/* XXX: Are these the available fields from IVM cookie? */
				co->base     = (phys_addr_t)ivm->ipa;
				co->peer     = ivm->peer_vmid;
				co->size     = ivm->size;
				co->vmid     = guestid;

				if (co->base == 0U || co->size == 0U) {
					ret = -EINVAL;
					goto fail;
				}

				/* See if this VM can allocate (or just create handle from ID)
				 * generated by peer partition
				 */
				if (!is_vi_heap) {
					prop = of_get_property(it.node, "alloc", NULL);
					if (!prop) {
						pr_err("failed to read alloc property\n");
						ret = -EINVAL;
						goto fail;
					}

					name = kzalloc(32, GFP_KERNEL);
					if (!name) {
						ret = -ENOMEM;
						goto fail;
					}

					co->can_alloc = of_read_number(prop, 1);
					co->is_ivm    = true;
					sprintf(name, "ivm%02u%02u%02d", co->vmid, co->peer,
						co->can_alloc);
					pr_info("IVM carveout IPA:%p, size=%zu, peer vmid=%u,"
						"name=%s\n", (void *)(uintptr_t)co->base, co->size,
						co->peer, name);
					co->name      = name;
				}

				if (check_add_overflow(nvmap_data.nr_carveouts, 1U, &result)) {
					co->name = NULL;
					kfree(name);
					ret = -EINVAL;
					goto fail;
				}

				nvmap_data.nr_carveouts = result;
			}
		}
	}
	return 0;
fail:
	(void)tegra_hv_mempool_unreserve(ivm);
	co->base     = 0;
	co->peer     = 0;
	co->size     = 0;
	co->vmid     = 0;
err:
	return ret;

}

/*
 * This requires proper kernel arguments to have been passed.
 */

static int __nvmap_init_dt(struct platform_device *pdev)
{
	if (!of_match_device(nvmap_of_ids, &pdev->dev) &&
		!(acpi_match_device(nvmap_acpi_ids, &pdev->dev))) {
		pr_err("Missing DT or ACPI entry!\n");
		return -EINVAL;
	}

	pdev->dev.platform_data = &nvmap_data;

	return 0;
}

struct device *nvmap_get_vpr_dev(void)
{
	struct device_node *dn = of_find_compatible_node(NULL, NULL, "nvidia,vpr-carveout");

	if (!dn)
		return NULL;

	if (!of_device_is_available(dn)) {
		of_node_put(dn);
		return NULL;
	}

	of_node_put(dn);
	return &tegra_vpr_dev;
}
EXPORT_SYMBOL(nvmap_get_vpr_dev);

struct device *nvmap_get_vpr1_dev(void)
{
	struct device_node *dn = of_find_compatible_node(NULL, NULL, "nvidia,vpr1-carveout");

	if (!dn)
		return NULL;

	if (!of_device_is_available(dn)) {
		of_node_put(dn);
		return NULL;
	}

	of_node_put(dn);
	return &tegra_vpr1_dev;
}
EXPORT_SYMBOL(nvmap_get_vpr1_dev);

static int __init nvmap_co_device_init(struct reserved_mem *rmem,
					struct device *dev)
{
	struct nvmap_platform_carveout *co = rmem->priv;
	int err = 0;

	if (!co)
		return -ENODEV;

	/* if co size is 0, => co is not present. So, skip init. */
	if (!co->size)
		return 0;

	if (co->cma_dev == NULL) {
		err = nvmap_dma_declare_coherent_memory(co->dma_dev, 0,
				co->base, co->size,
				DMA_MEMORY_NOMAP);
		if (!err) {
			pr_info("%s :dma coherent mem declare %pa,%zu\n",
				 co->name, &co->base, co->size);
			co->init_done = true;
			err = 0;
		} else
			pr_err("%s :dma coherent mem declare fail %pa,%zu,err:%d\n",
				co->name, &co->base, co->size, err);
	} else {
			co->init_done = true;
	}
	if (co->init_done)
		set_dev_node(co->dma_dev, co->numa_node_id);
	return err;
}

static void nvmap_co_device_release(struct reserved_mem *rmem, struct device *dev)
{
	return;
}

static const struct reserved_mem_ops nvmap_co_ops = {
	.device_init	= nvmap_co_device_init,
	.device_release	= nvmap_co_device_release,
};

static int __init nvmap_co_setup(struct reserved_mem *rmem)
{
	struct nvmap_platform_carveout *co;
	ulong start_time = sched_clock();
	int ret = 0;
	ulong result;

	co = nvmap_get_carveout_pdata(rmem->name);
	if (co == NULL) {
		ret = -ENOMEM;
		goto exit;
	}

	rmem->ops = &nvmap_co_ops;
	rmem->priv = co;

	co->base = rmem->base;
	co->size = rmem->size;
	co->cma_dev = NULL;

	if (check_sub_overflow((ulong)sched_clock(), start_time, &result) ||
		check_add_overflow(nvmap_init_time, result, &result))
		return -EOVERFLOW;

	nvmap_init_time = result;
exit:
	return ret;
}

/*
 * Fills in the platform data either from the device tree or with the
 * legacy path.
 */
int __init nvmap_init(struct platform_device *pdev)
{
	int err;
	struct reserved_mem rmem;
	struct reserved_mem *rmem2;
	struct device_node *np = pdev->dev.of_node;
	struct of_phandle_iterator it;
	const char *compp;

	if (!of_phandle_iterator_init(&it, np, "memory-region", NULL, 0)) {
		while (!of_phandle_iterator_next(&it) && it.node) {
			if (of_device_is_available(it.node) &&
			    !of_device_is_compatible(it.node, "nvidia,ivm_carveout") &&
			    !of_device_is_compatible(it.node, "nvidia,vi_carveout")) {
				rmem2 = of_reserved_mem_lookup(it.node);
				if (!rmem2) {
					if (!of_property_read_string(it.node, "compatible", &compp))
						pr_err("unable to acquire memory-region: %s\n",
							compp);
					return -EINVAL;
				}

				err = nvmap_co_setup(rmem2);
				if (err)
					goto end;
			}
		}
	}

	if (pdev->dev.of_node || ACPI_HANDLE(&pdev->dev)) {
		err = __nvmap_init_dt(pdev);
		if (err)
			return err;
	}

	err = of_reserved_mem_device_init(&pdev->dev);
	if (err)
		pr_debug("reserved_mem_device_init fails, try legacy init\n");

	/* try legacy init */
	if (!nvmap_carveouts[0].init_done) {
		rmem.priv = &nvmap_carveouts[0];
		err = nvmap_co_device_init(&rmem, &pdev->dev);
		if (err)
			goto end;
	}

	if (!nvmap_carveouts[1].init_done) {
		rmem.priv = &nvmap_carveouts[1];
		err = nvmap_co_device_init(&rmem, &pdev->dev);
		if (err)
			goto end;
	}

	err = nvmap_populate_ivm_carveout(&pdev->dev);

end:
	return err;
}

static bool nvmap_is_carveout_node_present(void)
{
	struct device_node *np;

	np = of_find_node_by_name(NULL, "tegra-carveouts");
	if (of_device_is_available(np)) {
		of_node_put(np);
		return true;
	}
	of_node_put(np);
	return false;
}

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void nvmap_remove_wrapper(struct platform_device *pdev)
{
	nvmap_remove(pdev);
}
#else
static int nvmap_remove_wrapper(struct platform_device *pdev)
{
	return nvmap_remove(pdev);
}
#endif

static struct platform_driver __refdata nvmap_driver = {
	.probe		= nvmap_probe,
	.remove	= nvmap_remove_wrapper,

	.driver = {
		.name	= "tegra-carveouts",
		.owner	= THIS_MODULE,
		.of_match_table = nvmap_of_ids,
#ifdef CONFIG_ACPI
		.acpi_match_table = nvmap_acpi_ids,
#endif
		.suppress_bind_attrs = true,
	},
};

static int __init nvmap_init_driver(void)
{
	int e = 0;

	e = nvmap_heap_init();
	if (e)
		goto fail;

	e = platform_driver_register(&nvmap_driver);
	if (e) {
		nvmap_heap_deinit();
		goto fail;
	}

fail:
	return e;
}

module_init(nvmap_init_driver);

static void __exit nvmap_exit_driver(void)
{
	if (!nvmap_is_carveout_node_present())
		platform_device_unregister(pdev);
	platform_driver_unregister(&nvmap_driver);
	nvmap_heap_deinit();
	nvmap_dev = NULL;
}
module_exit(nvmap_exit_driver);
#if defined(NV_MODULE_IMPORT_NS_CALLS_STRINGIFY)
MODULE_IMPORT_NS(DMA_BUF);
#else
MODULE_IMPORT_NS("DMA_BUF");
#endif
MODULE_DESCRIPTION("NvMap: Nvidia Tegra Memory Management Driver");
MODULE_AUTHOR("Ketan Patil <ketanp@nvidia.com>");
MODULE_AUTHOR("Ashish Mhetre <amhetre@nvidia.com>");
MODULE_AUTHOR("Pritesh Raithatha <praithatha@nvidia.com>");
MODULE_LICENSE("GPL v2");
