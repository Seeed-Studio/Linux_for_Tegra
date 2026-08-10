/*
 * SPDX-License-Identifier: GPL-2.0-only
 * SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef TEGRA_SYSCALLS_H
#define TEGRA_SYSCALLS_H

#include <soc/tegra/virt/tegra_hv_sysmgr.h>

/**
 * @defgroup hypervisor_calls Hypervisor Calls
 * @{
 */

/* @brief `always_inline` to ensure it is always inlined
 *          for performance-critical operations.
 */
#define ALWAYS_INLINE			__attribute__((always_inline)) inline

/*
 * @brief `no_sanitize_address` attribute to avoid
 * address sanitizer instrumentation, necessary for low-level code.
 */
#define NO_SANITIZE_ADDRESS		__attribute__((no_sanitize_address))

/* @brief Immediate hypercall value used to get read stat */
#define HVC_NR_READ_STAT		1
/* @brief Immediate hypercall value used in the hyp_read_ivc_info() wrapper */
#define HVC_NR_READ_IVC			2u
/** @brief Immediate hypercall value used in the hyp_read_gid() wrapper */
#define HVC_NR_READ_GID			3u
/* @brief Immediate hypercall value used in the hyp_raise_irq() wrapper */
#define HVC_NR_RAISE_IRQ		4u
/* @brief Immediate hypercall value used in the hyp_read_nguests() wrapper */
#define HVC_NR_READ_NGUESTS		5u
/* @brief Immediate hypercall value used in the hyp_read_ipa_pa_info() wrapper */
#define HVC_NR_READ_IPA_PA		6u
/* @brief Immediate hypercall value used in the hyp_read_guest_state() wrapper */
#define HVC_NR_READ_GUEST_STATE		7u
/* @brief Immediate hypercall value used in the hyp_read_hyp_info() wrapper */
#define HVC_NR_READ_HYP_INFO		9u
/* @brief Immediate hypercall value used in the hyp_guest_reset() wrapper */
#define HVC_NR_GUEST_RESET		10u
/* @brief Immediate hypercall value used in the hyp_sysinfo_ipa() wrapper */
#define HVC_NR_SYSINFO_IPA		13u
/* @brief Immediate hypercall value used in the hyp_lcpu0_mpidr() wrapper */
#define HVC_NR_LCPU0_MPIDR		15u
/* @brief Immediate hypercall value used in the hyp_read_vm_info() wrapper */
#define HVC_NR_READ_VM_INFO		16u
/* @brief Immediate hypercall value used in the hyp_trace_get_mask() wrapper */
#define HVC_NR_TRACE_GET_EVENT_DATA		0x8003U
/* @brief Immediate hypercall value used in the hyp_trace_set_mask() wrapper */
#define HVC_NR_TRACE_SET_EVENT_DATA		0x8004U
/* @brief Immediate hypercall value used in the hyp_smmu_diagnostic() wrapper */
#define HVC_NR_SMMU_DIAG		0x8009U
/* @brief Immediate hypercall value used in the hyp_cbb_err_diagnostic() wrapper */
#define HVC_NR_CBB_DIAG		0x8133U
/* @brief Immediate hypercall value used in the hyp_mc_err_diagnostic() wrapper */
#define HVC_NR_MC_DIAG		0x8134U

/* @brief FIXME Not being Used */
#define GUEST_PRIMARY		0
/* @brief FIXME Not being Used */
#define GUEST_IVC_SERVER	0
/* @brief Max number of nvlog producers */
#define MAX_NVLOG_PRODUCERS	32U
/* @brief Max number of nvlog entities */
#define MAX_NVLOG_ENTITIES	3U
/* @brief Immediate hypercall value used in
 * - hyp_read_freq_feedback() wrapper
 * - hyp_read_freq_request() wrapper
 * - hyp_write_freq_request() wrapper
 * - hyp_pct_cpu_id_read_freq_feedback() wrapper
 * - hyp_pct_cpu_id_read_freq_request() wrapper
 * - hyp_pct_cpu_id_write_freq_request() wrapper
 * - hyp_get_cpu_count() wrapper
 */
#define HVC_NR_CPU_FREQ		0xC6000022

/* @brief Set masks to dump evenlib events */
#define TRACE_SET_EVENT_MASK		0x0U
/* @brief Get masks to dump evenlib events */
#define TRACE_GET_EVENT_MASK		0x1U
/* @brief Set the hypervisor profiler frequency. */
#define TRACE_SET_PROFILER_FREQ	 	0x2U
/* @brief Get the hypervisor profiler frequency. */
#define TRACE_GET_PROFILER_FREQ	 	0x3U

/* @brief Max number of VM supported by Hypervisor */
#define NGUESTS_MAX 16

#ifndef __ASSEMBLY__

#if defined(__KERNEL__)
#include <linux/types.h>
#endif

/* @brief
 * Structure describing a single NvLog producer. A producer can have
 * multiple buffers (if it has multiple threads) and the buffers are stored
 * contiguously in memory as an array. This structure provides the base
 * address, stride, and length of the array so that the consumer can locate all
 * of the buffers belonging to the producer.
 */
