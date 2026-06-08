// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES.  All rights reserved.
 *
 * Tegra TSEC Module Support
 */

#include <nvidia/conftest.h>

#include "tsec_linux.h"
#include "tsec.h"
#include "tsec_boot.h"
#include "tsec_regs.h"
#include "tsec_t264.h"


/*
 * TSEC register offsets
 */
extern struct tsec_reg_offsets_t t23x_reg_offsets;
extern struct tsec_reg_offsets_t t264_reg_offsets;

/*
 * TSEC Device Data
 */

static struct tsec_device_data t23x_tsec_data = {
	.rate = {192000000, 0, 204000000},
	.riscv_desc_bin		= "tegra23x/nvhost_tsec_desc.fw",
	.riscv_image_bin	= "tegra23x/nvhost_tsec_riscv.fw",
	.dma_mask_bits		= 39,
	.soc			= TSEC_ON_T23x,
	.tsec_reg_offsets	= &t23x_reg_offsets
};
MODULE_FIRMWARE("tegra23x/nvhost_tsec_riscv.fw");
MODULE_FIRMWARE("tegra23x/nvhost_tsec_desc.fw");

static struct tsec_device_data t239_tsec_data = {
	.rate = {192000000, 0, 204000000},
	.riscv_desc_bin		= "tegra239/nvhost_tsec_desc.fw",
	.riscv_image_bin	= "tegra239/nvhost_tsec_riscv.fw",
	.dma_mask_bits		= 39,
	.soc			= TSEC_ON_T239,
	.tsec_reg_offsets	= &t23x_reg_offsets
};

static struct tsec_device_data t264_tsec_data = {
	.rate = {192000000, 0, 204000000},
	.riscv_desc_bin		= "tegra264/nvhost_tsec_desc.fw",
	.riscv_image_bin	= "tegra264/nvhost_tsec_riscv.fw",
	.dma_mask_bits		= 48,
	.soc			= TSEC_ON_T26x,
	.tsec_reg_offsets	= &t264_reg_offsets
};

/*
 * TSEC Register Access APIs
 */

void tsec_writel(struct tsec_device_data *pdata, u32 r, u32 v)
{
	void __iomem *addr = pdata->reg_aperture + r;

	writel(v, addr);
}

u32 tsec_readl(struct tsec_device_data *pdata, u32 r)
{
	void __iomem *addr = pdata->reg_aperture + r;

	return readl(addr);
}

/*
 * TSEC helpers for clock, reset and register initialisation
 */

static int tsec_enable_clks(struct tsec_device_data *pdata)
{
	int err = 0, index = 0;

	for (index = 0; index < TSEC_NUM_OF_CLKS; index++) {
		err = clk_prepare_enable(pdata->clk[index]);
		if (err) {
			err = -EINVAL;
			goto out;
		}
	}

out:
	return err;
}

static void tsec_disable_clk(struct tsec_device_data *pdata)
{
	int index = 0;

	for (index = 0; index < TSEC_NUM_OF_CLKS; index++)
		clk_disable_unprepare(pdata->clk[index]);
}

static void tsec_deassert_reset(struct tsec_device_data *pdata)
{
	reset_control_acquire(pdata->reset_control);
	/* Does assert and then deassert */
	reset_control_reset(pdata->reset_control);
	reset_control_release(pdata->reset_control);
}

static void tsec_assert_reset(struct tsec_device_data *pdata)
{
	reset_control_acquire(pdata->reset_control);
	reset_control_assert(pdata->reset_control);
	reset_control_release(pdata->reset_control);
}


static void tsec_set_cg_regs(struct tsec_device_data *pdata)
{
	struct tsec_reg_offsets_t *reg_off = pdata->tsec_reg_offsets;

	tsec_writel(pdata, reg_off->PRIV_BLOCKER_CTRL_CG1, 0x0);
	tsec_writel(pdata, reg_off->RISCV_CG, 0x3);
}

#ifdef CONFIG_DEBUG_FS

