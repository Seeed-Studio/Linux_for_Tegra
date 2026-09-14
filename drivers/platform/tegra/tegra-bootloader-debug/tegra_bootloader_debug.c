// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/notifier.h>
#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/efi.h>
#include <asm/page.h>
#include <asm/arch_timer.h>
#ifdef NV_SOC_TEGRA_VIRT_TEGRA_HV_IVC_H_PRESENT
#include <soc/tegra/virt/tegra_hv_ivc.h>
#else
#include <soc/tegra/virt/hv-ivc.h>
#endif
#ifdef NV_SOC_TEGRA_VIRT_TEGRA_HV_H_PRESENT
#include <soc/tegra/virt/tegra_hv.h>
#endif

#include "tegra_bootloader_debug.h"
#include "tegra_bootloader_debug_gr.h"

#ifdef CONFIG_EFI
/* NVIDIA Public Variable GUID for UEFI variables (from BasicProfilerDxe) */
#define NVIDIA_PUBLIC_VARIABLE_GUID \
	EFI_GUID(0x781e084c, 0xa330, 0x417c, 0xb6, 0x78, 0x38, 0xe6, 0x96, 0x38, 0x0c, 0xb9)

/* Profiler carveout layout (must match UEFI BasicProfilerDxe FW_PROFILER_DATA_SIZE) */
#define FW_PROFILER_DATA_SIZE	(64 * 1024)  /* 64KB for firmware profiler data */
#endif

#define BOOTLOADER_INIT_COMPLETE_MSG	"Bootloader Debug Driver Init Complete"

#define MAX_TEGRA_BL_PROF_SIZE (1024 * 128)

static char *tegra_bl_prof_buffer_ptr;
static bool buff_content_valid;
static phys_addr_t tegra_bl_prof_start;
static phys_addr_t tegra_bl_prof_size;
static phys_addr_t tegra_bl_prof_ro_start;
static phys_addr_t tegra_bl_prof_ro_size;

static const char *dir_name = "tegra_bootloader";
static void __iomem *usc;

static char *bl_prof_dataptr = "0@0x0";
static char *bl_prof_ro_ptr = "0@0x0";

static spinlock_t tegra_bl_lock;
static struct kobject *boot_profiler_kobj;
static void *tegra_bl_mapped_prof_start;
static void *tegra_bl_mapped_prof_ro_start;
static bool is_privileged_vm;

#define MAX_PROFILE_STRLEN	55

struct profiler_record {
	char str[MAX_PROFILE_STRLEN + 1];
	uint64_t timestamp;
} __packed;

