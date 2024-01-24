// SPDX-License-Identifier: GPL-2.0+
/*
 * PCIe DMA EPF test framework for Tegra PCIe
 *
 * Copyright (C) 2021-2024 NVIDIA Corporation. All rights reserved.
 */

#include <nvidia/conftest.h>

#include <linux/crc32.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of_platform.h>
#include <linux/pci-epc.h>
#include <linux/pci-epf.h>
#include <linux/pcie_dma.h>
#include <linux/platform_device.h>
#include <linux/kthread.h>
#include <linux/tegra-pcie-edma-test-common.h>
#include "pci-epf-wrapper.h"

static struct pcie_epf_dma *gepfnv;

struct pcie_epf_dma {
	struct pci_epf_header header;
	struct pci_epf *epf;
	struct pci_epc *epc;
	struct device *fdev;
	struct device *cdev;
	void *bar0_virt;
	struct dentry *debugfs;
	void __iomem *dma_base;
	int irq;

	u32 dma_size;
	u32 stress_count;
	u32 async_count;

	struct task_struct *wr0_task;
	struct task_struct *wr1_task;
	struct task_struct *wr2_task;
	struct task_struct *wr3_task;
	struct task_struct *rd0_task;
	struct task_struct *rd1_task;
	u8 task_done;
	wait_queue_head_t task_wq;
	void *cookie;

	wait_queue_head_t wr_wq[DMA_WR_CHNL_NUM];
	wait_queue_head_t rd_wq[DMA_RD_CHNL_NUM];
	unsigned long wr_busy;
	unsigned long rd_busy;
	ktime_t wr_start_time[DMA_WR_CHNL_NUM];
	ktime_t wr_end_time[DMA_WR_CHNL_NUM];
	ktime_t rd_start_time[DMA_RD_CHNL_NUM];
	ktime_t rd_end_time[DMA_RD_CHNL_NUM];
	u32 wr_cnt[DMA_WR_CHNL_NUM + DMA_RD_CHNL_NUM];
	u32 rd_cnt[DMA_WR_CHNL_NUM + DMA_RD_CHNL_NUM];
	bool pcs[DMA_WR_CHNL_NUM + DMA_RD_CHNL_NUM];
	bool async_dma;
	ktime_t edma_start_time[DMA_WR_CHNL_NUM];
	u64 tsz;
	u32 edma_ch;
	u32 prev_edma_ch;
	u32 nents;
	struct tegra_pcie_edma_desc *ll_desc;
	struct edmalib_common edma;
};

static void edma_lib_test_raise_irq(void *p)
{
	struct pcie_epf_dma *epfnv = (struct pcie_epf_dma *)p;

#if defined(PCI_EPC_IRQ_TYPE_ENUM_PRESENT) /* Dropped from Linux 6.8 */
	lpci_epc_raise_irq(epfnv->epc, epfnv->epf->func_no, PCI_EPC_IRQ_MSI, 1);
#else
	lpci_epc_raise_irq(epfnv->epc, epfnv->epf->func_no, PCI_IRQ_MSI, 1);
#endif
}

/* debugfs to perform eDMA lib transfers and do CRC check */
static int edmalib_test(struct seq_file *s, void *data)
{
	struct pcie_epf_dma *epfnv = (struct pcie_epf_dma *)
						dev_get_drvdata(s->private);
	struct pcie_epf_bar0 *epf_bar0 = (struct pcie_epf_bar0 *)
						epfnv->bar0_virt;

	if (!epf_bar0->rp_phy_addr) {
		dev_err(epfnv->fdev, "RP DMA address is null\n");
		return -1;
	}

	epfnv->edma.src_dma_addr = epf_bar0->ep_phy_addr + BAR0_DMA_BUF_OFFSET;
	epfnv->edma.dst_dma_addr = epf_bar0->rp_phy_addr + BAR0_DMA_BUF_OFFSET;
	epfnv->edma.fdev = epfnv->fdev;
	epfnv->edma.epf_bar0 = (struct pcie_epf_bar0 *)epfnv->bar0_virt;
	epfnv->edma.src_virt = epfnv->bar0_virt + BAR0_DMA_BUF_OFFSET;
	epfnv->edma.dma_base = epfnv->dma_base;
	epfnv->edma.dma_size = epfnv->dma_size;
	epfnv->edma.stress_count = epfnv->stress_count;
	epfnv->edma.edma_ch = epfnv->edma_ch;
	epfnv->edma.nents = epfnv->nents;
	epfnv->edma.of_node = epfnv->cdev->of_node;
	epfnv->edma.priv = (void *)epfnv;
	epfnv->edma.raise_irq = edma_lib_test_raise_irq;

	return edmalib_common_test(&epfnv->edma);
}

