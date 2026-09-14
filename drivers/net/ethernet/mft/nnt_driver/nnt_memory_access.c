#include "nnt_device_defs.h"
#include "nnt_pci_conf_access_defs.h"
#include "nnt_pci_conf_access.h"
#include "nnt_defs.h"


int write_memory(struct nnt_device* nnt_device, struct nnt_rw_operation* write_operation)
{
    /* Endianness conversion. */
    cpu_to_be32s(write_operation->data);
    write_operation->data[0] = cpu_to_le32(write_operation->data[0]);

    /* Write to the hardware memory address. */
    iowrite32(write_operation->data[0], nnt_device->memory_device.hardware_memory_address + write_operation->offset);

    return 0;
}



int read_memory(struct nnt_device* nnt_device, struct nnt_rw_operation* read_operation)
{
    /* Read from the hardware memory address. */
    read_operation->data[0] = ioread32(nnt_device->memory_device.hardware_memory_address + read_operation->offset);

    /* Endianness conversion */
    be32_to_cpus(read_operation->data);
    read_operation->data[0] = cpu_to_le32(read_operation->data[0]);

    return 0;
}


int init_memory(struct nnt_device* nnt_device)
{
    nnt_device->memory_device.connectx_wa_slot_p1 = 0;
    nnt_device->memory_device.hardware_memory_address =
        ioremap(pci_resource_start(nnt_device->pci_device, nnt_device->memory_device.pci_memory_bar_address),
                                   NNT_MEMORY_SIZE);

    if (nnt_device->memory_device.hardware_memory_address <= 0) {
            printk(KERN_ERR "could not map device memory\n");
    }

    return 0;
}
