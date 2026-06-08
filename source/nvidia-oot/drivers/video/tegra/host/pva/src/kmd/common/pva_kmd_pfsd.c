// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_pfsd.h"
#include "pva_kmd_op_handler.h"
#include "pva_kmd_device_memory.h"
#include "pva_kmd_device.h"
#include "pva_kmd_utils.h"
#include "pva_kmd_context.h"
#include "pva_kmd_cmdbuf.h"
#include "pva_kmd_constants.h"
#include "pva_utils.h"
#include "pva_kmd.h"
#include "pva_api_types.h"
#include "pva_api_ops.h"
#include "pva_kmd_submitter.h"
#include "pva_kmd_resource_table.h"

// Helper structure to pass multiple source buffers
struct pva_buffer_source {
	const void *data_ptr;
	uint64_t size;
};

// Helper function to register a buffer from multiple sources
static enum pva_error pva_kmd_pfsd_register_buffer(
	struct pva_kmd_context *ctx, const struct pva_buffer_source *sources,
	uint32_t num_sources, enum pva_memory_segment segment,
	uint32_t *resource_id_out)
{
	enum pva_error err = PVA_SUCCESS;
	struct pva_kmd_device_memory *mem = NULL;
	struct pva_cmd_update_resource_table update_cmd = { 0 };
	struct pva_resource_entry entry = { 0 };
	uint32_t resource_id = 0;
	uint8_t smmu_ctx_id;
	struct pva_kmd_submitter *dev_submitter;
	uint64_t total_size = 0;
	uint64_t offset = 0;
	uint32_t i;

	for (i = 0; i < num_sources; i++) {
		total_size = safe_addu64(total_size, sources[i].size);
	}

	if (segment == PVA_MEMORY_SEGMENT_R5) {
		smmu_ctx_id = PVA_R5_SMMU_CONTEXT_ID;
	} else {
		smmu_ctx_id = ctx->smmu_ctx_id;
	}

	mem = pva_kmd_device_memory_alloc_map(total_size, ctx->pva,
					      PVA_ACCESS_RO, smmu_ctx_id);
	if (mem == NULL) {
		pva_kmd_log_err(
			"Failed to allocate and map device memory for buffer");
		err = PVA_NOMEM;
		goto out;
	}

	for (i = 0; i < num_sources; i++) {
		(void)memcpy((void *)((uint8_t *)mem->va + offset),
			     (const void *)sources[i].data_ptr,
			     sources[i].size);
		offset = safe_addu64(offset, sources[i].size);
	}

	err = pva_kmd_add_dram_buffer_resource(&ctx->ctx_resource_table, mem,
					       &resource_id, true);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Failed to add buffer to resource table");
		goto free_memory;
	}

	err = pva_kmd_make_resource_entry(&ctx->ctx_resource_table, resource_id,
					  &entry);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Failed to make resource entry");
		goto free_dram_buffer_resource;
	}

	pva_kmd_set_cmd_update_resource_table(
		&update_cmd, ctx->resource_table_id, resource_id, &entry, NULL);

	dev_submitter = &ctx->pva->submitter;
	err = pva_kmd_submit_cmd_sync(dev_submitter, &update_cmd,
				      (uint32_t)sizeof(update_cmd),
				      PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
				      PVA_KMD_WAIT_FW_TIMEOUT_US);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("PFSD buffer memory registration to FW failed");
		goto free_dram_buffer_resource;
	}

	*resource_id_out = resource_id;

	return PVA_SUCCESS;

free_dram_buffer_resource:
	pva_kmd_drop_resource(&ctx->ctx_resource_table, resource_id);
free_memory:
	pva_kmd_device_memory_free(mem);
out:
	return err;
}

enum pva_error pva_kmd_pfsd_register_input_buffers(struct pva_kmd_context *ctx)
{
	enum pva_error err = PVA_SUCCESS;
	struct pva_buffer_source sources[4];

	// Register in1 buffer
	sources[0].data_ptr = in1;
	sources[0].size = sizeof(in1);
	err = pva_kmd_pfsd_register_buffer(
		ctx, sources, 1, PVA_MEMORY_SEGMENT_DMA,
		&ctx->pfsd_resource_ids.in1_resource_id);
	if (err != PVA_SUCCESS) {
		return err;
	}

	// Register in2 buffer
	sources[0].data_ptr = in2;
	sources[0].size = sizeof(in2);
	err = pva_kmd_pfsd_register_buffer(
		ctx, sources, 1, PVA_MEMORY_SEGMENT_DMA,
		&ctx->pfsd_resource_ids.in2_resource_id);
	if (err != PVA_SUCCESS) {
		return err;
	}

	// Register in3 buffer
	sources[0].data_ptr = in3;
	sources[0].size = sizeof(in3);
	err = pva_kmd_pfsd_register_buffer(
		ctx, sources, 1, PVA_MEMORY_SEGMENT_DMA,
		&ctx->pfsd_resource_ids.in3_resource_id);
	if (err != PVA_SUCCESS) {
		return err;
	}

	// Register hist_a buffer (hist_short_a followed by hist_int_a)
	sources[0].data_ptr = hist_short_a;
	sources[0].size = sizeof(hist_short_a);
	sources[1].data_ptr = hist_int_a;
	sources[1].size = sizeof(hist_int_a);
	err = pva_kmd_pfsd_register_buffer(
		ctx, sources, 2, PVA_MEMORY_SEGMENT_DMA,
		&ctx->pfsd_resource_ids.hist_a_resource_id);
	if (err != PVA_SUCCESS) {
		return err;
	}

