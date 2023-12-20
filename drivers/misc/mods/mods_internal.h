/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2008-2023, NVIDIA CORPORATION.  All rights reserved. */

#ifndef _MODS_INTERNAL_H_
#define _MODS_INTERNAL_H_

#include <linux/fb.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/version.h>

#include "mods_config.h"
#include "mods.h"

#ifdef MODS_HAS_ASM_SET_MEMORY_HEADER
#include <asm/set_memory.h>
#elif defined(MODS_HAS_SET_MEMORY_HEADER)
#include <linux/set_memory.h>
#endif

#ifdef MODS_ENABLE_BPMP_MRQ_API
#include <soc/tegra/bpmp.h>
#endif

#ifndef true
#define true	1
#define false	0
#endif

/* function return code */
#define OK		 0

#define IRQ_FOUND	 1
#define IRQ_NOT_FOUND	 0

#define DEV_FOUND	 1
#define DEV_NOT_FOUND	 0

#define MSI_DEV_FOUND	  1
#define MSI_DEV_NOT_FOUND 0

struct en_dev_entry {
	struct pci_dev	    *dev;
	struct en_dev_entry *next;
	struct msix_entry   *msix_entries;
	u32                  irq_flags;
	u32                  nvecs;
#ifdef MODS_HAS_SRIOV
	u32                  num_vfs;
#endif
	u8                   client_id;
};

struct mem_type {
	u64 phys_addr;
	u64 size;
	u8  type;
};

struct irq_q_data {
	u32		time;
	struct pci_dev *dev;
	u32		irq;
	u32		irq_index;
};

struct irq_q_info {
	struct irq_q_data data[MODS_MAX_IRQS];
	u32		  head;
	u32		  tail;
};

/* The driver can be opened simultaneously multiple times, from the same or from
 * different processes.  This structure tracks data specific to each open fd.
 */
struct mods_client {
	struct list_head         irq_list;
	struct list_head         mem_alloc_list;
	struct list_head         mem_map_list;
	struct list_head         free_mem_list; /* unused UC/WC chunks */
#if defined(CONFIG_PPC64)
	struct list_head         ppc_tce_bypass_list;
	struct list_head         nvlink_sysmem_trained_list;
#endif
	wait_queue_head_t        interrupt_event;
	struct irq_q_info        irq_queue;
	spinlock_t               irq_lock;
	struct en_dev_entry     *enabled_devices;
	struct workqueue_struct *work_queue;
	struct mem_type          mem_type;
#if defined(CONFIG_PCI)
	struct pci_dev          *cached_dev;
#endif
	struct mutex             mtx;
	int                      mods_fb_suspended[FB_MAX];
	u32                      access_token;
	atomic_t                 num_allocs;
	atomic_t                 num_pages;
#if defined(MODS_HAS_CONSOLE_LOCK)
	atomic_t                 console_is_locked;
#endif
	atomic_t                 last_bad_dbdf;
	u8                       client_id;
};

/* VM private data */
struct mods_vm_private_data {
	struct mods_client *client;
	atomic_t            usage_count;
};

/* Free WC or UC chunk, which can be reused */
struct MODS_FREE_PHYS_CHUNK {
	struct list_head list;
	struct page     *p_page;
	int              numa_node;
	u8               order;
	u8               cache_type : 2;
	u8               dma32      : 1;
};

/* DMA mapping into a PCI or non-PCI device */
struct MODS_DMA_MAP {
	struct list_head   list;
	struct pci_dev    *pcidev;     /* PCI device these mappings are for.
					* Can be NULL if the device the memory
					* was mapped to is not a PCI device.
					*/
	struct device     *dev;        /* device these mappings are for */
	struct scatterlist sg[1];      /* each entry corresponds to phys chunk
					* in sg array in MODS_MEM_INFO at the
					* same index
					*/
};

/* system memory allocation tracking */
struct MODS_MEM_INFO {
	struct list_head list;

