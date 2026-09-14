/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */
#include <nvidia/conftest.h>

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/kmsg_dump.h>
#include <linux/pstore_zone.h>
#include <linux/time.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/stdarg.h>
#include <soc/tegra/fuse.h>

#include <linux/atomic.h>
#include <linux/compiler.h>
#include <asm/barrier.h>
#include <soc/tegra/virt/hv-ivc.h>

#include "tegra_oops_nvlog.h"

/* Global device instance */
static struct nvlog_oops_dev *nvlog_oops_device;

/*
 * ===== BINARY PROTOCOL IMPLEMENTATION =====
 */

/*
 * ===== BINARY PROTOCOL IMPLEMENTATION =====
 *
 * This implementation follows the NvLog binary dump protocol
 * for crash data storage in pre-allocated hypervisor memory.
 *
 * Key Protocol Requirements:
 * 1. METADATA: 8 x u64 array with specific indices
 * 2. CIRCULAR BUFFER: Forward writing with wraparound
 * 3. SYNCHRONIZATION: Read/write offset updates with barriers
 * 4. COMPLETION: End tag flag when dump is complete
 */

/*
 * ===== ATOMIC OPERATIONS AND MEMORY BARRIERS =====
 */

/*
 * ===== ATOMIC OPERATIONS AND MEMORY BARRIERS =====
 * Core primitives for binary protocol synchronization
 */

/**
 * arm64_shared_memory_barriers - ARM64 memory barriers for shared memory operations
 *
 * Provides full system memory barriers for all shared memory operations.
 * Uses the strongest barriers (dsb(sy) + mb()) because:
 * 1. Crash data must be visible to hypervisor and external observers
 * 2. Panic context has unknown system state - safest to use full barriers
 * 3. Performance difference between dsb(sy) and dsb(ish) is minimal on modern ARM64
 * 4. Simplicity - eliminates conditional logic and potential barrier bugs
 *
 * Context: Interrupt handler, signal handler, thread-safe, sync, re-entrant
 * Can be used in init, runtime, and de-init phases
 */
static inline void arm64_shared_memory_barriers(void)
{
#ifdef CONFIG_ARM64
	dsb(sy);    /* Full system data synchronization barrier */
	isb();      /* Instruction synchronization barrier */
#endif
	mb();       /* Full memory barrier for all architectures */
}


/**
 * read64 - Atomic 64-bit read operation
 *
 * Performs atomic read of 64-bit value from shared memory using READ_ONCE
 * to prevent compiler optimizations and ensure single memory access.
 *
 * Return: Current value read atomically
 *
 * Context: Interrupt handler, signal handler, thread-safe, sync, re-entrant
 * Can be used in init, runtime, and de-init phases
 *
 * Note: volatile required for shared memory coherency with hypervisor
 */
static inline u64 read64(volatile u64 const *addr)
{
	return READ_ONCE(*addr);
}

/**
 * write64 - Atomic 64-bit write operation
 *
 * Performs atomic write of 64-bit value to shared memory using WRITE_ONCE
 * to prevent compiler optimizations and ensure single memory access.
 *
 * Context: Interrupt handler, signal handler, thread-safe, sync, re-entrant
 * Can be used in init, runtime, and de-init phases
 *
 * Note: volatile required for shared memory coherency with hypervisor
 */
static inline void write64(volatile u64 *addr, u64 value)
{
	WRITE_ONCE(*addr, value);
}


/*
 * ===== ARM64 SAFETY UTILITIES =====
 * Centralized ARM64-safe operations for panic context
 */

/**
 * arm64_safe_strncmp - ARM64-safe string comparison for panic context
 *
 * Performs byte-by-byte string comparison safe for panic context.
 * Standard strncmp() may use optimized assembly with alignment assumptions
 * that can fault in panic context. This implementation uses volatile access
 * to prevent compiler optimizations and ensure safe memory access.
 *
 * Return: 0 if equal, non-zero if different
 */
static inline int arm64_safe_strncmp(const char *s1, const char *s2, size_t n)
{
	size_t i;
	/* volatile required for safe memory access in panic context */
	volatile u8 c1, c2;

	for (i = 0; i < n; i++) {
		/* Use volatile access to prevent optimization */
		c1 = s1[i];
		c2 = s2[i];

		if (c1 != c2)
			return (int)c1 - (int)c2;

		/* Stop at null terminator */
		if (c1 == '\0')
			break;
	}

	return 0;
}

/**
 * arm64_safe_contains - ARM64-safe substring search for panic context
 *
 * Performs substring search safe for panic context using byte-by-byte access.
 * Standard strstr() may use SIMD instructions or optimized memory access
 * patterns that can fault on unaligned data in panic context. This
 * implementation uses volatile access with bounds checking.
 *
 * Return: true if needle found in haystack, false otherwise
 */
