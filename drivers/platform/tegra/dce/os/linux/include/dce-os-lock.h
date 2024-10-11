/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef DCE_OS_LOCK_H
#define DCE_OS_LOCK_H

#include <linux/mutex.h>

struct dce_os_mutex {
	struct mutex mutex;
};

/**
 * dce_os_mutex_init - Initialize dce_os_mutex in Linux style
 *
 * mutex : pointer to the mutext to be initialized.
 *
 * Return : 0 if successful
 */
static inline int dce_os_mutex_init(struct dce_os_mutex *mutex)
{
	mutex_init(&mutex->mutex);
	return 0;
};

/**
 * dce_os_mutex_lock - Acquire the dce_os_mutex in Linux style
 *
 * mutex : pointer to the mutext to be acquired.
 *
 * Return : void
 */
static inline void dce_os_mutex_lock(struct dce_os_mutex *mutex)
{
	mutex_lock(&mutex->mutex);
};

/**
 * dce_os_mutex_unlock - Release the dce_os_mutex in Linux style
 *
 * mutex : pointer to the mutext to be released.
 *
 * Return : void
 */
static inline void dce_os_mutex_unlock(struct dce_os_mutex *mutex)
{
	mutex_unlock(&mutex->mutex);
};

/**
 * dce_os_mutex_destroy - Destroy the dce_os_mutex in Linux style
 *
 * mutex : pointer to the mutext to be destroyed.
 *
 * Return : 0 if successful
 */
static inline void dce_os_mutex_destroy(struct dce_os_mutex *mutex)
{
	mutex_destroy(&mutex->mutex);
};

#endif /* DCE_OS_LOCK_H */