#define LOG_PROFILER_DATA(print_format, ...) do { \
	if (!buffer_overflow_detected) { \
		len = snprintf(buf + ret, MAX_TEGRA_BL_PROF_SIZE - ret, print_format, ##__VA_ARGS__); \
		if (len >= MAX_TEGRA_BL_PROF_SIZE - ret) { \
			buffer_overflow_detected = true; \
		} else { \
			ret += len; \
		} \
	} \
	pr_info(print_format, ##__VA_ARGS__); \
} while (0)

static ssize_t profiler_show_entries(void *addr, int size, char *buf)
{
	struct profiler_record *profiler_data;
	int count = 0;
	int i = 0;
	bool prof_data_section_valid = false;
	ssize_t ret = 0;
	size_t len = 0;
	bool buffer_overflow_detected = false;

	profiler_data = (struct profiler_record *)addr;
	count = size / sizeof(struct profiler_record);
	i = -1;
	LOG_PROFILER_DATA("\n");
	while (count--) {
		i++;
		if (!profiler_data[i].timestamp) {
			if (prof_data_section_valid) {
				LOG_PROFILER_DATA("\n");
				prof_data_section_valid = false;
			}
			continue;
		}
		if (i > 0 && profiler_data[i - 1].timestamp) {
			LOG_PROFILER_DATA("%-54s\t%16lld \t%16lld\n",
				profiler_data[i].str, profiler_data[i].timestamp,
				profiler_data[i].timestamp - profiler_data[i - 1].timestamp);
		} else {
			LOG_PROFILER_DATA("%-54s\t%16lld\n",
				profiler_data[i].str, profiler_data[i].timestamp);
		}
		prof_data_section_valid = true;
	}
	return ret;
}

static ssize_t profiler_show(struct file *filp, struct kobject *kobj,
#if defined(NV_BIN_ATTRIBUTE_STRUCT_READWRITE_HAS_CONST_BIN_ATTRIBUTE_ARG)
			const struct bin_attribute *attr,
#else
			struct bin_attribute *attr,
#endif
			 char *buf, loff_t pos, size_t count)
{
	static ssize_t valid_buffer_size;

	spin_lock(&tegra_bl_lock);
	if (!buff_content_valid) {
		memset(tegra_bl_prof_buffer_ptr, 0, MAX_TEGRA_BL_PROF_SIZE);
		valid_buffer_size = 0;

		if (!is_tegra_hypervisor_mode()) {
			ssize_t ro_ret = 0;
			ssize_t rw_ret = 0;

			if (tegra_bl_mapped_prof_ro_start && tegra_bl_prof_ro_size) {
				ro_ret = profiler_show_entries(tegra_bl_mapped_prof_ro_start,
					tegra_bl_prof_ro_size, tegra_bl_prof_buffer_ptr);
				if (ro_ret > 0)
					valid_buffer_size += ro_ret;
			}

			if (tegra_bl_mapped_prof_start && tegra_bl_prof_size) {
				rw_ret = profiler_show_entries(tegra_bl_mapped_prof_start,
					tegra_bl_prof_size, tegra_bl_prof_buffer_ptr
					+ valid_buffer_size);
				if (rw_ret > 0)
					valid_buffer_size += rw_ret;
			}
		} else if (is_privileged_vm) {
			if (!tegra_bl_mapped_prof_ro_start) {
				pr_err("%s\n", "Error mapping RO profiling data\n");
				spin_unlock(&tegra_bl_lock);
				return -EINVAL;
			}

			valid_buffer_size = profiler_show_entries(
				tegra_bl_mapped_prof_ro_start, tegra_bl_prof_ro_size,
				tegra_bl_prof_buffer_ptr);
		} else {
			if (!tegra_bl_mapped_prof_start) {
				pr_err("%s\n", "Error mapping RW profiling data\n");
				spin_unlock(&tegra_bl_lock);
				return -EINVAL;
			}

			valid_buffer_size = profiler_show_entries(tegra_bl_mapped_prof_start,
				tegra_bl_prof_size, tegra_bl_prof_buffer_ptr);
		}
		buff_content_valid = true;
	}
	spin_unlock(&tegra_bl_lock);
	/* Check if offset is beyond buffer size */
	if (pos >= valid_buffer_size)
		return 0;

	/* Adjust count if it exceeds the remaining buffer */
	if (pos + count > valid_buffer_size)
		count = valid_buffer_size - pos;

	memcpy(buf, tegra_bl_prof_buffer_ptr + pos, count);
	return count;
}

/* Define the binary attribute structure */
static struct bin_attribute profiler_attribute = {
	.attr = {
		.name = "profiler",
		.mode = 0400,
	},
	.size = MAX_TEGRA_BL_PROF_SIZE,
	.read = profiler_show,
	.write = NULL,
};

static u64 arch_timer_get_us(void)
{
	u64 cnt = arch_timer_read_counter();
	u32 freq = arch_timer_get_cntfrq();
	return cnt * 1000000ULL / freq;
}

/**
 * tegra_bl_add_profiler_entry - add a new profiling point
 * @buf: string to add as a profiling marker
 * @len: length of the string
 *
 * Return: 0 on success or error code in case of failure.
 */
size_t tegra_bl_add_profiler_entry(const char *buf, size_t len)
{
	int count = 0;
	int i = 0;
	struct profiler_record *profiler_data;

	if (len > MAX_PROFILE_STRLEN) {
		pr_err("%s: Failed to add record, invalid length: %ld\n",
			__func__, len);
		return -EINVAL;
	}

	if (!tegra_bl_mapped_prof_start || !tegra_bl_prof_size) {
		pr_err("Error mapping profiling data\n");
		return -EINVAL;
	}

	spin_lock(&tegra_bl_lock);

	profiler_data = (struct profiler_record *)tegra_bl_mapped_prof_start;
	count = tegra_bl_prof_size / sizeof(struct profiler_record);
	while (i < count) {
		if (!profiler_data[i].timestamp)
			break;
		i++;
	}

	if (i == count) {
		pr_err("Error profiling data buffer full\n");
		spin_unlock(&tegra_bl_lock);
		return -ENOMEM;
	}

	if (usc != NULL) {
		profiler_data[i].timestamp = readl(usc);
	} else {
		profiler_data[i].timestamp = arch_timer_get_us();
	}

	strncpy(profiler_data[i].str, buf, len);
	profiler_data[i].str[len] = '\0';
	/* Trim trailing '\n' in case 'echo' command is used */
	if (profiler_data[i].str[len - 1] == '\n') {
		profiler_data[i].str[len - 1] = '\0';
	}

	buff_content_valid = false;
	spin_unlock(&tegra_bl_lock);
	return 0;
}
EXPORT_SYMBOL(tegra_bl_add_profiler_entry);

static ssize_t add_profiler_record_store(struct kobject *kobj,
			struct kobj_attribute *attr,
			const char *buf, size_t len)
{
	if (tegra_bl_add_profiler_entry(buf, len))
		pr_err("Error adding profiler entry failed\n");

	return len;
}

static struct kobj_attribute add_profiler_record_attribute =
	__ATTR_WO(add_profiler_record);


/*
 * Notifier for capturing Linux Resume done stage timestamp.
 * Note that this notifier will get called for both suspend and resume cases. But we currently do
 * not have a way to measure Linux Suspend time because we cannot print the profiler stats anyways
 * when the device goes to suspend. And so there is no check added in the notifier function to check
 * if this is for suspend or resume.
 */
static int profiler_resume_notifier(struct notifier_block *nb, unsigned long event, void *data)
{
	tegra_bl_add_profiler_entry("Guest Linux Resume Notifier", 27);
	return NOTIFY_OK;
}

static struct notifier_block profiler_notifier = {
	.notifier_call = profiler_resume_notifier,
};

/*
 * Handler for SC7 Resume to profile the start of driver resume.
 * Since this driver is loaded after all drivers are loaded, it
 * is expected that this driver's resume handler will get called first.
 */
static int profiler_resume_noirq_handler(struct device *dev)
{
	tegra_bl_add_profiler_entry("Profiler Driver Resume Start", 28);
	return 0;
}

static struct dev_pm_ops profiler_pm_ops = {
	.resume_noirq = profiler_resume_noirq_handler,
};

/*
 * Read the Tegra Microsecond Timer register address.
 * In particular, we need to read address of reg USEC_CNTR_USECCVR_0.
 */
static int read_usec_timer_reg_base(struct platform_device *pdev, u32 *usec_timer_reg_phy_addr)
{
	struct device_node *node = pdev->dev.of_node;

	/* Read the usec_timer_reg_base property */
	return of_property_read_u32(node, "usec_timer_reg_base", usec_timer_reg_phy_addr);
}

static int tegra_bootloader_debuginit(struct platform_device *pdev)
{
	void __iomem *ptr_bl_prof_ro_carveout = NULL;
	void __iomem *ptr_bl_prof_carveout = NULL;
	int bl_debug_verify_file_entry;
	u32 usec_timer_reg_phy_addr = 0U;
	int ret;

	if (!tegra_bl_prof_start || !tegra_bl_prof_size) {
		dev_err(&pdev->dev, "%s: command line parameter bl_prof_dataptr not initialized\n",
			__func__);
		return -ENODEV;
	}

	tegra_bl_prof_buffer_ptr = kzalloc(MAX_TEGRA_BL_PROF_SIZE, GFP_KERNEL);
	if (!tegra_bl_prof_buffer_ptr) {
		dev_err_probe(&pdev->dev, -ENOMEM, "%s: failed to allocate Buffer\n", __func__);
		return -ENOMEM;
	}

	boot_profiler_kobj = kobject_create_and_add(dir_name, kernel_kobj);
	if (IS_ERR_OR_NULL(boot_profiler_kobj)) {
		dev_err(&pdev->dev, "%s: failed to create sysfs entries: %ld\n",
			__func__, PTR_ERR(boot_profiler_kobj));
		goto out_err;
	}

	bl_debug_verify_file_entry = sysfs_create_bin_file(boot_profiler_kobj,
			&profiler_attribute);
	if (bl_debug_verify_file_entry) {
		dev_err(&pdev->dev, "%s: failed to create sysfs file : %d\n",
			__func__, bl_debug_verify_file_entry);
		goto out_err;
	}

	bl_debug_verify_file_entry = sysfs_create_file(boot_profiler_kobj,
			&add_profiler_record_attribute.attr);
	if (bl_debug_verify_file_entry) {
		dev_err(&pdev->dev, "%s: failed to create sysfs file : %d\n",
			__func__, bl_debug_verify_file_entry);
		goto out_err;
	}

	ptr_bl_prof_carveout = ioremap(tegra_bl_prof_start, tegra_bl_prof_size);
	if (!ptr_bl_prof_carveout) {
		dev_err(&pdev->dev, "%s: failed to map tegra_bl_prof_start\n", __func__);
		goto out_err;
	}

	dev_info(&pdev->dev, "Remapped tegra_bl_prof_start(0x%llx) to address 0x%llx, size(0x%llx)\n",
		(u64)tegra_bl_prof_start,
		(__force u64)ptr_bl_prof_carveout,
		(u64)tegra_bl_prof_size);

	tegra_bl_mapped_prof_start = (__force void *)ptr_bl_prof_carveout;

	if (tegra_bl_prof_ro_start != 0 && tegra_bl_prof_ro_size != 0) {
		ptr_bl_prof_ro_carveout = ioremap(tegra_bl_prof_ro_start, tegra_bl_prof_ro_size);
		if (!ptr_bl_prof_ro_carveout) {
			dev_err(&pdev->dev, "%s: failed to map tegra_bl_prof_ro_start\n", __func__);
			goto out_err;
		}

		dev_info(&pdev->dev, "Remapped tegra_bl_prof_ro_start(0x%llx) "
			"to address 0x%llx, size(0x%llx)\n",
			(u64)tegra_bl_prof_ro_start,
			(__force u64)ptr_bl_prof_ro_carveout,
			(u64)tegra_bl_prof_ro_size);

		tegra_bl_mapped_prof_ro_start = (__force void *)ptr_bl_prof_ro_carveout;

		is_privileged_vm = true;
	} else {
		is_privileged_vm = false;
	}

	ret = read_usec_timer_reg_base(pdev, &usec_timer_reg_phy_addr);
	if (ret == 0) {
		usc = ioremap(usec_timer_reg_phy_addr, 4);
		if (!usc) {
			dev_err(&pdev->dev, "Failed to map TEGRA_US_COUNTER_REG\n");
			goto out_err;
		}

		dev_info(&pdev->dev, "Using Tegra USEC timer, base address: 0x%x\n", usec_timer_reg_phy_addr);
	} else {
		dev_info(&pdev->dev, "Tegra USEC timer not available, fallback to use ARM USEC timer\n");
	}

	ret = tegra_bootloader_debug_gr_init(pdev);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to initialize tegra_bl_debug_gr\n");
		goto out_err;
	}

	spin_lock_init(&tegra_bl_lock);

	/* Register SC7 resume notifier to profile SC7 Resume done */
	ret = register_pm_notifier(&profiler_notifier);
	if (ret) {
		dev_err(&pdev->dev, "%s: Failed to register resume notifier: %d\n", __func__, ret);
		/* continue even if this fails */
	}

	/* Add profiler entry to mark successful driver initialization */
	tegra_bl_add_profiler_entry(BOOTLOADER_INIT_COMPLETE_MSG,
		sizeof(BOOTLOADER_INIT_COMPLETE_MSG) - 1);

	return 0;

out_err:
	if (ptr_bl_prof_carveout)
		iounmap(ptr_bl_prof_carveout);
	if (ptr_bl_prof_ro_carveout)
		iounmap(ptr_bl_prof_ro_carveout);

	if (boot_profiler_kobj) {
		sysfs_remove_bin_file(boot_profiler_kobj,
			&profiler_attribute);
		sysfs_remove_file(boot_profiler_kobj,
			&add_profiler_record_attribute.attr);
		kobject_put(boot_profiler_kobj);
		boot_profiler_kobj = NULL;
	}
	kfree(tegra_bl_prof_buffer_ptr);
	return -ENODEV;
}

