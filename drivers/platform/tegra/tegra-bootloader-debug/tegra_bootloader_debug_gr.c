// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/memblock.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include "tegra_bootloader_debug_gr.h"

static phys_addr_t tegra_bl_debug_data_start;
static phys_addr_t tegra_bl_debug_data_size;
static phys_addr_t tegra_bl_bcp_start;
static phys_addr_t tegra_bl_bcp_size;

static char *bl_debug_data = "0@0x0";
static char *boot_cfg_dataptr = "0@0x0";

#ifdef CONFIG_DEBUG_FS
static const char *dir_name = "tegra_bootloader";

static const char *gr_file_mb1 = "gr_mb1";
static const char *gr_file_mb2 = "gr_mb2";
static const char *gr_file_cpu_bl = "gr_cpu_bl";
static const char *boot_cfg = "boot_cfg";

struct gr_address_value {
	unsigned int gr_address;
	unsigned int gr_value;
};

struct gr_header {
	uint32_t mb1_offset;
	uint32_t mb1_size;
	uint32_t mb2_offset;
	uint32_t mb2_size;
	uint32_t cpu_bl_offset;
	uint32_t cpu_bl_size;
};

enum gr_stage {
	enum_gr_mb1,
	enum_gr_mb2,
	enum_gr_cpu_bl,
};

struct spi_header {
	uint16_t crc;
	uint16_t crc_ack;
	uint16_t frame_len;
	struct {
		uint8_t id: 3;
		uint8_t version: 3;
		uint8_t reserved: 1;
		uint8_t has_ts: 1;
	} version;
} __packed;

struct spi_boot_header {
	struct spi_header header;
	bool rm_respond_evt: 1;
	uint8_t rm_respond_data: 4;
	uint8_t reserved1: 3;
};

struct spi_boot_rx_frame_full {
	struct spi_boot_header header;
	uint8_t data[8200 - sizeof(struct spi_boot_header)];
};

static const uint32_t gr_mb1 = enum_gr_mb1;
static const uint32_t gr_mb2 = enum_gr_mb2;
static const uint32_t gr_cpu_bl = enum_gr_cpu_bl;

static int dbg_golden_register_show(struct seq_file *s, void *unused);
static int dbg_golden_register_open_mb1(struct inode *inode, struct file *file);
static int dbg_golden_register_open_mb2(struct inode *inode, struct file *file);
static int dbg_golden_register_open_cpu_bl(struct inode *inode, struct file *file);
static struct dentry *bl_debug_node;
static struct dentry *bl_debug_verify_reg_node;
static struct dentry *bl_debug_boot_cfg;
static void *tegra_bl_mapped_debug_data_start;
static int boot_cfg_show(struct seq_file *s, void *unused);
static int boot_cfg_open(struct inode *inode, struct file *file);
static void *tegra_bl_mapped_boot_cfg_start;

static const struct file_operations debug_gr_fops_mb1 = {
	.open		= dbg_golden_register_open_mb1,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};


static const struct file_operations debug_gr_fops_mb2 = {
	.open		= dbg_golden_register_open_mb2,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};


