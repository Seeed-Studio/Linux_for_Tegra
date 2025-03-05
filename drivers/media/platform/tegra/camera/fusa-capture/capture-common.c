// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2017-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

/**
 * @file drivers/media/platform/tegra/camera/fusa-capture/capture-common.c
 *
 * @brief VI/ISP channel common operations for the T186/T194 Camera RTCPU
 * platform.
 */

#include <nvidia/conftest.h>

#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/nospec.h>
#include <linux/nvhost.h>
#include <linux/slab.h>
#include <linux/hashtable.h>
#include <linux/atomic.h>
#include <media/mc_common.h>

#include <media/fusa-capture/capture-common.h>


/**
 * @brief Capture buffer management table.
 */
struct capture_buffer_table {
	struct device *dev; /**< Originating device (VI or ISP) */
	struct kmem_cache *cache; /**< SLAB allocator cache */
	rwlock_t hlock; /**< Reader/writer lock on table contents */
	DECLARE_HASHTABLE(hhead, 4U); /**< Buffer hashtable head */
};

/**
 * @brief Capture surface NvRm and IOVA addresses handle.
 */
union capture_surface {
	uint64_t raw; /**< Pinned VI or ISP IOVA address */
	struct {
		uint32_t offset; /**< NvRm handle (upper 32 bits) */
		uint32_t hmem;
			/**<
			 * Offset of surface or pushbuffer address in descriptor
			 * (lower 32 bits) [byte]
			 */
	};
};

/**
 * @brief Capture buffer mapping (pinned).
 */
struct capture_mapping {
	struct hlist_node hnode; /**< Hash table node struct */
	struct list_head free_list;   /* List for cleanup */
	atomic_t refcnt; /**< Capture mapping reference count */
	struct dma_buf *buf; /** Capture mapping dma_buf */
	struct dma_buf_attachment *atch;
		/**< dma_buf attachment (VI or ISP device) */
	struct sg_table *sgt; /**< Scatterlist to dma_buf attachment */
	unsigned int flag; /**< Bitmask access flag */
};

/**
 * @brief Determines if all flags in @a other are set in @a self.
 *
 * This function performs the following operations:
 * - Computes the bitwise AND of @a self and @a other.
 * - Compares the result with @a other to determine compatibility.
 *
 * @param[in]  self   The source flags to be checked.
 *                    Valid range: [0 .. UINT32_MAX].
 * @param[in]  other  The flags to verify against @a self.
 *                    Valid range: [0 .. UINT32_MAX].
 *
 * @retval true   All flags in @a other are set in @a self.
 * @retval false  Not all flags in @a other are set in @a self.
 */
static inline bool flag_compatible(
	unsigned int self,
	unsigned int other)
{
	return (self & other) == other;
}

/**
 * @brief Extracts the access mode from the provided flag.
 *
 * This function performs the following operations:
 * - Applies a bitmask to the input flag to isolate the access mode bits using
 *   @ref BUFFER_RDWR.
 *
 * @param[in]  flag    The input flags containing access mode information.
 *                     Valid range: [0 .. UINT32_MAX].
 *
 * @retval 0                                       No access mode flags are set.
 * @retval @ref BUFFER_READ                        Read access mode is set.
 * @retval @ref BUFFER_WRITE                       Write access mode is set.
 * @retval (@ref BUFFER_READ | @ref BUFFER_WRITE)  Both read and write access
 *                                                 modes are set.
 */
static inline unsigned int flag_access_mode(
	unsigned int flag)
{
	return flag & BUFFER_RDWR;
}

/**
 * @brief Determines the DMA data direction based on the provided flag.
 *
 * This function performs the following operations:
 * - Calls @ref flag_access_mode() to determine the access mode from @a flag.
 * - Uses the access mode to index into a predefined array mapping to the corresponding
 *   @ref dma_data_direction.
 *
 * @param[in]  flag    Flag indicating the desired DMA data direction.
 *                     Valid range: [0 .. UINT32_MAX].
 *
 * @retval DMA_BIDIRECTIONAL  Returned when @ref flag_access_mode() indicates
 *                            bidirectional access.
 * @retval DMA_TO_DEVICE      Returned when @ref flag_access_mode() indicates
 *                            write access.
 * @retval DMA_FROM_DEVICE    Returned when @ref flag_access_mode() indicates
 *                            read access.
 */
static inline enum dma_data_direction flag_dma_direction(
	unsigned int flag)
{
	static const enum dma_data_direction dir[4U] = {
		[0U] = DMA_BIDIRECTIONAL,
		[BUFFER_READ] = DMA_TO_DEVICE,
		[BUFFER_WRITE] = DMA_FROM_DEVICE,
		[BUFFER_RDWR] = DMA_BIDIRECTIONAL,
	};

	return dir[flag_access_mode(flag)];
}

/**
 * @brief Maps a capture mapping and memory offset to a DMA IOVA address.
 *
 * This inline function calculates the I/O Virtual Address (IOVA) based on a given
 * capture mapping and a memory offset. The implementation varies depending
 * on the Linux kernel version:
 *
 * - **For Linux versions below 5.10.0:**
 *   - Retrieves the DMA address from the scatter-gather table using @ref sg_dma_address().
 *   - If @ref sg_dma_address() returns 0, obtains the physical address using @ref sg_phys().
 *   - Adds the provided @a mem_offset to the obtained address.
 *
 * - **For Linux versions 5.10.0 and above:**
 *   - Traverses the scatterlist using @ref for_each_sgtable_dma_sg().
 *   - For each scatterlist entry, obtains the length using @ref sg_dma_len().
 *   - Retrieves the address using @ref sg_dma_address() or @ref sg_phys().
 *   - Checks for overflow using @ref check_add_overflow().
 *   - If an overflow is detected or no suitable scatterlist entry is found, returns 0.
 *
 * @param[in]  pin         Pointer to the @ref capture_mapping structure.
 *                         Valid value: non-null.
 * @param[in]  mem_offset  Memory offset to be added to the base address.
 *                         Valid range: [0 .. UINT64_MAX].
 *
 * @retval (dma_addr_t)  The calculated IOVA address based on @a pin and @a mem_offset.
 * @retval 0             Indicates either:
 *                         - An overflow occurred during address calculation as detected by
 *                           @ref check_add_overflow().
 *                         - No suitable scatterlist entry was found using
 *                           @ref for_each_sgtable_dma_sg(), @ref sg_phys(), or
 *                           @ref sg_dma_address().
 */
