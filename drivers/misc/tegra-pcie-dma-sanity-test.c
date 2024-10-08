// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci-epc.h>
#include <linux/pci-epf.h>
#include <linux/random.h>
#include <linux/tegra-pcie-dma-sanity-helpers.h>
#include <soc/tegra/fuse-helper.h>

#define MODULENAME "tegra_pcie_dma_sanity_test"

/* Type definitions */
struct ep_sanity_pvt {
	struct pci_dev *pdev;
	void __iomem *bar_virt;
	dma_addr_t bar_phy;

	void *rp_dma_virt;
	dma_addr_t rp_dma_phy;

	void *ep_dma_virt;
	dma_addr_t ep_dma_phy;

	struct dentry *debugfs;
	struct edmalib_sanity edma;

	u8 chip_id;
	u32 msi_irq;
	u64 msi_addr;
	u32 msi_data;
};

static int write_sanity_test(struct seq_file *s, void *data)
{
	struct ep_sanity_pvt *ep = (struct ep_sanity_pvt *)dev_get_drvdata(s->private);
	struct pci_dev *pdev = ep->pdev;
	struct pci_dev *ppdev = pcie_find_root_port(pdev);
	/* Offset for header */
	u64 offset = (BAR0_DMA_BUF_OFFSET);
	uint32_t sizes[3] = {0.5 * SZ_1M, 2.5 * SZ_1M, 6.25 * SZ_1M};
	char *sizes_string[3] = {"1 MB", "5 MB", "12.5 MB"};
	int ret, i;

	/* Add relevant details here, and then call the common edmalib_sanity_tester */
	ep->edma.fdev = &ep->pdev->dev;
	ep->edma.priv = (void *)ep;

	/* RP -> EP */
	ep->edma.src_dma_addr = ep->rp_dma_phy + offset;
	ep->edma.dst_dma_addr = ep->bar_phy + offset;
	ep->edma.src_virt = ep->rp_dma_virt + offset;
	ep->edma.dst_virt = ep->bar_virt + offset;
	ep->edma.cdev = &ppdev->dev;

	if (ep->chip_id == TEGRA264) {
		ep->edma.msi_addr = ep->msi_addr;
		ep->edma.msi_data = ep->msi_data;
		ep->edma.msi_irq = ep->msi_irq;
	}

	ep->edma.stress_count = 10;
	ep->edma.nents = 2;
	ep->edma.edma_ch_type = TEGRA_PCIE_DMA_CHAN_XFER_ASYNC;
	for (i = 0; i < ARRAY_SIZE(sizes); i++) {
		dev_info(ep->edma.fdev, "%s ----- TESTING WITH TRANSFER SIZE %s -----\n",
				__func__, sizes_string[i]);
		ep->edma.dma_size = sizes[i];
		if (ep->edma.dma_size == (12.5 * SZ_1M))
			ep->edma.deinit_dma = true;
		ret = edmalib_sanity_tester(&ep->edma);
		if (ret) {
			dev_err(ep->edma.fdev, "%s: Failed at total transfer size = %s | ERROR: %d\n",
					__func__, sizes_string[i], ret);
			return ret;
		}
	}
	return 0;
}

static void init_debugfs_sanity(struct ep_sanity_pvt *ep)
{
	debugfs_create_devm_seqfile(&ep->pdev->dev, "write_sanity_test", ep->debugfs,
			write_sanity_test);
}

