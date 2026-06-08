#include <linux/module.h>
#include <linux/init.h>
#include <linux/semaphore.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include "nnt_ioctl.h"
#include "nnt_defs.h"
#include "nnt_device.h"
#include "nnt_ioctl_defs.h"
#include "nnt_pci_conf_access.h"
#include "mst_pciconf_bc.h"

MODULE_AUTHOR("Itay Avraham <itayavr@nvidia.com>");
MODULE_DESCRIPTION("NNT Linux driver (NVIDIA® networking tools driver), this is the backward compatibility driver");
MODULE_LICENSE("Dual BSD/GPL");

struct driver_info nnt_driver_info;
static int major_number = -1;
static char* name = "mst_pciconf";

#define INIT PCICONF_INIT
#define STOP PCICONF_STOP
#define READ4 PCICONF_READ4
#define READ4_NEW PCICONF_READ4_NEW
#define WRITE4 PCICONF_WRITE4
#define WRITE4_NEW PCICONF_WRITE4_NEW
#define MODIFY PCICONF_MODIFY
#define READ4_BUFFER PCICONF_READ4_BUFFER
#define READ4_BUFFER_EX PCICONF_READ4_BUFFER_EX
#define WRITE4_BUFFER PCICONF_WRITE4_BUFFER
#define MST_PARAMS PCICONF_MST_PARAMS
#define MST_META_DATA PCICONF_MST_META_DATA
#define GET_DMA_PAGES PCICONF_GET_DMA_PAGES
#define RELEASE_DMA_PAGES PCICONF_RELEASE_DMA_PAGES
#define READ_DWORD_FROM_CONFIG_SPACE PCICONF_READ_DWORD_FROM_CONFIG_SPACE

static int mst_pciconf_bc_open(struct inode* inode, struct file* file)
{
    if (file->private_data)
    {
        return 0;
    }

    set_private_data_open(file);

    return 0;
}

static ssize_t mst_pciconf_bc_read(struct file* file, char* buf, size_t count, loff_t* f_pos)
{
    struct nnt_device* nnt_device = NULL;
    int error = 0;

    /* Get the nnt device structure */
    error = get_nnt_device(file, &nnt_device);
    if (error)
    {
        count = -EFAULT;
        goto ReturnOnFinished;
    }

    error = mutex_lock_nnt(file);

    if (*f_pos >= nnt_device->buffer_used_bc)
    {
        count = 0;
        goto MutexUnlock;
    }

    if (*f_pos + count > nnt_device->buffer_used_bc)
    {
        count = nnt_device->buffer_used_bc - *f_pos;
    }

    if (copy_to_user(buf, nnt_device->buffer_bc + *f_pos, count))
    {
        count = -EFAULT;
        goto MutexUnlock;
    }

    *f_pos += count;

MutexUnlock:
    mutex_unlock_nnt(file);
ReturnOnFinished:
    return count;
}

static ssize_t mst_pciconf_bc_write(struct file* file, const char* buf, size_t count, loff_t* f_pos)
{
    struct nnt_device* nnt_device = NULL;
    int error = 0;

    /* Get the nnt device structure */
    error = get_nnt_device(file, &nnt_device);
    if (error)
    {
        count = -EFAULT;
        goto ReturnOnFinished;
    }

    error = mutex_lock_nnt(file);

    if (*f_pos >= MST_BC_BUFFER_SIZE)
    {
        count = 0;
        goto MutexUnlock;
    }

    if (*f_pos + count > MST_BC_BUFFER_SIZE)
    {
        count = MST_BC_BUFFER_SIZE - *f_pos;
    }

    if (copy_from_user(nnt_device->buffer_bc + *f_pos, buf, count))
    {
        count = -EFAULT;
        goto MutexUnlock;
    }

    *f_pos += count;

    if (nnt_device->buffer_used_bc < *f_pos)
    {
        nnt_device->buffer_used_bc = *f_pos;
    }

MutexUnlock:
    mutex_unlock_nnt(file);
ReturnOnFinished:
    return count;
}

