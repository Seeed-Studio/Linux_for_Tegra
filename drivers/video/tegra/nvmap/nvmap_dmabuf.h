/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef __NVMAP_DMABUF_H
#define __NVMAP_DMABUF_H

#define __DMA_ATTR(attrs) attrs
#define DEFINE_DMA_ATTRS(attrs) unsigned long attrs = 0

/**
 * dma_set_attr - set a specific attribute
 * @attr: attribute to set
 * @attrs: struct dma_attrs (may be NULL)
 */
#define dma_set_attr(attr, attrs) (attrs |= attr)

/**
 * dma_get_attr - check for a specific attribute
 * @attr: attribute to set
 * @attrs: struct dma_attrs (may be NULL)
 */
#define dma_get_attr(attr, attrs) (attrs & attr)

int nvmap_dmabuf_stash_init(void);
void nvmap_dmabuf_stash_deinit(void);

bool dmabuf_is_nvmap(struct dma_buf *dmabuf);

struct dma_buf *__nvmap_make_dmabuf(struct nvmap_client *client,
					struct nvmap_handle *handle,
					bool ro_buf);

int nvmap_get_dmabuf_fd(struct nvmap_client *client,
			struct nvmap_handle *h,
			bool is_ro);

struct nvmap_handle *nvmap_handle_get_from_dmabuf_fd(
			struct nvmap_client *client,
			int fd);

int is_nvmap_dmabuf_fd_ro(int fd, bool *is_ro);

int nvmap_dmabuf_duplicate_gen_fd(struct nvmap_client *client,
					struct dma_buf *dmabuf);

struct nvmap_vma_list {
	struct list_head list;
	struct vm_area_struct *vma;
	unsigned long save_vm_flags;
	pid_t pid;
	atomic_t ref;
};

struct nvmap_vma_priv {
	struct nvmap_handle *handle;
	size_t		offs;
	atomic_t	count;	/* number of processes cloning the VMA */
	u64 map_rss_count;
	struct mm_struct *mm;
	struct mutex vma_lock;
};

int is_nvmap_vma(struct vm_area_struct *vma);

void nvmap_vma_open(struct vm_area_struct *vma);

extern struct vm_operations_struct nvmap_vma_ops;

#endif /* __NVMAP_DMABUF_H */
