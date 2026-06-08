// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#include "pva_kmd_resource_table.h"
#include "pva_kmd_device.h"
#include "pva_kmd_context.h"
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
			    uint8_t user_smmu_ctx_id, uint32_t n_entries)
{
	uint32_t max_dma_config_size = get_max_dma_config_size(pva);
	enum pva_error err;
	uint64_t size;
	uint32_t size_u32;

	res_table->pva = pva;
	res_table->n_entries = safe_addu32(n_entries, PVA_MAX_PRIV_RES_ID);
	res_table->user_smmu_ctx_id = user_smmu_ctx_id;
	pva_kmd_sema_init(&res_table->resource_semaphore, res_table->n_entries);
	err = pva_kmd_mutex_init(&res_table->resource_table_lock);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err(
			"pva_kmd_resource_table_init mutex_init failed");
		return err;
	}

	size = (uint64_t)safe_mulu32(
		res_table->n_entries,
		(uint32_t)sizeof(struct pva_resource_entry));
	size = safe_addu64(
		size, (uint64_t)safe_mulu32(
			      res_table->n_entries,
			      (uint32_t)sizeof(struct pva_resource_aux_info)));
	res_table->table_mem = pva_kmd_device_memory_alloc_map(
		size, pva, PVA_ACCESS_RW, PVA_R5_SMMU_CONTEXT_ID);
	if (res_table->table_mem == NULL) {
		err = PVA_NOMEM;
		goto deinit_locks;
	}

	size_u32 = safe_mulu32((uint32_t)sizeof(struct pva_kmd_resource_record),
			       res_table->n_entries);
	res_table->records_mem =
		(struct pva_kmd_resource_record *)pva_kmd_zalloc(size_u32);

	if (res_table->records_mem == NULL) {
		err = PVA_NOMEM;
		goto free_table_mem;
	}

	err = pva_kmd_block_allocator_init(
		&res_table->priv_resource_record_allocator,
		res_table->records_mem, PVA_RESOURCE_ID_BASE,
		(uint32_t)sizeof(struct pva_kmd_resource_record),
		PVA_MAX_PRIV_RES_ID);
	if (err != PVA_SUCCESS) {
		goto free_records_mem;
	}

	err = pva_kmd_block_allocator_init(
		&res_table->resource_record_allocator,
		(uint8_t *)res_table->records_mem +
			safe_mulu32(PVA_MAX_PRIV_RES_ID,
				    (uint32_t)sizeof(
					    struct pva_kmd_resource_record)),
		PVA_USER_RESOURCE_ID_BASE,
		(uint32_t)sizeof(struct pva_kmd_resource_record), n_entries);
	if (err != PVA_SUCCESS) {
		goto free_priv_resource_record_allocator;
	}

	err = pva_kmd_devmem_pool_init(&res_table->dma_config_pool, pva,
				       PVA_R5_SMMU_CONTEXT_ID,
				       max_dma_config_size,
				       PVA_KMD_DMA_CONFIG_POOL_INCR);
	if (err != PVA_SUCCESS) {
		goto free_resource_record_allocator;
	}

	return PVA_SUCCESS;

free_resource_record_allocator:
	pva_kmd_block_allocator_deinit(&res_table->resource_record_allocator);
free_priv_resource_record_allocator:
	pva_kmd_block_allocator_deinit(
		&res_table->priv_resource_record_allocator);
free_records_mem:
	pva_kmd_free(res_table->records_mem);
free_table_mem:
	pva_kmd_device_memory_free(res_table->table_mem);
deinit_locks:
	pva_kmd_mutex_deinit(&res_table->resource_table_lock);
	pva_kmd_sema_deinit(&res_table->resource_semaphore);
	return err;
}

