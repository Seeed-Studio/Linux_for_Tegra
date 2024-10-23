// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#define pr_fmt(fmt) "tegra264-mem-qual: " fmt
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/export.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>

/* Number of mem qual devices */
#define MAX_QUAL_DEV 5

/* structures needed for ioctl call from userspace */
struct user_size_address {
	size_t buffer_size;
	unsigned long long iova_address;
};

#define MEMQUAL_IOC_MAGIC 'M'
#define MEMQUAL_IOC_CREATE _IOWR(MEMQUAL_IOC_MAGIC, 0, struct user_size_address)
#define MEMQUAL_IOC_FREE _IOWR(MEMQUAL_IOC_MAGIC, 1, unsigned long long)
#define MEMQUAL_IOC_MAXNR (_IOC_NR(MEMQUAL_IOC_FREE))

#define DEFINE_DMA_ATTRS(attrs) unsigned long attrs = 0
#define __DMA_ATTR(attrs) attrs
#define dma_set_attr(attr, attrs) (attrs |= attr)

struct class *class;
dev_t dev_num;
int device_index;

struct memqual_devdata {
	struct cdev cdev;
	struct device *dev_qual;
	struct platform_device *pdev;
	size_t buffer_size;
	dma_addr_t iova_address;
	dev_t dev_nr;
	struct page **pages;
	struct sg_table *sgt;
};

static int memqual_open(struct inode *inode, struct file *filp)
{
	struct memqual_devdata *data;

	data = container_of(inode->i_cdev, struct memqual_devdata, cdev);
	filp->private_data = data;
	return 0;
}

static int memqual_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static int memqual_iova_free(struct file *filp, void __user *arg)
{
	struct memqual_devdata *qual_data = filp->private_data;
	unsigned long long dma_adr;
	int ret = 0, i;
	int num_pages;
	DEFINE_DMA_ATTRS(attrs);

	if (copy_from_user(&dma_adr, arg, sizeof(dma_adr))) {
		pr_err("copy_from_user failed\n");
		return -EFAULT;
	}

	if (qual_data->iova_address != dma_adr) {
		pr_err("incorrect iova addrees given:%llu\n", dma_adr);
		ret = -EINVAL;
		goto exit;
	}

	dma_set_attr(DMA_ATTR_SKIP_CPU_SYNC, __DMA_ATTR(attrs));
	dma_unmap_sg_attrs(qual_data->dev_qual, qual_data->sgt->sgl, qual_data->sgt->nents,
			DMA_FROM_DEVICE, __DMA_ATTR(attrs));
	sg_free_table(qual_data->sgt);
	kfree(qual_data->sgt);
	num_pages = PAGE_ALIGN(qual_data->buffer_size) >> PAGE_SHIFT;

	for (i = 0; i < num_pages; i++)
		__free_pages(qual_data->pages[i], 0);
	vfree(qual_data->pages);

exit:
	return ret;
}

static int memqual_iova_generate(struct file *filp, void __user *arg)
{
	struct memqual_devdata *qual_data = filp->private_data;
	int ret = 0;
	struct user_size_address op;
	int i, num_pages, ents;
	struct page **pages;
	struct sg_table *sgt;
	DEFINE_DMA_ATTRS(attrs);

	if (copy_from_user(&op, arg, sizeof(op))) {
		pr_err("copy_from_user failed\n");
		return -EFAULT;
	}

	qual_data->buffer_size = op.buffer_size;
	dma_set_attr(DMA_ATTR_SKIP_CPU_SYNC, __DMA_ATTR(attrs));
	num_pages = PAGE_ALIGN(qual_data->buffer_size) >> PAGE_SHIFT;

	pages = vzalloc(sizeof(*pages) * num_pages);
	if (!pages) {
		pr_err("Failed to allocate pages\n");
		ret = -ENOMEM;
		goto fail;
	}

	for (i = 0; i < num_pages; i++) {
		pages[i] = alloc_pages(GFP_KERNEL | __GFP_ZERO, 0);
		if (!pages[i]) {
			pr_err("Failed to allocate page\n");
			i--;
			for (; i >= 0; i--)
				__free_pages(pages[i], 0);
			ret = -ENOMEM;
			goto free_pages_arr;
		}
	}

	sgt = kzalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!sgt) {
		ret = -ENOMEM;
		goto fail_sgt;
	}

	ret = sg_alloc_table_from_pages(sgt, pages, num_pages, 0,
			qual_data->buffer_size, GFP_KERNEL);
	if (ret) {
		pr_err("Failed to allocate sg table from pages\n");
		ret = -ENOMEM;
		goto free_sgt;
	}

	ents = dma_map_sg_attrs(qual_data->dev_qual, sgt->sgl, sgt->nents,
			DMA_FROM_DEVICE, __DMA_ATTR(attrs));
	if (ents <= 0) {
		pr_err("Failed to map sg attrs\n");
		ret = -ENOMEM;
		goto free_table;
	}

	qual_data->iova_address = sg_dma_address(sgt->sgl);
	op.iova_address = qual_data->iova_address;
	qual_data->sgt = sgt;
	qual_data->pages = pages;
	return copy_to_user(arg, &op, sizeof(op)) ? -EFAULT : 0;

