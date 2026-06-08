#ifndef NNT_DRIVER_PPC_H
#define NNT_DRIVER_PPC_H

LIST_HEAD(nnt_device_list);

#define NNT_MAXIMUM_DEVICE_NAME_LENGTH  128
#define NNT_MAXIMUM_NUMBER_OF_DEVICES   8
#define NNT_MAXIMUM_POLLING_NUMBER      100
#define NNT_UNKNOWN_DEVICE_ID           0xffff
#define NNT_MINIMUM_WAITING_TIME        100
#define NNT_DEVICE_LIST_SIZE            NNT_MAXIMUM_DEVICE_NAME_LENGTH * NNT_MAXIMUM_NUMBER_OF_DEVICES

struct nnt_ppc_device {
    struct list_head entry;
    struct pci_dev* pci_device;
    char* pci_device_dbdf_name;
};


struct nnt_ppc_reset_info {
    unsigned int number_of_found_pci_device;
    unsigned int number_of_requested_pci_device;
    int reset_was_done;
};

#endif