	// Register hist_b buffer (hist_short_b followed by hist_int_b)
	sources[0].data_ptr = hist_short_b;
	sources[0].size = sizeof(hist_short_b);
	sources[1].data_ptr = hist_int_b;
	sources[1].size = sizeof(hist_int_b);
	err = pva_kmd_pfsd_register_buffer(
		ctx, sources, 2, PVA_MEMORY_SEGMENT_DMA,
		&ctx->pfsd_resource_ids.hist_b_resource_id);
	if (err != PVA_SUCCESS) {
		return err;
	}

	// Register hist_c buffer (hist_short_c followed by hist_int_c)
	sources[0].data_ptr = hist_short_c;
	sources[0].size = sizeof(hist_short_c);
	sources[1].data_ptr = hist_int_c;
	sources[1].size = sizeof(hist_int_c);
	err = pva_kmd_pfsd_register_buffer(
		ctx, sources, 2, PVA_MEMORY_SEGMENT_DMA,
		&ctx->pfsd_resource_ids.hist_c_resource_id);
	if (err != PVA_SUCCESS) {
		return err;
	}

	// Register dlut_tbl_buf (all 4 DLUT tables combined)
	sources[0].data_ptr = pva_pfsd_data_dlut_tbl0;
	sources[0].size = PVA_PFSD_DLUT_TBL0_SIZE;
	sources[1].data_ptr = pva_pfsd_data_dlut_tbl1;
	sources[1].size = PVA_PFSD_DLUT_TBL1_SIZE;
	sources[2].data_ptr = pva_pfsd_data_dlut_tbl2;
	sources[2].size = PVA_PFSD_DLUT_TBL2_SIZE;
	sources[3].data_ptr = pva_pfsd_data_dlut_tbl3;
	sources[3].size = PVA_PFSD_DLUT_TBL3_SIZE;
	err = pva_kmd_pfsd_register_buffer(
		ctx, sources, 4, PVA_MEMORY_SEGMENT_DMA,
		&ctx->pfsd_resource_ids.dlut_tbl_resource_id);
	if (err != PVA_SUCCESS) {
		return err;
	}

	// Register dlut_indices_buf (all 4 DLUT indices combined)
	sources[0].data_ptr = pva_pfsd_data_dlut_indices0;
	sources[0].size = PVA_PFSD_DLUT_INDICES0_SIZE;
	sources[1].data_ptr = pva_pfsd_data_dlut_indices1;
	sources[1].size = PVA_PFSD_DLUT_INDICES1_SIZE;
	sources[2].data_ptr = pva_pfsd_data_dlut_indices2;
	sources[2].size = PVA_PFSD_DLUT_INDICES2_SIZE;
	sources[3].data_ptr = pva_pfsd_data_dlut_indices3;
	sources[3].size = PVA_PFSD_DLUT_INDICES3_SIZE;
	err = pva_kmd_pfsd_register_buffer(
		ctx, sources, 4, PVA_MEMORY_SEGMENT_DMA,
		&ctx->pfsd_resource_ids.dlut_indices_resource_id);
	if (err != PVA_SUCCESS) {
		return err;
	}

	return PVA_SUCCESS;
}

enum pva_error pva_kmd_pfsd_register_elf(struct pva_kmd_context *ctx)
{
	enum pva_error err = PVA_SUCCESS;
	struct pva_ops_executable_register exec_register = { 0 };
	struct pva_ops_response_executable_register response = { 0 };
	uint32_t response_size = 0;
	uint32_t total_size = 0;
	uint8_t *vpu_exec_buffer = NULL;
	const uint8_t *vpu_elf_data = NULL;
	uint32_t vpu_elf_size = 0;
	uint8_t *ppe_exec_buffer = NULL;
	const uint8_t *ppe_elf_data = NULL;
	uint32_t ppe_elf_size = 0;

	vpu_elf_data = ctx->pva->pfsd_info.vpu_elf_data;
	vpu_elf_size = ctx->pva->pfsd_info.vpu_elf_size;
	ppe_elf_data = ctx->pva->pfsd_info.ppe_elf_data;
	ppe_elf_size = ctx->pva->pfsd_info.ppe_elf_size;

	total_size = safe_addu32(
		(uint32_t)sizeof(struct pva_ops_executable_register),
		vpu_elf_size);
	// Align total size to 8 bytes as required by PVA operation handler
	total_size = safe_pow2_roundup_u32(total_size, 8U);

	vpu_exec_buffer = pva_kmd_zalloc(total_size);
	if (vpu_exec_buffer == NULL) {
		pva_kmd_log_err(
			"Failed to allocate buffer for PFSD ELF registration");
		err = PVA_NOMEM;
		goto out;
	}

	exec_register.header.opcode = PVA_OPS_OPCODE_EXECUTABLE_REGISTER;
	exec_register.header.size = total_size;
	exec_register.exec_size = vpu_elf_size;

	(void)memcpy((void *)vpu_exec_buffer, (const void *)&exec_register,
		     sizeof(exec_register));
	(void)memcpy((void *)(vpu_exec_buffer + sizeof(exec_register)),
		     (const void *)vpu_elf_data, vpu_elf_size);

