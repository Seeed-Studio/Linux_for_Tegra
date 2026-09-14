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
#include "mst_pci_bc.h"

MODULE_AUTHOR("Itay Avraham <itayavr@nvidia.com>");
MODULE_DESCRIPTION("NNT Linux driver (NVIDIA® networking tools driver), this is the backward compatibility driver");
MODULE_LICENSE("Dual BSD/GPL");

struct driver_info nnt_driver_info;
static int major_number = -1;
static char* name = "mst_pci";

#define INIT PCI_INIT
#define STOP PCI_STOP
#define PCI_PARAMS_ PCI_PARAMS
#define CONNECTX_WA PCI_CONNECTX_WA

struct mst_device_data
{
    char buffer[MST_BC_BUFFER_SIZE];
    int buffer_used;
};

static struct mst_device_data mst_devices[MST_BC_MAX_MINOR];

static int mst_pci_bc_open(struct inode* inode, struct file* file)
{
    if (file->private_data)
    {
        return 0;
    }

    set_private_data_open(file);

    return 0;
}

static ssize_t mst_pci_bc_read(struct file* file, char* buf, size_t count, loff_t* f_pos)
{
    struct mst_device_data* mst_device = NULL;
    struct nnt_device* nnt_device = NULL;
    int* buffer_used = NULL;
    char* buffer = NULL;
    int minor = 0;
    int error = 0;

    /* Get the nnt device structure */
    error = get_nnt_device(file, &nnt_device);
    if (error)
    {
        minor = iminor(file_inode(file));
        mst_device = &mst_devices[minor];
        buffer = mst_device->buffer;
        buffer_used = &mst_device->buffer_used;
    }
    else
    {
        buffer = nnt_device->buffer_bc;
        buffer_used = &nnt_device->buffer_used_bc;
        error = mutex_lock_nnt(file);
        CHECK_ERROR(error);
    }

    if (*f_pos >= *buffer_used)
    {
        count = 0;
        goto MutexUnlock;
    }

    if (*f_pos + count > *buffer_used)
    {
        count = *buffer_used - *f_pos;
    }

    if (copy_to_user(buf, buffer + *f_pos, count))
    {
        count = -EFAULT;
        goto MutexUnlock;
    }

    *f_pos += count;

MutexUnlock:
    if (nnt_device)
    {
        mutex_unlock_nnt(file);
    }

ReturnOnFinished:
    return count;
}

static ssize_t mst_pci_bc_write(struct file* file, const char* buf, size_t count, loff_t* f_pos)
{
    struct mst_device_data* mst_device = NULL;
    struct nnt_device* nnt_device = NULL;
    int* buffer_used = NULL;
    char* buffer = NULL;
    int minor = 0;
    int error = 0;

    /* Get the nnt device structure */
    error = get_nnt_device(file, &nnt_device);
    if (error)
    {
        minor = iminor(file_inode(file));
        mst_device = &mst_devices[minor];
        buffer = mst_device->buffer;
        buffer_used = &mst_device->buffer_used;
    }
    else
    {
        buffer = nnt_device->buffer_bc;
        buffer_used = &nnt_device->buffer_used_bc;
        error = mutex_lock_nnt(file);
        CHECK_ERROR(error);
    }

    if (*f_pos >= MST_BC_BUFFER_SIZE)
    {
        count = 0;
        goto MutexUnlock;
    }

    if (*f_pos + count > MST_BC_BUFFER_SIZE)
    {
        count = MST_BC_BUFFER_SIZE - *f_pos;
    }

    if (copy_from_user(buffer + *f_pos, buf, count))
    {
        count = -EFAULT;
        goto MutexUnlock;
    }

    *f_pos += count;

    if (*buffer_used < *f_pos)
    {
        *buffer_used = *f_pos;
    }

MutexUnlock:
    if (nnt_device)
    {
        mutex_unlock_nnt(file);
    }

ReturnOnFinished:
    return count;
}

static inline int noncached_address(unsigned long addr)
{
    return addr >= __pa(high_memory);
}

