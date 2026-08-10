/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2025, NVIDIA CORPORATION. All rights reserved.
 *
 * Tegra PVA header
 */

#ifndef __NVHOST_PVA_H__
#define __NVHOST_PVA_H__

#include <linux/firmware.h>
#include <linux/mutex.h>
#include <linux/version.h>
#include <linux/workqueue.h>

#include "nvpva_queue.h"
#include "pva_regs.h"
#include "pva_nvhost.h"
#include "pva-ucode-header.h"
#include "pva_vpu_app_auth.h"
#include "pva_fw_carveout.h"
#include "pva_hwseq.h"

#include <uapi/linux/tegra-soc-hwpm-uapi.h>

/**
 * PVA Host1x class IDs
 */
enum {
	NV_PVA0_CLASS_ID	= 0xF1,
	NV_PVA1_CLASS_ID	= 0xF2,
};

struct nvpva_client_context;

enum pva_submit_mode {
	PVA_SUBMIT_MODE_MAILBOX = 0,
	PVA_SUBMIT_MODE_MMIO_CCQ = 1,
};

struct pva_version_info {
	u32 pva_r5_version;
	u32 pva_compat_version;
	u32 pva_revision;
	u32 pva_built_on;
};

/**
 * Queue count of 8 is maintained per PVA.
 */
#define MAX_PVA_QUEUE_COUNT 8
#define MAX_PVA_CLIENTS 8
#define MAX_PVA_TASK_COUNT_PER_QUEUE		256U
#define MAX_PVA_SEG_COUNT_PER_QUEUE		4U
#define MAX_PVA_TASK_COUNT_PER_QUEUE_SEG	\
	(MAX_PVA_TASK_COUNT_PER_QUEUE/MAX_PVA_SEG_COUNT_PER_QUEUE)

/**
 * Maximum task count that a PVA engine can support
 */
#define MAX_PVA_TASK_COUNT					\
	((MAX_PVA_QUEUE_COUNT) * (MAX_PVA_TASK_COUNT_PER_QUEUE))

/**
 * Minium PVA frequency (10MHz)
 */
#define MIN_PVA_FREQUENCY 10000000

/**
 * Maximum number of IRQS to be serviced by the driver. Gen1 has a single IRQ,
 * Gen2 has 9.
 */
#define MAX_PVA_IRQS 9
#define MAX_PVA_INTERFACE 9
#define PVA_MAILBOX_INDEX 0
#define PVA_CCQ0_INDEX 1
#define PVA_CCQ1_INDEX 2
#define PVA_CCQ2_INDEX 3
#define PVA_CCQ3_INDEX 4
#define PVA_CCQ4_INDEX 5
#define PVA_CCQ5_INDEX 6
#define PVA_CCQ6_INDEX 7
#define PVA_CCQ7_INDEX 8

/**
 * Maximum number of DMA channels. Gen3 and Gen2 have 16 channels.
 * R5 FW reserves one DMA channel for internal use.
 */
#define MAX_PVA_DMA_CHANNELS	15U
/**
 * Maximum number of frames in a hwseq blob.Gen2 has single frame,
 * Gen3 can have upto 64.
 */
#define MAX_PVA_HWSEQ_FRAMES	64U
/**
 * Maximum number of column/rows(CR) in hwseq. Gen2 has single CR,
 * Gen3 can have upto 32.
 */
#define MAX_PVA_HWSEQ_CR_COUNT	32U

/**
 * Number of VPUs for each PVA
 */
#define NUM_VPU_BLOCKS 2

/**
 * nvpva_dbg_* macros provide wrappers around kernel print functions
 * that use a debug mask configurable at runtime to provide control over
 * the level of detail that gets printed.
 */
#ifdef CONFIG_DEBUG_FS
    /* debug info, default is compiled-in but effectively disabled (0 mask) */
    #define NVPVA_DEBUG
    /*e.g: echo 1 > /d/pva0/driver_dbg_mask */
    #define NVPVA_DEFAULT_DBG_MASK 0
#else
    /* manually enable and turn on the mask */
    #define NVPVA_DEFAULT_DBG_MASK (pva_dbg_info)
#endif

enum nvpva_dbg_categories {
	pva_dbg_info     = BIT(0),  /* slightly verbose info */
	pva_dbg_fn       = BIT(2),  /* fn name tracing */
	pva_dbg_prof     = BIT(7),  /* profiling info */
	pva_dbg_fw_trace = BIT(8),  /* FW trace logs */
};