static inline bool arm64_safe_contains(const char *haystack, const char *needle, size_t haystack_len)
{
	size_t needle_len;
	size_t i, j;
	/* volatile required for safe memory access in panic context */
	volatile u8 h_char, n_char;
	bool match;

	if (!haystack || !needle)
		return false;

	/* Calculate needle length safely */
	needle_len = 0;
	while (needle_len < 32 && needle[needle_len] != '\0') {  /* Limit search to reasonable length */
		needle_len++;
	}

	if (needle_len == 0 || needle_len > haystack_len)
		return false;

	/* Search for needle in haystack */
	for (i = 0; i <= haystack_len - needle_len; i++) {
		match = true;

		/* Compare characters byte by byte */
		for (j = 0; j < needle_len; j++) {
			h_char = haystack[i + j];
			n_char = needle[j];

			if (h_char != n_char) {
				match = false;
				break;
			}
		}

		if (match)
			return true;

		/* Stop at null terminator */
		if (haystack[i] == '\0')
			break;
	}

	return false;
}

/**
 * ARM64-safe byte-by-byte copy operation -
 *
 * Performs memory copy safe for panic context using byte-by-byte access.
 * Standard memcpy() may use optimized assembly with alignment assumptions
 * that can fault in panic context. This implementation uses volatile access
 * and periodic memory barriers for hypervisor visibility.
 *
 * Return: None
 * * *   - Async/Sync: Sync
 *   - Re-entrant: No
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
static inline void arm64_safe_copy_bytes(void *dest, const void *src, size_t len)
{
	u8 *dest_ptr = (u8 *)dest;
	const u8 *src_ptr = (const u8 *)src;
	size_t i;
	/* volatile required for safe memory access in panic context */
	volatile u8 src_byte;

	for (i = 0; i < len; i++) {
		src_byte = src_ptr[i];
		dest_ptr[i] = src_byte;

		/* Memory barrier every cache line for hypervisor visibility */
		if ((i % 64) == 63)
			arm64_shared_memory_barriers();
	}
}

/**
 * Check if address is qword-aligned (8-byte boundary) -
 *
 * Validates that an address is properly aligned to 8-byte boundary
 * required for atomic operations on 64-bit values in shared memory.
 *
 * Return: true if aligned, false if not
 * * *   - Async/Sync: Sync
 *   - Re-entrant: Yes
 * - API Group
 *   - Init: Yes
 *   - Runtime: Yes
 *   - De-Init: Yes
 */
static inline bool is_word_aligned(uintptr_t addr)
{
	return (addr % NVLOG_QWORD_ALIGNMENT) == 0;
}


/**
 * Standardized error logging with context information -
 *
 * Provides centralized error logging with standardized error codes and
 * human-readable descriptions. Maps internal error codes to descriptive
 * strings for better debugging and troubleshooting.
 *
 * Return: None
 * * *   - Async/Sync: Sync
 *   - Re-entrant: Yes
 * - API Group
 *   - Init: Yes
 *   - Runtime: Yes
 *   - De-Init: Yes
 */
static inline void nvlog_log_error(const char *func_name, int error_code, const char *fmt, ...)
{
	const char *error_type;
	char details[256];
	va_list args;

	/* Format the detail message */
	va_start(args, fmt);
	vsnprintf(details, sizeof(details), fmt, args);
	va_end(args);

	/* Map error codes to human-readable descriptions */
	switch (error_code) {
	case NVLOG_ERR_INVALID_PARAMS:
		error_type = "INVALID_PARAMS";
		break;
	case NVLOG_ERR_DEVICE_NOT_READY:
		error_type = "DEVICE_NOT_READY";
		break;
	case NVLOG_ERR_MEMORY_ALLOC:
		error_type = "MEMORY_ALLOC";
		break;
	case NVLOG_ERR_MEMORY_ALIGNMENT:
		error_type = "MEMORY_ALIGNMENT";
		break;
	case NVLOG_ERR_BUFFER_FULL:
		error_type = "BUFFER_FULL";
		break;
	case NVLOG_ERR_HYPERVISOR:
		error_type = "HYPERVISOR";
		break;
	default:
		error_type = "UNKNOWN_ERROR";
		break;
	}

	pr_err("tegra_nvlog_oops: ERROR in %s: %s (code=%d) - %s\n",
		func_name, error_type, error_code, details);
}

