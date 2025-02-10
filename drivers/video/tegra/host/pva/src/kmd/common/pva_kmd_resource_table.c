/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, NVIDIA Corporation.  All Rights Reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation.  Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */
#include "pva_kmd_resource_table.h"
#include "pva_kmd_device.h"
#include "pva_kmd_constants.h"

static uint32_t get_max_dma_config_size(struct pva_kmd_device *pva)
{
	uint32_t max_num_dyn_slots = PVA_DMA_MAX_NUM_SLOTS;
	uint32_t max_num_reloc_infos =
		safe_pow2_roundup_u32(max_num_dyn_slots, 2U);

	uint32_t max_dma_cfg_size =
		(uint32_t)sizeof(struct pva_dma_config_resource);

	max_dma_cfg_size = safe_addu32(
		max_dma_cfg_size,
		safe_mulu32(max_num_dyn_slots,
			    (uint32_t)sizeof(struct pva_fw_dma_slot)));

	max_dma_cfg_size = safe_addu32(
		max_dma_cfg_size,
		safe_mulu32(max_num_reloc_infos,
			    (uint32_t)sizeof(struct pva_fw_dma_reloc)));

	max_dma_cfg_size = safe_addu32(
		max_dma_cfg_size,
		safe_mulu32(pva->hw_consts.n_user_dma_channels,
			    (uint32_t)sizeof(struct pva_dma_channel)));

	max_dma_cfg_size = safe_addu32(
		max_dma_cfg_size,
		safe_mulu32(pva->hw_consts.n_dma_descriptors,
			    (uint32_t)sizeof(struct pva_dma_descriptor)));

	max_dma_cfg_size = safe_addu32(max_dma_cfg_size,
				       safe_mulu32(pva->hw_consts.n_hwseq_words,
						   (uint32_t)sizeof(uint32_t)));

	//Must be aligned to 8 to form array
	return safe_pow2_roundup_u32(max_dma_cfg_size,
				     (uint32_t)sizeof(uint64_t));
}

enum pva_error
pva_kmd_resource_table_init(struct pva_kmd_resource_table *res_table,
			    struct pva_kmd_device *pva,
			    uint8_t user_smmu_ctx_id, uint32_t n_entries,
			    uint32_t max_num_dma_configs)
{
	uint32_t max_dma_config_size = get_max_dma_config_size(pva);
	enum pva_error err;
	uint64_t size;

	res_table->pva = pva;
	res_table->n_entries = n_entries;
	res_table->user_smmu_ctx_id = user_smmu_ctx_id;

	size = (uint64_t)safe_mulu32(
		n_entries, (uint32_t)sizeof(struct pva_resource_entry));
	res_table->table_mem = pva_kmd_device_memory_alloc_map(
		size, pva, PVA_ACCESS_RW, PVA_R5_SMMU_CONTEXT_ID);
	ASSERT(res_table->table_mem != NULL);

	pva_kmd_sema_init(&res_table->resource_semaphore, n_entries);

	size = (uint64_t)safe_mulu32(sizeof(struct pva_kmd_resource_record),
				     n_entries);
	res_table->records_mem = pva_kmd_zalloc(size);

	ASSERT(res_table->records_mem != NULL);

	err = pva_kmd_block_allocator_init(
		&res_table->resource_record_allocator, res_table->records_mem,
		PVA_RESOURCE_ID_BASE, sizeof(struct pva_kmd_resource_record),
		n_entries);
	ASSERT(err == PVA_SUCCESS);

	size = (uint64_t)safe_mulu32(max_num_dma_configs, max_dma_config_size);
	res_table->dma_config_mem = pva_kmd_device_memory_alloc_map(
		size, pva, PVA_ACCESS_RW, PVA_R5_SMMU_CONTEXT_ID);
	ASSERT(res_table->dma_config_mem != NULL);

	err = pva_kmd_block_allocator_init(&res_table->dma_config_allocator,
					   res_table->dma_config_mem->va, 0,
					   max_dma_config_size,
					   max_num_dma_configs);
	ASSERT(err == PVA_SUCCESS);

	res_table->dma_aux = pva_kmd_zalloc(
		safe_mulu32((uint32_t)sizeof(struct pva_kmd_dma_resource_aux),
			    max_num_dma_configs));
	ASSERT(res_table->dma_aux != NULL);

	return PVA_SUCCESS;
}

