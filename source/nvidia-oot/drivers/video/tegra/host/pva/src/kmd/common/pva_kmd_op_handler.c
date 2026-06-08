// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#include "pva_kmd_op_handler.h"
#include "pva_api.h"
#include "pva_api_dma.h"
#include "pva_api_types.h"
#include "pva_kmd.h"
#include "pva_kmd_limits.h"
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
#include "pva_kmd_pfsd.h"
#include "pva_kmd_silicon_utils.h"
#include "pva_utils.h"

struct pva_kmd_ops_buffer {
	void const *base;
	uint32_t offset;
	uint32_t size;
};

/* Offset will always be multiple of 8 bytes */
static void incr_offset(struct pva_kmd_ops_buffer *buf, uint32_t incr)
{
	buf->offset = safe_addu32(buf->offset, incr);
	buf->offset =
		safe_pow2_roundup_u32(buf->offset, (uint32_t)sizeof(uint64_t));
}

static bool access_ok(struct pva_kmd_ops_buffer const *buf, uint32_t size)
{
	return safe_addu32(buf->offset, size) <= buf->size;
}

static const void *peek_data(struct pva_kmd_ops_buffer *buf)
{
	return (const void *)((const uint8_t *)buf->base + buf->offset);
}

static const void *consume_data(struct pva_kmd_ops_buffer *buf, uint32_t size)
{
	const void *data = peek_data(buf);
	incr_offset(buf, size);
	return data;
}

static void produce_data(struct pva_kmd_ops_buffer *buf, void const *data,
			 uint32_t size)
{
	union {
		void const *const_ptr;
		void *ptr;
	} base_converter;
	void *dest;

	base_converter.const_ptr = buf->base;
	dest = pva_offset_pointer(base_converter.ptr, (uintptr_t)buf->offset);
	(void)memcpy(dest, data, size);
	incr_offset(buf, size);
}

static enum pva_error pva_kmd_op_memory_register_async(
	struct pva_kmd_context *ctx, const void *input_buffer, uint32_t size,
	struct pva_kmd_ops_buffer *out_buffer,
	struct pva_kmd_cmdbuf_builder *cmdbuf_builder, bool priv)
{
	enum pva_error err = PVA_SUCCESS;
	const struct pva_ops_memory_register *args;
	struct pva_ops_response_register out_args = { 0 };
	struct pva_kmd_device_memory *dev_mem;
	struct pva_cmd_update_resource_table *update_cmd;
	struct pva_resource_entry entry = { 0 };
	struct pva_resource_aux_info aux_info = { 0 };
	uint8_t smmu_ctx_id;
	uint32_t resource_id = 0;

	if (!access_ok(out_buffer,
		       (uint32_t)sizeof(struct pva_ops_response_register))) {
		return PVA_INVAL;
	}

	if (size != (uint64_t)sizeof(struct pva_ops_memory_register)) {
		pva_kmd_log_err("Memory register size is not correct");
		return PVA_INVAL;
	}

	args = (const struct pva_ops_memory_register *)input_buffer;

	dev_mem = pva_kmd_device_memory_acquire(args->import_id, args->offset,
						args->size, ctx);
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
					       dev_mem, &resource_id, false);
	if (err != PVA_SUCCESS) {
		goto unmap;
	}

	update_cmd = (struct pva_cmd_update_resource_table *)
		pva_kmd_reserve_cmd_space(cmdbuf_builder,
					  (uint16_t)sizeof(*update_cmd));
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

	// Prepare aux info for the resource
	aux_info.serial_id_hi = PVA_HI32(args->serial_id);
	aux_info.serial_id_lo = PVA_LOW32(args->serial_id);

	pva_kmd_set_cmd_update_resource_table(update_cmd,
					      ctx->resource_table_id,
					      resource_id, &entry, &aux_info);

	out_args.error = PVA_SUCCESS;
	out_args.resource_id = resource_id;
	produce_data(out_buffer, &out_args, (uint32_t)sizeof(out_args));
	return PVA_SUCCESS;

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
	produce_data(out_buffer, &out_args, (uint32_t)sizeof(out_args));
	return PVA_SUCCESS;
}

static enum pva_error validate_executable_register_params(
	const void *input_buffer, uint32_t size,
	struct pva_kmd_ops_buffer *out_buffer,
	struct pva_ops_executable_register **out_args)
{
	enum pva_error err = PVA_SUCCESS;
	union {
		const void *const_ptr;
		void *ptr;
	} ptr_converter;