free_table:
	sg_free_table(sgt);
free_sgt:
	kfree(sgt);
fail_sgt:
	for (i = 0; i < num_pages; i++)
		__free_pages(pages[i], 0);
free_pages_arr:
	vfree(pages);
fail:
	return ret;
}

static long memqual_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	void __user *uarg = (void __user *)arg;

	if (_IOC_TYPE(cmd) != MEMQUAL_IOC_MAGIC) {
		pr_err("Incorrect ioctl magic number\n");
		ret = -ENOTTY;
		goto exit;
	}

	if (_IOC_NR(cmd) > MEMQUAL_IOC_MAXNR) {
		pr_err("Incorrect ioctl number\n");
		ret = -ENOTTY;
		goto exit;
	}

	switch (cmd) {
		case MEMQUAL_IOC_CREATE:
			ret = memqual_iova_generate(filp, uarg);
			break;

		case MEMQUAL_IOC_FREE:
			ret = memqual_iova_free(filp, uarg);
			break;

		default:
			ret = -EINVAL;
			pr_err("Incorrect ioctl\n");
			break;
	}
exit:
	return ret;
}

/* file operations of the driver */
static const struct file_operations qual_fops=
{
	.open = memqual_open,
	.release = memqual_release,
	.owner = THIS_MODULE,
	.unlocked_ioctl = memqual_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = memqual_ioctl,
#endif
};

static const struct of_device_id tegra_mqual_of_ids[] = {
	{ .compatible = "nvidia,tegra-t264-mem-qual"},
	{}
};
MODULE_DEVICE_TABLE(of, tegra_mqual_of_ids);
static int tegra_mem_qual_probe(struct platform_device *pdev)
{
	struct memqual_devdata *qual_data = NULL;
	int ret = 0;
	struct device *tmp;

	qual_data = devm_kzalloc(&pdev->dev, sizeof(struct memqual_devdata), GFP_KERNEL);
	if (!qual_data) {
		ret = -ENOMEM;
		goto exit;
	}

	qual_data->dev_qual = &pdev->dev;
	platform_set_drvdata(pdev, qual_data);
	cdev_init(&qual_data->cdev, &qual_fops);
	qual_data->cdev.owner = THIS_MODULE;
	qual_data->dev_nr = dev_num + device_index;
	ret = cdev_add(&qual_data->cdev, dev_num + device_index, 1);
	if (ret < 0) {
		pr_err("cdev add failed\n");
		goto exit;
	}

	tmp = device_create(class, &pdev->dev, qual_data->dev_nr, NULL, "qualdev-%d", device_index);
	if (IS_ERR(tmp)) {
		ret = PTR_ERR(tmp);
		pr_alert("failed to create device\n");
		goto fail_create;
	}

	device_index++;
	goto exit;

fail_create:
	cdev_del(&qual_data->cdev);
exit:
	return ret;
}

static void tegra_mem_qual_remove(struct platform_device *pdev)
{
	struct memqual_devdata *qual_data = platform_get_drvdata(pdev);

	device_destroy(class, qual_data->dev_nr);
	cdev_del(&qual_data->cdev);
}

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void tegra_mem_qual_remove_wrapper(struct platform_device *pdev)
{
	tegra_mem_qual_remove(pdev);
}
#else
static int tegra_mem_qual_remove_wrapper(struct platform_device *pdev)
{
	tegra_mem_qual_remove(pdev);
	return 0;
}
#endif

static struct platform_driver mqual_driver = {
	.driver = {
		.name = "tegra-mem-qual",
		.of_match_table = tegra_mqual_of_ids,
		.owner	= THIS_MODULE,
	},
	.probe = tegra_mem_qual_probe,
	.remove = tegra_mem_qual_remove_wrapper,
};

static int __init tegra_mem_qual_init(void)
{
	int ret;

	dev_num = 0;
	device_index = 0;

#if defined(NV_CLASS_CREATE_HAS_NO_OWNER_ARG) /* Linux v6.4 */
	class = class_create("qual_class");
#else
	class = class_create(THIS_MODULE, "qual_class");
#endif
	if(IS_ERR(class)){
		pr_err("Failed to create class\n");
		ret = PTR_ERR(class);
		return ret;
	}

	ret = alloc_chrdev_region(&dev_num, 0, MAX_QUAL_DEV, "qualdevs");
	if (ret) {
		pr_err("Failed to reserve chrdev region\n");
		goto destroy_class;
	}

	ret = platform_driver_register(&mqual_driver);
	if (ret)
		pr_err("Failed to register driver\n");
	else
		goto exit;

	unregister_chrdev_region(dev_num, MAX_QUAL_DEV);
destroy_class:
	class_destroy(class);
exit:
	return ret;
}

static void __exit tegra_mem_qual_exit(void)
{
	platform_driver_unregister(&mqual_driver);
	unregister_chrdev_region(dev_num, MAX_QUAL_DEV);
	class_destroy(class);
}

module_init(tegra_mem_qual_init);
module_exit(tegra_mem_qual_exit);

MODULE_DESCRIPTION("Mem Qual IOVA mapping provider");
MODULE_AUTHOR("Ketan Patil <ketanp@nvidia.com>");
MODULE_LICENSE("GPL v2");
