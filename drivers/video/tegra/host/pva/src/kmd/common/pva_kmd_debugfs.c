// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_device.h"
#include "pva_kmd_debugfs.h"
#include "pva_kmd_fw_profiler.h"
#include "pva_kmd_silicon_utils.h"
#include "pva_kmd_vpu_ocd.h"
#include "pva_kmd_tegra_stats.h"
#include "pva_kmd_vpu_app_auth.h"
#include "pva_kmd_shared_buffer.h"

static uint64_t read_from_buffer_to_user(void *to, uint64_t count,
					 uint64_t offset, const void *from,
					 uint64_t available)
{
	if (offset >= available || !count) {
		return 0;
	}
	if (count > available - offset) {
		count = available - offset;
	}
	if (pva_kmd_copy_data_to_user(to, (uint8_t *)from + offset, count)) {
		pva_kmd_log_err("failed to copy read buffer to user");
		return 0;
	}
	return count;
}

static enum pva_error
pva_kmd_notify_fw_set_profiling_level(struct pva_kmd_device *pva,
				      uint32_t level)
{
	struct pva_kmd_cmdbuf_builder builder;
	struct pva_kmd_submitter *dev_submitter = &pva->submitter;
	struct pva_cmd_set_profiling_level *cmd;
	uint32_t fence_val;
	enum pva_error err;

	err = pva_kmd_submitter_prepare(dev_submitter, &builder);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	cmd = pva_kmd_reserve_cmd_space(&builder, sizeof(*cmd));
	ASSERT(cmd != NULL);
	pva_kmd_set_cmd_set_profiling_level(cmd, level);

	err = pva_kmd_submitter_submit(dev_submitter, &builder, &fence_val);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	err = pva_kmd_submitter_wait(dev_submitter, fence_val,
				     PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
				     PVA_KMD_WAIT_FW_TIMEOUT_US);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err(
			"Waiting for FW timed out when setting profiling level");
		goto err_out;
	}

	return PVA_SUCCESS;

err_out:
	return err;
}

static int64_t profiling_level_read(struct pva_kmd_device *dev, void *file_data,
				    uint8_t *out_buffer, uint64_t offset,
				    uint64_t size)
{
	char kernel_buffer[256];
	int64_t formatted_len;

	/* Format the string only once */
	formatted_len = snprintf(kernel_buffer, sizeof(kernel_buffer), "%u\n",
				 dev->debugfs_context.profiling_level);

	if (formatted_len <= 0) {
		return 0;
	}

	formatted_len++; // Account for null terminator

	return read_from_buffer_to_user(out_buffer, size, offset, kernel_buffer,
					formatted_len);
}

static int64_t profiling_level_write(struct pva_kmd_device *dev,
				     void *file_data, const uint8_t *data,
				     uint64_t offset, uint64_t size)
{
	char kernel_buffer[256];
	uint32_t value;

	if (size >= sizeof(kernel_buffer)) {
		return 0;
	}

	if (pva_kmd_copy_data_from_user(kernel_buffer, data, size)) {
		pva_kmd_log_err("failed to copy write buffer from user");
		return 0;
	}

	kernel_buffer[size] = '\0';
	if (sscanf(kernel_buffer, "%u", &value) != 1) {
		return 0;
	}

	dev->debugfs_context.profiling_level = value;

	if (pva_kmd_device_maybe_on(dev)) {
		enum pva_error err;
		err = pva_kmd_device_busy(dev);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err(
				"pva_kmd_device_busy failed when submitting set profiling level cmd");
			return 0;
		}
		err = pva_kmd_notify_fw_set_profiling_level(dev, value);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err(
				"Failed to notify FW about profiling level change");
			return 0;
		}
		pva_kmd_device_idle(dev);
	}
	return size;
}

