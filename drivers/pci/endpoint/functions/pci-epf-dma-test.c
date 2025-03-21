// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2021-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * PCIe DMA EPF test framework for Tegra PCIe.
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
#include <linux/platform_device.h>
#include <linux/tegra-pcie-edma-test-common.h>
#include <linux/version.h>
#include <soc/tegra/fuse-helper.h>
#include "pci-epf-wrapper.h"

static struct pcie_epf_dma *gepfnv;

struct pcie_epf_dma {
	struct pci_epf_header header;
	struct pci_epf *epf;
	struct pci_epc *epc;
	struct device *fdev;
	struct device *cdev;
	void *bar_virt;
	struct dentry *debugfs;
	void __iomem *dma_virt;
	int irq;

	u8 chip_id;

	u32 dma_size;
	u32 stress_count;
	u32 async_count;

	u64 tsz;
	u32 edma_ch;
	u32 prev_edma_ch;
	u32 nents;
	struct edmalib_common edma;
};

static void edma_lib_test_raise_irq(void *p)
{
	struct pcie_epf_dma *epfnv = (struct pcie_epf_dma *)p;
	struct pcie_epf_bar *epf_bar = (__force struct pcie_epf_bar *)epfnv->bar_virt;
	struct sanity_data *wr_data = &epf_bar->wr_data[0];
	u64 *data = (u64 *)(epfnv->edma.src_virt + wr_data->src_offset);

	dev_info(epfnv->fdev, "%s: wr_data size(0x%x), offset(%d). data[0]=0x%llx, data[size-1]=0x%llx\n",
		 __func__, wr_data->size, wr_data->dst_offset, data[0],
		 data[(wr_data->size/8) - 1u]);

#if defined(PCI_EPC_IRQ_TYPE_ENUM_PRESENT) /* Dropped from Linux 6.8 */
	lpci_epc_raise_irq(epfnv->epc, epfnv->epf->func_no, PCI_EPC_IRQ_MSI,
			   TEGRA264_PCIE_DMA_MSI_CRC_VEC);
#else
	lpci_epc_raise_irq(epfnv->epc, epfnv->epf->func_no, PCI_IRQ_MSI,
			   TEGRA264_PCIE_DMA_MSI_CRC_VEC);
#endif
}

/* debugfs to perform eDMA lib transfers and do CRC check */
static int edmalib_test(struct seq_file *s, void *data)
{
	struct pcie_epf_dma *epfnv = (struct pcie_epf_dma *)dev_get_drvdata(s->private);
	struct pcie_epf_bar *epf_bar = (struct pcie_epf_bar *)epfnv->bar_virt;

	if (!epf_bar->rp_phy_addr) {
		dev_err(epfnv->fdev, "RP DMA address is null\n");
		return -1;
	}

	epfnv->edma.src_dma_addr = epf_bar->ep_phy_addr + BAR0_DMA_BUF_OFFSET;
	epfnv->edma.dst_dma_addr = epf_bar->rp_phy_addr + BAR0_DMA_BUF_OFFSET;
	epfnv->edma.fdev = epfnv->fdev;
	epfnv->edma.cdev = epfnv->cdev;
	epfnv->edma.epf_bar = (struct pcie_epf_bar *)epfnv->bar_virt;
	epfnv->edma.src_virt = epfnv->bar_virt + BAR0_DMA_BUF_OFFSET;
	epfnv->edma.dma_virt = epfnv->dma_virt;
	epfnv->edma.dma_size = epfnv->dma_size;
	epfnv->edma.stress_count = epfnv->stress_count;
	epfnv->edma.edma_ch = epfnv->edma_ch;
	epfnv->edma.nents = epfnv->nents;
	epfnv->edma.priv = (void *)epfnv;
	epfnv->edma.raise_irq = edma_lib_test_raise_irq;

	return edmalib_common_test(&epfnv->edma);
}

