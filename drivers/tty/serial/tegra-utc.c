// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
 */
#include <linux/acpi.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

/* Tegra UTC Registers */
#define TEGRA_UTC_ENABLE		0x0
#define TEGRA_UTC_FIFO_THRESHOLD	0x8
#define TEGRA_UTC_COMMAND		0xc
#define TEGRA_UTC_DATA			0x20
#define TEGRA_UTC_FIFO_STATUS		0x100
#define TEGRA_UTC_FIFO_OCCUPANCY	0x104
#define TEGRA_UTC_INTR_STATUS		0x108
#define TEGRA_UTC_INTR_SET		0x10c
#define TEGRA_UTC_INTR_MASK		0x110
#define TEGRA_UTC_INTR_CLEAR		0x114

#define TEGRA_UTC_FIFO_OVERRUN		BIT(3)
#define TEGRA_UTC_FIFO_FULL		BIT(1)
#define TEGRA_UTC_FIFO_EMPTY		BIT(0)
#define TEGRA_UTC_INTR_RX_TIMEOUT	BIT(4)
#define TEGRA_UTC_INTR_RX_OVERFLOW	BIT(3)
#define TEGRA_UTC_INTR_RX_REQ		BIT(2)
#define TEGRA_UTC_INTR_RX_FULL		BIT(1)
#define TEGRA_UTC_INTR_RX_EMPTY		BIT(0)
#define TEGRA_UTC_INTR_TX_REQ		BIT(2)
#define TEGRA_UTC_CMD_RST		BIT(0)

#define TEGRA_UTC_CLIENT_FIFO_SIZE	128

/* Maximum number of UART instances available. */
#define UART_NR			16

struct tegra_utc_port {
	struct uart_port	port;
#if IS_ENABLED(CONFIG_SERIAL_TEGRA_UTC_CONSOLE)
	struct console		console;
#endif
	void __iomem		*rx_base;
	void __iomem		*tx_base;

	unsigned int		tx_irqmask;
	unsigned int		rx_irqmask;

	unsigned int		baudrate;
	unsigned int		fifosize;
	int			irq;

	bool			tx_enabled;
	bool			rx_enabled;

	bool			polling_enabled;
	struct task_struct	*rx_thread;
	struct work_struct	rx_poll_stop;
};

static struct tegra_utc_port *utc_ports[UART_NR];

static int tegra_utc_rx_readl(struct tegra_utc_port *tup, unsigned int offset)
{
	void __iomem *addr = tup->rx_base + offset;

	return readl_relaxed(addr);
}

static void tegra_utc_rx_writel(unsigned int val, struct tegra_utc_port *tup, unsigned int offset)
{
	void __iomem *addr = tup->rx_base + offset;

	writel_relaxed(val, addr);
}

static int tegra_utc_tx_readl(struct tegra_utc_port *tup, unsigned int offset)
{
	void __iomem *addr = tup->tx_base + offset;

	return readl_relaxed(addr);
}

static void tegra_utc_tx_writel(unsigned int val, struct tegra_utc_port *tup, unsigned int offset)
{
	void __iomem *addr = tup->tx_base + offset;

	writel_relaxed(val, addr);
}

static void tegra_utc_stop_tx(struct uart_port *port)
{
	struct tegra_utc_port *tup = container_of(port, struct tegra_utc_port, port);

	tup->tx_irqmask = 0;
	tegra_utc_tx_writel(tup->tx_irqmask, tup, TEGRA_UTC_INTR_MASK);
	tegra_utc_tx_writel(tup->tx_irqmask, tup, TEGRA_UTC_INTR_SET);
}

static void tegra_utc_set_tx_irq(struct tegra_utc_port *tup, bool enable)
{
	tup->tx_irqmask = enable ? TEGRA_UTC_INTR_TX_REQ : 0x0;

	tegra_utc_tx_writel(tup->tx_irqmask, tup, TEGRA_UTC_INTR_MASK);
	tegra_utc_tx_writel(tup->tx_irqmask, tup, TEGRA_UTC_INTR_SET);
}

