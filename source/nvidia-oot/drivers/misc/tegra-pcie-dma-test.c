// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2021-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * PCIe DMA test framework for Tegra PCIe.
 */

#include <nvidia/conftest.h>

#include <linux/aer.h>
#include <linux/crc32.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/random.h>
#include <linux/tegra-pcie-edma-test-common.h>
#include <linux/types.h>
#include <soc/tegra/fuse-helper.h>

#define MODULENAME "pcie_dma_host"

struct ep_pvt {
	struct pci_dev *pdev;
	/* Configurable BAR0/BAR2 virt and phy base addresses */
	void __iomem *bar_virt;
	dma_addr_t bar_phy;
	/* DMA BAR to generate interrupts towards EP */
	void __iomem *msi_bar_virt;
	/* MSI address offset at which MSI data needs to be written */
	void __iomem *msi_bar_offset;
	dma_addr_t msi_bar_phy;
	/* DMA register BAR virt and phy base addresses */
	void __iomem *dma_virt;
	phys_addr_t dma_phy_base;
	u32 dma_phy_size;

	/* dma_alloc_coherent() using RP pci_dev */
	void *rp_dma_virt;
	dma_addr_t rp_dma_phy;
	/* dma_alloc_coherent() using EP pci_dev */
	void *ep_dma_virt;
	dma_addr_t ep_dma_phy;

	struct dentry *debugfs;

	u32 dma_size;
	u32 stress_count;
	u32 edma_ch;
	u32 prev_edma_ch;
	u32 msi_irq;
	u64 msi_addr;
	u32 msi_data;
	u32 pmsi_irq;
	u64 pmsi_addr;
	u32 pmsi_data;
	u8 chip_id;
	struct edmalib_common edma;
};

static irqreturn_t ep_isr(int irq, void *arg)
{
	struct ep_pvt *ep = (struct ep_pvt *)arg;
	struct pcie_epf_bar *epf_bar = (__force struct pcie_epf_bar *)ep->bar_virt;
	struct sanity_data *wr_data = &epf_bar->wr_data[0];
	u64 *data = (u64 *)(ep->ep_dma_virt + BAR0_DMA_BUF_OFFSET + wr_data->dst_offset);

	dev_info(&ep->pdev->dev, "%s: wr_data size(0x%x), offset(%d). data[0]=0x%llx, data[size-1]=0x%llx\n",
		 __func__, wr_data->size, wr_data->dst_offset, data[0],
		 data[(wr_data->size/8) - 1u]);

	wr_data->crc = crc32_le(~0, ep->ep_dma_virt + BAR0_DMA_BUF_OFFSET + wr_data->dst_offset,
				wr_data->size);

	return IRQ_HANDLED;
}

static void tegra_pcie_dma_raise_irq(void *p)
{
	struct ep_pvt *ep = (struct ep_pvt *)p;
	struct pcie_epf_bar *epf_bar = (__force struct pcie_epf_bar *)ep->bar_virt;
	struct sanity_data *wr_data = &epf_bar->wr_data[0];
	u64 *data = (u64 *)(ep->edma.src_virt + wr_data->src_offset);

	dev_info(&ep->pdev->dev, "%s: wr_data size(0x%x), offset(%d). data[0]=0x%llx, data[size-1]=0x%llx\n",
		 __func__, wr_data->size, wr_data->dst_offset, data[0],
		 data[(wr_data->size/8) - 1u]);
	dev_info(&ep->pdev->dev, "%s: IRQ towards EP using MSI virt offset is %p MSI BAR PHY %llx\n",
		 __func__, ep->msi_bar_offset, ep->msi_bar_phy);
	writel(TEGRA264_PCIE_DMA_MSI_CRC_VEC, ep->msi_bar_offset);

}

