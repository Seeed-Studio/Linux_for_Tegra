// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */
#include <dce.h>
#include <dce-os-thread.h>
#include <dce-linux-device.h>
#include <dce-os-utils.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/firmware.h>
#include <linux/bitops.h>
#include <linux/bitmap.h>

/**
 * Do not add any more util functions to this file.
 * We should add OS util functions to respective OS module files.
 */

/**
 * dce_os_writel - Dce io function to perform MMIO writes
 *
 * @d : Pointer to tegra_dce struct.
 * @r : register offset from dce_base.
 * @v : value to be written
 *
 * Return : Void
 */
void dce_os_writel(struct tegra_dce *d, u32 r, u32 v)
{
	struct dce_linux_device *d_dev = dce_linux_device_from_dce(d);

	if (unlikely(!d_dev->regs))
		dce_os_err(d, "DCE Register Space not IOMAPed to CPU");
	else
		writel(v, d_dev->regs + r);
}

/**
 * dce_os_readl - Dce io function to perform MMIO reads
 *
 * @d : Pointer to tegra_dce struct.
 * @r : register offset from dce_base.
 *
 * Return : the read value
 */
u32 dce_os_readl(struct tegra_dce *d, u32 r)
{
	u32 v = 0xffffffff;

	struct dce_linux_device *d_dev = dce_linux_device_from_dce(d);

	if (unlikely(!d_dev->regs))
		dce_os_err(d, "DCE Register Space not IOMAPed to CPU");
	else
		v = readl(d_dev->regs + r);
	/*TODO : Add error check here */
	return v;
}

/**
 * dce_os_writel_check - Performs MMIO writes and checks if the writes
 *			are actaully correct.
 *
 * @d : Pointer to tegra_dce struct.
 * @r : register offset from dce_base.
 * @v : value to be written
 *
 * Return : Void
 */
void dce_os_writel_check(struct tegra_dce *d, u32 r, u32 v)
{
	/* TODO : Write and read back to check */
}

/**
 * dce_os_io_exists - Dce io function to check if the registers are mapped
 *			to CPU correctly
 *
 * @d : Pointer to tegra_dce struct.
 *
 * Return : True if mapped.
 */
bool dce_os_io_exists(struct tegra_dce *d)
{
	struct dce_linux_device *d_dev = dce_linux_device_from_dce(d);

	return d_dev->regs != NULL;
}

/**
 * dce_os_io_valid_reg - Dce io function to check if the requested offset is
 *			within the range of CPU mapped MMIO range.
 *
 * @d : Pointer to tegra_dce struct.
 * @r : register offset from dce_base.
 *
 * Return : True if offset within range.
 */
bool dce_os_io_valid_reg(struct tegra_dce *d, u32 r)
{
	/* TODO : Implement range check here. Returning true for now*/
	return true;
}

/**
 * dce_os_kzalloc - Function to allocate contiguous kernel memory
 *
 * @d : Pointer to tegra_dce struct.
 * @size_t : Size of the memory to be allocated
 * @dma_flag: True if allocated memory should be DMAable
 *
 * Return : CPU Mapped Address if successful else NULL.
 */
void *dce_os_kzalloc(struct tegra_dce *d, size_t size, bool dma_flag)
{
	void *alloc;
	gfp_t flags = GFP_KERNEL;

	if (dma_flag)
		flags |= __GFP_DMA;

	alloc = kzalloc(size, flags);

	return alloc;
}

/**
 * dce_os_kfree - Frees an alloc from dce_os_kzalloc
 *
 * @d : Pointer to tegra_dce struct.
 * @addr : Address of the object to free.
 *
 * Return : void
 */
void dce_os_kfree(struct tegra_dce *d, void *addr)
{
	kfree(addr);
}

/**
 * dce_os_request_firmware - Reads the fw into memory.
 *
 * @d : Pointer to tegra_dce struct.
 * @fw_name : Name of the fw.
 *
 * Return : Pointer to dce_firmware if successful else NULL.
 */
