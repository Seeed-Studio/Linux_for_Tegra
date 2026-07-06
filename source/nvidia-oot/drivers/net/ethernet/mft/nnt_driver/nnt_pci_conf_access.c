#include "nnt_device_defs.h"
#include "nnt_pci_conf_access_defs.h"
#include "nnt_pci_conf_access.h"
#include "nnt_defs.h"
#include <linux/delay.h>
#include <linux/uaccess.h>


int clear_vsec_semaphore(struct nnt_device* nnt_device)
{
    /* Clear the semaphore. */
    int error = pci_write_config_dword(nnt_device->pci_device, nnt_device->pciconf_device.semaphore_offset,
                                      0);
    CHECK_PCI_WRITE_ERROR(error, nnt_device->pciconf_device.semaphore_offset,
                          0)

ReturnOnFinished:
    return error;
}


int get_semaphore_ticket(struct nnt_device* nnt_device, unsigned int* lock_value,
                         unsigned int* counter)
{
    unsigned int counter_offset = nnt_device->pciconf_device.vendor_specific_capability + PCI_COUNTER_OFFSET;
    int error = 0;

    /* Read ticket. */
    error = pci_read_config_dword(nnt_device->pci_device, counter_offset,
                                  counter);
    CHECK_PCI_READ_ERROR(error, counter_offset);

    /* Write to semaphore ticket. */
    error = pci_write_config_dword(nnt_device->pci_device, nnt_device->pciconf_device.semaphore_offset,
                                   *counter);
    CHECK_PCI_WRITE_ERROR(error, nnt_device->pciconf_device.semaphore_offset,
                          *counter);

    /* Read back semaphore to make sure the
     *   ticket is equal to semphore */
    error = pci_read_config_dword(nnt_device->pci_device, nnt_device->pciconf_device.semaphore_offset,
                                  lock_value);
    CHECK_PCI_READ_ERROR(error, nnt_device->pciconf_device.semaphore_offset);

ReturnOnFinished:
    return error;
}




int lock_vsec_semaphore(struct nnt_device* nnt_device)
{
    unsigned int lock_value = -1;
    unsigned int counter = 0;
    unsigned int retries = 0;
    int error = 0;

    do {
            if (retries > SEMAPHORE_MAX_RETRIES) {
                    return -EINVAL;
            }

            /* Read the semaphore, until we will get 0. */
            error = pci_read_config_dword(nnt_device->pci_device, nnt_device->pciconf_device.semaphore_offset,
                                          &lock_value);

            CHECK_PCI_READ_ERROR(error, nnt_device->pciconf_device.semaphore_offset);

            /* Is semaphore taken ? */
            if (lock_value) {
                    retries++;
                    udelay(1000);
                    continue;
            }

            /* Get semaphore ticket */
            error = get_semaphore_ticket(nnt_device, &lock_value,
                                         &counter);
            CHECK_ERROR(error);
    } while (counter != lock_value);

ReturnOnFinished:
    return error;
}




int read_dword(struct nnt_read_dword_from_config_space* read_from_cspace, struct nnt_device* nnt_device)
{
    int error = 0;

    /* Take semaphore. */
    error = lock_vsec_semaphore(nnt_device);
    CHECK_ERROR(error);

    /* Read dword from config space. */
    error = pci_read_config_dword(nnt_device->pci_device, read_from_cspace->offset,
                                  &read_from_cspace->data);
    CHECK_PCI_READ_ERROR(error, read_from_cspace->offset);

ReturnOnFinished:
    /* Clear semaphore. */
    clear_vsec_semaphore(nnt_device);
    return error;
}


int wait_on_flag(struct nnt_device* nnt_device, u8 expected_val)
{
	unsigned int flag = 0;
	int retries = 0;
	int error = -1;
    
    for (retries = 0; retries < IFC_MAX_RETRIES; retries++) {
            /* Read the flag. */
            error = pci_read_config_dword(nnt_device->pci_device, nnt_device->pciconf_device.address_offset,
                                          &flag);
            CHECK_PCI_READ_ERROR(error, nnt_device->pciconf_device.address_offset);

            flag = EXTRACT(flag, PCI_FLAG_BIT_OFFSET,
                           1);
            if (flag == expected_val) {
                    return 0;
            }
    }

ReturnOnFinished:
	return error;
}


int set_address_space(struct nnt_device* nnt_device, unsigned int address_space)
{
    unsigned int control_offset = nnt_device->pciconf_device.vendor_specific_capability + PCI_CONTROL_OFFSET;
	unsigned int value = 0;
	int error = 0;

    /* Read value from control offset. */
    error = pci_read_config_dword(nnt_device->pci_device, control_offset,
                                  &value);
    CHECK_PCI_READ_ERROR(error, control_offset);
    
    /* Set the bit address_space indication and write it back. */
    value = MERGE(value, address_space,
                  PCI_SPACE_BIT_OFFSET, PCI_SPACE_BIT_LENGTH);
    error = pci_write_config_dword(nnt_device->pci_device, control_offset,
                                   value);
    CHECK_PCI_WRITE_ERROR(error, control_offset,
                          value);
    
    /* Read status and make sure address_space is supported. */
    error = pci_read_config_dword(nnt_device->pci_device, control_offset,
                                  &value);
    CHECK_PCI_READ_ERROR(error, control_offset);

    if (EXTRACT(value, PCI_STATUS_BIT_OFFSET,
                    PCI_STATUS_BIT_LEN) == 0) {
        error = -EINVAL;
    }

ReturnOnFinished:
    return error;
}