static long ioctl(struct file* file, unsigned int command, unsigned long argument)
{
    void* user_buffer = (void*)argument;
    struct nnt_device* nnt_device = NULL;
    int error = 0;

    /* By convention, any user gets read access
     * and is allowed to use the device.
     * Commands with no direction are administration
     * commands, and you need write permission
     * for this */

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

    if (command != INIT)
    {
        error = mutex_lock_nnt(file);
        CHECK_ERROR(error);

        /* Get the nnt device structure */
        error = get_nnt_device(file, &nnt_device);
        if (error)
        {
            goto ReturnOnFinished;
        }
    }

    switch (command)
    {
        case INIT:
        {
            struct nnt_pciconf_init nnt_init;
            struct mst_pciconf_init_st mst_init;
            struct nnt_device* nnt_device = NULL;

            if (copy_from_user(&mst_init, user_buffer, sizeof(struct mst_pciconf_init_st)))
            {
                error = -EFAULT;
                goto ReturnOnFinished;
            }

            error = set_private_data_bc(file, mst_init.bus, mst_init.devfn, mst_init.domain);
            if (error)
            {
                goto ReturnOnFinished;
            }

            error = mutex_lock_nnt(file);
            CHECK_ERROR(error);

            /* Get the nnt device structure */
            error = get_nnt_device(file, &nnt_device);
            if (error)
            {
                goto ReturnOnFinished;
            }

            nnt_init.address_register = mst_init.addr_reg;
            nnt_init.address_data_register = mst_init.data_reg;

            /* Truncate to 0 length on open for writing. */
            if (file->f_flags & O_APPEND)
            {
                file->f_pos = nnt_device->buffer_used_bc;
            }
            else if ((file->f_flags & O_TRUNC) || (file->f_flags & O_WRONLY))
            {
                nnt_device->buffer_used_bc = 0;
            }

            error = nnt_device->access.init(nnt_device);
            break;
        }
        case WRITE4:
        {
            struct nnt_rw_operation rw_operation;
            struct mst_write4_st mst_write;

            /* Copy the request from user space. */
            if (copy_from_user(&mst_write, user_buffer, sizeof(struct mst_write4_st)))
            {
                error = -EFAULT;
                goto ReturnOnFinished;
            }
            rw_operation.data[0] = mst_write.data;
            rw_operation.offset = mst_write.offset;
            rw_operation.size = 4;

            error = nnt_device->access.write(nnt_device, &rw_operation);

            break;
        }
        case WRITE4_NEW:
        {
            struct nnt_rw_operation rw_operation;
            struct mst_write4_new_st mst_write;

            /* Copy the request from user space. */
            if (copy_from_user(&mst_write, user_buffer, sizeof(struct mst_write4_new_st)))
            {
                error = -EFAULT;
                goto ReturnOnFinished;
            }

            rw_operation.data[0] = mst_write.data;
            rw_operation.offset = mst_write.offset;
            rw_operation.address_space = mst_write.address_space;
            rw_operation.size = 4;

            error = nnt_device->access.write(nnt_device, &rw_operation);

            break;
        }
        case WRITE4_BUFFER:
        {
            struct mst_write4_buffer_st mst_write;

            /* Copy the request from user space. */
            if (copy_from_user(&mst_write, user_buffer, sizeof(struct mst_write4_buffer_st)))
            {
                error = -EFAULT;
                goto ReturnOnFinished;
            }

            error = nnt_device->access.write(nnt_device, (struct nnt_rw_operation*)&mst_write);
            if (error)
            {
                goto ReturnOnFinished;
            }

            /* No error, return the requested data length. */
            error = mst_write.size;

            break;
        }
        case READ4:
        {
            struct nnt_rw_operation rw_operation;
            struct mst_read4_st mst_read;

            /* Copy the request from user space. */
            if (copy_from_user(&mst_read, user_buffer, sizeof(struct mst_read4_st)))
            {
                error = -EFAULT;
                goto ReturnOnFinished;
            }

            rw_operation.offset = mst_read.offset;
            rw_operation.size = 4;

            error = nnt_device->access.read(nnt_device, &rw_operation);
            if (error)
            {
                goto ReturnOnFinished;
            }

            mst_read.data = rw_operation.data[0];

            /* Copy the data to the user space. */
            if (copy_to_user(user_buffer, &mst_read, sizeof(struct mst_read4_st)) != 0)
            {
                error = -EFAULT;
                goto ReturnOnFinished;
            }
            break;
        }
        case READ4_NEW:
        {
            struct nnt_rw_operation rw_operation;
            struct mst_read4_new_st mst_read;

            /* Copy the request from user space. */
            if (copy_from_user(&mst_read, user_buffer, sizeof(struct mst_read4_new_st)))
            {
                error = -EFAULT;
                goto ReturnOnFinished;
            }

            rw_operation.offset = mst_read.offset;
            rw_operation.address_space = mst_read.address_space;
            rw_operation.size = 4;
            error = nnt_device->access.read(nnt_device, &rw_operation);
            if (error)
            {
                goto ReturnOnFinished;
            }

            mst_read.data = rw_operation.data[0];

            /* Copy the data to the user space. */
            if (copy_to_user(user_buffer, &mst_read, sizeof(struct mst_read4_new_st)) != 0)
            {
                error = -EFAULT;
                goto ReturnOnFinished;
            }

            break;
        }
        case READ4_BUFFER_EX:
        case READ4_BUFFER:
        {
            struct mst_read4_buffer_st mst_read;

            /* Copy the request from user space. */
            if (copy_from_user(&mst_read, user_buffer, sizeof(struct mst_read4_buffer_st)))
            {
                error = -EFAULT;
                goto ReturnOnFinished;
            }

            error = nnt_device->access.read(nnt_device, (struct nnt_rw_operation*)&mst_read);
            if (error)
            {
                goto ReturnOnFinished;
            }

            /* Copy the data to the user space. */
            if (copy_to_user(user_buffer, &mst_read, sizeof(struct mst_read4_buffer_st)) != 0)
            {
                error = -EFAULT;
                goto ReturnOnFinished;
            }

            /* No error, return the requested data length. */
            error = mst_read.size;

            break;
        }
        case PCICONF_VPD_READ4:
        {
            int vpd_default_timeout = 2000;
            struct mst_vpd_read4_st mst_vpd_read;
            struct nnt_vpd nnt_vpd;

            if (!nnt_device->vpd_capability_address)
            {
                printk(KERN_ERR "Device %s not support Vital Product Data\n", nnt_device->device_name);
                error = -ENODEV;
                goto ReturnOnFinished;
            }

            /* Copy the request from user space. */
            if (copy_from_user(&mst_vpd_read, user_buffer, sizeof(struct mst_vpd_read4_st)) != 0)
            {
                error = -EFAULT;
                goto ReturnOnFinished;
            }

            nnt_vpd.offset = mst_vpd_read.offset;
            nnt_vpd.data = mst_vpd_read.data;

            if (!nnt_vpd.timeout)
            {
                nnt_vpd.timeout = vpd_default_timeout;
            }

            error = vpd_read(&nnt_vpd, nnt_device);
            if (error)
            {
                goto ReturnOnFinished;
            }

            mst_vpd_read.offset = nnt_vpd.offset;
            mst_vpd_read.data = nnt_vpd.data;

            /* Copy the data to the user space. */
            if (copy_to_user(user_buffer, &mst_vpd_read, sizeof(struct mst_vpd_read4_st)) != 0)
            {
                error = -EFAULT;
                goto ReturnOnFinished;
            }

            break;
        }
        case PCICONF_VPD_WRITE4:
        {
            int vpd_default_timeout = 2000;
            struct mst_vpd_write4_st mst_vpd_write;
            struct nnt_vpd nnt_vpd;

            if (!nnt_device->vpd_capability_address)
            {
                printk(KERN_ERR "Device %s not support Vital Product Data\n", nnt_device->device_name);
                error = -ENODEV;
                goto ReturnOnFinished;
            }

            /* Copy the request from user space. */
            if (copy_from_user(&mst_vpd_write, user_buffer, sizeof(struct mst_vpd_write4_st)) != 0)
            {
                error = -EFAULT;
                goto ReturnOnFinished;
            }

            nnt_vpd.offset = mst_vpd_write.offset;
            nnt_vpd.data = mst_vpd_write.data;

            if (!nnt_vpd.timeout)
            {
                nnt_vpd.timeout = vpd_default_timeout;
            }

            error = vpd_write(&nnt_vpd, nnt_device);
            if (error)
            {
                goto ReturnOnFinished;
            }

            mst_vpd_write.offset = nnt_vpd.offset;
            mst_vpd_write.data = nnt_vpd.data;

            /* Copy the data to the user space. */
            if (copy_to_user(user_buffer, &mst_vpd_write, sizeof(struct mst_vpd_write4_st)) != 0)
            {
                error = -EFAULT;
                goto ReturnOnFinished;
            }

            break;
        }
        case GET_DMA_PAGES:
        {
            error = dma_pages_ioctl(NNT_GET_DMA_PAGES, user_buffer, nnt_device);
            break;
        }
        case RELEASE_DMA_PAGES:
        {
            error = dma_pages_ioctl(NNT_RELEASE_DMA_PAGES, user_buffer, nnt_device);
            break;
        }
        case READ_DWORD_FROM_CONFIG_SPACE:
        {
            struct nnt_read_dword_from_config_space nnt_read_from_cspace = {0};

            /* Copy the request from user space. */
            if (copy_from_user(&nnt_read_from_cspace, user_buffer, sizeof(struct nnt_read_dword_from_config_space)))
            {
                error = -EFAULT;
                goto ReturnOnFinished;
            }

            /* Read the dword. */
            if (read_dword(&nnt_read_from_cspace, nnt_device))
            {
                goto ReturnOnFinished;
            }

            /* Copy the data to the user space. */
            if (copy_to_user(user_buffer, &nnt_read_from_cspace, sizeof(struct nnt_read_dword_from_config_space)) != 0)
            {
                error = -EFAULT;
                goto ReturnOnFinished;
            }

            break;
        }
        case MST_META_DATA:
        {
            struct mst_meta_data meta_data;
            struct mst_hdr hdr;
            memset(&meta_data, 0, sizeof(meta_data));

            /* Copy the request from user space. */
            if (copy_from_user(&hdr, user_buffer, sizeof(struct mst_hdr)))
            {
                error = -EFAULT;
                goto ReturnOnFinished;
            }

            if (hdr.payload_version_major != MST_META_DATA_VERSION_MAJOR || hdr.payload_len < sizeof(meta_data.data))
            {
                error = -EINVAL;
                goto ReturnOnFinished;
            }
            // fill meta_data hdr
            meta_data.hdr.hdr_version = MST_HDR_VERSION;
            meta_data.hdr.hdr_len = sizeof(meta_data.hdr);
            meta_data.hdr.payload_len = sizeof(meta_data.data);
            meta_data.hdr.payload_version_major = MST_META_DATA_VERSION_MAJOR;
            meta_data.hdr.payload_version_minor = MST_META_DATA_VERSION_MINOR;
            // fill payload
            meta_data.data.api_version_major = MST_API_VERSION_MAJOR;
            meta_data.data.api_version_minor = MST_API_VERSION_MINOR;

            /* Copy the data to the user space. */
            if (copy_to_user(user_buffer, &meta_data, sizeof(meta_data)) != 0)
            {
                error = -EFAULT;
                goto ReturnOnFinished;
            }

            break;
        }
        case MST_PARAMS:
        {
            struct nnt_device_parameters nnt_parameters;
            struct mst_params_st mst_params;

            error = get_nnt_device_parameters(&nnt_parameters, nnt_device);
            if (error)
            {
                goto ReturnOnFinished;
            }

            mst_params.bus = nnt_parameters.bus;
            mst_params.bar = 0;
            mst_params.domain = nnt_parameters.domain;
            mst_params.func = nnt_parameters.function;
            mst_params.slot = nnt_parameters.slot;
            mst_params.device = nnt_parameters.device;
            mst_params.vendor = nnt_parameters.vendor;
            mst_params.subsystem_device = nnt_parameters.subsystem_device;
            mst_params.subsystem_vendor = nnt_parameters.subsystem_vendor;
            mst_params.vendor_specific_cap = nnt_parameters.vendor_specific_capability;
            mst_params.multifunction = nnt_parameters.multifunction;
            mst_params.vsec_cap_mask = nnt_parameters.vsec_capability_mask;

            /* Copy the data to the user space. */
            if (copy_to_user(user_buffer, &mst_params, sizeof(struct mst_params_st)) != 0)
            {
                error = -EFAULT;
                goto ReturnOnFinished;
            }

            break;
        }
        case STOP:
        {
            error = destroy_nnt_device_bc(nnt_device);
            break;
        }
        case PCICONF_DMA_PROPS:
        case PCICONF_MEM_ACCESS:
        case MODIFY:
            break;
        default:
            error = -EINVAL;
            break;
    }

ReturnOnFinished:
    mutex_unlock_nnt(file);

    return error;
}

