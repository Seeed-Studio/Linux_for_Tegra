/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * This software product is a proprietary product of Nvidia Corporation and its affiliates
 * (the "Company") and all right, title, and interest in and to the software
 * product, including all associated intellectual property rights, are and
 * shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 */

#include <nvidia/conftest.h>

#include "bf3_livefish.h"

#include <linux/acpi.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>

static __iomem void *hca_va;


/*
 * A valid I/O must be entirely within CR space and not extend into
 * any unmapped areas of CR space.  We don't truncate I/O that extends
 * past the end of the CR space region (unlike the behavior of, for
 * example, simple_read_from_buffer) but instead just call the whole
 * I/O invalid.  We also enforce 4-byte alignment for all I/O.
 */
static bool valid_range(loff_t offset, size_t len)
{
    if ((offset % 4 != 0) || (len % 4 != 0)) {
        // Unaligned,
        return false;
    }

    if ((offset >= 0) && (offset + len <= CRSPACE_SIZE)) {
        // Inside the HCA space.
        return true;
    }

    return false;
}



static bool set_lock(void)
{
    int retries;

    for (retries = 0; retries < 100; retries++) {
        // Reading the lock value from g_gw_arm_nonsecure.lock.
        u32 lock_dword = readl_relaxed(hca_va);
        if (!(lock_dword & 0x80000000)) {
            return true;
        }
    }

    return false;
}



static void release_lock(void)
{
    // Reading the lock value from g_gw_arm_nonsecure.lock.
    u32 lock_dword = readl_relaxed(hca_va);
    lock_dword &= 0x7fffffff;
    writel_relaxed(lock_dword, hca_va);
}



static bool set_busy(void)
{
    int retries;

    // Set busy bit.
    u32 busy_dword = readl_relaxed(hca_va);
    busy_dword |= 1UL << 30;
    writel_relaxed(busy_dword, hca_va);

    for (retries = 0; retries < 1000; retries++) {
        // Reading the bust value from g_gw_arm_nonsecure.busy.
        u32 busy = readl_relaxed(hca_va);
        if (!(busy & 0x40000000)) {
            return true;
        }
    }

    return false;
}




static u32 crspace_read(int offset)
{
    u32 data, new_offset;

    if (!set_lock()) {
        return -EINVAL;
    }

    // Write the address to the GA: g_gw_arm_nonsecure.desc0.addr.
    new_offset = offset >> 2; // HW expects addr[25:2] in that register.
    writel_relaxed(new_offset, hca_va + 0x18);

    // Set read operation.
    writel_relaxed(1, hca_va + 0x0c);

    if (!set_busy()) {
        release_lock();
        return -EINVAL;
    }

    // Reading the value of the desired address from
    // gw_cr_64b.g_gw_arm_nonsecure.desc0.data_31_0 plus offset 0x14.
    data = readl_relaxed(hca_va + 0x14);

    release_lock();

    return data;
}




static void crspace_write(u32 data, int offset)
{
    u32 new_offset;

    if (!set_lock()) {
        return;
    }

    // Write the address to the GA: g_gw_arm_nonsecure.desc0.addr.
    new_offset = offset >> 2; // HW expects addr[25:2] in that register.
    writel_relaxed(new_offset, hca_va + 0x18);

    // Writing the value of the desired address from
    // gw_cr_64b.g_gw_arm_nonsecure.desc0.data_31_0 plus offset 0x14.
    writel_relaxed(data, hca_va + 0x14);

    // Set write operation.
    writel_relaxed(0, hca_va + 0x0c);

    if (!set_busy()) {
        release_lock();
        return;
    }

    release_lock();
}




/*
 * Note that you can seek to illegal areas within the livefish device,
 * but you won't be able to read or write there.
 */
static loff_t livefish_llseek(struct file *filp, loff_t offset, int whence)
{
    if (offset % 4 != 0) {
        return -EINVAL;
    }

    return fixed_size_llseek(filp, offset, whence, CRSPACE_SIZE);
}




