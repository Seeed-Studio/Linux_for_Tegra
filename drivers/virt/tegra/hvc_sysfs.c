// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <soc/tegra/fuse.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/mm.h>
#include <linux/bug.h>
#include <linux/io.h>
#include <linux/overflow.h>
#include <soc/tegra/virt/syscalls.h>
#include <soc/tegra/virt/hv-ivc.h>
#define TEGRA_HV_ERR(...) pr_err("hvc_sysfs: " __VA_ARGS__)
#define TEGRA_HV_INFO(...) pr_info("hvc_sysfs: " __VA_ARGS__)


/*
 * This file implements a hypervisor control driver that can be accessed
 * from user-space via the sysfs interface. Currently, supported use case are
 * retrieval of the HV trace log when it is available and mapping nvlog buffers
 * to user space.
 */

#define MAX_NAME_SIZE 50

struct nvlog_shmem_info {
	struct bin_attribute attr;
	struct bin_attribute region_size_attr;
	struct bin_attribute buf_size_attr;
	struct bin_attribute buf_count_attr;
	struct kobject *kobj;
	char node_name[MAX_NAME_SIZE];
	uint64_t ipa;
	uint64_t region_size;
	uint64_t buf_size;
	uint64_t buf_count;
};
static struct nvlog_shmem_info nvlog_shmem_attrs[MAX_NVLOG_PRODUCERS];

struct hyp_shared_memory_info {
	char node_name[MAX_NAME_SIZE];
	struct bin_attribute attr;
	uint64_t ipa;
	unsigned long size;
};

#define HYP_SHM_ID_NUM  (2 * NGUESTS_MAX + 1U) // trace buffers + PCT.
static struct hyp_shared_memory_info hyp_shared_memory_attrs[HYP_SHM_ID_NUM];
static uint64_t EventType = 1;

/* Map the HV trace buffer to the calling user process */
static int nvlog_buffer_mmap(struct file *fp, struct kobject *ko,
#if defined(NV_BIN_ATTRIBUTE_STRUCT_MMAP_HAS_CONST_BIN_ATTRIBUTE_ARG) /* Linux v6.13 */
			     const struct bin_attribute *attr,
#else
			     struct bin_attribute *attr,
#endif
			     struct vm_area_struct *vma)
{
	struct nvlog_shmem_info *info = container_of(attr, struct nvlog_shmem_info, attr);
	unsigned long result;

	if ((info->ipa == 0) || (info->region_size == 0))
		return -EINVAL;


	if (check_sub_overflow(vma->vm_end, vma->vm_start, &result)) {
		pr_err("%s: operation got overflown.\n", __func__);
		return -EINVAL;
	}

	if (result != attr->size)
		return -EINVAL;

	if (vma->vm_flags & VM_EXEC) {
		pr_err("%s: nvlog buffers can't have executable permission.\n", __func__);
		return -EINVAL;
	}

	return remap_pfn_range(
		vma, vma->vm_start,
		info->ipa >> PAGE_SHIFT,
		info->region_size, vma->vm_page_prot);
}

static ssize_t nvlog_region_size_read(struct file *fp, struct kobject *ko,
	struct bin_attribute *attr, char *buf, loff_t pos, size_t size)
{
	struct nvlog_shmem_info *info = container_of(attr, struct nvlog_shmem_info, region_size_attr);

	snprintf(buf, size, "%llu\n", info->region_size);
	return size;
}

static ssize_t nvlog_buffer_size_read(struct file *fp, struct kobject *ko,
	struct bin_attribute *attr, char *buf, loff_t pos, size_t size)
{
	struct nvlog_shmem_info *info = container_of(attr, struct nvlog_shmem_info, buf_size_attr);

	snprintf(buf, size, "%llu\n", info->buf_size);
	return size;
}

static ssize_t nvlog_buffer_count_read(struct file *fp, struct kobject *ko,
	struct bin_attribute *attr, char *buf, loff_t pos, size_t size)
{
	struct nvlog_shmem_info *info = container_of(attr, struct nvlog_shmem_info, buf_count_attr);

	snprintf(buf, size, "%llu\n", info->buf_count);
	return size;
}