struct nvlog_producer {
	/* @brief Base IPA of an array of NvLog buffers belonging to the producer. */
	uint64_t ipa;
	/* @brief Size of the IPA region containing the NvLog buffer array. */
	uint64_t region_size;
	/* @brief Size of a single NvLog buffer. This is the stride of the array of
	 * NvLog buffers belonging to the producer.
	 */
	uint64_t buf_size;
	/* @brief Number of NvLog buffers belonging to the producer. */
	uint64_t buf_count;
	/* @brief Name of the NvLog producer. */
	char name[32];
};

/*
 * @brief Structure to store information about an NvLog entity. An entity is
 * generally an operating system deployment. Log producers belonging to an
 * entity will inhabit the same PID space and share a log level region.
 */
struct nvlog_entity {
	/* @brief The NvLog entity ID. */
	uint16_t id;
	/* @brief IPA of shared memory used to request NvLog log level changes. */
	uint64_t log_level_region_ipa;
	/* @brief Size of shared memory used to request NvLog log level changes. */
	uint64_t log_level_region_size;
} __attribute__((packed, aligned(8)));

/* @brief
 * Data structure for the VM Information Region.
 *
 * The Intermediate Physical Address (IPA) of this structure is @returned by the
 * hyp_read_vm_info Hypercall.
 */
struct vm_info_region {
	/* @brief
	 * Table of NvLog producers.
	 *
	 * This table is only populated for VMs that have the 'log_access' PCT flag
	 * set. Valid entries in the table have non-zero 'region_ipa', 'buf_size',
	 * and buf_count' fields.
	 */
	struct nvlog_producer nvlog_producers[MAX_NVLOG_PRODUCERS];

	/**
	 * @brief NvLog Entity Info.
	 */
	struct nvlog_entity nvlog_entities[MAX_NVLOG_ENTITIES];
};

/**
 * @brief Data structure for an IVC queue.
 * Although an IVC queue points to the total shared memory that is required for
 * communication between RX and TX, the size field here only indicates the size
 * of RX FIFO or the TX FIFO which is half the size of the total IVC Queue
 * memory. This is to support backward compatibility with the Linux
 * drivers as well as the Hypervisor that maps this shared memory for each end
 * to use.
 *
 * @note Structure should be 8 bytes aligned, specified by compiler directives
 * for performance and atomicity guarantees.
 */
struct tegra_hv_queue_data {
	/* @brief IVC queue id */
	uint32_t	id;
	/* @brief IDs of the two users of the queue */
	uint32_t	peers[2];
	/* @brief size of SIVC FIFO memory (data plus header) */
	uint32_t	size;
	/* @brief number of data frames in each direction */
	uint32_t	nframes;
	/* @brief size of each frame in bytes */
	uint32_t	frame_size;
	/* @brief offset of this structure from the start of the IVC area */
	uint32_t	offset;
	/* @brief virtual IRQ ID associated to this queue */
	uint16_t	irq;
	/* @brief reserved for internal use */
	uint16_t	raise_irq;
	/* @brief IO address used to notify peer endpoint */
	uint64_t	trap_ipa;
	/* @brief MSI address used to notify peer endpoint */
	uint64_t	msi_ipa;
};

/**
 * @brief Data structure for an IVC mempool.
 *
 * @note Structure should be 8 bytes aligned, specified by compiler directives
 * for performance and atomicity guarantees.
 */
struct ivc_mempool {
	/* @brief start address of the mempool shared memory */
	uint64_t pa;
	/* @brief size of the mempool shared memory in bytes */
	uint64_t size;
	/* @brief ID of one of the mempool's clients */
	uint32_t id;
	/* @brief ID of the other mempool client */
	uint32_t peer_vmid;
};

/**
 * @brief Data structure for an IVC shared area.
 *
 * @note Structure should be 8 bytes aligned, specified by compiler directives
 * for performance and atomicity guarantees.
 */
struct ivc_shared_area {
	/* @brief start address of the shared area */
	uint64_t pa;
	/* @brief size of the area in bytes */
	uint64_t size;
	/* @brief ID of the VM to which this area belongs */
	uint32_t guest;
	/* @brief starting index of free irqs */
	uint16_t free_irq_start;
	/* @brief total count of free irqs */
	uint16_t free_irq_count;
};

/**
 * @brief Data structure that contains information about all IVC objects (queues
 * and mempools) used by the current VM.
 *
 * @note Structure should be 8 bytes aligned, specified by compiler directives
 * for performance and atomicity guarantees.
 */
struct ivc_info_page {
	/* @brief Total number of queues */
	uint32_t nr_queues;
	/* @brief Total number of areas */
	uint32_t nr_areas;
	/* @brief Total number of mempools */
	uint32_t nr_mempools;
	/* @brief reserved for internal use
	 *
	 * @note IMPORTANT: Padding is needed to align
	 * sizeof(struct ivc_info_page ) to 64 bits
	 */
	uint32_t padding;
	/* @brief MMIO trap region start address */
	uint64_t trap_region_base_ipa;
	/* @brief MMIO trap region size */
	uint64_t trap_region_size;
	/* @brief MMIO trap IPA stride size */
	uint64_t trap_ipa_stride;
	/* @brief MMIO msi region start address */
	uint64_t msi_region_base_ipa;
	/* @brief MMIO msi region size */
	uint64_t msi_region_size;
	/* @brief MMIO msi IPA stride size */
	uint64_t msi_ipa_stride;
	/* @brief The actual length of this array is nr_areas. */
	struct ivc_shared_area areas[];

