/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_DEVICE_H
#define PVA_KMD_DEVICE_H
#include "pva_constants.h"
#include "pva_fw.h"
#include "pva_kmd_cmdbuf.h"
#include "pva_kmd_utils.h"
#include "pva_kmd_mutex.h"
#include "pva_kmd_block_allocator.h"
#include "pva_kmd_queue.h"
#include "pva_kmd_resource_table.h"
#include "pva_kmd_submitter.h"
#include "pva_kmd_regs.h"
#include "pva_kmd_thread_sema.h"
#include "pva_kmd_fw_debug.h"
#include "pva_kmd_shim_init.h"
#include "pva_kmd_shim_ccq.h"
#include "pva_kmd_fw_profiler.h"
#include "pva_kmd_fw_debug.h"
#include "pva_kmd_constants.h"
#include "pva_kmd_debugfs.h"
#include "pva_kmd_co.h"

#define NV_PVA0_CLASS_ID 0xF1
#define NV_PVA1_CLASS_ID 0xF2

struct pva_syncpt_rw_info {
	uint32_t syncpt_id;
	uint64_t syncpt_iova;
};

/** A struct to maintain start and end address of vmem region */
struct vmem_region {
	/**! Start address of vmem region */
	uint32_t start;
	/**! End address of vmem region */
	uint32_t end;
};

struct pva_kmd_hw_constants {
	enum pva_hw_gen hw_gen;
	uint8_t n_vmem_regions;
	uint32_t n_dma_descriptors;
	uint32_t n_user_dma_channels;
	uint32_t n_hwseq_words;
	uint32_t n_dynamic_adb_buffs;
	uint32_t n_smmu_contexts;
};

/**
 * @brief This struct manages a single PVA cluster.
 *
 * Fields in this struct should be common across all platforms. Platform
 * specific data is stored in plat_data field.
 */
struct pva_kmd_device {
	uint32_t device_index;
	uint32_t r5_image_smmu_context_id;
	uint32_t stream_ids[PVA_MAX_NUM_SMMU_CONTEXTS];

	struct pva_kmd_hw_constants hw_consts;

	uint64_t reg_phy_base[PVA_KMD_APERTURE_COUNT];
	uint64_t reg_size[PVA_KMD_APERTURE_COUNT];

	struct pva_kmd_regspec regspec;

	uint8_t max_n_contexts;
	void *context_mem;
	struct pva_kmd_block_allocator context_allocator;

	struct pva_kmd_resource_table dev_resource_table;

	struct pva_kmd_submitter submitter;
	/** The lock protects the submission to the queue, including
	 * incrementing the post fence */
	pva_kmd_mutex_t submit_lock;
	struct pva_kmd_device_memory *queue_memory;
	struct pva_kmd_queue dev_queue;
	pva_kmd_mutex_t ccq0_lock;

	/** memory needed for submission: including command buffer chunks and fences */
	struct pva_kmd_device_memory *submit_memory;
	uint32_t submit_memory_resource_id;
	uint64_t fence_offset; /**< fence offset within submit_memory*/

	pva_kmd_mutex_t chunk_pool_lock;
	struct pva_kmd_cmdbuf_chunk_pool chunk_pool;

	pva_kmd_mutex_t powercycle_lock;
	uint32_t refcount;

	/** ISR post this semaphore when FW completes boot */
	pva_kmd_sema_t fw_boot_sema;
	bool recovery;

	struct pva_kmd_device_memory *fw_debug_mem;
	struct pva_kmd_device_memory *fw_bin_mem;

	// 'kmd_fw_buffers' holds DRAM buffers shared between KMD and FW
	// - Today, we have 1 buffer per CCQ. This may need to be extended in future
	//   to support buffered communication through mailbox
	// - Buffers will be used for the following purposes
	//   - CCQ 0: Communications common to a VM
	//		-- example, FW profiling data and NSIGHT data
	//   - CCQ 1-8: Communications specific to each context
	//		-- example, resource unregistration requests
	// In the future, we may want to extend this to support communications between
	// FW and Hypervisor
	struct pva_kmd_shared_buffer kmd_fw_buffers[PVA_MAX_NUM_CCQ];

	uint32_t fw_debug_log_level;
	struct pva_kmd_fw_print_buffer fw_print_buffer;

	struct pva_kmd_device_memory *tegra_stats_memory;
	uint32_t tegra_stats_resource_id;
	uint32_t tegra_stats_buf_size;

	bool load_from_gsc;
	bool is_hv_mode;
	struct pva_kmd_debugfs_context debugfs_context;
	/** Sector packing format for block linear surfaces */
	uint8_t bl_sector_pack_format;

	/** Offset between 2 syncpoints */
	uint32_t syncpt_page_size;
	uint64_t ro_syncpt_base_iova;
	uint32_t num_ro_syncpts;

	uint64_t rw_syncpt_base_iova;
	uint32_t rw_syncpt_region_size;
	struct pva_syncpt_rw_info rw_syncpts[PVA_NUM_RW_SYNCPTS];

	struct vmem_region *vmem_regions_tab;
	bool support_hwseq_frame_linking;

	void *plat_data;
	void *fw_handle;

	struct pva_vpu_auth *pva_auth;
	bool is_suspended;

	/** Carveout info for FW */
	struct pva_co_info fw_carveout;

	bool test_mode;
};

struct pva_kmd_device *pva_kmd_device_create(enum pva_chip_id chip_id,
					     uint32_t device_index,
					     bool app_authenticate,
					     bool test_mode);

void pva_kmd_device_destroy(struct pva_kmd_device *pva);

enum pva_error pva_kmd_device_busy(struct pva_kmd_device *pva);
void pva_kmd_device_idle(struct pva_kmd_device *pva);

enum pva_error pva_kmd_ccq_push_with_timeout(struct pva_kmd_device *pva,
					     uint8_t ccq_id, uint64_t ccq_entry,
					     uint64_t sleep_interval_us,
					     uint64_t timeout_us);

enum pva_error pva_kmd_config_fw_after_boot(struct pva_kmd_device *pva);

bool pva_kmd_device_maybe_on(struct pva_kmd_device *pva);

static inline uint32_t pva_kmd_get_device_class_id(struct pva_kmd_device *pva)
{
	if (pva->device_index == 0) {
		return NV_PVA0_CLASS_ID;
	} else {
		return NV_PVA1_CLASS_ID;
	}
}

static inline uint16_t
pva_kmd_get_max_cmdbuf_chunk_size(struct pva_kmd_device *pva)
{
	if (pva->test_mode) {
		return PVA_TEST_MODE_MAX_CMDBUF_CHUNK_SIZE;
	} else {
		return PVA_MAX_CMDBUF_CHUNK_SIZE;
	}
}
#endif // PVA_KMD_DEVICE_H