static int nvlog_create_sysfs_nodes(struct nvlog_shmem_info *info)
{
	int ret = 0;

	if (info == NULL)
		return -EINVAL;

	sysfs_bin_attr_init((struct bin_attribute *)&info->attr);
	info->attr.attr.name = info->node_name;
	info->attr.attr.mode = 0400;
	info->attr.size = info->region_size;
	info->attr.mmap = nvlog_buffer_mmap;
	ret = sysfs_create_bin_file(info->kobj, &info->attr);
	if (ret != 0)
		goto fail;

	sysfs_bin_attr_init((struct bin_attribute *)&info->region_size_attr);
	info->region_size_attr.attr.name = "region_size";
	info->region_size_attr.attr.mode = 0444;
	info->region_size_attr.size = 16u;
	info->region_size_attr.read = nvlog_region_size_read;
	ret = sysfs_create_bin_file(info->kobj, &info->region_size_attr);
	if (ret != 0)
		goto del_attr;

	sysfs_bin_attr_init((struct bin_attribute *)&info->buf_size_attr);
	info->buf_size_attr.attr.name = "buf_size";
	info->buf_size_attr.attr.mode = 0444;
	info->buf_size_attr.size = 16u;
	info->buf_size_attr.read = nvlog_buffer_size_read;
	ret = sysfs_create_bin_file(info->kobj, &info->buf_size_attr);
	if (ret != 0)
		goto del_region_size;

	sysfs_bin_attr_init((struct bin_attribute *)&info->buf_count_attr);
	info->buf_count_attr.attr.name = "buf_count";
	info->buf_count_attr.attr.mode = 0444;
	info->buf_count_attr.size = 16u;
	info->buf_count_attr.read = nvlog_buffer_count_read;
	ret = sysfs_create_bin_file(info->kobj, &info->buf_count_attr);
	if (ret != 0)
		goto del_buf_size;

	return 0;

del_buf_size:
	sysfs_remove_bin_file(info->kobj, &info->buf_size_attr);
del_region_size:
	sysfs_remove_bin_file(info->kobj, &info->region_size_attr);
del_attr:
	sysfs_remove_bin_file(info->kobj, &info->attr);
fail:
	return ret;
}

static int hyp_nvlog_buffer_init(void)
{
	struct vm_info_region *info;
	struct kobject *parent;
	uint64_t ipa, cntr;
	char dir_name[MAX_NAME_SIZE];
	int ret;

	parent = kobject_create_and_add("nvlog", NULL);
	if (parent == NULL) {
		TEGRA_HV_INFO("failed to add kobject\n");
		return -ENOMEM;
	}

	if (hyp_read_vm_info(&ipa) != 0) {
		ret = -EINVAL;
		goto fail;
	}

	info = (__force struct vm_info_region *)ioremap(ipa, sizeof(*info));
	if (info == NULL) {
		ret = -EFAULT;
		goto fail;
	}

	for (cntr = 0; cntr < MAX_NVLOG_PRODUCERS; cntr++) {
		if (info->nvlog_producers[cntr].ipa != 0U
					&& info->nvlog_producers[cntr].region_size != 0U) {
			nvlog_shmem_attrs[cntr].ipa = info->nvlog_producers[cntr].ipa;
			nvlog_shmem_attrs[cntr].region_size = info->nvlog_producers[cntr].region_size;
			nvlog_shmem_attrs[cntr].buf_size = info->nvlog_producers[cntr].buf_size;
			nvlog_shmem_attrs[cntr].buf_count = info->nvlog_producers[cntr].buf_count;

			ret = snprintf(nvlog_shmem_attrs[cntr].node_name, MAX_NAME_SIZE, "%s",
								info->nvlog_producers[cntr].name);
			if (ret > 0U) {
				nvlog_shmem_attrs[cntr].node_name[ret] = '\0';
			} else {
				TEGRA_HV_INFO("snprintf failure - %s\n",
							info->nvlog_producers[cntr].name);
				iounmap((void __iomem *)info);
				ret = -EFAULT;
				goto fail;
			}

			strscpy(dir_name, nvlog_shmem_attrs[cntr].node_name, MAX_NAME_SIZE);
			strlcat(dir_name, "_dir", MAX_NAME_SIZE);
			nvlog_shmem_attrs[cntr].kobj = kobject_create_and_add(dir_name, parent);
			if (nvlog_shmem_attrs[cntr].kobj == NULL) {
				TEGRA_HV_INFO("failed to add kobject\n");
				ret = -ENOMEM;
				goto fail;
			}

			ret = nvlog_create_sysfs_nodes(&nvlog_shmem_attrs[cntr]);
			if (ret != 0)
				goto fail;
		}
	}

	iounmap((void __iomem *)info);
	return 0;

fail:
	for (cntr = 0; cntr < MAX_NVLOG_PRODUCERS; cntr++) {
		if (nvlog_shmem_attrs[cntr].kobj)
			kobject_del(nvlog_shmem_attrs[cntr].kobj);
	}

	if (parent)
		kobject_del(parent);

	return ret;
}

