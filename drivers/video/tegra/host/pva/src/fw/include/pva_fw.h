/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_FW_H
#define PVA_FW_H
#include "pva_api.h"
#include "pva_bit.h"
#include "pva_constants.h"
#include "pva_fw_address_map.h"
#include "pva_math_utils.h"

/* The sizes of these structs must be explicitly padded to align to 4 bytes */

#define PVA_CMD_PRIV_OPCODE_FLAG (1U << 7U)

#define PVA_RESOURCE_ID_BASE 1U
struct pva_resource_entry {
	uint8_t access_flags : 2; // 1: RO, 2: WO, 3: RW
	uint8_t reserved : 4;
#define PVA_RESOURCE_TYPE_INVALID 0U
#define PVA_RESOURCE_TYPE_DRAM 1U
#define PVA_RESOURCE_TYPE_EXEC_BIN 2U
#define PVA_RESOURCE_TYPE_DMA_CONFIG 3U
	uint8_t type : 2;
	uint8_t smmu_context_id;
	uint8_t addr_hi;
	uint8_t size_hi;
	uint32_t addr_lo;
	uint32_t size_lo;
};

struct pva_resource_aux_info {
	// Serial ID of NvRM memory resources
	uint32_t serial_id_hi;
	uint32_t serial_id_lo;
};

struct pva_cmd_init_resource_table {
#define PVA_CMD_OPCODE_INIT_RESOURCE_TABLE (0U | PVA_CMD_PRIV_OPCODE_FLAG)
	struct pva_cmd_header header;
	/**< Resource table id is from 0 to 7, 0 is the device's resource table,
	 * 1-7 are users'. */
	uint8_t resource_table_id;
	uint8_t resource_table_addr_hi;
	uint8_t pad[2];
	uint32_t resource_table_addr_lo;
	uint32_t max_n_entries;
};

struct pva_cmd_deinit_resource_table {
#define PVA_CMD_OPCODE_DEINIT_RESOURCE_TABLE (1U | PVA_CMD_PRIV_OPCODE_FLAG)
	struct pva_cmd_header header;
	uint8_t resource_table_id;
	uint8_t pad[3];
};

struct pva_cmd_update_resource_table {
#define PVA_CMD_OPCODE_UPDATE_RESOURCE_TABLE (2U | PVA_CMD_PRIV_OPCODE_FLAG)
	struct pva_cmd_header header;
	uint8_t resource_table_id;
	uint8_t pad[3];
	uint32_t resource_id;
	struct pva_resource_entry entry;
	struct pva_resource_aux_info aux_info;
};

struct pva_cmd_init_queue {
#define PVA_CMD_OPCODE_INIT_QUEUE (3U | PVA_CMD_PRIV_OPCODE_FLAG)
	struct pva_cmd_header header;
	uint8_t ccq_id;
	uint8_t queue_id;
	uint8_t queue_addr_hi;
	uint8_t syncpt_addr_hi;
	uint32_t queue_addr_lo;
	uint32_t max_n_submits;
	uint32_t syncpt_addr_lo;
	uint32_t syncpt_id;
};

struct pva_cmd_deinit_queue {
#define PVA_CMD_OPCODE_DEINIT_QUEUE (4U | PVA_CMD_PRIV_OPCODE_FLAG)
	struct pva_cmd_header header;
	uint8_t ccq_id;
	uint8_t queue_id;
	uint8_t pad[2];
};

struct pva_cmd_enable_fw_profiling {
#define PVA_CMD_OPCODE_ENABLE_FW_PROFILING (5U | PVA_CMD_PRIV_OPCODE_FLAG)
	struct pva_cmd_header header;
	uint8_t timestamp_type;
	uint8_t pad[3];
	uint32_t filter;
};

struct pva_cmd_disable_fw_profiling {
#define PVA_CMD_OPCODE_DISABLE_FW_PROFILING (6U | PVA_CMD_PRIV_OPCODE_FLAG)
	struct pva_cmd_header header;
};

