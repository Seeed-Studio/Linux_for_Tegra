/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef __NVMAP_DMABUF_H
#define __NVMAP_DMABUF_H

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

#endif /* __NVMAP_DMABUF_H */
