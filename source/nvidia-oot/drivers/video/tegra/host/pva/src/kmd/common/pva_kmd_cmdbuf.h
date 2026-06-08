/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_CMDBUF_H
#define PVA_KMD_CMDBUF_H
#include "pva_fw.h"
#include "pva_resource.h"
#include "pva_kmd_block_allocator.h"
#include "pva_kmd_mutex.h"
#include "pva_api_cmdbuf.h"
#include "pva_utils.h"
#include "pva_math_utils.h"
#include "pva_kmd_limits.h"

struct pva_kmd_queue;

/**
 * @brief Fixed-size pool of command buffer chunks for efficient allocation
 *
 * @details A fixed-size pool of command buffer chunks designed for efficient
 * command buffer allocation and management. The pool allocates chunks from
 * a pre-allocated memory region and provides automatic cleanup through post
 * fence notifications. When submitting chunks, a post fence should be requested
 * for the first chunk, and when the fence is triggered, the entire chain of
 * chunks is automatically freed by the pool.
 */
struct pva_kmd_cmdbuf_chunk_pool {
	/**
	 * @brief Size of each chunk in bytes
	 * Valid range: [1 .. UINT16_MAX]
	 */
	uint16_t chunk_size;

	/**
	 * @brief Total number of chunks in the pool
	 * Valid range: [1 .. UINT32_MAX]
	 */
	uint32_t num_chunks;

	/**
	 * @brief Resource ID of the memory used for this pool
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t mem_resource_id;

	/**
	 * @brief Total size of memory allocated for the pool
	 * Valid range: [chunk_size .. UINT64_MAX]
	 */
	uint64_t mem_size;

	/**
	 * @brief Starting offset within the resource for this pool
	 *
	 * @details Starting offset in the resource that can be used by this pool
	 * Valid range: [0 .. UINT64_MAX]
	 */
	uint64_t mem_offset;

	/**
	 * @brief Offset to chunk state tracking information
	 * Valid range: [0 .. UINT64_MAX]
	 */
	uint64_t chunk_states_offset;

	/**
	 * @brief Virtual address base pointer for pool memory access
	 */
	void *mem_base_va;

	/**
	 * @brief Block allocator for managing chunk allocation within the pool
	 */
	struct pva_kmd_block_allocator block_allocator;

	/**
	 * @brief Mutex protecting chunk state modifications
	 */
	pva_kmd_mutex_t chunk_state_lock;
};

/**
 * @brief Calculate required memory size for a command buffer chunk pool
 *
 * @details This function performs the following operations:
 * - Calculates the memory needed for all chunks of the specified size
 * - Adds storage space required for free notifier fences (uint32_t per chunk)
 * - Returns the total memory requirement for pool initialization
 * - Accounts for chunk state tracking and fence notification overhead
 *
 * The returned size should be used when allocating memory for pool
 * initialization with @ref pva_kmd_cmdbuf_chunk_pool_init().
 *
 * @param[in] chunk_size  Size of each chunk in bytes
 *                        Valid range: [1 .. UINT16_MAX]
 * @param[in] num_chunks  Number of chunks in the pool
 *                        Valid range: [1 .. UINT32_MAX]
 *
 * @retval memory_size  Total memory size required for the pool in bytes
 */
static inline uint64_t
pva_kmd_cmdbuf_pool_get_required_mem_size(uint16_t chunk_size,
					  uint32_t num_chunks)
{
	/* Add storage required for free notifier fences */
	return (chunk_size + sizeof(uint32_t)) * num_chunks;
}

