// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_api_types.h"
#include "pva_kmd_shim_init.h"
#include "pva_kmd_utils.h"
#include "pva_api_cmdbuf.h"
#include "pva_api.h"
#include "pva_kmd_constants.h"
#include "pva_fw.h"
#include "pva_bit.h"
#include "pva_kmd_queue.h"
#include "pva_kmd_resource_table.h"
#include "pva_kmd_device.h"
#include "pva_kmd_context.h"
#include "pva_kmd_t23x.h"
#include "pva_kmd_t26x.h"
#include "pva_kmd_regs.h"
#include "pva_kmd_device_memory.h"
#include "pva_kmd_fw_profiler.h"
#include "pva_kmd_fw_debug.h"
#include "pva_kmd_vpu_app_auth.h"
#include "pva_utils.h"
#include "pva_kmd_debugfs.h"
#include "pva_kmd_tegra_stats.h"
#include "pva_kmd_shim_silicon.h"
#include "pva_kmd_shared_buffer.h"

#include "pva_kmd_abort.h"
/**
 * @brief Send address and size of the resource table to FW through CCQ.
 *
 * Initialization through CCQ is only intended for KMD's own resource table (the
 * first resource table created).
 */
void pva_kmd_send_resource_table_info_by_ccq(
	struct pva_kmd_device *pva, struct pva_kmd_resource_table *res_table)
{
	enum pva_error err;
	uint64_t addr = res_table->table_mem->iova;
	uint32_t n_entries = res_table->n_entries;
	uint64_t ccq_entry =
		PVA_INSERT64(PVA_FW_CCQ_OP_SET_RESOURCE_TABLE,
			     PVA_FW_CCQ_OPCODE_MSB, PVA_FW_CCQ_OPCODE_LSB) |
		PVA_INSERT64(addr, PVA_FW_CCQ_RESOURCE_TABLE_ADDR_MSB,
			     PVA_FW_CCQ_RESOURCE_TABLE_ADDR_LSB) |
		PVA_INSERT64(n_entries, PVA_FW_CCQ_RESOURCE_TABLE_N_ENTRIES_MSB,
			     PVA_FW_CCQ_RESOURCE_TABLE_N_ENTRIES_LSB);

	pva_kmd_mutex_lock(&pva->ccq0_lock);
	err = pva_kmd_ccq_push_with_timeout(pva, PVA_PRIV_CCQ_ID, ccq_entry,
					    PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
					    PVA_KMD_WAIT_FW_TIMEOUT_US);
	ASSERT(err == PVA_SUCCESS);
	pva_kmd_mutex_unlock(&pva->ccq0_lock);
}

/**
 * @brief Send address and size of the queue to FW through CCQ.
 *
 * Initialization through CCQ is only intended for KMD's own queue (the first
 * queue created).
 */
void pva_kmd_send_queue_info_by_ccq(struct pva_kmd_device *pva,
				    struct pva_kmd_queue *queue)
{
	enum pva_error err;
	uint64_t addr = queue->queue_memory->iova;
	uint32_t max_submit = queue->max_num_submit;
	uint64_t ccq_entry =
		PVA_INSERT64(PVA_FW_CCQ_OP_SET_SUBMISSION_QUEUE,
			     PVA_FW_CCQ_OPCODE_MSB, PVA_FW_CCQ_OPCODE_LSB) |
		PVA_INSERT64(addr, PVA_FW_CCQ_QUEUE_ADDR_MSB,
			     PVA_FW_CCQ_QUEUE_ADDR_LSB) |
		PVA_INSERT64(max_submit, PVA_FW_CCQ_QUEUE_N_ENTRIES_MSB,
			     PVA_FW_CCQ_QUEUE_N_ENTRIES_LSB);
	pva_kmd_mutex_lock(&pva->ccq0_lock);
	err = pva_kmd_ccq_push_with_timeout(pva, PVA_PRIV_CCQ_ID, ccq_entry,
					    PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
					    PVA_KMD_WAIT_FW_TIMEOUT_US);
	ASSERT(err == PVA_SUCCESS);
	pva_kmd_mutex_unlock(&pva->ccq0_lock);
}

/**
 * Initialize submission related data structures for this device.
 *
 * - Create a resource table.
 * - Add DRAM resources to the resource table. These are used for command buffer
 *   chunks and post fences.
 * - Create a queue.
 */
