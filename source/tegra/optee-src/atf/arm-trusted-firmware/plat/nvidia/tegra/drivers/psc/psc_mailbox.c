/*
 * Copyright (c) 2021, NVIDIA Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <assert.h>
#include <common/debug.h>
#include <drivers/delay_timer.h>
#include <lib/mmio.h>
#include <psc_mailbox.h>
#include <tegra_def.h>

/*
 * This function writes SC7 entry opcode to notify PSC from CCPLEX.
 * The protocol is to write the command to the MBOX and then trigger
 * a doorbell.
 */
void tegra_psc_notify_sc7_entry(void)
{
	INFO("%s: send SC7 opcode to PSC\n", __func__);
	mmio_write_32((TEGRA_PSC_MBOX_TZ_BASE + PSC_MBOX_TZ_IN),
		 PSC_MBOX_TZ_OPCODE_ENTER_SC7);

	mmio_write_32((TEGRA_PSC_MBOX_TZ_BASE + PSC_MBOX_TZ_EXT_CTRL),
		 (PSC_MBOX_OUT_DONE | PSC_MBOX_IN_VALID));
}

/*
 * This funciton read back the ACKs from PSC mailbox and verify their values.
 * The PSC mailbox should return the OPCODE in out0 and return 0 in out1 for
 * PSC operation done.
 */
void tegra_psc_wait_for_ack(void)
{
	uint32_t timeout_count = PSC_MBOX_ACK_TIMEOUT_MAX_USEC;
	uint32_t val, out_valid;

	do {
		if (timeout_count-- == 0U) {
			ERROR("!! %s: PSC handshake fail !!", __func__);
			panic();
		}

		udelay(1U);

		out_valid = mmio_read_32(TEGRA_PSC_MBOX_TZ_BASE +
			PSC_MBOX_TZ_PSC_CTRL);

	} while (PSC_MBOX_OUT_VALID != (out_valid & PSC_MBOX_OUT_VALID));

	INFO("%s: PSC handshake done\n", __func__);

	/* Get first ACK from PSC_MBOX_TZ_OUT_0 */
	val = mmio_read_32(TEGRA_PSC_MBOX_TZ_BASE + PSC_MBOX_TZ_OUT_0);
	assert(val == PSC_MBOX_TZ_OPCODE_ENTER_SC7);

	/* Get second ACK from PSC_MBOX_TZ_OUT_1 */
	val = mmio_read_32(TEGRA_PSC_MBOX_TZ_BASE + PSC_MBOX_TZ_OUT_1);
	assert(val == PSC_MBOX_SC7_ENTRY_PASS);

	/* Trigger MBOX_OUT_DONE, notify PSC handshake finish */
	mmio_write_32((TEGRA_PSC_MBOX_TZ_BASE + PSC_MBOX_TZ_EXT_CTRL),
			 PSC_MBOX_OUT_DONE);
}