static const struct file_operations debug_gr_fops_cpu_bl = {
	.open		= dbg_golden_register_open_cpu_bl,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations boot_cfg_fops = {
	.open		= boot_cfg_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int dbg_golden_register_show(struct seq_file *s, void *unused)
{
	struct gr_header *golden_reg_header = (struct gr_header *)tegra_bl_mapped_debug_data_start;
	struct gr_address_value *gr_memory_dump;
	unsigned int gr_entries = 0;
	int i;

	switch (*(int *)(s->private)) {
	case enum_gr_mb1:
		gr_entries = golden_reg_header->mb1_size / sizeof(struct gr_address_value);
		gr_memory_dump = (struct gr_address_value *)(golden_reg_header->mb1_offset +
				tegra_bl_mapped_debug_data_start + sizeof(struct gr_header));
		break;
	case enum_gr_mb2:
		gr_entries = golden_reg_header->mb2_size / sizeof(struct gr_address_value);
		gr_memory_dump = (struct gr_address_value *)(golden_reg_header->mb2_offset +
				tegra_bl_mapped_debug_data_start + sizeof(struct gr_header));
		break;
	case enum_gr_cpu_bl:
		gr_entries = golden_reg_header->cpu_bl_size / sizeof(struct gr_address_value);
		gr_memory_dump = (struct gr_address_value *)(golden_reg_header->cpu_bl_offset +
				tegra_bl_mapped_debug_data_start + sizeof(struct gr_header));
		break;
	default:
		seq_printf(s, "Eiiiirror mapping bootloader debug data%x \n", *(int *)(s->private));
		return 0;
	}
	if (!gr_entries || !tegra_bl_mapped_debug_data_start) {
		seq_puts(s, "Error mapping bootloader debug data\n");
		return 0;
	}

	for (i = 0; i < gr_entries; i++) {
		seq_printf(s, "{Address 0x%08x}, {Value 0x%08x}\n",
			gr_memory_dump->gr_address, gr_memory_dump->gr_value);

		gr_memory_dump++;
	}

	return 0;
}

static int dbg_golden_register_open_mb1(__attribute((unused))struct inode *inode, struct file *file)
{
	return single_open(file, dbg_golden_register_show, (void *)&gr_mb1);
}

static int dbg_golden_register_open_mb2(__attribute((unused))struct inode *inode, struct file *file)
{
	return single_open(file, dbg_golden_register_show, (void *)&gr_mb2);
}

static int dbg_golden_register_open_cpu_bl(__attribute((unused))struct inode *inode, struct file *file)
{
	return single_open(file, dbg_golden_register_show, (void *)&gr_cpu_bl);
}

static int boot_cfg_show(struct seq_file *s, void *unused)
{
	uint8_t *data = tegra_bl_mapped_boot_cfg_start;
	struct spi_boot_rx_frame_full *spi_frame =
			tegra_bl_mapped_boot_cfg_start;
	uint32_t i;

	seq_puts(s, "\n Dumping Boot Configuration Protocol ");
	seq_printf(s, "0x%08x bytes @ 0x%08x\n",
			(unsigned int)tegra_bl_bcp_size,
			(unsigned int)tegra_bl_bcp_start);

	seq_puts(s, "\n SPI frame header\n");
	seq_printf(s, " CRC	  : 0x%02x\n",
			spi_frame->header.header.crc);
	seq_printf(s, " CRC ACK	  : 0x%02x\n",
			spi_frame->header.header.crc_ack);
	seq_printf(s, " Frame len	: 0x%02x (%d)\n",
			spi_frame->header.header.frame_len,
			spi_frame->header.header.frame_len);
	seq_printf(s, " Protocol ID  : 0x%01x\n",
			spi_frame->header.header.version.id);
	seq_printf(s, " Version	  : 0x%01x\n",
			spi_frame->header.header.version.version);
	seq_printf(s, " Has ts	: 0x%01x\n",
			spi_frame->header.header.version.has_ts);
	seq_printf(s, " Run mode evt: 0x%01x\n",
			spi_frame->header.rm_respond_evt);
	seq_printf(s, " Run mode	 : 0x%01x\n",
			spi_frame->header.rm_respond_data);

	for (i = 0; i < tegra_bl_bcp_size; i++) {
		if (i % 12 == 0)
			seq_printf(s, "\n %05d | ", i);

		seq_printf(s, "0x%02x ", data[i]);
	}
	return 0;
}

static int boot_cfg_open(struct inode *inode, struct file *file)
{
	return single_open(file, boot_cfg_show, &inode->i_private);
}
#endif /* CONFIG_DEBUG_FS */

int tegra_bootloader_debug_gr_init(struct platform_device *pdev)
{
#ifdef CONFIG_DEBUG_FS
	void __iomem *ptr_bl_debug_data_start = NULL;
	void __iomem *ptr_bl_boot_cfg_start = NULL;

	if (!debugfs_initialized()) {
		dev_info(&pdev->dev, "%s: debugfs not initialized, skip debug_gr_init\n",
				__func__);
		return 0;
	}

	bl_debug_node = debugfs_create_dir(dir_name, NULL);
	if (IS_ERR_OR_NULL(bl_debug_node)) {
		dev_err(&pdev->dev, "%s: failed to create debugfs entries: %ld\n",
			__func__, PTR_ERR(bl_debug_node));
		goto out_err;
	}

	dev_info(&pdev->dev, "%s: created %s directory\n", __func__, dir_name);

	bl_debug_verify_reg_node = debugfs_create_file(gr_file_mb1, S_IRUGO,
				bl_debug_node, NULL, &debug_gr_fops_mb1);
	if (IS_ERR_OR_NULL(bl_debug_verify_reg_node)) {
		dev_err(&pdev->dev, "%s: failed to create debugfs entries: %ld\n",
			__func__, PTR_ERR(bl_debug_verify_reg_node));
		goto out_err;
	}

	bl_debug_verify_reg_node = debugfs_create_file(gr_file_mb2, S_IRUGO,
				bl_debug_node, NULL, &debug_gr_fops_mb2);
	if (IS_ERR_OR_NULL(bl_debug_verify_reg_node)) {
		dev_err(&pdev->dev, "%s: failed to create debugfs entries: %ld\n",
			__func__, PTR_ERR(bl_debug_verify_reg_node));
		goto out_err;
	}

	bl_debug_verify_reg_node = debugfs_create_file(gr_file_cpu_bl, S_IRUGO,
				bl_debug_node, NULL, &debug_gr_fops_cpu_bl);
	if (IS_ERR_OR_NULL(bl_debug_verify_reg_node)) {
		dev_err(&pdev->dev, "%s: failed to create debugfs entries: %ld\n",
			__func__, PTR_ERR(bl_debug_verify_reg_node));
		goto out_err;
	}

	if (tegra_bl_bcp_start && tegra_bl_bcp_size) {
		bl_debug_boot_cfg = debugfs_create_file(boot_cfg, 0444,
			bl_debug_node, NULL, &boot_cfg_fops);
		if (IS_ERR_OR_NULL(bl_debug_boot_cfg)) {
			dev_err(&pdev->dev, "%s: failed to create debugfs entries: %ld\n",
				__func__, PTR_ERR(bl_debug_boot_cfg));
			goto out_err;
		}
	}

	if (tegra_bl_debug_data_start && tegra_bl_debug_data_size) {
		tegra_bl_mapped_debug_data_start =
			phys_to_virt(tegra_bl_debug_data_start);

		ptr_bl_debug_data_start = ioremap(tegra_bl_debug_data_start,
				tegra_bl_debug_data_size);

		WARN_ON(!ptr_bl_debug_data_start);
		if (!ptr_bl_debug_data_start) {
			dev_err(&pdev->dev, "%s: Failed to map tegra_bl_debug_data_start%08x\n",
				__func__, (unsigned int)tegra_bl_debug_data_start);
			goto out_err;
		}

		dev_info(&pdev->dev, "Remapped tegra_bl_debug_data_start(0x%llx) to address(0x%llx), size(0x%llx)\n",
			(u64)tegra_bl_debug_data_start,
			(__force u64)ptr_bl_debug_data_start,
			(u64)tegra_bl_debug_data_size);
		tegra_bl_mapped_debug_data_start =
				(__force void *)ptr_bl_debug_data_start;

	}

	/*
	 * The BCP can be optional, so ignore creating if variables are not set
	 */
	if (tegra_bl_bcp_start && tegra_bl_bcp_size) {
		tegra_bl_mapped_boot_cfg_start =
			phys_to_virt(tegra_bl_bcp_start);

		ptr_bl_boot_cfg_start = ioremap(tegra_bl_bcp_start,
						tegra_bl_bcp_size);

		WARN_ON(!ptr_bl_boot_cfg_start);
		if (!ptr_bl_boot_cfg_start) {
			dev_err(&pdev->dev, "%s: Failed to map tegra_bl_prof_start %08x\n",
				__func__,
				(unsigned int)tegra_bl_bcp_start);
			goto out_err;
		}

		dev_info(&pdev->dev, "Remapped tegra_bl_bcp_start(0x%llx) to address(0x%llx), size(0x%llx)\n",
			(u64)tegra_bl_bcp_start,
			(__force u64)ptr_bl_boot_cfg_start,
			(u64)tegra_bl_bcp_size);
		tegra_bl_mapped_boot_cfg_start =
			(__force void *)ptr_bl_boot_cfg_start;
	}

	return 0;

out_err:
	if (!IS_ERR_OR_NULL(bl_debug_node))
		debugfs_remove_recursive(bl_debug_node);
	if (ptr_bl_debug_data_start)
		iounmap(ptr_bl_debug_data_start);
	if (ptr_bl_boot_cfg_start)
		iounmap(ptr_bl_boot_cfg_start);

	return -ENODEV;
#else
	return 0;
#endif /* CONFIG_DEBUG_FS */
}

int tegra_bl_args(char *options, phys_addr_t *tegra_bl_arg_size,
				phys_addr_t *tegra_bl_arg_start)
{
	char *p = options;

	*tegra_bl_arg_size = memparse(p, &p);

	if (!p)
		return -EINVAL;
	if (*p != '@')
		return -EINVAL;

	*tegra_bl_arg_start = memparse(p + 1, &p);

	if (!(*tegra_bl_arg_size) || !(*tegra_bl_arg_start)) {
		*tegra_bl_arg_size = 0;
		*tegra_bl_arg_start = 0;
		return 0;
	}

	return 0;
}

int tegra_bl_parse_command_line_debug_gr_args(struct platform_device *pdev)
{
	int err;

	dev_info(&pdev->dev, "%s: bl_debug_data=%s boot_cfg_dataptr=%s\n", __func__,
		bl_debug_data, boot_cfg_dataptr);

	err = tegra_bl_args(bl_debug_data,
			&tegra_bl_debug_data_size,
			&tegra_bl_debug_data_start);

	if (err != 0) {
		dev_err(&pdev->dev, "%s: tegra_bl_args failed for bl_debug_data: %d\n",
				__func__, err);
		return err;
	}

	err = tegra_bl_args(boot_cfg_dataptr,
			&tegra_bl_bcp_size,
			&tegra_bl_bcp_start);

	if (err != 0) {
		dev_err(&pdev->dev, "%s: tegra_bl_args failed for boot_cfg_dataptr: %d\n",
				__func__, err);
		return err;
	}

	return err;
}

void __exit tegra_bl_debug_gr_init_module_exit(void)
{
#ifdef CONFIG_DEBUG_FS
	if (!IS_ERR_OR_NULL(bl_debug_node))
		debugfs_remove_recursive(bl_debug_node);

	if (tegra_bl_mapped_debug_data_start)
		iounmap((void __iomem *)tegra_bl_mapped_debug_data_start);

	if (tegra_bl_mapped_boot_cfg_start)
		iounmap((void __iomem *)tegra_bl_mapped_boot_cfg_start);
#endif
}

module_param(bl_debug_data, charp, 0400);
module_param(boot_cfg_dataptr, charp, 0400);
