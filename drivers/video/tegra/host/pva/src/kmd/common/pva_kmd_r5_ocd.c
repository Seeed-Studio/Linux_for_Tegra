// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_r5_ocd.h"
#include "pva_api_types.h"
#include "pva_fw_address_map.h"
#include "pva_kmd_debugfs.h"
#include "pva_kmd_shim_debugfs.h"
#include "pva_kmd_shim_utils.h"
#include "pva_kmd_silicon_utils.h"
#include "pva_fw_hyp.h"

int pva_kmd_r5_ocd_open(struct pva_kmd_device *dev)
{
	enum pva_error err = pva_kmd_device_busy(dev);
	if (err == PVA_SUCCESS) {
		dev->r5_ocd_on = true;
	}
	return 0;
}

int pva_kmd_r5_ocd_release(struct pva_kmd_device *dev)
{
	if (dev->r5_ocd_on) {
		dev->r5_ocd_on = false;
		pva_kmd_device_idle(dev);
	}
	return 0;
}

int64_t pva_kmd_r5_ocd_write(struct pva_kmd_device *pva, void *file_data,
			     const uint8_t *data, uint64_t offset,
			     uint64_t size)
{
	struct pva_r5_ocd_request *req = NULL;
	if (size > PVA_R5_OCD_MAX_DATA_SIZE) {
		pva_kmd_log_err("pva_kmd_r5_ocd_write: size too large");
		return -1;
	}

	pva_kmd_copy_data_from_user(pva->debugfs_context.r5_ocd_stage_buffer,
				    data, size);

	req = (struct pva_r5_ocd_request *)
		      pva->debugfs_context.r5_ocd_stage_buffer;

	if (req->size > PVA_R5_OCD_MAX_DATA_SIZE) {
		pva_kmd_log_err("pva_kmd_r5_ocd_write: size too large");
		return -1;
	}

	pva_kmd_write_mailbox(pva, PVA_FW_MBOX_TO_R5_BASE, 0xFFFFFFFF);

	return size;
}

int64_t pva_kmd_r5_ocd_read(struct pva_kmd_device *pva, void *file_data,
			    uint8_t *data, uint64_t offset, uint64_t size)
{
	//wait until mailbox is cleared
	while (pva_kmd_read_mailbox(pva, PVA_FW_MBOX_TO_R5_BASE) != 0) {
		pva_kmd_sleep_us(1);
	}

	return pva_kmd_read_from_buffer_to_user(
		data, size, offset, pva->debugfs_context.r5_ocd_stage_buffer,
		PVA_R5_OCD_MAX_DATA_SIZE);
}
