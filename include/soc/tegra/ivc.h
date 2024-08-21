/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2016-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __TEGRA_IVC_H
#define __TEGRA_IVC_H

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/iosys-map.h>
#include <linux/types.h>

struct tegra_ivc_header;

struct tegra_ivc {
	struct device *peer;

	struct {
		struct iosys_map map;
		unsigned int position;
		dma_addr_t phys;
	} rx, tx;

	void (*notify)(struct tegra_ivc *ivc, void *data);
	void *notify_data;

	unsigned int num_frames;
	size_t frame_size;
};

/**
* tegra_ivc_empty - Checks whether channel is empty or not
* @map		pointer to iosys-map buffer for the IVC channel
*
* Checks whether channel is empty or not to read or write
*
* Returns true if channel is empty to read or write.
*/
bool tegra_ivc_empty(struct tegra_ivc *ivc, struct iosys_map *map);

/**
 * tegra_ivc_channel_sync - Syncs the IVC channel across reboots.
 * @ivc		pointer of the IVC channel
 *
 * Syncs the IVC channel across reboots.
 *
 * Returns 0 on success.
 */
int tegra_ivc_channel_sync(struct tegra_ivc *ivc);

/**
 * tegra_ivc_frames_available - Checks number of available frames.
 * @map		pointer to iosys-map buffer for the IVC channel
 *
 * Checks number of available frames.
 *
 * Returns integer value indicating number of available frames..
 */
uint32_t tegra_ivc_frames_available(struct tegra_ivc *ivc, struct iosys_map *map);

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
/**
 * tegra_ivc_read_get_next_frame - Peek at the next frame to receive
 * @ivc		pointer of the IVC channel
 *
 * Peek at the next frame to be received, without removing it from
 * the queue.
 *
 * Returns a pointer to the frame, or an error encoded pointer.
 */
int tegra_ivc_read_get_next_frame(struct tegra_ivc *ivc, struct iosys_map *map);

/**
 * tegra_ivc_read_advance - Advance the read queue
 * @ivc		pointer of the IVC channel
 *
 * Advance the read queue
 *
 * Returns 0, or a negative error value if failed.
 */
int tegra_ivc_read_advance(struct tegra_ivc *ivc);

/**
 * tegra_ivc_write_get_next_frame - Poke at the next frame to transmit
 * @ivc		pointer of the IVC channel
 *
 * Get access to the next frame.
 *
 * Returns a pointer to the frame, or an error encoded pointer.
 */
int tegra_ivc_write_get_next_frame(struct tegra_ivc *ivc, struct iosys_map *map);

/**
 * tegra_ivc_write_advance - Advance the write queue
 * @ivc		pointer of the IVC channel
 *
 * Advance the write queue
 *
 * Returns 0, or a negative error value if failed.
 */
int tegra_ivc_write_advance(struct tegra_ivc *ivc);

/**
 * tegra_ivc_notified - handle internal messages
 * @ivc		pointer of the IVC channel
 *
 * This function must be called following every notification.
 *
 * Returns 0 if the channel is ready for communication, or -EAGAIN if a channel
 * reset is in progress.
 */
int tegra_ivc_notified(struct tegra_ivc *ivc);

/**
 * tegra_ivc_reset - initiates a reset of the shared memory state
 * @ivc		pointer of the IVC channel
 *
 * This function must be called after a channel is reserved before it is used
 * for communication. The channel will be ready for use when a subsequent call
 * to notify the remote of the channel reset.
 */
void tegra_ivc_reset(struct tegra_ivc *ivc);

size_t tegra_ivc_align(size_t size);
unsigned tegra_ivc_total_queue_size(unsigned queue_size);
int tegra_ivc_init(struct tegra_ivc *ivc, struct device *peer, const struct iosys_map *rx,
		   dma_addr_t rx_phys, const struct iosys_map *tx, dma_addr_t tx_phys,
		   unsigned int num_frames, size_t frame_size,
		   void (*notify)(struct tegra_ivc *ivc, void *data),
		   void *data);
void tegra_ivc_cleanup(struct tegra_ivc *ivc);

#endif /* __TEGRA_IVC_H */
