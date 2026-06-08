// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025, NVIDIA CORPORATION. All rights reserved.
/*
 * PCIe EP controller driver for Tegra264 SoC
 *
 * Author: Manikanta Maddireddy <mmaddireddy@nvidia.com>
 */

#include <nvidia/conftest.h>

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/pci_ids.h>
#include <linux/pci.h>
#include <linux/pci-epc.h>
#include <linux/pci-epf.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <soc/tegra/bpmp.h>
#include <soc/tegra/bpmp-abi.h>

#include "pcie-tegra264-ep.h"

/* 5 msec PERST# gpio debounce */
#define PERST_DEBOUNCE_TIME	5000

/* Poll for every 10 msec */
#define PCIE_CBB_IDLE_DELAY	10000
/* 1 sec timeout */
#define PCIE_CBB_IDLE_TIMEOUT	1000000

/* Poll for every 10 msec */
#define PCIE_LTSSM_DELAY	10000
/* 1 sec timeout */
#define PCIE_LTSSM_TIMEOUT	1000000

/* PCIe EP support 8 outbound ATU windows */
#define NUM_OB_WINDOWS		8

#define EP_STATE_DISABLED	0
#define EP_STATE_ENABLED	1

#define XAL_RC_PCIX_INTR_STATUS		0x200
#define XAL_RC_PCIX_INTR_STATUS_EP_RESET	BIT(8)

#define XAL_RC_PCIX_INTR_EN		0x204
#define XAL_RC_PCIX_INTR_EN_EP_RESET		BIT(8)

#define XAL_RC_DEBUG_HB_REG_2		0x514
#define XAL_RC_DEBUG_HB_REG_2_HB_HIER_IDLE	BIT(29)

#define XTL_EP_PRI_BAR_CONFIG		0x214
#define XTL_EP_PRI_BAR_CONFIG_BAR2_EN		BIT(2)
#define XTL_EP_PRI_BAR_CONFIG_BAR1_EN		BIT(11)

#define XTL_EP_PRI_CLASSCODE_OVERRIDE	0x218
#define XTL_EP_PRI_CLASSCODE_OVERRIDE_VAL	GENMASK(7, 0)

#define XTL_EP_PRI_RESIZE_BAR1		0x278

#define XTL_EP_PRI_FUNC_INTR_STATUS	0x53c
#define XTL_EP_PRI_FUNC_INTR_STATUS_L2_ENTRY	BIT(1)

#define XTL_EP_PRI_DEVID		0xc80
#define XTL_EP_PRI_DEVID_OVERIDE		GENMASK(6, 0)
#define XTL_EP_PRI_DEVID_SW_UPDATE_PROD_ID	BIT(7)

#define XTL_EP_CFG_INT			0xe80
#define XTL_EP_CFG_INT_INTX_SET			BIT(0)
#define XTL_EP_CFG_INT_MSI_SET			BIT(1)
#define XTL_EP_CFG_INT_MSI_VEC_ID_SHIFT		2

#define XAL_EP_DM_I_ATU_REDIR_BAR1_ADDR_LSB	0x700
#define XAL_EP_DM_I_ATU_REDIR_BAR1_ADDR_MSB	0x704
#define XAL_EP_DM_I_ATU_REDIR_BAR2_ADDR_LSB	0x708
#define XAL_EP_DM_I_ATU_REDIR_BAR2_ADDR_MSB	0x70c

#define XAL_EP_DM_I_ATU_REDIR_CTRL	0x800
#define XAL_EP_DM_I_ATU_REDIR_CTRL_BAR1_EN	BIT(0)
#define XAL_EP_DM_I_ATU_REDIR_CTRL_BAR2_EN	BIT(4)

#define NV_XAL_EP_DM_E_ATU_LOCAL_BASE_HI(idx)		(0xb40 + (0x20 * (idx)))
#define NV_XAL_EP_DM_E_ATU_LOCAL_BASE_LO(idx)		(0xb44 + (0x20 * (idx)))
#define NV_XAL_EP_DM_E_ATU_LOCAL_LIMIT_HI(idx)		(0xb48 + (0x20 * (idx)))
#define NV_XAL_EP_DM_E_ATU_LOCAL_LIMIT_LO(idx)		(0xb4c + (0x20 * (idx)))
#define NV_XAL_EP_DM_E_ATU_REMOTE_TARGET_HI(idx)	(0xb50 + (0x20 * (idx)))
#define NV_XAL_EP_DM_E_ATU_REMOTE_TARGET_LO(idx)	(0xb54 + (0x20 * (idx)))

#define XPL_PL_LTSSM_STATE		0x1700
#define XPL_PL_LTSSM_STATE_FULL			GENMASK(7, 0)
#define XPL_PL_LTSSM_DETECT_QUIET		1

