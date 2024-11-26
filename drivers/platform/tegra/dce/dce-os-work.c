// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <linux/workqueue.h>
#include <dce-os-work.h>
#include <dce-os-types.h>
#include <dce-os-utils.h>
#include <dce-os-log.h>

struct dce_os_wq_struct {
	struct workqueue_struct *wq;
};

struct dce_os_work_struct {
	void *data;
	struct work_struct work;
	void (*dce_os_work_fn)(void *data);
};

 /**
  * dce_os_work_handle_fn : handler function for scheduled dce-work
  *
  * @work : Pointer to the scheduled work
  *
  * Return : void
  */
static void dce_os_work_handle_fn(struct work_struct *work)
{
	struct dce_os_work_struct *dce_os_work = container_of(work,
							struct dce_os_work_struct,
							work);

	if (dce_os_work->dce_os_work_fn != NULL)
		dce_os_work->dce_os_work_fn(dce_os_work->data);
}

int dce_os_wq_work_init(struct tegra_dce *d,
	dce_os_work_handle_t *p_work_handle,
	void (*work_fn)(void *data), void *data)
{
	struct dce_os_work_struct *p_work = NULL;
	int ret = 0;

	p_work = dce_os_kzalloc(d, sizeof(*p_work), false);
	if (p_work == NULL) {
		ret = -ENOMEM;
		dce_os_err(d, "Work alloc failed");
		goto fail;
	}

	p_work->data = data;
	p_work->dce_os_work_fn = work_fn;

	INIT_WORK(&p_work->work, dce_os_work_handle_fn);

	*p_work_handle = p_work;

fail:
	return ret;
}

void dce_os_wq_work_deinit(struct tegra_dce *d,
	dce_os_work_handle_t work_handle)
{
	dce_os_kfree(d, work_handle);
}

int dce_os_wq_work_schedule(struct tegra_dce *d,
	dce_os_wq_handle_t wq_handle,
	dce_os_work_handle_t work_handle)
{
	int ret = 0;
	struct workqueue_struct *wq = NULL;

	if (work_handle == NULL) {
		dce_os_err(d, "Invalid input work handle.");
		ret = -1;
		goto fail;
	}

	// If the OS Work queue is NULL, then use default sys hipri wq.
	if (wq_handle == NULL) {
		wq = system_highpri_wq;
	} else {
		/**
		 * If the OS Work queue is not NULL, then client must've
		 * initialized the work queue.
		 */
		wq = wq_handle->wq;
		if (wq == NULL) {
			dce_os_err(d, "Invalid WQ");
			ret = -1;
			goto fail;
		}
	}

	queue_work(wq, &work_handle->work);

fail:
	return ret;
}

int dce_os_wq_create(struct tegra_dce *d,
	dce_os_wq_handle_t *p_wq_handle, const char *wq_name)
{
	int ret = 0;
	struct dce_os_wq_struct *p_wq = NULL;

	if ((p_wq_handle == NULL) || (wq_name == NULL)) {
		ret = -EINVAL;
		dce_os_err(d, "Invalid input args");
		goto fail;
	}

	p_wq = dce_os_kzalloc(d, sizeof(*p_wq), false);
	if (p_wq == NULL) {
		ret = -ENOMEM;
		dce_os_err(d, "WQ alloc failed");
		goto fail;
	}

	p_wq->wq = create_singlethread_workqueue(wq_name);

	*p_wq_handle = p_wq;

fail:
	return ret;
}

void dce_os_wq_destroy(struct tegra_dce *d,
	dce_os_wq_handle_t wq_handle)
{
	if ((wq_handle == NULL) || (wq_handle->wq == NULL)) {
		dce_os_err(d, "Invalid input args");
		goto fail;
	}

	flush_workqueue(wq_handle->wq);
	destroy_workqueue(wq_handle->wq);

	dce_os_kfree(d, wq_handle);

fail:
	return;
}