void pva_kmd_debugfs_create_nodes(struct pva_kmd_device *pva)
{
	static const char *vpu_ocd_names[NUM_VPU_BLOCKS] = { "ocd_vpu0_v3",
							     "ocd_vpu1_v3" };
	struct pva_kmd_file_ops *profiling_fops;

	pva_kmd_debugfs_create_bool(pva, "stats_enabled",
				    &pva->debugfs_context.stats_enable);
	pva_kmd_debugfs_create_bool(pva, "vpu_debug",
				    &pva->debugfs_context.vpu_debug);

	// Create profiling_level file operations
	profiling_fops = &pva->debugfs_context.profiling_level_fops;
	profiling_fops->read = profiling_level_read;
	profiling_fops->write = profiling_level_write;
	profiling_fops->open = NULL;
	profiling_fops->release = NULL;
	profiling_fops->pdev = pva;
	pva_kmd_debugfs_create_file(pva, "profiling_level", profiling_fops);

	pva->debugfs_context.vpu_fops.read = &get_vpu_stats;
	pva->debugfs_context.vpu_fops.write = NULL;
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

	pva->debugfs_context.allowlist_ena_fops.read =
		&get_vpu_allowlist_enabled;
	pva->debugfs_context.allowlist_ena_fops.write = &update_vpu_allowlist;
	pva->debugfs_context.allowlist_ena_fops.pdev = pva;
	pva_kmd_debugfs_create_file(pva, "vpu_app_authentication",
				    &pva->debugfs_context.allowlist_ena_fops);

	pva->debugfs_context.allowlist_path_fops.read = &get_vpu_allowlist_path;
	pva->debugfs_context.allowlist_path_fops.write =
		&update_vpu_allowlist_path;
	pva->debugfs_context.allowlist_path_fops.pdev = pva;
	pva_kmd_debugfs_create_file(pva, "allowlist_path",
				    &pva->debugfs_context.allowlist_path_fops);

	pva->debugfs_context.fw_debug_log_level_fops.write =
		&update_fw_debug_log_level;
	pva->debugfs_context.fw_debug_log_level_fops.read = NULL;
	pva->debugfs_context.fw_debug_log_level_fops.pdev = pva;
	pva_kmd_debugfs_create_file(
		pva, "fw_debug_log_level",
		&pva->debugfs_context.fw_debug_log_level_fops);

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
	return read_from_buffer_to_user(out_buffer, len, offset, kernel_buffer,
					formatted_len);
}

int64_t get_vpu_stats(struct pva_kmd_device *dev, void *file_data,
		      uint8_t *out_buffer, uint64_t offset, uint64_t size)
{
	struct pva_kmd_tegrastats kmd_tegra_stats;

	kmd_tegra_stats.window_start_time = 0;
	kmd_tegra_stats.window_end_time = 0;
	kmd_tegra_stats.average_vpu_utilization[0] = 0;
	kmd_tegra_stats.average_vpu_utilization[1] = 0;

	pva_kmd_notify_fw_get_tegra_stats(dev, &kmd_tegra_stats);

	return print_vpu_stats(&kmd_tegra_stats, out_buffer, offset, size);
}

int64_t get_vpu_allowlist_enabled(struct pva_kmd_device *pva, void *file_data,
				  uint8_t *out_buffer, uint64_t offset,
				  uint64_t size)
{
	// 1 byte for '0' or '1' and another 1 byte for the Null character
	char out_str[2];
	pva_kmd_mutex_lock(&(pva->pva_auth->allow_list_lock));
	snprintf(out_str, sizeof(out_str), "%d",
		 (int)pva->pva_auth->pva_auth_enable);
	pva_kmd_mutex_unlock(&(pva->pva_auth->allow_list_lock));

	// Copy the formatted string from kernel buffer to user buffer
	return read_from_buffer_to_user(out_buffer, size, offset, out_str,
					sizeof(out_str));
}