static int tegra_bl_parse_command_line_args(struct platform_device *pdev)
{
	int err;

	dev_info(&pdev->dev, "%s: bl_prof_dataptr=%s bl_prof_ro_ptr=%s\n", __func__,
		bl_prof_dataptr, bl_prof_ro_ptr);

	if (strncmp(bl_prof_dataptr, "0@0x0", 5) == 0) {
		return -EINVAL;
	}

	err = tegra_bl_args(bl_prof_dataptr,
			&tegra_bl_prof_size,
			&tegra_bl_prof_start);

	if (err != 0) {
		dev_err(&pdev->dev, "%s: tegra_bl_args failed for bl_prof_dataptr: %d\n",
				__func__, err);
		return err;
	}

	err = tegra_bl_args(bl_prof_ro_ptr,
			&tegra_bl_prof_ro_size,
			&tegra_bl_prof_ro_start);

	if (err != 0) {
		dev_err(&pdev->dev, "%s: tegra_bl_args failed for bl_prof_ro_ptr: %d\n",
				__func__, err);
		return err;
	}

	err = tegra_bl_parse_command_line_debug_gr_args(pdev);
	if (err != 0) {
		dev_err(&pdev->dev, "%s: tegra_bl_args failed for debug_gr: %d\n",
				__func__, err);
		return err;
	}

	return err;
}

