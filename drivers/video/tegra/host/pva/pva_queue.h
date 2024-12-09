/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2016-2025 NVIDIA CORPORATION. All rights reserved.
 */

#ifndef PVA_QUEUE_H
#define PVA_QUEUE_H

#include <uapi/linux/nvpva_ioctl.h>
#include "nvpva_queue.h"
#include "nvpva_buffer.h"
#include "pva-sys-params.h"
#include "pva-interface.h"
#include "pva-task.h"
#include "pva_hwseq.h"

#ifdef CONFIG_TEGRA_T26X_GRHOST_PVA
#include <nvpva_ioctl_t264.h>
#else
#define NVPVA_TASK_MAX_DMA_DESCRIPTOR_ID_T26X \
		NVPVA_TASK_MAX_DMA_DESCRIPTORS_T23X
#define NVPVA_TASK_MAX_DMA_CHANNELS_T26X \
		NVPVA_TASK_MAX_DMA_CHANNELS_T23X
#define NVPVA_TASK_MAX_HWSEQ_FRAME_COUNT_T26X	1U
#endif

#define task_err(task, fmt, ...) \
	dev_err(&task->pva->pdev->dev, fmt, ##__VA_ARGS__)

#define MAX_VAL(x, y) (((x) > (y)) ? (x) : (y))
#define MAX_NUM_DESCS	 MAX_VAL((MAX_VAL(NVPVA_TASK_MAX_DMA_DESCRIPTORS_T19X, \
					NVPVA_TASK_MAX_DMA_DESCRIPTORS_T23X)), \
				  NVPVA_TASK_MAX_DMA_DESCRIPTOR_ID_T26X)

#define MAX_NUM_CHANNELS MAX_VAL((MAX_VAL(NVPVA_TASK_MAX_DMA_CHANNELS_T19X, \
					NVPVA_TASK_MAX_DMA_CHANNELS_T23X)), \
				  NVPVA_TASK_MAX_DMA_CHANNELS_T26X)

#define MAX_NUM_FRAMES NVPVA_TASK_MAX_HWSEQ_FRAME_COUNT_T26X

#define PVA_TASK_FREE		0
#define PVA_TASK_ASSIGNED	1
#define PVA_TASK_QUEUED		2
#define PVA_TASK_SUBMITTED	3
#define PVA_TASK_INVALID	4

struct dma_buf;

extern struct nvpva_queue_ops pva_queue_ops;

struct pva_pinned_memory {
	u64 size;
	u64 serial_id;
	dma_addr_t dma_addr;
	struct dma_buf *dmabuf;
	int id;
	enum nvpva_buffers_heap heap;
};

struct pva_cb {
	dma_addr_t head_addr;
	uint32_t *head_va;
	dma_addr_t tail_addr;
	uint32_t *tail_va;
	dma_addr_t err_addr;
	uint32_t *err_va;
	dma_addr_t buffer_addr;
	uint8_t *buffer_va;
	uint32_t tail;
	uint32_t size;
};

/**
 * @brief	descriptor src/dest buffer inforamtion
 *
 * This structure holds information about buffers addressed
 * by a DMA descriptor. This information is used to imrove
 * efficiency of bounds checking on DMA access.
 */
struct pva_dma_task_buffer_info_s {
	uint64_t src_buffer_size;
	uint64_t dst_buffer_size;
	uint64_t dst2_buffer_size;
};

/**
 * @brief	Describe a task for PVA
 *
 * This is an internal representation of the task structure. All
 * pointers refer to kernel memory.
 *
 * pva				Pointer to struct pva
 * buffers			Pointer to struct nvpva_buffers
 * queue			Pointer to struct nvpva_queue
 * node				Used to build queue task list
 * kref				Used to manage allocation and freeing
 * dma_addr			task dma_addr
 * aux_dma_addr			task auxdma_addr
 * va				task virtual address
 * aux_va			task aux virtual address
 * pool_index			task pool index
 * postfence_va			postfence virtual address
 * num_prefences		Number of pre-fences in this task
 * num_postfences		Number of post-fences in this task
 * num_input_surfaces		Number of input surfaces
 * num_output_surfaces		Number of output surfaces
 * num_input_task_status	Number of input task status structures
 * num_output_task_status	Number of output task status structures
 * operation			task operation
 * timeout			Latest Unix time when the task must complete or
 *				0 if disabled.
 * prefences			Pre-fence structures
 * postfences			Post-fence structures
 * input_surfaces		Input surfaces structures
 * input_scalars		Information for input scalars
 * output_surfaces		Output surfaces
 * output_scalars		Information for output scalars
 * input_task_status		Input status structure
 * output_task_status		Output status structure
 *
 */