	/**
	 * The IVC shared area follows the above fields.
	 * Accessing the IVC shared area must be done via the inline function
	 * ivc_shared_area_addr().
	 * The IVC shared area consists of an array of nr_areas ivc_shared areas
	 * structures: struct ivc_shared_area areas[nr_areas];
	 */

	/**
	 * Following the IVC shared area is the array of queue data structures
	 * with one entry per each queue that is accessible in the current VM.
	 * Accessing the queue data structures must be done via the inline
	 * function ivc_info_queue_array().
	 * The queue data area consists of an array of nr_queues
	 * tegra_hv_queue_data structures:
	 * struct tegra_hv_queue_data queue_data[nr_queues];
	 */

	/**
	 * Following the queue data area is the array of mempool structures
	 * with one entry per each mempool that is accessible in the current VM.
	 * Accessing the mempool data structures must be done via the inline
	 * function ivc_info_mempool_array().
	 * The mempool data area consists of an array of nr_mempools
	 * ivc_mempool structures:
	 * struct ivc_mempool mempool_data[nr_mempools];
	 */
};

/*
 * @brief          Obtain pointer to the shared area represnted by area number
 *
 * @param[in]      info IVC info page where IVC meta data is maintained by Hypervisor
 * @param[in]      area_num represents Area Number
 *
 * @return         @returns a pointer to the associated ivc_shared_area structure
 *
 * @pre            This API should be invoked on virtual/hypervisor environment only.
 */
static inline struct ivc_shared_area *ivc_shared_area_addr(
		const struct ivc_info_page *info, uint32_t area_num)
{
	return ((struct ivc_shared_area *) (((uintptr_t) info) + sizeof(*info)))
		+ area_num;
}

/*
 * @brief          Obtain pointer to the array of IVC Queue
 *
 * @param[in]      info IVC info page where IVC meta data is maintained by Hypervisor
 *
 * @return         pointer to the array of IVC Queue
 *
 * @pre            This API should be invoked on virtual/hypervisor environment only.
 */
static inline const struct tegra_hv_queue_data *ivc_info_queue_array(
		const struct ivc_info_page *info)
{
	return (struct tegra_hv_queue_data *)&info->areas[info->nr_areas];
}

/*
 * @brief          Obtain pointer to the array of mempool
 *
 * @param[in]      info IVC info page where IVC meta data is maintained by Hypervisor
 *
 * @return         pointer to the array of mempool
 *
 * @pre            This API should be invoked on virtual/hypervisor environment only.
 */
static inline const struct ivc_mempool *ivc_info_mempool_array(
		const struct ivc_info_page *info)
{
	return (struct ivc_mempool *)
			&ivc_info_queue_array(info)[info->nr_queues];
}

/* @brief Data structure used in the hyp_read_ipa_pa_info interface */
struct hyp_ipa_pa_info {
	/* @brief base of contiguous pa region */
	uint64_t base;
	/* @brief offset for requested ipa address */
	uint64_t offset;
	/* @brief size of pa region */
	uint64_t size;
};

/* @brief Max VCPUs supported by Hypervisor */
#define HVC_MAX_VCPU 64

/* @brief FIXME */
struct trapped_access {
	/* @brief FIXME */
	uint64_t ipa;
	/* @brief FIXME */
	uint32_t size;
	/* @brief FIXME */
	int32_t write_not_read;
	/* @brief FIXME */
	uint64_t data;
	/* @brief FIXME */
	uint32_t guest_id;
};

/* @brief @brief Structure to store the IPA, Length and Name. */
struct trace_buf {
	/* @brief IPA of trace buffer region. */
	uint64_t ipa;
	/* @brief Length of trace buffer region. */
	uint64_t size;
	/* @brief Trace Buffer Name */
	char name[32];
};

/*
 * @brief Data structure for the server's information page.
 *
 * The Intermediate Physical Address (IPA) of this structure is
 * @returned by the hyp_read_hyp_info Hypercall.
 */
struct hyp_server_page {
	/* @brief Virtual IRQ index used in the guest reset protocol */
	uint32_t guest_reset_virq;

	/* @brief boot delay offsets per VM needed by monitor partition */
	uint32_t boot_delay[NGUESTS_MAX];

	/* @brief The IPA of the Hypervisor trace log buffer. */
	uint64_t log_ipa;
	/* @brief The size of the Hypervisor trace log buffer. */
	uint32_t log_size;

	/* @brief IPA of Hypervisor secure trace log buffer. */
	uint64_t secure_log_ipa;
	/* @brief Size of Hypervisor secure trace log buffer. */
	uint32_t secure_log_size;

	/* @brief The IPA of a the PCT blob, 0 if the VM is not a server. */
	uint64_t pct_ipa;
	/* @brief The size of the PCT blob. */
	uint64_t pct_size;

