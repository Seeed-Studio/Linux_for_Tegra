/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#ifndef NVDISPLAY_SERVER_OS_IVC_LINUX_H
#define NVDISPLAY_SERVER_OS_IVC_LINUX_H

#include <soc/tegra/ivc.h>
#include <nvidia/conftest.h>

typedef struct tegra_ivc os_ivc_t;

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
static inline int os_ivc_init(os_ivc_t *ivc,
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

static inline void os_ivc_reset(os_ivc_t *ivc)
{
    return tegra_ivc_reset(ivc);
}

/* Returns 0 on success, or a negative error value if failed. */
static inline int os_ivc_notified(os_ivc_t *ivc)
{
    return tegra_ivc_notified(ivc);
}

/*
 * Return negative err code on Failure, or 0 on Success.
 * This function will populate address of next write frame
 * info functions ppframe input argument.
 */
static inline int os_ivc_get_next_write_frame(os_ivc_t *ivc, void **ppframe)
{
    int err = 0;

#if defined(NV_TEGRA_IVC_STRUCT_HAS_IOSYS_MAP) /* Linux v6.2 */
    struct iosys_map pframe;

    err = tegra_ivc_write_get_next_frame(ivc, &pframe);
    if (err) {
        iosys_map_clear(&pframe);
        goto done;
    }
#else
    void *pframe = NULL;

    pframe = tegra_ivc_write_get_next_frame(ivc);
    if (IS_ERR(pframe)) {
        err = -ENOMEM;
        goto done;
    }
#endif

    *ppframe = pframe;

done:
    return err;
}

/* Returns 0, or a negative error value if failed. */
static inline int os_ivc_write_advance(os_ivc_t *ivc)
{
    return tegra_ivc_write_advance(ivc);
}

/*
 * Return negative err code on Failure, or 0 on Success.
 * This function will populate address of next read frame
 * info functions ppframe input argument.
 */
static inline int os_ivc_get_next_read_frame(os_ivc_t *ivc, void **ppframe)
{
    int err = 0;

#if defined(NV_TEGRA_IVC_STRUCT_HAS_IOSYS_MAP) /* Linux v6.2 */
    struct iosys_map pframe;

    err = tegra_ivc_read_get_next_frame(ivc, &pframe);
    if (err) {
        iosys_map_clear(&pframe);
        goto done;
    }
#else
    void *pframe = NULL;

    pframe = tegra_ivc_read_get_next_frame(ivc);
    if (IS_ERR(pframe)) {
        err = -ENOMEM;
        goto done;
    }
#endif

    *ppframe = pframe;

done:
    return err;
}

/* Returns 0, or a negative error value if failed. */
static inline int os_ivc_read_advance(os_ivc_t *ivc)
{
    return tegra_ivc_read_advance(ivc);
}

/* TODO: Need safe coversion between size_t and uint32_t types. */
static inline uint32_t os_ivc_align(uint32_t size)
{
    return (uint32_t)tegra_ivc_align((size_t) size);
}

static inline uint32_t os_ivc_total_queue_size(uint32_t size)
{
    return tegra_ivc_total_queue_size(size);
}

#endif /* NVDISPLAY_SERVER_OS_IVC_LINUX_H */
