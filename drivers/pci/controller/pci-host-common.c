// SPDX-License-Identifier: GPL-2.0
/*
 * Generic PCI host driver common code
 *
 * Copyright (C) 2014 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/pci-ecam.h>
#include <linux/pci_regs.h>
#include <linux/platform_device.h>
#include <linux/iopoll.h>

#include "../pci.h"

static void gen_pci_unmap_cfg(void *ptr)
{
	pci_ecam_free((struct pci_config_window *)ptr);
}

static void pci_wait_for_link(struct device *dev, void __iomem *ecam_base)
{
	void __iomem *link_status_reg;
	u32 cap_ptr, cap_id;
	u32 pcie_cap_offset = 0;
	u16 pcie_cap_reg, link_status, slot_status;
	int poll_iter;
	int ret;

	/* Find PCIe capability in configuration space (BDF 0:0:0) */
	cap_ptr = readb(ecam_base + PCI_CAPABILITY_LIST);

	for (poll_iter = 0; poll_iter < 0xFF; poll_iter++) {
		if (cap_ptr == 0)
			break;

		cap_id = readb(ecam_base + cap_ptr + PCI_CAP_LIST_ID);
		if (cap_id == PCI_CAP_ID_EXP) {
			pcie_cap_offset = cap_ptr;
			break;
		}
		cap_ptr = readb(ecam_base + cap_ptr + PCI_CAP_LIST_NEXT);
	}

	if (pcie_cap_offset == 0) {
		dev_err(dev, "PCIe capability not found\n");
		return;
	}

	/* Check SLOT_IMPLEMENTED bit in PCI Express Capabilities Register */
	pcie_cap_reg = readw(ecam_base + pcie_cap_offset + PCI_EXP_FLAGS);
	dev_dbg(dev, "PCIe capability found at offset 0x%x, cap header 0x%x\n",
		pcie_cap_offset, pcie_cap_reg);

	if (pcie_cap_reg & PCI_EXP_FLAGS_SLOT) {
		/* Slot implemented, check PRESENCE_DET_STATE */
		slot_status = readw(ecam_base + pcie_cap_offset + PCI_EXP_SLTSTA);

		dev_dbg(dev, "Slot status 0x%x\n", slot_status);

		if (!(slot_status & PCI_EXP_SLTSTA_PDS)) {
			/* Slot implemented but device not present */
			dev_info(dev, "Slot implemented but device not present, skipping link poll\n");
			return;
		}
	}

	/* Poll for DLL_ACTIVE bit using kernel timeout API */
	link_status_reg = ecam_base + pcie_cap_offset + PCI_EXP_LNKSTA;
	ret = readw_poll_timeout(link_status_reg, link_status,
				 link_status & PCI_EXP_LNKSTA_DLLLA,
				 20000, 1000000);

	if (ret) {
		dev_info(dev, "DLL_ACTIVE bit not set after timeout\n");
		return;
	}

	dev_dbg(dev, "DLL_ACTIVE bit set, link is up\n");

	/* DLL_ACTIVE set, wait additional 100ms as per PCIe spec */
	msleep(100);

	dev_dbg(dev, "Link wait completed successfully\n");
}

static struct pci_config_window *gen_pci_init(struct device *dev,
		struct pci_host_bridge *bridge, const struct pci_ecam_ops *ops)
{
	int err;
	struct resource cfgres;
	struct resource_entry *bus;
	struct pci_config_window *cfg;

	err = of_address_to_resource(dev->of_node, 0, &cfgres);
	if (err) {
		dev_err(dev, "missing \"reg\" property\n");
		return ERR_PTR(err);
	}

	bus = resource_list_first_type(&bridge->windows, IORESOURCE_BUS);
	if (!bus)
		return ERR_PTR(-ENODEV);

	cfg = pci_ecam_create(dev, &cfgres, bus->res, ops);
	if (IS_ERR(cfg))
		return cfg;

	err = devm_add_action_or_reset(dev, gen_pci_unmap_cfg, cfg);
	if (err)
		return ERR_PTR(err);

	return cfg;
}

int pci_host_common_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pci_host_bridge *bridge;
	struct pci_config_window *cfg;
	const struct pci_ecam_ops *ops;

	ops = of_device_get_match_data(&pdev->dev);
	if (!ops)
		return -ENODEV;

	bridge = devm_pci_alloc_host_bridge(dev, 0);
	if (!bridge)
		return -ENOMEM;

	platform_set_drvdata(pdev, bridge);

	of_pci_check_probe_only();

	/* Parse and map our Configuration Space windows */
	cfg = gen_pci_init(dev, bridge, ops);
	if (IS_ERR(cfg))
		return PTR_ERR(cfg);

	/* Wait for PCIe link if not in probe-only mode */
	if (!of_pci_preserve_config(dev->of_node))
		pci_wait_for_link(dev, cfg->win);
	else
		dev_dbg(dev, "Probe only mode, skipping link wait\n");

	bridge->sysdata = cfg;
	bridge->ops = (struct pci_ops *)&ops->pci_ops;
	bridge->msi_domain = true;

	return pci_host_probe(bridge);
}
EXPORT_SYMBOL_GPL(pci_host_common_probe);

void pci_host_common_remove(struct platform_device *pdev)
{
	struct pci_host_bridge *bridge = platform_get_drvdata(pdev);

	pci_lock_rescan_remove();
	pci_stop_root_bus(bridge->bus);
	pci_remove_root_bus(bridge->bus);
	pci_unlock_rescan_remove();
}
EXPORT_SYMBOL_GPL(pci_host_common_remove);

MODULE_LICENSE("GPL v2");
