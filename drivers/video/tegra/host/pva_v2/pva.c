// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2016-2025, NVIDIA CORPORATION.  All rights reserved.
 */

#include <nvidia/conftest.h>

#include "pva_mailbox.h"

#include <linux/workqueue.h>
#include "nvpva_client.h"
#include <linux/export.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/fs.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <linux/iommu.h>
#include <linux/reset.h>
#include <linux/version.h>
#include <linux/platform/tegra/emc_bwmgr.h>
#include <linux/nvhost.h>
#include <linux/interrupt.h>
#ifdef CONFIG_PVA_INTERRUPT_DISABLED
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#endif
#include <soc/tegra/virt/hv-ivc.h>
#include <dt-bindings/interconnect/tegra_icc_id.h>
#if KERNEL_VERSION(4, 15, 0) > LINUX_VERSION_CODE
#include <soc/tegra/chip-id.h>
#endif
#include <soc/tegra/fuse-helper.h>

#if !IS_ENABLED(CONFIG_TEGRA_GRHOST)
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#endif

#include "pva_mailbox_t23x.h"
#include "pva_interface_regs_t23x.h"
#include "pva_version_config_t23x.h"
#include "pva_ccq_t23x.h"
#include "nvpva_queue.h"
#include "pva_queue.h"
#include "pva.h"
#include "pva_regs.h"
#include "pva_mailbox_t19x.h"
#include "pva_interface_regs_t19x.h"
#include "pva_version_config_t19x.h"
#include "pva_ccq_t19x.h"
#include "pva-ucode-header.h"
#include "pva_system_allow_list.h"
#include "pva_iommu_context_dev.h"
#include "nvpva_syncpt.h"
#include "pva-fw-address-map.h"
#include "pva_sec_ec.h"
#include "pva-debug-buffer.h"

#ifdef CONFIG_TEGRA_T26X_GRHOST_PVA
#include "pva_t264.h"
#endif

#include "pva-debug-buffer.h"
#include "pva_trace.h"

/*
 * NO IOMMU set 0x60000000 as start address.
 * With IOMMU set 0x80000000(>2GB) as startaddress
 */
#define DRAM_PVA_IOVA_START_ADDRESS 0x80000000
#define DRAM_PVA_NO_IOMMU_START_ADDRESS 0x60000000

extern struct platform_driver nvpva_iommu_context_dev_driver;
static u32 vm_regs_sid_idx_t19x[] = {0, 0, 0, 0, 0, 0, 0, 0,
				     0, 0, 0, 0, 0, 0, 0, 0};
static u32 vm_regs_reg_idx_t19x[] = {0, 1, 1, 0, 0, 0, 0, 0,
				     0, 0, 0, 0, 0, 0, 0, 0};
#ifdef CONFIG_PVA_CO_DISABLED
static u32 vm_regs_sid_idx_t234[] = {1, 2, 3, 4, 5, 6, 7, 7,
				     8, 8, 8, 8, 8, 0, 0, 0};
#else
static u32 vm_regs_sid_idx_t234[] = {1, 2, 3, 4, 5, 6, 7, 7,
				     8, 0, 8, 8, 8, 0, 0, 0};
#endif
static u32 vm_regs_reg_idx_t234[] = {0, 1, 2, 3, 4, 5, 6, 7,
				     8, 8, 8, 9, 9, 0, 0, 0};
#ifndef CONFIG_TEGRA_T26X_GRHOST_PVA
static u32 *vm_regs_sid_idx_t264 = vm_regs_sid_idx_t234;
static u32 *vm_regs_reg_idx_t264 = vm_regs_reg_idx_t234;
#endif
static char *aux_dev_name = "16000000.pva0:pva0_niso1_ctx7";
static u32 aux_dev_name_len = 29;

struct nvhost_device_data t19_pva1_info = {
	.version = PVA_HW_GEN1,
	.num_channels		= 1,
	.clocks			= {
		{"axi", UINT_MAX,},
		{"vps0", UINT_MAX,},
		{"vps1", UINT_MAX,},
	},
	.ctrl_ops		= &tegra_pva_ctrl_ops,
	.devfs_name_family	= "pva",
	.class			= NV_PVA1_CLASS_ID,
	.autosuspend_delay      = 500,
	.finalize_poweron	= pva_finalize_poweron,
	.prepare_poweroff	= pva_prepare_poweroff,
	.firmware_name		= "nvhost_pva010.fw",
	.resource_policy	= RESOURCE_PER_CHANNEL_INSTANCE,
	.vm_regs		= {
		{0x70000, true, 0},
		{0x80000, false, 0},
		{0x80000, false, 8}
	},
	.poweron_reset		= true,
	.serialize		= true,
	.push_work_done		= true,
	.get_reloc_phys_addr	= nvhost_t194_get_reloc_phys_addr,
	.can_powergate		= true,
};

struct nvhost_device_data t19_pva0_info = {
	.version = PVA_HW_GEN1,
	.num_channels		= 1,
	.clocks			= {
		{"nafll_pva_vps", UINT_MAX,},
		{"nafll_pva_core", UINT_MAX,},
		{"axi", UINT_MAX,},
		{"vps0", UINT_MAX,},
		{"vps1", UINT_MAX,},
	},
	.ctrl_ops		= &tegra_pva_ctrl_ops,
	.devfs_name_family	= "pva",
	.class			= NV_PVA0_CLASS_ID,
	.autosuspend_delay      = 500,
	.finalize_poweron	= pva_finalize_poweron,
	.prepare_poweroff	= pva_prepare_poweroff,
	.firmware_name		= "nvhost_pva010.fw",
	.resource_policy	= RESOURCE_PER_CHANNEL_INSTANCE,
	.vm_regs		= {
		{0x70000, true, 0},
		{0x80000, false, 0},
		{0x80000, false, 8}
	},
	.poweron_reset		= true,
	.serialize		= true,
	.get_reloc_phys_addr	= nvhost_t194_get_reloc_phys_addr,
	.can_powergate		= true,
};

struct nvhost_device_data t23x_pva0_info = {
	.version = PVA_HW_GEN2,
	.num_channels		= 1,
	.clocks			= {
		{"axi", UINT_MAX,},
		{"vps0", UINT_MAX,},
		{"vps1", UINT_MAX,},
	},
	.ctrl_ops		= &tegra_pva_ctrl_ops,
	.devfs_name_family	= "pva",
	.class			= NV_PVA0_CLASS_ID,
	.autosuspend_delay      = 500,
	.finalize_poweron	= pva_finalize_poweron,
	.prepare_poweroff	= pva_prepare_poweroff,
	.firmware_name		= "nvhost_pva020.fw",
	.resource_policy	= RESOURCE_PER_CHANNEL_INSTANCE,
	.vm_regs		= {
		{0x240000, false, 0},
		{0x240004, false, 0},
		{0x240008, false, 0},
		{0x24000c, false, 0},
		{0x240010, false, 0},
		{0x240014, false, 0},
		{0x240018, false, 0},
		{0x24001c, false, 0},
		{0x240020, false, 0},
		{0x240020, false, 8},
		{0x240020, false, 16},
		{0x240024, false, 0},
		{0x240024, false, 8}
	},
	.poweron_reset		= true,
	.serialize		= true,
	.get_reloc_phys_addr	= nvhost_t23x_get_reloc_phys_addr,
	.can_powergate		= true,
};

/* Map PVA-A and PVA-B to respective configuration items in nvhost */
static struct of_device_id tegra_pva_of_match[] = {
	{
		.name = "pva0",
		.compatible = "nvidia,tegra194-pva",
		.data = (struct nvhost_device_data *)&t19_pva0_info },
	{
		.name = "pva1",
		.compatible = "nvidia,tegra194-pva",
		.data = (struct nvhost_device_data *)&t19_pva1_info },
	{
		.name = "pva0",
		.compatible = "nvidia,tegra234-pva",
		.data = (struct nvhost_device_data *)&t23x_pva0_info },
	{
		.name = "pva0",
		.compatible = "nvidia,tegra234-pva-hv",
		.data = (struct nvhost_device_data *)&t23x_pva0_info },
#ifdef CONFIG_TEGRA_T26X_GRHOST_PVA
	{
		.name = "pva0",
		.compatible = "nvidia,tegra264-pva",
		.data = (struct nvhost_device_data *)&t264_pva0_info },
#endif
	{ },
};

MODULE_DEVICE_TABLE(of, tegra_pva_of_match);

#define EVP_REG_NUM 8
static u32 pva_get_evp_reg(u32 index)
{
	u32 evp_reg[EVP_REG_NUM] = {
		evp_reset_addr_r(),
		evp_undef_addr_r(),
		evp_swi_addr_r(),
		evp_prefetch_abort_addr_r(),
		evp_data_abort_addr_r(),
		evp_rsvd_addr_r(),
		evp_irq_addr_r(),
		evp_fiq_addr_r()
	};

	return evp_reg[index];
}

