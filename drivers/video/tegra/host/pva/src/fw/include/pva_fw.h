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
		return safe_subu32(tail, head);
	} else {
		return safe_addu32(safe_subu32(size, head), tail);
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
 * Most CCQ commands are meant to be used at init time.
 * During runtime, only use PVA_FW_CCQ_OP_UPDATE_TAIL
 */
#define PVA_FW_CCQ_OPCODE_MSB 63
#define PVA_FW_CCQ_OPCODE_LSB 60

/*
 * tail value bit field: 31 - 0
 * queue id bit field: 40 - 32
 */
#define PVA_FW_CCQ_OP_UPDATE_TAIL 0
#define PVA_FW_CCQ_TAIL_MSB 31
#define PVA_FW_CCQ_TAIL_LSB 0
#define PVA_FW_CCQ_QUEUE_ID_MSB 40
#define PVA_FW_CCQ_QUEUE_ID_LSB 32

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
#define PVA_KMD_FW_BUF_ELEMENT_SIZE (sizeof(uint32_t) + sizeof(uint64_t))

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

struct pva_kmd_fw_tegrastats {
	uint64_t window_start_time;
	uint64_t window_end_time;
	uint64_t total_utilization[PVA_NUM_PVE];
};

#endif // PVA_FW_H
