// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2022-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include "soc/tegra/camrtc-trace.h"
#include <linux/cdev.h>
#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nospec.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/printk.h>
#include <linux/seq_buf.h>
#include <linux/slab.h>
#include <linux/tegra-camera-rtcpu.h>
#include <linux/tegra-rtcpu-trace.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/nvhost.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/version.h>
#include <asm/cacheflush.h>
#include <uapi/linux/nvdev_fence.h>
#include "device-group.h"
#include <nvidia/conftest.h>

#define CREATE_TRACE_POINTS
#include <trace/events/tegra_rtcpu.h>
#include <trace/events/tegra_capture.h>
#include <trace/events/freertos.h>

/* Tracepoints used by other modules  */
EXPORT_TRACEPOINT_SYMBOL_GPL(capture_ivc_send);
EXPORT_TRACEPOINT_SYMBOL_GPL(capture_ivc_send_error);
EXPORT_TRACEPOINT_SYMBOL_GPL(capture_ivc_notify);
EXPORT_TRACEPOINT_SYMBOL_GPL(capture_ivc_recv);

#define NV(p) "nvidia," #p

#define WORK_INTERVAL_DEFAULT		100
#define EXCEPTION_STR_LENGTH		2048

#define ISP_CLASS_ID 0x32
#define VI_CLASS_ID 0x30

#define DEVICE_NAME		"rtcpu-raw-trace"
#define MAX_READ_SIZE		((ssize_t)(~0U >> 1))
#define UINT32_MAX (~0U)

/*
 * Private driver data structure
 */

struct tegra_rtcpu_trace {
	struct device *dev;
	struct device_node *of_node;
	struct mutex lock;

	/* memory */
	void *trace_memory;
	u32 trace_memory_size;
	dma_addr_t dma_handle;

	/* pointers to each block */
	void *exceptions_base;
	struct camrtc_event_struct *events;
	struct camrtc_event_struct *snapshot_events;
	dma_addr_t dma_handle_pointers;
	dma_addr_t dma_handle_exceptions;
	dma_addr_t dma_handle_events;
	dma_addr_t dma_handle_snapshots;

	/* limit */
	u32 exception_entries;
	u32 event_entries;
	u32 snapshot_entries;

	/* exception pointer */
	u32 exception_last_idx;

	/* last pointer */
	u32 event_last_idx;
	u32 snapshot_last_idx;

	/* worker */
	struct delayed_work work;
	unsigned long work_interval_jiffies;

	/* statistics */
	u32 n_exceptions;
	u64 n_events;
	u32 n_snapshots;

	/* copy of the latest exception and event */
	char last_exception_str[EXCEPTION_STR_LENGTH];
	struct camrtc_event_struct copy_last_event;

	/* debugfs */
	struct dentry *debugfs_root;

	struct platform_device *vi_platform_device;
	struct platform_device *vi1_platform_device;
	struct platform_device *isp_platform_device;
	struct platform_device *isp1_platform_device;

	/* printk logging */
	const char *log_prefix;
	bool enable_printk;
	u32 printk_used;
	char printk[EXCEPTION_STR_LENGTH];

	struct cdev s_dev;
	wait_queue_head_t wait_queue;

	/* flag to indicate a panic occurred */
	atomic_t panic_flag;
};

struct rtcpu_raw_trace_context {
	struct tegra_rtcpu_trace *tracer;
	u32 raw_trace_last_read_event_idx;
	bool first_read_call;
};

/**
 * @brief addition of two unsigned integers without MISRA C violation
 *
 * Adding two u32 values together and wrap in case of overflow
 *
 * @param[in] a  u32 value to be added
 * @param[in] b  u32 value to be added
 *
 * @retval the u32 wrapped addition
 */
static inline u32 wrap_add_u32(u32 const a, u32 const b)
{
	u32 ret = 0U;

	if ((a > 0U) && ((u32)(a - 1U) > ((u32)(UINT32_MAX) - b)))
		ret = (a - 1U) - ((u32)(UINT32_MAX) - b);
	else
		ret = a + b;
	return ret;
}

/**
 * @brief Sets up memory for RTCPU trace
 *
 * This function sets up memory for the RTCPU trace
 * - Reads memory specifications from device tree using @ref of_parse_phandle_with_fixed_args()
 * - Allocates coherent DMA memory using @ref dma_alloc_coherent()
 * - Initializes the trace memory header with appropriate configuration values
 * - In case of error, @ref of_node_put() is called to release the reference to the device node
 *
 * @param[in/out] tracer  Pointer to the tegra_rtcpu_trace structure.
 *                        Valid Range: Non-NULL pointer.
 *
 * @retval 0 on success
 * @retval -EINVAL if invalid trace entry is found in device tree
 * @retval -ENOMEM if memory allocation fails
 */
static int rtcpu_trace_setup_memory(struct tegra_rtcpu_trace *tracer)
{
	struct device *dev = tracer->dev;
	struct of_phandle_args reg_spec;
	int ret;
	void *trace_memory;
	size_t mem_size;
	dma_addr_t dma_addr;

	ret = of_parse_phandle_with_fixed_args(dev->of_node, NV(trace),
		3, 0, &reg_spec);
	if (unlikely(ret != 0)) {
		dev_err(dev, "%s: cannot find trace entry\n", __func__);
		return -EINVAL;
	}

	mem_size = reg_spec.args[2];
	trace_memory = dma_alloc_coherent(dev, mem_size, &dma_addr,
					GFP_KERNEL | __GFP_ZERO);
	if (trace_memory == NULL) {
		ret = -ENOMEM;
		goto error;
	}

	/* Save the information */
	tracer->trace_memory = trace_memory;
	tracer->trace_memory_size = mem_size;
	tracer->dma_handle = dma_addr;
	tracer->of_node = reg_spec.np;

	return 0;

error:
	of_node_put(reg_spec.np);
	return ret;
}

/**
 * @brief Initializes the trace memory
 *
 * This function initializes the trace memory by mapping the DMA handle to the trace memory
 * and setting up the exception base and exception entries.
 * - Checks for overflow in the DMA handle and exception base using @ref check_add_overflow() and
 *   @ref offsetof()
 * - Maps the DMA handle to the trace memory using @ref dma_handle_pointers
 * - Sets up the exception base and exception entries
 * - Initializes the trace memory header with appropriate configuration values
 * - Checks for overflow in the DMA handle and event entries using @ref check_add_overflow() and
 *   @ref check_sub_overflow()
 * - Sets up the event entries and DMA handle events
 * - Copies the trace memory header to the trace memory using @ref memcpy()
 * - Synchronizes the trace memory header for device using @ref dma_sync_single_for_device()
 *
 * @param[in/out] tracer  Pointer to the tegra_rtcpu_trace structure.
 *                        Valid Range: Non-NULL pointer.
 */
static void rtcpu_trace_init_memory(struct tegra_rtcpu_trace *tracer)
{
	u64 add_value = 0;
	u32 sub_value = 0;
	u32 CAMRTC_TRACE_SNAPSHOT_OFFSET = 0;

	if (unlikely(check_add_overflow(tracer->dma_handle,
			(u64)(offsetof(struct camrtc_trace_memory_header,
				exception_next_idx)),
			&add_value))) {
		dev_err(tracer->dev,
				"%s:dma_handle failed due to an overflow\n", __func__);
		return;
	}

	/* memory map */
	tracer->dma_handle_pointers = add_value;

	// exception section setup
	tracer->exceptions_base = tracer->trace_memory +
	    CAMRTC_TRACE_EXCEPTION_OFFSET;
	tracer->exception_entries = 7;

	if (unlikely(check_add_overflow(tracer->dma_handle,
			(u64)(CAMRTC_TRACE_EXCEPTION_OFFSET), &add_value))) {
		dev_err(tracer->dev,
				"%s:dma_handle failed due to an overflow\n", __func__);
		return;
	}

	tracer->dma_handle_exceptions = add_value;

	// event section setup
	tracer->events = tracer->trace_memory + CAMRTC_TRACE_EVENT_OFFSET;
	CAMRTC_TRACE_SNAPSHOT_OFFSET =
		tracer->trace_memory_size - (CAMRTC_TRACE_SNAPSHOT_ENTRIES * CAMRTC_TRACE_EVENT_SIZE);
	if (unlikely(check_sub_overflow(CAMRTC_TRACE_SNAPSHOT_OFFSET,
			CAMRTC_TRACE_EVENT_OFFSET, &sub_value))) {
		dev_err(tracer->dev,
				"%s:trace_memory_size failed due to an overflow\n", __func__);
		return;
	}
	tracer->event_entries = sub_value / CAMRTC_TRACE_EVENT_SIZE;

	if (unlikely(check_add_overflow(tracer->dma_handle,
			(u64)(CAMRTC_TRACE_EVENT_OFFSET), &add_value))) {
		dev_err(tracer->dev,
				"%s:dma_handle failed due to an overflow\n", __func__);
		return;
	}
	tracer->dma_handle_events = add_value;

	// snapshot section setup
	dev_dbg(tracer->dev, "%s: setup snapshot section\n", __func__);
	tracer->snapshot_events = tracer->trace_memory + CAMRTC_TRACE_SNAPSHOT_OFFSET;

	tracer->snapshot_entries = CAMRTC_TRACE_SNAPSHOT_ENTRIES;
	if (unlikely(check_add_overflow(tracer->dma_handle,
			(u64)(CAMRTC_TRACE_SNAPSHOT_OFFSET), &add_value))) {
		dev_err(tracer->dev,
				"%s:dma_handle for snapshots failed due to an overflow\n", __func__);
		return;
	}
	tracer->dma_handle_snapshots = add_value;

	dev_dbg(tracer->dev, "%s: exception section: offset=%x, size=%u, entries=%u\n", __func__,
		CAMRTC_TRACE_EXCEPTION_OFFSET, CAMRTC_TRACE_EXCEPTION_SIZE, tracer->exception_entries);
	dev_dbg(tracer->dev, "%s: event section: offset=%x, size=%u, entries=%u\n", __func__,
		CAMRTC_TRACE_EVENT_OFFSET, CAMRTC_TRACE_EVENT_SIZE, tracer->event_entries);
	dev_dbg(tracer->dev, "%s: snapshot section: offset=%x, size=%u, entries=%u\n", __func__,
		CAMRTC_TRACE_SNAPSHOT_OFFSET, CAMRTC_TRACE_EVENT_SIZE, tracer->snapshot_entries);
	dev_dbg(tracer->dev, "%s: total trace memory size=%u\n", __func__, tracer->trace_memory_size);

	{
		struct camrtc_trace_memory_header header = {
			.tlv.tag = CAMRTC_TAG_NV_TRCON,
			.tlv.len = tracer->trace_memory_size,
			.revision = 1,
			.exception_offset = CAMRTC_TRACE_EXCEPTION_OFFSET,
			.exception_size = CAMRTC_TRACE_EXCEPTION_SIZE,
			.exception_entries = tracer->exception_entries,
			.event_offset = CAMRTC_TRACE_EVENT_OFFSET,
			.event_size = CAMRTC_TRACE_EVENT_SIZE,
			.event_entries = tracer->event_entries,
			.snapshot_offset = CAMRTC_TRACE_SNAPSHOT_OFFSET,
			.snapshot_size = CAMRTC_TRACE_EVENT_SIZE,
			.snapshot_entries = tracer->snapshot_entries,
		};

		memcpy(tracer->trace_memory, &header, sizeof(header));

		dma_sync_single_for_device(tracer->dev,
			tracer->dma_handle, sizeof(header),
			DMA_TO_DEVICE);
	}
}

/**
 * @brief Invalidates cache entries for RTCPU trace
 *
 * This function invalidates cache entries for the RTCPU trace
 * - If the new next is greater than the old next, it invalidates the cache entries for the device
 *   using @ref dma_sync_single_for_cpu()
 * - Checks for overflow in the DMA handle and event entries using @ref check_add_overflow() and
 *   @ref check_sub_overflow()
 * - Synchronizes the trace memory header for device using @ref dma_sync_single_for_cpu()
 *
 * @param[in] tracer  Pointer to the tegra_rtcpu_trace structure.
 * 						Valid Range: Non-NULL pointer.
 * @param[in] dma_handle  DMA handle for the trace memory.
 * @param[in] old_next  Old next value for the trace memory.
 * 						Valid Range: 0 to UINT32_MAX
 * @param[in] new_next  New next value for the trace memory.
 * 						Valid Range: 0 to UINT32_MAX
 * @param[in] entry_size  Entry size for the trace memory.
 * 						Valid Range: 0 to UINT32_MAX
 * @param[in] entry_count  Entry count for the trace memory.
 * 						Valid Range: 0 to UINT32_MAX
 */
static void rtcpu_trace_invalidate_entries(struct tegra_rtcpu_trace *tracer,
	dma_addr_t dma_handle, u32 old_next, u32 new_next,
	u32 entry_size, u32 entry_count)
{
	u64 add_value = 0;
	u32 mul_value_u32 = 0;
	u32 sub_value = 0;
	u64 mul_value_u64 = (u64)old_next * (u64)entry_size;

	if (unlikely(check_add_overflow(dma_handle, mul_value_u64, &add_value))) {
		dev_err(tracer->dev,
				"%s:dma_handle failed due to an overflow\n", __func__);
		return;
	}

	/* invalidate cache */
	if (new_next > old_next) {
		if (unlikely(check_sub_overflow(new_next, old_next, &sub_value))) {
			dev_err(tracer->dev,
					"%s:new_next failed due to an overflow\n", __func__);
			return;
		}

		if (unlikely(check_mul_overflow(sub_value, entry_size, &mul_value_u32))) {
			dev_err(tracer->dev,
				"%s:sub_value failed due to an overflow\n", __func__);
			return;
		}

		dma_sync_single_for_cpu(tracer->dev,
			add_value,
			mul_value_u32,
			DMA_FROM_DEVICE);
	} else {
		if (unlikely(check_sub_overflow(entry_count, old_next, &sub_value))) {
			dev_err(tracer->dev,
					"%s:new_next failed due to an overflow\n", __func__);
			return;
		}

		if (unlikely(check_mul_overflow(sub_value, entry_size, &mul_value_u32))) {
			dev_err(tracer->dev,
					"%s:(entry_count-entry_size) failed due to an overflow\n",
					__func__);
			return;
		}

		dma_sync_single_for_cpu(tracer->dev,
			add_value,
			mul_value_u32,
			DMA_FROM_DEVICE);
		dma_sync_single_for_cpu(tracer->dev,
			dma_handle, new_next * entry_size,
			DMA_FROM_DEVICE);
	}
}