int check_address_space_support(struct nnt_device* nnt_device)
{
    int error = 0;

    if ((!nnt_device->pciconf_device.vendor_specific_capability) || (!nnt_device->pci_device)) {
		    return 0;
    }
    
    /* Get semaphore ticket */
    error = lock_vsec_semaphore(nnt_device);
    CHECK_ERROR(error);

    /* Is ICMD address space supported ?*/
    if(set_address_space(nnt_device, ADDRESS_SPACE_ICMD) == 0) {
            nnt_device->pciconf_device.address_space.icmd = 1;
    }

    /* Is CR Space address space supported ?*/
    if(set_address_space(nnt_device, ADDRESS_SPACE_CR_SPACE) == 0) {
            nnt_device->pciconf_device.address_space.cr_space = 1;
    }

    /* Is semaphore address space supported ?*/
    if(set_address_space(nnt_device, ADDRESS_SPACE_SEMAPHORE) == 0) {
            nnt_device->pciconf_device.address_space.semaphore = 1;
    }

ReturnOnFinished:
    /* Clear semaphore. */
    clear_vsec_semaphore(nnt_device);

    return 0;
}



int set_rw_address(unsigned int* offset, unsigned int rw)
{
    u32 address = *offset;

    /* Last 2 bits must be zero as we only allow 30 bits addresses. */
    if (EXTRACT(address, 30,
                    2)) {
            return -1;
    }

    address = MERGE(address, rw,
                    PCI_FLAG_BIT_OFFSET, 1);
    *offset = address;

    return 0;
}




int read(struct nnt_device* nnt_device, unsigned int offset,
         unsigned int* data)
{
    int error = set_rw_address(&offset, READ_OPERATION);
    CHECK_ERROR(error);

    /* Write address. */
    error = pci_write_config_dword(nnt_device->pci_device, nnt_device->pciconf_device.address_offset,
                                   offset);
    CHECK_PCI_WRITE_ERROR(error, nnt_device->pciconf_device.address_offset,
                          offset);

    error = wait_on_flag(nnt_device, 1);
    CHECK_ERROR(error);

    /* Read data. */
    error = pci_read_config_dword(nnt_device->pci_device, nnt_device->pciconf_device.data_offset,
                                  data);
    CHECK_PCI_READ_ERROR(error, nnt_device->pciconf_device.data_offset);

ReturnOnFinished:
    return error;
}



int read_pciconf(struct nnt_device* nnt_device, struct nnt_rw_operation* read_operation)
{
    int counter = 0;
    int error = 0;

    /* Lock semaphore. */
    error = lock_vsec_semaphore(nnt_device);
    CHECK_ERROR(error);
    
    /* Is CR Space address space supported ?*/
    error = set_address_space(nnt_device, read_operation->address_space);
    CHECK_ERROR(error);

	for (counter = 0; counter < read_operation->size; counter += 4) {
            if (read(nnt_device, read_operation->offset + counter,
                     &read_operation->data[counter >> 2])) {
                    error = counter;
                    goto ReturnOnFinished;
		    }
	}

ReturnOnFinished:
    /* Clear semaphore. */
    clear_vsec_semaphore(nnt_device);

    return error;
}



int write(struct nnt_device* nnt_device, unsigned int offset,
          unsigned int data)
{
    int error = set_rw_address(&offset, WRITE_OPERATION);
    CHECK_ERROR(error);
    
    /* Write data. */
    error = pci_write_config_dword(nnt_device->pci_device, nnt_device->pciconf_device.data_offset,
                                   data);
    CHECK_PCI_WRITE_ERROR(error,nnt_device->pciconf_device.data_offset,
                          data);

    /* Write address. */
    error = pci_write_config_dword(nnt_device->pci_device, nnt_device->pciconf_device.address_offset,
                                   offset);
    CHECK_PCI_WRITE_ERROR(error, nnt_device->pciconf_device.address_offset,
                          offset);

    error = wait_on_flag(nnt_device, 0);

ReturnOnFinished:
    return error;
}


