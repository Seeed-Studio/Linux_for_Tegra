/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_API_CMDBUF_H
#define PVA_API_CMDBUF_H
#include "pva_api_types.h"

/** The common header for all commands.
 */
struct pva_cmd_header {
	/** Opcode for the command. MSB of opcode indicates whether this command is
	 * privileged or not */
	uint8_t opcode;
	/** Command specific flags  */
	uint8_t flags;
	/**
	* For pva_cmd_barrier: barrier_group specifies which group this barrier
	*	waits for.
	* For pva_cmd_retire_barrier_group: barrier_group specifies which id will
	*	be retired. Retired ids can be re-used by future commands and will refer
	*	to a new logical group.
	* For all other commands: barrier_group specifies which barrier group this
	*	command belongs to. Other commands are able to defer execution until all
	* 	commands in the barrier group have completed, or stall the cmd buffer
	*	until such a time. Note that asynchronous commands may complete in an
	*	order different to the order in which they appear in the commmand
	*	buffer.
	*/
	uint8_t barrier_group;
	/** Length in 4-bytes, including this header. */
	uint8_t len;
};

struct pva_dma_misr_config {
#define PVA_DMA_FLAG_MISR_ENABLE 1u
	uint8_t enabled;
	uint8_t reserved;
	uint16_t channel_mask;
	uint32_t seed_crc0;
	uint32_t seed_crc1;
	uint32_t ref_addr;
	uint32_t ref_data_1;
	uint32_t ref_data_2;
	uint32_t misr_timeout;
};

struct pva_dma_misr {
	uint32_t slot_mask_low0;
	uint32_t slot_mask_low1;
	uint32_t slot_mask_high;
	struct pva_dma_misr_config misr_config;
};

struct pva_user_dma_allowance {
#define PVA_USER_DMA_ALLOWANCE_ADB_STEP_SIZE 8
	/*desc start index and descriptor count should be multiple of 4*/
	uint32_t channel_idx : 4;
	uint32_t desc_start_idx : 7;
	uint32_t desc_count : 7;
	uint32_t adb_start_idx : 6;
	uint32_t adb_count : 6;
};

/**
 * @brief Parameter array structure for pva_cmd_set_vpu_parameter_array command
 *
 * Memory layout: [pva_vpu_parameter_array][parameter_data][next_array]...
 * Parameter data must be 4-byte aligned. Each data_size is rounded up to 4-byte boundary.
 */
struct pva_vpu_parameter_array {
	uint16_t data_size;
	uint8_t pad[2];
	uint32_t symbol_id;
	uint32_t vmem_offset;
};

/*struct for set_vpu_array_parameter_with_address*/
struct pva_vpu_parameter_with_address_array {
	uint8_t flags; /**< Control flags: 0x1=legacy, 0=modern (default) */
	uint8_t dram_offset_hi;
	uint8_t pad[2];
	uint32_t symbol_id;
	uint32_t dram_resource_id;
	uint32_t dram_offset_lo;
};

/*struct for set_vpu_array_parameter_with_buffer*/
struct pva_vpu_parameter_with_buffer_array {
	uint8_t src_dram_offset_hi;
	uint8_t pad[3];
	uint32_t data_size;
	uint32_t dst_symbol_id;
	uint32_t dst_vmem_offset;
	uint32_t src_dram_resource_id;
	uint32_t src_dram_offset_lo;
};

/* Basic Commands */

/** Does nothing. It can be used as a place holder in the command buffer. */
struct pva_cmd_noop {
#define PVA_CMD_OPCODE_NOOP 0U
	struct pva_cmd_header header;
};

/** Link next chunk. This command can be placed anywhere in the command buffer.
 * Firmware will start fetching the next chunk when this command is executed. */