struct file_operations fop = {.unlocked_ioctl = ioctl,
                              .open = mst_pciconf_bc_open,
                              .write = mst_pciconf_bc_write,
                              .read = mst_pciconf_bc_read,
                              .owner = THIS_MODULE};

int with_unknown = 0;

module_param(with_unknown, int, S_IRUSR | S_IWUSR);

static int __init mst_pciconf_init_module(void)
{
    dev_t device_number = -1;
    int is_alloc_chrdev_region = 0;
    int error = 0;

    /* Allocate char driver region and assign major number */
    major_number = register_chrdev(0, name, &fop);
    if (major_number <= 0)
    {
        printk(KERN_ERR "Unable to register character mst pciconf driver.\n");
        error = -EINVAL;
    }

    /* Create device files for MFT. */
    error = create_nnt_devices(device_number, is_alloc_chrdev_region, &fop, NNT_PCICONF_DEVICES,
                               NNT_MELLANOX_PCI_VENDOR, with_unknown) ||
            create_nnt_devices(device_number, is_alloc_chrdev_region, &fop, NNT_PCI_DEVICES, NNT_NVIDIA_PCI_VENDOR,
                               with_unknown);

    return error;
}

static void __exit mst_pciconf_cleanup_module(void)
{
    int is_alloc_chrdev_region = 0;

    destroy_nnt_devices(is_alloc_chrdev_region);
    unregister_chrdev(major_number, name);
}

module_init(mst_pciconf_init_module);
module_exit(mst_pciconf_cleanup_module);