int write_pciconf(struct nnt_device* nnt_device, struct nnt_rw_operation* write_operation)
{
    int counter = 0;
    int error = 0;

    /* Lock semaphore. */
    error = lock_vsec_semaphore(nnt_device);
    CHECK_ERROR(error);
    
    /* Is CR Space address space supported ?*/
    error = set_address_space(nnt_device, write_operation->address_space);
    CHECK_ERROR(error);

	for (counter = 0; counter < write_operation->size; counter += 4) {
            if (write(nnt_device, write_operation->offset + counter,
                      write_operation->data[counter >> 2])) {
                    error = counter;
                    goto ReturnOnFinished;
		    }
	}

ReturnOnFinished:
    /* Clear semaphore. */
    clear_vsec_semaphore(nnt_device);

    return error;
}


int address_space_to_capability(u_int16_t address_space)
{
    switch (address_space) {
            case NNT_SPACE_ICMD:
                    return NNT_VSEC_ICMD_SPACE_SUPPORTED;
            case NNT_SPACE_CR_SPACE:
                    return NNT_VSEC_CRSPACE_SPACE_SUPPORTED;
            case NNT_SPACE_ALL_ICMD:
                    return NNT_VSEC_ALL_ICMD_SPACE_SUPPORTED;
            case NNT_SPACE_NODNIC_INIT_SEG:
                    return NNT_VSEC_NODNIC_INIT_SEG_SPACE_SUPPORTED;
            case NNT_SPACE_EXPANSION_ROM:
                    return NNT_VSEC_EXPANSION_ROM_SPACE_SUPPORTED;
            case NNT_SPACE_ND_CR_SPACE:
                    return NNT_VSEC_ND_CRSPACE_SPACE_SUPPORTED;
            case NNT_SPACE_SCAN_CR_SPACE:
                    return NNT_VSEC_SCAN_CRSPACE_SPACE_SUPPORTED;
            case NNT_SPACE_GLOBAL_SEMAPHORE:
                    return NNT_VSEC_GLOBAL_SEMAPHORE_SPACE_SUPPORTED;
            case NNT_SPACE_MAC:
                    return NNT_VSEC_MAC_SPACE_SUPPORTED;
            default:
                    return 0;
    }
}


int get_space_support_status(struct nnt_device* nnt_device, u_int16_t address_space)
{
    int status = 0;

    if(set_address_space(nnt_device, address_space) == 0) {
            status = 1;
    }

    nnt_device->pciconf_device.vsec_capability_mask |= 
            (status << address_space_to_capability(address_space));

    return status;
}


int init_vsec_capability_mask(struct nnt_device* nnt_device)
{
    int error = 0;
    
    /* Lock semaphore. */
    error = lock_vsec_semaphore(nnt_device);
    CHECK_ERROR(error);

    get_space_support_status(nnt_device, NNT_SPACE_ICMD);
    get_space_support_status(nnt_device, NNT_SPACE_CR_SPACE);
    get_space_support_status(nnt_device, NNT_SPACE_ALL_ICMD);
    get_space_support_status(nnt_device, NNT_SPACE_NODNIC_INIT_SEG);
    get_space_support_status(nnt_device, NNT_SPACE_EXPANSION_ROM);
    get_space_support_status(nnt_device, NNT_SPACE_ND_CR_SPACE);
    get_space_support_status(nnt_device, NNT_SPACE_SCAN_CR_SPACE);
    get_space_support_status(nnt_device, NNT_SPACE_GLOBAL_SEMAPHORE);
    get_space_support_status(nnt_device, NNT_SPACE_MAC);
    nnt_device->pciconf_device.vsec_capability_mask |= (1 << NNT_VSEC_INITIALIZED);

ReturnOnFinished:
    /* Clear semaphore. */
    clear_vsec_semaphore(nnt_device);

    return 0;
}


void check_vsec_minimum_support(struct nnt_device* nnt_device)
{
    if ((nnt_device->pciconf_device.vsec_capability_mask & (1 << NNT_VSEC_INITIALIZED)) &&
            (nnt_device->pciconf_device.vsec_capability_mask & (1 << NNT_VSEC_ICMD_SPACE_SUPPORTED)) &&
            (nnt_device->pciconf_device.vsec_capability_mask & (1 << NNT_VSEC_CRSPACE_SPACE_SUPPORTED)) &&
            (nnt_device->pciconf_device.vsec_capability_mask & (1 << NNT_VSEC_GLOBAL_SEMAPHORE_SPACE_SUPPORTED))) {

            nnt_device->pciconf_device.vsec_fully_supported = 1;
    }
}


int init_pciconf(struct nnt_device* nnt_device)
{
    int error = 0;

    nnt_device->pciconf_device.semaphore_offset =
        nnt_device->pciconf_device.vendor_specific_capability + PCI_SEMAPHORE_OFFSET;
    nnt_device->pciconf_device.data_offset =
        nnt_device->pciconf_device.vendor_specific_capability + PCI_DATA_OFFSET;
    nnt_device->pciconf_device.address_offset =
        nnt_device->pciconf_device.vendor_specific_capability + PCI_ADDRESS_OFFSET;

    error = init_vsec_capability_mask(nnt_device);
    check_vsec_minimum_support(nnt_device);

    return error;
}
