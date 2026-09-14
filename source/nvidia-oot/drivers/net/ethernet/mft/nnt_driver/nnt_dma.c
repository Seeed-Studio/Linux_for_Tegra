#include <linux/kernel.h>
#include <linux/pci.h>
#include "nnt_device_defs.h"
#include "nnt_pci_conf_access_defs.h"
#include "nnt_pci_conf_access.h"
#include "nnt_defs.h"
#include "nnt_ioctl_defs.h"

int dma_mapping_page(unsigned int total_pinned, unsigned int page_mapped_counter,
                     struct nnt_device* nnt_device, struct page** current_pages,
                     struct nnt_page_info* page_info)
{
    int current_page = total_pinned + page_mapped_counter;
    int error_code = 0;

    /* Get the dma address. */
    nnt_device->dma_page.dma_address_list[current_page] =
        dma_map_page(&nnt_device->pci_device->dev, current_pages[current_page],
                     0, PAGE_SIZE,
                     DMA_BIDIRECTIONAL);
    /* Do we get a valid dma address ? */
    if (dma_mapping_error(&nnt_device->pci_device->dev, nnt_device->dma_page.dma_address_list[current_page])) {
            printk(KERN_ERR "Failed to get DMA addresses\n");
            error_code = -EINVAL;
            goto ReturnOnFinsihed;
    }

    page_info->page_address_array[current_page].dma_address =
        nnt_device->dma_page.dma_address_list[current_page];

ReturnOnFinsihed:
    return error_code;
}




int pin_user_pages_in_kernel_space(unsigned int total_pages, unsigned int total_pinned,
                                   int* pinned_pages, struct nnt_device* nnt_device,
                                   struct nnt_page_info* page_info, struct page*** current_pages)
{
    unsigned long page_pointer_start = page_info->page_pointer_start;
    int error_code = 0;


    /* Remaining pages to pin. */
    int remaining_pages = total_pages - total_pinned;

    /* Point to the current offset. */
    uint64_t current_page_pointer = page_pointer_start + (total_pinned * PAGE_SIZE);
    
    /* Save the current page. */
    *current_pages = nnt_device->dma_page.page_list + total_pinned;

    /* Returns number of pages pinned - this may be fewer than the number requested
         or -errno in case of error. */
    *pinned_pages = get_user_pages_fast(current_page_pointer, remaining_pages,
                                        FOLL_WRITE, *current_pages);

    if (*pinned_pages < 1) {
            kfree(nnt_device->dma_page.page_list);
            error_code = -EFAULT;
    }

    /* Allocate dma addresses structure. */
    if ((nnt_device->dma_page.dma_address_list =
                kcalloc(total_pages, sizeof(dma_addr_t), GFP_KERNEL)) == NULL) {
            error_code = -ENOMEM;
    }

    return error_code;
}




int pin_user_memory_in_kernel_space(unsigned int total_pages, struct nnt_device* nnt_device,
                                    struct nnt_page_info* page_info)
{
    unsigned int page_mapped_counter= 0;
    unsigned int total_pinned = 0;
    unsigned int pinned_pages;
    struct page** current_pages = NULL;
    int error_code = 0;

    while (total_pinned < total_pages) {
            /* Pinning user pages in kernel space. */
            if((error_code =
                pin_user_pages_in_kernel_space(total_pages, total_pinned,
                                               &pinned_pages, nnt_device,
                                               page_info, &current_pages)) != 0)
                goto ReturnOnFinished;

            /* When the parameter 'inter_iommu' is on, we need to set up
                 a mapping on a pages in order to access the physical address. */
            while(page_mapped_counter < pinned_pages)
            {
                    if((error_code =
                        dma_mapping_page(total_pinned, page_mapped_counter,
                                         nnt_device, current_pages,
                                         page_info)) != 0)
                        goto ReturnOnFinished;

                    page_mapped_counter++;
            }

            /* Advance the memory that already pinned. */
            total_pinned += pinned_pages;
    }

        /* There is a page not pinned in the kernel space ? */
    if (total_pinned != total_pages) {
            return -EFAULT;
    }

ReturnOnFinished:
   return error_code;
}




int map_dma_pages(struct nnt_page_info* page_info, struct nnt_device* nnt_device)
{
    unsigned int total_pages = page_info->total_pages;
    int page_counter = 0;
    int error_code = 0;


    /* Check if we allow locking memory. */
    if (!can_do_mlock()) {
            error_code = -EPERM;
            goto ReturnOnFinished;
    }

    /* Allocate the page list. */
    if ((nnt_device->dma_page.page_list =
                kcalloc(total_pages, sizeof(struct page *), GFP_KERNEL)) == NULL) {
            error_code = -ENOMEM;
            goto ReturnOnFinished;
    }

    /* Go over the user memory buffer and pin user pages in kernel space */
    if((error_code =
            pin_user_memory_in_kernel_space(total_pages, nnt_device,
                                            page_info)) != 0)
            goto ReturnOnFinished;

    for (page_counter = 0;
            page_counter < total_pages;
            page_counter++) {
            printk(KERN_INFO "Page address structure number: %d, device: %04x:%02x:%02x.%0x\n",
                   page_counter, pci_domain_nr(nnt_device->pci_device->bus),
                   nnt_device->pci_device->bus->number, PCI_SLOT(nnt_device->pci_device->devfn),
                   PCI_FUNC(nnt_device->pci_device->devfn));
    }

ReturnOnFinished:
    return error_code;
}



int release_dma_pages(struct nnt_page_info* page_info, struct nnt_device* nnt_device)
{
    int page_counter;

    if (nnt_device->dma_page.page_list == NULL) {
        return 0;
    }

    /* Deallocate the pages. */
    for (page_counter = 0;
            page_counter < page_info->total_pages;
            page_counter++) {
            /* DMA activity is finished. */
            dma_unmap_page(&nnt_device->pci_device->dev, nnt_device->dma_page.dma_address_list[page_counter],
                           PAGE_SIZE, DMA_BIDIRECTIONAL);

            /* Release the page list. */
            set_page_dirty(nnt_device->dma_page.page_list[page_counter]);
            put_page(nnt_device->dma_page.page_list[page_counter]);
            nnt_device->dma_page.page_list[page_counter] = NULL;
            nnt_device->dma_page.dma_address_list[page_counter] = 0;

            printk(KERN_INFO "Page structure number: %d was released. device:%04x:%02x:%02x.%0x\n",
                   page_counter, pci_domain_nr(nnt_device->pci_device->bus),
                   nnt_device->pci_device->bus->number, PCI_SLOT(nnt_device->pci_device->devfn),
                   PCI_FUNC(nnt_device->pci_device->devfn));
    }

    // All the pages are clean.
    nnt_device->dma_page.page_list = NULL;

    return 0;
}