static void pva_kmd_device_init_submission(struct pva_kmd_device *pva)
{
	uint32_t queue_mem_size;
	uint64_t chunk_mem_size;
	uint64_t size;
	enum pva_error err;
	struct pva_fw_postfence post_fence = { 0 };

	/* Init KMD's queue */
	queue_mem_size = pva_get_submission_queue_memory_size(
		PVA_KMD_MAX_NUM_KMD_SUBMITS);

	pva->queue_memory = pva_kmd_device_memory_alloc_map(
		queue_mem_size, pva, PVA_ACCESS_RW, PVA_R5_SMMU_CONTEXT_ID);
	ASSERT(pva->queue_memory != NULL);

	pva_kmd_queue_init(&pva->dev_queue, pva, PVA_PRIV_CCQ_ID,
			   0 /* KMD's queue ID is 0 */, &pva->ccq0_lock,
			   pva->queue_memory, PVA_KMD_MAX_NUM_KMD_SUBMITS);

	/* Init KMD's resource table */
	err = pva_kmd_resource_table_init(&pva->dev_resource_table, pva,
					  PVA_R5_SMMU_CONTEXT_ID,
					  PVA_KMD_MAX_NUM_KMD_RESOURCES,
					  PVA_KMD_MAX_NUM_KMD_DMA_CONFIGS);
	ASSERT(err == PVA_SUCCESS);

	/* Allocate memory for submission*/
	chunk_mem_size = pva_kmd_cmdbuf_pool_get_required_mem_size(
		PVA_MAX_CMDBUF_CHUNK_SIZE, PVA_KMD_MAX_NUM_KMD_CHUNKS);

	size = safe_addu64(chunk_mem_size, (uint64_t)sizeof(uint32_t));
	/* Allocate one post fence at the end. We don't need to free this memory
	 * explicitly as it will be freed after we drop the resource. */
	pva->submit_memory = pva_kmd_device_memory_alloc_map(
		size, pva, PVA_ACCESS_RW, PVA_R5_SMMU_CONTEXT_ID);
	ASSERT(pva->submit_memory != NULL);

	/* Add submit memory to resource table */
	err = pva_kmd_add_dram_buffer_resource(&pva->dev_resource_table,
					       pva->submit_memory,
					       &pva->submit_memory_resource_id);
	ASSERT(err == PVA_SUCCESS);
	pva_kmd_update_fw_resource_table(&pva->dev_resource_table);

	/* Init chunk pool */
	pva_kmd_cmdbuf_chunk_pool_init(
		&pva->chunk_pool, pva->submit_memory_resource_id, 0,
		chunk_mem_size, PVA_MAX_CMDBUF_CHUNK_SIZE,
		PVA_KMD_MAX_NUM_KMD_CHUNKS, pva->submit_memory->va);

	/* Init fence */
	pva->fence_offset = chunk_mem_size;

	/* Init submitter */
	pva_kmd_mutex_init(&pva->submit_lock);
	pva_kmd_mutex_init(&pva->chunk_pool_lock);
	post_fence.resource_id = pva->submit_memory_resource_id;
	post_fence.offset_lo = iova_lo(pva->fence_offset);
	post_fence.offset_hi = iova_hi(pva->fence_offset);
	post_fence.ts_resource_id = PVA_RESOURCE_ID_INVALID;
	pva_kmd_submitter_init(
		&pva->submitter, &pva->dev_queue, &pva->submit_lock,
		&pva->chunk_pool, &pva->chunk_pool_lock,
		pva_offset_pointer(pva->submit_memory->va, pva->fence_offset),
		&post_fence);
}

static void pva_kmd_device_deinit_submission(struct pva_kmd_device *pva)
{
	pva_kmd_mutex_deinit(&pva->chunk_pool_lock);
	pva_kmd_mutex_deinit(&pva->submit_lock);
	pva_kmd_cmdbuf_chunk_pool_deinit(&pva->chunk_pool);
	/* Submit memory will be freed after dropping the resource */
	pva_kmd_drop_resource(&pva->dev_resource_table,
			      pva->submit_memory_resource_id);
	pva_kmd_resource_table_deinit(&pva->dev_resource_table);
	pva_kmd_queue_deinit(&pva->dev_queue);
	pva_kmd_device_memory_free(pva->queue_memory);
}

