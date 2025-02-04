/*
 * SPDX-License-Identifier: GPL-2.0-only
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef __TEGRA_SYSCALLS_H__
#define __TEGRA_SYSCALLS_H__

#include <soc/tegra/virt/tegra_hv_sysmgr.h>

#define HVC_NR_READ_STAT		1
#define HVC_NR_READ_IVC			2
#define HVC_NR_READ_GID			3
#define HVC_NR_RAISE_IRQ		4
#define HVC_NR_READ_NGUESTS		5
#define HVC_NR_READ_IPA_PA		6
#define HVC_NR_READ_GUEST_STATE		7
#define HVC_NR_READ_HYP_INFO		9
#define HVC_NR_GUEST_RESET		10
#define HVC_NR_SYSINFO_IPA		13
#define HVC_NR_READ_VM_INFO		16
#define HVC_NR_TRACE_GET_EVENT_MASK		0x8003U
#define HVC_NR_TRACE_SET_EVENT_MASK		0x8004U

#define GUEST_PRIMARY		0
#define GUEST_IVC_SERVER	0
#define MAX_NVLOG_PRODUCERS	32U
#define HVC_NR_CPU_FREQ		0xC6000022

#define NGUESTS_MAX 16

#ifndef __ASSEMBLY__

#if defined(__KERNEL__)
#include <linux/types.h>
#endif

/*
 * @brief Structure describing a single NvLog producer. A producer can have
 * multiple buffers (if it has multiple threads) and the buffers are stored
 * contiguously in memory as an array. This structure provides the base
 * address, stride, and length of the array so that the consumer can locate all
 * of the buffers belonging to the producer.
 */
struct nvlog_producer {
	/* Base IPA of an array of NvLog buffers belonging to the producer. */
	uint64_t ipa;
	/* Size of the IPA region containing the NvLog buffer array. */
	uint64_t region_size;
	/* Size of a single NvLog buffer. This is the stride of the array of
	 * NvLog buffers belonging to the producer.
	 */
	uint64_t buf_size;
	/* Number of NvLog buffers belonging to the producer. */
	uint64_t buf_count;
	/* Name of the NvLog producer. */
	char name[32];
};

/*
 * Data structure for the VM Information Region.
 *
 * The Intermediate Physical Address (IPA) of this structure is returned by the
 * hyp_read_vm_info Hypercall.
 */
struct vm_info_region {
	/*
	 * Table of NvLog producers.
	 *
	 * This table is only populated for VMs that have the 'log_access' PCT flag
	 * set. Valid entries in the table have non-zero 'region_ipa', 'buf_size',
	 * and buf_count' fields.
	 */
	struct nvlog_producer nvlog_producers[MAX_NVLOG_PRODUCERS];
};

struct tegra_hv_queue_data {
	uint32_t	id;	/* IVC id */
	uint32_t	peers[2];
	uint32_t	size;
	uint32_t	nframes;
	uint32_t	frame_size;
	uint32_t	offset;
	uint16_t	irq, raise_irq;
	uint64_t	trap_ipa; /** @brief IO address used to notify peer endpoint */
	uint64_t	msi_ipa; /** @brief MSI address used to notify peer endpoint */
};

struct ivc_mempool {
	uint64_t pa;
	uint64_t size;
	uint32_t id;
	uint32_t peer_vmid;
};

struct ivc_shared_area {
	uint64_t pa;
	uint64_t size;
	uint32_t guest;
	uint16_t free_irq_start;
	uint16_t free_irq_count;
};

struct ivc_info_page {
	uint32_t nr_queues;
	uint32_t nr_areas;
	uint32_t nr_mempools;
	uint32_t padding; /**< @brief reserved for internal use */
			// IMPORTANT: Padding is needed to align
			// sizeof(struct ivc_info_page ) to 64 bits
	uint64_t trap_region_base_ipa; /**< @brief MMIO trap region start address */
	uint64_t trap_region_size; /**< @brief MMIO trap region size */
	uint64_t trap_ipa_stride; /**< @brief MMIO trap IPA stride size */
	uint64_t msi_region_base_ipa; /**< @brief MMIO msi region start address */
	uint64_t msi_region_size; /**< @brief MMIO msi region size */
	uint64_t msi_ipa_stride; /**< @brief MMIO msi IPA stride size */

	/* The actual length of this array is nr_areas. */
	struct ivc_shared_area areas[];

	/*
	 * Following the shared array is an array of queue data structures with
	 * an entry per queue that is assigned to the guest. This  array is
	 * terminated by an entry with no frames.
	 *
	 * struct tegra_hv_queue_data queue_data[nr_queues];
	 */

	/*
	 * Following the queue data array is an array of mempool structures
	 * with an entry per mempool assigned to the guest.
	 *
	 * struct ivc_mempool[nr_mempools];
	 */
};

static inline struct ivc_shared_area *ivc_shared_area_addr(
		const struct ivc_info_page *info, uint32_t area_num)
{
	return ((struct ivc_shared_area *) (((uintptr_t) info) + sizeof(*info)))
		+ area_num;
}

static inline const struct tegra_hv_queue_data *ivc_info_queue_array(
		const struct ivc_info_page *info)
{
	return (struct tegra_hv_queue_data *)&info->areas[info->nr_areas];
}

static inline const struct ivc_mempool *ivc_info_mempool_array(
		const struct ivc_info_page *info)
{
	return (struct ivc_mempool *)
			&ivc_info_queue_array(info)[info->nr_queues];
}