struct dce_firmware *dce_os_request_firmware(struct tegra_dce *d,
					const char *fw_name)
{
	struct device *dev = dev_from_dce_linux_device(d);
	struct dce_firmware *fw;
	const struct firmware *l_fw;

	fw = dce_os_kzalloc(d, sizeof(*fw), false);
	if (!fw)
		return NULL;

	if (request_firmware(&l_fw, fw_name, dev) < 0) {
		dce_os_err(d, "FW Request Failed");
		goto err;
	}

	if (!l_fw)
		goto err;

	/* Make sure the address is aligned to 4K */
	fw->size = l_fw->size;

	fw->size = ALIGN(fw->size + SZ_4K, SZ_4K);
	/**
	 * BUG : Currently overwriting all alignment logic above to blinldy
	 * allocate 2MB FW virtual space. Ideally it should be as per the
	 * actual size of the fw.
	 */
	fw->size = SZ_32M;

	fw->data = dma_alloc_coherent(dev, fw->size,
				      (dma_addr_t *)&fw->dma_handle,
				      GFP_KERNEL);
	if (!fw->data)
		goto err_release;

	memcpy((u8 *)fw->data, (u8 *)l_fw->data, l_fw->size);

	release_firmware(l_fw);

	return fw;

err_release:
	release_firmware(l_fw);
err:
	dce_os_kfree(d, fw);
	return NULL;
}

/**
 * dce_release_firmware - Reads the fw into memory.
 *
 * @d : Pointer to tegra_dce struct.
 * @fw : Pointer to dce_firmware.
 *
 * Return : void
 */
void dce_os_release_fw(struct tegra_dce *d, struct dce_firmware *fw)
{
	struct device *dev = dev_from_dce_linux_device(d);

	if (!fw)
		return;

	dma_free_coherent(dev, fw->size,
			(void *)fw->data,
			(dma_addr_t)fw->dma_handle);

	dce_os_kfree(d, fw);
}

/**
 * dce_os_get_dce_stream_id - Gets the dce stream ID to be programmed from
 * platform data.
 *
 * @d : Pointer to tegra_dce struct.
 *
 * Return : Stream ID Value
 */
u8 dce_os_get_dce_stream_id(struct tegra_dce *d)
{
	return pdata_from_dce_linux_device(d)->stream_id;
}

static void dce_print(const char *func_name, int line,
			enum dce_os_log_type type, const char *log)
{
#define DCE_LOG_FMT	"dce: %15s:%-4d %s\n"

	switch (type) {
	case DCE_OS_DEBUG:
		pr_debug(DCE_LOG_FMT, func_name, line, log);
		break;
	case DCE_OS_INFO:
		pr_info(DCE_LOG_FMT, func_name, line, log);
		break;
	case DCE_OS_WARNING:
		pr_warn(DCE_LOG_FMT, func_name, line, log);
		break;
	case DCE_OS_ERROR:
		pr_err(DCE_LOG_FMT, func_name, line, log);
		break;
	}
#undef DCE_LOG_FMT
}

__printf(5, 6)
void dce_os_log_msg(struct tegra_dce *d, const char *func_name, int line,
			enum dce_os_log_type type, const char *fmt, ...)
{

#define BUF_LEN 100

	char log[BUF_LEN];
	va_list args;

	va_start(args, fmt);
	(void) vsnprintf(log, BUF_LEN, fmt, args);
	va_end(args);

	dce_print(func_name, line, type, log);
}

int dce_os_cond_init(struct dce_os_cond *cond)
{
	init_waitqueue_head(&cond->wq);
	cond->initialized = true;

	return 0;
}

void dce_os_cond_destroy(struct dce_os_cond *cond)
{
	cond->initialized = false;
}

void dce_os_cond_signal_interruptible(struct dce_os_cond *cond)
{
	WARN_ON(!cond->initialized);

	wake_up_interruptible(&cond->wq);
}

int dce_os_cond_broadcast_interruptible(struct dce_os_cond *cond)
{
	if (!cond->initialized)
		return -EINVAL;

	wake_up_interruptible_all(&cond->wq);

	return 0;
}

/**
 * dce_thread_proxy - Function to be passed to kthread.
 *
 * @thread_data : Pointer to actual dce_thread struct
 *
 * Return  : Ruturns the return value of the function to be run.
 */
