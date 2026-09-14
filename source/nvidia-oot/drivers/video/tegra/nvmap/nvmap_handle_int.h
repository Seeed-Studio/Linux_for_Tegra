/* SPDX-License-Identifier: GPL-2.0-only
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * GPU memory management driver for Tegra
 */

#ifndef _NVMAP_HANDLE_INT_H_
#define _NVMAP_HANDLE_INT_H_

struct nvmap_handle_ref *nvmap_duplicate_handle(struct nvmap_client *client,
					struct nvmap_handle *h, bool skip_val,
					bool is_ro);

#endif /* _NVMAP_HANDLE_INT_H_ */
