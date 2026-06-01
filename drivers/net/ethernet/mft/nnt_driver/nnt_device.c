#include "nnt_device.h"
#include "nnt_device_defs.h"
#include "nnt_defs.h"
#include "nnt_pci_conf_access.h"
#include "nnt_pci_conf_access_no_vsec.h"
#include "nnt_memory_access.h"
#include <linux/module.h>

MODULE_LICENSE("GPL");

/* Device list to check if device is available
     since it could be removed by hotplug event. */
LIST_HEAD(nnt_device_list);

int get_nnt_device(struct file* file, struct nnt_device** nnt_device)
{
    int error_code = 0;

    if (!file->private_data)
    {
        error_code = -EINVAL;
    }
    else
    {
        *nnt_device = file->private_data;
    }

    return error_code;
}

void set_private_data_open(struct file* file)
{
    struct nnt_device* current_nnt_device = NULL;
    struct nnt_device* temp_nnt_device = NULL;
    int minor = iminor(file_inode(file));

    /* Set private data to nnt structure. */
    list_for_each_entry_safe(current_nnt_device, temp_nnt_device, &nnt_device_list, entry)
    {
        if ((minor == current_nnt_device->device_minor_number) && current_nnt_device->device_enabled)
        {
            file->private_data = current_nnt_device;
            return;
        }
    }
}

int set_private_data_bc(struct file* file, unsigned int bus, unsigned int devfn, unsigned int domain)
{
    struct nnt_device* current_nnt_device = NULL;
    struct nnt_device* temp_nnt_device = NULL;
    int minor = iminor(file_inode(file));
    unsigned int current_function;
    unsigned int current_device;

    /* Set private data to nnt structure. */
    list_for_each_entry_safe(current_nnt_device, temp_nnt_device, &nnt_device_list, entry)
    {
        struct pci_bus* pci_bus = pci_find_bus(current_nnt_device->dbdf.domain, current_nnt_device->dbdf.bus);
        if (!pci_bus)
        {
            return -ENXIO;
        }

        current_nnt_device->pci_device = pci_get_slot(pci_bus, current_nnt_device->dbdf.devfn);
        if (!current_nnt_device->pci_device)
        {
            return -ENXIO;
        }

        current_function = PCI_FUNC(current_nnt_device->dbdf.devfn);
        current_device = PCI_SLOT(current_nnt_device->dbdf.devfn);

        if ((current_nnt_device->dbdf.bus == bus) && (current_device == PCI_SLOT(devfn)) &&
            (current_function == PCI_FUNC(devfn)) && (current_nnt_device->dbdf.domain == domain))
        {
            current_nnt_device->device_minor_number = minor;
            current_nnt_device->device_enabled = true;
            file->private_data = current_nnt_device;
            return 0;
        }
    }

    return -EINVAL;
}

int set_private_data(struct file* file)
{
    struct nnt_device* current_nnt_device = NULL;
    struct nnt_device* temp_nnt_device = NULL;
    int minor = iminor(file_inode(file));

    /* Set private data to nnt structure. */
    list_for_each_entry_safe(current_nnt_device, temp_nnt_device, &nnt_device_list, entry)
    {
        if (current_nnt_device->device_minor_number == minor)
        {
            file->private_data = current_nnt_device;
            return 0;
        }
    }

    printk(KERN_ERR "failed to find device with minor=%d\n", minor);

    return -EINVAL;
}

int create_file_name_mstflint(struct pci_dev* pci_device, struct nnt_device* nnt_dev, enum nnt_device_type device_type)
{
    sprintf(nnt_dev->device_name, "%4.4x:%2.2x:%2.2x.%1.1x_%s", pci_domain_nr(pci_device->bus), pci_device->bus->number,
            PCI_SLOT(pci_device->devfn), PCI_FUNC(pci_device->devfn),
            (device_type == NNT_PCICONF) ? MSTFLINT_PCICONF_DEVICE_NAME : MSTFLINT_MEMORY_DEVICE_NAME);

    printk(KERN_DEBUG
           "MSTFlint device name created: id: %d, slot id: %d, device name: /dev/%s domain: 0x%x bus: 0x%x\n",
           pci_device->device, PCI_FUNC(pci_device->devfn), nnt_dev->device_name, pci_domain_nr(pci_device->bus),
           pci_device->bus->number);

    return 0;
}

