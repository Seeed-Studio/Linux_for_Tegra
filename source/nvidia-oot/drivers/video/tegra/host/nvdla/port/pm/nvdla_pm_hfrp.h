/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 */

#ifndef __NVDLA_PM_HFRP_H_
#define __NVDLA_PM_HFRP_H_

#include <linux/completion.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>

#include "../../dla_os_interface.h"
#include "../nvdla_pm.h"

/* Header and Payload Defines */
#define DLA_HFRP_CMD_PAYLOAD_MAX_LEN            32U
#define DLA_HFRP_RESP_PAYLOAD_MAX_LEN           32U

/* Circular buffer size */
#define DLA_HFRP_CMD_BUFFER_SIZE                116U
#define DLA_HFRP_RESP_BUFFER_SIZE               116U

/* This is reasonable number given command buffer size 116 bytes */
#define DLA_HFRP_MAX_NUM_SEQ                    64U
struct hfrp_cmd_sequence {
	struct hfrp *hfrp;

#define DLA_HFRP_SEQUENCE_ID_ASYNC 0x3ffU
	uint32_t seqid;
	uint32_t cmdid;
	struct completion cmd_completion;

	/* Node pointer to be part of HFRP free list */
	struct list_head list;
};

struct hfrp {
	/* General */
	struct platform_device *pdev;
	void __iomem *regs;
	int irq;

	struct dla_lpwr_config *lpwr_config_va;
	dma_addr_t lpwr_config_pa;

	/* HFRP command management */
	struct mutex cmd_lock;

	struct hfrp_cmd_sequence *sequence_pool;
	uint32_t nsequences;
	struct list_head seq_freelist;

	/* Cache latest response & stats for quick accesses */
	bool clock_gated;
	uint32_t cg_delay_us;
	struct completion cg_delayed_completion;
	uint64_t clock_idle_count;
	uint64_t clock_idle_time_us;
	uint64_t cg_entry_start_timestamp_ns;
	uint64_t cg_entry_timestamp_ns;
	uint64_t cg_entry_latency_us_min;
	uint64_t cg_entry_latency_us_max;
	uint64_t cg_entry_latency_us_total;
	uint64_t clock_active_count;
	uint64_t clock_active_time_us;
	uint64_t cg_exit_start_timestamp_ns;
	uint64_t cg_exit_timestamp_ns;
	uint64_t cg_exit_latency_us_min;
	uint64_t cg_exit_latency_us_max;
	uint64_t cg_exit_latency_us_total;
	uint32_t core_freq_khz;

	bool power_gated;
	uint32_t pg_delay_us;
	struct completion pg_delayed_completion;
	uint64_t power_idle_count;
	uint64_t power_idle_time_us;
	uint64_t pg_entry_start_timestamp_ns;
	uint64_t pg_entry_timestamp_ns;
	uint64_t pg_entry_latency_us_min;
	uint64_t pg_entry_latency_us_max;
	uint64_t pg_entry_latency_us_total;
	uint64_t power_active_count;
	uint64_t power_active_time_us;
	uint64_t pg_exit_start_timestamp_ns;
	uint64_t pg_exit_timestamp_ns;
	uint64_t pg_exit_latency_us_min;
	uint64_t pg_exit_latency_us_max;
	uint64_t pg_exit_latency_us_total;
	uint32_t power_draw_mW;
	uint32_t voltage_mV;

	bool rail_gated;
	uint32_t rg_delay_us;
	struct completion rg_delayed_completion;
	uint64_t rail_idle_count;
	uint64_t rail_idle_time_us;
	uint64_t rg_entry_start_timestamp_ns;
	uint64_t rg_entry_timestamp_ns;
	uint64_t rg_entry_latency_us_min;
	uint64_t rg_entry_latency_us_max;
	uint64_t rg_entry_latency_us_total;
	uint64_t rail_active_count;
	uint64_t rail_active_time_us;
	uint64_t rg_exit_start_timestamp_ns;
	uint64_t rg_exit_timestamp_ns;
	uint64_t rg_exit_latency_us_min;
	uint64_t rg_exit_latency_us_max;
	uint64_t rg_exit_latency_us_total;

	/* vf curve and other info */
	struct nvdla_pm_info info;

	/* Node pointer to be a part of a list. */
	struct list_head list;
};

static inline void hfrp_reg_write1B(struct hfrp *hfrp,
	uint8_t value,
	uint32_t offset)
{
	uint32_t write_value;

	write_value = readl(hfrp->regs + ((offset >> 2U) << 2U));
	write_value &= ~(((uint32_t) 0xffU) << ((offset % 4U) * 8U));
	write_value |= (((uint32_t) value) << ((offset % 4U) * 8U));

	writel(write_value, hfrp->regs + ((offset >> 2U) << 2U));
}

static inline uint8_t hfrp_reg_read1B(struct hfrp *hfrp,
	uint32_t offset)
{
	uint32_t read_value;

	read_value = readl(hfrp->regs + ((offset >> 2U) << 2U));
	read_value = read_value >> ((offset % 4U) * 8U);
	read_value = read_value & 0xffU;

	return (uint8_t) read_value;
}

static inline void hfrp_reg_write(struct hfrp *hfrp,
	uint32_t value,
	uint32_t offset)
{
	writel(value, hfrp->regs + offset);
}

static inline uint32_t hfrp_reg_read(struct hfrp *hfrp, uint32_t offset)
{
	return readl(hfrp->regs + offset);
}

/* Command and Response Headers */
static inline uint32_t hfrp_buffer_cmd_header_size_f(uint32_t v)
{
	/* BUFFER_CMD_HEADER_SIZE 7:0 */
	return (v & 0xffU);
}