void pva_kmd_resource_table_deinit(struct pva_kmd_resource_table *res_table)
{
	pva_kmd_free(res_table->dma_aux);
	pva_kmd_block_allocator_deinit(&res_table->dma_config_allocator);
	pva_kmd_device_memory_free(res_table->dma_config_mem);
	pva_kmd_block_allocator_deinit(&res_table->resource_record_allocator);
	pva_kmd_free(res_table->records_mem);
	pva_kmd_sema_deinit(&res_table->resource_semaphore);
	pva_kmd_device_memory_free(res_table->table_mem);
}

static struct pva_kmd_resource_record *
pva_kmd_alloc_resource(struct pva_kmd_resource_table *resource_table,
		       uint32_t *out_resource_id)
{
	enum pva_error err;
	struct pva_kmd_resource_record *rec = NULL;

	err = pva_kmd_sema_wait_timeout(&resource_table->resource_semaphore,
					PVA_KMD_TIMEOUT_RESOURCE_SEMA_MS);
	if (err == PVA_TIMEDOUT) {
		pva_kmd_log_err("pva_kmd_alloc_resource Timed out");
	}

	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Failed to wait for resource IDs");
		goto out;
	}

	rec = (struct pva_kmd_resource_record *)pva_kmd_alloc_block(
		&resource_table->resource_record_allocator, out_resource_id);
	ASSERT(rec != NULL);

out:
	return rec;
}

static void pva_kmd_free_resource(struct pva_kmd_resource_table *resource_table,
				  uint32_t resource_id)
{
	enum pva_error err;

	err = pva_kmd_free_block(&resource_table->resource_record_allocator,
				 resource_id);
	ASSERT(err == PVA_SUCCESS);

	pva_kmd_sema_post(&resource_table->resource_semaphore);
}

enum pva_error
pva_kmd_add_syncpt_resource(struct pva_kmd_resource_table *resource_table,
			    struct pva_kmd_device_memory *dev_mem,
			    uint32_t *out_resource_id)
{
	struct pva_kmd_resource_record *rec =
		pva_kmd_alloc_resource(resource_table, out_resource_id);

	if (rec == NULL) {
		pva_kmd_log_err("No more resource id");
		return PVA_NO_RESOURCE_ID;
	}

	if (*out_resource_id > resource_table->curr_max_resource_id) {
		resource_table->curr_max_resource_id = *out_resource_id;
	}

	rec->type = PVA_RESOURCE_TYPE_DRAM;
	rec->dram.mem = dev_mem;
	rec->dram.syncpt = true;
	rec->ref_count = 1;

	return PVA_SUCCESS;
}

enum pva_error
pva_kmd_add_dram_buffer_resource(struct pva_kmd_resource_table *resource_table,
				 struct pva_kmd_device_memory *dev_mem,
				 uint32_t *out_resource_id)
{
	struct pva_kmd_resource_record *rec =
		pva_kmd_alloc_resource(resource_table, out_resource_id);

	if (rec == NULL) {
		pva_kmd_log_err("No more resource id");
		return PVA_NO_RESOURCE_ID;
	}

	if (*out_resource_id > resource_table->curr_max_resource_id) {
		resource_table->curr_max_resource_id = *out_resource_id;
	}

	rec->type = PVA_RESOURCE_TYPE_DRAM;
	rec->dram.mem = dev_mem;
	rec->dram.syncpt = false;
	rec->ref_count = 1;

	return PVA_SUCCESS;
}

static struct pva_resource_entry *
get_fw_resource(struct pva_kmd_resource_table *res_table, uint32_t resource_id)
{
	struct pva_resource_entry *entries = res_table->table_mem->va;
	uint32_t index;

	ASSERT(resource_id >= PVA_RESOURCE_ID_BASE);
	index = safe_subu32(resource_id, PVA_RESOURCE_ID_BASE);
	return &entries[index];
}

void pva_kmd_update_fw_resource_table(struct pva_kmd_resource_table *res_table)
{
	uint32_t id;

	for (id = PVA_RESOURCE_ID_BASE; id <= res_table->curr_max_resource_id;
	     id++) {
		struct pva_resource_entry *entry =
			get_fw_resource(res_table, id);
		struct pva_kmd_resource_record *rec = pva_kmd_get_block(
			&res_table->resource_record_allocator, id);
		if (rec == NULL) {
			continue;
		}

		entry->type = rec->type;
		switch (rec->type) {
		case PVA_RESOURCE_TYPE_DRAM:
			entry->addr_lo = iova_lo(rec->dram.mem->iova);
			entry->addr_hi = iova_hi(rec->dram.mem->iova);
			entry->size_lo = iova_lo(rec->dram.mem->size);
			entry->size_hi = iova_hi(rec->dram.mem->size);
			entry->smmu_context_id = rec->dram.mem->smmu_ctx_idx;
			break;
		case PVA_RESOURCE_TYPE_INVALID:
			break;
		default:
			pva_kmd_log_err("Unsupported resource type");
			pva_kmd_fault();
		}
	}
}

