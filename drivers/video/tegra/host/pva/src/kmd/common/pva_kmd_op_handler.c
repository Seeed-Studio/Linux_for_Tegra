// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#include "pva_kmd_op_handler.h"
#include "pva_kmd_resource_table.h"
#include "pva_kmd_device_memory.h"
#include "pva_kmd_cmdbuf.h"
#include "pva_kmd_device.h"
#include "pva_kmd_cmdbuf.h"
#include "pva_kmd_queue.h"
#include "pva_kmd_constants.h"
#include "pva_fw.h"
#include "pva_kmd_vpu_app_auth.h"
#include "pva_math_utils.h"

struct pva_kmd_buffer {
	void const *base;
	uint32_t offset;
	uint32_t size;
};

/* Offset will always be multiple of 8 bytes */
static void incr_offset(struct pva_kmd_buffer *buf, uint32_t incr)
{
	buf->offset = safe_addu32(buf->offset, incr);
	buf->offset =
		safe_pow2_roundup_u32(buf->offset, (uint32_t)sizeof(uint64_t));
}

static bool access_ok(struct pva_kmd_buffer const *buf, uint32_t size)
{
	return safe_addu32(buf->offset, size) <= buf->size;
}

static void *read_data(struct pva_kmd_buffer *buf, uint32_t size)
{
	void *data = (void *)((uint8_t *)buf->base + buf->offset);
	incr_offset(buf, size);
	return data;
}

static void write_data(struct pva_kmd_buffer *buf, void const *data,
		       uint32_t size)
{
	memcpy((uint8_t *)buf->base + buf->offset, data, size);
	incr_offset(buf, size);
}

static enum pva_error
pva_kmd_op_memory_register_async(struct pva_kmd_context *ctx,
				 struct pva_kmd_buffer *in_buffer,
				 struct pva_kmd_buffer *out_buffer,
				 struct pva_kmd_cmdbuf_builder *cmdbuf_builder)
{
	enum pva_error err = PVA_SUCCESS;
	struct pva_kmd_memory_register_in_args *args;
	struct pva_kmd_register_out_args out_args = { 0 };
	struct pva_kmd_device_memory *dev_mem;
	struct pva_cmd_update_resource_table *update_cmd;
	struct pva_resource_entry entry = { 0 };
	uint8_t smmu_ctx_id;

	uint32_t resource_id = 0;

	if (!access_ok(out_buffer, sizeof(struct pva_kmd_register_out_args))) {
		return PVA_INVAL;
	}

	if (!access_ok(in_buffer,
		       sizeof(struct pva_kmd_memory_register_in_args))) {
		err = PVA_INVAL;
		goto err_out;
	}

	args = read_data(in_buffer,
			 sizeof(struct pva_kmd_memory_register_in_args));

	dev_mem = pva_kmd_device_memory_acquire(args->memory_handle,
						args->offset, args->size, ctx);
	if (dev_mem == NULL) {
		err = PVA_NOMEM;
		goto err_out;
	}

	if (args->segment == PVA_MEMORY_SEGMENT_R5) {
		smmu_ctx_id = PVA_R5_SMMU_CONTEXT_ID;
	} else {
		smmu_ctx_id = ctx->smmu_ctx_id;
	}

	err = pva_kmd_device_memory_iova_map(dev_mem, ctx->pva,
					     args->access_flags, smmu_ctx_id);
	if (err != PVA_SUCCESS) {
		goto release;
	}

	err = pva_kmd_add_dram_buffer_resource(&ctx->ctx_resource_table,
					       dev_mem, &resource_id);
	if (err != PVA_SUCCESS) {
		goto unmap;
	}

	update_cmd =
		pva_kmd_reserve_cmd_space(cmdbuf_builder, sizeof(*update_cmd));
	if (update_cmd == NULL) {
		pva_kmd_log_err("Unable to reserve command buffer space");
		err = PVA_NOMEM;
		goto free_dram_buffer_resource;
	}

	err = pva_kmd_make_resource_entry(&ctx->ctx_resource_table, resource_id,
					  &entry);
	if (err != PVA_SUCCESS) {
		goto free_cmdbuf;
	}

	pva_kmd_set_cmd_update_resource_table(
		update_cmd, ctx->resource_table_id, resource_id, &entry);