/**
 * Validate device state for panic operations -
 *
 * Performs comprehensive validation of device state before panic operations.
 * Ensures device is properly initialized and mempool is ready for crash data
 * storage. Returns specific error codes to enable appropriate recovery strategies.
 *
 * Return: 0 on success, negative error code on failure
 * * *   - Async/Sync: Sync
 *   - Re-entrant: No
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
static int validate_panic_device_state(struct nvlog_oops_dev *dev)
{
	if (!dev) {
		pr_info("[NVLOG] validate_device: ERROR - device is NULL\n");
		nvlog_log_error("validate_panic_device_state", NVLOG_ERR_DEVICE_NOT_READY,
					"Device structure is NULL");
		return NVLOG_ERR_DEVICE_NOT_READY;
	}

	if (!dev->mempool.initialized || !dev->mempool.binary_initialized) {
		pr_info("[NVLOG] validate_device: ERROR - mempool not initialized\n");
		nvlog_log_error("validate_panic_device_state", NVLOG_ERR_DEVICE_NOT_READY,
					"Device mempool not initialized (mempool=%d, binary=%d)",
					dev->mempool.initialized, dev->mempool.binary_initialized);
		return NVLOG_ERR_DEVICE_NOT_READY;
	}

	if (!dev->mempool.base_addr) {
		pr_info("[NVLOG] validate_device: ERROR - base_addr is NULL\n");
		nvlog_log_error("validate_panic_device_state", NVLOG_ERR_DEVICE_NOT_READY,
					"Mempool base address is NULL");
		return NVLOG_ERR_DEVICE_NOT_READY;
	}

	pr_info("[NVLOG] validate_device: SUCCESS\n");
	return 0;
}

/**
 * Initialize binary protocol for crash data storage -
 *
 * Initializes the NvLog binary dump protocol in the provided memory pool.
 * Sets up metadata structure, validates alignment requirements, and configures
 * circular buffer parameters for crash data storage.
 *
 * Return: 0 on success, negative error code on failure
 * * *   - Async/Sync: Sync
 *   - Re-entrant: No
 * - API Group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
static int nvlog_binary_init(struct nvlog_mempool *mempool)
{
	/* volatile required for shared memory coherency with hypervisor */
	volatile u64 *metadata;
	u64 mempool_size;
	u64 data_start_offset;

	if (!mempool || !mempool->base_addr) {
		pr_info("[NVLOG] binary_init: ERROR - NULL mempool or base\n");
		nvlog_log_error("nvlog_binary_init", NVLOG_ERR_INVALID_PARAMS,
						"NULL mempool or base address");
		return NVLOG_ERR_INVALID_PARAMS;
	}

	/*
	 * Validate minimum size for metadata + data region.
	 * Must be at least one page to accommodate metadata and some data.
	 */
	mempool_size = mempool->size;
	if (mempool_size < NVLOG_PAGE_SIZE) {
		nvlog_log_error("nvlog_binary_init", NVLOG_ERR_INVALID_PARAMS,
						"Mempool size %llu too small (minimum %u bytes)",
						mempool_size, NVLOG_PAGE_SIZE);
		return NVLOG_ERR_INVALID_PARAMS;
	}

	/*
	 * Validate metadata base address alignment to 8-byte boundary
	 * required for atomic operations on 64-bit metadata values.
	 */
	if (!is_word_aligned((uintptr_t)mempool->base_addr)) {
		nvlog_log_error("nvlog_binary_init", NVLOG_ERR_MEMORY_ALIGNMENT,
						"Mempool base address 0x%pK not aligned to %u-byte boundary",
						mempool->base_addr, NVLOG_QWORD_ALIGNMENT);
		return NVLOG_ERR_MEMORY_ALIGNMENT;
	}

	/*
	 * Setup metadata pointers to access the 8-element u64 array
	 * at the beginning of the mempool for protocol synchronization.
	 */
	metadata = (volatile u64 *)mempool->base_addr;
	mempool->metadata = metadata;

	/*
	 * Calculate data region start offset after the metadata array.
	 * Data begins at index 8 (PAYLOAD_START_OFFSET_IDX) in the u64 array.
	 */
	data_start_offset = PAYLOAD_START_OFFSET_IDX * sizeof(u64);
	if (data_start_offset >= mempool_size) {
		nvlog_log_error("nvlog_binary_init", NVLOG_ERR_INVALID_PARAMS,
						"Metadata region too large for mempool");
		return NVLOG_ERR_INVALID_PARAMS;
	}

	/*
	 * Initialize metadata indices with protocol-specific values.
	 * These values establish the initial state of the binary protocol.
	 */
	metadata[0]                         = SIGNATURE;          /* Magic signature for validation */
	metadata[DATA_START_OFFSET_IDX]     = data_start_offset; /* Where data region begins */
	metadata[READ_OFFSET_IDX]           = SIGNATURE;          /* Initial state - no data read yet */
	metadata[WRITE_OFFSET_IDX]          = data_start_offset;  /* Start writing at data region */
	metadata[TOTAL_BYTES_WRITTEN_IDX]   = 0;                  /* No data written yet */
	metadata[END_TAG_IDX]               = 0;                  /* Not at end yet */

	/*
	 * Setup mempool binary protocol fields for efficient access
	 * to data region and offset pointers during runtime.
	 */
	mempool->data_start_offset  = data_start_offset;
	mempool->data_size          = mempool_size - data_start_offset;
	mempool->read_offset_ptr    = &metadata[READ_OFFSET_IDX];
	mempool->write_offset_ptr   = &metadata[WRITE_OFFSET_IDX];

	/*
	 * Validate that we have a non-zero data region after metadata.
	 * This ensures there is space for actual crash data storage.
	 */
	if (mempool->data_size == 0) {
		nvlog_log_error("nvlog_binary_init", NVLOG_ERR_INVALID_PARAMS,
						"No space for data region after metadata (mempool_size=%llu, data_start=%llu)",
						mempool_size, data_start_offset);
		return NVLOG_ERR_INVALID_PARAMS;
	}


	/*
	 * Memory barrier to ensure all metadata writes are visible to hypervisor
	 * before marking binary protocol as initialized.
	 */
	arm64_shared_memory_barriers();

	/* Mark binary protocol as initialized and ready for data operations */
	mempool->binary_initialized = true;

	pr_info("[NVLOG] Binary protocol initialized - mempool size=%llu, data_start=%llu, data_size=%llu\n",
			mempool_size, data_start_offset, mempool->data_size);

	return 0;
}