/* debugfs to perform EP outbound access */
static int edmalib_test_ob(struct seq_file *s, void *data)
{
	struct pcie_epf_dma *epfnv = (struct pcie_epf_dma *)dev_get_drvdata(s->private);
	struct pcie_epf_bar *epf_bar = (struct pcie_epf_bar *)epfnv->bar_virt;
	struct pci_epc *epc = epfnv->epc;
	void __iomem *dst_va[10] = { 0 };
	phys_addr_t dst_pci_addr[10] = { 0 };
	int i, ret;

	if (!epf_bar->rp_phy_addr) {
		dev_err(epfnv->fdev, "RP DMA address is null\n");
		return -1;
	}

	for (i = 0; i < 10; i++) {
		dst_va[i] = pci_epc_mem_alloc_addr(epc, &dst_pci_addr[i], SZ_64K);
		if (!dst_va[i]) {
			dev_err(epfnv->fdev, "failed to allocate dst PCIe address, ob ch: %u\n", i);
			break;
		}
		dev_dbg(epfnv->fdev, "Mapping %llx(EP) to %llx(RP) for size SZ_64K, ob ch: %d\n",
			 dst_pci_addr[i], epf_bar->rp_phy_addr, i);

		if (lpci_epc_map_addr(epc, 0, dst_pci_addr[i], epf_bar->rp_phy_addr, SZ_64K) < 0) {
			dev_err(epfnv->fdev, "failed to map rp_phy_addr, ob ch: %d\n", i);
			break;
		}
		if (*((u64 *) ((u8 *)dst_va[i] + PCIE_EP_OB_OFFSET)) != PCIE_EP_OB_MAGIC) {
			dev_err(epfnv->fdev, "magic number: %llx is not matching, ob ch: %d\n",
				*((u64 *) ((u8 *)dst_va[i] + PCIE_EP_OB_OFFSET)), i);
			break;
		}
		if (i == 0)
			dev_info(epfnv->fdev, "condition -EP OB validation- Passed\n");

	}

	if (i == 8) {
		dev_info(epfnv->fdev, "condition -All outbound channel mapping- Passed\n");
		ret = 0;
	} else {
		dev_info(epfnv->fdev, "condition -All outbound channel mapping- Failed at ch: %d\n", i);
		if (i == 0)
			dev_info(epfnv->fdev, "condition -EP OB validation- Failed\n");
		ret = -1;
	}

	while (i >= 0) {
		if (dst_pci_addr[i])
			lpci_epc_unmap_addr(epc, 0, dst_pci_addr[i]);
		if (dst_va[i])
			pci_epc_mem_free_addr(epc, dst_pci_addr[i], dst_va[i], SZ_64K);
		if (i == 1)
			dev_info(epfnv->fdev, "condition -Clear outbound mapping- Passed\n");
		i--;
	}

	return ret;
}

static void validate_api_status(char *api, char *cond, tegra_pcie_dma_status_t act,
				tegra_pcie_dma_status_t exp)
{
	if (act == exp)
		pr_info("DMA test condition -%s:%s- Passed with expected val %d\n", api, cond, act);
	else
		pr_err("DMA test condition -%s:%s- Failed actual %d expected %d\n", api, cond, act, exp);
}

static void sanitize_dma_info(struct tegra_pcie_dma_init_info *info, struct pcie_epf_dma *epfnv)
{
	info->soc = NVPCIE_DMA_SOC_T264;
	info->tx[0].ch_type = TEGRA_PCIE_DMA_CHAN_XFER_ASYNC;
	info->tx[0].num_descriptors = 4096;
	info->dev = epfnv->cdev;
	info->msi_irq = epfnv->edma.msi_irq;
}

static void sanitize_dma_xfer(struct tegra_pcie_dma_xfer_info *xfer,
			      struct tegra_pcie_dma_desc *desc)
{
	xfer->channel_num = 0;
	xfer->complete = edma_complete;
	xfer->nents = 100;
	xfer->desc = desc;
}