	out_args.error = PVA_SUCCESS;
	out_args.resource_id = resource_id;
	write_data(out_buffer, &out_args, sizeof(out_args));
	return err;
free_cmdbuf:
	pva_kmd_cmdbuf_builder_cancel(cmdbuf_builder);
free_dram_buffer_resource:
	pva_kmd_drop_resource(&ctx->ctx_resource_table, resource_id);
unmap:
	pva_kmd_device_memory_iova_unmap(dev_mem);
release:
	pva_kmd_device_memory_free(dev_mem);
err_out:
	out_args.error = err;
	write_data(out_buffer, &out_args, sizeof(out_args));
	return err;
}

static enum pva_error pva_kmd_op_executable_register_async(
	struct pva_kmd_context *ctx, struct pva_kmd_buffer *in_buffer,
	struct pva_kmd_buffer *out_buffer,
	struct pva_kmd_cmdbuf_builder *cmdbuf_builder)
{
	enum pva_error err = PVA_SUCCESS;
	struct pva_kmd_executable_register_in_args *args;
	struct pva_kmd_exec_register_out_args out_args = { 0 };
	struct pva_cmd_update_resource_table *update_cmd;
	struct pva_resource_entry entry = { 0 };
	struct pva_kmd_resource_record *rec;
	uint32_t num_symbols = 0;
	void *exec_data;

	uint32_t resource_id = 0;

	if (!access_ok(out_buffer,
		       sizeof(struct pva_kmd_exec_register_out_args))) {
		return PVA_INVAL;
	}

	if (!access_ok(in_buffer,
		       sizeof(struct pva_kmd_executable_register_in_args))) {
		err = PVA_INVAL;
		goto err_out;
	}

	args = read_data(in_buffer,
			 sizeof(struct pva_kmd_executable_register_in_args));

	if (!access_ok(in_buffer, args->size)) {
		err = PVA_INVAL;
		goto err_out;
	}

	exec_data = read_data(in_buffer, args->size);

	err = pva_kmd_verify_exectuable_hash(ctx->pva, (uint8_t *)exec_data,
					     args->size);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	err = pva_kmd_add_vpu_bin_resource(&ctx->ctx_resource_table, exec_data,
					   args->size, &resource_id);
	if (err == PVA_SUCCESS) {
		rec = pva_kmd_use_resource(&ctx->ctx_resource_table,
					   resource_id);
		ASSERT(rec != NULL);
		num_symbols = rec->vpu_bin.symbol_table.n_symbols;
		pva_kmd_drop_resource(&ctx->ctx_resource_table, resource_id);
	}
	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	update_cmd =
		pva_kmd_reserve_cmd_space(cmdbuf_builder, sizeof(*update_cmd));
	if (update_cmd == NULL) {
		pva_kmd_log_err("Unable to reserve memory in command buffer");
		err = PVA_NOMEM;
		goto drop_resource;
	}
	ASSERT(update_cmd != NULL);

	err = pva_kmd_make_resource_entry(&ctx->ctx_resource_table, resource_id,
					  &entry);
	ASSERT(err == PVA_SUCCESS);

	pva_kmd_set_cmd_update_resource_table(
		update_cmd, ctx->resource_table_id, resource_id, &entry);

	out_args.error = PVA_SUCCESS;
	out_args.resource_id = resource_id;
	out_args.num_symbols = num_symbols;
	write_data(out_buffer, &out_args, sizeof(out_args));
	return err;
drop_resource:
	pva_kmd_drop_resource(&ctx->ctx_resource_table, resource_id);
err_out:
	out_args.error = err;
	write_data(out_buffer, &out_args, sizeof(out_args));
	return err;
}

static enum pva_error
pva_kmd_op_dma_register_async(struct pva_kmd_context *ctx,
			      struct pva_kmd_buffer *in_buffer,
			      struct pva_kmd_buffer *out_buffer,
			      struct pva_kmd_cmdbuf_builder *cmdbuf_builder)
{
	enum pva_error err = PVA_SUCCESS;
	struct pva_kmd_dma_config_register_in_args *args;
	struct pva_kmd_register_out_args out_args = { 0 };
	struct pva_cmd_update_resource_table *update_cmd;
	struct pva_resource_entry entry = { 0 };
	void *dma_cfg_data;
	uint32_t dma_cfg_payload_size;
	uint32_t resource_id = 0;
	uint32_t dma_config_size = 0;

