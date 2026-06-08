// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES.  All rights reserved.
 *
 * Tegra TSEC Module Support
 */

#include "tsec_linux.h"
#include "tsec.h"
#include "tsec_boot.h"
#include "tsec_regs.h"
#include "tsec_cmds.h"
#include "tsec_comms/tsec_comms.h"
#include "tsec_comms/tsec_comms_plat.h"

#define CMD_INTERFACE_TEST 0
#if CMD_INTERFACE_TEST
#define NUM_OF_CMDS_TO_TEST (5)
#endif



/* Set this to 1 to force backdoor boot */
#define TSEC_FORCE_BACKDOOR_BOOT	(0)

/* Pointer to this device */
struct platform_device *g_tsec;

/* tsec device private data */
typedef void (*plat_work_cb_t)(void *);
struct tsec_device_priv_data {
	struct platform_device *pdev;
	struct delayed_work     poweron_work;
	u32                     fwreq_retry_interval_ms;
	u32                     fwreq_duration_ms;
	u32                     fwreq_fail_threshold_ms;
	struct work_struct      plat_work;
	plat_work_cb_t          plat_cb;
	void                   *plat_cb_ctx;
};

struct carveout_info {
	u64 base;
	u64 size;
};

/*
 * Platform specific APIs to be used by platform independent comms library
 */

static DEFINE_MUTEX(s_plat_comms_mutex);

void tsec_plat_acquire_comms_mutex(void)
{
	mutex_lock(&s_plat_comms_mutex);
}

void tsec_plat_release_comms_mutex(void)
{
	mutex_unlock(&s_plat_comms_mutex);
}

static void tsec_plat_work_handler(struct work_struct *work)
{
	struct tsec_device_priv_data *tsec_priv_data;
	plat_work_cb_t cb;
	void *cb_ctx;

	tsec_priv_data = container_of(work, struct tsec_device_priv_data,
		plat_work);
	cb = tsec_priv_data->plat_cb;
	cb_ctx = tsec_priv_data->plat_cb_ctx;
	tsec_priv_data->plat_cb = NULL;
	tsec_priv_data->plat_cb_ctx = NULL;
	if (cb)
		cb(cb_ctx);
}

void tsec_plat_queue_work(plat_work_cb_t cb, void *ctx)
{
	struct tsec_device_data *pdata = platform_get_drvdata(g_tsec);
	struct tsec_device_priv_data *tsec_priv_data =
		(struct tsec_device_priv_data *)pdata->private_data;

	tsec_priv_data->plat_cb = cb;
	tsec_priv_data->plat_cb_ctx = ctx;
	schedule_work(&tsec_priv_data->plat_work);

}

void tsec_plat_udelay(u64 usec)
{
	udelay(usec);
}

void tsec_plat_reg_write(u32 r, u32 v)
{
	tsec_writel(platform_get_drvdata(g_tsec), r, v);
}

u32 tsec_plat_reg_read(u32 r)
{
	return tsec_readl(platform_get_drvdata(g_tsec), r);
}

/*
 * Helpers to initialise riscv_data with image and descriptor info
 */

static int tsec_compute_ucode_offsets(struct platform_device *dev,
	struct riscv_data *rv_data, const struct firmware *fw_desc)
{
	struct RM_RISCV_UCODE_DESC *ucode_desc;

	ucode_desc = (struct RM_RISCV_UCODE_DESC *)fw_desc->data;
	rv_data->desc.manifest_offset = le32_to_cpu((__force __le32)ucode_desc->manifestOffset);
	rv_data->desc.code_offset = le32_to_cpu((__force __le32)ucode_desc->monitorCodeOffset);
	rv_data->desc.data_offset = le32_to_cpu((__force __le32)ucode_desc->monitorDataOffset);

	return 0;
}

