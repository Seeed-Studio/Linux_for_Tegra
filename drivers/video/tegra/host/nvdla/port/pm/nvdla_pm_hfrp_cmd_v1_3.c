// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * NVDLA HFRP Command Implementation
 */

#include "nvdla_pm_hfrp.h"

/* Commands */
#define DLA_HFRP_CMD_POWER_CONTROL              400
#define DLA_HFRP_CMD_CONFIG                     401
#define DLA_HFRP_CMD_GET_CURRENT_VOLTAGE        402
#define DLA_HFRP_CMD_GET_CURRENT_POWER_DRAW     405
#define DLA_HFRP_CMD_SET_POWER_DRAW_CAP         406
#define DLA_HFRP_CMD_GET_POWER_DRAW_CAP         407
#define DLA_HFRP_CMD_SET_POWER_CONTROL_TUNING   408
#define DLA_HFRP_CMD_RESOURCE_REQUEST           409
#define DLA_HFRP_CMD_RESET                      410
#define DLA_HFRP_CMD_RESET_IDLE                 411
#define DLA_HFRP_CMD_SRAM_HOLD                  412
#define DLA_HFRP_CMD_CLOCK_GATE                 413
#define DLA_HFRP_CMD_GET_CURRENT_CLOCK_FREQ     414
#define DLA_HFRP_CMD_GET_AVG_CLOCK_FREQ         416
#define DLA_HFRP_CMD_SLCG_OVERRIDE              417
#define DLA_HFRP_CMD_GET_CURRENT_MEM_CLOCK_FREQ 418
#define DLA_HFRP_CMD_GET_MAXIMUM_MEM_CLOCK_FREQ 419
#define DLA_HFRP_CMD_GET_MAXIMUM_MEMORY_BW      420
#define DLA_HFRP_CMD_CONFIG_EMI_BW_MONITOR_CTR  421
#define DLA_HFRP_CMD_READ_EMI_BW_MONITOR_CTR    422
#define DLA_HFRP_CMD_CONFIG_SLC_BW_MONITOR_CTR  423
#define DLA_HFRP_CMD_READ_SLC_BW_MONITOR_CTR    424
#define DLA_HFRP_CMD_SYS_GET_TEMPERATURE        425
#define DLA_HFRP_CMD_SYS_GET_TEMPERATURE_LIMIT  426
#define DLA_HFRP_CMD_GET_PERF_LIMIT_REASON      427

/* Command and header buffer size */
#define SZ(cmd) cmd##_SZ

#define DLA_HFRP_CMD_POWER_CONTROL_SZ               3U
#define DLA_HFRP_CMD_CONFIG_SZ                      14U
#define DLA_HFRP_CMD_GET_CURRENT_VOLTAGE_SZ         4U
#define DLA_HFRP_CMD_GET_CURRENT_POWER_DRAW_SZ      4U
#define DLA_HFRP_CMD_SET_POWER_DRAW_CAP_SZ          4U
#define DLA_HFRP_CMD_GET_POWER_DRAW_CAP_SZ          4U
#define DLA_HFRP_CMD_SET_POWER_CONTROL_TUNING_SZ    36U
#define DLA_HFRP_CMD_RESOURCE_REQUEST_SZ            8U
#define DLA_HFRP_CMD_RESET_SZ                       1U
#define DLA_HFRP_CMD_RESET_IDLE_SZ                  1U
#define DLA_HFRP_CMD_SRAM_HOLD_SZ                   1U
#define DLA_HFRP_CMD_CLOCK_GATE_SZ                  1U
#define DLA_HFRP_CMD_GET_CURRENT_CLOCK_FREQ_SZ      0U
#define DLA_HFRP_CMD_GET_AVG_CLOCK_FREQ_SZ          4U
#define DLA_HFRP_CMD_SLCG_OVERRIDE_SZ               1U
#define DLA_HFRP_CMD_GET_CURRENT_MEM_CLOCK_FREQ_SZ  4U
#define DLA_HFRP_CMD_GET_MAXIMUM_MEM_CLOCK_FREQ_SZ  4U
#define DLA_HFRP_CMD_GET_MAXIMUM_MEMORY_BW_SZ       4U
#define DLA_HFRP_CMD_CONFIG_EMI_BW_MONITOR_CTR_SZ   8U
#define DLA_HFRP_CMD_READ_EMI_BW_MONITOR_CTR_SZ     8U
#define DLA_HFRP_CMD_CONFIG_SLC_BW_MONITOR_CTR_SZ   12U
#define DLA_HFRP_CMD_READ_SLC_BW_MONITOR_CTR_SZ     8U
#define DLA_HFRP_CMD_SYS_GET_TEMPERATURE_SZ         2U
#define DLA_HFRP_CMD_SYS_GET_TEMPERATURE_LIMIT_SZ   4U
#define DLA_HFRP_CMD_GET_PERF_LIMIT_REASON_SZ       1U