	err = pva_kmd_ops_handler(ctx, PVA_OPS_SUBMIT_MODE_SYNC, NULL,
				  vpu_exec_buffer, total_size, &response,
				  (uint32_t)sizeof(response), &response_size,
				  true);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("PFSD ELF executable registration failed");
		goto free_vpu_buffer;
	}

	if (response.error != PVA_SUCCESS) {
		pva_kmd_log_err("PFSD ELF executable registration failed");
		err = response.error;
		goto free_vpu_buffer;
	}

	ctx->pfsd_resource_ids.vpu_elf_resource_id = response.resource_id;

	if (ppe_elf_data != NULL) {
		total_size = safe_addu32(
			(uint32_t)sizeof(struct pva_ops_executable_register),
			ppe_elf_size);
		// Align total size to 8 bytes as required by PVA operation handler
		total_size = safe_pow2_roundup_u32(total_size, 8U);

		ppe_exec_buffer = pva_kmd_zalloc(total_size);
		if (ppe_exec_buffer == NULL) {
			pva_kmd_log_err(
				"Failed to allocate buffer for PFSD PPE ELF registration");
			err = PVA_NOMEM;
			goto free_ppe_buffer;
		}

		exec_register.header.opcode =
			PVA_OPS_OPCODE_EXECUTABLE_REGISTER;
		exec_register.header.size = total_size;
		exec_register.exec_size = ppe_elf_size;

		(void)memcpy((void *)ppe_exec_buffer,
			     (const void *)&exec_register,
			     sizeof(exec_register));
		(void)memcpy((void *)(ppe_exec_buffer + sizeof(exec_register)),
			     (const void *)ppe_elf_data, ppe_elf_size);

		err = pva_kmd_ops_handler(ctx, PVA_OPS_SUBMIT_MODE_SYNC, NULL,
					  ppe_exec_buffer, total_size,
					  &response, (uint32_t)sizeof(response),
					  &response_size, true);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err(
				"PFSD PPE ELF executable registration failed");
			goto free_ppe_buffer;
		}

		if (response.error != PVA_SUCCESS) {
			pva_kmd_log_err(
				"PFSD PPE ELF executable registration failed");
			err = response.error;
			goto free_ppe_buffer;
		}

		ctx->pfsd_resource_ids.ppe_elf_resource_id =
			response.resource_id;
	}

free_ppe_buffer:
	pva_kmd_free(ppe_exec_buffer);
free_vpu_buffer:
	pva_kmd_free(vpu_exec_buffer);
out:
	return err;
}

enum pva_error pva_kmd_pfsd_register_dma_config(struct pva_kmd_context *ctx)
{
	enum pva_error err = PVA_SUCCESS;
	struct pva_ops_response_register response = { 0 };
	uint32_t response_size = 0;
	uint32_t total_size = 0;
	uint8_t *dma_buffer = NULL;
	uint32_t offset = 0;
	uint32_t i;
	struct pva_dma_config pfsd_dma_cfg = { 0 };
	struct pva_dma_static_binding static_bindings[8];
	struct pva_ops_dma_config_register dma_register = { 0 };

	pfsd_dma_cfg = ctx->pva->pfsd_info.pfsd_dma_cfg;
	total_size = (uint32_t)sizeof(struct pva_ops_dma_config_register);

	total_size = safe_pow2_roundup_u32(total_size, 8U);
	total_size = safe_addu32(
		total_size,
		safe_mulu32((uint32_t)pfsd_dma_cfg.header.num_channels,
			    (uint32_t)sizeof(struct pva_dma_channel)));

	total_size = safe_pow2_roundup_u32(total_size, 8U);
	total_size = safe_addu32(
		total_size,
		safe_mulu32((uint32_t)pfsd_dma_cfg.header.num_descriptors,
			    (uint32_t)sizeof(struct pva_dma_descriptor)));

	total_size = safe_pow2_roundup_u32(total_size, 8U);
	total_size = safe_addu32(
		total_size,
		safe_mulu32((uint32_t)pfsd_dma_cfg.header.num_hwseq_words,
			    (uint32_t)sizeof(uint32_t)));

	total_size = safe_pow2_roundup_u32(total_size, 8U);
	total_size = safe_addu32(
		total_size,
		safe_mulu32((uint32_t)pfsd_dma_cfg.header.num_static_slots,
			    (uint32_t)sizeof(struct pva_dma_static_binding)));

	total_size = safe_pow2_roundup_u32(total_size, 8U);

	dma_buffer = pva_kmd_zalloc(total_size);
	if (dma_buffer == NULL) {
		pva_kmd_log_err(
			"Failed to allocate buffer for PFSD DMA config registration");
		err = PVA_NOMEM;
		goto out;
	}

	pfsd_dma_cfg.header.vpu_exec_resource_id =
		ctx->pfsd_resource_ids.vpu_elf_resource_id;

	// Build dma_register structure locally and copy to buffer
	dma_register.header.opcode = PVA_OPS_OPCODE_DMA_CONFIG_REGISTER;
	dma_register.header.size = total_size;
	dma_register.dma_config_header = pfsd_dma_cfg.header;

	offset = (uint32_t)sizeof(struct pva_ops_dma_config_register);

	offset = safe_pow2_roundup_u32(offset, 8U);
	dma_register.channels_offset = offset;
	(void)memcpy((void *)(dma_buffer + offset),
		     (const void *)pfsd_dma_cfg.channels,
		     pfsd_dma_cfg.header.num_channels *
			     sizeof(struct pva_dma_channel));
	offset = safe_addu32(
		offset, safe_mulu32(pfsd_dma_cfg.header.num_channels,
				    (uint32_t)sizeof(struct pva_dma_channel)));

