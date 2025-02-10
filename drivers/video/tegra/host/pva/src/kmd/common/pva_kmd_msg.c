/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, NVIDIA Corporation.  All Rights Reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation.  Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */

#include "pva_kmd_msg.h"
#include "pva_fw.h"
#include "pva_kmd_utils.h"
#include "pva_kmd_thread_sema.h"
#include "pva_kmd_fw_debug.h"
#include "pva_kmd_device.h"
#include "pva_kmd_context.h"

static uint8_t get_msg_type(uint32_t hdr)
{
	return PVA_EXTRACT(hdr, PVA_FW_MSG_TYPE_MSB, PVA_FW_MSG_TYPE_LSB,
			   uint32_t);
}

void pva_kmd_handle_hyp_msg(void *pva_dev, uint32_t const *data, uint8_t len)
{
	struct pva_kmd_device *pva = pva_dev;
	uint8_t type = get_msg_type(data[0]);
	uint8_t updated_len = safe_subu8(len, 1U);
	uint8_t size = safe_mulu8((uint8_t)sizeof(uint32_t), updated_len);

	switch (type) {
	case PVA_FW_MSG_TYPE_BOOT_DONE: {
		uint64_t r5_start_time =
			pack64(data[PVA_FW_MSG_R5_START_TIME_HI_IDX],
			       data[PVA_FW_MSG_R5_START_TIME_LO_IDX]);
		uint64_t r5_ready_time =
			pack64(data[PVA_FW_MSG_R5_READY_TIME_HI_IDX],
			       data[PVA_FW_MSG_R5_READY_TIME_LO_IDX]);

		pva_kmd_log_err("Firmware boot completes");
		pva_kmd_log_err_u64("R5 start time (us)",
				    tsc_to_us(r5_start_time));
		pva_kmd_log_err_u64("R5 ready time (us)",
				    tsc_to_us(r5_ready_time));

		pva_kmd_sema_post(&pva->fw_boot_sema);
	} break;
	case PVA_FW_MSG_TYPE_ABORT: {
		char abort_msg[PVA_FW_MSG_ABORT_STR_MAX_LEN + 1];

		pva_kmd_drain_fw_print(&pva->fw_print_buffer);

		pva_kmd_log_err("Firmware aborted! The abort message is: ");
		abort_msg[0] = PVA_EXTRACT(data[0], 7, 0, uint32_t);
		abort_msg[1] = PVA_EXTRACT(data[0], 15, 8, uint32_t);
		memcpy(abort_msg + 2, &data[1], size);
		abort_msg[PVA_FW_MSG_ABORT_STR_MAX_LEN] = '\0';
		pva_kmd_log_err(abort_msg);
	} break;
	case PVA_FW_MSG_TYPE_FLUSH_PRINT:
		pva_kmd_drain_fw_print(&pva->fw_print_buffer);
		break;

	default:
		FAULT("Unknown message type from firmware");
	}
}

void pva_kmd_handle_msg(void *pva_dev, uint32_t const *data, uint8_t len)
{
	struct pva_kmd_device *pva = pva_dev;

	uint8_t type = get_msg_type(data[0]);
	switch (type) {
	case PVA_FW_MSG_TYPE_RESOURCE_UNREGISTER: {
		uint8_t table_id =
			PVA_EXTRACT(data[0], PVA_FW_MSG_RESOURCE_TABLE_ID_MSB,
				    PVA_FW_MSG_RESOURCE_TABLE_ID_LSB, uint8_t);
		/* Resource table ID equals context id */
		struct pva_kmd_context *ctx =
			pva_kmd_get_context(pva, table_id);
		uint32_t i;

		pva_kmd_mutex_lock(&ctx->resource_table_lock);
		for (i = 1; i < len; i++) {
			pva_kmd_drop_resource(&ctx->ctx_resource_table,
					      data[i]);
		}
		pva_kmd_mutex_unlock(&ctx->resource_table_lock);
		break;
	}
	default:
		FAULT("Unexpected CCQ msg type from FW");
		break;
	}
}