struct pva_kmd_resource_record *
pva_kmd_use_resource(struct pva_kmd_resource_table *res_table,
		     uint32_t resource_id)
{
	struct pva_kmd_resource_record *rec = pva_kmd_get_block(
		&res_table->resource_record_allocator, resource_id);

	if (rec == NULL) {
		return NULL;
	}

	rec->ref_count = safe_addu32(rec->ref_count, 1U);
	return rec;
}

struct pva_kmd_resource_record *
pva_kmd_peek_resource(struct pva_kmd_resource_table *res_table,
		      uint32_t resource_id)
{
	struct pva_kmd_resource_record *rec = pva_kmd_get_block(
		&res_table->resource_record_allocator, resource_id);

	return rec;
}

void pva_kmd_drop_resource(struct pva_kmd_resource_table *resource_table,
			   uint32_t resource_id)
{
	struct pva_kmd_resource_record *rec;

	rec = pva_kmd_get_block(&resource_table->resource_record_allocator,
				resource_id);

	ASSERT(rec != NULL);

	rec->ref_count = safe_subu32(rec->ref_count, 1U);
	if (rec->ref_count == 0) {
		pva_dbg_printf("Dropping resource %u of type %u\n", resource_id,
			       rec->type);
		switch (rec->type) {
		case PVA_RESOURCE_TYPE_DRAM:
			if (rec->dram.syncpt != true) {
				pva_kmd_device_memory_free(rec->dram.mem);
			}
			break;
		case PVA_RESOURCE_TYPE_EXEC_BIN:
			pva_kmd_unload_executable(&rec->vpu_bin.symbol_table,
						  rec->vpu_bin.metainfo_mem,
						  rec->vpu_bin.sections_mem);
			break;
		case PVA_RESOURCE_TYPE_DMA_CONFIG: {
			struct pva_kmd_dma_resource_aux *dma_aux;
			dma_aux =
				&resource_table
					 ->dma_aux[rec->dma_config.block_index];
			pva_kmd_unload_dma_config(dma_aux);
			pva_kmd_free_block(
				&resource_table->dma_config_allocator,
				rec->dma_config.block_index);
			break;
		}

		default:
			pva_kmd_log_err("Unsupported resource type");
			pva_kmd_fault();
		}

		pva_kmd_free_resource(resource_table, resource_id);
	}
}

enum pva_error
pva_kmd_add_vpu_bin_resource(struct pva_kmd_resource_table *resource_table,
			     void *executable, uint32_t executable_size,
			     uint32_t *out_resource_id)
{
	uint32_t res_id;
	struct pva_kmd_resource_record *rec =
		pva_kmd_alloc_resource(resource_table, &res_id);
	enum pva_error err;
	struct pva_kmd_vpu_bin_resource *vpu_bin;

	if (rec == NULL) {
		err = PVA_NO_RESOURCE_ID;
		goto err_out;
	}

	vpu_bin = &rec->vpu_bin;
	err = pva_kmd_load_executable(
		executable, executable_size, resource_table->pva,
		resource_table->user_smmu_ctx_id, &vpu_bin->symbol_table,
		&vpu_bin->metainfo_mem, &vpu_bin->sections_mem);
	if (err != PVA_SUCCESS) {
		goto free_block;
	}

	if (res_id > resource_table->curr_max_resource_id) {
		resource_table->curr_max_resource_id = res_id;
	}

	rec->type = PVA_RESOURCE_TYPE_EXEC_BIN;
	rec->ref_count = 1;
	*out_resource_id = res_id;

	return PVA_SUCCESS;
free_block:
	pva_kmd_free_resource(resource_table, res_id);
err_out:
	return err;
}

enum pva_error
pva_kmd_make_resource_entry(struct pva_kmd_resource_table *resource_table,
			    uint32_t resource_id,
			    struct pva_resource_entry *entry)
{
	struct pva_kmd_resource_record *rec =
		pva_kmd_use_resource(resource_table, resource_id);
	if (rec == NULL) {
		return PVA_NO_RESOURCE_ID;
	}