static u32 evp_reg_val[EVP_REG_NUM] = {
	EVP_RESET_VECTOR,
	EVP_UNDEFINED_INSTRUCTION_VECTOR,
	EVP_SVC_VECTOR,
	EVP_PREFETCH_ABORT_VECTOR,
	EVP_DATA_ABORT_VECTOR,
	EVP_RESERVED_VECTOR,
	EVP_IRQ_VECTOR,
	EVP_FIQ_VECTOR
};

static int pva_cleanup_after_boot_fail(struct platform_device *pdev);
static int pva_prepare_poweroff_core(struct platform_device *pdev,
				     bool hold_reset);

/**
 * Allocate and set a circular array for FW to provide status info about
 * completed tasks from all the PVA R5 queues.
 * To avoid possible overwrite of info, the size of circular array needs to be
 * sufficient to hold the status info for maximum allowed number of tasks
 * across all PVA R5 queues at any time.
 * PVA R5 FW shall fill task status info at incremental positions in the array
 * while PVA KMD shall read the task status info at incremental positions from
 * the array.
 * Both PVA R5 FW and PVA KMD shall independently maintain an internal index
 * to dictate the current write position and read position respectively.
 */
static int pva_alloc_task_status_buffer(struct pva *pva)
{
	size_t min_size = 0U;

	/* Determine worst case size required for circular array based on
	 * maximum allowed per PVA engine and maximum allowed number of task
	 * submissions per PVA queue at any time.
	 */
	min_size = MAX_PVA_TASK_COUNT * sizeof(struct pva_task_error_s);

	pva->priv_circular_array.size = ALIGN(min_size + 64, 64);

	pva->priv_circular_array.va =
		dma_alloc_coherent(&pva->aux_pdev->dev,
				   pva->priv_circular_array.size,
				   &pva->priv_circular_array.pa, GFP_KERNEL);

	if (pva->priv_circular_array.va == NULL) {
		pr_err("pva: failed to alloc mem for task status info");
		return -ENOMEM;
	}

	INIT_WORK(&pva->task_update_work, pva_task_update);

	atomic_set(&pva->n_pending_tasks, 0);
	pva->task_status_workqueue =
		create_workqueue("pva_task_status_workqueue");
	return 0;
}

static void pva_reset_task_status_buffer(struct pva *pva)
{
	flush_workqueue(pva->task_status_workqueue);
	WARN_ON(atomic_read(&pva->n_pending_tasks) != 0);
	atomic_set(&pva->n_pending_tasks, 0);
	pva->circular_array_rd_pos = 0U;
	pva->circular_array_wr_pos = 0U;
}

static void pva_free_task_status_buffer(struct pva *pva)
{
	flush_workqueue(pva->task_status_workqueue);
	destroy_workqueue(pva->task_status_workqueue);
	dma_free_coherent(&pva->aux_pdev->dev, pva->priv_circular_array.size,
			  pva->priv_circular_array.va,
			  pva->priv_circular_array.pa);
}

void pva_fw_log_dump(struct pva *pva, bool hold_mutex)
{
	uint32_t tail;
	char *content;
	struct pva_kmd_fw_print_buffer *fw_log_buffer;
	char log_msg[PVA_MAX_DEBUG_LOG_MSG_CHARACTERS];


	mutex_lock(&pva->pva_fw_log_mutex);

	fw_log_buffer = (struct pva_kmd_fw_print_buffer *) pva->fw_info.priv2_buffer.va;
	if(!fw_log_buffer)
		goto exit;

	tail = fw_log_buffer->tail;
	content = ((char *)fw_log_buffer) + sizeof(struct pva_kmd_fw_print_buffer);

	if(tail > fw_log_buffer->size) {
		nvpva_err(&pva->pdev->dev, "Firmware print tail is out of bounds");
		goto exit;
	}

	while (fw_log_buffer->head != tail) {
		const char *str = content + fw_log_buffer->head;
		uint32_t print_size;
		uint32_t i = 0U;

		if ((fw_log_buffer->head + PVA_MAX_DEBUG_LOG_MSG_CHARACTERS) > fw_log_buffer->size) {
			fw_log_buffer->head = 0;
			continue;
		}
		/* Scan max of (PVA_MAX_DEBUG_LOG_MSG_CHARACTERS-1) characters for \n or \0 */
		while ((i < (PVA_MAX_DEBUG_LOG_MSG_CHARACTERS - 1U))
			&& (str[i] != '\n') && (str[i] != '\0')) {

			log_msg[i] = str[i];
			i++;
		}
		log_msg[i] = '\0';

		nvpva_err(&pva->pdev->dev, "%s", log_msg);
		/* +1 for null terminator */
		print_size = i + 1;
		fw_log_buffer->head += print_size;
		(void)memset(log_msg, 0, PVA_MAX_DEBUG_LOG_MSG_CHARACTERS);
	}

	if ((fw_log_buffer->flags & PVA_FW_PRINT_BUFFER_LOG_MSG_OVERFLOWED) != 0U)
		nvpva_err(&pva->pdev->dev, "Firmware print buffer overflowed!");

	if ((fw_log_buffer->flags & PVA_FW_PRINT_FAILURE) != 0U)
		nvpva_err(&pva->pdev->dev, "Firmware print failed!");

	if ((fw_log_buffer->flags & PVA_FW_PRINT_BUFFER_FULL_LOG_DROPPED) != 0U)
		nvpva_err(&pva->pdev->dev, "Firmware print log dropped!");

exit:
	if(!hold_mutex)
		mutex_unlock(&pva->pva_fw_log_mutex);
}

static void pva_fw_log_dump_handler(struct work_struct *work)
{
	struct pva *pva = container_of(work, struct pva, pva_fw_log_work);

	pva_fw_log_dump(pva, false);
}

static void pva_fw_log_dump_init(struct pva *pva)
{
	INIT_WORK(&pva->pva_fw_log_work, pva_fw_log_dump_handler);
	mutex_init(&pva->pva_fw_log_mutex);
}

static void pva_fw_log_dump_deinit(struct pva *pva)
{
	mutex_destroy(&pva->pva_fw_log_mutex);
}

