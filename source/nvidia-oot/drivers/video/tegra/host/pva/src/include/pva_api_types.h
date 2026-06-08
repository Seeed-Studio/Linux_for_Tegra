/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_API_TYPES_H
#define PVA_API_TYPES_H
#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__KERNEL__)
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
/* Use offsetof to avoid INT36-C violation from NULL pointer arithmetic */
#define container_of(ptr, type, member)                                        \
	((type *)((char *)(ptr)-offsetof(type, member)))
#else
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#define UINT64_MAX U64_MAX
#define UINT32_MAX U32_MAX
#endif

#define FOREACH_ERR(ACT)                                                       \
	ACT(PVA_SUCCESS)                                                       \
	ACT(PVA_UNKNOWN_ERROR)                                                 \
	ACT(PVA_BAD_PARAMETER_ERROR)                                           \
	ACT(PVA_NOT_IMPL)                                                      \
	ACT(PVA_NOENT)                                                         \
	ACT(PVA_NOMEM)                                                         \
	ACT(PVA_INVAL)                                                         \
	ACT(PVA_TIMEDOUT)                                                      \
	ACT(PVA_INTERNAL)                                                      \
	ACT(PVA_CMDBUF_NOT_FOUND)                                              \
	ACT(PVA_CMDBUF_INVALID)                                                \
	ACT(PVA_CMDBUF_TOO_LARGE)                                              \
	ACT(PVA_RES_OUT_OF_RANGE)                                              \
	ACT(PVA_AGAIN)                                                         \
	ACT(PVA_NO_RESOURCE_ID)                                                \
	ACT(PVA_INVALID_RESOURCE)                                              \
	ACT(PVA_INVALID_RESOURCE_SIZE)                                         \
	ACT(PVA_INVALID_RESOURCE_ALIGNMENT)                                    \
	ACT(PVA_QUEUE_FULL)                                                    \
	ACT(PVA_INVALID_IOVA)                                                  \
	ACT(PVA_NO_PERM)                                                       \
	ACT(PVA_INVALID_CMD_OPCODE)                                            \
	ACT(PVA_BUF_OUT_OF_RANGE)                                              \
	ACT(PVA_CMDBUF_NO_BEGIN)                                               \
	ACT(PVA_NO_CCQ)                                                        \
	ACT(PVA_INPUT_STATUS_ERROR)                                            \
	ACT(PVA_ENOSPC)                                                        \
	ACT(PVA_EACCES)                                                        \
	ACT(PVA_ERANGE)                                                        \
	ACT(PVA_BAD_SURFACE_BASE_ALIGNMENT)                                    \
	ACT(PVA_BAD_DESC_ADDR_ALIGNMENT)                                       \
	ACT(PVA_INVALID_DMA_CONFIG)                                            \
	ACT(PVA_INVALID_SYMBOL)                                                \
	ACT(PVA_INVALID_BINDING)                                               \
	ACT(PVA_EINTR)                                                         \
	ACT(PVA_FILL_NVSCIBUF_ATTRS_FAILED)                                    \
	ACT(PVA_NVSCIBUF_SET_ATTR_FAILED)                                      \
	ACT(PVA_IMPORT_FROM_NVSCIBUF_FAILED)                                   \
	ACT(PVA_NVSCISYNC_SET_ATTR_FAILED)                                     \
	ACT(PVA_RETRIEVE_DATA_FROM_NVSCISYNC_FAILED)                           \
	ACT(PVA_UPDATE_DATA_TO_NVSCISYNC_FAILED)                               \
	ACT(PVA_UNSUPPORTED_NVSCISYNC_TIMESTAMP_FORMAT)                        \
	ACT(PVA_INVALID_NVSCISYNC_FENCE)                                       \
	ACT(PVA_ERR_CMD_NOT_SUPPORTED)                                         \
	ACT(PVA_CUDA_INITIALIZED)                                              \
	ACT(PVA_CUDA_LOAD_LIBRARY_FAILED)                                      \
	ACT(PVA_CUDA_ADD_CLIENT_FAILED)                                        \
	ACT(PVA_CUDA_REMOVE_CLIENT_FAILED)                                     \
	ACT(PVA_CUDA_INIT_FAILED)                                              \
	ACT(PVA_CUDA_SUBMIT_FAILED)                                            \
	ACT(PVA_CUDA_GET_RM_HANDLE_FAILED)                                     \
	ACT(PVA_CUDA_INTERNAL_ERROR)                                           \
	ACT(PVA_ERR_CMD_INVALID_VPU_STATE)                                     \
	ACT(PVA_ERR_CMD_VMEM_BUF_OUT_OF_RANGE)                                 \
	ACT(PVA_ERR_CMD_L2SRAM_BUF_OUT_OF_RANGE)                               \
	ACT(PVA_ERR_CMD_DRAM_BUF_OUT_OF_RANGE)                                 \
	ACT(PVA_ERR_CMD_INVALID_BLOCK_HEIGHT)                                  \
	ACT(PVA_ERR_CMD_PAYLOAD_TOO_SMALL)                                     \
	ACT(PVA_ERR_CMD_ENGINE_NOT_ACQUIRED)                                   \
	ACT(PVA_ERR_CMD_INVALID_SYMBOL_TYPE)                                   \
	ACT(PVA_ERR_CMD_INVALID_ENGINE)                                        \
	ACT(PVA_ERR_CMD_INVALID_DMA_SET_ID)                                    \
	ACT(PVA_ERR_CMD_INVALID_DMA_SLOT_ID)                                   \
	ACT(PVA_ERR_CMD_INVALID_DMA_SLOT_TYPE)                                 \
	ACT(PVA_ERR_CMD_INVALID_USER_ALLOWANCE)                                \
	ACT(PVA_ERR_CMD_INCOMPATIBLE_RESOURCE)                                 \
	ACT(PVA_ERR_CMD_INSUFFICIENT_PRIVILEGE)                                \
	ACT(PVA_ERR_CMD_INVALID_BARRIER_ID)                                    \
	ACT(PVA_ERR_CMD_CAPTURE_SLOTS_EXCEEDED)                                \
	ACT(PVA_ERR_CMD_INVALID_CAPTURE_MODE)                                  \
	ACT(PVA_ERR_CMD_INVALID_L2SRAM_POLICY)                                 \
	ACT(PVA_ERR_FW_DMA0_IRQ_ENABLE_FAILED)                                 \
	ACT(PVA_ERR_FW_DMA1_IRQ_ENABLE_FAILED)                                 \
	ACT(PVA_ERR_FW_BAD_DMA_STATE)                                          \
	ACT(PVA_ERR_FW_RESOURCE_IN_USE)                                        \
	ACT(PVA_ERR_FW_VPU_ERROR_STATE)                                        \
	ACT(PVA_ERR_FW_VPU_RETCODE_NONZERO)                                    \
	ACT(PVA_ERR_FW_INVALID_CMD_OPCODE)                                     \
	ACT(PVA_ERR_FW_INVALID_VPU_CMD_SEQ)                                    \
	ACT(PVA_ERR_FW_INVALID_DMA_CMD_SEQ)                                    \
	ACT(PVA_ERR_FW_INVALID_L2SRAM_CMD_SEQ)                                 \
	ACT(PVA_ERR_FW_ENGINE_NOT_RELEASED)                                    \
	ACT(PVA_ERR_FW_UTEST)                                                  \
	ACT(PVA_ERR_VPU_ERROR_STATE)                                           \
	ACT(PVA_ERR_VPU_RETCODE_NONZERO)                                       \
	ACT(PVA_ERR_VPU_ILLEGAL_INSTR)                                         \
	ACT(PVA_ERR_VPU_DIVIDE_BY_0)                                           \
	ACT(PVA_ERR_VPU_FP_NAN)                                                \
	ACT(PVA_ERR_VPU_IN_DEBUG)                                              \
	ACT(PVA_ERR_VPU_DLUT_CFG)                                              \
	ACT(PVA_ERR_VPU_DLUT_MISS)                                             \
	ACT(PVA_ERR_VPU_CP_ACCESS)                                             \
	ACT(PVA_ERR_PPE_ILLEGAL_INSTR)                                         \
	ACT(PVA_ERR_MATH_OP)                                                   \
	ACT(PVA_ERR_HWSEQ_INVALID)                                             \
	ACT(PVA_ERR_FW_ABORTED)                                                \
	ACT(PVA_ERR_PPE_DIVIDE_BY_0)                                           \
	ACT(PVA_ERR_PPE_FP_NAN)                                                \
	ACT(PVA_ERR_INVALID_ACCESS_MODE_COMBINATION)                           \
	ACT(PVA_ERR_CMD_TCM_BUF_OUT_OF_RANGE)                                  \
	ACT(PVA_ERR_MISR_NOT_RUN)                                              \
	ACT(PVA_ERR_MISR_DATA)                                                 \
	ACT(PVA_ERR_MISR_ADDR)                                                 \
	ACT(PVA_ERR_MISR_NOT_DONE)                                             \
	ACT(PVA_ERR_MISR_ADDR_DATA)                                            \
	ACT(PVA_ERR_MISR_TIMEOUT)                                              \
	ACT(PVA_ERR_DMA_ACTIVE_AFTER_VPU_EXIT)                                 \
	ACT(PVA_ERR_CCQ_TIMEOUT)                                               \
	ACT(PVA_ERR_WDT_TIMEOUT)                                               \
	ACT(PVA_ERR_HOST1X_ERR)                                                \
	ACT(PVA_ERR_GOLDEN_REG_MISMATCH)                                       \
	ACT(PVA_ERR_CRITICAL_REG_MISMATCH)                                     \
	ACT(PVA_ERR_CONFIG_REG_MISMATCH)                                       \
	ACT(PVA_ERR_BAD_CONTEXT)                                               \
	ACT(PVA_ERR_BAD_DEVICE)                                                \
	ACT(PVA_ERR_FAST_RESET_FAILURE)                                        \
	ACT(PVA_ERR_DVMS_GET_VM_STATE_FAILED)                                  \
	ACT(PVA_ERR_INVALID_VPU_SYSCALL)                                       \
	ACT(PVA_ERR_CODE_COUNT)