static void init_debugfs(struct pcie_epf_dma *epfnv)
{
	debugfs_create_devm_seqfile(epfnv->fdev, "edmalib_test", epfnv->debugfs,
				    edmalib_test);

	debugfs_create_u32("dma_size", 0644, epfnv->debugfs, &epfnv->dma_size);
	epfnv->dma_size = SZ_1M;
	epfnv->edma.st_as_ch = -1;

	debugfs_create_u32("edma_ch", 0644, epfnv->debugfs, &epfnv->edma_ch);
	/* Enable ASYNC for ch 0 as default and other channels. Usage:
	 * BITS 0-3 - Async mode or sync mode for corresponding WR channels
	 * BITS 4-7 - Enable/disable corresponding WR channel
	 * BITS 8-9 - Async mode or sync mode for corresponding RD channels
	 * BITS 10-11 - Enable/disable or corresponding RD channels
	 * Bit 12 - Abort testing.
	 */
	epfnv->edma_ch = 0xF1;

	debugfs_create_u32("nents", 0644, epfnv->debugfs, &epfnv->nents);
	/* Set DMA_LL_DEFAULT_SIZE as default nents, Max NUM_EDMA_DESC */
	epfnv->nents = DMA_LL_DEFAULT_SIZE;

	debugfs_create_u32("stress_count", 0644, epfnv->debugfs,
			   &epfnv->stress_count);
	epfnv->stress_count = DEFAULT_STRESS_COUNT;
}

static int pcie_dma_epf_core_init(struct pci_epf *epf)
{
	struct pci_epc *epc = epf->epc;
	struct device *fdev = &epf->dev;
	struct pci_epf_bar *epf_bar;
	int ret;

	ret = lpci_epc_write_header(epc, epf->func_no, epf->header);
	if (ret < 0) {
		dev_err(fdev, "Failed to write PCIe header: %d\n", ret);
		return ret;
	}

	epf_bar = &epf->bar[BAR_0];
	ret = lpci_epc_set_bar(epc, epf->func_no, epf_bar);
	if (ret < 0) {
		dev_err(fdev, "PCIe set BAR0 failed: %d\n", ret);
		return ret;
	}

	dev_info(fdev, "BAR0 phy_addr: %llx size: %lx\n",
		 epf_bar->phys_addr, epf_bar->size);

	ret = lpci_epc_set_msi(epc, epf->func_no, epf->msi_interrupts);
	if (ret) {
		dev_err(fdev, "pci_epc_set_msi() failed: %d\n", ret);
		return ret;
	}

	return 0;
}

#if defined(NV_PCI_EPC_EVENT_OPS_STRUCT_HAS_CORE_DEINIT) /* Nvidia Internal */
static int pcie_dma_epf_core_deinit(struct pci_epf *epf)
{
	struct pcie_epf_dma *epfnv = epf_get_drvdata(epf);
	void *cookie = epfnv->edma.cookie;
	struct pcie_epf_bar0 *epf_bar0 = (struct pcie_epf_bar0 *) epfnv->bar0_virt;
	struct pci_epc *epc = epf->epc;
	struct pci_epf_bar *epf_bar = &epf->bar[BAR_0];

	epfnv->edma.cookie = NULL;
	epf_bar0->rp_phy_addr = 0;
	tegra_pcie_edma_deinit(cookie);
	lpci_epc_clear_bar(epc, epf->func_no, epf_bar);

	return 0;
}
#endif

static void pcie_dma_epf_unbind(struct pci_epf *epf)
{
	struct pcie_epf_dma *epfnv = epf_get_drvdata(epf);
	struct pci_epc *epc = epf->epc;
	void *cookie = epfnv->edma.cookie;
	struct pcie_epf_bar0 *epf_bar0 = (struct pcie_epf_bar0 *) epfnv->bar0_virt;

	debugfs_remove_recursive(epfnv->debugfs);

	epfnv->edma.cookie = NULL;
	epf_bar0->rp_phy_addr = 0;
	tegra_pcie_edma_deinit(cookie);

	pci_epc_stop(epc);
	lpci_epf_free_space(epf, epfnv->bar0_virt, BAR_0);
}