int create_file_name_mft(struct pci_dev* pci_device, struct nnt_device* nnt_dev, enum nnt_device_type device_type)
{
    sprintf(nnt_dev->device_name, "mst/mt%d_%s0.%x", pci_device->device,
            (device_type == NNT_PCICONF) ? MFT_PCICONF_DEVICE_NAME : MFT_MEMORY_DEVICE_NAME,
            PCI_FUNC(pci_device->devfn));

    printk(KERN_DEBUG "MFT device name created: id: %d, slot id: %d, device name: /dev/%s domain: 0x%x bus: 0x%x\n",
           pci_device->device, PCI_FUNC(pci_device->devfn), nnt_dev->device_name, pci_domain_nr(pci_device->bus),
           pci_device->bus->number);

    return 0;
}

int nnt_device_structure_init(struct nnt_device** nnt_device)
{
    /* Allocate nnt device structure. */
    *nnt_device = kzalloc(sizeof(struct nnt_device), GFP_KERNEL);

    if (!(*nnt_device))
    {
        return -ENOMEM;
    }

    /* initialize nnt structure. */
    memset(*nnt_device, 0, sizeof(struct nnt_device));

    return 0;
}

int create_nnt_device(struct pci_dev* pci_device, enum nnt_device_type device_type, int is_alloc_chrdev_region)
{
    struct nnt_device* nnt_device = NULL;
    int error_code = 0;

    /* Allocate nnt device info structure. */
    if ((error_code = nnt_device_structure_init(&nnt_device)) != 0)
        goto ReturnOnError;

    if (is_alloc_chrdev_region)
    {
        /* Build the device file name of MSTFlint. */
        if ((error_code = create_file_name_mstflint(pci_device, nnt_device, device_type)) != 0)
            goto ReturnOnError;
    }
    else
    {
        /* Build the device file name of MFT. */
        if ((error_code = create_file_name_mft(pci_device, nnt_device, device_type)) != 0)
            goto ReturnOnError;
    }

    nnt_device->dbdf.bus = pci_device->bus->number;
    nnt_device->dbdf.devfn = pci_device->devfn;
    nnt_device->dbdf.domain = pci_domain_nr(pci_device->bus);
    nnt_device->pci_device = pci_device;
    nnt_device->device_type = device_type;

    /* Add the nnt device structure to the list. */
    list_add_tail(&nnt_device->entry, &nnt_device_list);

    return error_code;

ReturnOnError:
    if (nnt_device)
    {
        kfree(nnt_device);
    }

    return error_code;
}

int check_pci_id_range(unsigned short pci_device_id, unsigned short id_range_start)
{
    return (pci_device_id >= id_range_start) && (pci_device_id <= (id_range_start + 100));
}

int is_connectx(unsigned short pci_device_id)
{
    return check_pci_id_range(pci_device_id, CONNECTX3_PCI_ID);
}

int is_connectx3(unsigned short pci_device_id)
{
    return pci_device_id == CONNECTX3_PCI_ID || pci_device_id == CONNECTX3PRO_PCI_ID;
}

int is_bluefield(unsigned short pci_device_id)
{
    return check_pci_id_range(pci_device_id, BLUEFIELD_PCI_ID) ||
           check_pci_id_range(pci_device_id, BLUEFIELD_DPU_AUX_PCI_ID);
}

int is_pcie_switch(unsigned short pci_device_id)
{
    return check_pci_id_range(pci_device_id, SCHRODINGER_PCI_ID);
}

int is_quantum(unsigned short pci_device_id)
{
    return check_pci_id_range(pci_device_id, QUANTUM_PCI_ID);
}

int is_spectrum(unsigned short pci_device_id)
{
    return (pci_device_id == SPECTRUM_PCI_ID) || (check_pci_id_range(pci_device_id, SPECTRUM2_PCI_ID));
}

int is_switch_ib(unsigned short pci_device_id)
{
    return pci_device_id == SWITCHIB_PCI_ID || pci_device_id == SWITCHIB2_PCI_ID;
}

int is_bw00(unsigned short pci_device_id)
{
    return check_pci_id_range(pci_device_id, BW00_PCI_ID);
}

int is_bw02(unsigned short pci_device_id)
{
    return check_pci_id_range(pci_device_id, BW02_PCI_ID);
}