static void tegra_utc_reset_tx(struct tegra_utc_port *tup)
{
	tup->tx_irqmask = 0x0;

	tegra_utc_tx_writel(0x11, tup, TEGRA_UTC_COMMAND);
	tegra_utc_tx_writel(0x4, tup, TEGRA_UTC_FIFO_THRESHOLD);
	tegra_utc_tx_writel(0xf, tup, TEGRA_UTC_INTR_CLEAR);
	tegra_utc_tx_writel(tup->tx_irqmask, tup, TEGRA_UTC_INTR_MASK);
	tegra_utc_tx_writel(tup->tx_irqmask, tup, TEGRA_UTC_INTR_SET);

	tegra_utc_tx_writel(0x1, tup, TEGRA_UTC_ENABLE);
	tup->tx_enabled = true;
}

static void tegra_utc_reset_rx(struct tegra_utc_port *tup)
{
	tup->rx_irqmask = (TEGRA_UTC_INTR_RX_REQ | TEGRA_UTC_INTR_RX_TIMEOUT);

	tegra_utc_rx_writel(0x1, tup, TEGRA_UTC_COMMAND);
	tegra_utc_rx_writel(0x4, tup, TEGRA_UTC_FIFO_THRESHOLD);
	tegra_utc_rx_writel(0xf, tup, TEGRA_UTC_INTR_CLEAR);
	tegra_utc_rx_writel(tup->rx_irqmask, tup, TEGRA_UTC_INTR_MASK);
	tegra_utc_rx_writel(tup->rx_irqmask, tup, TEGRA_UTC_INTR_SET);

	tegra_utc_rx_writel(0x1, tup, TEGRA_UTC_ENABLE);
	tup->rx_enabled = true;
}

static bool tegra_utc_tx_char(struct tegra_utc_port *tup, unsigned char c)
{
	if (tegra_utc_tx_readl(tup, TEGRA_UTC_FIFO_STATUS) & TEGRA_UTC_FIFO_FULL)
		return false;

	tegra_utc_tx_writel(c, tup, TEGRA_UTC_DATA);

	return true;
}

static void tegra_utc_tx_char_poll(struct tegra_utc_port *tup, unsigned char c)
{
	while (tegra_utc_tx_readl(tup, TEGRA_UTC_FIFO_STATUS) & TEGRA_UTC_FIFO_FULL)
		cpu_relax();

	tegra_utc_tx_writel(c, tup, TEGRA_UTC_DATA);
}

static bool tegra_utc_tx_chars(struct tegra_utc_port *tup)
{
	struct circ_buf *xmit = &tup->port.state->xmit;

	if (tup->port.x_char) {
		if (tup->polling_enabled) {
			tegra_utc_tx_char_poll(tup, tup->port.x_char);
		} else {
			if (!tegra_utc_tx_char(tup, tup->port.x_char))
				return true;
		}

		tup->port.x_char = 0;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(&tup->port)) {
		tegra_utc_stop_tx(&tup->port);
		return false;
	}

	do {
		if (tup->polling_enabled) {
			tegra_utc_tx_char_poll(tup, xmit->buf[xmit->tail]);
		} else {
			if (!tegra_utc_tx_char(tup, xmit->buf[xmit->tail]))
				break;
		}

		uart_xmit_advance(&tup->port, 1);
	} while (!uart_circ_empty(xmit));

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&tup->port);

	if (uart_circ_empty(xmit)) {
		tegra_utc_stop_tx(&tup->port);
		return false;
	}

	return true;
}

static void tegra_utc_rx_chars(struct tegra_utc_port *tup)
{
	struct tty_port *port = &tup->port.state->port;
	unsigned int max_chars = 256;
	unsigned int status;
	unsigned int flag;
	unsigned char ch;
	int sysrq;

	while (max_chars--) {
		status = tegra_utc_rx_readl(tup, TEGRA_UTC_FIFO_STATUS);
		if (status & TEGRA_UTC_FIFO_EMPTY)
			break;

		ch = tegra_utc_rx_readl(tup, TEGRA_UTC_DATA);
		flag = TTY_NORMAL;
		tup->port.icount.rx++;

		if (status & TEGRA_UTC_FIFO_OVERRUN)
			tup->port.icount.overrun++;

		spin_unlock(&tup->port.lock);
		sysrq = uart_handle_sysrq_char(&tup->port, ch & 255);
		spin_lock(&tup->port.lock);

		if (!sysrq)
			tty_insert_flip_char(port, ch, flag);
	}

	tty_flip_buffer_push(port);
}