static int pcie_dma_epf_bind(struct pci_epf *epf)
{
	struct pci_epc *epc = epf->epc;
	struct pcie_epf_dma *epfnv = epf_get_drvdata(epf);
	struct device *fdev = &epf->dev;
	struct device *cdev = epc->dev.parent;
	struct platform_device *pdev = of_find_device_by_node(cdev->of_node);
	struct pci_epf_bar *epf_bar = &epf->bar[BAR_0];
	struct pcie_epf_bar0 *epf_bar0;
	char *name;
	int ret, i;

	epfnv->fdev = fdev;
	epfnv->cdev = cdev;
	epfnv->epf = epf;
	epfnv->epc = epc;

	epfnv->bar0_virt = lpci_epf_alloc_space(epf, BAR0_SIZE, BAR_0, SZ_64K);
	if (!epfnv->bar0_virt) {
		dev_err(fdev, "Failed to allocate memory for BAR0\n");
		return -ENOMEM;
	}
	get_random_bytes(epfnv->bar0_virt, BAR0_SIZE);
	memset(epfnv->bar0_virt, 0, BAR0_HEADER_SIZE);

	/* Update BAR header with EP DMA PHY addr */
	epf_bar0 = (struct pcie_epf_bar0 *)epfnv->bar0_virt;
	epf_bar0->ep_phy_addr = epf_bar->phys_addr;
	/* Set BAR0 mem type as 64-bit */
	epf_bar->flags |= PCI_BASE_ADDRESS_MEM_TYPE_64 |
			PCI_BASE_ADDRESS_MEM_PREFETCH;

	name = devm_kasprintf(fdev, GFP_KERNEL, "%s_epf_dma_test", pdev->name);
	if (!name) {
		ret = -ENOMEM;
		goto fail_atu_dma;
	}

	for (i = 0; i < DMA_WR_CHNL_NUM; i++) {
		init_waitqueue_head(&epfnv->edma.wr_wq[i]);
	}

	for (i = 0; i < DMA_RD_CHNL_NUM; i++) {
		init_waitqueue_head(&epfnv->edma.rd_wq[i]);
	}

	epfnv->debugfs = debugfs_create_dir(name, NULL);
	init_debugfs(epfnv);

	return 0;

fail_atu_dma:
	lpci_epf_free_space(epf, epfnv->bar0_virt, BAR_0);

	return ret;
}

static const struct pci_epf_device_id pcie_dma_epf_ids[] = {
	{
		.name = "tegra_pcie_dma_epf",
	},
	{},
};

static const struct pci_epc_event_ops pci_epf_dma_test_event_ops = {
	.core_init = pcie_dma_epf_core_init,
#if defined(NV_PCI_EPC_EVENT_OPS_STRUCT_HAS_CORE_DEINIT) /* Nvidia Internal */
	.core_deinit = pcie_dma_epf_core_deinit,
#endif
};

#if defined(NV_PCI_EPF_DRIVER_STRUCT_PROBE_HAS_ID_ARG) /* Linux 6.4 */
static int pcie_dma_epf_probe(struct pci_epf *epf, const struct pci_epf_device_id *id)
#else
static int pcie_dma_epf_probe(struct pci_epf *epf)
#endif
{
	struct device *dev = &epf->dev;
	struct pcie_epf_dma *epfnv;

	epfnv = devm_kzalloc(dev, sizeof(*epfnv), GFP_KERNEL);
	if (!epfnv)
		return -ENOMEM;

	epfnv->edma.ll_desc = devm_kzalloc(dev, sizeof(*epfnv->ll_desc) * NUM_EDMA_DESC,
					   GFP_KERNEL);
	gepfnv = epfnv;
	epf_set_drvdata(epf, epfnv);

	epf->event_ops = &pci_epf_dma_test_event_ops;

	epfnv->header.vendorid = PCI_VENDOR_ID_NVIDIA;
	epfnv->header.deviceid = 0x1AD6;
	epfnv->header.baseclass_code = PCI_BASE_CLASS_MEMORY;
	epfnv->header.interrupt_pin = PCI_INTERRUPT_INTA;
	epf->header = &epfnv->header;

	return 0;
}

static struct pci_epf_ops ops = {
	.unbind	= pcie_dma_epf_unbind,
	.bind	= pcie_dma_epf_bind,
};

static struct pci_epf_driver test_driver = {
	.driver.name	= "pcie_dma_epf",
	.probe		= pcie_dma_epf_probe,
	.id_table	= pcie_dma_epf_ids,
	.ops		= &ops,
	.owner		= THIS_MODULE,
};

static int __init pcie_dma_epf_init(void)
{
	int ret;

	ret = pci_epf_register_driver(&test_driver);
	if (ret < 0) {
		pr_err("Failed to register PCIe DMA EPF driver: %d\n", ret);
		return ret;
	}

	return 0;
}
module_init(pcie_dma_epf_init);

static void __exit pcie_dma_epf_exit(void)
{
	pci_epf_unregister_driver(&test_driver);
}
module_exit(pcie_dma_epf_exit);

MODULE_DESCRIPTION("TEGRA PCIe DMA EPF driver");
MODULE_AUTHOR("Om Prakash Singh <omp@nvidia.com>");
MODULE_LICENSE("GPL v2");
