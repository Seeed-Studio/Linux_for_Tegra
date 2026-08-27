// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#ifndef __NVIDIA_P2P_H__
#define __NVIDIA_P2P_H__

#include <linux/dma-mapping.h>
#include <linux/mmu_notifier.h>

#define	NVIDIA_P2P_UNINITIALIZED 0x0
#define	NVIDIA_P2P_PINNED 0x1U
#define	NVIDIA_P2P_MAPPED 0x2U

#define NVIDIA_P2P_MAJOR_VERSION_MASK   0xffff0000
#define NVIDIA_P2P_MINOR_VERSION_MASK   0x0000ffff

#define NVIDIA_P2P_MAJOR_VERSION(v) \
	(((v) & NVIDIA_P2P_MAJOR_VERSION_MASK) >> 16)

#define NVIDIA_P2P_MINOR_VERSION(v) \
	(((v) & NVIDIA_P2P_MINOR_VERSION_MASK))

#define NVIDIA_P2P_MAJOR_VERSION_MATCHES(p, v) \
	(NVIDIA_P2P_MAJOR_VERSION((p)->version) == NVIDIA_P2P_MAJOR_VERSION(v))

#define NVIDIA_P2P_VERSION_COMPATIBLE(p, v)    \
	(NVIDIA_P2P_MAJOR_VERSION_MATCHES(p, v) && \
	(NVIDIA_P2P_MINOR_VERSION((p)->version) >= \
	(NVIDIA_P2P_MINOR_VERSION(v))))

enum nvidia_p2p_page_size_type {
	NVIDIA_P2P_PAGE_SIZE_4KB = 0,
	NVIDIA_P2P_PAGE_SIZE_64KB,
	NVIDIA_P2P_PAGE_SIZE_128KB,
	NVIDIA_P2P_PAGE_SIZE_COUNT
};

typedef struct nvidia_p2p_page_table {
	u32 version;
	u32 page_size;
	u64 size;
	u32 entries;
	struct page **pages;

	u64 vaddr;
	u32 mapped;

	struct mm_struct *mm;
	struct mmu_notifier mn;
	struct mutex lock;
	void (*free_callback)(void *data);
	void *data;
} nvidia_p2p_page_table_t;

typedef struct nvidia_p2p_dma_mapping {
	u32 version;
	dma_addr_t *hw_address;
	u32 *hw_len;
	u32 entries;

	struct sg_table *sgt;
	struct device *dev;
	struct nvidia_p2p_page_table *page_table;
	enum dma_data_direction direction;
} nvidia_p2p_dma_mapping_t;

#define NVIDIA_P2P_PAGE_TABLE_VERSION   0x00010000

#define NVIDIA_P2P_PAGE_TABLE_VERSION_COMPATIBLE(p) \
	NVIDIA_P2P_VERSION_COMPATIBLE(p, NVIDIA_P2P_PAGE_TABLE_VERSION)

/*
 * @brief
 *   Make the pages underlying a range of GPU virtual memory
 *   accessible to a third-party device.
 *
 * @param[in]     vaddr
 *   A GPU Virtual Address
 * @param[in]     size
 *   The size of the requested mapping.
 *   Size must be a multiple of Page size.
 * @param[out]    **page_table
 *   A pointer to struct nvidia_p2p_page_table
 * @param[in]     free_callback
 *   A non-NULL pointer to the function to be invoked when the pages
 *   underlying the virtual address range are freed
 *   implicitly. Must be non NULL.
 * @param[in]     data
 *   A non-NULL opaque pointer to private data to be passed to the
 *   callback function.
 *
 * @return
 *    0           upon successful completion.
 *    Negative number if any error
 */
int nvidia_p2p_get_pages(u64 vaddr, u64 size,
			struct nvidia_p2p_page_table **page_table,
			void (*free_callback)(void *data), void *data);
/*
 * @brief
 * Release the pages previously made accessible to
 * a third-party device.
 *
 * @param[in]    *page_table
 *   A pointer to struct nvidia_p2p_page_table
 *
 * @return
 *    0           upon successful completion.
 *   -ENOMEM      if the driver failed to allocate memory or if
 *     insufficient resources were available to complete the operation.
 *    Negative number if any other error
 */
int nvidia_p2p_put_pages(struct nvidia_p2p_page_table *page_table);

/*
 * @brief
 * Release the pages previously made accessible to
 * a third-party device. This is called  during the
 * execution of the free_callback().
 *
 * @param[in]    *page_table
 *   A pointer to struct nvidia_p2p_page_table
 *
 * @return
 *    0           upon successful completion.
 *   -ENOMEM      if the driver failed to allocate memory or if
 *     insufficient resources were available to complete the operation.
 *    Negative number if any other error
 */
int nvidia_p2p_free_page_table(struct nvidia_p2p_page_table *page_table);

#define NVIDIA_P2P_DMA_MAPPING_VERSION   0x00010000

#define NVIDIA_P2P_DMA_MAPPING_VERSION_COMPATIBLE(p) \
	NVIDIA_P2P_VERSION_COMPATIBLE(p, NVIDIA_P2P_DMA_MAPPING_VERSION)

/*
 * @brief
 *   Map the pages retrieved using nvidia_p2p_get_pages and
 *   pass the dma address to a third-party device.
 *
 * @param[in]	*dev
 *   The peer device that needs to DMA to/from the
 *   mapping.
 * @param[in]	*page_table
 *   A pointer to struct nvidia_p2p_page_table
 * @param[out]	**map
 *   A pointer to struct nvidia_p2p_dma_mapping.
 *   The DMA mapping containing the DMA addresses to use.
 * @param[in]    direction
 *   DMA direction
 *
 * @return
 *    0           upon successful completion.
 *    Negative number if any other error
 */
int nvidia_p2p_dma_map_pages(struct device *dev,
		struct nvidia_p2p_page_table *page_table,
		struct nvidia_p2p_dma_mapping **map,
		enum dma_data_direction direction);
/*
 * @brief
 *   Unmap the pages previously mapped using nvidia_p2p_dma_map_pages
 *
 * @param[in]	*map
 *   A pointer to struct nvidia_p2p_dma_mapping.
 *   The DMA mapping containing the DMA addresses to use.
 *
 * @return
 *    0           upon successful completion.
 *    Negative number if any other error
 */
int nvidia_p2p_dma_unmap_pages(struct nvidia_p2p_dma_mapping *map);

/*
 * @brief
 *   Unmap the pages previously mapped using nvidia_p2p_dma_map_pages.
 *  This is called  during the  execution of the free_callback().
 *
 * @param[in]	*map
 *   A pointer to struct nvidia_p2p_dma_mapping.
 *   The DMA mapping containing the DMA addresses to use.
 *
 * @return
 *    0           upon successful completion.
 *    Negative number if any other error
 */
int nvidia_p2p_free_dma_mapping(struct nvidia_p2p_dma_mapping *dma_mapping);

#endif