/**
 * Write raw data to circular buffer using binary protocol (panic-safe) -
 *
 * Writes raw binary data directly to the circular buffer without message formatting.
 * All writes are panic-safe since this driver is designed for crash dumps.
 * The NvLog Server will handle message wrapping when processing the raw data.
 *
 * Return: 0 on success, negative error code on failure
 * * *   - Async/Sync: Sync
 *   - Re-entrant: No
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
static int nvlog_binary_write_raw(struct nvlog_mempool *mempool,
								  const void *data, size_t len)
{
	/* volatile required for shared memory coherency with hypervisor */
	volatile u64 *write_offset_ptr;
	volatile u64 *read_offset_ptr;
	volatile u64 *total_written_ptr;
	u64 write_offset;
	u64 read_offset;
	u64 new_write_offset;
	u64 data_start;
	u64 data_end;
	u8 *dest_ptr;
	size_t first_chunk;
	size_t second_chunk;

	/*
	 * Apply full system memory barriers to ensure data visibility to hypervisor.
	 * We use dsb(sy) + isb() because:
	 * 1. Crash data must be visible to external observers
	 * 2. Panic context has unknown system state - safest to use full barriers
	 * 3. Performance difference is minimal on modern ARM64
	 */
	arm64_shared_memory_barriers();

	if (!mempool || !mempool->binary_initialized || !data || len == 0) {
		pr_info("[NVLOG] binary_write_raw: ERROR invalid params mempool=%p init=%d data=%p len=%zu\n",
				mempool, mempool ? mempool->binary_initialized : -1, data, len);
		return NVLOG_ERR_INVALID_PARAMS;
	}

	write_offset_ptr = mempool->write_offset_ptr;
	read_offset_ptr = mempool->read_offset_ptr;
	total_written_ptr = &mempool->metadata[TOTAL_BYTES_WRITTEN_IDX];

	data_start = mempool->data_start_offset;
	data_end = data_start + mempool->data_size;

	/*
	 * Read current offsets with memory barriers to ensure we have the latest
	 * values from the hypervisor and prevent race conditions in panic context.
	 */
	arm64_shared_memory_barriers();

	write_offset = read64(write_offset_ptr);
	read_offset = read64(read_offset_ptr);

	/*
	 * Handle initial read state where read_offset still contains SIGNATURE.
	 * This indicates no data has been read yet, so we initialize read_offset
	 * to the start of the data region.
	 */
	if (read_offset == SIGNATURE) {
		read_offset = data_start;
		write64(read_offset_ptr, data_start);
		arm64_shared_memory_barriers();
	}

	/*
	 * Handle write with potential wrap-around when data exceeds buffer end.
	 * This implements circular buffer behavior for continuous data storage.
	 */
	if (write_offset + len > data_end) {
		/*
		 * Data wraps around - split write into two parts:
		 * 1. First part: from current write_offset to end of buffer
		 * 2. Second part: from start of data region with remaining data
		 */
		first_chunk = data_end - write_offset;
		second_chunk = len - first_chunk;

		/*
		 * Write first part to end of buffer using ARM64-safe copy
		 * to prevent alignment faults in panic context.
		 */
		dest_ptr = (u8 *)mempool->base_addr + write_offset;
		arm64_safe_copy_bytes(dest_ptr, data, first_chunk);

		/*
		 * Write second part to beginning of data region with
		 * offset into source data for the remaining bytes.
		 */
		dest_ptr = (u8 *)mempool->base_addr + data_start;
		arm64_safe_copy_bytes(dest_ptr, (const u8 *)data + first_chunk, second_chunk);

		/*
		 * Update write offset to wrapped position at the start of data region
		 * plus the number of bytes written in the second part.
		 */
		new_write_offset = data_start + second_chunk;
	} else {
		/*
		 * Normal write - no wrap around needed.
		 * Write data directly to current write position.
		 */
		dest_ptr = (u8 *)mempool->base_addr + write_offset;
		arm64_safe_copy_bytes(dest_ptr, data, len);
		new_write_offset = write_offset + len;
	}

	/*
	 * Ensure all data is written and visible to hypervisor before updating
	 * write offset. This prevents partial data visibility issues.
	 */
	arm64_shared_memory_barriers();

	/*
	 * Update write offset and total bytes written atomically.
	 * These updates signal to the hypervisor that new data is available.
	 */
	write64(write_offset_ptr, new_write_offset);
	write64(total_written_ptr, read64(total_written_ptr) + len);

	/*
	 * Final memory barriers to ensure all updates are visible to hypervisor
	 * and external observers before function returns.
	 */
	arm64_shared_memory_barriers();

	return 0;
}

/**
 * Set end tag to mark completion of panic dump -
 *
 * Sets the END_TAG in the metadata to indicate that the panic dump is complete.
 * This should be called after all data has been written to the circular buffer.
 * The hypervisor and NvLog Server use this flag to determine when all crash
 * data has been written and is ready for processing.
 *
 * Return: None
 * * *   - Async/Sync: Sync
 *   - Re-entrant: No
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
static void nvlog_set_end_tag(struct nvlog_mempool *mempool)
{
	/* volatile required for shared memory coherency with hypervisor */
	volatile u64 *end_tag_ptr;

	if (!mempool || !mempool->binary_initialized) {
		pr_err("[NVLOG] set_end_tag: ERROR - mempool not initialized\n");
		return;
	}
	/* Get pointer to end tag in metadata */
	end_tag_ptr = &mempool->metadata[END_TAG_IDX];

	/* Set end tag to 1 */
	write64(end_tag_ptr, 1);
	/* Ensure visibility to hypervisor */
	arm64_shared_memory_barriers();
}