static int dce_thread_proxy(void *thread_data)
{
	struct dce_thread *thread = thread_data;
	int ret = thread->fn(thread->data);

	thread->running = false;
	return ret;
}

/**
 * dce_os_thread_create - Create and run a new thread.
 *
 * @thread - thread structure to use
 * @data - data to pass to threadfn
 * @threadfn - Thread function
 * @name - name of the thread
 *
 * Create a thread and run threadfn in it. The thread stays alive as long as
 * threadfn is running. As soon as threadfn returns the thread is destroyed.
 *
 * threadfn needs to continuously poll dce_os_thread_should_stop() to determine
 * if it should exit.
 */
int dce_os_thread_create(struct dce_thread *thread,
		void *data,
		int (*threadfn)(void *data), const char *name)
{
	struct task_struct *task = kthread_create(dce_thread_proxy,
			thread, name);
	if (IS_ERR(task))
		return PTR_ERR(task);

	thread->task = task;
	thread->fn = threadfn;
	thread->data = data;
	thread->running = true;
	wake_up_process(task);
	return 0;
};

/**
 * dce_os_thread_stop - Destroy or request to destroy a thread
 *
 * @thread - thread to stop
 *
 * Request a thread to stop by setting dce_os_thread_should_stop() to
 * true and wait for thread to exit.
 */
void dce_os_thread_stop(struct dce_thread *thread)
{
	/*
	 * Threads waiting on wq's should have dce_os_thread_should_stop()
	 * as one of its wakeup condition. This allows the thread to be woken
	 * up when kthread_stop() is invoked and does not require an additional
	 * callback to wakeup the sleeping thread.
	 */
	if (thread->task) {
		kthread_stop(thread->task);
		thread->task = NULL;
	}
};

/**
 * dce_os_thread_should_stop - Query if thread should stop
 *
 * @thread
 *
 * Return true if thread should exit. Can be run only in the thread's own
 * context and with the thread as parameter.
 */
bool dce_os_thread_should_stop(struct dce_thread *thread)
{
	return kthread_should_stop();
};

/**
 * dce_os_thread_is_running - Query if thread is running
 *
 * @thread
 *
 * Return true if thread is started.
 */
bool dce_os_thread_is_running(struct dce_thread *thread)
{
	return READ_ONCE(thread->running);
};

/**
 * dce_os_thread_join - join a thread to reclaim resources
 * after it has exited
 *
 * @thread - thread to join
 *
 */
void dce_os_thread_join(struct dce_thread *thread)
{
	while (READ_ONCE(thread->running))
		usleep_range(10000, 20000);
};

/**
 * dce_os_get_nxt_pow_of_2 : get next power of 2 number for a given number
 *
 * @addr : Address of given number
 * @nbits : bits in given number
 *
 * Return : unsigned long next power of 2 value
 */
unsigned long dce_os_get_nxt_pow_of_2(unsigned long *addr, u8 nbits)
{
	u8 l_bit = 0;
	u8 bit_index = 0;
	unsigned long val;

	val = *addr;
	if (val == 0)
		return 0;

	bit_index = find_first_bit(addr, nbits);

	while (bit_index && (bit_index < nbits)) {
		l_bit = bit_index;
		bit_index = find_next_bit(addr, nbits, bit_index + 1);
	}

	if (BIT(l_bit) < val) {
		l_bit += 1UL;
		val = BIT(l_bit);
	}

	return val;
}

/*
 * dce_os_usleep_range : sleep between min-max range
 *
 * @min : minimum sleep time in usec
 * @max : maximum sleep time in usec
 *
 * Return : void
 */

void dce_os_usleep_range(unsigned long min, unsigned long max)
{
	usleep_range(min, max);
}

/**
 * ipc_allocate_region [Private] - Allocates IPC region
 *
 * @d : Pointer to tegra_dce structure.
 *
 * Return : 0 if successful
 */
