#include "nnt_device_defs.h"
#include "nnt_pci_conf_access_no_vsec.h"
#include "nnt_defs.h"


int read_no_vsec(struct nnt_device* nnt_device, unsigned int offset,
                 unsigned int* data)
{
    int error = 0;
    
    if (nnt_device->wo_address) {
        offset |= 0x1;
    }

    /* Write the wanted address to address register. */
    error = pci_write_config_dword(nnt_device->pci_device, nnt_device->pciconf_device.address_register,
                                   offset);
    CHECK_PCI_WRITE_ERROR(error, nnt_device->pciconf_device.address_register,
                          offset);

	/* Read the result from data register */
    error = pci_read_config_dword(nnt_device->pci_device, nnt_device->pciconf_device.data_register,
                                  data);
    CHECK_PCI_READ_ERROR(error, nnt_device->pciconf_device.data_register);

ReturnOnFinished:
        return error;
}


int read_pciconf_no_vsec(struct nnt_device* nnt_device, struct nnt_rw_operation* read_operation)
{
    int counter = 0;
    int error = 0;

    for (counter = 0; counter < read_operation->size; counter += 4) {
            if (read_no_vsec(nnt_device, read_operation->offset + counter,
                                  &read_operation->data[counter >> 2])) {
                    error = counter;
                    goto ReturnOnFinished;
            }
	}

ReturnOnFinished:
    return error;
}


int write_no_vsec(struct nnt_device* nnt_device, unsigned int offset,
                  unsigned int data)
{
    int error = 0;

    if (nnt_device->wo_address) {
            /* write the data to the data register. */
            error = pci_write_config_dword(nnt_device->pci_device, nnt_device->pciconf_device.data_register,
                                   data);
            CHECK_PCI_WRITE_ERROR(error, nnt_device->pciconf_device.data_register,
                                  data);
                /* Write the destination address to address register. */
            error = pci_write_config_dword(nnt_device->pci_device, nnt_device->pciconf_device.address_register,
                                           offset);
            CHECK_PCI_WRITE_ERROR(error, nnt_device->pciconf_device.address_register,
                                  offset);
    } else {
            /* Write the destination address to address register. */
            error = pci_write_config_dword(nnt_device->pci_device, nnt_device->pciconf_device.address_register,
                                           offset);
            CHECK_PCI_WRITE_ERROR(error, nnt_device->pciconf_device.address_register,
                                  offset);

            /* write the data to the data register. */
            error = pci_write_config_dword(nnt_device->pci_device, nnt_device->pciconf_device.data_register,
                                           data);
            CHECK_PCI_WRITE_ERROR(error, nnt_device->pciconf_device.data_register,
                                  data);
    }

ReturnOnFinished:
        return error;
}


int write_pciconf_no_vsec(struct nnt_device* nnt_device, struct nnt_rw_operation* write_operation)
{
    int counter = 0;
    int error = 0;

	for (counter = 0; counter < write_operation->size; counter += 4) {
            if (write_no_vsec(nnt_device, write_operation->offset + counter,
                              write_operation->data[counter >> 2])) {
                    error = counter;
                    goto ReturnOnFinished;
		    }
	}

ReturnOnFinished:
    return error;
}


int is_wo_gw(struct nnt_device* nnt_device)
{
	unsigned int data = 0;
    int error = 0;

    error = pci_write_config_dword(nnt_device->pci_device, nnt_device->pciconf_device.address_register,
                                   NNT_DEVICE_ID_OFFSET);
    CHECK_PCI_WRITE_ERROR(error, nnt_device->pciconf_device.address_register,
                          NNT_DEVICE_ID_OFFSET);

	/* Read the result from data register */
    error = pci_read_config_dword(nnt_device->pci_device, nnt_device->pciconf_device.address_register,
                                  &data);
    CHECK_PCI_READ_ERROR(error, nnt_device->pciconf_device.address_register);

	if (data == NNT_WO_REG_ADDR_DATA) {
		    error = 1;
    }

ReturnOnFinished:
	return error;
}


int init_pciconf_no_vsec(struct nnt_device* nnt_device)
{
    nnt_device->pciconf_device.address_register = NNT_CONF_ADDRES_REGISETER;
    nnt_device->pciconf_device.data_register = NNT_CONF_DATA_REGISTER;
    nnt_device->wo_address = is_wo_gw(nnt_device);
    return 0;
}
