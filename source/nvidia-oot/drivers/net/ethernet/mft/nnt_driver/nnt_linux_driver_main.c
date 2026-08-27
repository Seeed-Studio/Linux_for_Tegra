/*
 * Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/semaphore.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include "nnt_defs.h"
#include "nnt_device.h"
#include "nnt_ioctl.h"
#include "nnt_ioctl_defs.h"

MODULE_AUTHOR("Itay Avraham <itayavr@nvidia.com>");
MODULE_DESCRIPTION("NNT Linux driver (NVIDIA® networking tools driver)");

/* Passing MFT flag argument */
int mft_package = 0;

/* Create the file in sysfs. */
module_param(mft_package, int, S_IRUSR);

struct driver_info nnt_driver_info;

static long nnt_ioctl(struct file* file, unsigned int command, unsigned long argument)
{
    void* user_buffer = (void*)argument;
    struct nnt_device* nnt_device = NULL;
    int error;

    /* By convention, any user gets read access
     * and is allowed to use the device.
     * Commands with no direction are administration
     * commands, and you need write permission for this */

    if (_IOC_DIR(command) == _IOC_NONE)
    {
        if (!(file->f_mode & FMODE_WRITE))
        {
            return -EPERM;
        }
    }
    else
    {
        if (!(file->f_mode & FMODE_READ))
        {
            return -EPERM;
        }
    }

    error = mutex_lock_nnt(file);
    CHECK_ERROR(error);

    /* Get the nnt device structure */
    error = get_nnt_device(file, &nnt_device);
    if (error)
    {
        goto ReturnOnFinished;
    }

    switch (command)
    {
        case NNT_GET_DMA_PAGES:
        case NNT_RELEASE_DMA_PAGES:
            error = dma_pages_ioctl(command, user_buffer, nnt_device);
            break;

        case NNT_READ_DWORD_FROM_CONFIG_SPACE:
            error = read_dword_ioctl(command, user_buffer, nnt_device);
            break;

        case NNT_WRITE:
        case NNT_READ:
        {
            struct nnt_rw_operation rw_operation;

            /* Copy the request from user space. */
            if (copy_from_user(&rw_operation, user_buffer, sizeof(struct nnt_rw_operation)) != 0)
            {
                error = -EFAULT;
                goto ReturnOnFinished;
            }

            switch (command)
            {
                case NNT_WRITE:
                    nnt_device->access.write(nnt_device, &rw_operation);
                    break;
                case NNT_READ:
                    nnt_device->access.read(nnt_device, &rw_operation);
                    break;
            }

            /* Copy the data to the user space. */
            if (copy_to_user(user_buffer, &rw_operation, sizeof(struct nnt_rw_operation)) != 0)
            {
                error = -EFAULT;
                goto ReturnOnFinished;
            }
            break;
        }
        case NNT_GET_DEVICE_PARAMETERS:
        {
            struct nnt_device_parameters nnt_parameters;

            error = get_nnt_device_parameters(&nnt_parameters, nnt_device);

            /* Copy the data to the user space. */
            if (copy_to_user(user_buffer, &nn_parameters, sizeof(struct device_parameters)) != 0)
            {
                error = -EFAULT;
                goto ReturnOnFinished;
            }

            break;
        }
        case NNT_INIT:
        {
            struct nnt_pciconf_init init;

            /* Copy the request from user space. */
            if (copy_from_user(&init, user_buffer, sizeof(struct nnt_pciconf_init)) != 0)
            {
                error = -EFAULT;
                goto ReturnOnFinished;
            }
            error = nnt_device->access.init(&init, nnt_device);
            break;
        }
        case NNT_PCI_CONNECTX_WA:
        {
            struct nnt_connectx_wa connectx_wa;
            error = pci_connectx_wa(&connectx_wa, nnt_device);

            /* Copy the data to the user space. */
            if (copy_to_user(user_buffer, &connectx_wa, sizeof(struct nnt_connectx_wa)) != 0)
            {
                error = -EFAULT;
                goto ReturnOnFinished;
            }

            break;
        }
        case NNT_VPD_READ:
        case NNT_VPD_WRITE:
        {
            int vpd_default_timeout = 2000;
            struct nnt_vpd vpd;

            if (!nnt_device->vpd_capability_address)
            {
                printk(KERN_ERR "Device %s not support Vital Product Data\n", nnt_device->device_name);
                return -ENODEV;
            }

            /* Copy the request from user space. */
            if (copy_from_user(&vpd, user_buffer, sizeof(struct nnt_vpd)) != 0)
            {
                error = -EFAULT;
                goto ReturnOnFinished;
            }

            if (!vpd.timeout)
            {
                vpd.timeout = vpd_default_timeout;
            }

            switch (command)
            {
                case NNT_VPD_READ:
                    error = vpd_read(command, &vpd, nnt_device);
                    break;
                case NNT_VPD_WRITE:
                    error = vpd_write(command, &vpd, nnt_device);
                    break;
            }

            break;
        }
        default:
            printk(KERN_ERR "ioctl not implemented, command id: %x\n", command);
            error = -EINVAL;
            break;
    }

ReturnOnFinished:
    mutex_unlock_nnt(file);

    return error;
}