#define NV_EP_PCFG_MSI_64_HEADER	0x48

struct tegra264_pcie_ep {
	struct device *dev;

	void __iomem *xal_base;
	void __iomem *xpl_base;
	void __iomem *xal_ep_dm_base;
	void __iomem *xtl_ep_pri_base;
	void __iomem *xtl_ep_cfg_base;

	struct gpio_desc *pex_prsnt_gpiod;
	struct gpio_desc *pex_rst_gpiod;
	unsigned int pex_rst_irq;

	phys_addr_t phys_base;
	size_t phys_size;
	size_t page_size;
	dma_addr_t bar1_phys;
	dma_addr_t bar2_phys;

	struct pci_epc *epc;
	struct tegra_bpmp *bpmp;

	phys_addr_t ob_addr[NUM_OB_WINDOWS];
	unsigned long *ob_window_map;

	u32 ctl_id;
	u32 ep_state;
	u8 progif_code;
	u8 subclass_code;
	u8 baseclass_code;
	u8 deviceid;
	u8 bar1_size;
};

static int tegra264_pcie_bpmp_set_ep_state(struct tegra264_pcie_ep *pcie, bool enable)
{
#if defined(NV_MRQ_PCIE_REQUEST_STRUCT_PRESENT)
	struct tegra_bpmp_message msg;
	struct mrq_pcie_request req;
	int err;

	memset(&req, 0, sizeof(req));

	if (enable) {
		req.cmd = CMD_PCIE_EP_CONTROLLER_INIT;
		req.ep_ctrlr_init.ep_controller = pcie->ctl_id;
		req.ep_ctrlr_init.progif_code = pcie->progif_code;
		req.ep_ctrlr_init.subclass_code = pcie->subclass_code;
		req.ep_ctrlr_init.baseclass_code = pcie->baseclass_code;
		req.ep_ctrlr_init.deviceid = pcie->deviceid;
		req.ep_ctrlr_init.bar1_size = pcie->bar1_size;
	} else {
		req.cmd = CMD_PCIE_EP_CONTROLLER_OFF;
		req.ep_ctrlr_off.ep_controller = pcie->ctl_id;
	}

	memset(&msg, 0, sizeof(msg));
	msg.mrq = MRQ_PCIE;
	msg.tx.data = &req;
	msg.tx.size = sizeof(req);

	err = tegra_bpmp_transfer(pcie->bpmp, &msg);
	if (err)
		return err;
	if (msg.rx.ret)
		return -EINVAL;

	return 0;
#else
	return -EOPNOTSUPP;
#endif
}

static void tegra264_pcie_ep_rst_assert(struct tegra264_pcie_ep *pcie)
{
	u32 val;
	int ret;

	if (pcie->ep_state == EP_STATE_DISABLED)
		return;

#if defined(NV_PCI_EPC_DEINIT_NOTIFY_PRESENT) /* Linux v6.11 */
	pci_epc_deinit_notify(pcie->epc);
#else
	dev_WARN(pcie->dev, "kernel does not support pci_epc_deinit_notify!\n");
#endif

	ret = readl_poll_timeout(pcie->xal_base + XAL_RC_DEBUG_HB_REG_2, val,
				 val & XAL_RC_DEBUG_HB_REG_2_HB_HIER_IDLE,
				 PCIE_CBB_IDLE_DELAY, PCIE_CBB_IDLE_TIMEOUT);
	if (ret)
		dev_err(pcie->dev, "PCIe CBB idle timedout: %d\n", ret);

	/* When stopping the Endpoint, clear outbound mappings. */
	bitmap_zero(pcie->ob_window_map, NUM_OB_WINDOWS);

	ret = tegra264_pcie_bpmp_set_ep_state(pcie, false);
	if (ret)
		dev_err(pcie->dev, "Failed to turn off PCIe: %d\n", ret);

	pcie->ep_state = EP_STATE_DISABLED;

	dev_dbg(pcie->dev, "Uninitialization of endpoint is completed\n");
}

static void tegra264_pcie_ep_config_bar(struct tegra264_pcie_ep *pcie)
{
	u32 val;

	writel(upper_32_bits(pcie->bar1_phys),
	       pcie->xal_ep_dm_base + XAL_EP_DM_I_ATU_REDIR_BAR1_ADDR_MSB);
	writel(lower_32_bits(pcie->bar1_phys),
	       pcie->xal_ep_dm_base + XAL_EP_DM_I_ATU_REDIR_BAR1_ADDR_LSB);

	writel(upper_32_bits(pcie->bar2_phys),
	       pcie->xal_ep_dm_base + XAL_EP_DM_I_ATU_REDIR_BAR2_ADDR_MSB);
	writel(lower_32_bits(pcie->bar2_phys),
	       pcie->xal_ep_dm_base + XAL_EP_DM_I_ATU_REDIR_BAR2_ADDR_LSB);

	val = readl(pcie->xal_ep_dm_base + XAL_EP_DM_I_ATU_REDIR_CTRL);
	val |= (XAL_EP_DM_I_ATU_REDIR_CTRL_BAR1_EN | XAL_EP_DM_I_ATU_REDIR_CTRL_BAR2_EN);
	writel(val, pcie->xal_ep_dm_base + XAL_EP_DM_I_ATU_REDIR_CTRL);
}