static int pva_init_fw(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct pva *pva = pdata->private_data;
	struct pva_fw *fw_info = &pva->fw_info;
	struct pva_dma_alloc_info *priv1_buffer;
	struct pva_dma_alloc_info *priv2_buffer;
	int err = 0;
	u64 ucode_useg_addr;
	u32 sema_value = 0;
	u32 dram_base;
	u32 ucode_seg_addr_hi = 0;
	uint64_t useg_addr;
	u32 i;

	nvpva_dbg_fn(pva, "");

	priv1_buffer = &fw_info->priv1_buffer;
	priv2_buffer = &fw_info->priv2_buffer;

	/* Set the Ucode Header address for R5 */
	/* Program user seg subtracting the offset */
	ucode_useg_addr = 0x80000000;
	pva->version_config->write_mailbox(pdev, PVA_MBOXID_USERSEG_L,
					   PVA_LOW32(ucode_useg_addr));
	ucode_seg_addr_hi = PVA_EXTRACT64(ucode_useg_addr, 39, 32, u32);
	pva->version_config->write_mailbox(pdev, PVA_MBOXID_USERSEG_H,
					   ucode_seg_addr_hi);

	/* Program the extra memory to be used by R5 */
	ucode_useg_addr = priv2_buffer->pa;
	ucode_seg_addr_hi = PVA_EXTRACT64(ucode_useg_addr, 39, 32, u32);

	host1x_writel(pdev, cfg_priv_ar2_start_r(pva->version),
		      fw_info->priv2_reg_offset);
	host1x_writel(pdev, cfg_priv_ar2_end_r(pva->version),
		      fw_info->priv2_reg_offset + priv2_buffer->size);

	pva->version_config->write_mailbox(pdev, PVA_MBOXID_PRIV2SEG_L,
					   PVA_LOW32(ucode_useg_addr));
	pva->version_config->write_mailbox(pdev, PVA_MBOXID_PRIV2SEG_H,
					   ucode_seg_addr_hi);

	/* Write EVP registers */
	for (i = 0; i < EVP_REG_NUM; i++)
		host1x_writel(pdev, pva_get_evp_reg(i), evp_reg_val[i]);

	host1x_writel(pdev, scr_secext_intr_event_0_r(), PVA_SEC_SCR_SECEXT_INTR_EVENT_0_VAL);
	host1x_writel(pdev, scr_proc_0_r(), PVA_PROC_SCR_PROC_0_VAL);

	host1x_writel(pdev,
		      cfg_priv_ar1_start_r(pva->version),
		      FW_CODE_DATA_START_ADDR);
	host1x_writel(pdev,
		      cfg_priv_ar1_end_r(pva->version),
		      FW_CODE_DATA_END_ADDR);
	useg_addr = priv1_buffer->pa - FW_CODE_DATA_START_ADDR;
	if ((pva->is_hv_mode) && (!pva->boot_from_file)) {
		host1x_writel(pdev,
			      cfg_priv_ar1_lsegreg_r(pva->version),
			      0xFFFFFFFF);
		host1x_writel(pdev,
			      cfg_priv_ar1_usegreg_r(pva->version),
			      0xFFFFFFFF);

		host1x_writel(pdev, evp_scr_r(), PVA_EVP_SCR_VAL);
		host1x_writel(pdev, cfg_scr_status_ctrl_r(), PVA_STATUS_CTL_SCR_VAL);
		host1x_writel(pdev, cfg_scr_priv_0_r(), PVA_PRIV_SCR_VAL);
		host1x_writel(pdev, cfg_scr_ccq_ctrl_r(), PVA_CCQ_SCR_VAL);
	} else {
		host1x_writel(pdev,
			      cfg_priv_ar1_lsegreg_r(pva->version),
			      PVA_LOW32(useg_addr));
		host1x_writel(pdev,
			      cfg_priv_ar1_usegreg_r(pva->version),
			      PVA_EXTRACT64((useg_addr), 39, 32, u32));

		/* WAR: Bypass configuring status strl reg due to failure in gen 3 sim test */
		if (pdata->version == PVA_HW_GEN2) {
			host1x_writel(pdev, cfg_scr_status_ctrl_r(),
					PVA_STATUS_CTL_SCR_VAL | PVA_LOCK_SCR);
			host1x_writel(pdev, evp_scr_r(), PVA_EVP_SCR_VAL | PVA_LOCK_SCR);
			host1x_writel(pdev, cfg_scr_priv_0_r(), PVA_PRIV_SCR_VAL | PVA_LOCK_SCR);
			host1x_writel(pdev, cfg_scr_ccq_ctrl_r(), PVA_CCQ_SCR_VAL | PVA_LOCK_SCR);
		}
	}

	/* Indicate the OS is waiting for PVA ready Interrupt */
	pva->cmd_status[PVA_MAILBOX_INDEX] = PVA_CMD_STATUS_WFI;

	if (pva->r5_dbg_wait) {
		sema_value = PVA_WAIT_DEBUG;
		pva->timeout_enabled = false;
	}

	if (pva->slcg_disable)
		sema_value |= PVA_CG_DISABLE;

	if (pva->vmem_war_disable)
		sema_value |= PVA_VMEM_RD_WAR_DISABLE;

	sema_value |= (PVA_BOOT_INT | PVA_TEST_WAIT | PVA_VMEM_MBX_WAR_ENABLE);
	pva->boot_count += 1;

	nvpva_dbg_fn(pva, "boot count = %d", pva->boot_count);

	host1x_writel(pdev, hsp_ss0_clr_r(), 0xFFFFFFFF);
	host1x_writel(pdev, hsp_ss0_set_r(), sema_value);

	if (pva->version == PVA_HW_GEN1) {
		host1x_writel(pdev, hsp_ss2_set_r(), 0xFFFFFFFF);
		host1x_writel(pdev, hsp_ss3_set_r(), 0xFFFFFFFF);
	} else {
		if (pva->syncpts.syncpt_start_iova_r > 0xFBFFFFFF) {
			dev_err(&pdev->dev,
				"rd sema base greater than 32 bit ");
			err = -EINVAL;
			goto out;
		}

		sema_value = (u32)pva->syncpts.syncpt_start_iova_r;
		if (iommu_get_domain_for_dev(&pdev->dev))
			dram_base = DRAM_PVA_IOVA_START_ADDRESS;
		else
			dram_base = DRAM_PVA_NO_IOMMU_START_ADDRESS;

		if (sema_value < dram_base) {
			dev_err(&pdev->dev,
				"rd sema base less than dram base");
			err = -EINVAL;
			goto out;
		}

		sema_value -= dram_base;

		host1x_writel(pdev, hsp_ss2_clr_r(), 0xFFFFFFFF);
		host1x_writel(pdev, hsp_ss2_set_r(), sema_value);

		if (pva->syncpts.syncpt_start_iova_rw > 0xFFF7FFFF) {
			dev_err(&pdev->dev,
				"rw sema base greater than 32 bit ");
			err = -EINVAL;
			goto out;
		}

		sema_value = (u32)pva->syncpts.syncpt_start_iova_rw;
		if (sema_value < dram_base) {
			dev_err(&pdev->dev,
				"rw sema base less than dram base");
			err = -EINVAL;
			goto out;
		}

		sema_value -= dram_base;

		host1x_writel(pdev, hsp_ss3_clr_r(), 0xFFFFFFFF);
		host1x_writel(pdev, hsp_ss3_set_r(), sema_value);
	}

	/* Take R5 out of reset */
	host1x_writel(pdev, proc_cpuhalt_r(),
		      proc_cpuhalt_ncpuhalt_f(proc_cpuhalt_ncpuhalt_done_v()));

	nvpva_dbg_fn(pva, "Waiting for PVA to be READY");

	/* Wait PVA to report itself as ready */
#ifdef CONFIG_PVA_INTERRUPT_DISABLED
	err = pva_poll_mailbox_isr(pva, 600000);
#else
	err = pva_mailbox_wait_event(pva, 60000, false);
#endif
	nvpva_dbg_fn(pva, "PVA boot returned: %d", err);
	if (err) {
		dev_err(&pdev->dev, "mbox timedout boot sema=%x\n",
			(host1x_readl(pdev, hsp_ss0_state_r())));
		goto wait_timeout;
	}

	pva_reset_task_status_buffer(pva);
	(void)memset(pva->priv_circular_array.va, 0,
		     pva->priv_circular_array.size);
wait_timeout:
out:
	pva->cmd_status[PVA_MAILBOX_INDEX] = PVA_CMD_STATUS_INVALID;

	return err;
}

static int pva_free_fw(struct platform_device *pdev, struct pva *pva)
{
	struct pva_fw *fw_info = &pva->fw_info;

	if (pva->boot_from_file) {
		if (pva->priv1_dma.va)
			dma_free_coherent(&pva->aux_pdev->dev, pva->priv1_dma.size,
					  pva->priv1_dma.va, pva->priv1_dma.pa);
	} else {
		if (pva->map_co_needed && (pva->priv1_dma.pa != 0)) {
			nvpva_unmap_region(&pdev->dev,
					   pva->priv1_dma.pa,
					   pva->co->size,
					   DMA_BIDIRECTIONAL);
		}

		pva->co->base_pa = 0;
	}

	pva->priv1_dma.pa = 0;
	pva->priv1_dma.va = 0;
	if (pva->priv2_dma.va) {
		dma_free_coherent(&pva->aux_pdev->dev, pva->priv2_dma.size,
				  pva->priv2_dma.va, pva->priv2_dma.pa);
		pva->priv2_dma.va = 0;
		pva->priv2_dma.pa = 0;
	}

	memset(fw_info, 0, sizeof(struct pva_fw));
	pva->fw_debug_log.addr = NULL;
	pva->pva_trace.addr = NULL;

	return 0;
}

int nvpva_request_firmware(struct platform_device *pdev, const char *fw_name,
			   const struct firmware **ucode_fw)
{
	int err = 0;

#if IS_ENABLED(CONFIG_TEGRA_GRHOST)
	*ucode_fw = nvhost_client_request_firmware(pdev, fw_name, true);
	if (*ucode_fw == NULL)
		err = -ENOENT;
#else
	err = request_firmware(ucode_fw, fw_name, &pdev->dev);
#endif
	return err;
}

static int
pva_read_ucode_file(struct platform_device *pdev,
		    const char *fw_name,
		    struct pva *pva)
{
	int err = 0;
	struct pva_fw *fw_info = &pva->fw_info;
	int w;
	u32 *ucode_ptr;
	const struct firmware *ucode_fw = NULL;

	err = nvpva_request_firmware(pva->pdev, fw_name, &ucode_fw);
	if (err != 0) {
		dev_err(&pdev->dev, "Failed to load the %s firmware\n",
			fw_name);
		return err;
	}

	fw_info->priv1_buffer.size = ucode_fw->size;
	pva->priv1_dma.size = FW_CODE_DATA_END_ADDR - FW_CODE_DATA_START_ADDR;
	pva->priv1_dma.size = ALIGN(pva->priv1_dma.size + SZ_4K, SZ_4K);
	/* Allocate memory to R5 for app code, data or to log information */
	pva->priv1_dma.va = dma_alloc_coherent(&pdev->dev, pva->priv1_dma.size,
					       &pva->priv1_dma.pa, GFP_KERNEL);
	if (!pva->priv1_dma.va) {
		err = -ENOMEM;
		goto clean_up;
	}

	fw_info->priv1_buffer.va = pva->priv1_dma.va;
	fw_info->priv1_buffer.pa = pva->priv1_dma.pa;
	ucode_ptr = fw_info->priv1_buffer.va;