struct pva_cmd_link_chunk {
#define PVA_CMD_OPCODE_LINK_CHUNK 1U
	struct pva_cmd_header header;
	uint8_t next_chunk_offset_hi;
	uint8_t pad;
	uint16_t next_chunk_size; /**< Size of next chunk in bytes */
	uint32_t next_chunk_resource_id;
	uint32_t next_chunk_offset_lo;
	struct pva_user_dma_allowance user_dma;
};

/** Barrier command. The user can assign a barrier group to any asynchronous
 * command. The barrier command blocks FW execution until the specified group of
 * asynchronous commands have completed. Up to 8 barrier groups are supported.
 *
 * @note A barrier command is not typically required since FW stalls
 * automatically in the event of hardware conflicts or when issuing a command is
 * deemed unsafe according to the state machines. However, if a stall is needed
 * for other reasons, the barrier command can be utilized.
 */
struct pva_cmd_barrier {
#define PVA_CMD_OPCODE_BARRIER 2U
	struct pva_cmd_header header;
};

/** Acquire one or more PVE systems, each of which includes a VPS, DMA and PPE.
 * It blocks until specified number of engines are acquired.
 * By default, the lowest engine ID acquired is set as the current engine.
 * Acquired engines will be automatically released when this command buffer finishes.
 * They can also be released using release_engine command.
 */
struct pva_cmd_acquire_engine {
#define PVA_CMD_OPCODE_ACQUIRE_ENGINE 3U
	struct pva_cmd_header header;
	uint8_t engine_count;
	uint8_t pad[3];
};

/** Release all PVE systems acquired. It is legal to release engine when engine
 * is still running. The released engine won't be available to be acquired until
 * it finishes and becomes idle again. */
struct pva_cmd_release_engine {
#define PVA_CMD_OPCODE_RELEASE_ENGINE 4U
	struct pva_cmd_header header;
};

/** Set a PVE engine as current. Following commands will modify this engine. The
 * zero-based engine index must be less than the acquired engine number. */
struct pva_cmd_set_current_engine {
#define PVA_CMD_OPCODE_SET_CURRENT_ENGINE 5U
	struct pva_cmd_header header;
	uint8_t engine_index;
	uint8_t pad[3];
};

/** This command specifies the executable to use for the following VPU launches.
 * It doesn't do anything other than setting the context for the following
 * commands.
 *
 * Note: This command cannot be initiated if any of the DMA sets (that access
 * VMEM) are in a running state, in order to prevent mismatches between DMA sets
 * and VPU executables. The command buffer will stall until these DMA sets are
 * finished. */
struct pva_cmd_set_vpu_executable {
#define PVA_CMD_OPCODE_SET_VPU_EXECUTABLE 6U
	struct pva_cmd_header header;
	uint32_t vpu_exec_resource_id;
};

/** This command clears the entire VMEM. User may choose to skip VMEM clear if
 * there are no bss sections in the VPU executable. Since VMEM can be accessed
 * by both VPU and PPE, this command drives both the VPU state machine and the
 * PPE state machine. As a result, it can only be started if both VPU state
 * machine and PPE state machine are in valid states (Idle or Binded). */
struct pva_cmd_clear_vmem {
#define PVA_CMD_OPCODE_CLEAR_VMEM 7U
	struct pva_cmd_header header;
};

/** This command configures VPU hardware. Specifically, it configures code
 * segment register and copies data sections. */
struct pva_cmd_init_vpu_executable {
#define PVA_CMD_OPCODE_INIT_VPU_EXECUTABLE 8U
	struct pva_cmd_header header;
	struct pva_user_dma_allowance user_dma;
};

/** Start VPU instruction prefetch from specified entry point. Currently, the
 * entry point index must be 0. More entry points will be supported in the
 * future. Note that this command merely triggers the prefetch but does not wait
 * for the prefetch to complete. Therefore, this command is synchronous. */
struct pva_cmd_prefetch_vpu_code {
#define PVA_CMD_OPCODE_PREFETCH_VPU_CODE 9U
	struct pva_cmd_header header;
	uint32_t entry_point_index;
};

