// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2020-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/mailbox_client.h>

/* from drivers/mailbox/mailbox.h */
#define TXDONE_BY_POLL	BIT(1) /* controller can read status of last TX */
#define TXDONE_BY_ACK	BIT(2) /* S/W ACK received by Client ticks the TX */

#include "tegra23x_psc.h"

#define MBOX_REG_OFFSET	0x10000
/* 16 32-bit registers for MBOX_CHAN_IN/OUT */
#define MBOX_MSG_SIZE	16

#define MBOX_CHAN_ID	0x0

#define MBOX_CHAN_EXT_CTRL	0x4
#define MBOX_CHAN_PSC_CTRL	0x8
/* bit to indicate remote that IN parameters are ready. */
#define MBOX_IN_VALID	BIT(0)
/* bit to indicate remote that OUT parameters are read out */
#define MBOX_OUT_DONE	BIT(4)
#define LIC_INTR_EN	BIT(8)
#define MBOX_OUT_VALID	BIT(0)

#define MBOX_CHAN_TX	0x800
#define MBOX_CHAN_RX	0x1000

#ifdef PSC_HAVE_NUMA

struct io_data {
	u32 value;
	void __iomem *addr;
};

static const struct cpumask *node0mask;

static void write_callback(void *param)
{
	struct io_data *data = param;

	writel(data->value, data->addr);
}

void writel0(u32 value, void __iomem *addr)
{
	struct io_data d = {
		.value = value, .addr = addr
	};

	if (cpumask_test_cpu(smp_processor_id(), node0mask))
		writel(value, addr); /* optimization: direct call */
	else
		smp_call_function_any(node0mask, write_callback, &d, 1);
}

static void read_callback(void *param)
{
	struct io_data *data = param;

	data->value = readl(data->addr);
}

u32 readl0(void __iomem *addr)
{
	struct io_data d = {
		.addr = addr
	};

	if (cpumask_test_cpu(smp_processor_id(), node0mask))
		return readl(addr);	/* optimization: direct call */

	smp_call_function_any(node0mask, read_callback, &d, 1);
	return d.value;
}
#endif // PSC_HAVE_NUMA

static irqreturn_t psc_mbox_rx_interrupt(int irq, void *p)
{
	u32 data[MBOX_MSG_SIZE];
	struct mbox_chan *chan = p;
	struct mbox_vm_chan *vm_chan = chan->con_priv;
	struct device *dev = vm_chan->parent->dev;
	u32 ext_ctrl;
	u32 psc_ctrl;
	int i;

	psc_ctrl = readl(vm_chan->base + MBOX_CHAN_PSC_CTRL);
	/* not a valid case but it does happen. */
	if ((psc_ctrl & MBOX_OUT_VALID) == 0) {
		ext_ctrl = readl(vm_chan->base + MBOX_CHAN_EXT_CTRL);
		dev_err_once(dev, "invalid interrupt, psc_ctrl: 0x%08x ext_ctrl: 0x%08x\n",
			psc_ctrl, ext_ctrl);
		return IRQ_HANDLED;
	}

	for (i = 0; i < MBOX_MSG_SIZE; i++)
		data[i] = readl(vm_chan->base + MBOX_CHAN_RX + i * 4);

	mbox_chan_received_data(chan, data);
	/* finish read */
	ext_ctrl = readl(vm_chan->base + MBOX_CHAN_EXT_CTRL);
	ext_ctrl |= MBOX_OUT_DONE;
	writel(ext_ctrl, vm_chan->base + MBOX_CHAN_EXT_CTRL);
	/* Bug 4741744: extra read to clear spurous interrupt */
	ext_ctrl = readl(vm_chan->base + MBOX_CHAN_EXT_CTRL);

	return IRQ_HANDLED;
}

static int psc_mbox_send_data(struct mbox_chan *chan, void *data)
{
	struct mbox_vm_chan  *vm_chan = chan->con_priv;
	struct device *dev = vm_chan->parent->dev;
	u32 *buf = data;
	int i;
	u32 ext_ctrl;

	ext_ctrl = readl0(vm_chan->base + MBOX_CHAN_EXT_CTRL);

	if ((ext_ctrl & MBOX_IN_VALID) != 0) {
		dev_err(dev, "%s:pending write.\n", __func__);
		return -EBUSY;
	}
	for (i = 0; i < MBOX_MSG_SIZE; i++)
		writel0(buf[i], vm_chan->base + MBOX_CHAN_TX + i * 4);

	ext_ctrl |= MBOX_IN_VALID;
	writel0(ext_ctrl, vm_chan->base + MBOX_CHAN_EXT_CTRL);
	return 0;
}

static int psc_mbox_startup(struct mbox_chan *chan)
{
	struct mbox_vm_chan  *vm_chan = chan->con_priv;
	u32 ext_ctrl = LIC_INTR_EN;

	writel0(ext_ctrl, vm_chan->base + MBOX_CHAN_EXT_CTRL);
	chan->txdone_method = TXDONE_BY_ACK;
	return 0;
}

static void psc_mbox_shutdown(struct mbox_chan *chan)
{
	struct mbox_vm_chan  *vm_chan;
	struct device *dev;

	if (chan == NULL) {
		pr_err("%s: chan == NULL, exiting\n", __func__);
		return;
	}

	vm_chan = chan->con_priv;
	dev = vm_chan->parent->dev;

	dev_dbg(dev, "%s\n", __func__);
	writel0(0, vm_chan->base + MBOX_CHAN_EXT_CTRL);
}

static const struct mbox_chan_ops psc_mbox_ops = {
	.send_data = psc_mbox_send_data,
	.startup = psc_mbox_startup,
	.shutdown = psc_mbox_shutdown,
};

