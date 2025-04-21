// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2023, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/nvmem-consumer.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define TEGRA_EFUSE_ODM_0_OFFSET	0x408
#define TEGRA_EFUSE_ODM_1_OFFSET	0x40c
#define TEGRA_EFUSE_ODM_INFO_OFFSET	0x29c

struct tegra_efuse_soc {
	int				size;
	const struct nvmem_cell_lookup	*lookups;
	unsigned int			num_lookups;
	const struct nvmem_cell_info	*cells;
	unsigned int			num_cells;
	const struct nvmem_keepout	*keepouts;
	unsigned int			num_keepouts;
};

struct tegra_efuse {
	void __iomem	*base;

	struct nvmem_device		*nvmem;
	const struct tegra_efuse_soc	*soc;
	/* sysfs attribute for odm-id_0 */
	struct device_attribute          efuse_attr_odm_id_0;
	/* sysfs attribute for odm-id_1 */
	struct device_attribute          efuse_attr_odm_id_1;
	/* sysfs attribute for odm info */
	struct device_attribute          efuse_attr_odm_info;
};

static int tegra_efuse_readl(struct tegra_efuse *efuse, unsigned int offset)
{
	return readl_relaxed(efuse->base + offset);
}

static int tegra_efuse_read(void *priv, unsigned int offset, void *value, size_t bytes)
{
	unsigned int count = bytes / 4, i;
	struct tegra_efuse *efuse = priv;
	u32 *buffer = value;

	for (i = 0; i < count; i++)
		buffer[i] = tegra_efuse_readl(efuse, offset + i * 4);

	return 0;
}
static ssize_t tegra_efuse_odm_id_0_data_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct tegra_efuse *efuse = dev_get_drvdata(dev);
	int i, count = 0;
	u32 val;

	val = tegra_efuse_readl(efuse, TEGRA_EFUSE_ODM_0_OFFSET);
	count = scnprintf(buf, PAGE_SIZE, "0x%08x\n", val);

	return count;
}

static ssize_t tegra_efuse_odm_id_1_data_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct tegra_efuse *efuse = dev_get_drvdata(dev);
	int i, count = 0;
	u32 val;

	val = tegra_efuse_readl(efuse, TEGRA_EFUSE_ODM_1_OFFSET);
	count = scnprintf(buf, PAGE_SIZE, "0x%08x\n", val);

	return count;
}

static ssize_t tegra_efuse_odm_info_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct tegra_efuse *efuse = dev_get_drvdata(dev);
	int i, count = 0;
	u32 val;

	val = tegra_efuse_readl(efuse, TEGRA_EFUSE_ODM_INFO_OFFSET);
	count = scnprintf(buf, PAGE_SIZE, "0x%08x\n", val);

	return count;
}