static int tsec_read_img_and_desc(struct platform_device *dev,
	const char *desc_name, const char *image_name)
{
	int err, w;
	const struct firmware *fw_desc, *fw_image;
	struct tsec_device_data *pdata = platform_get_drvdata(dev);
	struct riscv_data *rv_data = (struct riscv_data *)pdata->riscv_data;

	if (!rv_data) {
		dev_err(&dev->dev, "riscv data is NULL\n");
		return -ENODATA;
	}

	err = request_firmware(&fw_desc, desc_name, &dev->dev);
	if (err) {
		dev_err(&dev->dev, "failed to get tsec desc binary\n");
		return -ENOENT;
	}
	err = request_firmware(&fw_image, image_name, &dev->dev);
	if (err) {
		dev_err(&dev->dev, "failed to get tsec image binary\n");
		release_firmware(fw_desc);
		return -ENOENT;
	}

	/* Allocate memory to copy image */
	rv_data->backdoor_img_size = fw_image->size;
	rv_data->backdoor_img_va = dma_alloc_attrs(&dev->dev,
		rv_data->backdoor_img_size, &rv_data->backdoor_img_iova,
		GFP_KERNEL, DMA_ATTR_FORCE_CONTIGUOUS);
	if (!rv_data->backdoor_img_va) {
		dev_err(&dev->dev, "dma memory allocation failed");
		err = -ENOMEM;
		goto clean_up;
	}

	/* Copy the whole image taking endianness into account */
	for (w = 0; w < fw_image->size/sizeof(u32); w++)
		rv_data->backdoor_img_va[w] = le32_to_cpu(((__le32 *)fw_image->data)[w]);
#if (KERNEL_VERSION(5, 14, 0) <= LINUX_VERSION_CODE)
	arch_invalidate_pmem(rv_data->backdoor_img_va, rv_data->backdoor_img_size);
#else
	__flush_dcache_area((void *)rv_data->backdoor_img_va, fw_image->size);
#endif

	/* Read the offsets from desc binary */
	err = tsec_compute_ucode_offsets(dev, rv_data, fw_desc);
	if (err) {
		dev_err(&dev->dev, "failed to parse desc binary\n");
		goto clean_up;
	}

	rv_data->valid = true;
	release_firmware(fw_desc);
	release_firmware(fw_image);

	return 0;

clean_up:
	if (rv_data->backdoor_img_va) {
		dma_free_attrs(&dev->dev, rv_data->backdoor_img_size,
			rv_data->backdoor_img_va, rv_data->backdoor_img_iova,
			DMA_ATTR_FORCE_CONTIGUOUS);
		rv_data->backdoor_img_va = NULL;
		rv_data->backdoor_img_iova = 0;
	}
	release_firmware(fw_desc);
	release_firmware(fw_image);
	return err;
}

static int tsec_riscv_data_init(struct platform_device *dev)
{
	int err = 0;
	struct tsec_device_data *pdata = platform_get_drvdata(dev);
	struct riscv_data *rv_data = (struct riscv_data *)pdata->riscv_data;

	if (rv_data)
		return 0;

	rv_data = kzalloc(sizeof(*rv_data), GFP_KERNEL);
	if (!rv_data)
		return -ENOMEM;
	pdata->riscv_data = rv_data;

	err = tsec_read_img_and_desc(dev, pdata->riscv_desc_bin,
				pdata->riscv_image_bin);

	if (err || !rv_data->valid) {
		dev_err(&dev->dev, "ucode not valid");
		goto clean_up;
	}

	return 0;

clean_up:
	dev_err(&dev->dev, "RISC-V init sw failed: err=%d", err);
	kfree(rv_data);
	pdata->riscv_data = NULL;
	return err;
}

static int tsec_riscv_data_deinit(struct platform_device *dev)
{
	struct tsec_device_data *pdata = platform_get_drvdata(dev);
	struct riscv_data *rv_data = (struct riscv_data *)pdata->riscv_data;

	if (!rv_data)
		return 0;

	if (rv_data->backdoor_img_va) {
		dma_free_attrs(&dev->dev, rv_data->backdoor_img_size,
			rv_data->backdoor_img_va, rv_data->backdoor_img_iova,
			DMA_ATTR_FORCE_CONTIGUOUS);
		rv_data->backdoor_img_va = NULL;
		rv_data->backdoor_img_iova = 0;
	}
	kfree(rv_data);
	pdata->riscv_data = NULL;
	return 0;
}

