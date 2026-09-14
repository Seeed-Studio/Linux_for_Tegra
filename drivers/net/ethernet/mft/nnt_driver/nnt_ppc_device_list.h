#ifndef NNT_DEVICE_LIST_H
#define NNT_DEVICE_LIST_H

#include "nnt_device_defs.h"

static struct pci_device_id pciconf_devices[] = {{PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, CONNECTX3_PCI_ID)},
                                                 {PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, CONNECTX3PRO_PCI_ID)},
                                                 {PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, CONNECTIB_PCI_ID)},
                                                 {PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, CONNECTX4_PCI_ID)},
                                                 {PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, CONNECTX4LX_PCI_ID)},
                                                 {PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, CONNECTX5_PCI_ID)},
                                                 {PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, CONNECTX5EX_PCI_ID)},
                                                 {PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, CONNECTX6_PCI_ID)},
                                                 {PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, CONNECTX6DX_PCI_ID)},
                                                 {PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, CONNECTX6LX_PCI_ID)},
                                                 {PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, CONNECTX7_PCI_ID)},
                                                 {PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, CONNECTX8_PCI_ID)},
                                                 {PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, SCHRODINGER_PCI_ID)},
                                                 {PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, FREYSA_PCI_ID)},
                                                 {PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, BLUEFIELD_PCI_ID)},
                                                 {PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, BLUEFIELD2_PCI_ID)},
                                                 {PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, BLUEFIELD3_PCI_ID)},
                                                 {PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, BLUEFIELD4_PCI_ID)},
                                                 {PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, SWITCHIB_PCI_ID)},
                                                 {PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, SWITCHIB2_PCI_ID)},
                                                 {PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, QUANTUM_PCI_ID)},
                                                 {PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, QUANTUM2_PCI_ID)},
                                                 {PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, QUANTUM3_PCI_ID)},
                                                 {PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, SPECTRUM_PCI_ID)},
                                                 {PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, SPECTRUM2_PCI_ID)},
                                                 {PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, SPECTRUM3_PCI_ID)},
                                                 {PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, SPECTRUM4_PCI_ID)},
                                                 {PCI_DEVICE(NNT_MELLANOX_PCI_VENDOR, BW00_PCI_ID)},
                                                 {
                                                   0,
                                                 }};

#endif // NNT_DEVICE_LIST_H