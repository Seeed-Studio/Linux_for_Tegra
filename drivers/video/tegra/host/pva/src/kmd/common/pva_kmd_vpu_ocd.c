// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_device.h"
#include "pva_math_utils.h"
#include "pva_kmd_vpu_ocd.h"
#include "pva_kmd_silicon_utils.h"

#define PVA_DEBUG_APERTURE_INDEX 1U

int pva_kmd_vpu_ocd_open(struct pva_kmd_device *dev)
{
	int retval = 0;
	enum pva_error err;
	err = pva_kmd_device_busy(dev);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err(
			"pva_kmd_vpu_ocd_open pva_kmd_device_busy failed");
		retval = -1;
		goto out;
	}
out:
	return retval;
}

int pva_kmd_vpu_ocd_release(struct pva_kmd_device *dev)
{
	pva_kmd_device_idle(dev);
	return 0;
}

int64_t pva_kmd_vpu_ocd_write(struct pva_kmd_device *dev, void *file_data,
			      const uint8_t *data, uint64_t offset,
			      uint64_t size)
{
	struct pva_vpu_ocd_write_param write_param;
	uint32_t i;
	unsigned long retval;
	uint32_t reg_offset;
	uint32_t const *vpu_ocd_offset = (uint32_t *)file_data;

	retval = pva_kmd_copy_data_from_user(&write_param, data,
					     sizeof(write_param));
	if (retval != 0u) {
		pva_kmd_log_err("Failed to copy write buffer from user");
		return -1;
	}

	if (write_param.n_write > VPU_OCD_MAX_NUM_DATA_ACCESS) {
		pva_kmd_log_err_u64("pva: too many vpu dbg reg write",
				    write_param.n_write);
		return -1;
	}

	/* Write instruction first */
	pva_kmd_aperture_write(dev, PVA_DEBUG_APERTURE_INDEX, *vpu_ocd_offset,
			       write_param.instr);

	/*
	* Write data
	* if there's 1 word, write to addr 0x4,
	* if there's 2 words, write to addr 2 * 0x4,
	* ...
	*/
	reg_offset = safe_addu32((uint32_t)*vpu_ocd_offset,
				 safe_mulu32(write_param.n_write,
					     (uint32_t)sizeof(uint32_t)));
	for (i = 0u; i < write_param.n_write; i++) {
		pva_kmd_aperture_write(dev, PVA_DEBUG_APERTURE_INDEX,
				       reg_offset, write_param.data[i]);
	}

	return 0;
}

int64_t pva_kmd_vpu_ocd_read(struct pva_kmd_device *dev, void *file_data,
			     uint8_t *data, uint64_t offset, uint64_t size)
{
	struct pva_vpu_ocd_read_param read_param;
	unsigned long retval;
	uint32_t i;
	uint32_t reg_offset;
	uint32_t const *vpu_ocd_offset = (uint32_t *)file_data;

	retval = pva_kmd_copy_data_from_user(&read_param, data,
					     sizeof(read_param));
	if (retval != 0u) {
		pva_kmd_log_err("failed to copy read buffer from user");
		return -1;
	}

	if (read_param.n_read > VPU_OCD_MAX_NUM_DATA_ACCESS) {
		pva_kmd_log_err_u64("pva: too many vpu dbg reg read",
				    read_param.n_read);
		return -1;
	}

	/*
	* Read data
	* if there's 1 word, read from addr 0x4,
	* if there's 2 words, read from addr 2 * 0x4,
	* ...
	*/
	reg_offset = safe_addu32((uint32_t)*vpu_ocd_offset,
				 safe_mulu32(read_param.n_read,
					     (uint32_t)sizeof(uint32_t)));
	for (i = 0; i < read_param.n_read; i++) {
		read_param.data[i] = pva_kmd_aperture_read(
			dev, PVA_DEBUG_APERTURE_INDEX, reg_offset);
	}

	retval = pva_kmd_copy_data_to_user(data, &read_param,
					   sizeof(read_param));
	if (retval != 0u) {
		pva_kmd_log_err("failed to copy read buffer to user");
		return -1;
	}

	return 0;
}