static int dma_neg_test(struct seq_file *s, void *data)
{
	struct pcie_epf_dma *epfnv = (struct pcie_epf_dma *)dev_get_drvdata(s->private);
	struct pcie_epf_bar *epf_bar = (struct pcie_epf_bar *)epfnv->bar_virt;
	struct tegra_pcie_dma_init_info info = {};
	tegra_pcie_dma_status_t status;
	struct tegra_pcie_dma_xfer_info xfer = {};
	struct tegra_pcie_dma_desc desc = {};
	void *prv;

	if ((epfnv->chip_id == TEGRA234) || (!epf_bar->rp_phy_addr)) {
		dev_err(epfnv->fdev, "%s: Not supported for T234 or Link is not up\n", __func__);
		return -EOPNOTSUPP;
	}

	sanitize_dma_info(&info, epfnv);
	status = tegra_pcie_dma_initialize(NULL, &prv);
	validate_api_status("tegra_pcie_dma_initialize", "init NULL", status,
			    TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS);

	sanitize_dma_info(&info, epfnv);
	info.tx[0].num_descriptors = 0;
	status = tegra_pcie_dma_initialize(&info, &prv);
	validate_api_status("tegra_pcie_dma_initialize", "No channel enabled", status,
			    TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS);

	sanitize_dma_info(&info, epfnv);
	info.tx[0].num_descriptors = 15;
	status = tegra_pcie_dma_initialize(&info, &prv);
	validate_api_status("tegra_pcie_dma_initialize", "Descriptors not power of 2", status,
			    TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS);

	sanitize_dma_info(&info, epfnv);
	info.tx[0].num_descriptors = 0x80000000;
	status = tegra_pcie_dma_initialize(&info, &prv);
	validate_api_status("tegra_pcie_dma_initialize", "Max num descriptor check", status,
			    TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS);

	sanitize_dma_info(&info, epfnv);
	status = tegra_pcie_dma_initialize(&info, NULL);
	validate_api_status("tegra_pcie_dma_initialize", "Cookie is NULL", status,
			    TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS);

	sanitize_dma_info(&info, epfnv);
	info.dev = NULL;
	status = tegra_pcie_dma_initialize(&info, &prv);
	validate_api_status("tegra_pcie_dma_initialize", "dev param is NULL", status,
			    TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS);

	sanitize_dma_info(&info, epfnv);
	info.soc = 0xFF; /* Invalid SoC type */
	status = tegra_pcie_dma_initialize(&info, &prv);
	validate_api_status("tegra_pcie_dma_initialize", "Invalid SoC type", status,
			    TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS);

	sanitize_dma_info(&info, epfnv);
	status = tegra_pcie_dma_initialize(&info, &prv);
	validate_api_status("tegra_pcie_dma_initialize", "All valid params", status,
			    TEGRA_PCIE_DMA_SUCCESS);

	status = tegra_pcie_dma_set_msi(NULL, 0, 0);
	validate_api_status("tegra_pcie_dma_set_msi", "NULL cookie", status,
			    TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS);

	sanitize_dma_xfer(&xfer, &desc);
	xfer.channel_num = 1;
	status = tegra_pcie_dma_submit_xfer(prv, &xfer);
	validate_api_status("tegra_pcie_dma_submit_xfer", "Invalid channel", status,
			    TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS);

	sanitize_dma_xfer(&xfer, &desc);
	xfer.complete = NULL;
	status = tegra_pcie_dma_submit_xfer(prv, &xfer);
	validate_api_status("tegra_pcie_dma_submit_xfer", "NULL complete", status,
			    TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS);

	sanitize_dma_xfer(&xfer, &desc);
	xfer.nents = 0;
	status = tegra_pcie_dma_submit_xfer(prv, &xfer);
	validate_api_status("tegra_pcie_dma_submit_xfer", "no nents", status,
			    TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS);

	sanitize_dma_xfer(&xfer, &desc);
	status = tegra_pcie_dma_submit_xfer(NULL, &xfer);
	validate_api_status("tegra_pcie_dma_submit_xfer", "NULL cookie", status,
			    TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS);

	status = tegra_pcie_dma_submit_xfer(prv, NULL);
	validate_api_status("tegra_pcie_dma_submit_xfer", "NULL xfer", status,
			    TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS);

	status = tegra_pcie_dma_stop(NULL);
	validate_api_status("tegra_pcie_dma_stop", "NULL cookie", status,
			    (tegra_pcie_dma_status_t)false);

	status = tegra_pcie_dma_deinit(NULL);
	validate_api_status("tegra_pcie_dma_deinit", "NULL cookie", status,
			    TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS);

	status = tegra_pcie_dma_stop(prv);
	validate_api_status("tegra_pcie_dma_stop", "Valid param", status,
			    (tegra_pcie_dma_status_t)true);

	status = tegra_pcie_dma_deinit(&prv);
	validate_api_status("tegra_pcie_dma_deinit", "Valid param", status, TEGRA_PCIE_DMA_SUCCESS);

	return 0;
}