struct pva_cmd_get_tegra_stats {
#define PVA_CMD_OPCODE_GET_TEGRA_STATS (7U | PVA_CMD_PRIV_OPCODE_FLAG)
	struct pva_cmd_header header;
	uint8_t buffer_offset_hi;
	bool enabled;
	uint8_t pad[2];
	uint32_t buffer_resource_id;
	uint32_t buffer_size;
	uint32_t buffer_offset_lo;
};

struct pva_cmd_suspend_fw {
#define PVA_CMD_OPCODE_SUSPEND_FW (8U | PVA_CMD_PRIV_OPCODE_FLAG)
	struct pva_cmd_header header;
};

struct pva_cmd_resume_fw {
#define PVA_CMD_OPCODE_RESUME_FW (9U | PVA_CMD_PRIV_OPCODE_FLAG)
	struct pva_cmd_header header;
};

struct pva_cmd_init_shared_dram_buffer {
#define PVA_CMD_OPCODE_INIT_SHARED_DRAM_BUFFER (10U | PVA_CMD_PRIV_OPCODE_FLAG)
	struct pva_cmd_header header;
	uint8_t interface;
	uint8_t buffer_iova_hi;
	uint8_t pad[2];
	uint32_t buffer_iova_lo;
	uint32_t buffer_size;
};

struct pva_cmd_deinit_shared_dram_buffer {
#define PVA_CMD_OPCODE_DEINIT_SHARED_DRAM_BUFFER                               \
	(11U | PVA_CMD_PRIV_OPCODE_FLAG)
	struct pva_cmd_header header;
	uint8_t interface;
	uint8_t pad[3];
};
struct pva_cmd_set_trace_level {
#define PVA_CMD_OPCODE_SET_TRACE_LEVEL (12U | PVA_CMD_PRIV_OPCODE_FLAG)
	struct pva_cmd_header header;
	uint32_t trace_level;
};

struct pva_cmd_set_profiling_level {
#define PVA_CMD_OPCODE_SET_PROFILING_LEVEL (13U | PVA_CMD_PRIV_OPCODE_FLAG)
	struct pva_cmd_header header;
	uint32_t level;
};

struct pva_cmd_get_version {
#define PVA_CMD_OPCODE_GET_VERSION (14U | PVA_CMD_PRIV_OPCODE_FLAG)
	struct pva_cmd_header header;
	uint8_t buffer_iova_hi;
	uint8_t pad[3];
	uint32_t buffer_iova_lo;
};

#define PVA_CMD_PRIV_OPCODE_COUNT 15U

struct pva_fw_prefence {
	uint8_t offset_hi;
	uint8_t pad0[3];
	uint32_t offset_lo;
	uint32_t resource_id;
	uint32_t value;
};

struct pva_fw_postfence {
	uint8_t offset_hi;
	uint8_t ts_offset_hi;
/** Privileged user queue may need to trigger fence that exists in user's own
 * resource table. Set this flags to tell FW to use user's resource table when
 * writing this post fence. This also applies to timestamp resource ID. */
#define PVA_FW_POSTFENCE_FLAGS_USER_FENCE (1 << 0)
	uint8_t flags;
	uint8_t pad0;
	uint32_t offset_lo;
	uint32_t resource_id;
	uint32_t value;

	/* Timestamp part */
	uint32_t ts_resource_id;
	uint32_t ts_offset_lo;
};

struct pva_fw_memory_addr {
	uint8_t offset_hi;
	uint8_t pad0[3];
	uint32_t resource_id;
	uint32_t offset_lo;
};

struct pva_fw_cmdbuf_submit_info {
	uint8_t num_prefence;
	uint8_t num_postfence;
	uint8_t num_input_status;
	uint8_t num_output_status;
#define PVA_CMDBUF_FLAGS_ENGINE_AFFINITY_MSB (1)
#define PVA_CMDBUF_FLAGS_ENGINE_AFFINITY_LSB (0)
	uint8_t flags;
	uint8_t first_chunk_offset_hi;
	/** First chunk size*/
	uint16_t first_chunk_size;
	struct pva_fw_prefence prefences[PVA_MAX_NUM_PREFENCES];
	struct pva_fw_memory_addr input_statuses[PVA_MAX_NUM_INPUT_STATUS];
	/** Resource ID of the first chunk */
	uint32_t first_chunk_resource_id;
	/** First chunk offset within the resource*/
	uint32_t first_chunk_offset_lo;
	/** Execution Timeout */
	uint32_t execution_timeout_ms;
	struct pva_fw_memory_addr output_statuses[PVA_MAX_NUM_OUTPUT_STATUS];
	struct pva_fw_postfence postfences[PVA_MAX_NUM_POSTFENCES];
	uint64_t submit_id;
};

