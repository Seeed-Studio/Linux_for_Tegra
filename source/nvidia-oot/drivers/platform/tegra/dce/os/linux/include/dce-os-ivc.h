/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef DCE_OS_IVC_H
#define DCE_OS_IVC_H

#include <soc/tegra/ivc.h>
#include <nvidia/conftest.h>

struct dce_ipc_channel;

typedef struct tegra_ivc dce_os_ivc_t;

/*
 * Kernel API tegra_ivc_init() needs notify function as non NULL.
 * We don't have a usecase for notify function as we handle it
 * separately in our signaling module.
 */
static void ivc_signal_target(struct tegra_ivc *ivc, void *data)
{
    // Empty function.
}


/* Returns 0 on success, or a negative error value if failed. */
static inline int dce_os_ivc_init(dce_os_ivc_t *ivc,
                void *recv_base, void *send_base,
                dma_addr_t rx_phys, dma_addr_t tx_phys, /* TODO: Confirm if it's ok to remove IOVA args. These shouldn't be required here. */
                unsigned int num_frames, size_t frame_size)
{
#if defined(NV_TEGRA_IVC_STRUCT_HAS_IOSYS_MAP) /* Linux v6.2 */
    struct iosys_map rx, tx;
    struct iosys_map *prx = &rx, *ptx = &tx;
    iosys_map_set_vaddr(prx, recv_base);
    iosys_map_set_vaddr(ptx, send_base);
#else
    void *prx = recv_base;
    void *ptx = send_base;
#endif

    return tegra_ivc_init(ivc, NULL,
                prx, rx_phys,
                ptx, tx_phys,
                num_frames, frame_size,
                ivc_signal_target, NULL);
}

static inline void dce_os_ivc_reset(dce_os_ivc_t *ivc)
{
    return tegra_ivc_reset(ivc);
}

/* Returns 0 on success, or a negative error value if failed. */
static inline int dce_os_ivc_notified(dce_os_ivc_t *ivc)
{
    return tegra_ivc_notified(ivc);
}

/* Returns 0, or a negative error value if failed. */
static inline int dce_os_ivc_write_advance(dce_os_ivc_t *ivc)
{
    return tegra_ivc_write_advance(ivc);
}

/* Returns 0, or a negative error value if failed. */
static inline int dce_os_ivc_read_advance(dce_os_ivc_t *ivc)
{
    return tegra_ivc_read_advance(ivc);
}

/* TODO: Need safe coversion between size_t and uint32_t types. */
static inline uint32_t dce_os_ivc_align(uint32_t size)
{
    return (uint32_t)tegra_ivc_align((size_t) size);
}

static inline uint32_t dce_os_ivc_total_queue_size(uint32_t size)
{
    return tegra_ivc_total_queue_size(size);
}

int dce_os_ivc_write_channel(struct dce_ipc_channel *ch,
		const void *data, size_t size);

int dce_os_ivc_read_channel(struct dce_ipc_channel *ch,
		void *data, size_t size);

/**
 * dce_os_ivc_is_data_available - Check if data is avaialble for reading.
 *
 * @ch : Pointer to DCE IPC channel struct
 *
 * Return : true if success and data is available, false otherwise
 */
bool dce_os_ivc_is_data_available(struct dce_ipc_channel *ch);

/**
 * dce_os_ivc_get_next_write_frame - Get next write frame.
 *
 * @ivc : Pointer to IVC struct
 * @ppFrame : Pointer to frame reference.
 *
 * Return : 0 if successful.
 */
#if defined(NV_TEGRA_IVC_STRUCT_HAS_IOSYS_MAP) /* Linux v6.2 */
int dce_os_ivc_get_next_write_frame(dce_os_ivc_t *ivc, struct iosys_map *ppframe);
#else /* NV_TEGRA_IVC_STRUCT_HAS_IOSYS_MAP */
int dce_os_ivc_get_next_write_frame(dce_os_ivc_t *ivc, void **ppframe);
#endif /* NV_TEGRA_IVC_STRUCT_HAS_IOSYS_MAP */

/**
 * dce_os_ivc_get_next_read_frame - Get next read frame.
 *
 * @ivc : Pointer to IVC struct
 * @ppFrame : Pointer to frame reference.
 *
 * Return : 0 if successful.
 */
#if defined(NV_TEGRA_IVC_STRUCT_HAS_IOSYS_MAP) /* Linux v6.2 */
int dce_os_ivc_get_next_read_frame(dce_os_ivc_t *ivc, struct iosys_map *ppframe);
#else /* NV_TEGRA_IVC_STRUCT_HAS_IOSYS_MAP */
int dce_os_ivc_get_next_read_frame(dce_os_ivc_t *ivc, void **ppframe);
#endif /* NV_TEGRA_IVC_STRUCT_HAS_IOSYS_MAP */

#endif /* DCE_OS_IVC_H */