struct pva_submit_task {
	struct pva *pva;
	struct nvpva_queue *queue;
	struct nvpva_client_context *client;

	struct list_head node;
	struct kref ref;

	dma_addr_t dma_addr;
	dma_addr_t aux_dma_addr;
	void *va;
	void *aux_va;
	int pool_index;

	bool pinned_app;
	u32 exe_id1;
	u64 stream_id;
	u64 prog_id;
	u32 exe_id2;

	u32 l2_alloc_size; /* Not applicable for Xavier */
	struct pva_cb *stdout;
	u32 symbol_payload_size;

	u32 flags;
	u8 num_prefences;
	u8 num_user_fence_actions;
	u8 num_input_task_status;
	u8 num_output_task_status;
	u8 num_dma_descriptors;
	u8 num_dma_channels;
	u8 num_symbols;
	u8 special_access;

	u64 timeout;
	u64 desc_hwseq_frm[2];
	u64 desc_hwseq_t26x[2];
	u64 desc_processed[2];
	u8 num_dma_desc_processed;
	u32 syncpt_thresh;
	u32 fence_num;
	u32 local_sync_counter;

	u32 sem_thresh;
	u32 sem_num;
	u32 id;
	/* Data provided by userspace "as is" */
	struct nvpva_submit_fence prefences[NVPVA_TASK_MAX_PREFENCES];
	struct nvpva_fence_action
		user_fence_actions[NVPVA_MAX_FENCE_TYPES *
				   NVPVA_TASK_MAX_FENCEACTIONS];
	struct nvpva_mem input_task_status[NVPVA_TASK_MAX_INPUT_STATUS];
	struct nvpva_mem output_task_status[NVPVA_TASK_MAX_OUTPUT_STATUS];
	struct nvpva_dma_descriptor
		dma_descriptors[MAX_NUM_DESCS]; /* max of T19x, T23x & T26x */
	struct nvpva_dma_channel dma_channels
		[MAX_NUM_CHANNELS]; /* max of T19x, T23x & T26x */
	struct nvpva_dma_misr dma_misr_config;
	struct nvpva_hwseq_config hwseq_config;
	struct nvpva_symbol_param symbols[NVPVA_TASK_MAX_SYMBOLS];
	u8 symbol_payload[NVPVA_TASK_MAX_PAYLOAD_SIZE];

	struct pva_pinned_memory pinned_memory[256];
	u32 num_pinned;
	u8 num_pva_fence_actions[NVPVA_MAX_FENCE_TYPES];
	struct nvpva_fence_action
		pva_fence_actions[NVPVA_MAX_FENCE_TYPES]
				 [NVPVA_TASK_MAX_FENCEACTIONS];
	u64 fence_act_serial_ids[NVPVA_MAX_FENCE_TYPES]
				[NVPVA_TASK_MAX_FENCEACTIONS];
	u64 prefences_serial_ids[NVPVA_TASK_MAX_PREFENCES];
	struct pva_hwseq_priv_s hwseq_info[MAX_NUM_CHANNELS][MAX_NUM_FRAMES];
	u8 desc_block_height_log2[MAX_NUM_DESCS];
	struct pva_dma_task_buffer_info_s task_buff_info[MAX_NUM_DESCS];
	struct pva_dma_hwseq_desc_entry_s
			 desc_entries[MAX_NUM_CHANNELS][MAX_NUM_FRAMES][PVA_HWSEQ_DESC_LIMIT];

	/** Store Suface base address */
	u64 src_surf_base_addr;
	u64 dst_surf_base_addr;
	bool is_system_app;
	bool default_sem_update_method;
	u8 task_state;
};

struct pva_submit_tasks {
	struct pva_submit_task *tasks[NVPVA_SUBMIT_MAX_TASKS];
	u32 task_thresh[NVPVA_SUBMIT_MAX_TASKS];
	u16 num_tasks;
	u64 execution_timeout_us;
};

#define ACTION_LIST_FENCE_SIZE 21U
#define ACTION_LIST_STATUS_OPERATION_SIZE 11U
#define ACTION_LIST_TERMINATION_SIZE 1U
#define ACTION_LIST_STATS_SIZE 9U
#define PVA_TSC_TICKS_TO_US_FACTOR (0.032f)

/*
 * The worst-case input action buffer size:
 * - Prefences trigger a word memory operation (size 13 bytes)
 * - Input status reads trigger a half-word memory operation (size 11 bytes)
 * - The action list is terminated by a null action (1 byte)
 */