static int mst_pci_mmap(struct file* file, struct vm_area_struct* vma)
{
    struct nnt_device* nnt_device = NULL;
    unsigned long long offset = 0;
    unsigned long vsize = 0;
    unsigned long off = 0;
    int error = 0;

    /* Get the nnt device structure */
    error = get_nnt_device(file, &nnt_device);
    if (error)
    {
        goto ReturnOnFinished;
    }

    off = vma->vm_pgoff << PAGE_SHIFT;
    vsize = vma->vm_end - vma->vm_start;

    if ((nnt_device->device_pci.bar_size <= off) || (nnt_device->device_pci.bar_size < off + vsize))
    {
        error = -EINVAL;
        goto ReturnOnFinished;
    }

    offset = nnt_device->device_pci.bar_address + off;

    /* Accessing memory above the top the kernel knows about or through
     a file pointer that was marked O_SYNC will be done non-cached. */
    if (noncached_address(offset) || (file->f_flags & O_SYNC))
    {
        vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    }

    error = io_remap_pfn_range(vma, vma->vm_start, offset >> PAGE_SHIFT, vsize, vma->vm_page_prot);

ReturnOnFinished:
    return error;
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
        if (error)
        {
            return 0;
        }
        CHECK_ERROR(error);

        /* Get the nnt device structure */
        error = get_nnt_device(file, &nnt_device);
        if (error)
        {
            error = 0;
            goto ReturnOnFinished;
        }
    }

    switch (command)
    {
        case INIT:
        {
            struct mst_pci_init_st mst_init;
            struct pci_bus* bus = NULL;

            /* Copy the request from user space. */
            if (copy_from_user(&mst_init, user_buffer, sizeof(struct mst_pci_init_st)))
            {
                return -EFAULT;
            }

            error = set_private_data_bc(file, mst_init.bus, mst_init.devfn, mst_init.domain);
            if (error)
            {
                return 0;
            }

            error = mutex_lock_nnt(file);
            CHECK_ERROR(error);

            /* Get the nnt device structure */
            error = get_nnt_device(file, &nnt_device);
            if (error)
            {
                goto ReturnOnFinished;
            }

            bus = pci_find_bus(mst_init.domain, mst_init.bus);

            if (!bus)
            {
                printk(KERN_ERR "unable to find pci bus for domain: %x and bus: %x\n", mst_init.domain, mst_init.bus);
                error = -ENXIO;
                goto ReturnOnFinished;
            }

            nnt_device->pci_device = NULL;
            nnt_device->pci_device = pci_get_slot(bus, mst_init.devfn);

            if (!nnt_device->pci_device)
            {
                printk(KERN_ERR "missing pci device");
                error = -ENXIO;
                goto ReturnOnFinished;
            }

            if (mst_init.bar >= DEVICE_COUNT_RESOURCE)
            {
                printk(KERN_ERR "bar offset is too large");
                error = -ENXIO;
                goto ReturnOnFinished;
            }

            nnt_device->device_pci.bar_address = nnt_device->pci_device->resource[mst_init.bar].start;
            nnt_device->device_pci.bar_size = nnt_device->pci_device->resource[mst_init.bar].end + 1 -
                                              nnt_device->pci_device->resource[mst_init.bar].start;

            if (nnt_device->device_pci.bar_size == 1)
            {
                nnt_device->device_pci.bar_size = 0;
            }

            nnt_device->buffer_used_bc = 0;
            break;
        }

        case PCI_PARAMS_:
        {
            struct mst_pci_params_st params;
            params.bar = nnt_device->device_pci.bar_address;
            params.size = nnt_device->device_pci.bar_size;

            if (copy_to_user(user_buffer, &params, sizeof(struct mst_pci_params_st)))
            {
                error = -EFAULT;
                goto ReturnOnFinished;
            }

            break;
        }
        case CONNECTX_WA:
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
        case STOP:
            error = destroy_nnt_device_bc(nnt_device);
            break;
        default:
            error = -EINVAL;
            break;
    }

ReturnOnFinished:
    mutex_unlock_nnt(file);

    return error;
}

static int mst_release(struct inode* inode, struct file* file)
{
    struct nnt_device* nnt_device = NULL;
    int error = 0;

    /* Get the nnt device structure */
    error = get_nnt_device(file, &nnt_device);
    if (error)
    {
        error = 0;
        goto ReturnOnFinished;
    }

    if (nnt_device->memory_device.connectx_wa_slot_p1)
    {
        unsigned int mask = 0;

        error = mutex_lock_nnt(file);
        CHECK_ERROR(error);

        mask = ~(1 << (nnt_device->memory_device.connectx_wa_slot_p1 - 1));

        nnt_device->memory_device.connectx_wa_slot_p1 &= mask; // Fix me

        nnt_device->memory_device.connectx_wa_slot_p1 = 0;
        mutex_unlock_nnt(file);
    }

ReturnOnFinished:
    return 0;
}

struct file_operations fop = {.unlocked_ioctl = ioctl,
                              .open = mst_pci_bc_open,
                              .write = mst_pci_bc_write,
                              .read = mst_pci_bc_read,
                              .mmap = mst_pci_mmap,
                              .release = mst_release,
                              .owner = THIS_MODULE};

int with_unknown = 0;

module_param(with_unknown, int, S_IRUSR | S_IWUSR);

static int __init mst_pci_init_module(void)
{
    dev_t device_number = -1;
    int is_alloc_chrdev_region = 0;
    int error = 0;

    /* Allocate char driver region and assign major number */
    major_number = register_chrdev(0, name, &fop);
    if (major_number <= 0)
    {
        printk(KERN_ERR "Unable to register character mst pci driver.\n");
        error = -EINVAL;
    }

    /* Create device files for MFT. */
    error = create_nnt_devices(device_number, is_alloc_chrdev_region, &fop, NNT_PCI_DEVICES, NNT_MELLANOX_PCI_VENDOR,
                               with_unknown) ||
            create_nnt_devices(device_number, is_alloc_chrdev_region, &fop, NNT_PCI_DEVICES, NNT_NVIDIA_PCI_VENDOR,
                               with_unknown);

    return error;
}

static void __exit mst_pci_cleanup_module(void)
{
    int is_alloc_chrdev_region = 0;

    destroy_nnt_devices(is_alloc_chrdev_region);
    unregister_chrdev(major_number, name);
}

module_init(mst_pci_init_module);
module_exit(mst_pci_cleanup_module);
