// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * NVDLA HFRP Command Implementation
 */

#include "nvdla_pm_hfrp.h"

#include "../../nvdla_debug.h"

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

#define DLA_HFRP_CMD_POWER_CONTROL_SZ               2U
#define DLA_HFRP_CMD_CONFIG_SZ                      12U
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

static int32_t s_nvdla_hfrp_send_cmd_clock_gate(struct hfrp *hfrp,
	struct nvdla_hfrp_cmd_power_ctrl *cmd,
	bool blocking)
{
	int32_t err;

	uint32_t payload[(SZ(DLA_HFRP_CMD_CLOCK_GATE) >> 2) + 1U];
	uint32_t payload_size;

	memset(payload, 0, sizeof(payload));
	payload_size = SZ(DLA_HFRP_CMD_CLOCK_GATE);

	/**
	 * Core 0:0
	 * MCU 1:1
	 * CFG 2:2
	 **/
	payload[0] |= (((uint32_t)(cmd->clock_off)) << 0);
	payload[0] |= (((uint32_t)(cmd->clock_off)) << 1);
	payload[0] |= (((uint32_t)(cmd->clock_off)) << 2);

	if (cmd->clock_off)
		hfrp_handle_cg_entry_start(hfrp);
	else
		hfrp_handle_cg_exit_start(hfrp);

	err = (hfrp_send_cmd(hfrp, DLA_HFRP_CMD_CLOCK_GATE,
			(uint8_t *) payload, payload_size, blocking));

	return err;
}

static int32_t s_nvdla_hfrp_send_cmd_power_ctrl(struct hfrp *hfrp,
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
	 * PPS 15:8
	 **/
	payload[0] |= (((uint32_t)(cmd->power_off)) << 0);
	payload[0] |= (((uint32_t)(cmd->rail_off)) << 1);
	payload[0] |= (((uint32_t)(cmd->power_on)) << 2);
	payload[0] |= (((uint32_t)(cmd->rail_on)) << 3);
	payload[0] |= (((uint32_t)(cmd->power_delayed_off)) << 4);
	payload[0] |= (((uint32_t)(cmd->rail_delayed_off)) << 5);
	payload[0] |= (((uint32_t)(cmd->pps) & 0xffU) << 8);

	/* Run some preactions prior to command execution. */
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

int32_t nvdla_hfrp_send_cmd_power_ctrl(struct hfrp *hfrp,
	struct nvdla_hfrp_cmd_power_ctrl *cmd,
	bool blocking)
{
	int32_t err;
	struct platform_device *pdev;

	pdev = hfrp->pdev;

	if (cmd->clock_off || cmd->clock_on || cmd->clock_delayed_off) {
		err = s_nvdla_hfrp_send_cmd_clock_gate(hfrp, cmd, blocking);
		if (err < 0) {
			nvdla_dbg_err(pdev, "Failed to send clk gate. err %d",
				err);
			goto fail;
		}
	}

	if (cmd->power_off || cmd->power_on || cmd->power_delayed_off ||
			cmd->rail_off || cmd->rail_on ||
			cmd->rail_delayed_off) {
		err = s_nvdla_hfrp_send_cmd_power_ctrl(hfrp, cmd, blocking);
		if (err < 0) {
			nvdla_dbg_err(pdev, "Failed to send power ctrl. err %d",
				err);
			goto fail;
		}
	}

	return 0;

fail:
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
	 **/
	payload[0] |= (((uint32_t)(cmd->pg_delay_ms) & 0xffffU));
	payload[0] |= (((uint32_t)(cmd->rg_delay_ms) & 0xffffU) << 16);
	payload[1] |= ((uint32_t)(cmd->pg_entry_freq_khz));
	payload[2] |= ((uint32_t)(cmd->pg_exit_freq_khz));

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
	(void) hfrp;
	(void) blocking;

	return -EINVAL;
}

static void s_nvdla_hfrp_handle_response_clock_gate(struct hfrp *hfrp,
	uint8_t *payload,
	uint32_t payload_size)
{
	uint32_t *response = (uint32_t *) payload;
	bool gated;

	/**
	 * Core 0:0
	 * MCU 1:1
	 * CFG 2:2
	 **/
	gated = ((response[0]) & 0x1U);
	gated = gated || ((response[0] >> 1) & 0x1U);
	gated = gated || ((response[0] >> 2) & 0x1U);

	if (gated)
		hfrp_handle_cg_entry(hfrp);
	else
		hfrp_handle_cg_exit(hfrp);
}

static void s_nvdla_hfrp_handle_response_power_ctrl(struct hfrp *hfrp,
	uint8_t *payload,
	uint32_t payload_size)
{
	uint32_t *response = (uint32_t *) payload;

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
	 * PPS 15:8
	 **/

	power_off   = ((response[0]) & 0x1u);
	rail_off    = ((response[0] >> 1) & 0x1u);
	power_on    = ((response[0] >> 2) & 0x1u);
	rail_on     = ((response[0] >> 3) & 0x1u);

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

	/**
	 * PG_DELAY 15:0
	 * RG_DELAY 31:16
	 **/
	pg_delay_ms = (response[0] & 0xffffU);
	rg_delay_ms = ((response[0] >> 16) & 0xffffU);

	if (pg_delay_ms > 0)
		hfrp->pg_delay_us = pg_delay_ms * 1000U;

	if (rg_delay_ms > 0)
		hfrp->rg_delay_us = rg_delay_ms * 1000U;
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
	case DLA_HFRP_CMD_CLOCK_GATE: {
		s_nvdla_hfrp_handle_response_clock_gate(hfrp,
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
	default:
		/* Nothing to handle */
		break;
	}
}