#ifdef CONFIG_EFI
static int tegra_bl_parse_uefi_args(struct platform_device *pdev)
{
	efi_guid_t nvidia_guid = NVIDIA_PUBLIC_VARIABLE_GUID;
	unsigned long data_size;
	efi_status_t status;
	u64 profiler_base = 0;
	u64 profiler_size = 0;

	if (strncmp(bl_prof_dataptr, "0@0x0", 5) != 0)
		return -EINVAL;

	if (!efi_enabled(EFI_RUNTIME_SERVICES)) {
		dev_err(&pdev->dev, "EFI runtime services not available\n");
		return -ENODEV;
	}

	data_size = sizeof(profiler_base);
	status = efi.get_variable(L"ProfilerBase", &nvidia_guid, NULL,
				  &data_size, &profiler_base);
	if (status != EFI_SUCCESS) {
		dev_err(&pdev->dev, "Failed to read ProfilerBase UEFI variable: 0x%lx\n",
			(unsigned long)status);
		return -ENOENT;
	}

	data_size = sizeof(profiler_size);
	status = efi.get_variable(L"ProfilerSize", &nvidia_guid, NULL,
				  &data_size, &profiler_size);
	if (status != EFI_SUCCESS) {
		dev_err(&pdev->dev, "Failed to read ProfilerSize UEFI variable: 0x%lx\n",
			(unsigned long)status);
		return -ENOENT;
	}

	if (profiler_base == 0 || profiler_size == 0 || profiler_size <= FW_PROFILER_DATA_SIZE) {
		dev_err(&pdev->dev, "Invalid UEFI profiler variables: base=0x%llx size=0x%llx\n",
			profiler_base, profiler_size);
		return -EINVAL;
	}

	tegra_bl_prof_ro_start = profiler_base;
	tegra_bl_prof_ro_size = FW_PROFILER_DATA_SIZE;
	tegra_bl_prof_start = profiler_base + FW_PROFILER_DATA_SIZE;
	tegra_bl_prof_size = profiler_size - FW_PROFILER_DATA_SIZE;

	dev_info(&pdev->dev, "UEFI variables: ProfilerBase=0x%llx ProfilerSize=0x%llx\n",
		profiler_base, profiler_size);
	dev_info(&pdev->dev, "Derived: bl_prof_dataptr=%llu@0x%llx bl_prof_ro_ptr=%llu@0x%llx\n",
		(u64)tegra_bl_prof_size, (u64)tegra_bl_prof_start,
		(u64)tegra_bl_prof_ro_size, (u64)tegra_bl_prof_ro_start);

	return 0;
}
#endif