enum pva_error {
#define ADD_COMMA(name) name,
	FOREACH_ERR(ADD_COMMA)
#undef ADD_COMMA
};

enum pva_chip_id {
	PVA_CHIP_T19X,
	PVA_CHIP_T23X,
	PVA_CHIP_T26X,
	PVA_CHIP_OTHERS
};

enum pva_hw_gen {
	PVA_HW_GEN1,
	PVA_HW_GEN2,
	PVA_HW_GEN3,
};

/**
 * @brief API restriction flags reported by @ref pva_get_api_restrictions.
 *
 * These flags indicate which categories of the public API are permitted by the
 * current platform/runtime policy. Implementations currently set exactly one
 * of the flags below, but the type is defined as bit-flags for forward
 * compatibility.
 */
/** No restrictions: all API categories are permitted. */
#define PVA_API_ALL_ALLOWED (0U)
/** Initialization-related APIs are not permitted in the current state.
 *  Callers must defer creation/initialization flows until allowed. */
#define PVA_API_INIT_NOT_ALLOWED (1U)

/* Opaque API data types */
struct pva_context;
struct pva_queue;
struct pva_memory;

struct pva_memory_attrs {
	uint32_t access_mode;
	uint64_t offset;
	uint64_t size;
};

/**
 * @brief A memory address accessible by PVA.
 */
