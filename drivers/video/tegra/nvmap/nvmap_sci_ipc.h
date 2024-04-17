/* SPDX-License-Identifier: GPL-2.0-only
 * SPDX-FileCopyrightText: Copyright (c) 2019-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * mapping between nvmap_hnadle and sci_ipc entery
 */

#ifndef __VIDEO_TEGRA_NVMAP_SCI_IPC_H
#define __VIDEO_TEGRA_NVMAP_SCI_IPC_H

int nvmap_validate_sci_ipc_params(struct nvmap_client *client,
			NvSciIpcEndpointAuthToken auth_token,
			NvSciIpcEndpointVuid *pr_vuid,
			NvSciIpcEndpointVuid *localusr_vuid);

int nvmap_create_sci_ipc_id(struct nvmap_client *client,
				struct nvmap_handle *h,
				u32 flags,
				u64 *sci_ipc_id,
				NvSciIpcEndpointVuid pr_vuid,
				bool is_ro);

int nvmap_get_handle_from_sci_ipc_id(struct nvmap_client *client,
				u32 flags,
				u64 sci_ipc_id,
				NvSciIpcEndpointVuid localusr_vuid,
				u32 *h);
#endif /*  __VIDEO_TEGRA_NVMAP_SCI_IPC_H */