struct pva_kmd_device *pva_kmd_device_create(enum pva_chip_id chip_id,
					     uint32_t device_index,
					     bool app_authenticate)
{
	struct pva_kmd_device *pva;
	enum pva_error err;
	uint32_t chunk_size;
	uint32_t size;

	pva = pva_kmd_zalloc_nofail(sizeof(*pva));

	pva->device_index = device_index;
	pva->load_from_gsc = false;
	pva->is_hv_mode = true;
	pva->max_n_contexts = PVA_MAX_NUM_USER_CONTEXTS;
	pva_kmd_mutex_init(&pva->powercycle_lock);
	pva_kmd_mutex_init(&pva->ccq0_lock);
	pva_kmd_sema_init(&pva->fw_boot_sema, 0);
	size = safe_mulu32((uint32_t)sizeof(struct pva_kmd_context),
			   pva->max_n_contexts);
	pva->context_mem = pva_kmd_zalloc(size);
	ASSERT(pva->context_mem != NULL);

	err = pva_kmd_block_allocator_init(&pva->context_allocator,
					   pva->context_mem,
					   PVA_KMD_USER_CONTEXT_ID_BASE,
					   sizeof(struct pva_kmd_context),
					   pva->max_n_contexts);
	ASSERT(err == PVA_SUCCESS);

	if (chip_id == PVA_CHIP_T23X) {
		pva_kmd_device_init_t23x(pva);
	} else if (chip_id == PVA_CHIP_T26X) {
		pva_kmd_device_init_t26x(pva);
	} else {
		FAULT("SOC not supported");
	}

	pva_kmd_device_plat_init(pva);

	chunk_size = safe_mulu32((uint32_t)sizeof(struct pva_syncpt_rw_info),
				 (uint32_t)PVA_NUM_RW_SYNCPTS_PER_CONTEXT);
	err = pva_kmd_block_allocator_init(&pva->syncpt_allocator,
					   pva->syncpt_rw, 0, chunk_size,
					   PVA_MAX_NUM_USER_CONTEXTS);
	ASSERT(err == PVA_SUCCESS);

	pva_kmd_device_init_submission(pva);

	err = pva_kmd_init_vpu_app_auth(pva, app_authenticate);
	ASSERT(err == PVA_SUCCESS);

	pva->is_suspended = false;

#if PVA_IS_DEBUG == 1
	pva->fw_debug_log_level = 255U;
#else
	pva->fw_debug_log_level = 0U;
#endif

	return pva;
}

static void pva_kmd_wait_for_active_contexts(struct pva_kmd_device *pva)
{
	uint8_t allocated = 0;

	/* Make sure no context is active by allocating all contexts here. */
	while (allocated < pva->max_n_contexts) {
		uint32_t unused_id;
		struct pva_kmd_context *ctx;

		ctx = pva_kmd_alloc_block(&pva->context_allocator, &unused_id);
		if (ctx != NULL) {
			allocated = safe_addu32(allocated, 1U);
		} else {
			pva_kmd_sleep_us(1000);
		}
	}
}

void pva_kmd_device_destroy(struct pva_kmd_device *pva)
{
	pva_kmd_wait_for_active_contexts(pva);
	pva_kmd_device_deinit_submission(pva);
	pva_kmd_device_plat_deinit(pva);
	pva_kmd_block_allocator_deinit(&pva->syncpt_allocator);
	pva_kmd_block_allocator_deinit(&pva->context_allocator);
	pva_kmd_free(pva->context_mem);
	pva_kmd_mutex_deinit(&pva->ccq0_lock);
	pva_kmd_mutex_deinit(&pva->powercycle_lock);
	pva_kmd_deinit_vpu_app_auth(pva);
	pva_kmd_free(pva);
}

static enum pva_error
pva_kmd_notify_fw_set_profiling_level(struct pva_kmd_device *pva,
				      uint32_t level)
{
	struct pva_kmd_cmdbuf_builder builder;
	struct pva_kmd_submitter *dev_submitter = &pva->submitter;
	struct pva_cmd_set_profiling_level *cmd;
	uint32_t fence_val;
	enum pva_error err;

	err = pva_kmd_submitter_prepare(dev_submitter, &builder);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	cmd = pva_kmd_reserve_cmd_space(&builder, sizeof(*cmd));
	ASSERT(cmd != NULL);
	pva_kmd_set_cmd_set_profiling_level(cmd, level);

	err = pva_kmd_submitter_submit(dev_submitter, &builder, &fence_val);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	err = pva_kmd_submitter_wait(dev_submitter, fence_val,
				     PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
				     PVA_KMD_WAIT_FW_TIMEOUT_US);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err(
			"Waiting for FW timed out when setting profiling level");
		goto err_out;
	}

	return PVA_SUCCESS;