static void validate_bar(struct pcie_epf_dma *epfnv, struct pci_epf_bar *bar, char *err_str)
{
	int ret = lpci_epc_set_bar(epfnv->epc, epfnv->epf->func_no, bar);

	if (ret < 0)
		dev_err(epfnv->fdev, "Set BAR test condition -%s- Passed: %d\n", err_str, ret);
	else
		dev_err(epfnv->fdev, "Set BAR test condition -%s- Failed\n", err_str);
}

static void sanitize_bar(struct pci_epf_bar *bar)
{
	bar->barno = BAR_1;
	bar->size = SZ_64M;
	bar->flags = PCI_BASE_ADDRESS_MEM_TYPE_64 | PCI_BASE_ADDRESS_MEM_PREFETCH;
}

static int bar_neg_test(struct seq_file *s, void *data)
{
	struct pcie_epf_dma *epfnv = (struct pcie_epf_dma *)dev_get_drvdata(s->private);
	struct pcie_epf_bar *epf_bar = (struct pcie_epf_bar *)epfnv->bar_virt;
	struct pci_epf_bar bar = {};

	if (epfnv->chip_id == TEGRA234) {
		dev_err(epfnv->fdev, "%s: Not supported for T234 or when Link is already up\n",
			__func__);
		return -EOPNOTSUPP;
	}

	bar.phys_addr = epf_bar->rp_phy_addr;

	if (epf_bar->rp_phy_addr) {
		sanitize_bar(&bar);
		validate_bar(epfnv, &bar, "set_bar post link up");
		return 0;
	}
	sanitize_bar(&bar);
	bar.flags = PCI_BASE_ADDRESS_MEM_TYPE_64;
	validate_bar(epfnv, &bar, "non pre_fetch bar check");

	sanitize_bar(&bar);
	bar.flags = PCI_BASE_ADDRESS_MEM_TYPE_32 | PCI_BASE_ADDRESS_MEM_PREFETCH;
	validate_bar(epfnv, &bar, "non 64_bit bar check");

	sanitize_bar(&bar);
	bar.size = SZ_32;
	validate_bar(epfnv, &bar, "BAR1 less than 64M size check");

	sanitize_bar(&bar);
	bar.size = SZ_64 - 1;
	validate_bar(epfnv, &bar, "BAR1 non power of 2 check");

	sanitize_bar(&bar);
	bar.barno = BAR_2;
	validate_bar(epfnv, &bar, "BAR2 non 32 MB check");

	sanitize_bar(&bar);
	bar.size = (SZ_32G * 2) + 1;
	validate_bar(epfnv, &bar, "BAR1 64 GB+1 check");

	sanitize_bar(&bar);
	bar.barno = BAR_3;
	validate_bar(epfnv, &bar, "BAR3 non supported check");
	return 0;
}

static int clear_bar(struct seq_file *s, void *data)
{
	struct pcie_epf_dma *epfnv = (struct pcie_epf_dma *)dev_get_drvdata(s->private);
	struct pcie_epf_bar *epf_bar = (struct pcie_epf_bar *)epfnv->bar_virt;
	struct pci_epf_bar *bar = &epfnv->epf->bar[BAR_2];

	if ((epfnv->chip_id == TEGRA234) || (!epf_bar->rp_phy_addr)) {
		dev_err(epfnv->fdev, "%s: Not supported for T234 or when Link is not up\n",
			__func__);
		return -EOPNOTSUPP;
	}

	lpci_epc_clear_bar(epfnv->epc, epfnv->epf->func_no, bar);
	dev_err(epfnv->fdev, "%s:  EPF BAR_2 Clear attempted. Check BAR0/1 access\n",
		__func__);

	return 0;
}

static void init_debugfs(struct pcie_epf_dma *epfnv)
{
	debugfs_create_devm_seqfile(epfnv->fdev, "edmalib_test", epfnv->debugfs, edmalib_test);

	debugfs_create_devm_seqfile(epfnv->fdev, "edmalib_test_ob", epfnv->debugfs,
				    edmalib_test_ob);

	debugfs_create_devm_seqfile(epfnv->fdev, "dma_neg_test", epfnv->debugfs, dma_neg_test);

	debugfs_create_devm_seqfile(epfnv->fdev, "bar_neg_test", epfnv->debugfs, bar_neg_test);

	debugfs_create_devm_seqfile(epfnv->fdev, "clear_bar", epfnv->debugfs, clear_bar);


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

	debugfs_create_u32("stress_count", 0644, epfnv->debugfs, &epfnv->stress_count);
	epfnv->stress_count = DEFAULT_STRESS_COUNT;
}

