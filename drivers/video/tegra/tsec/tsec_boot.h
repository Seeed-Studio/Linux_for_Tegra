// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION & AFFILIATES.  All rights reserved.
 *
 * Tegra TSEC Module Support
 */

#ifndef TSEC_BOOT_H
#define TSEC_BOOT_H

#define RISCV_IDLE_TIMEOUT_DEFAULT    100000  /* 100 milliseconds */
#define RISCV_IDLE_TIMEOUT_LONG       2000000 /* 2 seconds */
#define RISCV_IDLE_CHECK_PERIOD       10      /* 10 usec */
#define RISCV_IDLE_CHECK_PERIOD_LONG  1000    /* 1 milliseconds */

/* Image descriptor format */
struct RM_RISCV_UCODE_DESC {
	/*
	 * Version 1
	 * Version 2
	 * Vesrion 3 = for Partition boot
	 * Vesrion 4 = for eb riscv boot
	 */
	u32  version; /* structure version */
	u32  bootloaderOffset;
	u32  bootloaderSize;
	u32  bootloaderParamOffset;
	u32  bootloaderParamSize;
	u32  riscvElfOffset;
	u32  riscvElfSize;
	u32  appVersion; /* Changelist number associated with the image */
	/*
	 * Manifest contains information about Monitor and it is
	 * input to BR
	 */
	u32  manifestOffset;
	u32  manifestSize;
	/*
	 * Monitor Data offset within RISCV image and size
	 */
	u32  monitorDataOffset;
	u32  monitorDataSize;
	/*
	 * Monitor Code offset withtin RISCV image and size
	 */
	u32  monitorCodeOffset;
	u32  monitorCodeSize;
	u32  bIsMonitorEnabled;
	/*
	 * Swbrom Code offset within RISCV image and size
	 */
	u32  swbromCodeOffset;
	u32  swbromCodeSize;
	/*
	 * Swbrom Data offset within RISCV image and size
	 */
	u32  swbromDataOffset;
	u32  swbromDataSize;
};

struct riscv_image_desc {
	u32 manifest_offset;
	u32 manifest_size;
	u32 data_offset;
	u32 data_size;
	u32 code_offset;
	u32 code_size;
};

struct riscv_data {
	bool valid;
	struct riscv_image_desc desc;
	dma_addr_t backdoor_img_iova;
	u32 *backdoor_img_va;
	size_t backdoor_img_size;
	bool ipc_mem_initialised;
	void __iomem *ipc_co_va;
	dma_addr_t ipc_co_iova;
};

int tsec_kickoff_boot(struct platform_device *pdev);
int tsec_finalize_poweron(struct platform_device *dev);
int tsec_prepare_poweroff(struct platform_device *dev);

#endif /* TSEC_BOOT_H */