/**
 * @brief Initialize a command buffer chunk pool
 *
 * @details This function performs the following operations:
 * - Initializes the chunk pool structure with provided memory and parameters
 * - Sets up the block allocator for managing individual chunk allocation
 * - Configures chunk size, count, and memory layout parameters
 * - Establishes virtual address mapping for pool memory access
 * - Initializes synchronization primitives for thread-safe operations
 * - Prepares free notifier fence storage for automatic chunk cleanup
 * - Associates the pool with the specified memory resource
 *
 * The chunk pool enables efficient allocation and automatic cleanup of
 * command buffer chunks. After initialization, chunks can be allocated using
 * @ref pva_kmd_alloc_cmdbuf_chunk() and will be automatically freed when
 * their associated fence is triggered.
 *
 * @param[out] cmdbuf_chunk_pool  Pointer to @ref pva_kmd_cmdbuf_chunk_pool structure to initialize
 *                                Valid value: non-null
 * @param[in] mem_resource_id     Resource ID of the memory for the pool
 *                                Valid range: [0 .. UINT32_MAX]
 * @param[in] mem_offset          Offset within the resource for pool memory
 *                                Valid range: [0 .. UINT64_MAX]
 * @param[in] mem_size            Size of memory allocated for the pool
 *                                Valid range: [required_size .. UINT32_MAX]
 * @param[in] chunk_size          Size of each chunk in bytes
 *                                Valid range: [1 .. UINT16_MAX]
 * @param[in] num_chunks          Number of chunks in the pool
 *                                Valid range: [1 .. UINT32_MAX]
 * @param[in] mem_base_va         Virtual address base of the memory resource
 *                                Valid value: non-null
 *
 * @retval PVA_SUCCESS                Pool initialized successfully
 * @retval PVA_NOMEM                  Failed to initialize block allocator
 */
enum pva_error pva_kmd_cmdbuf_chunk_pool_init(
	struct pva_kmd_cmdbuf_chunk_pool *cmdbuf_chunk_pool,
	uint32_t mem_resource_id, uint64_t mem_offset, uint32_t mem_size,
	uint16_t chunk_size, uint32_t num_chunks, void *mem_base_va);

/**
 * @brief Deinitialize a command buffer chunk pool and clean up resources
 *
 * @details This function performs the following operations:
 * - Ensures all chunks have been properly freed and returned to the pool
 * - Deinitializes the block allocator used for chunk management
 * - Destroys synchronization primitives including mutexes
 * - Cleans up internal pool state and data structures
 * - Invalidates the pool for future use
 * - Does not free the underlying memory resource (caller's responsibility)
 *
 * All chunks should be freed before calling this function. After
 * deinitialization, the pool cannot be used for chunk allocation until
 * it is reinitialized.
 *
 * @param[in, out] cmdbuf_chunk_pool  Pointer to @ref pva_kmd_cmdbuf_chunk_pool structure
 *                                    to deinitialize
 *                                    Valid value: non-null, must be initialized
 */
void pva_kmd_cmdbuf_chunk_pool_deinit(
	struct pva_kmd_cmdbuf_chunk_pool *cmdbuf_chunk_pool);

/**
 * @brief Allocate a command buffer chunk from the pool
 *
 * @details This function performs the following operations:
 * - Allocates a chunk from the pool using the internal block allocator
 * - Returns a unique chunk ID for referencing the allocated chunk
 * - Marks the chunk as in use within the pool's tracking system
 * - Provides chunk memory for command buffer construction
 * - Enables automatic cleanup when the chunk is submitted with a fence
 *
 * If the chunk is submitted with a free-notifier fence, the chunk will be
 * automatically freed when the fence is triggered. If the chunk is not
 * submitted, it should be manually freed using @ref pva_kmd_free_linked_cmdbuf_chunks()
 * to avoid memory leaks.
 *
 * @param[in, out] cmdbuf_chunk_pool  Pointer to @ref pva_kmd_cmdbuf_chunk_pool structure
 *                                    Valid value: non-null, must be initialized
 * @param[out] out_chunk_id           Pointer to store the allocated chunk ID
 *                                    Valid value: non-null
 *
 * @retval PVA_SUCCESS         Chunk allocated successfully
 * @retval PVA_NOMEM           No chunks available in the pool
 */
enum pva_error
pva_kmd_alloc_cmdbuf_chunk(struct pva_kmd_cmdbuf_chunk_pool *cmdbuf_chunk_pool,
			   uint32_t *out_chunk_id);

