#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/uaccess.h>
#include <linux/highmem.h>
#include <linux/sched.h>
#include "nnt_dma.h"
#include "nnt_defs.h"
#include "nnt_pci_conf_access.h"


unsigned int mask = 0x7fff;
unsigned int msb_mask = 0x8000;

int dma_pages_ioctl(unsigned int command, void* user_buffer,
                    struct nnt_device* nnt_device)
{
    struct nnt_page_info page_info;
    int error_code = 0;


    /* Copy the page info structure from user space. */
    if (copy_from_user(&page_info, user_buffer,
                       sizeof(struct nnt_page_info))) {
        error_code = -EFAULT;
        goto ReturnOnFinished;
    }

    if (command == NNT_GET_DMA_PAGES) {
        if (map_dma_pages(&page_info, nnt_device)) {
            goto ReturnOnFinished;
        }

        /* Return the physical address to the user */
        if (copy_to_user(user_buffer, &page_info,
                         sizeof(struct nnt_page_info)) != 0) {
            error_code = -EFAULT;
            goto ReturnOnFinished;
        }
    } else {
        error_code =
            release_dma_pages(&page_info, nnt_device);
    }

ReturnOnFinished:
    return error_code;
}



int read_dword_ioctl(void* user_buffer, struct nnt_device* nnt_device)
{
    struct nnt_read_dword_from_config_space read_from_cspace;
    int error_code = 0;


    /* Copy the request from user space. */
    if (copy_from_user(&read_from_cspace, user_buffer,
                sizeof(struct nnt_read_dword_from_config_space)) != 0) {
        error_code = -EFAULT;
        goto ReturnOnFinished;
    }

    /* Read the dword. */
    if (read_dword(&read_from_cspace, nnt_device)) {
        goto ReturnOnFinished;
    }

    /* Copy the data to the user space. */
    if (copy_to_user(user_buffer, &read_from_cspace,
                sizeof(struct nnt_read_dword_from_config_space)) != 0) {
        error_code = -EFAULT;
        goto ReturnOnFinished;
    }

ReturnOnFinished:
    return error_code;
}


int get_nnt_device_parameters(struct nnt_device_parameters* nnt_parameters, struct nnt_device* nnt_device)
{
    int error = 0;

    nnt_parameters->domain = pci_domain_nr(nnt_device->pci_device->bus);
    nnt_parameters->bus = nnt_device->pci_device->bus->number;
    nnt_parameters->slot = PCI_SLOT(nnt_device->pci_device->devfn);
    nnt_parameters->function = PCI_FUNC(nnt_device->pci_device->devfn);
    nnt_parameters->pci_memory_bar_address= nnt_device->memory_device.pci_memory_bar_address;
    nnt_parameters->device = nnt_device->pci_device->device;
    nnt_parameters->vendor = nnt_device->pci_device->vendor;
    nnt_parameters->subsystem_device = nnt_device->pci_device->subsystem_device;
    nnt_parameters->multifunction = nnt_device->pci_device->multifunction;
    nnt_parameters->subsystem_vendor = nnt_device->pci_device->subsystem_vendor;

    check_address_space_support(nnt_device);
    if (nnt_device->pciconf_device.vendor_specific_capability &&
            (nnt_device->pciconf_device.address_space.icmd || nnt_device->pciconf_device.address_space.cr_space ||
             nnt_device->pciconf_device.address_space.semaphore)) {
        nnt_parameters->vendor_specific_capability = nnt_device->pciconf_device.vendor_specific_capability;
        nnt_parameters->vsec_capability_mask = nnt_device->pciconf_device.vsec_capability_mask;
    } else {
        nnt_parameters->vendor_specific_capability = 0;
    }

    return error;
}