#if defined(NVPVA_DEBUG)
#define nvpva_dbg(pva, dbg_mask, format, arg...)                               \
	do {                                                                   \
		if (unlikely((dbg_mask)&pva->driver_log_mask)) {               \
			pr_info("nvpva %s: " format "\n", __func__, ##arg);    \
		}                                                              \
	} while (0)

#else /* NVPVA_DEBUG */
#define nvpva_dbg(pva, dbg_mask, format, arg...)                               \
	do {                                                                   \
		if (0) {                                                       \
			(void) pva; /* unused variable */                      \
			pr_info("nvhost %s: " format "\n", __func__, ##arg);   \
		}                                                              \
	} while (0)

#endif

/* convenience,shorter err/fn/dbg_info */
#define nvpva_err(d, fmt, arg...) \
	dev_err(d, "%s: " fmt "\n", __func__, ##arg)

#define nvpva_err_ratelimited(d, fmt, arg...) \
	dev_err_ratelimited(d, "%s: " fmt "\n", __func__, ##arg)

#define nvpva_warn(d, fmt, arg...) \
	dev_warn(d, "%s: " fmt "\n", __func__, ##arg)

#define nvpva_dbg_fn(pva, fmt, arg...) \
	nvpva_dbg(pva, pva_dbg_fn, fmt, ##arg)

#define nvpva_dbg_info(pva, fmt, arg...) \
	nvpva_dbg(pva, pva_dbg_info, fmt, ##arg)

#define nvpva_dbg_prof(pva, fmt, arg...) \
	nvpva_dbg(pva, pva_dbg_prof, fmt, ##arg)

#define nvpva_dbg_fw_trace(pva, fmt, arg...) \
	nvpva_dbg(pva, pva_dbg_fw_trace, fmt, ##arg)

/**
 * @brief		struct to hold the segment details
 *
 * addr:		virtual addr of the segment from PRIV2 address base
 * size:		segment size
 * offset:		offset of the addr from priv2 base
 *
 */
struct pva_seg_info {
	void *addr;
	u32 size;
	u32 offset;
};

/**
 * @breif		struct to hold the segment details for debug purpose
 *
 * pva			Pointer to pva struct
 * seg_info		pva_seg_info struct
 *
 */
struct pva_crashdump_debugfs_entry {
	struct pva *pva;
	struct pva_seg_info seg_info;
};

/**
 * @brief		struct to handle dma alloc memory info
 *
 * size			size allocated
 * phys_addr		physical address
 * va			virtual address
 *
 */
struct pva_dma_alloc_info {
	size_t size;
	dma_addr_t pa;
	void *va;
};

/**
 * @brief		struct to handle the PVA firmware information
 *
 * hdr			pointer to the pva_code_hdr struct
 * priv1_buffer		pva_dma_alloc_info for priv1_buffer
 * priv2_buffer		pva_dma_alloc_info for priv2_buffer
 * priv2_reg_offset	priv2 register offset from uCode
 * trace_buffer_size	buffer size for trace log
 *
 */
struct pva_fw {
	struct pva_ucode_hdr_s *hdr;

	struct pva_dma_alloc_info priv1_buffer;
	struct pva_dma_alloc_info priv2_buffer;
	u32 priv2_reg_offset;

	u32 trace_buffer_size;
};

/*
 * @brief		store trace log segment's address and size
 *
 * addr		Pointer to the pva trace log segment
 * size		Size of pva trace log segment
 * offset		Offset in bytes for trace log segment
 *
 */
struct pva_trace_log {
	void *addr;
	u32 size;
	u32 offset;
};

struct pva_fw_debug_log {
	void *addr;
	u32 size;
	struct mutex saved_log_lock;
	u8 *saved_log;
};

void save_fw_debug_log(struct pva *pva);

/*
 * @brief	stores address and other attributes of the vpu function table
 *
 * addr		The pointer to start of the VPU function table
 * size		Table size of the function table
 * handle	The IOVA address of the function table
 * entries	The total number of entries in the function table
 *
 */
struct pva_func_table {
	struct vpu_func *addr;
	uint32_t size;
	dma_addr_t handle;
	uint32_t entries;
};

struct pva_status_interface_registers {
	uint32_t registers[5];
};

#define PVA_HW_GEN1 1
#define PVA_HW_GEN2 2
#define PVA_HW_GEN3 3

/**
 * @brief		HW version specific configuration and functions
 * read_mailbox		Function to read from mailbox based on PVA revision
 * write_mailbox	Function to write to mailbox based on PVA revision
 * ccq_send_task	Function to submit task to ccq based on PVA revision
 * submit_cmd_sync_locked
 *			Function to submit command to PVA based on PVA revision
 *			Should be called only if appropriate locks have been
 *			acquired
 *
 * submit_cmd_sync	Function to submit command to PVA based on PVA revision
 * irq_count		Number of IRQs associated with this PVA revision
 *
 */

struct pva_version_config {
	u32 (*read_mailbox)(struct platform_device *pdev, u32 mbox_id);
	void (*write_mailbox)(struct platform_device *pdev, u32 mbox_id,
			      u32 value);
	void (*read_status_interface)(struct pva *pva, uint32_t interface_id,
				      u32 isr_status,
				      struct pva_cmd_status_regs *status_out);
	int (*ccq_send_task)(struct pva *pva, u32 queue_id,
			     dma_addr_t task_addr, u8 batchsize, u8 *task_status, u32 flags);
	int (*submit_cmd_sync_locked)(struct pva *pva, struct pva_cmd_s *cmd,
				      u32 nregs, u32 queue_id,
				      struct pva_cmd_status_regs *status_regs);

	int (*submit_cmd_sync)(struct pva *pva, struct pva_cmd_s *cmd,
			       u32 nregs, u32 queue_id,
			       struct pva_cmd_status_regs *status_regs);
	int irq_count;
};

/**
 * @brief		Describe a VPU hardware debug block
 * vbase		Address mapped to virtual space
 */
struct pva_vpu_dbg_block {
	void __iomem *vbase;
};

/**
 * @brief		VPU utilization information
 *
 * start_stamp		time stamp when measurment started
 * end_stamp		time stamp when measurment is to end
 * vpu_stats		avaraged vpu utilization stats
 * stats_fw_buffer_iova
 * stats_fw_buffer_va
 */
struct pva_vpu_util_info {
	u64			start_stamp;
	u64			end_stamp;
	u64			vpu_stats[2];
	dma_addr_t		stats_fw_buffer_iova;
	struct pva_vpu_stats_s	*stats_fw_buffer_va;
};

struct scatterlist;
struct nvpva_syncpt_desc {
	dma_addr_t addr;
	size_t size;
	u32 id;
	u32 assigned;
};

struct nvpva_syncpts_desc {
	struct platform_device *host_pdev;
	struct nvpva_syncpt_desc syncpts_rw[MAX_PVA_QUEUE_COUNT];
	dma_addr_t syncpt_start_iova_r;
	dma_addr_t syncpt_range_r;
	dma_addr_t syncpt_start_iova_rw;
	dma_addr_t syncpt_range_rw;
	uint32_t   page_size;
	bool	   syncpts_mapped_r;
	bool	   syncpts_mapped_rw;
};

/**
 * @brief		Driver private data, shared with all applications
 *
 * version		pva version; 1, 2 or 3
 * pdev			Pointer to the PVA device
 * pool			Pointer to Queue table available for the PVA
 * fw_info		firmware information struct
 * irq			IRQ number obtained on registering the module
 * cmd_waitqueue	Command Waitqueue for response waiters
 *			for syncronous commands
 * cmd_status_regs	Response to commands is stored into this
 *			structure temporarily
 * cmd_status		Status of the command interface
 * mailbox_mutex	Mutex to avoid concurrent mailbox accesses
 * debugfs_entry_r5	debugfs segment information for r5
 * debugfs_entry_vpu0	debugfs segment information for vpu0
 * debugfs_entry_vpu1	debugfs segment information for vpu1
 * priv1_dma		struct pva_dma_alloc_info for priv1_dma
 * priv2_dma		struct pva_dma_alloc_info for priv2_dma
 * pva_trace		struct for pva_trace_log
 * submit_mode		Select the task submit mode
 * dbg_vpu_app_id	Set the vpu_app id to debug
 * r5_dbg_wait		Set the r5 debugger to wait
 * timeout_enabled	Set pva timeout enabled based on debug
 * slcg_disable		Second level Clock Gating control variable
 * vpu_printf_enabled
 * vpu_debug_enabled
 * log_level		controls the level of detail printed by FW
 *			debug statements
 * profiling_level
 * driver_log_mask	controls the level of detail printed by kernel
 *			debug statements
 */

struct pva {
	u32 version;
	struct pva_version_config *version_config;
	struct platform_device *pdev;
	struct platform_device *aux_pdev;
	struct nvpva_queue_pool *pool;
	struct pva_fw fw_info;
	struct nvpva_carveout_info *co;
	struct nvpva_carveout_info fw_carveout;
	struct pva_vpu_auth_s pva_auth;
	struct pva_vpu_auth_s pva_auth_sys;
	struct nvpva_syncpts_desc syncpts;

	int irq[MAX_PVA_IRQS];
	s32 sids[16];
	u32 sid_count;
	u32 ec_state[8];

	wait_queue_head_t cmd_waitqueue[MAX_PVA_INTERFACE];
	struct pva_cmd_status_regs cmd_status_regs[MAX_PVA_INTERFACE];
	enum pva_cmd_status cmd_status[MAX_PVA_INTERFACE];
	struct mutex mailbox_mutex;
	struct mutex recovery_mutex;
	struct mutex pva_busy_mutex;
	struct mutex ccq_mutex[MAX_PVA_INTERFACE];

	struct pva_crashdump_debugfs_entry debugfs_entry_r5;
	struct pva_crashdump_debugfs_entry debugfs_entry_vpu0;
	struct pva_crashdump_debugfs_entry debugfs_entry_vpu1;

	struct pva_dma_alloc_info priv1_dma;
	struct pva_dma_alloc_info priv2_dma;
	/* Circular array to share with PVA R5 FW for task status info */
	struct pva_dma_alloc_info priv_circular_array;
	/* Current position to read task status buffer from the circular
	 * array
	 */
	u32 circular_array_rd_pos;
	/* Current position to write task status buffer from the circular
	 * array
	 */
	u32 circular_array_wr_pos;
	struct work_struct task_update_work;
	atomic_t n_pending_tasks;
	atomic_t recovery_cnt;
	struct workqueue_struct *task_status_workqueue;
	struct pva_trace_log pva_trace;
	struct pva_fw_debug_log fw_debug_log;
	u32 submit_task_mode;
	u32 submit_cmd_mode;

	u32 r5_dbg_wait;
	u32 boot_count;
	int pva_power_on_err;
	bool timeout_enabled;
	u32 slcg_disable;
	u32 vmem_war_disable;
	bool vpu_printf_enabled;
	bool vpu_debug_enabled;
	bool stats_enabled;
	bool map_co_needed;
	bool boot_from_file;
	bool is_hv_mode;
	bool in_recovery;
	struct pva_vpu_util_info vpu_util_info;
	u32 profiling_level;
	atomic_t ccq_polling[MAX_PVA_INTERFACE];
	struct work_struct pva_abort_handler_work;
	struct work_struct pva_fw_log_work;
	struct mutex pva_fw_log_mutex;

#ifdef CONFIG_PVA_INTERRUPT_DISABLED
	struct task_struct *pva_aisr_handler_task;
#endif
	bool booted;
#ifdef CONFIG_PM
	bool is_suspended;
#endif
	u32 log_level;
	u32 driver_log_mask;
	struct nvpva_client_context *clients;
	struct mutex clients_lock;

	struct pva_vpu_dbg_block vpu_dbg_blocks[NUM_VPU_BLOCKS];
	struct pva_hwseq_cr_info_s hwseq_cr_info[MAX_PVA_DMA_CHANNELS]
					 [MAX_PVA_HWSEQ_FRAMES][MAX_PVA_HWSEQ_CR_COUNT];

	struct tegra_soc_hwpm_ip_ops hwpm_ip_ops;
};

/**
 * @brief	Copy traces to kernel trace buffer.
 *
 * When mailbox interrupt for copying ucode trace buffer to
 * kernel-ucode shared trace buffer is arrived it copies the kernel-ucode
 * shared trace buffer to kernel ftrace buffer
 *
 * @pva Pointer to pva structure
 *
 */
void pva_trace_copy_to_ftrace(struct pva *pva);

/**
 * @brief	Copy FW logs to kernel message log.
 *
 * when FW errors occur, this function is called to
 * dump out the FW log buffer to kernel msg log.
 *
 * @pva Pointer to pva structure
 *
 */
void pva_fw_log_dump(struct pva *pva, bool hold_mutex);

/**
 * @brief	Register PVA ISR
 *
 * This function called from driver to register the
 * PVA ISR with IRQ.
 *
 * @param pdev	Pointer to PVA device
 * @return	0 on Success or negative error code
 *
 */
int pva_register_isr(struct platform_device *dev);

/**
 * @brief	deInitiallze pva debug utils
 *
 * @param pva	Pointer to PVA device
 * @return	none
 *
 */
void pva_debugfs_deinit(struct pva *pva);

/**
 * @brief	Initiallze pva debug utils
 *
 * @param pdev	Pointer to PVA device
 * @return	none
 *
 */
void pva_debugfs_init(struct platform_device *pdev);

/**
 * @brief	Initiallze PVA abort handler
 *
 * @param pva	Pointer to PVA structure
 * @return	none
 *
 */
void pva_abort_init(struct pva *pva);

/**
 * @brief	Recover PVA back into working state
 *
 * @param pva	Pointer to PVA structure
 * @return	none
 *
 */
void pva_abort(struct pva *pva);

/**
 * @brief	Check if PVA is in recovery
 *
 * @param pva	Pointer to PVA structure
 * @return	True if in recover, else false
 *
 */
bool pva_recovery_acquire(struct pva *pva, struct mutex *mutex);
void pva_recovery_release(struct pva *pva);
/**
 * @brief	Run the ucode selftests
 *
 * This function is invoked if the ucode is in selftest mode.
 * The function will do the static memory allocation for the
 * ucode self test to run.
 *
 * @param pdev	Pointer to PVA device
 * @return	0 on Success or negative error code
 *
 */
int pva_run_ucode_selftest(struct platform_device *pdev);

/**
 * @brief	Allocate and populate the function table to the memory
 *
 * This function is called when the vpu table needs to be populated.
 * The function also allocates the memory required for the vpu table.
 *
 * @param pva			Pointer to PVA device
 * @param pva_func_table	Pointer to the function table which contains
 *				the address, table size and number of entries
 * @return			0 on Success or negative error code
 *
 */
int pva_alloc_and_populate_function_table(struct pva *pva,
					  struct pva_func_table *fn_table);

/**
 * @brief	Deallocate the memory of the function table
 *
 * This function is called once the allocated memory for vpu table needs to
 * be freed.
 *
 * @param pva			Pointer to PVA device
 * @param pva_func_table	Pointer to the function table which contains
 *				the address, table size and number of entries
 *
 */
void pva_dealloc_vpu_function_table(struct pva *pva,
				    struct pva_func_table *fn_table);

/**
 * @brief	Get PVA version information
 *
 * @param pva	Pointer to a PVA device node
 * @param info	Pointer to an information structure to be filled
 *
 * @return	0 on success, otherwise a negative error code
 */
int pva_get_firmware_version(struct pva *pva, struct pva_version_info *info);

/**
 * @brief	Set trace log level of PVA
 *
 * @param pva	Pointer to a PVA device node
 * @param log_level	32-bit mask for logs that we want to receive
 *
 * @return	0 on success, otherwise a negative error code
 */

/**
 * @brief	Get PVA Boot KPI
 *
 * @param pva	Pointer to a PVA device node
 * @param r5_boot_time	Pointer to a variable, where r5 boot time will be filled
 *
 * @return	0 on success, otherwise a negative error code
 */
int pva_boot_kpi(struct pva *pva, u64 *r5_boot_time);

int pva_set_log_level(struct pva *pva, u32 log_level, bool mailbox_locked);

int nvpva_request_firmware(struct platform_device *pdev, const char *fw_name,
			   const struct firmware **ucode_fw);

int nvpva_get_device_hwid(struct platform_device *pdev,
			  unsigned int id);

u32 nvpva_get_id_idx(struct pva *dev, struct platform_device *pdev);

void pva_push_aisr_status(struct pva *pva, uint32_t aisr_status);

static inline u64 nvpva_get_tsc_stamp(void)
{
	u64 timestamp;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)
	timestamp = arch_timer_read_counter();
#else
	timestamp = arch_counter_get_cntvct();
#endif
	return timestamp;
}
#endif

int pva_busy(struct pva *pva, u32 attempts);
void pva_idle(struct pva *pva);