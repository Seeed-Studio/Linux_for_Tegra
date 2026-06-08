#include <linux/pci.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/delay.h>
#include "nnt_ppc_device_list.h"
#include "nnt_ppc_driver_defs.h"
#include "nnt_defs.h"

MODULE_AUTHOR("Itay Avraham <itayavr@nvidia.com>");
MODULE_DESCRIPTION("NNT PPC driver (NVIDIA® networking tools driver)");
MODULE_LICENSE("Dual BSD/GPL");


/* Passing PCI devices (DBDF addresses), separated by comma, for example:
 * 0000:00:08.0,0000:00:08.1 */
char pci_device_list[NNT_DEVICE_LIST_SIZE];

/* Create the file in sysfs. */
module_param_string(pci_dev, pci_device_list, sizeof(pci_device_list), 0444);


struct nnt_ppc_reset_info nnt_ppc_reset;


void restore_pci_configuration_space(void)
{
    struct nnt_ppc_device* nnt_pci_device;

    list_for_each_entry(nnt_pci_device, &nnt_device_list,
                            entry) {
            /* Restore the saved state of a PCI device. */
            pci_restore_state(nnt_pci_device->pci_device);
    }
}


int wait_for_response(void)
{
    unsigned short device_id = NNT_UNKNOWN_DEVICE_ID;
    struct nnt_ppc_device* nnt_pci_device;
    int polling_counter = 0;
    int error = 0;

    list_for_each_entry(nnt_pci_device, &nnt_device_list,
                        entry) {
            struct pci_dev* pci_device = nnt_pci_device->pci_device;

            /* Device id still unknown ? */
            while(device_id != pci_device->device) {
                    /* 100ms is the minimum time that prevents error logs on 
                         dmesg (device is not ready for PCI configuration cycles). */
                    msleep(NNT_MINIMUM_WAITING_TIME);

                    /* Read the device id.
                       Access can fail (if device is not ready) and
                         as a result we might get errors in dmesg. */
                    pci_read_config_word(pci_device, PCI_DEVICE_ID,
                                         &device_id);

                    /* Polling counter violation. */
                    if (polling_counter > NNT_MAXIMUM_POLLING_NUMBER) {
                        printk(KERN_ERR "%s Polling on device id failed: reached max value of polling failures for device: %s\n",
                                   dev_driver_string(&pci_device->dev), dev_name(&pci_device->dev));
                        error = -EINVAL;
                        goto ReturnOnFinished;
                    }
        
                    polling_counter++;
            }
    }

ReturnOnFinished:
    return error;
}


int set_reset_state(enum pcie_reset_state state)
{
    struct nnt_ppc_device* nnt_pci_device;
    int error = 0;

    list_for_each_entry(nnt_pci_device, &nnt_device_list,
                        entry) {
            struct pci_dev* pci_device = nnt_pci_device->pci_device;

            if (PCI_FUNC(pci_device->devfn) == 0) {
                    /* Set reset state for device devce. */
                    printk(KERN_DEBUG "%s Send hot reset to the device: %s\n",dev_driver_string(&pci_device->dev), dev_name(&pci_device->dev));
                    error = pci_set_pcie_reset_state(pci_device, state);
                    if (error) {
                            printk(KERN_ERR "%s Set reset state for device failed for device: %s - error: %d\n",
                                      dev_driver_string(&pci_device->dev), dev_name(&pci_device->dev), error);
                        goto ReturnOnFinished;
                    }
        }
    }

ReturnOnFinished:
    return error;
}


int save_pci_configucation_space(void)
{
    struct nnt_ppc_device* nnt_pci_device = NULL;
    int error = 0;

    list_for_each_entry(nnt_pci_device, &nnt_device_list,
                        entry) {
            struct pci_dev* pci_device = nnt_pci_device->pci_device;

            /* Initialize device before it's used by a driver. */
            error = pci_enable_device(pci_device);
            if (error) {
                    printk(KERN_ERR "%s Reset failed for device: %s - error: %d\n",
                              dev_driver_string(&pci_device->dev), dev_name(&pci_device->dev), error);
                goto ReturnOnFinished;
            }

            /* Enables bus-mastering for device device. */
            pci_set_master(pci_device);

            /* Save the PCI configuration space of a device before sending hot reset. */
            error = pci_save_state(pci_device);
            if (error) {
                    printk(KERN_ERR "%s Reset failed for device: %s - error: %d\n",
                              dev_driver_string(&pci_device->dev), dev_name(&pci_device->dev), error);
                    goto ReturnOnFinished;
            }
    }

ReturnOnFinished:
    return error;
}