/**
 *
 */

/*
 * ===== PANIC WRITE HELPER FUNCTIONS =====
 * Validation and preparation functions for panic write operations
 */

/**
 * Write raw panic data to mempool using binary protocol -
 *
 * Writes raw binary data directly to the circular buffer using the binary protocol.
 * This function is designed for kernel panic context and handles all necessary
 * synchronization and error checking for crash data storage.
 *
 * Return: Number of bytes written on success, negative error code on failure
 * * *   - Async/Sync: Sync
 *   - Re-entrant: No
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
static ssize_t write_panic_message_to_mempool(struct nvlog_oops_dev *dev,
											const char *data, size_t data_len)
{
	int ret;

	/* Ensure binary protocol is initialized */
	if (!dev->mempool.binary_initialized) {
		pr_info("[NVLOG] write_panic_raw: ERROR - binary protocol not initialized\n");
		return NVLOG_ERR_INIT;
	}

	/* Write raw data directly to circular buffer */
	ret = nvlog_binary_write_raw(&dev->mempool, data, data_len);
	if (ret != 0) {
		pr_info("[NVLOG] write_panic_raw: nvlog_binary_write_raw failed, ret=%d\n", ret);
		return ret;
	}

	pr_info("[NVLOG] write_panic_raw: SUCCESS - wrote %zu bytes\n", data_len);
	return data_len;  /* Return success with original byte count */
}


/**
 *
 */

/*
 * ===== PSTORE INTERFACE FUNCTIONS =====
 * Linux pstore framework integration for kernel panic capture only
 */

/**
 * Pstore read function - returns 0 (write-only driver) -
 *
 * This driver is write-only for kernel panic capture. Reading is handled
 * by userspace components that access the shared memory directly.
 *
 * Return: 0 (no data to read)
 * * *   - Async/Sync: Sync
 *   - Re-entrant: Yes
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
static ssize_t nvlog_oops_pstore_read(char *buf __maybe_unused,
				      size_t bytes __maybe_unused,
				      loff_t pos __maybe_unused)
{
	/* Write-only driver - reading handled by userspace components */
	return 0;
}

/**
 * Pstore panic write function - only called with KMSG data during kernel panic -
 *
 * Main entry point for kernel panic data capture. This function is called by the
 * pstore framework during kernel panic to write KMSG data to the shared memory
 * using the binary protocol. Performs comprehensive validation and error handling.
 *
 * Return: Number of bytes processed on success, negative error code on failure
 * * *   - Async/Sync: Sync
 *   - Re-entrant: No
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
static ssize_t nvlog_oops_pstore_panic_write(const char *buf, size_t bytes, loff_t pos __maybe_unused)
{
	struct nvlog_oops_dev *dev = nvlog_oops_device;
	int validation_result;
	ssize_t result;

	/* Step 1: Entry parameter validation */
	if (!buf || bytes == 0)
		return NVLOG_ERR_INVALID_PARAMS;

	/* Step 2: Device state validation */
	validation_result = validate_panic_device_state(dev);
	if (validation_result != 0) {
		pr_info("[NVLOG] pstore_panic_write: device state validation failed, result=%d\n", validation_result);
		return validation_result;
	}

	/*
	 * Step 3: ARM64 context preparation
	 * Apply memory barriers to ensure proper synchronization in panic context
	 * where system state is unknown and we must be extra careful.
	 */
	arm64_shared_memory_barriers();

	/* Step 4: Write KMSG data to mempool using binary protocol */
	result = write_panic_message_to_mempool(dev, buf, bytes);

	/* Step 5: Mark end of dump if successful */
	if (result > 0) {
		/* Set end tag to mark dump complete */
		nvlog_set_end_tag(&dev->mempool);

		/*
		 * Final barriers for hypervisor visibility to ensure all crash data
		 * is properly written and visible before marking dump as complete.
		 */
		arm64_shared_memory_barriers();

		pr_info("tegra_nvlog_oops: Successfully wrote %zd bytes of KMSG panic data\n", result);
	}

	return result;
}


/**
 *
 */

/*
 * ===== DEVICE MANAGEMENT FUNCTIONS =====
 * Platform driver initialization, configuration, and lifecycle management
 */