int is_livefish_device(unsigned short pci_device_id)
{
    return pci_device_id >= CONNECTX3_LIVEFISH_ID && pci_device_id < CONNECTX3_PCI_ID;
}

int is_nic(unsigned short pci_device_id)
{
    return is_connectx(pci_device_id) || is_bluefield(pci_device_id);
}

int is_switch(unsigned short pci_device_id)
{
    return is_pcie_switch(pci_device_id) || is_quantum(pci_device_id) || is_spectrum(pci_device_id) ||
           is_switch_ib(pci_device_id) || is_bw00(pci_device_id) || is_bw02(pci_device_id);
}

int is_toolspf(unsigned short pci_device_id)
{
    return is_nic(pci_device_id - 4000) || is_switch(pci_device_id - 4000);
}

int is_pciconf_device(unsigned short pci_device_id)
{
    return is_nic(pci_device_id) || is_toolspf(pci_device_id) || is_livefish_device(pci_device_id) ||
           is_switch(pci_device_id);
}

int is_pcicr_device(unsigned short pci_device_id)
{
    return (is_switch(pci_device_id) || is_toolspf(pci_device_id) || is_connectx3(pci_device_id)) &&
           (!is_livefish_device(pci_device_id));
}

int create_device_file(struct nnt_device* current_nnt_device,
                       dev_t device_number,
                       int minor,
                       struct file_operations* fop,
                       int is_alloc_chrdev_region)
{
    struct device* device = NULL;
    int error = 0;
    int count = 1;

    /* NNT driver will create the device file
         once we stop support backward compatibility. */
    current_nnt_device->device_minor_number = -1;
    current_nnt_device->device_number = device_number;
    current_nnt_device->mcdev.owner = THIS_MODULE;

    mutex_init(&current_nnt_device->lock);

    if (!is_alloc_chrdev_region)
    {
        goto ReturnOnFinished;
    }

    // Create device with a new minor number.
    current_nnt_device->device_minor_number = minor;
    current_nnt_device->device_number = MKDEV(MAJOR(device_number), minor);

    current_nnt_device->device_enabled = true;
    current_nnt_device->connectx_wa_slot_p1 = 0;

    /* Create device node. */
    device = device_create(nnt_driver_info.class_driver, NULL, current_nnt_device->device_number, NULL,
                           current_nnt_device->device_name);
    if (!device)
    {
        printk(KERN_ERR "Device creation failed\n");
        error = -EINVAL;
        goto ReturnOnFinished;
    }

    /* Init new device. */
    cdev_init(&current_nnt_device->mcdev, fop);

    /* Add device to the system. */
    error = cdev_add(&current_nnt_device->mcdev, current_nnt_device->device_number, count);
    if (error)
    {
        goto ReturnOnFinished;
    }

ReturnOnFinished:
    return error;
}

int check_if_vsec_supported(struct nnt_device* nnt_device)
{
    int error = 0;

    error = nnt_device->access.init(nnt_device);
    CHECK_ERROR(error);

    if (!nnt_device->pciconf_device.vsec_fully_supported)
    {
        nnt_device->device_type = NNT_PCICONF_NO_VSEC;
        nnt_device->access.read = read_pciconf_no_vsec;
        nnt_device->access.write = write_pciconf_no_vsec;
        nnt_device->access.init = init_pciconf_no_vsec;
    }

ReturnOnFinished:
    return error;
}

int is_mellanox_vendor_type(struct nnt_device* current_nnt_device, unsigned int vsec_address)
{
    u_int8_t vendor_type = 0;
    int error = 0;
    /* Read the capability type field */
    if ((error = pci_read_config_byte(current_nnt_device->pci_device, (vsec_address + PCI_TYPE_OFFSET), &vendor_type)))
    {
        printk(KERN_ERR "Reading VSEC type failed with error %d\n", error);
        return 0;
    }

    if (vendor_type == MELLANOX_VSEC_TYPE)
    {
        return 1;
    }
    return 0;
}

