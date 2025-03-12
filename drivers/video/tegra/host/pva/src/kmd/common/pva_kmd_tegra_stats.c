// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#include "pva_api_cmdbuf.h"
#include "pva_api_types.h"
#include "pva_bit.h"
#include "pva_fw.h"
#include "pva_kmd_cmdbuf.h"
#include "pva_kmd_device.h"
#include "pva_kmd_constants.h"
#include "pva_utils.h"
#include "pva_kmd_tegra_stats.h"

void pva_kmd_device_init_tegra_stats(struct pva_kmd_device *pva)
{
	enum pva_error err = PVA_SUCCESS;

	pva->tegra_stats_buf_size = sizeof(struct pva_kmd_fw_tegrastats);

	pva->tegra_stats_memory =
		pva_kmd_device_memory_alloc_map(pva->tegra_stats_buf_size, pva,
						PVA_ACCESS_RW,
						PVA_R5_SMMU_CONTEXT_ID);
	ASSERT(pva->tegra_stats_memory != NULL);

	err = pva_kmd_add_dram_buffer_resource(&pva->dev_resource_table,
					       pva->tegra_stats_memory,
					       &pva->tegra_stats_resource_id);
	ASSERT(err == PVA_SUCCESS);
	pva_kmd_update_fw_resource_table(&pva->dev_resource_table);
}

void pva_kmd_device_deinit_tegra_stats(struct pva_kmd_device *pva)
{
	pva_kmd_drop_resource(&pva->dev_resource_table,
			      pva->tegra_stats_resource_id);
}

enum pva_error
pva_kmd_notify_fw_get_tegra_stats(struct pva_kmd_device *pva,
				  struct pva_kmd_tegrastats *kmd_tegra_stats)
{
	struct pva_kmd_cmdbuf_builder builder;
	struct pva_kmd_submitter *dev_submitter = &pva->submitter;
	struct pva_cmd_get_tegra_stats *cmd;
	uint64_t buffer_offset = 0U;
	uint32_t fence_val;
	enum pva_error err;
	struct pva_kmd_fw_tegrastats *fw_tegra_stats;
	bool stats_enabled = pva->debugfs_context.stats_enable;
	uint64_t duration = 0U;

	/* Power on PVA if not already */
	err = pva_kmd_device_busy(pva);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err(
			"pva_kmd_device_busy failed when submitting tegra stats cmd");
		return err;
	}

	err = pva_kmd_submitter_prepare(dev_submitter, &builder);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}
	cmd = pva_kmd_reserve_cmd_space(&builder, sizeof(*cmd));
	ASSERT(cmd != NULL);

	pva_kmd_set_cmd_get_tegra_stats(cmd, pva->tegra_stats_resource_id,
					pva->tegra_stats_buf_size,
					buffer_offset, stats_enabled);

	err = pva_kmd_submitter_submit(dev_submitter, &builder, &fence_val);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("tegra stats cmd submission failed");
		goto cancel_builder;
	}

	err = pva_kmd_submitter_wait(dev_submitter, fence_val,
				     PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
				     PVA_KMD_WAIT_FW_TIMEOUT_US);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err(
			"Waiting for FW timed out when getting tegra stats");
		goto err_out;
	}

	if (stats_enabled == false)
		goto err_out;

	fw_tegra_stats =
		(struct pva_kmd_fw_tegrastats *)(pva->tegra_stats_memory->va);

	duration = safe_subu64(fw_tegra_stats->window_end_time,
			       fw_tegra_stats->window_start_time);
	if (duration == 0) {
		pva_kmd_print_str("VPU Stats: Duration is zero");
		goto err_out;
	}

	pva_kmd_print_str("VPU Stats");
	pva_kmd_print_str_u64("Window Start Time",
			      fw_tegra_stats->window_start_time);
	pva_kmd_print_str_u64("Window End Time",
			      fw_tegra_stats->window_end_time);
	pva_kmd_print_str_u64("Total utilization VPU 0",
			      fw_tegra_stats->total_utilization[0]);
	pva_kmd_print_str_u64("Total utilization VPU 1",
			      fw_tegra_stats->total_utilization[1]);
	pva_kmd_print_str_u64(
		"VPU 0 percent utilization",
		safe_mulu64(100ULL, fw_tegra_stats->total_utilization[0]) /
			duration);
	pva_kmd_print_str_u64(
		"VPU 1 percent utilization",
		safe_mulu64(100ULL, fw_tegra_stats->total_utilization[1]) /
			duration);

	kmd_tegra_stats->average_vpu_utilization[0] =
		safe_mulu64(100ULL, fw_tegra_stats->total_utilization[0]) /
		duration;
	kmd_tegra_stats->average_vpu_utilization[1] =
		safe_mulu64(100ULL, fw_tegra_stats->total_utilization[1]) /
		duration;
	kmd_tegra_stats->window_start_time = fw_tegra_stats->window_start_time;
	kmd_tegra_stats->window_end_time = fw_tegra_stats->window_end_time;

	err = PVA_SUCCESS;

cancel_builder:
	pva_kmd_cmdbuf_builder_cancel(&builder);
err_out:
	pva_kmd_device_idle(pva);
	return err;
}
