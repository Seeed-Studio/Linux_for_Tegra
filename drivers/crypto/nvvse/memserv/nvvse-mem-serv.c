// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 */

#include <nvidia/conftest.h>
#include <linux/errno.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/dma-buf.h>
#include <linux/device.h>
#include <linux/slab.h>
#include "nvvse-linux-common.h"
#include "mem_serv_hal.h"
#include "nvvse-mem-serv-priv.h"

/**
 * @brief Initialize the memory service HAL
 */
MemServStatus_t mem_serv_hal_init(void)
{
	// Return success as there is no actual initialization logic
	return MEM_SERV_SUCCESS;
}

/**
 * @brief Deinitialize the memory service HAL
 */
MemServStatus_t mem_serv_hal_deinit(void)
{
	// Return success as there is no actual deinitialization logic
	return MEM_SERV_SUCCESS;
}

/**
 * @brief Initialize the memory context
 */
MemServStatus_t mem_serv_hal_mem_context_init(void **mem_context,
			MemServDeviceAttributesOS device_handle)
{
	struct memory_context_priv *mem_context_priv = NULL;
	struct MemServDeviceAttributes *device_attrs = NULL;
	MemServStatus_t ret = MEM_SERV_SUCCESS;

	if (mem_context == NULL) {
		NVVSE_ERR("%s: mem_context is NULL\n", __func__);
		ret = MEM_SERV_INVALID_PARAM;
		goto exit;
	}

	if (device_handle == NULL) {
		NVVSE_ERR("%s: device_handle is NULL\n", __func__);
		ret = MEM_SERV_INVALID_PARAM;
		goto exit;
	}

	device_attrs = (struct MemServDeviceAttributes *)device_handle;
	if (device_attrs->dev == NULL) {
		NVVSE_ERR("%s: device_attrs->dev is NULL\n", __func__);
		ret = MEM_SERV_INVALID_PARAM;
		goto exit;
	}

	/*
	 * Allocate memory for the memory context, use GFP_KERNEL
	 * as the memory is not being allocated in IRQ context
	 */
	mem_context_priv = devm_kzalloc(device_attrs->dev,
			sizeof(struct memory_context_priv), GFP_KERNEL);
	if (!mem_context_priv) {
		NVVSE_ERR("%s: Failed to allocate memory_context_priv\n", __func__);
		ret = MEM_SERV_OUT_OF_MEMORY;
		goto exit;
	}
	mem_context_priv->dev = device_attrs->dev;
	*mem_context = mem_context_priv;

exit:
	return ret;
}

/**
 * @brief Deinitialize the memory context
 */
MemServStatus_t mem_serv_hal_mem_context_deinit(void *mem_context)
{
	struct memory_context_priv *mem_context_priv = NULL;
	MemServStatus_t ret = MEM_SERV_SUCCESS;

	if (mem_context == NULL) {
		NVVSE_ERR("%s: mem_context is NULL\n", __func__);
		ret = MEM_SERV_INVALID_PARAM;
		goto exit;
	}

	mem_context_priv = (struct memory_context_priv *)mem_context;
	devm_kfree(mem_context_priv->dev, mem_context_priv);

exit:
	return ret;
}

/**
 * @brief Set memory attributes
 */
MemServStatus_t mem_serv_hal_set_attributes(MemoryParams_t *mem_params)
{
	// Return success as there is no actual attribute setting logic
	return MEM_SERV_SUCCESS;
}

/**
 * @brief Map memory based on memory attributes
 */
MemServStatus_t mem_serv_hal_map(MemoryAccessOS mem_access_os, MemoryParams_t *mem_params)
{
	struct memory_context_priv *mem_context_priv = NULL;
	struct MemoryAccess *mem_access = NULL;
	struct dma_buf *dmabuf = NULL;
	struct dma_buf_attachment *attach = NULL;
	struct sg_table *sgt = NULL;
	dma_addr_t dma_addr = 0;
	phys_addr_t phys_addr = 0;
	int err = 0;
	MemServStatus_t ret = MEM_SERV_SUCCESS;

	if (mem_params == NULL) {
		NVVSE_ERR("%s: mem_params is NULL\n", __func__);
		return MEM_SERV_INVALID_PARAM;
	}

	mem_context_priv = (struct memory_context_priv *)mem_params->mem_context;

	if (mem_context_priv == NULL) {
		NVVSE_ERR("%s: mem_context_priv is NULL\n", __func__);
		return MEM_SERV_INVALID_PARAM;
	}

	if (mem_context_priv->dev == NULL) {
		NVVSE_ERR("%s: device is NULL\n", __func__);
		return MEM_SERV_INVALID_PARAM;
	}

	mem_access = (struct MemoryAccess *)mem_access_os;

	if (mem_access == NULL) {
		NVVSE_ERR("%s: mem_access is NULL\n", __func__);
		return MEM_SERV_INVALID_PARAM;
	}

	if (mem_access->fd == -1) {
		NVVSE_ERR("%s: fd is -1\n", __func__);
		return MEM_SERV_INVALID_PARAM;
	}

	dmabuf = dma_buf_get(mem_access->fd);
	if (IS_ERR_OR_NULL(dmabuf)) {
		NVVSE_ERR("%s dma_buf_get failed\n", __func__);
		return MEM_SERV_INVALID_PARAM;
	}

	attach = dma_buf_attach(dmabuf, mem_context_priv->dev);
	if (IS_ERR_OR_NULL(attach)) {
		err = PTR_ERR(attach);
		NVVSE_ERR("%s dma_buf_attach failed\n", __func__);
		ret = MEM_SERV_INVALID_PARAM;
		goto buf_attach_err;
	}

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR_OR_NULL(sgt)) {
		err = PTR_ERR(sgt);
		NVVSE_ERR("%s dma_buf_map_attachment failed\n", __func__);
		ret = MEM_SERV_INVALID_PARAM;
		goto buf_map_err;
	}

	phys_addr = sg_phys(sgt->sgl);
	dma_addr = sg_dma_address(sgt->sgl);

	if (!dma_addr)
		dma_addr = phys_addr;

	mem_access->dmabuf = dmabuf;
	mem_access->attach = attach;
	mem_access->addr = dma_addr;

	return MEM_SERV_SUCCESS;