struct pva_dram_addr {
	uint32_t resource_id;
	uint64_t offset;
};

struct pva_vmem_addr {
	uint32_t symbol_id;
	uint32_t offset;
};

/**
 * @brief Represents a synchronization fence, which can be associated with
 * either a memory semaphore or a syncpoint for signaling or waiting operations.
 *
 * The UMD handles semaphores and syncpoints differently when used as
 * postfences:
 * - Semaphores: UMD does not track future values.
 * - Syncpoints: UMD tracks future values.
 *
 * To use semaphore for either prefences and postfences:
 * - Set `semaphore_resource_id` to the resource ID of the memory backing the semaphore.
 * - Set `index` to the byte offset divided by the semaphore size (`sizeof(uint32_t)`).
 * - Set `value` to the semaphore's signaling or waiting value.
 *
 * To use syncpoint for prefences:
 * - Set `semaphore_resource_id` to `PVA_RESOURCE_ID_INVALID`.
 * - Set `index` to the syncpoint ID to wait for.
 * - Set `value` to the waiting value.
 *
 * To use syncpoint for postfences:
 * - Set `semaphore_resource_id` to `PVA_RESOURCE_ID_INVALID`.
 * - Do not set `index` or `value`.
 * - After submission, UMD will assign `index` to the queue syncpoint ID and `value` to the expected future value.
 */