static struct pva_kmd_resource_record *
pva_kmd_alloc_resource_id(struct pva_kmd_resource_table *resource_table,
			  uint32_t *out_resource_id, bool priv)
{
	enum pva_error err;
	struct pva_kmd_resource_record *rec = NULL;

	err = pva_kmd_sema_wait_timeout(&resource_table->resource_semaphore,
					PVA_KMD_TIMEOUT_RESOURCE_SEMA_MS);
	if (err == PVA_TIMEDOUT) {
		pva_kmd_log_err("pva_kmd_alloc_resource Timed out");
	}

	if (err != PVA_SUCCESS) {
		pva_dbg_printf(
			"kmd: allocation failed. ctx_id = %u, n_entries = %u\n",
			resource_table->user_smmu_ctx_id,
			resource_table->n_entries);
		goto out;
	}

	if (priv) {
		rec = (struct pva_kmd_resource_record *)pva_kmd_zalloc_block(
			&resource_table->priv_resource_record_allocator,
			out_resource_id);
	} else {
		rec = (struct pva_kmd_resource_record *)pva_kmd_zalloc_block(
			&resource_table->resource_record_allocator,
			out_resource_id);
	}
	if (rec == NULL) {
		pva_kmd_log_err(
			"pva_kmd_alloc_resource_id: No available resource slots");
		pva_kmd_sema_post(&resource_table->resource_semaphore);
		goto out;
	}

out:
	return rec;
}

static void
pva_kmd_free_resource_id(struct pva_kmd_resource_table *resource_table,
			 uint32_t resource_id)
{
	enum pva_error err;

	if (resource_id <= PVA_MAX_PRIV_RES_ID) {
		err = pva_kmd_free_block(
			&resource_table->priv_resource_record_allocator,
			resource_id);
	} else {
		err = pva_kmd_free_block(
			&resource_table->resource_record_allocator,
			resource_id);
	}
	ASSERT(err == PVA_SUCCESS);

	pva_kmd_sema_post(&resource_table->resource_semaphore);
}

static void
pva_kmd_release_resource(struct pva_kmd_resource_table *resource_table,
			 uint32_t resource_id, bool drop_dma_reference)
{
	struct pva_kmd_resource_record *rec =
		pva_kmd_peek_resource(resource_table, resource_id);
	ASSERT(rec != NULL);

	switch (rec->type) {
	case PVA_RESOURCE_TYPE_DRAM:
		pva_kmd_device_memory_free(rec->dram.mem);
		break;
	case PVA_RESOURCE_TYPE_EXEC_BIN:
		pva_kmd_unload_executable(&rec->vpu_bin.symbol_table,
					  rec->vpu_bin.metainfo_mem,
					  rec->vpu_bin.sections_mem);
		break;
	case PVA_RESOURCE_TYPE_DMA_CONFIG: {
		if (drop_dma_reference) {
			pva_kmd_unload_dma_config_unsafe(
				rec->dma_config.aux_mem);
		}
		pva_kmd_free(rec->dma_config.aux_mem);
		pva_kmd_devmem_pool_free(&rec->dma_config.devmem);
		break;
	}

	default:
		FAULT("Unsupported resource type");
		/* NOTREACHED */
	}

	pva_kmd_free_resource_id(resource_table, resource_id);
}