int pci_devices_reset(void)
{
    int error = 0;

    if (nnt_ppc_reset.reset_was_done) {
        goto ReturnOnFinished;
    }

    /* Save configuration space for all devices. */
    error = save_pci_configucation_space();
    CHECK_ERROR(error);

    /* Disable the link by sending the hot reset. */
    error = set_reset_state(pcie_hot_reset);
    CHECK_ERROR(error);

    msleep(jiffies_to_msecs(HZ));
    
    /* Enable the link by sending the hot reset. */
    error = set_reset_state(pcie_deassert_reset);
    CHECK_ERROR(error);

    /* Wait for the device to response to PCI configuration cycles. */
    error = wait_for_response();
    CHECK_ERROR(error);

    /* Restore PCI configuration space for all PCI devices. */
    restore_pci_configuration_space();

    nnt_ppc_reset.reset_was_done = 1;

ReturnOnFinished:
    return error;
}


static int init_pci_device(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct nnt_ppc_device* nnt_pci_device;

    list_for_each_entry(nnt_pci_device, &nnt_device_list,
                        entry) {
            if (!strcmp(nnt_pci_device->pci_device_dbdf_name, dev_name(&pdev->dev))) {
                    nnt_pci_device->pci_device = pdev;
                    nnt_ppc_reset.number_of_found_pci_device++;
            }
    }

    if (nnt_ppc_reset.number_of_requested_pci_device == nnt_ppc_reset.number_of_found_pci_device) {
            return pci_devices_reset();
    }

    return 0;
}


static void remove_pci_device(struct pci_dev *pdev)
{
    struct nnt_ppc_device* nnt_pci_device;

    list_for_each_entry(nnt_pci_device, &nnt_device_list,
                        entry) {
            if (!strcmp(nnt_pci_device->pci_device_dbdf_name, dev_name(&pdev->dev))) {
                    pci_clear_master(pdev);
                    pci_disable_device(pdev);
                    return;
            }
    }
}


int ppc_device_structure_init(struct nnt_ppc_device** nnt_pci_device, unsigned int pci_device_name_length)
{
    /* Allocate nnt device structure. */
    *nnt_pci_device=
            kzalloc(sizeof(struct nnt_ppc_device),GFP_KERNEL);

    if (!(*nnt_pci_device)) {
            return -ENOMEM;
    }

    /* initialize nnt structure. */
    memset(*nnt_pci_device, 0, sizeof(struct nnt_ppc_device));

    (*nnt_pci_device)->pci_device_dbdf_name =
            kzalloc(pci_device_name_length,GFP_KERNEL);

    if (!(*nnt_pci_device)->pci_device_dbdf_name) {
            return -ENOMEM;
    }

    return 0;
}


int parse_pci_devices_string(void)
{
    struct nnt_ppc_device* nnt_pci_device;
    char buffer[NNT_DEVICE_LIST_SIZE];
    char* pci_device_dbdf_name = NULL;
    char* dbdf_list = NULL;
    int error;

    strncpy(buffer, pci_device_list, NNT_DEVICE_LIST_SIZE);
    dbdf_list = buffer;

    /* Add the pci device name (DBDF) to the list. */
    while ((pci_device_dbdf_name = strsep(&dbdf_list, ",")) != NULL) {
            /* Allocate ppc device info structure. */
            unsigned int pci_device_name_length = strlen(pci_device_dbdf_name);
            nnt_pci_device = NULL; 

            error = ppc_device_structure_init(&nnt_pci_device, pci_device_name_length);
            CHECK_ERROR(error);

            /* Copy the device name string. */
            strncpy(nnt_pci_device->pci_device_dbdf_name, pci_device_dbdf_name,
                    pci_device_name_length);

            /* Create a device entry in the list. */
            list_add_tail(&nnt_pci_device->entry, &nnt_device_list);
            nnt_ppc_reset.number_of_requested_pci_device++;
    }

ReturnOnFinished:
    return error;
}


void init_members(void)
{
    memset(&nnt_ppc_reset, 0, sizeof(struct nnt_ppc_reset_info));
}


static struct pci_driver nnt_ppc_driver = {
    .name       = "nnt_ppc_driver",
    .id_table   = pciconf_devices,
    .probe      = init_pci_device,
    .remove     = remove_pci_device,
};

static int __init init(void)
{
    int error;

    init_members();

    /* Parse the parameters from the user space. */
    error = parse_pci_devices_string();
    CHECK_ERROR(error);

    /* Register the NNT PPC driver. */
    return pci_register_driver(&nnt_ppc_driver);

ReturnOnFinished:
    return error;
}


static void __exit cleanup(void)
{
    /* Unregister the NNT PPC driver. */
    pci_unregister_driver(&nnt_ppc_driver);
}


module_init(init);
module_exit(cleanup);