	/* @brief Indicates whether the VM is a server.
	 *
	 * It is non-zero if the VM is a server, zero otherwise.
	 */
	uint32_t is_server_vm;

	/* @brief The IPA of the Golden Register data area */
	uint64_t gr_ipa;
	/* @brief The size of the Golden Register data area */
	uint32_t gr_size;

	/* @brief all vm mappings ipa */
	uint64_t mappings_ipa;

	/* @brief IPA, Length and Name trace buffer region. */
	struct trace_buf trace_buffs[2*NGUESTS_MAX];

};

/* @brief For backwards compatibility, alias the old name for hyp_server_name. */
#define hyp_info_page hyp_server_page

/* @brief CPU core registers from x3 to x17 */
#define _X3_X17 "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", \
"x13", "x14", "x15", "x16", "x17"

/* @brief CPU core registers from x4 to x17 */
#define _X4_X17 "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", \
"x13", "x14", "x15", "x16", "x17"

/**
 * @brief          Obtain the ID of the current VM.
 *
 * @param[in,out]  gid Pointer to an int location.
 *                 Index of current VM in struct guest_conf [] arrary (a.k.a PCT VM Index)
 *                 is updated in the provided location on successful @return from this API.
 * @retval         0 If successful.
 * @retval         -1 If the input pointer (gid) points to NULL.
 *
 * @invariant      Pointer gid shall point to a vaild location.
 *
 * @pre
 *                 - This API should be invoked on virtual/hypervisor environment only.
 *
 * @post
 *                 - Guest VM ID will be returned.
 *
 * @usage
 *                 - Allowed context for the API call
 *                   - Interrupt handler: Yes
 *                   - Signal handler: Yes
 *                   - Thread-safe: No
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
NO_SANITIZE_ADDRESS static inline int hyp_read_gid(unsigned int *gid)
{
	register uint64_t r0 asm("x0");
	register uint64_t r1 asm("x1");

	asm("hvc %2"
		: "=r"(r0), "=r"(r1)
		: "i"(HVC_NR_READ_GID)
		: "x2", "x3", _X4_X17);

	*gid = r1;
	return (int)r0;
}

/*
 * @brief          Obtain the number of Virtual Machines in the current configuration.
 *
 * @param[in,out]  nguests pointer to an int location
 *
 * @return         0 if successful: *nguests will contain the number of VMs;
 *                 non-zero otherwise
 *
 * @note           Only server VMs are allowed to obtain this information; for
 *                 non-server VMs this function @returns an error value.
 *
 * @pre            This API should be invoked on virtual/hypervisor environment only.
 */
NO_SANITIZE_ADDRESS static inline int hyp_read_nguests(unsigned int *nguests)
{
	register uint64_t r0 asm("x0");
	register uint64_t r1 asm("x1");

	asm("hvc %2"
		: "=r"(r0), "=r"(r1)
		: "i"(HVC_NR_READ_NGUESTS)
		: "x2", "x3", _X4_X17);

	*nguests = r1;
	return (int)r0;
}

__attribute__((no_sanitize_address))
static inline int hyp_read_vm_info(uint64_t *vm_info_region_pa)
{
	register uint64_t r0 asm("x0");
	register uint64_t r1 asm("x1");

	asm("hvc %2"
		: "=r"(r0), "=r"(r1)
		: "i"(HVC_NR_READ_VM_INFO)
		: "x2", "x3", _X4_X17);

	*vm_info_region_pa = r1;
	return (int)r0;
}

/*
 * @brief          Obtain Intermediate Physical Address of the location containing
 *	           the VMs IVC & Mempool information (struct ivc_info_page).
 *
 * @param[in,out]  ivc_info_page_pa pointer to the location where the Intermediate
 *	           Physical Address of the Page (64K in size) containing VMs IVC and Mempool
 *	           information (refer struct ivc_info_page) is updated on successful @return.
 *
 * @return         Function success or error code.
 * @retval         0 if successful
 * @retval         -IVC_EINVAL if ivc_info_page_pa value is NULL.
 *
 * @invarinat      ivc_info_page_pa pointer shall point to a vaild location.
 *
 * @pre            This API should be invoked on virtual/hypervisor environment only.
 */
NO_SANITIZE_ADDRESS static inline int hyp_read_ivc_info(uint64_t *ivc_info_page_pa)
{
	register uint64_t r0 asm("x0");
	register uint64_t r1 asm("x1");

	asm("hvc %2"
		: "=r"(r0), "=r"(r1)
		: "i"(HVC_NR_READ_IVC)
		: "x2", "x3", _X4_X17);

	*ivc_info_page_pa = r1;
	return (int)r0;
}

