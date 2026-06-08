// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * NVDLA Power Management - stub implementation
 */

#include "../nvdla_pm.h"

#include "../nvdla_device.h"
#include "../nvdla_fw.h"
#include "../nvdla_host_wrapper.h"

#include "../../dla_os_interface.h"
#include "../../nvdla.h"
#include "../../nvdla_debug.h"

#include "nvdla_pm_hfrp.h"
#include "nvdla_pm_hfrp_reg.h"

#include <asm/string.h>
#include <clocksource/arm_arch_timer.h>
#include <linux/arm64-barrier.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/mutex.h>

#define NVDLA_HFRP_APERTURE_IDX 1U

static LIST_HEAD(s_hfrp_list);
static DEFINE_MUTEX(s_hfrp_list_lock);

static struct hfrp_cmd_sequence *s_hfrp_cmd_sequence_alloc(struct hfrp *hfrp)
{
	struct hfrp_cmd_sequence *sequence = NULL;

	if (list_empty(&hfrp->seq_freelist))
		goto done;

	sequence = list_first_entry(&hfrp->seq_freelist, typeof(*sequence),
			list);

	/* Allocated successfully, delete it from the free list */
	list_del(&sequence->list);

done:
	return sequence;
}

static void s_hfrp_cmd_sequence_destroy(struct hfrp_cmd_sequence *sequence)
{
	struct hfrp *hfrp = sequence->hfrp;

	/* Return the sequence back to the free list */
	list_add_tail(&sequence->list, &hfrp->seq_freelist);
}

static struct hfrp *s_hfrp_get_by_pdev(struct platform_device *pdev)
{
	struct hfrp *hfrp = NULL;

	mutex_lock(&s_hfrp_list_lock);
	list_for_each_entry(hfrp, &s_hfrp_list, list) {
		if (hfrp->pdev == pdev)
			break;
	}
	spec_bar(); /* break_spec_p#5_1 */
	mutex_unlock(&s_hfrp_list_lock);

	return hfrp;
}

static uint64_t s_hfrp_read_timestamp_ns(void)
{
	uint64_t timestamp;

	/* Report timestamps in TSC ticks, and currently 1 tick = 1 ns */
	timestamp = arch_timer_read_counter();

	return timestamp;
}

void hfrp_handle_cg_entry_start(struct hfrp *hfrp)
{
	/* Update only if ungated -> gated. */
	if (hfrp->clock_gated)
		return;

	hfrp->cg_entry_start_timestamp_ns = s_hfrp_read_timestamp_ns();
}

void hfrp_handle_cg_entry(struct hfrp *hfrp)
{
	uint64_t latency_us;
	uint64_t timestamp;

	/* Update only if ungated -> gated. */
	if (hfrp->clock_gated)
		return;

	timestamp = s_hfrp_read_timestamp_ns();

	hfrp->clock_gated = true;
	hfrp->clock_idle_count++;
	hfrp->cg_entry_timestamp_ns = timestamp;

	/* Update the overall active time. */
	hfrp->clock_active_time_us +=
		((timestamp - hfrp->cg_exit_timestamp_ns) / 1000);

	/* Compute the latency */
	latency_us =
		((hfrp->cg_entry_timestamp_ns -
			hfrp->cg_entry_start_timestamp_ns) / 1000);

	hfrp->cg_entry_latency_us_total += latency_us;
	if (hfrp->cg_entry_latency_us_max < latency_us)
		hfrp->cg_entry_latency_us_max = latency_us;
	if (hfrp->cg_entry_latency_us_min > latency_us)
		hfrp->cg_entry_latency_us_min = latency_us;
}

void hfrp_handle_cg_exit_start(struct hfrp *hfrp)
{
	/* Update only if ungated -> gated. */
	if (!hfrp->clock_gated)
		return;

	hfrp->cg_exit_start_timestamp_ns = s_hfrp_read_timestamp_ns();
}

void hfrp_handle_cg_exit(struct hfrp *hfrp)
{
	uint64_t latency_us;
	uint64_t timestamp;

	/* Update only if gated -> ungated */
	if (!hfrp->clock_gated)
		return;

	timestamp = s_hfrp_read_timestamp_ns();

	hfrp->clock_gated = false;
	hfrp->clock_active_count++;
	hfrp->cg_exit_timestamp_ns = timestamp;

	/* Update the overall idle time. */
	hfrp->clock_idle_time_us +=
		((timestamp - hfrp->cg_entry_timestamp_ns) / 1000);

	/* Compute the latency */
	latency_us =
		((hfrp->cg_exit_timestamp_ns -
			hfrp->cg_exit_start_timestamp_ns) / 1000);

	hfrp->cg_exit_latency_us_total += latency_us;
	if (hfrp->cg_exit_latency_us_max < latency_us)
		hfrp->cg_exit_latency_us_max = latency_us;
	if (hfrp->cg_exit_latency_us_min > latency_us)
		hfrp->cg_exit_latency_us_min = latency_us;
}

void hfrp_handle_pg_entry_start(struct hfrp *hfrp)
{
	/* Update only if ungated -> gated. */
	if (hfrp->power_gated)
		return;

	hfrp->pg_entry_start_timestamp_ns = s_hfrp_read_timestamp_ns();
}

void hfrp_handle_pg_entry(struct hfrp *hfrp)
{
	uint64_t latency_us;
	uint64_t timestamp;

	/* Update only if ungated -> gated. */
	if (hfrp->power_gated)
		return;

	timestamp = s_hfrp_read_timestamp_ns();

	hfrp->power_gated = true;
	hfrp->power_idle_count++;
	hfrp->pg_entry_timestamp_ns = timestamp;

	/* Update the overall active time. */
	hfrp->power_active_time_us +=
		((timestamp - hfrp->pg_exit_timestamp_ns) / 1000);

	/* Compute the latency */
	latency_us =
		((hfrp->pg_entry_timestamp_ns -
			hfrp->pg_entry_start_timestamp_ns) / 1000);

	hfrp->pg_entry_latency_us_total += latency_us;
	if (hfrp->pg_entry_latency_us_max < latency_us)
		hfrp->pg_entry_latency_us_max = latency_us;
	if (hfrp->pg_entry_latency_us_min > latency_us)
		hfrp->pg_entry_latency_us_min = latency_us;
}

void hfrp_handle_pg_exit_start(struct hfrp *hfrp)
{
	/* Update only if ungated -> gated. */
	if (!hfrp->power_gated)
		return;

	hfrp->pg_exit_start_timestamp_ns = s_hfrp_read_timestamp_ns();
}