static int tegra_pcie_dma_sanity_test_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct ep_sanity_pvt *ep;
	struct pcie_epf_bar *epf_bar;
	struct pci_dev *ppdev = pcie_find_root_port(pdev);
	int ret = 0;
	char *name;
	u8 bar;
	u32 val;
	u16 val_16;

	/* Allocate the device space on the CPU memory space */
	ep = devm_kzalloc(&pdev->dev, sizeof(*ep), GFP_KERNEL);
	if (!ep)
		return -ENOMEM;
	/* Chip-ID */
	ep->chip_id = __tegra_get_chip_id();
	if (ep->chip_id == TEGRA234)
		ep->edma.chip_id = NVPCIE_DMA_SOC_T234;
	else
		ep->edma.chip_id = NVPCIE_DMA_SOC_T264;

	/* Allocate space for the Linked-List */
	ep->edma.ll_desc = devm_kzalloc(&pdev->dev, sizeof(*ep->edma.ll_desc) * NUM_EDMA_DESC,
			GFP_KERNEL);
	if (!ep->edma.ll_desc)
		return -ENOMEM;
	/* Link the device space to the device */
	ep->pdev = pdev;
	pci_set_drvdata(pdev, ep);

	/* Enable the PCI device */
	ret = pci_enable_device(pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to enable the PCI device\n");
		return ret;
	}
	/* Enable bus-mastering using pci_set_master()*/
	pci_set_master(pdev);

	/* Request regions for the EP */
	ret = pci_request_regions(pdev, MODULENAME);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request PCI regions\n");
		goto fail_region_request;
	}
	/* pci_resource_start for the bus starting address of bar0 for orin, bar2 for thor*/
	if (ep->chip_id == TEGRA234)
		bar = 0;
	else
		bar = 2;

	ep->bar_phy = pci_resource_start(pdev, bar);
	/* Remap bar to IO using Write-Combine caching. This allows the DMA to write to
	 * memory in bursts rather than with every transaction initiated
	 */
	ep->bar_virt = devm_ioremap_wc(&pdev->dev, ep->bar_phy, pci_resource_len(pdev, (u32)bar));
	if (!ep->bar_virt) {
		dev_err(&pdev->dev, "Failed to remap to IO\n");
		ret = -ENOMEM;
		goto fail_region_remap;
	}
	/* Allocate and initialize shared control data BAR0 for both RP and EP */
	/* RP */
	ep->rp_dma_virt = dma_alloc_coherent(&ppdev->dev, BAR0_SIZE, &ep->rp_dma_phy, GFP_KERNEL);
	if (!ep->rp_dma_virt) {
		ret = -ENOMEM;
		goto fail_rp_dma_alloc;
	}
	dev_dbg(&pdev->dev, "DMA memory pdev IOVA: 0x%llx, size: %d\n", ep->rp_dma_phy, BAR0_SIZE);
	/* EP */
	ep->ep_dma_virt = dma_alloc_coherent(&pdev->dev, BAR0_SIZE, &ep->ep_dma_phy, GFP_KERNEL);
	if (!ep->ep_dma_virt) {
		ret = -ENOMEM;
		goto fail_ep_dma_alloc;
	}
	dev_dbg(&pdev->dev, "DMA memory pdev IOVA: 0x%llx, size: %d\n", ep->ep_dma_phy, BAR0_SIZE);

	epf_bar = (__force struct pcie_epf_bar *)ep->bar_virt;
	epf_bar->rp_phy_addr = ep->ep_dma_phy;

	if (ep->chip_id == TEGRA264) {
		/* Allocating MSI's for DMA used in thor */
		ret = pci_alloc_irq_vectors(pdev, 16, 16, PCI_IRQ_MSI);
		if (ret < 0) {
			ret = -ENODEV;
			goto fail_vectors;
		}
		/* Reading the msi_address, to write data and IRQ vector */
		pci_read_config_word(ppdev, ppdev->msi_cap + PCI_MSI_FLAGS, &val_16);
		if (val_16 & PCI_MSI_FLAGS_64BIT) {
			pci_read_config_dword(ppdev, ppdev->msi_cap + PCI_MSI_ADDRESS_HI, &val);
			ep->msi_addr = val;

			pci_read_config_word(ppdev, ppdev->msi_cap + PCI_MSI_DATA_64, &val_16);
			ep->msi_data = val_16;
		} else {
			pci_read_config_word(ppdev, ppdev->msi_cap + PCI_MSI_DATA_32, &val_16);
			ep->msi_data = val_16;
		}
		pci_read_config_dword(ppdev, ppdev->msi_cap + PCI_MSI_ADDRESS_LO, &val);
		ep->msi_addr = (ep->msi_addr << 32) | val;
		ep->msi_irq = pci_irq_vector(ppdev, TEGRA264_PCIE_DMA_MSI_LOCAL_VEC);
		ep->msi_data += TEGRA264_PCIE_DMA_MSI_LOCAL_VEC;
	}

	/* Set-up debugfs */
	name = devm_kasprintf(&ep->pdev->dev, GFP_KERNEL, "%s_pcie_dma_sanity_test",
			dev_name(&pdev->dev));
	if (!name) {
		dev_err(&pdev->dev, "%s: Fail to set debugfs name\n", __func__);
		ret = -ENOMEM;
		goto fail_name;
	}
	ep->debugfs = debugfs_create_dir(name, NULL);
	init_debugfs_sanity(ep);

	return ret;

	/* Failure labels: */
fail_name:
	pci_free_irq_vectors(pdev);
fail_vectors:
	dma_free_coherent(&pdev->dev, BAR0_SIZE, ep->ep_dma_virt, ep->ep_dma_phy);
fail_ep_dma_alloc:
	dma_free_coherent(&ppdev->dev, BAR0_SIZE, ep->rp_dma_virt, ep->rp_dma_phy);
fail_rp_dma_alloc:
fail_region_remap:
	pci_release_regions(pdev);
fail_region_request:
	pci_clear_master(pdev);

	return ret;
}

static void tegra_pcie_dma_sanity_test_remove(struct pci_dev *pdev)
{
	struct ep_sanity_pvt *ep = pci_get_drvdata(pdev);
	struct pci_dev *ppdev = pcie_find_root_port(pdev);

	/* Remove the debugfs */
	debugfs_remove_recursive(ep->debugfs);
	/* Deinit the cookie */
	tegra_pcie_dma_deinit(&ep->edma.cookie);
	/* Free the IRQ */
	if (ep->chip_id == TEGRA264)
		pci_free_irq_vectors(pdev);
	/* Free the shared control data in bar0 of RP and EP */
	dma_free_coherent(&pdev->dev, BAR0_SIZE, ep->ep_dma_virt, ep->ep_dma_phy);
	dma_free_coherent(&ppdev->dev, BAR0_SIZE, ep->rp_dma_virt, ep->rp_dma_phy);
	/* Release the regions of EP */
	pci_release_regions(pdev);
	/* Undo the bus-mastering used for DMA */
	pci_clear_master(pdev);
}

static const struct pci_device_id ep_pci_tbl[] = {
	{ PCI_DEVICE(0x10DE, 0x229C) },
	{ PCI_DEVICE(0x10DE, 0x22D8) },
	{}
}; /* List of recognised device IDs for this module */
MODULE_DEVICE_TABLE(pci, ep_pci_tbl);
static struct pci_driver ep_pci_driver = {
	.name = MODULENAME,
	.id_table = ep_pci_tbl,
	.probe = tegra_pcie_dma_sanity_test_probe,
	.remove = tegra_pcie_dma_sanity_test_remove,
}; /* Module details of the pci driver */
module_pci_driver(ep_pci_driver);

MODULE_DESCRIPTION("Tegra PCIe client driver to test EP DMA functioning");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Srishti Goel <srgoel@nvidia.com>");