/*
 * @brief          Obtain contiguous physical block (start & size) that corresponds
 *                 to the given Guest Intermediate Physical Address.
 *
 * @param[in,out]  info struct hyp_ipa_pa_info* const
 *	           [in] pointer to struct hyp_ipa_pa_info where the physical block information
 *	           is @returned. -IVC_EINVAL is @returned in case this pointer is NULL
 *	           [out] struct hyp_ipa_pa_info* const info In case of successful @return value for this interface,
 *	           struct hyp_ipa_pa_info pointer location is updated as below:
 *	           struct hyp_ipa_pa_info.base contains start of contiguous physical block in which input IPA lies
 *	           struct hyp_ipa_pa_info.offset contains the offset with-in the contiguous physical block of the input IPA
 *	           struct hyp_ipa_pa_info.size contains the size of the contiguous physical block in which input IPA lies.
 * @param[in]      guestid PCT index of the guest for which the physical block information is to be obtained.
 * @param[in]      ipa IPA for which the physical contiguous block information is to be obtained.
 *
 * @return         Whether requested information was successfully obtained.
 * @retval         r0 value will be @return by function.
 *
 * @pre            This API should be invoked on virtual/hypervisor environment only.
 */
NO_SANITIZE_ADDRESS static inline int hyp_read_ipa_pa_info(struct hyp_ipa_pa_info *info,
		unsigned int guestid, uint64_t ipa)
{
	register uint64_t r0 asm("x0") = guestid;
	register uint64_t r1 asm("x1") = ipa;
	register uint64_t r2 asm("x2");
	register uint64_t r3 asm("x3");


	asm("hvc %4"
		: "+r"(r0), "+r"(r1), "=r"(r2), "=r"(r3)
		: "i"(HVC_NR_READ_IPA_PA)
		: _X4_X17);

	info->base = r1;
	info->offset = r2;
	info->size = r3;

	return (int)r0;
}

/*
 * @brief          Obtain the state of a given VM.
 *
 * @param[in]      irq to be raised
 * @param[in]      vmid the ID of the VM
 *
 * @return         0 if successful; -IVC_EINVAL otherwise.
 * @return         Value @return by Hypervisor in r0 register
 *
 * @pre            This API should be invoked on virtual/hypervisor environment only.
 */
NO_SANITIZE_ADDRESS static inline int hyp_raise_irq(unsigned int irq, unsigned int vmid)
{
	register uint64_t r0 asm("x0") = irq;
	register uint64_t r1 asm("x1") = vmid;

	asm volatile("hvc %1"
		: "+r"(r0)
		: "i"(HVC_NR_RAISE_IRQ), "r"(r1)
		: "x2", "x3", _X4_X17);

	return (int)r0;
}

/*
 * @brief          Obtain the state of a given VM.
 *
 * @param[in]      vmid the ID of the VM
 * @param[in,out]  state pointer to an int location
 *
 * @return         0 if successful; -IVC_EINVAL otherwise.
 *                 On successful completion, *state will be filled with the
 *                 state value as defined by the enumeration vm_state.
 *
 * @pre            This API should be invoked on virtual/hypervisor environment only.
 */
NO_SANITIZE_ADDRESS static inline int hyp_read_guest_state(unsigned int vmid, unsigned int *state)
{
	register uint64_t r0 asm("x0") = vmid;
	register uint64_t r1 asm("x1");

	asm("hvc %2"
		: "+r"(r0), "=r"(r1)
		: "i"(HVC_NR_READ_GUEST_STATE)
		: "x2", _X3_X17);

	*state = (unsigned int)r1;
	return (int)r0;
}

/*
 * @brief          Obtain the Intermediate Physical Address of the Server Page for the
 *                 current server VM.
 *
 * @param[in,out]  hyp_info_page_pa pointer to a uint64_t location. On successful
 *                 completion, *hyp_info_page_pa will contain the IPA of the
 *                 current VM's Server Page.
 *
 * @return         Whether the IPA was obtained successfully.
 * @retval         0 for success.
 * @retval         -IVC_EINVAL if hyp_info_page_pa is NULL.
 * @retval         -EINVAL if function is called by non-server VM (identified by
 *                 struct guest_conf->is_server_guest == 0).
 *
 * @note           Only server VMs have a Server Page; for non-server VMs this
 *                 function @returns an error value -EINVAL.
 *
 * @pre            This API should be invoked on virtual/hypervisor environment only.
 */
NO_SANITIZE_ADDRESS static inline int hyp_read_hyp_info(uint64_t *hyp_info_page_pa)
{
	register uint64_t r0 asm("x0");
	register uint64_t r1 asm("x1");

	asm("hvc %2"
		: "=r"(r0), "=r"(r1)
		: "i"(HVC_NR_READ_HYP_INFO)
		: "x2", "x3", _X4_X17);

	*hyp_info_page_pa = r1;
	return (int)r0;
}

/*
 * @brief          Initiate a System state transition from a privileged VM.
 *
 * @param[in]      id The system state transition Command ID which can be one of following:
 *                 - SYS_SHUTDOWN_INIT_CMD : Indicates System Shutdown. Refer to @p hyp_sys_shutdown for ICD.
 *                 - SYS_REBOOT_INIT_CMD   : Indicates System Reboot. Refer to @p hyp_sys_reboot for ICD.
 *                 - SYS_SUSPEND_INIT_CMD  : Indicates SC7. Refer to @p hyp_sys_suspend for ICD.
 *                 - GUEST_PAUSE_CMD       : Indicates Guest Suspend. Refer to @p hyp_guest_pause for ICD.
 * @param[out]     out is not used.
 *
 * @return         int         The function @returns an integer value.
 * @retval         0           System State Transition was successfully requested.
 * @retval         -EINVAL     if input command id is not supported.
 * @retval         -EACCESS    if guest VM issuing the call is not privileged. A privileged VM is a VM that
 *                 has field "is_privileged" set to 1U in guestCfg structure from PCT.
 *
 * @pre            This API should be invoked on virtual/hypervisor environment only.
 */