/* debugfs to perform eDMA lib transfers */
static int edmalib_test(struct seq_file *s, void *data)
{
	struct ep_pvt *ep = (struct ep_pvt *)dev_get_drvdata(s->private);
	struct pcie_epf_bar *epf_bar = (__force struct pcie_epf_bar *)ep->bar_virt;
	struct pci_dev *pdev = ep->pdev;
	struct edmalib_common *edma = &ep->edma;
	struct pci_dev *ppdev = pcie_find_root_port(pdev);
	/* RP uses "Base + (BAR0_SIZE / 2) + 1M(reserved)" offset for DMA data transfers */
	u64 offset = ((BAR0_SIZE / 2) + BAR0_DMA_BUF_OFFSET);

	ep->edma.fdev = &ep->pdev->dev;
	ep->edma.epf_bar = epf_bar;
	ep->edma.bar_phy = ep->bar_phy;
	ep->edma.dma_virt = ep->dma_virt;
	ep->edma.priv = (void *)ep;
	ep->edma.raise_irq = tegra_pcie_dma_raise_irq;

	if (REMOTE_EDMA_TEST_EN) {
		ep->edma.src_virt = ep->ep_dma_virt + offset;
		ep->edma.src_dma_addr = ep->ep_dma_phy + offset;
		ep->edma.dst_dma_addr = epf_bar->ep_phy_addr + offset;
		ep->edma.msi_addr = ep->msi_addr;
		ep->edma.msi_data = ep->msi_data;
		ep->edma.msi_irq = ep->msi_irq;
		ep->edma.cdev = &pdev->dev;
		ep->edma.remote.dma_phy_base = ep->dma_phy_base;
		ep->edma.remote.dma_size = ep->dma_phy_size;
	} else {
		ep->edma.src_dma_addr = ep->rp_dma_phy + offset;
		ep->edma.src_virt = ep->rp_dma_virt + offset;
		ep->edma.dst_dma_addr = ep->bar_phy + offset;
		ep->edma.msi_addr = ep->pmsi_addr;
		ep->edma.msi_data = ep->pmsi_data;
		ep->edma.msi_irq = ep->pmsi_irq;
		ep->edma.cdev = &ppdev->dev;
	}

	return edmalib_common_test(&ep->edma);
}

static void init_debugfs(struct ep_pvt *ep)
{
	debugfs_create_devm_seqfile(&ep->pdev->dev, "edmalib_test", ep->debugfs, edmalib_test);

	debugfs_create_u32("edma_ch", 0644, ep->debugfs, &ep->edma.edma_ch);
	/* Enable remote dma ASYNC for ch 0 as default */
	ep->edma.edma_ch = 0x80000011;
	ep->edma.st_as_ch = -1;

	debugfs_create_u32("stress_count", 0644, ep->debugfs, &ep->edma.stress_count);
	ep->edma.stress_count = 10;

	debugfs_create_u32("dma_size", 0644, ep->debugfs, &ep->edma.dma_size);
	ep->edma.dma_size = SZ_1M;

	debugfs_create_u32("nents", 0644, ep->debugfs, &ep->edma.nents);
	/* Set DMA_LL_DEFAULT_SIZE as default nents, Max NUM_EDMA_DESC */
	ep->edma.nents = DMA_LL_DEFAULT_SIZE;
}

static int ep_test_dma_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct ep_pvt *ep;
	struct pcie_epf_bar *epf_bar;
	struct pci_dev *ppdev = pcie_find_root_port(pdev);
	int ret = 0;
	u32 val, i, bar, dma_bar, msi_bar;
	u16 val_16;
	char *name;

	ep = devm_kzalloc(&pdev->dev, sizeof(*ep), GFP_KERNEL);
	if (!ep)
		return -ENOMEM;

	ep->chip_id = __tegra_get_chip_id();
	if (ep->chip_id == TEGRA234)
		ep->edma.chip_id = NVPCIE_DMA_SOC_T234;
	else
		ep->edma.chip_id = NVPCIE_DMA_SOC_T264;

	ep->edma.ll_desc = devm_kzalloc(&pdev->dev, sizeof(*ep->edma.ll_desc) * NUM_EDMA_DESC,
					GFP_KERNEL);
	if (!ep->edma.ll_desc)
		return -ENOMEM;

	ep->pdev = pdev;
	pci_set_drvdata(pdev, ep);

	ret = pci_enable_device(pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to enable PCI device\n");
		return ret;
	}

#if defined(NV_PCI_ENABLE_PCIE_ERROR_REPORTING_PRESENT) /* Linux 6.5 */
	pci_enable_pcie_error_reporting(pdev);