static int tegra_bl_parse_args(struct platform_device *pdev)
{
	int ret = -EINVAL;

	ret = tegra_bl_parse_command_line_args(pdev);
	if (ret == 0) {
		dev_info(&pdev->dev, "Arguments parsed from kernel command line\n");
		return ret;
	}

	#ifdef CONFIG_EFI
	ret = tegra_bl_parse_uefi_args(pdev);
	if (ret == 0) {
		dev_info(&pdev->dev, "Arguments parsed from UEFI variables\n");
		return ret;
	}
	#endif

	return ret;
}

static int tegra_bl_debug_probe(struct platform_device *pdev)
{
	int ret;

	ret = tegra_bl_parse_args(pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "tegra_bl arguments not found\n");
		return ret;
	}

	ret = tegra_bootloader_debuginit(pdev);
	if (ret != 0) {
		dev_err(&pdev->dev, "%s: tegra_bootloader_debuginit failed: %d\n", __func__, ret);
	}

	return 0; /* Return 0 for success */
}

static int tegra_bl_debug_remove(struct platform_device *pdev)
{
	/* Device removal code goes here */
	dev_info(&pdev->dev, "%s\n", __func__);
	return 0; /* Return 0 for success */
}

static const struct of_device_id tegra_bl_debug_of_match[] = {
	{ .compatible = "nvidia,tegra_bl_debug"},
	{},
};