void hfrp_handle_pg_exit(struct hfrp *hfrp)
{
	uint64_t latency_us;
	uint64_t timestamp;

	/* Update only if gated -> ungated */
	if (!hfrp->power_gated)
		return;

	timestamp = s_hfrp_read_timestamp_ns();

	hfrp->power_gated = false;
	hfrp->power_active_count++;
	hfrp->pg_exit_timestamp_ns = timestamp;

	/* Update the overall idle time. */
	hfrp->power_idle_time_us +=
		((timestamp - hfrp->pg_entry_timestamp_ns) / 1000);

	/* Compute the latency */
	latency_us =
		((hfrp->pg_exit_timestamp_ns -
			hfrp->pg_exit_start_timestamp_ns) / 1000);

	hfrp->pg_exit_latency_us_total += latency_us;
	if (hfrp->pg_exit_latency_us_max < latency_us)
		hfrp->pg_exit_latency_us_max = latency_us;
	if (hfrp->pg_exit_latency_us_min > latency_us)
		hfrp->pg_exit_latency_us_min = latency_us;
}

void hfrp_handle_rg_entry_start(struct hfrp *hfrp)
{
	/* Update only if ungated -> gated. */
	if (hfrp->rail_gated)
		return;

	hfrp->rg_entry_start_timestamp_ns = s_hfrp_read_timestamp_ns();
}

void hfrp_handle_rg_entry(struct hfrp *hfrp)
{
	uint64_t latency_us;
	uint64_t timestamp;

	/* Update only if ungated -> gated. */
	if (hfrp->rail_gated)
		return;

	timestamp = s_hfrp_read_timestamp_ns();

	hfrp->rail_gated = true;
	hfrp->rail_idle_count++;
	hfrp->rg_entry_timestamp_ns = timestamp;

	/* Update the overall active time. */
	hfrp->rail_active_time_us +=
		((timestamp - hfrp->rg_exit_timestamp_ns) / 1000);

	/* Compute the latency */
	latency_us =
		((hfrp->rg_entry_timestamp_ns -
			hfrp->rg_entry_start_timestamp_ns) / 1000);

	hfrp->rg_entry_latency_us_total += latency_us;
	if (hfrp->rg_entry_latency_us_max < latency_us)
		hfrp->rg_entry_latency_us_max = latency_us;
	if (hfrp->rg_entry_latency_us_min > latency_us)
		hfrp->rg_entry_latency_us_min = latency_us;
}

void hfrp_handle_rg_exit_start(struct hfrp *hfrp)
{
	/* Update only if ungated -> gated. */
	if (!hfrp->rail_gated)
		return;

	hfrp->rg_exit_start_timestamp_ns = s_hfrp_read_timestamp_ns();
}

void hfrp_handle_rg_exit(struct hfrp *hfrp)
{
	uint64_t latency_us;
	uint64_t timestamp;

	/* Update only if gated -> ungated */
	if (!hfrp->rail_gated)
		return;

	timestamp = s_hfrp_read_timestamp_ns();

	hfrp->rail_gated = false;
	hfrp->rail_active_count++;
	hfrp->rg_exit_timestamp_ns = timestamp;

	/* Update the overall idle time. */
	hfrp->rail_idle_time_us +=
		((timestamp - hfrp->rg_entry_timestamp_ns) / 1000);

	/* Compute the latency */
	latency_us =
		((hfrp->rg_exit_timestamp_ns -
			hfrp->rg_exit_start_timestamp_ns) / 1000);

	hfrp->rg_exit_latency_us_total += latency_us;
	if (hfrp->rg_exit_latency_us_max < latency_us)
		hfrp->rg_exit_latency_us_max = latency_us;
	if (hfrp->rg_exit_latency_us_min > latency_us)
		hfrp->rg_exit_latency_us_min = latency_us;
}

static void s_hfrp_handle_doorbell(struct hfrp *hfrp)
{
	struct platform_device *pdev;
	uint32_t clientoffs;
	uint32_t serveroffs;
	uint32_t head_offset;
	uint32_t tail_offset;
	uint32_t buffer_size;

	pdev = hfrp->pdev;

	clientoffs = hfrp_reg_read(hfrp, hfrp_buffer_clientoffs_r());
	serveroffs = hfrp_reg_read(hfrp, hfrp_buffer_serveroffs_r());
	head_offset = hfrp_buffer_serveroffs_resp_head_v(serveroffs);
	tail_offset = hfrp_buffer_clientoffs_resp_tail_v(clientoffs);

	buffer_size = DLA_HFRP_RESP_BUFFER_SIZE;

	/* Read all the pending response. */
	while (tail_offset != head_offset) {
		uint32_t ii;
		uint32_t header32;
		uint32_t seqid;
		uint32_t respid;
		uint32_t size;
		uint32_t payload_size;
		uint32_t payload[DLA_HFRP_RESP_PAYLOAD_MAX_LEN];
		uint8_t *payload8b;
		struct hfrp_cmd_sequence *sequence;

		/* read header */
		header32 = 0U;
		for (ii = 0U; ii < sizeof(header32); ii++) {
			uint8_t header_byte;

			header_byte = hfrp_reg_read1B(hfrp,
				hfrp_buffer_resp_r(tail_offset));
			header32 |= (((uint32_t) header_byte) << (ii * 8U));
			tail_offset = ((tail_offset + 1U) % buffer_size);
		}

		seqid = hfrp_buffer_resp_header_seqid_v(header32);
		respid = hfrp_buffer_resp_header_seqid_v(header32);
		size = hfrp_buffer_resp_header_size_v(header32);
		payload_size = size - sizeof(header32);

		nvdla_dbg_info(pdev, "seqid=%x respid=%x size=%x\n",
				seqid, respid, size);

		/* read payload. */
		payload8b = (uint8_t *) payload;
		for (ii = 0U; ii < payload_size; ii++) {
			payload8b[ii] = hfrp_reg_read1B(hfrp,
				hfrp_buffer_resp_r(tail_offset));
			tail_offset = ((tail_offset + 1U) % buffer_size);
		}

		/* Signal sequence ID only on success. */
		if (seqid != DLA_HFRP_SEQUENCE_ID_ASYNC) {
			sequence = &hfrp->sequence_pool[seqid];
			hfrp_handle_response(hfrp, sequence->cmdid,
					(uint8_t *) payload, sizeof(payload));
			complete(&sequence->cmd_completion);
			s_hfrp_cmd_sequence_destroy(sequence);
		}
	}

	/* Update the tail offset */
	clientoffs &= ~(hfrp_buffer_clientoffs_resp_tail_m());
	clientoffs |= hfrp_buffer_clientoffs_resp_tail_f(head_offset);
	hfrp_reg_write(hfrp, clientoffs, hfrp_buffer_clientoffs_r());
}

