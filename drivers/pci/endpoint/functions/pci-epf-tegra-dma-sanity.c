// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of_platform.h>
#include <linux/pci-epc.h>
#include <linux/pci-epf.h>
#include <linux/platform_device.h>
#include <linux/tegra-pcie-dma-sanity-helpers.h>
#include <soc/tegra/fuse-helper.h>
#include "pci-epf-wrapper.h"

struct pcie_epf_dma_sanity_pvt {
	struct pci_epf_header header;
	struct pci_epf *epf;
	struct pci_epc *epc;
	struct device *fdev;
	struct device *cdev;
	void *bar_virt;
	void *bar2_virt;
	struct dentry *debugfs;
	int irq;

	u8 chip_id;
	struct edmalib_sanity edma;
};
static struct pcie_epf_dma_sanity_pvt *gepfnv;

static irqreturn_t pcie_dma_epf_irq(int irq, void *arg)
{
	return IRQ_HANDLED;
}

#if defined(NV_PLATFORM_MSI_DOMAIN_ALLOC_IRQS_PRESENT)
static void pcie_dma_sanity_epf_write_msi_msg(struct msi_desc *desc, struct msi_msg *msg)
{
	if (gepfnv->edma.msi_addr == 0) {
		gepfnv->edma.msi_addr = msg->address_hi;
		gepfnv->edma.msi_addr <<= 32;
		gepfnv->edma.msi_addr |= msg->address_lo;
		/*
		 * First information received is for CRC MSI. So subtract the same to get base and
		 * add WR local vector
		 */
		gepfnv->edma.msi_data = msg->data - TEGRA264_PCIE_DMA_MSI_CRC_VEC +
			TEGRA264_PCIE_DMA_MSI_LOCAL_VEC;
	}
}
#endif
static int pcie_epf_dma_sanity_core_init(struct pci_epf *epf)
{
	struct pcie_epf_dma_sanity_pvt *epfnv = epf_get_drvdata(epf);
	struct pci_epc *epc = epf->epc;
	struct device *fdev = &epf->dev;
	struct pci_epf_bar *epf_bar;
	enum pci_barno bar;
	int ret;

	ret = lpci_epc_write_header(epc, epf->func_no, epf->header);
	if (ret < 0) {
		dev_err(fdev, "Failed to write PCIe header: %d\n", ret);
		return ret;
	}

	if (epfnv->chip_id == TEGRA234)
		bar = BAR_0;
	else
		bar = BAR_1;

	epf_bar = &epf->bar[bar];
	ret = lpci_epc_set_bar(epc, epf->func_no, epf_bar);
	if (ret < 0) {
		dev_err(fdev, "PCIe set BAR0 failed: %d\n", ret);
		return ret;
	}
	if (epfnv->chip_id == TEGRA264) {
		epf_bar = &epf->bar[BAR_2];
		epf_bar->size = SZ_32M;
		epf_bar->flags |= PCI_BASE_ADDRESS_MEM_TYPE_64 | PCI_BASE_ADDRESS_MEM_PREFETCH;
		epf_bar->barno = BAR_2;
		ret = lpci_epc_set_bar(epc, epf->func_no, &epf->bar[BAR_2]);
		if (ret < 0) {
			dev_err(fdev, "PCIe set BAR_2 failed: %d\n", ret);
			return ret;
		}
	}

	dev_dbg(fdev, "BAR0 phy_addr: %llx size: %lx\n", epf_bar->phys_addr, epf_bar->size);

	if (epf->msi_interrupts == 0) {
		dev_err(fdev, "MSI interrupts not set. msi_interrupts = %d\n", epf->msi_interrupts);
		epf->msi_interrupts = 16;
	}

	ret = lpci_epc_set_msi(epc, epf->func_no, epf->msi_interrupts);
	if (ret) {
		dev_err(fdev, "pci_epc_set_msi() failed: %d\n", ret);
		return ret;
	}
	return 0;
}