	offset = safe_pow2_roundup_u32(offset, 8U);
	dma_register.descriptors_offset = offset;
	(void)memcpy((void *)(dma_buffer + offset),
		     (const void *)pfsd_dma_cfg.descriptors,
		     pfsd_dma_cfg.header.num_descriptors *
			     sizeof(struct pva_dma_descriptor));
	offset = safe_addu32(
		offset,
		safe_mulu32(pfsd_dma_cfg.header.num_descriptors,
			    (uint32_t)sizeof(struct pva_dma_descriptor)));

	offset = safe_pow2_roundup_u32(offset, 8U);
	dma_register.hwseq_words_offset = offset;
	if (pfsd_dma_cfg.header.num_hwseq_words > 0U &&
	    pfsd_dma_cfg.hwseq_words != NULL) {
		(void)memcpy((void *)(dma_buffer + offset),
			     (const void *)pfsd_dma_cfg.hwseq_words,
			     pfsd_dma_cfg.header.num_hwseq_words *
				     sizeof(uint32_t));
	}
	offset = safe_addu32(offset,
			     safe_mulu32(pfsd_dma_cfg.header.num_hwseq_words,
					 (uint32_t)sizeof(uint32_t)));

	for (i = 0U; i < pfsd_dma_cfg.header.num_static_slots && i < 8U; i++) {
		(void)memcpy(&static_bindings[i],
			     &pfsd_dma_cfg.static_bindings[i],
			     sizeof(struct pva_dma_static_binding));
	}

	static_bindings[0].dram.resource_id =
		ctx->pfsd_resource_ids.in1_resource_id;
	static_bindings[1].dram.resource_id =
		ctx->pfsd_resource_ids.in2_resource_id;
	static_bindings[2].dram.resource_id =
		ctx->pfsd_resource_ids.in3_resource_id;
	static_bindings[3].dram.resource_id =
		ctx->pfsd_resource_ids.hist_a_resource_id;
	static_bindings[4].dram.resource_id =
		ctx->pfsd_resource_ids.hist_b_resource_id;
	static_bindings[5].dram.resource_id =
		ctx->pfsd_resource_ids.hist_c_resource_id;
	static_bindings[6].dram.resource_id =
		ctx->pfsd_resource_ids.dlut_tbl_resource_id;
	static_bindings[7].dram.resource_id =
		ctx->pfsd_resource_ids.dlut_indices_resource_id;

	offset = safe_pow2_roundup_u32(offset, 8U);
	dma_register.static_bindings_offset = offset;
	(void)memcpy((void *)(dma_buffer + offset),
		     (const void *)static_bindings,
		     pfsd_dma_cfg.header.num_static_slots *
			     sizeof(struct pva_dma_static_binding));

	(void)memcpy((void *)dma_buffer, (const void *)&dma_register,
		     sizeof(dma_register));

	/*
	 * MISRA C-2023 Directive 4.14: Validate values from external sources
	 * Note: dma_buffer is constructed internally from pfsd_dma_cfg which has
	 * been validated during PFSD initialization. All resource IDs are from
	 * internal driver state (ctx->pfsd_resource_ids), not direct user input.
	 * The buffer size and structure are validated by pva_kmd_ops_handler.
	 */
	err = pva_kmd_ops_handler(ctx, PVA_OPS_SUBMIT_MODE_SYNC, NULL,
				  dma_buffer, total_size, &response,
				  (uint32_t)sizeof(response), &response_size,
				  true);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("PFSD DMA config registration failed");
		goto free_buffer;
	}

	if (response.error != PVA_SUCCESS) {
		pva_kmd_log_err("PFSD DMA config registration failed");
		err = response.error;
		goto free_buffer;
	}

	ctx->pfsd_resource_ids.dma_config_resource_id = response.resource_id;

free_buffer:
	pva_kmd_free(dma_buffer);
out:
	return err;
}

enum pva_error pva_kmd_pfsd_t23x_register_cmdbuf(struct pva_kmd_context *ctx)
{
	enum pva_error err = PVA_SUCCESS;
	uint8_t *cmd_buffer = NULL;
	uint32_t cmd_buffer_size = 0;
	uint32_t offset = 0;
	struct pva_buffer_source cmd_source;
	struct pva_cmd_acquire_engine acquire_cmd = { 0 };
	struct pva_cmd_set_vpu_executable set_vpu_exec_cmd = { 0 };
	struct pva_cmd_prefetch_vpu_code prefetch_cmd = { 0 };
	struct pva_cmd_fetch_dma_configuration fetch_dma_cmd = { 0 };
	struct pva_cmd_init_vpu_executable init_vpu_cmd = { 0 };
	struct pva_cmd_set_vpu_instance_parameter set_vpu_param_cmd = { 0 };
	struct pva_cmd_setup_misr setup_misr_cmd = { 0 };
	struct pva_cmd_setup_dma setup_dma_cmd = { 0 };
	struct pva_cmd_run_dma run_dma_cmd = { 0 };
	struct pva_cmd_run_vpu run_vpu_cmd = { 0 };
	struct pva_cmd_release_engine release_cmd = { 0 };

