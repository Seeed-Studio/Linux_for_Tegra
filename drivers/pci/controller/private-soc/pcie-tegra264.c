// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2022-2025, NVIDIA CORPORATION. All rights reserved.
/*
 * PCIe host controller driver for Tegra264 SoC
 *
 * Author: Manikanta Maddireddy <mmaddireddy@nvidia.com>
 */

#include <nvidia/conftest.h>

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci-ecam.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/gpio/consumer.h>
#include <soc/tegra/bpmp.h>
#include <soc/tegra/bpmp-abi.h>
#include <soc/tegra/fuse.h>

extern int of_get_pci_domain_nr(struct device_node *node);

#define PCIE_LINK_UP_DELAY	10000	/* 10 msec */
#define PCIE_LINK_UP_TIMEOUT	1000000	/* 1 s */

/* XTL registers */
#define XTL_RC_PCIE_CFG_LINK_CONTROL_STATUS		0x58
#define XTL_RC_PCIE_CFG_LINK_CONTROL_STATUS_DLL_ACTIVE	BIT(29)

#define XTL_RC_MGMT_PERST_CONTROL		0x218
#define XTL_RC_MGMT_PERST_CONTROL_PERST_O_N	BIT(0)

#define XTL_RC_MGMT_CLOCK_CONTROL		0x47C
#define XTL_RC_MGMT_CLOCK_CONTROL_PEX_CLKREQ_I_N_PIN_USE_CONV_TO_PRSNT	BIT(9)

struct tegra264_pcie {
	struct device *dev;
	struct pci_config_window *cfg;
	struct pci_host_bridge *bridge;
	struct gpio_desc *pex_wake_gpiod;
	unsigned int pex_wake_irq;
	void __iomem *xtl_pri_base;
	void __iomem *ecam_base;
	u64 prefetch_mem_base;
	u64 prefetch_mem_limit;
	u64 mem_base;
	u64 mem_limit;
	u64 io_base;
	u64 io_limit;
	u32 ctl_id;
	struct tegra_bpmp *bpmp;
	bool link_state;
};

static int tegra264_pcie_parse_dt(struct tegra264_pcie *pcie)
{
	int ret;

	pcie->pex_wake_gpiod = devm_gpiod_get_optional(pcie->dev, "nvidia,pex-wake", GPIOD_IN);
	if (IS_ERR(pcie->pex_wake_gpiod)) {
		int err = PTR_ERR(pcie->pex_wake_gpiod);

		if (err == -EPROBE_DEFER)
			return err;

		dev_err(pcie->dev, "Failed to parse pex_wake gpio, err: %d\n", err);

		/* Don't fail PCie driver probe if pex_wake is not present.*/
		pcie->pex_wake_gpiod = NULL;
	}

	if (pcie->pex_wake_gpiod) {
		device_init_wakeup(pcie->dev, true);

		ret = gpiod_to_irq(pcie->pex_wake_gpiod);
		if (ret < 0) {
			dev_err(pcie->dev, "Failed to get IRQ for WAKE GPIO: %d\n", ret);
			return ret;
		}
		pcie->pex_wake_irq = (unsigned int)ret;
	}

	return 0;
}

static void tegra264_pcie_bpmp_set_rp_state(struct tegra264_pcie *pcie)
{
#if defined(NV_MRQ_PCIE_REQUEST_STRUCT_PRESENT) && defined(CMD_PCIE_RP_CONTROLLER_OFF)
	struct tegra_bpmp_message msg;
	struct mrq_pcie_request req;
	int err;

	memset(&req, 0, sizeof(req));

	req.cmd = CMD_PCIE_RP_CONTROLLER_OFF;
	req.rp_ctrlr_off.rp_controller = pcie->ctl_id;

	memset(&msg, 0, sizeof(msg));
	msg.mrq = MRQ_PCIE;
	msg.tx.data = &req;
	msg.tx.size = sizeof(req);

	err = tegra_bpmp_transfer(pcie->bpmp, &msg);
	if (err)
		dev_info(pcie->dev, "PCIe Controller-%d failed to turn off via BPMP with error %d\r\n",
			 pcie->ctl_id, err);

	if (msg.rx.ret)
		dev_info(pcie->dev, "PCIe Controller-%d failed to turn off via BPMP with error message %d\r\n",
			 pcie->ctl_id, msg.rx.ret);
#else
	dev_err(pcie->dev, "%s not supported!\n", __func__);
#endif
}