	if (!access_ok(out_buffer, sizeof(struct pva_kmd_register_out_args))) {
		return PVA_INVAL;
	}

	if (!access_ok(in_buffer,
		       sizeof(struct pva_kmd_dma_config_register_in_args))) {
		return PVA_INVAL;
	}

	args = read_data(in_buffer,
			 sizeof(struct pva_kmd_dma_config_register_in_args));

	dma_cfg_data = &args->dma_config_header;
	dma_cfg_payload_size = in_buffer->size - in_buffer->offset;
	// Discard the data we are about to pass to pva_kmd_add_dma_config_resource
	read_data(in_buffer, dma_cfg_payload_size);

	dma_config_size =
		safe_addu32(dma_cfg_payload_size,
			    (uint32_t)sizeof(args->dma_config_header));
	err = pva_kmd_add_dma_config_resource(&ctx->ctx_resource_table,
					      dma_cfg_data, dma_config_size,
					      &resource_id);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	update_cmd =
		pva_kmd_reserve_cmd_space(cmdbuf_builder, sizeof(*update_cmd));
	if (update_cmd == NULL) {
		err = PVA_NOMEM;
		goto drop_dma_config;
	}

	err = pva_kmd_make_resource_entry(&ctx->ctx_resource_table, resource_id,
					  &entry);
	ASSERT(err == PVA_SUCCESS);

	pva_kmd_set_cmd_update_resource_table(
		update_cmd, ctx->resource_table_id, resource_id, &entry);

	out_args.error = PVA_SUCCESS;
	out_args.resource_id = resource_id;
	write_data(out_buffer, &out_args, sizeof(out_args));

	return PVA_SUCCESS;
drop_dma_config:
	pva_kmd_drop_resource(&ctx->ctx_resource_table, resource_id);
err_out:
	out_args.error = err;
	write_data(out_buffer, &out_args, sizeof(out_args));
	/* Error is reported in the output buffer. So we return success here.  */
	return PVA_SUCCESS;
}

static enum pva_error
pva_kmd_op_unregister_async(struct pva_kmd_context *ctx,
			    struct pva_kmd_buffer *in_buffer,
			    struct pva_kmd_buffer *out_buffer,
			    struct pva_kmd_cmdbuf_builder *cmdbuf_builder)
{
	enum pva_error err = PVA_SUCCESS;
	struct pva_kmd_unregister_in_args *args;
	struct pva_cmd_unregister_resource *unreg_cmd;

	if (!access_ok(in_buffer, sizeof(struct pva_kmd_unregister_in_args))) {
		err = PVA_INVAL;
		goto err_out;
	}

	args = read_data(in_buffer, sizeof(struct pva_kmd_unregister_in_args));

	unreg_cmd =
		pva_kmd_reserve_cmd_space(cmdbuf_builder, sizeof(*unreg_cmd));
	if (unreg_cmd == NULL) {
		pva_kmd_log_err(
			"Unable to reserve memory for unregister command");
		err = PVA_NOMEM;
		goto err_out;
	}

	pva_kmd_set_cmd_unregister_resource(unreg_cmd, args->resource_id);

	return PVA_SUCCESS;
err_out:
	return err;
}

static enum pva_error pva_kmd_async_ops_handler(
	struct pva_kmd_context *ctx, struct pva_fw_postfence *post_fence,
	struct pva_kmd_buffer *in_arg, struct pva_kmd_buffer *out_arg)
{
	struct pva_kmd_cmdbuf_builder cmdbuf_builder;
	enum pva_error err = PVA_SUCCESS;
	uint32_t wait_time = 0;

	//first check if we have space in queue
	while (pva_kmd_queue_space(&ctx->ctx_queue) == 0) {
		pva_kmd_sleep_us(PVA_KMD_WAIT_FW_POLL_INTERVAL_US);
		wait_time += PVA_KMD_WAIT_FW_POLL_INTERVAL_US;
		if (wait_time > PVA_KMD_WAIT_FW_TIMEOUT_US) {
			err = PVA_TIMEDOUT;
			goto out;
		}
	}

	err = pva_kmd_submitter_prepare(&ctx->submitter, &cmdbuf_builder);
	if (err != PVA_SUCCESS) {
		goto out;
	}

