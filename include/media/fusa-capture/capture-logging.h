/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2017-2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

/**
 * @brief Pipeline ID aware logging macros for FuSa Capture KMD
 */

#ifndef __CAPTURE_LOGGING_H__
#define __CAPTURE_LOGGING_H__

#include <linux/kernel.h>
#include <linux/device.h>

/* Default pipeline ID when not specified */
#define CAPTURE_PIPELINE_ID_INVALID    0xFFFFFFFFU

/* ============================================================================
 * Direct pipeline_id passing macros (for flexibility)
 * ============================================================================
 */

/* Device-based logging with pipeline ID */
#define capture_dev_err_pl(dev, pipeline_id, fmt, ...) \
	do { \
		if ((pipeline_id) != CAPTURE_PIPELINE_ID_INVALID) { \
			dev_err(dev, "[PIPELINE %u] " fmt, pipeline_id, ##__VA_ARGS__); \
		} else { \
			dev_err(dev, fmt, ##__VA_ARGS__); \
		} \
	} while (false)

#define capture_dev_warn_pl(dev, pipeline_id, fmt, ...) \
	do { \
		if ((pipeline_id) != CAPTURE_PIPELINE_ID_INVALID) { \
			dev_warn(dev, "[PIPELINE %u] " fmt, pipeline_id, ##__VA_ARGS__); \
		} else { \
			dev_warn(dev, fmt, ##__VA_ARGS__); \
		} \
	} while (false)

#define capture_dev_info_pl(dev, pipeline_id, fmt, ...) \
	do { \
		if ((pipeline_id) != CAPTURE_PIPELINE_ID_INVALID) { \
			dev_info(dev, "[PIPELINE %u] " fmt, pipeline_id, ##__VA_ARGS__); \
		} else { \
			dev_info(dev, fmt, ##__VA_ARGS__); \
		} \
	} while (false)

#define capture_dev_dbg_pl(dev, pipeline_id, fmt, ...) \
	do { \
		if ((pipeline_id) != CAPTURE_PIPELINE_ID_INVALID) { \
			dev_dbg(dev, "[PIPELINE %u] " fmt, pipeline_id, ##__VA_ARGS__); \
		} else { \
			dev_dbg(dev, fmt, ##__VA_ARGS__); \
		} \
	} while (false)

/* Kernel print variants with pipeline ID */
#define capture_pr_err_pl(pipeline_id, fmt, ...) \
	do { \
		if ((pipeline_id) != CAPTURE_PIPELINE_ID_INVALID) { \
			pr_err("[PIPELINE %u] " fmt, pipeline_id, ##__VA_ARGS__); \
		} else { \
			pr_err(fmt, ##__VA_ARGS__); \
		} \
	} while (false)

#define capture_pr_warn_pl(pipeline_id, fmt, ...) \
	do { \
		if ((pipeline_id) != CAPTURE_PIPELINE_ID_INVALID) { \
			pr_warn("[PIPELINE %u] " fmt, pipeline_id, ##__VA_ARGS__); \
		} else { \
			pr_warn(fmt, ##__VA_ARGS__); \
		} \
	} while (false)

#define capture_pr_info_pl(pipeline_id, fmt, ...) \
	do { \
		if ((pipeline_id) != CAPTURE_PIPELINE_ID_INVALID) { \
			pr_info("[PIPELINE %u] " fmt, pipeline_id, ##__VA_ARGS__); \
		} else { \
			pr_info(fmt, ##__VA_ARGS__); \
		} \
	} while (false)

/* ============================================================================
 * VI Channel context convenience macros
 * ============================================================================
 */

/* VI channel-based convenience macros - use pipeline_id from channel structure */
#define vi_chan_err(chan, fmt, ...) \
	capture_dev_err_pl((chan)->dev, (chan)->pipeline_id, fmt, ##__VA_ARGS__)

#define vi_chan_warn(chan, fmt, ...) \
	capture_dev_warn_pl((chan)->dev, (chan)->pipeline_id, fmt, ##__VA_ARGS__)

#define vi_chan_info(chan, fmt, ...) \
	capture_dev_info_pl((chan)->dev, (chan)->pipeline_id, fmt, ##__VA_ARGS__)

#define vi_chan_dbg(chan, fmt, ...) \
	capture_dev_dbg_pl((chan)->dev, (chan)->pipeline_id, fmt, ##__VA_ARGS__)

/* ============================================================================
 * ISP Channel context convenience macros
 * ============================================================================
 */

/* ISP channel-based convenience macros - use pipeline_id from channel structure */
#define isp_chan_err(chan, fmt, ...) \
	capture_dev_err_pl((chan)->isp_dev, (chan)->pipeline_id, fmt, ##__VA_ARGS__)

#define isp_chan_warn(chan, fmt, ...) \
	capture_dev_warn_pl((chan)->isp_dev, (chan)->pipeline_id, fmt, ##__VA_ARGS__)

#define isp_chan_info(chan, fmt, ...) \
	capture_dev_info_pl((chan)->isp_dev, (chan)->pipeline_id, fmt, ##__VA_ARGS__)

#define isp_chan_dbg(chan, fmt, ...) \
	capture_dev_dbg_pl((chan)->isp_dev, (chan)->pipeline_id, fmt, ##__VA_ARGS__)

#endif /* __CAPTURE_LOGGING_H__ */
