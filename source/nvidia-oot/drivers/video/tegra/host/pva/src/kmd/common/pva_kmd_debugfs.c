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
#include "pva_kmd_r5_ocd.h"
#include "pva_kmd_fw_tracepoints.h"
#include "pva_kmd_limits.h"

int64_t pva_kmd_read_from_buffer_to_user(void *to, uint64_t count,
					 uint64_t offset, const void *from,
					 uint64_t available)
{
	uint64_t bytes_copied;

	if ((offset >= available) || (count == 0U)) {
		return 0;
	}
	if (count > available - offset) {
		count = available - offset;
	}
	if (pva_kmd_copy_data_to_user(to, (const uint8_t *)from + offset,
				      count) != 0UL) {
		pva_kmd_log_err("failed to copy read buffer to user");
		return 0;
	}
	bytes_copied = count;
	/* Ensure result fits in int64_t for debugfs interface */
	ASSERT(bytes_copied <= (uint64_t)S64_MAX);
	return (int64_t)bytes_copied;
}

#if PVA_ENABLE_NSYS_PROFILING == 1
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

	return pva_kmd_read_from_buffer_to_user(out_buffer, size, offset,
						kernel_buffer,
						(uint64_t)formatted_len);
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
		pva_kmd_device_idle(dev);

		if (err != PVA_SUCCESS) {
			pva_kmd_log_err(
				"Failed to notify FW about profiling level change");
			return 0;
		}
	}

	return (int64_t)size;
}
#endif

static int64_t get_vpu_allowlist_enabled(struct pva_kmd_device *pva,
					 void *file_data, uint8_t *out_buffer,
					 uint64_t offset, uint64_t size)
{
	char out_str[2]; // 1 byte for '0' or '1' and another 1 byte for the Null character
	int ret;

	pva_kmd_mutex_lock(&(pva->pva_auth->allow_list_lock));
	ret = snprintf(out_str, sizeof(out_str), "%d",
		       (int)pva->pva_auth->pva_auth_enable);
	pva_kmd_mutex_unlock(&(pva->pva_auth->allow_list_lock));

	if ((ret < 0) || ((size_t)ret >= sizeof(out_str))) {
		pva_kmd_log_err("snprintf failed or truncated");
		return (int64_t)PVA_INVAL;
	}

	// Copy the formatted string from kernel buffer to user buffer
	return pva_kmd_read_from_buffer_to_user(
		out_buffer, size, offset, out_str, (uint64_t)sizeof(out_str));
}

static int64_t update_vpu_allowlist(struct pva_kmd_device *pva, void *file_data,
				    const uint8_t *in_buffer, uint64_t offset,
				    uint64_t size)
{
	char input_buf
		[2]; // 1 byte for '0' or '1' and another 1 byte for the Null character
	uint32_t base = 10;
	uint32_t pva_auth_enable;
	unsigned long retval;
	unsigned long strtol_result;

	if (size == 0U) {
		pva_kmd_log_err("Write failed, no data provided");
		return -1;
	}

	// Copy a single character, ignore the rest
	retval = pva_kmd_copy_data_from_user(input_buf, in_buffer + offset, 1);
	if (retval != 0UL) {
		pva_kmd_log_err("Failed to copy write buffer from user");
		return -1;
	}

	// Explicitly null terminate the string for conversion
	input_buf[1] = '\0';
	/* MISRA C-2023 Rule 10.3: Explicit cast for narrowing conversion */
	strtol_result = pva_kmd_strtol(input_buf, (int32_t)base);
	/* Validate result fits in uint32_t */
	if (strtol_result > U32_MAX) {
		pva_kmd_log_err("pva_kmd_debugfs: Value exceeds U32_MAX");
		return -1;
	}
	/* CERT INT31-C: strtol_result validated to fit in uint32_t, safe to cast */
	pva_auth_enable = (uint32_t)strtol_result;

	pva_kmd_mutex_lock(&(pva->pva_auth->allow_list_lock));
	pva->pva_auth->pva_auth_enable = (pva_auth_enable == 1U) ? true : false;

	if (pva->pva_auth->pva_auth_enable) {
		pva->pva_auth->pva_auth_allow_list_parsed = false;
	}

	pva_kmd_mutex_unlock(&(pva->pva_auth->allow_list_lock));
	return (int64_t)size;
}