	/* copy the whole thing taking into account endianness */
	for (w = 0; w < ucode_fw->size / sizeof(u32); w++)
		ucode_ptr[w] = le32_to_cpu(((__le32 *)ucode_fw->data)[w]);
clean_up:
	release_firmware(ucode_fw);

	return err;
}

static int pva_read_ucode_co(struct platform_device *pdev,
			     struct pva *pva)
{
	int err = 0;
	struct pva_fw *fw_info = &pva->fw_info;

	if (pva->is_hv_mode) {
		pva->priv1_dma.pa = 0xFFFFFFFF;
		pva->priv1_dma.va = 0;
		goto update_info;
	}

	if (pva->map_co_needed) {
		err = nvpva_map_region(&pdev->dev,
				       pva->co->base,
				       pva->co->size,
				       &pva->priv1_dma.pa,
				       DMA_BIDIRECTIONAL);
		if (err) {
			err = -ENOMEM;
			goto out;
		}
	} else {
		pva->priv1_dma.pa = pva->co->base;
		pva->priv1_dma.va = 0;
	}

update_info:

	fw_info->priv1_buffer.va = pva->priv1_dma.va;
	fw_info->priv1_buffer.pa = pva->priv1_dma.pa;
	fw_info->priv1_buffer.size = pva->co->size;
	pva->priv1_dma.size = pva->co->size;

out:
	return err;
}

static void init_fw_print_buffer(struct pva_kmd_fw_print_buffer *print_buffer)
{
	print_buffer->size =
		FW_DEBUG_LOG_BUFFER_SIZE - sizeof(*print_buffer);
	print_buffer->head = 0;
	print_buffer->tail = 0;
	print_buffer->flags = 0;
}

static int pva_read_ucode(struct platform_device *pdev, const char *fw_name,
			  struct pva *pva)
{
	int err = 0;
	struct pva_fw *fw_info = &pva->fw_info;

	if (pva->boot_from_file)
		err = pva_read_ucode_file(pdev, fw_name, pva);
	else
		err = pva_read_ucode_co(pdev, pva);

	nvpva_dbg_fn(pva, "co iova = %llx\n", pva->priv1_dma.pa);

	fw_info->priv2_buffer.size = FW_DEBUG_DATA_TOTAL_SIZE;

	/* Make sure the address is aligned to 4K */
	pva->priv2_dma.size = ALIGN(fw_info->priv2_buffer.size, SZ_4K);

	/* Allocate memory to R5 for app code, data or to log information */
	pva->priv2_dma.va = dma_alloc_coherent(&pva->aux_pdev->dev, pva->priv2_dma.size,
					       &pva->priv2_dma.pa, GFP_KERNEL);
	if (!pva->priv2_dma.va) {
		err = -ENOMEM;
		goto out;
	}

	fw_info->priv2_buffer.pa = pva->priv2_dma.pa;
	fw_info->priv2_buffer.va = pva->priv2_dma.va;
	fw_info->priv2_reg_offset = FW_DEBUG_DATA_START_ADDR;

	/* setup FW debug log buffer */
	pva->fw_debug_log.addr = fw_info->priv2_buffer.va;

	init_fw_print_buffer((struct pva_kmd_fw_print_buffer *)fw_info->priv2_buffer.va);

	/* setup trace buffer */
	fw_info->trace_buffer_size = FW_TRACE_BUFFER_SIZE;
	pva->pva_trace.addr = fw_info->priv2_buffer.va + FW_DEBUG_LOG_BUFFER_SIZE;
	pva->pva_trace.size = FW_TRACE_BUFFER_SIZE;
	pva->pva_trace.offset = 0L;
out:
	return err;
}

static int pva_load_fw(struct platform_device *pdev, struct pva *pva)
{
	int err = 0;
	struct nvhost_device_data *pdata = platform_get_drvdata(pva->pdev);

	nvpva_dbg_fn(pva, "");

	err = pva_read_ucode(pdev, pdata->firmware_name, pva);
	if (err < 0)
		goto load_fw_err;

	return err;

load_fw_err:
	pva_free_fw(pdev, pva);

	return err;
}

int pva_get_firmware_version(struct pva *pva, struct pva_version_info *info)
{
	uint32_t flags = PVA_CMD_INT_ON_ERR | PVA_CMD_INT_ON_COMPLETE;
	struct pva_cmd_status_regs status;
	struct pva_cmd_s cmd;
	int err = 0;
	u32 nregs;

	nregs = pva_cmd_R5_version(&cmd, flags);

	/* Submit request to PVA and wait for response */
	err = pva_mailbox_send_cmd_sync(pva, &cmd, nregs, &status);
	if (err < 0) {
		nvpva_warn(&pva->pdev->dev,
			    "mbox get firmware version cmd failed: %d\n", err);

		return err;
	}

	info->pva_r5_version = status.status[PVA_CMD_STATUS4_INDEX];
	info->pva_compat_version = status.status[PVA_CMD_STATUS5_INDEX];
	info->pva_revision = status.status[PVA_CMD_STATUS6_INDEX];
	info->pva_built_on = status.status[PVA_CMD_STATUS7_INDEX];

	return err;
}

int pva_boot_kpi(struct pva *pva, u64 *r5_boot_time)
{
	uint32_t flags = PVA_CMD_INT_ON_ERR | PVA_CMD_INT_ON_COMPLETE;
	struct pva_cmd_status_regs status;
	struct pva_cmd_s cmd;
	int err = 0;
	u32 nregs;

	nregs = pva_cmd_pva_uptime(&cmd, 255, flags);

	/* Submit request to PVA and wait for response */
	err = pva_mailbox_send_cmd_sync(pva, &cmd, nregs, &status);
	if (err < 0) {
		nvpva_warn(&pva->pdev->dev, "mbox get uptime cmd failed: %d\n",
			    err);
		return err;
	}
	*r5_boot_time = status.status[PVA_CMD_STATUS7_INDEX];
	*r5_boot_time = ((*r5_boot_time) << 32);
	*r5_boot_time = (*r5_boot_time) | status.status[PVA_CMD_STATUS6_INDEX];

	return err;
}

int pva_set_log_level(struct pva *pva, u32 log_level, bool mailbox_locked)
{
	uint32_t flags = PVA_CMD_INT_ON_ERR | PVA_CMD_INT_ON_COMPLETE;
	struct pva_cmd_status_regs status;
	struct pva_cmd_s cmd;
	int err = 0;
	u32 nregs;

	nregs = pva_cmd_set_logging_level(&cmd, log_level, flags);

	if (mailbox_locked)
		err = pva_mailbox_send_cmd_sync_locked(pva, &cmd, nregs, &status);
	else
		err = pva_mailbox_send_cmd_sync(pva, &cmd, nregs, &status);

	if (err < 0)
		nvpva_warn(&pva->pdev->dev, "mbox set log level failed: %d\n",
			    err);

	return err;
}

u32 nvpva_get_id_idx(struct pva *dev, struct platform_device *pdev)
{
	s32 sid;
	u32 i;

	if (pdev == NULL)
		return 0;

	sid = nvpva_get_device_hwid(pdev, 0);
	if (sid < 0)
		return UINT_MAX;

	for (i = 0; i < dev->sid_count; i++)
		if (dev->sids[i] == sid)
			return i;

	return UINT_MAX;
}

int nvpva_get_device_hwid(struct platform_device *pdev,
					   unsigned int id)
{
	struct device *dev = &pdev->dev;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 0, 0)
	struct iommu_fwspec *fwspec = dev->iommu_fwspec;
#else
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
#endif

	if (!fwspec)
		return -EINVAL;

	if (id >= fwspec->num_ids)
		return -EINVAL;

	return fwspec->ids[id] & 0xffff;
}

static int nvpva_write_hwid(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct pva *pva = pdata->private_data;
	int i;
	u32 *id_idx;
	u32 *reg_idx;
	int *streamids = pva->sids;
	u32 reg_array[16] = {0};

	if (pva->version == PVA_HW_GEN1) {
		id_idx = vm_regs_sid_idx_t19x;
		reg_idx = vm_regs_reg_idx_t19x;
	} else if (pva->version == PVA_HW_GEN2) {
		id_idx = vm_regs_sid_idx_t234;
		reg_idx = vm_regs_reg_idx_t234;
	} else {
		if (!tegra_platform_is_silicon())
			id_idx = vm_regs_sid_idx_t264_cod;
		else
#if  defined(CONFIG_PVA_CO_DISABLED_T264)
			id_idx = vm_regs_sid_idx_t264_cod;
#else
			id_idx = vm_regs_sid_idx_t264_co;
#endif
		reg_idx = vm_regs_reg_idx_t264;
	}

	/* Go through the StreamIDs and assemble register values */
	for (i = 0; i < ARRAY_SIZE(pdata->vm_regs); i++) {
		u64 addr = pdata->vm_regs[i].addr;
		u32 shift = pdata->vm_regs[i].shift & 0x0000FFFF;
		u32 mask = (pdata->vm_regs[i].shift >> 16) & 0x0000FFFF;
		u32 val;

		/* Break if this was the last StreamID */
		if (!addr)
			break;

		/* Update the StreamID value */
		if(mask == 0 )
			mask = 0x000000FF;

		val = ((streamids[id_idx[i]] & mask) << shift);
		reg_array[reg_idx[i]] |= val;
	}

	/*write register values */
	for (i = 0; i < ARRAY_SIZE(pdata->vm_regs); i++) {
		u64 addr = pdata->vm_regs[i].addr;
		u32 val;

		/* Break if this was the last StreamID */
		if (!addr)
			break;

		val = reg_array[reg_idx[i]];
		nvpva_dbg_fn(pva, "i= %d, reg_idx[i] =  %d, val = %d\n",
			     i, reg_idx[i], val);
		host1x_writel(pdev, addr, val);
	}

	return 0;
}

