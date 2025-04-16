/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES.  All rights reserved.
 *
 * Tegra TSEC Module Support
 */

#ifndef TSEC_H
#define TSEC_H

/*
 * TSEC Device Data Structure
 */

#define TSEC_CLK_NAME        "tsec"
#define TSEC_CLK_INDEX       (0)
#define EFUSE_CLK_NAME       "efuse"
#define EFUSE_CLK_INDEX      (1)
#define TSEC_PKA_CLK_NAME    "tsec_pka"
#define TSEC_PKA_CLK_INDEX   (2)
#define TSEC_NUM_OF_CLKS     (3)

struct tsec_device_data {
	void __iomem *reg_aperture;
	struct device_dma_parameters dma_parms;

	int irq;
	/* spin lock for module irq */
	spinlock_t mirq_lock;

	/* If module is powered on */
	bool power_on;

	struct clk *clk[TSEC_NUM_OF_CLKS];
	long rate[TSEC_NUM_OF_CLKS];
	 /* private platform data */
	void *private_data;
	/* owner platform_device */
	struct platform_device *pdev;

	/* reset control for this device */
	struct reset_control *reset_control;

	/* store the risc-v info */
	void *riscv_data;
	/* name of riscv descriptor binary */
	char *riscv_desc_bin;
	/* name of riscv image binary */
	char *riscv_image_bin;

#ifdef CONFIG_DEBUG_FS
	struct dentry *debug_root;
#endif /* CONFIG_DEBUG_FS */

	/* Number of bits for DMA mask - IOVA/PA number of bits */
	u8 dma_mask_bits;
};

/*
 * TSEC Register Access APIs
 */

void tsec_writel(struct tsec_device_data *pdata, u32 r, u32 v);
u32 tsec_readl(struct tsec_device_data *pdata, u32 r);


/*
 * TSEC power on/off APIs
 */

int tsec_poweron(struct device *dev);
int tsec_poweroff(struct device *dev);

#endif /* TSEC_H */
