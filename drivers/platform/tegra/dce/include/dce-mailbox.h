/* SPDX-License-Identifier: MIT */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef DCE_MAILBOX_H
#define DCE_MAILBOX_H

struct tegra_dce;

#define DCE_MAILBOX_BOOT_INTERFACE 0U
#define DCE_MAILBOX_ADMIN_INTERFACE 1U
#define DCE_MAILBOX_DISPRM_INTERFACE 2U
#define DCE_MAILBOX_DISPRM_NOTIFY_INTERFACE 3U
#define DCE_MAILBOX_MAX_INTERFACES 4U

/**
 * struct dce_mailbox_interface - Contains dce mailbox interface state info
 *
 * @lock : dce_os_mutext for this mailbox interface.
 * @state : Stores the current status of the mailbox interface.
 * @ack_value : Stores the response received from dce f/w on an interface.
 * @s_mb : mailbox used to send commands to DCE CCPLEX for this interface.
 * @r_mb : mailbox used to receive commands from DCE for this interface.
 * @valid : true if the stored status is valid.
 */
struct dce_mailbox_interface {
	u8 s_mb;
	u8 r_mb;
	int state;
	bool valid;
	void *notify_data;
	struct dce_os_mutex lock;
	unsigned int ack_value;
	int (*dce_mailbox_wait)(struct tegra_dce *);
	void (*notify)(struct tegra_dce *, void *);
};


u32 dce_mailbox_get_interface_status(struct tegra_dce *d, u8 id);

void dce_mailbox_store_interface_status(struct tegra_dce *d,
						u32 v, u8 id);

void dce_mailbox_invalidate_status(struct tegra_dce *d, u8 id);

void dce_mailbox_isr(struct tegra_dce *d);

void dce_mailbox_set_full_interrupt(struct tegra_dce *d, u8 id);

int dce_mailbox_send_cmd_sync(struct tegra_dce *d, u32 cmd, u32 interface);
int dce_handle_mailbox_send_cmd_sync(struct tegra_dce *d, u32 cmd, u32 interface);

int dce_mailbox_init_interface(struct tegra_dce *d, u8 id, u8 s_mb,
		u8 r_mb, int (*dce_mailbox_wait)(struct tegra_dce *),
		void *notify_data, void (*notify)(struct tegra_dce *, void *));

void dce_mailbox_deinit_interface(struct tegra_dce *d, u8 id);

#endif