static void tegra264_pcie_ep_rst_deassert(struct tegra264_pcie_ep *pcie)
{
	struct device *dev = pcie->dev;
	int ret;
	u32 val;

	if (pcie->ep_state == EP_STATE_ENABLED)
		return;

	ret = tegra264_pcie_bpmp_set_ep_state(pcie, true);
	if (ret) {
		dev_err(dev, "Failed to initialize PCIe endpoint: %d\n", ret);
		return;
	}

	tegra264_pcie_ep_config_bar(pcie);

	val = readl(pcie->xal_base + XAL_RC_PCIX_INTR_EN);
	val |= XAL_RC_PCIX_INTR_EN_EP_RESET;
	writel(val, pcie->xal_base + XAL_RC_PCIX_INTR_EN);

	pcie->ep_state = EP_STATE_ENABLED;

	dev_dbg(dev, "Initialization of endpoint is completed\n");
}

static irqreturn_t tegra264_pcie_ep_rst_irq(int irq, void *arg)
{
	struct tegra264_pcie_ep *pcie = arg;

	if (gpiod_get_value(pcie->pex_rst_gpiod))
		tegra264_pcie_ep_rst_assert(pcie);
	else
		tegra264_pcie_ep_rst_deassert(pcie);

	return IRQ_HANDLED;
}

static int tegra264_pcie_ep_write_header(struct pci_epc *epc, u8 fn,
#if defined(NV_PCI_EPC_WRITE_HEADER_HAS_VFN_ARG)
					 u8 vfn,
#endif
					 struct pci_epf_header *hdr)
{
	struct tegra264_pcie_ep *pcie = epc_get_drvdata(epc);

	pcie->deviceid = (u8)(hdr->deviceid & 0x7F);
	pcie->baseclass_code = hdr->baseclass_code;
	pcie->subclass_code = hdr->subclass_code;
	pcie->progif_code = hdr->progif_code;

	return 0;
}

static int tegra264_pcie_ep_set_bar(struct pci_epc *epc, u8 fn,
#if defined(NV_PCI_EPC_WRITE_HEADER_HAS_VFN_ARG)
			            u8 vfn,
#endif
				    struct pci_epf_bar *epf_bar)
{
	struct tegra264_pcie_ep *pcie = epc_get_drvdata(epc);
	enum pci_barno bar = epf_bar->barno;
	size_t size = epf_bar->size;
	u32 sz_bitmap;

	if ((epf_bar->flags & 0xf) !=
	    (PCI_BASE_ADDRESS_MEM_TYPE_64 | PCI_BASE_ADDRESS_MEM_PREFETCH)) {
		dev_err(pcie->dev, "%s: Only 64-bit prefectable BAR is supported, flags: 0x%x\n",
			__func__, epf_bar->flags);
		return -EINVAL;
	}

	if (pcie->ep_state == EP_STATE_ENABLED) {
		dev_err(pcie->dev, "%s: Allowed only when link is not up\n", __func__);
		return -EINVAL;
	}

	if (bar == BAR_1) {
		/* Fail set BAR if size is not power of 2 or out of [64MB,64GB] range */
		if (!((size >= SZ_64M && size <= (SZ_32G * 2)) && !(size & (size - 1)))) {
			dev_err(pcie->dev, "%s: Invalid BAR1 size requested: %ld\n",
				__func__, size);
			return -EINVAL;
		}
		pcie->bar1_phys = epf_bar->phys_addr;
		/* Convert from bytes to MB in bitmap encoding */
		sz_bitmap = ilog2(size) - 20;
		pcie->bar1_size = (u8)(sz_bitmap & 0xFF);
	} else if (bar == BAR_2) {
		if (size != SZ_32M) {
			dev_err(pcie->dev, "%s: Only 32MB BAR2 is supported, requested size: %ld\n",
				__func__, size);
			return -EINVAL;
		}
		pcie->bar2_phys = epf_bar->phys_addr;
	} else {
		dev_err(pcie->dev, "%s: BAR-%d is not configurable\n", __func__, bar);
		return -EINVAL;
	}

	return 0;
}

