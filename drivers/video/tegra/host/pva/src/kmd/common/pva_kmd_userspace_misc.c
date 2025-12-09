// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#include "pva_kmd_mutex.h"
#include "pva_kmd_utils.h"
#include "pva_kmd_thread_sema.h"
#include "pva_kmd_device_memory.h"
#include "pva_kmd_device.h"
#include "pva_kmd_shim_init.h"
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

enum pva_error pva_kmd_mutex_init(pva_kmd_mutex_t *m)
{
	int ret = pthread_mutex_init(m, NULL);
	ASSERT(ret == 0);

	return PVA_SUCCESS;
}

void pva_kmd_mutex_lock(pva_kmd_mutex_t *m)
{
	int ret = pthread_mutex_lock(m);
	ASSERT(ret == 0);
}

void pva_kmd_mutex_unlock(pva_kmd_mutex_t *m)
{
	int ret = pthread_mutex_unlock(m);
	ASSERT(ret == 0);
}

void pva_kmd_mutex_deinit(pva_kmd_mutex_t *m)
{
	int ret = pthread_mutex_destroy(m);
	ASSERT(ret == 0);
}

void *pva_kmd_zalloc(uint64_t size)
{
	return calloc(1, size);
}

void pva_kmd_free(void *ptr)
{
	if (ptr != NULL) {
		free(ptr);
	}
}

void pva_kmd_fault(void)
{
	pva_kmd_log_err("PVA KMD fault");
	exit(1);
}

void pva_kmd_sema_init(pva_kmd_sema_t *sem, uint32_t val)
{
	int ret;

	ret = sem_init(sem, 0 /* Only sharing in threads */, val);
	ASSERT(ret == 0);
}

enum pva_error pva_kmd_sema_wait_timeout(pva_kmd_sema_t *sem,
					 uint32_t timeout_ms)
{
	struct timespec ts;
	int ret;
	ret = clock_gettime(CLOCK_REALTIME, &ts);
	ASSERT(ret == 0);

	/* Add timeout (specified in milliseconds) to the current time */
	{
		uint32_t sec_part = timeout_ms / 1000U;
		uint32_t nsec_part = (timeout_ms % 1000U) * 1000000U;
		ts.tv_sec += (time_t)sec_part;
		ts.tv_nsec += (long)nsec_part;
	}

	/* Handle case where nanoseconds exceed 1 second */
	if (ts.tv_nsec >= 1000000000) {
		ts.tv_nsec -= 1000000000;
		ts.tv_sec += 1;
	}

wait_again:
	ret = sem_timedwait(sem, &ts);
	if (ret == -1) {
		int saved_errno = errno;
		if (saved_errno == ETIMEDOUT) {
			pva_kmd_log_err("pva_kmd_sema_wait_timeout Timed out");
			return PVA_TIMEDOUT;
		} else if (saved_errno == EINTR) {
			goto wait_again;
		} else {
			FAULT("Unexpected sem_timedwait error");
		}
	}

	return PVA_SUCCESS;
}

void pva_kmd_sema_deinit(pva_kmd_sema_t *sem)
{
	int ret = sem_destroy(sem);
	ASSERT(ret == 0);
}

void pva_kmd_sema_post(pva_kmd_sema_t *sem)
{
	int ret = sem_post(sem);
	ASSERT(ret == 0);
}

struct pva_kmd_device_memory *
pva_kmd_device_memory_alloc_map(uint64_t size, struct pva_kmd_device *pva,
				uint32_t iova_access_flags,
				uint8_t smmu_ctx_idx)
{
	struct pva_kmd_device_memory *mem;
	enum pva_error err;

	mem = pva_kmd_device_memory_alloc(size);

	if (mem == NULL) {
		goto err_out;
	}

	err = pva_kmd_device_memory_iova_map(mem, pva, iova_access_flags,
					     smmu_ctx_idx);
	if (err != PVA_SUCCESS) {
		goto free_mem;
	}

	err = pva_kmd_device_memory_cpu_map(mem);
	if (err != PVA_SUCCESS) {
		goto iova_unmap;
	}

	return mem;
iova_unmap:
	pva_kmd_device_memory_iova_unmap(mem);
free_mem:
	pva_kmd_device_memory_free(mem);
err_out:
	return NULL;
}

void pva_kmd_atomic_store(pva_kmd_atomic_t *a_var, int val)
{
	atomic_store(a_var, val);
}

int pva_kmd_atomic_fetch_add(pva_kmd_atomic_t *a_var, int val)
{
	/* MISRA Deviation: Atomic to non-atomic conversion is required for return value */
	int result = (int)atomic_fetch_add(a_var, val);
	return result;
}

int pva_kmd_atomic_fetch_sub(pva_kmd_atomic_t *a_var, int val)
{
	/* MISRA Deviation: Atomic to non-atomic conversion is required for return value */
	int result = (int)atomic_fetch_sub(a_var, val);
	return result;
}

int pva_kmd_atomic_load(pva_kmd_atomic_t *a_var)
{
	/* MISRA Deviation: Atomic to non-atomic conversion is required for return value */
	int result = (int)atomic_load(a_var);
	return result;
}

bool pva_kmd_device_maybe_on(struct pva_kmd_device *pva)
{
	bool device_on = false;

	pva_kmd_mutex_lock(&pva->powercycle_lock);
	if (pva->refcount > 0U) {
		device_on = true;
	}
	pva_kmd_mutex_unlock(&pva->powercycle_lock);
	return device_on;
}
