// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/version.h>
#include "pva_kmd_mutex.h"
#include "pva_kmd_thread_sema.h"
#include "pva_kmd_utils.h"

void *pva_kmd_zalloc(uint64_t size)
{
	void *ptr = kvzalloc(size, GFP_KERNEL);

	if (IS_ERR_OR_NULL(ptr)) {
		return NULL;
	}
	return ptr;
}

void pva_kmd_free(void *ptr)
{
	kvfree(ptr);
}

void pva_kmd_print_str(const char *str)
{
	printk(KERN_INFO "%s\n", str);
}

void pva_kmd_print_str_u64(const char *str, uint64_t n)
{
	printk(KERN_INFO "%s:%llu\n", str, n);
}

void pva_kmd_print_str_hex32(const char *str, uint32_t n)
{
	printk("%s: 0x%08x\n", str, n);
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

uint64_t pva_kmd_get_time_tsc(void)
{
	uint64_t timestamp;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)
	timestamp = arch_timer_read_counter();
#else
	timestamp = arch_counter_get_cntvct();
#endif
	return timestamp;
}

void pva_kmd_atomic_store(pva_kmd_atomic_t *atomic_val, int val)
{
	atomic_set(atomic_val, val);
}

int pva_kmd_atomic_fetch_add(pva_kmd_atomic_t *atomic_val, int val)
{
	return atomic_fetch_add(val, atomic_val);
}

int pva_kmd_atomic_fetch_sub(pva_kmd_atomic_t *atomic_val, int val)
{
	return atomic_fetch_sub(val, atomic_val);
}

int pva_kmd_atomic_load(pva_kmd_atomic_t *atomic_val)
{
	return atomic_read(atomic_val);
}