/* This is the header of the circular buffer */
struct pva_fw_submit_queue_header {
	/**
	 * Head index of the circular buffer. Updated by R5, read by CCPLEX
	 * (UMD/KMD).
	 */
	volatile uint32_t cb_head;
	/**
	  * Tail index of the circular buffer. Updated by CCPLEX.
	  *
	  * CCPLEX informs R5 the tail index through CCQ. In case KMD needs to
	  * flush the queue. KMD may need to read the tail from here.
	  */
	volatile uint32_t cb_tail;
	/* Immediately followed by an array of struct pva_cmdbuf_submit_info */
};

static inline uint32_t pva_fw_queue_count(uint32_t head, uint32_t tail,
					  uint32_t size)
{
	if (tail >= head) {
		return tail - head;
	} else {
		return sat_sub32(size, head - tail);
	}
}

static inline uint32_t pva_fw_queue_space(uint32_t head, uint32_t tail,
					  uint32_t size)
{
	return safe_subu32(
		safe_subu32(size, pva_fw_queue_count(head, tail, size)), 1u);
}

/* CCQ commands: KMD -> R5, through CCQ FIFO */

/*
 * CCQ commands are meant to be used at init time.
 */
#define PVA_FW_CCQ_OPCODE_MSB 63
#define PVA_FW_CCQ_OPCODE_LSB 60

/*
 * resource table IOVA addr bit field: 39 - 0
 * resource table number of entries bit field: 59 - 40
 */
#define PVA_FW_CCQ_OP_SET_RESOURCE_TABLE 1
#define PVA_FW_CCQ_RESOURCE_TABLE_ADDR_MSB 39
#define PVA_FW_CCQ_RESOURCE_TABLE_ADDR_LSB 0
#define PVA_FW_CCQ_RESOURCE_TABLE_N_ENTRIES_MSB 59
#define PVA_FW_CCQ_RESOURCE_TABLE_N_ENTRIES_LSB 40

/*
 * submission queue IOVA addr bit field: 39 - 0
 * submission queue number of entries bit field: 59 - 40
 */
#define PVA_FW_CCQ_OP_SET_SUBMISSION_QUEUE 2
#define PVA_FW_CCQ_QUEUE_ADDR_MSB 39
#define PVA_FW_CCQ_QUEUE_ADDR_LSB 0
#define PVA_FW_CCQ_QUEUE_N_ENTRIES_MSB 59
#define PVA_FW_CCQ_QUEUE_N_ENTRIES_LSB 40

/* KMD and FW communicate using messages.
 *
 * Message can contain up to 6 uint32_t.
 *
 * The first uint32_t is the header that contains message type and length.
 */
#define PVA_FW_MSG_MAX_LEN 6

/* KMD send messages to R5 using CCQ FIFO. The message length is always 64 bit. */

/* When R5 send messages to KMD using CCQ statuses, we use status 3 - 8
 *
 * msg[0] = STATUS8 -> generate interrupt to KMD
 * msg[1] = STATUS3
 * msg[2] = STATUS4
 * msg[3] = STATUS5
 * msg[4] = STATUS6
 * msg[5] = STATUS7
 */
#define PVA_FW_MSG_STATUS_BASE 3
#define PVA_FW_MSG_STATUS_LAST 8

#define PVA_FW_MSG_TYPE_MSB 30
#define PVA_FW_MSG_TYPE_LSB 25
#define PVA_FW_MSG_LEN_MSB 24
#define PVA_FW_MSG_LEN_LSB 22
/* The remaining bits (0 - 21) of msg[0] can be used for message specific
 * payload */