	if (!access_ok(out_buffer,
		       (uint32_t)sizeof(
			       struct pva_ops_response_executable_register))) {
		pva_kmd_log_err(
			"pva_kmd_op_executable_register_async: Response buffer too small");
		err = PVA_INVAL;
		goto out;
	}

	if (size < (uint64_t)sizeof(struct pva_ops_executable_register)) {
		pva_kmd_log_err(
			"pva_kmd_op_executable_register_async: Executable register size is not correct");
		err = PVA_INVAL;
		goto out;
	}

	ptr_converter.const_ptr = input_buffer;
	*out_args = (struct pva_ops_executable_register *)ptr_converter.ptr;
	if ((*out_args)->exec_size >
	    (size - (uint64_t)sizeof(struct pva_ops_executable_register))) {
		pva_kmd_log_err(
			"pva_kmd_op_executable_register_async: Executable register payload size too small");
		err = PVA_INVAL;
	}

out:
	return err;
}

static enum pva_error add_vpu_resource_and_get_symbols(
	struct pva_kmd_context *ctx, const void *exec_data, uint32_t exec_size,
	uint32_t *resource_id, uint32_t *num_symbols, bool priv)
{
	enum pva_error err = PVA_SUCCESS;
	struct pva_kmd_resource_record *rec;

	err = pva_kmd_add_vpu_bin_resource(&ctx->ctx_resource_table, exec_data,
					   exec_size, resource_id, priv);
	if (err == PVA_SUCCESS) {
		rec = pva_kmd_use_resource(&ctx->ctx_resource_table,
					   *resource_id);
		ASSERT(rec != NULL);
		*num_symbols = rec->vpu_bin.symbol_table.n_symbols;
		pva_kmd_drop_resource(&ctx->ctx_resource_table, *resource_id);
	}

	return err;
}

static enum pva_error
update_resource_table_command(struct pva_kmd_context *ctx,
			      struct pva_kmd_cmdbuf_builder *cmdbuf_builder,
			      uint32_t resource_id)
{
	enum pva_error err = PVA_SUCCESS;
	struct pva_cmd_update_resource_table *update_cmd;
	struct pva_resource_entry entry = { 0 };

	update_cmd = (struct pva_cmd_update_resource_table *)
		pva_kmd_reserve_cmd_space(cmdbuf_builder,
					  (uint16_t)sizeof(*update_cmd));
	if (update_cmd == NULL) {
		pva_kmd_log_err(
			"pva_kmd_op_executable_register_async: Unable to reserve memory in command buffer");
		err = PVA_NOMEM;
		goto out;
	}
	ASSERT(update_cmd != NULL);

	err = pva_kmd_make_resource_entry(&ctx->ctx_resource_table, resource_id,
					  &entry);
	ASSERT(err == PVA_SUCCESS);

	pva_kmd_set_cmd_update_resource_table(
		update_cmd, ctx->resource_table_id, resource_id, &entry, NULL);

out:
	return err;
}

static enum pva_error pva_kmd_op_executable_register_async(
	struct pva_kmd_context *ctx, const void *input_buffer, uint32_t size,
	struct pva_kmd_ops_buffer *out_buffer,
	struct pva_kmd_cmdbuf_builder *cmdbuf_builder, bool priv)
{
	enum pva_error err = PVA_SUCCESS;
	struct pva_ops_executable_register *args;
	struct pva_ops_response_executable_register out_args = { 0 };
	uint32_t num_symbols = 0;
	const void *exec_data;
	uint32_t resource_id = 0;

	err = validate_executable_register_params(input_buffer, size,
						  out_buffer, &args);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	exec_data = pva_offset_const_ptr(
		input_buffer,
		(uint64_t)sizeof(struct pva_ops_executable_register));

	/* Validate that exec_size fits in uint32_t before casting */
	if (args->exec_size > U32_MAX) {
		pva_kmd_log_err("Executable size exceeds U32_MAX");
		err = PVA_INVAL;
		goto err_out;
	}

	//no need to verify hash for privileged executable
	if (!priv) {
		err = pva_kmd_verify_exectuable_hash(
			ctx->pva,
			(const uint8_t *)(pva_offset_const_ptr(
				input_buffer,
				(uint64_t)sizeof(
					struct pva_ops_executable_register))),
			args->exec_size);
		if (err != PVA_SUCCESS) {
			goto err_out;
		}
	}

	/* CERT INT31-C: exec_size validated to fit in uint32_t, safe to cast */
	err = add_vpu_resource_and_get_symbols(ctx, exec_data,
					       (uint32_t)args->exec_size,
					       &resource_id, &num_symbols,
					       priv);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	err = update_resource_table_command(ctx, cmdbuf_builder, resource_id);
	if (err != PVA_SUCCESS) {
		goto drop_resource;
	}