static void tegra264_pcie_ep_clear_bar(struct pci_epc *epc, u8 fn,
#if defined(NV_PCI_EPC_WRITE_HEADER_HAS_VFN_ARG)
				       u8 vfn,
#endif
				       struct pci_epf_bar *epf_bar)
{
	struct tegra264_pcie_ep *pcie = epc_get_drvdata(epc);
	enum pci_barno bar = epf_bar->barno;
	u32 val;

	if (bar == BAR_1) {
		writel(0x0, pcie->xal_ep_dm_base + XAL_EP_DM_I_ATU_REDIR_BAR1_ADDR_MSB);
		writel(0x0, pcie->xal_ep_dm_base + XAL_EP_DM_I_ATU_REDIR_BAR1_ADDR_LSB);

		val = readl(pcie->xal_ep_dm_base + XAL_EP_DM_I_ATU_REDIR_CTRL);
		val &= ~XAL_EP_DM_I_ATU_REDIR_CTRL_BAR1_EN;
		writel(val, pcie->xal_ep_dm_base + XAL_EP_DM_I_ATU_REDIR_CTRL);
	} else if (bar == BAR_2) {
		writel(0x0, pcie->xal_ep_dm_base + XAL_EP_DM_I_ATU_REDIR_BAR2_ADDR_MSB);
		writel(0x0, pcie->xal_ep_dm_base + XAL_EP_DM_I_ATU_REDIR_BAR2_ADDR_LSB);

		val = readl(pcie->xal_ep_dm_base + XAL_EP_DM_I_ATU_REDIR_CTRL);
		val &= ~XAL_EP_DM_I_ATU_REDIR_CTRL_BAR2_EN;
		writel(val, pcie->xal_ep_dm_base + XAL_EP_DM_I_ATU_REDIR_CTRL);
	}
}

static int tegra264_pcie_ep_map_addr(struct pci_epc *epc, u8 func_no,
#if defined(NV_PCI_EPC_WRITE_HEADER_HAS_VFN_ARG)
				     u8 vfunc_no,
#endif
				     phys_addr_t addr, u64 pci_addr, size_t size)
{
	struct tegra264_pcie_ep *pcie = epc_get_drvdata(epc);
	phys_addr_t limit = addr + size - 1;
	u32 free_win;

	if (pcie->ep_state != EP_STATE_ENABLED) {
		dev_err(pcie->dev, "%s: Allowed only when link is up\n", __func__);
		return -EINVAL;
	}

	free_win = find_first_zero_bit(pcie->ob_window_map, NUM_OB_WINDOWS);
	if (free_win >= NUM_OB_WINDOWS) {
		dev_err(pcie->dev, "No free outbound window\n");
		return -EINVAL;
	}

	writel(upper_32_bits(addr),
	       pcie->xal_ep_dm_base + NV_XAL_EP_DM_E_ATU_LOCAL_BASE_HI(free_win));
	writel(lower_32_bits(addr),
	       pcie->xal_ep_dm_base + NV_XAL_EP_DM_E_ATU_LOCAL_BASE_LO(free_win));
	writel(upper_32_bits(limit),
	       pcie->xal_ep_dm_base + NV_XAL_EP_DM_E_ATU_LOCAL_LIMIT_HI(free_win));
	writel(lower_32_bits(limit),
	       pcie->xal_ep_dm_base + NV_XAL_EP_DM_E_ATU_LOCAL_LIMIT_LO(free_win));
	writel(upper_32_bits(pci_addr),
	       pcie->xal_ep_dm_base + NV_XAL_EP_DM_E_ATU_REMOTE_TARGET_HI(free_win));
	writel(lower_32_bits(pci_addr),
	       pcie->xal_ep_dm_base + NV_XAL_EP_DM_E_ATU_REMOTE_TARGET_LO(free_win));

	set_bit(free_win, pcie->ob_window_map);
	pcie->ob_addr[free_win] = addr;

	return 0;
}