/**
 * @brief Prints the exception trace
 *
 * This function prints the exception trace
 * - constructs the sequence buffer using @ref seq_buf_init()
 * - prints the exception type using @ref seq_buf_printf()
 * - prints the exception registers using @ref seq_buf_printf()
 * - prints the exception callstack using @ref seq_buf_printf()
 * - If exception length is greater than the exception size, it prints multiple lines
 *   using @ref seq_buf_printf()
 *
 * @param[in] tracer  Pointer to the tegra_rtcpu_trace structure.
 * 						Valid Range: Non-NULL pointer.
 * @param[in] exc  Pointer to the camrtc_trace_armv7_exception structure.
 * 						Valid Range: Non-NULL pointer.
 */
static void rtcpu_trace_exception(struct tegra_rtcpu_trace *tracer,
	struct camrtc_trace_armv7_exception *exc)
{
	static const char * const s_str_exc_type[] = {
		"Invalid (Reset)",
		"Undefined instruction",
		"Invalid (SWI)",
		"Prefetch abort",
		"Data abort",
		"Invalid (Reserved)",
		"IRQ",
		"FIQ",
	};

	struct seq_buf sb;
	unsigned int i, count;
	char *buf = tracer->last_exception_str;
	size_t buf_size = sizeof(tracer->last_exception_str);
	char const header[] =
		"###################### RTCPU EXCEPTION ######################";
	char const trailer[] =
		"#############################################################";

	seq_buf_init(&sb, buf, buf_size);

	seq_buf_printf(&sb, "%s %s\n",
		tracer->log_prefix,
		(exc->type < ARRAY_SIZE(s_str_exc_type)) ?
			s_str_exc_type[exc->type] : "Unknown");

	seq_buf_printf(&sb,
	    "  R0:  %08x R1:  %08x R2:  %08x R3:  %08x\n",
	    exc->gpr.r0, exc->gpr.r1, exc->gpr.r2, exc->gpr.r3);
	seq_buf_printf(&sb,
	    "  R4:  %08x R5:  %08x R6:  %08x R7:  %08x\n",
	    exc->gpr.r4, exc->gpr.r5, exc->gpr.r6, exc->gpr.r7);
	seq_buf_printf(&sb,
	    "  R8:  %08x R9:  %08x R10: %08x R11: %08x\n",
	    exc->gpr.r8, exc->gpr.r9, exc->gpr.r10, exc->gpr.r11);
	seq_buf_printf(&sb,
	    "  R12: %08x SP:  %08x LR:  %08x PC:  %08x\n",
	    exc->gpr.r12, exc->gpr.sp, exc->gpr.lr, exc->gpr.pc);

	if (exc->type == CAMRTC_ARMV7_EXCEPTION_FIQ) {
		seq_buf_printf(&sb,
		    "  R8: %08x R9: %08x R10: %08x R11: %08x, R12: %08x\n",
		    exc->gpr.r8_prev, exc->gpr.r9_prev,
		    exc->gpr.r10_prev, exc->gpr.r11_prev,
		    exc->gpr.r12_prev);
	}
	seq_buf_printf(&sb, "  SP: %08x LR: %08x\n",
	    exc->gpr.sp_prev, exc->gpr.lr_prev);

	seq_buf_printf(&sb, "  CPSR: %08x SPSR: %08x\n",
	    exc->cpsr, exc->spsr);

	seq_buf_printf(&sb, "  DFSR: %08x DFAR: %08x ADFSR: %08x\n",
	    exc->dfsr, exc->dfar, exc->adfsr);
	seq_buf_printf(&sb, "  IFSR: %08x IFAR: %08x AIFSR: %08x\n",
	    exc->ifsr, exc->ifar, exc->aifsr);

	count = (exc->len -
		offsetof(struct camrtc_trace_armv7_exception, callstack)) /
		sizeof(struct camrtc_trace_callstack);

	if (count > 0)
		seq_buf_printf(&sb, "Callstack\n");

	for (i = 0; i < count; ++i) {
		if (i >= CAMRTC_TRACE_CALLSTACK_MAX)
			break;
		seq_buf_printf(&sb, "  [%08x]: %08x\n",
			exc->callstack[i].lr_stack_addr, exc->callstack[i].lr);
	}

	if (i < count)
		seq_buf_printf(&sb, "  ... [skipping %u entries]\n", count - i);

	printk(KERN_INFO "%s\n%s\n%s\n%s%s%s\n%s\n",
		" ", " ", header, buf, trailer, " ", " ");
}

/**
 * @brief Prints the exception trace
 *
 * This function prints the exception trace
 * - Gets the old and new next values from the trace memory header
 * - Checks if the old and new next values are the same, if so, return
 * - Checks if the new next value is greater than the exception entries, if so, print a warning and
 *   return
 * - Sets the new next value to the exception entries using @ref array_index_nospec()
 * - Invalidates the cache entries for the device using @ref rtcpu_trace_invalidate_entries
 * - Copies the exception to the exception structure using @ref memcpy()
 * - Prints the exception trace using @ref rtcpu_trace_exception()
 * - Increments the exception count using @ref wrap_add_u32()
 * - Increments the old next value using @ref wrap_add_u32()
 * - Sets the exception last index to the new next value
 *
 * @param[in/out] tracer  Pointer to the tegra_rtcpu_trace structure.
 *                        Valid Range: Non-NULL pointer.
 */
static inline void rtcpu_trace_exceptions(struct tegra_rtcpu_trace *tracer)
{
	const struct camrtc_trace_memory_header *header = tracer->trace_memory;
	union {
		struct camrtc_trace_armv7_exception exc;
		uint8_t mem[CAMRTC_TRACE_EXCEPTION_SIZE];
	} exc;
	u32 old_next = tracer->exception_last_idx;
	u32 new_next = header->exception_next_idx;
	u64 mul_value = 0;

	if (old_next == new_next)
		return;

	if (new_next >= tracer->exception_entries) {
		dev_warn_ratelimited(tracer->dev,
			"exception entry %u outside range 0..%u\n",
			new_next, tracer->exception_entries - 1);
		return;
	}

	new_next = array_index_nospec(new_next, tracer->exception_entries);

	rtcpu_trace_invalidate_entries(tracer,
				tracer->dma_handle_exceptions,
				old_next, new_next,
				CAMRTC_TRACE_EXCEPTION_SIZE,
				tracer->exception_entries);

	while (old_next != new_next) {
		void *emem;
		old_next = array_index_nospec(old_next, tracer->exception_entries);
		mul_value = CAMRTC_TRACE_EXCEPTION_SIZE * old_next;

		emem = tracer->exceptions_base + mul_value;
		memcpy(&exc.mem, emem, CAMRTC_TRACE_EXCEPTION_SIZE);
		rtcpu_trace_exception(tracer, &exc.exc);
		tracer->n_exceptions = wrap_add_u32(tracer->n_exceptions, 1U);
		old_next = wrap_add_u32(old_next, 1U);
		if (old_next == tracer->exception_entries)
			old_next = 0;
	}

	tracer->exception_last_idx = new_next;
}

/**
 * @brief Calculates the length of the event
 *
 * This function calculates the length of the event
 * - If the length is greater than the event size, it sets the length to the
 *   @ref CAMRTC_TRACE_EVENT_SIZE minus @ref CAMRTC_TRACE_EVENT_HEADER_SIZE
 * - If the length is greater than the header size, it sets the length to the
 *   length minus the @ref CAMRTC_TRACE_EVENT_HEADER_SIZE
 * - Otherwise, it sets the length to 0
 *
 * @param[in] event  Pointer to the camrtc_event_struct structure.
 * 						Valid Range: Non-NULL pointer.
 *
 * @retval (uint16_t) The length of the event.
 */
static uint16_t rtcpu_trace_event_len(const struct camrtc_event_struct *event)
{
	uint16_t len = event->header.len;

	if (len > CAMRTC_TRACE_EVENT_SIZE)
		len = CAMRTC_TRACE_EVENT_SIZE - CAMRTC_TRACE_EVENT_HEADER_SIZE;
	else if (len > CAMRTC_TRACE_EVENT_HEADER_SIZE)
		len = len - CAMRTC_TRACE_EVENT_HEADER_SIZE;
	else
		len = 0;

	return len;
}

/**
 * @brief Prints the unknown trace event
 *
 * This function prints the unknown trace event
 * - Gets the id and length of the event
 * - Prints the unknown trace event using @ref trace_rtcpu_unknown()
 *
 * @param[in] event  Pointer to the camrtc_event_struct structure.
 * 						Valid Range: Non-NULL pointer.
 */
static void rtcpu_unknown_trace_event(struct camrtc_event_struct *event)
{
	uint32_t id = event->header.id;
	uint16_t len = rtcpu_trace_event_len(event);
	uint64_t tstamp = event->header.tstamp;

	trace_rtcpu_unknown(tstamp, id, len, &event->data.data8[0]);
}

/**
 * @brief Prints the base trace event
 *
 * This function prints the base trace event
 * - Gets the id of the event
 * - If the id is @ref camrtc_trace_base_target_init, it prints the base trace event using
 *   @ref trace_rtcpu_target_init()
 * - If the id is @ref camrtc_trace_base_start_scheduler, it prints the base trace event using
 *   @ref trace_rtcpu_start_scheduler()
 * - Otherwise, it prints the unknown trace event using @ref rtcpu_unknown_trace_event()
 *
 * @param[in] event  Pointer to the camrtc_event_struct structure.
 * 						Valid Range: Non-NULL pointer.
 */
static void rtcpu_trace_base_event(struct camrtc_event_struct *event)
{
	switch (event->header.id) {
	case camrtc_trace_base_target_init:
		trace_rtcpu_target_init(event->header.tstamp);
		break;
	case camrtc_trace_base_start_scheduler:
		trace_rtcpu_start_scheduler(event->header.tstamp);
		break;
	default:
		rtcpu_unknown_trace_event(event);
		break;
	}
}