	/* List of DMA mappings for devices other than the default
	 * PCI device specified by the dev field.
	 */
	struct list_head dma_map_list;

	u32 num_pages;      /* total number of allocated pages */
	u32 num_chunks;     /* number of allocated contig chunks */
	int numa_node;      /* numa node for the allocation */
	u8  cache_type : 2; /* MODS_ALLOC_* */
	u8  dma32      : 1; /* true/false */
	u8  force_numa : 1; /* true/false */

	struct pci_dev     *dev;         /* (optional) pci_dev this allocation
					  * is for.
					  */
	unsigned long      *wc_bitmap;   /* marks which chunks use WC/UC */
	struct scatterlist *sg;          /* current list of chunks */
	struct scatterlist  contig_sg;   /* contiguous merged chunk */
	struct scatterlist  alloc_sg[1]; /* allocated memory chunks, each chunk
					  * consists of 2^n contiguous pages
					  */
};

static inline u32 get_num_chunks(const struct MODS_MEM_INFO *p_mem_info)
{
	if (unlikely(p_mem_info->sg == &p_mem_info->contig_sg))
		return 1;

	return p_mem_info->num_chunks;
}

/* map memory tracking */
struct SYS_MAP_MEMORY {
	struct list_head list;

	/* used for offset lookup, NULL for device memory */
	struct MODS_MEM_INFO *p_mem_info;

	phys_addr_t   phys_addr;
	unsigned long virtual_addr;
	unsigned long mapping_offs;   /* mapped offset from the beginning of the allocation */
	unsigned long mapping_length; /* how many bytes were mapped */
};

struct mods_smmu_dev {
	struct device *dev;
#ifdef MODS_ENABLE_BPMP_MRQ_API
	struct tegra_bpmp *bpmp;   //bpmp node for mrq
	int cid;                   //pcie ctrl id
#endif
	char dev_name[MAX_DT_SIZE];
};

/* functions used to avoid global debug variables */
void mods_set_debug_level(int mask);
int mods_get_debug_level(void);
int mods_check_debug_level(int mask);
int mods_get_multi_instance(void);
void mods_set_multi_instance(int mi);
u32 mods_get_access_token(void);

#if defined(CONFIG_PPC64)
void mods_set_ppc_tce_bypass(int bypass);
int mods_get_ppc_tce_bypass(void);

/* PPC TCE bypass tracking */
struct PPC_TCE_BYPASS {
	struct pci_dev *dev;
	u64 dma_mask;
	struct list_head   list;
};

int has_npu_dev(struct pci_dev *dev, int index);

int mods_is_nvlink_sysmem_trained(struct mods_client *client,
				  struct pci_dev     *dev);

/* NvLink Trained tracking */
struct NVL_TRAINED {
	struct pci_dev *dev;
	u8 trained;
	struct list_head list;
};
#endif

#define IRQ_MAX			(256+PCI_IRQ_MAX)
#define PCI_IRQ_MAX		15
#define MODS_MAX_CLIENTS	32

#define IRQ_VAL_POISON		0xfafbfcfdU

/* debug print masks */
#define DEBUG_IOCTL		0x2
#define DEBUG_PCI		0x4
#define DEBUG_ACPI		0x8
#define DEBUG_ISR		0x10
#define DEBUG_MEM		0x20
#define DEBUG_FUNC		0x40
#define DEBUG_CLOCK		0x80
#define DEBUG_DETAILED		0x100
#define DEBUG_TEGRADC		0x200
#define DEBUG_TEGRADMA		0x400
#define DEBUG_ISR_DETAILED	(DEBUG_ISR | DEBUG_DETAILED)
#define DEBUG_MEM_DETAILED	(DEBUG_MEM | DEBUG_DETAILED)
#define DEBUG_ALL	        (DEBUG_IOCTL | DEBUG_PCI | DEBUG_ACPI | \
	DEBUG_ISR | DEBUG_MEM | DEBUG_FUNC | DEBUG_CLOCK | DEBUG_DETAILED | \
	DEBUG_TEGRADC | DEBUG_TEGRADMA)

