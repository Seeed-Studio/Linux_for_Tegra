/* SPDX-License-Identifier: MIT */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
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