/* Message types: R5 -> CCPLEX */
#define PVA_FW_MSG_TYPE_ABORT 1
#define PVA_FW_MSG_TYPE_BOOT_DONE 2
#define PVA_FW_MSG_TYPE_FLUSH_PRINT 3
#define PVA_FW_MSG_TYPE_RESOURCE_UNREGISTER 3

/* Message types: CCPLEX -> R5 */
#define PVA_FW_MSG_TYPE_UPDATE_TAIL 32

/* Parameters for message ABORT
 * ABORT message contains a short string (up to 22 chars).
 * The first two charactors are in the message header (bit 15 - 0).
 */
#define PVA_FW_MSG_ABORT_STR_MAX_LEN 22

/* Parameters for message BOOT_DONE */
#define PVA_FW_MSG_R5_START_TIME_LO_IDX 1
#define PVA_FW_MSG_R5_START_TIME_HI_IDX 2
#define PVA_FW_MSG_R5_READY_TIME_LO_IDX 3
#define PVA_FW_MSG_R5_READY_TIME_HI_IDX 4

#define PVA_MAX_DEBUG_LOG_MSG_CHARACTERS 100
/* Parameters for message FLUSH PRINT */
struct pva_fw_print_buffer_header {
#define PVA_FW_PRINT_BUFFER_OVERFLOWED (1 << 0)
#define PVA_FW_PRINT_FAILURE (1 << 1)
	uint32_t flags;
	uint32_t head;
	uint32_t tail;
	uint32_t size;
	/* Followed by print content */
};

/* Parameters for message resource unregister */
/* Table ID is stored in msg[0], bit: 0 - 7 */
#define PVA_FW_MSG_RESOURCE_TABLE_ID_MSB 7
#define PVA_FW_MSG_RESOURCE_TABLE_ID_LSB 0
/* Followed by up to 5 resource IDs. The actual number of resource ID is
 * indicated by the message length. */

/** @brief Circular buffer based data channel to share data between R5 and CCPLEX */
struct pva_data_channel {
	uint32_t size;
#define PVA_DATA_CHANNEL_OVERFLOW (1U << 0U)
	uint32_t flags;
	uint32_t head;
	/**
	 * Offset location in the circular buffer where from VPU printf data will be written by FW
	 */
	uint32_t tail;
	/* Immediately followed by circular buffer data */
};

/* PVA FW Event profiling definitions */

// Event identifiers
#define PVA_FW_EVENT_DO_CMD PVA_BIT8(1)
#define PVA_FW_EVENT_SCAN_QUEUES PVA_BIT8(2)
#define PVA_FW_EVENT_SCAN_SLOTS PVA_BIT8(3)
#define PVA_FW_EVENT_RUN_VPU PVA_BIT8(4)

// Event message format
struct pva_fw_event_message {
	uint32_t event : 5;
	uint32_t type : 3;
	uint32_t arg1 : 8;
	uint32_t arg2 : 8;
	uint32_t arg3 : 8;
};

// Each event is one of the following types. This should fit within 3 bits
enum pva_fw_events_type {
	EVENT_TRY = 0U,
	EVENT_START,
	EVENT_YIELD,
	EVENT_DONE,
	EVENT_ERROR,
	EVENT_TYPE_MAX = 7U
};

static inline const char *event_type_to_string(enum pva_fw_events_type status)
{
	switch (status) {
	case EVENT_TRY:
		return "TRY";
	case EVENT_START:
		return "START";
	case EVENT_YIELD:
		return "YIELD";
	case EVENT_DONE:
		return "DONE";
	case EVENT_ERROR:
		return "ERROR";
	default:
		return "";
	}
}

enum pva_fw_timestamp_t {
	TIMESTAMP_TYPE_TSE = 0,
	TIMESTAMP_TYPE_CYCLE_COUNT = 1
};
/* End of PVA FW Event profiling definitions */