#define LOG_ENT() mods_debug_printk(DEBUG_FUNC, "> %s\n", __func__)
#define LOG_EXT() mods_debug_printk(DEBUG_FUNC, "< %s\n", __func__)

#define mods_debug_printk(level, fmt, args...)\
	({ \
		if (mods_check_debug_level(level)) \
			pr_info("mods debug: " fmt, ##args); \
	})

#define cl_debug(level, fmt, args...)\
	({ \
		if (mods_check_debug_level(level)) \
			pr_info("mods [%u] debug: " fmt, client->client_id, \
				##args); \
	})

#define mods_info_printk(fmt, args...)\
	pr_info("mods: " fmt, ##args)

#define cl_info(fmt, args...)\
	pr_info("mods [%u]: " fmt, client->client_id, ##args)

#define mods_error_printk(fmt, args...)\
	pr_err("mods error: " fmt, ##args)

#define cl_error(fmt, args...)\
	pr_err("mods [%u] error: " fmt, client->client_id, ##args)

#define mods_warning_printk(fmt, args...)\
	pr_notice("mods warning: " fmt, ##args)

#define cl_warn(fmt, args...)\
	pr_notice("mods [%u] warning: " fmt, client->client_id, ##args)

struct irq_mask_info {
	void __iomem *dev_irq_mask_reg;    /*IRQ mask register, read-only reg*/
	void __iomem *dev_irq_state;       /* IRQ status register*/
	void __iomem *dev_irq_disable_reg; /* potentionally a write-only reg*/
	u64           irq_and_mask;
	u64           irq_or_mask;
	u8            mask_type;
};

struct dev_irq_map {
	u8          __iomem *dev_irq_aperture;
	u32                  apic_irq;
	u32                  entry;
	u8                   type;
	u8                   client_id;
	u8                   mask_info_cnt;
	struct irq_mask_info mask_info[MODS_IRQ_MAX_MASKS];
	struct pci_dev      *dev;
	struct list_head     list;
};

struct mods_priv {
	/* Bitmap for each allocated client id. */
	unsigned long      client_flags;

	/* Client structures */
	struct mods_client clients[MODS_MAX_CLIENTS];
};

#ifdef MODS_HAS_POLL_T
#	define POLL_TYPE __poll_t
#else
#	define POLL_TYPE unsigned int
#endif

#if ((defined(CONFIG_ARM) || defined(CONFIG_ARM64)) && \
	  !defined(CONFIG_CPA)) || defined(CONFIG_PPC64)
#	define MODS_SET_MEMORY_UC(addr, pages) 0
#	define MODS_SET_MEMORY_WC(addr, pages) 0
#	define MODS_SET_MEMORY_WB(addr, pages) 0
#else
#	define MODS_SET_MEMORY_UC(addr, pages) set_memory_uc(addr, pages)
#	define MODS_SET_MEMORY_WC(addr, pages) set_memory_wc(addr, pages)
#	define MODS_SET_MEMORY_WB(addr, pages) set_memory_wb(addr, pages)
#endif

#define MODS_PGPROT_UC pgprot_noncached
#define MODS_PGPROT_WC pgprot_writecombine

/* ACPI */
#ifdef MODS_HAS_NEW_ACPI_WALK
#define MODS_ACPI_WALK_NAMESPACE(type, start_object, max_depth, user_function, \
				 context, return_value)\
	acpi_walk_namespace(type, start_object, max_depth, user_function, NULL,\
			    context, return_value)
#else
#define MODS_ACPI_WALK_NAMESPACE acpi_walk_namespace
#endif
#ifdef MODS_HAS_NEW_ACPI_HANDLE
#define MODS_ACPI_HANDLE(dev) ACPI_HANDLE(dev)
#else
#define MODS_ACPI_HANDLE(dev) DEVICE_ACPI_HANDLE(dev)
#endif

#if KERNEL_VERSION(3, 10, 0) <= MODS_KERNEL_VERSION
#	define MODS_SG_UNMARK_END(sg) sg_unmark_end(sg)
#else
#	define MODS_SG_UNMARK_END(sg) ({(sg)->page_link &= ~2; })
#endif

#if KERNEL_VERSION(5, 11, 0) <= MODS_KERNEL_VERSION
#	define MODS_KMAP kmap_local_page
#	define MODS_KUNMAP kunmap_local
#else
#	define MODS_KMAP kmap
#	define MODS_KUNMAP kunmap
#endif

/* ************************************************************************* */
/* ** MODULE WIDE FUNCTIONS						     */
/* ************************************************************************* */

/* client */
struct mods_client *mods_client_from_id(u8 client_id);
int mods_is_client_enabled(u8 client_id);

/* irq */
void mods_init_irq(void);
struct mutex *mods_get_irq_mutex(void);
void mods_free_client_interrupts(struct mods_client *client);
POLL_TYPE mods_irq_event_check(u8 client_id);

/* mem */
const char *mods_get_prot_str(u8 mem_type);
int mods_unregister_all_alloc(struct mods_client *client);
struct MODS_MEM_INFO *mods_find_alloc(struct mods_client *client,
				      u64                 phys_addr);

#if defined(CONFIG_PPC64)
/* ppc64 */
int mods_unregister_all_ppc_tce_bypass(struct mods_client *client);

int mods_unregister_all_nvlink_sysmem_trained(struct mods_client *client);
#endif

/* pci */
#ifdef CONFIG_PCI
int mods_enable_device(struct mods_client   *client,
		       struct pci_dev       *dev,
		       struct en_dev_entry **dev_entry);
void mods_disable_device(struct mods_client *client,
			 struct pci_dev     *pdev);
#endif

#ifdef CONFIG_PCI
int mods_is_pci_dev(struct pci_dev        *dev,
		    struct mods_pci_dev_2 *pcidev);
int mods_find_pci_dev(struct mods_client    *client,
		      struct mods_pci_dev_2 *pcidev,
		      struct pci_dev       **retdev);
#else
#define mods_is_pci_dev(a, b) 0
#define mods_find_pci_dev(a, b, c) (-ENODEV)
#endif

/* clock */
#if defined(MODS_HAS_TEGRA) && defined(CONFIG_COMMON_CLK)
void mods_init_clock_api(void);
void mods_shutdown_clock_api(void);
#endif

/* ioctl hanndlers */

/* mem */
int esc_mods_alloc_pages(struct mods_client      *client,
			 struct MODS_ALLOC_PAGES *p);
int esc_mods_device_alloc_pages(struct mods_client             *client,
				struct MODS_DEVICE_ALLOC_PAGES *p);
int esc_mods_device_alloc_pages_2(struct mods_client               *client,
				  struct MODS_DEVICE_ALLOC_PAGES_2 *p);
int esc_mods_alloc_pages_2(struct mods_client        *client,
			   struct MODS_ALLOC_PAGES_2 *p);
int esc_mods_free_pages(struct mods_client     *client,
			struct MODS_FREE_PAGES *p);
int esc_mods_merge_pages(struct mods_client      *client,
			 struct MODS_MERGE_PAGES *p);
int esc_mods_set_mem_type(struct mods_client      *client,
			  struct MODS_MEMORY_TYPE *p);
int esc_mods_get_phys_addr(struct mods_client               *client,
			   struct MODS_GET_PHYSICAL_ADDRESS *p);
int esc_mods_get_phys_addr_2(struct mods_client *client,
			     struct MODS_GET_PHYSICAL_ADDRESS_3 *p);
int esc_mods_get_mapped_phys_addr(struct mods_client       *client,
			  struct MODS_GET_PHYSICAL_ADDRESS *p);
int esc_mods_get_mapped_phys_addr_2(struct mods_client                 *client,
				    struct MODS_GET_PHYSICAL_ADDRESS_2 *p);
int esc_mods_get_mapped_phys_addr_3(struct mods_client                 *client,
				    struct MODS_GET_PHYSICAL_ADDRESS_3 *p);
int esc_mods_virtual_to_phys(struct mods_client              *client,
			     struct MODS_VIRTUAL_TO_PHYSICAL *p);
int esc_mods_phys_to_virtual(struct mods_client              *client,
			     struct MODS_PHYSICAL_TO_VIRTUAL *p);
int esc_mods_dma_map_memory(struct mods_client         *client,
			    struct MODS_DMA_MAP_MEMORY *p);
int esc_mods_dma_unmap_memory(struct mods_client         *client,
			      struct MODS_DMA_MAP_MEMORY *p);
int esc_mods_iommu_dma_map_memory(struct mods_client               *client,
				  struct MODS_IOMMU_DMA_MAP_MEMORY *p);
int esc_mods_iommu_dma_unmap_memory(struct mods_client               *client,
				    struct MODS_IOMMU_DMA_MAP_MEMORY *p);

#ifdef CONFIG_ARM
int esc_mods_memory_barrier(struct mods_client *client);
#endif

#ifdef CONFIG_ARM64
int esc_mods_flush_cpu_cache_range(struct mods_client                *client,
				   struct MODS_FLUSH_CPU_CACHE_RANGE *p);
#endif

#if defined(CONFIG_PPC64)
/* ppc64 */
int esc_mods_set_ppc_tce_bypass(struct mods_client             *client,
				struct MODS_SET_PPC_TCE_BYPASS *p);
int esc_mods_get_ats_address_range(struct mods_client                *client,
				   struct MODS_GET_ATS_ADDRESS_RANGE *p);
int esc_mods_set_nvlink_sysmem_trained(struct mods_client             *client,
				struct MODS_SET_NVLINK_SYSMEM_TRAINED *p);
int esc_mods_get_nvlink_line_rate(struct mods_client               *client,
				  struct MODS_GET_NVLINK_LINE_RATE *p);
#endif

/* acpi */
#ifdef CONFIG_ACPI
int esc_mods_eval_acpi_method(struct mods_client           *client,
			      struct MODS_EVAL_ACPI_METHOD *p);
int esc_mods_eval_dev_acpi_method(struct mods_client               *client,
				  struct MODS_EVAL_DEV_ACPI_METHOD *p);
int esc_mods_eval_dev_acpi_method_2(struct mods_client                 *client,
				    struct MODS_EVAL_DEV_ACPI_METHOD_2 *p);
int esc_mods_eval_dev_acpi_method_3(struct mods_client                 *client,
				    struct MODS_EVAL_DEV_ACPI_METHOD_3 *p);
int esc_mods_acpi_get_ddc(struct mods_client       *client,
			  struct MODS_ACPI_GET_DDC *p);
int esc_mods_acpi_get_ddc_2(struct mods_client         *client,
			    struct MODS_ACPI_GET_DDC_2 *p);
int esc_mods_get_acpi_dev_children(struct mods_client    *client,
				   struct MODS_GET_ACPI_DEV_CHILDREN *p);
#ifdef MODS_HAS_PXM_TO_NODE
int esc_mods_proximity_to_numa_node(struct mods_client                 *client,
				    struct MODS_PROXIMITY_TO_NUMA_NODE *p);
#endif
#endif
/* pci */
#ifdef CONFIG_PCI
int esc_mods_find_pci_dev(struct mods_client          *client,
			  struct MODS_FIND_PCI_DEVICE *p);
int esc_mods_find_pci_dev_2(struct mods_client            *client,
			    struct MODS_FIND_PCI_DEVICE_2 *p);
int esc_mods_find_pci_class_code(struct mods_client              *client,
				 struct MODS_FIND_PCI_CLASS_CODE *p);
int esc_mods_find_pci_class_code_2(struct mods_client                *client,
				   struct MODS_FIND_PCI_CLASS_CODE_2 *p);
int esc_mods_pci_get_bar_info(struct mods_client           *client,
			      struct MODS_PCI_GET_BAR_INFO *p);
int esc_mods_pci_get_bar_info_2(struct mods_client             *client,
				struct MODS_PCI_GET_BAR_INFO_2 *p);
int esc_mods_pci_get_irq(struct mods_client      *client,
			 struct MODS_PCI_GET_IRQ *p);
int esc_mods_pci_get_irq_2(struct mods_client        *client,
			   struct MODS_PCI_GET_IRQ_2 *p);
int esc_mods_pci_read(struct mods_client *client, struct MODS_PCI_READ *p);
int esc_mods_pci_read_2(struct mods_client *client, struct MODS_PCI_READ_2 *p);
int esc_mods_pci_write(struct mods_client *client, struct MODS_PCI_WRITE *p);
int esc_mods_pci_write_2(struct mods_client      *client,
			 struct MODS_PCI_WRITE_2 *p);
int esc_mods_pci_bus_rescan(struct mods_client         *client,
			    struct MODS_PCI_BUS_RESCAN *p);
int esc_mods_pci_bus_add_dev(struct mods_client              *client,
			     struct MODS_PCI_BUS_ADD_DEVICES *p);
int esc_mods_pci_bus_remove_dev(struct mods_client        *client,
			   struct MODS_PCI_BUS_REMOVE_DEV *p);
int esc_mods_pci_hot_reset(struct mods_client        *client,
			   struct MODS_PCI_HOT_RESET *p);
int esc_mods_pio_read(struct mods_client *client, struct MODS_PIO_READ *p);
int esc_mods_pio_write(struct mods_client *client, struct MODS_PIO_WRITE *p);
int esc_mods_device_numa_info(struct mods_client           *client,
			      struct MODS_DEVICE_NUMA_INFO *p);
int esc_mods_device_numa_info_2(struct mods_client             *client,
				struct MODS_DEVICE_NUMA_INFO_2 *p);
int esc_mods_device_numa_info_3(struct mods_client             *client,
				struct MODS_DEVICE_NUMA_INFO_3 *p);
int esc_mods_get_iommu_state(struct mods_client          *client,
			     struct MODS_GET_IOMMU_STATE *state);
int esc_mods_get_iommu_state_2(struct mods_client          *client,
			       struct MODS_GET_IOMMU_STATE *state);
int esc_mods_pci_set_dma_mask(struct mods_client      *client,
			     struct MODS_PCI_DMA_MASK *dma_mask);
int esc_mods_pci_reset_function(struct mods_client    *client,
				struct mods_pci_dev_2 *pcidev);
#ifdef MODS_HAS_DEV_PROPS
int esc_mods_read_dev_property(struct mods_client            *client,
			       struct MODS_READ_DEV_PROPERTY *p);
#endif
#endif
/* irq */
#if defined(MODS_HAS_TEGRA) && defined(CONFIG_OF) && defined(CONFIG_OF_IRQ)
int esc_mods_map_irq(struct mods_client *client, struct MODS_DT_INFO *p);
int esc_mods_map_irq_to_gpio(struct mods_client *client,
						struct MODS_GPIO_INFO *p);
#endif
int esc_mods_register_irq(struct mods_client       *client,
			  struct MODS_REGISTER_IRQ *p);
int esc_mods_register_irq_2(struct mods_client         *client,
			    struct MODS_REGISTER_IRQ_2 *p);
int esc_mods_register_irq_3(struct mods_client         *client,
			    struct MODS_REGISTER_IRQ_3 *p);
int esc_mods_unregister_irq(struct mods_client       *client,
			    struct MODS_REGISTER_IRQ *p);
int esc_mods_unregister_irq_2(struct mods_client         *client,
			      struct MODS_REGISTER_IRQ_2 *p);
int esc_mods_query_irq(struct mods_client *client, struct MODS_QUERY_IRQ *p);
int esc_mods_query_irq_2(struct mods_client      *client,
			 struct MODS_QUERY_IRQ_2 *p);
int esc_mods_irq_handled(struct mods_client       *client,
			 struct MODS_REGISTER_IRQ *p);
int esc_mods_irq_handled_2(struct mods_client         *client,
			   struct MODS_REGISTER_IRQ_2 *p);

int esc_mods_register_irq_4(struct mods_client         *client,
			    struct MODS_REGISTER_IRQ_4 *p);
int esc_mods_query_irq_3(struct mods_client      *client,
			 struct MODS_QUERY_IRQ_3 *p);

#ifdef MODS_HAS_TEGRA
/* bpmp uphy */
int esc_mods_bpmp_set_pcie_state(struct mods_client *client,
				  struct MODS_SET_PCIE_STATE *p);
int esc_mods_bpmp_init_pcie_ep_pll(struct mods_client *client,
				  struct MODS_INIT_PCIE_EP_PLL *p);

/* clock */
int esc_mods_get_clock_handle(struct mods_client           *client,
			      struct MODS_GET_CLOCK_HANDLE *p);
int esc_mods_set_clock_rate(struct mods_client     *client,
			    struct MODS_CLOCK_RATE *p);
int esc_mods_get_clock_rate(struct mods_client     *client,
			    struct MODS_CLOCK_RATE *p);
int esc_mods_get_clock_max_rate(struct mods_client     *client,
				struct MODS_CLOCK_RATE *p);
int esc_mods_set_clock_max_rate(struct mods_client     *client,
				struct MODS_CLOCK_RATE *p);
int esc_mods_set_clock_parent(struct mods_client       *client,
			      struct MODS_CLOCK_PARENT *p);
int esc_mods_get_clock_parent(struct mods_client       *client,
			      struct MODS_CLOCK_PARENT *p);
int esc_mods_enable_clock(struct mods_client       *client,
			  struct MODS_CLOCK_HANDLE *p);
int esc_mods_disable_clock(struct mods_client       *client,
			   struct MODS_CLOCK_HANDLE *p);
int esc_mods_is_clock_enabled(struct mods_client        *client,
			      struct MODS_CLOCK_ENABLED *p);
int esc_mods_clock_reset_assert(struct mods_client       *client,
				struct MODS_CLOCK_HANDLE *p);
int esc_mods_clock_reset_deassert(struct mods_client       *client,
				  struct MODS_CLOCK_HANDLE *p);
int esc_mods_reset_assert(struct mods_client       *client,
			  struct MODS_RESET_HANDLE *p);
int esc_mods_get_rst_handle(struct mods_client          *client,
			    struct MODS_GET_RESET_HANDLE *p);
int esc_mods_dma_alloc_coherent(struct mods_client                  *client,
				struct MODS_DMA_COHERENT_MEM_HANDLE *p);
int esc_mods_dma_free_coherent(struct mods_client                  *client,
			       struct MODS_DMA_COHERENT_MEM_HANDLE *p);
int esc_mods_dma_copy_to_user(struct mods_client           *client,
			      struct MODS_DMA_COPY_TO_USER *p);

/* oist */
int esc_mods_oist_status(struct mods_client             *client,
			     struct MODS_TEGRA_OIST_STATUS  *p);

#ifdef CONFIG_DMA_ENGINE
int mods_init_dma(void);
void mods_exit_dma(void);
int esc_mods_dma_request_channel(struct mods_client     *client,
				 struct MODS_DMA_HANDLE *p);
int esc_mods_dma_request_channel_2(struct mods_client       *client,
				   struct MODS_DMA_HANDLE_2 *p_handle_2);
int esc_mods_dma_release_channel(struct mods_client     *client,
				 struct MODS_DMA_HANDLE *p);
int esc_mods_dma_set_config(struct mods_client             *client,
			    struct MODS_DMA_CHANNEL_CONFIG *p);
int esc_mods_dma_wait(struct mods_client *client, struct MODS_DMA_WAIT_DESC *p);
int esc_mods_dma_submit_request(struct mods_client      *client,
				struct MODS_DMA_TX_DESC *p);
int esc_mods_dma_async_issue_pending(struct mods_client     *client,
				     struct MODS_DMA_HANDLE *p);
#endif

#if defined(MODS_HAS_TEGRA) && defined(CONFIG_NET)
int esc_mods_net_force_link(struct mods_client          *client,
			    struct MODS_NET_DEVICE_NAME *p);
#endif

#if defined(MODS_HAS_TEGRA) && defined(CONFIG_DMA_SHARED_BUFFER)
int esc_mods_dmabuf_get_phys_addr(struct mods_client *client,
				  struct MODS_DMABUF_GET_PHYSICAL_ADDRESS *p);
#else
static inline int esc_mods_dmabuf_get_phys_addr(struct mods_client *client,
				  struct MODS_DMABUF_GET_PHYSICAL_ADDRESS *p)
				  { return -EINVAL; }
#endif

#ifdef CONFIG_TEGRA_NVADSP
int esc_mods_adsp_load(struct mods_client *client);
int esc_mods_adsp_start(struct mods_client *client);
int esc_mods_adsp_stop(struct mods_client *client);
int esc_mods_adsp_run_app(struct mods_client            *client,
			  struct MODS_ADSP_RUN_APP_INFO *p);
#endif

#ifdef CONFIG_TRUSTY
/* trustzone app call */
int esc_mods_send_trustzone_msg(struct mods_client         *client,
	struct MODS_TZ_PARAMS      *p);
#endif

#if IS_ENABLED(CONFIG_OPTEE)
/* OP-TEE TA call */
int esc_mods_invoke_optee_ta(struct mods_client *client,
	struct MODS_OPTEE_PARAMS *p);
#endif
#endif

/* MODS SP call */
#if defined(MODS_HAS_ARM_FFA)
int esc_mods_arm_ffa_cmd(struct mods_client     *client,
			 struct MODS_FFA_PARAMS *p);
#endif

#ifdef CONFIG_DEBUG_FS
int mods_create_debugfs(struct miscdevice *modsdev);
void mods_remove_debugfs(void);
#else
static inline int mods_create_debugfs(struct miscdevice *modsdev)
{
	return 0;
}
static inline void mods_remove_debugfs(void) {}
#endif /* CONFIG_DEBUG_FS */

#if defined(MODS_HAS_TEGRA) && defined(CONFIG_DMA_SHARED_BUFFER)
int mods_init_dmabuf(void);
void mods_exit_dmabuf(void);
#else
static inline int mods_init_dmabuf(void) { return 0; }
static inline void mods_exit_dmabuf(void) {}
#endif

#if defined(MODS_HAS_TEGRA)
int get_mods_smmu_device_index(const char *name);
struct mods_smmu_dev *get_mods_smmu_device(u32 index);
int smmu_driver_init(void);
void smmu_driver_exit(void);
#endif

#if defined(MODS_HAS_TEGRA)
int esc_mods_send_ipi(struct mods_client *client, struct MODS_SEND_IPI *p);
#endif

#if defined(CONFIG_TEGRA_IVC)
int esc_mods_bpmp_uphy_lane_eom_scan(struct mods_client *client,
				     struct MODS_BPMP_UPHY_LANE_EOM_SCAN_PARAMS *p);
int mods_bpmpipc_init(struct mods_client *client,
		      u64 db_phys_addr,
		      u64 req_phys_addr,
		      u64 resp_phys_addr);
void mods_bpmpipc_cleanup(void);
#endif

#if defined(MODS_HAS_ARM_FFA)
int mods_ffa_abi_register(void);
void mods_ffa_abi_unregister(void);
#endif

#endif	/* _MODS_INTERNAL_H_  */