	// Calculate command buffer size
	cmd_buffer_size =
		(uint32_t)(sizeof(struct pva_cmd_acquire_engine) +
			   sizeof(struct pva_cmd_set_vpu_executable) +
			   sizeof(struct pva_cmd_prefetch_vpu_code) +
			   sizeof(struct pva_cmd_fetch_dma_configuration) +
			   sizeof(struct pva_cmd_init_vpu_executable) +
			   sizeof(struct pva_cmd_set_vpu_instance_parameter) +
			   sizeof(struct pva_cmd_setup_misr) +
			   sizeof(struct pva_cmd_setup_dma) +
			   sizeof(struct pva_cmd_run_dma) +
			   sizeof(struct pva_cmd_run_vpu) +
			   sizeof(struct pva_cmd_release_engine));

	// Allocate command buffer
	cmd_buffer = pva_kmd_zalloc(cmd_buffer_size);
	if (cmd_buffer == NULL) {
		pva_kmd_log_err("Failed to allocate command buffer");
		err = PVA_NOMEM;
		goto out;
	}

	// Build command sequence

	// 1. pva_cmd_acquire_engine
	acquire_cmd.header.opcode = PVA_CMD_OPCODE_ACQUIRE_ENGINE;
	acquire_cmd.header.len =
		(uint8_t)(sizeof(struct pva_cmd_acquire_engine) /
			  sizeof(uint32_t));
	acquire_cmd.engine_count = 1;
	(void)memcpy((void *)(cmd_buffer + offset), (const void *)&acquire_cmd,
		     sizeof(acquire_cmd));
	offset += (uint32_t)sizeof(struct pva_cmd_acquire_engine);

	// 2. pva_cmd_set_vpu_executable
	set_vpu_exec_cmd.header.opcode = PVA_CMD_OPCODE_SET_VPU_EXECUTABLE;
	set_vpu_exec_cmd.header.len =
		(uint8_t)(sizeof(struct pva_cmd_set_vpu_executable) /
			  sizeof(uint32_t));
	set_vpu_exec_cmd.vpu_exec_resource_id =
		ctx->pfsd_resource_ids.vpu_elf_resource_id;
	(void)memcpy((void *)(cmd_buffer + offset),
		     (const void *)&set_vpu_exec_cmd, sizeof(set_vpu_exec_cmd));
	offset += (uint32_t)sizeof(struct pva_cmd_set_vpu_executable);

	// 3. pva_cmd_prefetch_vpu_code
	prefetch_cmd.header.opcode = PVA_CMD_OPCODE_PREFETCH_VPU_CODE;
	prefetch_cmd.header.len =
		(uint8_t)(sizeof(struct pva_cmd_prefetch_vpu_code) /
			  sizeof(uint32_t));
	prefetch_cmd.entry_point_index = 0;
	(void)memcpy((void *)(cmd_buffer + offset), (const void *)&prefetch_cmd,
		     sizeof(prefetch_cmd));
	offset += (uint32_t)sizeof(struct pva_cmd_prefetch_vpu_code);

	// 4. pva_cmd_fetch_dma_configuration
	fetch_dma_cmd.header.opcode = PVA_CMD_OPCODE_FETCH_DMA_CONFIGURATION;
	fetch_dma_cmd.header.len =
		(uint8_t)(sizeof(struct pva_cmd_fetch_dma_configuration) /
			  sizeof(uint32_t));
	fetch_dma_cmd.dma_set_id = 0;
	fetch_dma_cmd.resource_id =
		ctx->pfsd_resource_ids.dma_config_resource_id;
	(void)memcpy((void *)(cmd_buffer + offset),
		     (const void *)&fetch_dma_cmd, sizeof(fetch_dma_cmd));
	offset += (uint32_t)sizeof(struct pva_cmd_fetch_dma_configuration);

	// 5. pva_cmd_init_vpu_executable
	init_vpu_cmd.header.opcode = PVA_CMD_OPCODE_INIT_VPU_EXECUTABLE;
	init_vpu_cmd.header.len =
		(uint8_t)(sizeof(struct pva_cmd_init_vpu_executable) /
			  sizeof(uint32_t));
	(void)memcpy((void *)(cmd_buffer + offset), (const void *)&init_vpu_cmd,
		     sizeof(init_vpu_cmd));
	offset += (uint32_t)sizeof(struct pva_cmd_init_vpu_executable);

	// 6. pva_cmd_set_vpu_instance_parameter
	set_vpu_param_cmd.header.opcode =
		PVA_CMD_OPCODE_SET_VPU_INSTANCE_PARAMETER;
	set_vpu_param_cmd.header.len =
		(uint8_t)(sizeof(struct pva_cmd_set_vpu_instance_parameter) /
			  sizeof(uint32_t));
	set_vpu_param_cmd.symbol_id = 0x40U;
	(void)memcpy((void *)(cmd_buffer + offset),
		     (const void *)&set_vpu_param_cmd,
		     sizeof(set_vpu_param_cmd));
	offset += (uint32_t)sizeof(struct pva_cmd_set_vpu_instance_parameter);

	// 7. pva_cmd_setup_misr
	setup_misr_cmd.header.opcode = PVA_CMD_OPCODE_SETUP_MISR;
	setup_misr_cmd.header.len =
		(uint8_t)(sizeof(struct pva_cmd_setup_misr) / sizeof(uint32_t));
	setup_misr_cmd.misr_params.slot_mask_low0 = 0xFFFFFFFFU;
	setup_misr_cmd.misr_params.slot_mask_low1 = 0x000007FFU;
	setup_misr_cmd.misr_params.slot_mask_high = 0;
	setup_misr_cmd.misr_params.misr_config.seed_crc0 = MISR_SEED_0_T23X;
	setup_misr_cmd.misr_params.misr_config.seed_crc1 = MISR_SEED_1_T23X;
	setup_misr_cmd.misr_params.misr_config.ref_addr = MISR_REF_0_T23X;
	setup_misr_cmd.misr_params.misr_config.ref_data_1 = MISR_REF_1_T23X;
	setup_misr_cmd.misr_params.misr_config.ref_data_2 = MISR_REF_2_T23X;
	setup_misr_cmd.misr_params.misr_config.misr_timeout = MISR_TIMEOUT_T23X;
	(void)memcpy((void *)(cmd_buffer + offset),
		     (const void *)&setup_misr_cmd, sizeof(setup_misr_cmd));
	offset += (uint32_t)sizeof(struct pva_cmd_setup_misr);