	out_args.error = PVA_SUCCESS;
	out_args.resource_id = resource_id;
	out_args.num_symbols = num_symbols;
	produce_data(out_buffer, &out_args, (uint32_t)sizeof(out_args));
	return PVA_SUCCESS;

drop_resource:
	pva_kmd_drop_resource(&ctx->ctx_resource_table, resource_id);
err_out:
	out_args.error = err;
	produce_data(out_buffer, &out_args, (uint32_t)sizeof(out_args));
	return PVA_SUCCESS;
}

static enum pva_error pva_kmd_op_dma_register_async(
	struct pva_kmd_context *ctx, const void *input_buffer,
	uint32_t input_buffer_size, struct pva_kmd_ops_buffer *out_buffer,
	struct pva_kmd_cmdbuf_builder *cmdbuf_builder, bool priv)
{
	enum pva_error err = PVA_SUCCESS;
	const struct pva_ops_dma_config_register *args;
	struct pva_ops_response_register out_args = { 0 };
	struct pva_cmd_update_resource_table *update_cmd;
	struct pva_resource_entry entry = { 0 };
	uint32_t resource_id = 0;

	if (!access_ok(out_buffer,
		       (uint32_t)sizeof(struct pva_ops_response_register))) {
		return PVA_INVAL;
	}

	if (input_buffer_size <
	    (uint64_t)sizeof(struct pva_ops_dma_config_register)) {
		pva_kmd_log_err("DMA ops size too small");
		return PVA_INVAL;
	}

	args = (const struct pva_ops_dma_config_register *)input_buffer;
	err = pva_kmd_add_dma_config_resource(&ctx->ctx_resource_table, args,
					      input_buffer_size, &resource_id,
					      priv);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	update_cmd = (struct pva_cmd_update_resource_table *)
		pva_kmd_reserve_cmd_space(cmdbuf_builder,
					  (uint16_t)sizeof(*update_cmd));
	if (update_cmd == NULL) {
		err = PVA_NOMEM;
		goto drop_dma_config;
	}

	err = pva_kmd_make_resource_entry(&ctx->ctx_resource_table, resource_id,
					  &entry);
	ASSERT(err == PVA_SUCCESS);

	pva_kmd_set_cmd_update_resource_table(
		update_cmd, ctx->resource_table_id, resource_id, &entry, NULL);

	out_args.error = PVA_SUCCESS;
	out_args.resource_id = resource_id;
	produce_data(out_buffer, &out_args, (uint32_t)sizeof(out_args));

	return PVA_SUCCESS;
drop_dma_config:
	pva_kmd_drop_resource(&ctx->ctx_resource_table, resource_id);
err_out:
	out_args.error = err;
	produce_data(out_buffer, &out_args, (uint32_t)sizeof(out_args));
	/* Error is reported in the output buffer. So we return success here.  */
	return PVA_SUCCESS;
}

static enum pva_error
pva_kmd_op_pfsd_register(struct pva_kmd_context *ctx, const void *input_buffer,
			 uint32_t size, struct pva_kmd_ops_buffer *out_buffer)
{
	struct pva_ops_response_pfsd_register out_args = { 0 };
	struct pva_cmd_set_pfsd_cmd_buffer_size size_cmd = { 0 };
	struct pva_kmd_submitter *dev_submitter;
	enum pva_error err = PVA_SUCCESS;

	if (!access_ok(
		    out_buffer,
		    (uint32_t)sizeof(struct pva_ops_response_pfsd_register))) {
		return PVA_INVAL;
	}

	if (size < sizeof(struct pva_ops_pfsd_register)) {
		pva_kmd_log_err("PFSD register size is too small");
		return PVA_INVAL;
	}

	err = pva_kmd_pfsd_register_input_buffers(ctx);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Failed to register PFSD input buffers");
		goto err_out;
	}

	err = pva_kmd_pfsd_register_elf(ctx);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Failed to register PFSD ELF");
		goto err_out;
	}

	err = pva_kmd_pfsd_register_dma_config(ctx);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Failed to register PFSD DMA config");
		goto err_out;
	}

	err = ctx->pva->pfsd_info.register_cmd_buffer(ctx);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Failed to register PFSD command buffer");
		goto err_out;
	}

	// Send command buffer size to firmware via privileged command
	dev_submitter = &ctx->pva->submitter;
	pva_kmd_set_cmd_set_pfsd_cmd_buffer_size(
		&size_cmd, ctx->pfsd_resource_ids.cmd_buffer_size);
	err = pva_kmd_submit_cmd_sync(dev_submitter, &size_cmd,
				      (uint32_t)sizeof(size_cmd),
				      PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
				      PVA_KMD_WAIT_FW_TIMEOUT_US);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Failed to submit PFSD command buffer size");
	}

	out_args.pfsd_cmd_resource_id =
		ctx->pfsd_resource_ids.cmd_buffer_resource_id;
	out_args.error = PVA_SUCCESS;
	produce_data(out_buffer, &out_args, (uint32_t)sizeof(out_args));
	return PVA_SUCCESS;