/**
 * @brief Prints the RTOS trace event
 *
 * This function prints the RTOS trace event
 * - Gets the id of the event
 * - If the id is @ref camrtc_trace_rtos_task_switched_in, it prints the RTOS trace event using
 *   @ref trace_rtos_task_switched_in()
 * - If the id is @ref camrtc_trace_rtos_increase_tick_count, it prints the RTOS trace event using
 *   @ref trace_rtos_increase_tick_count()
 * - If the id is @ref camrtc_trace_rtos_low_power_idle_begin, it prints the RTOS trace event using
 *   @ref trace_rtos_low_power_idle_begin()
 * - If the id is @ref camrtc_trace_rtos_low_power_idle_end, it prints the RTOS trace event using
 *   @ref trace_rtos_low_power_idle_end()
 * - If the id is @ref camrtc_trace_rtos_task_switched_out, it prints the RTOS trace event using
 *   @ref trace_rtos_task_switched_out()
 * - If the id is @ref camrtc_trace_rtos_task_priority_inherit, it prints the RTOS trace event using
 *   @ref trace_rtos_task_priority_inherit()
 * - If the id is @ref camrtc_trace_rtos_task_priority_disinherit, it prints the RTOS trace event
 *   using @ref trace_rtos_task_priority_disinherit()
 * - If the id is @ref camrtc_trace_rtos_blocking_on_queue_receive, it prints the RTOS trace event
 *   using @ref trace_rtos_blocking_on_queue_receive()
 * - If the id is @ref camrtc_trace_rtos_blocking_on_queue_send, it prints the RTOS trace event
 *   using @ref trace_rtos_blocking_on_queue_send()
 * - If the id is @ref camrtc_trace_rtos_moved_task_to_ready_state, it prints the RTOS trace event
 *   using @ref trace_rtos_moved_task_to_ready_state()
 * - If the id is @ref camrtc_trace_rtos_queue_create, it prints the RTOS trace event using
 *   @ref trace_rtos_queue_create()
 * - If the id is @ref camrtc_trace_rtos_queue_create_failed, it prints the RTOS trace event using
 *   @ref trace_rtos_queue_create_failed()
 * - If the id is @ref camrtc_trace_rtos_create_mutex, it prints the RTOS trace event using
 *   @ref trace_rtos_create_mutex()
 * - If the id is @ref camrtc_trace_rtos_create_mutex_failed, it prints the RTOS trace event using
 *   @ref trace_rtos_create_mutex_failed()
 * - If the id is @ref camrtc_trace_rtos_give_mutex_recursive, it prints the RTOS trace event
 *   using @ref trace_rtos_give_mutex_recursive()
 * - If the id is @ref camrtc_trace_rtos_give_mutex_recursive_failed, it prints the RTOS trace event
 *   using @ref trace_rtos_give_mutex_recursive_failed()
 * - If the id is @ref camrtc_trace_rtos_take_mutex_recursive, it prints the RTOS trace event using
 *   @ref trace_rtos_take_mutex_recursive()
 * - If the id is @ref camrtc_trace_rtos_take_mutex_recursive_failed, it prints the RTOS trace
 *   event using @ref trace_rtos_take_mutex_recursive_failed()
 * - If the id is @ref camrtc_trace_rtos_create_counting_semaphore, it prints the RTOS trace event using
 *   @ref trace_rtos_create_counting_semaphore()
 * - If the id is @ref camrtc_trace_rtos_create_counting_semaphore_failed, it prints the RTOS trace
 *   event using @ref trace_rtos_create_counting_semaphore_failed()
 * - If the id is @ref camrtc_trace_rtos_queue_send, it prints the RTOS trace event using
 *   @ref trace_rtos_queue_send()
 * - If the id is @ref camrtc_trace_rtos_queue_send_failed, it prints the RTOS trace event using
 *   @ref trace_rtos_queue_send_failed()
 * - If the id is @ref camrtc_trace_rtos_queue_receive, it prints the RTOS trace event using
 *   @ref trace_rtos_queue_receive()
 * - If the id is @ref camrtc_trace_rtos_queue_peek, it prints the RTOS trace event using
 *   @ref trace_rtos_queue_peek()
 * - If the id is @ref camrtc_trace_rtos_queue_peek_from_isr, it prints the RTOS trace event using
 *   @ref trace_rtos_queue_peek_from_isr()
 * - If the id is @ref camrtc_trace_rtos_queue_receive_failed, it prints the RTOS trace event using
 *   @ref trace_rtos_queue_receive_failed()
 * - If the id is @ref camrtc_trace_rtos_queue_send_from_isr, it prints the RTOS trace event using
 *   @ref trace_rtos_queue_send_from_isr()
 * - If the id is @ref camrtc_trace_rtos_queue_send_from_isr_failed, it prints the RTOS trace event
 *   using @ref trace_rtos_queue_send_from_isr_failed()
 * - If the id is @ref camrtc_trace_rtos_queue_receive_from_isr, it prints the RTOS trace event
 *   using @ref trace_rtos_queue_receive_from_isr()
 * - If the id is @ref camrtc_trace_rtos_queue_receive_from_isr_failed, it prints the RTOS trace
 *   event using @ref trace_rtos_queue_receive_from_isr_failed()
 * - If the id is @ref camrtc_trace_rtos_queue_peek_from_isr_failed, it prints the RTOS trace event
 *   using @ref trace_rtos_queue_peek_from_isr_failed()
 * - If the id is @ref camrtc_trace_rtos_queue_delete, it prints the RTOS trace event
 *   using @ref trace_rtos_queue_delete()
 * - If the id is @ref camrtc_trace_rtos_task_create, it prints the RTOS trace event
 *   using @ref trace_rtos_task_create()
 * - If the id is @ref camrtc_trace_rtos_task_create_failed, it prints the RTOS trace event
 using @ref trace_rtos_task_create_failed()
 * - If the id is @ref camrtc_trace_rtos_task_delete, it prints the RTOS trace event
 using @ref trace_rtos_task_delete()
 * - If the id is @ref camrtc_trace_rtos_task_delay_until, it prints the RTOS trace event
 using @ref trace_rtos_task_delay_until()
 * - If the id is @ref camrtc_trace_rtos_task_delay, it prints the RTOS trace event
 using @ref trace_rtos_task_delay()
 * - If the id is @ref camrtc_trace_rtos_task_priority_set, it prints the RTOS trace event
 using @ref trace_rtos_task_priority_set()
 * - If the id is @ref camrtc_trace_rtos_task_suspend, it prints the RTOS trace event
 using @ref trace_rtos_task_suspend()
 * - If the id is @ref camrtc_trace_rtos_task_resume, it prints the RTOS trace event
 using @ref trace_rtos_task_resume()
 * - If the id is @ref camrtc_trace_rtos_task_resume_from_isr, it prints the RTOS trace event
 using @ref trace_rtos_task_resume_from_isr()
 * - If the id is @ref camrtc_trace_rtos_task_increment_tick, it prints the RTOS trace event
 using @ref trace_rtos_task_increment_tick()
 * - If the id is @ref camrtc_trace_rtos_timer_create, it prints the RTOS trace event
 using @ref trace_rtos_timer_create()
 * - If the id is @ref camrtc_trace_rtos_timer_create_failed, it prints the RTOS trace event
 using @ref trace_rtos_timer_create_failed()
 * - If the id is @ref camrtc_trace_rtos_timer_command_send, it prints the RTOS trace event
 using @ref trace_rtos_timer_command_send()
 * - If the id is @ref camrtc_trace_rtos_timer_expired, it prints the RTOS trace event
 using @ref trace_rtos_timer_expired()
 * - If the id is @ref camrtc_trace_rtos_timer_command_received, it prints the RTOS trace event
 using @ref trace_rtos_timer_command_received()
 * - If the id is @ref camrtc_trace_rtos_malloc, it prints the RTOS trace event
 using @ref trace_rtos_malloc()
 * - If the id is @ref camrtc_trace_rtos_free, it prints the RTOS trace event
  using @ref trace_rtos_free()
 * - If the id is @ref camrtc_trace_rtos_event_group_create, it prints the RTOS trace event
 using @ref trace_rtos_event_group_create()
 * - If the id is @ref camrtc_trace_rtos_event_group_create_failed, it prints the RTOS trace event
 using @ref trace_rtos_event_group_create_failed()
 * - If the id is @ref camrtc_trace_rtos_event_group_sync_block, it prints the RTOS trace event
 using @ref trace_rtos_event_group_sync_block()
 * - If the id is @ref camrtc_trace_rtos_event_group_sync_end, it prints the RTOS trace event
 using @ref trace_rtos_event_group_sync_end()
 * - If the id is @ref camrtc_trace_rtos_event_group_wait_bits_block, it prints the RTOS trace event
 using @ref trace_rtos_event_group_wait_bits_block()
 * - If the id is @ref camrtc_trace_rtos_event_group_wait_bits_end, it prints the RTOS trace event
 using @ref trace_rtos_event_group_wait_bits_end()
 * - If the id is @ref camrtc_trace_rtos_event_group_clear_bits, it prints the RTOS trace event
 using @ref trace_rtos_event_group_clear_bits()
 * - If the id is @ref camrtc_trace_rtos_event_group_clear_bits_from_isr, it prints the RTOS
 trace event using @ref trace_rtos_event_group_clear_bits_from_isr()
 * - If the id is @ref camrtc_trace_rtos_event_group_set_bits, it prints the RTOS trace event
 using @ref trace_rtos_event_group_set_bits()
 * - If the id is @ref camrtc_trace_rtos_event_group_set_bits_from_isr, it prints the RTOS trace
 event using @ref trace_rtos_event_group_set_bits_from_isr()
 * - If the id is @ref camrtc_trace_rtos_event_group_delete, it prints the RTOS trace event
 using @ref trace_rtos_event_group_delete()
 * - If the id is @ref camrtc_trace_rtos_pend_func_call, it prints the RTOS trace event
 using @ref trace_rtos_pend_func_call()
 * - If the id is @ref camrtc_trace_rtos_pend_func_call_from_isr, it prints the RTOS trace event
 using @ref trace_rtos_pend_func_call_from_isr()
 * - If the id is @ref camrtc_trace_rtos_queue_registry_add, it prints the RTOS trace event
 using @ref trace_rtos_queue_registry_add()
 * - If id is not found, it prints the unknown trace event using @ref rtcpu_unknown_trace_event()
 *
 * @param[in] event  Pointer to the camrtc_event_struct structure.
 * 						Valid Range: Non-NULL pointer.
 */