enum pva_error
pva_kmd_add_dram_buffer_resource(struct pva_kmd_resource_table *resource_table,
				 struct pva_kmd_device_memory *dev_mem,
				 uint32_t *out_resource_id, bool priv)
{
	struct pva_kmd_resource_record *rec = pva_kmd_alloc_resource_id(
		resource_table, out_resource_id, priv);

	if (rec == NULL) {
		pva_kmd_log_err(
			"pva_kmd_add_dram_buffer_resource No more resource id");
		return PVA_NO_RESOURCE_ID;
	}

	pva_kmd_mutex_lock(&resource_table->resource_table_lock);
	if (*out_resource_id > resource_table->curr_max_resource_id) {
		resource_table->curr_max_resource_id = *out_resource_id;
	}
	pva_kmd_mutex_unlock(&resource_table->resource_table_lock);

	rec->type = PVA_RESOURCE_TYPE_DRAM;
	rec->dram.mem = dev_mem;
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

/** Since this API is called only during init time no need to add lock for resource entry */
void pva_kmd_update_fw_resource_table(struct pva_kmd_resource_table *res_table)
{
	uint32_t id;
	struct pva_kmd_resource_record *rec;
	uint32_t max_resource_id;

	/** This lock is unnecessary but added to avoid painful process of proving false positive on coverity */
	pva_kmd_mutex_lock(&res_table->resource_table_lock);
	max_resource_id = res_table->curr_max_resource_id;
	pva_kmd_mutex_unlock(&res_table->resource_table_lock);

	for (id = PVA_RESOURCE_ID_BASE; id <= max_resource_id; id++) {
		struct pva_resource_entry *entry =
			get_fw_resource(res_table, id);
		if (id <= PVA_MAX_PRIV_RES_ID) {
			rec = pva_kmd_get_block_unsafe(
				&res_table->priv_resource_record_allocator, id);
		} else {
			rec = pva_kmd_get_block_unsafe(
				&res_table->resource_record_allocator, id);
		}
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
			/* CERT INT31-C: iova_access_flags limited to PVA_ACCESS_* values (1,2,3),
		     * always fits in uint8_t, safe to cast */
			entry->access_flags =
				(uint8_t)rec->dram.mem->iova_access_flags;
			break;
		case PVA_RESOURCE_TYPE_INVALID:
			break;
		default:
			pva_kmd_log_err("Unsupported resource type");
			pva_kmd_fault();
			/* NOTREACHED */
		}
	}
}

struct pva_kmd_resource_record *
pva_kmd_use_resource_unsafe(struct pva_kmd_resource_table *res_table,
			    uint32_t resource_id)
{
	struct pva_kmd_resource_record *rec =
		pva_kmd_peek_resource(res_table, resource_id);
	if (rec == NULL) {
		return NULL;
	}

	rec->ref_count = safe_addu32(rec->ref_count, 1U);
	return rec;
}

struct pva_kmd_resource_record *
pva_kmd_use_resource(struct pva_kmd_resource_table *res_table,
		     uint32_t resource_id)
{
	struct pva_kmd_resource_record *rec;
	pva_kmd_mutex_lock(&res_table->resource_table_lock);

	rec = pva_kmd_peek_resource(res_table, resource_id);
	if (rec == NULL) {
		pva_kmd_mutex_unlock(&res_table->resource_table_lock);
		return NULL;
	}

	rec->ref_count = safe_addu32(rec->ref_count, 1U);
	pva_kmd_mutex_unlock(&res_table->resource_table_lock);
	return rec;
}

/** This API is not thread safe but only used inside pva_kmd_load_dma_config which is already protected */
struct pva_kmd_resource_record *
pva_kmd_peek_resource(struct pva_kmd_resource_table *res_table,
		      uint32_t resource_id)
{
	struct pva_kmd_resource_record *rec;
	if (resource_id <= PVA_MAX_PRIV_RES_ID) {
		rec = pva_kmd_get_block_unsafe(
			&res_table->priv_resource_record_allocator,
			resource_id);
	} else {
		rec = pva_kmd_get_block_unsafe(
			&res_table->resource_record_allocator, resource_id);
	}

	return rec;
}

void pva_kmd_drop_resource(struct pva_kmd_resource_table *resource_table,
			   uint32_t resource_id)
{
	pva_kmd_mutex_lock(&resource_table->resource_table_lock);
	pva_kmd_drop_resource_unsafe(resource_table, resource_id);
	pva_kmd_mutex_unlock(&resource_table->resource_table_lock);
}

void pva_kmd_drop_resource_unsafe(struct pva_kmd_resource_table *resource_table,
				  uint32_t resource_id)
{
	struct pva_kmd_resource_record *rec =
		pva_kmd_peek_resource(resource_table, resource_id);

	if (rec == NULL) {
		pva_kmd_log_err_u64("Unexpected resource ID drop", resource_id);
		return;
	}

	rec->ref_count = safe_subu32(rec->ref_count, 1U);
	if (rec->ref_count == 0U) {
		pva_kmd_release_resource(resource_table, resource_id, true);
	}
}