/*
 * The buffers shared between KMD and FW may contain a mixture of different
 * types of messages. Each message type may have a different packing and size.
 * However, to keep processing of messages simple and efficient, we will
 * enforce enqueuing and dequeuing of fixed size messages only. The size of
 * each element in the buffer would be equal to the size of the largest possible
 * message. KMD can further parse these messages to extract the exact size of the
 * message.
 */
#define PVA_KMD_FW_BUF_ELEMENT_SIZE sizeof(struct pva_kmd_fw_msg_vpu_trace)

// TODO: remove element size and buffer size fields from this struct.
//	 This struct is shared between KMD and FW. FW should not be able to change
//	 buffer size properties as KMD might use this for validation of buffer accesses.
//	 If FW somehow corrupts 'size', KMD might end up accessing out of bounds.
struct pva_fw_shared_buffer_header {
#define PVA_KMD_FW_BUF_FLAG_OVERFLOW (1 << 0)
#define PVA_KMD_FW_BUF_FLAG_ERROR (1 << 1)
	uint32_t flags;
	uint32_t element_size;
	uint32_t head;
	uint32_t tail;
};

struct pva_kmd_fw_buffer_msg_header {
#define PVA_KMD_FW_BUF_MSG_TYPE_FW_EVENT 0
#define PVA_KMD_FW_BUF_MSG_TYPE_VPU_TRACE 1
#define PVA_KMD_FW_BUF_MSG_TYPE_FENCE_TRACE 2
#define PVA_KMD_FW_BUF_MSG_TYPE_RES_UNREG 3
#define PVA_KMD_FW_BUF_MSG_TYPE_FW_TRACEPOINT 4
	uint32_t type : 8;
	// Size of payload in bytes. Includes the size of the header.
	uint32_t size : 24;
};

// Tracing information for NSIGHT
struct pva_kmd_fw_msg_vpu_trace {
	// VPU ID on which the job was executed
	uint8_t engine_id;
	// CCQ ID through which the job was submitted
	uint8_t ccq_id;
	// Queue ID through which the job was submitted
	// This is not relative to a context. It ranges from 0 to 55
	uint8_t queue_id;
	// Number of prefences in the cmdbuf
	uint8_t num_prefences;
	// Program ID of the VPU program executed.
	// Not supported today as CUPVA does not fully support this yet.
	// The intent is to for user applications to be able to assign
	// an identification to a VPU kernel. This ID will then be forwarded
	// by the FW to the KMD for tracing.
	uint64_t prog_id;
	// Start time of the VPU execution
	uint64_t vpu_start_time;
	// End time of the VPU execution
	uint64_t vpu_end_time;
	// Submit ID of the cmdbuf
	// User applications can assign distinct identifiers to command buffers.
	// FW will forward this identifier to the KMD for tracing.
	uint64_t submit_id;
};

struct pva_kmd_fw_msg_fence_trace {
	uint64_t submit_id;
	uint64_t timestamp;
	// For syncpt fences, fence_id is the syncpt index
	// For semaphore fences, fence_id is the serial ID of the semaphore NvRM memory
	uint64_t fence_id;
	// 'offset' is the offset into the semaphore memory where the value is stored
	// This is only valid for semaphore fences
	uint64_t offset;
	uint32_t value;
	uint8_t ccq_id;
	uint8_t queue_id;
#define PVA_KMD_FW_BUF_MSG_FENCE_ACTION_WAIT 0U
#define PVA_KMD_FW_BUF_MSG_FENCE_ACTION_SIGNAL 1U
	uint8_t action;
#define PVA_KMD_FW_BUF_MSG_FENCE_TYPE_SYNCPT 0U
#define PVA_KMD_FW_BUF_MSG_FENCE_TYPE_SEMAPHORE 1U
	uint8_t type;
};

// Resource unregister message
struct pva_kmd_fw_msg_res_unreg {
	uint32_t resource_id;
};

struct pva_kmd_fw_tegrastats {
	uint64_t window_start_time;
	uint64_t window_end_time;
	uint64_t total_utilization[PVA_NUM_PVE];
};

#define PVA_MAX_CMDBUF_CHUNK_LEN 1024
#define PVA_MAX_CMDBUF_CHUNK_SIZE (sizeof(uint32_t) * PVA_MAX_CMDBUF_CHUNK_LEN)

