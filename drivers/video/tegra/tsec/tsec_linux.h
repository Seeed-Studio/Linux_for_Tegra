/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024, NVIDIA CORPORATION.  All rights reserved.
 *
 * Tegra TSEC Module Support
 */

#ifndef TSEC_LINUX_H
#define TSEC_LINUX_H

#include <linux/types.h>                   /* for types like u8, u32 etc */
#include <linux/platform_device.h>         /* for platform_device */
#include <linux/of_device.h>               /* for of_match_device etc */
#include <linux/slab.h>                    /* for kzalloc */
#include <linux/delay.h>                   /* for udelay */
#include <linux/clk.h>                     /* for clk_prepare_enable */
#include <linux/reset.h>                   /* for reset_control_reset */
#include <linux/iommu.h>                   /* for dev_iommu_fwspec_get */
#include <linux/iopoll.h>                  /* for readl_poll_timeout */
#include <linux/dma-mapping.h>             /* for dma_map_page_attrs */
#include <linux/pm.h>                      /* for dev_pm_ops */
#include <linux/version.h>                 /* for KERNEL_VERSION */
#include <linux/interrupt.h>               /* for enable_irq */
#include <linux/firmware.h>                /* for request_firmware */
#if (KERNEL_VERSION(5, 14, 0) <= LINUX_VERSION_CODE)
#include <soc/tegra/mc.h>                  /* for tegra_mc_get_carveout_info */
#include <linux/libnvdimm.h>               /* for arch_invalidate_pmem */
#else
#include <linux/platform/tegra/tegra_mc.h> /* for mc_get_carveout_info */
#include <asm/cacheflush.h>                /* for __flush_dcache_area */
#endif
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>                 /* for debugfs APIs */
#endif
#include <linux/sizes.h>                   /* for SZ_* size macros */

#endif /* TSEC_LINUX_H */