/*
 * APIs to load firmware and boot tsec
 */

static int get_carveout_info_4(
	struct platform_device *dev, struct carveout_info *co_info)
{
#if (KERNEL_VERSION(5, 14, 0) <= LINUX_VERSION_CODE)
	int err;
	phys_addr_t base;
	u64 size;
	struct tegra_mc *mc;

	mc = devm_tegra_memory_controller_get(&dev->dev);
	if (IS_ERR(mc))
		return PTR_ERR(mc);
	err = tegra_mc_get_carveout_info(mc, 4, &base, &size);
	if (err)
		return err;
	co_info->base = (u64)base;
	co_info->size = size;

	return 0;
#else
	int err;
	struct mc_carveout_info mc_co_info;

	err = mc_get_carveout_info(&mc_co_info, NULL, MC_SECURITY_CARVEOUT4);
	if (err)
		return err;
	co_info->base = mc_co_info.base;
	co_info->size = mc_co_info.size;

	return 0;
#endif
}

static int get_carveout_info_lite42(
	struct platform_device *dev, struct carveout_info *co_info)
{
#define LITE42_BASE                        (0x2c10000 + 0x7324)
#define LITE42_SIZE                        (12)
#define LITE42_BOM_OFFSET                  (0)
#define LITE42_BOM_HI_OFFSET               (4)
#define LITE42_SIZE_128KB_OFFSET           (8)

	void __iomem *lit42_regs;

	lit42_regs = ioremap(LITE42_BASE, LITE42_SIZE);
	if (!lit42_regs) {
		dev_err(&dev->dev, "lit42_regs VA mapping failed\n");
		return -ENOMEM;
	}

	co_info->base = readl(lit42_regs + LITE42_BOM_OFFSET) |
		((u64)readl(lit42_regs + LITE42_BOM_HI_OFFSET) & 0xFF) << 32;
	co_info->size = readl(lit42_regs + LITE42_SIZE_128KB_OFFSET);
	co_info->size <<= 17; /* Convert to bytes. */
	iounmap(lit42_regs);

	return 0;

#undef LITE42_BASE
#undef LITE42_SIZE
#undef LITE42_BOM_OFFSET
#undef LITE42_BOM_HI_OFFSET
#undef LITE42_SIZE_128KB_OFFSET
}

