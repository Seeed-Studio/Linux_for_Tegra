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
#define DLA_HFRP_CMD_GET_VF_CURVE               428

/* Command and header buffer size */
#define SZ(cmd) cmd##_SZ

#define DLA_HFRP_CMD_POWER_CONTROL_SZ               3U
#define DLA_HFRP_CMD_CONFIG_SZ                      14U
#define DLA_HFRP_CMD_GET_CURRENT_VOLTAGE_SZ         0U
#define DLA_HFRP_CMD_GET_CURRENT_POWER_DRAW_SZ      0U
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
#define DLA_HFRP_CMD_GET_VF_CURVE_SZ                0U

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
	 * PPS 23:16
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

	/* Run some preactions prior to command execution. */
	if (cmd->clock_off)
		hfrp_handle_cg_entry_start(hfrp);

	if (cmd->clock_on)
		hfrp_handle_cg_exit_start(hfrp);

	if (cmd->power_off)
		hfrp_handle_pg_entry_start(hfrp);

	if (cmd->power_on)
		hfrp_handle_pg_exit_start(hfrp);

	if (cmd->rail_off)
		hfrp_handle_rg_entry_start(hfrp);

	if (cmd->rail_on)
		hfrp_handle_rg_exit_start(hfrp);

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

int32_t nvdla_hfrp_send_cmd_get_current_voltage(struct hfrp *hfrp,
	bool blocking)
{
	int32_t err;

	/* No payload */
	err = hfrp_send_cmd(hfrp, DLA_HFRP_CMD_GET_CURRENT_VOLTAGE, NULL,
			SZ(DLA_HFRP_CMD_GET_CURRENT_VOLTAGE), blocking);

	return err;
}

int32_t nvdla_hfrp_send_cmd_get_current_power_draw(struct hfrp *hfrp,
	bool blocking)
{
	int32_t err;

	/* No payload */
	err = hfrp_send_cmd(hfrp, DLA_HFRP_CMD_GET_CURRENT_POWER_DRAW, NULL,
			SZ(DLA_HFRP_CMD_GET_CURRENT_POWER_DRAW), blocking);

	return err;
}

int32_t nvdla_hfrp_send_cmd_get_vfcurve(struct hfrp *hfrp,
	bool blocking)
{
	int32_t err;

	/* No payload */
	err = hfrp_send_cmd(hfrp, DLA_HFRP_CMD_GET_VF_CURVE, NULL,
			SZ(DLA_HFRP_CMD_GET_VF_CURVE), blocking);

	return err;
}

