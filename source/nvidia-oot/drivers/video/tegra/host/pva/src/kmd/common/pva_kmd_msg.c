// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_msg.h"
#include "pva_api_types.h"
#include "pva_fw.h"
#include "pva_kmd_utils.h"
#include "pva_kmd_thread_sema.h"
#include "pva_kmd_device.h"
#include "pva_kmd_context.h"
#include "pva_kmd_abort.h"

static uint8_t get_msg_type(uint32_t hdr)
{
	return (uint8_t)PVA_EXTRACT(hdr, PVA_FW_MSG_TYPE_MSB,
				    PVA_FW_MSG_TYPE_LSB, uint32_t);
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

		pva_kmd_log_info("Firmware boot completes");
		pva_kmd_log_info_u64("R5 start time (us)",
				     pva_kmd_tsc_to_us(pva, r5_start_time));
		pva_kmd_log_info_u64("R5 ready time (us)",
				     pva_kmd_tsc_to_us(pva, r5_ready_time));

		pva_kmd_sema_post(&pva->fw_boot_sema);
	} break;
	case PVA_FW_MSG_TYPE_ABORT: {
		char abort_msg[PVA_FW_MSG_ABORT_STR_MAX_LEN + 1];

		pva_kmd_drain_fw_print(pva);

		pva_kmd_log_err("Firmware aborted! The abort message is: ");
		/* CERT INT31-C: PVA_EXTRACT returns 8-bit value, safe to cast to char */
		abort_msg[0] = (char)PVA_EXTRACT(data[0], 7, 0, uint32_t);
		abort_msg[1] = (char)PVA_EXTRACT(data[0], 15, 8, uint32_t);
		(void)memcpy((void *)(abort_msg + 2), (const void *)&data[1],
			     (size_t)size);
		abort_msg[PVA_FW_MSG_ABORT_STR_MAX_LEN] = '\0';
		pva_kmd_log_err(abort_msg);
		pva_kmd_abort_fw(pva, PVA_ERR_FW_ABORTED);
	} break;
	case PVA_FW_MSG_TYPE_FLUSH_PRINT:
		pva_kmd_drain_fw_print(pva);
		break;
	case PVA_FW_MSG_TYPE_FAST_RESET_FAILURE:
		pva_kmd_log_err("Fast reset failure");
		pva_kmd_report_error_fsi(pva,
					 (uint32_t)PVA_ERR_FAST_RESET_FAILURE);
		pva->recovery = true;
		break;

	default:
		FAULT("Unknown message type from firmware");
		/* MISRA Rule 16.3 requires break; Rule 2.1: break is unreachable but required by MISRA 16.3 */
		break;
	}
}