static int write_sanity_test(struct seq_file *s, void *data)
{
	struct pcie_epf_dma_sanity_pvt *epfnv = (struct pcie_epf_dma_sanity_pvt *)dev_get_drvdata(
			s->private);
	struct pcie_epf_bar *epf_bar = (struct pcie_epf_bar *)epfnv->bar_virt;
	/* Address of the memory local to the ep */
	phys_addr_t dst_pci_addr_on_ep = 0;
	/* Virtual address accessible to CPU */
	void __iomem *dst_va;
	u64 total_size = 12.5 * SZ_1M * 10;
	uint32_t sizes[3] = {0.5 * SZ_1M, 2.5 * SZ_1M, 6.25 * SZ_1M};
	char *sizes_string[3] = {"1 MB", "5 MB", "12.5 MB"};
	int ret, i;

	if (!epf_bar->rp_phy_addr) {
		dev_err(epfnv->fdev, "RP DMA address is NULL\n");
		return -1;
	}

	epfnv->edma.src_dma_addr = epf_bar->ep_phy_addr + BAR0_DMA_BUF_OFFSET;
	epfnv->edma.dst_dma_addr = epf_bar->rp_phy_addr + BAR0_DMA_BUF_OFFSET;
	epfnv->edma.src_virt = epfnv->bar_virt + BAR0_DMA_BUF_OFFSET;
	epfnv->edma.fdev = epfnv->fdev;
	epfnv->edma.cdev = epfnv->cdev;
	epfnv->edma.priv = (void *)epfnv;

	epfnv->edma.stress_count = 10;
	epfnv->edma.nents = 2;
	epfnv->edma.edma_ch_type = TEGRA_PCIE_DMA_CHAN_XFER_ASYNC;

	/* Generating a virtual address of the RP (dst) memory */
	/* Allocating memory at EP */
	dst_va = pci_epc_mem_alloc_addr(epfnv->epc, &dst_pci_addr_on_ep, total_size);
	if (!dst_va) {
		/* Error allocating memory */
		dev_err(epfnv->fdev, "Error allocating memory at the EP\n");
		return -1;
	}
	/* Mapping physical address of the DMA_BUFF to that allocated on the EP */
	if (lpci_epc_map_addr(epfnv->epc, 0, dst_pci_addr_on_ep,
				epfnv->edma.dst_dma_addr, total_size) < 0)
		dev_err(epfnv->fdev, "Mapping EP allocated memory to the RP failed\n");
	epfnv->edma.dst_virt = ((u64 *)dst_va);

	/* Begins testing */
	for (i = 0; i < ARRAY_SIZE(sizes); i++) {
		dev_info(epfnv->fdev, "%s ----- TESTING WITH TRANSFER SIZE %s -----\n",
				__func__, sizes_string[i]);
		epfnv->edma.dma_size = sizes[i];
		if (epfnv->edma.dma_size == (12.5 * SZ_1M))
			epfnv->edma.deinit_dma = true;
		ret = edmalib_sanity_tester(&epfnv->edma);
		if (ret) {
			dev_err(epfnv->edma.fdev, "%s: Failed at total transfer size = %s| ERROR: %d\n",
					__func__, sizes_string[i], ret);
			return ret;
		}
	}

	lpci_epc_unmap_addr(epfnv->epc, 0, dst_pci_addr_on_ep);
	pci_epc_mem_free_addr(epfnv->epc, dst_pci_addr_on_ep, dst_va, total_size);

	return 0;
}
static void init_debugfs_sanity(struct pcie_epf_dma_sanity_pvt *epfnv)
{
	debugfs_create_devm_seqfile(epfnv->fdev, "write_sanity_test", epfnv->debugfs,
			write_sanity_test);
}
static int pcie_epf_dma_sanity_bind(struct pci_epf *epf)
{
	const struct pci_epc_features *epc_features;
	struct pci_epc *epc = epf->epc;
	struct pcie_epf_dma_sanity_pvt *epfnv = epf_get_drvdata(epf);
	struct device *fdev = &epf->dev;
	struct device *cdev = epc->dev.parent;
	struct platform_device *pdev = of_find_device_by_node(cdev->of_node);
	struct pcie_epf_bar *epf_bar_virt;
	struct pci_epf_bar *epf_bar;
	struct irq_domain *domain;
	enum pci_barno bar;
	char *name;
	int ret;
	u32 irq;
#if !defined(NV_MSI_GET_VIRQ_PRESENT)
	struct msi_desc *desc;
#endif

	epfnv->chip_id = __tegra_get_chip_id();
	if (epfnv->chip_id == TEGRA234) {
		bar = BAR_0;
		epfnv->edma.chip_id = NVPCIE_DMA_SOC_T234;
		epfnv->header.deviceid = 0x22D8;
	} else {
		bar = BAR_1;
		epfnv->edma.chip_id = NVPCIE_DMA_SOC_T264;
	}
	epf_bar = &epf->bar[bar];
	epfnv->fdev = fdev;
	epfnv->cdev = cdev;
	epfnv->epf = epf;
	epfnv->epc = epc;

	epc_features = pci_epc_get_features(epc, epf->func_no, epf->vfunc_no);
	if (!epc_features) {
		dev_err(fdev, "Failed to get endpoint features!\n");
		return -EINVAL;
	}

#if defined(NV_PCI_EPF_ALLOC_SPACE_HAS_EPC_FEATURES_ARG)
	epfnv->bar_virt = lpci_epf_alloc_space(epf, BAR0_SIZE, bar, epc_features);
	if (epfnv->chip_id == TEGRA264)
		lpci_epf_alloc_space(epf, SZ_32M, BAR_2, epc_features);
#else
	epfnv->bar_virt = lpci_epf_alloc_space(epf, BAR0_SIZE, bar, epc_features->align);
	if (epfnv->chip_id == TEGRA264)
		epfnv->bar2_virt = lpci_epf_alloc_space(epf, SZ_32M, BAR_2, epc_features->align);
#endif

	if (!epfnv->bar_virt) {
		dev_err(fdev, "Failed to allocate memory for BAR0\n");
		return -ENOMEM;
	}
	memset(epfnv->bar_virt, 0, BAR0_HEADER_SIZE);

	/* Update BAR header with EP DMA PHY addr */
	epf_bar_virt = (struct pcie_epf_bar *)epfnv->bar_virt;
	epf_bar_virt->ep_phy_addr = epf_bar->phys_addr;
	/* Set BAR0 mem type as 64-bit */
	epf_bar->flags |= PCI_BASE_ADDRESS_MEM_TYPE_64 | PCI_BASE_ADDRESS_MEM_PREFETCH;

	/* Creating the debugfs */
	name = devm_kasprintf(fdev, GFP_KERNEL, "%s_epf_dma_sanity_test", pdev->name);
	if (!name) {
		ret = -ENOMEM;
		goto fail_atu_dma;
	}

	epfnv->debugfs = debugfs_create_dir(name, NULL);
	init_debugfs_sanity(epfnv);

	if (epfnv->chip_id == TEGRA264) {
		domain = dev_get_msi_domain(&pdev->dev);
		if (!domain) {
			dev_err(fdev, "failed to get MSI domain\n");
			ret = -ENOMEM;
			goto fail_kasnprintf;
		}
#if defined(NV_PLATFORM_MSI_DOMAIN_ALLOC_IRQS_PRESENT)
		ret = platform_msi_domain_alloc_irqs(&pdev->dev, 8,
				pcie_dma_sanity_epf_write_msi_msg);
		if (ret < 0) {
			dev_err(fdev, "failed to allocate MSIs: %d\n", ret);
			goto fail_kasnprintf;
		}
#endif
#if defined(NV_MSI_GET_VIRQ_PRESENT)
		epfnv->edma.msi_irq = msi_get_virq(&pdev->dev, TEGRA264_PCIE_DMA_MSI_LOCAL_VEC);
		irq = msi_get_virq(&pdev->dev, TEGRA264_PCIE_DMA_MSI_CRC_VEC);
#else
		for_each_msi_entry(desc, cdev) {
			if (desc->platform.msi_index == TEGRA264_PCIE_DMA_MSI_CRC_VEC)
				irq = desc->irq;
			else if (desc->platform.msi_index == TEGRA264_PCIE_DMA_MSI_LOCAL_VEC)
				epfnv->edma.msi_irq = desc->irq;
		}
#endif
		dev_dbg(fdev, "DMA IRQ no: %d | IRQ defined\n", epfnv->edma.msi_irq);
		ret = request_irq(irq, pcie_dma_epf_irq, IRQF_SHARED, "pcie_dma_epf_isr", epfnv);
		if (ret < 0) {
			dev_err(fdev, "failed to request irq: %d\n", ret);
			goto fail_msi_alloc;
		}
		dev_dbg(fdev, "Request IRQ succeeded\n");
		gepfnv->irq = irq;
	}

	epc_features = pci_epc_get_features(epc, epf->func_no, epf->vfunc_no);
	if (!epc_features) {
		dev_err(fdev, "epc_features not implemented\n");
		ret = -EOPNOTSUPP;
		goto fail_get_features;
	}

#if defined(NV_PCI_EPC_FEATURES_STRUCT_HAS_CORE_INIT_NOTIFIER)
	if (!epc_features->core_init_notifier) {
		ret = pcie_epf_dma_sanity_core_init(epf);
		if (ret) {
			dev_err(fdev, "EPF core init failed with err: %d\n", ret);
			goto fail_get_features;
		}
	}
#endif

	return 0;

fail_get_features:
	debugfs_remove_recursive(epfnv->debugfs);
	if (epfnv->chip_id == TEGRA264)
		free_irq(irq, epfnv);
fail_msi_alloc:
#if defined(NV_PLATFORM_MSI_DOMAIN_FREE_IRQS_PRESENT) /* Linux v6.9 */
	if (epfnv->chip_id == TEGRA264)
		platform_msi_domain_free_irqs(&pdev->dev);
#endif
fail_kasnprintf:
	devm_kfree(fdev, name);
fail_atu_dma:
	lpci_epf_free_space(epf, epfnv->bar2_virt, bar);
	lpci_epf_free_space(epf, epfnv->bar_virt, bar);

	return ret;
}
static void pcie_epf_dma_sanity_unbind(struct pci_epf *epf)
{
	struct pcie_epf_dma_sanity_pvt *epfnv = epf_get_drvdata(epf);
	struct pci_epc *epc = epf->epc;
	void *cookie = epfnv->edma.cookie;
	struct pcie_epf_bar *epf_bar = (struct pcie_epf_bar *)epfnv->bar_virt;
	enum pci_barno bar;
	u32 irq = gepfnv->irq;

	debugfs_remove_recursive(epfnv->debugfs);

	epfnv->edma.cookie = NULL;
	epf_bar->rp_phy_addr = 0;
	tegra_pcie_dma_deinit(&cookie);
	if (epfnv->chip_id == TEGRA264)
		free_irq(irq, epfnv);
	pci_epc_stop(epc);
	if (epfnv->chip_id == TEGRA234)
		bar = BAR_0;
	else
		bar = BAR_1;
	lpci_epf_free_space(epf, epfnv->bar_virt, bar);
}


