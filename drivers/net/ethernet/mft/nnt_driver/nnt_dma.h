#ifndef NNT_DMA_H
#define NNT_DMA_H

#include "nnt_ioctl_defs.h"
#include "nnt_device_defs.h"

int map_dma_pages(struct nnt_page_info* page_info, struct nnt_device* nnt_device);
int release_dma_pages(struct nnt_page_info* page_info, struct nnt_device* nnt_device);

#endif