/** Run the VPU program from the specified entry point until finish. The
 * lifetime of this command covers the entire VPU program execution. Since this
 * command is asynchronous, it doesn't block the following commands from
 * execution. */
struct pva_cmd_run_vpu {
#define PVA_CMD_OPCODE_RUN_VPU 10U
	struct pva_cmd_header header;
	uint32_t entry_point_index;
};

/** Copy data from opaque payload to a VPU variable. Firmware may choose to copy
 * with R5 or DMA. If using DMA, channel 0 will be used. */
struct pva_cmd_set_vpu_parameter {
#define PVA_CMD_OPCODE_SET_VPU_PARAMETER 11U
	struct pva_cmd_header header;
	uint16_t data_size;
	uint16_t pad;
	uint32_t symbol_id;
	uint32_t vmem_offset;
	/* Followed by <data_size> number of bytes, padded to 4 bytes  */
};

/** Copy data from a DRAM buffer to a VPU variable. DMA will be used to perform
 * the copy. The user can optionally provide a user channel, a descriptor and
 * ADBs to speed up the copy. */
struct pva_cmd_set_vpu_parameter_with_buffer {
#define PVA_CMD_OPCODE_SET_VPU_PARAMETER_WITH_BUFFER 12U
	struct pva_cmd_header header;
	struct pva_user_dma_allowance user_dma;
	uint8_t src_dram_offset_hi;
	uint8_t pad[3];
	uint32_t data_size;
	uint32_t dst_symbol_id;
	uint32_t dst_vmem_offset;
	uint32_t src_dram_resource_id;
	uint32_t src_dram_offset_lo;
};

/** Copy the address of a DRAM buffer to a VPU variable. The variable must be
 * laid out exactly according to pva_fw_vpu_ptr_symbol
 */
struct pva_cmd_set_vpu_parameter_with_address {
#define PVA_CMD_OPCODE_SET_VPU_PARAMETER_WITH_ADDRESS 13U
	struct pva_cmd_header header;
	uint8_t dram_offset_hi;
	uint8_t pad[3];
	uint32_t symbol_id;
	uint32_t dram_resource_id;
	uint32_t dram_offset_lo;
};

#define PVA_MAX_DMA_SETS_PER_DMA_ENGINE 4

/** This command first acquires the TCM scratch and then fetches DMA configuration
 * into the scratch. The command does not modify DMA
 * hardware, allowing FW to continue using user channels for data transfer after
 * its execution. This command only uses channel 0 to fetch the DMA
 * configuration. However, user can still help speed up the process by
 * providing additional ADBs. This command will block if there's no TCM scratch
 * available. If there's no pending commands AND there's no TCM scratch, then it
 * means we encountered a dead lock, the command buffer will be aborted. */
struct pva_cmd_fetch_dma_configuration {
#define PVA_CMD_OPCODE_FETCH_DMA_CONFIGURATION 14U
	struct pva_cmd_header header;
	uint8_t dma_set_id;
	uint8_t pad[3];
	uint32_t resource_id;
	struct pva_user_dma_allowance user_dma;
};

/** Setup DMA hardware registers using previously fetched DMA configuration. FW
 * uses channel 0 to copy DMA descriptors into descriptor RAM. The user can
 * provide additional ADBs to speed up the process. The command will block until
 * the needed channels, descriptors and hwseq words are acquired. The command must
 * also validate that all source and destinations fields of each DMA descriptor
 * being programmed is bound to a resource.
 */
struct pva_cmd_setup_dma {
#define PVA_CMD_OPCODE_SETUP_DMA 15U
	struct pva_cmd_header header;
	struct pva_user_dma_allowance user_dma;
	uint8_t dma_set_id;
	uint8_t pad[3];
};

/** Run DMA channels according to the current DMA configuration until they are
 * finished. The lifetime of this command covers the entire DMA transfer. The
 * command shall block until the needed VDBs/ADBs and triggers (GPIOs) are
 * acquired.

 * @note This command checks that the DMA set to be started is indeed paired
 * with the currently bound VPU executable. If not, this constitutes a
 * programming error, and the command buffer will be aborted. */
