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

#include "pva_kmd_silicon_isr.h"
#include "pva_kmd_device.h"
#include "pva_fw_hyp.h"
#include "pva_kmd_msg.h"

struct pva_fw_msg {
	uint8_t len;
	uint32_t data[PVA_FW_MSG_MAX_LEN];
};

static void read_hyp_msg(struct pva_kmd_device *pva, struct pva_fw_msg *msg)
{
	uint32_t i;

	msg->data[0] = pva_kmd_read_mailbox(pva, PVA_FW_MBOX_TO_HYP_LAST);
	msg->len = PVA_EXTRACT(msg->data[0], PVA_FW_MSG_LEN_MSB,
			       PVA_FW_MSG_LEN_LSB, uint8_t);
	ASSERT(msg->len <= PVA_ARRAY_SIZE(msg->data));
	for (i = 1; i < msg->len; i++) {
		msg->data[i] = pva_kmd_read_mailbox(
			pva, PVA_FW_MBOX_TO_HYP_BASE + i - 1);
	}
}

void pva_kmd_hyp_isr(void *data)
{
	struct pva_kmd_device *pva = data;
	uint32_t intr_status;
	uint32_t wdt_val, hsp_val, h1x_val;

	intr_status = pva_kmd_read(pva, pva->regspec.sec_lic_intr_status);

	wdt_val = PVA_EXTRACT(intr_status, PVA_REG_SEC_LIC_INTR_WDT_MSB,
			      PVA_REG_SEC_LIC_INTR_WDT_LSB, uint32_t);
	hsp_val = PVA_EXTRACT(intr_status, PVA_REG_SEC_LIC_INTR_HSP_MSB,
			      PVA_REG_SEC_LIC_INTR_HSP_LSB, uint32_t);
	h1x_val = PVA_EXTRACT(intr_status, PVA_REG_SEC_LIC_INTR_H1X_MSB,
			      PVA_REG_SEC_LIC_INTR_H1X_LSB, uint32_t);

	if (wdt_val != 0) {
		/* Clear interrupt status */
		pva_kmd_write(pva, pva->regspec.sec_lic_intr_status,
			      intr_status &
				      PVA_MASK(PVA_REG_SEC_LIC_INTR_WDT_MSB,
					       PVA_REG_SEC_LIC_INTR_WDT_LSB));
		/* TODO: reboot firmware when we can */
		FAULT("PVA watchdog timeout!");
	}

	if (h1x_val != 0) {
		pva_kmd_log_err_u64("Host1x errors", h1x_val);
		/* Clear interrupt status */
		pva_kmd_write(pva, pva->regspec.sec_lic_intr_status,
			      intr_status &
				      PVA_MASK(PVA_REG_SEC_LIC_INTR_H1X_MSB,
					       PVA_REG_SEC_LIC_INTR_H1X_LSB));
	}

	if (hsp_val != 0) {
		struct pva_fw_msg msg = { 0 };

		read_hyp_msg(pva, &msg);

		pva_kmd_handle_hyp_msg(pva, &msg.data[0], msg.len);

		msg.data[0] &= ~PVA_FW_MBOX_FULL_BIT;
		/* Clear interrupt bit in mailbox */
		pva_kmd_write_mailbox(pva, PVA_FW_MBOX_TO_HYP_LAST,
				      msg.data[0]);
	}
}

static uint32_t read_ccq0_status(struct pva_kmd_device *pva, uint8_t status_id)
{
	return pva_kmd_read(pva, pva->regspec.ccq_regs[0].status[status_id]);
}

static void write_ccq0_status(struct pva_kmd_device *pva, uint8_t status_id,
			      uint32_t value)
{
	pva_kmd_write(pva, pva->regspec.ccq_regs[0].status[status_id], value);
}

static void read_ccq_msg(struct pva_kmd_device *pva, struct pva_fw_msg *msg)
{
	uint32_t i;

	msg->data[0] = read_ccq0_status(pva, PVA_FW_MSG_STATUS_LAST);
	msg->len = PVA_EXTRACT(msg->data[0], PVA_FW_MSG_LEN_MSB,
			       PVA_FW_MSG_LEN_LSB, uint8_t);
	ASSERT(msg->len <= PVA_ARRAY_SIZE(msg->data));
	for (i = 1; i < msg->len; i++) {
		msg->data[i] =
			read_ccq0_status(pva, PVA_FW_MSG_STATUS_BASE + i - 1);
	}
}

/* Handle interrupt from CCQ0 */
void pva_kmd_isr(void *data)
{
	struct pva_kmd_device *pva = data;
	uint32_t intr_status;

	intr_status =
		read_ccq0_status(pva, 2) & PVA_REG_CCQ_STATUS2_INTR_ALL_BITS;
	pva_dbg_printf("CCQ0_INTR_STATUS 0x%x\n", intr_status);
	/* Clear interupt status This must be done prior to ack CCQ messages
	 * otherwise we risk losing CCQ messages.
	 */
	write_ccq0_status(pva, 2, intr_status);

	if (intr_status & PVA_REG_CCQ_STATUS2_INTR_STATUS8_BIT) {
		struct pva_fw_msg msg;

		read_ccq_msg(pva, &msg);

		pva_kmd_handle_msg(pva, &msg.data[0], msg.len);

		/* Ack through status1 write. */
		write_ccq0_status(pva, 1, 0 /* Value doesn't matter for now */);
	}

	/* We don't care about Status7 or CCQ overflow interrupt */
}