static int tegra_efuse_probe(struct platform_device *pdev)
{
	struct tegra_efuse *efuse;
	struct nvmem_config nvmem;
	struct resource *res;
	int id;
	int err;

	id = of_alias_get_id(pdev->dev.of_node, "efuse");
	if (id < 0)
		return dev_err_probe(&pdev->dev, id, "failed to get alias id\n");

	efuse = devm_kzalloc(&pdev->dev, sizeof(*efuse), GFP_KERNEL);
	if (!efuse)
		return -ENOMEM;

	efuse->soc = device_get_match_data(&pdev->dev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	efuse->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(efuse->base))
		return PTR_ERR(efuse->base);

	platform_set_drvdata(pdev, efuse);

	memset(&nvmem, 0, sizeof(nvmem));
	nvmem.name = "efuse";
	nvmem.dev = &pdev->dev;
	nvmem.id = id;
	nvmem.owner = THIS_MODULE;
	nvmem.cells = efuse->soc->cells;
	nvmem.ncells = efuse->soc->num_cells;
	nvmem.keepout = efuse->soc->keepouts;
	nvmem.nkeepout = efuse->soc->num_keepouts;
	nvmem.type = NVMEM_TYPE_OTP;
	nvmem.read_only = true;
	nvmem.root_only = false;
	nvmem.reg_read = tegra_efuse_read;
	nvmem.size = efuse->soc->size;
	nvmem.word_size = 4;
	nvmem.stride = 4;
	nvmem.priv = efuse;

	efuse->nvmem = devm_nvmem_register(&pdev->dev, &nvmem);
	if (IS_ERR(efuse->nvmem))
		return dev_err_probe(&pdev->dev, PTR_ERR(efuse->nvmem),
				     "failed to register NVMEM device\n");
	/* Create sysfs odm-id-0 attribute */
	sysfs_attr_init(&efuse->efuse_attr_odm_id_0.attr);
	efuse->efuse_attr_odm_id_0.attr.name = "efuse_odm_id_0";
	efuse->efuse_attr_odm_id_0.attr.mode = 0444;  /* read-only */
	efuse->efuse_attr_odm_id_0.show = tegra_efuse_odm_id_0_data_show;
	efuse->efuse_attr_odm_id_0.store = NULL;  /* read-only attribute */

	err = device_create_file(&pdev->dev, &efuse->efuse_attr_odm_id_0);
	if (err)
		return dev_err_probe(&pdev->dev, err,
				    "failed to create odm_id_0 sysfs attribute\n");

	/* Create sysfs odm-id-1 attribute */
	sysfs_attr_init(&efuse->efuse_attr_odm_id_1.attr);
	efuse->efuse_attr_odm_id_1.attr.name = "efuse_odm_id_1";
	efuse->efuse_attr_odm_id_1.attr.mode = 0444;  /* read-only */
	efuse->efuse_attr_odm_id_1.show = tegra_efuse_odm_id_1_data_show;
	efuse->efuse_attr_odm_id_1.store = NULL;  /* read-only attribute */

	err = device_create_file(&pdev->dev, &efuse->efuse_attr_odm_id_1);
	if (err)
		return dev_err_probe(&pdev->dev, err,
				    "failed to create odm_id_1 sysfs attribute\n");

	/* Create sysfs odm-data attribute */
	sysfs_attr_init(&efuse->efuse_attr_odm_info.attr);
	efuse->efuse_attr_odm_info.attr.name = "efuse_odm_info";
	efuse->efuse_attr_odm_info.attr.mode = 0444;  /* read-only */
	efuse->efuse_attr_odm_info.show = tegra_efuse_odm_info_show;
	efuse->efuse_attr_odm_info.store = NULL;  /* read-only attribute */

	err = device_create_file(&pdev->dev, &efuse->efuse_attr_odm_info);
	if (err)
		return dev_err_probe(&pdev->dev, err,
				    "failed to create odm_info sysfs attribute\n");
	return 0;
}

static int tegra_efuse_remove(struct platform_device *pdev)
{
	struct tegra_efuse *efuse = platform_get_drvdata(pdev);

	device_remove_file(&pdev->dev, &efuse->efuse_attr_odm_id_0);
	device_remove_file(&pdev->dev, &efuse->efuse_attr_odm_id_1);
	device_remove_file(&pdev->dev, &efuse->efuse_attr_odm_info);
	return 0;
}

static const struct nvmem_cell_info tegra264_efuse_cells[] = {
	{
		.name = "tsensor-cpu1",
		.offset = 0x084,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	},
};

static const struct nvmem_cell_lookup tegra264_efuse_lookups[] = {
	/* Sample Node */
	{
		.nvmem_name = "efuse0",
		.cell_name = "tensor-cpu1",
		.dev_id = "700e2000.thermal-sensor",
		.con_id = "cpu1",
	},
};

