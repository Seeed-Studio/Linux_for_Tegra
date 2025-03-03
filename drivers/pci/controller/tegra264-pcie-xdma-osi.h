// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved. */

#ifndef TEGRA264_PCIE_XDMA_OSI_H
#define TEGRA264_PCIE_XDMA_OSI_H

#undef OSI_BIT
#define OSI_BIT(b)		(1U << (b))
/** generates bit mask for 32 bit value */
#undef OSI_GENMASK
#define OSI_GENMASK(h, l)	(((~0U) << (l)) & (~0U >> (31U - (h))))

#define XDMA_MSI_CFG_MASK			0x100
#define XDMA_MSI_CFG_MASK_STATUS_MSI			BIT(8)

#define XDMA_MSI_CFG_LOCAL_ADDRESS_LO	0x104
#define XDMA_MSI_CFG_LOCAL_ADDRESS_HI	0x108
#define XDMA_MSI_CFG_REMOTE_ADDRESS_LO	0x10c
#define XDMA_MSI_CFG_REMOTE_ADDRESS_HI	0x110

/* Channel specific registers */
#define XDMA_CHANNEL_CTRL	0x2000
#define XDMA_CHANNEL_CTRL_MSI_CHANNEL_NUMBER		OSI_GENMASK(11, 9)
#define XDMA_CHANNEL_CTRL_MSI_CHANNEL_SHIFT		9
#define XDMA_CHANNEL_CTRL_DMA_CMD_SOURCE		OSI_BIT(2)
#define XDMA_CHANNEL_CTRL_DMA_OPERATION			OSI_BIT(1)
#define XDMA_CHANNEL_CTRL_EN				OSI_BIT(0)

#define XDMA_CHANNEL_DESCRIPTOR_LIST_POINTER_LOW	0x2020
#define XDMA_CHANNEL_DESCRIPTOR_LIST_POINTER_HIGH	0x2040
#define XDMA_CHANNEL_DESCRIPTOR_LIST_SIZE		0x2060
#define XDMA_CHANNEL_DESCRIPTOR_LIST_SIZE_DESCRIPTOR_COUNT	OSI_GENMASK(15, 0)

#define XDMA_CHANNEL_SINGLE_TRANSFER_SOURCE_ADDRESS_LOW	0x2080
#define XDMA_CHANNEL_SINGLE_TRANSFER_SOURCE_ADDRESS_HIGH	0x20a0
#define XDMA_CHANNEL_SINGLE_TRANSFER_DESTINATION_ADDRESS_LOW	0x20c0
#define XDMA_CHANNEL_SINGLE_TRANSFER_DESTINATION_ADDRESS_HIGH	0x2100
#define XDMA_CHANNEL_SINGLE_TRANSFER_SIZE		0x2120

#define XDMA_CHANNEL_TRANSFER_DOORBELL		0x2140
#define XDMA_CHANNEL_TRANSFER_DOORBELL_DOORBELL	BIT(0)
#define XDMA_CHANNEL_TRANSFER_DOORBELL_NUM_DESC_ADDED	GENMASK(15, 8)
#define XDMA_CHANNEL_TRANSFER_DOORBELL_NUM_DESC_ADDED_SHIFT	8

#define XDMA_CHANNEL_STATUS				0x2160
#define XDMA_CHANNEL_STATUS_LAST_DESC_COMPLETED_ADDRESS_VALID	BIT(9)

#define XDMA_CHANNEL_STATUS_LAST_DESC_COMPLETED_ADDRESS_LO	0x2280
#define XDMA_CHANNEL_STATUS_LAST_DESC_COMPLETED_ADDRESS_HI	0x22a0

#define XDMA_CHANNEL_STATUS_LAST_DESC_COMPLETED_INDEX	0x22c0
#define XDMA_CHANNEL_STATUS_LAST_DESC_COMPLETED_INDEX_VALUE	GENMASK(15, 0)

#define XDMA_CHANNEL_FUNC_ERROR_STATUS      0x23e0