NO_SANITIZE_ADDRESS static inline int hyp_guest_reset(unsigned int id,
				  struct hyp_sys_state_info *out)
{
	register uint64_t r0 asm("x0") = id;
	register uint64_t r1 asm("x1");
	register uint64_t r2 asm("x2");
	register uint64_t r3 asm("x3");

	asm volatile("hvc %4"
		: "+r"(r0), "=r"(r1),
		  "=r"(r2), "=r"(r3)
		: "i"(HVC_NR_GUEST_RESET)
		: _X4_X17);

	if (out != NULL) {
		out->sys_transition_mask = (uint32_t)r1;
		out->vm_shutdown_mask = (uint32_t)r2;
		out->vm_reboot_mask = (uint32_t)r3;
	}

	return (int)r0;
}

__attribute__((no_sanitize_address))
static inline int hyp_guest_enter_vm_op(unsigned int const id, unsigned int const vm_op)
{
	register uint64_t r0 asm("x0") = id;
	register uint64_t r1 asm("x1") = vm_op;

	asm volatile("hvc %2"
		: "+r"(r0), "+r"(r1)
		: "i"(HVC_NR_GUEST_RESET)
		: "x2", _X3_X17);

	return (int)r0;
}

/*
 * @brief          Return the IPA of the SysInfo area for the current VM.
 *
 * @return         non-zero IPA address
 *
 * @pre            This API should be invoked on virtual/hypervisor environment only.
 */
NO_SANITIZE_ADDRESS static inline uint64_t hyp_sysinfo_ipa(void)
{
	register uint64_t r0 asm("x0");

	asm("hvc %1"
		: "=r"(r0)
		: "i"(HVC_NR_SYSINFO_IPA)
		: "x1", "x2", "x3", _X4_X17);

	return r0;
}

/*
 * @brief          API to read frequency feedback
 *
 * @param[in,out]  value pointer to location as input where result will be updated.
 *                 Return value from r1 register will be store in value pointer
 *
 * @return         Value @return by Hypervisor in r0 register
 *
 * @pre            This API should be invoked on virtual/hypervisor environment only.
 */
NO_SANITIZE_ADDRESS static inline int hyp_read_freq_feedback(uint64_t *value)
{
	register uint64_t r0 asm("x0") = HVC_NR_CPU_FREQ;
	register uint64_t r1 asm("x1") = 1U;

	asm volatile("hvc #0"
		: "+r"(r0), "+r"(r1)
		:
		: "x2", "x3", _X4_X17);

	if (r0 == 1 &&  value != NULL)
		*value = r1;

	return (int16_t)r0;
}

/*
 * @brief          API to read frequency request
 *
 * @param[in,out]  value pointer to location as input where result will be updated.
 *                 Return value from r1 register will be store in value pointer
 *
 * @return         Value @return by Hypervisor in r0 register
 *
 * @pre            This API should be invoked on virtual/hypervisor environment only.
 */
NO_SANITIZE_ADDRESS static inline int hyp_read_freq_request(uint64_t *value)
{
	register uint64_t r0 asm("x0") = HVC_NR_CPU_FREQ;
	register uint64_t r1 asm("x1") = 0U;

	asm volatile("hvc #0"
		: "+r"(r0), "+r"(r1)
		:
		: "x2", "x3", _X4_X17);

	if (r0 == 1 &&  value != NULL)
		*value = r1;

	return (int16_t)r0;
}

/*
 * @brief          API to write frequency request
 *
 * @param[in]      value frequency value to be set.
 *
 * @return         Value @return by Hypervisor in r0 register
 *
 * @pre            This API should be invoked on virtual/hypervisor environment only.
 */
NO_SANITIZE_ADDRESS static inline int hyp_write_freq_request(uint64_t value)
{
	register uint64_t r0 asm("x0") = HVC_NR_CPU_FREQ;
	register uint64_t r1 asm("x1") = 2U;
	register uint64_t r2 asm("x2") = value;

	asm volatile("hvc #0"
		: "+r"(r0)
		: "r"(r1), "r"(r2)
		: "x3", _X4_X17);

	return (int16_t)r0;
}

/*
 * @brief          API to read frequency feedback for dedicated CPU
 *
 * @param[in]      cpu_id ID to the dedicated CPU for which freq to be read.
 * @param[in,out]  value pointer to location as input where result will be updated.
 *                 Return value from r1 register will be store in value pointer
 *
 * @return         Value @return by Hypervisor in r0 register
 *
 * @pre            This API should be invoked on virtual/hypervisor environment only.
 */
