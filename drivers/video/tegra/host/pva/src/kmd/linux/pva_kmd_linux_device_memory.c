// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#include "pva_kmd_device_memory.h"
#include "pva_kmd_utils.h"
#include "pva_kmd_linux_device.h"
#include <linux/dma-mapping.h>
#include <linux/dma-buf.h>
#include <linux/iosys-map.h>
#include <linux/fdtable.h>

static struct device *get_context_device(struct pva_kmd_device *pva_device,
					 uint8_t smmu_ctx_idx)
{
	struct nvpva_device_data *pdata =
		pva_kmd_linux_device_get_data(pva_device);

	ASSERT(smmu_ctx_idx < pva_device->hw_consts.n_smmu_contexts);

	return &pdata->smmu_contexts[smmu_ctx_idx]->dev;
}

struct pva_kmd_device_memory_impl {
	struct pva_kmd_device_memory dev_mem;
	struct dma_buf *dmabuf;
	struct iosys_map iosysmap;
	struct dma_buf_attachment *dmabuf_attach;
	struct sg_table *sgt;
	uint64_t offset;
};

struct pva_kmd_device_memory *
pva_kmd_device_memory_alloc_map(uint64_t size, struct pva_kmd_device *pva,
				uint32_t iova_access_flags,
				uint8_t smmu_ctx_idx)
{
	struct device *dev = get_context_device(pva, smmu_ctx_idx);
	dma_addr_t pa = 0U;
	void *va = NULL;
	struct pva_kmd_device_memory_impl *mem_impl;

	mem_impl = pva_kmd_zalloc(sizeof(struct pva_kmd_device_memory_impl));
	if (mem_impl == NULL) {
		pva_kmd_log_err("pva_kmd_zalloc failed");
		goto err_out;
	}

	if (size == 0u) {
		pva_kmd_log_err("Invalid allocation size");
		goto free_mem;
	}

	va = dma_alloc_coherent(dev, size, &pa, GFP_KERNEL);
	if (IS_ERR_OR_NULL(va)) {
		pva_kmd_log_err("dma_alloc_coherent failed");
		goto free_mem;
	}
	mem_impl->dev_mem.iova = pa;
	mem_impl->dev_mem.va = va;
	mem_impl->dev_mem.size = size;
	mem_impl->dev_mem.pva = pva;
	mem_impl->dev_mem.smmu_ctx_idx = smmu_ctx_idx;
	mem_impl->dev_mem.iova_access_flags = iova_access_flags;
	mem_impl->dmabuf = NULL;

	return &mem_impl->dev_mem;
free_mem:
	pva_kmd_free(mem_impl);
err_out:
	return NULL;
}

/**
 * memory_handle is dma fd in Linux, NvRM import_id in QNX, shard memory fd in
 * sim and native.
 */
struct pva_kmd_device_memory *
pva_kmd_device_memory_acquire(uint64_t memory_handle, uint64_t offset,
			      uint64_t size, struct pva_kmd_context *ctx)
{
	struct dma_buf *dma_buf;
	struct pva_kmd_device_memory_impl *mem_impl;

	mem_impl = pva_kmd_zalloc(sizeof(struct pva_kmd_device_memory_impl));
	if (mem_impl == NULL) {
		goto err_out;
	}

	dma_buf = dma_buf_get(memory_handle);
	if (IS_ERR_OR_NULL(dma_buf)) {
		pva_kmd_log_err("Failed to acquire memory");
		goto free_mem;
	}

	if (size > dma_buf->size) {
		pva_kmd_log_err(
			"Trying to register device memory with wrong size");
		goto put_dmabuf;
	}

	mem_impl->dmabuf = dma_buf;
	mem_impl->dev_mem.size = size;
	mem_impl->offset = offset;
	return &mem_impl->dev_mem;

put_dmabuf:
	dma_buf_put(dma_buf);
free_mem:
	pva_kmd_free(mem_impl);
err_out:
	return NULL;
}

void pva_kmd_device_memory_free(struct pva_kmd_device_memory *mem)
{
	struct pva_kmd_device_memory_impl *mem_impl =
		container_of(mem, struct pva_kmd_device_memory_impl, dev_mem);
	struct device *dev;

	if (mem_impl->dmabuf != NULL) {
		/* This memory comes from dma_buf_get */
		if (mem_impl->dmabuf_attach != NULL) {
			pva_kmd_device_memory_iova_unmap(mem);
		}

		if (mem->va != NULL) {
			pva_kmd_device_memory_cpu_unmap(mem);
		}

		dma_buf_put(mem_impl->dmabuf);
		mem_impl->dmabuf = NULL;
	} else {
		/* This memory comes from dma_alloc_coherent */
		dev = get_context_device(mem_impl->dev_mem.pva,
					 mem_impl->dev_mem.smmu_ctx_idx);
		dma_free_coherent(dev, mem->size, mem->va, mem->iova);
		mem->iova = 0U;
	}
	pva_kmd_free(mem_impl);
}