static const struct pci_epf_device_id pcie_dma_epf_sanity_ids[] = {
	{
		.name = "tegra_pcie_epf_dma",
	},
	{},
};

static const struct pci_epc_event_ops pci_epf_dma_sanity_event_ops = {
	.core_init = pcie_epf_dma_sanity_core_init,
};

static struct pci_epf_ops ops = {
	.unbind	= pcie_epf_dma_sanity_unbind,
	.bind	= pcie_epf_dma_sanity_bind,
};
#if defined(NV_PCI_EPF_DRIVER_STRUCT_PROBE_HAS_ID_ARG) /* linux-rhivos-1, nvmainline, android-v*/
static int pcie_epf_tegra_dma_sanity_probe(struct pci_epf *epf, const struct pci_epf_device_id *id)
#else
static int pcie_epf_tegra_dma_sanity_probe(struct pci_epf *epf)
#endif
{
	struct device *dev = &epf->dev;
	struct pcie_epf_dma_sanity_pvt *epfnv;
	/* Allocating memory for the EPF */
	epfnv = devm_kzalloc(dev, sizeof(*epfnv), GFP_KERNEL);
	if (!epfnv)
		return -ENOMEM;
	/* Allocating the Linked-List of the descriptors*/
	epfnv->edma.ll_desc = devm_kzalloc(dev, sizeof(*epfnv->edma.ll_desc) * NUM_EDMA_DESC,
			GFP_KERNEL);
	epf_set_drvdata(epf, epfnv);
	gepfnv = epfnv;
	/* Bind and unbind operations*/
	epf->event_ops = &pci_epf_dma_sanity_event_ops;
	/* Device Header */
	epfnv->header.vendorid = PCI_VENDOR_ID_NVIDIA;
	epfnv->header.deviceid = 0x229C;
	epfnv->header.subsys_vendor_id = PCI_VENDOR_ID_NVIDIA;
	epfnv->header.baseclass_code = PCI_BASE_CLASS_MEMORY;
	epfnv->header.interrupt_pin = PCI_INTERRUPT_INTA;
	epf->msi_interrupts = 16;
	epf->header = &epfnv->header;

	return 0;
}

static struct pci_epf_driver pcie_epf_tegra_dma_sanity = {
	.driver.name = "pcie_epf_tegra_dma_sanity",
	.probe = pcie_epf_tegra_dma_sanity_probe,
	.id_table = pcie_dma_epf_sanity_ids,
	.ops = &ops,
	.owner = THIS_MODULE
};

static int __init pcie_epf_dma_sanity_init(void)
{
	int ret;

	ret = pci_epf_register_driver(&pcie_epf_tegra_dma_sanity);
	if (ret < 0) {
		pr_err("Failed to register PCIe DMA EPF sanity test driver. Error code: %d\n", ret);
		return ret;
	}
	return 0;
}
static void __exit pcie_epf_dma_sanity_exit(void)
{
	pci_epf_unregister_driver(&pcie_epf_tegra_dma_sanity);
}

/* Configuring init and exit functions */
module_init(pcie_epf_dma_sanity_init);
module_exit(pcie_epf_dma_sanity_exit);

/* Module parameters */
MODULE_DESCRIPTION("TEGRA PCIe DMA EPF sanity test driver");
MODULE_AUTHOR("Srishti Goel <srgoel@nvidia.com");
MODULE_LICENSE("GPL");