#endif

	pci_set_master(pdev);

	ret = pci_request_regions(pdev, MODULENAME);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request PCI regions\n");
		goto fail_region_request;
	}

	if (ep->chip_id == TEGRA234)
		bar = 0;
	else
		bar = 2;
	ep->bar_phy = pci_resource_start(pdev, bar);
	ep->bar_virt = devm_ioremap_wc(&pdev->dev, ep->bar_phy, pci_resource_len(pdev, bar));
	if (!ep->bar_virt) {
		dev_err(&pdev->dev, "Failed to IO remap BAR%d\n", bar);
		ret = -ENOMEM;
		goto fail_region_remap;
	}

	if (ep->chip_id == TEGRA234)
		msi_bar = 2;
	else
		msi_bar = 4;
	ep->msi_bar_phy = pci_resource_start(pdev, msi_bar);
	ep->msi_bar_virt = devm_ioremap_wc(&pdev->dev, ep->msi_bar_phy,
					   pci_resource_len(pdev, msi_bar));
	if (!ep->msi_bar_virt) {
		dev_err(&pdev->dev, "Failed to IO remap MSI bar BAR%d\n", msi_bar);
		ret = -ENOMEM;
		goto fail_region_remap;
	}

	/**
	 * For T264, MSI address(GIC_TRANSTALATER) is at 0x1FFF040 offset. due to its
	 * 32 MB allignment.
	 */
	if (ep->chip_id == TEGRA264)
		ep->msi_bar_offset = (void __iomem *)((u8 *)ep->msi_bar_virt + 0x1FFF040);
	else
		ep->msi_bar_offset = ep->msi_bar_virt;

	if (ep->chip_id == TEGRA234)
		dma_bar = 4;
	else
		dma_bar = 0;
	ep->dma_phy_base = pci_resource_start(pdev, dma_bar);
	ep->dma_phy_size = pci_resource_len(pdev, dma_bar);
	ep->dma_virt = devm_ioremap(&pdev->dev, ep->dma_phy_base, ep->dma_phy_size);
	if (!ep->dma_virt) {
		dev_err(&pdev->dev, "Failed to IO remap BAR%d\n", dma_bar);
		ret = -ENOMEM;
		goto fail_region_remap;
	}

	ret = pci_alloc_irq_vectors(pdev, 16, 16, PCI_IRQ_MSI);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to enable MSI interrupt\n");
		ret = -ENODEV;
		goto fail_region_remap;
	}

	ret = request_irq(pci_irq_vector(pdev, TEGRA264_PCIE_DMA_MSI_CRC_VEC), ep_isr, IRQF_SHARED,
			  "pcie_ep_isr", ep);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register isr\n");
		goto fail_isr;
	}

	ep->rp_dma_virt = dma_alloc_coherent(&ppdev->dev, BAR0_SIZE, &ep->rp_dma_phy, GFP_KERNEL);
	if (!ep->rp_dma_virt) {
		dev_err(&pdev->dev, "Failed to allocate DMA memory\n");
		ret = -ENOMEM;
		goto fail_rp_dma_alloc;
	}
	get_random_bytes(ep->rp_dma_virt, BAR0_SIZE);
	dev_info(&ppdev->dev, "DMA mem ppdev, IOVA: 0x%llx size: %d\n", ep->rp_dma_phy, BAR0_SIZE);

	ep->ep_dma_virt = dma_alloc_coherent(&pdev->dev, BAR0_SIZE, &ep->ep_dma_phy, GFP_KERNEL);
	if (!ep->ep_dma_virt) {
		dev_err(&pdev->dev, "Failed to allocate DMA memory for EP\n");
		ret = -ENOMEM;
		goto fail_ep_dma_alloc;
	}
	get_random_bytes(ep->ep_dma_virt, BAR0_SIZE);
	dev_info(&pdev->dev, "DMA mem pdev, IOVA: 0x%llx size: %d\n", ep->ep_dma_phy, BAR0_SIZE);

	/* Update RP DMA system memory base address allocated with EP pci_dev in BAR0 */
	epf_bar = (__force struct pcie_epf_bar *)ep->bar_virt;
	epf_bar->rp_phy_addr = ep->ep_dma_phy;
	/* Assign OB magic number */
	*((u64 *)((u8 *)ep->ep_dma_virt + PCIE_EP_OB_OFFSET)) = PCIE_EP_OB_MAGIC;

	pci_read_config_word(pdev, pdev->msi_cap + PCI_MSI_FLAGS, &val_16);
	if (val_16 & PCI_MSI_FLAGS_64BIT) {
		pci_read_config_dword(pdev, pdev->msi_cap + PCI_MSI_ADDRESS_HI, &val);
		ep->msi_addr = val;

		pci_read_config_word(pdev, pdev->msi_cap + PCI_MSI_DATA_64, &val_16);
		ep->msi_data = val_16;
	} else {
		pci_read_config_word(pdev, pdev->msi_cap + PCI_MSI_DATA_32, &val_16);
		ep->msi_data = val_16;
	}
	pci_read_config_dword(pdev, pdev->msi_cap + PCI_MSI_ADDRESS_LO, &val);
	ep->msi_addr = (ep->msi_addr << 32) | val;
	ep->msi_irq = pci_irq_vector(pdev, TEGRA264_PCIE_DMA_MSI_REMOTE_VEC);
	ep->msi_data += TEGRA264_PCIE_DMA_MSI_REMOTE_VEC;

	pci_read_config_word(ppdev, ppdev->msi_cap + PCI_MSI_FLAGS, &val_16);
	if (val_16 & PCI_MSI_FLAGS_64BIT) {
		pci_read_config_dword(ppdev, ppdev->msi_cap + PCI_MSI_ADDRESS_HI, &val);
		ep->pmsi_addr = val;

		pci_read_config_word(ppdev, ppdev->msi_cap + PCI_MSI_DATA_64, &val_16);
		ep->pmsi_data = val_16;
	} else {
		pci_read_config_word(ppdev, ppdev->msi_cap + PCI_MSI_DATA_32, &val_16);
		ep->pmsi_data = val_16;
	}
	pci_read_config_dword(ppdev, ppdev->msi_cap + PCI_MSI_ADDRESS_LO, &val);
	ep->pmsi_addr = (ep->pmsi_addr << 32) | val;
	ep->pmsi_irq = pci_irq_vector(ppdev, TEGRA264_PCIE_DMA_MSI_LOCAL_VEC);
	ep->pmsi_data += TEGRA264_PCIE_DMA_MSI_LOCAL_VEC;

	name = devm_kasprintf(&ep->pdev->dev, GFP_KERNEL, "%s_pcie_dma_test", dev_name(&pdev->dev));
	if (!name) {
		dev_err(&pdev->dev, "%s: Fail to set debugfs name\n", __func__);
		ret = -ENOMEM;
		goto fail_name;
	}

	for (i = 0; i < TEGRA_PCIE_DMA_WRITE; i++)
		init_waitqueue_head(&ep->edma.wr_wq[i]);

	ep->debugfs = debugfs_create_dir(name, NULL);
	init_debugfs(ep);

	return ret;

