/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * NVDLA Synchornization Management Header
 */

#ifndef __NVDLA_SYNC_H__
#define __NVDLA_SYNC_H__

#include <linux/platform_device.h>
#include <uapi/linux/nvdev_fence.h>

/**
 * @brief Creates nvdla synchronization device based on the platform_device.
 *
 * @param[in]   pdev Pointer to platform device.
 *
 * @return
 *  - non-NULL synchronization device pointer upon successful execution.
 *  - NULL, otherwise.
 **/
struct nvdla_sync_device *nvdla_sync_device_create_syncpoint(
	struct platform_device *pdev);

/**
 * @brief Destroys nvdla synchronization device.
 *
 * @param[in]   device Pointer to synchronization device to destroy.
 **/
void nvdla_sync_device_destroy(struct nvdla_sync_device *device);

/**
 * @brief Fetches the IOVA address based on syncpoint.
 *
 * This function is applicable only to the synchronization devices of type
 * syncpoint.
 *
 * @param[in] device Synchronization device of type sync point.
 * @param[in] id Syncpoint index for which IOVA needs to be fetched.
 *
 * @return
 * - non-zero, if valid IOVA is associated with syncpt id.
 * - zero, otherwise.
 **/
dma_addr_t nvdla_sync_get_address_by_syncptid(
	struct nvdla_sync_device *device,
	uint32_t syncptid);

/**
 * @brief Creates synchronization context for a given platform device.
 *
 * @param[in]   device Pointer to synchronization device.
 *
 * @return
 *  - non-NULL synchronization context pointer upon successful execution.
 *  - NULL, otherwise.
 **/
struct nvdla_sync_context *nvdla_sync_create(
	struct nvdla_sync_device *device);

/**
 * @brief Destroys synchronization context.
 *
 * @param[in] context Pointer to synchronization context to destroy.
 **/
void nvdla_sync_destroy(struct nvdla_sync_context *context);

/**
 * @brief Fetches the IOVA address corresponding to the synchronization context.
 *
 * @param[in] context Synchronization context for which IOVA is required.
 *
 * @return
 * - non-zero, if valid IOVA is associated with context.
 * - zero, otherwise.
 **/
dma_addr_t nvdla_sync_get_address(struct nvdla_sync_context *context);

/**
 * @brief Increments the max value corresponding to the synchronization context.
 *
 * The max value represents the number of events dependent on synchronization
 * context. In a typical case, the max value is incremented by one for each
 * signal events that is queued up during the submission.
 *
 * @param[in] context Synchronization context for which max value is set.
 * @param[in] increment Value that is added to the max value.
 *
 * @return
 * - max_value corresponding to the context after the increment.
 **/
uint32_t nvdla_sync_increment_max_value(struct nvdla_sync_context *context,
	uint32_t increment);

/**
 * @brief Gets the max value corresponding to the synchronization context.
 *
 * @param[in] context Synchronization context from which max value is fetched.
 *
 * @return
 * - max_value corresponding to the context.
 **/
uint32_t nvdla_sync_get_max_value(struct nvdla_sync_context *context);

/**
 * @brief, CPU wait on the synchronization context for a given threshold.
 *
 * @param[in] context Synchronization context on which wait is performed.
 * @param[in] threshold Target value that the sync payload has to reach.
 * @param[in] timeout Maximum timeout for which wait blocks.
 *
 * @return
 * - zero, if the wait unblocks within given timeout for a specified threshold.
 * - non-zero, otherwise.
 **/
int32_t nvdla_sync_wait(struct nvdla_sync_context *context,
	uint32_t threshold,
	uint64_t timeout);

/**
 * @brief, Signal on the synchronization context for a given signal value.
 *
 * @param[in] context Synchronization context on which signal is performed.
 * @param[in] signal_value Target value that sync payload is set.
 *
 * @return
 * - zero, upon successful signaling.
 * - non-zero, otherwise.
 **/
int32_t nvdla_sync_signal(struct nvdla_sync_context *context,
	uint32_t signal_value);

/**
 * @brief, Prints the synchronization context.
 *
 * @param[in] context Synchronization context which is to be printed.
 **/
void nvdla_sync_print(struct nvdla_sync_context *context);

/**
 * @brief, Gets the syncpoint index corresponding to the context.
 *
 * This function is applicable only for the context created using
 * synchronization device of type syncpoint.
 *
 * @param[in] context Synchronization context
 *
 * @return
 * - 0xFFFFFFFF if queried with invalid context.
 * - valid syncpoint index otherwise.
 **/
uint32_t nvdla_sync_get_syncptid(struct nvdla_sync_context *context);

#endif /*__NVDLA_SYNC_H__ */