static void s_nvdla_hfrp_handle_response_power_ctrl(struct hfrp *hfrp,
	uint8_t *payload,
	uint32_t payload_size)
{
	uint32_t *response = (uint32_t *) payload;

	bool clock_off;
	bool clock_on;
	bool power_off;
	bool power_on;
	bool rail_off;
	bool rail_on;

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
	 * PPS 23:16
	 **/

	power_off   = ((response[0]) & 0x1u);
	rail_off    = ((response[0] >> 1) & 0x1u);
	power_on    = ((response[0] >> 2) & 0x1u);
	rail_on     = ((response[0] >> 3) & 0x1u);
	clock_off   = ((response[0] >> 6) & 0x1u);
	clock_on    = ((response[0] >> 7) & 0x1u);

	/**
	 * gated if off , ungated if on.
	 * delayed-off is asynchronous and is notified in separate interrupt.
	 * pps is unused currently but hfrp shall be extended in the future.
	 **/
	if (power_off)
		hfrp_handle_pg_entry(hfrp);

	if (power_on)
		hfrp_handle_pg_exit(hfrp);

	if (rail_off)
		hfrp_handle_rg_entry(hfrp);

	if (rail_on)
		hfrp_handle_rg_exit(hfrp);

	if (clock_off)
		hfrp_handle_cg_entry(hfrp);

	if (clock_on)
		hfrp_handle_cg_exit(hfrp);

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

static void s_nvdla_hfrp_handle_response_config(struct hfrp *hfrp,
	uint8_t *payload,
	uint32_t payload_size)
{
	uint32_t *response = (uint32_t *) payload;
	uint32_t pg_delay_ms;
	uint32_t rg_delay_ms;
	uint32_t cg_delay_ms;

	/**
	 * PG_DELAY 15:0
	 * RG_DELAY 31:16
	 * CG_DELAY 15:0 (byte 12)
	 **/
	pg_delay_ms = (response[0] & 0xffffU);
	rg_delay_ms = ((response[0] >> 16) & 0xffffU);
	cg_delay_ms = (response[3] & 0xffffU);

	if (pg_delay_ms > 0)
		hfrp->pg_delay_us = pg_delay_ms * 1000U;

	if (rg_delay_ms > 0)
		hfrp->rg_delay_us = rg_delay_ms * 1000U;

	if (cg_delay_ms > 0)
		hfrp->cg_delay_us = cg_delay_ms * 1000U;
}

static void s_nvdla_hfrp_handle_response_current_voltage(struct hfrp *hfrp,
	uint8_t *payload,
	uint32_t payload_size)
{
	uint32_t *response = (uint32_t *) payload;
	uint32_t voltage;

	/**
	 * VOLTAGE 31:0
	 **/
	voltage = response[0];

	hfrp->voltage_mV = voltage;
}

static void s_nvdla_hfrp_handle_response_current_power_draw(struct hfrp *hfrp,
	uint8_t *payload,
	uint32_t payload_size)
{
	uint32_t *response = (uint32_t *) payload;
	uint32_t power_draw;

	/**
	 * POWER_DRAW 31:0
	 **/
	power_draw = response[0];

	hfrp->power_draw_mW = power_draw;
}

static void s_nvdla_hfrp_handle_response_vfcurve(struct hfrp *hfrp,
	uint8_t *payload,
	uint32_t payload_size)
{
	uint32_t *response = (uint32_t *) payload;
	uint32_t ii;

	/**
	 * NUM_POINTS 4:0
	 * P0_V 31:16 (word 0)
	 * P0_F 15:0  (word 1)
	 * P1_V 31:16 (word 1)
	 * P1_F 15:0  (word 2)
	 * P2_V 31:16 (word 2)
	 * P2_F 15:0  (word 3)
	 * P3_V 31:16 (word 3)
	 * P3_F 15:0  (word 4)
	 * P4_V 31:16 (word 4)
	 * P4_F 15:0  (word 5)
	 * P5_V 31:16 (word 5)
	 * P5_F 15:0  (word 6)
	 * P6_V 31:16 (word 6)
	 * P6_F 15:0  (word 7)
	 * P7_V 31:16 (word 7)
	 * P7_F 15:0  (word 8)
	 **/
	hfrp->info.num_vftable_entries = (response[0] & 0x1f);
	for (ii = 0U; ii < NVDLA_PM_MAX_VFTABLE_ENTRIES; ii++) {
		hfrp->info.vftable_voltage_mV[ii] =
			((response[ii] >> 16) & 0xffffU);
		hfrp->info.vftable_freq_kHz[ii] = (response[ii + 1] & 0xffffU);
	}
}

void hfrp_handle_response(struct hfrp *hfrp,
	uint32_t cmd,
	uint8_t *payload,
	uint32_t payload_size)
{
	// Expects payload to be 4byte aligned. Eases out the parsing.
	WARN_ON(payload_size % 4U != 0U);

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
	case DLA_HFRP_CMD_CONFIG: {
		s_nvdla_hfrp_handle_response_config(hfrp,
			payload, payload_size);
		break;
	}
	case DLA_HFRP_CMD_GET_CURRENT_VOLTAGE: {
		s_nvdla_hfrp_handle_response_current_voltage(hfrp,
			payload, payload_size);
		break;
	}
	case DLA_HFRP_CMD_GET_CURRENT_POWER_DRAW: {
		s_nvdla_hfrp_handle_response_current_power_draw(hfrp,
			payload, payload_size);
		break;
	}
	case DLA_HFRP_CMD_GET_VF_CURVE: {
		s_nvdla_hfrp_handle_response_vfcurve(hfrp,
			payload, payload_size);
		break;
	}
	default:
		/* Nothing to handle */
		break;
	}
}