/**
 * @brief Free a linked list of command buffer chunks
 *
 * @details This function performs the following operations:
 * - Traverses the linked list of chunks starting from the specified chunk ID
 * - Frees each chunk in the chain back to the pool for reuse
 * - Updates pool state to reflect the freed chunks
 * - Handles chunk linking and ensures proper cleanup of the entire chain
 * - Resets chunk state tracking for the freed chunks
 *
 * This function should only be called if the chunks are not submitted,
 * typically in error paths where command buffer construction fails.
 * For submitted chunks, automatic cleanup occurs when the free-notifier
 * fence is triggered, making manual cleanup unnecessary.
 *
 * @param[in, out] cmdbuf_chunk_pool  Pointer to @ref pva_kmd_cmdbuf_chunk_pool structure
 *                                    Valid value: non-null, must be initialized
 * @param[in] chunk_id                ID of the first chunk in the linked list to free
 *                                    Valid range: Valid chunk ID from previous allocation
 */
void pva_kmd_free_linked_cmdbuf_chunks(
	struct pva_kmd_cmdbuf_chunk_pool *cmdbuf_chunk_pool, uint32_t chunk_id);

/**
 * @brief Get the free-notifier fence for automatic chunk cleanup
 *
 * @details This function performs the following operations:
 * - Configures a post fence for automatic chunk cleanup upon completion
 * - Associates the fence with the specified chunk for cleanup tracking
 * - Sets up the fence to trigger when the command buffer completes execution
 * - Enables automatic return of chunks to the pool when work is finished
 * - Configures fence parameters for proper cleanup timing
 *
 * The returned fence should be submitted along with the command buffer
 * to enable automatic cleanup. When the fence is triggered by firmware
 * upon command completion, all linked chunks starting from the specified
 * chunk will be automatically freed and returned to the pool.
 *
 * @param[in] cmdbuf_chunk_pool  Pointer to @ref pva_kmd_cmdbuf_chunk_pool structure
 *                               Valid value: non-null, must be initialized
 * @param[in] chunk_id           ID of the first chunk in the command buffer to be submitted
 *                               Valid range: Valid chunk ID from previous allocation
 * @param[out] fence             Pointer to @ref pva_fw_postfence structure to configure
 *                               Valid value: non-null
 */
void pva_kmd_get_free_notifier_fence(
	struct pva_kmd_cmdbuf_chunk_pool *cmdbuf_chunk_pool, uint32_t chunk_id,
	struct pva_fw_postfence *fence);

/**
 * @brief Get virtual address pointer for a command buffer chunk
 *
 * @details This function performs the following operations:
 * - Calculates the virtual memory address for the specified chunk
 * - Uses the chunk ID and pool's base address for address calculation
 * - Returns a pointer suitable for writing command data
 * - Provides direct memory access to the chunk's storage area
 * - Enables efficient command buffer construction in allocated chunks
 *
 * The returned pointer can be used to write command data directly into
 * the chunk memory. The pointer remains valid until the chunk is freed
 * or the pool is deinitialized.
 *
 * @param[in] cmdbuf_chunk_pool  Pointer to @ref pva_kmd_cmdbuf_chunk_pool structure
 *                               Valid value: non-null, must be initialized
 * @param[in] chunk_id           Chunk ID to get virtual address for
 *                               Valid range: Valid chunk ID from previous allocation
 *
 * @retval pointer  Virtual address pointer to the chunk memory
 */
static inline void *
pva_kmd_get_cmdbuf_chunk_va(struct pva_kmd_cmdbuf_chunk_pool *cmdbuf_chunk_pool,
			    uint32_t chunk_id)
{
	/* Use byte pointer arithmetic to avoid INT36-C violation */
	char *base;
	size_t offset;

	/* Verify alignment for proper access */
	ASSERT(cmdbuf_chunk_pool->chunk_size % sizeof(uint32_t) == 0U);

	base = (char *)cmdbuf_chunk_pool->mem_base_va;
	offset = (size_t)chunk_id * cmdbuf_chunk_pool->chunk_size;
	return (void *)(base + offset);
}