enum pva_error
pva_kmd_device_memory_cpu_map(struct pva_kmd_device_memory *memory)
{
	struct pva_kmd_device_memory_impl *mem_impl = container_of(
		memory, struct pva_kmd_device_memory_impl, dev_mem);
	int ret;

	ret = dma_buf_vmap(mem_impl->dmabuf, &mem_impl->iosysmap);
	if (ret != 0) {
		pva_kmd_log_err("CPU map failed\n");
		return PVA_NOMEM;
	}

	memory->va =
		pva_offset_pointer(mem_impl->iosysmap.vaddr, mem_impl->offset);
	return PVA_SUCCESS;
}

void pva_kmd_device_memory_cpu_unmap(struct pva_kmd_device_memory *memory)
{
	struct pva_kmd_device_memory_impl *mem_impl = container_of(
		memory, struct pva_kmd_device_memory_impl, dev_mem);

	ASSERT(mem_impl->dmabuf != NULL);

	dma_buf_vunmap(mem_impl->dmabuf, &mem_impl->iosysmap);
	memory->va = NULL;
}

enum pva_error
pva_kmd_device_memory_iova_map(struct pva_kmd_device_memory *memory,
			       struct pva_kmd_device *pva,
			       uint32_t access_flags, uint8_t smmu_ctx_idx)
{
	pva_math_error math_err = MATH_OP_SUCCESS;
	struct pva_kmd_device_memory_impl *mem_impl = container_of(
		memory, struct pva_kmd_device_memory_impl, dev_mem);
	struct device *dev = get_context_device(pva, smmu_ctx_idx);
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	enum pva_error err = PVA_SUCCESS;
	enum dma_data_direction dma_direction;
	uint64_t iova;

	switch (access_flags) {
	case PVA_ACCESS_RO: // Read-Only
		dma_direction = DMA_TO_DEVICE;
		break;
	case PVA_ACCESS_WO: // Write-Only
		dma_direction = DMA_FROM_DEVICE;
		break;
	case PVA_ACCESS_RW: // Read-Write
		dma_direction = DMA_BIDIRECTIONAL;
		break;
	default:
		pva_kmd_log_err("Invalid access flags\n");
		err = PVA_INVAL;
		goto err_out;
	}

	attach = dma_buf_attach(mem_impl->dmabuf, dev);
	if (IS_ERR_OR_NULL(attach)) {
		err = PVA_INVAL;
		pva_kmd_log_err("Failed to attach dma_buf\n");
		goto err_out;
	}

	sgt = dma_buf_map_attachment(attach, dma_direction);
	if (IS_ERR_OR_NULL(sgt)) {
		err = PVA_INVAL;
		pva_kmd_log_err("Failed to map attachment\n");
		goto detach;
	}
	iova = addu64(sg_dma_address(sgt->sgl), mem_impl->offset, &math_err);
	if (math_err != MATH_OP_SUCCESS) {
		err = PVA_INVAL;
		pva_kmd_log_err(
			"pva_kmd_device_memory_iova_map Invalid DMA address\n");
		goto unmap;
	}

	mem_impl->sgt = sgt;
	mem_impl->dmabuf_attach = attach;
	mem_impl->dev_mem.iova = iova;
	mem_impl->dev_mem.pva = pva;
	mem_impl->dev_mem.smmu_ctx_idx = smmu_ctx_idx;
	mem_impl->dev_mem.iova_access_flags = access_flags;
	return PVA_SUCCESS;

unmap:
	dma_buf_unmap_attachment(attach, sgt, dma_direction);
detach:
	dma_buf_detach(mem_impl->dmabuf, attach);
err_out:
	return err;
}

void pva_kmd_device_memory_iova_unmap(struct pva_kmd_device_memory *memory)
{
	struct pva_kmd_device_memory_impl *mem_impl = container_of(
		memory, struct pva_kmd_device_memory_impl, dev_mem);

	ASSERT(mem_impl->dmabuf != NULL);

	dma_buf_unmap_attachment(mem_impl->dmabuf_attach, mem_impl->sgt,
				 DMA_BIDIRECTIONAL);
	dma_buf_detach(mem_impl->dmabuf, mem_impl->dmabuf_attach);
	mem_impl->sgt = NULL;
	mem_impl->dmabuf_attach = NULL;
}

uint64_t pva_kmd_get_r5_iova_start(void)
{
	return 0;
}