	while (access_ok(in_arg, sizeof(struct pva_kmd_op_header))) {
		struct pva_kmd_op_header *header =
			read_data(in_arg, sizeof(struct pva_kmd_op_header));

		if (header->op_type >= PVA_KMD_OP_MAX) {
			err = PVA_INVAL;
			goto out;
		}

		switch (header->op_type) {
		case PVA_KMD_OP_MEMORY_REGISTER:
			err = pva_kmd_op_memory_register_async(
				ctx, in_arg, out_arg, &cmdbuf_builder);
			break;

		case PVA_KMD_OP_EXECUTABLE_REGISTER:
			err = pva_kmd_op_executable_register_async(
				ctx, in_arg, out_arg, &cmdbuf_builder);
			break;

		case PVA_KMD_OP_DMA_CONFIG_REGISTER:
			err = pva_kmd_op_dma_register_async(
				ctx, in_arg, out_arg, &cmdbuf_builder);
			break;
		case PVA_KMD_OP_UNREGISTER:
			err = pva_kmd_op_unregister_async(ctx, in_arg, out_arg,
							  &cmdbuf_builder);
			break;

		default:
			err = PVA_INVAL;
			break;
		}

		if (err != PVA_SUCCESS) {
			break;
		}
	}

	/* This fence comes from user, so set the flag to inform FW */
	post_fence->flags |= PVA_FW_POSTFENCE_FLAGS_USER_FENCE;
	err = pva_kmd_submitter_submit_with_fence(&ctx->submitter,
						  &cmdbuf_builder, post_fence);
	ASSERT(err == PVA_SUCCESS);

out:
	return err;
}

static enum pva_error pva_kmd_op_context_init(struct pva_kmd_context *ctx,
					      struct pva_kmd_buffer *in_buffer,
					      struct pva_kmd_buffer *out_buffer)
{
	struct pva_kmd_context_init_in_args *ctx_init_args;
	struct pva_kmd_context_init_out_args ctx_init_out = { 0 };
	enum pva_error err;

	if (!access_ok(in_buffer,
		       sizeof(struct pva_kmd_context_init_in_args))) {
		return PVA_INVAL;
	}

	if (!access_ok(out_buffer,
		       sizeof(struct pva_kmd_context_init_out_args))) {
		return PVA_INVAL;
	}

	ctx_init_args = read_data(in_buffer,
				  sizeof(struct pva_kmd_context_init_in_args));

	err = pva_kmd_context_init(ctx, ctx_init_args->resource_table_capacity);
	ctx_init_out.error = err;
	ctx_init_out.ccq_shm_hdl = (uint64_t)ctx->ccq_shm_handle;

	write_data(out_buffer, &ctx_init_out, sizeof(ctx_init_out));

	return err;
}

static enum pva_error
pva_kmd_op_syncpt_register_async(struct pva_kmd_context *ctx,
				 struct pva_kmd_buffer *in_buffer,
				 struct pva_kmd_buffer *out_buffer,
				 struct pva_kmd_cmdbuf_builder *cmdbuf_builder)
{
	enum pva_error err;
	struct pva_syncpt_rw_info *syncpts;
	struct pva_kmd_device_memory dev_mem;
	uint32_t resource_id = 0;
	struct pva_cmd_update_resource_table *update_cmd;
	struct pva_resource_entry entry = { 0 };
	struct pva_kmd_syncpt_register_out_args syncpt_register_out = { 0 };