struct nvriscv_log_buffer {
	/* read offset updated by client RM */
	uint32_t read_offset;
	/* write offset updated by firmware RM */
	uint32_t write_offset;
	/* buffer size configured by client RM */
	uint32_t buffer_size;
	/* magic number for header validation in nvwatch */
	uint32_t magic;
};

#define LOG_BUF_SIZE	((4 * SZ_1K) - sizeof(struct nvriscv_log_buffer))
#define INFO_BUF_SIZE	(SZ_128)
static u8 log_buf[LOG_BUF_SIZE]; /* Read from DMEM into local buffer */
static u8 info_buf[INFO_BUF_SIZE];

static int tsec_debug_show(struct seq_file *s, void *unused)
{
	int info_len = 0;
	struct tsec_device_data *pdata = (struct tsec_device_data *)s->private;
	struct tsec_reg_offsets_t *reg_off = pdata->tsec_reg_offsets;

	/* Do not attempt to read DMEM if TSEC is power-down */
	if (pdata->power_on) {
#define DMEM_PORT	(0)
		/* Offset of Log Buffer in DMEM */
		u32 log_bug_off  = reg_off->DMEM_LOGBUF_OFFSET;
		u32 dmemC        = tsec_falcon_dmemc_r(DMEM_PORT, reg_off->FALCON_DMEMC_0);
		u32 dmemD        = tsec_falcon_dmemd_r(DMEM_PORT, reg_off->FALCON_DMEMD_0);
		struct nvriscv_log_buffer log_buf_info;

		/* Auto Increment Read */
		u32 loop_index = 0;

		tsec_writel(pdata, dmemC, log_bug_off | 0x02000000);
		while (loop_index < LOG_BUF_SIZE) {
			u32 reg_val;

			reg_val = tsec_readl(pdata, dmemD);
			log_buf[loop_index++] = (u8)((reg_val >> 0) & 0xFF);
			log_buf[loop_index++] = (u8)((reg_val >> 8) & 0xFF);
			log_buf[loop_index++] = (u8)((reg_val >> 16) & 0xFF);
			log_buf[loop_index++] = (u8)((reg_val >> 24) & 0xFF);
		}
		log_buf_info.read_offset  = tsec_readl(pdata, dmemD);
		log_buf_info.write_offset = tsec_readl(pdata, dmemD);
		log_buf_info.buffer_size  = tsec_readl(pdata, dmemD);
		log_buf_info.magic        = tsec_readl(pdata, dmemD);

		/*
		* Replace any null charecter with new line because tsec ucode
		* does the reverse before dumping the logs in the buffer
		*/
		for (loop_index = 0; loop_index < LOG_BUF_SIZE; loop_index++) {
			if (log_buf[loop_index] == 0)
				log_buf[loop_index] = 0xA;
		}

		/* Insert null charecter at end of log buffer */
		log_buf[log_buf_info.write_offset] = 0;

		/* Print log_buf */
		info_len = sprintf(info_buf,
			"Tsec Logs Start: read_offset=%d write_offset=%d buffer_size=%d magic=0x%x\n",
			log_buf_info.read_offset, log_buf_info.write_offset,
			log_buf_info.buffer_size, log_buf_info.magic);
		if (info_len > 0)
			seq_write(s, info_buf, info_len);

		seq_write(s, log_buf,
			(log_buf_info.write_offset - log_buf_info.read_offset));

		info_len = strlen("Tsec Logs End\n");
		strcpy(info_buf, "Tsec Logs End\n");
		info_buf[info_len] = '\0';
		seq_write(s, info_buf, info_len);
	} else {
		info_len = strlen("Tsec power-down\n");
		strcpy(info_buf, "Tsec power-down\n");
		info_buf[info_len] = '\0';
		seq_write(s, info_buf, info_len);
	}

	return 0;
}

static int tsec_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, tsec_debug_show, inode->i_private);
}

