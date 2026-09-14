/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __TEGRA_HV_NVSCICOM_H
#define __TEGRA_HV_NVSCICOM_H

#include <linux/of.h>
#include <soc/tegra/nvscicom_kernel_api.h>
#include <soc/tegra/virt/tegra_hv_ivc.h>

/**
 * tegra_hv_nvscicom_reserve - Reserve an NvSciCom IVC queue for use
 * @dn:		Device node pointer to the queue in the DT
 * @id:		Id number of the queue to use
 *
 * Reserves the queue for use with NvSciCom
 *
 * Returns a pointer to the ivc_cookie to use or an ERR_PTR.
 * Note that returning EPROBE_DEFER means that the driver
 * hasn't loaded yet and you should try again later in the
 * boot sequence.
 */
struct tegra_hv_ivc_cookie *tegra_hv_nvscicom_reserve(struct device_node *dn,
						      int id);

/**
 * tegra_hv_nvscicom_activate - Activate an NvSciCom endpoint
 * @ivck:	IVC cookie returned by tegra_hv_nvscicom_reserve
 * @role:	NvSciCom role (UNCAST, CLIENT, SERVER, STREAM, or IVCLIB)
 *
 * Initializes the NvSciCom endpoint with the specified role.
 *
 * Returns 0 on success, a negative error code otherwise.
 */
int tegra_hv_nvscicom_activate(struct tegra_hv_ivc_cookie *ivck,
			       enum nvscicom_role role);

/**
 * tegra_hv_nvscicom_unreserve - Unreserve an NvSciCom IVC queue
 * @ivck:	IVC cookie returned by tegra_hv_nvscicom_reserve
 *
 * Releases the reservation on an IVC queue.
 *
 * Returns 0 on success, a negative error code otherwise.
 */
int tegra_hv_nvscicom_unreserve(struct tegra_hv_ivc_cookie *ivck);

/**
 * tegra_hv_nvscicom_read - Read data from an NvSciCom endpoint
 * @ivck:	IVC cookie returned by tegra_hv_nvscicom_reserve
 * @buf:	Buffer to read data into
 * @len:	Maximum number of bytes to read
 *
 * Returns actual bytes read on success, a negative error code otherwise.
 */
ssize_t tegra_hv_nvscicom_read(struct tegra_hv_ivc_cookie *ivck, void *buf,
			       size_t len);

/**
 * tegra_hv_nvscicom_write - Write data to an NvSciCom endpoint
 * @ivck:	IVC cookie returned by tegra_hv_nvscicom_reserve
 * @buf:	Buffer containing data to write
 * @len:	Number of bytes to write
 *
 * Returns actual bytes written on success, a negative error code otherwise.
 */
ssize_t tegra_hv_nvscicom_write(struct tegra_hv_ivc_cookie *ivck,
				const void *buf, size_t len);

/**
 * tegra_hv_nvscicom_can_read - Check if data is available to read
 * @ivck:	IVC cookie returned by tegra_hv_nvscicom_reserve
 *
 * Returns 1 if data is available, 0 otherwise.
 */
int tegra_hv_nvscicom_can_read(struct tegra_hv_ivc_cookie *ivck);

/**
 * tegra_hv_nvscicom_can_write - Check if space is available to write
 * @ivck:	IVC cookie returned by tegra_hv_nvscicom_reserve
 *
 * Returns 1 if space is available, 0 otherwise.
 */
int tegra_hv_nvscicom_can_write(struct tegra_hv_ivc_cookie *ivck);

/**
 * tegra_hv_nvscicom_begin_read_txn - Begin a read transaction
 * @ivck:	IVC cookie returned by tegra_hv_nvscicom_reserve
 * @len:	Length of the transaction
 *
 * Returns 0 on success, a negative error code otherwise.
 */
int tegra_hv_nvscicom_begin_read_txn(struct tegra_hv_ivc_cookie *ivck,
				     size_t len);

/**
 * tegra_hv_nvscicom_begin_write_txn - Begin a write transaction
 * @ivck:	IVC cookie returned by tegra_hv_nvscicom_reserve
 * @len:	Length of the transaction
 *
 * Returns 0 on success, a negative error code otherwise.
 */
int tegra_hv_nvscicom_begin_write_txn(struct tegra_hv_ivc_cookie *ivck,
				      size_t len);

/**
 * tegra_hv_nvscicom_commit_txn - Commit a pending transaction
 * @ivck:	IVC cookie returned by tegra_hv_nvscicom_reserve
 *
 * Returns 0 on success, a negative error code otherwise.
 */
int tegra_hv_nvscicom_commit_txn(struct tegra_hv_ivc_cookie *ivck);

/**
 * tegra_hv_nvscicom_abort_txn - Abort a pending transaction
 * @ivck:	IVC cookie returned by tegra_hv_nvscicom_reserve
 *
 * Returns 0 on success, a negative error code otherwise.
 */
int tegra_hv_nvscicom_abort_txn(struct tegra_hv_ivc_cookie *ivck);

/**
 * tegra_hv_nvscicom_reconnect - Reconnect an NvSciCom endpoint
 * @ivck:	IVC cookie returned by tegra_hv_nvscicom_reserve
 *
 * Resets and reconnects the endpoint.
 *
 * Returns 0 on success, a negative error code otherwise.
 */
int tegra_hv_nvscicom_reconnect(struct tegra_hv_ivc_cookie *ivck);

/**
 * tegra_hv_nvscicom_set_write_low_water_mark - Set write low water mark
 * @ivck:		IVC cookie returned by tegra_hv_nvscicom_reserve
 * @low_water_mark:	The low water mark threshold
 *
 * Sets the low water mark for write notifications.
 *
 * Returns 0 on success, a negative error code otherwise.
 */
int tegra_hv_nvscicom_set_write_low_water_mark(struct tegra_hv_ivc_cookie *ivck,
					       size_t low_water_mark);

#endif /* __TEGRA_HV_NVSCICOM_H */