int tsec_finalize_poweron(struct platform_device *dev)
{
#if CMD_INTERFACE_TEST
	union RM_FLCN_CMD cmd;
	struct RM_FLCN_HDCP22_CMD_MONITOR_OFF hdcp22Cmd;
	u8 cmd_size = RM_FLCN_CMD_SIZE(HDCP22, MONITOR_OFF);
	u32 cmdDataSize = RM_FLCN_CMD_BODY_SIZE(HDCP22, MONITOR_OFF);
	int idx;
#endif //CMD_INTERFACE_TEST
	int err = 0;
	struct riscv_data *rv_data;
	u32 val;
	phys_addr_t img_pa, pa;
	struct iommu_domain *domain;
	void __iomem *cpuctl_addr, *retcode_addr, *mailbox0_addr;
	struct carveout_info img_co_info;
	unsigned int img_co_gscid = 0x0;
	struct tsec_device_data *pdata = platform_get_drvdata(dev);
	struct carveout_info ipc_co_info;
	void __iomem *ipc_co_va = NULL;
	dma_addr_t ipc_co_iova = 0;
	dma_addr_t ipc_co_iova_with_streamid;
	struct tsec_reg_offsets_t *reg_off = pdata->tsec_reg_offsets;

	if (!pdata) {
		dev_err(&dev->dev, "no platform data\n");
		return -ENODATA;
	}

	/* Init rv_data with image and descriptor info */
	err = tsec_riscv_data_init(dev);
	if (err)
		return err;
	rv_data = (struct riscv_data *)pdata->riscv_data;

	/* Get pa of memory having tsec fw image */
	err = get_carveout_info_4(dev, &img_co_info);
	if (err) {
		dev_err(&dev->dev, "Carveout memory allocation failed");
		err = -ENOMEM;
		goto clean_up;
	}
	dev_dbg(&dev->dev, "CARVEOUT4 base=0x%llx size=0x%llx\n",
		img_co_info.base, img_co_info.size);
	/* Get iommu domain to convert iova to physical address for backdoor boot*/
	domain = iommu_get_domain_for_dev(&dev->dev);
	if ((img_co_info.base) && !(TSEC_FORCE_BACKDOOR_BOOT)) {
		img_pa = img_co_info.base;
		img_co_gscid = 0x4;
		dev_info(&dev->dev, "RISC-V booting from GSC\n");
	} else {
		/* For backdoor non-secure boot only. It can be depricated later */
		img_pa = iommu_iova_to_phys(domain, rv_data->backdoor_img_iova);
		dev_info(&dev->dev, "RISC-V boot using kernel allocated Mem\n");
	}

	/* Get va and iova of careveout used for ipc */
	err = get_carveout_info_lite42(dev, &ipc_co_info);
	if (err) {
		dev_err(&dev->dev, "IPC Carveout memory allocation failed");
		err = -ENOMEM;
		goto clean_up;
	}
	dev_dbg(&dev->dev, "IPCCO base=0x%llx size=0x%llx\n", ipc_co_info.base, ipc_co_info.size);

	if (!(rv_data->ipc_mem_initialised)) {
		ipc_co_va = ioremap(ipc_co_info.base, ipc_co_info.size);
		if (!ipc_co_va) {
			dev_err(&dev->dev, "IPC Carveout memory VA mapping failed");
			err = -ENOMEM;
			goto clean_up;
		}
		dev_dbg(&dev->dev, "IPCCO va=0x%llx pa=0x%llx\n",
			(__force phys_addr_t)(ipc_co_va), page_to_phys(vmalloc_to_page(ipc_co_va)));
#if (KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE)
		ipc_co_iova = dma_map_page_attrs(&dev->dev, vmalloc_to_page(ipc_co_va),
			offset_in_page(ipc_co_va), ipc_co_info.size, DMA_BIDIRECTIONAL, 0);
#else
		ipc_co_iova = dma_map_page(&dev->dev, vmalloc_to_page(ipc_co_va),
			offset_in_page(ipc_co_va), ipc_co_info.size, DMA_BIDIRECTIONAL);
#endif
		err = dma_mapping_error(&dev->dev, ipc_co_iova);
		if (err) {
			dev_err(&dev->dev, "IPC Carveout memory IOVA mapping failed");
			ipc_co_iova = 0;
			err = -ENOMEM;
			goto clean_up;
		}
		dev_dbg(&dev->dev, "IPCCO iova=0x%llx\n", ipc_co_iova);
		rv_data->ipc_co_va = ipc_co_va;
		rv_data->ipc_co_iova = ipc_co_iova;
		rv_data->ipc_mem_initialised = true;
	} else {
		ipc_co_va = rv_data->ipc_co_va;
		ipc_co_iova = rv_data->ipc_co_iova;
	}

	/* Lock channel so that non-TZ channel request can't write non-THI region */
	tsec_writel(pdata, reg_off->THI_SEC_0, reg_off->THI_SEC_CHLOCK);

	/* Select RISC-V core */
	tsec_writel(pdata, reg_off->RISCV_BCR_CTRL,
			reg_off->RISCV_BCR_CTRL_CORE_SELECT_RISCV);

	/* Program manifest start address */
	pa = (img_pa + rv_data->desc.manifest_offset) >> 8;
	tsec_writel(pdata, reg_off->RISCV_BCR_DMAADDR_PKCPARAM_LO,
			lower_32_bits(pa));
	tsec_writel(pdata, reg_off->RISCV_BCR_DMAADDR_PKCPARAM_HI,
			upper_32_bits(pa));

	/* Program FMC code start address */
	pa = (img_pa + rv_data->desc.code_offset) >> 8;
	tsec_writel(pdata, reg_off->RISCV_BCR_DMAADDR_FMCCODE_LO,
			lower_32_bits(pa));
	tsec_writel(pdata, reg_off->RISCV_BCR_DMAADDR_FMCCODE_HI,
			upper_32_bits(pa));

	/* Program FMC data start address */
	pa = (img_pa + rv_data->desc.data_offset) >> 8;
	tsec_writel(pdata, reg_off->RISCV_BCR_DMAADDR_FMCDATA_LO,
			lower_32_bits(pa));
	tsec_writel(pdata, reg_off->RISCV_BCR_DMAADDR_FMCDATA_HI,
			upper_32_bits(pa));

	/* Program DMA config registers */
	tsec_writel(pdata, reg_off->RISCV_BCR_DMACFG_SEC,
			tsec_riscv_bcr_dmacfg_sec_gscid_f(img_co_gscid, reg_off->RISCV_BCR_DMACFG_SEC));
	tsec_writel(pdata, reg_off->RISCV_BCR_DMACFG,
			reg_off->RISCV_BCR_DMACFG_TARGET_LOCAL_FB |
			reg_off->RISCV_BCR_DMACFG_LOCK_LOCKED);

	/* Pass the address of ipc carveout via mailbox registers */
	ipc_co_iova_with_streamid = (ipc_co_iova | TSEC_RISCV_SMMU_STREAMID1);
	tsec_writel(pdata, reg_off->FALCON_MAILBOX0,
		lower_32_bits((unsigned long long)ipc_co_iova_with_streamid));
	tsec_writel(pdata, reg_off->FALCON_MAILBOX1,
		upper_32_bits((unsigned long long)ipc_co_iova_with_streamid));

	/* Kick start RISC-V and let BR take over */
	tsec_writel(pdata, reg_off->RISCV_CPUCTL,
			reg_off->RISCV_CPUCTL_STARTCPU_TRUE);

	cpuctl_addr = pdata->reg_aperture + reg_off->RISCV_CPUCTL;
	retcode_addr = pdata->reg_aperture + reg_off->RISCV_BR_RETCODE;
	mailbox0_addr = pdata->reg_aperture + reg_off->FALCON_MAILBOX0;

	/* Check BR return code */
	err  = readl_poll_timeout(retcode_addr, val,
		(tsec_riscv_br_retcode_result_v(val, reg_off->RISCV_BR_RETCODE_RESULT) ==
		reg_off->RISCV_BR_RETCODE_RESULT_PASS),
		RISCV_IDLE_CHECK_PERIOD,
		RISCV_IDLE_TIMEOUT_DEFAULT);
	if (err) {
		dev_err(&dev->dev, "BR return code timeout! val=0x%x\n", val);
		goto clean_up;
	}

	/* Check cpuctl active state */
	err  = readl_poll_timeout(cpuctl_addr, val,
		(tsec_riscv_cpuctl_active_stat_v(val, reg_off->RISCV_CPUCTL_ACTIVE_STAT) ==
		reg_off->RISCV_CPUCTL_ACTIVE_STAT_ACTIVE),
		RISCV_IDLE_CHECK_PERIOD,
		RISCV_IDLE_TIMEOUT_DEFAULT);
	if (err) {
		dev_err(&dev->dev, "cpuctl active state timeout! val=0x%x\n",
			val);
		goto clean_up;
	}

	/* Check tsec has reached a proper initialized state */
	err  = readl_poll_timeout(mailbox0_addr, val,
		(val == TSEC_RISCV_INIT_SUCCESS),
		RISCV_IDLE_CHECK_PERIOD_LONG,
		RISCV_IDLE_TIMEOUT_LONG);
	if (err) {
		dev_err(&dev->dev,
			"not reached initialized state, timeout! val=0x%x\n",
			val);
		goto clean_up;
	}

	/* Mask out TSEC SWGEN1 Interrupt.
	 * Host should not receive SWGEN1, as it uses only SWGEN0 for message
	 * communication with tsec. RISCV Fw is generating SWGEN1 for some debug
	 * purpose at below path,, we want to ensure that this doesn't interrupt
	 * Arm driver code.
	 * nvriscv/drivers/src/debug/debug.c:164: irqFireSwGen(SYS_INTR_SWGEN1)
	 */
	tsec_writel(pdata, reg_off->RISCV_IRQMCLR_0,
		reg_off->RISCV_IRQMCLR_SWGEN1_SET);

	/* initialise the comms library before enabling msg interrupt */
	tsec_comms_initialize((__force u64)ipc_co_va, ipc_co_info.size);

	/* enable message interrupt from tsec to ccplex */
	enable_irq(pdata->irq);

	/* Booted-up successfully */
	dev_info(&dev->dev, "RISC-V boot success\n");

#if CMD_INTERFACE_TEST
	pr_debug("cmd_size=%d, cmdDataSize=%d\n", cmd_size, cmdDataSize);
	msleep(3000);
	for (idx = 0; idx < NUM_OF_CMDS_TO_TEST; idx++) {
		hdcp22Cmd.cmdType = RM_FLCN_HDCP22_CMD_ID_MONITOR_OFF;
		hdcp22Cmd.sorNum = -1;
		hdcp22Cmd.dfpSublinkMask = -1;
		cmd.cmdGen.hdr.size = cmd_size;
		cmd.cmdGen.hdr.unitId = RM_GSP_UNIT_HDCP22WIRED;
		cmd.cmdGen.hdr.seqNumId = idx+1;
		cmd.cmdGen.hdr.ctrlFlags = 0;
		memcpy(&cmd.cmdGen.cmd, &hdcp22Cmd, cmdDataSize);
		tsec_comms_send_cmd((void *)&cmd, 0, NULL, NULL);
		msleep(200);
	}
#endif //CMD_INTERFACE_TEST

	return err;

clean_up:
	if (ipc_co_iova) {
#if (KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE)
		dma_unmap_page_attrs(&dev->dev, ipc_co_iova,
			ipc_co_info.size, DMA_BIDIRECTIONAL, 0);
#else
		dma_unmap_page(&dev->dev, ipc_co_iova,
			ipc_co_info.size, DMA_BIDIRECTIONAL);
#endif
	}
	if (ipc_co_va)
		iounmap(ipc_co_va);
	tsec_riscv_data_deinit(dev);
	return err;
}

