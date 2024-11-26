/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef DCE_OS_WORK_H
#define DCE_OS_WORK_H

struct tegra_dce;

// Opaque work structs defined per OS implementation.
typedef struct dce_os_wq_struct *dce_os_wq_handle_t;
typedef struct dce_os_work_struct *dce_os_work_handle_t;

/*
 * dce_os_wq_create : Create DCE OS Work queue.
 *
 * @d : Pointer to tegra_dce struct.
 * @p_wq_handle : Pointer to DCE OS WQ handle to create.
 * @wq_name : Worker queue name.
 *
 * Return : 0 if successful.
 */
int dce_os_wq_create(struct tegra_dce *d,
	dce_os_wq_handle_t *p_wq_handle, const char *wq_name);

/*
 * dce_os_wq_destroy : Destroy DCE OS Work queue.
 *
 * @d : Pointer to tegra_dce struct.
 * @wq_handle : DCE OS WQ handle to destroy.
 *
 * Return : void.
 *
 * Note: Destroy will also flush the WQ.
 * This function should ensure that flush
 * doesn't fail if WQ is empty.
 */
void dce_os_wq_destroy(struct tegra_dce *d,
	dce_os_wq_handle_t wq_handle);

/*
 * dce_os_wq_work_init : Init dce work structure.
 *
 * @d : Pointer to tegra_dce struct.
 * @p_work_handle : Pointer to DCE OS Work handle to init.
 * @work_fn : Worker function to be called.
 * @data : Input data pointer for worker function.
 *
 * Return : 0 if successful
 */
int dce_os_wq_work_init(struct tegra_dce *d,
	dce_os_work_handle_t *p_work_handle,
	void (*work_fn)(void *data), void *data);

/*
 * dce_os_wq_work_deinit : Deinit dce work structure.
 *
 * @d : Pointer to tegra_dce struct.
 * @work_handle : DCE os work handle.
 *
 * Return : void
 */
void dce_os_wq_work_deinit(struct tegra_dce *d,
	dce_os_work_handle_t work_handle);

/*
 * dce_os_wq_work_schedule : Schedule dce work.
 *
 * @d : Pointer to tegra_dce struct.
 * @wq_handle : Work Queue handle to schedule work to.
 *	NULL for default WQ.
 * @work_handle : DCE OS Work handle to schedule.
 *
 * Return : 0 if successful.
 */
int dce_os_wq_work_schedule(struct tegra_dce *d,
	dce_os_wq_handle_t wq_handle,
	dce_os_work_handle_t work_handle);

#endif /* DCE_OS_WORK_H */