/**
 * Initialize nvlog mempool with binary protocol -
 *
 * Initializes the shared memory pool from hypervisor and sets up the binary
 * protocol for crash data storage. Maps memory with write-through caching
 * and validates alignment requirements.
 *
 * Return: 0 on success, negative error code on failure
 * * *   - Async/Sync: Sync
 *   - Re-entrant: No
 * - API Group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
static int nvlog_oops_init_mempool(struct nvlog_oops_dev *dev)
{
	struct nvlog_mempool *mempool = &dev->mempool;
	int ret = 0;

	/*
	 * Reserve shared memory from hypervisor using the mempool ID from device tree.
	 * This memory is pre-allocated by the hypervisor and survives kernel panics.
	 */
	mempool->ivmk = tegra_hv_mempool_reserve(mempool->mempool_id);
	if (IS_ERR_OR_NULL(mempool->ivmk)) {
		nvlog_log_error("nvlog_oops_init_mempool", NVLOG_ERR_HYPERVISOR,
					"Failed to reserve mempool ID %u from hypervisor",
					mempool->mempool_id);
		ret = NVLOG_ERR_HYPERVISOR;
		goto out;
	}

	/*
	 * Map shared memory with write-through caching to ensure data is immediately
	 * visible to hypervisor without requiring explicit cache flushes.
	 */
	mempool->base_addr = devm_memremap(dev->device, mempool->ivmk->ipa,
									mempool->ivmk->size, MEMREMAP_WT);
	if (IS_ERR_OR_NULL(mempool->base_addr)) {
		nvlog_log_error("nvlog_oops_init_mempool", NVLOG_ERR_MEMORY_ALLOC,
					"Failed to map mempool memory at IPA 0x%llx size %zu",
					mempool->ivmk->ipa, mempool->ivmk->size);
		ret = NVLOG_ERR_MEMORY_ALLOC;
		goto cleanup_reserve;
	}

	/* Store actual mempool size for validation and calculations */
	mempool->size = mempool->ivmk->size;

	/*
	 * Validate mempool base address alignment to 8-byte boundary required
	 * for atomic operations on 64-bit values in shared memory.
	 */
	if (!is_word_aligned((uintptr_t)mempool->base_addr)) {
		nvlog_log_error("nvlog_oops_init_mempool", NVLOG_ERR_MEMORY_ALIGNMENT,
					"Mempool base address 0x%pK not properly aligned to %u-byte boundary",
					mempool->base_addr, NVLOG_QWORD_ALIGNMENT);
		ret = NVLOG_ERR_MEMORY_ALIGNMENT;
		goto cleanup_reserve;
	}

	/* Mark mempool as initialized before binary protocol setup */
	mempool->initialized = true;

	/*
	 * Initialize binary protocol in the mempool to set up metadata structure
	 * and configure circular buffer parameters for crash data storage.
	 */
	ret = nvlog_binary_init(mempool);
	if (ret != 0) {
		dev_err(dev->device, "Failed to initialize binary protocol: error code %d\n", ret);
		mempool->initialized = false;
		goto cleanup_reserve;
	}

	goto out;

cleanup_reserve:
	tegra_hv_mempool_unreserve(mempool->ivmk);
	mempool->ivmk = NULL;
out:
	return ret;
}

/**
 * Setup pstore integration for kernel panic capture -
 *
 * Configures the pstore framework for kernel panic capture. Sets up the
 * pstore zone with appropriate size constraints and registers callback
 * functions for panic data handling.
 *
 * Return: None
 * * *   - Async/Sync: Sync
 *   - Re-entrant: No
 * - API Group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
static void nvlog_oops_setup_pstore(struct nvlog_oops_dev *dev)
{
	u64 aligned_size;

	/*
	 * Configure pstore zone with page-aligned size to ensure proper
	 * memory management and avoid alignment issues with the pstore framework.
	 */
	aligned_size = (dev->mempool.data_size / NVLOG_PAGE_SIZE) * NVLOG_PAGE_SIZE;

	/*
	 * Validate pstore size constraints to ensure we have sufficient space
	 * for at least one crash dump record after page alignment.
	 */
	if (aligned_size == 0) {
		dev_warn(dev->device, "Data region too small for pstore after alignment (data_size=%llu)\n",
				 dev->mempool.data_size);
		return;
	}

	/*
	 * Clamp KMSG size to aligned data size if it exceeds available space.
	 * This prevents pstore framework from requesting more space than available.
	 */
	if (dev->pstore_kmsg_size > aligned_size) {
		dev_warn(dev->device, "KMSG size %u exceeds aligned data size %llu, clamping\n",
				 dev->pstore_kmsg_size, aligned_size);
		dev->pstore_kmsg_size = (u32)aligned_size;
	}

	/*
	 * Basic pstore zone setup with driver name, size, and maximum panic reason.
	 * This configures the pstore framework for kernel panic capture.
	 */
	dev->pstore_zone.name = NVLOG_OOPS_DRV_NAME;
	dev->pstore_zone.total_size = aligned_size;
	dev->pstore_zone.max_reason = dev->pstore_max_reason;

	/*
	 * Only KMSG is captured during kernel panic since this driver is designed
	 * specifically for crash dump storage. Other pstore types are not supported.
	 */
	dev->pstore_zone.kmsg_size = dev->pstore_kmsg_size;

	/*
	 * Console, PMSG, and FTRACE are not written during panic as they require
	 * different handling and are not part of the binary protocol.
	 */
	dev->pstore_zone.console_size = 0;
	dev->pstore_zone.pmsg_size = 0;
	dev->pstore_zone.ftrace_size = 0;

	/*
	 * Set pstore callback functions for read and write operations.
	 * Both write and panic_write use the same function since this is a panic-only driver.
	 */
	dev->pstore_zone.read = nvlog_oops_pstore_read;
	dev->pstore_zone.write = nvlog_oops_pstore_panic_write;
	dev->pstore_zone.panic_write = nvlog_oops_pstore_panic_write;

	/*
	 * Set module ownership for proper reference counting to prevent
	 * module unload while pstore framework is using the driver.
	 */
	dev->pstore_zone.owner = THIS_MODULE;

	/* Register with pstore framework */