static void tegra264_pcie_init(struct tegra264_pcie *pcie)
{
	u32 val;

	/* Setup bus numbers */
	val = readl(pcie->ecam_base + PCI_PRIMARY_BUS);
	val &= 0xff000000;
	val |= 0x00ff0100;
	writel(val, pcie->ecam_base + PCI_PRIMARY_BUS);

	/* Setup command register */
	val = readl(pcie->ecam_base + PCI_COMMAND);
	val &= 0xffff0000;
	val |= PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER | PCI_COMMAND_SERR;
	writel(val, pcie->ecam_base + PCI_COMMAND);

	val  = readl(pcie->xtl_pri_base + XTL_RC_MGMT_PERST_CONTROL);
	val |= XTL_RC_MGMT_PERST_CONTROL_PERST_O_N;
	writel(val, pcie->xtl_pri_base + XTL_RC_MGMT_PERST_CONTROL);

	if (tegra_sku_info.platform == TEGRA_PLATFORM_VDK) {
		dev_info(pcie->dev, "PCIe Controller-%d - Skip link state check for VDK\n",
			 pcie->ctl_id);
		pcie->link_state = true;
		return;
	}

	/* Poll every 10 msec for 1 sec to link up */
	readl_poll_timeout(pcie->ecam_base + XTL_RC_PCIE_CFG_LINK_CONTROL_STATUS, val,
			   val & XTL_RC_PCIE_CFG_LINK_CONTROL_STATUS_DLL_ACTIVE,
			   PCIE_LINK_UP_DELAY, PCIE_LINK_UP_TIMEOUT);

	if (val & XTL_RC_PCIE_CFG_LINK_CONTROL_STATUS_DLL_ACTIVE) {
		/* Per PCIe r5.0, 6.6.1 wait for 100ms after DLL up */
		msleep(100);
		dev_info(pcie->dev, "PCIe Controller-%d Link is UP (Speed: %d)\n",
			 pcie->ctl_id, (val & 0xf0000) >> 16);
		pcie->link_state = true;
	} else {
		dev_info(pcie->dev, "PCIe Controller-%d Link is DOWN\r\n", pcie->ctl_id);
		val = readl(pcie->xtl_pri_base + XTL_RC_MGMT_CLOCK_CONTROL);
		/** Set link state only when link fails and no hot-plug feature is present */
		if ((val & XTL_RC_MGMT_CLOCK_CONTROL_PEX_CLKREQ_I_N_PIN_USE_CONV_TO_PRSNT) == 0) {
			dev_info(pcie->dev, "PCIe Controller-%d Link is DOWN and not hot-plug-capable. Turning off Controller.\r\n",
				 pcie->ctl_id);
			tegra264_pcie_bpmp_set_rp_state(pcie);
			pcie->link_state = false;
		} else {
			pcie->link_state = true;
		}
	}

	return;
}

static int tegra264_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tegra264_pcie *pcie;
	struct pci_host_bridge *bridge;
	struct resource_entry *bus;
	struct resource_entry *entry;
	struct resource *res;
	int ret = 0;

	bridge = devm_pci_alloc_host_bridge(dev, sizeof(struct tegra264_pcie));
	if (!bridge) {
		dev_err(dev, "failed to allocate host bridge\n");
		return -ENOMEM;
	}

	pcie = pci_host_bridge_priv(bridge);
	pcie->dev = dev;
	platform_set_drvdata(pdev, pcie);
	pcie->bridge = bridge;

	ret = pinctrl_pm_select_default_state(dev);
	if (ret < 0) {
		dev_err(dev, "Failed to configure sideband pins: %d\n", ret);
		return ret;
	}

	resource_list_for_each_entry(entry, &bridge->windows) {
		struct resource *res = entry->res;

		if (resource_type(res) == IORESOURCE_IO) {
			pcie->io_base = pci_pio_to_address(res->start);
			pcie->io_limit = pcie->io_base + resource_size(res) - 1U;
		} else if (resource_type(res) == IORESOURCE_MEM) {
			if (res->flags & IORESOURCE_PREFETCH) {
				pcie->prefetch_mem_base = res->start;
				pcie->prefetch_mem_limit = res->end;
			} else {
				pcie->mem_base = res->start;
				pcie->mem_limit = res->end;
			}
		}
	}


	ret = tegra264_pcie_parse_dt(pcie);
	if (ret < 0) {
		const char *level = KERN_ERR;

		if (ret == -EPROBE_DEFER)
			level = KERN_DEBUG;

		dev_printk(level, dev,
			   dev_fmt("Failed to parse device tree: %d\n"),
			   ret);
		return ret;
	}

	pcie->xtl_pri_base = devm_platform_ioremap_resource_byname(pdev, "xtl-pri");
	if (IS_ERR(pcie->xtl_pri_base)) {
		ret = PTR_ERR(pcie->xtl_pri_base);
		dev_err(dev, "failed to map xtl-pri memory: %d\n", ret);
		return ret;
	}

	bus = resource_list_first_type(&bridge->windows, IORESOURCE_BUS);
	if (!bus) {
		dev_err(dev, "failed to get bus resource\n");
		return -ENODEV;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ecam");
	if (!res) {
		dev_err(dev, "failed to get ecam resource\n");
		return -ENXIO;
	}

	/* Parse BPMP property only for non VDK, as interaction with BPMP not needed for VDK */
	if (tegra_sku_info.platform != TEGRA_PLATFORM_VDK) {
		ret = of_property_read_u32_index(dev->of_node, "nvidia,bpmp", 1, &pcie->ctl_id);
		if (ret) {
			dev_err(pcie->dev, "Failed to read Controller-ID: %d\n", ret);
			return ret;
		}

		pcie->bpmp = tegra_bpmp_get(dev);
		if (IS_ERR(pcie->bpmp)) {
			dev_err(dev, "tegra_bpmp_get fail: %ld\n", PTR_ERR(pcie->bpmp));
			ret = PTR_ERR(pcie->bpmp);
			return ret;
		}
	}

	pcie->cfg = pci_ecam_create(dev, res, bus->res, &pci_generic_ecam_ops);
	if (IS_ERR(pcie->cfg)) {
		dev_err(dev, "failed to create ecam config window\n");
		return PTR_ERR(pcie->cfg);
	}

	bridge->ops = (struct pci_ops *)&pci_generic_ecam_ops.pci_ops;
	bridge->sysdata = pcie->cfg;
	pcie->ecam_base = pcie->cfg->win;

	tegra264_pcie_init(pcie);
	if (pcie->link_state == false) {
		/** De-register ECAM */
		pci_ecam_free(pcie->cfg);
		return 0;
	}

	ret = pci_host_probe(bridge);
	if (ret < 0) {
		dev_err(dev, "failed to register host: %d\n", ret);
		if (tegra_sku_info.platform != TEGRA_PLATFORM_VDK)
			tegra_bpmp_put(pcie->bpmp);
		pci_ecam_free(pcie->cfg);
		return ret;
	}

	return ret;
}