	// 8. pva_cmd_setup_dma
	setup_dma_cmd.header.opcode = PVA_CMD_OPCODE_SETUP_DMA;
	setup_dma_cmd.header.len =
		(uint8_t)(sizeof(struct pva_cmd_setup_dma) / sizeof(uint32_t));
	setup_dma_cmd.dma_set_id = 0;
	(void)memcpy((void *)(cmd_buffer + offset),
		     (const void *)&setup_dma_cmd, sizeof(setup_dma_cmd));
	offset += (uint32_t)sizeof(struct pva_cmd_setup_dma);

	// 9. pva_cmd_run_dma
	run_dma_cmd.header.opcode = PVA_CMD_OPCODE_RUN_DMA;
	run_dma_cmd.header.len =
		(uint8_t)(sizeof(struct pva_cmd_run_dma) / sizeof(uint32_t));
	run_dma_cmd.dma_set_id = 0;
	(void)memcpy((void *)(cmd_buffer + offset), (const void *)&run_dma_cmd,
		     sizeof(run_dma_cmd));
	offset += (uint32_t)sizeof(struct pva_cmd_run_dma);

	// 10. pva_cmd_run_vpu
	run_vpu_cmd.header.opcode = PVA_CMD_OPCODE_RUN_VPU;
	run_vpu_cmd.header.len =
		(uint8_t)(sizeof(struct pva_cmd_run_vpu) / sizeof(uint32_t));
	run_vpu_cmd.entry_point_index = 0;
	(void)memcpy((void *)(cmd_buffer + offset), (const void *)&run_vpu_cmd,
		     sizeof(run_vpu_cmd));
	offset += (uint32_t)sizeof(struct pva_cmd_run_vpu);

	// 11. pva_cmd_release_engine
	release_cmd.header.opcode = PVA_CMD_OPCODE_RELEASE_ENGINE;
	release_cmd.header.len =
		(uint8_t)(sizeof(struct pva_cmd_release_engine) /
			  sizeof(uint32_t));
	(void)memcpy((void *)(cmd_buffer + offset), (const void *)&release_cmd,
		     sizeof(release_cmd));

	// Register the command buffer with R5 segment so firmware can access it
	cmd_source.data_ptr = cmd_buffer;
	cmd_source.size = cmd_buffer_size;
	err = pva_kmd_pfsd_register_buffer(
		ctx, &cmd_source, 1, PVA_MEMORY_SEGMENT_R5,
		&ctx->pfsd_resource_ids.cmd_buffer_resource_id);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Failed to register PFSD command buffer");
		goto free_buffer;
	}

	// Store the command buffer size in the global structure
	ctx->pfsd_resource_ids.cmd_buffer_size = cmd_buffer_size;

free_buffer:
	pva_kmd_free(cmd_buffer);
out:
	return err;
}

enum pva_error pva_kmd_pfsd_t26x_register_cmdbuf(struct pva_kmd_context *ctx)
{
	enum pva_error err = PVA_SUCCESS;
	uint8_t *cmd_buffer = NULL;
	uint32_t cmd_buffer_size = 0;
	uint32_t offset = 0;
	struct pva_buffer_source cmd_source;
	struct pva_cmd_acquire_engine acquire_cmd = { 0 };
	struct pva_cmd_set_vpu_executable set_vpu_exec_cmd = { 0 };
	struct pva_cmd_set_ppe_executable set_ppe_exec_cmd = { 0 };
	struct pva_cmd_prefetch_vpu_code prefetch_cmd = { 0 };
	struct pva_cmd_fetch_dma_configuration fetch_dma_cmd = { 0 };
	struct pva_cmd_init_vpu_executable init_vpu_cmd = { 0 };
	struct pva_cmd_init_ppe_executable init_ppe_cmd = { 0 };
	struct pva_cmd_set_vpu_instance_parameter set_vpu_param_cmd = { 0 };
	struct pva_cmd_setup_misr setup_misr_cmd = { 0 };
	struct pva_cmd_setup_dma setup_dma_cmd = { 0 };
	struct pva_cmd_run_dma run_dma_cmd = { 0 };
	struct pva_cmd_run_vpu run_vpu_cmd = { 0 };
	struct pva_cmd_run_ppe run_ppe_cmd = { 0 };
	struct pva_cmd_release_engine release_cmd = { 0 };