/* Map the HV trace buffer to the calling user process */
static int hvc_sysfs_mmap(struct file *fp, struct kobject *ko,
#if defined(NV_BIN_ATTRIBUTE_STRUCT_MMAP_HAS_CONST_BIN_ATTRIBUTE_ARG) /* Linux v6.13 */
			  const struct bin_attribute *attr,
#else
			  struct bin_attribute *attr,
#endif
			  struct vm_area_struct *vma)
{
	struct hyp_shared_memory_info *hyp_shm_info =
		container_of(attr, struct hyp_shared_memory_info, attr);
	unsigned long result;

	if ((hyp_shm_info->ipa == 0) || (hyp_shm_info->size == 0))
		return -EINVAL;


	if (check_sub_overflow(vma->vm_end, vma->vm_start, &result)) {
		pr_err("%s: operation got overflown.\n", __func__);
		return -EINVAL;
	}

	if (result != attr->size)
		return -EINVAL;

	return remap_pfn_range(
		vma,
		vma->vm_start,
		hyp_shm_info->ipa >> PAGE_SHIFT,
		hyp_shm_info->size,
		vma->vm_page_prot);
}

/* Discover availability and placement of the HV trace buffer */
static int hvc_create_sysfs(
	struct kobject *kobj,
	struct hyp_shared_memory_info *hyp_shm_info)
{
	sysfs_bin_attr_init((struct bin_attribute *)&hyp_shm_info->attr);

	hyp_shm_info->attr.attr.name = hyp_shm_info->node_name;
	hyp_shm_info->attr.attr.mode = 0400;
	hyp_shm_info->attr.mmap = hvc_sysfs_mmap;
	hyp_shm_info->attr.size = (size_t)hyp_shm_info->size;

	if ((hyp_shm_info->ipa == 0) || (hyp_shm_info->size == 0))
		return -EINVAL;

	return sysfs_create_bin_file(kobj, &hyp_shm_info->attr);
}

static ssize_t log_mask_read(struct file *fp, struct kobject *ko,
	struct bin_attribute *attr, char *buf, loff_t pos, size_t size)
{
	uint64_t value = 0;

	/* Kernel checks for validity of buf, no need to check here. */
	value = EventType;

	if (size == sizeof(uint64_t)) {
		hyp_trace_get_mask(&value);
		memcpy(buf, &value, sizeof(value));
	}

	return size;
}

static ssize_t log_mask_write(struct file *fp, struct kobject *ko,
	struct bin_attribute *attr, char *buf, loff_t pos, size_t size)
{
	uint64_t type, value;

	memcpy(&type, buf, sizeof(uint64_t));
	memcpy(&value, buf + sizeof(uint64_t), sizeof(uint64_t));

	if (size == 2 * sizeof(uint64_t))
		hyp_trace_set_mask(type, value);
	else
		EventType = *buf;

	return size;
}

static struct bin_attribute log_mask_attr;
static int create_log_mask_node(struct kobject *kobj)
{
	if (kobj == NULL)
		return -EINVAL;

	sysfs_bin_attr_init((struct bin_attribute *)&log_mask_attr);
	log_mask_attr.attr.name = "log_mask";
	log_mask_attr.attr.mode = 0600;
	log_mask_attr.read = log_mask_read;
	log_mask_attr.write = log_mask_write;
	log_mask_attr.size = sizeof(uint64_t) * 2U;
	return sysfs_create_bin_file(kobj, &log_mask_attr);
}