static irqreturn_t s_hfrp_isr(int irq, void *dev_id)
{
	struct platform_device *pdev;
	struct hfrp *hfrp;
	uint32_t intstat;

	(void) irq;

	pdev = (struct platform_device *)(dev_id);
	hfrp = s_hfrp_get_by_pdev(pdev);
	intstat = hfrp_reg_read(hfrp, hfrp_irq_out_set_r());

	nvdla_dbg_info(pdev, "Received interrupt: %x\n", intstat);

	if (intstat == 0U) {
		nvdla_dbg_warn(pdev, "Spurious interrupt\n");
		goto done;
	}

	if (hfrp_irq_out_set_reset_v(intstat) > 0) {
		/* Clear the reset bit */
		hfrp_reg_write(hfrp, hfrp_irq_out_clr_reset_f(1U),
			hfrp_irq_out_clr_r());
	}

	if (hfrp_irq_out_set_doorbell_v(intstat) > 0) {
		/* handle doorbell response interrupt */
		s_hfrp_handle_doorbell(hfrp);

		/* Clear the doorbell. Intentionally cleared after handling. */
		hfrp_reg_write(hfrp, hfrp_irq_out_clr_doorbell_f(1U),
				hfrp_irq_out_clr_r());
	}

	if (hfrp_irq_out_set_cgstart_v(intstat) > 0) {
		/* Clear the cgstart bit */
		hfrp_reg_write(hfrp, hfrp_irq_out_clr_cgstart_f(1U),
			hfrp_irq_out_clr_r());

		hfrp_handle_cg_entry_start(hfrp);
	}

	if (hfrp_irq_out_set_cgend_v(intstat) > 0) {
		/* Clear the cgend bit */
		hfrp_reg_write(hfrp, hfrp_irq_out_clr_cgend_f(1U),
			hfrp_irq_out_clr_r());

		hfrp_handle_cg_entry(hfrp);

		/* Notify the waiters for delayed completion */
		complete(&hfrp->cg_delayed_completion);
	}

	if (hfrp_irq_out_set_pgstart_v(intstat) > 0) {
		/* Clear the pgstart bit */
		hfrp_reg_write(hfrp, hfrp_irq_out_clr_pgstart_f(1U),
			hfrp_irq_out_clr_r());

		hfrp_handle_pg_entry_start(hfrp);
	}

	if (hfrp_irq_out_set_pgend_v(intstat) > 0) {
		/* Clear the pgend bit */
		hfrp_reg_write(hfrp, hfrp_irq_out_clr_pgend_f(1U),
			hfrp_irq_out_clr_r());

		hfrp_handle_pg_entry(hfrp);

		/* Notify the waiters for delayed completion */
		complete(&hfrp->pg_delayed_completion);
	}

	if (hfrp_irq_out_set_rgstart_v(intstat) > 0) {
		/* Clear the rgstart bit */
		hfrp_reg_write(hfrp, hfrp_irq_out_clr_rgstart_f(1U),
			hfrp_irq_out_clr_r());

		hfrp_handle_rg_entry_start(hfrp);
	}

	if (hfrp_irq_out_set_rgend_v(intstat) > 0) {
		/* Clear the rgend bit */
		hfrp_reg_write(hfrp, hfrp_irq_out_clr_rgend_f(1U),
			hfrp_irq_out_clr_r());

		hfrp_handle_rg_entry(hfrp);

		/* Notify the waiters for delayed completion */
		complete(&hfrp->rg_delayed_completion);
	}

done:
	/* For now clear unhandled interrupt lines */
	hfrp_reg_write(hfrp,
		~(hfrp_irq_out_clr_reset_f(1U) |
			hfrp_irq_out_clr_doorbell_f(1U) |
			hfrp_irq_out_clr_cgstart_f(1U) |
			hfrp_irq_out_clr_cgend_f(1U) |
			hfrp_irq_out_clr_pgstart_f(1U) |
			hfrp_irq_out_clr_pgend_f(1U) |
			hfrp_irq_out_clr_rgstart_f(1U) |
			hfrp_irq_out_clr_rgend_f(1U)),
		hfrp_irq_out_clr_r());

	return IRQ_HANDLED;
}

int32_t hfrp_send_cmd(struct hfrp *hfrp,
	uint32_t cmd,
	uint8_t *payload,
	uint32_t payload_size,
	bool blocking)
{
	int32_t err;

	int32_t ii;
	uint32_t header32;
	uint32_t header_size;
	uint8_t *header;

	uint32_t clientoffs;
	uint32_t serveroffs;
	uint32_t head_offset;
	uint32_t tail_offset;
	uint32_t buffer_size;
	uint32_t buffer_used;

	uint64_t timeout;

	struct hfrp_cmd_sequence *sequence;

	header32 = 0U;
	header_size = sizeof(header32);
	header = (uint8_t *) &header32;

	/* Check if we have sufficient space for sending command. */
	clientoffs = hfrp_reg_read(hfrp, hfrp_buffer_clientoffs_r());
	serveroffs = hfrp_reg_read(hfrp, hfrp_buffer_serveroffs_r());

	head_offset = hfrp_buffer_clientoffs_cmd_head_v(clientoffs);
	tail_offset = hfrp_buffer_serveroffs_cmd_tail_v(serveroffs);
	buffer_size = DLA_HFRP_CMD_BUFFER_SIZE;
	buffer_used = ((buffer_size + head_offset - tail_offset) % buffer_size);
	if ((buffer_used + header_size + payload_size) >= buffer_size) {
		err = -ENOMEM;
		goto fail;
	}

	sequence = s_hfrp_cmd_sequence_alloc(hfrp);
	if (sequence == NULL) {
		err = -ENOMEM;
		goto fail;
	}

	sequence->cmdid = cmd;

	nvdla_dbg_info(hfrp->pdev, "seqid=%x cmdid=%x\n",
		sequence->seqid, sequence->cmdid);

	/* Construct header */
	header32 |= hfrp_buffer_cmd_header_size_f((payload_size + header_size));
	header32 |= hfrp_buffer_cmd_header_seqid_f(sequence->seqid);
	header32 |= hfrp_buffer_cmd_header_cmdid_f(cmd);

	/* Copy the header and payload byte-by-byte (to be safe). */
	for (ii = 0U; ii < header_size; ii++) {
		hfrp_reg_write1B(hfrp, header[ii],
			hfrp_buffer_cmd_r(head_offset));
		head_offset = ((head_offset + 1U) % buffer_size);
	}

	for (ii = 0U; ii < payload_size; ii++) {
		hfrp_reg_write1B(hfrp, payload[ii],
			hfrp_buffer_cmd_r(head_offset));
		head_offset = ((head_offset + 1U) % buffer_size);
	}

	/* Update the command head pointer */
	clientoffs &= ~(hfrp_buffer_clientoffs_cmd_head_m());
	clientoffs |= hfrp_buffer_clientoffs_cmd_head_f(head_offset);
	hfrp_reg_write(hfrp, clientoffs, hfrp_buffer_clientoffs_r());

	/* Ring the door bell */
	hfrp_reg_write(hfrp, hfrp_irq_in_set_doorbell_f(1U),
		hfrp_irq_in_set_r());

	/* Block if requested for response and error with 1s timeout */
	if (blocking) {
		timeout = msecs_to_jiffies(1000U);
		if (!wait_for_completion_timeout(&sequence->cmd_completion,
				timeout)) {
			nvdla_dbg_err(hfrp->pdev,
				"DLA-HFRP response timedout.\n");
			err = -ETIMEDOUT;
			goto fail;
		}
	}
	return 0;

fail:
	return err;
}

