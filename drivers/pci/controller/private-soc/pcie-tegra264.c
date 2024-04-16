// SPDX-License-Identifier: GPL-2.0+
/*
 * PCIe host controller driver for Tegra264 SoC
 *
 * Copyright (c) 2022-2024, NVIDIA CORPORATION. All rights reserved.
 *
 * Author: Manikanta Maddireddy <mmaddireddy@nvidia.com>
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci-ecam.h>
#include <linux/platform_device.h>

extern int of_get_pci_domain_nr(struct device_node *node);

#define PCIE_LINK_UP_DELAY	10000	/* 10 msec */
#define PCIE_LINK_UP_TIMEOUT	1000000	/* 1 s */

/* XAL registers */
#define XAL_RC_MEM_32BIT_BASE_HI		0x1c
#define XAL_RC_MEM_32BIT_BASE_LO		0x20
#define XAL_RC_MEM_32BIT_LIMIT_HI		0x24
#define XAL_RC_MEM_32BIT_LIMIT_LO		0x28
#define XAL_RC_MEM_64BIT_BASE_HI		0x2c
#define XAL_RC_MEM_64BIT_BASE_LO		0x30
#define XAL_RC_MEM_64BIT_LIMIT_HI		0x34
#define XAL_RC_MEM_64BIT_LIMIT_LO		0x38
#define XAL_RC_BAR_CNTL_STANDARD		0x40
#define XAL_RC_BAR_CNTL_STANDARD_IOBAR_EN	BIT(0)
#define XAL_RC_BAR_CNTL_STANDARD_32B_BAR_EN	BIT(1)
#define XAL_RC_BAR_CNTL_STANDARD_64B_BAR_EN	BIT(2)

/* XTL registers */
#define XTL_RC_PCIE_CFG_LINK_CONTROL_STATUS		0x58
#define XTL_RC_PCIE_CFG_LINK_CONTROL_STATUS_DLL_ACTIVE	BIT(29)

#define XTL_RC_MGMT_PERST_CONTROL		0x218
#define XTL_RC_MGMT_PERST_CONTROL_PERST_O_N	BIT(0)

struct tegra264_pcie {
	struct device *dev;
	struct pci_config_window *cfg;
	void __iomem *xal_base;
	void __iomem *xtl_pri_base;
	void __iomem *ecam_base;
	u64 prefetch_mem_base;
	u64 prefetch_mem_limit;
	u64 mem_base;
	u64 mem_limit;
	u32 ctl_id;
};

static void tegra264_pcie_init(struct tegra264_pcie *pcie)
{
	u32 val;

	/* Program XAL */
	writel(upper_32_bits(pcie->mem_base), pcie->xal_base + XAL_RC_MEM_32BIT_BASE_HI);
	writel(lower_32_bits(pcie->mem_base), pcie->xal_base + XAL_RC_MEM_32BIT_BASE_LO);

	writel(upper_32_bits(pcie->mem_limit), pcie->xal_base + XAL_RC_MEM_32BIT_LIMIT_HI);
	writel(lower_32_bits(pcie->mem_limit), pcie->xal_base + XAL_RC_MEM_32BIT_LIMIT_LO);

	writel(upper_32_bits(pcie->prefetch_mem_base), pcie->xal_base + XAL_RC_MEM_64BIT_BASE_HI);
	writel(lower_32_bits(pcie->prefetch_mem_base), pcie->xal_base + XAL_RC_MEM_64BIT_BASE_LO);

	writel(upper_32_bits(pcie->prefetch_mem_limit), pcie->xal_base + XAL_RC_MEM_64BIT_LIMIT_HI);
	writel(lower_32_bits(pcie->prefetch_mem_limit), pcie->xal_base + XAL_RC_MEM_64BIT_LIMIT_LO);

	val = XAL_RC_BAR_CNTL_STANDARD_IOBAR_EN | XAL_RC_BAR_CNTL_STANDARD_32B_BAR_EN |
		XAL_RC_BAR_CNTL_STANDARD_64B_BAR_EN;
	writel(val, pcie->xal_base + XAL_RC_BAR_CNTL_STANDARD);

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

	/* Poll every 10 msec for 1 sec to link up */
	readl_poll_timeout(pcie->ecam_base + XTL_RC_PCIE_CFG_LINK_CONTROL_STATUS, val,
			   val & XTL_RC_PCIE_CFG_LINK_CONTROL_STATUS_DLL_ACTIVE,
			   PCIE_LINK_UP_DELAY, PCIE_LINK_UP_TIMEOUT);

	if (val & XTL_RC_PCIE_CFG_LINK_CONTROL_STATUS_DLL_ACTIVE) {
		/* Per PCIe r5.0, 6.6.1 wait for 100ms after DLL up */
		msleep(100);
		dev_info(pcie->dev, "PCIe Controller-%d Link is UP (Speed: %d)\n",
			 pcie->ctl_id, (val & 0xf0000) >> 16);
	} else {
		dev_info(pcie->dev, "PCIe Controller-%d Link is DOWN\r\n", pcie->ctl_id);
	}
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

	resource_list_for_each_entry(entry, &bridge->windows) {
		struct resource *res = entry->res;

		if (resource_type(res) != IORESOURCE_MEM)
			continue;

		if (res->flags & IORESOURCE_PREFETCH) {
			pcie->prefetch_mem_base = res->start;
			pcie->prefetch_mem_limit = res->end;
		} else {
			pcie->mem_base = res->start;
			pcie->mem_limit = res->end;
		}
	}

	ret = of_get_pci_domain_nr(dev->of_node);
	if (ret < 0) {
		dev_err(dev, "failed to get domain number: %d\n", ret);
		return ret;
	}
	pcie->ctl_id = ret;

	pcie->xal_base = devm_platform_ioremap_resource_byname(pdev, "xal");
	if (IS_ERR(pcie->xal_base)) {
		ret = PTR_ERR(pcie->xal_base);
		dev_err(dev, "failed to map xal memory: %d\n", ret);
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

	pcie->cfg = pci_ecam_create(dev, res, bus->res, &pci_generic_ecam_ops);
	if (IS_ERR(pcie->cfg)) {
		dev_err(dev, "failed to create ecam config window\n");
		return PTR_ERR(pcie->cfg);
	}

	bridge->ops = (struct pci_ops *)&pci_generic_ecam_ops.pci_ops;
	bridge->sysdata = pcie->cfg;
	pcie->ecam_base = pcie->cfg->win;

	tegra264_pcie_init(pcie);

	ret = pci_host_probe(bridge);
	if (ret < 0) {
		dev_err(dev, "failed to register host: %d\n", ret);
		pci_ecam_free(pcie->cfg);
		return ret;
	}

	return ret;
}

static int tegra264_pcie_resume_noirq(struct device *dev)
{
	struct tegra264_pcie *pcie = dev_get_drvdata(dev);

	tegra264_pcie_init(pcie);

	return 0;
}

static const struct dev_pm_ops tegra264_pcie_pm_ops = {
	.resume_noirq = tegra264_pcie_resume_noirq,
};

static const struct of_device_id tegra264_pcie_of_match[] = {
	{
		.compatible = "nvidia,tegra264-pcie",
	},
	{},
};

static struct platform_driver tegra264_pcie_driver = {
	.probe = tegra264_pcie_probe,
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
