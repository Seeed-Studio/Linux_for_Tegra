// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_device.h"
#include "pva_math_utils.h"
#include "pva_kmd_vpu_ocd.h"
#include "pva_kmd_silicon_utils.h"
#include "pva_kmd_debugfs.h"

#if (PVA_BUILD_MODE == PVA_BUILD_MODE_QNX)
#define PVA_KMD_DEBUG_VPU_APERTURE_INDEX PVA_KMD_APERTURE_VPU_DEBUG
#else
#define PVA_KMD_DEBUG_VPU_APERTURE_INDEX 1
#endif

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
	uint32_t retval;
	uint32_t reg_offset;
	uint32_t const *vpu_ocd_offset = (uint32_t *)file_data;

	retval = pva_kmd_copy_data_from_user(&write_param, data,
					     (uint64_t)sizeof(write_param));
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
	pva_kmd_aperture_write(dev, PVA_KMD_DEBUG_VPU_APERTURE_INDEX,
			       *vpu_ocd_offset, write_param.instr);
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
		pva_kmd_aperture_write(dev, PVA_KMD_DEBUG_VPU_APERTURE_INDEX,
				       reg_offset, write_param.data[i]);
	}

	return (int64_t)sizeof(struct pva_vpu_ocd_write_param);
}

int64_t pva_kmd_vpu_ocd_read(struct pva_kmd_device *dev, void *file_data,
			     uint8_t *data, uint64_t offset, uint64_t size)
{
	uint32_t i;
	uint32_t reg_offset;
	uint32_t const *vpu_ocd_offset = (uint32_t *)file_data;
	uint32_t n_read = (uint32_t)(size / sizeof(uint32_t));
	uint32_t data_buf[VPU_OCD_MAX_NUM_DATA_ACCESS];

	if (n_read > VPU_OCD_MAX_NUM_DATA_ACCESS) {
		pva_kmd_log_err_u64("pva: too many vpu dbg reg read", n_read);
		return -1;
	}

	/*
	* Read data
	* if there's 1 word, read from addr 0x4,
	* if there's 2 words, read from addr 2 * 0x4,
	* ...
	*/
	reg_offset =
		safe_addu32((uint32_t)*vpu_ocd_offset,
			    safe_mulu32(n_read, (uint32_t)sizeof(uint32_t)));
	for (i = 0; i < n_read; i++) {
		data_buf[i] = pva_kmd_aperture_read(
			dev, PVA_KMD_DEBUG_VPU_APERTURE_INDEX, reg_offset);
	}

	pva_kmd_copy_data_to_user(data, data_buf, size);

	return (int64_t)size;
}

enum pva_error pva_kmd_vpu_ocd_init_debugfs(struct pva_kmd_device *pva)
{
	static const char *vpu_ocd_names[NUM_VPU_BLOCKS] = { "ocd_vpu0_v3",
							     "ocd_vpu1_v3" };
	enum pva_error err;

	// Create vpu_debug boolean node
	pva_kmd_debugfs_create_bool(pva, "vpu_debug",
				    &pva->debugfs_context.vpu_debug);

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
		err = pva_kmd_debugfs_create_file(
			pva, vpu_ocd_names[i],
			&pva->debugfs_context.vpu_ocd_fops[i]);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err(
				"Failed to create vpu_ocd debugfs file");
			return err;
		}
	}

	return PVA_SUCCESS;
}
