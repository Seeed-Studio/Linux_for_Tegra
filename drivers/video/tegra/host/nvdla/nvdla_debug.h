/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2016-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * NVDLA debug utils header
 */

#ifndef NVDLA_DEBUG_H
#define NVDLA_DEBUG_H

#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include "port/nvdla_host_wrapper.h"

#include "nvdla.h"

enum nvdla_dbg_categories {
	debug_err	= BIT(0),  /* error logs */
	debug_warn	= BIT(1),  /* warnings */
	debug_info	= BIT(2),  /* slightly verbose info */
	debug_fn	= BIT(3),  /* fn name tracing */
	debug_reg	= BIT(4),  /* reg accesses, including operation desc */
	debug_perf	= BIT(5),  /* for tracking perf impact */
	debug_fw	= BIT(6),  /* enable firmware log */
};

#ifdef CONFIG_TEGRA_NVDLA_TRACE_PRINTK
#define nvdla_dbg(check_mask, pdev, format, arg...)			\
do {									\
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);	\
	struct nvdla_device *nvdla_dev = (struct nvdla_device *)	\
					pdata->private_data;		\
	if (unlikely((check_mask) & nvdla_dev->dbg_mask)) {		\
		if (nvdla_dev->en_trace)				\
			trace_printk("%s: " format "\n",		\
					dev_name(&pdev->dev),		\
					##arg);				\
		else							\
			pr_info("%s:%s: " format "\n",			\
					dev_name(&pdev->dev),		\
					__func__, ##arg);		\
	}								\
} while (0)
#else /* CONFIG_TEGRA_NVDLA_TRACE_PRINTK */
#define nvdla_dbg(check_mask, pdev, format, arg...)			\
do {									\
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);	\
	struct nvdla_device *nvdla_dev = (struct nvdla_device *)	\
					pdata->private_data;		\
	if (unlikely((check_mask) & nvdla_dev->dbg_mask)) {		\
		pr_info("%s:%s: " format "\n",			\
					dev_name(&pdev->dev),		\
					__func__, ##arg);		\
	}								\
} while (0)
#endif

#define nvdla_dbg_err(pdev, fmt, arg...) \
	nvdla_dbg(debug_err, pdev, fmt, ##arg)

#define nvdla_dbg_warn(pdev, fmt, arg...) \
	nvdla_dbg(debug_warn, pdev, fmt, ##arg)

#define nvdla_dbg_info(pdev, fmt, arg...) \
	nvdla_dbg(debug_info, pdev, fmt, ##arg)

#define nvdla_dbg_fn(pdev, fmt, arg...) \
	nvdla_dbg(debug_fn, pdev, fmt, ##arg)

#define nvdla_dbg_reg(pdev, fmt, arg...) \
	nvdla_dbg(debug_reg, pdev, fmt, ##arg)

#define nvdla_dbg_fw(pdev, fmt, arg...) \
	nvdla_dbg(debug_fw, pdev, fmt, ##arg)

/**
 * nvdla_debug_init() initiallze dla debug utils
 * @pdev	pointer to platform device
 *
 * Return	void
 *
 */
void nvdla_debug_init(struct platform_device *pdev);

#endif /* End of NVDLA_DEBUG_H */
