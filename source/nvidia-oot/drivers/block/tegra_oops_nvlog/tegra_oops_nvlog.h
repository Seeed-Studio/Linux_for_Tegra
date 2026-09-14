/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */
#ifndef TEGRA_NVLOG_OOPS_H
#define TEGRA_NVLOG_OOPS_H
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/pstore_zone.h>
#include <soc/tegra/virt/hv-ivc.h>

struct device;

/*
 * ===== BINARY PROTOCOL CONFIGURATION =====
 * NvLog Binary Dump Protocol - RAW protocol definitions for tegra_oops_nvlog driver
 *
 * This section contains the minimal definitions required for implementing the NvLog
 * binary dump RAW protocol for kernel crash dumps.
 *
 * The driver:
 * - Writes raw binary data directly to the circular buffer
 * - Updates metadata indices (READ_OFFSET, WRITE_OFFSET, etc.)
 * - Sets END_TAG when dump is complete
 *
 * The NvLog Server then reads this raw data and wraps it in appropriate message formats.
 */

/* Magic number signature for binary dump validation */
#define SIGNATURE						0xDEADBEEFDEADBEEF

/* Index for data start offset in memory pool */
#define DATA_START_OFFSET_IDX			(1U)

/* Index for read offset (consumer position) */
#define READ_OFFSET_IDX					(2U)

/* Index for write offset (producer position) */
#define WRITE_OFFSET_IDX				(3U)

/* Index for total bytes written counter */
#define TOTAL_BYTES_WRITTEN_IDX			(5U)

/* Index for end tag flag (1 when dump complete) */
#define END_TAG_IDX						(7U)

/* Index for payload start offset */
#define PAYLOAD_START_OFFSET_IDX		(8U)

/* Memory alignment requirements */
#define NVLOG_POINTER_ALIGNMENT			sizeof(void *)
#define NVLOG_QWORD_ALIGNMENT			8U
#define NVLOG_PAGE_SIZE					4096U

/* Error codes */
#define NVLOG_ERR_INVALID_PARAMS		(-EINVAL)
#define NVLOG_ERR_DEVICE_NOT_READY		(-ENODEV)
#define NVLOG_ERR_MEMORY_ALLOC			(-ENOMEM)
#define NVLOG_ERR_MEMORY_ALIGNMENT		(-EFAULT)
#define NVLOG_ERR_BUFFER_FULL			(-ENOSPC)
#define NVLOG_ERR_INIT					(-EIO)
#define NVLOG_ERR_HYPERVISOR			(-EACCES)

/* Driver configuration */
#define NVLOG_OOPS_DRV_NAME				"tegra_oops_nvlog"
#define NVLOG_PSTORE_KMSG_RECORD_SIZE	(64 * 1024)
#define NVLOG_DEFAULT_MEMPOOL_SIZE		(1024 * 1024)

/*
 * ===== CRASH DUMP STORAGE CONFIGURATION =====
 * Pre-allocated hypervisor mempool that survives kernel panics
 */

/**
 * struct nvlog_mempool - NvLog crash dump storage structure
 * @mempool_id: Hypervisor mempool ID
 * @base_addr: Mapped memory base address
 * @size: Total mempool size in bytes
 * @ivmk: Hypervisor memory cookie
 * @initialized: Mempool initialization status
 * @lock: Access synchronization mutex
 * @metadata: Metadata array base address
 * @data_start_offset: Offset where data region begins
 * @data_size: Size of data region
 * @read_offset_ptr: Pointer to read offset in metadata
 * @write_offset_ptr: Pointer to write offset in metadata
 * @binary_initialized: Binary protocol initialization status
 *
 * Manages pre-allocated hypervisor memory for crash data
 */
struct nvlog_mempool {
	u32 mempool_id;
	void *base_addr;
	u32 size;
	struct tegra_hv_ivm_cookie *ivmk;
	bool initialized;
	struct mutex lock;

	/* Binary protocol fields - volatile required for hypervisor shared memory */
	volatile u64 *metadata;
	u64 data_start_offset;
	u64 data_size;
	volatile u64 *read_offset_ptr;
	volatile u64 *write_offset_ptr;
	bool binary_initialized;
};

/*
 * ===== DRIVER DATA STRUCTURES =====
 */

/**
 * struct nvlog_oops_dev - Crash dump driver device structure
 * @device: Platform device pointer
 * @mempool: Shared memory management
 * @entity_id: Entity ID for kernel panic dumps
 * @pstore_kmsg_size: Max pstore record size for kmsg
 * @pstore_max_reason: Max panic reason to capture
 * @initialized: Driver initialization status
 * @pstore_zone: Pstore zone configuration structure
 *
 * Driver state for kernel panic/oops capture via pstore
 */
struct nvlog_oops_dev {
	struct device *device;
	struct nvlog_mempool mempool;
	u32 entity_id;
	u32 pstore_kmsg_size;
	int pstore_max_reason;
	bool initialized;
	struct pstore_zone_info pstore_zone;
};

/*
 * ===== BINARY PROTOCOL MEMORY LAYOUT =====
 *
 * Memory Pool Layout for Binary Dump Protocol:
 *
 * The binary dump protocol uses a circular buffer with the following layout:
 *
 * Index 0: SIGNATURE (0xDEADBEEFDEADBEEF) - Magic number for validation
 * Index 1: DATA_START_OFFSET - Offset where actual data begins
 * Index 2: READ_OFFSET - Current read position (consumer)
 * Index 3: WRITE_OFFSET - Current write position (producer)
 * Index 4: [Reserved]
 * Index 5: TOTAL_BYTES_WRITTEN - Total bytes written by producer
 * Index 6: [Reserved]
 * Index 7: END_TAG - Flag indicating dump completion (0 or 1)
 * Index 8+: Actual dump data begins here
 *
 * All indices are u64 offsets. The producer (kernel) writes data and
 * updates WRITE_OFFSET. The consumer (NvLog server) reads data and updates
 * READ_OFFSET. Memory barriers (dmb sy) ensure coherency between producer
 * and consumer.
 *
 * Data Flow:
 * 1. Producer checks signature for validation
 * 2. Producer writes raw binary data to circular buffer
 * 3. Producer updates WRITE_OFFSET with memory barriers
 * 4. Producer sets END_TAG when complete
 * 5. Consumer reads raw data between READ_OFFSET and WRITE_OFFSET
 * 6. Consumer processes raw data and updates READ_OFFSET
 */

#endif /* TEGRA_NVLOG_OOPS_H */