struct pva_cmd_run_dma {
#define PVA_CMD_OPCODE_RUN_DMA 16U
	struct pva_cmd_header header;
	uint8_t dma_set_id;
	uint8_t pad[3];
};

/** This command specifies the executable to use for the following PPE launches.
 * It doesn't do anything other than setting the context for the following
 * commands. */
struct pva_cmd_set_ppe_executable {
#define PVA_CMD_OPCODE_SET_PPE_EXECUTABLE 17U
	struct pva_cmd_header header;
	uint32_t ppe_exec_resource_id;
};

/** Start PPE instruction prefetch from specified entry point. Currently, the
 * entry point index must be 0. Note that this command merely triggers the
 * prefetch but does not wait for the prefetch to complete. Therefore, this
 * command is synchronous. */
struct pva_cmd_prefetch_ppe_code {
#define PVA_CMD_OPCODE_PREFETCH_PPE_CODE 18U
	struct pva_cmd_header header;
	uint32_t entry_point_index;
};

/** Setup PPE code segment and data sections. */
struct pva_cmd_init_ppe_executable {
#define PVA_CMD_OPCODE_INIT_PPE_EXECUTABLE 19U
	struct pva_cmd_header header;
	struct pva_user_dma_allowance user_dma;
};

/** Run the PPE program until finish. This lifetime of this command covers the
 * entire PPE program execution. */
struct pva_cmd_run_ppe {
#define PVA_CMD_OPCODE_RUN_PPE 20U
	struct pva_cmd_header header;
	uint32_t entry_point_index;
};

#define PVA_MAX_BARRIER_GROUPS 8U
#define PVA_BARRIER_GROUP_INVALID 0xFFU

/**
 * @brief Captures a timestamp to DRAM
 *
 * This command allows you to capture a timestamp using one of three modes:
 *
 * - **IMMEDIATE_MODE**: Captures the timestamp immediately.
 * - **VPU START MODE**: Enqueue a timestamp to be captured the next time the
 *   current VPU starts. Up to 8 VPU start timestamps may be active at a time
 *   for a given engine.
 * - **VPU DONE MODE**: Enqueue a timestamp to be captured the next time the
 *   current VPU enters done state. Up to 8 VPU done timestamps may be active at
 *   a time for a given engine.
 * - **DEFER MODE**: Defers the timestamp capture by specifying a barrier group.
 *   The timestamp will be captured once the commands in the specified barrier
 *   group have completed. Each barrier group allows one timestamp to be active
 *   at a time.
 *
 * The timestamp will be available in DRAM after waiting on any postfence.
 *
 * @note This command is asynchronous, ensuring it does not block the next command.
 */
struct pva_cmd_capture_timestamp {
#define PVA_CMD_OPCODE_CAPTURE_TIMESTAMP 21U
	struct pva_cmd_header header;
	uint8_t offset_hi;
	uint8_t defer_barrier_group;
#define PVA_CMD_CAPTURE_MODE_IMMEDIATE 0U
#define PVA_CMD_CAPTURE_MODE_VPU_START 1U
#define PVA_CMD_CAPTURE_MODE_VPU_DONE 2U
#define PVA_CMD_CAPTURE_MODE_DEFER 3U
	uint8_t capture_mode;
	uint8_t pad;
	uint32_t resource_id;
	uint32_t offset_lo;
};

/** Set the address of the status buffer. FW will output detailed command buffer
 * status in case of command buffer abort. */
struct pva_cmd_request_status {
#define PVA_CMD_OPCODE_CAPTURE_STATUS 22U
	struct pva_cmd_header header;
	uint8_t offset_hi;
	uint8_t pad[3];
	uint32_t resource_id;
	uint32_t offset_lo;
};