static const struct file_operations tsec_debug_fops = {
	.open		= tsec_debug_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * TSEC debugfs interface
 * create: /sys/kernel/debug/tegra_tsec/fw_logs.
 * cat /sys/kernel/debug/tegra_tsec/fw_logs will read the DMEM reserved for
 * debug messages and print it on console.
 */

static int tsec_module_init_debugfs(struct platform_device *dev)
{
	struct tsec_device_data *pdata = platform_get_drvdata(dev);

	pdata->debug_root = debugfs_create_dir("tegra_tsec", NULL);
	if (!pdata->debug_root)
		return -ENOMEM;

	debugfs_create_file("fw_logs", 0444, pdata->debug_root,
			pdata, &tsec_debug_fops);

	return 0;
}

static void tsec_module_deinit_debugfs(struct platform_device *dev)
{
	struct tsec_device_data *pdata = platform_get_drvdata(dev);

	debugfs_remove_recursive(pdata->debug_root);
}
#endif /* CONFIG_DEBUG_FS */

/*
 * TSEC StreamID Register Programming Operation
 */
void tsec_set_streamid_regs(struct device *dev,
	struct tsec_device_data *pdata)
{
	struct iommu_fwspec *fwspec;
	int streamid;
	struct tsec_reg_offsets_t *reg_off = pdata->tsec_reg_offsets;
	/* Get the StreamID value */
	switch (pdata->soc) {
	case TSEC_ON_T26x:
		streamid = 0x7F; /* bypass hwid */
		break;
	default:
		fwspec = dev_iommu_fwspec_get(dev);
		if (fwspec && fwspec->num_ids)
			streamid = fwspec->ids[0] & 0xffff;
		else
			streamid = 0x7F; /* bypass hwid */
		break;
	}

	/* Update the StreamID value */
	tsec_writel(pdata, reg_off->THI_STREAMID0_0, streamid);
	tsec_writel(pdata, reg_off->THI_STREAMID1_0, streamid);

	/* Indicate that streamid programming is done */
	tsec_writel(pdata, reg_off->MAILBOX0, TSEC_RISCV_STREAMID_SET_DONE);
}

/*
 * TSEC Power Management Operations
 */

int tsec_poweron(struct device *dev)
{
	struct tsec_device_data *pdata;
	int err = 0, tsec_clks_enabled = 0;

	pdata = dev_get_drvdata(dev);

	err = tsec_enable_clks(pdata);
	if (err) {
		dev_err(dev, "Cannot enable tsec clocks %d\n", err);
		goto out;
	}
	tsec_clks_enabled = 1;

	tsec_deassert_reset(pdata);
	tsec_set_cg_regs(pdata);
	tsec_set_streamid_regs(dev, pdata);

	err = tsec_finalize_poweron(to_platform_device(dev));
	/* Failed to start the device */
	if (err) {
		dev_err(dev, "tsec_finalize_poweron error %d\n", err);
		goto out;
	}

	pdata->power_on = true;

out:
	if (err && tsec_clks_enabled)
		tsec_disable_clk(pdata);
	return err;
}

int tsec_poweroff(struct device *dev)
{
	struct tsec_device_data *pdata;

	pdata = dev_get_drvdata(dev);

	if (pdata->power_on) {
		tsec_assert_reset(pdata);
		tsec_disable_clk(pdata);
		tsec_prepare_poweroff(to_platform_device(dev));
		pdata->power_on = false;
	}

	return 0;
}

static int tsec_module_suspend(struct device *dev)
{
	struct tsec_device_data *pdata = dev_get_drvdata(dev);

	switch (pdata->soc) {
	case TSEC_ON_T26x:
		/*
		 * If TSEC is on T26x, we don't need to power off TSEC.
		 */
		return 0;
	default:
		return tsec_poweroff(dev);
	}

}

static int tsec_module_resume(struct device *dev)
{
	struct tsec_device_data *pdata = dev_get_drvdata(dev);
	switch (pdata->soc) {
	case TSEC_ON_T26x:
		/*
		 * If the system resumed from suspend to idle, we don't need to resume TSEC.
		 * This is because TSEC is already powered on and running.
		 */
		if (pm_suspend_target_state == PM_SUSPEND_TO_IDLE)
			return 0;
		return tsec_t264_init(to_platform_device(dev));
	default:
		return tsec_poweron(dev);
	}
}

/*
 * TSEC Probe/Remove and Module Init
 */

static int tsec_module_init(struct platform_device *dev)
{
	struct tsec_device_data *pdata = platform_get_drvdata(dev);
	struct resource *res = NULL;
	void __iomem *regs = NULL;

	/* Initialize dma parameters */
	dma_set_mask_and_coherent(&dev->dev, DMA_BIT_MASK(pdata->dma_mask_bits));
	dev->dev.dma_parms = &pdata->dma_parms;
	dma_set_max_seg_size(&dev->dev, UINT_MAX);

	/* Get register aperture */
	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;
	regs = devm_ioremap_resource(&dev->dev, res);
	if (IS_ERR(regs)) {
		int err = PTR_ERR(regs);

		dev_err(&dev->dev, "failed to get register memory %d\n", err);
		return err;
	}
	pdata->reg_aperture = regs;

	/* skip enabling clocks for T26x since PSC already does it*/
	if (pdata->soc == TSEC_ON_T26x) {
		return 0;
	}

	/* Get interrupt */
	pdata->irq = platform_get_irq(dev, 0);
	if (pdata->irq < 0) {
		dev_err(&dev->dev, "failed to get irq %d\n", -pdata->irq);
		return -ENXIO;
	}

	/* get TSEC_CLK and enable it */
	pdata->clk[TSEC_CLK_INDEX] = devm_clk_get(&dev->dev, TSEC_CLK_NAME);
	if (IS_ERR(pdata->clk[TSEC_CLK_INDEX])) {
		dev_err(&dev->dev, "failed to get %s clk", TSEC_CLK_NAME);
		return -ENXIO;
	}
	clk_set_rate(pdata->clk[TSEC_CLK_INDEX],
		clk_round_rate(pdata->clk[TSEC_CLK_INDEX],
		pdata->rate[TSEC_CLK_INDEX]));
	if (clk_prepare_enable(pdata->clk[TSEC_CLK_INDEX]) != 0) {
		dev_err(&dev->dev, "failed to enable %s clk", TSEC_CLK_NAME);
		return -ENXIO;
	}

	/* get EFUSE_CLK and enable it */
	pdata->clk[EFUSE_CLK_INDEX] = devm_clk_get(&dev->dev, EFUSE_CLK_NAME);
	if (IS_ERR(pdata->clk[EFUSE_CLK_INDEX])) {
		dev_err(&dev->dev, "failed to get %s clk", EFUSE_CLK_NAME);
		clk_disable_unprepare(pdata->clk[TSEC_CLK_INDEX]);
		return -ENXIO;
	}
	clk_set_rate(pdata->clk[EFUSE_CLK_INDEX],
		clk_round_rate(pdata->clk[EFUSE_CLK_INDEX],
		pdata->rate[EFUSE_CLK_INDEX]));
	if (clk_prepare_enable(pdata->clk[EFUSE_CLK_INDEX]) != 0) {
		dev_err(&dev->dev, "failed to enable %s clk", EFUSE_CLK_NAME);
		clk_disable_unprepare(pdata->clk[TSEC_CLK_INDEX]);
		return -ENXIO;
	}

	/* get TSEC_PKA_CLK and enable it */
	pdata->clk[TSEC_PKA_CLK_INDEX] = devm_clk_get(&dev->dev, TSEC_PKA_CLK_NAME);
	if (IS_ERR(pdata->clk[TSEC_PKA_CLK_INDEX])) {
		dev_err(&dev->dev, "failed to get %s clk", TSEC_PKA_CLK_NAME);
		clk_disable_unprepare(pdata->clk[EFUSE_CLK_INDEX]);
		clk_disable_unprepare(pdata->clk[TSEC_CLK_INDEX]);
		return -ENXIO;
	}
	clk_set_rate(pdata->clk[TSEC_PKA_CLK_INDEX],
		clk_round_rate(pdata->clk[TSEC_PKA_CLK_INDEX],
		pdata->rate[TSEC_PKA_CLK_INDEX]));
	if (clk_prepare_enable(pdata->clk[TSEC_PKA_CLK_INDEX]) != 0) {
		dev_err(&dev->dev, "failed to enable %s clk", TSEC_PKA_CLK_NAME);
		clk_disable_unprepare(pdata->clk[EFUSE_CLK_INDEX]);
		clk_disable_unprepare(pdata->clk[TSEC_CLK_INDEX]);
		return -ENXIO;
	}

	/* get reset_control and reset the module */
	pdata->reset_control = devm_reset_control_get_exclusive_released(
					&dev->dev, NULL);
	if (IS_ERR(pdata->reset_control))
		pdata->reset_control = NULL;
	tsec_deassert_reset(pdata);

	/* disable the clocks after resetting the module */
	tsec_disable_clk(pdata);

	return 0;
}


static const struct dev_pm_ops tsec_module_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(tsec_module_suspend, tsec_module_resume)
};