static int64_t get_vpu_allowlist_path(struct pva_kmd_device *pva,
				      void *file_data, uint8_t *out_buffer,
				      uint64_t offset, uint64_t size)
{
	int64_t len;

	pva_kmd_mutex_lock(&(pva->pva_auth->allow_list_lock));
	len = pva_kmd_read_from_buffer_to_user(
		out_buffer, size, offset,
		pva->pva_auth->pva_auth_allowlist_path,
		safe_addu64(strlen(pva->pva_auth->pva_auth_allowlist_path),
			    1u));
	pva_kmd_mutex_unlock(&(pva->pva_auth->allow_list_lock));
	return len;
}

static int64_t update_vpu_allowlist_path(struct pva_kmd_device *pva,
					 void *file_data,
					 const uint8_t *in_buffer,
					 uint64_t offset, uint64_t size)
{
	char buffer[ALLOWLIST_FILE_LEN];
	unsigned long retval;

	if (size == 0U) {
		return 0;
	}

	if (size > sizeof(buffer)) {
		pva_kmd_log_err_u64(
			"Length of allowlist path is too long. It must be less than ",
			sizeof(buffer));
		return -1;
	}

	retval = pva_kmd_copy_data_from_user(buffer, in_buffer, size);
	if (retval != 0UL) {
		pva_kmd_log_err("Failed to copy write buffer from user");
		return -1;
	}

	//Replacing last character from new-line to null terminator
	buffer[size - 1u] = '\0';

	pva_kmd_mutex_lock(&(pva->pva_auth->allow_list_lock));
	pva_kmd_update_allowlist_path(pva, buffer);
	pva_kmd_mutex_unlock(&(pva->pva_auth->allow_list_lock));

	return (int64_t)size;
}

static int64_t write_simulate_sc7(struct pva_kmd_device *pva, void *file_data,
				  const uint8_t *in_buffer, uint64_t offset,
				  uint64_t size)
{
	uint8_t buf = 0;
	enum pva_error err;
	unsigned long ret;

	if ((offset != 0U) || (size < 1U)) {
		return -EINVAL;
	}

	ret = pva_kmd_copy_data_from_user(&buf, in_buffer, 1);
	if (ret != 0U) {
		pva_kmd_log_err(
			"SC7 simulation: failed to copy data from user");
		return -EFAULT;
	}

	if (buf == (uint8_t)'1') {
		if (pva->debugfs_context.entered_sc7 == false) {
			err = pva_kmd_simulate_enter_sc7(pva);
			if (err != PVA_SUCCESS) {
				return -EFAULT;
			}
			pva->debugfs_context.entered_sc7 = true;
		}
	} else if (buf == (uint8_t)'0') {
		if (pva->debugfs_context.entered_sc7 == true) {
			err = pva_kmd_simulate_exit_sc7(pva);
			if (err != PVA_SUCCESS) {
				return -EFAULT;
			}
			pva->debugfs_context.entered_sc7 = false;
		}
	} else {
		pva_kmd_log_err(
			"SC7 simulation: invalid input; Must be 0 or 1");
		return -EINVAL;
	}

	return (int64_t)size;
}

static int64_t read_simulate_sc7(struct pva_kmd_device *pva, void *file_data,
				 uint8_t *out_buffer, uint64_t offset,
				 uint64_t size)
{
	char buf;
	buf = pva->debugfs_context.entered_sc7 ? '1' : '0';

	return pva_kmd_read_from_buffer_to_user(out_buffer, size, offset, &buf,
						(uint64_t)1);
}

