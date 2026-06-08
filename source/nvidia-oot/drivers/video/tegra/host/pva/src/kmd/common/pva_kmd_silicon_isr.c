// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_silicon_isr.h"
#include "pva_kmd_device.h"
#include "pva_fw_hyp.h"
#include "pva_kmd_msg.h"
#include "pva_kmd_abort.h"
#include "pva_kmd_limits.h"

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
	for (i = 1U; i < msg->len; i++) {
		msg->data[i] = pva_kmd_read_mailbox(
			pva, PVA_FW_MBOX_TO_HYP_BASE + i - 1U);
	}
}

void pva_kmd_hyp_isr(void *data, enum pva_kmd_intr_line intr_line)
{
	struct pva_kmd_device *pva = data;
	uint32_t intr_status;
	uint32_t wdt_val, hsp_val, h1x_val;

	(void)intr_line;

	intr_status = pva_kmd_read(pva, pva->regspec.sec_lic_intr_status);

	wdt_val = PVA_EXTRACT(intr_status, PVA_REG_SEC_LIC_INTR_WDT_MSB,
			      PVA_REG_SEC_LIC_INTR_WDT_LSB, uint32_t);
	hsp_val = PVA_EXTRACT(intr_status, PVA_REG_SEC_LIC_INTR_HSP_MSB,
			      PVA_REG_SEC_LIC_INTR_HSP_LSB, uint32_t);
	h1x_val = PVA_EXTRACT(intr_status, PVA_REG_SEC_LIC_INTR_H1X_MSB,
			      PVA_REG_SEC_LIC_INTR_H1X_LSB, uint32_t);

	if (wdt_val != 0U) {
		/* Clear interrupt status */
		pva_kmd_write(pva, pva->regspec.sec_lic_intr_status, wdt_val);
		pva_kmd_log_err("PVA watchdog timeout!");
		pva_kmd_abort_fw(pva, (enum pva_error)PVA_ERR_WDT_TIMEOUT);
	}

	if (h1x_val != 0U) {
		pva_kmd_log_err_u64("Host1x errors", h1x_val);
		/* Clear interrupt status */
		pva_kmd_write(pva, pva->regspec.sec_lic_intr_status, h1x_val);
		pva_kmd_abort_fw(pva, (enum pva_error)PVA_ERR_HOST1X_ERR);
	}

	if (hsp_val != 0U) {
		struct pva_fw_msg msg = { 0 };

		read_hyp_msg(pva, &msg);

		pva_kmd_handle_hyp_msg(pva, &msg.data[0], msg.len);

		msg.data[0] &= ~PVA_FW_MBOX_FULL_BIT;
		/* Clear interrupt bit in mailbox */
		pva_kmd_write_mailbox(pva, PVA_FW_MBOX_TO_HYP_LAST,
				      msg.data[0]);
	}
}

static uint32_t read_ccq_status(struct pva_kmd_device *pva, uint8_t ccq_id,
				uint8_t status_id)
{
	return pva_kmd_read(pva,
			    pva->regspec.ccq_regs[ccq_id].status[status_id]);
}

static void write_ccq_status(struct pva_kmd_device *pva, uint8_t ccq_id,
			     uint8_t status_id, uint32_t value)
{
	pva_kmd_write(pva, pva->regspec.ccq_regs[ccq_id].status[status_id],
		      value);
}

/* Handle interrupt from CCQ0 */
void pva_kmd_isr(void *data, enum pva_kmd_intr_line intr_line)
{
	struct pva_kmd_device *pva = data;
	uint32_t intr_status;
	uint8_t intr_interface;

	/* Convert interrupt line to interface index (CCQ0=1 -> interface=0, etc.) */
	if (intr_line >= PVA_KMD_INTR_LINE_CCQ0) {
		intr_interface = (uint8_t)((uint8_t)intr_line -
					   (uint8_t)PVA_KMD_INTR_LINE_CCQ0);
	} else {
		intr_interface = 0U; /* Fallback for invalid input */
	}

	intr_status = read_ccq_status(pva, intr_interface, 2) &
		      PVA_REG_CCQ_STATUS2_INTR_ALL_BITS;

	/* Clear interupt status This must be done prior to ack CCQ messages
	 * otherwise we risk losing CCQ messages.
	 */
	write_ccq_status(pva, intr_interface, 2, intr_status);

	if ((intr_status & PVA_REG_CCQ_STATUS2_INTR_STATUS8_BIT) != 0U) {
		pva_kmd_shared_buffer_process(pva, intr_interface);
	}
}

enum pva_error pva_kmd_bind_shared_buffer_handler(void *pva_dev,
						  uint8_t interface, void *data)
{
	struct pva_kmd_device *pva = (struct pva_kmd_device *)pva_dev;
	uint8_t base_line = (uint8_t)PVA_KMD_INTR_LINE_CCQ0;
	uint8_t target_line = safe_addu8(base_line, interface);
	return pva_kmd_bind_intr_handler(
		pva, (enum pva_kmd_intr_line)target_line, pva_kmd_isr, data);
}

void pva_kmd_release_shared_buffer_handler(void *pva_dev, uint8_t interface)
{
	struct pva_kmd_device *pva = (struct pva_kmd_device *)pva_dev;
	uint8_t base_line = (uint8_t)PVA_KMD_INTR_LINE_CCQ0;
	uint8_t target_line = safe_addu8(base_line, interface);
	pva_kmd_free_intr(pva, (enum pva_kmd_intr_line)target_line);
}