static inline uint32_t hfrp_buffer_cmd_header_size_v(uint32_t r)
{
	/* BUFFER_CMD_HEADER_SIZE 7:0 */
	return (r & 0xffU);
}

static inline uint32_t hfrp_buffer_cmd_header_seqid_f(uint32_t v)
{
	/* BUFFER_CMD_HEADER_SEQUENCE_ID 17:8 */
	return ((v & 0x3ffU) << 8);
}

static inline uint32_t hfrp_buffer_cmd_header_seqid_v(uint32_t r)
{
	/* BUFFER_CMD_HEADER_SEQUENCE_ID 17:8 */
	return ((r >> 8) & 0x3ffU);
}

static inline uint32_t hfrp_buffer_cmd_header_cmdid_f(uint32_t v)
{
	/* BUFFER_CMD_HEADER_COMMAND_ID 27:18 */
	return ((v & 0x3ffU) << 18);
}

static inline uint32_t hfrp_buffer_cmd_header_cmdid_v(uint32_t r)
{
	/* BUFFER_CMD_HEADER_COMMAND_ID 27:18 */
	return ((r >> 18) & 0x3ffU);
}

static inline uint32_t hfrp_buffer_resp_header_size_f(uint32_t v)
{
	/* BUFFER_RESPONSE_HEADER_SIZE 7:0 */
	return (v & 0xffU);
}

static inline uint32_t hfrp_buffer_resp_header_size_v(uint32_t r)
{
	/* BUFFER_RESPONSE_HEADER_SIZE 7:0 */
	return (r & 0xffU);
}

static inline uint32_t hfrp_buffer_resp_header_seqid_f(uint32_t v)
{
	/* BUFFER_RESPONSE_HEADER_SEQUENCE_ID 17:8 */
	return ((v & 0x3ffU) << 8);
}

static inline uint32_t hfrp_buffer_resp_header_seqid_v(uint32_t r)
{
	/* BUFFER_RESPONSE_HEADER_SEQUENCE_ID 17:8 */
	return ((r >> 8) & 0x3ffU);
}

static inline uint32_t hfrp_buffer_resp_header_respid_f(uint32_t v)
{
	/* BUFFER_RESPONSE_HEADER_RESPONSE_ID 27:18 */
	return ((v & 0x3ffU) << 18);
}

static inline uint32_t hfrp_buffer_resp_header_respid_v(uint32_t r)
{
	/* BUFFER_RESPONSE_HEADER_RESPONSE_ID 27:18 */
	return ((r >> 18) & 0x3ffU);
}

/**
 * HFRP Communication protocol
 **/
int32_t hfrp_send_cmd(struct hfrp *hfrp,
	uint32_t cmd,
	uint8_t *payload,
	uint32_t payload_size,
	bool blocking);

/**
 * DLA-HFRP Command Request and Response Policy
 **/
void hfrp_handle_response(struct hfrp *hfrp,
	uint32_t cmd,
	uint8_t *payload,
	uint32_t payload_size);

/**
 * Handle clock, power, rail entries and exits.
 **/
void hfrp_handle_cg_entry_start(struct hfrp *hfrp);
void hfrp_handle_cg_entry(struct hfrp *hfrp);
void hfrp_handle_cg_exit_start(struct hfrp *hfrp);
void hfrp_handle_cg_exit(struct hfrp *hfrp);
void hfrp_handle_pg_entry_start(struct hfrp *hfrp);
void hfrp_handle_pg_entry(struct hfrp *hfrp);
void hfrp_handle_pg_exit_start(struct hfrp *hfrp);
void hfrp_handle_pg_exit(struct hfrp *hfrp);
void hfrp_handle_rg_entry_start(struct hfrp *hfrp);
void hfrp_handle_rg_entry(struct hfrp *hfrp);
void hfrp_handle_rg_exit_start(struct hfrp *hfrp);
void hfrp_handle_rg_exit(struct hfrp *hfrp);

/* For gating and ungating of Clock, Power, and Rail */
struct nvdla_hfrp_cmd_power_ctrl {
	bool power_on;
	bool power_off;
	bool power_delayed_off;
	bool rail_on;
	bool rail_off;
	bool rail_delayed_off;
	bool clock_on;
	bool clock_off;
	bool clock_delayed_off;
	int8_t pps;
};

int32_t nvdla_hfrp_send_cmd_power_ctrl(struct hfrp *hfrp,
	struct nvdla_hfrp_cmd_power_ctrl *cmd,
	bool blocking);

/* For configuration of delay value in the event of delayed gating */
struct nvdla_hfrp_cmd_config {
	uint16_t cg_delay_ms;
	uint16_t pg_delay_ms;
	uint16_t rg_delay_ms;

	uint32_t pg_entry_freq_khz;
	uint32_t pg_exit_freq_khz;
};

int32_t nvdla_hfrp_send_cmd_config(struct hfrp *hfrp,
	struct nvdla_hfrp_cmd_config *cmd,
	bool blocking);

/* For getting the current frequency */
int32_t nvdla_hfrp_send_cmd_get_current_freq(struct hfrp *hfrp,
	bool blocking);

/* For getting the current voltage */
int32_t nvdla_hfrp_send_cmd_get_current_voltage(struct hfrp *hfrp,
	bool blocking);

/* For getting the current power draw */
int32_t nvdla_hfrp_send_cmd_get_current_power_draw(struct hfrp *hfrp,
	bool blocking);

/* For getting the vf curve */
int32_t nvdla_hfrp_send_cmd_get_vfcurve(struct hfrp *hfrp,
	bool blocking);

#endif /* __NVDLA_PM_HFRP_H_ */