enum pva_error pva_kmd_debugfs_create_nodes(struct pva_kmd_device *pva)
{
	enum pva_error err;

#if PVA_ENABLE_NSYS_PROFILING == 1
	struct pva_kmd_file_ops *profiling_fops;

	// Create profiling_level file operations
	profiling_fops = &pva->debugfs_context.profiling_level_fops;
	profiling_fops->read = profiling_level_read;
	profiling_fops->write = profiling_level_write;
	profiling_fops->open = NULL;
	profiling_fops->release = NULL;
	profiling_fops->pdev = pva;

	err = pva_kmd_debugfs_create_file(pva, "profiling_level",
					  profiling_fops);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err(
			"Failed to create profiling_level debugfs file");
		return err;
	}
#endif

	err = pva_kmd_tegrastats_init_debugfs(pva);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Failed to create tegrastats debugfs file");
		return err;
	}

	err = pva_kmd_vpu_ocd_init_debugfs(pva);
	if (err != PVA_SUCCESS) {
		return err;
	}

	pva->debugfs_context.allowlist_ena_fops.read =
		&get_vpu_allowlist_enabled;
	pva->debugfs_context.allowlist_ena_fops.write = &update_vpu_allowlist;
	pva->debugfs_context.allowlist_ena_fops.pdev = pva;
	err = pva_kmd_debugfs_create_file(
		pva, "vpu_app_authentication",
		&pva->debugfs_context.allowlist_ena_fops);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err(
			"Failed to create vpu_app_authentication debugfs file");
		return err;
	}

	pva->debugfs_context.allowlist_path_fops.read = &get_vpu_allowlist_path;
	pva->debugfs_context.allowlist_path_fops.write =
		&update_vpu_allowlist_path;
	pva->debugfs_context.allowlist_path_fops.pdev = pva;
	err = pva_kmd_debugfs_create_file(
		pva, "allowlist_path",
		&pva->debugfs_context.allowlist_path_fops);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Failed to create allowlist_path debugfs file");
		return err;
	}

	err = pva_kmd_fw_tracepoints_init_debugfs(pva);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Failed to create fw_trace_level debugfs file");
		return err;
	}

	pva_kmd_device_init_profiler(pva);
	pva_kmd_device_init_tegra_stats(pva);

	pva->debugfs_context.simulate_sc7_fops.read = &read_simulate_sc7;
	pva->debugfs_context.simulate_sc7_fops.write = &write_simulate_sc7;
	pva->debugfs_context.simulate_sc7_fops.pdev = pva;
	err = pva_kmd_debugfs_create_file(
		pva, "simulate_sc7", &pva->debugfs_context.simulate_sc7_fops);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Failed to create simulate_sc7 debugfs file");
		return err;
	}

#if PVA_ENABLE_R5_OCD == 1
	pva->debugfs_context.r5_ocd_fops.open = &pva_kmd_r5_ocd_open;
	pva->debugfs_context.r5_ocd_fops.release = &pva_kmd_r5_ocd_release;
	pva->debugfs_context.r5_ocd_fops.read = &pva_kmd_r5_ocd_read;
	pva->debugfs_context.r5_ocd_fops.write = &pva_kmd_r5_ocd_write;
	pva->debugfs_context.r5_ocd_fops.pdev = pva;
	err = pva_kmd_debugfs_create_file(pva, "r5_ocd",
					  &pva->debugfs_context.r5_ocd_fops);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Failed to create r5_ocd debugfs file");
		return err;
	}
#endif

	return PVA_SUCCESS;
}

void pva_kmd_debugfs_destroy_nodes(struct pva_kmd_device *pva)
{
	pva_kmd_device_deinit_tegra_stats(pva);
	pva_kmd_device_deinit_profiler(pva);
	pva_kmd_debugfs_remove_nodes(pva);
}