struct pva_fence {
	/** Resource ID of the memory semaphore. If resource ID is
	 * PVA_RESOURCE_ID_INVALID, then the sync object primitive is assumed to
	 * be syncpoint. */
	uint32_t semaphore_resouce_id;
	/** Represents either the semaphore index or the syncpoint ID, depending
	 *  on the sync object primitive type.
	 */
	uint32_t index;
	/** Represents the semaphore or syncpoint value used for signaling or
	 * waiting. */
	uint32_t value;
};

struct pva_fw_vpu_ptr_symbol {
	uint64_t base;
	uint64_t offset;
	uint64_t size;
};

enum pva_surface_format {
	PVA_SURF_FMT_PITCH_LINEAR = 0,
	PVA_SURF_FMT_BLOCK_LINEAR
};

enum pva_memory_segment {
	/** Memory segment directly reachable by R5. Command buffer chunk
	 * memories need to be allocated from this segment */
	PVA_MEMORY_SEGMENT_R5 = 1,
	/** Memory segment reachable only by DMA. User buffers should be
	 * allocated from this segment */
	PVA_MEMORY_SEGMENT_DMA = 2,
};

enum pva_symbol_type {
	/*! Specifies the an invalid symbol type */
	PVA_SYM_TYPE_INVALID = 0,
	/*! Specifies a data symbol */
	PVA_SYM_TYPE_DATA,
	/*! Specifies a VPU config table symbol */
	PVA_SYM_TYPE_VPUC_TABLE,
	/*! Specifies a Pointer symbol */
	PVA_SYM_TYPE_POINTER,
	/*! Specifies a System symbol */
	PVA_SYM_TYPE_SYSTEM,
	/*! Specifies an extended Pointer symbol */
	PVA_SYM_TYPE_POINTER_EX,
	PVA_SYM_TYPE_MAX,
};

#define PVA_SYMBOL_ID_INVALID 0U
#define PVA_SYMBOL_ID_BASE 1U
#define PVA_MAX_SYMBOL_NAME_LEN 64U
struct pva_symbol_info {
	char name[PVA_MAX_SYMBOL_NAME_LEN + 1U];
	enum pva_symbol_type symbol_type;
	uint32_t size;
	uint32_t vmem_addr;
	/** Symbol ID local to this executable */
	uint32_t symbol_id; /*< Starting from PVA_SYMBOL_ID_BASE */
};

#define PVA_RESOURCE_ID_INVALID 0U

/** \brief Maximum number of queues per context */
#define PVA_MAX_QUEUES_PER_CONTEXT (8)

/** \brief Specifies the memory is GPU CACHED. */
#define PVA_GPU_CACHED_MEMORY (1u << 1u)

#define PVA_ACCESS_RO (1U << 0) /**< Read only access */
#define PVA_ACCESS_WO (1U << 1) /**< Write only access */
#define PVA_ACCESS_RW                                                          \
	(PVA_ACCESS_RO | PVA_ACCESS_WO) /**< Read and write access */

// unify timeout to uint64_t, in microseconds
#define PVA_SUBMIT_TIMEOUT_INF UINT64_MAX /**< Infinite timeout */