/** Blocks until l2ram is available. To prevent deadlock with other command
 * buffers, l2ram must be acquired prior to acquiring any engine. It will be
 * automatically freed when this command buffer finishes. If persistence is
 * required, it must be saved to DRAM. One command buffer may only hold one
 * L2SRAM allocation at a time. */
struct pva_cmd_bind_l2sram {
#define PVA_CMD_OPCODE_BIND_L2SRAM 23U
	struct pva_cmd_header header;
	uint8_t dram_offset_hi;
#define FILL_ON_MISS (1U << 0U)
#define FLUSH_ON_EVICTION (1U << 1U)
	uint8_t access_policy;
	uint8_t pad[2];
	uint32_t dram_resource_id;
	uint32_t dram_offset_lo;
	uint32_t l2sram_size;
	struct pva_user_dma_allowance user_dma;
};

/** Free previously allocated l2ram. This command is asynchronous because it
 * needs to wait for all commands that are started before it to complete. */
struct pva_cmd_release_l2sram {
#define PVA_CMD_OPCODE_RELEASE_L2SRAM 24U
	struct pva_cmd_header header;
};

/*
 * This command writes data to a DRAM region. The DRAM region is described
 * by resource ID, offset and size fields. The data to be written is placed
 * right after the command struct. For this command to successfully execute,
 * the following conditions must be met:
 * 1. 'resource_id' should point to a valid resource in DRAM.
 * 2. the offset and size fields should add up to be less than or equal to the size of DRAM resource.
 */
struct pva_cmd_write_dram {
#define PVA_CMD_OPCODE_WRITE_DRAM 25U
	struct pva_cmd_header header;
	uint8_t offset_hi;
	uint8_t pad;
	uint16_t write_size;
	uint32_t resource_id;
	uint32_t offset_lo;
	/* Followed by write_size bytes, padded to 4 bytes boundary */
};

/** Set this bit to @ref pva_surface_format to indicate if the surface format is
 * block linear or pitch linear.
 *
 * For block linear surfaces, the starting address for a descriptor is:
 * IOVA_OF(resource_id) + surface_base_offset + PL2BL(slot_offset + desc_offset).
 *
 * For pitch linear surfaces, the starting address for a descriptor is:
 * IOVA_OF(resource_id) + surface_base_offset + slot_offset + desc_offset
 */
#define PVA_CMD_FLAGS_SURFACE_FORMAT_MSB 0U
#define PVA_CMD_FLAGS_SURFACE_FORMAT_LSB 0U
/** MSB of log2 block height in flags field of the command header */
#define PVA_CMD_FLAGS_LOG2_BLOCK_HEIGHT_MSB 3U
/** LSB of log2 block height in flags field of the command header */
#define PVA_CMD_FLAGS_LOG2_BLOCK_HEIGHT_LSB 1U
/** Bind a DRAM surface to a slot. The surface can be block linear or pitch
 * linear. */
struct pva_cmd_bind_dram_slot {
#define PVA_CMD_OPCODE_BIND_DRAM_SLOT 26U
	/** flags field will contain block linear flag and block height */
	struct pva_cmd_header header;
	uint8_t dma_set_id; /**< ID of the DMA set */
	uint8_t slot_offset_hi;
	uint8_t surface_base_offset_hi;
	uint8_t slot_id; /**< ID of slot to bind */
	uint32_t resource_id; /**< Resource ID of the DRAM allocation for the surface */
	uint32_t slot_offset_lo; /**< Per-slot offset in pitch linear domain, from slot base to surface base */
	uint32_t surface_base_offset_lo; /**< Surface base offset in bytes, from surface base to allocation base */
};

struct pva_cmd_bind_vmem_slot {
#define PVA_CMD_OPCODE_BIND_VMEM_SLOT 27U
	struct pva_cmd_header header;
	uint8_t dma_set_id;
	uint8_t slot_id;
	uint8_t pad[2];
	uint32_t symbol_id;
	uint32_t offset;
};

