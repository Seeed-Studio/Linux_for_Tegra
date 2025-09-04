/*
 * SPDX-FileCopyrightText: Copyright (c) 2011-2019 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#define  __NO_VERSION__

#include "os-interface.h"
#include "nv-linux.h"
#include "nv-ibmnpu.h"
#include "nv-rsync.h"

#include "nv-p2p.h"
#include "rmp2pdefines.h"

typedef struct nv_p2p_dma_mapping {
    struct list_head list_node;
    struct nvidia_p2p_dma_mapping *dma_mapping;
} nv_p2p_dma_mapping_t;

typedef struct nv_p2p_mem_info {
    void (*free_callback)(void *data);
    void *data;
    struct nvidia_p2p_page_table page_table;
    struct {
        struct list_head list_head;
        struct semaphore lock;
    } dma_mapping_list;
    NvBool bPersistent;
    void *private;
} nv_p2p_mem_info_t;

int nvidia_p2p_cap_persistent_pages = 1;
EXPORT_SYMBOL(nvidia_p2p_cap_persistent_pages);

// declared and created in nv.c
extern void *nvidia_p2p_page_t_cache;

static struct nvidia_status_mapping {
    NV_STATUS status;
    int error;
} nvidia_status_mappings[] = {
    { NV_ERR_GENERIC,                -EIO      },
    { NV_ERR_INSUFFICIENT_RESOURCES, -ENOMEM   },
    { NV_ERR_NO_MEMORY,              -ENOMEM   },
    { NV_ERR_INVALID_ARGUMENT,       -EINVAL   },
    { NV_ERR_INVALID_OBJECT_HANDLE,  -EINVAL   },
    { NV_ERR_INVALID_STATE,          -EIO      },
    { NV_ERR_NOT_SUPPORTED,          -ENOTSUPP },
    { NV_ERR_OBJECT_NOT_FOUND,       -EINVAL   },
    { NV_ERR_STATE_IN_USE,           -EBUSY    },
    { NV_ERR_GPU_UUID_NOT_FOUND,     -ENODEV   },
    { NV_OK,                          0        },
};

#define NVIDIA_STATUS_MAPPINGS \
    (sizeof(nvidia_status_mappings) / sizeof(struct nvidia_status_mapping))

static int nvidia_p2p_map_status(NV_STATUS status)
{
    int error = -EIO;
    uint8_t i;

    for (i = 0; i < NVIDIA_STATUS_MAPPINGS; i++)
    {
        if (nvidia_status_mappings[i].status == status)
        {
            error = nvidia_status_mappings[i].error;
            break;
        }
    }
    return error;
}

static NvU32 nvidia_p2p_page_size_mappings[NVIDIA_P2P_PAGE_SIZE_COUNT] = {
    NVRM_P2P_PAGESIZE_SMALL_4K, NVRM_P2P_PAGESIZE_BIG_64K, NVRM_P2P_PAGESIZE_BIG_128K
};

static NV_STATUS nvidia_p2p_map_page_size(NvU32 page_size, NvU32 *page_size_index)
{
    NvU32 i;

    for (i = 0; i < NVIDIA_P2P_PAGE_SIZE_COUNT; i++)
    {
        if (nvidia_p2p_page_size_mappings[i] == page_size)
        {
            *page_size_index = i;
            break;
        }
    }

    if (i == NVIDIA_P2P_PAGE_SIZE_COUNT)
        return NV_ERR_GENERIC;

    return NV_OK;
}

static NV_STATUS nv_p2p_insert_dma_mapping(
    struct nv_p2p_mem_info *mem_info,
    struct nvidia_p2p_dma_mapping *dma_mapping
)
{
    NV_STATUS status;
    struct nv_p2p_dma_mapping *node;

    status = os_alloc_mem((void**)&node, sizeof(*node));
    if (status != NV_OK)
    {
        return status;
    }

    down(&mem_info->dma_mapping_list.lock);

    node->dma_mapping = dma_mapping;
    list_add_tail(&node->list_node, &mem_info->dma_mapping_list.list_head);

    up(&mem_info->dma_mapping_list.lock);

    return NV_OK;
}

static struct nvidia_p2p_dma_mapping* nv_p2p_remove_dma_mapping(
    struct nv_p2p_mem_info *mem_info,
    struct nvidia_p2p_dma_mapping *dma_mapping
)
{
    struct nv_p2p_dma_mapping *cur;
    struct nvidia_p2p_dma_mapping *ret_dma_mapping = NULL;

    down(&mem_info->dma_mapping_list.lock);

    list_for_each_entry(cur, &mem_info->dma_mapping_list.list_head, list_node)
    {
        if (dma_mapping == NULL || dma_mapping == cur->dma_mapping)
        {
            ret_dma_mapping = cur->dma_mapping;
            list_del(&cur->list_node);
            os_free_mem(cur);
            break;
        }
    }

    up(&mem_info->dma_mapping_list.lock);

    return ret_dma_mapping;
}

static void nv_p2p_free_dma_mapping(
    struct nvidia_p2p_dma_mapping *dma_mapping
)
{
    nv_dma_device_t peer_dma_dev = {{ 0 }};
    NvU32 page_size;
    NV_STATUS status;
    NvU32 i;

    peer_dma_dev.dev = &dma_mapping->pci_dev->dev;
    peer_dma_dev.addressable_range.limit = dma_mapping->pci_dev->dma_mask;

    page_size = nvidia_p2p_page_size_mappings[dma_mapping->page_size_type];

    if (dma_mapping->private != NULL)
    {
        WARN_ON(page_size != PAGE_SIZE);

        status = nv_dma_unmap_alloc(&peer_dma_dev,
                                    dma_mapping->entries,
                                    dma_mapping->dma_addresses,
                                    &dma_mapping->private);
        WARN_ON(status != NV_OK);
    }
    else
    {
        for (i = 0; i < dma_mapping->entries; i++)
        {
            nv_dma_unmap_peer(&peer_dma_dev, page_size / PAGE_SIZE,
                              dma_mapping->dma_addresses[i]);
        }
    }

    os_free_mem(dma_mapping->dma_addresses);

    os_free_mem(dma_mapping);
}

static void nv_p2p_free_page_table(
    struct nvidia_p2p_page_table *page_table
)
{
    NvU32 i;
    struct nvidia_p2p_dma_mapping *dma_mapping;
    struct nv_p2p_mem_info *mem_info = NULL;

    mem_info = container_of(page_table, nv_p2p_mem_info_t, page_table);

    dma_mapping = nv_p2p_remove_dma_mapping(mem_info, NULL);
    while (dma_mapping != NULL)
    {
        nv_p2p_free_dma_mapping(dma_mapping);

        dma_mapping = nv_p2p_remove_dma_mapping(mem_info, NULL);
    }

    for (i = 0; i < page_table->entries; i++)
    {
        NV_KMEM_CACHE_FREE(page_table->pages[i], nvidia_p2p_page_t_cache);
    }

    if (page_table->gpu_uuid != NULL)
    {
        os_free_mem(page_table->gpu_uuid);
    }

    if (page_table->pages != NULL)
    {
        os_free_mem(page_table->pages);
    }

    os_free_mem(mem_info);
}

static NV_STATUS nv_p2p_put_pages(
    nvidia_stack_t * sp,
    uint64_t p2p_token,
    uint32_t va_space,
    uint64_t virtual_address,
    struct nvidia_p2p_page_table **page_table
)
{
    NV_STATUS status;
    struct nv_p2p_mem_info *mem_info = NULL;

    mem_info = container_of(*page_table, nv_p2p_mem_info_t, page_table);

    /*
     * rm_p2p_put_pages returns NV_OK if the page_table was found and
     * got unlinked from the RM's tracker (atomically). This ensures that
     * RM's tear-down path does not race with this path.
     *
     * rm_p2p_put_pages returns NV_ERR_OBJECT_NOT_FOUND if the page_table
     * was already unlinked.
     */
    if (mem_info->bPersistent)
    {
        status = rm_p2p_put_pages_persistent(sp, mem_info->private, *page_table);
    }
    else
    {
        status = rm_p2p_put_pages(sp, p2p_token, va_space,
                                  virtual_address, *page_table);
    }

    if (status == NV_OK)
    {
        nv_p2p_free_page_table(*page_table);
        *page_table = NULL;
    }
    else if (!mem_info->bPersistent && (status == NV_ERR_OBJECT_NOT_FOUND))
    {
        status = NV_OK;
        *page_table = NULL;
    }
    else
    {
        WARN_ON(status != NV_OK);
    }

    return status;
}