static inline dma_addr_t mapping_iova(
	const struct capture_mapping *pin,
	uint64_t mem_offset)
{
	dma_addr_t iova = 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	iova = (sg_dma_address(pin->sgt->sgl) != 0) ? sg_dma_address(pin->sgt->sgl) :
		sg_phys(pin->sgt->sgl);
	iova += mem_offset;
#else
	struct scatterlist *sg;
	uint64_t mem_offset_adjusted = mem_offset;
	int i;

	/* Traverse the scatterlist and adjust the offset
	 * as per the page block. This is needed in case
	 * where memory spans across multiple pages and
	 * is non-contiguous
	 */
	for_each_sgtable_dma_sg(pin->sgt, sg, i) {
		if (mem_offset_adjusted < sg_dma_len(sg)) {
			iova = (sg_dma_address(sg) == 0) ? sg_phys(sg) : sg_dma_address(sg);
			if (check_add_overflow(iova, mem_offset_adjusted, &iova))
				return 0;
			break;
		}
		mem_offset_adjusted -=  sg_dma_len(sg);
	}
#endif

	return iova;
}

/**
 * @brief Retrieves the DMA buffer from a capture mapping.
 *
 * This function performs the following operations:
 * - Accesses the @a buf member of the provided @ref capture_mapping structure.
 *
 * @param[in]  pin    Pointer to the @ref capture_mapping structure.
 *                    Valid value: non-null.
 *
 * @retval (dma_buf *)  Pointer to the @ref dma_buf structure contained in @a pin.
 * @retval NULL         If the @a buf member in @a pin is not set.
 */
static inline struct dma_buf *mapping_buf(
	const struct capture_mapping *pin)
{
	return pin->buf;
}

/**
 * @brief Checks if the mapping is preserved based on the BUFFER_ADD flag.
 *
 * This function performs the following operations:
 * - Validates the input parameter.
 * - Checks if the BUFFER_ADD flag is set in the provided @ref capture_mapping structure.
 *
 * @param[in]  pin         Pointer to the @ref capture_mapping structure.
 *                         Valid value: non-null.
 *
 * @retval true   Indicates that the BUFFER_ADD flag is set, preserving the mapping.
 * @retval false  Indicates that the BUFFER_ADD flag is not set, not preserving the mapping.
 */
static inline bool mapping_preserved(
	const struct capture_mapping *pin)
{
	return (bool)(pin->flag & BUFFER_ADD);
}

/**
 * @brief Sets or clears the BUFFER_ADD flag and updates the reference count.
 *
 * This inline function modifies the BUFFER_ADD flag within the provided
 * @ref capture_mapping structure based on the boolean value provided. It also
 * updates the reference count accordingly:
 *
 * - If @a val is `true`:
 *   - Sets the BUFFER_ADD flag in the @a pin structure.
 *   - Increments the reference count using @ref atomic_inc().
 *
 * - If @a val is `false`:
 *   - Clears the BUFFER_ADD flag in the @a pin structure.
 *   - Decrements the reference count using @ref atomic_dec().
 *
 * @param[in, out] pin  Pointer to the @ref capture_mapping structure.
 *                      Valid value: non-null.
 * @param[in]      val  lean value indicating whether to preserve mapping.
 *                      Valid value: `true` or `false`.
 */
static inline void set_mapping_preservation(
	struct capture_mapping *pin,
	bool val)
{
	if (val) {
		pin->flag |= BUFFER_ADD;
		atomic_inc(&pin->refcnt);
	} else {
		pin->flag &= (~BUFFER_ADD);
		atomic_dec(&pin->refcnt);
	}
}

/**
 * @brief Searches for a capture mapping in the buffer table matching the specified buffer and flag.
 *
 * This function performs the following operations:
 * - Acquires a read lock on the buffer table using @ref read_lock().
 * - Iterates over possible hash entries using @ref hash_for_each_possible() to find a
 *   matching @ref capture_mapping.
 * - For each entry, checks if the buffer matches and the flags are compatible using
 *   @ref flag_compatible().
 * - If a match is found, attempts to increment the reference count using
 *   @ref atomic_inc_not_zero().
 * - If the reference count is successfully incremented, releases the read lock using
 *   @ref read_unlock() and returns the matching @ref capture_mapping.
 * - Releases the read lock using @ref read_unlock() if no matching mapping is found
 *   or the reference count cannot be incremented.
 *
 * @param[in]  tab    Pointer to the @ref capture_buffer_table structure.
 *                    Valid value: non-null.
 * @param[in]  buf    Pointer to the @ref dma_buf structure to search for.
 *                    Valid value: non-null.
 * @param[in]  flag   Flags to match against the capture mappings.
 *                    Valid range: [0 .. UINT32_MAX].
 *
 * @retval (capture_mapping *)  Pointer to the matching @ref capture_mapping structure if found.
 * @retval NULL                 No matching capture mapping was found or
 *                              @ref atomic_inc_not_zero() failed, influenced by
 *                              @ref read_lock(), @ref hash_for_each_possible(),
 *                              @ref flag_compatible(), @ref atomic_inc_not_zero() or
 *                              @ref read_unlock().
 */