NO_SANITIZE_ADDRESS static inline int hyp_pct_cpu_id_read_freq_feedback(uint8_t cpu_id,
							uint64_t *value)
{
	register uint64_t r0 asm("x0") = HVC_NR_CPU_FREQ;
	register uint64_t r1 asm("x1") = 4U;
	register uint64_t r2 asm("x2") = cpu_id;

	asm volatile("hvc #0"
		: "+r"(r0), "+r"(r1)
		: "r"(r2)
		: "x3", _X4_X17);

	if (r0 == 1 &&  value != 0)
		*value = r1;

	return (int16_t)r0;

}

/*
 * @brief          API to read frequency request for dedicated CPU
 *
 * @param[in]      cpu_id ID to the dedicated CPU for which freq to be read.
 * @param[in,out]  value pointer to location as input where result will be updated.
 *                 Return value from r1 register will be store in value pointer
 *
 * @return         Value @return by Hypervisor in r0 register
 *
 * @pre            This API should be invoked on virtual/hypervisor environment only.
 */
NO_SANITIZE_ADDRESS static inline int hyp_pct_cpu_id_read_freq_request(uint8_t cpu_id,
							uint64_t *value)
{
	register uint64_t r0 asm("x0") = HVC_NR_CPU_FREQ;
	register uint64_t r1 asm("x1") = 3U;
	register uint64_t r2 asm("x2") = cpu_id;

	asm volatile("hvc #0"
		: "+r"(r0), "+r"(r1)
		: "r"(r2)
		: "x3", _X4_X17);

	if (r0 == 1 &&  value != 0)
		*value = r1;

	return (int16_t)r0;
}

/*
 * @brief          API to write frequency request for dedicated CPU
 *
 * @param[in]      cpu_id CPU id for which freq is written
 * @param[in]      value frequency value to be written
 *
 * @return         Value @return by Hypervisor in r0 register
 *
 * @pre            This API should be invoked on virtual/hypervisor environment only.
 */
NO_SANITIZE_ADDRESS static inline int hyp_pct_cpu_id_write_freq_request(uint8_t cpu_id,
							uint64_t value)
{
	register uint64_t r0 asm("x0") = HVC_NR_CPU_FREQ;
	register uint64_t r1 asm("x1") = 5U;
	register uint64_t r2 asm("x2") = value;
	register uint64_t r3 asm("x3") = cpu_id;

	asm volatile("hvc #0"
		: "+r"(r0)
		: "r"(r1), "r"(r2), "r"(r3)
		: _X4_X17);

	return (int16_t)r0;
}

/*
 * @brief          API to get CPU count
 *
 * @return         Value @return by Hypervisor in r1 register
 *
 * @pre            This API should be invoked on virtual/hypervisor environment only.
 */
NO_SANITIZE_ADDRESS static inline uint8_t hyp_get_cpu_count(void)
{
	register uint64_t r0 asm("x0") = HVC_NR_CPU_FREQ;
	register uint64_t r1 asm("x1") = 6U;

	asm volatile("hvc #0"
		: "+r"(r0), "+r"(r1)
		:
		: "x2", "x3", _X4_X17);

	if (r0 == 1)
		return r1;

	return 0;
}

/*
 * @brief          API to trigger hypervisor call
 *
 * @param[in,out]  id Hypervisor Call id for which HVC call is being invoked
 * @param[in]      args pointer to the array of arguments of size 4.
 *                 args[4] gets updated with stored value of x0 to x3 registers
 *
 * @return         Value @return by Hypervisor in r0 register
 *
 * @pre            This API should be invoked on virtual/hypervisor environment only.
 */
NO_SANITIZE_ADDRESS static ALWAYS_INLINE inline void hyp_call44(uint16_t id,
			uint64_t *args)
{
		register uint64_t x0 asm("x0") = args[0];
		register uint64_t x1 asm("x1") = args[1];
		register uint64_t x2 asm("x2") = args[2];
		register uint64_t x3 asm("x3") = args[3];

		asm volatile("HVC %[imm16]"
			: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
			:
			[imm16] "i"(((uint32_t)id)));

		args[0] = x0;
		args[1] = x1;
		args[2] = x2;
		args[3] = x3;
}

/*
 * @brief          Get mask set for hypervisor trace buffers
 *
 * @param[in,out]  value pointer will be passed as input.
 *                 Return value from r1 register will be store in value pointer
 *
 * @return         Value @return by Hypervisor in r0 register
 *
 * @pre            This API should be invoked on virtual/hypervisor environment only.
 */
NO_SANITIZE_ADDRESS static inline int hyp_trace_get_mask(uint64_t *value)
{
	uint64_t args[4] = { TRACE_GET_EVENT_MASK, 0U, 0U, 0U };
	hyp_call44(HVC_NR_TRACE_GET_EVENT_DATA, args);
	if (args[0] == 0U)
		*value = args[1];

	return (int) args[0];
}

/*
 * @brief          Set mask for hypervisor trace buffers
 *
 * @param[in]      mask variable will be passed as input.
 *
 * @return         Value @return by Hypervisor in r0 register
 *
 * @pre            This API should be invoked on virtual/hypervisor environment only.
 */