static int pcie_dma_epf_core_init(struct pci_epf *epf)
{
	struct pcie_epf_dma *epfnv = epf_get_drvdata(epf);
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

	dev_info(fdev, "BAR0 phy_addr: %llx size: %lx\n", epf_bar->phys_addr, epf_bar->size);

	if (epf->msi_interrupts == 0) {
		dev_err(fdev, "pci_epc_set_msi() failed: %d\n", epf->msi_interrupts);
		epf->msi_interrupts = 16;
	}

	ret = lpci_epc_set_msi(epc, epf->func_no, epf->msi_interrupts);
	if (ret) {
		dev_err(fdev, "pci_epc_set_msi() failed: %d\n", ret);
		return ret;
	}

	if (epfnv->chip_id == TEGRA234)
		return 0;

	/* Expose MSI address as BAR2 to allow RP to send MSI to EP. */
	/**
	 * Note: This is only for testing MSI interrupt HW feature and should not be used for
	 * any prodcution use case, as it will result in SMMU errors and system hangs.
	 */
	bar = BAR_2;
	epf_bar = &epf->bar[bar];
	epf_bar->phys_addr = gepfnv->edma.msi_addr & ~(SZ_32M - 1);
	epf_bar->addr = NULL;
	epf_bar->size = SZ_32M;
	epf_bar->barno = bar;
	epf_bar->flags |= PCI_BASE_ADDRESS_MEM_TYPE_64 | PCI_BASE_ADDRESS_MEM_PREFETCH;
	ret = lpci_epc_set_bar(epc, epf->func_no, epf_bar);
	if (ret < 0) {
		dev_err(fdev, "PCIe set BAR2 failed: %d\n", ret);
		return ret;
	}

	dev_info(fdev, "%s received\n", __func__);
	return 0;
}

#if defined(NV_PCI_EPC_EVENT_OPS_STRUCT_HAS_EPC_DEINIT) || \
    defined(NV_PCI_EPC_EVENT_OPS_STRUCT_HAS_CORE_DEINIT) /* Linux v6.11 || Nvidia Internal */
static int pcie_dma_epf_core_deinit(struct pci_epf *epf)
{
	struct pcie_epf_dma *epfnv = epf_get_drvdata(epf);
	void *cookie = epfnv->edma.cookie;
	struct pcie_epf_bar *epf_bar_virt = (struct pcie_epf_bar *)epfnv->bar_virt;
	struct pci_epc *epc = epf->epc;
	struct device *fdev = &epf->dev;
	struct pci_epf_bar *epf_bar;
	enum pci_barno bar;

	if (epfnv->chip_id == TEGRA234)
		bar = BAR_0;

	dev_info(fdev, "%s received\n", __func__);
	epf_bar = &epf->bar[bar];
	epfnv->edma.cookie = NULL;
	epf_bar_virt->rp_phy_addr = 0;
	tegra_pcie_dma_deinit(&cookie);
	if (epfnv->chip_id == TEGRA234)
		lpci_epc_clear_bar(epc, epf->func_no, epf_bar);
	return 0;
}

#if defined(NV_PCI_EPC_EVENT_OPS_STRUCT_HAS_EPC_DEINIT)
static void pcie_dma_epf_epc_deinit(struct pci_epf *epf)
{
	pcie_dma_epf_core_deinit(epf);
}
#endif
#endif