	/* Register RO syncpts */
	dev_mem.iova = ctx->pva->syncpt_ro_iova;
	dev_mem.va = 0;
	dev_mem.size = ctx->pva->syncpt_offset * ctx->pva->num_syncpts;
	dev_mem.pva = ctx->pva;
	dev_mem.smmu_ctx_idx = PVA_R5_SMMU_CONTEXT_ID;
	err = pva_kmd_add_syncpt_resource(&ctx->ctx_resource_table, &dev_mem,
					  &resource_id);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}
	syncpt_register_out.syncpt_ro_res_id = resource_id;
	syncpt_register_out.num_ro_syncpoints = ctx->pva->num_syncpts;
	update_cmd =
		pva_kmd_reserve_cmd_space(cmdbuf_builder, sizeof(*update_cmd));
	ASSERT(update_cmd != NULL);
	err = pva_kmd_make_resource_entry(&ctx->ctx_resource_table, resource_id,
					  &entry);
	ASSERT(err == PVA_SUCCESS);
	pva_kmd_set_cmd_update_resource_table(
		update_cmd, ctx->resource_table_id, resource_id, &entry);

	/* Register RW syncpts */
	pva_kmd_mutex_lock(&ctx->pva->syncpt_allocator.allocator_lock);
	syncpts = (struct pva_syncpt_rw_info *)pva_kmd_get_block_unsafe(
		&ctx->pva->syncpt_allocator, ctx->syncpt_block_index);
	ASSERT(syncpts != NULL);

	for (uint32_t i = 0; i < PVA_NUM_RW_SYNCPTS_PER_CONTEXT; i++) {
		ctx->syncpt_ids[i] = syncpts[i].syncpt_id;
		syncpt_register_out.synpt_ids[i] = syncpts[i].syncpt_id;
	}

	dev_mem.iova = syncpts[0].syncpt_iova;
	pva_kmd_mutex_unlock(&ctx->pva->syncpt_allocator.allocator_lock);
	dev_mem.va = 0;
	dev_mem.size = ctx->pva->syncpt_offset * PVA_NUM_RW_SYNCPTS_PER_CONTEXT;
	dev_mem.pva = ctx->pva;
	dev_mem.smmu_ctx_idx = PVA_R5_SMMU_CONTEXT_ID;
	err = pva_kmd_add_syncpt_resource(&ctx->ctx_resource_table, &dev_mem,
					  &resource_id);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}
	syncpt_register_out.syncpt_rw_res_id = resource_id;
	syncpt_register_out.synpt_size = ctx->pva->syncpt_offset;
	update_cmd =
		pva_kmd_reserve_cmd_space(cmdbuf_builder, sizeof(*update_cmd));
	ASSERT(update_cmd != NULL);
	err = pva_kmd_make_resource_entry(&ctx->ctx_resource_table, resource_id,
					  &entry);
	ASSERT(err == PVA_SUCCESS);
	pva_kmd_set_cmd_update_resource_table(
		update_cmd, ctx->resource_table_id, resource_id, &entry);

err_out:
	syncpt_register_out.error = err;
	write_data(out_buffer, &syncpt_register_out,
		   sizeof(syncpt_register_out));
	return err;
}

static enum pva_error pva_kmd_op_queue_create(struct pva_kmd_context *ctx,
					      struct pva_kmd_buffer *in_arg,
					      struct pva_kmd_buffer *out_arg)
{
	struct pva_kmd_queue_create_in_args *queue_create_args;
	struct pva_kmd_queue_create_out_args queue_out_args = { 0 };
	uint32_t queue_id = PVA_INVALID_QUEUE_ID;
	enum pva_error err = PVA_SUCCESS;

	if (!access_ok(in_arg, sizeof(struct pva_kmd_queue_create_in_args))) {
		return PVA_INVAL;
	}

	if (!access_ok(out_arg, sizeof(struct pva_kmd_queue_create_out_args))) {
		return PVA_INVAL;
	}

	queue_create_args =
		read_data(in_arg, sizeof(struct pva_kmd_queue_create_in_args));
	queue_out_args.error =
		pva_kmd_queue_create(ctx, queue_create_args, &queue_id);
	if (queue_out_args.error == PVA_SUCCESS) {
		queue_out_args.queue_id = queue_id;
	}

	if (queue_id >= PVA_MAX_NUM_QUEUES_PER_CONTEXT) {
		pva_kmd_log_err("pva_kmd_op_queue_create invalid queue id");
		err = PVA_INVAL;
		goto err_out;
	}

	pva_kmd_read_syncpt_val(ctx->pva, ctx->syncpt_ids[queue_id],
				&queue_out_args.syncpt_fence_counter);

	write_data(out_arg, &queue_out_args,
		   sizeof(struct pva_kmd_queue_create_out_args));

err_out:
	return err;
}

static enum pva_error pva_kmd_op_queue_destroy(struct pva_kmd_context *ctx,
					       struct pva_kmd_buffer *in_arg,
					       struct pva_kmd_buffer *out_arg)
{
	struct pva_kmd_queue_destroy_in_args *queue_destroy_args;
	struct pva_kmd_queue_destroy_out_args queue_out_args = { 0 };

	if (!access_ok(in_arg, sizeof(struct pva_kmd_queue_destroy_in_args))) {
		return PVA_INVAL;
	}

	if (!access_ok(out_arg,
		       sizeof(struct pva_kmd_queue_destroy_out_args))) {
		return PVA_INVAL;
	}