#ifdef CONFIG_PVA_INTERRUPT_DISABLED
int pva_aisr_handler(void *arg)
{
	struct pva *pva = (struct pva *)arg;
	struct platform_device *pdev = pva->pdev;
	bool recover = false;
	u32 status5 = 0;
	u32 sleep_us = 127; // 127us

	nvpva_warn(&pva->pdev->dev, "Thread started for polling PVA AISR");

	while (true) {
		/* Wait for AISR to be updated to INT_PENDING*/
		do {
			// schedule();
			usleep_range((sleep_us >> 2) + 1, sleep_us);
			// cond_resched();
		} while ((pva->version_config->read_mailbox(pdev, PVA_MBOX_AISR)
			 & PVA_AISR_INT_PENDING) == 0);

		nvpva_warn(&pva->pdev->dev, "PVA AISR received");

		recover = false;

		status5 = pva->version_config->read_mailbox(pdev, PVA_MBOX_AISR);
		if (status5 & PVA_AISR_INT_PENDING) {
			nvpva_dbg_info(pva, "PVA AISR (%x)", status5);
			if (status5 & (PVA_AISR_TASK_COMPLETE | PVA_AISR_TASK_ERROR)) {
				atomic_add(1, &pva->n_pending_tasks);
				queue_work(pva->task_status_workqueue,
				&pva->task_update_work);
				if ((status5 & PVA_AISR_ABORT) == 0U)
					pva_push_aisr_status(pva, status5);
			}

			/* For now, just log the errors */
			if (status5 & PVA_AISR_TASK_COMPLETE)
				nvpva_warn(&pdev->dev, "PVA AISR: PVA_AISR_TASK_COMPLETE");
			if (status5 & PVA_AISR_TASK_ERROR)
				nvpva_warn(&pdev->dev, "PVA AISR: PVA_AISR_TASK_ERROR");
			if (status5 & PVA_AISR_ABORT) {
				nvpva_warn(&pdev->dev, "PVA AISR: PVA_AISR_ABORT");
				recover = true;
			}

			pva->version_config->write_mailbox(pdev, PVA_MBOX_AISR, 0x0);
			nvpva_dbg_info(pva, "Clearing AISR");
		}
		/* Flush the work queue before abort?*/
		if (recover)
			pva_abort(pva);
	}
}
#endif

int pva_finalize_poweron(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct pva *pva = pdata->private_data;
	int err = 0;
	int i;
	u64 timestamp;
	u64 timestamp2;

	timestamp = nvpva_get_tsc_stamp();

	nvpva_dbg_fn(pva, "");

	if (!pva->boot_from_file) {
		nvpva_dbg_fn(pva, "boot from co");
		pva->co = pva_fw_co_get_info(pva);
		if (pva->co == NULL) {
			nvpva_dbg_fn(pva, "failed to get carveout");
			err = -ENOMEM;
			goto err_poweron_1;
		}

		nvpva_dbg_fn(pva, "CO base = %llx, CO size = %llu\n",
			  (u64)pva->co->base, (u64)pva->co->size);
	}

	/* Enable LIC_INTERRUPT line for HSP1, H1X and WDT */
	if (pva->version == PVA_HW_GEN1) {
		host1x_writel(pva->pdev, sec_lic_intr_enable_r(pva->version),
			      sec_lic_intr_enable_hsp_f(SEC_LIC_INTR_HSP1) |
			      sec_lic_intr_enable_h1x_f(SEC_LIC_INTR_H1X_ALL_19) |
			      sec_lic_intr_enable_wdt_f(SEC_LIC_INTR_WDT));
	} else {
		host1x_writel(pva->pdev, sec_lic_intr_enable_r(pva->version),
			      sec_lic_intr_enable_hsp_f(SEC_LIC_INTR_HSP1) |
			      sec_lic_intr_enable_h1x_f(SEC_LIC_INTR_H1X_ALL_23) |
			      sec_lic_intr_enable_wdt_f(SEC_LIC_INTR_WDT));

	}

	nvpva_write_hwid(pdev);
	if (!pva->boot_from_file)
		err = pva_load_fw(pdev, pva);
	else
		err = pva_load_fw(pva->aux_pdev, pva);

	if (err < 0) {
		nvpva_err(&pdev->dev, " pva fw failed to load\n");
		goto err_poweron_1;
	}

	for (i = 0; i < pva->version_config->irq_count; i++)
		enable_irq(pva->irq[i]);

	err = pva_init_fw(pdev);
	if (err < 0) {
		nvpva_err(&pdev->dev, " pva fw failed to init\n");
		goto err_poweron;
	}

#ifdef CONFIG_PVA_INTERRUPT_DISABLED
	pva->pva_aisr_handler_task = kthread_create(pva_aisr_handler,
							 (void *)pva, "pva_aisr_handler");

	if (pva->pva_aisr_handler_task != NULL)
		wake_up_process(pva->pva_aisr_handler_task);
	else
		nvpva_err(&pdev->dev, " PVA AISR thread failed to init\n");

#endif

	timestamp2 = nvpva_get_tsc_stamp() - timestamp;

	err = pva_set_log_level(pva, pva->log_level, true);

	if (err < 0) {
		nvpva_err(&pdev->dev, " pva fw init: set log level failed\n");
		goto err_poweron;
	}

	pva->booted = true;
	timestamp = nvpva_get_tsc_stamp() - timestamp;
	nvpva_dbg_prof(pva, "Power on took %lld us, without log level %lld\n",
		       (32 * timestamp)/1000, (32 * timestamp2)/1000);
	pva->pva_power_on_err = err;

	pva_trace_copy_to_ftrace(pva);

	return err;

err_poweron:

	pva_trace_copy_to_ftrace(pva);
	pva_cleanup_after_boot_fail(pdev);

err_poweron_1:

	pva->pva_power_on_err = err;

	return err;
}

void save_fw_debug_log(struct pva *pva)
{
	if (pva->fw_debug_log.saved_log != NULL &&
	    pva->fw_debug_log.addr != NULL) {
		mutex_lock(&pva->fw_debug_log.saved_log_lock);
		memcpy(pva->fw_debug_log.saved_log, pva->fw_debug_log.addr,
		       pva->fw_debug_log.size);
		mutex_unlock(&pva->fw_debug_log.saved_log_lock);
	}
}

static int pva_cleanup_after_boot_fail(struct platform_device *pdev)
{
	return pva_prepare_poweroff_core(pdev, false);
}

int pva_prepare_poweroff(struct platform_device *pdev)
{
	return pva_prepare_poweroff_core(pdev, true);
}

static int pva_prepare_poweroff_core(struct platform_device *pdev,
				     bool hold_reset)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct pva *pva = pdata->private_data;
	int i;
#ifdef CONFIG_PVA_INTERRUPT_DISABLED
	int ret = 0;
#endif

	nvpva_dbg_fn(pva, "");

	/*
	 * Disable IRQs. Interrupt handler won't be under execution after the
	 * call returns.
	 */
	for (i = 0; i < pva->version_config->irq_count; i++)
		disable_irq(pva->irq[i]);

#ifdef CONFIG_PVA_INTERRUPT_DISABLED
	if (pva->pva_aisr_handler_task != NULL) {
		ret = kthread_stop(pva->pva_aisr_handler_task);
		if (ret == 0)
			nvpva_warn(&pva->pdev->dev, "Thread for polling PVA AISR stopped");
		else
			nvpva_warn(&pva->pdev->dev, "Could not stop thread for polling PVA AISR");
	}
#endif

	/* disable error reporting to HSM*/
	pva_disable_ec_err_reporting(pva);

	/* Put PVA to reset to ensure that the firmware doesn't get accessed */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 2, 0)
	reset_control_acquire(pdata->reset_control);
#endif
	if(hold_reset)
		reset_control_assert(pdata->reset_control);
	else
		reset_control_reset(pdata->reset_control);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 2, 0)
	reset_control_release(pdata->reset_control);
#endif
	save_fw_debug_log(pva);
	pva->booted = false;
	pva_free_fw(pdev, pva);

	return 0;
}

