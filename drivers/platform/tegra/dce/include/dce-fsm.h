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

#ifndef DCE_FSM_H
#define DCE_FSM_H

#include <dce-os-cond.h>
#include <dce-os-lock.h>

/**
 * enum dce_fsm_event_id_type - IDs to be used to convey various
 * events throughout the life cycle of dce.
 */
enum dce_fsm_event_id_type {
	EVENT_ID_DCE_INVALID = -1,
	EVENT_ID_DCE_FSM_START = 0,
	EVENT_ID_DCE_BOOT_COMPLETE_REQUESTED,
	EVENT_ID_DCE_BOOT_COMPLETE_RECEIVED,
	EVENT_ID_DCE_BOOT_CMD_MSG_REQUESTED,
	EVENT_ID_DCE_BOOT_CMD_MSG_RECEIVED,
	EVENT_ID_DCE_ADMIN_IPC_MSG_REQUESTED,
	EVENT_ID_DCE_ADMIN_IPC_MSG_RECEIVED,
	EVENT_ID_DCE_SC7_ENTER_REQUESTED,
	EVENT_ID_DCE_SC7_ENTERED_RECEIVED,
	EVENT_ID_DCE_SC7_EXIT_RECEIVED,
	EVENT_ID_DCE_LOG_REQUESTED,
	EVENT_ID_DCE_LOG_READY_RECEIVED,
	EVENT_ID_DCE_ABORT_RECEIVED,
	EVENT_ID_DCE_CRASH_LOG_RECEIVED,
	EVENT_ID_DCE_LOG_OVERFLOW_RECEIVED,
	EVENT_ID_DCE_FSM_STOP,
};

/**
 * dce_fsm_state - Different states of dce.
 */
enum dce_fsm_state {
	STATE_DCE_INVALID = -1,
	STATE_DCE_FSM_IDLE = 0,
	STATE_DCE_BOOT_WAIT,
	STATE_DCE_BOOTCMD_WFI,
	STATE_DCE_ADMIN_WFI,
	STATE_DCE_SC7_ENTER_WFI,
	STATE_DCE_SC7_ENTERED,
	STATE_DCE_LOG_READY_WFI,
	STATE_DCE_ABORT,
};

/**
 * struct dce_fsm_info - contains info used by FSM
 * @d : Pointer to tegra_dce struct
 * @initialized : If FSM is initialized
 * @c_state : Current state of FSM
 * @lock : Mutex to protect FSM operations
 * @requested_ipcs : Indicate what IPCs/requests are currently running.
 *		     In case we support more than one FSM request at a time.
 */
struct dce_fsm_info {
	struct tegra_dce *d;
	bool initialized;
	enum dce_fsm_state c_state;
	struct dce_os_mutex lock;
	u32 requested_ipcs;
};

/**
 * struct dce_mailbox_send_cmd_params - contains params for callback function
 * @cmd : u32 command to be send
 * @interface : interface id on which command need to be sent
 */
struct dce_mailbox_send_cmd_params {
	u32 cmd;
	u32 interface;
};

/**
 * struct dce_admin_send_msg_params - contains params for callback function
 * @msg : Pointer to dce_ipc_msg
 */
struct dce_admin_send_msg_params {
	struct dce_ipc_message *msg;
};

int dce_fsm_init_unlocked(struct tegra_dce *d);
void dce_fsm_start(struct tegra_dce *d);
void dce_fsm_stop(struct tegra_dce *d);
void dce_fsm_deinit_unlocked(struct tegra_dce *d);
int dce_fsm_post_event(struct tegra_dce *d,
		       enum dce_fsm_event_id_type event,
		       void *data);

int dce_handle_fsm_start_event(struct tegra_dce *d, void *params);
int dce_handle_event_stub(struct tegra_dce *d, void *params);
#endif
