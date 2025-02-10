/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025, NVIDIA Corporation.  All Rights Reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation.  Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */
#include "pva_kmd_device.h"
#include "pva_kmd_debugfs.h"
#include "pva_kmd_fw_profiler.h"
#include "pva_kmd_silicon_utils.h"
#include "pva_kmd_vpu_ocd.h"
#include "pva_kmd_tegra_stats.h"
#include "pva_kmd_vpu_app_auth.h"

void pva_kmd_debugfs_create_nodes(struct pva_kmd_device *pva)
{
	static const char *vpu_ocd_names[NUM_VPU_BLOCKS] = { "ocd_vpu0_v3",
							     "ocd_vpu1_v3" };
	pva_kmd_debugfs_create_bool(pva, "stats_enable",
				    &pva->debugfs_context.stats_enable);
	pva_kmd_debugfs_create_bool(pva, "vpu_debug",
				    &pva->debugfs_context.vpu_debug);
	pva_kmd_debugfs_create_u32(pva, "profile_level",
				   &pva->debugfs_context.profile_level);
	pva->debugfs_context.vpu_fops.read = &update_vpu_stats;
	pva->debugfs_context.vpu_fops.pdev = pva;
	pva_kmd_debugfs_create_file(pva, "vpu_stats",
				    &pva->debugfs_context.vpu_fops);
	for (uint32_t i = 0; i < NUM_VPU_BLOCKS; i++) {
		pva->debugfs_context.vpu_ocd_fops[i].open =
			&pva_kmd_vpu_ocd_open;
		pva->debugfs_context.vpu_ocd_fops[i].release =
			&pva_kmd_vpu_ocd_release;
		pva->debugfs_context.vpu_ocd_fops[i].read =
			&pva_kmd_vpu_ocd_read;
		pva->debugfs_context.vpu_ocd_fops[i].write =
			&pva_kmd_vpu_ocd_write;
		pva->debugfs_context.vpu_ocd_fops[i].pdev = pva;
		pva->debugfs_context.vpu_ocd_fops[i].file_data =
			(void *)&pva->regspec.vpu_dbg_instr_reg_offset[i];
		pva_kmd_debugfs_create_file(
			pva, vpu_ocd_names[i],
			&pva->debugfs_context.vpu_ocd_fops[i]);
	}

	pva->debugfs_context.allowlist_fops.write = &update_vpu_allowlist;
	pva->debugfs_context.allowlist_fops.pdev = pva;
	pva_kmd_debugfs_create_file(pva, "vpu_app_authentication",
				    &pva->debugfs_context.allowlist_fops);

	pva_kmd_device_init_profiler(pva);
	pva_kmd_device_init_tegra_stats(pva);
}

void pva_kmd_debugfs_destroy_nodes(struct pva_kmd_device *pva)
{
	pva_kmd_device_deinit_tegra_stats(pva);
	pva_kmd_device_deinit_profiler(pva);
	pva_kmd_debugfs_remove_nodes(pva);
}

static int64_t print_vpu_stats(struct pva_kmd_tegrastats *kmd_tegra_stats,
			       uint8_t *out_buffer, uint64_t len)
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
	if (pva_kmd_copy_data_to_user(out_buffer, kernel_buffer,
				      formatted_len)) {
		pva_kmd_log_err("failed to copy read buffer to user");
		return 0;
	}

	return formatted_len;
}

int64_t update_vpu_stats(struct pva_kmd_device *dev, void *file_data,
			 uint8_t *out_buffer, uint64_t offset, uint64_t size)
{
	uint64_t size_read = 0U;
	struct pva_kmd_tegrastats kmd_tegra_stats;

	kmd_tegra_stats.window_start_time = 0;
	kmd_tegra_stats.window_end_time = 0;
	kmd_tegra_stats.average_vpu_utilization[0] = 0;
	kmd_tegra_stats.average_vpu_utilization[1] = 0;

	pva_kmd_log_err("Reading VPU stats");
	pva_kmd_notify_fw_get_tegra_stats(dev, &kmd_tegra_stats);

	size_read = print_vpu_stats(&kmd_tegra_stats, out_buffer, size);

	return size_read;
}

int64_t update_vpu_allowlist(struct pva_kmd_device *pva, void *file_data,
			     const uint8_t *in_buffer, uint64_t offset,
			     uint64_t size)
{
	char strbuf[2]; // 1 byte for '0' or '1' and another 1 byte for the Null character
	uint32_t pva_auth_enable;
	unsigned long retval;
	retval = pva_kmd_copy_data_from_user(strbuf, in_buffer, sizeof(strbuf));
	if (retval != 0u) {
		pva_kmd_log_err("Failed to copy write buffer from user");
		return -1;
	}

	pva_auth_enable = pva_kmd_strtol(strbuf, 16);

	pva->pva_auth->pva_auth_enable = (pva_auth_enable == 1) ? true : false;

	if (pva->pva_auth->pva_auth_enable)
		pva->pva_auth->pva_auth_allow_list_parsed = false;

	return 2;
}