int pva_busy(struct pva *pva, u32 attempts)
{
	int err = 0;

	nvpva_dbg_fn(pva, "b");

#ifdef CONFIG_PM
	mutex_lock(&pva->pva_busy_mutex);
	while ( attempts-- > 0) {
		nvpva_dbg_fn(pva, "%d\n", attempts);
		err = nvhost_module_busy(pva->pdev);
		if (err == 0)
			break;

		if (!pm_runtime_suspended(&pva->pdev->dev))
			break;

		if(!pva->pdev->dev.power.runtime_error)
			break;

		pm_runtime_set_suspended(&pva->pdev->dev);
	}

	mutex_unlock(&pva->pva_busy_mutex);
#else
	err = nvhost_module_busy(pva->pdev);
#endif

	nvpva_dbg_fn(pva, "e");
	return err;
}

void pva_idle(struct pva *pva)
{
	nvhost_module_idle(pva->pdev);
}

int pva_hwpm_ip_pm(void *ip_dev, bool disable)
{
	int err = 0;
	struct platform_device *dev = (struct platform_device *)ip_dev;

	struct nvhost_device_data *pdata = platform_get_drvdata(dev);
	struct pva *pva = pdata->private_data;

	nvpva_dbg_info(pva, "ip power management %s",
			disable ? "disable" : "enable");

	if (disable) {
		err = nvhost_module_busy(ip_dev);
		if (err < 0)
			dev_err(&dev->dev, "nvhost_module_busy failed");
	} else {
		nvhost_module_idle(ip_dev);
	}

	return err;
}

int pva_hwpm_ip_reg_op(void *ip_dev, enum tegra_soc_hwpm_ip_reg_op reg_op,
	u32 inst_element_index, u64 reg_offset, u32 *reg_data)
{
	struct platform_device *dev = (struct platform_device *)ip_dev;
	struct nvhost_device_data *pdata = platform_get_drvdata(dev);
	struct pva *pva = pdata->private_data;

	if (reg_offset > UINT_MAX)
		return -EINVAL;

	nvpva_dbg_info(pva, "reg_op %d reg_offset %llu", reg_op, reg_offset);

	if (reg_op == TEGRA_SOC_HWPM_IP_REG_OP_READ)
		*reg_data = host1x_readl(dev,
			(hwpm_get_offset() + (unsigned int)reg_offset));
	else if (reg_op == TEGRA_SOC_HWPM_IP_REG_OP_WRITE)
		host1x_writel(dev,
			(hwpm_get_offset() + (unsigned int)reg_offset),
			*reg_data);

	return 0;
}

#if !IS_ENABLED(CONFIG_TEGRA_GRHOST)
static ssize_t clk_cap_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct nvhost_device_data *pdata =
		container_of(kobj, struct nvhost_device_data, clk_cap_kobj);
	/* i is indeed 'index' here after type conversion */
	int ret, i = attr - pdata->clk_cap_attrs;
	struct clk_bulk_data *clks = &pdata->clks[i];
	struct clk *clk = clks->clk;
	unsigned long freq_cap;
	long freq_cap_signed;

	ret = kstrtoul(buf, 0, &freq_cap);
	if (ret)
		return -EINVAL;
	/* Remove previous freq cap to get correct rounted rate for new cap */
	ret = clk_set_max_rate(clk, UINT_MAX);
	if (ret < 0)
		return ret;

	freq_cap_signed = clk_round_rate(clk, freq_cap);
	if (freq_cap_signed < 0)
		return -EINVAL;
	freq_cap = (unsigned long)freq_cap_signed;
	/* Apply new freq cap */
	ret = clk_set_max_rate(clk, freq_cap);
	if (ret < 0)
		return ret;

	/* Update the clock rate */
	clk_set_rate(clks->clk, freq_cap);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t clk_cap_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	struct nvhost_device_data *pdata =
		container_of(kobj, struct nvhost_device_data, clk_cap_kobj);
	/* i is indeed 'index' here after type conversion */
	int i = attr - pdata->clk_cap_attrs;
	struct clk_bulk_data *clks = &pdata->clks[i];
	struct clk *clk = clks->clk;
	long max_rate;

	max_rate = clk_round_rate(clk, UINT_MAX);
	if (max_rate < 0)
		return max_rate;

	return snprintf(buf, PAGE_SIZE, "%ld\n", max_rate);
}

static struct kobj_type nvpva_kobj_ktype = {
	.sysfs_ops  = &kobj_sysfs_ops,
};

#endif

static int pva_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nvhost_device_data *pdata;
	const struct of_device_id *match;
	struct pva *pva;
	int err = 0;
	size_t i;
	u64 offset;

#if !IS_ENABLED(CONFIG_TEGRA_GRHOST)
	struct kobj_attribute *attr = NULL;
	int j = 0;
	struct clk_bulk_data *clks;
	struct clk *c;
#endif

	match = of_match_device(tegra_pva_of_match, dev);
	if (!match) {
		dev_err(dev, "no match for pva dev\n");
		err = -ENODATA;
		goto err_get_pdata;
	}

	pdata = (struct nvhost_device_data *)match->data;

	WARN_ON(!pdata);
	if (!pdata) {
		dev_info(dev, "no platform data\n");
		err = -ENODATA;
		goto err_get_pdata;
	}
#if !IS_ENABLED(CONFIG_TEGRA_GRHOST)
	of_platform_default_populate(dev->of_node, NULL, dev);
#endif

	if ((pdata->version != PVA_HW_GEN1)
	     && !is_cntxt_initialized(pdata->version)) {
		dev_warn(&pdev->dev,
			 "nvpva cntxt was not initialized, deferring probe.");
		return -EPROBE_DEFER;
	}

	if (pdata->version == PVA_HW_GEN1 &&
	    tegra_get_sku_id() == 0x9E) {
		dev_err(dev, "PVA IP is disabled in SKU\n");
		err = -ENODEV;
		goto err_no_ip;
	}

	if (pdata->version == PVA_HW_GEN1 &&
	    tegra_get_sku_id() == 0x9F && pdata->class == NV_PVA1_CLASS_ID) {
		dev_err(dev, "PVA1 IP is disabled in SKU\n");
		err = -ENODEV;
		goto err_no_ip;
	}

	pva = devm_kzalloc(dev, sizeof(*pva), GFP_KERNEL);
	if (!pva) {
		err = -ENOMEM;
		goto err_alloc_pva;
	}

	/* Initialize PVA private data */
	if (pdata->version == PVA_HW_GEN3) {
		pva->version = PVA_HW_GEN3;
		pdata->firmware_name = "nvpva_030.fw";
		pdata->firmware_not_in_subdir = true;
		pva->submit_cmd_mode = PVA_SUBMIT_MODE_MMIO_CCQ;
		pva->version_config = &pva_t23x_config;
	} else if (pdata->version == PVA_HW_GEN2) {
		pva->version = PVA_HW_GEN2;
		dev_info(&pdev->dev, "pdata->version is HW_GEN2");
		pdata->firmware_name = "nvpva_020.fw";
		pdata->firmware_not_in_subdir = true;
		pva->submit_cmd_mode = PVA_SUBMIT_MODE_MMIO_CCQ;
		pva->version_config = &pva_t23x_config;
	} else {
		pva->version = PVA_HW_GEN1;
		pdata->firmware_name = "nvpva_010.fw";
		pdata->firmware_not_in_subdir = true;
		pva->submit_cmd_mode = PVA_SUBMIT_MODE_MAILBOX;
		pva->version_config = &pva_t19x_config;
	}

	pva->pdev = pdev;

	/* Enable powergating and timeout only on silicon */
	if (!tegra_platform_is_silicon()) {
		pdata->can_powergate = false;
		pva->timeout_enabled = false;
	} else {
		pva->timeout_enabled = true;
	}

	/* Initialize nvhost specific data */
	pdata->pdev = pdev;
	mutex_init(&pdata->lock);
	pdata->private_data = pva;
	platform_set_drvdata(pdev, pdata);
	mutex_init(&pva->mailbox_mutex);
	for (i = 0; i < MAX_PVA_INTERFACE; i++)
		mutex_init(&pva->ccq_mutex[i]);
	mutex_init(&pva->recovery_mutex);
	mutex_init(&pva->pva_busy_mutex);
	atomic_set(&pva->recovery_cnt, 0);
	pva->submit_task_mode = PVA_SUBMIT_MODE_MMIO_CCQ;
	pva->slcg_disable = 0;
	pva->vmem_war_disable = 0;
	pva->vpu_printf_enabled = true;
	pva->vpu_debug_enabled = true;
	pva->driver_log_mask = NVPVA_DEFAULT_DBG_MASK;
	pva->log_level = NVPVA_DEFAULT_LG_MASK;
	pva->profiling_level = 0;
	pva->stats_enabled = false;
	pva->in_recovery = false;
	memset(&pva->vpu_util_info, 0, sizeof(pva->vpu_util_info));
	pva->syncpts.syncpts_mapped_r = false;
	pva->syncpts.syncpts_mapped_rw = false;