int tsec_prepare_poweroff(struct platform_device *dev)
{
	struct tsec_device_data *pdata = platform_get_drvdata(dev);

	if (!pdata) {
		dev_err(&dev->dev, "no platform data\n");
		return -ENODATA;
	}

	if (pdata->irq < 0) {
		dev_err(&dev->dev, "found interrupt number to be negative\n");
		return -ENODATA;
	}
	disable_irq_nosync((unsigned int) pdata->irq);

	return 0;
}

/*
 * Irq top and bottom half handling. On receiving a message interrupt from
 * the bottom half we call the comms lib API to drain and handle that message.
 */

static irqreturn_t tsec_irq_top_half(int irq, void *dev_id)
{
	unsigned long flags;
	struct platform_device *pdev = (struct platform_device *)(dev_id);
	struct tsec_device_data *pdata = platform_get_drvdata(pdev);
	irqreturn_t irq_ret_val = IRQ_HANDLED;
	u32 irq_status;
	struct tsec_reg_offsets_t *reg_off = pdata->tsec_reg_offsets;

	spin_lock_irqsave(&pdata->mirq_lock, flags);

	/* Read the interrupt status */
	irq_status = tsec_readl(pdata, reg_off->RISCV_IRQSTAT_0);

	/* Clear the interrupt */
	tsec_writel(pdata, reg_off->THI_INT_STATUS_0,
			reg_off->THI_INT_STATUS_CLR_0);

	/* Wakeup threaded handler for SWGEN0 Irq */
	if (irq_status & reg_off->RISCV_IRQSTAT_SWGEN0) {
		/* Clear SWGEN0 Interrupt */
		tsec_writel(pdata, reg_off->RISCV_IRQSCLR_0,
			reg_off->RISCV_IRQSCLR_SWGEN0_SET);
		/* Mask the interrupt.
		 * Clear RISCV Mask for SWGEN0, so that no more SWGEN0
		 * interrupts will be routed to CCPLEX, it will be re-enabled
		 * by the bottom half
		 */
		tsec_writel(pdata, reg_off->RISCV_IRQMCLR_0,
			reg_off->RISCV_IRQMCLR_SWGEN0_SET);
		irq_ret_val = IRQ_WAKE_THREAD;
		irq_status &= ~(reg_off->RISCV_IRQSTAT_SWGEN0);
	}

	/* RISCV FW is generating SWGEN1 when it logs something
	 * in the print buffer at below path
	 * nvriscv/drivers/src/debug/debug.c:164: irqFireSwGen(SYS_INTR_SWGEN1)
	 * We dont want to pull out the print buffer from CCPLEX
	 * hence we just mask out SWGEN1 interrupt here so that it
	 * is not received any further
	 */
	if (irq_status & reg_off->RISCV_IRQSTAT_SWGEN1) {
		tsec_writel(pdata, reg_off->RISCV_IRQMCLR_0,
			reg_off->RISCV_IRQMCLR_SWGEN1_SET);
		irq_status &= ~(reg_off->RISCV_IRQSTAT_SWGEN1);
	}

	spin_unlock_irqrestore(&pdata->mirq_lock, flags);

	return irq_ret_val;
}