#if IS_ENABLED(CONFIG_PSTORE_ZONE)
	if (register_pstore_zone(&dev->pstore_zone))
		dev_err(dev->device, "Failed to register with pstore framework\n");
	else
		dev_info(dev->device, "Successfully registered with pstore framework\n");
#else
	dev_warn(dev->device, "Pstore zone support not available - crash dump capture disabled\n");
#endif
}

/**
 * Parse device tree configuration -
 *
 * Parses device tree properties to configure the nvlog oops driver.
 * Extracts mempool ID, entity ID, and pstore configuration parameters
 * from the device tree node.
 *
 * Return: 0 on success, negative error code on failure
 * * *   - Async/Sync: Sync
 *   - Re-entrant: No
 * - API Group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
static int nvlog_oops_parse_dt(struct nvlog_oops_dev *dev, struct device_node *np)
{
	u32 entity_id_temp;

	/*
	 * Get mempool ID for shared storage from device tree.
	 * This ID is used to reserve the pre-allocated hypervisor memory.
	 */
	if (of_property_read_u32(np, "nvlog_mempool_id", &dev->mempool.mempool_id)) {
		nvlog_log_error("nvlog_oops_parse_dt", NVLOG_ERR_INVALID_PARAMS,
					"Missing required nvlog_mempool_id device tree property");
		return NVLOG_ERR_INVALID_PARAMS;
	}

	/*
	 * Get KMSG entity ID for routing panic data to the correct NvLog entity.
	 * This is the only entity ID used during kernel panic.
	 */
	if (of_property_read_u32(np, "nvlog_kmsg_entity_id", &entity_id_temp)) {
		nvlog_log_error("nvlog_oops_parse_dt", NVLOG_ERR_INVALID_PARAMS,
					"Missing required nvlog_kmsg_entity_id device tree property");
		return NVLOG_ERR_INVALID_PARAMS;
	}
	/*
	 * Validate entity ID range to ensure it fits in u16 as required by NvLog protocol.
	 * Entity IDs are used for routing data to specific NvLog entities.
	 */
	if (entity_id_temp > U16_MAX) {
		nvlog_log_error("nvlog_oops_parse_dt", NVLOG_ERR_INVALID_PARAMS,
					"Entity ID %u exceeds maximum value %u", entity_id_temp, U16_MAX);
		return NVLOG_ERR_INVALID_PARAMS;
	}
	dev->entity_id = (u16)entity_id_temp;

	/*
	 * Parse pstore KMSG size from device tree with fallback to default.
	 * This determines the maximum size of a single panic dump record.
	 */
	if (of_property_read_u32(np, "pstore_kmsg_size", &dev->pstore_kmsg_size)) {
		dev_warn(dev->device, "Missing pstore_kmsg_size, using default\n");
		dev->pstore_kmsg_size = NVLOG_PSTORE_KMSG_RECORD_SIZE;
	}

	/*
	 * Parse maximum panic reason to capture with fallback to OOPS only.
	 * This determines which types of kernel panics will be captured.
	 */
	if (of_property_read_u32(np, "pstore_max_reason", (u32 *)&dev->pstore_max_reason))
		dev->pstore_max_reason = KMSG_DUMP_OOPS;

	/*
	 * Log final configuration for debugging and verification purposes.
	 * This helps confirm that all parameters were parsed correctly.
	 */
	dev_info(dev->device, "Crash dump config: mempool_id=%u, entity_id=0x%04x, kmsg_size=%u\n",
			dev->mempool.mempool_id, dev->entity_id, dev->pstore_kmsg_size);

	return 0;
}