static void tegra264_pcie_ep_unmap_addr(struct pci_epc *epc, u8 func_no,
#if defined(NV_PCI_EPC_WRITE_HEADER_HAS_VFN_ARG)
					u8 vfunc_no,
#endif
					phys_addr_t addr)
{
	struct tegra264_pcie_ep *pcie = epc_get_drvdata(epc);
	u32 ob_idx;

	if (pcie->ep_state != EP_STATE_ENABLED) {
		dev_err(pcie->dev, "%s: Allowed only when link is up\n", __func__);
		return;
	}

	for (ob_idx = 0; ob_idx < NUM_OB_WINDOWS; ob_idx++) {
		if (pcie->ob_addr[ob_idx] != addr)
			continue;
		break;
	}

	if (ob_idx == NUM_OB_WINDOWS)
		return;

	writel(0x0, pcie->xal_ep_dm_base + NV_XAL_EP_DM_E_ATU_LOCAL_BASE_HI(ob_idx));
	writel(0x0, pcie->xal_ep_dm_base + NV_XAL_EP_DM_E_ATU_LOCAL_BASE_LO(ob_idx));
	writel(0x0, pcie->xal_ep_dm_base + NV_XAL_EP_DM_E_ATU_LOCAL_LIMIT_HI(ob_idx));
	writel(0x0, pcie->xal_ep_dm_base + NV_XAL_EP_DM_E_ATU_LOCAL_LIMIT_LO(ob_idx));
	writel(0x0, pcie->xal_ep_dm_base + NV_XAL_EP_DM_E_ATU_REMOTE_TARGET_HI(ob_idx));
	writel(0x0, pcie->xal_ep_dm_base + NV_XAL_EP_DM_E_ATU_REMOTE_TARGET_LO(ob_idx));

	clear_bit(ob_idx, pcie->ob_window_map);
}

static int tegra264_pcie_ep_set_msi(struct pci_epc *epc, u8 fn,
#if defined(NV_PCI_EPC_WRITE_HEADER_HAS_VFN_ARG)
				    u8 vfn,
#endif
				    u8 mmc)
{
	/*
	 * Tegra264 PCIe EP HW supports 16 MSIs only, return success if multi msg cap encoded
	 * value is <= 4 without programming MSI capability register since we can enable
	 * desired number of MSIs even if more MSIs are supported. This will avoid unnecessary
	 * failure of pci_epc_set_msi() in EPF driver. If more than 16 MSIs are requested then
	 * return error.
	 */
	if (mmc <= 4)
		return 0;
	else
		return -EINVAL;
}

static int tegra264_pcie_ep_get_msi(struct pci_epc *epc,
#if defined(NV_PCI_EPC_WRITE_HEADER_HAS_VFN_ARG)
				    u8 fn, u8 vfn)
#else
				    u8 fn)
#endif
{
	struct tegra264_pcie_ep *pcie = epc_get_drvdata(epc);
	u32 val;

	val = readw(pcie->xtl_ep_cfg_base + NV_EP_PCFG_MSI_64_HEADER + PCI_MSI_FLAGS);
	if (!(val & PCI_MSI_FLAGS_ENABLE))
		return -EINVAL;

	val = (val & PCI_MSI_FLAGS_QSIZE) >> 4;

	return val;
}

static int tegra264_pcie_ep_send_legacy_irq(struct tegra264_pcie_ep *pcie, u8 fn)
{
	u32 val;

	val = XTL_EP_CFG_INT_INTX_SET;
	writel(val, pcie->xtl_ep_pri_base + XTL_EP_CFG_INT);
	mdelay(1);
	val &= ~XTL_EP_CFG_INT_INTX_SET;
	writel(val, pcie->xtl_ep_pri_base + XTL_EP_CFG_INT);

	return 0;
}

static int tegra264_pcie_ep_send_msi_irq(struct tegra264_pcie_ep *pcie, u8 fn, u8 irq_num)
{
	u32 val;

	val = XTL_EP_CFG_INT_MSI_SET | (irq_num << XTL_EP_CFG_INT_MSI_VEC_ID_SHIFT);
	writel(val, pcie->xtl_ep_pri_base + XTL_EP_CFG_INT);

	return 0;
}

static int tegra264_pcie_ep_raise_irq(struct pci_epc *epc, u8 fn,
#if defined(NV_PCI_EPC_WRITE_HEADER_HAS_VFN_ARG)
				      u8 vfn,
#endif
#if defined(PCI_EPC_IRQ_TYPE_ENUM_PRESENT) /* Dropped from Linux 6.8 */
				      enum pci_epc_irq_type type,
#else
				      unsigned int type,
#endif
				      u16 irq_num)
{
	struct tegra264_pcie_ep *pcie = epc_get_drvdata(epc);

	/*
	 * Legacy INTX and MSI are generated by writing to same register. But We don't need to
	 * synchronize these functions because we don't expect both interrupts enabled at the
	 * same time.
	 */
	switch (type) {
#if defined(NV_PCI_IRQ_INTX)
	case PCI_IRQ_INTX:
#else
	case PCI_EPC_IRQ_LEGACY:
#endif
		/* Only INTA is supported. */
		return tegra264_pcie_ep_send_legacy_irq(pcie, fn);
#if defined(PCI_EPC_IRQ_TYPE_ENUM_PRESENT) /* Dropped from Linux 6.8 */
	case PCI_EPC_IRQ_MSI:
#else
	case PCI_IRQ_MSI:
#endif
		return tegra264_pcie_ep_send_msi_irq(pcie, fn, irq_num);
	default:
		return -EINVAL;
	}
}

