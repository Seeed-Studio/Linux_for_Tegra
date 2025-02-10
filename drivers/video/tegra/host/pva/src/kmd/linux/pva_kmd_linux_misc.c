/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, NVIDIA Corporation.  All Rights Reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation.  Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include "pva_kmd_mutex.h"
#include "pva_kmd_thread_sema.h"
#include "pva_kmd_utils.h"

void *pva_kmd_zalloc(uint64_t size)
{
	return kvzalloc(size, GFP_KERNEL);
}

void pva_kmd_free(void *ptr)
{
	kvfree(ptr);
}

void pva_kmd_print_str(const char *str)
{
	printk(KERN_INFO "%s", str);
}

void pva_kmd_print_str_u64(const char *str, uint64_t n)
{
	printk(KERN_INFO "%s:%llu", str, n);
}

enum pva_error pva_kmd_mutex_init(pva_kmd_mutex_t *m)
{
	mutex_init(m);
	return PVA_SUCCESS;
}

void pva_kmd_mutex_lock(pva_kmd_mutex_t *m)
{
	mutex_lock(m);
}

void pva_kmd_mutex_unlock(pva_kmd_mutex_t *m)
{
	mutex_unlock(m);
}

void pva_kmd_mutex_deinit(pva_kmd_mutex_t *m)
{
	mutex_destroy(m);
}

void pva_kmd_fault()
{
	BUG();
}

void pva_kmd_sleep_us(uint64_t us)
{
	usleep_range(us, safe_mulu64(2, us));
}

void pva_kmd_sema_init(pva_kmd_sema_t *sem, uint32_t val)
{
	sema_init(sem, val);
}

enum pva_error pva_kmd_sema_wait_timeout(pva_kmd_sema_t *sem,
					 uint32_t timeout_ms)
{
	long timeout_jiffies = usecs_to_jiffies(safe_mulu64(timeout_ms, 1000u));
	int err = down_timeout(sem, timeout_jiffies);
	if (err == -ETIME) {
		return PVA_TIMEDOUT;
	} else if (err == -EINTR) {
		return PVA_AGAIN;
	} else {
		return PVA_SUCCESS;
	}
}

void pva_kmd_sema_deinit(pva_kmd_sema_t *sem)
{
}

void pva_kmd_sema_post(pva_kmd_sema_t *sem)
{
	up(sem);
}