static irqreturn_t tsec_irq_bottom_half(int irq, void *args)
{
	struct tsec_device_data *pdata;
	struct tsec_reg_offsets_t *reg_off;
	pdata = platform_get_drvdata(g_tsec);
	reg_off = pdata->tsec_reg_offsets;

	/* Call into the comms lib API to drain the message */
	tsec_comms_drain_msg(true);

	/* Unmask the interrupt.
	 * Set RISCV Mask for SWGEN0, so that it is re-enabled
	 * and if it is pending the CCPLEX will be interrupted
	 * by this the top half
	 */
	tsec_writel(pdata, reg_off->RISCV_IRQMSET_0,
		reg_off->RISCV_IRQMSET_SWGEN0_SET);
	return IRQ_HANDLED;
}

/*
 * Tsec power on handler attempts to boot tsec from a worker thread as
 * soon as the fw descriptor image is available
 */

static void tsec_poweron_handler(struct work_struct *work)
{
	struct tsec_device_priv_data *tsec_priv_data;
	struct tsec_device_data *pdata;
	const struct firmware *tsec_fw_desc;
	int err;

	tsec_priv_data = container_of(to_delayed_work(work), struct tsec_device_priv_data,
		poweron_work);
	pdata = platform_get_drvdata(tsec_priv_data->pdev);
	err = firmware_request_nowarn(&tsec_fw_desc, pdata->riscv_desc_bin,
		&(tsec_priv_data->pdev->dev));
	tsec_priv_data->fwreq_duration_ms += tsec_priv_data->fwreq_retry_interval_ms;

	if (!err) {
		dev_info(&(tsec_priv_data->pdev->dev),
			"tsec fw req success in %d ms\n",
			tsec_priv_data->fwreq_duration_ms);
		release_firmware(tsec_fw_desc);
		err = tsec_poweron(&(tsec_priv_data->pdev->dev));
		if (err)
			dev_dbg(&(tsec_priv_data->pdev->dev),
				"tsec_poweron returned with error: %d\n",
				err);
	} else if (tsec_priv_data->fwreq_duration_ms < tsec_priv_data->fwreq_fail_threshold_ms) {
		dev_info(&(tsec_priv_data->pdev->dev),
			"retry tsec fw req, total retry duration %d ms\n",
			tsec_priv_data->fwreq_duration_ms);
		schedule_delayed_work(&tsec_priv_data->poweron_work,
			msecs_to_jiffies(tsec_priv_data->fwreq_retry_interval_ms));
	} else {
		dev_err(&(tsec_priv_data->pdev->dev),
			"tsec boot failure, fw not available within %d ms\n",
			tsec_priv_data->fwreq_fail_threshold_ms);
	}
}