static int ipc_allocate_region(struct tegra_dce *d)
{
	unsigned long tot_q_sz;
	unsigned long tot_ivc_q_sz;
	struct device *dev;
	struct dce_ipc_region *region;

	dev = dev_from_dce_linux_device(d);
	region = &d->d_ipc.region;

	tot_q_sz = ((DCE_ADMIN_CMD_MAX_NFRAMES *
		     tegra_ivc_align(DCE_ADMIN_CMD_MAX_FSIZE) * 2) +
		    (DCE_DISPRM_CMD_MAX_NFRAMES	*
		     tegra_ivc_align(DCE_DISPRM_CMD_MAX_FSIZE) * 2) +
		    (DCE_ADMIN_CMD_MAX_NFRAMES *
		     tegra_ivc_align(DCE_ADMIN_CMD_CHAN_FSIZE) * 2) +
		    (DCE_DISPRM_EVENT_NOTIFY_CMD_MAX_NFRAMES *
		     tegra_ivc_align(DCE_DISPRM_EVENT_NOTIFY_CMD_MAX_FSIZE) * 2)
		   );

	tot_ivc_q_sz = tegra_ivc_total_queue_size(tot_q_sz);
	region->size = dce_os_get_nxt_pow_of_2(&tot_ivc_q_sz, 32);
	region->base = dma_alloc_coherent(dev, region->size,
			&region->iova, GFP_KERNEL | __GFP_ZERO);
	if (!region->base)
		return -ENOMEM;

	region->s_offset = 0;

	return 0;
}


/**
 * ipc_free_region [Private] - Frees up the IPC region
 *
 * @d : Pointer to the tegra_dce struct.
 *
 * Return : Void
 */
static void ipc_free_region(struct tegra_dce *d)
{
	struct device *dev;
	struct dce_ipc_region *region;

	dev = dev_from_dce_linux_device(d);
	region = &d->d_ipc.region;

	dma_free_coherent(dev, region->size,
		(void *)region->base, region->iova);

	region->s_offset = 0;
}

/**
 * dce_os_ipc_init_region_info - Initialize IPC region information.
 *
 * @d : Pointer to tegra_dce structure.
 *
 * Return : 0 if successful
 */
int dce_os_ipc_init_region_info(struct tegra_dce *d)
{
	return ipc_allocate_region(d);
}

/**
 * dce_os_ipc_deinit_region_info - De-initialize the IPC region
 *
 * @d : Pointer to the tegra_dce struct.
 *
 * Return : Void
 */
void dce_os_ipc_deinit_region_info(struct tegra_dce *d)
{
	return ipc_free_region(d);
}

/**
 * dce_os_bitmap_set - Set bits in a bitmap
 *
 * @map : Pointer to map
 * @start : Start bit
 * @len : Length indicating number of bits to set.
 *
 * Return : Void
 */
void dce_os_bitmap_set(unsigned long *map,
				  unsigned int start, unsigned int len)
{
	bitmap_set(map, start, (int)len);
}

/**
 * dce_os_bitmap_set - Set bits in a bitmap
 *
 * @map : Pointer to map
 * @start : Start bit
 * @len : Length indicating number of bits to clear.
 *
 * Return : Void
 */
void dce_os_bitmap_clear(unsigned long *map,
				    unsigned int start, unsigned int len)
{
	bitmap_clear(map, start, (int)len);
}

int dce_os_init_log_buffer(struct tegra_dce *d)
{
	struct dce_log_buffer *buffer;
	struct device *dev = dev_from_dce_linux_device(d);

	buffer = &d->dce_log_buff;
	buffer->size = SZ_512K; // Allocate 512KB for log buffer

	buffer->cpu_base = dma_alloc_coherent(dev, buffer->size, &buffer->iova_addr,
								GFP_KERNEL);

	if (!buffer->iova_addr)
		return -ENOMEM;

	return 0;
}

void dce_os_deinit_log_buffer(struct tegra_dce *d)
{
	struct dce_log_buffer *buffer;
	struct device *dev = dev_from_dce_linux_device(d);

	buffer = &d->dce_log_buff;
	if (buffer->iova_addr) {
		dma_free_coherent(dev, buffer->size, (void *)buffer->cpu_base,
					buffer->iova_addr);
	}
}