/* PM Implementation */
static int32_t s_nvdla_pm_lpwr_config_reset(struct platform_device *pdev)
{
	int32_t err = 0;
	struct nvdla_cmd_data cmd_data;
	struct hfrp *hfrp;

	if (pdev == NULL) {
		err = -EFAULT;
		goto fail;
	}

	hfrp = s_hfrp_get_by_pdev(pdev);
	if (hfrp == NULL) {
		err = -EINVAL;
		nvdla_dbg_err(pdev, "Invalid hfrp handle");
		goto fail;
	}

	/* prepare command data */
	cmd_data.method_id = DLA_CMD_SET_LPWR_CONFIG;
	cmd_data.method_data = ALIGNED_DMA(hfrp->lpwr_config_pa);
	cmd_data.wait = true;

	/* pass set debug command to falcon */
	err = nvdla_fw_send_cmd(pdev, &cmd_data);
	if (err != 0) {
		nvdla_dbg_err(pdev, "failed to send set lpwr config command");
		goto fail;
	}

	return 0;

fail:
	return err;
}

static int32_t s_nvdla_pm_lpwr_config_init(struct platform_device *pdev)
{
	int32_t err = 0;
	struct nvdla_cmd_data cmd_data;
	struct hfrp *hfrp;

	if (pdev == NULL) {
		err = -EFAULT;
		goto fail;
	}

	hfrp = s_hfrp_get_by_pdev(pdev);
	if (hfrp == NULL) {
		err = -EINVAL;
		nvdla_dbg_err(pdev, "Invalid hfrp handle");
		goto fail;
	}

	/* prepare command data */
	cmd_data.method_id = DLA_CMD_GET_LPWR_CONFIG;
	cmd_data.method_data = ALIGNED_DMA(hfrp->lpwr_config_pa);
	cmd_data.wait = true;

	/* pass set debug command to falcon */
	err = nvdla_fw_send_cmd(pdev, &cmd_data);
	if (err != 0) {
		nvdla_dbg_err(pdev, "failed to send set lpwr config command");
		goto fail;
	}

	return 0;

fail:
	return err;
}

int32_t nvdla_pm_init(struct platform_device *pdev)
{
	int32_t err;
	int32_t irq;
	struct hfrp *hfrp;
	struct nvhost_device_data *pdata;
	struct nvdla_device *nvdladev;
	void __iomem *regs;
	struct hfrp_cmd_sequence *seqpool;
	uint64_t timestamp;
	uint32_t ii;

	pdata = platform_get_drvdata(pdev);
	nvdladev = pdata->private_data;
	regs = pdata->aperture[NVDLA_HFRP_APERTURE_IDX];

	irq = platform_get_irq(pdev, NVDLA_HFRP_APERTURE_IDX);
	if (irq < 0) {
		nvdla_dbg_err(pdev, "failed to get HFRP IRQ\n");
		err = -ENXIO;
		goto fail;
	}

	err = devm_request_irq(&pdev->dev, irq, s_hfrp_isr, 0,
			dev_name(&pdev->dev), pdev);
	if (err) {
		nvdla_dbg_err(pdev, "failed to request hfrp irq. err %d\n",
			err);
		goto fail;
	}

	/* Keep the interrupt disabled */
	disable_irq(irq);

	/* Device managed allocation - for HFRP */
	hfrp = devm_kzalloc(&pdev->dev, sizeof(*hfrp), GFP_KERNEL);
	if (!hfrp) {
		err = -ENOMEM;
		goto free_irq;
	}

	/* Device managed allocation - for sequence pool */
	seqpool = devm_kzalloc(&pdev->dev,
				sizeof(struct hfrp_cmd_sequence) *
					DLA_HFRP_MAX_NUM_SEQ,
				GFP_KERNEL);
	if (!seqpool) {
		err = -ENOMEM;
		goto free_hfrp;
	}

	/* Device managed allocation - for lpwr config */
	hfrp->lpwr_config_va =
		dma_alloc_attrs(&pdev->dev, sizeof(struct dla_lpwr_config),
			&hfrp->lpwr_config_pa, GFP_KERNEL, 0);
	if (hfrp->lpwr_config_va == NULL) {
		nvdla_dbg_err(pdev, "lpwr_config dma alloc failed");
		err = -ENOMEM;
		goto free_hfrp_sequence_pool;
	}

	hfrp->pdev = pdev;
	hfrp->irq = irq;
	hfrp->regs = regs;
	hfrp->sequence_pool = seqpool;
	hfrp->nsequences = DLA_HFRP_MAX_NUM_SEQ;
	INIT_LIST_HEAD(&hfrp->seq_freelist);
	mutex_init(&hfrp->cmd_lock);
	for (ii = 0U; ii < DLA_HFRP_MAX_NUM_SEQ; ii++) {
		struct hfrp_cmd_sequence *sequence;

		sequence = &hfrp->sequence_pool[ii];

		sequence->hfrp = hfrp;
		sequence->seqid = ii;
		sequence->cmdid = 0U;
		init_completion(&sequence->cmd_completion);

		list_add_tail(&sequence->list, &hfrp->seq_freelist);
	}

	timestamp = s_hfrp_read_timestamp_ns();
	init_completion(&hfrp->cg_delayed_completion);
	hfrp->cg_entry_latency_us_min = ~((uint64_t) 0ULL);
	hfrp->cg_exit_latency_us_min = ~((uint64_t) 0ULL);
	hfrp->cg_exit_timestamp_ns = timestamp;
	hfrp->clock_active_count = 1ULL;

	init_completion(&hfrp->pg_delayed_completion);
	hfrp->pg_entry_latency_us_min = ~((uint64_t) 0ULL);
	hfrp->pg_exit_latency_us_min = ~((uint64_t) 0UL);
	hfrp->pg_exit_timestamp_ns = timestamp;
	hfrp->power_active_count = 1ULL;

	init_completion(&hfrp->rg_delayed_completion);
	hfrp->rg_entry_latency_us_min = ~((uint64_t) 0ULL);
	hfrp->rg_exit_latency_us_min = ~((uint64_t) 0ULL);
	hfrp->rg_exit_timestamp_ns = timestamp;
	hfrp->rail_active_count = 1ULL;

	mutex_lock(&s_hfrp_list_lock);
	list_add_tail(&hfrp->list, &s_hfrp_list);
	mutex_unlock(&s_hfrp_list_lock);

	enable_irq(irq);

	/* Select circular mode */
	hfrp_reg_write(hfrp, hfrp_mailbox0_mode_circular_v(),
		hfrp_mailbox0_mode_r());

	return 0;

free_hfrp_sequence_pool:
	devm_kfree(&pdev->dev, seqpool);
free_hfrp:
	devm_kfree(&pdev->dev, hfrp);
free_irq:
	devm_free_irq(&pdev->dev, irq, pdev);
fail:
	return err;
}