/*
 * Register irq handlers and kick off tsec boot from a separate worker thread
 */

int tsec_kickoff_boot(struct platform_device *pdev)
{
	int ret = 0;
	struct tsec_device_data *pdata = platform_get_drvdata(pdev);
	struct tsec_device_priv_data *tsec_priv_data = NULL;

	tsec_priv_data = devm_kzalloc(&pdev->dev, sizeof(*tsec_priv_data), GFP_KERNEL);
	if (!tsec_priv_data)
		return -ENOMEM;
	tsec_priv_data->pdev = pdev;
	INIT_DELAYED_WORK(&tsec_priv_data->poweron_work, tsec_poweron_handler);
	INIT_WORK(&tsec_priv_data->plat_work, tsec_plat_work_handler);
	tsec_priv_data->fwreq_retry_interval_ms = 100;
	tsec_priv_data->fwreq_duration_ms = 0;
	tsec_priv_data->fwreq_fail_threshold_ms = tsec_priv_data->fwreq_retry_interval_ms * 10;
	pdata->private_data = tsec_priv_data;

	spin_lock_init(&pdata->mirq_lock);
	ret = request_threaded_irq(pdata->irq, tsec_irq_top_half,
		tsec_irq_bottom_half, 0, "tsec_riscv_irq", pdev);
	if (ret) {
		dev_err(&pdev->dev, "CMD: failed to request irq %d\n", ret);
		devm_kfree(&pdev->dev, tsec_priv_data);
		return ret;
	}

	/* keep irq disabled */
	disable_irq(pdata->irq);

	g_tsec = pdev;

	/* schedule work item to turn on tsec */
	schedule_delayed_work(&tsec_priv_data->poweron_work,
		msecs_to_jiffies(tsec_priv_data->fwreq_retry_interval_ms));

	return 0;
}

