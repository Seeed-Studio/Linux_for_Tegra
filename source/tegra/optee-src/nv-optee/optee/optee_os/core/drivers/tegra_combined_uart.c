/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
 */

#include <io.h>
#include <kernel/delay.h>
#include <drivers/tegra_combined_uart.h>

/* 50 ms */
#define TX_TIMEOUT		50

/*
 * Triggers an interrupt. Also indicates that the remote processor
 * is busy when set.
 */
#define MBOX_INTR_TRIGGER	(1 << 31)
/*
 * Ensures that prints up to and including this packet are flushed on
 * the physical uart before de-asserting MBOX_INTR_TRIGGER.
 */
#define MBOX_FLUSH		(1 << 26)
/*
 * Indicates that we're only sending one byte at a time.
 */
#define MBOX_BYTE_COUNT		(1 << 24)

static vaddr_t chip_to_base(struct serial_chip *chip)
{
	struct tegra_combined_uart_data *tcud =
		container_of(chip, struct tegra_combined_uart_data, chip);

	return io_pa_or_va(&tcud->base, tcud->base_size);
}

static void send_msg(vaddr_t base, uint32_t msg)
{
	int timeout = TX_TIMEOUT;

	while (io_read32(base) & (MBOX_INTR_TRIGGER)) {
		if (timeout-- <= 0)
			return;
		udelay(300);
	}
	io_write32(base, msg);
}

static void comb_uart_putc(struct serial_chip *chip, int c)
{
	uint32_t msg;
	vaddr_t base = chip_to_base(chip);

	(void)chip;
	if (c == '\0')
		return;
	msg = MBOX_INTR_TRIGGER | MBOX_BYTE_COUNT | (uint8_t)(c & 0xff);
	send_msg(base, msg);
}


static void comb_uart_flush(struct serial_chip *chip)
{
	uint32_t msg = MBOX_INTR_TRIGGER | MBOX_FLUSH;
	vaddr_t base = chip_to_base(chip);

	send_msg(base, msg);
}

static int comb_uart_getc(struct serial_chip *chip)
{
	(void)chip;
	return -1;
}

static bool comb_uart_have_rx_data(struct serial_chip *chip)
{
	(void)chip;
	return false;
}

static const struct serial_ops comb_uart_ops = {
	.flush = comb_uart_flush,
	.getchar = comb_uart_getc,
	.have_rx_data = comb_uart_have_rx_data,
	.putc = comb_uart_putc
};
DECLARE_KEEP_PAGER(comb_uart_ops);

void tegra_combined_uart_init(struct tegra_combined_uart_data *tcud,
                            paddr_t base, size_t size)
{
	if (!tcud)
		return;
	tcud->base.pa = base;
	tcud->base_size= size;
	tcud->chip.ops = &comb_uart_ops;
}