#ifdef CONFIG_PM
	pva->is_suspended = false;
#endif
	nvpva_dbg_fn(pva, "match. compatible = %s", match->compatible);
	pva->is_hv_mode = is_tegra_hypervisor_mode();
	pva->booted = false;
	if (pva->is_hv_mode)
		pva->map_co_needed = false;
	else
		pva->map_co_needed = true;


	if (pdata->version == PVA_HW_GEN1)
		pva->boot_from_file = true;
	else
		pva->boot_from_file = false;

	if (!tegra_platform_is_silicon())
		pva->boot_from_file = true;

#if  defined(CONFIG_PVA_CO_DISABLED_T264)
	if (pdata->version == PVA_HW_GEN3)
		pva->boot_from_file = true;
#endif

#if  defined(CONFIG_PVA_CO_DISABLED)
	if (pdata->version == PVA_HW_GEN2)
		pva->boot_from_file = true;
#endif

#ifdef __linux__
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0)
#if KERNEL_VERSION(4, 15, 0) > LINUX_VERSION_CODE
	if (tegra_chip_get_revision() != TEGRA194_REVISION_A01)
#else
	if (pdata->version != PVA_HW_GEN1)
#endif
		pva->vmem_war_disable = 1;
#endif
#endif

	/* Map MMIO range to kernel space */
	err = nvhost_client_device_get_resources(pdev);
	if (err < 0) {
		dev_err(&pva->pdev->dev, "nvhost_client_device_get_resources failed\n");
		goto err_get_resources;
	}

	/* Get clocks */
	err = nvhost_module_init(pdev);
	if (err < 0) {
		dev_err(&pva->pdev->dev, "nvhost_module_init failed\n");
		goto err_module_init;
	}

	/*
	 * Add this to nvhost device list, initialize scaling,
	 * setup memory management for the device, create dev nodes
	 */
	err = nvhost_client_device_init(pdev);
	if (err < 0) {
		dev_err(&pva->pdev->dev, "nvhost_client_device_init failed\n");
		goto err_client_device_init;
	}

	dev_info(dev, "Completed nvhost_client_device_init\n");

	if (pdata->version == PVA_HW_GEN1) {
		pva->aux_pdev = pva->pdev;
	} else if (pdata->version == PVA_HW_GEN2) {
		pva->aux_pdev =
			nvpva_iommu_context_dev_allocate(aux_dev_name,
							 aux_dev_name_len,
							 false);
	} else {
#ifdef CONFIG_TEGRA_T26X_GRHOST_PVA
		pva->aux_pdev =
			nvpva_iommu_context_dev_allocate(aux_dev_name_t264,
							 aux_dev_name_len_t264,
							 false);
#endif
	}

	if (pva->aux_pdev == NULL) {
		dev_err(&pva->pdev->dev,
			"failed to allocate aux device");
		goto err_context_alloc;
	}

	pva->pool = nvpva_queue_init(pdev,  pva->aux_pdev, &pva_queue_ops,
				     MAX_PVA_QUEUE_COUNT);
	if (IS_ERR(pva->pool)) {
		err = PTR_ERR(pva->pool);
		goto err_queue_init;
	}

	err = pva_alloc_task_status_buffer(pva);
	if (err) {
		dev_err(&pva->pdev->dev, "failed to init task status buffer");
		goto err_status_init;
	}

	err = nvpva_client_context_init(pva);
	if (err) {
		dev_err(&pva->pdev->dev, "failed to init client context");
		goto err_client_ctx_init;
	}

	err = pva_register_isr(pdev);
	if (err < 0) {
		dev_err(&pva->pdev->dev, "failed to register isr");
		goto err_isr_init;
	}

	for (i = 0; i < pva->version_config->irq_count; i++)
		init_waitqueue_head(&pva->cmd_waitqueue[i]);

	pva_abort_init(pva);
	pva_fw_log_dump_init(pva);

	err = nvhost_syncpt_unit_interface_init(pdev);
	if (err)
		goto err_mss_init;

	err = nvpva_syncpt_unit_interface_init(pdev, pva->aux_pdev);
	if (err)
		goto err_syncpt_xface_init;

	mutex_init(&pva->pva_auth.allow_list_lock);
	mutex_init(&pva->pva_auth_sys.allow_list_lock);
	pva->pva_auth.pva_auth_enable = true;
	pva->pva_auth_sys.pva_auth_enable = true;

#ifdef CONFIG_DEBUG_FS
	pva_debugfs_init(pdev);
#endif

	pva->sid_count = 0;
	err = nvpva_iommu_context_dev_get_sids(&pva->sids[1],
					 &pva->sid_count,
					 pdata->version);
	if (err)
		goto err_iommu_ctxt_init;

	pva->sids[0] = nvpva_get_device_hwid(pdev, 0);
	if (pva->sids[0] < 0) {
		err =  pva->sids[0];
		goto err_iommu_ctxt_init;
	}

	++(pva->sid_count);

	offset = (u64) hwpm_get_offset();

	if ((U64_MAX - offset) < pdev->resource[0].start) {
		err = -ENODEV;
		goto err_mss_init;
	}

	nvpva_dbg_info(pva, "hwpm ip %s register", pdev->name);
	pva->hwpm_ip_ops.ip_dev = (void *)pdev;
	pva->hwpm_ip_ops.ip_base_address = (pdev->resource[0].start + offset);
	pva->hwpm_ip_ops.resource_enum = TEGRA_SOC_HWPM_RESOURCE_PVA;
	pva->hwpm_ip_ops.hwpm_ip_pm = &pva_hwpm_ip_pm;
	pva->hwpm_ip_ops.hwpm_ip_reg_op = &pva_hwpm_ip_reg_op;
	tegra_soc_hwpm_ip_register(&pva->hwpm_ip_ops);

#if !IS_ENABLED(CONFIG_TEGRA_GRHOST)
	if (pdata->num_clks > 0) {
		err = kobject_init_and_add(&pdata->clk_cap_kobj, &nvpva_kobj_ktype,
				&pdev->dev.kobj, "%s", "clk_cap");
		if (err) {
			dev_err(dev, "Could not add dir 'clk_cap'\n");
				goto err_iommu_ctxt_init;
		}

		pdata->clk_cap_attrs = devm_kcalloc(dev, pdata->num_clks,
			sizeof(*attr), GFP_KERNEL);
		if (!pdata->clk_cap_attrs)
			goto err_cleanup_sysfs;

		for (j = 0; j < pdata->num_clks; ++j) {
			clks = &pdata->clks[j];
			c = clks->clk;
			if (!c)
				continue;

			attr = &pdata->clk_cap_attrs[j];
			attr->attr.name = __clk_get_name(c);
			/* octal permission is preferred nowadays */
			attr->attr.mode = 0644;
			attr->show = clk_cap_show;
			attr->store = clk_cap_store;
			sysfs_attr_init(&attr->attr);
			if (sysfs_create_file(&pdata->clk_cap_kobj, &attr->attr)) {
				dev_err(dev, "Could not create sysfs attribute %s\n",
					__clk_get_name(c));
				err = -EIO;
				goto err_cleanup_sysfs;
			}
		}
	}
#endif

	return 0;

#if !IS_ENABLED(CONFIG_TEGRA_GRHOST)
err_cleanup_sysfs:
	/* kobj of nvpva_kobj_ktype cleans up sysfs entries automatically */
	kobject_put(&pdata->clk_cap_kobj);
#endif
err_iommu_ctxt_init:
	nvpva_syncpt_unit_interface_deinit(pdev, pva->aux_pdev);
err_syncpt_xface_init:
err_mss_init:
err_isr_init:
	nvpva_client_context_deinit(pva);
err_client_ctx_init:
	pva_free_task_status_buffer(pva);
err_status_init:
	nvpva_queue_deinit(pva->pool);
err_queue_init:
	if (pdata->version != PVA_HW_GEN1)
		nvpva_iommu_context_dev_release(pva->aux_pdev);
err_context_alloc:
	nvhost_client_device_release(pdev);
err_client_device_init:
	nvhost_module_deinit(pdev);
err_module_init:
err_get_resources:
	devm_kfree(dev, pva);
err_alloc_pva:
err_no_ip:
err_get_pdata:

	return err;
}

