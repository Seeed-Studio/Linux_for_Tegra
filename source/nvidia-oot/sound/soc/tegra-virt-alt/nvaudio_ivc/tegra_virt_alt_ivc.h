/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef __TEGRA_VIRT_ALT_IVC_H__
#define __TEGRA_VIRT_ALT_IVC_H__

#include "tegra_virt_alt_ivc_common.h"

#define NVAUDIO_IVC_WAIT_TIMEOUT	1000000
struct nvaudio_ivc_dev;

struct nvaudio_ivc_ctxt {
	struct tegra_hv_ivc_cookie	*ivck;
	struct device			*dev;
	int				ivc_queue;
	wait_queue_head_t		wait;
	int				timeout;
	enum rx_state_t			rx_state;
	struct nvaudio_ivc_dev		*ivcdev;
	spinlock_t			ivck_rx_lock;
	spinlock_t			ivck_tx_lock;
	spinlock_t			lock;
};

void nvaudio_ivc_rx(struct tegra_hv_ivc_cookie *ivck);

struct nvaudio_ivc_ctxt *nvaudio_ivc_alloc_ctxt(struct device *dev);

void nvaudio_ivc_free_ctxt(struct device *dev);

int nvaudio_ivc_send(struct nvaudio_ivc_ctxt *ictxt,
				struct nvaudio_ivc_msg *msg,
				int size);

int nvaudio_ivc_send_retry(struct nvaudio_ivc_ctxt *ictxt,
				struct nvaudio_ivc_msg *msg,
				int size);

int nvaudio_ivc_send_receive(struct nvaudio_ivc_ctxt *ictxt,
				struct nvaudio_ivc_msg *msg,
				int size);

int tegra124_virt_xbar_set_ivc(struct nvaudio_ivc_ctxt *ictxt,
					int rx_idx,
					int tx_idx);
int tegra124_virt_xbar_get_ivc(struct nvaudio_ivc_ctxt *ictxt,
					int rx_idx,
					int *tx_idx);

struct nvaudio_ivc_ctxt *nvaudio_get_ivc_alloc_ctxt(void);

#endif