static void pcie_dma_epf_unbind(struct pci_epf *epf)
{
	struct pcie_epf_dma *epfnv = epf_get_drvdata(epf);
	struct pci_epc *epc = epf->epc;
	void *cookie = epfnv->edma.cookie;
	struct pcie_epf_bar *epf_bar = (struct pcie_epf_bar *)epfnv->bar_virt;
	struct device *cdev = epc->dev.parent;
	struct platform_device *pdev = of_find_device_by_node(cdev->of_node);
#if !defined(NV_MSI_GET_VIRQ_PRESENT) /* Linux v6.1 */
	struct msi_desc *desc;
#endif
	enum pci_barno bar;
	u32 irq;

	debugfs_remove_recursive(epfnv->debugfs);

	epfnv->edma.cookie = NULL;
	epf_bar->rp_phy_addr = 0;
	tegra_pcie_dma_deinit(&cookie);

	if (epfnv->chip_id == TEGRA264) {
#if defined(NV_MSI_GET_VIRQ_PRESENT) /* Linux v6.1 */
		irq = msi_get_virq(&pdev->dev, TEGRA264_PCIE_DMA_MSI_CRC_VEC);
#else
		for_each_msi_entry(desc, cdev) {
			if (desc->platform.msi_index == TEGRA264_PCIE_DMA_MSI_CRC_VEC) {
				irq = desc->irq;
				break;
			}
		}
#endif
		free_irq(irq, epfnv);

#if defined(NV_PLATFORM_MSI_DOMAIN_FREE_IRQS_PRESENT) /* Linux v6.9 */
		platform_msi_domain_free_irqs(&pdev->dev);
#endif
	}

	pci_epc_stop(epc);
	if (epfnv->chip_id == TEGRA234)
		bar = BAR_0;
	else
		bar = BAR_1;
	lpci_epf_free_space(epf, epfnv->bar_virt, bar);
}

#if defined(NV_PLATFORM_MSI_DOMAIN_ALLOC_IRQS_PRESENT) /* Linux 6.9 */
static void pcie_dma_epf_write_msi_msg(struct msi_desc *desc, struct msi_msg *msg)
{
	if (gepfnv->edma.msi_addr == 0) {
		gepfnv->edma.msi_addr = msg->address_hi;
		gepfnv->edma.msi_addr <<= 32;
		gepfnv->edma.msi_addr |= msg->address_lo;
		/**
		 * First information received is for CRC MSI. So substract the same to get base and
		 * add WR local vector
		 */
		gepfnv->edma.msi_data = msg->data -TEGRA264_PCIE_DMA_MSI_CRC_VEC +
					TEGRA264_PCIE_DMA_MSI_LOCAL_VEC;
	}
}
#endif

static irqreturn_t pcie_dma_epf_irq(int irq, void *arg)
{
	struct pcie_epf_dma *epfnv = (struct pcie_epf_dma *)arg;
	struct pcie_epf_bar *epf_bar = (__force struct pcie_epf_bar *)epfnv->bar_virt;
	struct sanity_data *wr_data = &epf_bar->wr_data[0];
	u64 *data = (u64 *)(epfnv->bar_virt + ((BAR0_SIZE / 2) + BAR0_DMA_BUF_OFFSET) +
		    wr_data->dst_offset);

	dev_info(epfnv->fdev, "Received MSI IRQ From RP\n");
	dev_info(epfnv->fdev, "%s: wr_data size(0x%x), offset(%d). data[0]=0x%llx, data[size-1]=0x%llx\n",
		 __func__, wr_data->size, wr_data->dst_offset, data[0],
		 data[(wr_data->size/8) - 1u]);

	wr_data->crc = crc32_le(~0, epfnv->bar_virt + ((BAR0_SIZE / 2) + BAR0_DMA_BUF_OFFSET) +
				wr_data->dst_offset, wr_data->size);

	return IRQ_HANDLED;
}