void nvdla_pm_deinit(struct platform_device *pdev)
{
	struct hfrp *hfrp;

	hfrp = s_hfrp_get_by_pdev(pdev);
	if (hfrp != NULL) {
		mutex_lock(&s_hfrp_list_lock);
		list_del(&hfrp->list);
		mutex_unlock(&s_hfrp_list_lock);

		mutex_destroy(&hfrp->cmd_lock);
		disable_irq(hfrp->irq);
		devm_free_irq(&pdev->dev, hfrp->irq, pdev);

		if (hfrp->lpwr_config_pa) {
			/* Free the lpwr_config memory. */
			dma_free_attrs(&pdev->dev,
				sizeof(struct dla_lpwr_config),
				hfrp->lpwr_config_va,
				hfrp->lpwr_config_pa,
				0);

			hfrp->lpwr_config_va = NULL;
			hfrp->lpwr_config_pa = 0;
		}

		devm_kfree(&pdev->dev, hfrp->sequence_pool);
		devm_kfree(&pdev->dev, hfrp);
	}
}

int32_t nvdla_pm_rail_gate(struct platform_device *pdev,
	bool blocking)
{
	int32_t err;

	struct nvdla_hfrp_cmd_power_ctrl cmd;
	struct hfrp *hfrp;
	uint64_t timeout;

	hfrp = s_hfrp_get_by_pdev(pdev);
	if (hfrp == NULL) {
		err = -EINVAL;
		goto fail;
	}

	/* Send power control command */
	memset(&cmd, 0, sizeof(cmd));
	if (hfrp->rg_delay_us > 0U)
		cmd.rail_delayed_off = true;
	else
		cmd.rail_off = true;
	cmd.pps = 3U;

	err = nvdla_hfrp_send_cmd_power_ctrl(hfrp, &cmd, blocking);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Power ctrl cmd send fail. err %d", err);
		goto fail;
	}

	/* In case of delayed off, we need to wait for delayed completion. */
	if (blocking && (hfrp->rg_delay_us > 0U)) {
		/* +1s to account for overall contentions across the KMD */
		timeout = msecs_to_jiffies((hfrp->rg_delay_us / 1000) + 1000U);
		if (!wait_for_completion_timeout(&hfrp->rg_delayed_completion,
				timeout)) {
			nvdla_dbg_err(hfrp->pdev,
				"rg delayed off - response timed out.\n");
			err = -ETIMEDOUT;
			goto fail;
		}
	}

	return 0;

fail:
	return err;
}

int32_t nvdla_pm_rail_ungate(struct platform_device *pdev)
{
	int32_t err;

	struct nvdla_hfrp_cmd_power_ctrl cmd;
	struct hfrp *hfrp;

	hfrp = s_hfrp_get_by_pdev(pdev);
	if (hfrp == NULL) {
		err = -EINVAL;
		goto fail;
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.rail_on = true;

	err = nvdla_hfrp_send_cmd_power_ctrl(hfrp, &cmd, true);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Power ctrl cmd send fail. err %d", err);
		goto fail;
	}

	return 0;

fail:
	return err;
}

int32_t nvdla_pm_rail_is_gated(struct platform_device *pdev,
	bool *gated)
{
	int32_t err;
	struct hfrp *hfrp;

	hfrp = s_hfrp_get_by_pdev(pdev);
	if (hfrp == NULL) {
		err = -EINVAL;
		goto fail;
	}

	mutex_lock(&hfrp->cmd_lock);
	*gated = hfrp->rail_gated;
	mutex_unlock(&hfrp->cmd_lock);

	return 0;
fail:
	return err;
}

int32_t nvdla_pm_rail_gate_set_delay_us(struct platform_device *pdev,
	uint32_t delay_us)
{
	int32_t err;

	struct hfrp *hfrp;
	struct nvdla_hfrp_cmd_config config_cmd;

	hfrp = s_hfrp_get_by_pdev(pdev);
	if (hfrp == NULL) {
		err = -EINVAL;
		goto fail;
	}

	/* Configure delay values through config command */
	if (delay_us > 0U) {
		memset(&config_cmd, 0, sizeof(config_cmd));
		config_cmd.rg_delay_ms = delay_us / 1000;
		err = nvdla_hfrp_send_cmd_config(hfrp, &config_cmd, true);
		if (err < 0) {
			nvdla_dbg_err(pdev, "Config cmd send fail. err %d",
				err);
			goto fail;
		}
	} else {
		/* Zero delay means immediate gating and no need to configure */
		hfrp->rg_delay_us = 0U;
	}

	return 0;

fail:
	return err;
}

int32_t nvdla_pm_rail_gate_get_delay_us(struct platform_device *pdev,
	uint32_t *delay_us)
{
	int32_t err;
	struct hfrp *hfrp;

	hfrp = s_hfrp_get_by_pdev(pdev);
	if (hfrp == NULL) {
		err = -EINVAL;
		goto fail;
	}

	*delay_us = hfrp->rg_delay_us;

	return 0;

fail:
	return err;
}

int32_t nvdla_pm_power_gate(struct platform_device *pdev,
	bool blocking)
{
	int32_t err;

	struct nvdla_hfrp_cmd_power_ctrl cmd;
	struct hfrp *hfrp;
	uint64_t timeout;

	hfrp = s_hfrp_get_by_pdev(pdev);
	if (hfrp == NULL) {
		err = -EINVAL;
		goto fail;
	}

	/* Send power control command */
	memset(&cmd, 0, sizeof(cmd));
	if (hfrp->pg_delay_us > 0U)
		cmd.power_delayed_off = true;
	else
		cmd.power_off = true;

	err = nvdla_hfrp_send_cmd_power_ctrl(hfrp, &cmd, blocking);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Power ctrl cmd send fail. err %d", err);
		goto fail;
	}

	/* In case of delayed off, we need to wait for delayed completion. */
	if (blocking && (hfrp->pg_delay_us > 0U)) {
		/* +1s to account for overall contentions across the KMD */
		timeout = msecs_to_jiffies((hfrp->pg_delay_us / 1000) + 1000U);
		if (!wait_for_completion_timeout(&hfrp->pg_delayed_completion,
				timeout)) {
			nvdla_dbg_err(hfrp->pdev,
				"pg delayed off - response timed out.\n");
			err = -ETIMEDOUT;
			goto fail;
		}
	}

	return 0;