static irqreturn_t tegra_utc_isr(int irq, void *dev_id)
{
	struct tegra_utc_port *tup = dev_id;
	unsigned int tx_status, rx_status = 0;
	unsigned long flags;

	spin_lock_irqsave(&tup->port.lock, flags);

	/* Process RX_REQ and RX_TIMEOUT interrupts. */
	do {
		rx_status = tegra_utc_rx_readl(tup, TEGRA_UTC_INTR_STATUS) & tup->rx_irqmask;
		if (rx_status) {
			tegra_utc_rx_writel(tup->rx_irqmask, tup, TEGRA_UTC_INTR_CLEAR);
			tegra_utc_rx_chars(tup);
		}
	} while (rx_status);

	/* Process TX_REQ interrupt. */
	do {
		tx_status = tegra_utc_tx_readl(tup, TEGRA_UTC_INTR_STATUS) & tup->tx_irqmask;
		if (tx_status) {
			tegra_utc_tx_writel(tup->tx_irqmask, tup, TEGRA_UTC_INTR_CLEAR);
			tegra_utc_tx_chars(tup);
		}
	} while (tx_status);

	spin_unlock_irqrestore(&tup->port.lock, flags);

	return IRQ_HANDLED;
}

static unsigned int tegra_utc_tx_empty(struct uart_port *port)
{
	struct tegra_utc_port *tup = container_of(port, struct tegra_utc_port, port);

	return tegra_utc_tx_readl(tup, TEGRA_UTC_FIFO_OCCUPANCY) ? 0 : TIOCSER_TEMT;
}

static void tegra_utc_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}

static unsigned int tegra_utc_get_mctrl(struct uart_port *port)
{
	return 0;
}

static void tegra_utc_start_tx(struct uart_port *port)
{
	struct tegra_utc_port *tup = container_of(port, struct tegra_utc_port, port);

	if (tegra_utc_tx_chars(tup) && !tup->polling_enabled)
			tegra_utc_set_tx_irq(tup, true);
}

static void tegra_utc_stop_rx(struct uart_port *port)
{
	struct tegra_utc_port *tup = container_of(port, struct tegra_utc_port, port);

	tup->rx_irqmask = 0x0;
	tegra_utc_rx_writel(tup->rx_irqmask, tup, TEGRA_UTC_INTR_MASK);
	tegra_utc_rx_writel(tup->rx_irqmask, tup, TEGRA_UTC_INTR_SET);

	tup->rx_enabled = false;

	if (tup->polling_enabled)
		schedule_work(&tup->rx_poll_stop);
}

static void tegra_utc_hw_init(struct tegra_utc_port *tup)
{
	if (!tup->tx_enabled)
		tegra_utc_reset_tx(tup);

	if (!tup->rx_enabled)
		tegra_utc_reset_rx(tup);
}

static int tegra_utc_rx_poll_thread(void *data)
{
	struct tegra_utc_port *tup = data;
	unsigned int status;

	dev_dbg(tup->port.dev, "Tegra-UTC RX poll thread initialized\n");

	/* We are polling now disable interrupts. */
	tup->rx_irqmask = 0x0;
	tegra_utc_rx_writel(tup->rx_irqmask, tup, TEGRA_UTC_INTR_SET);

	/* Continously poll FIFO status and read data if available. */
	while (!kthread_should_stop()) {
		status = tegra_utc_rx_readl(tup, TEGRA_UTC_FIFO_STATUS);

		/* Wait for one character time and continue. */
		if (status & TEGRA_UTC_FIFO_EMPTY) {
			msleep(5);
			continue;
		}
		spin_lock(&tup->port.lock);
		tegra_utc_rx_chars(tup);
		spin_unlock(&tup->port.lock);
	}

	return 0;
}

static void tegra_utc_stop_rx_poll_thread(struct work_struct *work)
{
	struct tegra_utc_port *tup = container_of(work, struct tegra_utc_port, rx_poll_stop);

	if (tup->rx_thread != NULL) {
		kthread_stop(tup->rx_thread);
		tup->rx_thread = NULL;
	}
}

