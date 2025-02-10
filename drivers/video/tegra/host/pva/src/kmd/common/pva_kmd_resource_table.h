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
#ifndef PVA_KMD_RESOURCE_TABLE_H
#define PVA_KMD_RESOURCE_TABLE_H
#include "pva_fw.h"
#include "pva_bit.h"
#include "pva_resource.h"
#include "pva_kmd_block_allocator.h"
#include "pva_kmd.h"
#include "pva_kmd_utils.h"
#include "pva_kmd_executable.h"
#include "pva_constants.h"
#include "pva_kmd_dma_cfg.h"
#include "pva_kmd_mutex.h"
#include "pva_kmd_thread_sema.h"

struct pva_kmd_device;

struct pva_kmd_dram_resource {
	struct pva_kmd_device_memory *mem;
	bool syncpt;
};

struct pva_kmd_vpu_bin_resource {
	struct pva_kmd_device_memory *metainfo_mem;
	struct pva_kmd_device_memory *sections_mem;
	struct pva_kmd_exec_symbol_table symbol_table;
};

struct pva_kmd_dma_config_resource {
	uint32_t block_index;
	uint64_t size;
	uint64_t iova_addr;
};

struct pva_kmd_resource_record {
	/**
	* Possible types:
	* PVA_RESOURCE_TYPE_DRAM
	* PVA_RESOURCE_TYPE_EXEC_BIN
	* PVA_RESOURCE_TYPE_DMA_CONFIG
	*/
	uint8_t type;
	uint32_t ref_count;
	union {
		struct pva_kmd_dram_resource dram;
		struct pva_kmd_vpu_bin_resource vpu_bin;
		struct pva_kmd_dma_config_resource dma_config;
	};
};

/**
 *
 */
struct pva_kmd_resource_table {
	/** @brief User smmu context ID.
	 *
	 * - DRAM memory, VPU data/text sections will be mapped to this space.
	 * - VPU metadata, DMA configurations will always be mapped to R5 SMMU
	 * context. */
	uint8_t user_smmu_ctx_id;
	uint32_t n_entries;
	/** Maximum resource ID we have seen so far */
	uint32_t curr_max_resource_id;

	/** Semaphore to keep track of resources in use*/
	pva_kmd_sema_t resource_semaphore;

	/** Memory for resource table entries, in R5 segment */
	struct pva_kmd_device_memory *table_mem;

	/** Memory for fw dma configs, in DMA segment */
	struct pva_kmd_device_memory *dma_config_mem;
	struct pva_kmd_block_allocator dma_config_allocator;

	/** Memory for tracking resources used by DMA configuration. Single
	 * allocation shared by all DMA configs */
	struct pva_kmd_dma_resource_aux *dma_aux;

	/** Pointer to syncpt_allocator in pva_kmd_device created during kmd boot */
	struct pva_kmd_block_allocator *syncpt_allocator;

	/** Memory for resource records */
	void *records_mem;
	struct pva_kmd_block_allocator resource_record_allocator;
	struct pva_kmd_device *pva;
};

enum pva_error
pva_kmd_resource_table_init(struct pva_kmd_resource_table *res_table,
			    struct pva_kmd_device *pva,
			    uint8_t user_smmu_ctx_id, uint32_t n_entries,
			    uint32_t max_num_dma_configs);
void pva_kmd_resource_table_deinit(struct pva_kmd_resource_table *res_table);

/** KMD only writes to FW resource table during init time. Once the address of
 * the resource table is sent to FW, all updates should be done through commands.
 */
void pva_kmd_update_fw_resource_table(struct pva_kmd_resource_table *res_table);

enum pva_error
pva_kmd_add_syncpt_resource(struct pva_kmd_resource_table *resource_table,
			    struct pva_kmd_device_memory *dev_mem,
			    uint32_t *out_resource_id);

enum pva_error
pva_kmd_add_dram_buffer_resource(struct pva_kmd_resource_table *resource_table,
				 struct pva_kmd_device_memory *memory,
				 uint32_t *out_resource_id);

enum pva_error
pva_kmd_add_vpu_bin_resource(struct pva_kmd_resource_table *resource_table,
			     void *executable, uint32_t executable_size,
			     uint32_t *out_resource_id);

enum pva_error
pva_kmd_add_dma_config_resource(struct pva_kmd_resource_table *resource_table,
				void *dma_config, uint32_t dma_config_size,
				uint32_t *out_resource_id);

/**
 * Increment reference count of the resources
 *
 * TODO: make use and drop thread safe.
 * */
struct pva_kmd_resource_record *
pva_kmd_use_resource(struct pva_kmd_resource_table *resource_table,
		     uint32_t resource_id);

struct pva_kmd_resource_record *
pva_kmd_peek_resource(struct pva_kmd_resource_table *resource_table,
		      uint32_t resource_id);

void pva_kmd_drop_resource(struct pva_kmd_resource_table *resource_table,
			   uint32_t resource_id);

enum pva_error
pva_kmd_make_resource_entry(struct pva_kmd_resource_table *resource_table,
			    uint32_t resource_id,
			    struct pva_resource_entry *entry);

void pva_kmd_verify_all_resources_free(
	struct pva_kmd_resource_table *resource_table);

#endif // PVA_KMD_RESOURCE_TABLE_H