static ssize_t livefish_read(struct file *filp, char __user *to,
                             size_t len, loff_t *ppos)
{
    loff_t pos = *ppos;
    size_t counter;
    int word;

    if (!valid_range(pos, len)) {
        return -EINVAL;
    }

    if (len == 0) {
        return 0;
    }

    for (counter = 0; counter < len; counter += 4, pos += 4) {
        word = crspace_read(pos);

        if (put_user(word, (int __user *)(to + counter)) != 0) {
            break;
        }
    }

    *ppos = pos;

    return counter ?: -EFAULT;
}




static ssize_t livefish_write(struct file *filp, const char __user *from,
                              size_t len, loff_t *ppos)
{
    loff_t pos = *ppos;
    size_t counter;
    int word;

    if (!valid_range(pos, len)) {
        return -EINVAL;
    }

    if (len == 0) {
        return 0;
    }

    for (counter = 0; counter < len; counter += 4, pos += 4) {
        if (get_user(word, (int __user *)(from + counter)) != 0) {
            break;
        }

        crspace_write(word, pos);
    }

    *ppos = pos;

    return counter ?: -EFAULT;
}


static const struct file_operations livefish_fops = {
    .owner      = THIS_MODULE,
    .llseek     = livefish_llseek,
    .read       = livefish_read,
    .write      = livefish_write,
};

/* This name causes the correct semantics for the Mellanox MST tools. */
static struct miscdevice livefish_dev = {
    .minor      = MISC_DYNAMIC_MINOR,
    .name       = "bf3-livefish",
    .mode       = 0600,
    .fops       = &livefish_fops
};


/* Release any VA or PA mappings that have been set up. */
static void livefish_cleanup_mappings(void)
{
    if (hca_va) {
        iounmap(hca_va);
    }
}




static int livefish_probe(struct platform_device *pdev)
{
    struct acpi_device *acpi_dev = ACPI_COMPANION(&pdev->dev);
    const char *hid = acpi_device_hid(acpi_dev);
    struct resource *res;
    int error = 0;

    // Device ID validation.
    if (strcmp(hid, "MLNXBF45") != 0) {
        dev_err(&pdev->dev, "Invalid device ID %s\n", hid);
        error = -ENODEV;
        goto ReturnOnError;
    }

    // Find and map the HCA region.
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (res == NULL) {
        error = -ENODEV;
        goto ReturnOnError;
    }

    if (request_mem_region(res->start, resource_size(res),
                   "LiveFish (HCA)") == NULL) {
        error = -ENODEV;
        goto ReturnOnError;
    }

    hca_va = ioremap(res->start, resource_size(res));

    if (!hca_va) {
        error = -EINVAL;
        goto ReturnOnError;
    }

    dev_info(&pdev->dev, "HCA Region PA: 0x%llx Size: 0x%llx\n",
             res->start, resource_size(res));

    error = misc_register(&livefish_dev);
    if (error) {
        goto ReturnOnError;
    }

    dev_info(&pdev->dev, "probed\n");

    return error;

ReturnOnError:
    livefish_cleanup_mappings();
    return error;
}




static int livefish_remove(struct platform_device *pdev)
{
    misc_deregister(&livefish_dev);
    livefish_cleanup_mappings();
    return 0;
}

static const struct of_device_id livefish_of_match[] = {
    { .compatible = "mellanox,mlxbf-livefish" },
    {},
};

MODULE_DEVICE_TABLE(of, livefish_of_match);

static const struct acpi_device_id livefish_acpi_match[] = {
    { "MLNXBF45", 0 },
    {},
};
MODULE_DEVICE_TABLE(acpi, livefish_acpi_match);

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void livefish_remove_wrapper(struct platform_device *pdev)
{
    livefish_remove(pdev);
}
#else
static int livefish_remove_wrapper(struct platform_device *pdev)
{
    return livefish_remove(pdev);
}
#endif

static struct platform_driver livefish_driver = {
    .driver = {
        .name = "mlxbf-livefish",
        .of_match_table = livefish_of_match,
        .acpi_match_table = ACPI_PTR(livefish_acpi_match),
    },
    .probe  = livefish_probe,
    .remove = livefish_remove_wrapper,
};


module_platform_driver(livefish_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("BlueField 3 LiveFish driver");
MODULE_AUTHOR("Itay Avraham <itayavr@nvidia.com>");
MODULE_VERSION(STRINGIFY(DRIVER_VERSION));