static int tegra264_pcie_ep_start(struct pci_epc *epc)
{
	struct tegra264_pcie_ep *pcie = epc_get_drvdata(epc);

	if (gpiod_get_value_cansleep(pcie->pex_rst_gpiod) == 0U) {
		dev_dbg(pcie->dev, "RP already started. Starting EP\n");
		tegra264_pcie_ep_rst_deassert(pcie);
	}

	if (pcie->pex_prsnt_gpiod) {
		dev_dbg(pcie->dev, "De-Asserting PRSNT\n");
		gpiod_set_value_cansleep(pcie->pex_prsnt_gpiod, 1);
	}

	enable_irq(pcie->pex_rst_irq);

	return 0;
}

static void tegra264_pcie_ep_stop(struct pci_epc *epc)
{
	struct tegra264_pcie_ep *pcie = epc_get_drvdata(epc);
	disable_irq(pcie->pex_rst_irq);

	if (pcie->pex_prsnt_gpiod) {
		dev_dbg(pcie->dev, "Asserting PRSNT\n");
		gpiod_set_value_cansleep(pcie->pex_prsnt_gpiod, 0);
	}

	tegra264_pcie_ep_rst_assert(pcie);
}

static const struct pci_epc_features tegra264_pcie_epc_features = {
	.linkup_notifier = true,
	.msi_capable = true,
	.msix_capable = false,
#if defined (NV_PCI_EPC_FEATURES_STRUCT_HAS_BAR)
	.bar[BAR_0] = { .type = BAR_RESERVED, },
	.bar[BAR_2] = { .type = BAR_FIXED, .fixed_size = SZ_32M, },
	.bar[BAR_3] = { .type = BAR_RESERVED, },
	.bar[BAR_4] = { .type = BAR_RESERVED, },
	.bar[BAR_5] = { .type = BAR_RESERVED, },
#else
	.reserved_bar = 1 << BAR_0 | 1 << BAR_3 | 1 << BAR_4 | 1 << BAR_5,
	.bar_fixed_size[2] = SZ_32M,
#endif
	.align = SZ_64K,
};

static const struct pci_epc_features *tegra264_pcie_ep_get_features(struct pci_epc *epc,
#if defined(NV_PCI_EPC_WRITE_HEADER_HAS_VFN_ARG)
								    u8 fn, u8 vfn)
#else
								    u8 fn)
#endif
{
	return &tegra264_pcie_epc_features;
}

static const struct pci_epc_ops tegra264_pcie_epc_ops = {
	.write_header	= tegra264_pcie_ep_write_header,
	.set_bar	= tegra264_pcie_ep_set_bar,
	.clear_bar	= tegra264_pcie_ep_clear_bar,
	.map_addr	= tegra264_pcie_ep_map_addr,
	.unmap_addr	= tegra264_pcie_ep_unmap_addr,
	.set_msi	= tegra264_pcie_ep_set_msi,
	.get_msi	= tegra264_pcie_ep_get_msi,
	.raise_irq	= tegra264_pcie_ep_raise_irq,
	.start		= tegra264_pcie_ep_start,
	.stop		= tegra264_pcie_ep_stop,
	.get_features	= tegra264_pcie_ep_get_features,
};

static irqreturn_t tegra264_pcie_ep_irq_thread(int irq, void *arg)
{
	struct tegra264_pcie_ep *pcie = arg;
	u32 val;
	int ret;

	ret = readl_poll_timeout(pcie->xpl_base + XPL_PL_LTSSM_STATE, val,
			(val & XPL_PL_LTSSM_STATE_FULL) == XPL_PL_LTSSM_DETECT_QUIET,
			 PCIE_LTSSM_DELAY, PCIE_LTSSM_TIMEOUT);
	if (ret)
		dev_err(pcie->dev, "PCIe LTSSM state not in detect reset, ltssm: 0x%x\n", val);

	tegra264_pcie_ep_rst_assert(pcie);
	/* Per PCIe CEM spec minimum time between PERST# toggle is 100 usec(TPERST) */
	usleep_range(100, 200);
	tegra264_pcie_ep_rst_deassert(pcie);

	return IRQ_HANDLED;
}

static irqreturn_t tegra264_pcie_ep_irq_handler(int irq, void *arg)
{
	struct tegra264_pcie_ep *pcie = arg;
	u32 val;

	val = readl(pcie->xtl_ep_pri_base + XTL_EP_PRI_FUNC_INTR_STATUS);
	writel(val, pcie->xtl_ep_pri_base + XTL_EP_PRI_FUNC_INTR_STATUS);

	val = readl(pcie->xal_base + XAL_RC_PCIX_INTR_STATUS);
	writel(val, pcie->xal_base + XAL_RC_PCIX_INTR_STATUS);

	/* Wake irq thread to handle EP_RESET(SBR) */
	if (val & XAL_RC_PCIX_INTR_STATUS_EP_RESET)
		return IRQ_WAKE_THREAD;

	return IRQ_HANDLED;
}