void NV_API_CALL nv_p2p_free_platform_data(
    void *data
)
{
    if (data == NULL)
    {
        WARN_ON(data == NULL);
        return;
    }

    nv_p2p_free_page_table((struct nvidia_p2p_page_table*)data);
}

int nvidia_p2p_init_mapping(
    uint64_t p2p_token,
    struct nvidia_p2p_params *params,
    void (*destroy_callback)(void *data),
    void *data
)
{
    return -ENOTSUPP;
}

EXPORT_SYMBOL(nvidia_p2p_init_mapping);

int nvidia_p2p_destroy_mapping(uint64_t p2p_token)
{
    return -ENOTSUPP;
}

EXPORT_SYMBOL(nvidia_p2p_destroy_mapping);

static void nv_p2p_mem_info_free_callback(void *data)
{
    nv_p2p_mem_info_t *mem_info = (nv_p2p_mem_info_t*) data;

    mem_info->free_callback(mem_info->data);

    nv_p2p_free_platform_data(&mem_info->page_table);
}





























































































































































































































































































































































































































































int nvidia_p2p_register_rsync_driver(
    nvidia_p2p_rsync_driver_t *driver,
    void *data
)
{
    if (driver == NULL)
    {
        return -EINVAL;
    }

    if (!NVIDIA_P2P_RSYNC_DRIVER_VERSION_COMPATIBLE(driver))
    {
        return -EINVAL;
    }

    if (driver->get_relaxed_ordering_mode == NULL ||
        driver->put_relaxed_ordering_mode == NULL ||
        driver->wait_for_rsync == NULL)
    {
        return -EINVAL;
    }

    return nv_register_rsync_driver(driver->get_relaxed_ordering_mode,
                                    driver->put_relaxed_ordering_mode,
                                    driver->wait_for_rsync, data);
}

