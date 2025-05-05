// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2017-2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

/**
 * @file drivers/media/platform/tegra/camera/fusa-capture/capture-isp.c
 *
 * @brief ISP channel operations for the T186/T194/T234/T264 Camera RTCPU platform.
 */

#include <nvidia/conftest.h>

#include <linux/completion.h>
#include <linux/nospec.h>
/*
 * The host1x-next.h header must be included before the nvhost.h
 * header, as the nvhost.h header includes the host1x.h header,
 * which is mutually exclusive with host1x-next.h.
 */
#include <linux/host1x-next.h>
#include <linux/nvhost.h>
#include <linux/of_platform.h>
#include <linux/printk.h>
#include <linux/vmalloc.h>
#include <linux/tegra-capture-ivc.h>
#include <linux/tegra-camera-rtcpu.h>
#include <linux/io.h>
#include <linux/iosys-map.h>
#include <linux/dma-buf.h>
#include <linux/of.h>
#include <asm/arch_timer.h>
#include <soc/tegra/fuse.h>

#include <camera/nvcamera_log.h>
#include <uapi/linux/nvhost_events.h>
#include "soc/tegra/camrtc-capture.h"
#include "soc/tegra/camrtc-capture-messages.h"
#include <media/fusa-capture/capture-isp-channel.h>
#include <media/fusa-capture/capture-common.h>
#include <media/fusa-capture/capture-isp.h>
#include <linux/arm64-barrier.h>
#include <linux/tegra-rtcpu-trace.h>

/**
 * @brief Invalid ISP channel ID; the channel is not initialized.
 */
#define CAPTURE_CHANNEL_ISP_INVALID_ID	U16_C(0xFFFF)

/**
 * @brief Maximum number of ISP channels supported by KMD
 */
#define NUM_ISP_CHANNELS	U32_C(16)
/**
 * @brief Maximum number of ISP channels supported by KMD for T26x
 */
#define NUM_ISP_CHANNELS_T26x	U32_C(32)

/**
 * @brief Maximum number of ISP devices supported.
 */
#define MAX_ISP_UNITS	U32_C(0x2)

/**
 * @brief Maximum surfaces ISP can read on its input port.
 */
#define MAX_ISP_INPUT_SURFACES	U32_C(3)

/**
 * @brief Maximum number of ISP input prefences.
 */
#define MAX_ISP_INPUT_PREFENCES	U32_C(14)

/**
 * @brief The Capture-ISP standalone driver context.
 */
struct tegra_capture_isp_data {
	uint32_t num_isp_devices;/**< Number of available ISP devices */
	struct platform_device *isp_pdevices[MAX_ISP_UNITS];
	uint32_t max_isp_channels;
		/**< Maximum number of ISP capture channel devices */
};

/**
 * @brief ISP channel process descriptor queue context.
 */
struct isp_desc_rec {
	struct capture_common_buf requests; /**< Process descriptor queue */
	size_t request_buf_size; /**< Size of process descriptor queue [byte] */
	uint32_t queue_depth; /**< No. of process descriptors in queue */
	uint32_t request_size;
		/**< Size of a single process descriptor [byte] */
	void *requests_memoryinfo;
		/**< memory info ringbuffer */
	uint64_t requests_memoryinfo_iova;
		/**< memory info ringbuffer rtcpu iova */

	uint32_t progress_status_buffer_depth;
		/**< No. of process descriptors. */

	struct mutex unpins_list_lock; /**< Lock for unpins_list */
	struct capture_common_unpins *unpins_list;
		/**< List of process request buffer unpins */
};

/**
 * @brief ISP channel capture context.
 */
struct isp_capture {
	uint16_t channel_id; /**< RCE-assigned ISP FW channel id */
	struct device *rtcpu_dev; /**< rtcpu device */
	struct tegra_isp_channel *isp_channel; /**< ISP channel context */
	struct capture_buffer_table *buffer_ctx;
		/**< Surface buffer management table */

	struct isp_desc_rec capture_desc_ctx;
		/**< Capture process descriptor queue context */
	struct isp_desc_rec program_desc_ctx;
		/**< Program process descriptor queue context */

	struct capture_common_status_notifier progress_status_notifier;
		/**< Process progress status notifier context */
	bool is_progress_status_notifier_set;
		/**< Whether progress_status_notifer has been initialized */

#ifdef HAVE_ISP_GOS_TABLES
	uint32_t num_gos_tables; /**< No. of cv devices in gos_tables */
	const dma_addr_t *gos_tables; /**< IOVA addresses of all GoS devices */
#endif

	struct syncpoint_info progress_sp; /**< Syncpoint for frame progress */
	struct syncpoint_info stats_progress_sp;
		/**< Syncpoint for stats progress */
	uint32_t stats_progress_delta;
		/**< Expected stats progress increment for a frame */

	struct completion control_resp;
		/**< Completion for capture-control IVC response */
	struct completion capture_resp;
		/**<
		 * Completion for capture process requests (frame), if progress
		 * status notifier is not in use
		 */
	struct completion capture_program_resp;
		/**<
		 * Completion for program process requests (frame), if progress
		 * status notifier is not in use
		 */

	struct mutex control_msg_lock;
		/**< Lock for capture-control IVC control_resp_msg */
	struct CAPTURE_CONTROL_MSG control_resp_msg;
		/**< capture-control IVC resp msg written to by callback */

	struct mutex reset_lock;
		/**< Channel lock for reset/abort support (via RCE) */
	bool reset_capture_program_flag;
		/**< Reset flag to drain pending program process requests */
	bool reset_capture_flag;
		/**< Reset flag to drain pending capture process requests */
};

/**
 * @brief Initialize an ISP syncpoint and get its GoS backing.
 *
 * This function performs the following operations:
 * - Initializes the @a sp structure to zeroes.
 * - If @a enable is false, returns success.
 * - Allocates a synchronization point by calling
 *   @ref struct tegra_isp_channel::ops::alloc_syncpt().
 * - Gets the syncpoint handle using @ref host1x_syncpt_get_by_id_noref().
 * - Reads the syncpoint value using @ref host1x_syncpt_read().
 * - Retrieves GOS backing information by calling
 *   @ref struct tegra_isp_channel::ops::get_syncpt_gos_backing().
 * - Sets the GOS index and offset in the @a sp structure.
 * - On failure after allocation, releases the synchronization point with
 *   @ref struct tegra_isp_channel::ops::release_syncpt() and re-initializes the @a sp
 *   structure.
 *
 * @param[in]  chan    Pointer to the @ref tegra_isp_channel structure.
 *                     Valid Value: non-NULL.
 * @param[in]  name    Name of the synchronization point.
 *                     Valid Value: non-NULL.
 * @param[in]  enable  Flag to enable or disable the synchronization point setup.
 *                     Valid Value: true or false.
 * @param[out] sp      Pointer to the @ref syncpoint_info structure to be populated.
 *                     Valid Value: non-NULL.
 *
 * @retval 0              On successful setup or if @a enable is false.
 * @retval -EINVAL        If @ref host1x_syncpt_get_by_id_noref() fails.
 * @retval (int)          If @ref struct tegra_isp_channel::ops::alloc_syncpt() or
 *                        @ref struct tegra_isp_channel::ops::get_syncpt_gos_backing() fails.
 */
static int isp_capture_setup_syncpt(
	struct tegra_isp_channel *chan,
	const char *name,
	bool enable,
	struct syncpoint_info *sp)
{
	struct platform_device *pdev = chan->ndev;
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct host1x_syncpt *host1x_sp;
	uint32_t gos_index = GOS_INDEX_INVALID;
	uint32_t gos_offset = 0;
	int err;

	memset(sp, 0, sizeof(*sp));

	if (!enable)
		return 0;


	err = chan->ops->alloc_syncpt(pdev, name, &sp->id);
	if (err)
		return err;

	host1x_sp = host1x_syncpt_get_by_id_noref(pdata->host1x, sp->id);
	if (!host1x_sp) {
		err = -EINVAL;
		goto cleanup;
	}

	sp->threshold = host1x_syncpt_read(host1x_sp);

	err = chan->ops->get_syncpt_gos_backing(pdev, sp->id, &sp->shim_addr,
				&gos_index, &gos_offset);
	if (err)
		goto cleanup;

	sp->gos_index = gos_index;
	sp->gos_offset = gos_offset;

	return 0;

cleanup:
	chan->ops->release_syncpt(pdev, sp->id);
	memset(sp, 0, sizeof(*sp));

	return err;
}

/**
 * @brief Fast-forwards a synchronization point in the ISP channel.
 *
 * This function performs the following operations:
 * - Validates the synchronization point ID.
 * - If the ID is valid, invokes @ref struct tegra_isp_channel::ops::fast_forward_syncpt()
 *   with the device handle, synchronization point ID, and threshold.
 *
 * @param[in]  chan  Pointer to the @ref tegra_isp_channel structure.
 *                   Valid value: non-NULL.
 * @param[in]  sp    Pointer to the @ref syncpoint_info structure.
 *                   Valid value: non-NULL.
 */
static void isp_capture_fastforward_syncpt(
	struct tegra_isp_channel *chan,
	struct syncpoint_info *sp)
{
	if (sp->id)
		chan->ops->fast_forward_syncpt(chan->ndev, sp->id, sp->threshold);
}

/**
 * @brief Fast-forwards synchronization points in the ISP capture process.
 *
 * This function performs the following operations:
 * - Retrieves the @ref isp_capture structure from the provided channel.
 * - Invokes @ref isp_capture_fastforward_syncpt() with the channel and the
 *   progress synchronization point.
 * - Invokes @ref isp_capture_fastforward_syncpt() with the channel and the
 *   statistics progress synchronization point.
 *
 * @param[in]  chan  Pointer to the @ref tegra_isp_channel structure.
 *                   Valid value: non-NULL.
 */
static void isp_capture_fastforward_syncpts(
	struct tegra_isp_channel *chan)
{
	struct isp_capture *capture = chan->capture_data;

	isp_capture_fastforward_syncpt(chan, &capture->progress_sp);
	isp_capture_fastforward_syncpt(chan, &capture->stats_progress_sp);
}

/**
 * @brief Releases a synchronization point in the ISP channel and clears its
 *        associated information.
 *
 * This function performs the following operations:
 * - Validates the synchronization point ID.
 * - If the ID is valid, invokes @ref struct tegra_isp_channel::ops::release_syncpt() with the
 *   device handle and synchronization point ID.
 * - Resets the synchronization point information by clearing the structure
 *   using @ref memset().
 *
 * @param[in]  chan  Pointer to the @ref tegra_isp_channel structure.
 *                   Valid value: non-NULL.
 * @param[in]  sp    Pointer to the @ref syncpoint_info structure.
 *                   Valid value: non-NULL.
 */
static void isp_capture_release_syncpt(
	struct tegra_isp_channel *chan,
	struct syncpoint_info *sp)
{
	if (sp->id)
		chan->ops->release_syncpt(chan->ndev, sp->id);

	memset(sp, 0, sizeof(*sp));
}

/**
 * @brief Releases synchronization points in the ISP capture process.
 *
 * This function performs the following operations:
 * - Retrieves the @ref isp_capture structure from the provided channel.
 * - Checks if the capture structure is valid.
 *   If invalid, returns early.
 * - Invokes @ref isp_capture_release_syncpt() with the channel and the
 *   progress synchronization point.
 * - Invokes @ref isp_capture_release_syncpt() with the channel and the
 *   statistics progress synchronization point.
 *
 * @param[in]  chan  Pointer to the @ref tegra_isp_channel structure.
 *                   Valid value: non-NULL.
 */
static void isp_capture_release_syncpts(
	struct tegra_isp_channel *chan)
{
	struct isp_capture *capture = chan->capture_data;
	if (capture == NULL) {
		pr_err("%s: invalid context\n", __func__);
		return;
	}

	isp_capture_release_syncpt(chan, &capture->progress_sp);
	isp_capture_release_syncpt(chan, &capture->stats_progress_sp);
}

/**
 * @brief Sets up synchronization points for the ISP capture channel.
 *
 * This function performs the following operations:
 * - Validates the capture data associated with the ISP channel.
 * - If @ref HAVE_ISP_GOS_TABLES is defined, retrieves the GOS tables by calling
 *   @ref struct tegra_isp_channel::ops::get_gos_table().
 * - Calls @ref isp_capture_setup_syncpt() to set up the "progress" synchronization
 *   point.
 * - Calls @ref isp_capture_setup_syncpt() to set up the "stats_progress" synchronization
 *   point.
 * - In case of any failure during setup, releases all synchronization points by calling
 *   @ref isp_capture_release_syncpts().
 *
 * @param[in]  chan  Pointer to the @ref tegra_isp_channel structure.
 *                   Valid value: non-NULL.
 *
 * @retval 0           On successful setup of all synchronization points.
 * @retval -ENODEV     If the capture data is uninitialized.
 * @retval (int)       If @ref isp_capture_setup_syncpt() fails for any synchronization
 *                     point or if @ref isp_capture_release_syncpts() fails during error handling.
 */
static int isp_capture_setup_syncpts(
	struct tegra_isp_channel *chan)
{
	struct isp_capture *capture = chan->capture_data;
	int err = 0;

	if (capture == NULL) {
		dev_err(chan->isp_dev,
			"%s: isp capture uninitialized\n", __func__);
		return -ENODEV;
	}

#ifdef HAVE_ISP_GOS_TABLES
	capture->num_gos_tables = chan->ops->get_gos_table(chan->ndev,
							&capture->gos_tables);
#endif

	err = isp_capture_setup_syncpt(chan, "progress", true,
			&capture->progress_sp);
	if (err < 0)
		goto fail;

	err = isp_capture_setup_syncpt(chan, "stats_progress",
				true,
				&capture->stats_progress_sp);
	if (err < 0)
		goto fail;

	return 0;

fail:
	isp_capture_release_syncpts(chan);
	return err;
}

/**
 * @brief Reads the value of a synchronization point in the ISP channel.
 *
 * This function performs the following operations:
 * - Checks if the synchronization point ID is valid.
 * - If valid, gets the syncpoint handle using @ref host1x_syncpt_get_by_id_noref().
 * - If handle is valid, reads its value using @ref host1x_syncpt_read().
 *
 * @param[in]  chan  Pointer to the @ref tegra_isp_channel structure.
 *                   Valid value: non-NULL.
 * @param[in]  sp    Pointer to the @ref syncpoint_info structure.
 *                   Valid value: non-NULL.
 * @param[out] val   Pointer to a variable where the synchronization point value
 *                   will be stored.
 *                   Valid value: non-NULL.
 *
 * @retval 0        On successful reading of the synchronization point value.
 * @retval -EINVAL  If @ref host1x_syncpt_get_by_id_noref() fails.
 */
static int isp_capture_read_syncpt(
	struct tegra_isp_channel *chan,
	struct syncpoint_info *sp,
	uint32_t *val)
{
	struct platform_device *pdev = chan->ndev;
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct host1x_syncpt *host1x_sp;

	if (sp->id) {
		host1x_sp = host1x_syncpt_get_by_id_noref(pdata->host1x, sp->id);
		if (!host1x_sp) {
			dev_err(chan->isp_dev,
				"%s: get syncpt %i val failed\n", __func__,
				sp->id);
			return -EINVAL;
		}
		*val = host1x_syncpt_read(host1x_sp);
	}

	return 0;
}

/**
 * @brief Populates fence information for ISP capture using synchronization points.
 *
 * This function performs the following operations:
 * - Adjusts the relocation page address using @a fence_offset, @a reloc_page_addr and
 *   @ref PAGE_MASK.
 * - Validates the relocation page address.
 * - Reads the raw synchronization point value from the ISP IOVA-mapped relocation address using
 *   @ref __raw_readq().
 * - Extracts the synchronization point ID from the raw syncpoint.
 * - Calls @ref struct tegra_isp_channel::ops::get_syncpt_gos_backing() with the channel device,
 *   synchronization point ID, and retrieves synchronization point address, GoS index,
 *   and GoS offset.
 * - Combines GoS index and GoS offset in a GoS information buffer.
 * - Updates the relocation page address for GoS information.
 * - Writes the GoS information to the updated relocation page address using @ref __raw_writeq().
 * - Updates the relocation page address for synchronization point information.
 * - Writes the synchronization point address to the updated relocation page address using
 *   @ref __raw_writeq().
 *
 * @param[in]  chan             Pointer to the @ref tegra_isp_channel structure.
 *                              Valid value: non-NULL.
 * @param[in]  fence_offset     Offset value for the fence.
 *                              Valid range: [0 .. UINT16_MAX].
 * @param[in]  gos_relative     Relative byte offset for GoS.
 *                              Valid range: non-negative.
 * @param[in]  sp_relative      Relative byte offset for synchronization point.
 *                              Valid range: non-negative.
 * @param[in]  reloc_page_addr  Pointer to the relocation page address.
 *                              Valid value: non-NULL.
 *
 * @retval 0          On successful completion.
 * @retval -ENOMEM    If the relocation page address is invalid.
 * @retval (int)      Errors returned from invocation of
 *                    @ref struct tegra_isp_channel::ops::get_syncpt_gos_backing().
 */