static int tegra264_pcie_ep_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tegra264_pcie_ep *pcie;
	struct resource *res;
	char *name, *level;
	int ret = 0, irq;

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	platform_set_drvdata(pdev, pcie);
	pcie->dev = dev;

	pcie->ob_window_map = devm_bitmap_zalloc(dev, NUM_OB_WINDOWS, GFP_KERNEL);
	if (!pcie->ob_window_map)
		return -ENOMEM;

	ret = of_property_read_u32_index(dev->of_node, "nvidia,bpmp", 1, &pcie->ctl_id);
	if (ret) {
		dev_err(dev, "Failed to read Controller-ID: %d\n", ret);
		return ret;
	}

	ret = pinctrl_pm_select_default_state(dev);
	if (ret < 0) {
		dev_err(dev, "Failed to configure sideband pins: %d\n", ret);
		return ret;
	}

	pcie->pex_prsnt_gpiod = devm_gpiod_get_optional(dev, "nvidia,pex-prsnt", GPIOD_OUT_LOW);
	if (IS_ERR(pcie->pex_prsnt_gpiod)) {
		ret = PTR_ERR(pcie->pex_prsnt_gpiod);
		if (ret == -EPROBE_DEFER)
			return ret;

		dev_dbg(dev, "Failed to get PCIe PRSNT GPIO: %d\n", ret);
	}

	pcie->pex_rst_gpiod = devm_gpiod_get(dev, "reset", GPIOD_IN);
	if (IS_ERR(pcie->pex_rst_gpiod)) {
		ret = PTR_ERR(pcie->pex_rst_gpiod);
		if (ret == -EPROBE_DEFER)
			level = KERN_DEBUG;
		else
			level = KERN_ERR;

		dev_printk(level, dev, dev_fmt("Failed to get PERST GPIO: %d\n"), ret);
		return ret;
	}

	ret = gpiod_to_irq(pcie->pex_rst_gpiod);
	if (ret < 0) {
		dev_err(dev, "Failed to get IRQ for PERST GPIO: %d\n", ret);
		return ret;
	}
	pcie->pex_rst_irq = (unsigned int)ret;

	ret = gpiod_set_debounce(pcie->pex_rst_gpiod, PERST_DEBOUNCE_TIME);
	if (ret < 0) {
		dev_err(dev, "Failed to set PERST GPIO debounce time: %d\n", ret);
		return ret;
	}

	name = devm_kasprintf(dev, GFP_KERNEL, "tegra264_pcie_%u_rst_irq", pcie->ctl_id);
	if (!name) {
		dev_err(dev, "Failed to create PERST IRQ string\n");
		return -ENOMEM;
	}

	ret = devm_request_threaded_irq(dev, pcie->pex_rst_irq, NULL, tegra264_pcie_ep_rst_irq,
					IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					name, (void *)pcie);
	if (ret < 0) {
		dev_err(dev, "Failed to request IRQ for PERST: %d\n", ret);
		return ret;
	}
	disable_irq(pcie->pex_rst_irq);

	pcie->xal_base = devm_platform_ioremap_resource_byname(pdev, "xal");
	if (IS_ERR(pcie->xal_base)) {
		ret = PTR_ERR(pcie->xal_base);
		dev_err(dev, "failed to map xal memory: %d\n", ret);
		return ret;
	}

	pcie->xpl_base = devm_platform_ioremap_resource_byname(pdev, "xpl");
	if (IS_ERR(pcie->xpl_base)) {
		ret = PTR_ERR(pcie->xpl_base);
		dev_err(dev, "failed to map xpl memory: %d\n", ret);
		return ret;
	}

	pcie->xal_ep_dm_base = devm_platform_ioremap_resource_byname(pdev, "xal-ep-dm");
	if (IS_ERR(pcie->xal_ep_dm_base)) {
		ret = PTR_ERR(pcie->xal_ep_dm_base);
		dev_err(dev, "failed to map xal ep dm memory: %d\n", ret);
		return ret;
	}

	pcie->xtl_ep_pri_base = devm_platform_ioremap_resource_byname(pdev, "xtl-ep-pri");
	if (IS_ERR(pcie->xtl_ep_pri_base)) {
		ret = PTR_ERR(pcie->xtl_ep_pri_base);
		dev_err(dev, "failed to map xtl ep pri memory: %d\n", ret);
		return ret;
	}

	pcie->xtl_ep_cfg_base = devm_platform_ioremap_resource_byname(pdev, "xtl-ep-cfg");
	if (IS_ERR(pcie->xtl_ep_cfg_base)) {
		ret = PTR_ERR(pcie->xtl_ep_cfg_base);
		dev_err(dev, "failed to map xtl ep cfg memory: %d\n", ret);
		return ret;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "addr_space");
	if (!res) {
		dev_err(dev, "faled to get addr_space resource\n");
		return -EINVAL;
	}

	pcie->phys_base = res->start;
	pcie->phys_size = resource_size(res);
	pcie->page_size = SZ_64K;

	pcie->epc = devm_pci_epc_create(dev, &tegra264_pcie_epc_ops);
	if (IS_ERR(pcie->epc)) {
		dev_err(dev, "failed to create epc device\n");
		return PTR_ERR(pcie->epc);
	}
	pcie->epc->max_functions = 1;

	ret = pci_epc_mem_init(pcie->epc, pcie->phys_base, pcie->phys_size, pcie->page_size);
	if (ret < 0) {
		dev_err(dev, "failed to initialize the memory space\n");
		return ret;
	}

	pcie->bpmp = tegra_bpmp_get(dev);
	if (IS_ERR(pcie->bpmp)) {
		dev_err(dev, "tegra_bpmp_get fail: %ld\n", PTR_ERR(pcie->bpmp));
		ret = PTR_ERR(pcie->bpmp);
		goto fail_get_bpmp;
	}

	epc_set_drvdata(pcie->epc, pcie);
	pcie->ep_state = EP_STATE_DISABLED;

	irq = platform_get_irq_byname(pdev, "intr");
	if (irq < 0) {
		dev_err(dev, "failed to get intr: %d\n", irq);
		ret = irq;
		goto fail_get_irq;
	}

	ret = devm_request_threaded_irq(dev, irq, tegra264_pcie_ep_irq_handler,
					tegra264_pcie_ep_irq_thread, IRQF_SHARED | IRQF_ONESHOT,
					"tegra264-pcie-ep-intr", (void *)pcie);
	if (ret) {
		dev_err(dev, "Failed to request IRQ %d: %d\n", irq, ret);
		goto fail_get_irq;
	}

	return 0;