static struct capture_mapping *find_mapping(
	struct capture_buffer_table *tab,
	struct dma_buf *buf,
	unsigned int flag)
{
	struct capture_mapping *pin;
	bool success;

	read_lock(&tab->hlock);

	hash_for_each_possible(tab->hhead, pin, hnode, (unsigned long)buf) {
		if (
			(pin->buf == buf) &&
			flag_compatible(pin->flag, flag)
		) {
			success =  atomic_inc_not_zero(&pin->refcnt);
			if (success) {
				read_unlock(&tab->hlock);
				return pin;
			}
		}
	}

	read_unlock(&tab->hlock);

	return NULL;
}

/**
 * @brief Retrieves or creates a capture mapping for a given file descriptor and flags.
 *
 * This function performs the following operations:
 *   - Checks if the input parameter @a tab is non-null.
 *   - Calls @ref dma_buf_get() to obtain a @ref dma_buf structure for the provided file
 *     descriptor @a fd.
 *   - Calls @ref find_mapping() to search for an existing @ref capture_mapping that matches
 *     the buffer @a buf and flags @a flag.
 *   - If a matching mapping is found, calls @ref dma_buf_put() to release the buffer and returns
 *     the existing mapping.
 *   - Allocates a new @ref capture_mapping structure using @ref kmem_cache_alloc().
 *   - Calls @ref dma_buf_attach() to attach the buffer @a buf to the device associated with
 *     @a tab.
 *     - If @ref dma_buf_attach() fails, frees the allocated mapping with @ref kmem_cache_free().
 *   - Determines the DMA data direction by calling @ref flag_dma_direction() with @a flag.
 *   - Calls @ref dma_buf_map_attachment() to map the DMA buffer.
 *     - If @ref dma_buf_map_attachment() fails, detaches the buffer with @ref dma_buf_detach().
 *   - Initializes the @ref capture_mapping structure with the provided flags and buffer.
 *   - Sets the reference count to 1 and initializes the hash node.
 *   - Acquires a write lock on the buffer table using @ref write_lock().
 *   - Adds the new mapping to the buffer table's hash using @ref hash_add().
 *   - Releases the write lock using @ref write_unlock().
 *
 * @param[in]  tab   Pointer to the @ref capture_buffer_table structure.
 *                   Valid value: non-null.
 * @param[in]  fd    File descriptor associated with the DMA buffer.
 *                   Valid range: [0 .. UINT32_MAX].
 * @param[in]  flag  Flags indicating the desired DMA data direction.
 *                   Valid range: [0 .. UINT32_MAX].
 *
 * @retval (capture_mapping *)       Pointer to the matching or newly created @ref capture_mapping.
 * @retval @ref ERR_PTR(-EINVAL)     If @a tab is `NULL`.
 * @retval @ref ERR_CAST(buf)        If @ref dma_buf_get() fails.
 * @retval @ref ERR_PTR(-ENOMEM)     If memory allocation via @ref kmem_cache_alloc() fails.
 * @retval @ref ERR_PTR()            If @ref dma_buf_attach() or @ref dma_buf_map_attachment() fails.
 */
