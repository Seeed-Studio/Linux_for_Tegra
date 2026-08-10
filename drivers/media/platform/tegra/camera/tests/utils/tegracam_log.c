// SPDX-License-Identifier: GPL-2.0
/*
 * tegracam_log - tegra camera test logging utility
 *
 * Copyright (c) 2018-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 */
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/module.h>
#include <linux/stdarg.h>

#include "tegracam_log.h"

static DEFINE_MUTEX(global_log_mutex);
static int (*global_log)(const char *fmt, va_list args);

int camtest_log(const char *fmt, ...)
{
	va_list args;
	int ret;

	if (fmt == NULL)
		return -EINVAL;

	va_start(args, fmt);
	if (global_log == NULL)
		ret = vprintk(fmt, args);
	else
		ret = global_log(printk_skip_level(fmt), args);
	va_end(args);

	return ret;
}
EXPORT_SYMBOL_GPL(camtest_log);

int camtest_try_acquire_global_log(int (*log)(const char *fmt, va_list args))
{
	if (log == NULL)
		return -EINVAL;

	if (!mutex_trylock(&global_log_mutex))
		return -EBUSY;

	global_log = log;

	return 0;
}
EXPORT_SYMBOL_GPL(camtest_try_acquire_global_log);

void camtest_release_global_log(void)
{
	global_log = NULL;
	mutex_unlock(&global_log_mutex);
}
EXPORT_SYMBOL_GPL(camtest_release_global_log);

MODULE_LICENSE("GPL");
