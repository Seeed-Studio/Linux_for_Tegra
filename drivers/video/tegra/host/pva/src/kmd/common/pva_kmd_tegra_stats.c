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
#include "pva_kmd_debugfs.h"

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
					       &pva->tegra_stats_resource_id,
					       false);
	ASSERT(err == PVA_SUCCESS);
	pva_kmd_update_fw_resource_table(&pva->dev_resource_table);
}

void pva_kmd_device_deinit_tegra_stats(struct pva_kmd_device *pva)
{
	pva_kmd_drop_resource(&pva->dev_resource_table,
			      pva->tegra_stats_resource_id);
}

static uint64_t calc_vpu_utilization(uint64_t total_utilization,
				     uint64_t duration)
{
	if (duration == 0) {
		return 0;
	} else {
		/* tegrastats expects 10000 scale */
		pva_math_error err = MATH_OP_SUCCESS;
		uint64_t util =
			mulu64(10000ULL, total_utilization, &err) / duration;

		if (err != MATH_OP_SUCCESS) {
			pva_kmd_log_err(
				"Overflow when computing VPU utilization");
		}

		return util;
	}
}

static enum pva_error
notify_fw_get_tegra_stats(struct pva_kmd_device *pva,
			  struct pva_kmd_tegrastats *kmd_tegra_stats)
{
	struct pva_cmd_get_tegra_stats cmd = { 0 };
	uint64_t buffer_offset = 0U;
	enum pva_error err = PVA_SUCCESS;
	struct pva_kmd_fw_tegrastats fw_tegra_stats = { 0 };
	bool stats_enabled = pva->debugfs_context.stats_enable;
	uint64_t duration = 0U;

	if (stats_enabled == false) {
		pva_kmd_log_info("Tegra stats are disabled");
		goto err_out;
	}

	if (!pva_kmd_device_maybe_on(pva)) {
		goto out;
	}

	/* Power on PVA if not already */
	err = pva_kmd_device_busy(pva);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err(
			"pva_kmd_device_busy failed when submitting tegra stats cmd");
		goto err_out;
	}

	pva_kmd_set_cmd_get_tegra_stats(&cmd, pva->tegra_stats_resource_id,
					pva->tegra_stats_buf_size,
					buffer_offset, stats_enabled);

	err = pva_kmd_submit_cmd_sync(&pva->submitter, &cmd,
				      (uint32_t)sizeof(cmd),
				      PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
				      PVA_KMD_WAIT_FW_TIMEOUT_US);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("tegra stats cmd submission failed");
		goto dev_idle;
	}

	memcpy(&fw_tegra_stats, pva->tegra_stats_memory->va,
	       sizeof(fw_tegra_stats));

	pva_kmd_device_idle(pva);

out:
	duration = sat_sub64(fw_tegra_stats.window_end_time,
			     fw_tegra_stats.window_start_time);

	kmd_tegra_stats->average_vpu_utilization[0] = calc_vpu_utilization(
		fw_tegra_stats.total_utilization[0], duration);
	kmd_tegra_stats->average_vpu_utilization[1] = calc_vpu_utilization(
		fw_tegra_stats.total_utilization[1], duration);
	kmd_tegra_stats->window_start_time = fw_tegra_stats.window_start_time;
	kmd_tegra_stats->window_end_time = fw_tegra_stats.window_end_time;

	return PVA_SUCCESS;

dev_idle:
	pva_kmd_device_idle(pva);
err_out:
	return err;
}

static int64_t print_vpu_stats(struct pva_kmd_tegrastats *kmd_tegra_stats,
			       uint8_t *out_buffer, uint64_t offset,
			       uint64_t len)
{
	char kernel_buffer[256];
	int64_t formatted_len;

	formatted_len = snprintf(
		kernel_buffer, sizeof(kernel_buffer),
		"%llu\n%llu\n%llu\n%llu\n",
		(long long unsigned int)(kmd_tegra_stats->window_start_time),
		(long long unsigned int)(kmd_tegra_stats->window_end_time),
		(long long unsigned int)
			kmd_tegra_stats->average_vpu_utilization[0],
		(long long unsigned int)
			kmd_tegra_stats->average_vpu_utilization[1]);

	if (formatted_len <= 0) {
		return 0;
	}

	formatted_len++; //accounting for null terminating character

	if (len < (uint64_t)formatted_len) {
		return 0;
	}

	// Copy the formatted string from kernel buffer to user buffer
	return pva_kmd_read_from_buffer_to_user(out_buffer, len, offset,
						kernel_buffer,
						(uint64_t)formatted_len);
}

static int64_t get_vpu_stats(struct pva_kmd_device *dev, void *file_data,
			     uint8_t *out_buffer, uint64_t offset,
			     uint64_t size)
{
	struct pva_kmd_tegrastats kmd_tegra_stats;

	// We don't support partial reads for vpu stats because we cannot mix two
	// reads at different times together.
	if (offset != 0U) {
		return 0;
	}

	kmd_tegra_stats.window_start_time = 0;
	kmd_tegra_stats.window_end_time = 0;
	kmd_tegra_stats.average_vpu_utilization[0] = 0;
	kmd_tegra_stats.average_vpu_utilization[1] = 0;

	notify_fw_get_tegra_stats(dev, &kmd_tegra_stats);

	return print_vpu_stats(&kmd_tegra_stats, out_buffer, offset, size);
}

enum pva_error pva_kmd_tegrastats_init_debugfs(struct pva_kmd_device *pva)
{
	enum pva_error err;

	pva_kmd_debugfs_create_bool(pva, "stats_enabled",
				    &pva->debugfs_context.stats_enable);

	pva->debugfs_context.vpu_fops.read = &get_vpu_stats;
	pva->debugfs_context.vpu_fops.write = NULL;
	pva->debugfs_context.vpu_fops.pdev = pva;
	err = pva_kmd_debugfs_create_file(pva, "vpu_stats",
					  &pva->debugfs_context.vpu_fops);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Failed to create vpu_stats debugfs file");
		return err;
	}

	return PVA_SUCCESS;
}