enum pva_error
pva_kmd_add_vpu_bin_resource(struct pva_kmd_resource_table *resource_table,
			     const void *executable, uint32_t executable_size,
			     uint32_t *out_resource_id, bool priv)
{
	uint32_t res_id;
	struct pva_kmd_resource_record *rec =
		pva_kmd_alloc_resource_id(resource_table, &res_id, priv);
	enum pva_error err;
	struct pva_kmd_vpu_bin_resource *vpu_bin;

	if (rec == NULL) {
		pva_kmd_log_err(
			"pva_kmd_add_vpu_bin_resource No more resource id");
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

	pva_kmd_mutex_lock(&resource_table->resource_table_lock);
	if (res_id > resource_table->curr_max_resource_id) {
		resource_table->curr_max_resource_id = res_id;
	}
	pva_kmd_mutex_unlock(&resource_table->resource_table_lock);

	rec->type = PVA_RESOURCE_TYPE_EXEC_BIN;
	rec->ref_count = 1;
	*out_resource_id = res_id;

	return PVA_SUCCESS;
free_block:
	pva_kmd_free_resource_id(resource_table, res_id);
err_out:
	return err;
}

enum pva_error
pva_kmd_make_resource_entry(struct pva_kmd_resource_table *resource_table,
			    uint32_t resource_id,
			    struct pva_resource_entry *entry)
{
	struct pva_kmd_resource_record *rec;
	rec = pva_kmd_use_resource(resource_table, resource_id);
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
		/* CERT INT31-C: iova_access_flags limited to PVA_ACCESS_* values (1,2,3),
		 * always fits in uint8_t, safe to cast */
		entry->access_flags = (uint8_t)rec->dram.mem->iova_access_flags;
		break;
	case PVA_RESOURCE_TYPE_EXEC_BIN:
		entry->type = rec->type;
		entry->addr_lo = iova_lo(rec->vpu_bin.metainfo_mem->iova);
		entry->addr_hi = iova_hi(rec->vpu_bin.metainfo_mem->iova);
		entry->size_lo = iova_lo(rec->vpu_bin.metainfo_mem->size);
		entry->size_hi = iova_hi(rec->vpu_bin.metainfo_mem->size);
		entry->smmu_context_id =
			rec->vpu_bin.metainfo_mem->smmu_ctx_idx;
		entry->access_flags = PVA_ACCESS_RO;
		break;
	case PVA_RESOURCE_TYPE_DMA_CONFIG:
		entry->type = rec->type;
		entry->addr_lo = iova_lo(rec->dma_config.iova_addr);
		entry->addr_hi = iova_hi(rec->dma_config.iova_addr);
		entry->size_lo = iova_lo(rec->dma_config.size);
		entry->size_hi = iova_hi(rec->dma_config.size);
		entry->smmu_context_id = PVA_R5_SMMU_CONTEXT_ID;
		entry->access_flags = PVA_ACCESS_RO;
		break;
	default:
		pva_kmd_log_err("Unsupported resource type");
		pva_kmd_fault();
		/* NOTREACHED */
	}

	pva_kmd_drop_resource(resource_table, resource_id);
	return PVA_SUCCESS;
}