	// Calculate command buffer size
	cmd_buffer_size =
		(uint32_t)(sizeof(struct pva_cmd_acquire_engine) +
			   sizeof(struct pva_cmd_set_vpu_executable) +
			   sizeof(struct pva_cmd_set_ppe_executable) +
			   sizeof(struct pva_cmd_prefetch_vpu_code) +
			   sizeof(struct pva_cmd_fetch_dma_configuration) +
			   sizeof(struct pva_cmd_init_vpu_executable) +
			   sizeof(struct pva_cmd_init_ppe_executable) +
			   sizeof(struct pva_cmd_set_vpu_instance_parameter) +
			   sizeof(struct pva_cmd_setup_misr) +
			   sizeof(struct pva_cmd_setup_dma) +
			   sizeof(struct pva_cmd_run_dma) +
			   sizeof(struct pva_cmd_run_vpu) +
			   sizeof(struct pva_cmd_run_ppe) +
			   sizeof(struct pva_cmd_release_engine));

	// Allocate command buffer
	cmd_buffer = pva_kmd_zalloc(cmd_buffer_size);
	if (cmd_buffer == NULL) {
		pva_kmd_log_err("Failed to allocate command buffer");
		err = PVA_NOMEM;
		goto out;
	}

	// Build command sequence

	// 1. pva_cmd_acquire_engine
	acquire_cmd.header.opcode = PVA_CMD_OPCODE_ACQUIRE_ENGINE;
	acquire_cmd.header.len =
		(uint8_t)(sizeof(struct pva_cmd_acquire_engine) /
			  sizeof(uint32_t));
	acquire_cmd.engine_count = 1;
	(void)memcpy((void *)(cmd_buffer + offset), (const void *)&acquire_cmd,
		     sizeof(acquire_cmd));
	offset += (uint32_t)sizeof(struct pva_cmd_acquire_engine);

	// 2. pva_cmd_set_vpu_executable
	set_vpu_exec_cmd.header.opcode = PVA_CMD_OPCODE_SET_VPU_EXECUTABLE;
	set_vpu_exec_cmd.header.len =
		(uint8_t)(sizeof(struct pva_cmd_set_vpu_executable) /
			  sizeof(uint32_t));
	set_vpu_exec_cmd.vpu_exec_resource_id =
		ctx->pfsd_resource_ids.vpu_elf_resource_id;
	(void)memcpy((void *)(cmd_buffer + offset),
		     (const void *)&set_vpu_exec_cmd, sizeof(set_vpu_exec_cmd));
	offset += (uint32_t)sizeof(struct pva_cmd_set_vpu_executable);

	// 3. pva_cmd_set_ppe_executable
	set_ppe_exec_cmd.header.opcode = PVA_CMD_OPCODE_SET_PPE_EXECUTABLE;
	set_ppe_exec_cmd.header.len =
		(uint8_t)(sizeof(struct pva_cmd_set_ppe_executable) /
			  sizeof(uint32_t));
	set_ppe_exec_cmd.ppe_exec_resource_id =
		ctx->pfsd_resource_ids.ppe_elf_resource_id;
	(void)memcpy((void *)(cmd_buffer + offset),
		     (const void *)&set_ppe_exec_cmd, sizeof(set_ppe_exec_cmd));
	offset += (uint32_t)sizeof(struct pva_cmd_set_ppe_executable);

	// 4. pva_cmd_prefetch_vpu_code
	prefetch_cmd.header.opcode = PVA_CMD_OPCODE_PREFETCH_VPU_CODE;
	prefetch_cmd.header.len =
		(uint8_t)(sizeof(struct pva_cmd_prefetch_vpu_code) /
			  sizeof(uint32_t));
	prefetch_cmd.entry_point_index = 0;
	(void)memcpy((void *)(cmd_buffer + offset), (const void *)&prefetch_cmd,
		     sizeof(prefetch_cmd));
	offset += (uint32_t)sizeof(struct pva_cmd_prefetch_vpu_code);

	// 5. pva_cmd_fetch_dma_configuration
	fetch_dma_cmd.header.opcode = PVA_CMD_OPCODE_FETCH_DMA_CONFIGURATION;
	fetch_dma_cmd.header.len =
		(uint8_t)(sizeof(struct pva_cmd_fetch_dma_configuration) /
			  sizeof(uint32_t));
	fetch_dma_cmd.dma_set_id = 0;
	fetch_dma_cmd.resource_id =
		ctx->pfsd_resource_ids.dma_config_resource_id;
	(void)memcpy((void *)(cmd_buffer + offset),
		     (const void *)&fetch_dma_cmd, sizeof(fetch_dma_cmd));
	offset += (uint32_t)sizeof(struct pva_cmd_fetch_dma_configuration);

	// 6. pva_cmd_init_vpu_executable
	init_vpu_cmd.header.opcode = PVA_CMD_OPCODE_INIT_VPU_EXECUTABLE;
	init_vpu_cmd.header.len =
		(uint8_t)(sizeof(struct pva_cmd_init_vpu_executable) /
			  sizeof(uint32_t));
	(void)memcpy((void *)(cmd_buffer + offset), (const void *)&init_vpu_cmd,
		     sizeof(init_vpu_cmd));
	offset += (uint32_t)sizeof(struct pva_cmd_init_vpu_executable);

	// 7. pva_cmd_init_ppe_executable
	init_ppe_cmd.header.opcode = PVA_CMD_OPCODE_INIT_PPE_EXECUTABLE;
	init_ppe_cmd.header.len =
		(uint8_t)(sizeof(struct pva_cmd_init_ppe_executable) /
			  sizeof(uint32_t));
	(void)memcpy((void *)(cmd_buffer + offset), (const void *)&init_ppe_cmd,
		     sizeof(init_ppe_cmd));
	offset += (uint32_t)sizeof(struct pva_cmd_init_ppe_executable);