static int isp_capture_populate_fence_info(
	struct tegra_isp_channel *chan,
	int fence_offset,
	uint32_t gos_relative,
	uint32_t sp_relative,
	void *reloc_page_addr)
{
	int err = 0;
	uint64_t sp_raw;
	uint32_t sp_id;
	dma_addr_t syncpt_addr;
	uint32_t gos_index;
	uint32_t gos_offset;
	uint64_t gos_info = 0;
	reloc_page_addr += fence_offset & PAGE_MASK;

	if (unlikely(reloc_page_addr == NULL)) {
		dev_err(chan->isp_dev, "%s: couldn't map request\n", __func__);
		return -ENOMEM;
	}

	sp_raw = __raw_readq(
			(void __iomem *)(reloc_page_addr +
			(fence_offset & ~PAGE_MASK)));
	sp_id = sp_raw & 0xFFFFFFFF;

	err = chan->ops->get_syncpt_gos_backing(chan->ndev, sp_id, &syncpt_addr,
			&gos_index, &gos_offset);
	if (err) {
		dev_err(chan->isp_dev,
			"%s: get GoS backing failed\n", __func__);
		goto ret;
	}

	gos_info = ((((uint16_t)gos_offset << 16) | ((uint8_t)gos_index) << 8)
			& 0xFFFFFFFF);

	reloc_page_addr += (((fence_offset + gos_relative) & PAGE_MASK) - (fence_offset & PAGE_MASK));

	__raw_writeq(gos_info, (void __iomem *)(reloc_page_addr +
			((fence_offset + gos_relative) & ~PAGE_MASK)));

	reloc_page_addr += (((fence_offset + sp_relative) & PAGE_MASK) - ((fence_offset + gos_relative) & PAGE_MASK));

	__raw_writeq((uint64_t)syncpt_addr, (void __iomem *)(reloc_page_addr +
			((fence_offset + sp_relative) & ~PAGE_MASK)));

ret:
	return err;
}

/**
 * @brief Sets up input fence syncpoints for ISP capture requests.
 *
 * This function performs the following operations:
 * - Validates the capture data within the provided channel.
 * - Checks if input fences are present for the given capture request.
 * - Allocates memory for input fence relocations by calling @ref kcalloc().
 * - Copies input fence relocation data from user space using @ref copy_from_user().
 * - Maps the DMA buffer using @ref dma_buf_vmap() and retrieves the mapped address.
 * - Iterates through each input fence relocation:
 *   - Calculates the input fence offset, handling overflow using @ref check_add_overflow().
 *   - Calls @ref isp_capture_populate_fence_info() with the channel, calculated
 *     offset, GoS relative offset, synchronization point relative offset, and
 *     relocation page address.
 * - Unmaps the DMA buffer using @ref dma_buf_vunmap() and frees allocated memory
 *   in case of failure.
 *
 * @param[in]  chan            Pointer to the @ref tegra_isp_channel structure.
 *                             Valid value: non-NULL.
 * @param[in]  req             Pointer to the @ref isp_capture_req structure.
 *                             Valid value: non-NULL.
 * @param[in]  request_offset  Offset value for the capture request.
 *                             Valid range: depends on context.
 *
 * @retval 0           On successful completion.
 * @retval -ENODEV     If the ISP capture is uninitialized.
 * @retval -ENOMEM     If memory allocation via @ref kcalloc() or buffer mapping
 *                     via @ref dma_buf_vmap() fails.
 * @retval -EFAULT     If copying from user space fails via @ref copy_from_user().
 * @retval -EOVERFLOW  If input fence offset calculation results in overflow via
 *                     @ref check_add_overflow().
 * @retval (int)       Errors returned from invocation of @ref isp_capture_populate_fence_info().
 */
static int isp_capture_setup_inputfences(
	struct tegra_isp_channel *chan,
	struct isp_capture_req *req,
	int request_offset)
{
	int i = 0;
	int err = 0;

	uint32_t __user *inpfences_reloc_user;
	uint32_t *inpfences_relocs = NULL;
	uint32_t inputfences_offset = 0;
	void *reloc_page_addr  = NULL;
	struct isp_capture *capture = chan->capture_data;
	void *vmap_base = NULL;
#if defined(NV_LINUX_IOSYS_MAP_H_PRESENT)
	struct iosys_map map;
#else
	struct dma_buf_map map;
#endif

	if (capture == NULL) {
		dev_err(chan->isp_dev,
			"%s: isp capture uninitialized\n", __func__);
		return -ENODEV;
	}

	/* It is valid not to have inputfences for given frame capture */
	if (!req->inputfences_relocs.num_relocs)
		return 0;

	if (req->inputfences_relocs.num_relocs > MAX_ISP_INPUT_SURFACES) {
		dev_err(chan->isp_dev, "inputfences num exceeds max allowed\n");
		return -EINVAL;
	}

	inpfences_reloc_user = (uint32_t __user *)
			(uintptr_t)req->inputfences_relocs.reloc_relatives;

	inpfences_relocs = kcalloc(req->inputfences_relocs.num_relocs,
				sizeof(uint32_t), GFP_KERNEL);
	if (unlikely(inpfences_relocs == NULL)) {
		dev_err(chan->isp_dev,
			"failed to allocate inputfences reloc array\n");
		return -ENOMEM;
	}

	err = copy_from_user(inpfences_relocs, inpfences_reloc_user,
		req->inputfences_relocs.num_relocs * sizeof(uint32_t)) ?
			-EFAULT : 0;
	if (err < 0) {
		dev_err(chan->isp_dev, "failed to copy inputfences relocs\n");
		goto fail;
	}

	err = dma_buf_vmap(capture->capture_desc_ctx.requests.buf, &map);
	vmap_base = err ? NULL : map.vaddr;
	if (!vmap_base) {
		pr_err("%s: Cannot map capture descriptor request\n", __func__);
		err = -ENOMEM;
		goto fail;
	}
	reloc_page_addr = vmap_base;

	for (i = 0; i < req->inputfences_relocs.num_relocs; i++) {
		if (check_add_overflow(
			request_offset, (int)inpfences_relocs[i], (int *)(&inputfences_offset))) {
			err = -EOVERFLOW;
			goto fail;
		}

		err = isp_capture_populate_fence_info(chan, inputfences_offset,
				req->gos_relative, req->sp_relative, reloc_page_addr);
		if (err < 0) {
			dev_err(chan->isp_dev,
				"Populate inputfences info failed\n");
			goto fail;
		}
	}

	spec_bar();

fail:
	if (vmap_base != NULL)
		dma_buf_vunmap(capture->capture_desc_ctx.requests.buf,
			&map);
	kfree(inpfences_relocs);
	return err;
}

/**
 * @brief Sets up prefence syncpoints for ISP capture requests.
 *
 * This function performs the following operations:
 * - Validates the capture data within the provided channel is not NULL.
 * - Validates the capture request structure.
 * - Checks if prefences are present for the given capture request.
 * - Allocates memory for prefence relocations by calling @ref kcalloc().
 * - Copies prefence relocation data from user space using @ref copy_from_user().
 * - Maps the DMA buffer by calling @ref dma_buf_vmap() and retrieves the mapped address.
 * - Iterates through each prefence relocation entry:
 *   - Calculates the prefence offset, handling overflow using @ref check_add_overflow().
 *   - Calls @ref isp_capture_populate_fence_info() with the channel, calculated
 *     offset, GoS relative offset, synchronization point relative offset, and
 *     relocation page address.
 * - Implements a speculative barrier using @ref spec_bar().
 * - On failure, unmaps the DMA buffer using @ref dma_buf_vunmap() and frees allocated
 *   memory.
 *
 * @param[in]  chan            Pointer to the @ref tegra_isp_channel structure.
 *                             Valid value: non-NULL.
 * @param[in]  req             Pointer to the @ref isp_capture_req structure.
 *                             Valid value: non-NULL.
 * @param[in]  request_offset  Byte offset value for the capture request.
 *                             Valid range: [0 .. UINT16_MAX].
 *
 * @retval 0                On successful setup or if no prefences are configured.
 * @retval -ENODEV          If the ISP capture data is uninitialized or the request is NULL.
 * @retval -ENOMEM          If memory allocation via @ref kcalloc() or buffer mapping
 *                          via @ref dma_buf_vmap() fails.
 * @retval -EFAULT          If copying data from user space fails via @ref copy_from_user().
 * @retval -EOVERFLOW       If prefence offset calculation results in overflow via
 *                          @ref check_add_overflow().
 * @retval (int)            Errors returned from invocation of
 *                          @ref isp_capture_populate_fence_info().
 */