static void rtcpu_trace_rtos_event(struct camrtc_event_struct *event)
{
	switch (event->header.id) {
	case camrtc_trace_rtos_task_switched_in:
		trace_rtos_task_switched_in(event->header.tstamp);
		break;
	case camrtc_trace_rtos_increase_tick_count:
		trace_rtos_increase_tick_count(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_low_power_idle_begin:
		trace_rtos_low_power_idle_begin(event->header.tstamp);
		break;
	case camrtc_trace_rtos_low_power_idle_end:
		trace_rtos_low_power_idle_end(event->header.tstamp);
		break;
	case camrtc_trace_rtos_task_switched_out:
		trace_rtos_task_switched_out(event->header.tstamp);
		break;
	case camrtc_trace_rtos_task_priority_inherit:
		trace_rtos_task_priority_inherit(event->header.tstamp,
			event->data.data32[0],
			event->data.data32[1]);
		break;
	case camrtc_trace_rtos_task_priority_disinherit:
		trace_rtos_task_priority_disinherit(event->header.tstamp,
			event->data.data32[0],
			event->data.data32[1]);
		break;
	case camrtc_trace_rtos_blocking_on_queue_receive:
		trace_rtos_blocking_on_queue_receive(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_blocking_on_queue_send:
		trace_rtos_blocking_on_queue_send(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_moved_task_to_ready_state:
		trace_rtos_moved_task_to_ready_state(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_queue_create:
		trace_rtos_queue_create(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_queue_create_failed:
		trace_rtos_queue_create_failed(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_create_mutex:
		trace_rtos_create_mutex(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_create_mutex_failed:
		trace_rtos_create_mutex_failed(event->header.tstamp);
		break;
	case camrtc_trace_rtos_give_mutex_recursive:
		trace_rtos_give_mutex_recursive(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_give_mutex_recursive_failed:
		trace_rtos_give_mutex_recursive_failed(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_take_mutex_recursive:
		trace_rtos_take_mutex_recursive(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_take_mutex_recursive_failed:
		trace_rtos_take_mutex_recursive_failed(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_create_counting_semaphore:
		trace_rtos_create_counting_semaphore(event->header.tstamp);
		break;
	case camrtc_trace_rtos_create_counting_semaphore_failed:
		trace_rtos_create_counting_semaphore_failed(
			event->header.tstamp);
		break;
	case camrtc_trace_rtos_queue_send:
		trace_rtos_queue_send(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_queue_send_failed:
		trace_rtos_queue_send_failed(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_queue_receive:
		trace_rtos_queue_receive(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_queue_peek:
		trace_rtos_queue_peek(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_queue_peek_from_isr:
		trace_rtos_queue_peek_from_isr(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_queue_receive_failed:
		trace_rtos_queue_receive_failed(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_queue_send_from_isr:
		trace_rtos_queue_send_from_isr(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_queue_send_from_isr_failed:
		trace_rtos_queue_send_from_isr_failed(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_queue_receive_from_isr:
		trace_rtos_queue_receive_from_isr(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_queue_receive_from_isr_failed:
		trace_rtos_queue_receive_from_isr_failed(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_queue_peek_from_isr_failed:
		trace_rtos_queue_peek_from_isr_failed(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_queue_delete:
		trace_rtos_queue_delete(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_task_create:
		trace_rtos_task_create(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_task_create_failed:
		trace_rtos_task_create_failed(event->header.tstamp);
		break;
	case camrtc_trace_rtos_task_delete:
		trace_rtos_task_delete(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_task_delay_until:
		trace_rtos_task_delay_until(event->header.tstamp);
		break;
	case camrtc_trace_rtos_task_delay:
		trace_rtos_task_delay(event->header.tstamp);
		break;
	case camrtc_trace_rtos_task_priority_set:
		trace_rtos_task_priority_set(event->header.tstamp,
			event->data.data32[0],
			event->data.data32[1]);
		break;
	case camrtc_trace_rtos_task_suspend:
		trace_rtos_task_suspend(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_task_resume:
		trace_rtos_task_resume(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_task_resume_from_isr:
		trace_rtos_task_resume_from_isr(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_task_increment_tick:
		trace_rtos_task_increment_tick(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_timer_create:
		trace_rtos_timer_create(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_timer_create_failed:
		trace_rtos_timer_create_failed(event->header.tstamp);
		break;
	case camrtc_trace_rtos_timer_command_send:
		trace_rtos_timer_command_send(event->header.tstamp,
			event->data.data32[0],
			event->data.data32[1],
			event->data.data32[2],
			event->data.data32[3]);
		break;
	case camrtc_trace_rtos_timer_expired:
		trace_rtos_timer_expired(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_timer_command_received:
		trace_rtos_timer_command_received(event->header.tstamp,
			event->data.data32[0],
			event->data.data32[1],
			event->data.data32[2]);
		break;
	case camrtc_trace_rtos_malloc:
		trace_rtos_malloc(event->header.tstamp,
			event->data.data32[0],
			event->data.data32[1]);
		break;
	case camrtc_trace_rtos_free:
		trace_rtos_free(event->header.tstamp,
			event->data.data32[0],
			event->data.data32[1]);
		break;
	case camrtc_trace_rtos_event_group_create:
		trace_rtos_event_group_create(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_event_group_create_failed:
		trace_rtos_event_group_create_failed(event->header.tstamp);
		break;
	case camrtc_trace_rtos_event_group_sync_block:
		trace_rtos_event_group_sync_block(event->header.tstamp,
			event->data.data32[0],
			event->data.data32[1],
			event->data.data32[2]);
		break;
	case camrtc_trace_rtos_event_group_sync_end:
		trace_rtos_event_group_sync_end(event->header.tstamp,
			event->data.data32[0],
			event->data.data32[1],
			event->data.data32[2],
			event->data.data32[3]);
		break;
	case camrtc_trace_rtos_event_group_wait_bits_block:
		trace_rtos_event_group_wait_bits_block(event->header.tstamp,
			event->data.data32[0],
			event->data.data32[1]);
		break;
	case camrtc_trace_rtos_event_group_wait_bits_end:
		trace_rtos_event_group_wait_bits_end(event->header.tstamp,
			event->data.data32[0],
			event->data.data32[1],
			event->data.data32[2]);
		break;
	case camrtc_trace_rtos_event_group_clear_bits:
		trace_rtos_event_group_clear_bits(event->header.tstamp,
			event->data.data32[0],
			event->data.data32[1]);
		break;
	case camrtc_trace_rtos_event_group_clear_bits_from_isr:
		trace_rtos_event_group_clear_bits_from_isr(event->header.tstamp,
			event->data.data32[0],
			event->data.data32[1]);
		break;
	case camrtc_trace_rtos_event_group_set_bits:
		trace_rtos_event_group_set_bits(event->header.tstamp,
			event->data.data32[0],
			event->data.data32[1]);
		break;
	case camrtc_trace_rtos_event_group_set_bits_from_isr:
		trace_rtos_event_group_set_bits_from_isr(event->header.tstamp,
			event->data.data32[0],
			event->data.data32[1]);
		break;
	case camrtc_trace_rtos_event_group_delete:
		trace_rtos_event_group_delete(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_rtos_pend_func_call:
		trace_rtos_pend_func_call(event->header.tstamp,
			event->data.data32[0],
			event->data.data32[1],
			event->data.data32[2],
			event->data.data32[3]);
		break;
	case camrtc_trace_rtos_pend_func_call_from_isr:
		trace_rtos_pend_func_call_from_isr(event->header.tstamp,
			event->data.data32[0],
			event->data.data32[1],
			event->data.data32[2],
			event->data.data32[3]);
		break;
	case camrtc_trace_rtos_queue_registry_add:
		trace_rtos_queue_registry_add(event->header.tstamp,
			event->data.data32[0],
			event->data.data32[1]);
		break;
	default:
		rtcpu_unknown_trace_event(event);
		break;
	}
}

static void rtcpu_trace_dbg_event(struct camrtc_event_struct *event)
{
	switch (event->header.id) {
	case camrtc_trace_dbg_unknown:
		trace_rtcpu_dbg_unknown(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_dbg_enter:
		trace_rtcpu_dbg_enter(event->header.tstamp,
			event->data.data32[0]);
		break;
	case camrtc_trace_dbg_exit:
		trace_rtcpu_dbg_exit(event->header.tstamp);
		break;
	case camrtc_trace_dbg_set_loglevel:
		trace_rtcpu_dbg_set_loglevel(event->header.tstamp,
			event->data.data32[0],
			event->data.data32[1]);
		break;
	default:
		rtcpu_unknown_trace_event(event);
		break;
	}
}

const char * const g_trace_vinotify_tag_strs[] = {
	"FS", "FE",
	"CSIMUX_FRAME", "CSIMUX_STREAM",
	"CHANSEL_PXL_SOF", "CHANSEL_PXL_EOF",
	"CHANSEL_EMBED_SOF", "CHANSEL_EMBED_EOF",
	"CHANSEL_NLINES", "CHANSEL_FAULT",
	"CHANSEL_FAULT_FE", "CHANSEL_NOMATCH",
	"CHANSEL_COLLISION", "CHANSEL_SHORT_FRAME",
	"CHANSEL_LOAD_FRAMED", "ATOMP_PACKER_OVERFLOW",
	"ATOMP_FS", "ATOMP_FE",
	"ATOMP_FRAME_DONE", "ATOMP_EMB_DATA_DONE",
	"ATOMP_FRAME_NLINES_DONE", "ATOMP_FRAME_TRUNCATED",
	"ATOMP_FRAME_TOSSED", "ATOMP_PDAF_DATA_DONE",
	"VIFALC_TDSTATE", "VIFALC_ACTIONLST",
	"ISPBUF_FIFO_OVERFLOW", "ISPBUF_FS",
	"ISPBUF_FE", "VGP0_DONE",
	"VGP1_DONE", "FMLITE_DONE",
};
const unsigned int g_trace_vinotify_tag_str_count =
	ARRAY_SIZE(g_trace_vinotify_tag_strs);

/**
 * @brief Trace VINOTIFY events
 *
 * This function traces VINOTIFY events based on the event ID.
 *  - It handles different VINOTIFY event types and their corresponding trace functions.
 *  - If the event ID is not found, it prints the unknown trace event using
 *    @ref rtcpu_unknown_trace_event()
 *  - If event ID is @ref camrtc_trace_vinotify_event_ts64, it prints the VINOTIFY trace event using
 *    @ref trace_rtcpu_vinotify_event_ts64()
 *  - If event ID is @ref camrtc_trace_vinotify_event, it prints the VINOTIFY trace event using
 *    @ref trace_rtcpu_vinotify_event()
 *  - If event ID is @ref camrtc_trace_vinotify_error, it prints the VINOTIFY trace event using
 *    @ref trace_rtcpu_vinotify_error()
 *
 * @param[in] event Pointer to the camrtc_event_struct structure.
 * 						Valid Range: Non-NULL pointer.
 */
static void rtcpu_trace_vinotify_event(struct camrtc_event_struct *event)
{
	switch (event->header.id) {
	case camrtc_trace_vinotify_event_ts64:
		trace_rtcpu_vinotify_event_ts64(event->header.tstamp,
		(event->data.data32[0] >> 1) & 0x7f, event->data.data32[0],
		((u64)event->data.data32[3] << 32) | event->data.data32[1],
		event->data.data32[2]);
		break;
	case camrtc_trace_vinotify_event:
		trace_rtcpu_vinotify_event(event->header.tstamp,
		event->data.data32[0], event->data.data32[1],
		event->data.data32[2], event->data.data32[3],
		event->data.data32[4], event->data.data32[5],
		event->data.data32[6]);
		break;
	case camrtc_trace_vinotify_error:
		trace_rtcpu_vinotify_error(event->header.tstamp,
		event->data.data32[0], event->data.data32[1],
		event->data.data32[2], event->data.data32[3],
		event->data.data32[4], event->data.data32[5],
		event->data.data32[6]);
		break;
	default:
		rtcpu_unknown_trace_event(event);
		break;
	}
}

/**
 * @brief Trace VI frame events
 *
 * This function traces VI frame events based on the event ID.
 *  - Gets the platform device data for the VI unit using @ref platform_get_drvdata()
 *  - If the platform device data is not found, it returns
 *  - If the event ID is not found, it prints the unknown trace event using
 *    @ref rtcpu_unknown_trace_event()
 *  - If event ID is @ref camrtc_trace_vi_frame_begin, it prints the VI frame begin trace event using
 *    @ref trace_vi_frame_begin()
 *  - If event ID is @ref camrtc_trace_vi_frame_end, it prints the VI frame end trace event using
 *    @ref trace_vi_frame_end() and @ref trace_task_fence()
 *
 * @param[in] tracer Pointer to the tegra_rtcpu_trace structure.
 * 						Valid Range: Non-NULL pointer.
 * @param[in] event Pointer to the camrtc_event_struct structure.
 * 						Valid Range: Non-NULL pointer.
 */
static void rtcpu_trace_vi_frame_event(struct tegra_rtcpu_trace *tracer,
				struct camrtc_event_struct *event)
{
	struct nvhost_device_data *pdata = NULL;
	u64 ts = 0;
	u32 vi_unit_id = event->data.data32[6];

	if (tracer->vi_platform_device == NULL)
		return;

	if (vi_unit_id == 0)
		pdata = platform_get_drvdata(tracer->vi_platform_device);
	else if (vi_unit_id == 1)
		pdata = platform_get_drvdata(tracer->vi1_platform_device);

	if (pdata == NULL)
		return;

	switch (event->header.id) {
	case camrtc_trace_vi_frame_begin:
		ts = ((u64)event->data.data32[5] << 32) |
			(u64)event->data.data32[4];

		trace_vi_frame_begin(
			ts,
			event->data.data32[2],
			event->data.data32[0],
			event->data.data32[1],
			pdata->class
		);
		break;
	case camrtc_trace_vi_frame_end:
		ts = ((u64)event->data.data32[5] << 32) |
			(u64)event->data.data32[4];

		trace_vi_frame_end(
			ts,
			event->data.data32[2],
			event->data.data32[0],
			event->data.data32[1],
			pdata->class
		);
		trace_task_fence(
			NVDEV_FENCE_KIND_POST,
			VI_CLASS_ID,
			event->data.data32[0],
			event->data.data32[1],
			NVDEV_FENCE_TYPE_SYNCPT,
			event->data.data32[0],
			event->data.data32[1],
			0, 0, 0, 0, 0
		);
		break;
	default:
		pr_warn("%pS invalid event id %d\n",
			__func__, event->header.id);
		break;
	}
}

/**
 * @brief Trace VI events
 *
 * This function traces VI events based on the event ID.
 *  - If the event ID is @ref camrtc_trace_vi_frame_begin or @ref camrtc_trace_vi_frame_end,
 *    it prints the VI frame event using @ref rtcpu_trace_vi_frame_event()
 *  - If the event ID is not found, it prints the unknown trace event using
 *    @ref rtcpu_unknown_trace_event()
 *
 * @param[in] tracer Pointer to the tegra_rtcpu_trace structure.
 * 						Valid Range: Non-NULL pointer.
 * @param[in] event Pointer to the camrtc_event_struct structure.
 * 						Valid Range: Non-NULL pointer.
 */
static void rtcpu_trace_vi_event(struct tegra_rtcpu_trace *tracer,
				struct camrtc_event_struct *event)
{
	switch (event->header.id) {
	case camrtc_trace_vi_frame_begin:
	case camrtc_trace_vi_frame_end:
		rtcpu_trace_vi_frame_event(tracer, event);
		break;
	default:
		rtcpu_unknown_trace_event(event);
		break;
	}
}

const char * const g_trace_isp_falcon_task_strs[] = {
	"UNUSED",
	"SCHED_ERROR",
	"SCHED_HANDLE_STAT",
	"SCHED_FINISH_TILE",
	"SCHED_FINISH_SLICE",
	"HANDLE_EVENT",
	"INPUT_ACTION",
	"ISR"
};

const unsigned int g_trace_isp_falcon_task_str_count =
	ARRAY_SIZE(g_trace_isp_falcon_task_strs);

#define TRACE_ISP_FALCON_EVENT_TS         13U
#define TRACE_ISP_FALCON_EVENT_TE         14U
#define TRACE_ISP_FALCON_PROFILE_START    16U
#define TRACE_ISP_FALCON_PROFILE_END      17U

/**
 * @brief Trace ISP task events
 *
 * This function traces ISP task events based on the event ID.
 *  - It handles different ISP task event types and their corresponding trace functions.
 *  - If event ID is @ref camrtc_trace_isp_task_begin, it prints the ISP task begin trace event
 *    using @ref trace_isp_task_begin() and @ref trace_task_fence()
 *  - If event ID is @ref camrtc_trace_isp_task_end, it prints the ISP task end trace event using
 *    @ref trace_isp_task_end() and @ref trace_task_fence()
 *
 * @param[in] tracer Pointer to the tegra_rtcpu_trace structure.
 * 						Valid Range: Non-NULL pointer.
 * @param[in] event Pointer to the camrtc_event_struct structure.
 * 						Valid Range: Non-NULL pointer.
 */
static void rtcpu_trace_isp_task_event(struct tegra_rtcpu_trace *tracer,
	struct camrtc_event_struct *event)
{
	struct nvhost_device_data *pdata = NULL;
	struct platform_device *pdev = NULL;
	u32 isp_unit_id = event->data.data32[4];

	if (isp_unit_id == 0U)
		pdev = tracer->isp_platform_device;
	else if (isp_unit_id == 1U)
		pdev = tracer->isp1_platform_device;
	else
		pdev = NULL;

	if (pdev == NULL)
		return;

	pdata = platform_get_drvdata(pdev);
	if (pdata == NULL)
		return;

	switch (event->header.id) {
	case camrtc_trace_isp_task_begin:
		trace_isp_task_begin(
			event->header.tstamp,
			event->data.data32[2],
			event->data.data32[0],
			event->data.data32[1],
			pdata->class
		);
		trace_task_fence(
			NVDEV_FENCE_KIND_PRE,
			ISP_CLASS_ID,
			event->data.data32[0],
			event->data.data32[1],
			NVDEV_FENCE_TYPE_SYNCPT,
			event->data.data32[0],
			event->data.data32[1],
			0, 0, 0, 0, 0
		);
		break;
	case camrtc_trace_isp_task_end:
		trace_isp_task_end(
			event->header.tstamp,
			event->data.data32[2],
			event->data.data32[0],
			event->data.data32[1],
			pdata->class
		);
		trace_task_fence(
			NVDEV_FENCE_KIND_POST,
			ISP_CLASS_ID,
			event->data.data32[0],
			event->data.data32[1],
			NVDEV_FENCE_TYPE_SYNCPT,
			event->data.data32[0],
			event->data.data32[1],
			0, 0, 0, 0, 0
		);
		break;
	default:
		pr_warn("%pS invalid event id %d\n",
			__func__, event->header.id);
		break;
	}
}

/**
 * @brief Trace ISP Falcon events
 *
 * This function traces ISP Falcon events based on the event ID.
 *  - It handles different ISP Falcon event types and their corresponding trace functions.
 *  - If event ID is @ref TRACE_ISP_FALCON_EVENT_TS, it prints the ISP Falcon tile start trace
 *    event using @ref trace_rtcpu_isp_falcon_tile_start()
 *  - If event ID is @ref TRACE_ISP_FALCON_EVENT_TE, it prints the ISP Falcon tile end trace event
 *    using @ref trace_rtcpu_isp_falcon_tile_end()
 *  - If event ID is @ref TRACE_ISP_FALCON_PROFILE_START, it prints the ISP Falcon task start trace
 *    event using @ref trace_rtcpu_isp_falcon_task_start()
 *  - If event ID is @ref TRACE_ISP_FALCON_PROFILE_END, it prints the ISP Falcon task end trace
 *    event using @ref trace_rtcpu_isp_falcon_task_end()
 *  - If event ID is not found, it prints the unknown trace event using
 *    @ref trace_rtcpu_isp_falcon()
 *
 * @param[in] event Pointer to the camrtc_event_struct structure.
 * 						Valid Range: Non-NULL pointer.
 */
static void rtcpu_trace_isp_falcon_event(struct camrtc_event_struct *event)
{
	u8 ispfalcon_tag = (u8) ((event->data.data32[0] & 0xFF) >> 1U);
	u8 ch = (u8) ((event->data.data32[0] & 0xFF00) >> 8U);
	u8 seq = (u8) ((event->data.data32[0] & 0xFF0000) >> 16U);
	u32 low_bits_ts = event->data.data32[1];
	u32 high_bits_ts = event->data.data32[2];
	u64 tstamp = ((u64)high_bits_ts << 32U) | low_bits_ts;
	u32 isp_unit_id = event->data.data32[5];

	switch (ispfalcon_tag) {
	case TRACE_ISP_FALCON_EVENT_TS:
		trace_rtcpu_isp_falcon_tile_start(
			ch, seq, tstamp, isp_unit_id,
			(u8) (event->data.data32[4] & 0xFF),
			(u8) ((event->data.data32[4] & 0xFF00) >> 8U),
			(u16) (event->data.data32[3] & 0xFFFF),
			(u16) ((event->data.data32[3] & 0xFFFF0000) >> 16U));
		break;
	case TRACE_ISP_FALCON_EVENT_TE:
		trace_rtcpu_isp_falcon_tile_end(
			ch, seq, tstamp, isp_unit_id,
			(u8) (event->data.data32[4] & 0xFF),
			(u8) ((event->data.data32[4] & 0xFF00) >> 8U));
		break;
	case TRACE_ISP_FALCON_PROFILE_START:
		trace_rtcpu_isp_falcon_task_start(
			ch, tstamp, isp_unit_id,
			event->data.data32[3]);
		break;
	case TRACE_ISP_FALCON_PROFILE_END:
		trace_rtcpu_isp_falcon_task_end(
			tstamp, isp_unit_id,
			event->data.data32[3]);
		break;
	default:
		trace_rtcpu_isp_falcon(
			ispfalcon_tag, ch, seq, tstamp, isp_unit_id,
			event->data.data32[3],
			event->data.data32[4]);
		break;
	}

}

/**
 * @brief Trace ISP events
 *
 * This function traces ISP events based on the event ID.
 *  - If the event ID is @ref camrtc_trace_isp_task_begin or @ref camrtc_trace_isp_task_end,
 *    it prints the ISP task event using @ref rtcpu_trace_isp_task_event()
 *  - If the event ID is @ref camrtc_trace_isp_falcon_traces_event, it prints the ISP Falcon
 *    traces event using @ref rtcpu_trace_isp_falcon_event()
 *  - If the event ID is not found, it prints the unknown trace event using
 *    @ref rtcpu_unknown_trace_event()
 *
 * @param[in] tracer Pointer to the tegra_rtcpu_trace structure.
 * 						Valid Range: Non-NULL pointer.
 * @param[in] event Pointer to the camrtc_event_struct structure.
 * 						Valid Range: Non-NULL pointer.
 */
static void rtcpu_trace_isp_event(struct tegra_rtcpu_trace *tracer,
	struct camrtc_event_struct *event)
{
	switch (event->header.id) {
	case camrtc_trace_isp_task_begin:
	case camrtc_trace_isp_task_end:
		rtcpu_trace_isp_task_event(tracer, event);
		break;
	case camrtc_trace_isp_falcon_traces_event:
		rtcpu_trace_isp_falcon_event(event);
		break;
	default:
		rtcpu_unknown_trace_event(event);
		break;
	}
}

const char * const g_trace_nvcsi_intr_class_strs[] = {
	"GLOBAL",
	"CORRECTABLE_ERR",
	"UNCORRECTABLE_ERR",
};
const unsigned int g_trace_nvcsi_intr_class_str_count =
	ARRAY_SIZE(g_trace_nvcsi_intr_class_strs);

const char * const g_trace_nvcsi_intr_type_strs[] = {
	"SW_DEBUG",
	"HOST1X",
	"PHY_INTR", "PHY_INTR0", "PHY_INTR1",
	"STREAM_NOVC", "STREAM_VC",
};
const unsigned int g_trace_nvcsi_intr_type_str_count =
	ARRAY_SIZE(g_trace_nvcsi_intr_type_strs);

/**
 * @brief Trace NVCSI events
 *
 * This function traces NVCSI events based on the event ID.
 *  - If the event ID is @ref camrtc_trace_nvcsi_intr, it prints the NVCSI interrupt trace
 *    event using @ref trace_rtcpu_nvcsi_intr()
 *  - If the event ID is not found, it prints the unknown trace event using
 *    @ref rtcpu_unknown_trace_event()
 *
 * @param[in] event Pointer to the camrtc_event_struct structure.
 * 						Valid Range: Non-NULL pointer.
 */
static void rtcpu_trace_nvcsi_event(struct camrtc_event_struct *event)
{
	u64 ts_tsc = ((u64)event->data.data32[5] << 32) |
			(u64)event->data.data32[4];

	switch (event->header.id) {
	case camrtc_trace_nvcsi_intr:
		trace_rtcpu_nvcsi_intr(ts_tsc,
			(event->data.data32[0] & 0xff),
			(event->data.data32[1] & 0xff),
			event->data.data32[2],
			event->data.data32[3]);
		break;
	default:
		rtcpu_unknown_trace_event(event);
		break;
	}
}

struct capture_event_progress {
	uint32_t channel_id;
	uint32_t sequence;
};

struct capture_event_isp {
	uint32_t channel_id;
	uint32_t prog_sequence;
	uint32_t cap_sequence;
	uint8_t isp_settings_id;
	uint8_t vi_channel_id;
	uint8_t pad_[2];
};

struct capture_event {
	union {
		struct capture_event_progress progress;
		struct capture_event_isp isp;
		bool suspend;
	};
};

/**
 * @brief Trace capture events
 *
 * This function traces capture events based on the event ID.
 *  - If the event ID is @ref camrtc_trace_capture_event_sof, it prints the capture start of frame
 *    trace event using @ref trace_capture_event_sof()
 *  - If the event ID is @ref camrtc_trace_capture_event_eof, it prints the capture end of frame
 *    trace event using @ref trace_capture_event_eof()
 *  - If the event ID is @ref camrtc_trace_capture_event_error, it prints the capture error trace
 *    event using @ref trace_capture_event_error()
 *  - If the event ID is @ref camrtc_trace_capture_event_reschedule, it prints the capture
 *    reschedule trace event using @ref trace_capture_event_reschedule()
 *  - If the event ID is @ref camrtc_trace_capture_event_reschedule_isp, it prints the capture
 *    reschedule ISP trace event using @ref trace_capture_event_reschedule_isp()
 *  - If the event ID is @ref camrtc_trace_capture_event_isp_done, it prints the capture ISP
 *    done trace event using @ref trace_capture_event_isp_done()
 *  - If the event ID is @ref camrtc_trace_capture_event_isp_error, it prints the capture ISP error
 *    trace event using @ref trace_capture_event_isp_error()
 *  - If the event ID is @ref camrtc_trace_capture_event_wdt, it prints the capture WDT trace
 *    event using @ref trace_capture_event_wdt()
 *  - If the event ID is @ref camrtc_trace_capture_event_report_program, it prints the capture
 *    report program trace event using @ref trace_capture_event_report_program()
 *  - If the event ID is @ref camrtc_trace_capture_event_suspend, it prints the capture suspend
 *    trace event using @ref trace_capture_event_suspend()
 *  - If the event ID is @ref camrtc_trace_capture_event_suspend_isp, it prints the capture suspend
 *    ISP trace event using @ref trace_capture_event_suspend_isp()
 *  - If the event ID is @ref camrtc_trace_capture_event_inject or
 *    @ref camrtc_trace_capture_event_sensor or unknown, it prints the unknown trace event using
 *    @ref rtcpu_unknown_trace_event()
 *
 * @param[in] event Pointer to the camrtc_event_struct structure.
 * 						Valid Range: Non-NULL pointer.
 */
static void rtcpu_trace_capture_event(struct camrtc_event_struct *event)
{
	const struct capture_event *ev = (const void *)&event->data;

	switch (event->header.id) {
	case camrtc_trace_capture_event_sof:
		trace_capture_event_sof(event->header.tstamp,
				ev->progress.channel_id, ev->progress.sequence);
		break;
	case camrtc_trace_capture_event_eof:
		trace_capture_event_eof(event->header.tstamp,
				ev->progress.channel_id, ev->progress.sequence);
		break;
	case camrtc_trace_capture_event_error:
		trace_capture_event_error(event->header.tstamp,
				ev->progress.channel_id, ev->progress.sequence);
		break;
	case camrtc_trace_capture_event_reschedule:
		trace_capture_event_reschedule(event->header.tstamp,
				ev->progress.channel_id, ev->progress.sequence);
		break;
	case camrtc_trace_capture_event_reschedule_isp:
		trace_capture_event_reschedule_isp(event->header.tstamp,
			ev->isp.channel_id, ev->isp.cap_sequence, ev->isp.prog_sequence,
			ev->isp.isp_settings_id, ev->isp.vi_channel_id);
		break;
	case camrtc_trace_capture_event_isp_done:
		trace_capture_event_isp_done(event->header.tstamp,
			ev->isp.channel_id, ev->isp.cap_sequence, ev->isp.prog_sequence,
			ev->isp.isp_settings_id, ev->isp.vi_channel_id);
		break;
	case camrtc_trace_capture_event_isp_error:
		trace_capture_event_isp_error(event->header.tstamp,
			ev->isp.channel_id, ev->isp.cap_sequence, ev->isp.prog_sequence,
			ev->isp.isp_settings_id, ev->isp.vi_channel_id);
		break;
	case camrtc_trace_capture_event_wdt:
		trace_capture_event_wdt(event->header.tstamp);
		break;
	case camrtc_trace_capture_event_report_program:
		trace_capture_event_report_program(event->header.tstamp,
				ev->progress.channel_id, ev->progress.sequence);
		break;
	case camrtc_trace_capture_event_suspend:
		trace_capture_event_suspend(event->header.tstamp, ev->suspend);
		break;
	case camrtc_trace_capture_event_suspend_isp:
		trace_capture_event_suspend_isp(event->header.tstamp, ev->suspend);
		break;

	case camrtc_trace_capture_event_inject:
	case camrtc_trace_capture_event_sensor:
	default:
		rtcpu_unknown_trace_event(event);
		break;
	}
}

/**
 * @brief Trace performance events
 *
 * This function traces performance events based on the event ID.
 *  - If the event ID is @ref camrtc_trace_perf_reset, it prints the performance reset trace
 *    event using @ref trace_rtcpu_perf_reset()
 *  - If the event ID is @ref camrtc_trace_perf_counters, it prints the performance counters
 *    trace event using @ref trace_rtcpu_perf_counters()
 *  - If the event ID is not found, it prints the unknown trace event using
 *    @ref rtcpu_unknown_trace_event()
 *
 * @param[in] event Pointer to the camrtc_event_struct structure.
 * 						Valid Range: Non-NULL pointer.
 */
static void rtcpu_trace_perf_event(struct camrtc_event_struct *event)
{
	const struct camrtc_trace_perf_counter_data *perf = (const void *)&event->data;

	switch (event->header.id) {
	case camrtc_trace_perf_reset:
		trace_rtcpu_perf_reset(event->header.tstamp, perf);
		break;
	case camrtc_trace_perf_counters:
		trace_rtcpu_perf_counters(event->header.tstamp, perf);
		break;

	default:
		rtcpu_unknown_trace_event(event);
		break;
	}
}

/**
 * @brief Trace array events
 *
 * This function traces array events based on the event ID.
 *  - If the event ID is @ref CAMRTC_EVENT_MODULE_BASE, it parses the base event using
 *    @ref rtcpu_trace_base_event()
 *  - If the event ID is @ref CAMRTC_EVENT_MODULE_RTOS, it parses the RTOS event using
 *    @ref rtcpu_trace_rtos_event()
 *  - If the event ID is @ref CAMRTC_EVENT_MODULE_DBG, it parses the debug event using
 *    @ref rtcpu_trace_dbg_event()
 *  - If the event ID is @ref CAMRTC_EVENT_MODULE_VINOTIFY, it parses the VINOTIFY event
 *    using @ref rtcpu_trace_vinotify_event()
 *  - If the event ID is @ref CAMRTC_EVENT_MODULE_I2C, it parses the I2C event using
 *    @ref rtcpu_trace_i2c_event()
 *  - If the event ID is @ref CAMRTC_EVENT_MODULE_VI, it parses the VI event using
 *    @ref rtcpu_trace_vi_event()
 *  - If the event ID is @ref CAMRTC_EVENT_MODULE_ISP, it parses the ISP event using
 *    @ref rtcpu_trace_isp_event()
 *  - If the event ID is @ref CAMRTC_EVENT_MODULE_NVCSI, it parses the NVCSI event using
 *    @ref rtcpu_trace_nvcsi_event()
 *  - If the event ID is @ref CAMRTC_EVENT_MODULE_CAPTURE, it parses the capture event using
 *    @ref rtcpu_trace_capture_event()
 *  - If the event ID is @ref CAMRTC_EVENT_MODULE_PERF, it parses the performance event using
 *    @ref rtcpu_trace_perf_event()
 *  - If the event ID is not found, it prints the unknown trace event using
 *    @ref rtcpu_unknown_trace_event()
 *
 * @param[in] tracer Pointer to the tegra_rtcpu_trace structure.
 * 						Valid Range: Non-NULL pointer.
 * @param[in] event Pointer to the camrtc_event_struct structure.
 * 						Valid Range: Non-NULL pointer.
 */
static void rtcpu_trace_array_event(struct tegra_rtcpu_trace *tracer,
	struct camrtc_event_struct *event)
{
	switch (CAMRTC_EVENT_MODULE_FROM_ID(event->header.id)) {
	case CAMRTC_EVENT_MODULE_BASE:
		rtcpu_trace_base_event(event);
		break;
	case CAMRTC_EVENT_MODULE_RTOS:
		rtcpu_trace_rtos_event(event);
		break;
	case CAMRTC_EVENT_MODULE_DBG:
		rtcpu_trace_dbg_event(event);
		break;
	case CAMRTC_EVENT_MODULE_VINOTIFY:
		rtcpu_trace_vinotify_event(event);
		break;
	case CAMRTC_EVENT_MODULE_I2C:
		break;
	case CAMRTC_EVENT_MODULE_VI:
		rtcpu_trace_vi_event(tracer, event);
		break;
	case CAMRTC_EVENT_MODULE_ISP:
		rtcpu_trace_isp_event(tracer, event);
		break;
	case CAMRTC_EVENT_MODULE_NVCSI:
		rtcpu_trace_nvcsi_event(event);
		break;
	case CAMRTC_EVENT_MODULE_CAPTURE:
		rtcpu_trace_capture_event(event);
		break;
	case CAMRTC_EVENT_MODULE_PERF:
		rtcpu_trace_perf_event(event);
		break;
	default:
		rtcpu_unknown_trace_event(event);
		break;
	}
}

/**
 * @brief Trace log events
 *
 * This function traces log events.
 *  - If the id is not @ref camrtc_trace_type_string, it returns.
 *  - If the length is greater than @ref CAMRTC_TRACE_EVENT_PAYLOAD_SIZE, it ignores the NULs at
 *    the end of the buffer.
 *  - If the used is greater than the printk buffer size, it prints the log event using
 *    @ref pr_info()
 *  - Copies the log event to the printk buffer using @ref memcpy()
 *  - Updates the used with the length of the log event
 *  - Logs the log event using @ref tracer->printk()
 *  - If end is '\r' or '\n', it prints the log event using @ref tracer->printk()
 *  - Update @ref tracer->printk_used with the used
 *
 * @param[in/out] tracer Pointer to the tegra_rtcpu_trace structure.
 *                        Valid Range: Non-NULL pointer.
 * @param[in] id Event ID.
 *               Valid Range: @ref camrtc_trace_type_string.
 * @param[in] len Length of the log event.
 *                Valid Range: @ref CAMRTC_TRACE_EVENT_PAYLOAD_SIZE.
 * @param[in] data8 Pointer to the log event.
 *                  Valid Range: Non-NULL pointer.
 */
static void trace_rtcpu_log(struct tegra_rtcpu_trace *tracer,
		uint32_t id, uint32_t len, const uint8_t *data8)
{
	size_t used;

	if (unlikely(id != camrtc_trace_type_string))
		return;

	if (len >= CAMRTC_TRACE_EVENT_PAYLOAD_SIZE)
		/* Ignore NULs at the end of buffer */
		len = strnlen(data8, CAMRTC_TRACE_EVENT_PAYLOAD_SIZE);

	used = tracer->printk_used;

	if (unlikely(used + len > sizeof(tracer->printk))) {
		/* Too long concatenated message, print it out now */
		pr_info("%s %.*s\n", tracer->log_prefix,
			(int)used, tracer->printk);
		used = 0;
	}

	memcpy(tracer->printk + used, data8, len);

	used += len;

	if (likely(used > 0)) {
		char end = tracer->printk[used - 1];

		/*
		 * Some log entries from rtcpu consists of multiple
		 * messages.  If the string does not end with \r or
		 * \n, do not print it now but rather wait for the
		 * next piece.
		 */
		if (end == '\r' || end == '\n') {
			while (--used > 0) {
				end = tracer->printk[used - 1];
				if (!(end == '\r' || end == '\n'))
					break;
			}

			pr_info("%s %.*s\n", tracer->log_prefix,
				(int)used, tracer->printk);
			used = 0;
		}
	}

	tracer->printk_used = used;
}

/**
 * @brief Processes a single RTCPU trace event
 *
 * This function processes a single RTCPU trace event based on its type
 * - Gets the event id using @ref event->header.id
 * - Extracts the event type using @ref CAMRTC_EVENT_TYPE_FROM_ID
 * - Gets the event length using @ref rtcpu_trace_event_len
 * - Gets a pointer to the event data
 * - Based on event type, calls appropriate handler:
 *   - If type is @ref CAMRTC_EVENT_TYPE_ARRAY, calls @ref rtcpu_trace_array_event
 *   - If type is @ref CAMRTC_EVENT_TYPE_ARMV7_EXCEPTION, calls @ref trace_rtcpu_armv7_exception
 *   - If type is @ref CAMRTC_EVENT_TYPE_PAD, ignores the event
 *   - If type is @ref CAMRTC_EVENT_TYPE_START, calls @ref trace_rtcpu_start
 *   - If type is @ref CAMRTC_EVENT_TYPE_STRING, calls @ref trace_rtcpu_string and
 *     optionally @ref trace_rtcpu_log if printk is enabled
 *   - If type is @ref CAMRTC_EVENT_TYPE_BULK, calls @ref trace_rtcpu_bulk
 *   - For any other type, calls @ref rtcpu_unknown_trace_event
 *
 * @param[in] tracer  Pointer to the tegra_rtcpu_trace structure.
 *                    Valid Range: Non-NULL pointer.
 * @param[in] event   Pointer to the camrtc_event_struct structure.
 *                    Valid Range: Non-NULL pointer.
 */
static void rtcpu_trace_event(struct tegra_rtcpu_trace *tracer,
	struct camrtc_event_struct *event)
{
	uint32_t id = event->header.id;
	uint32_t type = CAMRTC_EVENT_TYPE_FROM_ID(id);
	uint16_t len = rtcpu_trace_event_len(event);
	uint8_t *data8 = &event->data.data8[0];

	switch (type) {
	case CAMRTC_EVENT_TYPE_ARRAY:
		rtcpu_trace_array_event(tracer, event);
		break;
	case CAMRTC_EVENT_TYPE_ARMV7_EXCEPTION:
		trace_rtcpu_armv7_exception(event->header.tstamp,
			event->data.data32[0]);
		break;
	case CAMRTC_EVENT_TYPE_PAD:
		/* ignore */
		break;
	case CAMRTC_EVENT_TYPE_START:
		trace_rtcpu_start(event->header.tstamp);
		break;
	case CAMRTC_EVENT_TYPE_STRING:
		trace_rtcpu_string(event->header.tstamp, id, len, (char *)data8);
		if (likely(tracer->enable_printk))
			trace_rtcpu_log(tracer, id, len, data8);
		break;
	case CAMRTC_EVENT_TYPE_BULK:
		trace_rtcpu_bulk(event->header.tstamp, id, len, data8);
		break;
	default:
		rtcpu_unknown_trace_event(event);
		break;
	}
}

/**
 * @brief Processes RTCPU trace events
 *
 * This function processes multiple RTCPU trace events
 * - Gets the memory header from @ref tracer->trace_memory
 * - Gets the old and new next index values
 * - Checks if the new next index is valid (less than @ref tracer->event_entries)
 * - Uses @ref array_index_nospec to validate the new next index
 * - If old and new indices are the same, returns (no new events)
 * - Wakes up polling processes using @ref wake_up_all
 * - Invalidates cache entries using @ref rtcpu_trace_invalidate_entries
 * - Processes events in the range between old and new indices:
 *   - Uses @ref array_index_nospec to validate the old next index
 *   - Gets a pointer to the event
 *   - Processes the event using @ref rtcpu_trace_event
 *   - Increments the events counter using @ref wrap_add_u32
 *   - Increments the old next index using @ref wrap_add_u32
 *   - Handles wraparound of the index at the end of the buffer
 * - Updates @ref tracer->event_last_idx with the new next index
 * - Makes a copy of the last event
 *
 * @param[in/out] tracer  Pointer to the tegra_rtcpu_trace structure.
 *                        Valid Range: Non-NULL pointer.
 */
static inline void rtcpu_trace_events(struct tegra_rtcpu_trace *tracer)
{
	const struct camrtc_trace_memory_header *header = tracer->trace_memory;
	u32 old_next = tracer->event_last_idx;
	u32 new_next = header->event_next_idx;
	struct camrtc_event_struct *event, *last_event;

	if (new_next >= tracer->event_entries) {
		dev_warn_ratelimited(tracer->dev,
			"trace entry %u outside range 0..%u\n",
			new_next, tracer->event_entries - 1);
		return;
	}

	new_next = array_index_nospec(new_next, tracer->event_entries);

	if (old_next == new_next)
		return;

	/* Wake up any polling process waiting for data */
	wake_up_all(&tracer->wait_queue);

	rtcpu_trace_invalidate_entries(tracer,
				tracer->dma_handle_events,
				old_next, new_next,
				CAMRTC_TRACE_EVENT_SIZE,
				tracer->event_entries);

	/* pull events */
	while (old_next != new_next) {
		old_next = array_index_nospec(old_next, tracer->event_entries);
		event = &tracer->events[old_next];
		last_event = event;
		rtcpu_trace_event(tracer, event);
		tracer->n_events = wrap_add_u32(tracer->n_events, 1U);
		old_next = wrap_add_u32(old_next, 1U);
		if (old_next == tracer->event_entries)
			old_next = 0;
	}

	tracer->event_last_idx = new_next;
	tracer->copy_last_event = *last_event;
}

/**
 * @brief Processes RTCPU snapshot trace events from shared memory.
 *
 * This function reads and processes snapshot trace events recorded by the RTCPU
 * into a dedicated circular buffer in shared memory. Similar to rtcpu_trace_events
 * it assumes the caller to lock the mutex.
 *
 * - Reads the current snapshot write index (@ref snapshot_next_idx) from the
 *   shared memory header.
 * - Compares it with the last processed index (@ref tracer->snapshot_last_idx)
 *   to find new events.
 * - Returns early if the tracer is NULL or no new events are found.
 * - Validates the new index using @ref array_index_nospec.
 * - Invalidates CPU cache for the relevant memory range using
 *   @ref rtcpu_trace_invalidate_entries to ensure visibility of RTCPU writes.
 * - Iterates through new events from the old index up to (but not including)
 *   the new index, handling potential buffer wrap-around.
 * - For each event:
 *   - Validates the index using @ref array_index_nospec.
 *   - Gets a pointer to the event structure in the snapshot buffer.
 *   - Processes the event using @ref rtcpu_trace_event.
 *   - Increments the snapshot event counter (@ref tracer->n_snapshots).
 *   - Advances the index, handling wrap-around.
 * - Updates the last processed snapshot index (@ref tracer->snapshot_last_idx).
 *
 * @param[in/out] tracer  Pointer to the tegra_rtcpu_trace structure.
 *                        Valid Range: Non-NULL pointer.
 */
static inline void rtcpu_trace_snapshot(struct tegra_rtcpu_trace *tracer)
{
	const struct camrtc_trace_memory_header *header = NULL;
	u32 old_next = 0U;
	u32 new_next = 0U;
	struct camrtc_event_struct *event = NULL;

	if (tracer == NULL)
		return;

	header = tracer->trace_memory;
	old_next = tracer->snapshot_last_idx;
	new_next = header->snapshot_next_idx;

	if (new_next >= tracer->snapshot_entries) {
		dev_warn_ratelimited(tracer->dev,
			"trace entry %u outside range 0..%u\n",
			new_next, tracer->snapshot_entries - 1);
		return;
	}

	new_next = array_index_nospec(new_next, tracer->snapshot_entries);

	if (old_next == new_next)
		return;

	rtcpu_trace_invalidate_entries(tracer,
				tracer->dma_handle_snapshots,
				old_next, new_next,
				CAMRTC_TRACE_EVENT_SIZE,
				tracer->snapshot_entries);

	while (old_next != new_next) {
		old_next = array_index_nospec(old_next, tracer->snapshot_entries);
		event = &tracer->snapshot_events[old_next];
		rtcpu_trace_event(tracer, event);
		tracer->n_snapshots = wrap_add_u32(tracer->n_snapshots, 1U);
		old_next = wrap_add_u32(old_next, 1U);
		if (old_next == tracer->snapshot_entries)
			old_next = 0;
	}

	tracer->snapshot_last_idx = new_next;
}

/**
 * @brief Flushes the RTCPU trace buffer
 *
 * This function flushes the RTCPU trace buffer by processing all available trace events
 * - Checks if @ref tracer is NULL, returns if it is
 * - Locks the tracer mutex using @ref mutex_lock
 * - Invalidates the cache line for pointers using @ref dma_sync_single_for_cpu
 * - Processes exceptions using @ref rtcpu_trace_exceptions
 * - Processes events using @ref rtcpu_trace_events
 * - If panic_flag was 1, atomically set it to 0 by calling @ref atomic_cmpxchg
 *   and take a snapshot by calling @ref rtcpu_trace_snapshot
 * - Unlocks the mutex using @ref mutex_unlock
 *
 * @param[in] tracer  Pointer to the tegra_rtcpu_trace structure.
 *                    Valid Range: NULL or valid pointer.
 */
void tegra_rtcpu_trace_flush(struct tegra_rtcpu_trace *tracer)
{
	if (tracer == NULL)
		return;

	mutex_lock(&tracer->lock);

	/* invalidate the cache line for the pointers */
	dma_sync_single_for_cpu(tracer->dev, tracer->dma_handle_pointers,
	    CAMRTC_TRACE_NEXT_IDX_SIZE, DMA_FROM_DEVICE);

	/* process exceptions and events */
	rtcpu_trace_exceptions(tracer);
	rtcpu_trace_events(tracer);

	if (atomic_cmpxchg(&tracer->panic_flag, 1, 0) == 1) {
		rtcpu_trace_snapshot(tracer);
	}

	mutex_unlock(&tracer->lock);
}
EXPORT_SYMBOL(tegra_rtcpu_trace_flush);

/**
 * @brief Worker function for periodic trace processing
 *
 * This function is executed periodically as a delayed work item
 * - Retrieves the tracer structure using @ref container_of
 * - Flushes the trace buffer using @ref tegra_rtcpu_trace_flush
 * - Reschedules itself using @ref schedule_delayed_work
 *
 * @param[in] work  Pointer to the work_struct within the tracer.
 *                  Valid Range: Non-NULL pointer.
 */
static void rtcpu_trace_worker(struct work_struct *work)
{
	struct tegra_rtcpu_trace *tracer;

	tracer = container_of(work, struct tegra_rtcpu_trace, work.work);

	tegra_rtcpu_trace_flush(tracer);

	/* reschedule */
	schedule_delayed_work(&tracer->work, tracer->work_interval_jiffies);
}

/**
 * @brief Implementation for reading raw trace events
 *
 * This function implements the raw trace reading mechanism
 * - Gets the memory header from @ref tracer->trace_memory
 * - Gets the old and new next index values
 * - Validates that the new next index is within range
 * - Uses @ref array_index_nospec to validate indices
 * - If old and new indices are the same, returns (no new events)
 * - Invalidates cache entries using @ref rtcpu_trace_invalidate_entries
 * - Determines if buffer has wrapped around
 * - Calculates number of events to copy based on available events and requested amount
 * - Checks for multiplication overflow using @ref check_mul_overflow
 * - Handles copying events to user space with or without buffer wraparound:
 *   - For non-wrapped buffer, copies events directly using @ref copy_to_user
 *   - For wrapped buffer, copies events in two parts using @ref copy_to_user
 *     - First part from old_next to end of buffer
 *     - Second part from beginning of buffer
 * - Updates the last read event index
 * - Updates the number of events copied
 *
 * @param[in] tracer Pointer to the tegra_rtcpu_trace structure.
 *                   Valid Range: Non-NULL pointer.
 * @param[in] user_buffer User space buffer to copy events to.
 *                   Valid Range: Valid user space pointer.
 * @param[in,out] events_copied Pointer to number of events already copied.
 *                   Valid Range: Non-NULL pointer.
 * @param[in,out] last_read_event_idx Pointer to the last read event index.
 *                   Valid Range: Non-NULL pointer.
 * @param[in] num_events_requested Number of events requested to be read.
 *                   Valid Range: > 0.
 *
 * @retval 0 Success
 * @retval -EIO Invalid trace entry
 * @retval -EFAULT Error copying data to user space or overflow
 */
static int32_t raw_trace_read_impl(
	struct tegra_rtcpu_trace *tracer,
	char __user *user_buffer,
	ssize_t *events_copied,
	uint32_t *last_read_event_idx,
	const u32 num_events_requested)
{
	bool buffer_wrapped;
	const struct camrtc_trace_memory_header *header = tracer->trace_memory;

	u32 old_next = *last_read_event_idx;

	u32 new_next = header->event_next_idx;

	uint32_t num_events_to_copy;

	int64_t mul_value = 0;

	if (new_next >= tracer->event_entries) {
		dev_warn_ratelimited(tracer->dev,
			"trace entry %u outside range 0..%u\n",
			new_next, tracer->event_entries - 1);
		return -EIO;
	}

	new_next = array_index_nospec(new_next, tracer->event_entries);
	old_next = array_index_nospec(old_next, tracer->event_entries);
	if (old_next == new_next)
		return 0;

	rtcpu_trace_invalidate_entries(
		tracer,
		tracer->dma_handle_events,
		old_next, new_next,
		CAMRTC_TRACE_EVENT_SIZE,
		tracer->event_entries);

	buffer_wrapped = (new_next < old_next);

	num_events_to_copy =
		(!buffer_wrapped) ?
			(new_next - old_next) : (tracer->event_entries - old_next + new_next);
	num_events_to_copy =
		num_events_requested > num_events_to_copy ?
			num_events_to_copy : num_events_requested;

	if (unlikely(check_mul_overflow((int64_t)(*events_copied),
		(int64_t)(sizeof(struct camrtc_event_struct)),
		&mul_value))) {
		dev_err(tracer->dev,
				"%s:events_copied failed due to an overflow\n", __func__);
		return -EFAULT;
	}

	/* No wrap around */
	if (!buffer_wrapped) {
		if (copy_to_user(
			&user_buffer[mul_value],
			&tracer->events[old_next],
			num_events_to_copy * sizeof(struct camrtc_event_struct))) {
			return -EFAULT;
		}
	}
	/* Handling the buffer's circular wrap around */
	else {
		u32 first_part;
		u32 second_part;

		/* copy from old_next to the end of buffer
		 * or till max number of events that can be copied.
		 */
		first_part = tracer->event_entries - old_next;

		if (first_part > num_events_to_copy)
			first_part = num_events_to_copy;

		if (copy_to_user(
			&user_buffer[mul_value],
			&tracer->events[old_next],
			first_part * sizeof(struct camrtc_event_struct)))
			return -EFAULT;

		/* for wrap around usecase, copy from buffer's beginning */
		second_part = num_events_to_copy - first_part;

		if (second_part > 0)
			if (copy_to_user(
				&user_buffer[
				(*events_copied + first_part) * sizeof(struct camrtc_event_struct)],
				&tracer->events[0],
				second_part * sizeof(struct camrtc_event_struct)))
				return -EFAULT;
	}

	*last_read_event_idx = (old_next + num_events_to_copy) % tracer->event_entries;
	*events_copied += num_events_to_copy;

	return 0;
}

/**
 * @brief Checks if new trace events are available for reading
 *
 * This function determines if there are new events available since the last read
 * - Gets the last read event index from @ref fd_context
 * - Gets the memory header from @ref tracer->trace_memory
 * - For first read call, handles special case when buffer has wrapped:
 *   - If @ref header->wrapped_counter is greater than 0, sets read index to next position
 *   - Handles wraparound at the end of the buffer
 * - Compares the current event index with last read index to determine if new events exist
 *
 * @param[in] fd_context Pointer to the rtcpu_raw_trace_context structure.
 *                      Valid Range: Non-NULL pointer.
 * @param[in] tracer    Pointer to the tegra_rtcpu_trace structure.
 *                      Valid Range: Non-NULL pointer.
 *
 * @retval true  New events are available
 * @retval false No new events are available
 */
static bool check_event_availability(
	struct rtcpu_raw_trace_context *fd_context,
	struct tegra_rtcpu_trace *tracer)
{
	bool ret;
	u32 last_read_event_idx;
	struct camrtc_trace_memory_header *header;

	last_read_event_idx = fd_context->raw_trace_last_read_event_idx;

	header = tracer->trace_memory;

	/* If buffer has already wrapped around before the 1st read */
	if (unlikely(fd_context->first_read_call)) {
		if (header->wrapped_counter > 0) {
			last_read_event_idx = header->event_next_idx + 1;
			if (last_read_event_idx == tracer->event_entries)
				last_read_event_idx = 0;
		}
	}

	/* check if new event on worker thread is relavant for current reader */
	ret = header->event_next_idx != last_read_event_idx;

	return ret;
}

/**
 * @brief Implements the read file operation for the RTCPU raw trace device
 *
 * This function reads trace events from the RTCPU trace buffer into a user-supplied buffer
 *  - Gets the file descriptor context from @ref file->private_data
 *  - Validates file context and tracer, returns error if either is invalid
 *  - Handles special case for first read when buffer has wrapped around
 *  - Truncates requested buffer size if it exceeds @ref MAX_READ_SIZE
 *  - Calculates number of requested events based on buffer size
 *  - Validates user buffer address using @ref access_ok
 *  - Reads events using @ref raw_trace_read_impl in a loop:
 *    - For non-blocking calls, performs a single read attempt
 *    - For blocking calls, waits for events using @ref wait_event_interruptible
 *      and @ref check_event_availability until requested number of events is read
 *  - Updates the file context with last read event index
 *  - Calculates total bytes read using @ref check_mul_overflow
 *
 * @param[in] file         Pointer to the file structure.
 *                         Valid Range: Non-NULL pointer.
 * @param[out] user_buffer User space buffer to copy events to.
 *                         Valid Range: Valid user space pointer.
 * @param[in] buffer_size  Size of the user buffer in bytes.
 *                         Valid Range: > sizeof(struct camrtc_event_struct).
 * @param[in,out] ppos     Pointer to the file position (ignored).
 *                         Valid Range: Any.
 *
 * @retval >0 Number of bytes successfully read
 * @retval -ENODEV File descriptor context or tracer not set
 * @retval -ENOMEM Requested buffer size too small for even one event
 * @retval -EINVAL Invalid user buffer address or multiplication overflow
 * @retval (int) Return code propagated from raw_trace_read_impl or wait_event_interruptible
 */
static ssize_t
rtcpu_raw_trace_read(struct file *file, char __user *user_buffer, size_t buffer_size, loff_t *ppos)
{
	struct tegra_rtcpu_trace *tracer;
	u32 last_read_event_idx;
	u32 num_events_requested;
	struct camrtc_trace_memory_header *header;
	ssize_t events_copied = 0;
	ssize_t events_amount = 0;

	bool blocking_call = !(file->f_flags & O_NONBLOCK);

	struct rtcpu_raw_trace_context *fd_context = file->private_data;

	if (!fd_context) {
		pr_err("file descriptor context is not set in private data\n");
		return -ENODEV;
	}

	tracer = fd_context->tracer;

	if (!tracer) {
		pr_err("Tracer is not set in file descriptor context\n");
		return -ENODEV;
	}

	last_read_event_idx = fd_context->raw_trace_last_read_event_idx;

	header = tracer->trace_memory;

	/* If buffer has already wrapped around before the 1st read */
	if (unlikely(fd_context->first_read_call)) {
		if (header->wrapped_counter > 0) {
			last_read_event_idx = header->event_next_idx + 1;
			if (last_read_event_idx == tracer->event_entries)
				last_read_event_idx = 0;
		}
		fd_context->first_read_call = false;
	}

	/* Truncate buffer_size if it exceeds the maximum read size */
	if (buffer_size > MAX_READ_SIZE) {
		dev_dbg(tracer->dev,
				"Requested read size too large, truncating to %zd\n", MAX_READ_SIZE);
		buffer_size = MAX_READ_SIZE;
	}

	num_events_requested = buffer_size / sizeof(struct camrtc_event_struct);

	if (num_events_requested == 0) {
		dev_dbg(tracer->dev, "Invalid buffer size\n");
		return -ENOMEM;
	}

	/* Validate if user buffer is a valid address */
	if (!access_ok(user_buffer, buffer_size)) {
		dev_err(tracer->dev, "Invalid user buffer address\n");
		return -EINVAL;
	}

	do {
		int32_t ret = raw_trace_read_impl(
			tracer,
			user_buffer,
			&events_copied,
			&last_read_event_idx,
			num_events_requested - events_copied);

		if (ret < 0) {
			dev_err(tracer->dev, "Call to raw_trace_read_impl() failed.\n");
			return ret;
		}

		if (!blocking_call)
			break;

		/* Wait indefinitely until event is not available */
		ret = wait_event_interruptible(
				tracer->wait_queue,
				check_event_availability(fd_context, tracer));
		if (ret < 0)
			return ret;

	} while (events_copied < num_events_requested);

	fd_context->raw_trace_last_read_event_idx = last_read_event_idx;
	file->private_data = fd_context;

	if (check_mul_overflow(events_copied,
		(ssize_t)sizeof(struct camrtc_event_struct), &events_amount)) {
		dev_err(tracer->dev, "Events copy failed due to an overflow\n");
		return -EINVAL;
	}

	return events_amount;
}

/**
 * @brief Implements the write file operation for the RTCPU raw trace device
 *
 * This function is called when a user writes to the raw trace device file
 * - Gets the file context from @ref file->private_data
 * - Validates file context and tracer
 * - Gets the memory header from @ref tracer->trace_memory
 * - Updates the last read event index to the current event next index
 * - Updates the file context in @ref file->private_data
 * - Returns the buffer size (write is effectively a reset operation)
 *
 * @param[in/out] file         Pointer to the file structure.
 *                         Valid Range: Non-NULL pointer.
 * @param[in] user_buffer  User space buffer (not used).
 *                         Valid Range: Any.
 * @param[in] buffer_size  Size of the user buffer in bytes.
 *                         Valid Range: Any.
 * @param[in,out] ppos     Pointer to the file position (not used).
 *                         Valid Range: Any.
 *
 * @retval buffer_size Size of the input buffer (always successful)
 * @retval -ENODEV File descriptor context or tracer not set
 */
static ssize_t rtcpu_raw_trace_write(
	struct file *file, const char __user *user_buffer, size_t buffer_size, loff_t *ppos)
{
	struct tegra_rtcpu_trace *tracer;
	const struct camrtc_trace_memory_header *header;
	struct rtcpu_raw_trace_context *fd_context = file->private_data;

	if (!fd_context) {
		pr_err("file descriptor context is not set in private data\n");
		return -ENODEV;
	}

	tracer = fd_context->tracer;

	if (!tracer) {
		pr_err("Tracer is not set in file descriptor context\n");
		return -ENODEV;
	}

	header = tracer->trace_memory;

	fd_context->raw_trace_last_read_event_idx = header->event_next_idx;
	file->private_data = fd_context;

	return buffer_size;
}

/**
 * @brief Implements the poll file operation for the RTCPU raw trace device
 *
 * This function is called when a user polls the raw trace device file
 * - Gets the file context from @ref file->private_data
 * - Validates file context and tracer
 * - Checks for event availability using @ref check_event_availability
 * - If events are available, returns POLLIN and POLLRDNORM flags
 * - If no events are available, registers the wait queue using @ref poll_wait
 * - Wait queue will be woken up by @ref rtcpu_trace_events when new events arrive
 *
 * @param[in] file  Pointer to the file structure.
 *                  Valid Range: Non-NULL pointer.
 * @param[in] wait  Pointer to the poll_table structure.
 *                  Valid Range: May be NULL.
 *
 * @retval 0 No events available
 * @retval POLLIN|POLLRDNORM Events are available for reading
 * @retval -ENODEV File descriptor context or tracer not set
 */
static unsigned int rtcpu_raw_trace_poll(struct file *file, poll_table *wait)
{
	struct tegra_rtcpu_trace *tracer;
	unsigned int ret = 0;

	struct rtcpu_raw_trace_context *fd_context = file->private_data;

	if (!fd_context) {
		pr_err("file descriptor context is not set in private data\n");
		return -ENODEV;
	}

	tracer = fd_context->tracer;

	if (!tracer) {
		pr_err("Tracer is not set in file descriptor context\n");
		return -ENODEV;
	}

	/* check if new event on worker thread is relavant for current reader */
	if (check_event_availability(fd_context, tracer)) {
		ret = POLLIN | POLLRDNORM;  // event is available to read for current reader
		return ret;
	}

	/* No data available, register for wait queue */
	poll_wait(file, &tracer->wait_queue, wait);

	return ret;
}

/**
 * @brief Implements the open file operation for the RTCPU raw trace device
 *
 * This function is called when a user opens the raw trace device file
 * - Allocates a new file descriptor context using @ref kzalloc
 * - Retrieves the tracer using @ref container_of from the inode's cdev
 * - Validates the tracer
 * - Initializes the file descriptor context:
 *   - Sets the tracer reference
 *   - Sets the last read event index to 0
 *   - Sets first_read_call to true to handle special first read case
 * - Stores the context in @ref file->private_data for future operations
 * - Calls @ref nonseekable_open to complete the file opening
 *
 * @param[in] inode  Pointer to the inode structure.
 *                   Valid Range: Non-NULL pointer.
 * @param[in] file   Pointer to the file structure.
 *                   Valid Range: Non-NULL pointer.
 *
 * @retval (int) Return code propagated from @ref nonseekable_open()
 * @retval -ENOMEM Failed to allocate file descriptor context
 * @retval -ENODEV Failed to retrieve tracer
 */
static int rtcpu_raw_trace_open(struct inode *inode, struct file *file)
{
	struct tegra_rtcpu_trace *tracer;
	struct rtcpu_raw_trace_context *fd_context;

	fd_context = kzalloc(sizeof(*fd_context), GFP_KERNEL);
	if (unlikely(fd_context == NULL))
		return -ENOMEM;

	tracer = container_of(inode->i_cdev, struct tegra_rtcpu_trace, s_dev);

	if (!tracer) {
		pr_err("Failed to retrieve tracer\n");
		kfree(fd_context);
		return -ENODEV;
	}

	fd_context->tracer = tracer;
	fd_context->raw_trace_last_read_event_idx = 0;
	fd_context->first_read_call = true;
	file->private_data = fd_context;

	return nonseekable_open(inode, file);
}

/**
 * @brief Implements the release file operation for the RTCPU raw trace device
 *
 * This function is called when a user closes the raw trace device file
 * - Frees the file descriptor context using @ref kfree
 * - The context contains references to the tracer and read position state
 *
 * @param[in] inode  Pointer to the inode structure (not used).
 *                   Valid Range: Any.
 * @param[in] file   Pointer to the file structure containing private_data.
 *                   Valid Range: Non-NULL pointer with valid private_data.
 *
 * @retval 0 Always successful
 */
static int rtcpu_raw_trace_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);

	return 0;
}

/*
 * Debugfs
 */

#define DEFINE_SEQ_FOPS(_fops_, _show_) \
	static int _fops_ ## _open(struct inode *inode, struct file *file) \
	{ \
		return single_open(file, _show_, inode->i_private); \
	} \
	static const struct file_operations _fops_ = { \
		.open = _fops_ ## _open, \
		.read = seq_read, \
		.llseek = seq_lseek, \
		.release = single_release }

static const struct file_operations rtcpu_raw_trace_fops = {
	.owner = THIS_MODULE,
#if defined(NV_NO_LLSEEK_PRESENT)
	.llseek = no_llseek,
#endif
	.read = rtcpu_raw_trace_read,
	.write = rtcpu_raw_trace_write,
	.poll = rtcpu_raw_trace_poll,
	.open = rtcpu_raw_trace_open,
	.release = rtcpu_raw_trace_release,
};

static int rtcpu_trace_debugfs_stats_read(
	struct seq_file *file, void *data)
{
	struct tegra_rtcpu_trace *tracer = file->private;

	seq_printf(file, "Exceptions: %u\nEvents: %llu\nSnapshots: %u\n",
			tracer->n_exceptions, tracer->n_events, tracer->n_snapshots);

	return 0;
}

DEFINE_SEQ_FOPS(rtcpu_trace_debugfs_stats, rtcpu_trace_debugfs_stats_read);

static int rtcpu_trace_debugfs_last_exception_read(
	struct seq_file *file, void *data)
{
	struct tegra_rtcpu_trace *tracer = file->private;

	seq_puts(file, tracer->last_exception_str);

	return 0;
}

DEFINE_SEQ_FOPS(rtcpu_trace_debugfs_last_exception,
	rtcpu_trace_debugfs_last_exception_read);

static int rtcpu_trace_debugfs_last_event_read(
	struct seq_file *file, void *data)
{
	struct tegra_rtcpu_trace *tracer = file->private;
	struct camrtc_event_struct *event = &tracer->copy_last_event;
	unsigned int i;
	uint16_t payload_len;

	if (tracer->n_events == 0)
		return 0;

	payload_len = rtcpu_trace_event_len(event);

	seq_printf(file, "Len: %u\nID: 0x%08x\nTimestamp: %llu\n",
	    event->header.len, event->header.id, event->header.tstamp);

	switch (CAMRTC_EVENT_TYPE_FROM_ID(event->header.id)) {
	case CAMRTC_EVENT_TYPE_ARRAY:
		for (i = 0; i < payload_len / 4; ++i)
			seq_printf(file, "0x%08x ", event->data.data32[i]);
		seq_puts(file, "\n");
		break;
	case CAMRTC_EVENT_TYPE_ARMV7_EXCEPTION:
		seq_puts(file, "Exception.\n");
		break;
	case CAMRTC_EVENT_TYPE_PAD:
		break;
	case CAMRTC_EVENT_TYPE_START:
		seq_puts(file, "Start.\n");
		break;
	case CAMRTC_EVENT_TYPE_STRING:
		seq_puts(file, (char *) event->data.data8);
		break;
	case CAMRTC_EVENT_TYPE_BULK:
		for (i = 0; i < payload_len; ++i)
			seq_printf(file, "0x%02x ", event->data.data8[i]);
		seq_puts(file, "\n");
		break;
	default:
		seq_puts(file, "Unknown type.\n");
		break;
	}

	return 0;
}

DEFINE_SEQ_FOPS(rtcpu_trace_debugfs_last_event,
	rtcpu_trace_debugfs_last_event_read);

static void rtcpu_trace_debugfs_deinit(struct tegra_rtcpu_trace *tracer)
{
	debugfs_remove_recursive(tracer->debugfs_root);
}

static void rtcpu_trace_debugfs_init(struct tegra_rtcpu_trace *tracer)
{
	struct dentry *entry;

	tracer->debugfs_root = debugfs_create_dir("tegra_rtcpu_trace", NULL);
	if (IS_ERR_OR_NULL(tracer->debugfs_root))
		return;

	entry = debugfs_create_file("stats", S_IRUGO,
	    tracer->debugfs_root, tracer, &rtcpu_trace_debugfs_stats);
	if (IS_ERR_OR_NULL(entry))
		goto failed_create;

	entry = debugfs_create_file("last_exception", S_IRUGO,
	    tracer->debugfs_root, tracer, &rtcpu_trace_debugfs_last_exception);
	if (IS_ERR_OR_NULL(entry))
		goto failed_create;

	entry = debugfs_create_file("last_event", S_IRUGO,
	    tracer->debugfs_root, tracer, &rtcpu_trace_debugfs_last_event);
	if (IS_ERR_OR_NULL(entry))
		goto failed_create;

	return;

failed_create:
	debugfs_remove_recursive(tracer->debugfs_root);
}

/* Character device */
static struct class *rtcpu_raw_trace_class;
static int rtcpu_raw_trace_major;

/**
 * @brief Registers the RTCPU raw trace device driver
 *
 * This function registers the character device driver for the raw trace device
 * - Registers a character device using @ref register_chrdev
 * - Creates a device number using @ref MKDEV
 * - Initializes the character device using @ref cdev_init
 * - Adds the character device to the system using @ref cdev_add
 * - Creates a device class using @ref class_create
 * - Creates a device node using @ref device_create
 *
 * @param[in] tracer  Pointer to the tegra_rtcpu_trace structure.
 *                    Valid Range: Non-NULL pointer.
 *
 * @retval 0 Success
 * @retval <0 Error code from register_chrdev, cdev_add, or class_create
 */
static int raw_trace_node_drv_register(struct tegra_rtcpu_trace *tracer)
{
	dev_t devt;
	int ret;
	rtcpu_raw_trace_major = register_chrdev(0, DEVICE_NAME, &rtcpu_raw_trace_fops);
	if (rtcpu_raw_trace_major < 0) {
		dev_err(tracer->dev, "Register_chrdev failed\n");
		return rtcpu_raw_trace_major;
	}

	if (rtcpu_raw_trace_major > MAJOR(INT_MAX))
		dev_err(tracer->dev, "rtcpu_raw_trace_major Overflow range\n");
	else
		devt = MKDEV(rtcpu_raw_trace_major, 0);

	cdev_init(&tracer->s_dev, &rtcpu_raw_trace_fops);
	tracer->s_dev.owner = THIS_MODULE;
	tracer->s_dev.ops = &rtcpu_raw_trace_fops;
	ret = cdev_add(&tracer->s_dev, devt, 1);

	if (ret < 0) {
		dev_err(tracer->dev, "cdev_add() failed %d\n", ret);
		return ret;
	}

#if defined(NV_CLASS_CREATE_HAS_NO_OWNER_ARG) /* Linux v6.4 */
	rtcpu_raw_trace_class = class_create(DEVICE_NAME);
#else
	rtcpu_raw_trace_class = class_create(THIS_MODULE, DEVICE_NAME);
#endif

	if (IS_ERR(rtcpu_raw_trace_class)) {
		dev_err(tracer->dev, "device class file already in use\n");
		unregister_chrdev(rtcpu_raw_trace_major, DEVICE_NAME);
		return PTR_ERR(rtcpu_raw_trace_class);
	}

	device_create(rtcpu_raw_trace_class, tracer->dev, devt, tracer, DEVICE_NAME);

	return 0;
}

/**
 * @brief Unregisters the RTCPU raw trace device driver
 *
 * This function cleans up the character device driver registration
 * - Creates a device number using @ref MKDEV
 * - Destroys the device node using @ref device_destroy
 * - Deletes the character device using @ref cdev_del
 * - Destroys the device class using @ref class_destroy
 * - Unregisters the character device using @ref unregister_chrdev
 *
 * @param[in] tracer  Pointer to the tegra_rtcpu_trace structure.
 *                    Valid Range: Non-NULL pointer.
 */
static void raw_trace_node_unregister(
	struct tegra_rtcpu_trace *tracer)
{
	dev_t devt;

	if (rtcpu_raw_trace_major > MAJOR(INT_MAX))
		dev_err(tracer->dev, "rtcpu_raw_trace_major Overflow range\n");
	else
		devt = MKDEV(rtcpu_raw_trace_major, 0);

	device_destroy(rtcpu_raw_trace_class, devt);
	cdev_del(&tracer->s_dev);
	class_destroy(rtcpu_raw_trace_class);
	unregister_chrdev(rtcpu_raw_trace_major, DEVICE_NAME);
}

/*
 * Init/Cleanup
 */

/**
 * @brief Creates and initializes a new RTCPU trace structure
 *
 * This function allocates and initializes all resources needed for tracing
 * - Allocates memory for the trace structure using @ref kzalloc
 * - Initializes the mutex using @ref mutex_init
 * - Sets up trace memory using @ref rtcpu_trace_setup_memory
 * - Initializes trace memory using @ref rtcpu_trace_init_memory
 * - Initializes debugfs entries using @ref rtcpu_trace_debugfs_init
 * - Gets camera devices using @ref camrtc_device_get_byname for ISP and VI
 * - Initializes the wait queue using @ref init_waitqueue_head
 * - Reads device tree properties for configuration
 * - Initializes and schedules the worker using @ref INIT_DELAYED_WORK and
 *   @ref schedule_delayed_work
 * - Registers the character device driver using @ref raw_trace_node_drv_register
 *
 * @param[in] dev             Pointer to the device structure.
 *                            Valid Range: Non-NULL pointer.
 * @param[in] camera_devices  Pointer to the camera device group structure.
 *                            Valid Range: NULL or valid pointer.
 *
 * @retval Non-NULL pointer to tegra_rtcpu_trace structure on success
 * @retval NULL if memory allocation, device tree reading, or device registration fails
 */
struct tegra_rtcpu_trace *tegra_rtcpu_trace_create(struct device *dev,
	struct camrtc_device_group *camera_devices)
{
	struct tegra_rtcpu_trace *tracer;
	u32 param;
	int ret;

	tracer = kzalloc(sizeof(*tracer), GFP_KERNEL);
	if (unlikely(tracer == NULL)) {
		dev_err(dev, "%s: failed to allocate tracer\n", __func__);
		return NULL;
	}

	tracer->dev = dev;
	mutex_init(&tracer->lock);

	/* Get the trace memory */
	ret = rtcpu_trace_setup_memory(tracer);
	if (ret) {
		dev_err(dev, "%s: failed to setup trace memory, err=%d\n", __func__, ret);
		kfree(tracer);
		return NULL;
	}

	/* Initialize the trace memory */
	rtcpu_trace_init_memory(tracer);

	/* Debugfs */
	rtcpu_trace_debugfs_init(tracer);

	if (camera_devices != NULL) {
		tracer->isp_platform_device =
			camrtc_device_get_byname(camera_devices, "isp");
		if (IS_ERR(tracer->isp_platform_device)) {
			dev_info(dev, "no camera-device \"%s\"\n", "isp");
			tracer->isp_platform_device = NULL;
		}

		tracer->isp1_platform_device =
			camrtc_device_get_byname(camera_devices, "isp1");
		if (IS_ERR(tracer->isp1_platform_device)) {
			dev_info(dev, "no camera-device \"%s\"\n", "isp1");
			tracer->isp1_platform_device = NULL;
		}

		tracer->vi_platform_device =
			camrtc_device_get_byname(camera_devices, "vi0");
		if (IS_ERR(tracer->vi_platform_device)) {
			dev_info(dev, "no camera-device \"%s\"\n", "vi0");
			tracer->vi_platform_device = NULL;
		}

		tracer->vi1_platform_device =
			camrtc_device_get_byname(camera_devices, "vi1");
		if (IS_ERR(tracer->vi1_platform_device)) {
			dev_info(dev, "no camera-device \"%s\"\n", "vi1");
			tracer->vi1_platform_device = NULL;
		}
	}

	/* Initialize the wait queue */
	init_waitqueue_head(&tracer->wait_queue);

	/* Worker */
	param = WORK_INTERVAL_DEFAULT;
	if (of_property_read_u32(tracer->of_node, NV(interval-ms), &param)) {
		dev_err(dev, "%s: interval-ms property not present\n", __func__);
		kfree(tracer);
		return NULL;
	}

	tracer->enable_printk = of_property_read_bool(tracer->of_node,
						NV(enable-printk));

	tracer->log_prefix = "[RTCPU]";
	if (of_property_read_string(tracer->of_node, NV(log-prefix),
				&tracer->log_prefix)) {
		dev_err(dev, "%s: RTCPU property not present\n", __func__);
		kfree(tracer);
		return NULL;
	}

	INIT_DELAYED_WORK(&tracer->work, rtcpu_trace_worker);
	tracer->work_interval_jiffies = msecs_to_jiffies(param);

	/* Done with initialization */
	schedule_delayed_work(&tracer->work, 0);

	dev_info(dev, "Trace buffer configured at IOVA=0x%08x\n",
		 (u32)tracer->dma_handle);

	ret = raw_trace_node_drv_register(tracer);
	if (ret) {
		dev_err(dev, "%s: failed to register device node, err=%d\n", __func__, ret);
		kfree(tracer);
		return NULL;
	}

	return tracer;
}
EXPORT_SYMBOL(tegra_rtcpu_trace_create);

/**
 * @brief Synchronizes the RTCPU trace memory with the device
 *
 * This function sets up the I/O virtual memory mapping for RTCPU trace buffer
 * in the RTCPU memory space. This must be called after the RTCPU has booted
 * to ensure the trace buffer is visible to the RTCPU.
 *
 * @param[in] tracer  Pointer to the tegra_rtcpu_trace structure.
 *                    Valid Range: NULL or valid pointer.
 *
 * @retval 0 Success or tracer was NULL
 * @retval -EIO IOVM setup error
 */
int tegra_rtcpu_trace_boot_sync(struct tegra_rtcpu_trace *tracer)
{
	int ret;

	if (tracer == NULL)
		return 0;

	ret = tegra_camrtc_iovm_setup(tracer->dev, tracer->dma_handle);
	if (ret == 0)
		return 0;

	dev_err(tracer->dev, "RTCPU trace: IOVM setup error: %d\n", ret);

	return -EIO;
}
EXPORT_SYMBOL(tegra_rtcpu_trace_boot_sync);

/**
 * @brief Cleans up and destroys an RTCPU trace structure
 *
 * This function frees all resources allocated for the RTCPU trace structure
 * - Validates the tracer pointer is not NULL or ERR_PTR
 * - Releases platform devices references using @ref platform_device_put
 * - Releases device tree node using @ref of_node_put
 * - Cancels and flushes the periodic worker using @ref cancel_delayed_work_sync
 *   and @ref flush_delayed_work
 * - Unregisters the character device driver using @ref raw_trace_node_unregister
 * - Cleans up debugfs entries using @ref rtcpu_trace_debugfs_deinit
 * - Frees DMA memory using @ref dma_free_coherent
 * - Frees the tracer structure using @ref kfree
 *
 * @param[in/out] tracer  Pointer to the tegra_rtcpu_trace structure.
 *                    Valid Range: NULL, ERR_PTR, or valid pointer.
 */
void tegra_rtcpu_trace_destroy(struct tegra_rtcpu_trace *tracer)
{
	if (IS_ERR_OR_NULL(tracer))
		return;
	platform_device_put(tracer->isp_platform_device);
	platform_device_put(tracer->vi_platform_device);
	platform_device_put(tracer->vi1_platform_device);
	of_node_put(tracer->of_node);
	cancel_delayed_work_sync(&tracer->work);
	flush_delayed_work(&tracer->work);
	raw_trace_node_unregister(tracer);
	rtcpu_trace_debugfs_deinit(tracer);
	dma_free_coherent(tracer->dev, tracer->trace_memory_size,
			tracer->trace_memory, tracer->dma_handle);
	kfree(tracer);
}
EXPORT_SYMBOL(tegra_rtcpu_trace_destroy);

/**
 * @brief Sets the panic_flag in the tracer structure to indicate a panic.
 *
 * This function does the following:
 * - Checks if the tracer is NULL, returns if it is.
 * - Set the panic_flag to 1 by calling @ref atomic_cmpxchg
 *
 * @param[in] tracer Pointer to the tegra_rtcpu_trace structure.
 */
void tegra_rtcpu_trace_set_panic_flag(struct tegra_rtcpu_trace *tracer)
{
	if (tracer == NULL)
		return;

	/* Atomically set panic_flag to 1 if it is currently 0 */
	atomic_cmpxchg(&tracer->panic_flag, 0, 1);
}
EXPORT_SYMBOL(tegra_rtcpu_trace_set_panic_flag);

MODULE_DESCRIPTION("NVIDIA Tegra RTCPU trace driver");
MODULE_LICENSE("GPL v2");