int64_t update_vpu_allowlist(struct pva_kmd_device *pva, void *file_data,
			     const uint8_t *in_buffer, uint64_t offset,
			     uint64_t size)
{
	char strbuf[2]; // 1 byte for '0' or '1' and another 1 byte for the Null character
	uint32_t base = 10;
	uint32_t pva_auth_enable;
	unsigned long retval;

	if (size == 0) {
		pva_kmd_log_err("Write failed, no data provided");
		return -1;
	}

	// Copy a single character, ignore the rest
	retval = pva_kmd_copy_data_from_user(strbuf, in_buffer + offset, 1);
	if (retval != 0u) {
		pva_kmd_log_err("Failed to copy write buffer from user");
		return -1;
	}

	// Explicitly null terminate the string for conversion
	strbuf[1] = '\0';
	pva_auth_enable = pva_kmd_strtol(strbuf, base);

	pva_kmd_mutex_lock(&(pva->pva_auth->allow_list_lock));
	pva->pva_auth->pva_auth_enable = (pva_auth_enable == 1) ? true : false;

	if (pva->pva_auth->pva_auth_enable)
		pva->pva_auth->pva_auth_allow_list_parsed = false;

	pva_kmd_mutex_unlock(&(pva->pva_auth->allow_list_lock));
	return size;
}

int64_t get_vpu_allowlist_path(struct pva_kmd_device *pva, void *file_data,
			       uint8_t *out_buffer, uint64_t offset,
			       uint64_t size)
{
	uint64_t len;
	pva_kmd_mutex_lock(&(pva->pva_auth->allow_list_lock));
	len = read_from_buffer_to_user(
		out_buffer, size, offset,
		pva->pva_auth->pva_auth_allowlist_path,
		safe_addu64(strlen(pva->pva_auth->pva_auth_allowlist_path),
			    1u));
	pva_kmd_mutex_unlock(&(pva->pva_auth->allow_list_lock));
	return len;
}

int64_t update_vpu_allowlist_path(struct pva_kmd_device *pva, void *file_data,
				  const uint8_t *in_buffer, uint64_t offset,
				  uint64_t size)
{
	char buffer[ALLOWLIST_FILE_LEN];
	unsigned long retval;

	if (size > sizeof(buffer)) {
		pva_kmd_log_err_u64(
			"Length of allowlist path is too long. It must be less than ",
			sizeof(buffer));
		return -1;
	}

	retval = pva_kmd_copy_data_from_user(buffer, in_buffer, size);
	if (retval != 0u) {
		pva_kmd_log_err("Failed to copy write buffer from user");
		return -1;
	}

	//Replacing last character from new-line to null terminator
	buffer[safe_subu64(size, 1u)] = '\0';

	pva_kmd_mutex_lock(&(pva->pva_auth->allow_list_lock));
	pva_kmd_update_allowlist_path(pva, buffer);
	pva_kmd_mutex_unlock(&(pva->pva_auth->allow_list_lock));

	return size;
}

int64_t update_fw_debug_log_level(struct pva_kmd_device *pva, void *file_data,
				  const uint8_t *in_buffer, uint64_t offset,
				  uint64_t size)
{
	uint32_t log_level;
	unsigned long retval;
	size_t copy_size;
	uint32_t base = 10;
	char strbuf[11]; // 10 bytes for the highest 32bit value and another 1 byte for the Null character
	strbuf[10] = '\0';

	if (size == 0) {
		pva_kmd_log_err("Write failed, no data provided");
		return -1;
	}

	/* Copy minimum of buffer size and input size */
	copy_size = (size < (sizeof(strbuf) - 1)) ? size : (sizeof(strbuf) - 1);

	retval = pva_kmd_copy_data_from_user(strbuf, in_buffer + offset,
					     copy_size);
	if (retval != 0u) {
		pva_kmd_log_err("Failed to copy write buffer from user");
		return -1;
	}

	log_level = pva_kmd_strtol(strbuf, base);

	pva->fw_debug_log_level = log_level;

	/* If device is on, busy the device and set the debug log level */
	if (pva_kmd_device_maybe_on(pva) == true) {
		enum pva_error err;
		err = pva_kmd_device_busy(pva);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err(
				"pva_kmd_device_busy failed when submitting set debug log level cmd");
			goto err_end;
		}

		pva_kmd_notify_fw_set_debug_log_level(pva, log_level);

		pva_kmd_device_idle(pva);
	}
err_end:
	return copy_size;
}