fail:
	return err;
}

int32_t nvdla_pm_power_ungate(struct platform_device *pdev)
{
	int32_t err;

	struct nvdla_hfrp_cmd_power_ctrl cmd;
	struct hfrp *hfrp;

	hfrp = s_hfrp_get_by_pdev(pdev);
	if (hfrp == NULL) {
		err = -EINVAL;
		goto fail;
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.power_on = true;

	err = nvdla_hfrp_send_cmd_power_ctrl(hfrp, &cmd, true);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Power ctrl cmd send fail. err %d", err);
		goto fail;
	}

	return 0;

fail:
	return err;
}

int32_t nvdla_pm_power_is_gated(struct platform_device *pdev,
	bool *gated)
{
	int32_t err;
	struct hfrp *hfrp;

	hfrp = s_hfrp_get_by_pdev(pdev);
	if (hfrp == NULL) {
		err = -EINVAL;
		goto fail;
	}

	mutex_lock(&hfrp->cmd_lock);
	*gated = hfrp->power_gated;
	mutex_unlock(&hfrp->cmd_lock);

	return 0;
fail:
	return err;
}

int32_t nvdla_pm_power_gate_set_delay_us(struct platform_device *pdev,
	uint32_t delay_us)
{
	int32_t err;

	struct hfrp *hfrp;
	struct nvdla_hfrp_cmd_config config_cmd;

	hfrp = s_hfrp_get_by_pdev(pdev);
	if (hfrp == NULL) {
		err = -EINVAL;
		goto fail;
	}

	/* Configure delay values through config command */
	if (delay_us > 0U) {
		memset(&config_cmd, 0, sizeof(config_cmd));
		config_cmd.pg_delay_ms = delay_us / 1000;
		err = nvdla_hfrp_send_cmd_config(hfrp, &config_cmd, true);
		if (err < 0) {
			nvdla_dbg_err(pdev, "Config cmd send fail. err %d",
				err);
			goto fail;
		}
	} else {
		/* Zero delay means immediate gating and no need to configure */
		hfrp->pg_delay_us = 0U;
	}

	return 0;

fail:
	return err;
}

int32_t nvdla_pm_power_gate_get_delay_us(struct platform_device *pdev,
	uint32_t *delay_us)
{
	int32_t err;
	struct hfrp *hfrp;

	hfrp = s_hfrp_get_by_pdev(pdev);
	if (hfrp == NULL) {
		err = -EINVAL;
		goto fail;
	}

	*delay_us = hfrp->pg_delay_us;

	return 0;

fail:
	return err;
}

int32_t nvdla_pm_clock_gate(struct platform_device *pdev,
	bool blocking)
{
	int32_t err;

	struct nvdla_hfrp_cmd_power_ctrl cmd;
	struct hfrp *hfrp;
	uint64_t timeout;

	hfrp = s_hfrp_get_by_pdev(pdev);
	if (hfrp == NULL) {
		err = -EINVAL;
		goto fail;
	}

	/* Send power control command */
	memset(&cmd, 0, sizeof(cmd));
	if (hfrp->cg_delay_us > 0)
		cmd.clock_delayed_off = true;
	else
		cmd.clock_off = true;

	err = nvdla_hfrp_send_cmd_power_ctrl(hfrp, &cmd, blocking);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Power ctrl cmd send fail. err %d", err);
		goto fail;
	}

	/* In case of delayed off, we need to wait for delayed completion. */
	if (blocking && (hfrp->cg_delay_us > 0U)) {
		/* +1s to account for overall contentions across the KMD */
		timeout = msecs_to_jiffies((hfrp->cg_delay_us / 1000) + 1000U);
		if (!wait_for_completion_timeout(&hfrp->cg_delayed_completion,
				timeout)) {
			nvdla_dbg_err(hfrp->pdev,
				"cg delayed off - response timed out.\n");
			err = -ETIMEDOUT;
			goto fail;
		}
	}

	return 0;

fail:
	return err;
}

int32_t nvdla_pm_clock_ungate(struct platform_device *pdev)
{
	int32_t err;

	struct nvdla_hfrp_cmd_power_ctrl cmd;
	struct hfrp *hfrp;

	hfrp = s_hfrp_get_by_pdev(pdev);
	if (hfrp == NULL) {
		err = -EINVAL;
		goto fail;
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.clock_on = true;

	err = nvdla_hfrp_send_cmd_power_ctrl(hfrp, &cmd, true);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Power ctrl cmd send fail. err %d", err);
		goto fail;
	}

	return 0;
fail:
	return err;
}

int32_t nvdla_pm_clock_is_gated(struct platform_device *pdev,
	bool *gated)
{
	int32_t err;
	struct hfrp *hfrp;

	hfrp = s_hfrp_get_by_pdev(pdev);
	if (hfrp == NULL) {
		err = -EINVAL;
		goto fail;
	}

	mutex_lock(&hfrp->cmd_lock);
	*gated = hfrp->clock_gated;
	mutex_unlock(&hfrp->cmd_lock);

	return 0;
fail:
	return err;
}

int32_t nvdla_pm_clock_gate_set_delay_us(struct platform_device *pdev,
	uint32_t delay_us)
{
	int32_t err;

	struct hfrp *hfrp;
	struct nvdla_hfrp_cmd_config config_cmd;

	hfrp = s_hfrp_get_by_pdev(pdev);
	if (hfrp == NULL) {
		err = -EINVAL;
		goto fail;
	}

	/* Configure delay values through config command */
	if (delay_us > 0U) {
		memset(&config_cmd, 0, sizeof(config_cmd));
		config_cmd.cg_delay_ms = delay_us / 1000;
		err = nvdla_hfrp_send_cmd_config(hfrp, &config_cmd, true);
		if (err < 0) {
			nvdla_dbg_err(pdev, "Config cmd send fail. err %d",
				err);
			goto fail;
		}
	} else {
		/* Zero delay means immediate gating and no need to configure */
		hfrp->cg_delay_us = 0U;
	}

	return 0;

fail:
	return err;
}

int32_t nvdla_pm_clock_gate_get_delay_us(struct platform_device *pdev,
	uint32_t *delay_us)
{
	int32_t err;
	struct hfrp *hfrp;

	hfrp = s_hfrp_get_by_pdev(pdev);
	if (hfrp == NULL) {
		err = -EINVAL;
		goto fail;
	}

	*delay_us = hfrp->cg_delay_us;

	return 0;

fail:
	return err;
}