	// 8. pva_cmd_set_vpu_instance_parameter
	set_vpu_param_cmd.header.opcode =
		PVA_CMD_OPCODE_SET_VPU_INSTANCE_PARAMETER;
	set_vpu_param_cmd.header.len =
		(uint8_t)(sizeof(struct pva_cmd_set_vpu_instance_parameter) /
			  sizeof(uint32_t));
	set_vpu_param_cmd.symbol_id = 0x40U;
	(void)memcpy((void *)(cmd_buffer + offset),
		     (const void *)&set_vpu_param_cmd,
		     sizeof(set_vpu_param_cmd));
	offset += (uint32_t)sizeof(struct pva_cmd_set_vpu_instance_parameter);

	// 9. pva_cmd_setup_misr
	setup_misr_cmd.header.opcode = PVA_CMD_OPCODE_SETUP_MISR;
	setup_misr_cmd.header.len =
		(uint8_t)(sizeof(struct pva_cmd_setup_misr) / sizeof(uint32_t));
	setup_misr_cmd.misr_params.slot_mask_low0 = 0xFFFFFFFFU;
	setup_misr_cmd.misr_params.slot_mask_low1 = 0xFFFFFFFFU;
	setup_misr_cmd.misr_params.slot_mask_high = 0x1FU;
	setup_misr_cmd.misr_params.misr_config.seed_crc0 = MISR_SEED_0_T26X;
	setup_misr_cmd.misr_params.misr_config.seed_crc1 = MISR_SEED_1_T26X;
	setup_misr_cmd.misr_params.misr_config.ref_addr = MISR_REF_0_T26X;
	setup_misr_cmd.misr_params.misr_config.ref_data_1 = MISR_REF_1_T26X;
	setup_misr_cmd.misr_params.misr_config.ref_data_2 = MISR_REF_2_T26X;
	setup_misr_cmd.misr_params.misr_config.misr_timeout = MISR_TIMEOUT_T26X;
	(void)memcpy((void *)(cmd_buffer + offset),
		     (const void *)&setup_misr_cmd, sizeof(setup_misr_cmd));
	offset += (uint32_t)sizeof(struct pva_cmd_setup_misr);

	// 10. pva_cmd_setup_dma
	setup_dma_cmd.header.opcode = PVA_CMD_OPCODE_SETUP_DMA;
	setup_dma_cmd.header.len =
		(uint8_t)(sizeof(struct pva_cmd_setup_dma) / sizeof(uint32_t));
	setup_dma_cmd.dma_set_id = 0;
	(void)memcpy((void *)(cmd_buffer + offset),
		     (const void *)&setup_dma_cmd, sizeof(setup_dma_cmd));
	offset += (uint32_t)sizeof(struct pva_cmd_setup_dma);

	// 11. pva_cmd_run_dma
	run_dma_cmd.header.opcode = PVA_CMD_OPCODE_RUN_DMA;
	run_dma_cmd.header.len =
		(uint8_t)(sizeof(struct pva_cmd_run_dma) / sizeof(uint32_t));
	run_dma_cmd.dma_set_id = 0;
	(void)memcpy((void *)(cmd_buffer + offset), (const void *)&run_dma_cmd,
		     sizeof(run_dma_cmd));
	offset += (uint32_t)sizeof(struct pva_cmd_run_dma);

	// 12. pva_cmd_run_vpu
	run_vpu_cmd.header.opcode = PVA_CMD_OPCODE_RUN_VPU;
	run_vpu_cmd.header.len =
		(uint8_t)(sizeof(struct pva_cmd_run_vpu) / sizeof(uint32_t));
	run_vpu_cmd.entry_point_index = 0;
	(void)memcpy((void *)(cmd_buffer + offset), (const void *)&run_vpu_cmd,
		     sizeof(run_vpu_cmd));
	offset += (uint32_t)sizeof(struct pva_cmd_run_vpu);

	// 13. pva_cmd_run_ppe
	run_ppe_cmd.header.opcode = PVA_CMD_OPCODE_RUN_PPE;
	run_ppe_cmd.header.len =
		(uint8_t)(sizeof(struct pva_cmd_run_ppe) / sizeof(uint32_t));
	run_ppe_cmd.entry_point_index = 0;
	(void)memcpy((void *)(cmd_buffer + offset), (const void *)&run_ppe_cmd,
		     sizeof(run_ppe_cmd));
	offset += (uint32_t)sizeof(struct pva_cmd_run_ppe);

	// 14. pva_cmd_release_engine
	release_cmd.header.opcode = PVA_CMD_OPCODE_RELEASE_ENGINE;
	release_cmd.header.len =
		(uint8_t)(sizeof(struct pva_cmd_release_engine) /
			  sizeof(uint32_t));
	(void)memcpy((void *)(cmd_buffer + offset), (const void *)&release_cmd,
		     sizeof(release_cmd));

	// Register the command buffer with R5 segment so firmware can access it
	cmd_source.data_ptr = cmd_buffer;
	cmd_source.size = cmd_buffer_size;
	err = pva_kmd_pfsd_register_buffer(
		ctx, &cmd_source, 1, PVA_MEMORY_SEGMENT_R5,
		&ctx->pfsd_resource_ids.cmd_buffer_resource_id);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Failed to register PFSD command buffer");
		goto free_buffer;
	}

	// Store the command buffer size in the global structure
	ctx->pfsd_resource_ids.cmd_buffer_size = cmd_buffer_size;

free_buffer:
	pva_kmd_free(cmd_buffer);
out:
	return err;
}