err_out:
	out_args.error = err;
	produce_data(out_buffer, &out_args, (uint32_t)sizeof(out_args));
	return PVA_SUCCESS;
}

static enum pva_error pva_kmd_op_unregister_async(
	struct pva_kmd_context *ctx, const void *input_buffer,
	uint32_t input_buffer_size, struct pva_kmd_ops_buffer *out_buffer,
	struct pva_kmd_cmdbuf_builder *cmdbuf_builder, bool priv)
{
	enum pva_error err = PVA_SUCCESS;
	const struct pva_ops_unregister *args;
	struct pva_cmd_unregister_resource *unreg_cmd;

	if (input_buffer_size != (uint64_t)sizeof(struct pva_ops_unregister)) {
		pva_kmd_log_err("Unregister size is not correct");
		return PVA_INVAL;
	}

	if (!access_ok(out_buffer,
		       (uint32_t)sizeof(struct pva_ops_response_unregister))) {
		return PVA_INVAL;
	}

	args = (const struct pva_ops_unregister *)input_buffer;

	unreg_cmd =
		(struct pva_cmd_unregister_resource *)pva_kmd_reserve_cmd_space(
			cmdbuf_builder, (uint16_t)sizeof(*unreg_cmd));
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

static enum pva_error wait_for_queue_space(struct pva_kmd_context *ctx)
{
	enum pva_error err = PVA_SUCCESS;
	uint32_t wait_time = 0;
	const uint32_t poll_interval_us =
		(uint32_t)((uint32_t)PVA_KMD_WAIT_FW_POLL_INTERVAL_US);
	const uint32_t timeout_us =
		(uint32_t)((uint32_t)PVA_KMD_WAIT_FW_TIMEOUT_US);

	//first check if we have space in queue
	while (pva_kmd_queue_space(&ctx->ctx_queue) == 0U) {
		pva_kmd_sleep_us(PVA_KMD_WAIT_FW_POLL_INTERVAL_US);
		wait_time = safe_addu32(wait_time, poll_interval_us);
		if (wait_time > timeout_us) {
			err = PVA_TIMEDOUT;
			goto out;
		}
	}

out:
	return err;
}

static enum pva_error process_operation_by_opcode(
	struct pva_kmd_context *ctx, const struct pva_ops_header *header,
	const void *input_buffer, struct pva_kmd_ops_buffer *out_arg,
	struct pva_kmd_cmdbuf_builder *cmdbuf_builder, bool priv)
{
	enum pva_error err = PVA_SUCCESS;
	uint32_t size_u32;

	/* Validate that size fits in uint32_t before casting */
	if (header->size > U32_MAX) {
		pva_kmd_log_err("Operation size exceeds U32_MAX");
		return PVA_INVAL;
	}
	size_u32 = (uint32_t)header->size;

	switch (header->opcode) {
	case PVA_OPS_OPCODE_MEMORY_REGISTER:
		err = pva_kmd_op_memory_register_async(ctx, input_buffer,
						       size_u32, out_arg,
						       cmdbuf_builder, false);
		break;

	case PVA_OPS_OPCODE_EXECUTABLE_REGISTER:
		err = pva_kmd_op_executable_register_async(ctx, input_buffer,
							   size_u32, out_arg,
							   cmdbuf_builder,
							   priv);
		break;

	case PVA_OPS_OPCODE_DMA_CONFIG_REGISTER:
		err = pva_kmd_op_dma_register_async(ctx, input_buffer, size_u32,
						    out_arg, cmdbuf_builder,
						    priv);
		break;
	case PVA_OPS_OPCODE_UNREGISTER:
		err = pva_kmd_op_unregister_async(ctx, input_buffer, size_u32,
						  out_arg, cmdbuf_builder,
						  false);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err("Unregister async operation failed");
		}
		break;

	default:
		err = PVA_INVAL;
		break;
	}

	return err;
}