int pci_connectx_wa(struct nnt_connectx_wa* connectx_wa, struct nnt_device* nnt_device)
{
    unsigned int slot_mask;
    int error = 0;

    /* Is this slot exists ? */
    if (nnt_device->memory_device.connectx_wa_slot_p1) {
            printk(KERN_DEBUG "Slot exits for file %s, slot:0x%x\n",
                      nnt_device->device_name, nnt_device->memory_device.connectx_wa_slot_p1);
            error = 0;
            goto ReturnOnFinished;
    }

    /* Find first un(set) bit. and remember the slot */
    nnt_device->memory_device.connectx_wa_slot_p1= ffs(~nnt_device->memory_device.connectx_wa_slot_p1);
    if (nnt_device->memory_device.connectx_wa_slot_p1 == 0 ||
            nnt_device->memory_device.connectx_wa_slot_p1 > NNT_CONNECTX_WA_SIZE) {
            error = -ENOLCK;
            goto ReturnOnFinished;
    }

    slot_mask = 1 << (nnt_device->memory_device.connectx_wa_slot_p1 - 1);

    /* set the slot as taken */
    nnt_device->memory_device.connectx_wa_slot_p1 |= slot_mask;

    connectx_wa->connectx_wa_slot_p1 = nnt_device->memory_device.connectx_wa_slot_p1;

ReturnOnFinished:
    return error;
}


int vpd_read(struct nnt_vpd* vpd, struct nnt_device* nnt_device)
{
	unsigned long jiffies_time;
	unsigned int address;
    unsigned short data;
	int is_bit_set = 0;
    int error;

	/* Sets F bit to zero and write VPD address. */
	address = mask & vpd->offset;
	error = pci_write_config_word(nnt_device->pci_device, nnt_device->vpd_capability_address + PCI_VPD_ADDR,
                                  address);
    CHECK_PCI_WRITE_ERROR(error, nnt_device->vpd_capability_address + PCI_VPD_ADDR,
                          address);

	/* Wait for data until F bit is set with one */
	jiffies_time = msecs_to_jiffies(vpd->timeout) + jiffies;
	while (time_before(jiffies, jiffies_time)) {
		    error = pci_read_config_word(nnt_device->pci_device, nnt_device->vpd_capability_address + PCI_VPD_ADDR,
                                         &data);
            CHECK_PCI_READ_ERROR(error, nnt_device->vpd_capability_address + PCI_VPD_ADDR);

            if (data & msb_mask) {
                    is_bit_set = 1;
                    break;
            }

		cond_resched();
	}

	if (!is_bit_set) {
            printk(KERN_ERR "Failed to retrieve valid data\n");
            return -ETIMEDOUT;
    }

	/* read data */
	error = pci_read_config_dword(nnt_device->pci_device, nnt_device->vpd_capability_address + PCI_VPD_DATA,
                                  &vpd->data);
    CHECK_PCI_READ_ERROR(error, nnt_device->vpd_capability_address + PCI_VPD_DATA);

ReturnOnFinished:
	return error;
}


int vpd_write(struct nnt_vpd* vpd, struct nnt_device* nnt_device)
{
	unsigned long jiffies_time;
	unsigned int address;
    unsigned short data;
	int is_bit_set = 0;
    int error;

	/* Write the user data */
	error = pci_write_config_dword(nnt_device->pci_device, nnt_device->vpd_capability_address + PCI_VPD_DATA,
                                   vpd->data);
    CHECK_PCI_WRITE_ERROR(error, nnt_device->vpd_capability_address + PCI_VPD_DATA,
                          vpd->data);

	/* sets F bit to one and write VPD addr */
	address = msb_mask | (mask & vpd->offset);
	error = pci_write_config_word(nnt_device->pci_device, nnt_device->vpd_capability_address + PCI_VPD_ADDR,
                                  address);
    CHECK_PCI_WRITE_ERROR(error, nnt_device->vpd_capability_address + PCI_VPD_ADDR,
                          address);

	/* wait for data until F bit is set with zero */
	jiffies_time = msecs_to_jiffies(vpd->timeout) + jiffies;
	while (time_before(jiffies, jiffies_time)) {
		error = pci_read_config_word(nnt_device->pci_device, nnt_device->vpd_capability_address + PCI_VPD_ADDR,
                                     &data);
        CHECK_PCI_READ_ERROR(error, nnt_device->vpd_capability_address + PCI_VPD_ADDR);

		if (!(data & msb_mask)) {
			is_bit_set = 1;
			break;
		}

		cond_resched();
	}

	if (!is_bit_set) {
        printk(KERN_ERR "Failed to retrieve valid data\n");
		return -ETIMEDOUT;
    }

ReturnOnFinished:
	return error;
}