struct hyp_ipa_pa_info {
	uint64_t base;       /* base of contiguous pa region */
	uint64_t offset;     /* offset for requested ipa address */
	uint64_t size;       /* size of pa region */
};

#define HVC_MAX_VCPU 64

struct trapped_access {
	uint64_t ipa;
	uint32_t size;
	int32_t write_not_read;
	uint64_t data;
	uint32_t guest_id;
};

/* Structure to store the IPA, Length and Name. */
struct trace_buf {
/* @brief IPA of trace buffer region. */
	uint64_t ipa;
/* @brief Length of trace buffer region. */
	uint64_t size;
/* @brief Name */
	char name[32];
};

struct hyp_server_page {
	/* guest reset protocol */
	uint32_t guest_reset_virq;

	/* boot delay offsets per VM needed by monitor partition */
	uint32_t boot_delay[NGUESTS_MAX];

	/* hypervisor trace log */
	uint64_t log_ipa;
	uint32_t log_size;

	/* secure-hypervisor trace log */
	uint64_t secure_log_ipa;
	uint32_t secure_log_size;

	/* PCT data */
	uint64_t pct_ipa;
	uint64_t pct_size;

	/* check if the VM is a server or a guest */
	uint32_t is_server_vm;

	/* golden register data */
	uint64_t gr_ipa;
	uint32_t gr_size;

	/* all vm mappings ipa */
	uint64_t mappings_ipa;

	/* IPA, Length and Name for trace buffer region. */
	struct trace_buf trace_buffs[2*NGUESTS_MAX];

};

/* For backwards compatibility, alias the old name for hyp_server_name. */
#define hyp_info_page hyp_server_page

#define _X3_X17 "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", \
"x13", "x14", "x15", "x16", "x17"

#define _X4_X17 "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", \
"x13", "x14", "x15", "x16", "x17"

__attribute__((no_sanitize_address))
static inline int hyp_read_gid(unsigned int *gid)
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

__attribute__((no_sanitize_address))
static inline int hyp_read_nguests(unsigned int *nguests)
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

__attribute__((no_sanitize_address))
static inline int hyp_read_ivc_info(uint64_t *ivc_info_page_pa)
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

__attribute__((no_sanitize_address))
static inline int hyp_read_ipa_pa_info(struct hyp_ipa_pa_info *info, unsigned int guestid, uint64_t ipa)
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

__attribute__((no_sanitize_address))
static inline int hyp_raise_irq(unsigned int irq, unsigned int vmid)
{
	register uint64_t r0 asm("x0") = irq;
	register uint64_t r1 asm("x1") = vmid;

	asm volatile("hvc %1"
		: "+r"(r0)
		: "i"(HVC_NR_RAISE_IRQ), "r"(r1)
		: "x2", "x3", _X4_X17);

	return (int)r0;
}

__attribute__((no_sanitize_address))
static inline int hyp_read_guest_state(unsigned int vmid, unsigned int *state)
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

__attribute__((no_sanitize_address))
static inline int hyp_read_hyp_info(uint64_t *hyp_info_page_pa)
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

__attribute__((no_sanitize_address))
static inline int hyp_guest_reset(unsigned int id, struct hyp_sys_state_info *out)
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
static inline uint64_t hyp_sysinfo_ipa(void)
{
	register uint64_t r0 asm("x0");

	asm("hvc %1"
		: "=r"(r0)
		: "i"(HVC_NR_SYSINFO_IPA)
		: "x1", "x2", "x3", _X4_X17);

	return r0;
}

__attribute__((no_sanitize_address))
static inline int hyp_read_freq_feedback(uint64_t *value)
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

__attribute__((no_sanitize_address))
static inline int hyp_read_freq_request(uint64_t *value)
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

__attribute__((no_sanitize_address))
static inline int hyp_write_freq_request(uint64_t value)
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

__attribute__((no_sanitize_address))
static inline int hyp_pct_cpu_id_read_freq_feedback(uint8_t cpu_id, uint64_t *value)
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

__attribute__((no_sanitize_address))
static inline int hyp_pct_cpu_id_read_freq_request(uint8_t cpu_id, uint64_t *value)
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

__attribute__((no_sanitize_address))
static inline int hyp_pct_cpu_id_write_freq_request(uint8_t cpu_id, uint64_t value)
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

__attribute__((no_sanitize_address))
static inline uint8_t hyp_get_cpu_count(void)
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

__attribute__((no_sanitize_address))
static __attribute__((always_inline)) inline void hyp_call44(uint16_t id, uint64_t args[4])
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

__attribute__((no_sanitize_address))
static inline int hyp_trace_get_mask(uint64_t *value)
{
	uint64_t args[4] = { *value, 0U, 0U, 0U };

	hyp_call44(HVC_NR_TRACE_GET_EVENT_MASK, args);
	if (args[0] == 0U)
		*value = args[1];

	return (int) args[0];
}

__attribute__((no_sanitize_address))
static inline int hyp_trace_set_mask(uint64_t type, uint64_t value)
{
	uint64_t args[4] = { type, value, 0U, 0U };

	hyp_call44(HVC_NR_TRACE_SET_EVENT_MASK, args);
	return (int) args[0];
}

#undef _X3_X17
#undef _X4_X17

#endif /* !__ASSEMBLY__ */

#endif /* __TEGRA_SYSCALLS_H__ */