buf_map_err:
	dma_buf_detach(dmabuf, attach);
buf_attach_err:
	dma_buf_put(dmabuf);

	return ret;
}

/**
 * @brief Allocate memory
 */
MemServStatus_t mem_serv_hal_alloc(MemoryParams_t *mem_params)
{
	struct memory_context_priv *mem_context_priv = NULL;
	dma_addr_t dma_addr;
	void *vaddr = NULL;
	MemServStatus_t ret = MEM_SERV_SUCCESS;

	if (mem_params == NULL) {
		NVVSE_ERR("%s: mem_params is NULL\n", __func__);
		ret = MEM_SERV_INVALID_PARAM;
		goto exit;
	}

	if (mem_params->mem_context == NULL) {
		NVVSE_ERR("%s: mem_context is NULL\n", __func__);
		ret = MEM_SERV_INVALID_PARAM;
		goto exit;
	}

	if (mem_params->size == 0) {
		NVVSE_ERR("%s: size is 0\n", __func__);
		ret = MEM_SERV_INVALID_PARAM;
		goto exit;
	}

	mem_context_priv = (struct memory_context_priv *)mem_params->mem_context;
	if (mem_context_priv->dev == NULL) {
		NVVSE_ERR("%s: device is NULL\n", __func__);
		ret = MEM_SERV_INVALID_PARAM;
		goto exit;
	}

	/* Allocate managed DMA coherent memory */
	vaddr = dmam_alloc_coherent(mem_context_priv->dev, mem_params->size,
			&dma_addr, GFP_KERNEL);
	if (!vaddr) {
		NVVSE_ERR("%s: Failed to allocate managed DMA coherent memory of size %llu\n",
				__func__, mem_params->size);
		ret = MEM_SERV_OUT_OF_MEMORY;
		goto exit;
	}

	/* Set the allocated memory parameters */
	mem_params->vaddr = (uint8_t *)vaddr;
	mem_params->iova = (uint64_t)dma_addr;

exit:
	return ret;
}

/**
 * @brief Deallocate memory
 *
 * Note: Since dmam_alloc_coherent is used for allocation, the memory is automatically
 * freed when the device is removed/unbound. This function only clears the parameters.
 */
MemServStatus_t mem_serv_hal_dealloc(MemoryParams_t *mem_params)
{
	MemServStatus_t ret = MEM_SERV_SUCCESS;

	if (mem_params == NULL) {
		NVVSE_ERR("%s: mem_params is NULL\n", __func__);
		ret = MEM_SERV_INVALID_PARAM;
		goto exit;
	}

	/* Clear the memory parameters since managed memory is automatically freed */
	mem_params->vaddr = NULL;
	mem_params->iova = 0;

exit:
	return ret;
}

/**
 * @brief Unmap memory
 */
MemServStatus_t mem_serv_hal_unmap(MemoryAccessOS mem_access_os, MemoryParams_t *mem_params)
{
	struct memory_context_priv *mem_context_priv = NULL;
	struct MemoryAccess *mem_access = NULL;

	if (mem_params == NULL) {
		NVVSE_ERR("%s: mem_params is NULL\n", __func__);
		return MEM_SERV_INVALID_PARAM;
	}

	mem_context_priv = (struct memory_context_priv *)mem_params->mem_context;
	if (mem_context_priv->dev == NULL) {
		NVVSE_ERR("%s: device is NULL\n", __func__);
		return MEM_SERV_INVALID_PARAM;
	}

	mem_access = (struct MemoryAccess *)mem_access_os;

	if (mem_access == NULL) {
		NVVSE_ERR("%s: mem_access is NULL\n", __func__);
		return MEM_SERV_INVALID_PARAM;
	}

	if (mem_access->attach == NULL) {
		NVVSE_ERR("%s: attach is NULL\n", __func__);
		return MEM_SERV_INVALID_PARAM;
	}

	if (mem_access->dmabuf == NULL) {
		NVVSE_ERR("%s: dmabuf is NULL\n", __func__);
		return MEM_SERV_INVALID_PARAM;
	}

	dma_buf_detach(mem_access->dmabuf, mem_access->attach);
	dma_buf_put(mem_access->dmabuf);

	return MEM_SERV_SUCCESS;
}

/**
 * @brief Get hardware ID of the bus master
 */
MemServStatus_t mem_serv_hal_get_hwid(void *mem_context, uint32_t stream_id, uint32_t phandle)
{
	// Return failure as there is no actual hardware ID retrieval logic
	return MEM_SERV_OPERATION_FAILED;
}

#if defined(NV_MODULE_IMPORT_NS_CALLS_STRINGIFY)
MODULE_IMPORT_NS(DMA_BUF);
#else
MODULE_IMPORT_NS("DMA_BUF");
#endif