static enum pva_error
pva_kmd_async_ops_handler(struct pva_kmd_context *ctx,
			  struct pva_fw_postfence *post_fence,
			  struct pva_kmd_ops_buffer *in_arg,
			  struct pva_kmd_ops_buffer *out_arg, bool priv)
{
	struct pva_kmd_cmdbuf_builder cmdbuf_builder;
	enum pva_error err = PVA_SUCCESS;
	enum pva_error submit_error = PVA_SUCCESS;

	if (ctx->inited == false) {
		pva_kmd_log_err(
			"pva_kmd_async_ops_handler: Context is not initialized");
		err = PVA_INVAL;
		goto out;
	}

	err = wait_for_queue_space(ctx);
	if (err != PVA_SUCCESS) {
		goto out;
	}

	err = pva_kmd_submitter_prepare(&ctx->submitter, &cmdbuf_builder);
	if (err != PVA_SUCCESS) {
		goto out;
	}

	while (access_ok(in_arg, (uint32_t)sizeof(struct pva_ops_header))) {
		const struct pva_ops_header *header = peek_data(in_arg);
		const void *input_buffer;
		uint32_t size_u32;

		/* Validate that size fits in uint32_t before casting */
		if (header->size > U32_MAX) {
			pva_kmd_log_err(
				"pva_kmd_async_ops_handler: Ops header size exceeds U32_MAX");
			err = PVA_INVAL;
			goto out;
		}
		size_u32 = (uint32_t)header->size;

		if (!access_ok(in_arg, size_u32)) {
			pva_kmd_log_err(
				"pva_kmd_async_ops_handler: Ops header size is bigger than buffer");
			err = PVA_INVAL;
			goto out;
		}

		input_buffer = consume_data(in_arg, size_u32);

		if (header->size % (uint64_t)sizeof(uint64_t) != 0UL) {
			pva_kmd_log_err(
				"pva_kmd_async_ops_handler: PVA operation size is not a multiple of 8");
			err = PVA_INVAL;
			goto exit_loop;
		}
		/* Validate that opcode fits in uint32_t before casting */
		if (header->opcode > U32_MAX) {
			pva_kmd_log_err(
				"pva_kmd_async_ops_handler: Opcode exceeds U32_MAX");
			err = PVA_INVAL;
			goto exit_loop;
		}
		/*
	 * Check if the operation is allowed by the DVMS.
	 * If not, return an error.
	 */
		/* CERT INT31-C: opcode validated to fit in uint32_t, safe to cast */
		if (!pva_kmd_is_ops_allowed(ctx, (uint32_t)header->opcode)) {
			err = PVA_NO_PERM;
			goto exit_loop;
		}
		err = process_operation_by_opcode(ctx, header, input_buffer,
						  out_arg, &cmdbuf_builder,
						  priv);
		if (err != PVA_SUCCESS) {
			break;
		}
	}

exit_loop:
	/* This fence comes from user, so set the flag to inform FW */
	post_fence->flags |= PVA_FW_POSTFENCE_FLAGS_USER_FENCE;
	submit_error = pva_kmd_submitter_submit_with_fence(
		&ctx->submitter, &cmdbuf_builder, post_fence);
	if (submit_error != PVA_SUCCESS) {
		pva_kmd_log_err("Failed to submit command buffer with fence");
	}

	if (err == PVA_SUCCESS) {
		err = submit_error;
	}
out:
	return err;
}

static enum pva_error
pva_kmd_op_context_init(struct pva_kmd_context *ctx, const void *input_buffer,
			uint32_t input_buffer_size,
			struct pva_kmd_ops_buffer *out_buffer)
{
	const struct pva_ops_context_init *ctx_init_args;
	struct pva_ops_response_context_init ctx_init_out = { 0 };
	enum pva_error err;

	if (input_buffer_size !=
	    (uint64_t)sizeof(struct pva_ops_context_init)) {
		pva_kmd_log_err("Context init size is not correct");
		return PVA_INVAL;
	}

	if (!access_ok(out_buffer,
		       (uint32_t)sizeof(struct pva_ops_response_context_init))) {
		return PVA_INVAL;
	}

	ctx_init_args = (const struct pva_ops_context_init *)input_buffer;

	err = pva_kmd_context_init(ctx, ctx_init_args->resource_table_capacity,
				   ctx_init_args->status_shm_hdl);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Context initialization failed");
	}
	ctx_init_out.error = err;
	ctx_init_out.ccq_shm_hdl = (uint64_t)ctx->ccq_shm_handle;
	ctx_init_out.max_cmdbuf_chunk_size =
		pva_kmd_get_max_cmdbuf_chunk_size(ctx->pva);

	produce_data(out_buffer, &ctx_init_out, sizeof(ctx_init_out));

	return PVA_SUCCESS;
}