static int nnt_open(struct inode* inode, struct file* file)
{
    if (file->private_data)
    {
        return 0;
    }

    return set_private_data(inode, file);
}

struct file_operations fop = {.unlocked_ioctl = ioctl, .open = nnt_open, .owner = THIS_MODULE};

int with_unknown = 0;

module_param(with_unknown, int, S_IRUSR | S_IWUSR);

static int __init nnt_init_module(void)
{
    int first_minor_number = 0;
    int error = 0;
    dev_t device_numbers;

    /* Get the amount of the Nvidia devices. */
    if ((nnt_driver_info.contiguous_device_numbers = get_amount_of_nvidia_devices()) == 0)
    {
        printk(KERN_ERR "No devices found\n");
        goto ReturnOnFinished;
    }

    /* Allocate char driver region and assign major number */
    if ((error = alloc_chrdev_region(&device_numbers, first_minor_number, nnt_driver_info.contiguous_device_numbers,
                                     NNT_DRIVER_NAME)) != 0)
    {
        printk(KERN_ERR "failed to allocate chrdev_region\n");
        goto CharDeviceAllocated;
    }

    nnt_driver_info.driver_major_number = MAJOR(device_numbers);

    /* create sysfs class. */
    if ((nnt_driver_info.class_driver = class_create(THIS_MODULE, NNT_CLASS_NAME)) == NULL)
    {
        printk(KERN_ERR "Class creation failed\n");
        error = -EFAULT;
        goto DriverClassAllocated;
    }

    /* Create device files for MSTflint and MFT */
    error = create_nnt_devices(nnt_driver_info.contiguous_device_numbers, device_numbers, mft_package,
                               NNT_MELLANOX_PCI_VENDOR, &fop) ||
            create_nnt_devices(nnt_driver_info.contiguous_device_numbers, device_numbers, mft_package,
                               NNT_NVIDIA_PCI_VENDOR, &fop);
    if ((error) == 0)
    {
        goto ReturnOnFinished;
    }

DriverClassAllocated:
    destroy_nnt_devices();
    class_destroy(nnt_driver_info.class_driver);

CharDeviceAllocated:
    unregister_chrdev_region(nnt_driver_info.driver_major_number, nnt_driver_info.contiguous_device_numbers);

ReturnOnFinished:
    return error;
}

static void __exit nnt_cleanup_module(void)
{
    destroy_nnt_devices();
    class_destroy(nnt_driver_info.class_driver);
    unregister_chrdev_region(nnt_driver_info.driver_major_number, nnt_driver_info.contiguous_device_numbers);
}

module_init(nnt_init_module);
module_exit(nnt_cleanup_module);