static int __exit pva_remove(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct pva *pva = pdata->private_data;
	int i;

#if !IS_ENABLED(CONFIG_TEGRA_GRHOST)
	struct kobj_attribute *attr = NULL;

	if (pdata->clk_cap_attrs) {
		for (i = 0; i < pdata->num_clks; i++) {
			attr = &pdata->clk_cap_attrs[i];
			sysfs_remove_file(&pdata->clk_cap_kobj, &attr->attr);
		}

		kobject_put(&pdata->clk_cap_kobj);
	}
#endif

	tegra_soc_hwpm_ip_unregister(&pva->hwpm_ip_ops);

#ifdef CONFIG_DEBUG_FS
	pva_debugfs_deinit(pva);
#endif
	if (pdata->version != PVA_HW_GEN1)
		nvpva_iommu_context_dev_release(pva->aux_pdev);

	pva_auth_allow_list_destroy(&pva->pva_auth_sys);
	pva_auth_allow_list_destroy(&pva->pva_auth);
	pva_free_task_status_buffer(pva);
	pva_fw_log_dump_deinit(pva);
	nvpva_syncpt_unit_interface_deinit(pdev, pva->aux_pdev);
	nvpva_client_context_deinit(pva);
	nvpva_queue_deinit(pva->pool);
	nvhost_client_device_release(pdev);
	for (i = 0; i < pva->version_config->irq_count; i++)
		free_irq(pva->irq[i], pva);

	nvhost_module_deinit(pdev);
	mutex_destroy(&pdata->lock);
	mutex_destroy(&pva->mailbox_mutex);
	for (i = 0; i < MAX_PVA_INTERFACE; i++)
		mutex_destroy(&pva->ccq_mutex[i]);
	mutex_destroy(&pva->recovery_mutex);
	mutex_destroy(&pva->pva_busy_mutex);
	mutex_destroy(&pva->pva_auth.allow_list_lock);
	mutex_destroy(&pva->pva_auth_sys.allow_list_lock);

	return 0;
}

#ifdef CONFIG_PM
static int nvpva_module_runtime_suspend(struct device *dev)
{
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);
	struct pva *pva = pdata->private_data;

	nvpva_dbg_fn(pva, "");

	if (nvhost_module_pm_ops.runtime_suspend != NULL)
		return nvhost_module_pm_ops.runtime_suspend(dev);

	return -EOPNOTSUPP;
}

static int nvpva_module_runtime_resume(struct device *dev)
{
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);
	struct pva *pva = pdata->private_data;

	nvpva_dbg_fn(pva, "");

	if (nvhost_module_pm_ops.runtime_resume != NULL)
		return nvhost_module_pm_ops.runtime_resume(dev);

	return -EOPNOTSUPP;
}

static int nvpva_module_suspend(struct device *dev)
{
	int err = 0;
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);
	struct pva *pva = pdata->private_data;

	nvpva_dbg_fn(pva, "");

	if (nvhost_module_pm_ops.suspend != NULL) {
		err = nvhost_module_pm_ops.suspend(dev);
		if (err != 0) {
			dev_err(dev, "(FAIL) NvHost suspend\n");
			goto fail_nvhost_module_suspend;
		}
	} else {
		err = pm_runtime_force_suspend(dev);
		if (err != 0) {
			dev_err(dev, "(FAIL) PM suspend\n");
			goto fail_nvhost_module_suspend;
		}
	}

	/* Mark module to be in suspend state. */
	pva->is_suspended = true;

fail_nvhost_module_suspend:
	return err;
}

static int nvpva_module_resume(struct device *dev)
{
	int err = 0;
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);
	struct pva *pva = pdata->private_data;	nvpva_dbg_fn(pva, "");


	nvpva_dbg_fn(pva, "");

	/* Confirm if module is in suspend state. */
	if (!pva->is_suspended) {
		dev_warn(dev, "nvpva is not in suspend state.\n");
		goto fail_not_in_suspend;
	}

	if (nvhost_module_pm_ops.resume != NULL) {
		err = nvhost_module_pm_ops.resume(dev);
		if (err != 0) {
			dev_err(dev, "(FAIL) NvHost resume\n");
			goto fail_nvhost_module_resume;
		}
	} else {
		err = pm_runtime_force_resume(dev);
		if (err != 0) {
			dev_err(dev, "(FAIL) PM resume\n");
			goto fail_nvhost_module_resume;
		}
	}

	return 0;

fail_nvhost_module_resume:
fail_not_in_suspend:
	return err;
}

static int nvpva_module_prepare_suspend(struct device *dev)
{
	int err = 0;
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);
	struct pva *pva = pdata->private_data;

	nvpva_dbg_fn(pva, "");

	/* Confirm if module is not in suspend state. */
	if (pva->is_suspended) {
		dev_warn(dev, "nvpva is already in suspend state.\n");
		goto fail_already_in_suspend;
	}

	/* Prepare for queue pool suspension. */
	err = nvpva_queue_pool_prepare_suspend(pva->pool);
	if (err != 0) {
		dev_err(dev, "(FAIL) Queue suspend\n");
		goto fail_nvpva_queue_pool_prepare_suspend;
	}

	/* NvHost prepare suspend - callback */
	if (nvhost_module_pm_ops.prepare != NULL) {
		err = nvhost_module_pm_ops.prepare(dev);
		if (err != 0) {
			dev_err(dev, "(FAIL) NvHost prepare suspend\n");
			goto fail_nvhost_module_prepare_suspend;
		}
	} else {
		/* If we took an extra reference, drop it now to prevent
		 * the device from automatically resuming upon system
		 * resume.
		 */
		pm_runtime_put_sync(dev);
	}


	return 0;

fail_nvhost_module_prepare_suspend:
fail_nvpva_queue_pool_prepare_suspend:
fail_already_in_suspend:
	return err;
}

static void nvpva_module_complete_resume(struct device *dev)
{
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);
	struct pva *pva = pdata->private_data;

	nvpva_dbg_fn(pva, "");

	if (nvhost_module_pm_ops.complete != NULL) {
		nvhost_module_pm_ops.complete(dev);
	} else {
		/* Retake reference dropped above */
		pm_runtime_get_noresume(dev);
	}

	/* Module is no longer in suspend and has resumed successfully */
	pva->is_suspended = false;
}

/**
 * SC7 suspend sequence
 * - prepare_suspend
 * - suspend
 *
 * SC7 resume sequence
 * - resume
 * - complete_resume
 **/
const struct dev_pm_ops nvpva_module_pm_ops = {
	SET_RUNTIME_PM_OPS(nvpva_module_runtime_suspend,
		nvpva_module_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(nvpva_module_suspend,
		nvpva_module_resume)
	.prepare = nvpva_module_prepare_suspend,
	.complete = nvpva_module_complete_resume,
};
#endif /* CONFIG_PM */

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void __exit pva_remove_wrapper(struct platform_device *pdev)
{
	pva_remove(pdev);
}
#else
static int __exit pva_remove_wrapper(struct platform_device *pdev)
{
	return pva_remove(pdev);
}
#endif

static struct platform_driver pva_driver = {
	.probe = pva_probe,
	.remove = __exit_p(pva_remove_wrapper),
	.driver = {
		.owner = THIS_MODULE,
		.name = "pva",
#ifdef CONFIG_OF
		.of_match_table = tegra_pva_of_match,
#endif
#ifdef CONFIG_PM
		.pm = &nvpva_module_pm_ops,
#endif
	},
};
#if IS_ENABLED(CONFIG_TEGRA_GRHOST)
static int __init nvpva_init(void)
{
	int err;

	err = platform_driver_register(&nvpva_iommu_context_dev_driver);
	if (err < 0)
		return err;

	err = platform_driver_register(&pva_driver);
	if (err < 0)
		platform_driver_unregister(&nvpva_iommu_context_dev_driver);

	return err;
}
module_init(nvpva_init);
static void __exit nvpva_exit(void)
{
	platform_driver_unregister(&pva_driver);
	platform_driver_unregister(&nvpva_iommu_context_dev_driver);
}
module_exit(nvpva_exit);
#else
static struct host1x_driver host1x_nvpva_driver = {
	.driver = {
		.name = "host1x-nvpva",
	},
	.subdevs = tegra_pva_of_match,
};
static int __init nvpva_init(void)
{
	int err;

	err = host1x_driver_register(&host1x_nvpva_driver);
	if (err < 0)
		goto out;

	err = platform_driver_register(&nvpva_iommu_context_dev_driver);
	if (err < 0)
		goto ctx_failed;

	err = platform_driver_register(&pva_driver);
	if (err)
		goto pva_failed;

	return err;

pva_failed:
	platform_driver_unregister(&nvpva_iommu_context_dev_driver);
ctx_failed:
	host1x_driver_unregister(&host1x_nvpva_driver);
out:
	return err;
}

module_init(nvpva_init);
static void __exit nvpva_exit(void)
{
	platform_driver_unregister(&pva_driver);
	platform_driver_unregister(&nvpva_iommu_context_dev_driver);
	host1x_driver_unregister(&host1x_nvpva_driver);
}

module_exit(nvpva_exit);
#endif

#if defined(NV_MODULE_IMPORT_NS_CALLS_STRINGIFY)
MODULE_IMPORT_NS(DMA_BUF);
#else
MODULE_IMPORT_NS("DMA_BUF");
#endif
MODULE_LICENSE("GPL v2");