static enum pva_error
pva_kmd_op_queue_create(struct pva_kmd_context *ctx, const void *input_buffer,
			uint32_t input_buffer_size,
			struct pva_kmd_ops_buffer *out_buffer)
{
	const struct pva_ops_queue_create *queue_create_args;
	struct pva_ops_response_queue_create queue_out_args = { 0 };
	const struct pva_syncpt_rw_info *syncpt_info;
	uint32_t queue_id = PVA_INVALID_QUEUE_ID;
	enum pva_error err = PVA_SUCCESS;

	if (input_buffer_size !=
	    (uint64_t)sizeof(struct pva_ops_queue_create)) {
		pva_kmd_log_err("Queue create size is not correct");
		return PVA_INVAL;
	}

	if (!access_ok(out_buffer,
		       (uint32_t)sizeof(struct pva_ops_response_queue_create))) {
		return PVA_INVAL;
	}

	queue_create_args = (const struct pva_ops_queue_create *)input_buffer;
	err = pva_kmd_queue_create(ctx, queue_create_args, &queue_id);

	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Failed to create queue");
		goto out;
	}

	/* Validate that queue_id fits in uint8_t before casting */
	if (queue_id > (uint32_t)U8_MAX) {
		pva_kmd_log_err("Queue ID exceeds U8_MAX");
		queue_out_args.error = PVA_INVAL;
		goto out;
	}

	/* CERT INT31-C: queue_id validated to fit in uint8_t, safe to cast */
	syncpt_info = pva_kmd_queue_get_rw_syncpt_info(ctx->pva, ctx->ccq_id,
						       queue_id);
	queue_out_args.error = err;
	queue_out_args.queue_id = queue_id;
	queue_out_args.syncpt_id = syncpt_info->syncpt_id;
	pva_kmd_read_syncpt_val(ctx->pva, syncpt_info->syncpt_id,
				&queue_out_args.syncpt_current_value);

out:
	produce_data(out_buffer, &queue_out_args,
		     (uint64_t)sizeof(struct pva_ops_response_queue_create));
	return PVA_SUCCESS;
}

static enum pva_error
pva_kmd_op_queue_destroy(struct pva_kmd_context *ctx, const void *input_buffer,
			 uint32_t input_buffer_size,
			 struct pva_kmd_ops_buffer *out_buffer)
{
	const struct pva_ops_queue_destroy *queue_destroy_args;
	struct pva_ops_response_queue_destroy queue_out_args = { 0 };

	if (input_buffer_size !=
	    (uint64_t)sizeof(struct pva_ops_queue_destroy)) {
		pva_kmd_log_err("Queue destroy size is not correct");
		return PVA_INVAL;
	}

	if (!access_ok(
		    out_buffer,
		    (uint64_t)sizeof(struct pva_ops_response_queue_destroy))) {
		return PVA_INVAL;
	}

	queue_destroy_args = (const struct pva_ops_queue_destroy *)input_buffer;
	queue_out_args.error =
		pva_kmd_queue_destroy(ctx, queue_destroy_args->queue_id);

	produce_data(out_buffer, &queue_out_args,
		     (uint64_t)sizeof(struct pva_ops_response_queue_destroy));

	return PVA_SUCCESS;
}

static enum pva_error pva_kmd_op_executable_get_symbols(
	struct pva_kmd_context *ctx, const void *input_buffer,
	uint32_t input_buffer_size, struct pva_kmd_ops_buffer *out_buffer)
{
	const struct pva_ops_executable_get_symbols *sym_in_args;
	struct pva_ops_response_executable_get_symbols sym_out_args = { 0 };
	struct pva_kmd_resource_record *rec;
	enum pva_error err = PVA_SUCCESS;
	uint32_t table_size = 0;
	uint32_t size = 0;

	if (input_buffer_size !=
	    (uint64_t)sizeof(struct pva_ops_executable_get_symbols)) {
		pva_kmd_log_err("Executable get symbols size is not correct");
		return PVA_INVAL;
	}

	if (!access_ok(
		    out_buffer,
		    (uint32_t)sizeof(
			    struct pva_ops_response_executable_get_symbols))) {
		return PVA_INVAL;
	}

	sym_in_args =
		(const struct pva_ops_executable_get_symbols *)input_buffer;

	rec = pva_kmd_use_resource(&ctx->ctx_resource_table,
				   sym_in_args->exec_resource_id);
	if (rec == NULL) {
		err = PVA_INVAL;
		pva_kmd_log_err("Invalid resource ID");
		goto err_response;
	}
	if (rec->type != PVA_RESOURCE_TYPE_EXEC_BIN) {
		err = PVA_INVAL;
		pva_kmd_log_err("Not an executable resource");
		goto err_drop;
	}

	table_size = safe_mulu32(rec->vpu_bin.symbol_table.n_symbols,
				 (uint32_t)sizeof(struct pva_symbol_info));
	size = safe_addu32(
		table_size,
		(uint32_t)sizeof(
			struct pva_ops_response_executable_get_symbols));
	if (!access_ok(out_buffer, size)) {
		err = PVA_INVAL;
		goto err_drop;
	}

	sym_out_args.error = PVA_SUCCESS;
	sym_out_args.num_symbols = rec->vpu_bin.symbol_table.n_symbols;
	produce_data(out_buffer, &sym_out_args, sizeof(sym_out_args));
	produce_data(out_buffer, rec->vpu_bin.symbol_table.symbols, table_size);
	pva_kmd_drop_resource(&ctx->ctx_resource_table,
			      sym_in_args->exec_resource_id);
	return PVA_SUCCESS;

err_drop:
	pva_kmd_drop_resource(&ctx->ctx_resource_table,
			      sym_in_args->exec_resource_id);

err_response:
	sym_out_args.error = err;
	sym_out_args.num_symbols = 0;
	produce_data(out_buffer, &sym_out_args, sizeof(sym_out_args));
	return PVA_SUCCESS;
}