#define PVA_MAX_NUM_INPUT_STATUS 2U /**< Maximum number of input statuses */
#define PVA_MAX_NUM_OUTPUT_STATUS 2U /**< Maximum number of output statuses */
#define PVA_MAX_NUM_PREFENCES 2U /**< Maximum number of pre-fences */
#define PVA_MAX_NUM_POSTFENCES 2U /**< Maximum number of post-fences */
/** Maximum number of timestamps */
#define PVA_MAX_NUM_TIMESTAMPS PVA_MAX_NUM_POSTFENCES

struct pva_cmdbuf_submit_info {
	uint8_t num_prefences;
	uint8_t num_postfences;
	uint8_t num_input_status;
	uint8_t num_output_status;
	uint8_t num_timestamps;
#define PVA_ENGINE_AFFINITY_NONE 0
#define PVA_ENGINE_AFFINITY_ENGINE0 (1U << 0)
#define PVA_ENGINE_AFFINITY_ENGINE1 (1U << 1)
#define PVA_ENGINE_AFFINITY_ANY                                                \
	(PVA_ENGINE_AFFINITY_ENGINE0 | PVA_ENGINE_AFFINITY_ENGINE1)
	uint8_t engine_affinity;
	/** Size of the first chunk */
	uint16_t first_chunk_size;
	/** Resource ID of the first chunk */
	uint32_t first_chunk_resource_id;
	/** User provided submission identifier */
	uint64_t submit_id;
	/** Offset of the first chunk within the resource */
	uint64_t first_chunk_offset;
/** Execution timeout is in ms */
#define PVA_EXEC_TIMEOUT_INF UINT32_MAX
#define PVA_EXEC_TIMEOUT_REUSE (UINT32_MAX - 1U)
	/** Execution Timeout */
	uint32_t execution_timeout_ms;
	struct pva_fence prefences[PVA_MAX_NUM_PREFENCES];
	struct pva_fence postfences[PVA_MAX_NUM_POSTFENCES];
	struct pva_dram_addr input_statuses[PVA_MAX_NUM_INPUT_STATUS];
	struct pva_dram_addr output_statuses[PVA_MAX_NUM_OUTPUT_STATUS];
	struct pva_dram_addr timestamps[PVA_MAX_NUM_TIMESTAMPS];
};

struct pva_cmdbuf_status {
	/** Timestamp reflecting when the status was updated. This is in resolution of ns */
	uint64_t timestamp;
	/** Additional status information for the engine state */
	uint32_t info32;
	/** Index of cmd that resulted in error */
	uint16_t info16;
	/** Error code. Type: enum pva_error */
	uint16_t status;
};

/** @brief Holds the PVA capabilities. */
struct pva_characteristics {
	/** Holds the number of PVA engines. */
	uint32_t pva_engine_count;
	/** Holds the number of VPUs per PVA engine. */
	uint32_t pva_pve_count;
	/** Holds the PVA generation information */
	enum pva_hw_gen hw_version;
	uint16_t max_desc_count;
	uint16_t max_ch_count;
	uint16_t max_adb_count;
	uint16_t max_hwseq_word_count;
	uint16_t max_vmem_region_count;
	uint16_t reserved_desc_start;
	uint16_t reserved_desc_count;
	uint16_t reserved_adb_start;
	uint16_t reserved_adb_count;
};

/*
 * !!!! DO NOT MODIFY !!!!!!
 * These values are defined as per DriveOS guidelines
 */
#define PVA_INPUT_STATUS_SUCCESS ((uint16_t)(0))
#define PVA_INPUT_STATUS_INVALID ((uint16_t)(0xFFFF))

/**
 * @brief Context attribute keys.
 */
enum pva_attr {
	PVA_CONTEXT_ATTR_MAX_CMDBUF_CHUNK_SIZE,
	PVA_ATTR_HW_CHARACTERISTICS,
	PVA_ATTR_VERSION
};

/**
 * @brief Maximum size of a command buffer chunk.
 */
struct pva_ctx_attr_max_cmdbuf_chunk_size {
	uint16_t max_size;
};

struct pva_async_error {
	uint32_t error;
	uint32_t queue_id;
	uint32_t cmd_idx;
	int32_t vpu_retcode;
	uint64_t submit_id;
};

#ifdef __cplusplus
}
#endif

#endif // PVA_API_TYPES_H