unsigned int get_mellanox_vsec_address(struct nnt_device* current_nnt_device)
{
    unsigned int vsec_address = 0;

    /* Look for the Mellanox VSEC address. if Mellanox VSEC isn't supported the address should be 0 */
    vsec_address = pci_find_capability(current_nnt_device->pci_device, VSEC_CAPABILITY_ADDRESS);
    if (!vsec_address || is_mellanox_vendor_type(current_nnt_device, vsec_address))
    {
        return vsec_address;
    }

    /* if found a non-Mellanox type VSEC, iterate of the next available VSECs*/
    while (
      (vsec_address = pci_find_next_capability(current_nnt_device->pci_device, vsec_address, VSEC_CAPABILITY_ADDRESS)))
    {
        if (is_mellanox_vendor_type(current_nnt_device, vsec_address))
        {
            return vsec_address;
        }
    }
    /* if Mellanox VSEC address was not found, return 0 */
    return 0;
}

int create_devices(dev_t device_number, struct file_operations* fop, int is_alloc_chrdev_region)
{
    struct nnt_device* current_nnt_device = NULL;
    struct nnt_device* temp_nnt_device = NULL;
    int minor = 0;
    int error = 0;

    /* Create necessary number of the devices. */
    list_for_each_entry_safe(current_nnt_device, temp_nnt_device, &nnt_device_list, entry)
    {
        /* Create the device file. */
        if ((error = create_device_file(current_nnt_device, device_number, minor, fop, is_alloc_chrdev_region)) != 0)
            goto ReturnOnFinished;

        /* Members initialization. */
        current_nnt_device->pciconf_device.vendor_specific_capability = get_mellanox_vsec_address(current_nnt_device);
        current_nnt_device->vpd_capability_address =
          pci_find_capability(current_nnt_device->pci_device, PCI_CAP_ID_VPD);

        if (!current_nnt_device->pciconf_device.vendor_specific_capability)
        {
            current_nnt_device->device_type = NNT_PCICONF_NO_VSEC;
        }

        switch (current_nnt_device->device_type)
        {
            case NNT_PCICONF:
                current_nnt_device->access.read = read_pciconf;
                current_nnt_device->access.write = write_pciconf;
                current_nnt_device->access.init = init_pciconf;

                error = check_if_vsec_supported(current_nnt_device);
                CHECK_ERROR(error);
                break;

            case NNT_PCICONF_NO_VSEC:
                current_nnt_device->access.read = read_pciconf_no_vsec;
                current_nnt_device->access.write = write_pciconf_no_vsec;
                current_nnt_device->access.init = init_pciconf_no_vsec;
                break;

            case NNT_PCI_MEMORY:
                current_nnt_device->access.read = read_memory;
                current_nnt_device->access.write = write_memory;
                current_nnt_device->access.init = init_memory;
                break;
        }

        if (is_alloc_chrdev_region)
        {
            error = current_nnt_device->access.init(current_nnt_device);
        }

        minor++;
    }

ReturnOnFinished:
    return error;
}

int create_nnt_devices(dev_t device_number,
                       int is_alloc_chrdev_region,
                       struct file_operations* fop,
                       enum nnt_device_type_flag nnt_device_flag,
                       unsigned int vendor_id,
                       int with_unknown)
{
    struct pci_dev* pci_device = NULL;
    int error_code = 0;

    /* Find all Nvidia PCI devices. */
    while ((pci_device = pci_get_device(vendor_id, PCI_ANY_ID, pci_device)) != NULL)
    {
        if ((nnt_device_flag == NNT_PCICONF_DEVICES) || (nnt_device_flag == NNT_ALL_DEVICES))
        {
            /* Create pciconf device. */
            if (with_unknown || is_pciconf_device(pci_device->device))
            {
                if ((error_code = create_nnt_device(pci_device, NNT_PCICONF, is_alloc_chrdev_region)) != 0)
                {
                    printk(KERN_ERR "Failed to create pci conf device\n");
                    goto ReturnOnFinished;
                }
            }
        }

        if ((nnt_device_flag == NNT_PCI_DEVICES) || (nnt_device_flag == NNT_ALL_DEVICES))
        {
            /* Create pci memory device. */
            if (with_unknown || is_pcicr_device(pci_device->device))
            {
                if ((error_code = create_nnt_device(pci_device, NNT_PCI_MEMORY, is_alloc_chrdev_region)) != 0)
                {
                    printk(KERN_ERR "Failed to create pci memory device\n");
                    goto ReturnOnFinished;
                }
            }
        }
    }

    /* Create the devices. */
    if ((error_code = create_devices(device_number, fop, is_alloc_chrdev_region)) != 0)
    {
        return error_code;
    }

ReturnOnFinished:
    return error_code;
}