enum pva_error pva_kmd_add_dma_config_resource(
	struct pva_kmd_resource_table *resource_table,
	const struct pva_ops_dma_config_register *dma_cfg_hdr,
	uint32_t dma_config_size, uint32_t *out_resource_id, bool priv)
{
	enum pva_error err = PVA_SUCCESS;
	uint32_t fw_fetch_size;
	void *fw_dma_cfg;
	struct pva_kmd_dma_resource_aux *dma_aux;
	struct pva_kmd_resource_record *rec;
	uint32_t res_id;
	struct pva_kmd_devmem_element dma_cfg_mem = { 0 };
	bool skip_validation = false;

	err = pva_kmd_devmem_pool_zalloc(&resource_table->dma_config_pool,
					 &dma_cfg_mem);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}
	fw_dma_cfg = pva_kmd_get_devmem_va(&dma_cfg_mem);

	ASSERT(((uintptr_t)(void *)fw_dma_cfg) % sizeof(uint64_t) == 0UL);

	dma_aux = pva_kmd_zalloc(sizeof(struct pva_kmd_dma_resource_aux));
	if (dma_aux == NULL) {
		err = PVA_NOMEM;
		goto free_dma_cfg_mem;
	}
	dma_aux->res_table = resource_table;

	skip_validation = (priv || resource_table->pva->test_mode);
	pva_kmd_mutex_lock(&resource_table->resource_table_lock);
	err = pva_kmd_load_dma_config(resource_table, dma_cfg_hdr,
				      dma_config_size, dma_aux, fw_dma_cfg,
				      &fw_fetch_size, skip_validation);
	pva_kmd_mutex_unlock(&resource_table->resource_table_lock);
	if (err != PVA_SUCCESS) {
		goto free_dma_aux;
	}

	rec = pva_kmd_alloc_resource_id(resource_table, &res_id, priv);
	if (rec == NULL) {
		err = PVA_NO_RESOURCE_ID;
		goto unload_dma;
	}

	pva_kmd_mutex_lock(&resource_table->resource_table_lock);
	if (res_id > resource_table->curr_max_resource_id) {
		resource_table->curr_max_resource_id = res_id;
	}
	pva_kmd_mutex_unlock(&resource_table->resource_table_lock);

	rec->type = PVA_RESOURCE_TYPE_DMA_CONFIG;
	rec->ref_count = 1;
	rec->dma_config.devmem = dma_cfg_mem;
	rec->dma_config.aux_mem = dma_aux;
	rec->dma_config.iova_addr = pva_kmd_get_devmem_iova(&dma_cfg_mem);
	rec->dma_config.size = fw_fetch_size;

	*out_resource_id = res_id;

	return PVA_SUCCESS;
unload_dma:
	pva_kmd_mutex_lock(&resource_table->resource_table_lock);
	pva_kmd_unload_dma_config_unsafe(dma_aux);
	pva_kmd_mutex_unlock(&resource_table->resource_table_lock);
free_dma_aux:
	pva_kmd_free(dma_aux);
free_dma_cfg_mem:
	pva_kmd_devmem_pool_free(&dma_cfg_mem);
err_out:
	return err;
}

static enum pva_error
pva_kmd_release_all_resources(struct pva_kmd_resource_table *res_table)
{
	uint32_t id;

	pva_kmd_mutex_lock(&res_table->resource_table_lock);

	// Iterate through all possible resource IDs
	for (id = PVA_RESOURCE_ID_BASE; id <= res_table->curr_max_resource_id;
	     id++) {
		struct pva_kmd_resource_record *rec =
			pva_kmd_peek_resource(res_table, id);
		if (rec != NULL) {
			pva_kmd_release_resource(res_table, id, false);
		}
	}
	pva_kmd_mutex_unlock(&res_table->resource_table_lock);
	return PVA_SUCCESS;
}

void pva_kmd_resource_table_deinit(struct pva_kmd_resource_table *res_table)
{
	(void)pva_kmd_release_all_resources(res_table);
	pva_kmd_block_allocator_deinit(&res_table->resource_record_allocator);
	pva_kmd_block_allocator_deinit(
		&res_table->priv_resource_record_allocator);
	pva_kmd_free(res_table->records_mem);
	pva_kmd_devmem_pool_deinit(&res_table->dma_config_pool);
	pva_kmd_mutex_deinit(&res_table->resource_table_lock);
	pva_kmd_sema_deinit(&res_table->resource_semaphore);
	pva_kmd_device_memory_free(res_table->table_mem);
}

void pva_kmd_resource_table_lock(struct pva_kmd_device *pva,
				 uint8_t res_table_id)
{
	struct pva_kmd_context *ctx = pva_kmd_get_context(pva, res_table_id);

	if (ctx != NULL) {
		pva_kmd_mutex_lock(
			&ctx->ctx_resource_table.resource_table_lock);
	}
}

void pva_kmd_resource_table_unlock(struct pva_kmd_device *pva,
				   uint8_t res_table_id)
{
	struct pva_kmd_context *ctx = pva_kmd_get_context(pva, res_table_id);

	if (ctx != NULL) {
		pva_kmd_mutex_unlock(
			&ctx->ctx_resource_table.resource_table_lock);
	}
}