static int tegra264_pcie_remove(struct platform_device *pdev)
{
	struct tegra264_pcie *pcie = platform_get_drvdata(pdev);

	if (pcie->link_state == false)
		return 0;

	if (tegra_sku_info.platform != TEGRA_PLATFORM_VDK)
		tegra_bpmp_put(pcie->bpmp);
	/*
	 * If we undo tegra264_pcie_init() then link goes down and need controller reset to bring up
	 * the link again. Remove intention is to clean up the root bridge and re enumerate during
	 * bind.
	 */
	pci_lock_rescan_remove();
	pci_stop_root_bus(pcie->bridge->bus);
	pci_remove_root_bus(pcie->bridge->bus);
	pci_unlock_rescan_remove();

	pci_ecam_free(pcie->cfg);

	return 0;
}

static int tegra264_pcie_suspend_noirq(struct device *dev)
{
	struct tegra264_pcie *pcie = dev_get_drvdata(dev);
	int ret = 0;

	if (pcie->pex_wake_gpiod && device_may_wakeup(dev)) {
		ret = enable_irq_wake(pcie->pex_wake_irq);
		if (ret < 0)
			dev_err(dev, "enable wake irq failed: %d\n", ret);
	}

	return 0;
}


static int tegra264_pcie_resume_noirq(struct device *dev)
{
	struct tegra264_pcie *pcie = dev_get_drvdata(dev);
	int ret;

	if (pcie->pex_wake_gpiod && device_may_wakeup(dev)) {
		ret = disable_irq_wake(pcie->pex_wake_irq);
		if (ret < 0)
			dev_err(dev, "disable wake irq failed: %d\n", ret);
	}

	if (pcie->link_state == false)
		return 0;

	tegra264_pcie_init(pcie);

	return 0;
}

static const struct dev_pm_ops tegra264_pcie_pm_ops = {
	.resume_noirq = tegra264_pcie_resume_noirq,
	.suspend_noirq = tegra264_pcie_suspend_noirq,
};

static const struct of_device_id tegra264_pcie_of_match[] = {
	{
		.compatible = "nvidia,tegra264-pcie",
	},
	{},
};

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void tegra264_pcie_remove_wrapper(struct platform_device *pdev)
{
	tegra264_pcie_remove(pdev);
}
#else
static int tegra264_pcie_remove_wrapper(struct platform_device *pdev)
{
	return tegra264_pcie_remove(pdev);
}
#endif

static struct platform_driver tegra264_pcie_driver = {
	.probe = tegra264_pcie_probe,
	.remove = tegra264_pcie_remove_wrapper,
	.driver = {
		.name = "tegra264-pcie",
		.pm = &tegra264_pcie_pm_ops,
		.of_match_table = tegra264_pcie_of_match,
	},
};

module_platform_driver(tegra264_pcie_driver);

MODULE_DEVICE_TABLE(of, tegra264_pcie_of_match);

MODULE_AUTHOR("Manikanta Maddireddy <mmaddireddy@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra264 PCIe host controller driver");
MODULE_LICENSE("GPL v2");