/** @brief Unregisters a resource.
 *
 * This command immediately removes the specified resource from the resource
 * table upon execution. However, FW does not immediately notify KMD to
 * deallocate the resource as it may still be in use by other concurrently
 * running command buffers in the same context.
 *
 * The FW takes note of the currently running command buffers and notifies the
 * KMD to deallocate the resource once these command buffers have completed
 * their execution.
 *
 * @note If a command buffer in the same context either hangs or executes for an
 * extended period, no resources can be effectively freed, potentially leading
 * to resource exhaustion.
 */
struct pva_cmd_unregister_resource {
#define PVA_CMD_OPCODE_UNREGISTER_RESOURCE 28U
	struct pva_cmd_header header;
	uint32_t resource_id;
};

/** Write instance parameter to a VMEM symbol. */
struct pva_cmd_set_vpu_instance_parameter {
#define PVA_CMD_OPCODE_SET_VPU_INSTANCE_PARAMETER 29U
	struct pva_cmd_header header;
	uint32_t symbol_id;
};

struct pva_cmd_set_vpu_print_buffer {
#define PVA_CMD_OPCODE_SET_VPU_PRINT_BUFFER 30U
	struct pva_cmd_header header;
	uint32_t resource_id;
	uint32_t offset;
};

struct pva_cmd_invalidate_l2sram {
#define PVA_CMD_OPCODE_INVALIDATE_L2SRAM 31U
	struct pva_cmd_header header;
	uint8_t dram_offset_hi;
	uint8_t pad[3];
	uint32_t dram_resource_id;
	uint32_t dram_offset_lo;
	uint32_t l2sram_size;
};

struct pva_cmd_flush_l2sram {
#define PVA_CMD_OPCODE_FLUSH_L2SRAM 32U
	struct pva_cmd_header header;
	uint8_t dram_offset_hi;
	uint8_t pad[3];
	uint32_t dram_resource_id;
	uint32_t dram_offset_lo;
	uint32_t l2sram_size;
	struct pva_user_dma_allowance user_dma;
};

struct pva_cmd_patch_l2sram_offset {
#define PVA_CMD_OPCODE_PATCH_L2SRAM_OFFSET 33U
	struct pva_cmd_header header;
	uint8_t dma_set_id;
	uint8_t slot_id;
	uint8_t pad[2];
	uint32_t offset;
};

/** After retiring a barrier group, all future commands which refer to that barrier group id will be
 * mapped to a new logical barrier group. This allows re-using barrier ids within a command buffer.
 */
struct pva_cmd_retire_barrier_group {
#define PVA_CMD_OPCODE_RETIRE_BARRIER_GROUP 34U
	struct pva_cmd_header header;
};

struct pva_cmd_setup_misr {
#define PVA_CMD_OPCODE_SETUP_MISR 35U
	struct pva_cmd_header header;
	struct pva_dma_misr misr_params;
};

struct pva_cmd_set_vpu_parameter_array {
#define PVA_CMD_OPCODE_SET_VPU_PARAMETER_ARRAY 36U
	struct pva_cmd_header header;
	uint16_t param_count;
	uint16_t pad;
};

struct pva_cmd_set_vpu_parameter_with_address_array {
#define PVA_CMD_OPCODE_SET_VPU_PARAMETER_WITH_ADDRESS_ARRAY 37U
	struct pva_cmd_header header;
	uint16_t param_count;
	uint16_t pad;
	/*Followed by param_count number of struct pva_vpu_parameter_with_address_array*/
};

struct pva_cmd_set_vpu_parameter_with_buffer_array {
#define PVA_CMD_OPCODE_SET_VPU_PARAMETER_WITH_BUFFER_ARRAY 38U
	struct pva_cmd_header header;
	uint16_t param_count;
	uint16_t pad;
	struct pva_user_dma_allowance user_dma;
	/*Followed by param_count number of struct pva_vpu_parameter_with_buffer_array*/
};

#define PVA_CMD_OPCODE_MAX 39U

#endif // PVA_API_CMDBUF_H