#define PVA_TEST_MODE_MAX_CMDBUF_CHUNK_LEN 256
#define PVA_TEST_MODE_MAX_CMDBUF_CHUNK_SIZE                                    \
	(sizeof(uint32_t) * PVA_TEST_MODE_MAX_CMDBUF_CHUNK_LEN)

#define PVA_FW_TP_LVL_NONE 0U
#define PVA_FW_TP_LVL_CMD_BUF PVA_BIT8(0)
#define PVA_FW_TP_LVL_VPU PVA_BIT8(1)
#define PVA_FW_TP_LVL_DMA PVA_BIT8(2)
#define PVA_FW_TP_LVL_L2SRAM PVA_BIT8(3)
#define PVA_FW_TP_LVL_PPE PVA_BIT8(4)
#define PVA_FW_TP_LVL_ALL                                                      \
	(PVA_FW_TP_LVL_CMD_BUF | PVA_FW_TP_LVL_VPU | PVA_FW_TP_LVL_DMA |       \
	 PVA_FW_TP_LVL_PPE | PVA_FW_TP_LVL_L2SRAM)

/* Tracepoint Flags for PVA */
/** @brief Macro to define flag field for a normal checkpoint*/
#define PVA_FW_TP_FLAG_NONE (0U)
/** @brief Macro to define a checkpoint's flag field to indicate start of an operation */
#define PVA_FW_TP_FLAG_START (1U)
/** @brief Macro to define a checkpoint's flag field to indicate end of an operation */
#define PVA_FW_TP_FLAG_END (2U)
/** @brief Macro to define a checkpoint's flag field to indicate error */
#define PVA_FW_TP_FLAG_ERROR (3U)

struct pva_fw_tracepoint {
	uint32_t type : 3;
	uint32_t flags : 2;
	uint32_t slot_id : 2;
	uint32_t ccq_id : 3;
	uint32_t queue_id : 3;
	uint32_t engine_id : 1;
	uint32_t arg1 : 2;
	uint32_t arg2 : 16;
};

static inline const char *pva_fw_tracepoint_type_to_string(uint32_t type)
{
	switch (type) {
	case PVA_FW_TP_LVL_NONE:
		return "NONE";
	case PVA_FW_TP_LVL_CMD_BUF:
		return "CMD_BUF";
	case PVA_FW_TP_LVL_VPU:
		return "VPU";
	case PVA_FW_TP_LVL_DMA:
		return "DMA";
	case PVA_FW_TP_LVL_L2SRAM:
		return "L2SRAM";
	case PVA_FW_TP_LVL_PPE:
		return "PPE";
	default:
		return "UNKNOWN";
	}
}

static inline const char *pva_fw_tracepoint_flags_to_string(uint32_t flags)
{
	switch (flags) {
	case PVA_FW_TP_FLAG_NONE:
		return "NONE";
	case PVA_FW_TP_FLAG_START:
		return "START";
	case PVA_FW_TP_FLAG_END:
		return "END";
	case PVA_FW_TP_FLAG_ERROR:
		return "ERROR";
	default:
		return "UNKNOWN";
	}
}

static inline const char *pva_fw_tracepoint_slot_id_to_string(uint32_t slot_id)
{
	switch (slot_id) {
	case 0:
		return "PRIV_SLOT";
	case 1:
		return "USER_SLOT_1";
	case 2:
		return "USER_SLOT_2";
	case 3:
		return "USER_PRIV_SLOT";
	default:
		return "UNKNOWN";
	}
}

#define PVA_R5_OCD_TYPE_MMIO_READ 1
#define PVA_R5_OCD_TYPE_MMIO_WRITE 2
#define PVA_R5_OCD_TYPE_REG_READ 3
#define PVA_R5_OCD_TYPE_REG_WRITE 4

#define PVA_R5_OCD_MAX_DATA_SIZE FW_TRACE_BUFFER_SIZE

struct pva_r5_ocd_request {
	uint32_t type;
	uint32_t addr;
	uint32_t size;
	//followed by data if any
};

#endif // PVA_FW_H