NO_SANITIZE_ADDRESS static inline int hyp_trace_set_mask(uint64_t mask)
{
	uint64_t args[4] = { TRACE_SET_EVENT_MASK, mask, 0U, 0U};
	hyp_call44(HVC_NR_TRACE_SET_EVENT_DATA, args);
	return (int) args[0];
}

/*
 * @brief          Get the current hypervisor profiler frequency.
 *
 * @param[out]     freq Pointer to a uint64_t location where the current frequency
 *                 will be stored on success.
 *
 * @return         Function success or error code.
 * @retval         0 if successful.
 * @retval         -IVC_EINVAL if the input pointer (freq) is NULL.
 * @retval         Other non-zero value indicates a hypervisor error (e.g., permission denied).
 *
 * @pre            This API should be invoked on virtual/hypervisor environment only.
 */
NO_SANITIZE_ADDRESS static inline int hyp_trace_get_profiler_freq(uint64_t *freq)
{
	uint64_t args[4] = { TRACE_GET_PROFILER_FREQ, 0U, 0U, 0U };
	hyp_call44(HVC_NR_TRACE_GET_EVENT_DATA, args);
	if (args[0] == 0U)
		*freq = args[1];

	return (int) args[0];
}

/*
 * @brief          Set the hypervisor profiler frequency.
 *
 * @param[in]      freq The frequency value to set.
 *
 * @return         Function success or error code.
 * @retval         0 if successful.
 * @retval         Other non-zero value indicates a hypervisor error (e.g., permission denied).
 *
 * @pre            This API should be invoked on virtual/hypervisor environment only.
 */
NO_SANITIZE_ADDRESS static inline int hyp_trace_set_profiler_freq(uint64_t freq)
{
	uint64_t args[4] = { TRACE_SET_PROFILER_FREQ, freq, 0U, 0U};
	hyp_call44(HVC_NR_TRACE_SET_EVENT_DATA, args);
	return (int) args[0];
}

/*
 * @brief          Perform SMMU diagnostics.
 *
 * @details        This interface detects and reports SMMU errors that has happened
 *                 prior to calling this interface.
 *                 For all the SMMU instances this interface checks for any global fault, and context fault.
 *                 If there is/are any then those are reported along with the fault details read from fault
 *                 syndrome registers, such as the type of faults, fault address, Stream Id etc.
 *
 * @return         bool type that is based on the value held in X0 after HVC call is complete.
 *                 It @returns true if X0 holds 0x1UL, false in case X0 holds 0x0UL.
 *
 * @pre            This API should be invoked on virtual/hypervisor environment only.
 */
NO_SANITIZE_ADDRESS static inline bool hyp_smmu_diagnostic(void)
{
	uint64_t args[4] = { 0U, 0U, 0U, 0U };
	hyp_call44(HVC_NR_SMMU_DIAG, args);

	return (args[0] == 0U);
}

/*
 * @brief          Perform CBB diagnostics.
 *
 * @details        This interface detects and report CBB errors that has happened
 *                 prior to calling this interface.
 *
 * @return         bool type that is based on the value held in X0 after HVC call is complete.
 *                 It @returns true if X0 holds 0x1UL, false in case X0 holds 0x0UL.
 *
 * @pre            This API should be invoked on virtual/hypervisor environment only.
 */
NO_SANITIZE_ADDRESS static inline bool hyp_cbb_err_diagnostic(void)
{
	uint64_t args[4] = { 0U, 0U, 0U, 0U };
	hyp_call44(HVC_NR_CBB_DIAG, args);

	return (args[0] == 0U);
}

/*
 * @brief          Perform MC diagnostics.
 *
 * @details        This interface detects and report MC errors that has happened
 *                 prior to calling this interface.
 *
 * @return         bool type that is based on the value held in X0 after HVC call is complete.
 *                 It @returns true if X0 holds 0x1UL, false in case X0 holds 0x0UL.
 *
 * @pre            This API should be invoked on virtual/hypervisor environment only.
 */
NO_SANITIZE_ADDRESS static inline bool hyp_mc_err_diagnostic(void)
{
	uint64_t args[4] = { 0U, 0U, 0U, 0U };
	hyp_call44(HVC_NR_MC_DIAG, args);

	return (args[0] == 0U);
}

/*
 * @brief          Return the MPIDR value for LCPU0
 *
 * @details        This API @returns the MPIDR_EL1 value of physical (host) CPU0.  The @return value is
 *                 constant across multiple calls while the system remains powered on.
 *
 * @return         mpidr value for LCPU0
 * @note           The caller of this API is not required to have a VCPU that runs on LCPU0.
 *
 * @pre            This API should be invoked on virtual/hypervisor environment only.
 */
NO_SANITIZE_ADDRESS static inline long int hyp_lcpu0_mpidr(void)
{
	uint64_t args[4] = { 0U, 0U, 0U, 0U };
	hyp_call44(HVC_NR_LCPU0_MPIDR, args);

	return args[0];
}

#undef _X3_X17
#undef _X4_X17

#endif /* !__ASSEMBLY__ */

/** @} */

#endif /* TEGRA_SYSCALLS_H */
