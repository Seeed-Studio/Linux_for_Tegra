/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_DMA_CFG_H
#define PVA_KMD_DMA_CFG_H

#include "pva_kmd.h"
#include "pva_resource.h"

/* Mask to extract the GOB offset from the Surface address */
#define PVA_DMA_BL_GOB_OFFSET_MASK 0x3E00U

/* Right shift value for moving GOB offset value extracted from surface address to LSB  */
#define PVA_DMA_BL_GOB_OFFSET_MASK_RSH 6U

#define MAX_DESC_ID 0x3FU

enum pva_dma_frame_rep {
	REPLICATION_NONE = 0,
	REPLICATION_TWO_WAY,
	REPLICATION_FOUR_WAY,
	REPLICATION_EIGHT_WAY,
	REPLICATION_SIXTEEN_WAY,
	REPLICATION_THIRTYTWO_WAY,
	REPLICATION_FULL
};

struct pva_kmd_dma_access_entry {
	int64_t start_addr;
	int64_t end_addr;
};
struct pva_kmd_dma_access {
	struct pva_kmd_dma_access_entry src;
	struct pva_kmd_dma_access_entry dst;
	struct pva_kmd_dma_access_entry dst2;
};

struct pva_kmd_resource_table;
struct pva_kmd_hw_constants;

/** Auxiliary information needed for managing DMA resources:
 *
 * - Hold references to DRAM buffers and VPU bin used by the DMA configuration.
 * - Scratch buffers needed during DMA configuration loading.
 */
struct pva_kmd_dma_resource_aux {
	struct pva_kmd_resource_table *res_table;
	uint32_t vpu_bin_res_id;

	uint32_t dram_res_count;
	/** DRAM buffers statically referenced by the DMA configuration */
	uint32_t static_dram_res_ids[PVA_KMD_MAX_NUM_DMA_DRAM_SLOTS];
};

/* Scratch buffers needed during DMA configuration loading. They don't fit on stack. */
struct pva_kmd_dma_scratch_buffer {
	struct pva_fw_dma_slot static_slots[PVA_KMD_MAX_NUM_DMA_SLOTS];
	struct pva_fw_dma_reloc static_relocs[PVA_KMD_MAX_NUM_DMA_SLOTS];
	struct pva_kmd_dma_access access_sizes[PVA_MAX_NUM_DMA_DESC];
	uint64_t hw_dma_descs_mask[((PVA_MAX_NUM_DMA_DESC / 64ULL) + 1ULL)];
};

enum pva_error
pva_kmd_parse_dma_config(const struct pva_ops_dma_config_register *dma_cfg_hdr,
			 uint32_t dma_config_size,
			 struct pva_dma_config *out_cfg,
			 struct pva_kmd_hw_constants const *hw_consts);

enum pva_error
pva_kmd_dma_use_resources(struct pva_dma_config const *dma_cfg,
			  struct pva_kmd_dma_resource_aux *dma_aux);

enum pva_error
pva_kmd_validate_dma_config(struct pva_dma_config const *dma_cfg,
			    struct pva_kmd_hw_constants const *hw_consts,
			    struct pva_kmd_dma_access *access_sizes,
			    uint64_t *hw_dma_descs_mask);

enum pva_error
pva_kmd_compute_dma_access(struct pva_dma_config const *dma_cfg,
			   struct pva_kmd_dma_access *access_sizes,
			   uint64_t *hw_dma_descs_mask);

void pva_kmd_collect_relocs(struct pva_dma_config const *dma_cfg,
			    struct pva_kmd_dma_access const *access_sizes,
			    struct pva_fw_dma_slot *out_static_slots,
			    uint16_t num_static_slots,
			    struct pva_fw_dma_reloc *out_static_relocs,
			    struct pva_fw_dma_slot *out_dyn_slots,
			    uint16_t num_dyn_slots,
			    struct pva_fw_dma_reloc *out_dyn_relocs,
			    uint8_t const *desc_to_ch);

/**
 * @brief Bind static buffers to the DMA configuration.
 *
 * When binding static buffers, we edit pva_dma_config in-place and replace the
 * offset field with the final addresses of static buffers.
 *
 * We also validate that the DMA configuration does not access those static
 * buffers out of range.
 */
enum pva_error pva_kmd_bind_static_buffers(
	struct pva_dma_config_resource *fw_dma_cfg,
	struct pva_kmd_dma_resource_aux *dma_aux,
	struct pva_fw_dma_slot const *static_slots, uint16_t num_static_slots,
	struct pva_fw_dma_reloc const *static_relocs,
	struct pva_dma_static_binding const *static_bindings,
	uint32_t num_static_bindings);

/**
 * @brief Convert user DMA configuration to firmware format.
 */
void pva_kmd_write_fw_dma_config(struct pva_dma_config const *dma_cfg,
				 void *fw_dma_config,
				 uint32_t *out_fw_fetch_size,
				 bool support_hwseq_frame_linking);

/**
 * @brief Load DMA configuration into firmware format.
 *
 * This function mostly does the following things:
 *
 * - Validate the DMA configuration.
 * - Bind static resources (buffers) and embed their addresses directly in the
 *   firmware DMA configuration.
 * - Hold references to DRAM buffers and VPU bin used by the DMA configuration.
 * - Convert the DMA configuration into firmware format.
 *
 * @param resource_table the resource table for the context.
 * @param dma_config DMA configuration from user space.
 * @param dma_config_size Size of the dma_config buffer.
 * @param dma_aux Auxiliary information needed for loading the DMA
 *        configuration.
 * @param fw_dma_cfg Output buffer for the firmware DMA configuration.
 * @param out_fw_fetch_size Size of the firmware DMA configuration that needs to
 *        be fetched into TCM.
 */
enum pva_error
pva_kmd_load_dma_config(struct pva_kmd_resource_table *resource_table,
			const struct pva_ops_dma_config_register *dma_cfg_hdr,
			uint32_t dma_config_size,
			struct pva_kmd_dma_resource_aux *dma_aux,
			void *fw_dma_cfg, uint32_t *out_fw_fetch_size);

void pva_kmd_unload_dma_config_unsafe(struct pva_kmd_dma_resource_aux *dma_aux);
#endif // PVA_KMD_DMA_CFG_H