int32_t nvdla_pm_clock_set_core_freq(struct platform_device *pdev,
	uint32_t freq_khz)
{
	int32_t err = 0;
	struct nvdla_cmd_data cmd_data;

	/* prepare command data */
	cmd_data.method_id = DLA_CMD_SET_CLOCK_FREQ;
	cmd_data.method_data = freq_khz;
	cmd_data.wait = true;

	if (pdev == NULL) {
		err = -EFAULT;
		goto fail_no_dev;
	}

	/* make sure that device is powered on */
	err = nvdla_module_busy(pdev);
	if (err != 0) {
		nvdla_dbg_err(pdev, "failed to power on\n");
		err = -ENODEV;
		goto fail_no_dev;
	}

	/* pass set debug command to falcon */
	err = nvdla_fw_send_cmd(pdev, &cmd_data);
	if (err != 0) {
		nvdla_dbg_err(pdev, "failed to send set freq command");
		goto fail_to_send_cmd;
	}

fail_to_send_cmd:
	nvdla_module_idle(pdev);
fail_no_dev:
	return err;
}

int32_t nvdla_pm_clock_get_mcu_freq(struct platform_device *pdev,
	uint32_t *freq_khz)
{
	int32_t err;

	err = nvdla_pm_clock_get_core_freq(pdev, freq_khz);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to get mcu freq. err: %d\n", err);
		goto fail;
	}

	/* Core frequency is twice the MCU frequency. */
	*freq_khz = ((*freq_khz) >> 1);

	return 0;

fail:
	return err;
}

int32_t nvdla_pm_clock_set_mcu_freq(struct platform_device *pdev,
	uint32_t freq_khz)
{
	int32_t err;

	/* Core frequency is twice the MCU frequency. */
	err = nvdla_pm_clock_set_core_freq(pdev, (freq_khz << 1));
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to get freq. err: %d\n", err);
		goto fail;
	}

	return 0;

fail:
	return err;
}

int32_t nvdla_pm_clock_get_core_freq(struct platform_device *pdev,
	uint32_t *freq_khz)
{
	int32_t err;

	struct hfrp *hfrp;

	hfrp = s_hfrp_get_by_pdev(pdev);
	if (hfrp == NULL) {
		err = -EINVAL;
		goto fail;
	}

	err = nvdla_hfrp_send_cmd_get_current_freq(hfrp, true);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to get freq. err: %d\n", err);
		goto fail;
	}

	*freq_khz = hfrp->core_freq_khz;

	return 0;

fail:
	return err;
}

int32_t nvdla_pm_get_stat(struct platform_device *pdev,
	struct nvdla_pm_stat *stat)
{
	int32_t err;

	struct hfrp *hfrp;
	uint64_t timestamp;

	hfrp = s_hfrp_get_by_pdev(pdev);
	if (hfrp == NULL) {
		nvdla_dbg_err(pdev, "Failed to fetch HFRP handle\n");
		err = -EINVAL;
		goto fail;
	}

	if (stat == NULL) {
		nvdla_dbg_err(pdev, "Null stat\n");
		err = -EINVAL;
		goto fail;
	}

	timestamp = s_hfrp_read_timestamp_ns();

	/* Clock Stats */
	stat->clock_idle_count = hfrp->clock_idle_count;
	stat->clock_idle_time_us = hfrp->clock_idle_time_us;
	if (hfrp->clock_gated) {
		stat->clock_idle_time_us +=
			(timestamp - hfrp->cg_entry_timestamp_ns) / 1000;
	}
	stat->cg_entry_latency_us_min = hfrp->cg_entry_latency_us_min;
	stat->cg_entry_latency_us_max = hfrp->cg_entry_latency_us_max;
	stat->cg_entry_latency_us_total = hfrp->cg_entry_latency_us_total;

	stat->clock_active_count = hfrp->clock_active_count;
	stat->clock_active_time_us = hfrp->clock_active_time_us;
	if (!hfrp->clock_gated) {
		stat->clock_active_time_us +=
			(timestamp - hfrp->cg_exit_timestamp_ns) / 1000;
	}
	stat->cg_exit_latency_us_min = hfrp->cg_exit_latency_us_min;
	stat->cg_exit_latency_us_max = hfrp->cg_exit_latency_us_max;
	stat->cg_exit_latency_us_total = hfrp->cg_exit_latency_us_total;

	/* Power Stats */
	stat->power_idle_count = hfrp->power_idle_count;
	stat->power_idle_time_us = hfrp->power_idle_time_us;
	if (hfrp->power_gated) {
		stat->power_idle_time_us +=
			(timestamp - hfrp->pg_entry_timestamp_ns) / 1000;
	}
	stat->pg_entry_latency_us_min = hfrp->pg_entry_latency_us_min;
	stat->pg_entry_latency_us_max = hfrp->pg_entry_latency_us_max;
	stat->pg_entry_latency_us_total = hfrp->pg_entry_latency_us_total;

	stat->power_active_count = hfrp->power_active_count;
	stat->power_active_time_us = hfrp->power_active_time_us;
	if (!hfrp->power_gated) {
		stat->power_active_time_us +=
			(timestamp - hfrp->pg_exit_timestamp_ns) / 1000;
	}
	stat->pg_exit_latency_us_min = hfrp->pg_exit_latency_us_min;
	stat->pg_exit_latency_us_max = hfrp->pg_exit_latency_us_max;
	stat->pg_exit_latency_us_total = hfrp->pg_exit_latency_us_total;

	/* Rail Stats */
	stat->rail_idle_count = hfrp->rail_idle_count;
	stat->rail_idle_time_us = hfrp->rail_idle_time_us;
	if (hfrp->rail_gated) {
		stat->rail_idle_time_us +=
			(timestamp - hfrp->rg_entry_timestamp_ns) / 1000;
	}
	stat->rg_entry_latency_us_min = hfrp->rg_entry_latency_us_min;
	stat->rg_entry_latency_us_max = hfrp->rg_entry_latency_us_max;
	stat->rg_entry_latency_us_total = hfrp->rg_entry_latency_us_total;

	stat->rail_active_count = hfrp->rail_active_count;
	stat->rail_active_time_us = hfrp->rail_active_time_us;
	if (!hfrp->rail_gated) {
		stat->rail_active_time_us +=
			(timestamp - hfrp->rg_exit_timestamp_ns) / 1000;
	}
	stat->rg_exit_latency_us_min = hfrp->rg_exit_latency_us_min;
	stat->rg_exit_latency_us_max = hfrp->rg_exit_latency_us_max;
	stat->rg_exit_latency_us_total = hfrp->rg_exit_latency_us_total;

