// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION & AFFILIATES.  All rights reserved.
 *
 * Tegra TSEC Module Support
 */

#ifndef TSEC_COMMS_H
#define TSEC_COMMS_H

typedef void (*callback_func_t)(void *, void *);

/* -------- Tsec driver internal functions to be called by platform dependent code --------- */

/* @brief: Initialises IPC CO and reserves pages on the same.
 *
 * usage: To be called when tsec driver is initialised. Must be
 * called  before any other API is used from the comms lib.
 *
 * params[in]: ipc_co_va       carveout base virtual address
 *             ipc_co_va_size  carveout address space size
 */
void tsec_comms_initialize(u64 ipc_co_va, u64 ipc_co_va_size);

/* @brief: This function will drain all the messages
 * from the tsec queue. It is called when interrupt is
 * received from TSec. It should be called in threaded
 * context and not interrupt context.
 *
 * usage: To be called when interrupt is received from TSec.
 *
 * params[in]: invoke_cb indicates whether to invoke callback or not.
 */
void tsec_comms_drain_msg(bool invoke_cb);

/* -------- END -------- */

/* -------- Exported functions which are invoked from DisplayRM. -------- */

/* @brief: Sets callback for init message
 *
 * usage: Called for setting callback for init msg
 *
 * params[in]:  cb_func function to be called after init msg is
 *                      received
 *              cb_ctx  pointer to callback context
 *
 * params[out]: return value(0 for success).
 */
int tsec_comms_set_init_cb(callback_func_t cb_func, void *cb_ctx);

/* @brief: Clear callback for init message
 *
 * usage: When DisplayRM is unloaded it would call this API to
 * clear the init callback it previousy set.
 *
 * params[in]:  NONE
 * params[out]: NONE
 */
void tsec_comms_clear_init_cb(void);

/* @brief: Send the command upon receiving it by putting it into the
 * tsec queue. Also sets appropriate callback to be called when
 * response arrives.
 *
 * usage: Called when sending a command to tsec.
 *
 * params[in]: cmd      pointer to the memory containing the command
 *             queue_id Id of the queue being used.
 *             cb_func  callback function tobe registered
 *             cb_ctx   pointer to context of the callback function.
 *
 * params[out]: return value(0 for success)
 */
int tsec_comms_send_cmd(void *cmd, u32 queue_id,
	callback_func_t cb_func, void *cb_ctx);

/* @brief: Retrieves a page from the carevout memory
 *
 * usage: Called to get a particular page from the carveout.
 *
 * params[in]: page_number  page number
 * params[in/out]: gscco_offset filled with offset of the allocated co page
 * params[out]: return value - ccplex va for the co page or NULL if
 * page_number more than number of available pages
 */
void *tsec_comms_get_gscco_page(u32 page_number, u32 *gscco_offset);


/* @brief: Allocates memory from carveout
 *
 * usage: Called to allocate memory from the carveout.
 *
 * params[in]: size_in_bytes conveys the required size (must be less
 * than page size)
 * params[in/out]: gscco_offset filled with offset of the allocated co memory
 * params[out]: return value - ccplex va for the co memory or NULL if
 * allocation failure
 */
void *tsec_comms_alloc_mem_from_gscco(u32 size_in_bytes, u32 *gscco_offset);

/* @brief: Free the memory previously allocated using
 *         tsec_comms_alloc_mem_from_gscco
 *
 * params[in]: page_va previously allocated using
 *             tsec_comms_alloc_mem_from_gscco
 */
void tsec_comms_free_gscco_mem(void *page_va);


/* @brief: Gets and saves the TSEC context and then shutsdown TSEC
 *
 * params[out]: return value(0 for success)
 */
int tsec_comms_shutdown(void);


typedef int (*shutdown_callback_func_t)(void);

/* @brief: Registers a callback function to cleanly shutdown external
 * firmware running on tsec
 *
 * params[in]: func is the call back function to shutdown external
 * firmware running on tsec
 */
void tsec_comms_register_shutdown_callback(shutdown_callback_func_t func);

/* -------- END -------- */

#endif /* TSEC_COMMS_H */