static const struct of_device_id tsec_of_match[] = {
	{ .compatible = "nvidia,tegra234-tsec",
		.data = (struct tsec_device_data *)&t23x_tsec_data },
	{ .compatible = "nvidia,tegra239-tsec",
		.data = (struct tsec_device_data *)&t239_tsec_data },
	{ .compatible = "nvidia,tegra264-tsec",
		.data = (struct tsec_device_data *)&t264_tsec_data },
	{ },
};

static int tsec_probe(struct platform_device *dev)
{
	int err;
	struct tsec_device_data *pdata = NULL;

	/* Get device platform data */
	if (dev->dev.of_node) {
		const struct of_device_id *match;

		match = of_match_device(tsec_of_match, &dev->dev);
		if (match)
			pdata = (struct tsec_device_data *)match->data;
	} else {
		pdata = (struct tsec_device_data *)dev->dev.platform_data;
	}
	pdata->pdev = dev;
	platform_set_drvdata(dev, pdata);

	err = tsec_module_init(dev);
	if (err) {
		dev_err(&dev->dev, "error %d in tsec_module_init\n", err);
		return err;
	}

#ifdef CONFIG_DEBUG_FS
	if (debugfs_initialized()) {
		err = tsec_module_init_debugfs(dev);
		if (err) {
			dev_err(&dev->dev, "error %d in tsec_module_init_debugfs\n", err);
			return err;
		}
	}
#endif /* CONFIG_DEBUG_FS */

	switch (pdata->soc) {
	case TSEC_ON_T26x:
		err = tsec_t264_init(dev);
		break;
	default:
		err = tsec_kickoff_boot(dev);
		break;
	}

	return err;
}

static int tsec_remove(struct platform_device *dev)
{
#ifdef CONFIG_DEBUG_FS
	tsec_module_deinit_debugfs(dev);
#endif /* CONFIG_DEBUG_FS */

	return tsec_poweroff(&dev->dev);
}

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void tsec_remove_wrapper(struct platform_device *pdev)
{
	tsec_remove(pdev);
}
#else
static int tsec_remove_wrapper(struct platform_device *pdev)
{
	return tsec_remove(pdev);
}
#endif

static struct platform_driver tsec_driver = {
	.probe = tsec_probe,
	.remove = tsec_remove_wrapper,
	.driver = {
		.owner = THIS_MODULE,
		.name = "tsec",
		.pm = &tsec_module_pm_ops,
		.of_match_table = tsec_of_match,
	}
};

module_platform_driver(tsec_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nikesh Oswal <noswal@nvidia.com>");
MODULE_DEVICE_TABLE(of, tsec_of_match);
MODULE_DESCRIPTION("TSEC Driver");
