/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef DCE_OS_TRACE_H
#define DCE_OS_TRACE_H

#define CREATE_TRACE_POINTS
#include <trace/events/dce_events.h>

static inline void dce_os_trace_ivc_channel_init_complete(struct tegra_dce *d,
	struct dce_ipc_channel *ch)
{
	trace_ivc_channel_init_complete(d, ch);
}

static inline void dce_os_trace_ivc_channel_reset_triggered(struct tegra_dce *d,
	struct dce_ipc_channel *ch)
{
	trace_ivc_channel_reset_triggered(d, ch);
}

static inline void dce_os_trace_ivc_channel_reset_complete(struct tegra_dce *d,
	struct dce_ipc_channel *ch)
{
	trace_ivc_channel_reset_complete(d, ch);
}

static inline void dce_os_trace_ivc_send_req_received(struct tegra_dce *d,
	struct dce_ipc_channel *ch)
{
	trace_ivc_send_req_received(d, ch);
}

static inline void dce_os_trace_ivc_send_complete(struct tegra_dce *d,
	struct dce_ipc_channel *ch)
{
	trace_ivc_send_complete(d, ch);
}

static inline void dce_os_trace_ivc_receive_req_received(struct tegra_dce *d,
	struct dce_ipc_channel *ch)
{
	trace_ivc_receive_req_received(d, ch);
}

static inline void dce_os_trace_ivc_receive_req_complete(struct tegra_dce *d,
	struct dce_ipc_channel *ch)
{
	trace_ivc_receive_req_complete(d, ch);
}

static inline void dce_os_trace_ivc_wait_complete(struct tegra_dce *d,
	struct dce_ipc_channel *ch)
{
	trace_ivc_wait_complete(d, ch);
}

#endif /* DCE_OS_TRACE_H */