/**
 * @brief Get resource offset for a command buffer chunk
 *
 * @details This function performs the following operations:
 * - Calculates the offset of the chunk within the memory resource
 * - Uses safe arithmetic to prevent overflow during calculation
 * - Adds the pool's memory offset to the chunk-specific offset
 * - Returns the absolute offset for firmware addressing
 * - Enables proper IOVA calculations for hardware access
 *
 * The returned offset can be used by firmware to calculate IOVA addresses
 * for accessing the chunk memory during command execution. The offset is
 * relative to the base of the memory resource.
 *
 * @param[in] cmdbuf_chunk_pool  Pointer to @ref pva_kmd_cmdbuf_chunk_pool structure
 *                               Valid value: non-null, must be initialized
 * @param[in] chunk_id           Chunk ID to get resource offset for
 *                               Valid range: Valid chunk ID from previous allocation
 *
 * @retval offset  Resource offset of the chunk in bytes
 */
static inline uint64_t pva_kmd_get_cmdbuf_chunk_res_offset(
	struct pva_kmd_cmdbuf_chunk_pool *cmdbuf_chunk_pool, uint32_t chunk_id)
{
	uint64_t chunk_size = (uint64_t)safe_mulu32(
		chunk_id, (uint32_t)cmdbuf_chunk_pool->chunk_size);
	return safe_addu64(cmdbuf_chunk_pool->mem_offset, chunk_size);
}

/**
 * @brief Utility for building command buffers with automatic chunk management
 *
 * @details This structure provides a convenient interface for building command
 * buffers that may span multiple chunks. The builder automatically allocates
 * new chunks from the pool when the current chunk becomes full, links chunks
 * together with appropriate commands, and manages the overall command buffer
 * structure. It simplifies command buffer construction by handling chunk
 * boundaries and linking transparently.
 */
struct pva_kmd_cmdbuf_builder {
	/**
	 * @brief Size of the first chunk in the command buffer
	 * Valid range: [1 .. UINT16_MAX]
	 */
	uint16_t first_chunk_size;

	/**
	 * @brief Current write offset within the active chunk
	 * Valid range: [0 .. chunk_size-1]
	 */
	uint16_t current_chunk_offset;

	/**
	 * @brief Chunk ID of the first chunk in the command buffer
	 * Valid range: Valid chunk ID from pool allocation
	 */
	uint32_t first_chunk_id;

	/**
	 * @brief Chunk ID of the currently active chunk being written
	 * Valid range: Valid chunk ID from pool allocation
	 */
	uint32_t current_chunk_id;

	/**
	 * @brief Pointer to the chunk pool for automatic allocation
	 */
	struct pva_kmd_cmdbuf_chunk_pool *pool;

	/**
	 * @brief Pointer to chunk size field of the previous link_chunk command
	 *
	 * @details Pointer to the chunk size field of the previous link_chunk command
	 */
	uint16_t *chunk_size_ptr;
};

/**
 * @brief Initialize a command buffer builder with the specified chunk pool
 *
 * @details This function performs the following operations:
 * - Initializes the builder structure with the provided chunk pool
 * - Allocates the first chunk from the pool for command buffer construction
 * - Sets up internal state for tracking chunk usage and linking
 * - Prepares the builder for command addition through @ref pva_kmd_reserve_cmd_space()
 * - Configures chunk linking and boundary management
 * - Establishes the foundation for multi-chunk command buffer construction
 *
 * The initialized builder can be used to construct command buffers by
 * reserving space for commands and writing command data. The builder
 * automatically handles chunk allocation and linking as needed.
 *
 * @param[out] builder     Pointer to @ref pva_kmd_cmdbuf_builder structure to initialize
 *                         Valid value: non-null
 * @param[in] chunk_pool   Pointer to @ref pva_kmd_cmdbuf_chunk_pool for chunk allocation
 *                         Valid value: non-null, must be initialized
 *
 * @retval PVA_SUCCESS         Builder initialized successfully
 * @retval PVA_NOMEM           Failed to allocate initial chunk from pool
 */
enum pva_error
pva_kmd_cmdbuf_builder_init(struct pva_kmd_cmdbuf_builder *builder,
			    struct pva_kmd_cmdbuf_chunk_pool *chunk_pool);