u32 tsec_plat_cmdq_head_r(u32 r)
{
	u32 offset = U32_MAX;
	struct tsec_device_data *pdata = platform_get_drvdata(g_tsec);
	struct tsec_reg_offsets_t *reg_off = pdata->tsec_reg_offsets;

	if (reg_off != NULL) {
		offset = reg_off->QUEUE_HEAD_0 + (r) * 8;
	}

	return offset;
}

u32 tsec_plat_cmdq_tail_r(u32 r)
{
	u32 offset = U32_MAX;
	struct tsec_device_data *pdata = platform_get_drvdata(g_tsec);
	struct tsec_reg_offsets_t *reg_off = pdata->tsec_reg_offsets;

	if (reg_off != NULL) {
		offset = reg_off->QUEUE_TAIL_0 + (r) * 8;
	}

	return offset;
}

u32 tsec_plat_msgq_head_r(u32 r)
{
	u32 offset = U32_MAX;
	struct tsec_device_data *pdata = platform_get_drvdata(g_tsec);
	struct tsec_reg_offsets_t *reg_off = pdata->tsec_reg_offsets;

	if (reg_off != NULL) {
		offset = reg_off->MSGQ_HEAD_0 + (r) * 8;
	}

	return offset;
}

u32 tsec_plat_msgq_tail_r(u32 r)
{
	u32 offset = U32_MAX;
	struct tsec_device_data *pdata = platform_get_drvdata(g_tsec);
	struct tsec_reg_offsets_t *reg_off = pdata->tsec_reg_offsets;

	if (reg_off != NULL) {
		offset = reg_off->MSGQ_TAIL_0 + (r) * 8;
	}

	return offset;
}

u32 tsec_plat_ememc_r(u32 r)
{
	u32 offset = U32_MAX;
	struct tsec_device_data *pdata = platform_get_drvdata(g_tsec);
	struct tsec_reg_offsets_t *reg_off = pdata->tsec_reg_offsets;

	if (reg_off != NULL) {
		offset = reg_off->EMEMC_0 + (r) * 8;
	}

	return offset;
}

u32 tsec_plat_ememd_r(u32 r)
{
	u32 offset = U32_MAX;
	struct tsec_device_data *pdata = platform_get_drvdata(g_tsec);
	struct tsec_reg_offsets_t *reg_off = pdata->tsec_reg_offsets;

	if (reg_off != NULL) {
		offset = reg_off->EMEMD_0 + (r) * 8;
	}

	return offset;
}