static int isp_capture_setup_prefences(
	struct tegra_isp_channel *chan,
	struct isp_capture_req *req,
	int request_offset)
{
	uint32_t __user *prefence_reloc_user;
	uint32_t *prefence_relocs = NULL;
	uint32_t prefence_offset = 0;
	int i = 0;
	int err = 0;
	void *reloc_page_addr = NULL;
	struct isp_capture *capture = chan->capture_data;
	void *vmap_base = NULL;
#if defined(NV_LINUX_IOSYS_MAP_H_PRESENT)
	struct iosys_map map;
#else
	struct dma_buf_map map;
#endif

	if (capture == NULL) {
		dev_err(chan->isp_dev,
			"%s: isp capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (req == NULL) {
		dev_err(chan->isp_dev,
			"%s: NULL isp capture received\n", __func__);
		return -ENODEV;
	}

	/* It is valid not to have prefences for given frame capture */
	if (!req->prefences_relocs.num_relocs)
		return 0;

	if (req->prefences_relocs.num_relocs > MAX_ISP_INPUT_PREFENCES) {
		dev_err(chan->isp_dev, "prefences num exceeds max allowed\n");
		return -EINVAL;
	}

	prefence_reloc_user = (uint32_t __user *)
			(uintptr_t)req->prefences_relocs.reloc_relatives;

	prefence_relocs = kcalloc(req->prefences_relocs.num_relocs,
		sizeof(uint32_t), GFP_KERNEL);
	if (unlikely(prefence_relocs == NULL)) {
		dev_err(chan->isp_dev,
			"failed to allocate prefences reloc array\n");
		return -ENOMEM;
	}

	err = copy_from_user(prefence_relocs, prefence_reloc_user,
		req->prefences_relocs.num_relocs * sizeof(uint32_t)) ?
			-EFAULT : 0;
	if (err < 0) {
		dev_err(chan->isp_dev, "failed to copy prefences relocs\n");
		goto fail;
	}

	err = dma_buf_vmap(capture->capture_desc_ctx.requests.buf, &map);
	vmap_base = err ? NULL : map.vaddr;
	if (!vmap_base) {
		pr_err("%s: Cannot map capture descriptor request\n", __func__);
		err = -ENOMEM;
		goto fail;
	}
	reloc_page_addr = vmap_base;

	for (i = 0; i < req->prefences_relocs.num_relocs; i++) {
		if (check_add_overflow(
			request_offset, (int)prefence_relocs[i], (int *)(&prefence_offset))) {
			err = -EOVERFLOW;
			goto fail;
		}

		err = isp_capture_populate_fence_info(chan, prefence_offset,
				req->gos_relative, req->sp_relative, reloc_page_addr);
		if (err < 0) {
			dev_err(chan->isp_dev, "Populate prefences info failed\n");
			goto fail;
		}
	}

	spec_bar();

fail:
	if (vmap_base != NULL)
		dma_buf_vunmap(capture->capture_desc_ctx.requests.buf,
			&map);
	kfree(prefence_relocs);
	return err;
}

/**
 * @brief Unpins buffers for a specified capture request in the ISP channel.
 *
 * This function performs the following operations:
 * - Retrieves the capture data from the provided channel.
 * - Validates that the capture data is initialized. If not, returns immediately.
 * - Acquires the unpins list lock using @ref mutex_lock().
 * - Validates the buffer index against the queue depth.
 *   If invalid, returns immediately.
 * - Retrieves the list of unpins for the specified buffer index.
 * - If there are unpins, iterates through them:
 *   - Calls @ref put_mapping() for each unpin entry.
 *   - Resets the unpins list using @ref memset().
 * - Releases the unpins list lock using @ref mutex_unlock().
 *
 * @param[in]  chan          Pointer to the @ref tegra_isp_channel structure.
 *                           Valid Value: non-NULL.
 * @param[in]  buffer_index  Index of the buffer to unpin.
 *                           Valid range: [0 .. (queue_depth - 1)].
 */
static void isp_capture_request_unpin(
	struct tegra_isp_channel *chan,
	uint32_t buffer_index)
{
	struct isp_capture *capture = chan->capture_data;
	struct capture_common_unpins *unpins;
	int i = 0;

	if (capture == NULL) {
		dev_err(chan->isp_dev,
			"%s: isp capture uninitialized\n", __func__);
		return;
	}

	mutex_lock(&capture->capture_desc_ctx.unpins_list_lock);
	if (buffer_index >= capture->program_desc_ctx.queue_depth) {
		dev_err(chan->isp_dev,
			"%s: buffer index is out of bound\n", __func__);
		return;
	}
	unpins = &capture->capture_desc_ctx.unpins_list[buffer_index];
	if (unpins->num_unpins != 0U) {
		for (i = 0; i < unpins->num_unpins; i++)
			put_mapping(capture->buffer_ctx, unpins->data[i]);
		(void)memset(unpins, 0U, sizeof(*unpins));
	}
	mutex_unlock(&capture->capture_desc_ctx.unpins_list_lock);
}

/**
 * @brief Unpins buffers for a specified ISP program request in the ISP channel.
 *
 * This function performs the following operations:
 * - Retrieves the capture data from the provided channel.
 * - Validates that the capture data is initialized. If not, returns immediately.
 * - Acquires the unpins list lock using @ref mutex_lock().
 * - Validates the buffer index against the queue depth.
 *   If invalid, returns immediately.
 * - Retrieves the list of unpins for the specified buffer index.
 * - If there are unpins, iterates through them:
 *   - Calls @ref put_mapping() for each unpin entry.
 *   - Resets the unpins list using @ref memset().
 * - Releases the unpins list lock using @ref mutex_unlock().
 *
 * @param[in]  chan          Pointer to the @ref tegra_isp_channel structure.
 *                           Valid value: non-NULL.
 * @param[in]  buffer_index  Index of the buffer to unpin.
 *                           Valid range: [0 .. (queue_depth - 1)].
 */
static void isp_capture_program_request_unpin(
	struct tegra_isp_channel *chan,
	uint32_t buffer_index)
{
	struct isp_capture *capture = chan->capture_data;
	struct capture_common_unpins *unpins;
	int i = 0;

	if (capture == NULL) {
		dev_err(chan->isp_dev,
			"%s: isp capture uninitialized\n", __func__);
		return;
	}

	mutex_lock(&capture->program_desc_ctx.unpins_list_lock);
	if (buffer_index >= capture->program_desc_ctx.queue_depth) {
		dev_err(chan->isp_dev,
			"%s: buffer index is out of bound\n", __func__);
		return;
	}
	unpins = &capture->program_desc_ctx.unpins_list[buffer_index];
	if (unpins->num_unpins != 0U) {
		for (i = 0; i < unpins->num_unpins; i++)
			put_mapping(capture->buffer_ctx, unpins->data[i]);
		(void)memset(unpins, 0U, sizeof(*unpins));
	}
	mutex_unlock(&capture->program_desc_ctx.unpins_list_lock);
}

/**
 * @brief Retrieves the number of statistical progress flags set in a program request.
 *
 * This function performs the following operations:
 * - Calculates the offset by multiplying the buffer index with the request size
 *   using @ref __builtin_umul_overflow().
 * - Creates a program handle by adding the offset to the base address of the requests.
 * - Checks if the program handle is valid.
 * - Computes and returns the number of set bits in the stats_aidx_flag field of
 *   the program using @ref hweight32().
 *
 * @param[in]  chan  Pointer to the @ref tegra_isp_channel structure.
 *                   Valid value: non-NULL.
 * @param[in]  req   Pointer to the @ref isp_program_req structure.
 *                   Valid value: non-NULL.
 *
 * @retval 0           If the program handle could not be created.
 * @retval -EOVERFLOW  If the calculation of the offset overflows using
 *                     @ref __builtin_umul_overflow().
 * @retval (int)       Number of set bits in the @c stats_aidx_flag field, as
 *                     computed by @ref hweight32().
 */
static uint32_t isp_capture_get_num_stats_progress(
	struct tegra_isp_channel *chan,
	struct isp_program_req *req)
{
	uint32_t offset = 0U;
	struct isp_desc_rec *program_desc_ctx =
		&chan->capture_data->program_desc_ctx;
	struct isp5_program *program = NULL;

	if (__builtin_umul_overflow(req->buffer_index, program_desc_ctx->request_size, &offset)) {
		dev_dbg(chan->isp_dev,
			"%s: calculation of the offset failed due to an overflow\n", __func__);
		return -EOVERFLOW;
	}

	program = (struct isp5_program *)
		(program_desc_ctx->requests.va + sizeof(struct isp_program_descriptor) + offset);

	if (!program) {
		dev_dbg(chan->isp_dev,
			"%s: could not create program handle from pool\n", __func__);
		return 0U;
	}

	return hweight32(program->stats_aidx_flag);
}

/**
 * @brief Prepares a program request for ISP capture.
 *
 * This function performs the following operations:
 * - Validates the capture data within the provided channel.
 * - Checks if the ISP channel is properly set up by checking that channel
 *   ID is not @ref CAPTURE_CHANNEL_ISP_INVALID_ID.
 * - Validates the program request structure.
 * - Ensures that the unpins list is initialized.
 * - Verifies that the requested buffer index is within the queue depth.
 * - Executes a speculative barrier using @ref spec_bar().
 * - Acquires the reset lock using @ref mutex_lock().
 * - Processes any pending completions if a reset flag is set using
 *   @ref try_wait_for_completion().
 * - Sets reset flag to false.
 * - Releases the reset lock after processing using @ref mutex_unlock().
 * - Acquires the unpins list lock using @ref mutex_lock().
 * - Checks if the program request is still in use.
 * - Retrieves memory information for the specified buffer index.
 * - Calculates the request offset based on the buffer index and request size.
 * - Checks for overflow in the offset calculation using @ref check_add_overflow().
 * - Calls @ref capture_common_pin_and_get_iova() to pin memory and obtain IOVA.
 * - Releases the unpins list lock using @ref mutex_unlock().
 * - Retrieves the number of statistical progress flags by calling
 *   @ref isp_capture_get_num_stats_progress().
 *
 * @param[in]  chan          Pointer to the @ref tegra_isp_channel structure.
 *                           Valid value: non-NULL.
 * @param[in]  req           Pointer to the @ref isp_program_req structure.
 *                           Valid value: non-NULL.
 *
 * @retval 0                On successful preparation of the program request.
 * @retval -ENODEV          If the ISP capture is uninitialized or the channel setup is invalid.
 * @retval -EINVAL          If the program request is invalid, the unpins list is incomplete,
 *                          or the buffer index is out of bounds.
 * @retval -EOVERFLOW       If the offset calculation overflows via @ref check_add_overflow().
 * @retval -EBUSY           If the program request is still in use.
 * @retval (int)            Errors returned from invocation of
 *                          @ref capture_common_pin_and_get_iova().
 */
static int isp_capture_program_prepare(
	struct tegra_isp_channel *chan,
	struct isp_program_req *req)
{
	struct isp_capture *capture = chan->capture_data;
	int err = 0;
	struct memoryinfo_surface *meminfo;
	struct isp_program_descriptor *desc;
	uint32_t request_offset;
	uint32_t mem_offset;

	if (capture == NULL) {
		dev_err(chan->isp_dev,
			"%s: isp capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (capture->channel_id == CAPTURE_CHANNEL_ISP_INVALID_ID) {
		dev_err(chan->isp_dev,
			"%s: setup channel first\n", __func__);
		return -ENODEV;
	}

	if (req == NULL) {
		dev_err(chan->isp_dev,
			"%s: Invalid program req\n", __func__);
		return -EINVAL;
	}

	if (capture->program_desc_ctx.unpins_list == NULL) {
		dev_err(chan->isp_dev, "Channel setup incomplete\n");
		return -EINVAL;
	}

	if (req->buffer_index >= capture->program_desc_ctx.queue_depth) {
		dev_err(chan->isp_dev, "buffer index is out of bound\n");
		return -EINVAL;
	}

	spec_bar();

	mutex_lock(&capture->reset_lock);
	if (capture->reset_capture_program_flag) {
		/* consume any pending completions when coming out of reset */
		while (try_wait_for_completion(&capture->capture_program_resp))
			; /* do nothing */
	}
	capture->reset_capture_program_flag = false;
	mutex_unlock(&capture->reset_lock);

	mutex_lock(&capture->program_desc_ctx.unpins_list_lock);

	if (capture->program_desc_ctx.unpins_list[req->buffer_index].num_unpins != 0) {
		dev_err(chan->isp_dev,
			"%s: program request is still in use by rtcpu\n",
			__func__);
		mutex_unlock(&capture->program_desc_ctx.unpins_list_lock);
		return -EBUSY;
	}

	meminfo = &((struct memoryinfo_surface *)
			capture->program_desc_ctx.requests_memoryinfo)
				[req->buffer_index];

	desc = (struct isp_program_descriptor *)
		(capture->program_desc_ctx.requests.va + req->buffer_index *
				capture->program_desc_ctx.request_size);

	/* Pushbuffer 1 is located after program desc in same ringbuffer */
	request_offset = req->buffer_index *
			capture->program_desc_ctx.request_size;

	if (check_add_overflow((uint32_t)desc->isp_pb1_mem, request_offset, &mem_offset))
		return -EOVERFLOW;

	err = capture_common_pin_and_get_iova(chan->capture_data->buffer_ctx,
		(uint32_t)(desc->isp_pb1_mem >> 32U), /* mem handle */
		mem_offset, /* offset */
		&meminfo->base_address,
		&meminfo->size,
		&capture->program_desc_ctx.unpins_list[req->buffer_index]);

	mutex_unlock(&capture->program_desc_ctx.unpins_list_lock);

	capture->stats_progress_delta = isp_capture_get_num_stats_progress(chan, req);
	return err;
}

/**
 * @brief Cleans up IVC capture by unpinning buffers and synchronizing DMA.
 *
 * This function performs the following operations:
 * - Retrieves the ISP channel from the capture structure.
 * - Validates the ISP channel; if invalid, returns.
 * - Calls @ref isp_capture_request_unpin() with the channel and buffer index.
 * - Synchronizes the DMA buffer range for CPU access by calling
 *   @ref dma_sync_single_range_for_cpu().
 *
 * @param[in]  capture        Pointer to the @ref isp_capture structure.
 *                            Valid value: non-NULL.
 * @param[in]  buffer_index   Index of the buffer to clean up.
 *                            Valid range: [0 .. (queue_depth - 1)].
 */
static inline void isp_capture_ivc_capture_cleanup(
	struct isp_capture *capture,
	uint32_t buffer_index)
{
	struct tegra_isp_channel *chan = capture->isp_channel;

	if (chan == NULL) {
		pr_err("%s: invalid context\n", __func__);
		return;
	}

	isp_capture_request_unpin(chan, buffer_index);
	dma_sync_single_range_for_cpu(capture->rtcpu_dev,
		capture->capture_desc_ctx.requests.iova,
		buffer_index * capture->capture_desc_ctx.request_size,
		capture->capture_desc_ctx.request_size,
		DMA_FROM_DEVICE);
}

/**
 * @brief Signals capture progress status or completes the capture response.
 *
 * This function performs the following operations:
 * - Checks if the progress status notifier is set.
 * - If set, calls @ref capture_common_set_progress_status() with the progress
 *   status notifier, buffer index, progress status buffer depth, and completion
 *   status.
 * - Otherwise, completes the capture response by calling @ref complete().
 *
 * @param[in]  capture        Pointer to the @ref isp_capture structure.
 *                            Valid value: non-NULL.
 * @param[in]  buffer_index   Index of the buffer to signal.
 *                            Valid range: [0 .. (queue_depth - 1)].
 */
static inline void isp_capture_ivc_capture_signal(
	struct isp_capture *capture,
	uint32_t buffer_index)
{
	if (capture->is_progress_status_notifier_set) {
		(void)capture_common_set_progress_status(
			&capture->progress_status_notifier,
			buffer_index,
			capture->capture_desc_ctx.progress_status_buffer_depth,
			PROGRESS_STATUS_DONE);
	} else {
		/*
		 * Only fire completions if not using
		 * the new progress status buffer mechanism
		 */
		complete(&capture->capture_resp);
	}
}

/**
 * @brief Cleans up program resources for a specified ISP capture buffer.
 *
 * This function performs the following operations:
 * - Retrieves the ISP channel from the capture structure.
 * - Validates the ISP channel; if invalid, returns early.
 * - Calls @ref isp_capture_program_request_unpin() with the channel and buffer index.
 * - Synchronizes the DMA buffer range for CPU access by calling
 *   @ref dma_sync_single_range_for_cpu().
 *
 * @param[in]  capture        Pointer to the @ref isp_capture structure.
 *                            Valid value: non-NULL.
 * @param[in]  buffer_index   Index of the buffer to clean up.
 *                            Valid range: [0 .. (queue_depth - 1)].
 */
static inline void isp_capture_ivc_program_cleanup(
	struct isp_capture *capture,
	uint32_t buffer_index)
{
	struct tegra_isp_channel *chan = capture->isp_channel;

	if (chan == NULL) {
		pr_err("%s: invalid context\n", __func__);
		return;
	}

	isp_capture_program_request_unpin(chan, buffer_index);
	dma_sync_single_range_for_cpu(capture->rtcpu_dev,
		capture->program_desc_ctx.requests.iova,
		buffer_index * capture->program_desc_ctx.request_size,
		capture->program_desc_ctx.request_size,
		DMA_FROM_DEVICE);
}

/**
 * @brief Signals ISP program progress status or completes the program response.
 *
 * This function performs the following operations:
 * - Checks if the progress status notifier is set.
 * - If set:
 *   - Finds the buffer slot by summing buffer index and progress status buffer depth
 *     and checks for overflow by calling @ref check_add_overflow().
 *   - Finds the buffer depth by summing the the program status buffer depth and
 *     progress status buffer depth and checks for overflow by calling
 *     @ref check_add_overflow().
 *   - Returns immediately in case of overflow.
 *   - Calls @ref capture_common_set_progress_status() with the progress status
 *     notifier, the two above sums, and @ref PROGRESS_STATUS_DONE.
 * - Otherwise:
 *   - Completes the program response by calling @ref complete().
 *
 * @param[in]  capture        Pointer to the @ref isp_capture structure.
 *                            Valid value: non-NULL.
 * @param[in]  buffer_index   Index of the program descriptor buffer to signal.
 *                            Valid range: [0 .. (queue_depth - 1)].
 */
static inline void isp_capture_ivc_program_signal(
	struct isp_capture *capture,
	uint32_t buffer_index)
{
	uint32_t buffer_slot = 0;
	uint32_t buffer_depth = 0;

	if (capture->is_progress_status_notifier_set) {
		if (check_add_overflow(buffer_index,
			capture->capture_desc_ctx.progress_status_buffer_depth, &buffer_slot))
			return;

		if (check_add_overflow(capture->program_desc_ctx.progress_status_buffer_depth,
			capture->capture_desc_ctx.progress_status_buffer_depth, &buffer_depth))
			return;

		/*
		 * Program status notifiers are after the process status
		 * notifiers; add the process status buffer depth as an offset.
		 */
		(void)capture_common_set_progress_status(
			&capture->progress_status_notifier,
			buffer_slot,
			buffer_depth,
			PROGRESS_STATUS_DONE);
	} else {
		/*
		 * Only fire completions if not using
		 * the new progress status buffer mechanism
		 */
		complete(&capture->capture_program_resp);
	}
}

/**
 * @brief Handles IVC status callbacks for ISP capture.
 *
 * This function performs the following operations:
 * - Casts @a ivc_resp to a @ref CAPTURE_MSG type.
 * - Casts @a pcontext to a @ref isp_capture type.
 * - Validates that the @ref CAPTURE_MSG, @ref isp_capture, and ISP channel are
 *   initialized. If invalid, returns immediately.
 * - Processes the status message based on the message ID:
 *   - For @ref CAPTURE_ISP_STATUS_IND:
 *     - Retrieves the buffer index.
 *     - Calls @ref isp_capture_ivc_capture_cleanup() and
 *       @ref isp_capture_ivc_capture_signal().
 *   - For @ref CAPTURE_ISP_PROGRAM_STATUS_IND:
 *     - Retrieves the buffer index.
 *     - Calls @ref isp_capture_ivc_program_cleanup() and
 *       @ref isp_capture_ivc_program_signal().
 *   - For @ref CAPTURE_ISP_EX_STATUS_IND:
 *     - Retrieves the process buffer index and program buffer index.
 *     - Calls @ref isp_capture_ivc_program_cleanup(),
 *       @ref isp_capture_ivc_capture_cleanup(), and
 *       @ref isp_capture_ivc_capture_signal().
 *   - For unknown message IDs, logs an error.
 *
 * @param[in]  ivc_resp       Pointer to the IVC response data.
 *                            Valid value: non-NULL.
 * @param[in]  pcontext       Pointer to the ISP channel capture context structure.
 *                            Valid value: non-NULL.
 */
static void isp_capture_ivc_status_callback(
	const void *ivc_resp,
	const void *pcontext)
{
	struct CAPTURE_MSG *status_msg = (struct CAPTURE_MSG *)ivc_resp;
	struct isp_capture *capture = (struct isp_capture *)pcontext;
	struct tegra_isp_channel *chan;
	uint32_t buffer_index;

	if (unlikely(status_msg == NULL)) {
		pr_err("%s: invalid context\n", __func__);
		return;
	}

	if (unlikely(capture == NULL)) {
		pr_err("%s: invalid context\n", __func__);
		return;
	}

	chan = capture->isp_channel;
	if (unlikely(chan == NULL)) {
		pr_err("%s: invalid context\n", __func__);
		return;
	}

	switch (status_msg->header.msg_id) {
	case CAPTURE_ISP_STATUS_IND:
		buffer_index = status_msg->capture_isp_status_ind.buffer_index;
		isp_capture_ivc_capture_cleanup(capture, buffer_index);
		isp_capture_ivc_capture_signal(capture, buffer_index);
		dev_dbg(chan->isp_dev, "%s: status chan_id %u msg_id %u\n",
			__func__, status_msg->header.channel_id,
			status_msg->header.msg_id);
		break;
	case CAPTURE_ISP_PROGRAM_STATUS_IND:
		buffer_index =
			status_msg->capture_isp_program_status_ind.buffer_index;
		isp_capture_ivc_program_cleanup(capture, buffer_index);
		isp_capture_ivc_program_signal(capture, buffer_index);
		dev_dbg(chan->isp_dev,
			"%s: isp_ program status chan_id %u msg_id %u\n",
			__func__, status_msg->header.channel_id,
			status_msg->header.msg_id);
		break;
	case CAPTURE_ISP_EX_STATUS_IND:
		buffer_index =
			status_msg->capture_isp_ex_status_ind
			.process_buffer_index;
		isp_capture_ivc_program_cleanup(capture,
			status_msg->capture_isp_ex_status_ind
			.program_buffer_index);
		isp_capture_ivc_capture_cleanup(capture, buffer_index);
		isp_capture_ivc_capture_signal(capture, buffer_index);

		dev_dbg(chan->isp_dev,
			"%s: isp extended status chan_id %u msg_id %u\n",
			__func__, status_msg->header.channel_id,
			status_msg->header.msg_id);
		break;
	default:
		dev_err(chan->isp_dev,
			"%s: unknown capture resp", __func__);
		break;
	}
}

/**
 * @brief Sends a @em capture-control message over IVC for ISP capture.
 *
 * This function performs the following operations:
 * - Retrieves the capture data from the provided channel.
 * - Validates that the capture data is initialized.
 * - Logs a debug message indicating the message being sent using @ref dev_dbg().
 * - Sets the response message ID to the provided @a resp_id.
 * - Acquires the control message lock using @ref mutex_lock().
 * - Sends the control message by calling @ref tegra_capture_ivc_control_submit().
 * - Waits for the capture response with a timeout by calling
 *   @ref wait_for_completion_timeout().
 * - If the response is not received within the timeout,
 *   log the RCE snapshot by calling @ref rtcpu_trace_panic_callback().
 * - Compares the response header with the expected header using @ref memcmp().
 * - Releases the control message lock using @ref mutex_unlock().
 * - Logs a debug message indicating the received response using @ref dev_dbg().
 *
 * @param[in]  chan         Pointer to the @ref tegra_isp_channel structure.
 *                          Valid value: non-NULL.
 * @param[in]  msg          Pointer to the @ref CAPTURE_CONTROL_MSG structure.
 *                          Valid value: non-NULL.
 * @param[in]  size         Size of the control message in bytes.
 *                          Valid range: depends on context.
 * @param[in]  resp_id      Response message ID to set.
 *                          Valid range: depends on context.
 *
 * @retval 0               On successful sending of the control message and
 *                         receiving the expected response.
 * @retval -ENODEV         If the ISP capture context is invalid.
 * @retval -ETIMEDOUT      If waiting for the capture response times out using
 *                         @ref wait_for_completion_timeout().
 * @retval -EINVAL         If the response received is unexpected as determined by
 *                         @ref memcmp().
 * @retval (int)           Errors returned from invocation of
 *                         @ref tegra_capture_ivc_control_submit().
 */
static int isp_capture_ivc_send_control(struct tegra_isp_channel *chan,
		const struct CAPTURE_CONTROL_MSG *msg, size_t size,
		uint32_t resp_id)
{
	struct isp_capture *capture = chan->capture_data;
	struct CAPTURE_MSG_HEADER resp_header = msg->header;
	uint32_t timeout = HZ;
	int err = 0;

	if (capture == NULL) {
		pr_err("%s: invalid context\n", __func__);
		return -ENODEV;
	}

	dev_dbg(chan->isp_dev, "%s: sending chan_id %u msg_id %u\n",
			__func__, resp_header.channel_id, resp_header.msg_id);

	resp_header.msg_id = resp_id;

	/* Send capture control IVC message */
	mutex_lock(&capture->control_msg_lock);
	err = tegra_capture_ivc_control_submit(msg, size);
	if (err < 0) {
		dev_err(chan->isp_dev, "IVC control submit failed\n");
		goto fail;
	}

	timeout = wait_for_completion_timeout(&capture->control_resp, timeout);
	if (timeout <= 0) {
		dev_err(chan->isp_dev,
			"isp capture control message timed out\n");
		rtcpu_trace_panic_callback(capture->rtcpu_dev);
		err = -ETIMEDOUT;
		goto fail;
	}

	if (memcmp(&resp_header, &capture->control_resp_msg.header,
			sizeof(resp_header)) != 0) {
		dev_err(chan->isp_dev,
			"unexpected response from camera processor\n");
		err = -EINVAL;
		goto fail;
	}
	mutex_unlock(&capture->control_msg_lock);

	dev_dbg(chan->isp_dev, "%s: response chan_id %u msg_id %u\n",
			__func__, capture->control_resp_msg.header.channel_id,
			capture->control_resp_msg.header.msg_id);
	return 0;

fail:
	mutex_unlock(&capture->control_msg_lock);
	return err;
}

/**
 * @brief Handles control callbacks for ISP capture over IVC.
 *
 * This function performs the following operations:
 * - Casts @a ivc_resp to a @ref CAPTURE_CONTROL_MSG type.
 * - Casts @a pcontext to a @ref isp_capture type.
 * - Validates that the @ref CAPTURE_CONTROL_MSG, @ref isp_capture, and ISP channel are
 *   initialized. If not, returns immediately.
 * - Retrieves the ISP channel from the capture structure.
 * - Processes the control message based on the message ID:
 *   - For @ref CAPTURE_CHANNEL_ISP_SETUP_RESP, @ref CAPTURE_CHANNEL_ISP_RESET_RESP, and
 *     @ref CAPTURE_CHANNEL_ISP_RELEASE_RESP:
 *     - Copies the control message to the capture response message using @ref memcpy().
 *     - Completes the capture response by calling @ref complete().
 *   - For unknown message IDs:
 *     - Logs an error using @ref dev_err().
 *
 * @param[in]  ivc_resp  Pointer to the IVC response data.
 *                       Valid value: non-NULL.
 * @param[in]  pcontext  Pointer to the context structure.
 *                       Valid value: non-NULL.
 */
static void isp_capture_ivc_control_callback(
	const void *ivc_resp,
	const void *pcontext)
{
	const struct CAPTURE_CONTROL_MSG *control_msg = ivc_resp;
	struct isp_capture *capture = (struct isp_capture *)pcontext;
	struct tegra_isp_channel *chan;

	if (unlikely(control_msg == NULL)) {
		pr_err("%s: invalid context\n", __func__);
		return;
	}

	if (unlikely(capture == NULL)) {
		pr_err("%s: invalid context\n", __func__);
		return;
	}

	chan = capture->isp_channel;
	if (unlikely(chan == NULL)) {
		pr_err("%s: invalid context\n", __func__);
		return;
	}

	switch (control_msg->header.msg_id) {
	case CAPTURE_CHANNEL_ISP_SETUP_RESP:
	case CAPTURE_CHANNEL_ISP_RESET_RESP:
	case CAPTURE_CHANNEL_ISP_RELEASE_RESP:
		memcpy(&capture->control_resp_msg, control_msg,
				sizeof(*control_msg));
		complete(&capture->control_resp);
		break;
	default:
		dev_err(chan->isp_dev,
			"%s: unknown capture isp control resp", __func__);
		break;
	}
}

/**
 * @brief Initializes the ISP capture channel.
 *
 * This function performs the following operations:
 * - Validates the input channel pointer is not NULL.
 * - Calls @ref of_find_node_by_path() to locate the rtcpu device node.
 * - Checks the availability of the device node using @ref of_device_is_available().
 * - Calls @ref of_find_device_by_node() to obtain the rtcpu platform device.
 * - Allocates memory for the ISP capture structure using @ref kzalloc().
 * - Initializes completion variables with @ref init_completion().
 * - Initializes mutexes using @ref mutex_init().
 * - Associates the capture structure with the provided channel.
 * - Sets channel ID to @ref CAPTURE_CHANNEL_ISP_INVALID_ID.
 * - Sets reset flags to false.
 *
 * @param[in]  chan    Pointer to the @ref tegra_isp_channel structure.
 *                      Valid value: non-NULL.
 *
 * @retval 0           On successful initialization.
 * @retval -ENODEV     If the channel context is invalid,
 *                     @ref of_find_node_by_path(),
 *                     @ref of_device_is_available(),
 *                     @ref of_find_device_by_node(),
 *                     @ref init_completion(), or
 *                     @ref mutex_init() fails.
 * @retval -ENOMEM     If memory allocation via @ref kzalloc() fails.
 */
int isp_capture_init(
	struct tegra_isp_channel *chan)
{
	struct isp_capture *capture;
	struct device_node *node;
	struct platform_device *rtc_pdev;

	if (unlikely(chan == NULL)) {
		pr_err("%s: invalid context\n", __func__);
		return -ENODEV;
	}

	dev_dbg(chan->isp_dev, "%s++\n", __func__);
	node = of_find_node_by_path("tegra-camera-rtcpu");
	if (of_device_is_available(node) == 0) {
		dev_err(chan->isp_dev, "failed to find rtcpu device node\n");
		return -ENODEV;
	}
	rtc_pdev = of_find_device_by_node(node);
	if (rtc_pdev == NULL) {
		dev_err(chan->isp_dev, "failed to find rtcpu platform\n");
		return -ENODEV;
	}

	capture = kzalloc(sizeof(*capture), GFP_KERNEL);
	if (unlikely(capture == NULL)) {
		dev_err(chan->isp_dev, "failed to allocate capture channel\n");
		return -ENOMEM;
	}

	capture->rtcpu_dev = &rtc_pdev->dev;

	init_completion(&capture->control_resp);
	init_completion(&capture->capture_resp);
	init_completion(&capture->capture_program_resp);

	mutex_init(&capture->control_msg_lock);
	mutex_init(&capture->capture_desc_ctx.unpins_list_lock);
	mutex_init(&capture->program_desc_ctx.unpins_list_lock);
	mutex_init(&capture->reset_lock);

	capture->isp_channel = chan;
	chan->capture_data = capture;

	capture->channel_id = CAPTURE_CHANNEL_ISP_INVALID_ID;

	capture->reset_capture_program_flag = false;
	capture->reset_capture_flag = false;

	return 0;
}

/**
 * @brief Shuts down the ISP capture functionality for a given channel.
 *
 * This function performs the following operations:
 * - Validates the input channel is non-NULL.
 * - Retrieves the capture data from the channel.
 * - If the capture channel ID is valid:
 *   - Calls @ref isp_capture_reset().
 *   - Calls @ref isp_capture_release().
 * - Frees the capture structure using @ref kfree().
 * - Sets the channel's capture data pointer to NULL.
 *
 * @param[in]  chan          Pointer to the @ref tegra_isp_channel structure.
 *                           Valid Value: non-NULL.
 */
void isp_capture_shutdown(
	struct tegra_isp_channel *chan)
{
	struct isp_capture *capture;

	if (unlikely(chan == NULL)) {
		pr_err("%s: invalid context\n", __func__);
		return;
	}

	capture = chan->capture_data;

	dev_dbg(chan->isp_dev, "%s--\n", __func__);
	if (capture == NULL)
		return;

	if (capture->channel_id != CAPTURE_CHANNEL_ISP_INVALID_ID) {
		/* No valid ISP reset flags defined now, use zero */
		isp_capture_reset(chan, 0);
		isp_capture_release(chan, 0);
	}

	kfree(capture);
	chan->capture_data = NULL;
}

/**
 * @brief Initializes the NvHost device for the ISP capture channel.
 *
 * This function performs the following operations:
 * - Retrieves driver data using @ref platform_get_drvdata().
 * - Validates the ISP capture setup structure by checking if @a setup is non-NULL.
 * - Extracts the ISP unit index from the setup structure.
 * - Checks if the ISP unit index is within the valid range.
 * - Assigns the NvHost device and corresponding node to the ISP channel.
 *
 * @param[in]  chan          Pointer to the @ref tegra_isp_channel structure.
 *                           Valid value: non-NULL.
 * @param[in]  setup         Pointer to the @ref isp_capture_setup structure.
 *                           Valid value: non-NULL.
 */
void isp_get_nvhost_device(
	struct tegra_isp_channel *chan,
	struct isp_capture_setup *setup)
{
	uint32_t isp_inst = 0U;
	struct tegra_capture_isp_data *info =
		platform_get_drvdata(chan->isp_capture_pdev);

	if (setup == NULL) {
		pr_err("%s: Invalid ISP capture request\n", __func__);
		return;
	}

	isp_inst = setup->isp_unit;

	if (isp_inst >= MAX_ISP_UNITS) {
		pr_err("%s: ISP unit index is out of bound\n", __func__);
		return;
	}

	if (info->isp_pdevices[isp_inst] == NULL) {
		pr_err("%s:ISP devices[%u] is NULL\n", __func__, isp_inst);
		return;
	}
	chan->isp_dev = &info->isp_pdevices[isp_inst]->dev;
	chan->ndev = info->isp_pdevices[isp_inst];
}

/**
 * @brief Initializes capture descriptors for the ISP channel.
 *
 * This function performs the following operations:
 * - Pins the process descriptor ring buffer to RTCPU by calling
 *   @ref capture_common_pin_memory().
 * - Pins the process descriptor ring buffer to ISP by calling
 *   @ref capture_buffer_add().
 * - Caches ISP capture descriptor ring buffer details.
 * - Allocates the unpins list based on the queue depth using @ref vzalloc().
 *   - If allocation fails, unpins the memory using @ref capture_common_unpin_memory()
 *     and frees allocated memory with @ref vfree().
 * - Allocates memory info ring buffer for ISP capture descriptors using
 *   @ref dma_alloc_coherent().
 *   - If allocation fails, frees the unpins list using @ref vfree()
 *     and unpins the memory with @ref capture_common_unpin_memory().
 *
 * @param[in]  chan        Pointer to the @ref tegra_isp_channel structure.
 *                         Valid value: non-NULL.
 * @param[in]  capture     Pointer to the @ref isp_capture structure.
 *                         Valid value: non-NULL.
 * @param[in]  setup       Pointer to the @ref isp_capture_setup structure.
 *                         Valid value: non-NULL.
 * @param[in]  buffer_ctx  Pointer to the @ref capture_buffer_table structure.
 *                         Valid value: non-NULL.
 *
 * @retval 0                On successful setup of capture descriptors.
 * @retval -ENOMEM          If memory allocation with @ref vzalloc() or
 *                          @ref dma_alloc_coherent() fails.
 * @retval (int)            If @ref capture_common_pin_memory() or
 *                          @ref capture_buffer_add() fails.
 */
static int setup_capture_descriptors(
	struct tegra_isp_channel *chan,
	struct isp_capture *capture,
	const struct isp_capture_setup *setup,
	struct capture_buffer_table *buffer_ctx)
{
	int err;

	/* pin the process descriptor ring buffer to RTCPU */
	dev_dbg(chan->isp_dev, "%s: descr buffer handle 0x%x\n", __func__, setup->mem);
	err = capture_common_pin_memory(capture->rtcpu_dev,
			setup->mem, &capture->capture_desc_ctx.requests);
	if (err < 0) {
		dev_err(chan->isp_dev, "%s: memory setup failed\n", __func__);
		return err;
	}

	/* pin the process descriptor ring buffer to ISP */
	err = capture_buffer_add(buffer_ctx, setup->mem);
	if (err < 0) {
		dev_err(chan->isp_dev, "%s: memory setup failed\n", __func__);
		return err;
	}

	/* cache isp capture desc ring buffer details */
	capture->capture_desc_ctx.queue_depth = setup->queue_depth;
	capture->capture_desc_ctx.request_size = setup->request_size;
	capture->capture_desc_ctx.request_buf_size = setup->request_size *
							setup->queue_depth;

	/* allocate isp capture desc unpin list based on queue depth */
	capture->capture_desc_ctx.unpins_list = vzalloc(
		capture->capture_desc_ctx.queue_depth *
			sizeof(*capture->capture_desc_ctx.unpins_list));
	if (unlikely(capture->capture_desc_ctx.unpins_list == NULL)) {
		dev_err(chan->isp_dev, "failed to allocate unpins array\n");
		capture_common_unpin_memory(&capture->capture_desc_ctx.requests);
		return -ENOMEM;
	}

	/* Allocate memory info ring buffer for isp capture descriptors */
	capture->capture_desc_ctx.requests_memoryinfo =
		dma_alloc_coherent(capture->rtcpu_dev,
			capture->capture_desc_ctx.queue_depth *
			sizeof(struct isp_capture_descriptor_memoryinfo),
			&capture->capture_desc_ctx.requests_memoryinfo_iova,
			GFP_KERNEL);

	if (!capture->capture_desc_ctx.requests_memoryinfo) {
		dev_err(chan->isp_dev,
			"%s: capture_desc_ctx meminfo alloc failed\n",
			__func__);
		vfree(capture->capture_desc_ctx.unpins_list);
		capture_common_unpin_memory(&capture->capture_desc_ctx.requests);
		return -ENOMEM;
	}

	return 0;
}

/**
 * @brief Sets up program descriptors for the ISP channel.
 *
 * This function performs the following operations:
 * - Pins the ISP program descriptor ring buffer to RTCPU by calling
 *   @ref capture_common_pin_memory().
 * - Pins the ISP program descriptor ring buffer to ISP by calling
 *   @ref capture_buffer_add().
 *   - If the memory setup fails, unpins memory using
 *     @ref capture_common_unpin_memory().
 * - Caches ISP program descriptor ring buffer details to ISP channel capture context.
 * - Allocates the ISP program unpin list based on the queue depth using
 *   @ref vzalloc().
 *   - If allocation fails, unpins memory using
 *     @ref capture_common_unpin_memory() and frees the unpins list with
 *     @ref vfree().
 * - Allocates memory info ring buffer for ISP program descriptors using
 *   @ref dma_alloc_coherent().
 *   - If allocation fails, frees the unpins list with @ref vfree() and
 *     unpins memory using @ref capture_common_unpin_memory().
 *
 *
 * @param[in]      chan        Pointer to the @ref tegra_isp_channel structure.
 *                             Valid Value: non-NULL.
 * @param[in, out] capture     Pointer to the @ref isp_capture structure.
 *                             Valid Value: non-NULL.
 * @param[in]      setup       Pointer to the @ref isp_capture_setup structure.
 *                             Valid Value: non-NULL.
 * @param[in, out] buffer_ctx  Pointer to the @ref capture_buffer_table structure.
 *                             Valid Value: non-NULL.
 *
 * @retval 0                On successful setup of program descriptors.
 * @retval -ENOMEM          If memory allocation with @ref vzalloc() or
 *                          @ref dma_alloc_coherent() fails.
 * @retval (int)            If @ref capture_common_pin_memory() or
 *                          @ref capture_buffer_add() fails.
 */
static int setup_program_descriptors(
	struct tegra_isp_channel *chan,
	struct isp_capture *capture,
	const struct isp_capture_setup *setup,
	struct capture_buffer_table *buffer_ctx)
{
	int err;

	/* pin the isp program descriptor ring buffer */
	dev_dbg(chan->isp_dev, "%s: descr buffer handle %u\n", __func__, setup->isp_program_mem);
	err = capture_common_pin_memory(capture->rtcpu_dev,
				setup->isp_program_mem,
				&capture->program_desc_ctx.requests);
	if (err < 0) {
		dev_err(chan->isp_dev,
			"%s: isp_program memory setup failed\n", __func__);
		return err;
	}

	/* pin the isp program descriptor ring buffer to ISP */
	err = capture_buffer_add(buffer_ctx, setup->isp_program_mem);
	if (err < 0) {
		dev_err(chan->isp_dev, "%s: isp_program memory setup failed\n", __func__);
		capture_common_unpin_memory(&capture->program_desc_ctx.requests);
		return err;
	}

	/* cache isp program desc ring buffer details */
	capture->program_desc_ctx.queue_depth = setup->isp_program_queue_depth;
	capture->program_desc_ctx.request_size =
					setup->isp_program_request_size;
	capture->program_desc_ctx.request_buf_size =
					setup->isp_program_request_size *
						setup->isp_program_queue_depth;

	/* allocate isp program unpin list based on queue depth */
	capture->program_desc_ctx.unpins_list = vzalloc(
			capture->program_desc_ctx.queue_depth *
				sizeof(*capture->program_desc_ctx.unpins_list));
	if (unlikely(capture->program_desc_ctx.unpins_list == NULL)) {
		dev_err(chan->isp_dev,
			"failed to allocate isp program unpins array\n");
		capture_common_unpin_memory(&capture->program_desc_ctx.requests);
		return -ENOMEM;
	}

	/* Allocate memory info ring buffer for program descriptors */
	capture->program_desc_ctx.requests_memoryinfo =
		dma_alloc_coherent(capture->rtcpu_dev,
			capture->program_desc_ctx.queue_depth *
			sizeof(struct memoryinfo_surface),
			&capture->program_desc_ctx.requests_memoryinfo_iova,
			GFP_KERNEL);

	if (!capture->program_desc_ctx.requests_memoryinfo) {
		dev_err(chan->isp_dev,
			"%s: program_desc_ctx meminfo alloc failed\n",
				__func__);
		vfree(capture->program_desc_ctx.unpins_list);
		capture_common_unpin_memory(&capture->program_desc_ctx.requests);
		return -ENOMEM;
	}

	return 0;
}

/**
 * @brief Initializes the ISP capture setup for the given channel.
 *
 * This function performs the following operations:
 * - Retrieves driver data by calling @ref platform_get_drvdata().
 * - Validates that the provided channel is initialized.
 * - Retrieves and validates the capture data from the channel.
 * - Checks if the capture channel is already set up by verifying channel ID.
 * - Validates setup parameters including channel flags, queue depth, and request size are
 *   non-zero, and ISP unit ID is within the number of configured ISP devices.
 * - Creates a buffer table by calling @ref create_buffer_table().
 * - Sets up capture descriptors by calling @ref setup_capture_descriptors().
 * - Sets up program descriptors by calling @ref setup_program_descriptors().
 * - Sets up synchronization points by calling @ref isp_capture_setup_syncpts().
 * - Registers a control callback by calling @ref tegra_capture_ivc_register_control_cb().
 * - Initializes the control message structure.
 * - Populates a @ref capture_channel_isp_config configuration structure using properties of
 *   input @ref isp_capture_setup structure.
 * - If ISP GOS tables are enabled, configures them accordingly.
 *   - Iterates through configured GOS tables and assigns ISP GOS tables in the capture channel
 *     to ISP GOS tables in the channel configuration structure.
 * - Sends the control message by calling @ref isp_capture_ivc_send_control().
 * - Checks the response message is successful.
 * - Sets the channel ID based on the response message.
 * - Notifies the channel ID by calling @ref tegra_capture_ivc_notify_chan_id().
 * - Registers a capture callback by calling @ref tegra_capture_ivc_register_capture_cb().
 * - Assigns the buffer context to the capture structure.
 * - In case of any failure, releases resources by invoking:
 *   - @ref isp_capture_release() to release input @ref tegra_isp_channel structure in case of
 *     callback update or registration failure.
 *   - @ref destroy_buffer_table() to destroy buffer table.
 *   - @ref tegra_capture_ivc_unregister_control_cb() to unregister the control callback.
 *   - @ref dma_free_coherent() to free memory info ring buffers.
 *   - @ref vfree() to free allocated unpins lists.
 *   - @ref capture_common_unpin_memory() to unpin the program and capture descriptor requests
 *     memory.
 *
 * @param[in]  chan        Pointer to the @ref tegra_isp_channel structure.
 *                         Valid value: non-NULL.
 * @param[in]  setup       Pointer to the @ref isp_capture_setup structure.
 *                         Valid value: non-NULL.
 *
 * @retval 0                On successful setup of capture descriptors.
 * @retval -ENODEV          If the channel is NULL, capture is uninitialized, or external
 *                          functions such as @ref platform_get_drvdata() fail.
 * @retval -EEXIST          If the capture channel is already set up.
 * @retval -EINVAL          If the capture setup parameters are invalid.
 * @retval -ENOMEM          If memory allocation with @ref create_buffer_table(),
 *                          @ref setup_capture_descriptors(), @ref setup_program_descriptors(),
 *                          @ref vzalloc(), or @ref dma_alloc_coherent() fails.
 * @retval -EIO             If the capture response is unexpected via
 *                          @ref isp_capture_ivc_send_control().
 * @retval (int)            Errors returned from external functions such as
 *                          @ref capture_common_pin_memory(),
 *                          @ref capture_buffer_add(),
 *                          @ref setup_capture_descriptors(),
 *                          @ref setup_program_descriptors(),
 *                          @ref isp_capture_setup_syncpts(),
 *                          @ref tegra_capture_ivc_register_control_cb(),
 *                          @ref tegra_capture_ivc_notify_chan_id(),
 *                          or @ref tegra_capture_ivc_register_capture_cb().
 */
int isp_capture_setup(
	struct tegra_isp_channel *chan,
	struct isp_capture_setup *setup)
{
	struct capture_buffer_table *buffer_ctx;
	struct isp_capture *capture;
	uint32_t transaction;
	struct CAPTURE_CONTROL_MSG control_msg;
	struct CAPTURE_CONTROL_MSG *resp_msg;
	struct capture_channel_isp_config *config;
	int err = 0;
#ifdef HAVE_ISP_GOS_TABLES
	int i;
#endif

	struct tegra_capture_isp_data *info =
			platform_get_drvdata(chan->isp_capture_pdev);

	if (unlikely(chan == NULL)) {
		pr_err("%s: invalid context\n", __func__);
		return -ENODEV;
	}

	capture = chan->capture_data;
	if (capture == NULL) {
		dev_err(chan->isp_dev,
			"%s: isp capture uninitialized\n", __func__);
		return -ENODEV;
	}

	resp_msg = &capture->control_resp_msg;
	config = &control_msg.channel_isp_setup_req.channel_config;

	nv_camera_log(chan->ndev,
		__arch_counter_get_cntvct(),
		NVHOST_CAMERA_ISP_CAPTURE_SETUP);

	if (capture->channel_id != CAPTURE_CHANNEL_ISP_INVALID_ID) {
		dev_err(chan->isp_dev,
			"%s: already setup, release first\n", __func__);
		return -EEXIST;
	}

	dev_dbg(chan->isp_dev, "chan flags %u\n", setup->channel_flags);
	dev_dbg(chan->isp_dev, "queue depth %u\n", setup->queue_depth);
	dev_dbg(chan->isp_dev, "request size %u\n", setup->request_size);
	dev_dbg(chan->isp_dev, "isp unit %u\n", setup->isp_unit);

	if (setup->channel_flags == 0 ||
			setup->queue_depth == 0 ||
			setup->request_size == 0 ||
			setup->isp_unit >= info->num_isp_devices)
		return -EINVAL;

	buffer_ctx = create_buffer_table(chan->isp_dev);
	if (unlikely(buffer_ctx == NULL)) {
		dev_err(chan->isp_dev, "cannot setup buffer context");
		return -ENOMEM;
	}

	err = setup_capture_descriptors(chan, capture, setup, buffer_ctx);
	if (err < 0)
		goto pin_fail;

	err = setup_program_descriptors(chan, capture, setup, buffer_ctx);
	if (err < 0)
		goto capture_meminfo_alloc_fail;

	err = isp_capture_setup_syncpts(chan);
	if (err < 0) {
		dev_err(chan->isp_dev, "%s: syncpt setup failed\n", __func__);
		goto program_meminfo_alloc_fail;
	}

	err = tegra_capture_ivc_register_control_cb(
			&isp_capture_ivc_control_callback,
			&transaction, capture);
	if (err < 0) {
		dev_err(chan->isp_dev, "failed to register control callback\n");
		goto control_cb_fail;
	}

	/* Fill in control config msg to be sent over ctrl ivc chan to RTCPU */
	memset(&control_msg, 0, sizeof(control_msg));

	control_msg.header.msg_id = CAPTURE_CHANNEL_ISP_SETUP_REQ;
	control_msg.header.transaction = transaction;

	config->channel_flags = setup->channel_flags;

	config->isp_unit_id = setup->isp_unit;

	config->request_queue_depth = setup->queue_depth;
	config->request_size = setup->request_size;
	config->requests = capture->capture_desc_ctx.requests.iova;
	config->requests_memoryinfo =
		capture->capture_desc_ctx.requests_memoryinfo_iova;
	config->request_memoryinfo_size =
		sizeof(struct isp_capture_descriptor_memoryinfo);

	config->program_queue_depth = setup->isp_program_queue_depth;
	config->program_size = setup->isp_program_request_size;
	config->programs = capture->program_desc_ctx.requests.iova;
	config->programs_memoryinfo =
		capture->program_desc_ctx.requests_memoryinfo_iova;
	config->program_memoryinfo_size =
		sizeof(struct memoryinfo_surface);

	config->progress_sp = capture->progress_sp;
	config->stats_progress_sp = capture->stats_progress_sp;

#ifdef HAVE_ISP_GOS_TABLES
	dev_dbg(chan->isp_dev, "%u GoS tables configured.\n",
		capture->num_gos_tables);
	for (i = 0; i < capture->num_gos_tables; i++) {
		config->isp_gos_tables[i] = (iova_t)capture->gos_tables[i];
		dev_dbg(chan->isp_dev, "gos[%d] = 0x%08llx\n",
			i, (u64)capture->gos_tables[i]);
	}
	config->num_isp_gos_tables = capture->num_gos_tables;
#endif

	err = isp_capture_ivc_send_control(chan, &control_msg,
			sizeof(control_msg), CAPTURE_CHANNEL_ISP_SETUP_RESP);
	if (err < 0)
		goto submit_fail;

	if (resp_msg->channel_isp_setup_resp.result != CAPTURE_OK) {
		dev_err(chan->isp_dev, "%s: control failed, errno %d", __func__,
			resp_msg->channel_setup_resp.result);
		err = -EIO;
		goto submit_fail;
	}

	capture->channel_id = resp_msg->channel_isp_setup_resp.channel_id;

	err = tegra_capture_ivc_notify_chan_id(capture->channel_id,
			transaction);
	if (err < 0) {
		dev_err(chan->isp_dev, "failed to update control callback\n");
		goto cb_fail;
	}

	err = tegra_capture_ivc_register_capture_cb(
			&isp_capture_ivc_status_callback,
			capture->channel_id, capture);
	if (err < 0) {
		dev_err(chan->isp_dev, "failed to register capture callback\n");
		goto cb_fail;
	}

	capture->buffer_ctx = buffer_ctx;

	return 0;

cb_fail:
	if (isp_capture_release(chan, CAPTURE_CHANNEL_RESET_FLAG_IMMEDIATE))
		destroy_buffer_table(buffer_ctx);
	return err;
submit_fail:
	tegra_capture_ivc_unregister_control_cb(transaction);
control_cb_fail:
	isp_capture_release_syncpts(chan);
program_meminfo_alloc_fail:
	dma_free_coherent(capture->rtcpu_dev,
		capture->program_desc_ctx.queue_depth *
			sizeof(struct memoryinfo_surface),
		capture->program_desc_ctx.requests_memoryinfo,
		capture->program_desc_ctx.requests_memoryinfo_iova);
	vfree(capture->program_desc_ctx.unpins_list);
	capture_common_unpin_memory(&capture->program_desc_ctx.requests);
capture_meminfo_alloc_fail:
	dma_free_coherent(capture->rtcpu_dev,
		capture->capture_desc_ctx.queue_depth *
			sizeof(struct isp_capture_descriptor_memoryinfo),
		capture->capture_desc_ctx.requests_memoryinfo,
		capture->capture_desc_ctx.requests_memoryinfo_iova);
	vfree(capture->capture_desc_ctx.unpins_list);
	capture_common_unpin_memory(&capture->capture_desc_ctx.requests);
pin_fail:
	destroy_buffer_table(buffer_ctx);
	return err;
}

/**
 * @brief Releases ISP capture resources and cleans up the capture channel.
 *
 * This function performs the following operations:
 * - Validates the input channel by checking if it is non-NULL.
 * - Retrieves and validates the capture data from the channel.
 * - Logs the capture release event using @ref nv_camera_log().
 * - Checks if the channel is set up by verifying channel ID is not
 *   @ref CAPTURE_CHANNEL_ISP_INVALID_ID.
 * - Initializes the control message structure using @ref memset().
 * - Configures the control message with the release request by setting
 *   msg_id, channel_id, and reset_flags.
 * - Sends the control message by calling @ref isp_capture_ivc_send_control().
 *   - If sending fails, reboots the RTCPU device using @ref tegra_camrtc_reboot().
 * - Checks the response message's result for success.
 * - Unregisters the capture callback by calling @ref tegra_capture_ivc_unregister_capture_cb().
 * - Unregisters the control callback by calling @ref tegra_capture_ivc_unregister_control_cb().
 * - Iterates through the program descriptor queue:
 *   - Completes the program response using @ref complete().
 *   - Unpins the program request using @ref isp_capture_program_request_unpin().
 * - Unpins the program descriptor requests memory by calling @ref capture_common_unpin_memory().
 * - Iterates through the capture descriptor queue:
 *   - Completes the capture response using @ref complete().
 *   - Unpins the capture request using @ref isp_capture_request_unpin().
 * - Executes a speculative barrier using @ref spec_bar().
 * - Releases synchronization points by calling @ref isp_capture_release_syncpts().
 * - Unpins the capture descriptor requests memory using @ref capture_common_unpin_memory().
 * - Frees the unpins lists using @ref vfree() and sets the pointers to NULL.
 * - Frees memory info ring buffers by calling @ref dma_free_coherent().
 * - Releases the progress status notifier if set by calling
 *   @ref capture_common_release_progress_status_notifier().
 * - Destroys the buffer table using @ref destroy_buffer_table() and sets
 *   the buffer context to NULL.
 * - Invalidates the channel ID by setting channel ID to @ref CAPTURE_CHANNEL_ISP_INVALID_ID.
 * - Returns the accumulated error code.
 *
 *
 * @param[in]  chan         Pointer to the @ref tegra_isp_channel structure.
 *                          Valid value: non-NULL.
 * @param[in]  reset_flags  Reset flags for the capture release.
 *                          Valid range: [0 .. UINT32_MAX].
 *
 * @retval 0               On successful release of ISP capture resources.
 * @retval -ENODEV         If the channel is NULL, capture is uninitialized,
 *                         or the channel is not set up.
 * @retval -EIO            If sending the control message fails or the response
 *                         is invalid, involving @ref isp_capture_ivc_send_control()
 *                         or @ref tegra_camrtc_reboot().
 * @retval -ETIMEDOUT      If waiting for the capture response times out via
 *                         @ref wait_for_completion_timeout().
 * @retval -EINVAL         If the response received from @ref isp_capture_ivc_send_control()
 *                         is not successful.
 * @retval -ENOMEM         If memory allocation fails during cleanup operations.
 * @retval (int)           Errors returned from external functions such as
 *                         @ref tegra_capture_ivc_unregister_capture_cb(),
 *                         @ref tegra_capture_ivc_unregister_control_cb(),
 *                         @ref isp_capture_release_syncpts(),
 *                         @ref capture_common_unpin_memory(),
 *                         @ref dma_free_coherent(),
 *                         @ref vfree(), or @ref destroy_buffer_table().
 */
int isp_capture_release(
	struct tegra_isp_channel *chan,
	uint32_t reset_flags)
{
	struct isp_capture *capture;
	struct CAPTURE_CONTROL_MSG control_msg;
	struct CAPTURE_CONTROL_MSG *resp_msg;
	int err = 0;
	int ret = 0;
	int i;

	if (chan == NULL) {
		pr_err("%s: invalid context\n", __func__);
		return -ENODEV;
	}

	capture = chan->capture_data;
	if (capture == NULL) {
		dev_err(chan->isp_dev,
			"%s: isp capture uninitialized\n", __func__);
		return -ENODEV;
	}

	resp_msg = &capture->control_resp_msg;

	nv_camera_log(chan->ndev,
		__arch_counter_get_cntvct(),
		NVHOST_CAMERA_ISP_CAPTURE_RELEASE);

	if (capture->channel_id == CAPTURE_CHANNEL_ISP_INVALID_ID) {
		dev_err(chan->isp_dev,
			"%s: setup channel first\n", __func__);
		return -ENODEV;
	}

	memset(&control_msg, 0, sizeof(control_msg));

	control_msg.header.msg_id = CAPTURE_CHANNEL_ISP_RELEASE_REQ;
	control_msg.header.channel_id = capture->channel_id;
	control_msg.channel_release_req.reset_flags = reset_flags;

	err = isp_capture_ivc_send_control(chan, &control_msg,
			sizeof(control_msg), CAPTURE_CHANNEL_ISP_RELEASE_RESP);
	if (err < 0) {
		dev_err(chan->isp_dev,
				"%s: release channel IVC failed\n", __func__);
		pr_warn("RTCPU is in a bad state. Reboot to recover");

		tegra_camrtc_reboot(capture->rtcpu_dev);

		err = -EIO;
	} else if (resp_msg->channel_isp_release_resp.result != CAPTURE_OK) {
		dev_err(chan->isp_dev, "%s: control failed, errno %d", __func__,
			resp_msg->channel_isp_release_resp.result);
		err = -EIO;
	}

	ret = tegra_capture_ivc_unregister_capture_cb(capture->channel_id);
	if (ret < 0 && err == 0) {
		dev_err(chan->isp_dev,
			"failed to unregister capture callback\n");
		err = ret;
	}

	ret = tegra_capture_ivc_unregister_control_cb(capture->channel_id);
	if (ret < 0 && err == 0) {
		dev_err(chan->isp_dev,
			"failed to unregister control callback\n");
		err = ret;
	}

	for (i = 0; i < capture->program_desc_ctx.queue_depth; i++) {
		complete(&capture->capture_program_resp);
		isp_capture_program_request_unpin(chan, i);
	}

	capture_common_unpin_memory(&capture->program_desc_ctx.requests);

	for (i = 0; i < capture->capture_desc_ctx.queue_depth; i++) {
		complete(&capture->capture_resp);
		isp_capture_request_unpin(chan, i);
	}

	spec_bar();

	isp_capture_release_syncpts(chan);

	capture_common_unpin_memory(&capture->capture_desc_ctx.requests);

	vfree(capture->program_desc_ctx.unpins_list);
	capture->program_desc_ctx.unpins_list = NULL;
	vfree(capture->capture_desc_ctx.unpins_list);
	capture->capture_desc_ctx.unpins_list = NULL;

	dma_free_coherent(capture->rtcpu_dev,
		capture->program_desc_ctx.queue_depth *
			sizeof(struct memoryinfo_surface),
		capture->program_desc_ctx.requests_memoryinfo,
		capture->program_desc_ctx.requests_memoryinfo_iova);

	dma_free_coherent(capture->rtcpu_dev,
		capture->capture_desc_ctx.queue_depth *
			sizeof(struct isp_capture_descriptor_memoryinfo),
		capture->capture_desc_ctx.requests_memoryinfo,
		capture->capture_desc_ctx.requests_memoryinfo_iova);

	if (capture->is_progress_status_notifier_set)
		capture_common_release_progress_status_notifier(
			&capture->progress_status_notifier);

	destroy_buffer_table(capture->buffer_ctx);
	capture->buffer_ctx = NULL;

	capture->channel_id = CAPTURE_CHANNEL_ISP_INVALID_ID;

	return err;
}

/**
 * @brief Resets the ISP capture channel with specified reset flags.
 *
 * This function performs the following operations:
 * - Validates the input channel by checking if it is non-NULL.
 * - Retrieves and validates the capture data from the channel.
 * - Logs the capture reset event using @ref nv_camera_log().
 * - Checks if the channel is set up by verifying the channel ID.
 * - Acquires the reset lock using @ref mutex_lock().
 * - Sets the reset flags for both program and capture operations.
 * - If @ref CAPTURE_ISP_RESET_BARRIER_IND is defined:
 *   - Initializes the capture message structure using @ref memset().
 *   - Configures the capture message with the reset request by setting
 *     msg_id and channel_id.
 *   - Submits the capture reset message by calling @ref tegra_capture_ivc_capture_submit().
 * - Initializes the control message structure using @ref memset().
 * - Configures the control message with the reset request by setting
 *   msg_id, channel_id, and reset_flags.
 * - Sends the control message by calling @ref isp_capture_ivc_send_control().
 * - If @ref CAPTURE_ISP_RESET_BARRIER_IND is defined:
 *   - Checks the response result for timeout.
 * - Checks the response result for success.
 * - Fast-forwards synchronization points by calling @ref isp_capture_fastforward_syncpts().
 *
 * Error Handling:
 * - Iterates through the program descriptor queue:
 *   - Unpins the program request using @ref isp_capture_program_request_unpin().
 *   - Completes the program response using @ref complete().
 * - Executes a speculative barrier using @ref spec_bar().
 * - Iterates through the capture descriptor queue:
 *   - Unpins the capture request using @ref isp_capture_request_unpin().
 *   - Completes the capture response using @ref complete().
 * - Executes another speculative barrier using @ref spec_bar().
 * - Releases the reset lock using @ref mutex_unlock().
 *
 * @param[in]  chan          Pointer to the @ref tegra_isp_channel structure.
 *                           Valid value: non-NULL.
 * @param[in]  reset_flags   Reset flags for the capture reset.
 *                           Valid range: [0 .. UINT32_MAX].
 *
 * @retval 0               On successful reset of the ISP capture channel.
 * @retval -ENODEV         If the channel is NULL, capture is uninitialized,
 *                         or the channel is not set up.
 * @retval -EAGAIN         If the ISP reset operation times out, involving
 *                         @ref tegra_capture_ivc_capture_submit().
 * @retval -EINVAL         If the response received from @ref isp_capture_ivc_send_control()
 *                         is not successful.
 * @retval -EIO            If sending the control message fails or the RTCPU device
 *                         needs to reboot, involving @ref isp_capture_ivc_send_control()
 *                         or @ref tegra_camrtc_reboot().
 * @retval (int)           If @ref tegra_capture_ivc_capture_submit() fails.
 */
int isp_capture_reset(
	struct tegra_isp_channel *chan,
	uint32_t reset_flags)
{
	struct isp_capture *capture;
#ifdef CAPTURE_ISP_RESET_BARRIER_IND
	struct CAPTURE_MSG capture_msg;
#endif
	struct CAPTURE_CONTROL_MSG control_msg;
	struct CAPTURE_CONTROL_MSG *resp_msg;
	int i;
	int err = 0;

	if (chan == NULL) {
		pr_err("%s: invalid context\n", __func__);
		return -ENODEV;
	}

	capture = chan->capture_data;
	if (capture == NULL) {
		dev_err(chan->isp_dev,
			"%s: isp capture uninitialized\n", __func__);
		return -ENODEV;
	}

	resp_msg = &capture->control_resp_msg;

	nv_camera_log(chan->ndev,
		__arch_counter_get_cntvct(),
		NVHOST_CAMERA_ISP_CAPTURE_RESET);


	if (capture->channel_id == CAPTURE_CHANNEL_ISP_INVALID_ID) {
		dev_err(chan->isp_dev,
			"%s: setup channel first\n", __func__);
		return -ENODEV;
	}

	mutex_lock(&capture->reset_lock);
	capture->reset_capture_program_flag = true;
	capture->reset_capture_flag = true;

#ifdef CAPTURE_ISP_RESET_BARRIER_IND
	memset(&capture_msg, 0, sizeof(capture_msg));
	capture_msg.header.msg_id = CAPTURE_ISP_RESET_BARRIER_IND;
	capture_msg.header.channel_id = capture->channel_id;

	err = tegra_capture_ivc_capture_submit(&capture_msg,
			sizeof(capture_msg));
	if (err < 0) {
		dev_err(chan->isp_dev, "IVC capture submit failed\n");
		goto error;
	}
#endif

	memset(&control_msg, 0, sizeof(control_msg));
	control_msg.header.msg_id = CAPTURE_CHANNEL_ISP_RESET_REQ;
	control_msg.header.channel_id = capture->channel_id;
	control_msg.channel_isp_reset_req.reset_flags = reset_flags;

	err = isp_capture_ivc_send_control(chan, &control_msg,
			sizeof(control_msg), CAPTURE_CHANNEL_ISP_RESET_RESP);
	if (err < 0)
		goto error;

#ifdef CAPTURE_ISP_RESET_BARRIER_IND
	if (resp_msg->channel_isp_reset_resp.result == CAPTURE_ERROR_TIMEOUT) {
		dev_dbg(chan->isp_dev, "%s: isp reset timedout\n", __func__);
		err = -EAGAIN;
		goto error;
	}
#endif

	if (resp_msg->channel_isp_reset_resp.result != CAPTURE_OK) {
		dev_err(chan->isp_dev, "%s: control failed, errno %d", __func__,
			resp_msg->channel_isp_reset_resp.result);
		err = -EINVAL;
		goto error;
	}

	isp_capture_fastforward_syncpts(chan);

	err = 0;

error:
	for (i = 0; i < capture->program_desc_ctx.queue_depth; i++) {
		isp_capture_program_request_unpin(chan, i);
		complete(&capture->capture_program_resp);
	}
	spec_bar();

	for (i = 0; i < capture->capture_desc_ctx.queue_depth; i++) {
		isp_capture_request_unpin(chan, i);
		complete(&capture->capture_resp);
	}

	spec_bar();

	mutex_unlock(&capture->reset_lock);
	return err;
}

/**
 * @brief Retrieves information about the ISP capture channel.
 *
 * This function performs the following operations:
 * - Validates the input channel by checking if it is non-NULL.
 * - Retrieves and validates the capture data from the channel.
 * - Logs the capture get_info event using @ref nv_camera_log().
 * - Checks if the channel is set up by verifying the channel ID.
 * - Validates the output info parameter by checking if it is non-NULL.
 * - Assigns the channel ID to the info structure.
 * - Assigns the progress and stats progress syncpoint IDs to the info structure.
 * - Reads the current value of the progress syncpoint by calling
 *   @ref isp_capture_read_syncpt().
 * - Reads the current value of the stats progress syncpoint by calling
 *   @ref isp_capture_read_syncpt().
 *
 * @param[in]  chan    Pointer to the @ref tegra_isp_channel structure.
 *                     Valid value: non-NULL.
 * @param[out] info    Pointer to the @ref isp_capture_info structure to be filled.
 *                     Valid value: non-NULL.
 *
 * @retval 0            On successful retrieval of capture information.
 * @retval -ENODEV      If the channel is NULL, capture data is uninitialized,
 *                      or the channel is not set up.
 * @retval -EINVAL      If the output info parameter is NULL.
 * @retval (int)        Errors returned from invocation of @ref isp_capture_read_syncpt().
 */
int isp_capture_get_info(
	struct tegra_isp_channel *chan,
	struct isp_capture_info *info)
{
	struct isp_capture *capture;
	int err;
	if (chan == NULL) {
		pr_err("%s: invalid context\n", __func__);
		return -ENODEV;
	}

	capture = chan->capture_data;

	if (capture == NULL) {
		dev_err(chan->isp_dev,
			"%s: isp capture uninitialized\n", __func__);
		return -ENODEV;
	}

	nv_camera_log(chan->ndev,
		__arch_counter_get_cntvct(),
		NVHOST_CAMERA_ISP_CAPTURE_GET_INFO);


	if (capture->channel_id == CAPTURE_CHANNEL_ISP_INVALID_ID) {
		dev_err(chan->isp_dev,
			"%s: setup channel first\n", __func__);
		return -ENODEV;
	}

	if (info == NULL) {
		dev_err(chan->isp_dev,
			"%s: Invalid user parameter\n", __func__);
		return -EINVAL;
	}

	info->channel_id = capture->channel_id;

	info->syncpts.progress_syncpt = capture->progress_sp.id;
	info->syncpts.stats_progress_syncpt =
			capture->stats_progress_sp.id;

	err = isp_capture_read_syncpt(chan, &capture->progress_sp,
			&info->syncpts.progress_syncpt_val);
	if (err < 0)
		return err;

	err = isp_capture_read_syncpt(chan, &capture->stats_progress_sp,
			&info->syncpts.stats_progress_syncpt_val);
	if (err < 0)
		return err;

	return 0;
}

/**
 * @brief Pins and maps ISP capture request buffers and save IOVA boundaries.
 *
 * This function performs the following operations:
 * - Retrieves the capture descriptor context from the channel structure.
 * - Calculates descriptor and request offsets, checking for overflow using
 *   @ref check_mul_overflow() and @ref check_add_overflow().
 * - Retrieves the capture descriptor based on the calculated descriptor offset.
 * - Calculates the ISP pushbuffer2 memory offset, ensuring no overflow occurs.
 * - Pins the pushbuffer2 memory region by calling
 *   @ref capture_common_pin_and_get_iova().
 * - Iterates through input surfaces and pins each using
 *   @ref capture_common_pin_and_get_iova().
 * - Iterates through output surfaces and pins each using
 *   @ref capture_common_pin_and_get_iova().
 * - Pins statistics surfaces by iterating through predefined arrays and
 *   calling @ref capture_common_pin_and_get_iova() for each surface.
 * - Pins the engine status surface using @ref capture_common_pin_and_get_iova().
 * - In case of any error during the above steps, unpin cleanup is handled by
 *   @ref isp_capture_request_unpin().
 *
 * @param[in]      chan            Pointer to the @ref tegra_isp_channel structure.
 *                                 Valid value: non-NULL.
 * @param[in]      req             Pointer to the @ref isp_capture_req structure.
 *                                 Valid value: non-NULL.
 * @param[in, out] request_unpins  Pointer to the @ref capture_common_unpins structure
 *                                 used for managing unpin operations.
 *                                 Valid value: non-NULL.
 *
 * @retval 0           On successful pinning of all request buffers.
 * @retval -EOVERFLOW  If an overflow is detected during offset calculations via
 *                     @ref check_mul_overflow() or @ref check_add_overflow().
 * @retval (int)       Errors propagated from @ref capture_common_pin_and_get_iova().
 */
static int pin_isp_capture_request_buffers_locked(
		struct tegra_isp_channel *chan,
		struct isp_capture_req *req,
		struct capture_common_unpins *request_unpins)
{
	struct isp_desc_rec *capture_desc_ctx =
			&chan->capture_data->capture_desc_ctx;
	struct isp_capture_descriptor *desc;

	struct isp_capture_descriptor_memoryinfo *desc_mem =
		&((struct isp_capture_descriptor_memoryinfo *)
			capture_desc_ctx->requests_memoryinfo)
				[req->buffer_index];

	struct capture_buffer_table *buffer_ctx =
			chan->capture_data->buffer_ctx;
	int i, j;
	int err = 0;
	uint32_t desc_offset = 0;

	/* Pushbuffer 2 is located after isp desc, in same ringbuffer */
	uint32_t request_offset = 0;
	uint32_t isp_pb2_mem_offset = 0;

	if (check_mul_overflow(req->buffer_index, capture_desc_ctx->request_size, &desc_offset)) {
		err = -EOVERFLOW;
		goto fail;
	}

	desc = (struct isp_capture_descriptor *)(capture_desc_ctx->requests.va + desc_offset);

	if (check_mul_overflow(req->buffer_index, capture_desc_ctx->request_size, &request_offset)) {
		err = -EOVERFLOW;
		goto fail;
	}

	if (check_add_overflow((uint32_t)desc->isp_pb2_mem, request_offset, &isp_pb2_mem_offset)) {
		err = -EOVERFLOW;
		goto fail;
	}

	err = capture_common_pin_and_get_iova(buffer_ctx,
			(uint32_t)(desc->isp_pb2_mem >> 32U),
			isp_pb2_mem_offset,
			&desc_mem->isp_pb2_mem.base_address,
			&desc_mem->isp_pb2_mem.size,
			request_unpins);

	if (err) {
		dev_err(chan->isp_dev, "%s: get pushbuffer2 iova failed\n",
			__func__);
		goto fail;
	}

	for (i = 0; i < ISP_MAX_INPUT_SURFACES; i++) {
		err = capture_common_pin_and_get_iova(buffer_ctx,
			desc->input_mr_surfaces[i].offset_hi,
			desc->input_mr_surfaces[i].offset,
			&desc_mem->input_mr_surfaces[i].base_address,
			&desc_mem->input_mr_surfaces[i].size,
			request_unpins);

		if (err) {
			dev_err(chan->isp_dev,
				"%s: get input_mr_surfaces iova failed\n",
				__func__);
			goto fail;
		}
	}

	for (i = 0; i < ISP_MAX_OUTPUTS; i++) {
		for (j = 0; j < ISP_MAX_OUTPUT_SURFACES; j++) {
			err = capture_common_pin_and_get_iova(buffer_ctx,
				desc->outputs_mw[i].surfaces[j].offset_hi,
				desc->outputs_mw[i].surfaces[j].offset,
				&desc_mem->outputs_mw[i].surfaces[j].base_address,
				&desc_mem->outputs_mw[i].surfaces[j].size,
				request_unpins);

			if (err) {
				dev_err(chan->isp_dev,
					"%s: get outputs_mw iova failed\n",
					__func__);
				goto fail;
			}
		}
	}

	/* Pin stats surfaces */
	{
		struct stats_surface *stats_surfaces[] = {
			&desc->fb_surface,	&desc->fm_surface,
			&desc->afm_surface,	&desc->lac0_surface,
			&desc->lac1_surface,	&desc->h0_surface,
			&desc->h1_surface,	&desc->hist_raw24_surface,
			&desc->pru_bad_surface,	&desc->ltm_surface,
			&desc->h2_surface,
		};

		struct memoryinfo_surface *meminfo_surfaces[] = {
			&desc_mem->fb_surface,	&desc_mem->fm_surface,
			&desc_mem->afm_surface,	&desc_mem->lac0_surface,
			&desc_mem->lac1_surface,	&desc_mem->h0_surface,
			&desc_mem->h1_surface,	&desc_mem->hist_raw24_surface,
			&desc_mem->pru_bad_surface,	&desc_mem->ltm_surface,
			&desc_mem->h2_surface,
		};

		BUILD_BUG_ON(ARRAY_SIZE(stats_surfaces) !=
				ARRAY_SIZE(meminfo_surfaces));

		for (i = 0; i < ARRAY_SIZE(stats_surfaces); i++) {
			err = capture_common_pin_and_get_iova(buffer_ctx,
					stats_surfaces[i]->offset_hi,
					stats_surfaces[i]->offset,
					&meminfo_surfaces[i]->base_address,
					&meminfo_surfaces[i]->size,
					request_unpins);
			if (err)
				goto fail;
		}
	}

	/* pin engine status surface */
	err = capture_common_pin_and_get_iova(buffer_ctx,
			desc->engine_status.offset_hi,
			desc->engine_status.offset,
			&desc_mem->engine_status.base_address,
			&desc_mem->engine_status.size,
			request_unpins);
fail:
	/* Unpin cleanup is done in isp_capture_request_unpin() */
	return err;
}

/**
 * @brief Retrieves the number of progress steps for a capture request.
 *
 * This function performs the following operations:
 * - Calculates the descriptor offset by multiplying the buffer index with the
 *   request size using @ref check_mul_overflow().
 * - Retrieves the capture descriptor from the calculated offset.
 * - Extracts the slice height and height from the descriptor's surface configurations.
 * - Adjusts the slice height by subtracting 1 using @ref check_sub_overflow().
 * - Adjusts the height by adding the adjusted slice height using @ref check_add_overflow().
 * - Calculates and returns the number of progress steps by dividing the adjusted
 *   height by the slice height.
 *
 * In case of any overflow during calculations, the function returns 0.
 *
 * @param[in]  chan  Pointer to the @ref tegra_isp_channel structure.
 *                   Valid value: non-NULL.
 * @param[in]  req   Pointer to the @ref isp_capture_req structure.
 *                   Valid value: non-NULL.
 *
 * @retval 0          If any overflow check fails via @ref check_mul_overflow(),
 *                    @ref check_sub_overflow(), or @ref check_add_overflow().
 * @retval (int)      The number of progress steps, calculated by dividing the
 *                    adjusted height by the slice height.
 */
static uint32_t isp_capture_get_num_progress(
	struct tegra_isp_channel *chan,
	struct isp_capture_req *req)
{
	struct isp_desc_rec *capture_desc_ctx =
		&chan->capture_data->capture_desc_ctx;
	struct isp_capture_descriptor *desc = NULL;
	uint16_t sliceHeight = 0U;
	uint16_t height = 0U;
	uint32_t desc_offset = 0U;
	uint16_t slice_hight_sub = 1U;
	uint16_t adjust_slice_height = 0U;
	uint16_t adjust_height = 0U;

	if (check_mul_overflow(req->buffer_index, capture_desc_ctx->request_size, &desc_offset))
		return 0;

	desc = (struct isp_capture_descriptor *)(capture_desc_ctx->requests.va + desc_offset);
	sliceHeight = desc->surface_configs.slice_height;
	height = desc->surface_configs.mr_height;

	if (check_sub_overflow(sliceHeight, slice_hight_sub, &adjust_slice_height))
		return 0;

	if (check_add_overflow(height, adjust_slice_height, &adjust_height))
		return 0;

	return (adjust_height / sliceHeight);
}

/**
 * @brief Submits a capture request to the ISP channel.
 *
 * This function performs the following operations:
 * - Validates the input channel and capture request are not null.
 * - Ensures the capture channel is initialized and valid.
 * - Ensures buffer index is not out of bounds of configured queue depth.
 * - If reset capture flag is set, waits for any pending completions before proceeding
 *   using @ref try_wait_for_completion().
 * - Calculates the request offset based on the buffer index.
 * - Calls @ref isp_capture_setup_inputfences() and @ref isp_capture_setup_prefences()
 *   to configure input and pre-fences for the capture request.
 * - Checks if descriptor unpins list for the buffer index is empty, or if it is still
 *   in use by RTCPU.
 * - Checks and pins the request buffers using
 *   @ref pin_isp_capture_request_buffers_locked().
 * - Submits the capture message via @ref tegra_capture_ivc_capture_submit().
 * - Updates synchronization points upon successful submission incrementing
 *   @ref isp_capture_get_num_progress().
 * - In case of an error, unpins the request buffers using @ref isp_capture_request_unpin().
 *
 * @param[in]  chan  Pointer to the @ref tegra_isp_channel structure representing
 *                   the ISP channel.
 *                   Valid value: non-NULL.
 * @param[in]  req   Pointer to the @ref isp_capture_req structure containing
 *                   the capture request details.
 *                   Valid value: non-NULL and must reference a valid buffer index
 *                   within the queue depth of the capture context.
 *
 * @retval 0               On successful submission of the capture request.
 * @retval -ENODEV         If the channel context is invalid, capture is
 *                         uninitialized, or the capture channel is not set up.
 * @retval -EINVAL         If the capture request is invalid, the capture
 *                         descriptor context is incomplete, or the buffer
 *                         index is out of bounds.
 * @retval -EBUSY          If the descriptor for the buffer index is still
 *                         in use by RTCPU.
 * @retval (int)           If any external function such as
 *                         @ref isp_capture_setup_inputfences(),
 *                         @ref isp_capture_setup_prefences(),
 *                         @ref pin_isp_capture_request_buffers_locked(),
 *                         or @ref tegra_capture_ivc_capture_submit()
 *                         fails, the corresponding error code is returned.
 */
int isp_capture_request(
	struct tegra_isp_channel *chan,
	struct isp_capture_req *req)
{
	struct isp_capture *capture;
	struct CAPTURE_MSG capture_msg;
	uint32_t request_offset;
	int err = 0;
	if (chan == NULL) {
		pr_err("%s: invalid context\n", __func__);
		return -ENODEV;
	}

	capture = chan->capture_data;

	if (capture == NULL) {
		dev_err(chan->isp_dev,
			"%s: isp capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (capture->channel_id == CAPTURE_CHANNEL_ISP_INVALID_ID) {
		dev_err(chan->isp_dev,
			"%s: setup channel first\n", __func__);
		return -ENODEV;
	}

	if (req == NULL) {
		dev_err(chan->isp_dev,
			"%s: Invalid req\n", __func__);
		return -EINVAL;
	}

	if (capture->capture_desc_ctx.unpins_list == NULL) {
		dev_err(chan->isp_dev, "Channel setup incomplete\n");
		return -EINVAL;
	}

	if (req->buffer_index >= capture->capture_desc_ctx.queue_depth) {
		dev_err(chan->isp_dev, "buffer index is out of bound\n");
		return -EINVAL;
	}

	spec_bar();

	mutex_lock(&capture->reset_lock);
	if (capture->reset_capture_flag) {
		/* consume any pending completions when coming out of reset */
		while (try_wait_for_completion(&capture->capture_resp))
			; /* do nothing */
	}
	capture->reset_capture_flag = false;
	mutex_unlock(&capture->reset_lock);

	memset(&capture_msg, 0, sizeof(capture_msg));
	capture_msg.header.msg_id = CAPTURE_ISP_REQUEST_REQ;
	capture_msg.header.channel_id = capture->channel_id;
	capture_msg.capture_isp_request_req.buffer_index = req->buffer_index;

	request_offset = req->buffer_index *
			capture->capture_desc_ctx.request_size;

	err = isp_capture_setup_inputfences(chan, req, request_offset);
	if (err < 0) {
		dev_err(chan->isp_dev, "failed to setup inputfences\n");
		goto fail;
	}

	err = isp_capture_setup_prefences(chan, req, request_offset);
	if (err < 0) {
		dev_err(chan->isp_dev, "failed to setup prefences\n");
		goto fail;
	}

	mutex_lock(&capture->capture_desc_ctx.unpins_list_lock);

	if (capture->capture_desc_ctx.unpins_list[req->buffer_index].num_unpins != 0U) {
		dev_err(chan->isp_dev,
			"%s: descriptor is still in use by rtcpu\n",
			__func__);
		mutex_unlock(&capture->capture_desc_ctx.unpins_list_lock);
		err = -EBUSY;
		goto fail;
	}

	err = pin_isp_capture_request_buffers_locked(chan, req,
		&capture->capture_desc_ctx.unpins_list[req->buffer_index]);

	mutex_unlock(&capture->capture_desc_ctx.unpins_list_lock);

	if (err < 0) {
		dev_err(chan->isp_dev, "%s failed to pin request buffers\n",
			__func__);
		goto fail;
	}

	nv_camera_log_isp_submit(
			chan->ndev,
			capture->progress_sp.id,
			capture->progress_sp.threshold,
			capture_msg.header.channel_id,
			__arch_counter_get_cntvct());

	dev_dbg(chan->isp_dev, "%s: sending chan_id %u msg_id %u buf:%u\n",
			__func__, capture_msg.header.channel_id,
			capture_msg.header.msg_id, req->buffer_index);


	err = tegra_capture_ivc_capture_submit(&capture_msg,
			sizeof(capture_msg));
	if (err < 0) {
		dev_err(chan->isp_dev, "IVC capture submit failed\n");
		goto fail;
	}

	// Progress syncpoints + 1 for frame completion
	capture->progress_sp.threshold += isp_capture_get_num_progress(chan, req) + 1;
	capture->stats_progress_sp.threshold += chan->capture_data->stats_progress_delta;

	return 0;

fail:
	isp_capture_request_unpin(chan, req->buffer_index);
	return err;
}

/**
 * @brief Retrieves the capture status for the specified ISP channel.
 *
 * This function performs the following operations:
 * - Validates the input channel context.
 * - Retrieves the capture data from the channel structure.
 * - Checks if the capture data is not null and capture channel is valid.
 * - If given timeout is negative, waits for capture response completion
 *   using @ref wait_for_completion_killable().
 * - Otherwise, waits using @ref wait_for_completion_killable_timeout().
 * - If the response is not received within the timeout,
 *   log the RCE snapshot by calling @ref rtcpu_trace_panic_callback().
 * - Acquires the capture channel reset lock using @ref mutex_lock().
 * - Checks if a reset capture flag is set.
 * - Releases the capture channel reset lock using @ref mutex_unlock().
 * - Returns any errors encountered during the wait operations via
 *   @ref wait_for_completion_killable() or @ref wait_for_completion_killable_timeout().
 * - On successful completion, returns 0.
 *
 * @param[in]  chan        Pointer to the @ref tegra_isp_channel structure.
 *                         Valid value: non-NULL.
 * @param[in]  timeout_ms  Timeout in milliseconds to wait for capture status.
 *                         Valid range: Negative value indicates wait forever,
 *                         non-negative values specify waiting duration.
 *
 * @retval 0             On successful completion.
 * @retval -ENODEV       If the channel context is invalid, capture data is
 *                       uninitialized, or the channel is not properly set up.
 * @retval -ETIMEDOUT    If waiting for capture status timed out via
 *                       @ref wait_for_completion_killable_timeout().
 * @retval -EIO          If a reset capture flag is detected via
 *                       @ref mutex_lock() and @ref mutex_unlock().
 * @retval (int)         Errors propagated from @ref wait_for_completion_killable()
 *                       or @ref wait_for_completion_killable_timeout().
 */
int isp_capture_status(
	struct tegra_isp_channel *chan,
	int32_t timeout_ms)
{
	struct isp_capture *capture;
	int err = 0;
	if (chan == NULL) {
		pr_err("%s: invalid context\n", __func__);
		return -ENODEV;
	}

	capture = chan->capture_data;

	nv_camera_log(chan->ndev,
		__arch_counter_get_cntvct(),
		NVHOST_CAMERA_ISP_CAPTURE_STATUS);

	if (capture == NULL) {
		dev_err(chan->isp_dev,
			 "%s: isp capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (capture->channel_id == CAPTURE_CHANNEL_ISP_INVALID_ID) {
		dev_err(chan->isp_dev,
			"%s: setup channel first\n", __func__);
		return -ENODEV;
	}

	/* negative timeout means wait forever */
	if (timeout_ms < 0) {
		err = wait_for_completion_killable(&capture->capture_resp);
	} else {
		err = wait_for_completion_killable_timeout(
				&capture->capture_resp,
				msecs_to_jiffies(timeout_ms));
		if (err == 0) {
			dev_dbg(chan->isp_dev,
				"isp capture status timed out\n");
			rtcpu_trace_panic_callback(capture->rtcpu_dev);
			return -ETIMEDOUT;
		}
	}

	if (err < 0) {
		dev_err(chan->isp_dev,
			"wait for capture status failed\n");
		return err;
	}

	mutex_lock(&capture->reset_lock);
	if (capture->reset_capture_flag) {
		mutex_unlock(&capture->reset_lock);
		return -EIO;
	}
	mutex_unlock(&capture->reset_lock);

	return 0;
}

/**
 * @brief Submits a program capture request to the ISP channel.
 *
 * This function performs the following operations:
 * - Validates the input channel context is not null.
 * - Retrieves the capture data from the channel structure.
 * - Validates the program capture request is not null.
 * - Calls @ref isp_capture_program_prepare() to prepare the program request data.
 * - Initializes a capture message structure.
 * - Sets header message ID to @ref CAPTURE_ISP_PROGRAM_REQUEST_REQ.
 * - Sets necessary fields in the capture message based on the request based on
 *   request and input @a chan.
 * - Submits the capture message via @ref tegra_capture_ivc_capture_submit().
 * - If submission fails, calls @ref isp_capture_program_request_unpin() to
 *   unpin the request buffer.
 *
 * @param[in]  chan  Pointer to the @ref tegra_isp_channel structure.
 *                   Valid value: non-NULL.
 * @param[in]  req   Pointer to the @ref isp_program_req structure containing
 *                   the program capture request details.
 *                   Valid value: non-NULL.
 *
 * @retval 0               On successful submission of the program capture request.
 * @retval -ENODEV         If the channel context is invalid or capture data is
 *                         uninitialized.
 * @retval -EINVAL         If the program request is invalid.
 * @retval (int)           If @ref isp_capture_program_prepare() or
 *                         @ref tegra_capture_ivc_capture_submit() fails, the
 *                         corresponding error code is returned.
 */
int isp_capture_program_request(
	struct tegra_isp_channel *chan,
	struct isp_program_req *req)
{
	struct isp_capture *capture;
	struct CAPTURE_MSG capture_msg;
	int err = 0;
	if (chan == NULL) {
		pr_err("%s: invalid context\n", __func__);
		return -ENODEV;
	}

	capture = chan->capture_data;

	if (capture == NULL) {
		dev_err(chan->isp_dev,
			 "%s: isp capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (req == NULL) {
		dev_err(chan->isp_dev,
			"%s: Invalid req\n", __func__);
		return -EINVAL;
	}

	nv_camera_log(chan->ndev,
		__arch_counter_get_cntvct(),
		NVHOST_CAMERA_ISP_CAPTURE_PROGRAM_REQUEST);

	err = isp_capture_program_prepare(chan, req);
	if (err < 0) {
		/* no cleanup needed */
		return err;
	}

	memset(&capture_msg, 0, sizeof(capture_msg));
	capture_msg.header.msg_id = CAPTURE_ISP_PROGRAM_REQUEST_REQ;
	capture_msg.header.channel_id = capture->channel_id;
	capture_msg.capture_isp_program_request_req.buffer_index =
				req->buffer_index;

	dev_dbg(chan->isp_dev, "%s: sending chan_id %u msg_id %u buf:%u\n",
			__func__, capture_msg.header.channel_id,
			capture_msg.header.msg_id, req->buffer_index);

	err = tegra_capture_ivc_capture_submit(&capture_msg,
			sizeof(capture_msg));
	if (err < 0) {
		dev_err(chan->isp_dev, "IVC program submit failed\n");
		isp_capture_program_request_unpin(chan, req->buffer_index);
		return err;
	}

	return 0;
}

/**
 * @brief Retrieves the program capture status for the specified ISP channel.
 *
 * This function performs the following operations:
 * - Validates the input channel context.
 * - Retrieves the capture data from the channel.
 * - Validates that capture data and channel ID.
 * - Waits for the capture program response using @ref wait_for_completion_killable().
 * - Acquires capture channel reset lock using @ref mutex_lock().
 * - Checks if a reset capture program flag is set.
 * - Releases capture channel reset lock using @ref mutex_unlock().
 * - Returns the status of the operations.
 *
 * @param[in]  chan  Pointer to the @ref tegra_isp_channel structure.
 *                   Valid value: non-NULL.
 *
 * @retval 0          On successful completion.
 * @retval -ENODEV    If the channel context is invalid, capture data is
 *                    uninitialized, or the channel is not properly set up.
 * @retval -EIO       If a reset capture program flag is detected.
 * @retval (int)      Errors returned from @ref wait_for_completion_killable().
 */
int isp_capture_program_status(
	struct tegra_isp_channel *chan)
{
	struct isp_capture *capture;
	int err = 0;
	if (chan == NULL) {
		pr_err("%s: invalid context\n", __func__);
		return -ENODEV;
	}
	capture = chan->capture_data;

	nv_camera_log(chan->ndev,
		__arch_counter_get_cntvct(),
		NVHOST_CAMERA_ISP_CAPTURE_PROGRAM_STATUS);

	if (capture == NULL) {
		dev_err(chan->isp_dev,
			 "%s: isp capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (capture->channel_id == CAPTURE_CHANNEL_ISP_INVALID_ID) {
		dev_err(chan->isp_dev,
			"%s: setup channel first\n", __func__);
		return -ENODEV;
	}

	dev_dbg(chan->isp_dev, "%s: waiting for isp program status\n",
		__func__);

	/* no timeout as an isp_program may get used for mutliple frames */
	err = wait_for_completion_killable(&capture->capture_program_resp);
	if (err < 0) {
		dev_err(chan->isp_dev,
			"isp program status wait failed\n");
		return err;
	}

	mutex_lock(&capture->reset_lock);
	if (capture->reset_capture_program_flag) {
		mutex_unlock(&capture->reset_lock);
		return -EIO;
	}
	mutex_unlock(&capture->reset_lock);

	return 0;
}

/**
 * @brief Submits extended (joint program and capture) request to the ISP channel.
 *
 * This function performs the following operations:
 * - Validates the input channel and extended capture request.
 * - If the buffer index in the request is set to its maximum value,
 *   forwards the request to @ref isp_capture_request().
 * - Otherwise, calls @ref isp_capture_program_prepare() to prepare
 *   the program request data.
 * - Submits the capture request by calling @ref isp_capture_request().
 * - If submitting the capture request fails, calls @ref
 *   isp_capture_program_request_unpin() to unpin the prepared program.
 *
 * @param[in]  chan  Pointer to the @ref tegra_isp_channel structure.
 *                   Valid value: non-NULL.
 * @param[in]  req   Pointer to the @ref isp_capture_req_ex structure containing
 *                   the extended capture request details.
 *                   Valid value: non-NULL.
 *
 * @retval 0            On successful submission of the extended capture request.
 * @retval -ENODEV      If the channel context is invalid or capture is
 *                      uninitialized.
 * @retval -EINVAL      If the extended capture request is invalid.
 * @retval (int)        If @ref isp_capture_program_prepare() or
 *                      @ref isp_capture_request() fails, the corresponding error
 *                      code is returned.
 */
int isp_capture_request_ex(
	struct tegra_isp_channel *chan,
	struct isp_capture_req_ex *req)
{
	int err;
	if (chan == NULL) {
		pr_err("%s: invalid context\n", __func__);
		return -ENODEV;
	}


	nv_camera_log(chan->ndev,
		__arch_counter_get_cntvct(),
		NVHOST_CAMERA_ISP_CAPTURE_REQUEST_EX);

	if (req == NULL) {
		dev_err(chan->isp_dev,
			"%s: Invalid req\n", __func__);
		return -EINVAL;
	}

	if (req->program_req.buffer_index == U32_MAX) {
		/* forward to process request */
		return isp_capture_request(chan, &req->capture_req);
	}

	err = isp_capture_program_prepare(chan, &req->program_req);

	if (err < 0) {
		/* no cleanup required */
		return err;
	}

	err = isp_capture_request(chan, &req->capture_req);

	if (err < 0) {
		/* unpin prepared program */
		isp_capture_program_request_unpin(
			chan, req->program_req.buffer_index);
	}

	return err;
}

/**
 * @brief Sets the progress status notifier for the ISP capture channel.
 *
 * This function performs the following operations:
 * - Validates the input channel and progress status request.
 *   - Ensures input channel, capture data, and request are non-null.
 *   - Ensures program and process request buffer depths are valid.
 * - Logs the progress status setup using @ref nv_camera_log().
 * - Calls @ref capture_common_setup_progress_status_notifier() to configure
 *   the progress status notifier.
 * - Sets the progress status buffer depths for capture and program contexts.
 * - Marks the progress status notifier as set.
 *
 * @param[in]  chan  Pointer to the @ref tegra_isp_channel structure.
 *                   Valid value: non-NULL.
 * @param[in]  req   Pointer to the @ref isp_capture_progress_status_req structure
 *                   containing the progress status request details.
 *                   Valid value: non-NULL, with valid memory and buffer depths.
 *
 * @retval 0               On successful setup of the progress status notifier.
 * @retval -ENODEV         If the channel context is invalid or capture is
 *                         uninitialized.
 * @retval -EINVAL         If the progress status request is invalid, including
 *                         invalid memory or buffer depth parameters.
 * @retval -EFAULT         If @ref capture_common_setup_progress_status_notifier()
 *                         fails, indicating a fault in setting up the notifier.
 */
int isp_capture_set_progress_status_notifier(
	struct tegra_isp_channel *chan,
	struct isp_capture_progress_status_req *req)
{
	int err = 0;
	struct isp_capture *capture;

	if (chan == NULL) {
		pr_err("%s: invalid context\n", __func__);
		return -ENODEV;
	}

	capture = chan->capture_data;

	nv_camera_log(chan->ndev,
		__arch_counter_get_cntvct(),
		NVHOST_CAMERA_ISP_CAPTURE_SET_PROGRESS_STATUS);

	if (capture == NULL) {
		dev_err(chan->isp_dev,
				"%s: isp capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (req == NULL) {
		dev_err(chan->isp_dev,
			"%s: Invalid req\n", __func__);
		return -EINVAL;
	}

	if (req->mem == 0 ||
		req->process_buffer_depth == 0) {
		dev_err(chan->isp_dev,
				"%s: process request buffer is invalid\n",
				__func__);
		return -EINVAL;
	}

	if (req->mem == 0 ||
		req->program_buffer_depth == 0) {
		dev_err(chan->isp_dev,
				"%s: program request buffer is invalid\n",
				__func__);
		return -EINVAL;
	}


	if (req->process_buffer_depth < capture->capture_desc_ctx.queue_depth) {
		dev_err(chan->isp_dev,
			"%s: Process progress status buffer smaller than queue depth\n",
			__func__);
		return -EINVAL;
	}

	if (req->program_buffer_depth < capture->program_desc_ctx.queue_depth) {
		dev_err(chan->isp_dev,
			"%s: Program progress status buffer smaller than queue depth\n",
			__func__);
		return -EINVAL;
	}

	if (req->process_buffer_depth > U32_MAX - req->program_buffer_depth) {
		dev_err(chan->isp_dev,
			"%s: Process and Program status buffer larger than expected\n",
			__func__);
		return -EINVAL;
	}

	if ((req->process_buffer_depth + req->program_buffer_depth) >
		(U32_MAX / sizeof(uint32_t))) {
		dev_err(chan->isp_dev,
			"%s: Process and Program status buffer larger than expected\n",
			__func__);
		return -EINVAL;
	}

	/* Setup the progress status buffer */
	err = capture_common_setup_progress_status_notifier(
		&capture->progress_status_notifier,
		req->mem,
		(req->process_buffer_depth + req->program_buffer_depth) *
			sizeof(uint32_t),
		req->mem_offset);

	if (err < 0) {
		dev_err(chan->isp_dev,
			"%s: Process progress status setup failed\n",
			__func__);
		return -EFAULT;
	}

	dev_dbg(chan->isp_dev, "Progress status mem offset %u\n",
			req->mem_offset);
	dev_dbg(chan->isp_dev, "Process buffer depth %u\n",
			req->process_buffer_depth);
	dev_dbg(chan->isp_dev, "Program buffer depth %u\n",
			req->program_buffer_depth);

	capture->capture_desc_ctx.progress_status_buffer_depth =
			req->process_buffer_depth;
	capture->program_desc_ctx.progress_status_buffer_depth =
			req->program_buffer_depth;

	capture->is_progress_status_notifier_set = true;
	return err;
}

/**
 * @brief Submits a buffer request to the ISP capture channel.
 *
 * This function performs the following operations:
 * - Validates the input channel context.
 * - Validates the buffer request parameter.
 * - Retrieves the capture data from the channel structure.
 * - Calls @ref capture_buffer_request() to submit the buffer request with the
 *   specified memory and flags.
 *
 * @param[in]  chan  Pointer to the @ref tegra_isp_channel structure.
 *                   Valid value: non-NULL.
 * @param[in]  req   Pointer to the @ref isp_buffer_req structure containing
 *                   the buffer request details.
 *                   Valid value: non-NULL.
 *
 * @retval 0            On successful submission of the buffer request.
 * @retval -ENODEV      If the channel context is invalid or capture data is
 *                      uninitialized.
 * @retval -EINVAL      If the buffer request is invalid.
 * @retval (int)        If @ref capture_buffer_request() fails, the corresponding
 *                      error code is returned.
 */
int isp_capture_buffer_request(
	struct tegra_isp_channel *chan,
	struct isp_buffer_req *req)
{
	struct isp_capture *capture;
	int err;

	if (chan == NULL) {
		pr_err("%s: invalid context\n", __func__);
		return -ENODEV;
	}

	if (req == NULL) {
		dev_err(chan->isp_dev,
			"%s: Invalid req\n", __func__);
		return -EINVAL;
	}
	capture = chan->capture_data;

	if (capture == NULL) {
		dev_err(chan->isp_dev,
			"%s: isp capture uninitialized\n", __func__);
		return -ENODEV;
	}

	err = capture_buffer_request(
		capture->buffer_ctx, req->mem, req->flag);
	return err;
}

/**
 * @brief Determines if the ISP capture device chip ID is a Tegra T26x.
 *
 * This function performs the following operations:
 * - Parses the device tree phandle "nvidia,isp-devices" using @ref of_parse_phandle().
 * - Reads the "compatible" property from the node using @ref of_property_read_string().
 * - If reading the property fails, releases the node with @ref of_node_put().
 * - Checks if the "compatible" string contains "tegra26" using @ref strstr().
 * - Releases the node using @ref of_node_put().
 * - Determines if "tegra26" is found in the compatible string.
 *
 * @param[in]  of_node  Pointer to the @ref device_node structure representing the device.
 *                      Valid value: non-NULL.
 *
 * @retval true       If the ISP capture device is identified as Tegra T26x.
 * @retval false      If @ref of_parse_phandle(), @ref of_property_read_string(),
 *                    @ref of_node_put(), or @ref strstr() fails to identify the
 *                    device as Tegra T26x.
 */
static inline bool isp_capture_is_t26x(struct device_node *of_node)
{
	struct device_node *node;
	const char *compatible;
	int ret = 0;
	bool is_t26x = false;

	node = of_parse_phandle(of_node, "nvidia,isp-devices", 0);
	if (node == NULL)
		return false;

	ret = of_property_read_string(node, "compatible", &compatible);
	if (ret != 0) {
		of_node_put(node);
		return false;
	}

	is_t26x = (strstr(compatible, "tegra26") != NULL);
	of_node_put(node);
	return is_t26x;
}

/**
 * @brief Probes and initializes ISP capture devices for the platform.
 *
 * This function performs the following operations:
 * - Allocates memory for @ref tegra_capture_isp_data using @ref devm_kzalloc().
 * - Reads the "nvidia,isp-max-channels" property from the device tree using
 *   @ref of_property_read_u32().
 * - Determines if the device is Tegra T26x by calling @ref isp_capture_is_t26x().
 * - Validates the maximum number of ISP channels based on the device type.
 * - Iterates through the "nvidia,isp-devices" nodes using @ref of_parse_phandle()
 *   and finds corresponding devices using @ref of_find_device_by_node().
 * - Releases device tree nodes after finding devices using @ref of_node_put().
 * - Associates the allocated data with the platform device using
 *   @ref platform_set_drvdata().
 * - Registers ISP channels by calling @ref isp_channel_drv_register().
 * - In case of any error after allocation, releases allocated devices using
 *   @ref put_device().
 *
 * @param[in]  pdev
 *                    Pointer to the @ref platform_device structure.
 *
 *                    Valid value: non-NULL.
 *
 * @retval 0               On successful probing and initialization of ISP capture devices.
 * @retval -ENOMEM         If memory allocation via @ref devm_kzalloc() fails.
 * @retval -EINVAL         If reading device property via @ref of_property_read_u32() fails,
 *                         max channels read via @ref of_property_read_u32() exceeds
 *                         channel limits, iterating "nvidia,isp-devices" nodes exceeds
 *                         array size, or no ISP devices are found.
 * @retval -ENODEV         If ISP devices are not found using @ref of_find_device_by_node().
 * @retval (int)           On failure of @ref isp_channel_drv_register().
 */
static int capture_isp_probe(struct platform_device *pdev)
{
	uint32_t i;
	int err = 0;
	struct tegra_capture_isp_data *info;
	struct device *dev;

	if (pdev == NULL) {
		pr_err("%s: Invalid platform device\n", __func__);
		return -EINVAL;
	}

	dev = &pdev->dev;
	if (dev == NULL) {
		pr_err("%s: Invalid device\n", __func__);
		return -EINVAL;
	}

	dev_dbg(dev, "%s:tegra-camrtc-capture-isp probe\n", __func__);

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;

	info->num_isp_devices = 0;

	if (dev->of_node == NULL) {
		dev_err(dev, "No device tree node found\n");
		return -EINVAL;
	}

	err = of_property_read_u32(dev->of_node, "nvidia,isp-max-channels",
			&info->max_isp_channels);
	if (err < 0) {
		err = -EINVAL;
		goto cleanup;
	}
	if (isp_capture_is_t26x(dev->of_node)) {
		if (info->max_isp_channels > NUM_ISP_CHANNELS_T26x) {
			err = -EINVAL;
			goto cleanup;
		}
	} else {
		if ((info->max_isp_channels == 0) || (info->max_isp_channels > NUM_ISP_CHANNELS)) {
			err = -EINVAL;
			goto cleanup;
		}
	}

	memset(info->isp_pdevices, 0, sizeof(info->isp_pdevices));

	i = 0U;
	do {
		struct device_node *node;
		struct platform_device *ispdev;

		node = of_parse_phandle(dev->of_node, "nvidia,isp-devices", i);
		if (node == NULL)
			break;

		if (info->num_isp_devices >= ARRAY_SIZE(info->isp_pdevices)) {
			of_node_put(node);
			err = -EINVAL;
			goto cleanup;
		}

		ispdev = of_find_device_by_node(node);
		of_node_put(node);

		if (ispdev == NULL) {
			dev_warn(dev, "isp node %u has no device\n", i);
			err = -ENODEV;
			goto cleanup;
		}

		info->isp_pdevices[i] = ispdev;
		info->num_isp_devices++;
	} while (!check_add_overflow(i, 1U, &i));

	if (info->num_isp_devices < 1)
		return -EINVAL;

	platform_set_drvdata(pdev, info);

	err = isp_channel_drv_register(pdev, info->max_isp_channels);
	if (err)
		goto cleanup;

	return 0;

cleanup:
	for (i = 0; i < info->num_isp_devices; i++)
		put_device(&info->isp_pdevices[i]->dev);

	dev_err(dev, "%s:tegra-camrtc-capture-isp probe failed\n", __func__);
	return err;
}

/**
 * @brief Removes and cleans up ISP capture devices for the platform.
 *
 * This function performs the following operations:
 * - Logs the removal process using @ref dev_dbg().
 * - Retrieves the capture data associated with the platform device using
 *   @ref platform_get_drvdata().
 * - Iterates through all ISP devices and releases each device using
 *   @ref put_device().
 * - Returns a success status upon completion.
 *
 * @param[in]  pdev  Pointer to the @ref platform_device structure.
 *                   Valid value: non-NULL.
 *
 * @retval 0         On successful removal and cleanup of ISP capture devices.
 */
static int capture_isp_remove(struct platform_device *pdev)
{
	struct tegra_capture_isp_data *info;
	uint32_t i;
	struct device *dev = &pdev->dev;

	dev_dbg(dev, "%s:tegra-camrtc-capture-isp remove\n", __func__);

	info = platform_get_drvdata(pdev);

	for (i = 0; i < info->num_isp_devices; i++)
		put_device(&info->isp_pdevices[i]->dev);

	return 0;
}

static const struct of_device_id capture_isp_of_match[] = {
	{ .compatible = "nvidia,tegra-camrtc-capture-isp" },
	{ },
};

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
/**
 * @brief Wrapper function to remove and clean up ISP capture devices.
 *
 * This function performs the following operations:
 * - Calls @ref capture_isp_remove() to remove and clean up ISP capture
 *   devices associated with the platform device.
 *
 * @param[in]  pdev  Pointer to the @ref platform_device structure.
 *                   Valid value: non-NULL.
 */
static void capture_isp_remove_wrapper(struct platform_device *pdev)
{
	capture_isp_remove(pdev);
}
#else
/**
 * @brief Wrapper function to remove the ISP capture device.
 *
 * This function performs the following operations:
 * - Calls @ref capture_isp_remove() to remove the ISP capture device.
 * - Returns the status of the remove operation.
 *
 * @param[in]  pdev  Pointer to the @ref platform_device structure.
 *                   Valid value: non-NULL.
 *
 * @retval 0            On successful removal of the ISP capture device.
 * @retval (int)        Errors returned from @ref capture_isp_remove().
 */
static int capture_isp_remove_wrapper(struct platform_device *pdev)
{
	return capture_isp_remove(pdev);
}
#endif

static struct platform_driver capture_isp_driver = {
	.probe = capture_isp_probe,
	.remove = capture_isp_remove_wrapper,
	.driver = {
		.owner = THIS_MODULE,
		.name = "tegra-camrtc-capture-isp",
		.of_match_table = capture_isp_of_match
	}
};

/**
 * @brief Initializes and registers the ISP capture driver.
 *
 * This function performs the following operations:
 * - Calls @ref isp_channel_drv_init() to initialize the ISP channel driver.
 * - Calls @ref platform_driver_register() to register the capture ISP platform driver.
 * - If registration fails, calls @ref isp_channel_drv_exit() to clean up.
 *
 * @retval 0            On successful initialization and registration of the ISP
 *                      capture driver.
 * @retval (int)        If @ref isp_channel_drv_init() or @ref platform_driver_register()
 *                      fails, the corresponding error code is returned.
 */
static int __init capture_isp_init(void)
{
	int err;
	err = isp_channel_drv_init();
	if (err)
		return err;

	err = platform_driver_register(&capture_isp_driver);
	if (err) {
		isp_channel_drv_exit();
		return err;
	}
	return 0;
}

/**
 * @brief Cleans up and unregisters the ISP capture driver.
 *
 * This function performs the following operations:
 * - Calls @ref isp_channel_drv_exit() to clean up the ISP channel driver.
 * - Calls @ref platform_driver_unregister() to unregister the capture ISP platform
 *   driver.
 */
static void __exit capture_isp_exit(void)
{
	isp_channel_drv_exit();
	platform_driver_unregister(&capture_isp_driver);
}

module_init(capture_isp_init);
module_exit(capture_isp_exit);

#if defined(NV_MODULE_IMPORT_NS_CALLS_STRINGIFY)
MODULE_IMPORT_NS(DMA_BUF);
#else
MODULE_IMPORT_NS("DMA_BUF");
#endif
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("tegra capture-isp driver");