/**
 * @brief Reserve space in the command buffer for writing command data
 *
 * @details This function performs the following operations:
 * - Checks if the current chunk has sufficient space for the requested size
 * - Allocates a new chunk and links it if the current chunk is insufficient
 * - Updates chunk linking commands to maintain proper command buffer structure
 * - Returns a pointer to the reserved memory space for command writing
 * - Advances internal tracking to account for the reserved space
 * - Handles chunk boundaries transparently for the caller
 *
 * The returned pointer can be used to write command data directly. The
 * space remains reserved until the next call to this function or until
 * the builder is finalized or cancelled.
 *
 * @param[in, out] builder  Pointer to @ref pva_kmd_cmdbuf_builder structure
 *                          Valid value: non-null, must be initialized
 * @param[in] size          Number of bytes to reserve for command data
 *                          Valid range: [1 .. chunk_size]
 *
 * @retval non-null  Pointer to reserved memory space for command writing
 * @retval NULL      Failed to allocate new chunk or insufficient space
 */
void *pva_kmd_reserve_cmd_space(struct pva_kmd_cmdbuf_builder *builder,
				uint16_t size);

/**
 * @brief Finalize command buffer construction and get submission parameters
 *
 * @details This function performs the following operations:
 * - Completes the command buffer construction process
 * - Updates the final chunk size for the last chunk used
 * - Prepares the command buffer for submission to the firmware
 * - Returns the first chunk ID and size for submission setup
 * - Ensures proper command buffer termination and linking
 * - Transfers ownership of chunks from builder to submission system
 *
 * After finalization, the builder should not be used for further command
 * construction. The returned chunk ID and size should be used for command
 * buffer submission through the queue system.
 *
 * @param[in, out] builder           Pointer to @ref pva_kmd_cmdbuf_builder structure
 *                                   Valid value: non-null, must be initialized with commands
 * @param[out] out_first_chunk_id    Pointer to store the first chunk ID
 *                                   Valid value: non-null
 * @param[out] out_first_chunk_size  Pointer to store the first chunk size
 *                                   Valid value: non-null
 */
void pva_kmd_cmdbuf_builder_finalize(struct pva_kmd_cmdbuf_builder *builder,
				     uint32_t *out_first_chunk_id,
				     uint16_t *out_first_chunk_size);

/**
 * @brief Cancel command buffer construction and free allocated chunks
 *
 * @details This function performs the following operations:
 * - Cancels the command buffer construction process
 * - Frees all chunks allocated by the builder back to the pool
 * - Cleans up internal builder state and chunk linking
 * - Ensures no memory leaks from partially constructed command buffers
 * - Invalidates the builder for future use without reinitialization
 *
 * This function should be called when command buffer construction fails
 * or is cancelled before completion. It ensures proper cleanup of all
 * resources allocated during the construction process.
 *
 * @param[in, out] builder  Pointer to @ref pva_kmd_cmdbuf_builder structure to cancel
 *                          Valid value: non-null, must be initialized
 */
void pva_kmd_cmdbuf_builder_cancel(struct pva_kmd_cmdbuf_builder *builder);

/**
 * @brief Initialize a resource table initialization command
 *
 * @details This function performs the following operations:
 * - Initializes all fields of the command structure to appropriate values
 * - Sets the command opcode to @ref PVA_CMD_OPCODE_INIT_RESOURCE_TABLE
 * - Configures the resource table address using IOVA low and high parts
 * - Sets the maximum number of entries supported by the resource table
 * - Calculates and sets the command length based on structure size
 * - Prepares the command for submission to firmware
 *
 * This inline function provides a convenient way to construct resource table
 * initialization commands with proper field setup and validation. The
 * command instructs firmware to initialize its resource table handling
 * for the specified resource table.
 *
 * @param[out] cmd                Pointer to @ref pva_cmd_init_resource_table command structure
 *                                Valid value: non-null
 * @param[in] resource_table_id   Resource table identifier
 *                                Valid range: [0 .. PVA_KMD_MAX_NUM_KMD_RESOURCES-1]
 * @param[in] iova_addr           IOVA address of the resource table
 *                                Valid range: [0 .. UINT64_MAX]
 * @param[in] max_num_entries     Maximum number of entries in the resource table
 *                                Valid range: [1 .. UINT32_MAX]
 */
