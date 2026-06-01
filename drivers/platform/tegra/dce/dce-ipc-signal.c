// SPDX-License-Identifier: MIT
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

#include <dce.h>
#include <dce-ipc.h>
#include <dce-os-utils.h>
#include <interface/dce-interface.h>

static struct dce_ipc_signal_instance *mb_signals[DCE_NUM_MBOX_REGS];

static void dce_ipc_mbox_notify(struct tegra_dce *d,
		struct dce_ipc_signal_instance *s)
{
	if (s == NULL) {
		dce_os_info(d, "Invalid signal instance for notification");
		return;
	}

	if (s->sema_num < DCE_NUM_SEMA_REGS)
		d->hsp.ss_set(d, s->sema_bit, d->hsp_id, s->sema_num);

	dce_mailbox_set_full_interrupt(d, s->form.mbox.mb_type);
}

static void dce_ipc_mbox_handle_signal(struct tegra_dce *d, void *data)
{
	u32 sema_val;
	struct dce_ipc_channel *ch;
	struct dce_ipc_signal_instance *s;
	struct dce_ipc_signal_instance *cur_s;

	s = (struct dce_ipc_signal_instance *)data;
	if ((s == NULL) || (s->signal == NULL) ||
			(s->signal->ch == NULL)	||
			(s->form.mbox.mb_num > DCE_NUM_MBOX_REGS)) {
		dce_os_err(d, "Invalid signal instance in mailbox callback");
		return;
	}

	for (cur_s = s; cur_s != NULL; cur_s = cur_s->next) {
		if (cur_s->sema_num < DCE_NUM_SEMA_REGS) {
			sema_val = d->hsp.ss_get_state(d, d->hsp_id, cur_s->sema_num);
			if ((sema_val & BIT(cur_s->sema_bit)) == 0)
				continue;
		}

		d->hsp.ss_clear(d, cur_s->sema_num, d->hsp_id, BIT(cur_s->sema_bit));

		ch = cur_s->signal->ch;

		dce_admin_ipc_handle_signal(d, ch->ch_type);
	}
}

/**
 * Lock is acquired in dce-ipc before calling this API.
 * Shouldn't be called from anywhere else.
 */
int dce_ipc_init_signaling(struct tegra_dce *d, struct dce_ipc_channel *ch)
{
	u8 mb_type;
	u32 to_mbox;
	u32 from_mbox;
	int ret = 0;
	struct dce_ipc_signal_instance *temp_s;
	struct dce_ipc_signal_instance *to_d = &ch->signal.to_d;
	struct dce_ipc_signal_instance *from_d = &ch->signal.from_d;

	ch->signal.ch = ch;

	if ((from_d == NULL) || (to_d == NULL)) {
		dce_os_err(d, "Invalid signal instances");
		ret = -1;
		goto out;
	}

	mb_type = to_d->form.mbox.mb_type;
	if (to_d->form.mbox.mb_type !=
			from_d->form.mbox.mb_type) {
		dce_os_err(d, "Mailbox type doesn't match");
		ret = -1;
		goto out;
	}

	to_mbox = to_d->form.mbox.mb_num;
	from_mbox = from_d->form.mbox.mb_num;

	to_d->signal = &ch->signal;

	if (to_d->type == DCE_IPC_SIGNAL_MAILBOX) {
		to_d->signal->notify = dce_ipc_mbox_notify;
		mb_signals[to_mbox] = to_d;
	} else {
		dce_os_info(d, "Signal type not supported : [%d]", to_d->type);
	}

	from_d->signal = &ch->signal;

	if (from_d->type == DCE_IPC_SIGNAL_MAILBOX) {
		if ((from_d->next != NULL)
				|| (from_mbox >= DCE_NUM_MBOX_REGS)) {
			dce_os_err(d, "Invalid Signal Instance");
			ret = -1;
			goto out;
		}

		temp_s = mb_signals[from_mbox];
		if (temp_s != NULL)
			from_d->next = temp_s;

		mb_signals[from_d->form.mbox.mb_num] = from_d;
	} else {
		dce_os_info(d, "Signal type not supported : [%d]", from_d->type);
	}

	/**
	 * TODO : Call this API on a conditional basis.
	 */
	ret = dce_mailbox_init_interface(d, mb_type,
			to_mbox, from_mbox, NULL, (void *)from_d,
			dce_ipc_mbox_handle_signal);
out:
	return ret;
}

/**
 * Lock is acquired in dce-ipc before calling this API.
 * Shouldn't be called from anywhere else.
 */
void dce_ipc_deinit_signaling(struct tegra_dce *d, struct dce_ipc_channel *ch)
{
	u8 mb_type;
	struct dce_ipc_signal_instance *to_d = &ch->signal.to_d;
	struct dce_ipc_signal_instance *from_d = &ch->signal.from_d;

	/**
	 * TODO : Nullify other signal parameters as well.
	 */
	mb_type = to_d->form.mbox.mb_type;
	if (to_d->form.mbox.mb_type !=
			from_d->form.mbox.mb_type) {
		dce_os_err(d, "Mailbox type doesn't match");
		return;
	}
	dce_mailbox_deinit_interface(d,	mb_type);

	ch->signal.ch = NULL;
}