static int tegra_utc_startup(struct uart_port *port)
{
	struct tegra_utc_port *tup = container_of(port, struct tegra_utc_port, port);

	tegra_utc_hw_init(tup);

	if (tup->polling_enabled) {
		struct task_struct *rx_thread;

		rx_thread = kthread_create(tegra_utc_rx_poll_thread, (void *) tup,
						"tegra_utc_rx_poll_thread");
		if (IS_ERR(rx_thread)) {
			dev_err(port->dev, "unable to start rx poll thread\n");
			return PTR_ERR(rx_thread);
		}

		tup->rx_thread = rx_thread;
		wake_up_process(rx_thread);
	} else {
		int ret;

		ret = request_irq(tup->irq, tegra_utc_isr, 0, dev_name(port->dev), tup);
		if (ret < 0) {
			dev_err(port->dev, "failed to register interrupt handler\n");
			return ret;
		}
	}

	return 0;
}

static void tegra_utc_shutdown(struct uart_port *port)
{
	struct tegra_utc_port *tup = container_of(port, struct tegra_utc_port, port);

	tegra_utc_rx_writel(0x0, tup, TEGRA_UTC_ENABLE);
	tup->rx_enabled = false;

	if (!tup->polling_enabled)
		free_irq(tup->irq, tup);
}

static void tegra_utc_set_termios(struct uart_port *port, struct ktermios *termios,
				  const struct ktermios *old)
{
	struct tegra_utc_port *tup = container_of(port, struct tegra_utc_port, port);
	unsigned long flags;

	tty_termios_encode_baud_rate(termios, tup->baudrate, tup->baudrate);
	termios->c_cflag &= ~(CSIZE | CSTOPB | PARENB | PARODD);
	termios->c_cflag &= ~(CMSPAR | CRTSCTS);
	termios->c_cflag |= CS8 | CLOCAL;

	spin_lock_irqsave(&port->lock, flags);
	uart_update_timeout(port, CS8, tup->baudrate);
	spin_unlock_irqrestore(&port->lock, flags);
}

#ifdef CONFIG_CONSOLE_POLL

static int tegra_utc_poll_init(struct uart_port *port)
{
	struct tegra_utc_port *tup = container_of(port, struct tegra_utc_port, port);

	tegra_utc_hw_init(tup);
	return 0;
}

static int tegra_utc_get_poll_char(struct uart_port *port)
{
	struct tegra_utc_port *tup = container_of(port, struct tegra_utc_port, port);

	while (tegra_utc_rx_readl(tup, TEGRA_UTC_FIFO_STATUS) & TEGRA_UTC_FIFO_EMPTY)
		cpu_relax();

	return tegra_utc_rx_readl(tup, TEGRA_UTC_DATA);
}

static void tegra_utc_put_poll_char(struct uart_port *port, unsigned char ch)
{
	struct tegra_utc_port *tup = container_of(port, struct tegra_utc_port, port);

	while (tegra_utc_tx_readl(tup, TEGRA_UTC_FIFO_STATUS) & TEGRA_UTC_FIFO_FULL)
		cpu_relax();

	tegra_utc_tx_writel(ch, tup, TEGRA_UTC_DATA);
}

#endif

static const struct uart_ops tegra_utc_uart_ops = {
	.tx_empty = tegra_utc_tx_empty,
	.set_mctrl = tegra_utc_set_mctrl,
	.get_mctrl = tegra_utc_get_mctrl,
	.stop_tx = tegra_utc_stop_tx,
	.start_tx = tegra_utc_start_tx,
	.stop_rx = tegra_utc_stop_rx,
	.startup = tegra_utc_startup,
	.shutdown = tegra_utc_shutdown,
	.set_termios = tegra_utc_set_termios,
#ifdef CONFIG_CONSOLE_POLL
	.poll_init = tegra_utc_poll_init,
	.poll_get_char = tegra_utc_get_poll_char,
	.poll_put_char = tegra_utc_put_poll_char,
#endif
};

static int tegra_utc_find_free_port(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(utc_ports); i++)
		if (utc_ports[i] == NULL)
			return i;
	return -EBUSY;
}

static void tegra_utc_setup_port(struct device *dev, struct tegra_utc_port *tup,
				 int index)
{
	tup->port.dev		= dev;
	tup->port.fifosize	= tup->fifosize;
	tup->port.flags		= UPF_BOOT_AUTOCONF;
	tup->port.iotype	= UPIO_MEM;
	tup->port.line		= index;
	tup->port.ops		= &tegra_utc_uart_ops;
	tup->port.type		= PORT_TEGRA_TCU;
	tup->port.private_data	= tup;
	utc_ports[index]	= tup;
}