static const struct nvmem_keepout tegra264_efuse_keepouts[] = {
	{ .start = 0x00038, .end = 0x00050 },
	{ .start = 0x00054, .end = 0x0005c },
	{ .start = 0x00060, .end = 0x00064 },
	{ .start = 0x00080, .end = 0x00088 },
	{ .start = 0x000a4, .end = 0x00100 },
	{ .start = 0x0029c, .end = 0x002a0 },
	{ .start = 0x00408, .end = 0x00410 },
	{ .start = 0x0042c, .end = 0x00434 },
	{ .start = 0x00450, .end = 0x00454 },
	{ .start = 0x00594, .end = 0x0059c },
	{ .start = 0x0088c, .end = 0x00890 },
	{ .start = 0x008a0, .end = 0x008a8 },
	{ .start = 0x008dc, .end = 0x008e4 },
	{ .start = 0x009d8, .end = 0x009dc },
	{ .start = 0x00a6c, .end = 0x00a70 },
	{ .start = 0x00a74, .end = 0x00a7c },
	{ .start = 0x00af4, .end = 0x00af8 },
	{ .start = 0x00b14, .end = 0x00b20 },
	{ .start = 0x00b44, .end = 0x00b4c },
	{ .start = 0x00b50, .end = 0x00b58 },
	{ .start = 0x00b5c, .end = 0x00b64 },
	{ .start = 0x00b68, .end = 0x00b70 },
	{ .start = 0x00bcc, .end = 0x00bd0 },
	{ .start = 0x00c0c, .end = 0x00c18 },
	{ .start = 0x00d80, .end = 0x00d8c },
	{ .start = 0x00eac, .end = 0x00eb4 },
	{ .start = 0x00eb8, .end = 0x00ebc },
	{ .start = 0x00f0c, .end = 0x00f10 },
	{ .start = 0x010d0, .end = 0x02000 },
	{ .start = 0x0201c, .end = 0x10164 },
	{ .start = 0x10184, .end = 0x101a0 },
	{ .start = 0x101a4, .end = 0x1029c },
	{ .start = 0x102a0, .end = 0x102cc },
	{ .start = 0x102d0, .end = 0x10408 },
	{ .start = 0x10410, .end = 0x1065c },
	{ .start = 0x1067c, .end = 0x107b0 },
	{ .start = 0x107b4, .end = 0x11108 },
	{ .start = 0x1110c, .end = 0x11118 },
	{ .start = 0x11120, .end = 0x111b8 },
	{ .start = 0x111c4, .end = 0x111c8 },
	{ .start = 0x111e8, .end = 0x111ec },
	{ .start = 0x111f0, .end = 0x11224 },
	{ .start = 0x11228, .end = 0x11268 },
	{ .start = 0x1126c, .end = 0x112b8 },
	{ .start = 0x112bc, .end = 0x112e4 },
	{ .start = 0x112e8, .end = 0x112ec },
	{ .start = 0x112f0, .end = 0x1131c },
	{ .start = 0x11350, .end = 0x1143c },
	{ .start = 0x11440, .end = 0x114c8 },
	{ .start = 0x114d0, .end = 0x11520 },
	{ .start = 0x11530, .end = 0x11540 },
	{ .start = 0x11544, .end = 0x11568 },
	{ .start = 0x1156c, .end = 0x115a8 },
	{ .start = 0x115ac, .end = 0x116cc },
	{ .start = 0x116d0, .end = 0x116dc },
	{ .start = 0x116e0, .end = 0x117b4 },
	{ .start = 0x117bc, .end = 0x11840 },
	{ .start = 0x11844, .end = 0x118b8 },
	{ .start = 0x118bc, .end = 0x11910 },
	{ .start = 0x11914, .end = 0x11950 },
	{ .start = 0x11958, .end = 0x119a0 },
	{ .start = 0x119ac, .end = 0x11c48 },
	{ .start = 0x11c50, .end = 0x11c5c },
	{ .start = 0x11c60, .end = 0x11cf8 },
	{ .start = 0x11cfc, .end = 0x11d18 },
	{ .start = 0x11d40, .end = 0x12100 },
	{ .start = 0x12104, .end = 0x12110 },
	{ .start = 0x12114, .end = 0x121fc },
	{ .start = 0x1221c, .end = 0x12220 },
	{ .start = 0x12224, .end = 0x12244 },
	{ .start = 0x1224c, .end = 0x12400 },
	{ .start = 0x12408, .end = 0x1246c },
	{ .start = 0x12470, .end = 0x127ac },
	{ .start = 0x127b0, .end = 0x12d60 },
	{ .start = 0x12d64, .end = 0x12e94 },
	{ .start = 0x12ea4, .end = 0x13104 },
	{ .start = 0x13108, .end = 0x1310c },
	{ .start = 0x13110, .end = 0x13114 },
	{ .start = 0x13118, .end = 0x13120 },
	{ .start = 0x13164, .end = 0x13184 },
	{ .start = 0x131a0, .end = 0x131a4 },
	{ .start = 0x131b8, .end = 0x131c4 },
	{ .start = 0x131c8, .end = 0x131e8 },
	{ .start = 0x131ec, .end = 0x131f0 },
	{ .start = 0x131fc, .end = 0x1321c },
	{ .start = 0x13220, .end = 0x13228 },
	{ .start = 0x13244, .end = 0x1324c },
	{ .start = 0x13268, .end = 0x1326c },
	{ .start = 0x1329c, .end = 0x132a0 },
	{ .start = 0x132ac, .end = 0x132c0 },
	{ .start = 0x132cc, .end = 0x132d0 },
	{ .start = 0x132e4, .end = 0x132e8 },
	{ .start = 0x132ec, .end = 0x132f0 },
	{ .start = 0x1331c, .end = 0x13350 },
	{ .start = 0x133f0, .end = 0x13410 },
	{ .start = 0x1342c, .end = 0x13434 },
	{ .start = 0x1343c, .end = 0x13444 },
	{ .start = 0x13450, .end = 0x13454 },
	{ .start = 0x1346c, .end = 0x13470 },
	{ .start = 0x134ac, .end = 0x134b0 },
	{ .start = 0x134c8, .end = 0x134d0 },
	{ .start = 0x13520, .end = 0x13530 },
	{ .start = 0x13540, .end = 0x13544 },
	{ .start = 0x13568, .end = 0x13570 },
	{ .start = 0x13594, .end = 0x1359c },
	{ .start = 0x135a8, .end = 0x135ac },
	{ .start = 0x13644, .end = 0x1364c },
	{ .start = 0x1365c, .end = 0x1367c },
	{ .start = 0x136cc, .end = 0x136e0 },
	{ .start = 0x136e4, .end = 0x13708 },
	{ .start = 0x1370c, .end = 0x13720 },
	{ .start = 0x137ac, .end = 0x137bc },
	{ .start = 0x137c0, .end = 0x137c8 },
	{ .start = 0x13814, .end = 0x13818 },
	{ .start = 0x13824, .end = 0x13828 },
	{ .start = 0x1382c, .end = 0x13830 },
	{ .start = 0x13834, .end = 0x1383c },
	{ .start = 0x13840, .end = 0x13844 },
	{ .start = 0x13854, .end = 0x13858 },
	{ .start = 0x13860, .end = 0x13884 },
	{ .start = 0x1388c, .end = 0x13890 },
	{ .start = 0x138a0, .end = 0x138a8 },
	{ .start = 0x138b8, .end = 0x138bc },
	{ .start = 0x138dc, .end = 0x138e4 },
	{ .start = 0x138ec, .end = 0x138f0 },
	{ .start = 0x13900, .end = 0x13918 },
	{ .start = 0x13920, .end = 0x13928 },
	{ .start = 0x13930, .end = 0x13958 },
	{ .start = 0x139a0, .end = 0x139b0 },
	{ .start = 0x139d0, .end = 0x139dc },
	{ .start = 0x13a6c, .end = 0x13a70 },
	{ .start = 0x13a74, .end = 0x13a7c },
	{ .start = 0x13af4, .end = 0x13af8 },
	{ .start = 0x13b14, .end = 0x13b20 },
	{ .start = 0x13b44, .end = 0x13b4c },
	{ .start = 0x13b50, .end = 0x13b58 },
	{ .start = 0x13b5c, .end = 0x13b64 },
	{ .start = 0x13b68, .end = 0x13b70 },
	{ .start = 0x13bcc, .end = 0x13bd8 },
	{ .start = 0x13c0c, .end = 0x13c18 },
	{ .start = 0x13c48, .end = 0x13c50 },
	{ .start = 0x13c5c, .end = 0x13c60 },
	{ .start = 0x13cf8, .end = 0x13cfc },
	{ .start = 0x13d18, .end = 0x13d40 },
	{ .start = 0x13d60, .end = 0x13d64 },
	{ .start = 0x13d74, .end = 0x13d8c },
	{ .start = 0x13e94, .end = 0x13ea4 },
	{ .start = 0x13eac, .end = 0x13eb4 },
	{ .start = 0x13eb8, .end = 0x13ebc },
	{ .start = 0x13ef4, .end = 0x13f04 },
	{ .start = 0x13f0c, .end = 0x13f10 },
	{ .start = 0x13fb8, .end = 0x1470c },
	{ .start = 0x14720, .end = 0x147c0 },
	{ .start = 0x147c8, .end = 0x14824 },
	{ .start = 0x14828, .end = 0x1482c },
	{ .start = 0x14830, .end = 0x14834 },
	{ .start = 0x1483c, .end = 0x14870 },
	{ .start = 0x14874, .end = 0x148ec },
	{ .start = 0x148f0, .end = 0x14900 },
	{ .start = 0x14910, .end = 0x14914 },
	{ .start = 0x14918, .end = 0x149ac },
	{ .start = 0x149b0, .end = 0x14bd0 },
	{ .start = 0x14bd8, .end = 0x152bc },
	{ .start = 0x152c0, .end = 0x156d4 },
	{ .start = 0x156d8, .end = 0x15814 },
	{ .start = 0x15818, .end = 0x15860 },
	{ .start = 0x15870, .end = 0x15920 },
	{ .start = 0x15924, .end = 0x15930 },
	{ .start = 0x15940, .end = 0x159d0 },
	{ .start = 0x159d8, .end = 0x15d74 },
	{ .start = 0x15d80, .end = 0x15ef4 },
	{ .start = 0x15efc, .end = 0x162ac },
	{ .start = 0x162b8, .end = 0x166d0 },
	{ .start = 0x166d4, .end = 0x166d8 },
	{ .start = 0x166dc, .end = 0x166e4 },
	{ .start = 0x166ec, .end = 0x16854 },
	{ .start = 0x16858, .end = 0x16874 },
	{ .start = 0x16884, .end = 0x16924 },
	{ .start = 0x16928, .end = 0x16940 },
	{ .start = 0x16950, .end = 0x16efc },
	{ .start = 0x16f04, .end = 0x17000 },
	{ .start = 0x17008, .end = 0x1f000 },
	{ .start = 0x1f010, .end = 0x1f014 },
	{ .start = 0x1f024, .end = 0x1f030 },
	{ .start = 0x1f04c, .end = 0x1f050 },
};

static const struct tegra_efuse_soc tegra264_efuse_soc = {
	.cells = tegra264_efuse_cells,
	.num_cells = ARRAY_SIZE(tegra264_efuse_cells),
	.lookups = tegra264_efuse_lookups,
	.num_lookups = ARRAY_SIZE(tegra264_efuse_lookups),
	.keepouts = tegra264_efuse_keepouts,
	.num_keepouts = ARRAY_SIZE(tegra264_efuse_keepouts),
	.size = 0x1f094,
};

static const struct of_device_id tegra_efuse_of_match[] = {
	{ .compatible = "nvidia,tegra264-efuse", .data = &tegra264_efuse_soc },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, tegra_efuse_of_match);

static struct platform_driver tegra_efuse_driver = {
	.driver = {
		.name = "tegra-efuse",
		.of_match_table = tegra_efuse_of_match,
	},
	.probe = tegra_efuse_probe,
	.remove = tegra_efuse_remove,
};
module_platform_driver(tegra_efuse_driver);

MODULE_LICENSE("GPL v2");