int find_all_vendor_devices(unsigned int vendor_id)
{
    struct pci_dev* pci_device = NULL;
    int contiguous_device_numbers = 0;
    while ((pci_device = pci_get_device(vendor_id, PCI_ANY_ID, pci_device)) != NULL)
    {
        contiguous_device_numbers++;
    }
    return contiguous_device_numbers;
}

int get_amount_of_nvidia_devices(void)
{
    int contiguous_device_numbers = 0;
    /* Find all Mellanox & Nvidia PCI devices. */
    contiguous_device_numbers +=
      find_all_vendor_devices(NNT_MELLANOX_PCI_VENDOR) + find_all_vendor_devices(NNT_NVIDIA_PCI_VENDOR);
    return contiguous_device_numbers;
}

int mutex_lock_nnt(struct file* file)
{
    struct nnt_device* nnt_device;

    if (!file)
    {
        return 1;
    }

    nnt_device = file->private_data;

    if (!nnt_device)
    {
        return -EINVAL;
    }

    mutex_lock(&nnt_device->lock);

    return 0;
}

void mutex_unlock_nnt(struct file* file)
{
    struct nnt_device* nnt_device = file->private_data;

    if (nnt_device)
    {
        mutex_unlock(&nnt_device->lock);
    }
}

void destroy_nnt_devices(int is_alloc_chrdev_region)
{
    struct nnt_device* current_nnt_device;
    struct nnt_device* temp_nnt_device;

    /* free all nnt_devices */
    list_for_each_entry_safe(current_nnt_device, temp_nnt_device, &nnt_device_list, entry)
    {
        /* Character device is no longer, it must be properly destroyed. */
        if (is_alloc_chrdev_region)
        {
            cdev_del(&current_nnt_device->mcdev);
            device_destroy(nnt_driver_info.class_driver, current_nnt_device->device_number);
        }

        list_del(&current_nnt_device->entry);
        kfree(current_nnt_device);
    }
}

void destroy_nnt_devices_bc(void)
{
    struct nnt_device* current_nnt_device;
    struct nnt_device* temp_nnt_device;

    /* free all nnt_devices */
    list_for_each_entry_safe(current_nnt_device, temp_nnt_device, &nnt_device_list, entry)
    {
        /* Character device is no longer, it must be properly destroyed. */
        list_del(&current_nnt_device->entry);
        kfree(current_nnt_device);
    }
}

int destroy_nnt_device_bc(struct nnt_device* nnt_device)
{
    struct nnt_device* current_nnt_device;
    struct nnt_device* temp_nnt_device;
    unsigned int current_function;
    unsigned int current_device;

    /* Set private data to nnt structure. */
    list_for_each_entry_safe(current_nnt_device, temp_nnt_device, &nnt_device_list, entry)
    {
        struct pci_bus* pci_bus = pci_find_bus(current_nnt_device->dbdf.domain, current_nnt_device->dbdf.bus);
        if (!pci_bus)
        {
            return -ENXIO;
        }

        current_nnt_device->pci_device = pci_get_slot(pci_bus, current_nnt_device->dbdf.devfn);
        if (!current_nnt_device->pci_device)
        {
            return -ENXIO;
        }

        current_function = PCI_FUNC(current_nnt_device->dbdf.devfn);
        current_device = PCI_SLOT(current_nnt_device->dbdf.devfn);

        if ((current_nnt_device->dbdf.bus == nnt_device->dbdf.bus) &&
            (current_device == PCI_SLOT(nnt_device->dbdf.devfn)) &&
            (current_function == PCI_FUNC(nnt_device->dbdf.devfn)) &&
            (current_nnt_device->dbdf.domain == nnt_device->dbdf.domain))
        {
            /* Character device is no longer, it must be properly disabled. */
            current_nnt_device->device_enabled = false;
            printk(KERN_DEBUG "Device removed: domain: %d, bus: %d, device:%d, function:%d \n",
                   current_nnt_device->dbdf.domain, current_nnt_device->dbdf.bus, current_device, current_function);
            return 0;
        }
    }

    return 0;
}
