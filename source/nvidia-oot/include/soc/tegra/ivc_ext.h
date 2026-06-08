/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef __TEGRA_IVC_EXT_H
#define __TEGRA_IVC_EXT_H

#include <nvidia/conftest.h>

#include <linux/types.h>
#include <soc/tegra/ivc-priv.h>

/**
 * tegra_ivc_channel_notified - notifies the peer device
 * @ivc		pointer of the IVC channel
 *
 * notifies the peer device
 *
 * Returns 0 if success else -EAGAIN in case of failure.
 */
int tegra_ivc_channel_notified(struct tegra_ivc *ivc);

/**
 * tegra_ivc_channel_reset - Resets the channel state
 * @ivc		pointer of the IVC channel
 *
 * Resets the channel state
 */
void tegra_ivc_channel_reset(struct tegra_ivc *ivc);

#if defined(NV_TEGRA_IVC_STRUCT_HAS_IOSYS_MAP)
/**
 * tegra_ivc_empty - Checks whether channel is empty or not
 * @map		pointer to iosys-map buffer for the IVC channel
 *
 * Checks whether channel is empty or not to read or write
 *
 * Returns true if channel is empty to read or write.
 */
bool tegra_ivc_empty(struct tegra_ivc *ivc, struct iosys_map *map);
#else
/**
 * tegra_ivc_empty - Checks whether channel is empty or not
 * @ivc		pointer of the IVC channel
 *
 * Checks whether channel is empty or not to read or write
 *
 * Returns true if channel is empty to read or write.
 */
bool tegra_ivc_empty(struct tegra_ivc *ivc, struct tegra_ivc_header *header);
#endif

/**
 * tegra_ivc_channel_sync - Syncs the IVC channel accross reboots.
 * @ivc		pointer of the IVC channel
 *
 * Syncs the IVC channel accross reboots.
 *
 * Returns 0 on success.
 */
int tegra_ivc_channel_sync(struct tegra_ivc *ivc);

#if defined(NV_TEGRA_IVC_STRUCT_HAS_IOSYS_MAP)
/**
 * tegra_ivc_frames_available - Checks number of available frames.
 * @map		pointer to iosys-map buffer for the IVC channel
 *
 * Checks number of available frames.
 *
 * Returns integer value indicating number of available frames..
 */
uint32_t tegra_ivc_frames_available(struct tegra_ivc *ivc, struct iosys_map *map);
#else
/**
 * tegra_ivc_frames_available - Checks number of available frames.
 * @ivc		pointer of the IVC channel
 *
 * Checks number of available frames.
 *
 * Returns integer value indicating number of available frames..
 */
uint32_t tegra_ivc_frames_available(struct tegra_ivc *ivc, struct tegra_ivc_header *header);
#endif

/**
 * tegra_ivc_can_read - Checks whether we can read from ivc channel
 * @ivc		pointer of the IVC channel
 *
 * Checks whether we can read from ivc channel or not
 *
 * Returns 1 for success and 0 for failure.
 */
int tegra_ivc_can_read(struct tegra_ivc *ivc);

/**
 * tegra_ivc_can_write - Checks whether we can write to ivc channel
 * @ivc		pointer of the IVC channel
 *
 * Checks whether we can write to ivc channel or not
 *
 * Returns 1 for success and 0 for failure.
 */
int tegra_ivc_can_write(struct tegra_ivc *ivc);

/**
 * tegra_ivc_read - Reads frame form ivc channel
 * @ivc		pointer of the IVC channel
 * @usr_buf	user buffer to be updated with data
 * @buf		kernel buffer to be updated with data
 * @max_read	max data ro be read form ivc channel
 *
 * Reads frame form ivc channel
 *
 * Returns no. of bytes read from ivc channel else return error.
 */
int tegra_ivc_read(struct tegra_ivc *ivc, void __user *usr_buf, void *buf, size_t max_read);

/**
 * tegra_ivc_read_peek - Reads frame form ivc channel
 * @ivc		pointer of the IVC channel
 * @usr_buf	user buffer to be updated with data
 * @buf		kernel buffer to be updated with data
 * @offset	kernel buffer to be updated with data from some offset
 * @size	max data ro be read form ivc channel
 *
 * Reads frame form ivc channel from some offset
 *
 * Returns no. of bytes read from ivc channel else return error.
 */
int tegra_ivc_read_peek(struct tegra_ivc *ivc, void __user *usr_buf, void *buf, size_t offset, size_t size);

/**
 * tegra_ivc_write - Writes frame to ivc channel
 * @ivc		pointer of the IVC channel
 * @usr_buf	user buffer to e written to ivc channel
 * @buf		kernel buffer to be written to ivc channel
 * @size	Data size to be written to ivc channel
 *
 * Writes frame to ivc channel
 *
 * Returns no. of bytes written to ivc channel else return error.
 */
int tegra_ivc_write(struct tegra_ivc *ivc, const void __user *usr_buf, const void *buf, size_t size);

#endif /* __TEGRA_IVC_EXT_H */