/* MSI channel registers */
#define XDMA_MSI_CHANNEL_CFG_INTR	0x2420
#define XDMA_MSI_CHANNEL_CFG_INTR_MASK		OSI_BIT(0)
#define XDMA_MSI_CHANNEL_CFG_INTR_DESTINATION	OSI_BIT(1)

#define XDMA_MSI_CHANNEL_CFG_INTR_ID	0x2440

#define XDMA_MSI_CHANNEL_CFG_INTR_MOD	0x2460
#define XDMA_MSI_CHANNEL_CFG_INTR_MOD_SCALE		OSI_GENMASK(12, 10)
#define XDMA_MSI_CHANNEL_CFG_INTR_ID_VALUE		OSI_GENMASK(9, 0)

#define XDMA_MSI_CHANNEL_PRIMARY_INTR_STATUS	0x2480
#define XDMA_MSI_CHANNEL_PRIMARY_INTR_STATUS_INTR_START_PROCESSING	OSI_BIT(0)
#define XDMA_MSI_CHANNEL_PRIMARY_INTR_STATUS_INTR_END_PROCESSING	OSI_BIT(1)
#define XDMA_MSI_CHANNEL_PRIMARY_INTR_STATUS_FUNC_ERR_VALID		OSI_GENMASK(15, 8)
#define XDMA_MSI_CHANNEL_PRIMARY_INTR_STATUS_NEW_XFER_VALID		OSI_GENMASK(23, 16)

#define XDMA_CHANNEL_DEBUG_REGISTER_4	0x2580
#define XDMA_CHANNEL_DEBUG_REGISTER_4_INTR_ENGINE_MSI_CHAN_ISR_INPROG_FSM	GENMASK(3, 2)
#define XDMA_CHANNEL_DEBUG_REGISTER_4_INTR_ENGINE_MSI_CHAN_MSI_DISP_FSM		GENMASK(5, 4)
#define XDMA_CHANNEL_DEBUG_REGISTER_4_INTR_ENGINE_MSI_CHAN_INTR_MOD_FSM		BIT(6)
#define XDMA_CHANNEL_DEBUG_REGISTER_4_INTR_ENGINE_MSI_CHAN_MSI_DISP_SHADOW_GEN_STATUS_MSI	BIT(9)
#define XDMA_CHANNEL_DEBUG_REGISTER_4_INTR_ENGINE_MSI_CHAN_MSI_DISP_SHADOW_FUNC_ERR_DETECTED	BIT(8)

struct xdma_attr {
	uint32_t wm:1;
};

struct xdma_hw_desc {
	volatile union {
		struct xdma_attr ctrl_e;
		uint32_t attr;
	} attr_reg;
	uint32_t sar_low;
	uint32_t sar_high;
	uint32_t dar_low;
	uint32_t dar_high;
	uint32_t size;
	uint32_t resv1;
	uint32_t resv2;
};

static inline unsigned int xdma_common_rd(void __iomem *p, unsigned int offset)
{
	return readl(p + offset);
}

static inline void xdma_common_wr(void __iomem *p, unsigned int val, unsigned int offset)
{
	writel(val, p + offset);
}

static inline void xdma_channel_wr(void __iomem *p, unsigned char c, unsigned int val, u32 offset)
{
	writel(val, (0x4 * c) + p + offset);
}

static inline unsigned int xdma_channel_rd(void __iomem *p, unsigned char c, u32 offset)
{
	return readl((0x4 * c) + p + offset);
}

irqreturn_t xdma_irq(int irq, void *cookie);
irqreturn_t xdma_irq_handler(int irq, void *cookie);

void *tegra264_pcie_xdma_initialize(struct tegra_pcie_dma_init_info *info, void *priv_dma);
tegra_pcie_dma_status_t tegra264_pcie_xdma_set_msi(void *cookie, u64 msi_addr, u32 msi_data);
tegra_pcie_dma_status_t tegra264_pcie_xdma_submit_xfer(void *cookie,
						       struct tegra_pcie_dma_xfer_info *tx_info);
bool tegra264_pcie_xdma_stop(void *cookie);
void tegra264_pcie_xdma_deinit(void *cookie, void *priv_dma);
#endif // TEGRA264_PCIE_XDMA_OSI_H