static int hyp_trace_buffer_init(void)
{
	struct kobject *kobj;
	int ret;
	uint64_t ipa;
	struct hyp_info_page *info;
	uint64_t log_mask = 1;
	struct trace_buf *buffs;
	uint16_t i, count;

	kobj = kobject_create_and_add("hvc", NULL);
	if (kobj == NULL) {
		TEGRA_HV_INFO("failed to add kobject\n");
		return -ENOMEM;
	}

	if (hyp_read_hyp_info(&ipa) != 0) {
		ret = -EINVAL;
		goto fail;
	}

	info = (__force struct hyp_info_page *)ioremap(ipa, sizeof(*info));
	if (info == NULL) {
		ret = -EFAULT;
		goto fail;
	}

	buffs = info->trace_buffs;
	count = ARRAY_SIZE(info->trace_buffs);

	/* 2 bytes for number, 1 for "_" and 1 for null character.*/
	if (MAX_NAME_SIZE < (ARRAY_SIZE(buffs[i].name) + 4U)) {
		TEGRA_HV_INFO("hyp_shared_memory_attrs.name size is small\n");
		iounmap((void __iomem *)info);
		ret = -EFAULT;
		goto fail;
	}

	for (i = 0; i < count; i++) {
		if (buffs[i].ipa != 0U && buffs[i].size != 0U) {
			hyp_shared_memory_attrs[i].ipa = buffs[i].ipa;
			hyp_shared_memory_attrs[i].size = (size_t)buffs[i].size;

			ret = snprintf(hyp_shared_memory_attrs[i].node_name, MAX_NAME_SIZE,
					"%s", buffs[i].name);

			if (ret > 0U) {
				hyp_shared_memory_attrs[i].node_name[ret] = '\0';
			} else {
				TEGRA_HV_INFO("snprintf failure - %s\n", buffs[i].name);
				iounmap((void __iomem *)info);
				ret = -EFAULT;
				goto fail;
			}

			ret = hvc_create_sysfs(kobj, &hyp_shared_memory_attrs[i]);
			if (ret == 0)
				TEGRA_HV_INFO(" %s trace is available\n", buffs[i].name);
			else
				TEGRA_HV_INFO(" %s trace is unavailable\n", buffs[i].name);
		}
	}

	if (hyp_trace_get_mask(&log_mask) == 0) {
		ret = create_log_mask_node(kobj);
		if (ret == 0)
			TEGRA_HV_INFO("access to trace event mask is available\n");
	}

	hyp_shared_memory_attrs[i].ipa = info->pct_ipa;
	hyp_shared_memory_attrs[i].size = (size_t)info->pct_size;
	ret = snprintf(hyp_shared_memory_attrs[i].node_name, MAX_NAME_SIZE, "pct");

	if (ret > 0U) {
		hyp_shared_memory_attrs[i].node_name[ret] = '\0';
	} else {
		TEGRA_HV_INFO("snprintf failure - pct\n");
		iounmap((void __iomem *)info);
		ret = -EFAULT;
		goto fail;
	}

	ret = hvc_create_sysfs(kobj, &hyp_shared_memory_attrs[i]);
	if (ret == 0)
		TEGRA_HV_INFO("pct is available\n");
	else
		TEGRA_HV_INFO("pct is unavailable\n");

	iounmap((void __iomem *)info);

	return 0;

fail:
	kobject_del(kobj);
	return ret;
}

/* Set up all relevant hypervisor control nodes */
static int __init hvc_sysfs_register(void)
{
	int ret;

	if (is_tegra_hypervisor_mode() == false) {
		TEGRA_HV_INFO("hypervisor is not present\n");
		/* retunring success in case of native kernel otherwise
		 * systemd-modules-load service will failed.
		 */
		return 0;
	}

	ret = hyp_trace_buffer_init();
	if (ret == 0)
		TEGRA_HV_INFO("Hypervisor trace buffer initialized successfully\n");

	ret = hyp_nvlog_buffer_init();
	if (ret != 0)
		TEGRA_HV_ERR("Error: Hypervisor nvlog buffer init failed\n");
	else
		TEGRA_HV_INFO("Hypervisor nvlog buffer initialized successfully\n");

	return 0;
}

late_initcall(hvc_sysfs_register);

MODULE_LICENSE("GPL");