static int pcie_dma_epf_bind(struct pci_epf *epf)
{
	const struct pci_epc_features *epc_features;
	struct pci_epc *epc = epf->epc;
	struct pcie_epf_dma *epfnv = epf_get_drvdata(epf);
	struct device *fdev = &epf->dev;
	struct device *cdev = epc->dev.parent;
	struct platform_device *pdev = of_find_device_by_node(cdev->of_node);
	struct pcie_epf_bar *epf_bar_virt;
	struct pci_epf_bar *epf_bar;
	struct irq_domain *domain;
#if !defined(NV_MSI_GET_VIRQ_PRESENT) /* Linux v6.1 */
	struct msi_desc *desc;
#endif
	enum pci_barno bar;
	char *name;
	int ret, i;
	u32 irq;

	epfnv->chip_id = __tegra_get_chip_id();
	if (epfnv->chip_id == TEGRA234) {
		bar = BAR_0;
		epfnv->edma.chip_id = NVPCIE_DMA_SOC_T234;
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

#if defined(NV_PCI_EPF_ALLOC_SPACE_HAS_EPC_FEATURES_ARG) /* Linux v6.9 */
	epfnv->bar_virt = lpci_epf_alloc_space(epf, BAR0_SIZE, bar, epc_features);
#else
	epfnv->bar_virt = lpci_epf_alloc_space(epf, BAR0_SIZE, bar, epc_features->align);
#endif
	if (!epfnv->bar_virt) {
		dev_err(fdev, "Failed to allocate memory for BAR0\n");
		return -ENOMEM;
	}
	get_random_bytes(epfnv->bar_virt, BAR0_SIZE);
	memset(epfnv->bar_virt, 0, BAR0_HEADER_SIZE);

	/* Update BAR header with EP DMA PHY addr */
	epf_bar_virt = (struct pcie_epf_bar *)epfnv->bar_virt;
	epf_bar_virt->ep_phy_addr = epf_bar->phys_addr;
	/* Set BAR0 mem type as 64-bit */
	epf_bar->flags |= PCI_BASE_ADDRESS_MEM_TYPE_64 | PCI_BASE_ADDRESS_MEM_PREFETCH;

	name = devm_kasprintf(fdev, GFP_KERNEL, "%s_epf_dma_test", pdev->name);
	if (!name) {
		ret = -ENOMEM;
		goto fail_atu_dma;
	}

	for (i = 0; i < TEGRA_PCIE_DMA_WRITE; i++)
		init_waitqueue_head(&epfnv->edma.wr_wq[i]);

	if (epfnv->chip_id == TEGRA264) {
		domain = dev_get_msi_domain(&pdev->dev);
		if (!domain) {
			dev_err(fdev, "failed to get MSI domain\n");
			ret = -ENOMEM;
			goto fail_kasnprintf;
		}

#if defined(NV_PLATFORM_MSI_DOMAIN_ALLOC_IRQS_PRESENT) /* Linux 6.9 */
		ret = platform_msi_domain_alloc_irqs(&pdev->dev, 8, pcie_dma_epf_write_msi_msg);
		if (ret < 0) {
			dev_err(fdev, "failed to allocate MSIs: %d\n", ret);
			goto fail_kasnprintf;
		}
#endif
#if defined(NV_MSI_GET_VIRQ_PRESENT) /* Linux v6.1 */
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

		ret = request_irq(irq, pcie_dma_epf_irq, IRQF_SHARED, "pcie_dma_epf_isr", epfnv);
		if (ret < 0) {
			dev_err(fdev, "failed to request irq: %d\n", ret);
			goto fail_msi_alloc;
		}
	}

	epfnv->debugfs = debugfs_create_dir(name, NULL);
	init_debugfs(epfnv);

	epc_features = pci_epc_get_features(epc, epf->func_no, epf->vfunc_no);
	if (!epc_features) {
		dev_err(fdev, "epc_features not implemented\n");
		ret = -EOPNOTSUPP;
		goto fail_get_features;
	}

#if defined(NV_PCI_EPC_FEATURES_STRUCT_HAS_CORE_INIT_NOTIFIER)
	if (!epc_features->core_init_notifier) {
		ret = pcie_dma_epf_core_init(epf);
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
	lpci_epf_free_space(epf, epfnv->bar_virt, bar);

	return ret;
}

static const struct pci_epf_device_id pcie_dma_epf_ids[] = {
	{
		.name = "tegra_pcie_dma_epf",
	},
	{},
};

static const struct pci_epc_event_ops pci_epf_dma_test_event_ops = {
#if defined(NV_PCI_EPC_EVENT_OPS_STRUCT_HAS_EPC_INIT) /* Linux v6.11 */
	.epc_init = pcie_dma_epf_core_init,
#else
	.core_init = pcie_dma_epf_core_init,
#endif
#if defined(NV_PCI_EPC_EVENT_OPS_STRUCT_HAS_EPC_DEINIT) /* Linux v6.11 */
	.epc_deinit = pcie_dma_epf_epc_deinit,
#elif defined(NV_PCI_EPC_EVENT_OPS_STRUCT_HAS_CORE_DEINIT) /* Nvidia Internal */
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

	epfnv->edma.ll_desc = devm_kzalloc(dev, sizeof(*epfnv->edma.ll_desc) * NUM_EDMA_DESC,
					   GFP_KERNEL);
	epf_set_drvdata(epf, epfnv);
	gepfnv = epfnv;

	epf->event_ops = &pci_epf_dma_test_event_ops;

	epfnv->header.vendorid = PCI_VENDOR_ID_NVIDIA;
	epfnv->header.deviceid = 0x22D7;
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
