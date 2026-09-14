// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES.  All rights reserved.
 *
 * Tegra TSEC Module Support
 */

#ifndef TSEC_COMMS_PLAT_H
#define TSEC_COMMS_PLAT_H

#define LVL_INFO (1)
#define LVL_DBG  (2)
#define LVL_WARN (3)
#define LVL_ERR  (4)

#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/semaphore.h>
#include <nvidia/conftest.h>
#include "../tsec.h"

extern struct platform_device *g_tsec;

#define EXPORT_SYMBOL_COMMS(sym) EXPORT_SYMBOL(sym)

#define TSEC_EINVAL EINVAL
#define TSEC_ENODEV ENODEV

#define plat_print(level, fmt, ...) \
do { \
	if (level == LVL_INFO) \
		dev_info(&g_tsec->dev, fmt, ##__VA_ARGS__); \
	else if (level == LVL_DBG) \
		dev_dbg(&g_tsec->dev, fmt, ##__VA_ARGS__); \
	else if (level == LVL_WARN) \
		dev_warn(&g_tsec->dev, fmt, ##__VA_ARGS__); \
	else if (level == LVL_ERR) \
		dev_err(&g_tsec->dev, fmt, ##__VA_ARGS__); \
} while (0)

#if defined(NV_DEFINE_SEMAPHORE_HAS_NUMBER_ARG)
#define PLAT_DEFINE_SEMA(s)  DEFINE_SEMAPHORE(s, 0)
#else
#define PLAT_DEFINE_SEMA(s)  DEFINE_SEMAPHORE(s)
#endif
#define PLAT_INIT_SEMA(s, v) sema_init(s, v)
#define PLAT_UP_SEMA(s)      up(s)
#define PLAT_DOWN_SEMA(s)    down_interruptible(s)

#elif __DCE_KERNEL__

// Functions to be implemented by DCE

#else

// Platform not supported

#endif

typedef void (*tsec_plat_work_cb_t)(void *);

/* @brief: API to write a register r with the value specified by v.
 *
 * usage:  Writes a register r with a value specified by v.
 *
 * params[in]:	r register address to write to
 *              v value to write
 */
void tsec_plat_reg_write(u32 r, u32 v);

/* @brief: API to Read a register specified by address r.
 *
 * usage:  Reads a register specified by address r.
 *
 * params[in]:	r register to read from
 *
 * params[out]:	value that is read
 */
uint32_t tsec_plat_reg_read(u32 r);

/* @brief: Adds a delay of usec micro-seconds.
 *
 * usage:  Add a delay
 *
 * params[in]:	usec delay specified in micro-seconds.
 */
void tsec_plat_udelay(u64 usec);

/* @brief: The Tsec comms unit needs a comms mutex for its internal
 * synchronization. Tsec driver provides this mutex. This is an API
 * to acquire the mutex.
 *
 * usage:  Called to acquire mutex provided by Tsec driver.
 */
void tsec_plat_acquire_comms_mutex(void);

/* @brief: API to release the mutex provided by TSec driver.
 *
 * usage:  Called to release the mutex acquired by
 * tsec_plat_acquire_comms_mutex.
 */
void tsec_plat_release_comms_mutex(void);

/* @brief: A generic API to queue a work item. This work item
 * will later be scheduled in work queue's thread/task context.
 *
 * usage:  Used for queueing a work item.
 *
 * params[in]:	cb  callback
 *              ctx context
 */
void tsec_plat_queue_work(tsec_plat_work_cb_t cb, void *ctx);

/*
 * @brief: Power On Tsec
 */
static inline void tsec_plat_poweron(void)
{
	tsec_poweron(&g_tsec->dev);
}

/*
 * @brief: Power Off Tsec
 */
static inline void tsec_plat_poweroff(void)
{
	tsec_poweroff(&g_tsec->dev);
}

u32 tsec_plat_cmdq_head_r(u32 r);
u32 tsec_plat_cmdq_tail_r(u32 r);
u32 tsec_plat_msgq_head_r(u32 r);
u32 tsec_plat_msgq_tail_r(u32 r);
u32 tsec_plat_ememc_r(u32 r);
u32 tsec_plat_ememd_r(u32 r);

#endif /* TSEC_COMMS_PLAT_H */