/**
 * Driver probe function - initialize nvlog oops driver -
 *
 * Main driver initialization function called by the platform driver framework.
 * Allocates device structure, parses device tree, initializes mempool,
 * and sets up pstore integration for kernel panic capture.
 *
 * Return: 0 on success, negative error code on failure
 * * *   - Async/Sync: Sync
 *   - Re-entrant: No
 * - API Group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
static int nvlog_oops_probe(struct platform_device *pdev)
{
	struct nvlog_oops_dev *dev = NULL;
	struct device_node *np = pdev->dev.of_node;
	int ret = 0;

	/*
	 * Verify whether hypervisor mode is enabled as this driver requires
	 * hypervisor-managed shared memory for crash data storage.
	 */
	if (!is_tegra_hypervisor_mode()) {
		nvlog_log_error("nvlog_oops_probe", NVLOG_ERR_HYPERVISOR, "Hypervisor not present");
		ret = NVLOG_ERR_HYPERVISOR;
		goto out;
	}

	/*
	 * Validate device tree node exists as this driver requires
	 * device tree configuration for mempool and entity parameters.
	 */
	if (!np) {
		nvlog_log_error("nvlog_oops_probe", NVLOG_ERR_INVALID_PARAMS, "No device tree node");
		ret = NVLOG_ERR_INVALID_PARAMS;
		goto out;
	}

	/*
	 * Allocate device structure using devm_kzalloc for automatic cleanup.
	 * This structure holds all driver state and configuration.
	 */
	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		nvlog_log_error("nvlog_oops_probe", NVLOG_ERR_MEMORY_ALLOC, "Failed to allocate device structure");
		ret = NVLOG_ERR_MEMORY_ALLOC;
		goto out;
	}

	/*
	 * Initialize device structure with platform device reference
	 * and initialize mutex for mempool access synchronization.
	 */
	dev->device = &pdev->dev;
	mutex_init(&dev->mempool.lock);

	/*
	 * Parse device tree configuration to extract mempool ID, entity ID,
	 * and pstore parameters required for driver operation.
	 */
	ret = nvlog_oops_parse_dt(dev, np);
	if (ret) {
		dev_err(&pdev->dev, "Failed to parse device tree: %d\n", ret);
		goto cleanup_mutex;
	}

	/*
	 * Initialize mempool by reserving hypervisor memory and setting up
	 * binary protocol for crash data storage.
	 */
	ret = nvlog_oops_init_mempool(dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to init mempool: %d\n", ret);
		goto cleanup_mutex;
	}

	/*
	 * Setup pstore integration to register with Linux pstore framework
	 * for kernel panic capture functionality.
	 */
	nvlog_oops_setup_pstore(dev);

	/*
	 * Set global device pointer for panic context access and
	 * store device data in platform device for cleanup.
	 */
	nvlog_oops_device = dev;
	platform_set_drvdata(pdev, dev);

	/*
	 * Mark driver as initialized and log success message.
	 * Driver is now ready to capture kernel panic/oops data.
	 */
	dev->initialized = true;
	dev_info(&pdev->dev,
		 "NvLog crash dump driver initialized successfully - "
		 "ready to capture kernel panic/oops data\n");

	goto out;

cleanup_mutex:
	if (dev)
		mutex_destroy(&dev->mempool.lock);
out:
	return ret;
}

/**
 * Driver remove function - cleanup nvlog oops driver -
 *
 * Cleans up driver resources during device removal. Unregisters from
 * pstore framework and releases hypervisor mempool resources.
 *
 * Return: 0 on success (always succeeds)
 * * *   - Async/Sync: Sync
 *   - Re-entrant: No
 * - API Group
 *   - Init: No
 *   - Runtime: No
 *   - De-Init: Yes
 */
static int nvlog_oops_remove(struct platform_device *pdev)
{
	struct nvlog_oops_dev *dev = platform_get_drvdata(pdev);

	/*
	 * Clean up driver resources if device was successfully probed.
	 * This ensures proper cleanup even if probe failed partially.
	 */
	if (dev) {
		/*
		 * Unregister from pstore framework to prevent further
		 * panic write attempts during shutdown.
		 */
#if IS_ENABLED(CONFIG_PSTORE_ZONE)
		unregister_pstore_zone(&dev->pstore_zone);
#endif
		/*
		 * Clear global device pointer to prevent panic context
		 * from accessing freed device structure.
		 */
		nvlog_oops_device = NULL;

		/*
		 * Release mempool resources by unreserving hypervisor memory
		 * and clearing initialization flags.
		 */
		if (dev->mempool.initialized && dev->mempool.ivmk) {
			tegra_hv_mempool_unreserve(dev->mempool.ivmk);
			dev->mempool.ivmk = NULL;
			dev->mempool.initialized = false;
			dev->mempool.binary_initialized = false;
		}

		dev_info(&pdev->dev, "NvLog crash dump driver removed\n");
	}

	return 0;
}


/**
 *
 */

/*
 * ===== PLATFORM DRIVER REGISTRATION =====
 * Linux platform driver framework integration
 */

/**
 * Wrapper function for nvlog_oops_remove with kernel version compatibility -
 *
 * Handles the Linux v6.11 change where platform driver remove functions
 * changed from returning int to returning void. This wrapper allows the
 * driver to work with both older and newer kernels.
 *
 * Return: void or int depending on kernel version
 * * *   - Async/Sync: Sync
 *   - Re-entrant: No
 * - API Group
 *   - Init: No
 *   - Runtime: No
 *   - De-Init: Yes
 */
#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void nvlog_oops_remove_wrapper(struct platform_device *pdev)
{
	nvlog_oops_remove(pdev);
}
#else
static int nvlog_oops_remove_wrapper(struct platform_device *pdev)
{
	return nvlog_oops_remove(pdev);
}
#endif

/* Device tree compatibility matching */
#ifdef CONFIG_OF
static const struct of_device_id nvlog_oops_match[] = {
	{ .compatible = "nvidia,tegra-nvlog-oops-storage", },
	{},
};
MODULE_DEVICE_TABLE(of, nvlog_oops_match);
#endif

/* Platform driver registration structure */
static struct platform_driver nvlog_oops_driver = {
	.probe = nvlog_oops_probe,
	.remove = nvlog_oops_remove_wrapper,
	.driver = {
		.name = NVLOG_OOPS_DRV_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(nvlog_oops_match),
#endif
	},
};


/* Standard platform driver registration using modern kernel conventions */
module_platform_driver(nvlog_oops_driver);

MODULE_AUTHOR("Christopher Taylor <christophert@nvidia.com>");
MODULE_DESCRIPTION("NvLog OOPS storage device driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" NVLOG_OOPS_DRV_NAME);