static inline void pva_kmd_set_cmd_init_resource_table(
	struct pva_cmd_init_resource_table *cmd, uint8_t resource_table_id,
	uint64_t iova_addr, uint32_t max_num_entries, uint64_t ctx_status_addr)
{
	(void)memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_INIT_RESOURCE_TABLE;
	cmd->header.len = (uint8_t)(sizeof(*cmd) / sizeof(uint32_t));
	cmd->resource_table_id = resource_table_id;
	cmd->resource_table_addr_lo = iova_lo(iova_addr);
	cmd->resource_table_addr_hi = iova_hi(iova_addr);
	cmd->max_n_entries = max_num_entries;
	cmd->ctx_status_addr_lo = iova_lo(ctx_status_addr);
	cmd->ctx_status_addr_hi = iova_hi(ctx_status_addr);
}

static inline void
pva_kmd_set_cmd_deinit_resource_table(struct pva_cmd_deinit_resource_table *cmd,
				      uint8_t resource_table_id)
{
	(void)memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_DEINIT_RESOURCE_TABLE;
	cmd->header.len = (uint8_t)(sizeof(*cmd) / sizeof(uint32_t));
	cmd->resource_table_id = resource_table_id;
}

static inline void pva_kmd_set_cmd_init_queue(struct pva_cmd_init_queue *cmd,
					      uint8_t ccq_id, uint8_t queue_id,
					      uint64_t queue_addr,
					      uint32_t max_num_submit,
					      uint32_t syncpt_id,
					      uint64_t syncpt_addr)
{
	(void)memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_INIT_QUEUE;
	cmd->header.len = (uint8_t)(sizeof(*cmd) / sizeof(uint32_t));
	cmd->ccq_id = ccq_id;
	cmd->queue_id = queue_id;
	cmd->queue_addr_lo = iova_lo(queue_addr);
	cmd->queue_addr_hi = iova_hi(queue_addr);
	cmd->max_n_submits = max_num_submit;
	cmd->syncpt_id = syncpt_id;
	cmd->syncpt_addr_lo = iova_lo(syncpt_addr);
	cmd->syncpt_addr_hi = iova_hi(syncpt_addr);
}

static inline void
pva_kmd_set_cmd_deinit_queue(struct pva_cmd_deinit_queue *cmd, uint8_t ccq_id,
			     uint8_t queue_id)
{
	(void)memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_DEINIT_QUEUE;
	cmd->header.len = (uint8_t)(sizeof(*cmd) / sizeof(uint32_t));
	cmd->ccq_id = ccq_id;
	cmd->queue_id = queue_id;
}

static inline void pva_kmd_set_cmd_update_resource_table(
	struct pva_cmd_update_resource_table *cmd, uint32_t resource_table_id,
	uint32_t resource_id, struct pva_resource_entry const *entry,
	struct pva_resource_aux_info const *aux_info)
{
	(void)memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_UPDATE_RESOURCE_TABLE;
	cmd->header.len = (uint8_t)(sizeof(*cmd) / sizeof(uint32_t));
	/* resource_table_id field is uint8_t - bounded by CCQ ID (max 7) */
	ASSERT(resource_table_id <= U8_MAX);
	cmd->resource_table_id = (uint8_t)resource_table_id;
	cmd->resource_id = resource_id;
	cmd->entry = *entry;
	if (aux_info != NULL) {
		cmd->aux_info = *aux_info;
	}
}

static inline void
pva_kmd_set_cmd_unregister_resource(struct pva_cmd_unregister_resource *cmd,
				    uint32_t resource_id)
{
	(void)memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_UNREGISTER_RESOURCE;
	cmd->header.len = (uint8_t)(sizeof(*cmd) / sizeof(uint32_t));
	cmd->resource_id = resource_id;
}

static inline void
pva_kmd_set_cmd_enable_fw_profiling(struct pva_cmd_enable_fw_profiling *cmd,
				    uint32_t filter, uint8_t timestamp_type)
{
	(void)memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_ENABLE_FW_PROFILING;
	cmd->header.len = (uint8_t)(sizeof(*cmd) / sizeof(uint32_t));
	cmd->filter = filter;
	cmd->timestamp_type = timestamp_type;
}