int32_t nvdla_hfrp_send_cmd_power_ctrl(struct hfrp *hfrp,
	struct nvdla_hfrp_cmd_power_ctrl *cmd,
	bool blocking)
{
	int32_t err;

	uint32_t payload[(SZ(DLA_HFRP_CMD_POWER_CONTROL) >> 2) + 1U];
	uint32_t payload_size;

	memset(payload, 0, sizeof(payload));
	payload_size = SZ(DLA_HFRP_CMD_POWER_CONTROL);

	/**
	 * MTCMOS OFF 0:0
	 * RAIL OFF 1:1
	 * MTCMOS ON 2:2
	 * RAIL ON 3:3
	 * MTCMOS DELAYED_OFF 4:4
	 * RAIL DELAYED_OFF 5:5
	 * CLOCK OFF 6:6
	 * CLOCK ON 7:7
	 * CLOCK_DELAYED_OFF 8:8
	 * PPS 16:23
	 **/
	payload[0] |= (((uint32_t)(cmd->power_off)) << 0);
	payload[0] |= (((uint32_t)(cmd->rail_off)) << 1);
	payload[0] |= (((uint32_t)(cmd->power_on)) << 2);
	payload[0] |= (((uint32_t)(cmd->rail_on)) << 3);
	payload[0] |= (((uint32_t)(cmd->power_delayed_off)) << 4);
	payload[0] |= (((uint32_t)(cmd->rail_delayed_off)) << 5);
	payload[0] |= (((uint32_t)(cmd->clock_off)) << 6);
	payload[0] |= (((uint32_t)(cmd->clock_on)) << 7);
	payload[0] |= (((uint32_t)(cmd->clock_delayed_off)) << 8);
	payload[0] |= (((uint32_t)(cmd->pps) & 0xffU) << 16);

	err = (hfrp_send_cmd(hfrp, DLA_HFRP_CMD_POWER_CONTROL,
				(uint8_t *) payload, payload_size, blocking));

	return err;
}

int32_t nvdla_hfrp_send_cmd_config(struct hfrp *hfrp,
	struct nvdla_hfrp_cmd_config *cmd,
	bool blocking)
{
	int32_t err;

	uint32_t payload[(SZ(DLA_HFRP_CMD_CONFIG) >> 2) + 1U];
	uint32_t payload_size;

	memset(payload, 0, sizeof(payload));
	payload_size = SZ(DLA_HFRP_CMD_CONFIG);

	/**
	 * PG_DELAY 15:0
	 * RG_DELAY 31:16 (15:0 byte 2)
	 * PG_ENTRY_FREQ 31:0 (byte 4)
	 * PG_EXIT_FREQ 31:0 (byte 8)
	 * CG_DELAY 15:0 (byte 12)
	 **/
	payload[0] |= (((uint32_t)(cmd->pg_delay_ms) & 0xffffU));
	payload[0] |= (((uint32_t)(cmd->rg_delay_ms) & 0xffffU) << 16);
	payload[1] |= ((uint32_t)(cmd->pg_entry_freq_khz));
	payload[2] |= ((uint32_t)(cmd->pg_exit_freq_khz));
	payload[3] |= (((uint32_t)(cmd->cg_delay_ms) & 0xffffU));

	err = (hfrp_send_cmd(hfrp, DLA_HFRP_CMD_CONFIG,
			(uint8_t *) payload, payload_size, blocking));

	return err;
}

int32_t nvdla_hfrp_send_cmd_get_current_freq(struct hfrp *hfrp,
	bool blocking)
{
	int32_t err;

	/* No payload */
	err = hfrp_send_cmd(hfrp, DLA_HFRP_CMD_GET_CURRENT_CLOCK_FREQ, NULL,
			SZ(DLA_HFRP_CMD_GET_CURRENT_CLOCK_FREQ), blocking);

	return err;
}

static void s_nvdla_hfrp_handle_response_power_ctrl(struct hfrp *hfrp,
	uint8_t *payload,
	uint32_t payload_size)
{
	uint32_t *response = (uint32_t *) payload;

	/**
	 * MTCMOS OFF 0:0
	 * RAIL OFF 1:1
	 * MTCMOS ON 2:2
	 * RAIL ON 3:3
	 * MTCMOS DELAYED_OFF 4:4
	 * RAIL DELAYED_OFF 5:5
	 * CLOCK OFF 6:6
	 * CLOCK ON 7:7
	 * CLOCK_DELAYED_OFF 8:8
	 * PPS 16:23
	 **/

	/* Gated if off or delayed off, Ungated if on. */
	if (((response[0]) & 0x1U) || ((response[0] >> 4) & 0x1U))
		hfrp->power_gated = true;
	if (((response[0] >> 2) & 0x1U))
		hfrp->power_gated = false;

	if (((response[0] >> 1) & 0x1U) || ((response[0] >> 5) & 0x1U))
		hfrp->rail_gated = true;
	if (((response[0] >> 3) & 0x1U))
		hfrp->rail_gated = false;

	if (((response[0] >> 6) & 0x1U) || ((response[0] >> 8) & 0x1U))
		hfrp->clock_gated = true;
	if (((response[0] >> 7) & 0x1U))
		hfrp->clock_gated = false;

	/* PPS is unused currently but hfrp shall be extended in the future */
}

static void s_nvdla_hfrp_handle_response_get_current_freq(struct hfrp *hfrp,
	uint8_t *payload,
	uint32_t payload_size)
{
	uint32_t *response = (uint32_t *) payload;

	/**
	 * FREQ 31:0
	 **/
	hfrp->core_freq_khz = response[0];
}

void hfrp_handle_response(struct hfrp *hfrp,
	uint32_t cmd,
	uint8_t *payload,
	uint32_t payload_size)
{
	// Expects payload to be 4byte aligned. Eases out the parsing.
	BUG_ON(payload_size % 4U != 0U);

	switch (cmd) {
	case DLA_HFRP_CMD_POWER_CONTROL: {
		s_nvdla_hfrp_handle_response_power_ctrl(hfrp,
			payload, payload_size);
		break;
	}
	case DLA_HFRP_CMD_GET_CURRENT_CLOCK_FREQ: {
		s_nvdla_hfrp_handle_response_get_current_freq(hfrp,
			payload, payload_size);
		break;
	}
	default:
		/* Nothing to handle */
		break;
	}
}