typedef enum pva_error (*pva_kmd_async_op_func_t)(
	struct pva_kmd_context *ctx, const void *input_buffer,
	uint32_t input_buffer_size, struct pva_kmd_ops_buffer *out_buffer,
	struct pva_kmd_cmdbuf_builder *cmdbuf_builder, bool priv);

static enum pva_error
pva_kmd_op_synced_submit(struct pva_kmd_context *ctx, const void *input_buffer,
			 uint32_t input_buffer_size,
			 struct pva_kmd_ops_buffer *out_buffer,
			 pva_kmd_async_op_func_t async_op_func, bool priv)
{
	enum pva_error err = PVA_SUCCESS;
	struct pva_kmd_cmdbuf_builder cmdbuf_builder;
	uint32_t fence_val;

	err = pva_kmd_submitter_prepare(&ctx->submitter, &cmdbuf_builder);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	err = async_op_func(ctx, input_buffer, input_buffer_size, out_buffer,
			    &cmdbuf_builder, priv);
	if (err != PVA_SUCCESS) {
		goto cancel_submit;
	}

	err = pva_kmd_submitter_submit(&ctx->submitter, &cmdbuf_builder,
				       &fence_val);
	if (err != PVA_SUCCESS) {
		goto cancel_submit;
	}

	err = pva_kmd_submitter_wait(&ctx->submitter, fence_val,
				     PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
				     PVA_KMD_WAIT_FW_TIMEOUT_US);

	if (err != PVA_SUCCESS) {
		goto cancel_submit;
	}

	return PVA_SUCCESS;
cancel_submit:
	pva_kmd_cmdbuf_builder_cancel(&cmdbuf_builder);
err_out:
	return err;
}

static enum pva_error
pva_kmd_sync_ops_handler(struct pva_kmd_context *ctx,
			 struct pva_kmd_ops_buffer *in_arg,
			 struct pva_kmd_ops_buffer *out_arg, bool priv)
{
	enum pva_error err = PVA_SUCCESS;
	const struct pva_ops_header *header;
	const void *input_buffer;
	uint32_t input_buffer_size;

	if (!access_ok(in_arg, (uint32_t)sizeof(struct pva_ops_header))) {
		err = PVA_INVAL;
		goto out;
	}

	header = peek_data(in_arg);

	if (!access_ok(in_arg, header->size)) {
		err = PVA_INVAL;
		goto out;
	}

	/* Validate that size fits in uint32_t before casting for consume_data */
	if (header->size > U32_MAX) {
		pva_kmd_log_err("Operation size exceeds U32_MAX");
		err = PVA_INVAL;
		goto out;
	}

	input_buffer = consume_data(in_arg, (uint32_t)header->size);
	input_buffer_size = (uint64_t)header->size;

	if (input_buffer_size % (uint64_t)sizeof(uint64_t) != 0UL) {
		pva_kmd_log_err("PVA operation size is not a multiple of 8");
		err = PVA_INVAL;
		goto out;
	}

	/* Validate that opcode fits in uint32_t before casting */
	if (header->opcode > U32_MAX) {
		pva_kmd_log_err("Opcode exceeds U32_MAX");
		err = PVA_INVAL;
		goto out;
	}
	/*
	 * Check if the operation is allowed by the DVMS.
	 * If not, return an error.
	 */
	/* CERT INT31-C: opcode validated to fit in uint32_t, safe to cast */
	if (!pva_kmd_is_ops_allowed(ctx, (uint32_t)header->opcode)) {
		err = PVA_NO_PERM;
		goto out;
	}