	queue_destroy_args =
		read_data(in_arg, sizeof(struct pva_kmd_queue_destroy_in_args));
	queue_out_args.error = pva_kmd_queue_destroy(ctx, queue_destroy_args);

	write_data(out_arg, &queue_out_args,
		   sizeof(struct pva_kmd_queue_destroy_out_args));

	return PVA_SUCCESS;
}

static enum pva_error
pva_kmd_op_executable_get_symbols(struct pva_kmd_context *ctx,
				  struct pva_kmd_buffer *in_arg,
				  struct pva_kmd_buffer *out_arg)
{
	struct pva_kmd_executable_get_symbols_in_args *sym_in_args;
	struct pva_kmd_executable_get_symbols_out_args sym_out_args = { 0 };
	struct pva_kmd_resource_record *rec;
	enum pva_error err = PVA_SUCCESS;
	uint32_t table_size = 0;
	uint32_t size = 0;

	if (!access_ok(in_arg,
		       sizeof(struct pva_kmd_executable_get_symbols_in_args))) {
		return PVA_INVAL;
	}

	if (!access_ok(out_arg,
		       sizeof(struct pva_kmd_executable_get_symbols_out_args))) {
		return PVA_INVAL;
	}

	sym_in_args = read_data(
		in_arg, sizeof(struct pva_kmd_executable_get_symbols_in_args));
	rec = pva_kmd_use_resource(&ctx->ctx_resource_table,
				   sym_in_args->exec_resource_id);
	if (rec == NULL) {
		err = PVA_INVAL;
		pva_kmd_log_err("pva_kmd_use_resource failed");
		goto err_out;
	}
	if (rec->type != PVA_RESOURCE_TYPE_EXEC_BIN) {
		err = PVA_INVAL;
		pva_kmd_log_err("Not an executable resource");
		goto err_drop;
	}

	table_size = safe_mulu32(rec->vpu_bin.symbol_table.n_symbols,
				 sizeof(struct pva_symbol_info));
	size = safe_addu32(
		table_size,
		sizeof(struct pva_kmd_executable_get_symbols_out_args));
	if (!access_ok(out_arg, size)) {
		err = PVA_INVAL;
		goto err_drop;
	}

	sym_out_args.error = err;
	sym_out_args.num_symbols = rec->vpu_bin.symbol_table.n_symbols;
	write_data(out_arg, &sym_out_args, sizeof(sym_out_args));
	write_data(out_arg, rec->vpu_bin.symbol_table.symbols, table_size);

	pva_kmd_drop_resource(&ctx->ctx_resource_table,
			      sym_in_args->exec_resource_id);

	return PVA_SUCCESS;

err_drop:
	pva_kmd_drop_resource(&ctx->ctx_resource_table,
			      sym_in_args->exec_resource_id);

err_out:
	sym_out_args.error = err;
	write_data(out_arg, &sym_out_args, sizeof(sym_out_args));
	return err;
}

typedef enum pva_error (*pva_kmd_async_op_func_t)(
	struct pva_kmd_context *ctx, struct pva_kmd_buffer *in_buffer,
	struct pva_kmd_buffer *out_buffer,
	struct pva_kmd_cmdbuf_builder *cmdbuf_builder);

static enum pva_error
pva_kmd_op_synced_submit(struct pva_kmd_context *ctx,
			 struct pva_kmd_buffer *in_buffer,
			 struct pva_kmd_buffer *out_buffer,
			 pva_kmd_async_op_func_t async_op_func)
{
	enum pva_error err = PVA_SUCCESS;
	struct pva_kmd_cmdbuf_builder cmdbuf_builder;
	uint32_t fence_val;

	err = pva_kmd_submitter_prepare(&ctx->submitter, &cmdbuf_builder);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	err = async_op_func(ctx, in_buffer, out_buffer, &cmdbuf_builder);
	if (err != PVA_SUCCESS) {
		goto cancel_submit;
	}

	err = pva_kmd_submitter_submit(&ctx->submitter, &cmdbuf_builder,
				       &fence_val);
	/* TODO: handle this error */
	ASSERT(err == PVA_SUCCESS);

	err = pva_kmd_submitter_wait(&ctx->submitter, fence_val,
				     PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
				     PVA_KMD_WAIT_FW_TIMEOUT_US);

	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	return PVA_SUCCESS;
cancel_submit:
	pva_kmd_cmdbuf_builder_cancel(&cmdbuf_builder);
err_out:
	return err;
}