static inline void
pva_kmd_set_cmd_disable_fw_profiling(struct pva_cmd_disable_fw_profiling *cmd)
{
	(void)memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_DISABLE_FW_PROFILING;
	cmd->header.len = (uint8_t)(sizeof(*cmd) / sizeof(uint32_t));
}

static inline void pva_kmd_set_cmd_get_tegra_stats(
	struct pva_cmd_get_tegra_stats *cmd, uint32_t buffer_resource_id,
	uint32_t buffer_size, uint64_t offset, bool enabled)
{
	(void)memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_GET_TEGRA_STATS;
	cmd->header.len = (uint8_t)(sizeof(*cmd) / sizeof(uint32_t));
	cmd->buffer_resource_id = buffer_resource_id;
	cmd->buffer_offset_hi = iova_hi(offset);
	cmd->buffer_offset_lo = iova_lo(offset);
	cmd->buffer_size = buffer_size;
	cmd->enabled = enabled;
}

static inline void
pva_kmd_set_cmd_set_trace_level(struct pva_cmd_set_trace_level *cmd,
				uint32_t trace_level)
{
	(void)memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_SET_TRACE_LEVEL;
	cmd->header.len = (uint8_t)(sizeof(*cmd) / sizeof(uint32_t));
	cmd->trace_level = trace_level;
}

static inline void pva_kmd_set_cmd_suspend_fw(struct pva_cmd_suspend_fw *cmd)
{
	(void)memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_SUSPEND_FW;
	cmd->header.len = (uint8_t)(sizeof(*cmd) / sizeof(uint32_t));
}

static inline void pva_kmd_set_cmd_resume_fw(struct pva_cmd_resume_fw *cmd)
{
	(void)memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_RESUME_FW;
	cmd->header.len = (uint8_t)(sizeof(*cmd) / sizeof(uint32_t));
}

static inline void pva_kmd_set_cmd_init_shared_dram_buffer(
	struct pva_cmd_init_shared_dram_buffer *cmd, uint8_t interface,
	uint32_t buffer_iova, uint32_t buffer_size)
{
	(void)memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_INIT_SHARED_DRAM_BUFFER;
	cmd->header.len = (uint8_t)(sizeof(*cmd) / sizeof(uint32_t));
	cmd->buffer_iova_hi = iova_hi(buffer_iova);
	cmd->buffer_iova_lo = iova_lo(buffer_iova);
	cmd->buffer_size = buffer_size;
	cmd->interface = interface;
}

static inline void pva_kmd_set_cmd_deinit_shared_dram_buffer(
	struct pva_cmd_deinit_shared_dram_buffer *cmd, uint8_t interface)
{
	(void)memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_DEINIT_SHARED_DRAM_BUFFER;
	cmd->header.len = (uint8_t)(sizeof(*cmd) / sizeof(uint32_t));
	cmd->interface = interface;
}

static inline void
pva_kmd_set_cmd_set_profiling_level(struct pva_cmd_set_profiling_level *cmd,
				    uint32_t level)
{
	(void)memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_SET_PROFILING_LEVEL;
	cmd->header.len = (uint8_t)(sizeof(*cmd) / sizeof(uint32_t));
	cmd->level = level;
}

static inline void pva_kmd_set_cmd_get_version(struct pva_cmd_get_version *cmd,
					       uint64_t buffer_iova)
{
	(void)memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_GET_VERSION;
	cmd->header.len = (uint8_t)(sizeof(*cmd) / sizeof(uint32_t));
	cmd->buffer_iova_hi = iova_hi(buffer_iova);
	cmd->buffer_iova_lo = iova_lo(buffer_iova);
}

static inline void pva_kmd_set_cmd_set_pfsd_cmd_buffer_size(
	struct pva_cmd_set_pfsd_cmd_buffer_size *cmd, uint32_t cmd_buffer_size)
{
	(void)memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_SET_PFSD_CMD_BUFFER_SIZE;
	cmd->header.len = (uint8_t)(sizeof(*cmd) / sizeof(uint32_t));
	cmd->cmd_buffer_size = cmd_buffer_size;
}

#define CMD_LEN(cmd_type) (sizeof(cmd_type) / sizeof(uint32_t))

#endif // PVA_KMD_CMDBUF_H