static int tegra234_psc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct psc_mbox *psc;
	void __iomem *base;
	int i;
	int ret;

	dev_dbg(dev, "psc driver init\n");

#ifdef PSC_HAVE_NUMA
	node0mask = cpumask_of_node(0);
#endif
	psc = devm_kzalloc(dev, sizeof(*psc), GFP_KERNEL);
	if (!psc)
		return -ENOMEM;

	// first mailbox address (name mbox-regs)
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base)) {
		dev_err(dev, "ioremap failed\n");
		return PTR_ERR(base);
	}

	psc->vm_chan_base = base;
	psc->dev = dev;

	for (i = 0; i < MBOX_NUM; i++) {
		int irq;

		irq = platform_get_irq(pdev, i);
		if (irq < 0) {
			dev_err(dev, "Unable to get IRQ %d\n", irq);
			return irq;
		}
		ret = devm_request_irq(dev, irq, psc_mbox_rx_interrupt,
				IRQF_ONESHOT, dev_name(dev), &psc->chan[i]);
		if (ret) {
			dev_err(dev, "Unable to acquire IRQ %d\n", irq);
			return ret;
		}

#ifdef PSC_HAVE_NUMA
		irq_set_affinity(irq, node0mask);
#endif

		psc->chan[i].con_priv = &psc->vm_chan[i];
		psc->vm_chan[i].parent = psc;
		psc->vm_chan[i].irq = irq;
		psc->vm_chan[i].base = base + (MBOX_REG_OFFSET * i);
		dev_dbg(dev, "vm_chan[%d].base:%p, irq:%d\n",
			i, psc->vm_chan[i].base, irq);
	}
	psc->mbox.dev = dev;
	psc->mbox.chans = &psc->chan[0];	/* mbox_request_channel(cl,0) returns this one */
	psc->mbox.num_chans = MBOX_NUM;
	psc->mbox.ops = &psc_mbox_ops;
	/* drive txdone by mailbox client ACK with tx_block set to false */
	psc->mbox.txdone_irq = false;
	psc->mbox.txdone_poll = false;

	platform_set_drvdata(pdev, psc);

	/* adds to global kernel mbox_cons */
	ret = mbox_controller_register(&psc->mbox);
	if (ret) {
		dev_err(dev, "Failed to register mailboxes %d\n", ret);
		return ret;
	}

	psc_debugfs_create(pdev, &psc->mbox);
	dev_info(dev, "init done\n");

	return 0;
}

static int tegra234_psc_remove(struct platform_device *pdev)
{
	struct psc_mbox *psc = platform_get_drvdata(pdev);

	psc_debugfs_remove(pdev);

	mbox_controller_unregister(&psc->mbox);

	return 0;
}

static const struct of_device_id tegra234_psc_match[] = {
	{ .compatible = "nvidia,tegra234-psc", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, tegra234_psc_match);

#ifdef CONFIG_ACPI
static const struct acpi_device_id tegra23x_psc_acpi_match[] = {
	{
		.id = "NVDA2003",
		.driver_data = 0
	},
	{}
};
MODULE_DEVICE_TABLE(acpi, tegra23x_psc_acpi_match);
#endif

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void tegra234_psc_remove_wrapper(struct platform_device *pdev)
{
	tegra234_psc_remove(pdev);
}
#else
static int tegra234_psc_remove_wrapper(struct platform_device *pdev)
{
	return tegra234_psc_remove(pdev);
}
#endif

static struct platform_driver tegra234_psc_driver = {
	.probe          = tegra234_psc_probe,
	.remove		= tegra234_psc_remove_wrapper,
	.driver = {
		.owner  = THIS_MODULE,
		.name   = "tegra23x-psc",
		.of_match_table = of_match_ptr(tegra234_psc_match),
#ifdef CONFIG_ACPI
		.acpi_match_table = ACPI_PTR(tegra23x_psc_acpi_match),
#endif
	},
};

struct mbox_chan *psc_mbox_request_channel0(struct mbox_controller *mbox, struct mbox_client *cl)
{
	struct device *dev = cl->dev;
	struct mbox_chan *chan;
	unsigned long flags;
	int ret;

	if (!dev) {
		pr_err("%s: device is NULL\n", __func__);
		return ERR_PTR(-ENODEV);
	}

	chan = &mbox->chans[0];
	if (IS_ERR(chan)) {
		dev_err(dev, "%s: channel [0] has an error\n", __func__);
		return chan;
	}

	if (chan->cl || !try_module_get(mbox->dev->driver->owner)) {
		dev_err(dev, "%s: mailbox not free\n", __func__);
		return ERR_PTR(-EBUSY);
	}
	spin_lock_irqsave(&chan->lock, flags);
	chan->msg_free = 0;
	chan->msg_count = 0;
	chan->active_req = NULL;
	chan->cl = cl;
	init_completion(&chan->tx_complete);

	if (chan->txdone_method	== TXDONE_BY_POLL && cl->knows_txdone)
		chan->txdone_method |= TXDONE_BY_ACK;

	chan->mbox = mbox;

	spin_unlock_irqrestore(&chan->lock, flags);

	if (chan->mbox->ops == NULL || chan->mbox->ops->startup == NULL) {
		pr_err("%s%d invalid ops\n", __func__, __LINE__);
		return NULL;
	}

	ret = chan->mbox->ops->startup(chan);
	if (ret) {
		dev_err(dev, "Unable to startup the channel (%d)\n", ret);
		mbox_free_channel(chan);
		chan = ERR_PTR(ret);
	}

	return chan;
}

module_platform_driver(tegra234_psc_driver);

MODULE_DESCRIPTION("Tegra PSC driver");
MODULE_AUTHOR("dpu@nvidia.com");
MODULE_LICENSE("GPL v2");