static enum pva_error pva_kmd_sync_ops_handler(struct pva_kmd_context *ctx,
					       struct pva_kmd_buffer *in_arg,
					       struct pva_kmd_buffer *out_arg)
{
	enum pva_error err = PVA_SUCCESS;
	struct pva_kmd_op_header *header;

	if (ctx->pva->recovery) {
		pva_kmd_log_err("In Recovery state, do not accept ops");
		err = PVA_INVAL;
		goto out;
	}

	if (!access_ok(in_arg, sizeof(struct pva_kmd_op_header))) {
		err = PVA_INVAL;
		goto out;
	}

	header = read_data(in_arg, sizeof(struct pva_kmd_op_header));

	switch (header->op_type) {
	case PVA_KMD_OP_CONTEXT_INIT:
		err = pva_kmd_op_context_init(ctx, in_arg, out_arg);
		break;
	case PVA_KMD_OP_QUEUE_CREATE:
		err = pva_kmd_op_queue_create(ctx, in_arg, out_arg);
		break;
	case PVA_KMD_OP_QUEUE_DESTROY:
		err = pva_kmd_op_queue_destroy(ctx, in_arg, out_arg);
		break;
	case PVA_KMD_OP_EXECUTABLE_GET_SYMBOLS:
		err = pva_kmd_op_executable_get_symbols(ctx, in_arg, out_arg);
		break;
	case PVA_KMD_OP_MEMORY_REGISTER:
		err = pva_kmd_op_synced_submit(
			ctx, in_arg, out_arg, pva_kmd_op_memory_register_async);
		break;
	case PVA_KMD_OP_SYNPT_REGISTER:
		err = pva_kmd_op_synced_submit(
			ctx, in_arg, out_arg, pva_kmd_op_syncpt_register_async);
		break;
	case PVA_KMD_OP_EXECUTABLE_REGISTER:
		err = pva_kmd_op_synced_submit(
			ctx, in_arg, out_arg,
			pva_kmd_op_executable_register_async);
		break;
	case PVA_KMD_OP_DMA_CONFIG_REGISTER:
		err = pva_kmd_op_synced_submit(ctx, in_arg, out_arg,
					       pva_kmd_op_dma_register_async);
		break;
	case PVA_KMD_OP_UNREGISTER:
		err = pva_kmd_op_synced_submit(ctx, in_arg, out_arg,
					       pva_kmd_op_unregister_async);
		break;
	default:
		err = PVA_INVAL;
		break;
	}

out:
	return err;
}

enum pva_error pva_kmd_ops_handler(struct pva_kmd_context *ctx,
				   void const *ops_buffer, uint32_t ops_size,
				   void *response,
				   uint32_t response_buffer_size,
				   uint32_t *out_response_size)
{
	struct pva_kmd_operations *ops;
	struct pva_kmd_buffer in_buffer = { 0 }, out_buffer = { 0 };
	enum pva_error err = PVA_SUCCESS;
	struct pva_kmd_response_header *resp_hdr;

	in_buffer.base = ops_buffer;
	in_buffer.size = ops_size;

	out_buffer.base = response;
	out_buffer.size = response_buffer_size;

	if (!access_ok(&in_buffer, sizeof(struct pva_kmd_operations))) {
		err = PVA_INVAL;
		goto out;
	}

	if (!access_ok(&out_buffer, sizeof(struct pva_kmd_response_header))) {
		err = PVA_INVAL;
		goto out;
	}

	resp_hdr =
		read_data(&out_buffer, sizeof(struct pva_kmd_response_header));

	ops = read_data(&in_buffer, sizeof(struct pva_kmd_operations));

	if (ops->mode == PVA_KMD_OPS_MODE_SYNC) {
		/* Process one sync operation */
		err = pva_kmd_sync_ops_handler(ctx, &in_buffer, &out_buffer);

	} else {
		/* Process async operations:
		 * - memory register
		 * - executable register
		 * - DMA configuration registration
		 * - unregister
		 */
		err = pva_kmd_async_ops_handler(ctx, &ops->postfence,
						&in_buffer, &out_buffer);
	}
	//Update the size of the responses in the response header.
	// This size also include the header size.
	resp_hdr->rep_size = out_buffer.offset;
out:
	*out_response_size = out_buffer.offset;
	return err;
}
