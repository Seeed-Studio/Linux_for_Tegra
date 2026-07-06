#ifndef NNT_IOCTL_H
#define NNT_IOCTL_H

#include "nnt_device_defs.h"

int dma_pages_ioctl(unsigned int command, void* user_buffer,
                    struct nnt_device* nnt_device);
int read_dword_ioctl(unsigned int command, void* user_buffer,
                     struct nnt_device* nnt_device);
int get_nnt_device_parameters(struct nnt_device_parameters* nnt_parameters, struct nnt_device* nnt_device);
int pci_connectx_wa(struct nnt_connectx_wa* connectx_wa, struct nnt_device* nnt_device);
int vpd_read(struct nnt_vpd* vpd, struct nnt_device* nnt_device);
int vpd_write(struct nnt_vpd* vpd, struct nnt_device* nnt_device);
#endif