static struct capture_mapping *get_mapping(
	struct capture_buffer_table *tab,
	uint32_t fd,
	unsigned int flag)
{
	struct capture_mapping *pin;
	struct dma_buf *buf;
	void *err;

	if (unlikely(tab == NULL)) {
		pr_err("%s: invalid buffer table\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	buf = dma_buf_get((int)fd);
	if (IS_ERR(buf)) {
		dev_err(tab->dev, "%s:%d: invalid memfd %u; errno %ld \n",
			 __func__, __LINE__, fd, PTR_ERR(buf));
		return ERR_CAST(buf);
	}

	pin = find_mapping(tab, buf, flag);
	if (pin != NULL) {
		dma_buf_put(buf);
		return pin;
	}

	pin = kmem_cache_alloc(tab->cache, GFP_KERNEL);
	if (unlikely(pin == NULL)) {
		err = ERR_PTR(-ENOMEM);
		goto err0;
	}

	pin->atch = dma_buf_attach(buf, tab->dev);
	if (unlikely(IS_ERR(pin->atch))) {
		err = pin->atch;
		goto err1;
	}

	pin->sgt = dma_buf_map_attachment(pin->atch, flag_dma_direction(flag));
	if (unlikely(IS_ERR(pin->sgt))) {
		err = pin->sgt;
		goto err2;
	}

	pin->flag = flag;
	pin->buf = buf;
	atomic_set(&pin->refcnt, 1U);
	INIT_HLIST_NODE(&pin->hnode);

	write_lock(&tab->hlock);
	hash_add(tab->hhead, &pin->hnode, (unsigned long)pin->buf);
	write_unlock(&tab->hlock);

	return pin;
err2:
	dma_buf_detach(buf, pin->atch);
err1:
	kmem_cache_free(tab->cache, pin);
err0:
	dma_buf_put(buf);
	dev_err(tab->dev, "%s:%d: memfd %u, flag %u; errno %ld \n",
		__func__, __LINE__,fd, flag, PTR_ERR(buf));
	return err;
}

/**
 * @brief Creates and initializes a capture buffer table for the specified device.
 *
 * This function performs the following operations:
 * - Allocates memory for a @ref capture_buffer_table structure using @ref kmalloc().
 * - Initializes a memory cache for @ref capture_mapping structures using @ref KMEM_CACHE().
 *   - If @ref KMEM_CACHE() fails, frees the allocated table using @ref kfree().
 * - Sets the device pointer within the table to the provided @a dev.
 * - Initializes the hash table using @ref hash_init().
 * - Initializes the read-write lock using @ref rwlock_init().
 *
 * @param[in]  dev   Pointer to the @ref device structure.
 *                   Valid value: non-null.
 *
 * @retval (capture_buffer_table *)  Pointer to the newly created @ref capture_buffer_table.
 * @retval NULL                      If memory allocation via @ref kmalloc() or cache creation via
 *                                   @ref KMEM_CACHE() fails.
 */
struct capture_buffer_table *create_buffer_table(
	struct device *dev)
{
	struct capture_buffer_table *tab;

	tab = kmalloc(sizeof(*tab), GFP_KERNEL);

	if (likely(tab != NULL)) {
		tab->cache = KMEM_CACHE(capture_mapping, 0U);

		if (likely(tab->cache != NULL)) {
			tab->dev = dev;
			hash_init(tab->hhead);
			rwlock_init(&tab->hlock);
		} else {
			kfree(tab);
			tab = NULL;
		}
	}

	return tab;
}
EXPORT_SYMBOL_GPL(create_buffer_table);

/**
 * @brief Destroys and frees the specified capture buffer table.
 *
 * This function performs the following operations:
 * - Checks if the input parameter @a tab is non-null.
 * - Iterates over all capture mappings in the buffer table using
 *   @ref hash_for_each_safe().
 *   - For each @ref capture_mapping:
 *     - Acquires a write lock on the buffer table using @ref write_lock().
 *     - Removes the mapping from the hash table using @ref hash_del().
 *     - Releases the write lock using @ref write_unlock().
 *     - Unmaps the DMA attachment using @ref dma_buf_unmap_attachment().
 *     - Detaches the DMA buffer using @ref dma_buf_detach().
 *     - Releases the DMA buffer using @ref dma_buf_put().
 *     - Frees the capture mapping structure using @ref kmem_cache_free().
 * - Destroys the memory cache for capture mappings using
 *   @ref kmem_cache_destroy().
 * - Frees the buffer table using @ref kfree().
 *
 * @param[in]  tab  Pointer to the @ref capture_buffer_table structure.
 *                  Valid value: non-null.
 */
void destroy_buffer_table(
	struct capture_buffer_table *tab)
{
	size_t bkt;
	struct hlist_node *next;
	struct capture_mapping *pin, *temp;
	struct list_head tmp_list;

	if (unlikely(tab == NULL))
		return;

	/* First acquire write lock and build list of entries to free */
	write_lock(&tab->hlock);

	INIT_LIST_HEAD(&tmp_list);

	hash_for_each_safe(tab->hhead, bkt, next, pin, hnode) {
		/* Remove from hash table but keep pin structure */
		hash_del(&pin->hnode);
		/* Add to our free list */
		list_add(&pin->free_list, &tmp_list);
	}

	/* Release lock - hash table is now empty */
	write_unlock(&tab->hlock);

	/* Now safe to free all the entries without holding lock */
	list_for_each_entry_safe(pin, temp, &tmp_list, free_list) {
		list_del(&pin->free_list);

		/* Free the mapping resources */
		dma_buf_unmap_attachment(
			pin->atch, pin->sgt, flag_dma_direction(pin->flag));
		dma_buf_detach(pin->buf, pin->atch);
		dma_buf_put(pin->buf);
		kmem_cache_free(tab->cache, pin);
	}

	kmem_cache_destroy(tab->cache);
	kfree(tab);
}
EXPORT_SYMBOL_GPL(destroy_buffer_table);

static DEFINE_MUTEX(req_lock);

/**
 * @brief Requests to add or remove a buffer in the capture buffer table.
 *
 * This function performs the following operations:
 * - Validates the input parameters.
 * - Acquires the request mutex lock using @ref mutex_lock().
 * - If adding a buffer:
 *   - Calls @ref get_mapping() to retrieve the mapping for the given @a memfd and access
 *     mode obtained from @ref flag_access_mode().
 *   - Checks if @ref get_mapping() returned an error using @ref IS_ERR().
 *   - Checks if the mapping is already preserved using @ref mapping_preserved().
 * - If removing a buffer:
 *   - Calls @ref dma_buf_get() to obtain the DMA buffer for the given @a memfd.
 *   - Checks if @ref dma_buf_get() returned an error using @ref IS_ERR().
 *   - Calls @ref find_mapping() to find the corresponding capture mapping.
 *   - Calls @ref dma_buf_put() to release the DMA buffer reference.
 * - Sets the mapping preservation state by calling @ref set_mapping_preservation().
 * - Releases the capture mapping by calling @ref put_mapping().
 * - Releases the request mutex lock using @ref mutex_unlock().
 *
 * In case of any errors during the operations, the function ensures that the mutex
 * lock is released before returning the error code.
 *
 * @param[in]  tab    Pointer to the @ref capture_buffer_table structure.
 *                    Valid value: non-null.
 * @param[in]  memfd  File descriptor associated with the buffer.
 *                    Valid range: [0 .. UINT32_MAX].
 * @param[in]  flag   Flags indicating the operation to perform.
 *                    Valid range: [0 .. UINT32_MAX].
 *
 * @retval 0            Operation completed successfully.
 * @retval -EINVAL      If the input buffer table @a tab is NULL.
 * @retval -EEXIST      If attempting to add a buffer that already exists, as determined
 *                      by @ref mapping_preserved().
 * @retval -ENOENT      If attempting to remove a buffer that does not exist in the table.
 * @retval (int)        Errors returned by @ref get_mapping() or @ref dma_buf_get().
 */
int capture_buffer_request(
	struct capture_buffer_table *tab,
	uint32_t memfd,
	uint32_t flag)
{
	struct capture_mapping *pin;
	struct dma_buf *buf;
	bool add = (bool)(flag & BUFFER_ADD);
	int err = 0;

	if (unlikely(tab == NULL)) {
		pr_err("%s: invalid buffer table\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&req_lock);

	if (add) {
		pin = get_mapping(tab, memfd, flag_access_mode(flag));
		if (IS_ERR(pin)) {
			err = PTR_ERR_OR_ZERO(pin);
			dev_err(tab->dev, "%s:%d: memfd %u, flag %u; errno %d",
				__func__, __LINE__, memfd, flag, err);
			goto end;
		}

		if (mapping_preserved(pin)) {
			err = -EEXIST;
			dev_err(tab->dev, "%s:%d: memfd %u exists; errno %d",
				__func__, __LINE__, memfd, err);
			put_mapping(tab, pin);
			goto end;
		}
	} else {
		buf = dma_buf_get((int)memfd);
		if (IS_ERR(buf)) {
			err = PTR_ERR_OR_ZERO(buf);
			dev_err(tab->dev, "%s:%d: invalid memfd %u; errno %d",
				__func__, __LINE__, memfd, err);
			goto end;
		}

		pin = find_mapping(tab, buf, BUFFER_ADD);
		if (pin == NULL) {
			err = -ENOENT;
			dev_err(tab->dev, "%s:%d: memfd %u not exists; errno %d",
				__func__, __LINE__, memfd, err);
			dma_buf_put(buf);
			goto end;
		}
		dma_buf_put(buf);
	}

	set_mapping_preservation(pin, add);
	put_mapping(tab, pin);

end:
	mutex_unlock(&req_lock);
	return err;
}
EXPORT_SYMBOL_GPL(capture_buffer_request);

/**
 * @brief Adds a buffer to the capture buffer table with read-write permissions.
 *
 * This function performs the following operations:
 * - Calls @ref capture_buffer_request() to request the addition of a buffer identified
 *   by @a fd to the capture buffer table @a t with flags BUFFER_ADD | BUFFER_RDWR.
 *   - The flags indicate that the buffer should be added and have read-write access.
 *
 * @param[in]  t   Pointer to the @ref capture_buffer_table structure.
 *                 Valid value: non-null.
 * @param[in]  fd  File descriptor representing the buffer to be added.
 *                 Valid range: [0 .. UINT32_MAX].
 *
 * @retval 0            Buffer added successfully.
 * @retval (int)        Errors returned by @ref capture_buffer_request().
 */
int capture_buffer_add(
	struct capture_buffer_table *t,
	uint32_t fd)
{
	return capture_buffer_request(t, fd, BUFFER_ADD | BUFFER_RDWR);
}
EXPORT_SYMBOL_GPL(capture_buffer_add);

/**
 * @brief Releases a capture mapping and cleans up if it is no longer referenced.
 *
 * This function performs the following operations:
 * - Decrements the reference count of the @ref capture_mapping structure using
 *   @ref atomic_dec_and_test().
 * - If the reference count reaches zero:
 *   - Checks if the mapping is preserved using @ref mapping_preserved().
 *     - If the mapping is preserved, increments the reference count using
 *       @ref atomic_inc().
 *   - Otherwise:
 *     - Acquires a write lock on the buffer table using @ref write_lock().
 *     - Removes the mapping from the hash table using @ref hash_del().
 *     - Releases the write lock using @ref write_unlock().
 *     - Unmaps the DMA attachment using @ref dma_buf_unmap_attachment().
 *     - Detaches the DMA buffer using @ref dma_buf_detach().
 *     - Releases the DMA buffer using @ref dma_buf_put().
 *     - Frees the capture mapping structure using @ref kmem_cache_free().
 *
 * @param[in]  t    Pointer to the @ref capture_buffer_table structure.
 *                  Valid value: non-null.
 * @param[in]  pin  Pointer to the @ref capture_mapping structure.
 *                  Valid value: non-null.
 */
void put_mapping(
	struct capture_buffer_table *t,
	struct capture_mapping *pin)
{
	bool zero;

	zero = atomic_dec_and_test(&pin->refcnt);
	if (zero) {
		if (unlikely(mapping_preserved(pin))) {
			dev_err(t->dev, "%s:%d: unexpected put for a preserved mapping",
				__func__, __LINE__);
			atomic_inc(&pin->refcnt);
			return;
		}

		write_lock(&t->hlock);
		hash_del(&pin->hnode);
		write_unlock(&t->hlock);

		dma_buf_unmap_attachment(
			pin->atch, pin->sgt, flag_dma_direction(pin->flag));
		dma_buf_detach(pin->buf, pin->atch);
		dma_buf_put(pin->buf);
		kmem_cache_free(t->cache, pin);
	}
}
EXPORT_SYMBOL_GPL(put_mapping);

/**
 * @brief Pins a memory buffer and retrieves its IOVA address based on the handle and offset.
 *
 * This function performs the following operations:
 * - Checks if the memory handle @a mem_handle is zero.
 * - Validates that the number of unpins in @a unpins does not exceed
 *   @ref MAX_PIN_BUFFER_PER_REQUEST.
 * - Calls @ref get_mapping() to retrieve or create a @ref capture_mapping based on
 *   the buffer context @a buf_ctx and memory handle @a mem_handle with flags
 *   @ref BUFFER_RDWR.
 * - Calls @ref mapping_buf() to obtain the @ref dma_buf associated with the mapping.
 * - Retrieves the size of the DMA buffer.
 * - Validates that the memory offset @a mem_offset is within the bounds of the buffer size.
 * - Calls @ref mapping_iova() to compute the IOVA address for the given memory offset.
 * - Updates the memory information base address and size using @a meminfo_base_address and
 *   @a meminfo_size with the computed IOVA and remaining size.
 * - Adds the @ref capture_mapping to the @a unpins structure and increments the
 *   unpins count.
 *
 * @param[in]      buf_ctx              Pointer to the @ref capture_buffer_table structure.
 *                                      Valid value: non-null.
 * @param[in]      mem_handle           File descriptor associated with the memory buffer.
 *                                      Valid range: [1 .. UINT32_MAX].
 * @param[in]      mem_offset           Memory offset within the buffer.
 *                                      Valid range: [0 .. UINT64_MAX].
 * @param[out]     meminfo_base_address Pointer to store the IOVA base address.
 *                                      Valid value: non-null.
 * @param[out]     meminfo_size         Pointer to store the size of the memory region.
 *                                      Valid value: non-null.
 * @param[in, out] unpins               Pointer to the @ref capture_common_unpins structure.
 *                                      Valid value: non-null.
 *
 * @retval 0         No buffer was processed (mem_handle is zero) or the operation
 *                   succeeded.
 * @retval -ENOMEM   The number of unpins exceeded @c MAX_PIN_BUFFER_PER_REQUEST.
 *                   Influenced by the check on @p unpins->num_unpins.
 * @retval -EINVAL   Failed to retrieve a valid mapping using @ref get_mapping(),
 *                   the memory offset is out of bounds, or an invalid IOVA was
 *                   computed using @ref mapping_iova().
 */
int capture_common_pin_and_get_iova(struct capture_buffer_table *buf_ctx,
		uint32_t mem_handle, uint64_t mem_offset,
		uint64_t *meminfo_base_address, uint64_t *meminfo_size,
		struct capture_common_unpins *unpins)
{
	struct capture_mapping *map;
	struct dma_buf *buf;
	uint64_t size;
	uint64_t iova;

	/* NULL is a valid unput indicating unused data field */
	if (!mem_handle) {
		return 0;
	}

	if (unpins->num_unpins >= MAX_PIN_BUFFER_PER_REQUEST) {
		pr_err("%s: too many buffers per request\n", __func__);
			return -ENOMEM;
	}

	map = get_mapping(buf_ctx, mem_handle, BUFFER_RDWR);

	if (IS_ERR(map)) {
		pr_err("%s: cannot get mapping\n", __func__);
		return -EINVAL;
	}

	buf = mapping_buf(map);
	size = buf->size;
	if (mem_offset >= size) {
		pr_err("%s: offset is out of bounds\n", __func__);
		return -EINVAL;
	}

	iova = mapping_iova(map, mem_offset);
	if (iova == 0) {
		pr_err("%s: Invalid iova\n", __func__);
		return -EINVAL;
	}

	*meminfo_base_address = iova;
	*meminfo_size = size - mem_offset;

	unpins->data[unpins->num_unpins] = map;
	unpins->num_unpins++;
	return 0;
}
EXPORT_SYMBOL_GPL(capture_common_pin_and_get_iova);

/**
 * @brief Sets up a progress status notifier by mapping a DMA buffer and initializing
 *        the notifier structure.
 *
 * This function performs the following operations:
 * - Acquires a DMA buffer reference using @ref dma_buf_get().
 * - Validates that the combined @a buffer_size and @a mem_offset do not exceed the
 *   maximum allowable size.
 * - Checks that the sum of @a buffer_size and @a mem_offset does not exceed the size
 *   of the DMA buffer.
 *   - If it does, releases the DMA buffer using @ref dma_buf_put().
 * - Maps the DMA buffer using @ref dma_buf_vmap().
 *   - If @ref dma_buf_vmap() fails, releases the DMA buffer using @ref dma_buf_put().
 * - Clears the mapped memory region using @ref memset().
 * - Initializes the @ref capture_common_status_notifier structure with the DMA buffer,
 *   virtual address, and memory offset.
 *
 * In case of any errors during the operations, the function ensures that the DMA buffer
 * reference is released before returning the error code.
 *
 * @param[in, out]  status_notifier   Pointer to the @ref capture_common_status_notifier structure.
 *                                    Valid value: non-null.
 * @param[in]       mem               Memory handle representing the buffer.
 *                                    Valid range: [0 .. UINT32_MAX].
 * @param[in]       buffer_size       Size of the buffer to be mapped.
 *                                    Valid range: [0 .. UINT32_MAX - mem_offset].
 * @param[in]       mem_offset        Offset within the buffer.
 *                                    Valid range: [0 .. UINT32_MAX].
 *
 * @retval 0           Operation completed successfully.
 * @retval -EINVAL     The combined buffer size and memory offset exceed allowable limits,
 *                     or the memory offset is out of bounds.
 * @retval -ENOMEM     Failed to map the DMA buffer using @ref dma_buf_vmap().
 * @retval (int)       Errors returned by @ref dma_buf_get() or @ref PTR_ERR_OR_ZERO().
 */
int capture_common_setup_progress_status_notifier(
	struct capture_common_status_notifier *status_notifier,
	uint32_t mem,
	uint32_t buffer_size,
	uint32_t mem_offset)
{
	struct dma_buf *dmabuf;
#if defined(NV_LINUX_IOSYS_MAP_H_PRESENT)
	struct iosys_map map = {0};
#else
	struct dma_buf_map map = {0};
#endif
	void *va;
	int err = 0;

	/* take reference for the userctx */
	dmabuf = dma_buf_get(mem);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);

	if (buffer_size > U32_MAX - mem_offset) {
		pr_err("%s: buffer_size or mem_offset too large\n", __func__);
		return -EINVAL;
	}

	if ((buffer_size + mem_offset) > dmabuf->size) {
		dma_buf_put(dmabuf);
		pr_err("%s: invalid offset\n", __func__);
		return -EINVAL;
	}

	/* map handle and clear error notifier struct */
	err = dma_buf_vmap(dmabuf, &map);
  	va = err ? NULL : map.vaddr;
	if (!va) {
		dma_buf_put(dmabuf);
		pr_err("%s: Cannot map notifier handle\n", __func__);
		return -ENOMEM;
	}

	memset(va, 0, buffer_size);

	status_notifier->buf = dmabuf;
	status_notifier->va = va;
	status_notifier->offset = mem_offset;
	return 0;
}
EXPORT_SYMBOL_GPL(capture_common_setup_progress_status_notifier);

/**
 * @brief Releases the progress status notifier by unmapping the DMA buffer and
 *        clearing the notifier structure.
 *
 * This function performs the following operations:
 * - Retrieves the DMA buffer and virtual address from the provided
 *   @ref capture_common_status_notifier structure.
 * - Initializes the appropriate DMA buffer map structure based on the
 *   compilation configuration.
 *   - If NV_LINUX_IOSYS_MAP_H_PRESENT is defined, initializes the @ref iosys_map structure
 *     calling @ref IOSYS_MAP_INIT_VADDR() given the status notifier virtual address.
 *   - Else, initializes the @ref dma_buf_map structure using @ref DMA_BUF_MAP_INIT_VADDR().
 * - If the DMA buffer is not NULL:
 *   - If the virtual address is not NULL, calls @ref dma_buf_vunmap() to unmap the DMA buffer.
 *   - Calls @ref dma_buf_put() to release the DMA buffer reference.
 * - Resets the @ref capture_common_status_notifier structure members to NULL or 0.
 *
 * @param[in, out]  progress_status_notifier  Pointer to the
 *                                            @ref capture_common_status_notifier structure.
 *                                            Valid value: non-null.
 *
 * @retval 0    Operation completed successfully.
 */
int capture_common_release_progress_status_notifier(
	struct capture_common_status_notifier *progress_status_notifier)
{
	struct dma_buf *dmabuf = progress_status_notifier->buf;
	void *va = progress_status_notifier->va;
#if defined(NV_LINUX_IOSYS_MAP_H_PRESENT)
	struct iosys_map map = IOSYS_MAP_INIT_VADDR(va);
#else
	struct dma_buf_map map = DMA_BUF_MAP_INIT_VADDR(va);
#endif

	if (dmabuf != NULL) {
		if (va != NULL)
			dma_buf_vunmap(dmabuf, &map);

		dma_buf_put(dmabuf);
	}

	progress_status_notifier->buf = NULL;
	progress_status_notifier->va = NULL;
	progress_status_notifier->offset = 0;

	return 0;
}
EXPORT_SYMBOL_GPL(capture_common_release_progress_status_notifier);

/**
 * @brief Sets the progress status for a specific buffer slot.
 *
 * This function performs the following operations:
 * - Calculates the status notifier address by adding the memory offset to the virtual address.
 * - Validates that the provided @a buffer_slot is within the range of @a buffer_depth.
 * - Sanitizes @a buffer_slot using @ref array_index_nospec().
 * - Inserts a memory barrier using @ref wmb() to ensure proper memory ordering.
 * - Updates the progress status notifier buffer at the sanitized @a buffer_slot with @a new_val.
 *
 * @param[in, out]  progress_status_notifier  Pointer to the
 *                                           @ref capture_common_status_notifier structure.
 *                                           Valid value: non-null.
 * @param[in]       buffer_slot               Index of the buffer slot to set the status.
 *                                           Valid range: [0 .. buffer_depth - 1].
 * @param[in]       buffer_depth              Total number of buffer slots.
 *                                           Valid range: [1 .. UINT32_MAX].
 * @param[in]       new_val                   New value to set in the progress status.
 *                                           Valid range: [0 .. UINT8_MAX].
 *
 * @retval 0           Operation completed successfully.
 * @retval -EINVAL     If @a buffer_slot is out of range as validated by @ref array_index_nospec()
 *                     or if the memory offset is invalid.
 */
int capture_common_set_progress_status(
	struct capture_common_status_notifier *progress_status_notifier,
	uint32_t buffer_slot,
	uint32_t buffer_depth,
	uint8_t new_val)
{
	uint32_t *status_notifier = (uint32_t *) (progress_status_notifier->va +
			progress_status_notifier->offset);

	if (buffer_slot >= buffer_depth) {
		pr_err("%s: Invalid offset!", __func__);
		return -EINVAL;
	}
	buffer_slot = array_index_nospec(buffer_slot, buffer_depth);

	/*
	 * Since UMD and KMD can both write to the shared progress status
	 * notifier buffer, insert memory barrier here to ensure that any
	 * other store operations to the buffer would be done before the
	 * write below.
	 */
	wmb();

	status_notifier[buffer_slot] = new_val;

	return 0;
}
EXPORT_SYMBOL_GPL(capture_common_set_progress_status);

/**
 * @brief Pins a memory buffer and retrieves its IOVA address and associated data.
 *
 * This function performs the following operations:
 * - Obtains a reference to the DMA buffer using @ref dma_buf_get() with the memory handle @a mem.
 * - Attaches the DMA buffer to the device @a dev using @ref dma_buf_attach().
 * - Maps the DMA buffer attachment with bidirectional access using @ref dma_buf_map_attachment().
 * - Checks if the DMA scatter-gather list DMA address is zero using @ref sg_dma_address().
 *   - If zero, retrieves the physical address using @ref sg_phys() and updates the DMA address.
 * - Maps the DMA buffer into the virtual address space using @ref dma_buf_vmap().
 *   - If @ref dma_buf_vmap() fails, sets @ref unpin_data virtual address to NULL.
 * - Clears the mapped memory region using @ref memset().
 * - Initializes the @ref capture_common_buf structure with the DMA buffer, virtual address,
 *   IOVA address, attachment, and scatter-gather table.
 * - If any step fails, cleans up by calling @ref capture_common_unpin_memory().
 *
 * @param[in]      dev          Pointer to the @ref device structure.
 *                              Valid value: non-null.
 * @param[in]      mem          Memory handle representing the buffer.
 *                              Valid range: [1 .. UINT32_MAX].
 * @param[in, out] unpin_data   Pointer to the @ref capture_common_buf structure to populate.
 *                              Valid value: non-null.
 *
 * @retval 0          Operation completed successfully.
 * @retval -EINVAL    If the buffer handle is invalid or buffer offset exceeds buffer size.
 * @retval -ENOMEM    Failed to map the DMA buffer using @ref dma_buf_vmap().
 * @retval (int)      Errors returned by @ref dma_buf_get(), @ref dma_buf_attach(),
 *                    or @ref dma_buf_map_attachment(), as retrieved by @ref PTR_ERR().
 */
int capture_common_pin_memory(
	struct device *dev,
	uint32_t mem,
	struct capture_common_buf *unpin_data)
{
	struct dma_buf *buf;
#if defined(NV_LINUX_IOSYS_MAP_H_PRESENT)
	struct iosys_map map = {0};
#else
	struct dma_buf_map map = {0};
#endif
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	int err = 0;

	buf = dma_buf_get(mem);
	if (IS_ERR(buf)) {
		err = PTR_ERR(buf);
		goto fail;
	}

	attach = dma_buf_attach(buf, dev);
	if (IS_ERR(attach)) {
		err = PTR_ERR(attach);
		goto fail;
	}

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		err = PTR_ERR(sgt);
		goto fail;
	}

	if (sg_dma_address(sgt->sgl) == 0)
		sg_dma_address(sgt->sgl) = sg_phys(sgt->sgl);

	err = dma_buf_vmap(buf, &map);
	unpin_data->va = err ? NULL : map.vaddr;
	if (unpin_data->va == NULL) {
		pr_err("%s: failed to map pinned memory\n", __func__);
		goto fail;
	}

	unpin_data->iova = sg_dma_address(sgt->sgl);
	unpin_data->buf = buf;
	unpin_data->attach = attach;
	unpin_data->sgt = sgt;

	return 0;

fail:
	capture_common_unpin_memory(unpin_data);
	return err;
}
EXPORT_SYMBOL_GPL(capture_common_pin_memory);

/**
 * @brief Unpins previously pinned memory and performs necessary cleanup.
 *
 * This function performs the following operations:
 * - Initializes a mapping structure based on the presence of
 *   NV_LINUX_IOSYS_MAP_H_PRESENT.
 *   - If present, initialize mapping structure by calling @ref IOSYS_MAP_INIT_VADDR()
 *     with the virtual address of the provided @ref capture_common_buf.
 *   - Else, initialize mapping structure by calling @ref DMA_BUF_MAP_INIT_VADDR()
 *     with the virtual address of the provided @ref capture_common_buf.
 * - If the virtual address of @a unpin_data is non-null, calls @ref dma_buf_vunmap()
 *   to unmap the virtual address.
 * - If the scatter gather table of @a unpin_data is non-null, calls
 *   @ref dma_buf_unmap_attachment() to unmap the DMA attachment with the @ref DMA_BIDIRECTIONAL
 *   flag.
 * - If the buffer attattchment of @a unpin_data is non-null, calls @ref dma_buf_detach()
 *   to detach the DMA buffer from the device.
 * - If DMA buffer of @a unpin_data is non-null, calls @ref dma_buf_put()
 *   to release the DMA buffer.
 * - Resets all fields in the @ref capture_common_buf structure to NULL or zero.
 *
 * @param[in, out]  unpin_data  Pointer to the @ref capture_common_buf structure to be cleaned up.
 *                              Valid value: non-null.
 */
void capture_common_unpin_memory(
	struct capture_common_buf *unpin_data)
{
#if defined(NV_LINUX_IOSYS_MAP_H_PRESENT)
	struct iosys_map map = IOSYS_MAP_INIT_VADDR(unpin_data->va);
#else
	struct dma_buf_map map = DMA_BUF_MAP_INIT_VADDR(unpin_data->va);
#endif

	if (unpin_data->va)
		dma_buf_vunmap(unpin_data->buf, &map);

	if (unpin_data->sgt != NULL)
		dma_buf_unmap_attachment(unpin_data->attach, unpin_data->sgt,
				DMA_BIDIRECTIONAL);
	if (unpin_data->attach != NULL)
		dma_buf_detach(unpin_data->buf, unpin_data->attach);
	if (unpin_data->buf != NULL)
		dma_buf_put(unpin_data->buf);

	unpin_data->sgt = NULL;
	unpin_data->attach = NULL;
	unpin_data->buf = NULL;
	unpin_data->iova = 0;
	unpin_data->va = NULL;
}
EXPORT_SYMBOL_GPL(capture_common_unpin_memory);