#if IS_ENABLED(CONFIG_SERIAL_TEGRA_UTC_CONSOLE)
static void tegra_utc_putc(struct uart_port *port, unsigned char c)
{
	writel(c, port->membase + TEGRA_UTC_DATA);
}

static void tegra_utc_early_write(struct console *con, const char *s, unsigned int n)
{
	struct earlycon_device *dev = con->data;

	while (n) {
		unsigned int burst_size = TEGRA_UTC_CLIENT_FIFO_SIZE;

		burst_size -= readl(dev->port.membase + TEGRA_UTC_FIFO_OCCUPANCY);
		if (n < burst_size)
			burst_size = n;

		uart_console_write(&dev->port, s, burst_size, tegra_utc_putc);

		n -= burst_size;
		s += burst_size;
	}
}

static int __init tegra_utc_early_console_setup(struct earlycon_device *device, const char *opt)
{
	if (!device->port.membase)
		return -ENODEV;

	writel(0x11, device->port.membase + TEGRA_UTC_COMMAND);
	writel(0x4, device->port.membase + TEGRA_UTC_FIFO_THRESHOLD);
	writel(0xf, device->port.membase + TEGRA_UTC_INTR_CLEAR);
	writel(0x0, device->port.membase + TEGRA_UTC_INTR_MASK);
	writel(0x0, device->port.membase + TEGRA_UTC_INTR_SET);
	writel(0x1, device->port.membase + TEGRA_UTC_ENABLE);

	device->con->write = tegra_utc_early_write;

	return 0;
}
OF_EARLYCON_DECLARE(tegra_utc, "nvidia,tegra-utc", tegra_utc_early_console_setup);

static void tegra_utc_console_putchar(struct uart_port *port, unsigned char ch)
{
	struct tegra_utc_port *tup = container_of(port, struct tegra_utc_port, port);

	tegra_utc_tx_writel(ch, tup, TEGRA_UTC_DATA);
}

static void tegra_utc_console_write(struct console *cons, const char *s, unsigned int count)
{
	struct tegra_utc_port *tup = utc_ports[cons->index];
	unsigned long flags;
	int locked = 1;

	if (tup->port.sysrq || oops_in_progress)
		locked = spin_trylock_irqsave(&tup->port.lock, flags);
	else
		spin_lock_irqsave(&tup->port.lock, flags);

	while (count) {
		unsigned int burst_size = TEGRA_UTC_CLIENT_FIFO_SIZE;

		burst_size -= tegra_utc_tx_readl(tup, TEGRA_UTC_FIFO_OCCUPANCY);
		if (count < burst_size)
			burst_size = count;

		uart_console_write(&tup->port, s, burst_size, tegra_utc_console_putchar);

		count -= burst_size;
		s += burst_size;
	};

	if (locked)
		spin_unlock_irqrestore(&tup->port.lock, flags);
}

static int tegra_utc_console_setup(struct console *cons, char *options)
{
	struct tegra_utc_port *tup = utc_ports[cons->index];

	tegra_utc_reset_tx(tup);

	return 0;
}

static struct uart_driver tegra_utc_driver;
static struct console tegra_utc_console = {
	.name = "ttyUTC",
	.write = tegra_utc_console_write,
	.device = uart_console_device,
	.setup = tegra_utc_console_setup,
	.flags = CON_PRINTBUFFER | CON_CONSDEV | CON_ANYTIME,
	.index = -1,
	.data = &tegra_utc_driver,
};
#define TEGRA_UTC_CONSOLE	(&tegra_utc_console)
#else
#define TEGRA_UTC_CONSOLE	NULL
#endif
static struct uart_driver tegra_utc_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= "tegra-utc",
	.dev_name	= "ttyUTC",
	.nr		= UART_NR,
	.cons		= TEGRA_UTC_CONSOLE,
};

static int tegra_utc_register_port(struct tegra_utc_port *tup)
{
	int ret;

	ret = uart_add_one_port(&tegra_utc_driver, &tup->port);
	if (ret) {
		utc_ports[tup->port.line] = NULL;
	} else {
		if (!(tup->console.flags & CON_ENABLED))
			register_console(&tup->console);
	}

	return ret;
}