fail_get_irq:
	tegra_bpmp_put(pcie->bpmp);
fail_get_bpmp:
	pci_epc_mem_exit(pcie->epc);

	return ret;
}

static const struct of_device_id tegra264_pcie_ep_of_match[] = {
	{
		.compatible = "nvidia,tegra264-pcie-ep",
	},
	{},
};

static int tegra264_pcie_ep_remove(struct platform_device *pdev)
{
	struct tegra264_pcie_ep *pcie = platform_get_drvdata(pdev);

	pci_epc_mem_exit(pcie->epc);
	tegra_bpmp_put(pcie->bpmp);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int tegra264_pcie_ep_suspend(struct device *dev)
{
	struct tegra264_pcie_ep *pcie = dev_get_drvdata(dev);

	if (pcie->ep_state == EP_STATE_ENABLED) {
		dev_err(dev, "Suspend is not allowed when PCIe EP is initialized\n");
		return -EPERM;
	}

	disable_irq(pcie->pex_rst_irq);

	return 0;
}

static int tegra264_pcie_ep_resume(struct device *dev)
{
	struct tegra264_pcie_ep *pcie = dev_get_drvdata(dev);

	enable_irq(pcie->pex_rst_irq);

	return 0;
}

static const struct dev_pm_ops tegra264_pcie_ep_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(tegra264_pcie_ep_suspend, tegra264_pcie_ep_resume)
};
#endif

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void tegra264_pcie_ep_remove_wrapper(struct platform_device *pdev)
{
	tegra264_pcie_ep_remove(pdev);
}
#else
static int tegra264_pcie_ep_remove_wrapper(struct platform_device *pdev)
{
	return tegra264_pcie_ep_remove(pdev);
}
#endif

static struct platform_driver tegra264_pcie_ep_driver = {
	.probe = tegra264_pcie_ep_probe,
	.remove = tegra264_pcie_ep_remove_wrapper,
	.driver = {
		.name = "tegra264-pcie-ep",
		.of_match_table = tegra264_pcie_ep_of_match,
#ifdef CONFIG_PM_SLEEP
		.pm = &tegra264_pcie_ep_dev_pm_ops,
#endif
	},
};

module_platform_driver(tegra264_pcie_ep_driver);

MODULE_DEVICE_TABLE(of, tegra264_pcie_ep_of_match);

MODULE_AUTHOR("Manikanta Maddireddy <mmaddireddy@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra264 PCIe EP controller driver");
MODULE_LICENSE("GPL v2");