	switch (rec->type) {
	case PVA_RESOURCE_TYPE_DRAM:
		entry->type = rec->type;
		entry->addr_lo = iova_lo(rec->dram.mem->iova);
		entry->addr_hi = iova_hi(rec->dram.mem->iova);
		entry->size_lo = iova_lo(rec->dram.mem->size);
		entry->size_hi = iova_hi(rec->dram.mem->size);
		entry->smmu_context_id = rec->dram.mem->smmu_ctx_idx;
		break;
	case PVA_RESOURCE_TYPE_EXEC_BIN:
		entry->type = rec->type;
		entry->addr_lo = iova_lo(rec->vpu_bin.metainfo_mem->iova);
		entry->addr_hi = iova_hi(rec->vpu_bin.metainfo_mem->iova);
		entry->size_lo = iova_lo(rec->vpu_bin.metainfo_mem->size);
		entry->size_hi = iova_hi(rec->vpu_bin.metainfo_mem->size);
		entry->smmu_context_id =
			rec->vpu_bin.metainfo_mem->smmu_ctx_idx;
		break;
	case PVA_RESOURCE_TYPE_DMA_CONFIG:
		entry->type = rec->type;
		entry->addr_lo = iova_lo(rec->dma_config.iova_addr);
		entry->addr_hi = iova_hi(rec->dma_config.iova_addr);
		entry->size_lo = iova_lo(rec->dma_config.size);
		entry->size_hi = iova_hi(rec->dma_config.size);
		entry->smmu_context_id = PVA_R5_SMMU_CONTEXT_ID;
		break;
	default:
		pva_kmd_log_err("Unsupported resource type");
		pva_kmd_fault();
	}

	pva_kmd_drop_resource(resource_table, resource_id);
	return PVA_SUCCESS;
}

enum pva_error pva_kmd_add_dma_config_resource(
	struct pva_kmd_resource_table *resource_table, void *dma_config_payload,
	uint32_t dma_config_size, uint32_t *out_resource_id)
{
	enum pva_error err = PVA_SUCCESS;
	uint32_t block_idx, fw_fetch_size;
	void *fw_dma_cfg;
	struct pva_kmd_dma_resource_aux *dma_aux;
	struct pva_kmd_resource_record *rec;
	uint32_t res_id;

	fw_dma_cfg = pva_kmd_zalloc_block(&resource_table->dma_config_allocator,
					  &block_idx);
	if (fw_dma_cfg == NULL) {
		err = PVA_NOMEM;
		goto err_out;
	}

	// Must satisfy alignment requirement for converting to struct
	// pva_dma_config_resource*
	ASSERT(((uintptr_t)fw_dma_cfg) % sizeof(uint64_t) == 0);

	dma_aux = &resource_table->dma_aux[block_idx];

	err = pva_kmd_load_dma_config(resource_table, dma_config_payload,
				      dma_config_size, dma_aux, fw_dma_cfg,
				      &fw_fetch_size);
	if (err != PVA_SUCCESS) {
		goto free_block;
	}

	rec = pva_kmd_alloc_resource(resource_table, &res_id);
	if (rec == NULL) {
		err = PVA_NO_RESOURCE_ID;
		goto unload_dma;
	}

	if (res_id > resource_table->curr_max_resource_id) {
		resource_table->curr_max_resource_id = res_id;
	}

	rec->type = PVA_RESOURCE_TYPE_DMA_CONFIG;
	rec->ref_count = 1;
	rec->dma_config.block_index = block_idx;
	rec->dma_config.iova_addr = safe_addu64(
		resource_table->dma_config_mem->iova,
		(uint64_t)safe_mulu32(
			block_idx,
			resource_table->dma_config_allocator.block_size));
	rec->dma_config.size = fw_fetch_size;

	*out_resource_id = res_id;

	return PVA_SUCCESS;
unload_dma:
	pva_kmd_unload_dma_config(dma_aux);
free_block:
	pva_kmd_free_block(&resource_table->dma_config_allocator, block_idx);
err_out:
	return err;
}

void pva_kmd_verify_all_resources_free(
	struct pva_kmd_resource_table *resource_table)
{
	enum pva_error err;
	for (uint32_t i = 0; i < resource_table->n_entries; i++) {
		err = pva_kmd_sema_wait_timeout(
			&resource_table->resource_semaphore,
			PVA_KMD_TIMEOUT_RESOURCE_SEMA_MS);
		ASSERT(err == PVA_SUCCESS);
	}
}