static void tegra_utc_unregister_port(struct tegra_utc_port *tup)
{
	unsigned int i;
	bool busy = false;

	for (i = 0; i < ARRAY_SIZE(utc_ports); i++) {
		if (utc_ports[i] == tup)
			utc_ports[i] = NULL;
		else if (utc_ports[i])
			busy = true;
	}

	if (!busy)
		uart_unregister_driver(&tegra_utc_driver);
}

static int tegra_utc_probe(struct platform_device *pdev)
{
	struct tegra_utc_port *tup;
	struct resource *mmiobase;
	int baudrate;
	int portnr;
	int ret;
	int i;

	portnr = tegra_utc_find_free_port();
	if (portnr < 0)
		return portnr;

	tup = devm_kzalloc(&pdev->dev, sizeof(struct tegra_utc_port), GFP_KERNEL);
	if (!tup)
		return -ENOMEM;

	if (pdev->dev.of_node) {
		struct device_node *np = pdev->dev.of_node;

		ret = of_property_read_u32(np, "current-speed", &baudrate);
		if (ret)
			return ret;

		if (of_property_read_bool(np, "nvidia,utc-polling-enabled")) {
			tup->polling_enabled = true;
			INIT_WORK(&tup->rx_poll_stop, tegra_utc_stop_rx_poll_thread);
		}
	} else {
		baudrate = 115200;
	}

	tup->irq = platform_get_irq(pdev, i);
	if (tup->irq < 0) {
		dev_err(&pdev->dev, "failed to get interrupts");
		return tup->irq;
	}

	tup->fifosize = 32;
	tup->baudrate = baudrate;

	mmiobase = platform_get_resource_byname(pdev, IORESOURCE_MEM, "rx");
	tup->rx_base = devm_ioremap_resource(&pdev->dev, mmiobase);
	if (IS_ERR(tup->rx_base))
		return PTR_ERR(tup->rx_base);

	mmiobase = platform_get_resource_byname(pdev, IORESOURCE_MEM, "tx");
	tup->tx_base = devm_ioremap_resource(&pdev->dev, mmiobase);
	if (IS_ERR(tup->tx_base))
		return PTR_ERR(tup->tx_base);

	tegra_utc_setup_port(&pdev->dev, tup, portnr);
	platform_set_drvdata(pdev, tup);

	strcpy(tup->console.name, "ttyUTC");
	tup->console.write = tegra_utc_console_write;
	tup->console.device = uart_console_device;
	tup->console.setup = tegra_utc_console_setup;
	tup->console.flags = CON_PRINTBUFFER | CON_CONSDEV | CON_ANYTIME;
	tup->console.index = portnr;
	tup->console.data = &tegra_utc_driver;

	return tegra_utc_register_port(tup);
}
static int tegra_utc_remove(struct platform_device *pdev)
{
	struct tegra_utc_port *tup = platform_get_drvdata(pdev);

	uart_remove_one_port(&tegra_utc_driver, &tup->port);
	tegra_utc_unregister_port(tup);

	return 0;
}
static const struct of_device_id tegra_utc_of_match[] = {
	{ .compatible = "nvidia,tegra264-utc" },
	{},
};
MODULE_DEVICE_TABLE(of, tegra_utc_of_match);

static struct platform_driver tegra_utc_platform_driver = {
	.probe = tegra_utc_probe,
	.remove = tegra_utc_remove,
	.driver = {
		.name = "tegra-utc",
		.of_match_table = of_match_ptr(tegra_utc_of_match),
	},
};

static int __init tegra_utc_init(void)
{
	int ret;

	ret = uart_register_driver(&tegra_utc_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&tegra_utc_platform_driver);
	if (ret) {
		uart_unregister_driver(&tegra_utc_driver);
		return ret;
	}

	return 0;
}

static void __exit tegra_utc_exit(void)
{
	uart_unregister_driver(&tegra_utc_driver);
	platform_driver_unregister(&tegra_utc_platform_driver);
}

device_initcall(tegra_utc_init);
module_exit(tegra_utc_exit);
MODULE_AUTHOR("Kartik <kkartik@nvidia.com>");
MODULE_DESCRIPTION("Tegra UART Trace Controller");
MODULE_LICENSE("GPL");