	/* Check if context is initialized for operations that require it */
	if ((header->opcode != PVA_OPS_OPCODE_CONTEXT_INIT) &&
	    (ctx->inited == false)) {
		pva_kmd_log_err(
			"pva_kmd_sync_ops_handler: Context is not initialized");
		err = PVA_INVAL;
		goto out;
	}

	switch (header->opcode) {
	case PVA_OPS_OPCODE_CONTEXT_INIT:
		err = pva_kmd_op_context_init(ctx, input_buffer,
					      input_buffer_size, out_arg);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err("Context init operation failed");
		}
		break;
	case PVA_OPS_OPCODE_QUEUE_CREATE:
		err = pva_kmd_op_queue_create(ctx, input_buffer,
					      input_buffer_size, out_arg);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err("Queue create operation failed");
		}
		break;
	case PVA_OPS_OPCODE_QUEUE_DESTROY:
		err = pva_kmd_op_queue_destroy(ctx, input_buffer,
					       input_buffer_size, out_arg);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err("Queue destroy operation failed");
		}
		break;
	case PVA_OPS_OPCODE_EXECUTABLE_GET_SYMBOLS:
		err = pva_kmd_op_executable_get_symbols(
			ctx, input_buffer, input_buffer_size, out_arg);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err(
				"Executable get symbols operation failed");
		}
		break;
	case PVA_OPS_OPCODE_MEMORY_REGISTER:
		err = pva_kmd_op_synced_submit(ctx, input_buffer,
					       input_buffer_size, out_arg,
					       pva_kmd_op_memory_register_async,
					       false);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err("Memory register operation failed");
		}
		break;
	case PVA_OPS_OPCODE_EXECUTABLE_REGISTER:
		err = pva_kmd_op_synced_submit(
			ctx, input_buffer, input_buffer_size, out_arg,
			pva_kmd_op_executable_register_async, priv);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err("Executable register operation failed");
		}
		break;
	case PVA_OPS_OPCODE_DMA_CONFIG_REGISTER:
		err = pva_kmd_op_synced_submit(ctx, input_buffer,
					       input_buffer_size, out_arg,
					       pva_kmd_op_dma_register_async,
					       priv);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err("DMA config register operation failed");
		}
		break;
	case PVA_OPS_OPCODE_UNREGISTER: {
		err = pva_kmd_op_synced_submit(ctx, input_buffer,
					       input_buffer_size, out_arg,
					       pva_kmd_op_unregister_async,
					       false);
		if (err == PVA_SUCCESS) {
			// Process unregister requests from firmware to make sure memory is
			// freed before returning to user
			pva_kmd_shared_buffer_process(ctx->pva, ctx->ccq_id);
		}
		break;
	}
	case PVA_OPS_OPCODE_PFSD_REGISTER:
		err = pva_kmd_op_pfsd_register(ctx, input_buffer,
					       input_buffer_size, out_arg);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err("PFSD register operation failed");
		}
		break;
	default:
		err = PVA_INVAL;
		break;
	}

out:
	return err;
}

enum pva_error pva_kmd_ops_handler(struct pva_kmd_context *ctx,
				   enum pva_ops_submit_mode mode,
				   struct pva_fw_postfence *postfence,
				   void const *ops_buffer, uint32_t ops_size,
				   void *resp_buffer, uint32_t resp_buffer_size,
				   uint32_t *out_response_size, bool priv)
{
	struct pva_kmd_ops_buffer in_buffer = { 0 }, out_buffer = { 0 };
	enum pva_error err = PVA_SUCCESS;

	in_buffer.base = ops_buffer;
	in_buffer.size = ops_size;

	out_buffer.base = resp_buffer;
	out_buffer.size = resp_buffer_size;

	if (mode == PVA_OPS_SUBMIT_MODE_SYNC) {
		/* Process one sync operation */
		err = pva_kmd_sync_ops_handler(ctx, &in_buffer, &out_buffer,
					       priv);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err("Sync operation handler failed");
		}
	} else {
		/* Process async operations:
		 * - memory register
		 * - executable register
		 * - DMA configuration registration
		 * - unregister
		 */
		err = pva_kmd_async_ops_handler(ctx, postfence, &in_buffer,
						&out_buffer, priv);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err("Async operation handler failed");
		}
	}

	*out_response_size = out_buffer.offset;
	return err;
}