EXPORT_SYMBOL(nvidia_p2p_register_rsync_driver);

void nvidia_p2p_unregister_rsync_driver(
    nvidia_p2p_rsync_driver_t *driver,
    void *data
)
{
    if (driver == NULL)
    {
        WARN_ON(1);
        return;
    }

    if (!NVIDIA_P2P_RSYNC_DRIVER_VERSION_COMPATIBLE(driver))
    {
        WARN_ON(1);
        return;
    }

    if (driver->get_relaxed_ordering_mode == NULL ||
        driver->put_relaxed_ordering_mode == NULL ||
        driver->wait_for_rsync == NULL)
    {
        WARN_ON(1);
        return;
    }

    nv_unregister_rsync_driver(driver->get_relaxed_ordering_mode,
                               driver->put_relaxed_ordering_mode,
                               driver->wait_for_rsync, data);
}

EXPORT_SYMBOL(nvidia_p2p_unregister_rsync_driver);

int nvidia_p2p_get_rsync_registers(
    nvidia_p2p_rsync_reg_info_t **reg_info
)
{
    nv_linux_state_t *nvl;
    nv_state_t *nv;
    NV_STATUS status;
    void *ptr = NULL;
    NvU64 addr;
    NvU64 size;
    struct pci_dev *ibmnpu = NULL;
    NvU32 index = 0;
    NvU32 count = 0;
    nvidia_p2p_rsync_reg_info_t *info = NULL;
    nvidia_p2p_rsync_reg_t *regs = NULL;

    if (reg_info == NULL)
    {
        return -EINVAL;
    }

    status = os_alloc_mem((void**)&info, sizeof(*info));
    if (status != NV_OK)
    {
        return -ENOMEM;
    }

    memset(info, 0, sizeof(*info));

    info->version = NVIDIA_P2P_RSYNC_REG_INFO_VERSION;

    LOCK_NV_LINUX_DEVICES();

    for (nvl = nv_linux_devices; nvl; nvl = nvl->next)
    {
        count++;
    }

    status = os_alloc_mem((void**)&regs, (count * sizeof(*regs)));
    if (status != NV_OK)
    {
        nvidia_p2p_put_rsync_registers(info);
        UNLOCK_NV_LINUX_DEVICES();
        return -ENOMEM;
    }

    for (nvl = nv_linux_devices; nvl; nvl = nvl->next)
    {
        nv = NV_STATE_PTR(nvl);

        addr = 0;
        size = 0;

        status = nv_get_ibmnpu_genreg_info(nv, &addr, &size, (void**)&ibmnpu);
        if (status != NV_OK)
        {
            continue;
        }

        ptr = nv_ioremap_nocache(addr, size);
        if (ptr == NULL)
        {
            continue;
        }

        regs[index].ptr = ptr;
        regs[index].size = size;
        regs[index].gpu = nvl->pci_dev;
        regs[index].ibmnpu = ibmnpu;
        regs[index].cluster_id = 0;
        regs[index].socket_id = nv_get_ibmnpu_chip_id(nv);

        index++;
    }

    UNLOCK_NV_LINUX_DEVICES();

    info->regs = regs;
    info->entries = index;

    if (info->entries == 0)
    {
        nvidia_p2p_put_rsync_registers(info);
        return -ENODEV;
    }

    *reg_info = info;

    return 0;
}

EXPORT_SYMBOL(nvidia_p2p_get_rsync_registers);

void nvidia_p2p_put_rsync_registers(
    nvidia_p2p_rsync_reg_info_t *reg_info
)
{
    NvU32 i;
    nvidia_p2p_rsync_reg_t *regs = NULL;

    if (reg_info == NULL)
    {
        return;
    }

    if (reg_info->regs)
    {
        for (i = 0; i < reg_info->entries; i++)
        {
            regs = &reg_info->regs[i];

            if (regs->ptr)
            {
                nv_iounmap(regs->ptr, regs->size);
            }
        }

        os_free_mem(reg_info->regs);
    }

    os_free_mem(reg_info);
}

EXPORT_SYMBOL(nvidia_p2p_put_rsync_registers);