	return 0;

fail:
	return err;
}

int32_t nvdla_pm_set_lpwr_config(struct platform_device *pdev,
	struct dla_lpwr_config *config)
{
	int32_t err = 0;

	struct hfrp *hfrp;
	struct dla_lpwr_config old_config;

	if (pdev == NULL) {
		err = -EFAULT;
		goto fail;
	}

	hfrp = s_hfrp_get_by_pdev(pdev);
	if (hfrp == NULL) {
		err = -EINVAL;
		nvdla_dbg_err(pdev, "Invalid hfrp handle");
		goto fail;
	}

	if (config == NULL) {
		err = -EINVAL;
		nvdla_dbg_err(pdev, "Invalid lpwr configuration");
		goto fail;
	}

	/* back up prior to sending command */
	(void) memcpy(&old_config, hfrp->lpwr_config_va,
			sizeof(struct dla_lpwr_config));

	/* Copy the data to set prior to sending command */
	(void) memcpy(hfrp->lpwr_config_va, config,
			sizeof(struct dla_lpwr_config));

	/* make sure that device is powered on */
	err = nvdla_module_busy(pdev);
	if (err != 0) {
		nvdla_dbg_err(pdev, "failed to power on\n");
		err = -ENODEV;
		goto restore_config;
	}

	err = s_nvdla_pm_lpwr_config_reset(pdev);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to set lpwr config");
		goto module_idle;
	}

	nvdla_module_idle(pdev);

	return 0;

module_idle:
	nvdla_module_idle(pdev);
restore_config:
	(void) memcpy(hfrp->lpwr_config_va, &old_config,
			sizeof(struct dla_lpwr_config));
fail:
	return err;
}

int32_t nvdla_pm_get_lpwr_config(struct platform_device *pdev,
	struct dla_lpwr_config *config)
{
	int32_t err;

	struct hfrp *hfrp;

	if (pdev == NULL) {
		err = -EFAULT;
		goto fail;
	}

	hfrp = s_hfrp_get_by_pdev(pdev);
	if (hfrp == NULL) {
		err = -EINVAL;
		nvdla_dbg_err(pdev, "Invalid hfrp handle");
		goto fail;
	}

	if (config == NULL) {
		err = -EINVAL;
		nvdla_dbg_err(pdev, "Invalid lpwr configuration");
		goto fail;
	}

	/* If uninitialized, get the configuration from the firmware */
	if (hfrp->lpwr_config_va->version == 0U) {
		/* make sure that device is powered on */
		err = nvdla_module_busy(pdev);
		if (err != 0) {
			nvdla_dbg_err(pdev, "failed to power on\n");
			err = -ENODEV;
			goto fail;
		}

		err = s_nvdla_pm_lpwr_config_init(pdev);
		if (err < 0) {
			nvdla_dbg_err(pdev, "failed to init lpwr config");
			nvdla_module_idle(pdev);
			goto fail;
		}

		nvdla_module_idle(pdev);
	}

	(void) memcpy(config, hfrp->lpwr_config_va,
			sizeof(struct dla_lpwr_config));

	return 0;

fail:
	return err;
}

int32_t nvdla_pm_reset(struct platform_device *pdev)
{
	int32_t err;

	struct hfrp *hfrp;

	if (pdev == NULL) {
		err = -EFAULT;
		goto fail;
	}

	hfrp = s_hfrp_get_by_pdev(pdev);
	if (hfrp == NULL) {
		err = -EINVAL;
		nvdla_dbg_err(pdev, "Invalid hfrp handle");
		goto fail;
	}

	/* For the first time, fetch defaults from FW */
	if (hfrp->lpwr_config_va->version == 0U) {
		err = s_nvdla_pm_lpwr_config_init(pdev);
		if (err < 0) {
			nvdla_dbg_err(pdev, "failed to init lpwr config");
			nvdla_module_idle(pdev);
			goto fail;
		}
	}

	err = s_nvdla_pm_lpwr_config_reset(pdev);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to reset lpwr config");
		goto fail;
	}

	return 0;

fail:
	return err;
}

int32_t nvdla_pm_get_current_voltage(struct platform_device *pdev,
	uint32_t *voltage_mV)
{
	int32_t err;

	struct hfrp *hfrp;

	hfrp = s_hfrp_get_by_pdev(pdev);
	if (hfrp == NULL) {
		err = -EINVAL;
		nvdla_dbg_err(pdev, "failed to get hfrp\n");
		goto fail;
	}

	if (voltage_mV == NULL) {
		err = -EINVAL;
		nvdla_dbg_err(pdev, "Invalid input\n");
		goto fail;
	}

	err = nvdla_hfrp_send_cmd_get_current_voltage(hfrp, true);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to get voltage. err: %d\n", err);
		goto fail;
	}

	*voltage_mV = hfrp->voltage_mV;

	return 0;

fail:
	return err;
}

int32_t nvdla_pm_get_current_power_draw(struct platform_device *pdev,
	uint32_t *power_draw_mW)
{
	int32_t err;

	struct hfrp *hfrp;

	hfrp = s_hfrp_get_by_pdev(pdev);
	if (hfrp == NULL) {
		err = -EINVAL;
		nvdla_dbg_err(pdev, "failed to get hfrp\n");
		goto fail;
	}

	if (power_draw_mW == NULL) {
		err = -EINVAL;
		nvdla_dbg_err(pdev, "Invalid input\n");
		goto fail;
	}

	err = nvdla_hfrp_send_cmd_get_current_power_draw(hfrp, true);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to get power_draw. err: %d\n", err);
		goto fail;
	}

	*power_draw_mW = hfrp->power_draw_mW;

	return 0;

fail:
	return err;
}

int32_t nvdla_pm_get_info(struct platform_device *pdev,
	struct nvdla_pm_info *info)
{
	int32_t err;

	struct hfrp *hfrp;

	hfrp = s_hfrp_get_by_pdev(pdev);
	if (hfrp == NULL) {
		err = -EINVAL;
		nvdla_dbg_err(pdev, "failed to get hfrp\n");
		goto fail;
	}

	if (info == NULL) {
		err = -EINVAL;
		nvdla_dbg_err(pdev, "Invalid input\n");
		goto fail;
	}

	/* Fetch the information if not cached already */
	if (hfrp->info.num_vftable_entries == 0U) {
		err = nvdla_hfrp_send_cmd_get_vfcurve(hfrp, true);
		if (err < 0) {
			nvdla_dbg_err(pdev, "failed to get vfcurve. err: %d\n",
				err);
			goto fail;
		}
	}

	(void) memcpy(info, &hfrp->info, sizeof(struct nvdla_pm_info));

	return 0;

fail:
	return err;
}
