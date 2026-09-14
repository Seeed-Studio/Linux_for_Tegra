// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

void cpuidle_dbg_sleep(u64 duration_ns, u64 exit_latency_ns);

void cpuidle_dbg_sleep_many(struct cpumask *mask_A, struct cpumask *mask_B,
	u64 residency_ns_A, u64 residency_ns_B, u64 exit_latency_ns, u64 src_cpu,
	bool do_coordinated_wakeup);

int cpuidle_fs_init(void);

void cpuidle_fs_teardown(void);