MODULE_DEVICE_TABLE(of, tegra_bl_debug_of_match);

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void tegra_bl_debug_remove_wrapper(struct platform_device *pdev)
{
	tegra_bl_debug_remove(pdev);
}
#else
static int tegra_bl_debug_remove_wrapper(struct platform_device *pdev)
{
	return tegra_bl_debug_remove(pdev);
}
#endif

static struct platform_driver tegra_bl_debug_driver = {
	.probe = tegra_bl_debug_probe,
	.remove = tegra_bl_debug_remove_wrapper,
	.driver = {
		.name = "tegra_bl_debug",
		.pm = &profiler_pm_ops,
		.of_match_table = tegra_bl_debug_of_match,
	},
};

static int __init tegra_bl_debuginit_module_init(void)
{
	int err = 0;

	err = platform_driver_register(&tegra_bl_debug_driver);
	if (err < 0) {
		pr_err("%s: Failed to register platform driver: %d\n", __func__, err);
	}

	return err;
}

static void __exit tegra_bl_debuginit_module_exit(void)
{
	tegra_bl_debug_gr_init_module_exit();

	if (tegra_bl_mapped_prof_ro_start)
		iounmap((void __iomem *)tegra_bl_mapped_prof_ro_start);

	if (tegra_bl_mapped_prof_start)
		iounmap((void __iomem *)tegra_bl_mapped_prof_start);

	if (boot_profiler_kobj) {
		sysfs_remove_file(boot_profiler_kobj,
			&profiler_attribute.attr);
		sysfs_remove_file(boot_profiler_kobj,
			&add_profiler_record_attribute.attr);
		kobject_put(boot_profiler_kobj);
		boot_profiler_kobj = NULL;
	}

	if (usc)
		iounmap(usc);

	kfree(tegra_bl_prof_buffer_ptr);
	platform_driver_unregister(&tegra_bl_debug_driver);
	unregister_pm_notifier(&profiler_notifier);
}

module_param(bl_prof_dataptr, charp, 0400);
module_param(bl_prof_ro_ptr, charp, 0400);

module_init(tegra_bl_debuginit_module_init);
module_exit(tegra_bl_debuginit_module_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Driver to enumerate bootloader's debug and profiler data");
MODULE_AUTHOR("Mohit Dhingra <mdhingra@nvidia.com>");
MODULE_AUTHOR("Deepak Nibade <dnibade@nvidia.com>");