fail_name:
	dma_free_coherent(&pdev->dev, BAR0_SIZE, ep->ep_dma_virt, ep->ep_dma_phy);
fail_ep_dma_alloc:
	dma_free_coherent(&ppdev->dev, BAR0_SIZE, ep->rp_dma_virt, ep->rp_dma_phy);
fail_rp_dma_alloc:
	free_irq(pci_irq_vector(pdev, TEGRA264_PCIE_DMA_MSI_CRC_VEC), ep);
fail_isr:
	pci_free_irq_vectors(pdev);
fail_region_remap:
	pci_release_regions(pdev);
fail_region_request:
	pci_clear_master(pdev);

	return ret;
}

static void ep_test_dma_remove(struct pci_dev *pdev)
{
	struct ep_pvt *ep = pci_get_drvdata(pdev);
	struct pci_dev *ppdev = pcie_find_root_port(pdev);

	debugfs_remove_recursive(ep->debugfs);
	tegra_pcie_dma_deinit(&ep->edma.cookie);
	dma_free_coherent(&pdev->dev, BAR0_SIZE, ep->ep_dma_virt, ep->ep_dma_phy);
	dma_free_coherent(&ppdev->dev, BAR0_SIZE, ep->rp_dma_virt, ep->rp_dma_phy);
	free_irq(pci_irq_vector(pdev, TEGRA264_PCIE_DMA_MSI_CRC_VEC), ep);
	pci_free_irq_vectors(pdev);
	pci_release_regions(pdev);
	pci_clear_master(pdev);
}

static const struct pci_device_id ep_pci_tbl[] = {
	{ PCI_DEVICE(0x10DE, 0x22D7)},
	{ PCI_DEVICE(0x10DE, 0x229B)},
	{},
};

MODULE_DEVICE_TABLE(pci, ep_pci_tbl);

static struct pci_driver ep_pci_driver = {
	.name		= MODULENAME,
	.id_table	= ep_pci_tbl,
	.probe		= ep_test_dma_probe,
	.remove		= ep_test_dma_remove,
};

module_pci_driver(ep_pci_driver);

MODULE_DESCRIPTION("Tegra PCIe client driver for endpoint DMA test func");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Manikanta Maddireddy <mmaddireddy@nvidia.com>");