#define INPUT_ACTION_BUFFER_SIZE                                               \
	ALIGN(((NVPVA_TASK_MAX_PREFENCES * ACTION_LIST_FENCE_SIZE) +           \
	       ((NVPVA_TASK_MAX_FENCEACTIONS * 2U) * ACTION_LIST_FENCE_SIZE) + \
	       NVPVA_TASK_MAX_INPUT_STATUS *                                   \
	       ACTION_LIST_STATUS_OPERATION_SIZE +                             \
	       ACTION_LIST_TERMINATION_SIZE),                                  \
	      256)

/**
 * Ensure that sufficient preactions per task are supported by FW/KMD interface.
 * Maximum possible number of preactions can be determined by adding below
 * limits:
 * - Maximum number of prefences allowed per task
 * - Maximum number of SOT_R and SOT_V fences allowed per task
 * - Maximum number of input status buffers allowed per task
 */
#if ((PVA_MAX_PREACTION_LISTS) < \
		( \
			(NVPVA_TASK_MAX_PREFENCES) + \
			(NVPVA_TASK_MAX_FENCEACTIONS * 2U) + \
			(NVPVA_TASK_MAX_INPUT_STATUS) \
		) \
	)
#error "Insufficient preactions supported by FW/KMD interface"
#endif

/**
 * Ensure that sufficient postactions per task are supported by FW/KMD interface.
 * Maximum possible number of postactions can be determined by adding below
 * limits:
 * - Maximum number of EOT_V, EOT_R and EOT fences allowed per task
 * - Maximum number of output status buffers allowed per task
 * - Maximum one postaction for statistics
 */
#if ((PVA_MAX_POSTACTION_LISTS) < \
		( \
			(NVPVA_TASK_MAX_FENCEACTIONS * 3U) + \
			(NVPVA_TASK_MAX_OUTPUT_STATUS) + \
			(1U) \
		) \
	)
#error "Insufficient postactions supported by FW/KMD interface"
#endif

struct PVA_PACKED pva_task_action_ptr_s {
	/* IOVA Pointer to update Sync Point Value */
	pva_iova			p;
	/* Value to be written to Sync Point */
	uint32_t			v;
	/* Pointer to write timestamp */
	pva_iova			t;
};

struct PVA_PACKED pva_task_action_status_s {
	/* IOVA to pva_gen_task_status_t struct */
	pva_iova		p;
	uint16_t		status;
	/* Padding to ensure that structure is 4byte aligned for FW perf optimization */
	uint8_t			pad[2];
};

struct PVA_PACKED pva_task_action_statistics_s {
	/* IOVA to pva_task_statistics_t struct */
	pva_iova			p;
};
struct PVA_PACKED pva_task_action_s {
	uint8_t		action;
	/* Padding to ensure that structure is 4byte aligned for FW perf optimization */
	uint8_t		pad[3];
	union {
		struct pva_task_action_ptr_s		ptr;
		struct pva_task_action_status_s		status;
		struct pva_task_action_statistics_s	statistics;
	} args;
};

/* This structure is created to ensure dma_info and params_list is always
 * stored in contiguous memory within the HW task structure. This is done as a perf
 * optimization so that a single dma copy can be triggered by R5 FW for copying both
 * the dma_info and param_list.
 */
struct pva_dma_info_and_params_list_s {
	struct pva_dma_info_s dma_info;
	struct pva_vpu_parameters_s param_list[NVPVA_TASK_MAX_SYMBOLS];
};

struct pva_hw_task {
	struct pva_td_s task;
	struct pva_task_action_s preactions[PVA_MAX_PREACTION_LISTS];
	struct pva_task_action_s postactions[PVA_MAX_POSTACTION_LISTS];
	struct pva_dma_info_and_params_list_s dma_info_and_params_list;
	struct pva_dma_misr_config_s dma_misr_config;
	struct pva_dtd_s dma_desc[MAX_NUM_DESCS]; /* max of all gens */
	struct pva_vpu_parameter_info_s param_info;
	struct pva_task_statistics_s statistics;
	struct pva_circular_buffer_info_s stdout_cb_info;
};

void pva_task_remove(struct pva_submit_task *task);
void pva_task_free(struct kref *ref);

void pva_task_update(struct work_struct *work);

struct pva_pinned_memory *pva_task_pin_mem(struct pva_submit_task *task,
					   u32 id);

void pva_dmabuf_vunmap(struct dma_buf *dmabuf, void *addr);

void *pva_dmabuf_vmap(struct dma_buf *dmabuf);
/**
 * @brief	pva_dump_queues
 *
 * This function dumps out the contents of queues in the pool
 *
 * @param pva		Pointer to the pva device instance
 * @return		0
 *
 */
int pva_dump_queues(struct pva *pva);
#endif