err_out:
	return err;
}
enum pva_error pva_kmd_device_busy(struct pva_kmd_device *pva)
{
	enum pva_error err = PVA_SUCCESS;

	pva_kmd_mutex_lock(&pva->powercycle_lock);
	if (pva->refcount == 0) {
		pva_kmd_allocate_syncpts(pva);

		err = pva_kmd_power_on(pva);
		if (err != PVA_SUCCESS) {
			goto unlock;
		}

		err = pva_kmd_init_fw(pva);
		if (err != PVA_SUCCESS) {
			goto poweroff;
		}
		/* Reset KMD queue */
		pva->dev_queue.queue_header->cb_head = 0;
		pva->dev_queue.queue_header->cb_tail = 0;

		pva_kmd_send_resource_table_info_by_ccq(
			pva, &pva->dev_resource_table);
		pva_kmd_send_queue_info_by_ccq(pva, &pva->dev_queue);

		// TODO: need better error handling here
		err = pva_kmd_shared_buffer_init(
			pva, PVA_PRIV_CCQ_ID, PVA_KMD_FW_BUF_ELEMENT_SIZE,
			PVA_KMD_FW_PROFILING_BUF_NUM_ELEMENTS, NULL, NULL);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err_u64(
				"pva kmd buffer initialization failed for interface ",
				PVA_PRIV_CCQ_ID);
			goto deinit_fw;
		}
		pva_kmd_notify_fw_enable_profiling(pva);

		/* Set FW debug log level */
		pva_kmd_notify_fw_set_debug_log_level(pva,
						      pva->fw_debug_log_level);

		// If the user had set profiling level before power-on, send the update to FW
		pva_kmd_notify_fw_set_profiling_level(
			pva, pva->debugfs_context.profiling_level);
	}
	pva->refcount = safe_addu32(pva->refcount, 1U);

	pva_kmd_mutex_unlock(&pva->powercycle_lock);
	return PVA_SUCCESS;

deinit_fw:
	pva_kmd_deinit_fw(pva);
poweroff:
	pva_kmd_power_off(pva);
unlock:
	pva_kmd_mutex_unlock(&pva->powercycle_lock);
	return err;
}

void pva_kmd_device_idle(struct pva_kmd_device *pva)
{
	enum pva_error err = PVA_SUCCESS;

	pva_kmd_mutex_lock(&pva->powercycle_lock);
	ASSERT(pva->refcount > 0);
	pva->refcount--;
	if (pva->refcount == 0) {
		if (!pva->recovery) {
			/* Disable FW profiling */
			/* TODO: once debugfs is up, move these calls */
			pva_kmd_notify_fw_disable_profiling(pva);
		}
		// TOOD: need better error handling here
		err = pva_kmd_shared_buffer_deinit(pva, PVA_PRIV_CCQ_ID);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err("pva_kmd_shared_buffer_deinit failed");
		}
		pva_kmd_deinit_fw(pva);
		pva_kmd_power_off(pva);
	}
	pva_kmd_mutex_unlock(&pva->powercycle_lock);
}

enum pva_error pva_kmd_ccq_push_with_timeout(struct pva_kmd_device *pva,
					     uint8_t ccq_id, uint64_t ccq_entry,
					     uint64_t sleep_interval_us,
					     uint64_t timeout_us)
{
	/* spin until we have space or timeout reached */
	while (pva_kmd_get_ccq_space(pva, ccq_id) == 0) {
		if (timeout_us == 0) {
			pva_kmd_log_err(
				"pva_kmd_ccq_push_with_timeout Timed out");
			pva_kmd_abort(pva);
			return PVA_TIMEDOUT;
		}
		pva_kmd_sleep_us(sleep_interval_us);
		timeout_us = sat_sub64(timeout_us, sleep_interval_us);
	}
	/* TODO: memory write barrier is needed here */
	pva_kmd_ccq_push(pva, ccq_id, ccq_entry);

	return PVA_SUCCESS;
}

bool pva_kmd_device_maybe_on(struct pva_kmd_device *pva)
{
	bool device_on = false;

	pva_kmd_mutex_lock(&pva->powercycle_lock);
	if (pva->refcount > 0) {
		device_on = true;
	}
	pva_kmd_mutex_unlock(&pva->powercycle_lock);
	return device_on;
}
