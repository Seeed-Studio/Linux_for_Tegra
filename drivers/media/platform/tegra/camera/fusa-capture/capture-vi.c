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
 * @file drivers/media/platform/tegra/camera/fusa-capture/capture-vi.c
 *
 * @brief VI channel operations for the T234 Camera RTCPU platform.
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
#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/tegra-capture-ivc.h>
#include <linux/tegra-camera-rtcpu.h>
#include <linux/tegra-rtcpu-trace.h>

#include <asm/arch_timer.h>
#include <uapi/linux/nvhost_events.h>
#include <camera/nvcamera_log.h>
#include "soc/tegra/camrtc-capture.h"
#include "soc/tegra/camrtc-capture-messages.h"
#include <media/fusa-capture/capture-vi-channel.h>
#include <media/fusa-capture/capture-common.h>

#include <media/fusa-capture/capture-vi.h>
#ifdef NV_IS_L4T
#include <media/vi.h>
#include <media/mc_common.h>
#include <media/tegra_camera_platform.h>
#include "camera/vi/vi5_fops.h"
#endif

/**
 * @brief Invalid VI channel ID; the channel is not initialized.
 */
#define CAPTURE_CHANNEL_INVALID_ID	U16_C(0xFFFF)

/**
 * @brief Invalid VI channel mask; no channels are allocated.
 */
#define CAPTURE_CHANNEL_INVALID_MASK	U64_C(0x0)

/**
 * @brief Invalid NVCSI stream ID; the stream is not initialized.
 */
#define NVCSI_STREAM_INVALID_ID		U32_C(0xFFFF)

/**
 * @brief INVALID NVCSI TPG virtual channel ID; the TPG stream is not enabled.
 */
#define NVCSI_STREAM_INVALID_TPG_VC_ID	U32_C(0xFFFF)

/**
 * @brief The default number of VI channels to be used if not specified in
 * the device tree.
 */
#define DEFAULT_VI_CHANNELS	U32_C(64)

/**
 * @brief Maximum number of VI channels supported by KMD in total
 */
#define NUM_VI_CHANNELS	U32_C(72)

/**
 * @brief Maximum number of VI devices supported.
 */
#define MAX_VI_UNITS	U32_C(0x2)

/**
 * @brief Invalid VI unit ID, to initialize vi-mapping table before parsing DT.
 */
#define INVALID_VI_UNIT_ID  U32_C(0xFFFF)

/**
 * @brief Maximum number of NVCSI streams supported.
 */
#define MAX_NVCSI_STREAM_IDS  U32_C(0x6)

/**
 * @brief Maximum number of virtual channel supported per stream.
 */
#define MAX_VIRTUAL_CHANNEL_PER_STREAM  U32_C(16)

/**
 * @brief Maximum number of NVCSI ports supported.
 */
#define MAX_NVCSI_PORT_IDS              U32_C(0x8)

/**
 * @brief A 2-D array for storing all possible tegra_vi_channel struct pointers.
 */
static struct tegra_vi_channel *channels[MAX_NVCSI_STREAM_IDS][MAX_VIRTUAL_CHANNEL_PER_STREAM];
/**
 * @brief Names of VI-unit and CSI-stream mapping elements in device-tree node
 */
static const char * const vi_mapping_elements[] = {
	"csi-stream-id",
	"vi-unit-id"
};

/**
 * @brief The Capture-VI standalone driver context.
 */
struct tegra_capture_vi_data {
#ifdef NV_IS_L4T
	struct vi vi_common; /**< VI device context */
#endif
	uint32_t num_vi_devices; /**< Number of available VI devices */
	struct platform_device *vi_pdevices[MAX_VI_UNITS];
		/**< VI nvhost client platform device for each VI instance */
	uint32_t max_vi_channels;
		/**< Maximum number of VI capture channel devices */
	uint32_t num_csi_vi_maps;
		/**< Number of NVCSI to VI mapping elements in the table */
	uint32_t vi_instance_table[MAX_NVCSI_STREAM_IDS];
		/**< NVCSI stream-id & VI instance mapping, read from the DT */
};

/**
 * @brief Initialize a VI syncpoint and get its GoS backing.
 *
 * This function performs the following operations:
 * - Clears the syncpoint handle if enable is false.
 * - Allocates a syncpoint with the given name using
 *   @ref struct syncpoint_info::ops::alloc_syncpt().
 * - Gets the syncpoint handle using @ref host1x_syncpt_get_by_id_noref().
 * - Reads the syncpoint value using @ref host1x_syncpt_read().
 * - Gets the GoS backing for the syncpoint using
 *   @ref struct syncpoint_info::ops::get_gos_backing().
 * - In case of failure during any step, releases the allocated syncpoint and clears
 *   the handle using @ref struct syncpoint_info::ops::release_syncpt().
 *
 * @param[in] chan       VI channel context.
 *                       Valid value: non-NULL
 * @param[in] name       Syncpoint name.
 *                       Valid value: non-NULL
 * @param[in] enable     Whether to initialize or just clear the syncpoint.
 *                       Valid range: [true, false]
 * @param[out] sp        Syncpoint handle to initialize.
 *                       Valid value: non-NULL
 *
 * @retval 0        Operation completed successfully.
 * @retval -EINVAL  Error returned from @ref host1x_syncpt_get_by_id_noref().
 * @retval (int)    Error codes returned from @ref struct syncpoint_info::ops::alloc_syncpt() or
 *                  @ref struct syncpoint_info::ops::get_syncpt_gos_backing().
 */
static int vi_capture_setup_syncpt(
	struct tegra_vi_channel *chan,
	const char *name,
	bool enable,
	struct syncpoint_info *sp)
{
	struct platform_device *pdev = chan->ndev;
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct host1x_syncpt *host1x_sp;
	uint32_t gos_index, gos_offset;
	int err;

	memset(sp, 0, sizeof(*sp));
	sp->gos_index = GOS_INDEX_INVALID;

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
 * @brief Fast forward a VI syncpoint to its threshold value.
 *
 * This function performs the following operation:
 * - If progress syncpoint exists, calls @ref struct syncpoint_info::ops::fast_forward_syncpt() to advance the
 *   syncpoint to its threshold value.
 *
 * @param[in] chan  VI channel context.
 *                  Valid value: non-NULL.
 */
static void vi_capture_fastforward_syncpt(
	struct tegra_vi_channel *chan)
{
	struct vi_capture *capture = chan->capture_data;

	if (capture->progress_sp.id)
		chan->ops->fast_forward_syncpt(chan->ndev,
						capture->progress_sp.id,
						capture->progress_sp.threshold);
}

/**
 * @brief Release a VI syncpoint and clear its handle.
 *
 * This function performs the following operations:
 * - If syncpoint ID exists, releases it using @ref struct syncpoint_info::ops::release_syncpt().
 * - Clears the syncpoint handle structure.
 *
 * @param[in] chan  VI channel context.
 *                  Valid value: non-NULL.
 * @param[out] sp   Syncpoint handle to release.
 *                  Valid value: non-NULL.
 */
static void vi_capture_release_syncpt(
	struct tegra_vi_channel *chan,
	struct syncpoint_info *sp)
{
	if (sp->id)
		chan->ops->release_syncpt(chan->ndev, sp->id);

	memset(sp, 0, sizeof(*sp));
}

/**
 * @brief Release the VI channel progress, embedded data and line timer syncpoints.
 *
 * This function performs the following operations:
 * - Releases the progress syncpoint using @ref vi_capture_release_syncpt()
 * - Releases the embedded data syncpoint using @ref vi_capture_release_syncpt()
 * - Releases the line timer syncpoint using @ref vi_capture_release_syncpt()
 *
 * @param[in] chan  VI channel context
 *                  Valid value: non-NULL
 */
static void vi_capture_release_syncpts(
	struct tegra_vi_channel *chan)
{
	struct vi_capture *capture = chan->capture_data;

	vi_capture_release_syncpt(chan, &capture->progress_sp);
	vi_capture_release_syncpt(chan, &capture->embdata_sp);
	vi_capture_release_syncpt(chan, &capture->linetimer_sp);
}

/**
 * @brief Set up the VI channel progress, embedded data and line timer syncpoints.
 *
 * This function performs the following operations:
 * - Gets the GoS table using @ref struct tegra_vi_channel::ops::get_gos_table().
 * - Sets up the progress syncpoint using @ref vi_capture_setup_syncpt().
 * - Sets up the embedded data syncpoint if enabled using @ref vi_capture_setup_syncpt().
 * - Sets up the line timer syncpoint if enabled using @ref vi_capture_setup_syncpt().
 * - In case of failure, releases all allocated syncpoints using @ref vi_capture_release_syncpts().
 *
 * @param[in] chan         VI channel context.
 *                        Valid value: non-NULL
 * @param[in] flags       Bitmask for channel flags.
 *                        Valid range: @ref CAPTURE_CHANNEL_FLAGS
 *
 * @retval 0        Operation completed successfully.
 * @retval -EINVAL  Error returned from @ref vi_capture_setup_syncpt().
 */
static int vi_capture_setup_syncpts(
	struct tegra_vi_channel *chan,
	uint32_t flags)
{
	struct vi_capture *capture = chan->capture_data;
	int err = 0;

	chan->ops->get_gos_table(chan->ndev,
				&capture->num_gos_tables,
				&capture->gos_tables);

	err = vi_capture_setup_syncpt(chan, "progress", true,
			&capture->progress_sp);
	if (err < 0)
		goto fail;

	err = vi_capture_setup_syncpt(chan, "embdata",
				(flags & CAPTURE_CHANNEL_FLAG_EMBDATA) != 0,
				&capture->embdata_sp);
	if (err < 0)
		goto fail;

	err = vi_capture_setup_syncpt(chan, "linetimer",
				(flags & CAPTURE_CHANNEL_FLAG_LINETIMER) != 0,
				&capture->linetimer_sp);
	if (err < 0)
		goto fail;

	return 0;

fail:
	vi_capture_release_syncpts(chan);
	return err;
}

/**
 * @brief Read the value of a VI channel syncpoint.
 *
 * This function performs the following operation:
 * - If syncpoint ID exists, gets the syncpoint handle using @ref host1x_syncpt_get_by_id_noref().
 * - If handle is valid, reads its value using @ref host1x_syncpt_read().
 *
 * @param[in] chan  VI channel context.
 *                  Valid value: non-NULL
 * @param[in] sp    Syncpoint handle.
 *                  Valid value: non-NULL
 * @param[out] val  Syncpoint value.
 *                  Valid value: non-NULL
 *
 * @retval 0        Operation completed successfully.
 * @retval -EINVAL  Error returned from @ref host1x_syncpt_get_by_id_noref().
 */
static int vi_capture_read_syncpt(
	struct tegra_vi_channel *chan,
	struct syncpoint_info *sp,
	uint32_t *val)
{
	struct platform_device *pdev = chan->ndev;
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct host1x_syncpt *host1x_sp;

	if (sp->id) {
		host1x_sp = host1x_syncpt_get_by_id_noref(pdata->host1x, sp->id);
		if (!host1x_sp) {
			dev_err(chan->dev,
				"%s: get syncpt %i val failed\n", __func__,
				sp->id);
			return -EINVAL;
		}
		*val = host1x_syncpt_read(host1x_sp);
	}

	return 0;
}

/**
 * @brief VI channel callback function for capture IVC messages.
 *
 * This function performs the following operations:
 * - Validates input parameters.
 * - For CAPTURE_STATUS_IND messages:
 *   - Unpins memory if pinned using @ref vi_capture_request_unpin().
 *   - Synchronizes DMA memory using @ref dma_sync_single_range_for_cpu().
 *   - Updates progress status using @ref capture_common_set_progress_status() if enabled.
 *   - Otherwise completes the capture response using @ref complete().
 *
 * @param[in] ivc_resp  IVC @ref CAPTURE_MSG from RCE.
 *                      Valid value: non-NULL.
 * @param[in] pcontext  VI channel capture context.
 *                      Valid value: non-NULL.
 */
static void vi_capture_ivc_status_callback(
	const void *ivc_resp,
	const void *pcontext)
{
	struct CAPTURE_MSG *status_msg = (struct CAPTURE_MSG *)ivc_resp;
	struct vi_capture *capture = (struct vi_capture *)pcontext;
	struct tegra_vi_channel *chan = capture->vi_channel;
	uint32_t buffer_index;

	if (unlikely(capture == NULL)) {
		dev_err(chan->dev, "%s: invalid context", __func__);
		return;
	}

	if (unlikely(status_msg == NULL)) {
		dev_err(chan->dev, "%s: invalid response", __func__);
		return;
	}

	switch (status_msg->header.msg_id) {
	case CAPTURE_STATUS_IND:
		buffer_index = status_msg->capture_status_ind.buffer_index;
		if (capture->is_mem_pinned)
			vi_capture_request_unpin(chan, buffer_index);
		dma_sync_single_range_for_cpu(capture->rtcpu_dev,
			capture->requests.iova,
			buffer_index * capture->request_size,
			capture->request_size, DMA_FROM_DEVICE);

		if (capture->is_progress_status_notifier_set) {
			capture_common_set_progress_status(
					&capture->progress_status_notifier,
					buffer_index,
					capture->progress_status_buffer_depth,
					PROGRESS_STATUS_DONE);
		} else {
			/*
			 * Only fire completions if not using
			 * the new progress status buffer mechanism
			 */
			complete(&capture->capture_resp);
		}
		dev_dbg(chan->dev, "%s: status chan_id %u msg_id %u\n",
				__func__, status_msg->header.channel_id,
				status_msg->header.msg_id);
		break;
	default:
		dev_err(chan->dev,
			"%s: unknown capture resp", __func__);
		break;
	}
}

/**
 * @brief Send a capture-control IVC message to RCE on a VI channel.
 *
 * This function performs the following operations:
 * - Locks the control message mutex using @ref mutex_lock().
 * - Submits the capture control message using @ref tegra_capture_ivc_control_submit().
 * - Waits for the response with timeout using @ref wait_for_completion_timeout().
 * - If the response is not received within the timeout,
 *   log the RCE snapshot by calling @ref rtcpu_trace_panic_callback().
 * - Validates the response header matches the request using @ref memcmp().
 * - Unlocks the control message mutex using @ref mutex_unlock().
 *
 * @param[in] chan      VI channel context.
 *                      Valid value: non-NULL.
 * @param[in] msg       IVC message payload.
 *                      Valid value: non-NULL
 * @param[in] size      Size of message in bytes.
 *                      Valid value: non-zero.
 * @param[in] resp_id   IVC message identifier.
 *                      Valid range: @ref CAPTURE_MSG_IDS.
 *
 * @retval 0         Operation completed successfully.
 * @retval -EINVAL   Response header validation failed.
 * @retval -ETIMEDOUT Response timed out.
 * @retval (int)     Error codes returned from @ref tegra_capture_ivc_control_submit().
 */
static int vi_capture_ivc_send_control(
	struct tegra_vi_channel *chan,
	const struct CAPTURE_CONTROL_MSG *msg,
	size_t size,
	uint32_t resp_id)
{
	struct vi_capture *capture = chan->capture_data;
	struct CAPTURE_MSG_HEADER resp_header = msg->header;
	uint32_t timeout = HZ;
	int err = 0;

	dev_dbg(chan->dev, "%s: sending chan_id %u msg_id %u\n",
			__func__, resp_header.channel_id, resp_header.msg_id);
	resp_header.msg_id = resp_id;
	/* Send capture control IVC message */
	mutex_lock(&capture->control_msg_lock);
	err = tegra_capture_ivc_control_submit(msg, size);
	if (err < 0) {
		dev_err(chan->dev, "IVC control submit failed\n");
		goto fail;
	}

	timeout = wait_for_completion_timeout(
			&capture->control_resp, timeout);
	if (timeout <= 0) {
		dev_err(chan->dev,
			"capture control message timed out\n");
		rtcpu_trace_panic_callback(capture->rtcpu_dev);
		err = -ETIMEDOUT;
		goto fail;
	}

	if (memcmp(&resp_header, &capture->control_resp_msg.header,
			sizeof(resp_header)) != 0) {
		dev_err(chan->dev,
			"unexpected response from camera processor\n");
		err = -EINVAL;
		goto fail;
	}

	mutex_unlock(&capture->control_msg_lock);
	dev_dbg(chan->dev, "%s: response chan_id %u msg_id %u\n",
			__func__, capture->control_resp_msg.header.channel_id,
			capture->control_resp_msg.header.msg_id);
	return 0;

fail:
	mutex_unlock(&capture->control_msg_lock);
	return err;
}

/**
 * @brief VI channel callback function for capture-control IVC messages.
 *
 * This function performs the following operations:
 * - Validates input parameters.
 * - For supported control message responses:
 *   - Copies the response message to the channel's control response buffer using @ref memcpy().
 *   - Completes the control response wait using @ref complete().
 *
 * @param[in] ivc_resp  IVC @ref CAPTURE_CONTROL_MSG from RCE.
 *                      Valid value: non-NULL.
 * @param[in] pcontext  VI channel capture context.
 *                      Valid value: non-NULL.
 */
static void vi_capture_ivc_control_callback(
	const void *ivc_resp,
	const void *pcontext)
{
	const struct CAPTURE_CONTROL_MSG *control_msg = ivc_resp;
	struct vi_capture *capture = (struct vi_capture *)pcontext;
	struct tegra_vi_channel *chan = capture->vi_channel;

	if (unlikely(capture == NULL)) {
		dev_err(chan->dev, "%s: invalid context", __func__);
		return;
	}

	if (unlikely(control_msg == NULL)) {
		dev_err(chan->dev, "%s: invalid response", __func__);
		return;
	}

	switch (control_msg->header.msg_id) {
	case CAPTURE_CHANNEL_SETUP_RESP:
	case CAPTURE_CHANNEL_RESET_RESP:
	case CAPTURE_CHANNEL_RELEASE_RESP:
	case CAPTURE_COMPAND_CONFIG_RESP:
	case CAPTURE_PDAF_CONFIG_RESP:
	case CAPTURE_SYNCGEN_ENABLE_RESP:
	case CAPTURE_SYNCGEN_DISABLE_RESP:
	case CAPTURE_PHY_STREAM_OPEN_RESP:
	case CAPTURE_PHY_STREAM_CLOSE_RESP:
	case CAPTURE_PHY_STREAM_DUMPREGS_RESP:
	case CAPTURE_CSI_STREAM_SET_CONFIG_RESP:
	case CAPTURE_CSI_STREAM_SET_PARAM_RESP:
	case CAPTURE_CSI_STREAM_TPG_SET_CONFIG_RESP:
	case CAPTURE_CSI_STREAM_TPG_START_RESP:
	case CAPTURE_CSI_STREAM_TPG_START_RATE_RESP:
	case CAPTURE_CSI_STREAM_TPG_APPLY_GAIN_RESP:
	case CAPTURE_CSI_STREAM_TPG_STOP_RESP:
	case CAPTURE_CHANNEL_EI_RESP:
	case CAPTURE_HSM_CHANSEL_ERROR_MASK_RESP:
		memcpy(&capture->control_resp_msg, control_msg,
				sizeof(*control_msg));
		complete(&capture->control_resp);
		break;
	default:
		dev_err(chan->dev,
			"%s: unknown capture control resp 0x%x", __func__,
			control_msg->header.msg_id);
		break;
	}
}

/**
 * @brief Initialize a VI capture channel.
 *
 * This function performs the following operations:
 * - Finds and validates the RTCPU device node using @ref of_find_node_by_path().
 * - Checks device availability using @ref of_device_is_available().
 * - Gets platform device using @ref of_find_device_by_node().
 * - Allocates and initializes the VI capture channel context using @ref kzalloc().
 * - Initializes completion objects using @ref init_completion().
 * - Initializes mutex locks using @ref mutex_init().
 * - Sets up initial channel state.
 *
 * @param[in] chan           VI channel context.
 *                          Valid value: non-NULL.
 * @param[in] is_mem_pinned  Whether memory is pinned.
 *                          Valid range: [true, false].
 *
 * @retval 0        Operation completed successfully.
 * @retval -ENODEV  RTCPU device node not found or invalid.
 * @retval -ENOMEM  Failed to allocate capture channel context from @ref kzalloc().
 */
int vi_capture_init(
	struct tegra_vi_channel *chan,
	bool is_mem_pinned)
{
	struct vi_capture *capture;
	struct device_node *dn;
	struct platform_device *rtc_pdev;
	struct device *dev;

	dev = &chan->vi_capture_pdev->dev;

	dev_dbg(dev, "%s++\n", __func__);
	dn = of_find_node_by_path("tegra-camera-rtcpu");
	if (of_device_is_available(dn) == 0) {
		dev_err(dev, "failed to find rtcpu device node\n");
		return -ENODEV;
	}
	rtc_pdev = of_find_device_by_node(dn);
	if (rtc_pdev == NULL) {
		dev_err(dev, "failed to find rtcpu platform\n");
		return -ENODEV;
	}

	capture = kzalloc(sizeof(*capture), GFP_KERNEL);
	if (unlikely(capture == NULL)) {
		dev_err(dev, "failed to allocate capture channel\n");
		return -ENOMEM;
	}

	capture->rtcpu_dev = &rtc_pdev->dev;

	init_completion(&capture->control_resp);
	init_completion(&capture->capture_resp);

	mutex_init(&capture->reset_lock);
	mutex_init(&capture->control_msg_lock);
	mutex_init(&capture->unpins_list_lock);

	capture->vi_channel = chan;
	chan->capture_data = capture;
	chan->rtcpu_dev = capture->rtcpu_dev;

	capture->is_mem_pinned = is_mem_pinned;
	capture->channel_id = CAPTURE_CHANNEL_INVALID_ID;

	capture->stream_id = NVCSI_STREAM_INVALID_ID;
	capture->csi_port = NVCSI_PORT_UNSPECIFIED;
	capture->virtual_channel_id = NVCSI_STREAM_INVALID_TPG_VC_ID;

	return 0;
}
EXPORT_SYMBOL_GPL(vi_capture_init);

/**
 * @brief Shutdown and cleanup a VI capture channel.
 *
 * This function performs the following operations:
 * - Validates input parameters.
 * - Resets the channel using @ref vi_capture_reset() if active.
 * - Releases CSI stream using @ref csi_stream_release() if active.
 * - Releases the channel using @ref vi_capture_release().
 * - Unpins memory using @ref vi_capture_request_unpin() if memory was pinned.
 * - Unpins capture requests using @ref capture_common_unpin_memory().
 * - Destroys buffer table using @ref destroy_buffer_table() if exists.
 * - Frees allocated resources using @ref kfree() and @ref vfree().
 *
 * @param[in] chan  VI channel context.
 *                  Valid value: non-NULL.
 */
void vi_capture_shutdown(
	struct tegra_vi_channel *chan)
{
	struct vi_capture *capture;

	if (unlikely(chan == NULL)) {
		pr_err("%s: vi channel pointer is NULL\n", __func__);
		return;
	}

	dev_dbg(chan->dev, "%s--\n", __func__);

	capture = chan->capture_data;
	if (unlikely(capture == NULL)) {
		dev_err(chan->dev, "%s: invalid context", __func__);
		return;
	}

	if (capture->channel_id != CAPTURE_CHANNEL_INVALID_ID)
		vi_capture_reset(chan,
			CAPTURE_CHANNEL_RESET_FLAG_IMMEDIATE);

	if (capture->stream_id != NVCSI_STREAM_INVALID_ID)
		csi_stream_release(chan);

	if (capture->channel_id != CAPTURE_CHANNEL_INVALID_ID)	{
		int i;

		vi_capture_release(chan,
			CAPTURE_CHANNEL_RESET_FLAG_IMMEDIATE);

		if (capture->is_mem_pinned) {
			for (i = 0; i < capture->queue_depth; i++)
				vi_capture_request_unpin(chan, i);
		}
		capture_common_unpin_memory(&capture->requests);
		if (capture->buf_ctx != NULL) {
			destroy_buffer_table(capture->buf_ctx);
			capture->buf_ctx = NULL;
		}

		vfree(capture->unpins_list);
		capture->unpins_list = NULL;
	}
	kfree(capture);
	chan->capture_data = NULL;
}
EXPORT_SYMBOL_GPL(vi_capture_shutdown);

/**
 * @brief Get the nvhost device for a VI channel based on CSI stream ID.
 *
 * This function performs the following operations:
 * - Retrieves the platform driver data for the channel's capture device
 *   using @ref platform_get_drvdata().
 * - Gets the VI instance from the mapping table using CSI stream ID.
 * - Validates the VI instance ID.
 * - Applies array bounds check using @ref array_index_nospec().
 * - Sets the channel's device and nvhost device pointers from platform device.
 *
 * @param[in] chan   VI channel context.
 *                   Valid value: non-NULL.
 * @param[in] setup  VI capture setup parameters.
 *                   Valid value: non-NULL.
 */
void vi_get_nvhost_device(
	struct tegra_vi_channel *chan,
	struct vi_capture_setup *setup)
{
	uint32_t vi_inst = 0;

	struct tegra_capture_vi_data *info =
		platform_get_drvdata(chan->vi_capture_pdev);

	if (setup->csi_stream_id >= MAX_NVCSI_STREAM_IDS) {
		dev_err(&chan->vi_capture_pdev->dev, "CSI stream ID over the limit\n");
		return;
	}

	vi_inst = info->vi_instance_table[setup->csi_stream_id];

	if (vi_inst >= MAX_VI_UNITS) {
		dev_err(&chan->vi_capture_pdev->dev, "Invalid VI device Id\n");
		chan->dev = NULL;
		chan->ndev = NULL;
		return;
	}
	vi_inst = array_index_nospec(vi_inst, MAX_VI_UNITS);

	chan->dev = &info->vi_pdevices[vi_inst]->dev;
	chan->ndev = info->vi_pdevices[vi_inst];
}
EXPORT_SYMBOL_GPL(vi_get_nvhost_device);

/**
 * @brief Get the nvhost device for a given CSI stream ID.
 *
 * This function performs the following operations:
 * - Retrieves the platform driver data for the channel's capture device
 *   using @ref platform_get_drvdata().
 * - Gets the VI instance ID from the mapping table using CSI stream ID.
 * - Returns the corresponding platform device.
 *
 * @param[in] pdev          Platform device pointer.
 *                          Valid value: non-NULL.
 * @param[in] csi_stream_id CSI stream identifier.
 *                          Valid range: [0, MAX_NVCSI_STREAM_IDS - 1].
 *
 * @retval device*  Pointer to the nvhost device on success.
 * @retval NULL     If CSI stream ID is invalid.
 */
struct device *vi_csi_stream_to_nvhost_device(
	struct platform_device *pdev,
	uint32_t csi_stream_id)
{
	struct tegra_capture_vi_data *info = platform_get_drvdata(pdev);
	uint32_t vi_inst_id = 0;

	if (csi_stream_id >= MAX_NVCSI_STREAM_IDS) {
		dev_err(&pdev->dev, "Invalid NVCSI stream Id\n");
		return NULL;
	}

	vi_inst_id = info->vi_instance_table[csi_stream_id];
	return &info->vi_pdevices[vi_inst_id]->dev;
}
EXPORT_SYMBOL(vi_csi_stream_to_nvhost_device);

/**
 * @brief Set up a VI capture channel.
 *
 * This function performs the following operations:
 * - Validates input parameters and channel state.
 * - Retrieves the platform driver data for the channel's capture device
 *   using @ref platform_get_drvdata().
 * - Gets the VI instance from mapping table using CSI stream ID.
 * - If the channel's device is NULL, gets the nvhost device for the channel
 *   using @ref vi_get_nvhost_device().
 * - Validates setup parameters and capture channel ID.
 * - Copies setup parameters to channel context.
 * - Sets up channel syncpoints using @ref vi_capture_setup_syncpts().
 * - Registers control callback using @ref tegra_capture_ivc_register_control_cb().
 * - Allocates memory for memoryinfo ring buffer using @ref dma_alloc_coherent().
 * - Allocates memory for unpins list using @ref vzalloc().
 * - If @ref HAVE_VI_GOS_TABLES is defined, copies GoS tables to config.
 * - Sends channel setup request using @ref vi_capture_ivc_send_control().
 * - Updates channel state with response info.
 * - Updates control callback with capture channel ID using
 *   @ref tegra_capture_ivc_notify_capture_cb().
 * - Registers capture callback using @ref tegra_capture_ivc_register_capture_cb().
 * - Updates global channels array.
 * - In case of failure, releases allocated resources using @ref vfree(),
 *   @ref dma_free_coherent(), @ref tegra_capture_ivc_unregister_control_cb(),
 *   and @ref vi_capture_release_syncpts().
 *
 * @param[in] chan   VI channel context.
 *                   Valid value: non-NULL.
 * @param[in] setup  VI capture setup parameters.
 *                   Valid value: non-NULL.
 *
 * @retval 0        Operation completed successfully.
 * @retval -EINVAL  Invalid input parameters or response.
 * @retval -EEXIST  Channel already set up.
 * @retval -ENODEV  Capture channel is not initialized.
 * @retval (int)    Error codes from helper functions.
 */
int vi_capture_setup(
	struct tegra_vi_channel *chan,
	struct vi_capture_setup *setup)
{
	struct vi_capture *capture = chan->capture_data;
	struct tegra_capture_vi_data *info;
	uint32_t transaction;
	struct CAPTURE_CONTROL_MSG control_desc;
	struct CAPTURE_CONTROL_MSG *resp_msg = &capture->control_resp_msg;
	struct capture_channel_config *config =
		&control_desc.channel_setup_req.channel_config;
	int err = 0;
#ifdef HAVE_VI_GOS_TABLES
	int i;
#endif

	uint32_t vi_inst = 0;
	struct device *dev;

	dev = &chan->vi_capture_pdev->dev;

	if (setup->csi_stream_id >= MAX_NVCSI_STREAM_IDS ||
		setup->virtual_channel_id >= MAX_VIRTUAL_CHANNEL_PER_STREAM) {
		dev_err(dev, "Invalid stream id or virtual channel id\n");
		return -EINVAL;
	}

	if (chan->vi_capture_pdev == NULL) {
		dev_err(dev,
			"%s: channel capture device is NULL", __func__);
		return -EINVAL;
	}

#ifndef NV_IS_L4T
	if (setup->csi_port >= MAX_NVCSI_PORT_IDS) {
		dev_err(dev, "Invalid csi port\n");
		return -EINVAL;
	}
#endif

	info = platform_get_drvdata(chan->vi_capture_pdev);
	vi_inst = info->vi_instance_table[setup->csi_stream_id];

	/* V4L2 directly calls this function. So need to make sure the
	 * correct VI5 instance is associated with the VI capture channel.
	 */
	if (chan->dev == NULL) {
		vi_get_nvhost_device(chan, setup);
		if (chan->dev == NULL) {
			dev_err(&chan->vi_capture_pdev->dev,
				"%s: channel device is NULL", __func__);
			return -EINVAL;
		}
	}

	nv_camera_log(chan->ndev,
		__arch_counter_get_cntvct(),
		NVHOST_CAMERA_VI_CAPTURE_SETUP);

	if (setup->mem == 0 && setup->iova == 0) {
		dev_err(chan->dev,
			"%s: request buffer is NULL\n", __func__);
		return -EINVAL;
	}

	if (capture == NULL) {
		dev_err(chan->dev,
			 "%s: vi capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (capture->channel_id != CAPTURE_CHANNEL_INVALID_ID) {
		dev_err(chan->dev,
			"%s: already setup, release first\n", __func__);
		return -EEXIST;
	}

#ifndef NV_IS_L4T
	if (channels[setup->csi_stream_id][setup->virtual_channel_id] != NULL) {
		dev_err(chan->dev,
			"%s: channel already setup\n", __func__);
		return -EEXIST;
	}
#endif

	dev_dbg(chan->dev, "chan flags %u\n", setup->channel_flags);
	dev_dbg(chan->dev, "chan mask %llx\n", setup->vi_channel_mask);
	dev_dbg(chan->dev, "queue depth %u\n", setup->queue_depth);
	dev_dbg(chan->dev, "request size %u\n", setup->request_size);
	dev_dbg(chan->dev, "csi_stream_id %u\n", setup->csi_stream_id);
	dev_dbg(chan->dev, "vi unit id %u\n", vi_inst);
	dev_dbg(chan->dev, "vi2 chan mask %llx\n", setup->vi2_channel_mask);

	if ((vi_inst == VI_UNIT_VI &&
		setup->vi_channel_mask == CAPTURE_CHANNEL_INVALID_MASK) ||
		(vi_inst == VI_UNIT_VI2 &&
		setup->vi2_channel_mask == CAPTURE_CHANNEL_INVALID_MASK) ||
		setup->channel_flags == 0 ||
		setup->queue_depth == 0 ||
		setup->request_size == 0 ||
		setup->csi_stream_id == NVCSI_STREAM_INVALID_ID) {

		dev_warn(chan->dev, "%s: invalid setup parameters\n", __func__);
		return -EINVAL;
	}

	capture->queue_depth = setup->queue_depth;
	capture->request_size = setup->request_size;
	capture->request_buf_size = setup->request_size * setup->queue_depth;

	capture->stream_id = setup->csi_stream_id;
	capture->csi_port = setup->csi_port;
	capture->virtual_channel_id = setup->virtual_channel_id;

	err = vi_capture_setup_syncpts(chan, setup->channel_flags);
	if (err < 0) {
		dev_err(chan->dev, "failed to setup syncpts\n");
		goto syncpt_fail;
	}

	err = tegra_capture_ivc_register_control_cb(
			&vi_capture_ivc_control_callback,
			&transaction, capture);
	if (err < 0) {
		dev_err(chan->dev, "failed to register control callback\n");
		goto control_cb_fail;
	}

	memset(&control_desc, 0, sizeof(control_desc));
	control_desc.header.msg_id = CAPTURE_CHANNEL_SETUP_REQ;
	control_desc.header.transaction = transaction;

	/* Allocate memoryinfo ringbuffer */
	capture->requests_memoryinfo = dma_alloc_coherent(capture->rtcpu_dev,
		setup->queue_depth * sizeof(*capture->requests_memoryinfo),
		&capture->requests_memoryinfo_iova, GFP_KERNEL);

	if (!capture->requests_memoryinfo) {
		dev_err(chan->dev,
			"%s: memoryinfo ringbuffer alloc failed\n", __func__);
		goto memoryinfo_alloc_fail;
	}

	if (capture->unpins_list != NULL)
		dev_warn(chan->dev, "%s: unpins_list is not NULL\n", __func__);

	capture->unpins_list =
		vzalloc(setup->queue_depth * sizeof(*capture->unpins_list));

	if (!capture->unpins_list) {
			dev_err(chan->dev,
				"%s: channel_unpins alloc failed\n", __func__);
			goto unpin_alloc_fail;
	}

	config->requests_memoryinfo = capture->requests_memoryinfo_iova;
	config->request_memoryinfo_size =
			sizeof(struct capture_descriptor_memoryinfo);

	config->channel_flags = setup->channel_flags;
	config->vi_channel_mask = setup->vi_channel_mask;
	config->vi2_channel_mask = setup->vi2_channel_mask;
	config->slvsec_stream_main = setup->slvsec_stream_main;
	config->slvsec_stream_sub = setup->slvsec_stream_sub;

	config->vi_unit_id = vi_inst;

	config->csi_stream.stream_id = setup->csi_stream_id;
	config->csi_stream.csi_port = setup->csi_port;
	config->csi_stream.virtual_channel = setup->virtual_channel_id;

	config->queue_depth = setup->queue_depth;
	config->request_size = setup->request_size;
	config->requests = setup->iova;

	config->error_mask_correctable = setup->error_mask_correctable;
	config->error_mask_uncorrectable = setup->error_mask_uncorrectable;
	config->stop_on_error_notify_bits = setup->stop_on_error_notify_bits;

#ifdef HAVE_VI_GOS_TABLES
	dev_dbg(chan->dev, "%u GoS tables configured.\n",
		capture->num_gos_tables);
	for (i = 0; i < capture->num_gos_tables; i++) {
		config->vi_gos_tables[i] = (iova_t)capture->gos_tables[i];
		dev_dbg(chan->dev, "gos[%d] = 0x%08llx\n",
			i, (u64)capture->gos_tables[i]);
	}
	config->num_vi_gos_tables = capture->num_gos_tables;
#endif

	config->progress_sp = capture->progress_sp;
	config->embdata_sp = capture->embdata_sp;
	config->linetimer_sp = capture->linetimer_sp;

	err = vi_capture_ivc_send_control(chan, &control_desc,
			sizeof(control_desc), CAPTURE_CHANNEL_SETUP_RESP);
	if (err < 0)
		goto submit_fail;

	if (resp_msg->channel_setup_resp.result != CAPTURE_OK) {
		dev_err(chan->dev, "%s: control failed, errno %d", __func__,
			resp_msg->channel_setup_resp.result);
		err = -EINVAL;
		goto resp_fail;
	}

	capture->channel_id = resp_msg->channel_setup_resp.channel_id;

	if (vi_inst == VI_UNIT_VI)
		capture->vi_channel_mask =
				resp_msg->channel_setup_resp.vi_channel_mask;
	else if (vi_inst == VI_UNIT_VI2)
		capture->vi2_channel_mask =
				resp_msg->channel_setup_resp.vi_channel_mask;
	else {
		dev_err(chan->dev, "failed response for vi:%u\n", vi_inst);
		err = -EINVAL;
		goto resp_fail;
	}


	err = tegra_capture_ivc_notify_chan_id(capture->channel_id,
			transaction);
	if (err < 0) {
		dev_err(chan->dev, "failed to update control callback\n");
		goto cb_fail;
	}

	err = tegra_capture_ivc_register_capture_cb(
			&vi_capture_ivc_status_callback,
			capture->channel_id, capture);
	if (err < 0) {
		dev_err(chan->dev, "failed to register capture callback\n");
		goto cb_fail;
	}

	channels[setup->csi_stream_id][setup->virtual_channel_id] = chan;

	return 0;

cb_fail:
resp_fail:
submit_fail:
	vfree(capture->unpins_list);
	capture->unpins_list = NULL;
unpin_alloc_fail:
	/* Release memoryinfo ringbuffer */
	dma_free_coherent(capture->rtcpu_dev,
		capture->queue_depth *
		sizeof(struct capture_descriptor_memoryinfo),
		capture->requests_memoryinfo,
		capture->requests_memoryinfo_iova);
	capture->requests_memoryinfo = NULL;
memoryinfo_alloc_fail:
	tegra_capture_ivc_unregister_control_cb(transaction);
control_cb_fail:
	vi_capture_release_syncpts(chan);
syncpt_fail:
	return err;
}
EXPORT_SYMBOL_GPL(vi_capture_setup);

/**
 * @brief Get a VI channel handle for a given CSI stream and virtual channel ID.
 *
 * This function performs the following operation:
 * - Returns the VI channel handle from the global channels array if valid IDs.
 *
 * @param[in] stream_id           CSI stream identifier.
 *                                Valid range: [0, MAX_NVCSI_STREAM_IDS - 1].
 * @param[in] virtual_channel_id  Virtual channel identifier.
 *                                Valid range: [0, MAX_VIRTUAL_CHANNEL_PER_STREAM - 1].
 *
 * @retval tegra_vi_channel*  VI channel handle if found.
 * @retval NULL               If IDs are invalid or no channel exists.
 */
struct tegra_vi_channel *get_tegra_vi_channel(
	unsigned int stream_id,
	unsigned int virtual_channel_id)
{
	if (stream_id >= MAX_NVCSI_STREAM_IDS || virtual_channel_id >= MAX_VIRTUAL_CHANNEL_PER_STREAM)
		return NULL;

	return channels[stream_id][virtual_channel_id];
}

/**
 * @brief Reset a VI capture channel.
 *
 * This function performs the following operations:
 * - Validates input parameters and channel state.
 * - Locks the reset lock using @ref mutex_lock().
 * - If @ref CAPTURE_RESET_BARRIER_IND is defined, sends reset barrier
 *   indication using @ref tegra_capture_ivc_capture_submit().
 * - Sends channel reset request using @ref vi_capture_ivc_send_control().
 * - If @ref CAPTURE_RESET_BARRIER_IND is defined, checks reset response.
 * - Fast forwards syncpoints using @ref vi_capture_fastforward_syncpt().
 * - Unlocks the reset lock using @ref mutex_unlock().
 *
 * @param[in] chan         VI channel context.
 *                         Valid value: non-NULL.
 * @param[in] reset_flags  Channel reset flags.
 *                         Valid range: @ref CAPTURE_CHANNEL_RESET_FLAGS.
 *
 * @retval 0        Operation completed successfully.
 * @retval -ENODEV  Channel not initialized.
 * @retval -EAGAIN  Reset timed out.
 * @retval -EINVAL  Invalid response from RCE.
 * @retval (int)    Error codes from helper functions.
 */
int vi_capture_reset(
	struct tegra_vi_channel *chan,
	uint32_t reset_flags)
{
	struct vi_capture *capture = chan->capture_data;
	struct CAPTURE_CONTROL_MSG control_desc;
#ifdef CAPTURE_RESET_BARRIER_IND
	struct CAPTURE_MSG capture_desc;
#endif
	struct CAPTURE_CONTROL_MSG *resp_msg = &capture->control_resp_msg;
	int err = 0;

	nv_camera_log(chan->ndev,
		__arch_counter_get_cntvct(),
		NVHOST_CAMERA_VI_CAPTURE_RESET);

	if (capture == NULL) {
		dev_err(chan->dev,
			 "%s: vi capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (capture->channel_id == CAPTURE_CHANNEL_INVALID_ID) {
		dev_err(chan->dev,
			"%s: setup channel first\n", __func__);
		return -ENODEV;
	}

	mutex_lock(&capture->reset_lock);

#ifdef CAPTURE_RESET_BARRIER_IND
	memset(&capture_desc, 0, sizeof(capture_desc));
	capture_desc.header.msg_id = CAPTURE_RESET_BARRIER_IND;
	capture_desc.header.channel_id = capture->channel_id;
	err = tegra_capture_ivc_capture_submit(&capture_desc,
			sizeof(capture_desc));
	if (err < 0) {
		dev_err(chan->dev, "%s:IVC capture submit failed\n", __func__);
		goto submit_fail;
	}
#endif

	memset(&control_desc, 0, sizeof(control_desc));
	control_desc.header.msg_id = CAPTURE_CHANNEL_RESET_REQ;
	control_desc.header.channel_id = capture->channel_id;
	control_desc.channel_reset_req.reset_flags = reset_flags;

	err = vi_capture_ivc_send_control(chan, &control_desc,
			sizeof(control_desc), CAPTURE_CHANNEL_RESET_RESP);
	if (err < 0)
		goto submit_fail;

#ifdef CAPTURE_RESET_BARRIER_IND
	if (resp_msg->channel_reset_resp.result == CAPTURE_ERROR_TIMEOUT) {
		dev_dbg(chan->dev, "%s:reset timeout\n", __func__);
		err = -EAGAIN;
		goto submit_fail;
	}
#endif

	if (resp_msg->channel_reset_resp.result != CAPTURE_OK) {
		dev_err(chan->dev, "%s: control failed, errno %d", __func__,
			resp_msg->channel_reset_resp.result);
		err = -EINVAL;
	}

	vi_capture_fastforward_syncpt(chan);

submit_fail:
	mutex_unlock(&capture->reset_lock);
	return err;
}
EXPORT_SYMBOL_GPL(vi_capture_reset);

/**
 * @brief Release a VI capture channel.
 *
 * This function performs the following operations:
 * - Validates input parameters and channel state.
 * - Sends channel release request using @ref vi_capture_ivc_send_control().
 * - If returns error, reboots the RTCPU using @ref tegra_camrtc_reboot().
 * - Frees memory info ringbuffer using @ref dma_free_coherent() if allocated.
 * - Unregisters callbacks using @ref tegra_capture_ivc_unregister_capture_cb()
 *   and @ref tegra_capture_ivc_unregister_control_cb().
 * - Completes any pending capture responses using @ref complete().
 * - Releases syncpoints using @ref vi_capture_release_syncpts().
 * - Clears channel state and removes from global channels array.
 * - Releases progress status notifier using @ref capture_common_release_progress_status_notifier()
 *   if set.
 *
 * @param[in] chan         VI channel context.
 *                         Valid value: non-NULL.
 * @param[in] reset_flags  Channel reset flags.
 *                         Valid range: @ref CAPTURE_CHANNEL_RESET_FLAGS.
 *
 * @retval 0        Operation completed successfully.
 * @retval -ENODEV  Channel not initialized.
 * @retval -EIO     @ref vi_capture_ivc_send_control() failed or response is not CAPTURE_OK.
 * @retval (int)    Error codes from @ref tegra_capture_ivc_unregister_capture_cb()
 *                  and @ref tegra_capture_ivc_unregister_control_cb().
 */
int vi_capture_release(
	struct tegra_vi_channel *chan,
	uint32_t reset_flags)
{
	struct vi_capture *capture;
	struct CAPTURE_CONTROL_MSG control_desc;
	struct CAPTURE_CONTROL_MSG *resp_msg;
	int err = 0;
	int ret = 0;
	int i = 0;

	if (unlikely(chan == NULL)) {
		pr_err("%s: vi channel pointer is NULL\n", __func__);
		return -ENODEV;
	}

	nv_camera_log(chan->ndev,
		__arch_counter_get_cntvct(),
		NVHOST_CAMERA_VI_CAPTURE_RELEASE);

	capture = chan->capture_data;
	if (unlikely(capture == NULL)) {
		dev_err(chan->dev, "%s: vi capture uninitialized\n", __func__);
		return -ENODEV;
	}

	resp_msg = &capture->control_resp_msg;

	if (capture->channel_id == CAPTURE_CHANNEL_INVALID_ID) {
		dev_err(chan->dev,
			"%s: setup channel first\n", __func__);
		return -ENODEV;

	}

	memset(&control_desc, 0, sizeof(control_desc));
	control_desc.header.msg_id = CAPTURE_CHANNEL_RELEASE_REQ;
	control_desc.header.channel_id = capture->channel_id;
	control_desc.channel_release_req.reset_flags = reset_flags;

	err = vi_capture_ivc_send_control(chan, &control_desc,
			sizeof(control_desc), CAPTURE_CHANNEL_RELEASE_RESP);
	if (err < 0) {
		dev_warn(chan->dev,
				"%s: release channel IVC failed, reboot RTCPU to recover\n", __func__);
		tegra_camrtc_reboot(chan->rtcpu_dev);

		err = -EIO;
	} else if (resp_msg->channel_release_resp.result != CAPTURE_OK) {
		dev_err(chan->dev, "%s: control failed, errno %d", __func__,
			resp_msg->channel_release_resp.result);
		err = -EIO;
	}

	if (capture->requests_memoryinfo) {
		/* Release memoryinfo ringbuffer */
		dma_free_coherent(capture->rtcpu_dev,
			capture->queue_depth * sizeof(struct capture_descriptor_memoryinfo),
			capture->requests_memoryinfo, capture->requests_memoryinfo_iova);
		capture->requests_memoryinfo = NULL;
	}

	ret = tegra_capture_ivc_unregister_capture_cb(capture->channel_id);
	if (ret < 0 && err == 0) {
		dev_err(chan->dev,
			"failed to unregister capture callback\n");
		err = ret;
	}

	ret = tegra_capture_ivc_unregister_control_cb(capture->channel_id);
	if (ret < 0 && err == 0) {
		dev_err(chan->dev,
			"failed to unregister control callback\n");
		err = ret;
	}

	for (i = 0; i < capture->queue_depth; i++)
		complete(&capture->capture_resp);

	vi_capture_release_syncpts(chan);

	if (capture->stream_id < MAX_NVCSI_STREAM_IDS &&
	    capture->virtual_channel_id < MAX_VIRTUAL_CHANNEL_PER_STREAM) {
		channels[capture->stream_id][capture->virtual_channel_id] = NULL;
	}

	capture->channel_id = CAPTURE_CHANNEL_INVALID_ID;
	capture->stream_id = NVCSI_STREAM_INVALID_ID;
	capture->csi_port = NVCSI_PORT_UNSPECIFIED;
	capture->virtual_channel_id = NVCSI_STREAM_INVALID_TPG_VC_ID;

	if (capture->is_progress_status_notifier_set) {
		capture_common_release_progress_status_notifier(
			&capture->progress_status_notifier);
	}

	return err;
}
EXPORT_SYMBOL_GPL(vi_capture_release);

/**
 * @brief Send a control message to RCE through IVC for a VI channel.
 *
 * This function performs the following operations:
 * - Sets the channel ID in the message header.
 * - Determines the expected response message ID based on request type.
 * - For PHY stream open requests, checks channel stream is not already opened
 *   and copies stream ID and CSI port to channel context.
 * - For PHY stream close requests, checks channel stream is opened.
 * - For TPG start requests, copies virtual channel ID to channel context.
 * - Sends the control message using @ref vi_capture_ivc_send_control().
 * - Updates stream state for PHY stream operations.
 *
 * @param[in] chan      VI channel context.
 *                      Valid value: non-NULL.
 * @param[in] msg_cpy   Control message to send.
 *                      Valid value: non-NULL.
 * @param[in] size      Size of message in bytes.
 *                      Valid value: non-zero.
 *
 * @retval 0        Operation completed successfully.
 * @retval -EINVAL  Invalid message ID or response ID.
 * @retval (int)    Error codes from @ref vi_capture_ivc_send_control().
 */
static int vi_capture_control_send_message(
	struct tegra_vi_channel *chan,
	struct CAPTURE_CONTROL_MSG *msg_cpy,
	size_t size)
{
	int err = 0;
	struct vi_capture *capture = chan->capture_data;
	uint32_t resp_id;

	struct CAPTURE_MSG_HEADER *header = &msg_cpy->header;
	header->channel_id = capture->channel_id;

	switch (header->msg_id) {
	case CAPTURE_COMPAND_CONFIG_REQ:
		resp_id = CAPTURE_COMPAND_CONFIG_RESP;
		break;
	case CAPTURE_PDAF_CONFIG_REQ:
		resp_id = CAPTURE_PDAF_CONFIG_RESP;
		break;
	case CAPTURE_SYNCGEN_ENABLE_REQ:
		resp_id = CAPTURE_SYNCGEN_ENABLE_RESP;
		break;
	case CAPTURE_SYNCGEN_DISABLE_REQ:
		resp_id = CAPTURE_SYNCGEN_DISABLE_RESP;
		break;
	case CAPTURE_PHY_STREAM_OPEN_REQ:
		if (chan->is_stream_opened) {
			dev_dbg(chan->dev,
				"%s: NVCSI stream is already opened for this VI channel",
				__func__);
			return 0;
		}
		resp_id = CAPTURE_PHY_STREAM_OPEN_RESP;
		capture->stream_id = msg_cpy->phy_stream_open_req.stream_id;
		capture->csi_port = msg_cpy->phy_stream_open_req.csi_port;
		break;
	case CAPTURE_PHY_STREAM_CLOSE_REQ:
		if (!chan->is_stream_opened) {
			dev_dbg(chan->dev,
				"%s: NVCSI stream is already closed for this VI channel",
				__func__);
			return 0;
		}
		resp_id = CAPTURE_PHY_STREAM_CLOSE_RESP;
		break;
	case CAPTURE_PHY_STREAM_DUMPREGS_REQ:
		resp_id = CAPTURE_PHY_STREAM_DUMPREGS_RESP;
		break;
	case CAPTURE_CSI_STREAM_SET_CONFIG_REQ:
		resp_id = CAPTURE_CSI_STREAM_SET_CONFIG_RESP;
		break;
	case CAPTURE_CSI_STREAM_SET_PARAM_REQ:
		resp_id = CAPTURE_CSI_STREAM_SET_PARAM_RESP;
		break;
	case CAPTURE_CSI_STREAM_TPG_SET_CONFIG_REQ:
		resp_id = CAPTURE_CSI_STREAM_TPG_SET_CONFIG_RESP;
		break;
	case CAPTURE_CSI_STREAM_TPG_START_REQ:
		resp_id = CAPTURE_CSI_STREAM_TPG_START_RESP;
		capture->virtual_channel_id =
			msg_cpy->csi_stream_tpg_start_req.virtual_channel_id;
		break;
	case CAPTURE_CSI_STREAM_TPG_START_RATE_REQ:
		resp_id = CAPTURE_CSI_STREAM_TPG_START_RATE_RESP;
		capture->virtual_channel_id = msg_cpy->
			csi_stream_tpg_start_rate_req.virtual_channel_id;
		break;
	case CAPTURE_CSI_STREAM_TPG_APPLY_GAIN_REQ:
		resp_id = CAPTURE_CSI_STREAM_TPG_APPLY_GAIN_RESP;
		break;
	case CAPTURE_CSI_STREAM_TPG_STOP_REQ:
		resp_id = CAPTURE_CSI_STREAM_TPG_STOP_RESP;
		break;
	case CAPTURE_CHANNEL_EI_REQ:
		resp_id = CAPTURE_CHANNEL_EI_RESP;
		break;
	case CAPTURE_HSM_CHANSEL_ERROR_MASK_REQ:
		resp_id = CAPTURE_HSM_CHANSEL_ERROR_MASK_RESP;
		break;
	default:
		dev_err(chan->dev, "%s: unknown capture control req 0x%x",
			__func__, header->msg_id);
		return -EINVAL;
	}

	err = vi_capture_ivc_send_control(chan, msg_cpy, size, resp_id);
	if (err < 0) {
		dev_err(chan->dev, "%s: failed to send IVC control message", __func__);
		return err;
	}

	if (header->msg_id == CAPTURE_PHY_STREAM_OPEN_REQ)
		chan->is_stream_opened = true;
	else if (header->msg_id == CAPTURE_PHY_STREAM_CLOSE_REQ)
		chan->is_stream_opened = false;

	return err;
}

/**
 * @brief Disable the VI channel's NVCSI TPG stream in RCE.
 *
 * This function performs the following operations:
 * - Prepares TPG stop request message.
 * - Sends request to RCE using @ref vi_capture_ivc_send_control().
 * - Validates response from RCE.
 *
 * @param[in] chan  VI channel context.
 *                  Valid value: non-NULL.
 *
 * @retval 0        Operation completed successfully.
 * @retval -EINVAL  Invalid response from RCE.
 * @retval (int)    Error codes from @ref vi_capture_ivc_send_control().
 */
static int csi_stream_tpg_disable(
	struct tegra_vi_channel *chan)
{
	struct vi_capture *capture = chan->capture_data;
	struct CAPTURE_CONTROL_MSG control_desc;
	struct CAPTURE_CONTROL_MSG *resp_msg = &capture->control_resp_msg;
	int err = 0;

	memset(&control_desc, 0, sizeof(control_desc));
	control_desc.header.msg_id = CAPTURE_CSI_STREAM_TPG_STOP_REQ;
	control_desc.header.channel_id = capture->channel_id;
	control_desc.csi_stream_tpg_stop_req.stream_id = capture->stream_id;
	control_desc.csi_stream_tpg_stop_req.virtual_channel_id =
		capture->virtual_channel_id;

	err = vi_capture_ivc_send_control(chan, &control_desc,
		sizeof(control_desc), CAPTURE_CSI_STREAM_TPG_STOP_RESP);
	if ((err < 0) ||
			(resp_msg->csi_stream_tpg_stop_resp.result
				!= CAPTURE_OK))
		return err;

	return 0;
}

/**
 * @brief Close the VI channel's NVCSI stream in RCE.
 *
 * This function performs the following operations:
 * - Prepares stream close request message.
 * - Sends request to RCE using @ref vi_capture_control_send_message().
 * - Validates response from RCE.
 *
 * @param[in] chan  VI channel context.
 *                  Valid value: non-NULL.
 *
 * @retval 0        Operation completed successfully.
 * @retval -EINVAL  Invalid response from RCE.
 * @retval (int)    Error codes from @ref vi_capture_control_send_message().
 */
static int csi_stream_close(
	struct tegra_vi_channel *chan)
{
	struct vi_capture *capture = chan->capture_data;
	struct CAPTURE_CONTROL_MSG control_desc;
	struct CAPTURE_CONTROL_MSG *resp_msg = &capture->control_resp_msg;
	int err = 0;

	memset(&control_desc, 0, sizeof(control_desc));
	control_desc.header.msg_id = CAPTURE_PHY_STREAM_CLOSE_REQ;
	control_desc.header.channel_id = capture->channel_id;
	control_desc.phy_stream_close_req.phy_type = NVPHY_TYPE_CSI;
	control_desc.phy_stream_close_req.stream_id = capture->stream_id;
	control_desc.phy_stream_close_req.csi_port = capture->csi_port;

	err = vi_capture_control_send_message(chan, &control_desc,
		sizeof(control_desc));
	if ((err < 0) ||
			(resp_msg->phy_stream_close_resp.result != CAPTURE_OK))
		return err;

	return 0;
}

/**
 * @brief Release the VI channel's NVCSI stream resources.
 *
 * This function performs the following operations:
 * - If stream ID is invalid, returns success.
 * - If TPG virtual channel is active, disables it using @ref csi_stream_tpg_disable().
 * - If stream is open, closes it using @ref csi_stream_close().
 *
 * @param[in] chan  VI channel context.
 *                  Valid value: non-NULL.
 *
 * @retval 0        Operation completed successfully.
 * @retval (int)    Error codes from @ref csi_stream_tpg_disable() or @ref csi_stream_close().
 */
int csi_stream_release(
	struct tegra_vi_channel *chan)
{
	struct vi_capture *capture = chan->capture_data;
	int err = 0;

	if (capture->stream_id == NVCSI_STREAM_INVALID_ID)
		return 0;

	if (capture->virtual_channel_id != NVCSI_STREAM_INVALID_TPG_VC_ID) {
		err = csi_stream_tpg_disable(chan);
		if (err < 0) {
			dev_err(chan->dev,
				"%s: failed to disable nvcsi tpg on stream %u virtual channel %u\n",
				__func__, capture->stream_id,
				capture->virtual_channel_id);
			return err;
		}
	}

	if (chan->is_stream_opened) {
		err = csi_stream_close(chan);
		if (err < 0)
			dev_err(chan->dev,
				"%s: failed to close nvcsi stream %u\n",
				__func__, capture->stream_id);
	}

	return err;
}

/**
 * @brief Process a VI capture control message from userspace.
 *
 * This function performs the following operations:
 * - Validates input parameters.
 * - Copies message from userspace using @ref kzalloc() and @ref copy_from_user().
 * - Sends control message using @ref vi_capture_control_send_message().
 * - Copies response back to userspace using @ref copy_to_user().
 *
 * @param[in] chan  VI channel context.
 *                  Valid value: non-NULL.
 * @param[in] msg   Control message from userspace.
 *                  Valid value: non-NULL with valid pointers.
 *
 * @retval 0        Operation completed successfully.
 * @retval -ENODEV  Channel not initialized.
 * @retval -EINVAL  Invalid message parameters.
 * @retval -ENOMEM  Failed to allocate message copy buffer from @ref kzalloc().
 * @retval (int)    Error codes from @ref vi_capture_control_send_message(), @ref copy_to_user(),
 *                  or @ref copy_from_user().
 */
int vi_capture_control_message_from_user(
	struct tegra_vi_channel *chan,
	struct vi_capture_control_msg *msg)
{
	struct vi_capture *capture;
	const void __user *msg_ptr;
	void __user *response;
	void *msg_cpy;
	struct CAPTURE_CONTROL_MSG *resp_msg;
	int err = 0;
	size_t copy_size = 0;

	if (chan == NULL) {
		dev_err(NULL, "%s: NULL VI channel received\n", __func__);
		return -ENODEV;
	}

	if (msg == NULL) {
		dev_err(NULL, "%s: NULL vi capture control message received\n", __func__);
		return -EINVAL;
	}

	nv_camera_log(chan->ndev,
		__arch_counter_get_cntvct(),
		NVHOST_CAMERA_VI_CAPTURE_SET_CONFIG);

	capture = chan->capture_data;

	if (capture == NULL) {
		dev_err(chan->dev,
			 "%s: vi capture uninitialized\n", __func__);
		return -ENODEV;
	}

	resp_msg = &capture->control_resp_msg;

	if (msg->ptr == 0ull || msg->response == 0ull || msg->size == 0)
		return -EINVAL;

	msg_ptr = (const void __user *)(uintptr_t)msg->ptr;
	response = (void __user *)(uintptr_t)msg->response;

	msg_cpy = kzalloc(sizeof(struct CAPTURE_CONTROL_MSG), GFP_KERNEL);
	if (unlikely(msg_cpy == NULL))
		return -ENOMEM;

	copy_size = min_t(size_t, msg->size, sizeof(struct CAPTURE_CONTROL_MSG));

	err = copy_from_user(msg_cpy, msg_ptr, copy_size) ? -EFAULT : 0;
	if (err < 0)
		goto fail;


	err = vi_capture_control_send_message(chan, msg_cpy, copy_size);
	if (err < 0)
		goto fail;

	err = copy_to_user(response, resp_msg,
		sizeof(*resp_msg)) ? -EFAULT : 0;
	if (err < 0)
		goto fail;

fail:
	kfree(msg_cpy);
	return err;
}
EXPORT_SYMBOL_GPL(vi_capture_control_message_from_user);

/**
 * @brief Process a VI capture control message from kernel space.
 *
 * This function performs the following operations:
 * - Validates input parameters.
 * - Copies message from kernel buffer using @ref kzalloc() and @ref memcpy().
 * - Sends control message using @ref vi_capture_control_send_message().
 * - Copies response to output buffer using @ref memcpy().
 * - Frees message copy buffer using @ref kfree().
 *
 * @param[in] chan  VI channel context.
 *                  Valid value: non-NULL.
 * @param[in] msg   Control message from kernel.
 *                  Valid value: non-NULL with valid pointers.
 *
 * @retval 0        Operation completed successfully.
 * @retval -ENODEV  Channel not initialized.
 * @retval -EINVAL  Invalid message parameters.
 * @retval -ENOMEM  Failed to allocate message copy buffer from @ref kzalloc().
 * @retval (int)    Error codes from @ref vi_capture_control_send_message().
 */
int vi_capture_control_message(
	struct tegra_vi_channel *chan,
	struct vi_capture_control_msg *msg)
{
	struct vi_capture *capture;
	void *msg_cpy;
	struct CAPTURE_CONTROL_MSG *resp_msg;
	int err = 0;

	if (chan == NULL) {
		dev_err(NULL,"%s: NULL VI channel received\n", __func__);
		return -ENODEV;
	}

	nv_camera_log(chan->ndev,
		__arch_counter_get_cntvct(),
		NVHOST_CAMERA_VI_CAPTURE_SET_CONFIG);

	capture = chan->capture_data;

	if (capture == NULL) {
		dev_err(chan->dev,
			 "%s: vi capture uninitialized\n", __func__);
		return -ENODEV;
	}

	resp_msg = &capture->control_resp_msg;
	if (msg->ptr == 0ull || msg->response == 0ull || msg->size == 0)
		return -EINVAL;

	msg_cpy = kzalloc(msg->size, GFP_KERNEL);
	if (unlikely(msg_cpy == NULL))
		return -ENOMEM;

	memcpy(msg_cpy, (const void *)(uintptr_t)msg->ptr,
						msg->size);

	err = vi_capture_control_send_message(chan, msg_cpy, msg->size);
	if (err < 0)
		goto fail;

	memcpy((void *)(uintptr_t)msg->response, resp_msg,
						sizeof(*resp_msg));

fail:
	kfree(msg_cpy);
	return err;
}

/**
 * @brief Get information about a VI capture channel.
 *
 * This function performs the following operations:
 * - Validates input parameters and channel state.
 * - Retrieves syncpoint IDs and values using @ref vi_capture_read_syncpt().
 * - Returns channel configuration information including:
 *   - Progress, embedded data and line timer syncpoints.
 *   - Hardware channel ID.
 *   - VI channel masks.
 *
 * @param[in] chan   VI channel context.
 *                   Valid value: non-NULL.
 * @param[out] info  Capture channel information structure.
 *                   Valid value: non-NULL.
 *
 * @retval 0        Operation completed successfully.
 * @retval -ENODEV  Channel not initialized.
 * @retval -EINVAL  Invalid info parameter.
 * @retval (int)    Error codes from @ref vi_capture_read_syncpt().
 */
int vi_capture_get_info(
	struct tegra_vi_channel *chan,
	struct vi_capture_info *info)
{
	struct vi_capture *capture = chan->capture_data;
	int err;

	nv_camera_log(chan->ndev,
		__arch_counter_get_cntvct(),
		NVHOST_CAMERA_VI_CAPTURE_GET_INFO);

	if (capture == NULL) {
		dev_err(chan->dev,
			 "%s: vi capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (capture->channel_id == CAPTURE_CHANNEL_INVALID_ID) {
		dev_err(chan->dev,
			"%s: setup channel first\n", __func__);
		return -ENODEV;
	}

	if (info == NULL)
		return -EINVAL;

	info->syncpts.progress_syncpt = capture->progress_sp.id;
	info->syncpts.emb_data_syncpt = capture->embdata_sp.id;
	info->syncpts.line_timer_syncpt = capture->linetimer_sp.id;

	err = vi_capture_read_syncpt(chan, &capture->progress_sp,
			&info->syncpts.progress_syncpt_val);
	if (err < 0)
		return err;
	err = vi_capture_read_syncpt(chan, &capture->embdata_sp,
			&info->syncpts.emb_data_syncpt_val);
	if (err < 0)
		return err;
	err = vi_capture_read_syncpt(chan, &capture->linetimer_sp,
			&info->syncpts.line_timer_syncpt_val);
	if (err < 0)
		return err;

	info->hw_channel_id = capture->channel_id;
	info->vi_channel_mask = capture->vi_channel_mask;
	info->vi2_channel_mask = capture->vi2_channel_mask;

	return 0;
}
EXPORT_SYMBOL_GPL(vi_capture_get_info);

/**
 * @brief Calculate number of progress syncpoints needed for a capture request.
 *
 * This function performs the following operations:
 * - Gets capture descriptor for the given buffer index using @ref check_mul_overflow().
 * - Calculates required progress syncpoints based on:
 *   - Minimum of 2 for PXL_SOF and PXL_EOF.
 *   - Additional points for flush operations if enabled.
 *   - Adjusts count if HEIGHT is divisible by TRIPLINE.
 * - Handles overflow conditions in calculations.
 *
 * @param[in] chan  VI channel context.
 *                  Valid value: non-NULL.
 * @param[in] req   Capture request parameters.
 *                  Valid value: non-NULL.
 *
 * @retval (uint32_t)  Number of progress syncpoints needed.
 */
static uint32_t vi_capture_get_num_progress(
	struct tegra_vi_channel *chan,
	struct vi_capture_req *req)
{
	struct vi_capture *capture = chan->capture_data;

	const uint16_t minProgress = 2U;
	uint16_t numProgress = minProgress;

	struct capture_descriptor *desc;
	struct vi_channel_config *config;

	unsigned int mul_value = 0;

	if (check_mul_overflow(req->buffer_index, capture->request_size, &mul_value)) {
		dev_err(chan->dev,
			"%s:capture descriptor offset failed due to an overflow\n", __func__);
		return minProgress;
	}

	desc = (struct capture_descriptor *)(capture->requests.va + mul_value);
	config = &desc->ch_cfg;

	/* Minimum of two progress fences for PXL_SOF and PXL_EOF */
	if (config->flush_enable == 0x1UL)
	{
		if (config->flush_periodic == 0x1UL)
		{
			if (config->flush_first != 0U)
			{
				numProgress++;
			}
			numProgress += (config->frame.frame_y - config->flush_first) /
				config->flush;
		}
		else
		{
			numProgress++;
		}
		/**
		 * if (HEIGHT % TRIPLINE == 0) then TRIPLINE is reached at the same
		 * time as PXL_EOF. EOF occurs on the cropped last line, hence EOF and
		 * EOL are simultaneous. In this case, final PXL_EOF tag have NLINES bit
		 * set in its payload, while there is no NLINE_DONE tag. Therefore,
		 * number of progress fences should be decreased by one.
		 */
		if (((config->frame.frame_y - config->flush_first) % config->flush) == 0U)
		{
			if (numProgress < minProgress) {
				dev_err(chan->dev,
						"%s:numProgress is less than the minimum value\n",
						__func__);
				numProgress = minProgress;
			} else {
				numProgress--;
			}
		}
	}
	return (uint32_t)numProgress;
}

/**
 * @brief Submit a capture request to a VI channel.
 *
 * This function performs the following operations:
 * - Validates input parameters and channel state.
 * - Locks the reset lock using @ref mutex_lock().
 * - Prepares capture request message.
 * - Logs the capture request using @ref nv_camera_log_vi_submit().
 * - Submits request using @ref tegra_capture_ivc_capture_submit().
 * - Calculates progress points using @ref vi_capture_get_num_progress().
 * - Updates progress syncpoint threshold using @ref check_add_overflow().
 * - Unlocks the reset lock using @ref mutex_unlock().
 *
 * @param[in] chan  VI channel context.
 *                  Valid value: non-NULL.
 * @param[in] req   Capture request parameters.
 *                  Valid value: non-NULL.
 *
 * @retval 0        Operation completed successfully.
 * @retval -ENODEV  Channel not initialized.
 * @retval -EINVAL  Invalid request parameter.
 * @retval (int)    Error codes from @ref tegra_capture_ivc_capture_submit().
 */
int vi_capture_request(
	struct tegra_vi_channel *chan,
	struct vi_capture_req *req)
{
	struct vi_capture *capture = chan->capture_data;
	struct CAPTURE_MSG capture_desc;
	int err = 0;
	uint32_t sum_value = 0;

	nv_camera_log(chan->ndev,
		__arch_counter_get_cntvct(),
		NVHOST_CAMERA_VI_CAPTURE_REQUEST);

	if (capture == NULL) {
		dev_err(chan->dev,
			"%s: vi capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (capture->channel_id == CAPTURE_CHANNEL_INVALID_ID) {
		dev_err(chan->dev,
			"%s: setup channel first\n", __func__);
		return -ENODEV;
	}

	if (req == NULL) {
		dev_err(chan->dev,
			"%s: Invalid req\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&capture->reset_lock);

	memset(&capture_desc, 0, sizeof(capture_desc));
	capture_desc.header.msg_id = CAPTURE_REQUEST_REQ;
	capture_desc.header.channel_id = capture->channel_id;
	capture_desc.capture_request_req.buffer_index = req->buffer_index;

	nv_camera_log_vi_submit(
			chan->ndev,
			capture->progress_sp.id,
			capture->progress_sp.threshold,
			capture_desc.header.channel_id,
			__arch_counter_get_cntvct());

	dev_dbg(chan->dev, "%s: sending chan_id %u msg_id %u buf:%u\n",
			__func__, capture_desc.header.channel_id,
			capture_desc.header.msg_id, req->buffer_index);
	err = tegra_capture_ivc_capture_submit(&capture_desc,
			sizeof(capture_desc));
	if (err < 0) {
		mutex_unlock(&capture->reset_lock);
		dev_err(chan->dev, "IVC capture submit failed\n");
		return err;
	}

	// Progress syncpoints + 1 for status syncpoint
	if (check_add_overflow(vi_capture_get_num_progress(chan, req), 1U, &sum_value)) {
		dev_err(chan->dev, "%s: check_sub failed due to an overflow\n", __func__);
	} else if (check_add_overflow(capture->progress_sp.threshold, sum_value,
				&capture->progress_sp.threshold)) {
		dev_err(chan->dev, "%s: procress_sp failed due to an overflow\n", __func__);
	}

	mutex_unlock(&capture->reset_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(vi_capture_request);

/**
 * @brief Wait for capture status completion on a VI channel.
 *
 * This function performs the following operations:
 * - Validates input parameters and channel state.
 * - Waits for capture completion with specified timeout using
 *   @ref wait_for_completion_interruptible() if selected timtout is negative,
 *   or @ref wait_for_completion_timeout() otherwise.
 * - If the response is not received within the timeout,
 *   log the RCE snapshot by calling @ref rtcpu_trace_panic_callback().
 * - Convert timeout value to jiffies using @ref msecs_to_jiffies().
 *
 * @param[in] chan         VI channel context.
 *                         Valid value: non-NULL.
 * @param[in] timeout_ms   Timeout in milliseconds.
 *                         Valid range: [-1, INT_MAX].
 *                         -1 means wait forever.
 *
 * @retval 0           Operation completed successfully.
 * @retval -ENODEV     Channel not initialized.
 * @retval -ETIMEDOUT  Wait timed out.
 * @retval (int)       Error codes from @ref wait_for_completion_interruptible()
 *                     or @ref wait_for_completion_timeout().
 */
int vi_capture_status(
	struct tegra_vi_channel *chan,
	int32_t timeout_ms)
{
	struct vi_capture *capture = chan->capture_data;
	int ret = 0;

	nv_camera_log(chan->ndev,
		__arch_counter_get_cntvct(),
		NVHOST_CAMERA_VI_CAPTURE_STATUS);

	if (capture == NULL) {
		dev_err(chan->dev,
			 "%s: vi capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (capture->channel_id == CAPTURE_CHANNEL_INVALID_ID) {
		dev_err(chan->dev,
			"%s: setup channel first\n", __func__);
		return -ENODEV;
	}

	dev_dbg(chan->dev, "%s: waiting for status, timeout:%d ms\n",
		__func__, timeout_ms);

	/* negative timeout means wait forever */
	if (timeout_ms < 0) {
		ret = wait_for_completion_interruptible(&capture->capture_resp);
		if (ret == -ERESTARTSYS) {
			dev_dbg(chan->dev,
				"capture status interrupted\n");
			rtcpu_trace_panic_callback(capture->rtcpu_dev);
			return -ETIMEDOUT;
		}
	} else {
		ret = wait_for_completion_timeout(
				&capture->capture_resp,
				msecs_to_jiffies(timeout_ms));
		if (ret == 0) {
			dev_dbg(chan->dev,
				"capture status timed out\n");
			rtcpu_trace_panic_callback(capture->rtcpu_dev);
			return -ETIMEDOUT;
		}
	}

	if (ret < 0) {
		dev_err(chan->dev,
			"wait for capture status failed\n");
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(vi_capture_status);

/**
 * @brief Set up progress status notification for a VI channel.
 *
 * This function performs the following operations:
 * - Validates input parameters and channel state.
 * - Verifies buffer depth is sufficient for queue depth.
 * - Sets up progress status notifier using @ref capture_common_setup_progress_status_notifier().
 * - Updates channel state for progress status notification.
 *
 * @param[in] chan  VI channel context.
 *                  Valid value: non-NULL.
 * @param[in] req   Progress status request parameters.
 *                  Valid value: non-NULL with valid memory parameters.
 *
 * @retval 0        Operation completed successfully.
 * @retval -EINVAL  Invalid request parameters.
 * @retval -ENODEV  Channel not initialized.
 * @retval -EFAULT  Error from @ref capture_common_setup_progress_status_notifier().
 */
int vi_capture_set_progress_status_notifier(
	struct tegra_vi_channel *chan,
	struct vi_capture_progress_status_req *req)
{
	int err = 0;
	struct vi_capture *capture = chan->capture_data;

	nv_camera_log(chan->ndev,
		__arch_counter_get_cntvct(),
		NVHOST_CAMERA_VI_CAPTURE_SET_PROGRESS_STATUS);

	if (req->mem == 0 ||
		req->buffer_depth == 0) {
		dev_err(chan->dev,
				"%s: request buffer is invalid\n", __func__);
		return -EINVAL;
	}

	if (capture == NULL) {
		dev_err(chan->dev,
				"%s: vi capture uninitialized\n", __func__);
		return -ENODEV;
	}

	if (req->buffer_depth < capture->queue_depth) {
		dev_err(chan->dev,
			"Progress status buffer is smaller than queue depth");
		return -EINVAL;
	}

	/* Setup the progress status buffer */
	err = capture_common_setup_progress_status_notifier(
		&capture->progress_status_notifier,
		req->mem,
		sizeof(uint32_t) * req->buffer_depth,
		req->mem_offset);

	if (err < 0) {
		dev_err(chan->dev, "%s: memory setup failed\n", __func__);
		return -EFAULT;
	}

	dev_dbg(chan->dev, "mem offset %u\n", req->mem_offset);
	dev_dbg(chan->dev, "buffer depth %u\n", req->buffer_depth);

	capture->progress_status_buffer_depth = req->buffer_depth;
	capture->is_progress_status_notifier_set = true;
	return err;
}
EXPORT_SYMBOL_GPL(vi_capture_set_progress_status_notifier);

/**
 * @brief Read CSI-to-VI mapping table from device tree.
 *
 * This function performs the following operations:
 * - Reads mapping table size using @ref of_property_read_u32().
 * - Ensures mapping table size is within valid range.
 * - Validates mapping element names using @ref of_property_count_strings().
 * - Checks element order using @ref of_property_match_string().
 * - Reads and validates CSI stream ID and VI unit ID pairs using
 *   @ref of_property_read_u32_index().
 * - Checks each CSI stream ID is unique and within valid range.
 * - Checks each VI unit ID is within valid range.
 * - Populates VI instance mapping table.
 *
 * @param[in] pdev  Platform device pointer.
 *                  Valid value: non-NULL.
 *
 * @retval 0        Operation completed successfully.
 * @retval -EINVAL  Invalid mapping parameters or device tree entries.
 * @retval (int)    Error codes from @ref of_property_read_u32_index().
 */
static int csi_vi_get_mapping_table(struct platform_device *pdev)
{
	uint32_t index = 0;
	int err = 0;
	struct device *dev = &pdev->dev;
	struct tegra_capture_vi_data *info = platform_get_drvdata(pdev);

	int nmap_elems;
	uint32_t map_table_size;
	uint32_t *map_table = info->vi_instance_table;

	const struct device_node *np = dev->of_node;

	(void)of_property_read_u32(np,
			"nvidia,vi-mapping-size", &map_table_size);
	if (map_table_size > MAX_NVCSI_STREAM_IDS) {
		dev_err(dev, "invalid mapping table size %u\n", map_table_size);
		return -EINVAL;
	}
	info->num_csi_vi_maps = map_table_size;

	nmap_elems = of_property_count_strings(np, "nvidia,vi-mapping-names");
	if (nmap_elems != ARRAY_SIZE(vi_mapping_elements))
		return -EINVAL;

	/* check for order of csi-stream-id and vi-unit-id in DT entry */
	for (index = 0; index < ARRAY_SIZE(vi_mapping_elements); index++) {
		int map_elem = of_property_match_string(np,
			"nvidia,vi-mapping-names", vi_mapping_elements[index]);
		if (map_elem != index) {
			dev_err(dev, "invalid mapping order\n");
			return -EINVAL;
		}
	}

	for (index = 0; index < map_table_size; index++)
		map_table[index] = INVALID_VI_UNIT_ID;

	for (index = 0; index < map_table_size; index++) {
		uint32_t stream_index = NVCSI_STREAM_INVALID_ID;
		uint32_t vi_unit_id = INVALID_VI_UNIT_ID;

		err = of_property_read_u32_index(np,
			"nvidia,vi-mapping",
			2 * index,
			&stream_index);
		if (err) {
			dev_err(dev,
				"%s: ERR %d: missing property nvidia,vi-mapping or csi_stream_id at index %d",
				__func__, err, index);
			return err;
		}

		/* Check for valid/duplicate csi-stream-id */
		if (stream_index >= MAX_NVCSI_STREAM_IDS ||
			map_table[stream_index] != INVALID_VI_UNIT_ID) {
			dev_err(dev, "%s: mapping invalid csi_stream_id: %u\n",
					__func__, stream_index);
			return -EINVAL;
		}

		err = of_property_read_u32_index(np,
			"nvidia,vi-mapping",
			2 * index + 1,
			&vi_unit_id);
		if (err) {
			dev_err(dev,
				"%s: ERR %d: missing property nvidia,vi-mapping or vi_unit_id at index %d",
				__func__, err, index);
			return err;
		}

		/* check for valid vi-unit-id */
		if (vi_unit_id >= MAX_VI_UNITS) {
			dev_err(dev, "%s: mapping invalid vi_unit_id: %u\n",
					__func__, vi_unit_id);
			return -EINVAL;
		}

		map_table[stream_index] = vi_unit_id;
	}

	dev_dbg(dev, "%s: csi-stream to vi-instance mapping table size: %u\n",
		__func__, info->num_csi_vi_maps);

	for (index = 0; index < ARRAY_SIZE(info->vi_instance_table); index++)
		dev_dbg(dev, "%s: vi_instance_table[%d] = %d\n",
			__func__, index, info->vi_instance_table[index]);

	return 0;
}

/**
 * @brief Probe function for VI capture driver.
 *
 * This function performs the following operations:
 * - Allocates and initializes driver data using @ref devm_kzalloc().
 * - Reads maximum VI channels using @ref of_property_read_u32().
 * - Validates maximum VI channels is less than @ref NUM_VI_CHANNELS.
 * - Gets VI device nodes using @ref of_parse_phandle().
 * - Gets platform devices using @ref of_find_device_by_node().
 * - Release each VI device node using @ref of_node_put().
 * - Sets driver data using @ref platform_set_drvdata().
 * - If more than one VI device, reads CSI-VI mapping using @ref csi_vi_get_mapping_table().
 * - Registers VI channel driver using @ref vi_channel_drv_register().
 * - If error, exits VI channel driver subsystem using @ref vi_channel_drv_exit().
 * - Initializes media controller using @ref tegra_capture_vi_media_controller_init().
 *
 * @param[in] pdev  Platform device pointer.
 *                  Valid value: non-NULL.
 *
 * @retval 0        Operation completed successfully.
 * @retval -ENOMEM  Memory allocation failed from @ref devm_kzalloc().
 * @retval -EINVAL  Invalid device tree parameters or error from @ref of_property_read_u32().
 * @retval -ENODEV  Required devices not found from @ref of_parse_phandle() or
 *                  @ref of_find_device_by_node().
 * @retval (int)    Error codes from @ref csi_vi_get_mapping_table(),
 *                  @ref vi_channel_drv_register(),
 *                  or @ref tegra_capture_vi_media_controller_init().
 */
static int capture_vi_probe(struct platform_device *pdev)
{
	uint32_t ii;
	int err = 0;
	struct tegra_capture_vi_data *info;
	struct device *dev = &pdev->dev;

	dev_dbg(dev, "%s: tegra-camrtc-capture-vi probe\n", __func__);

	info = devm_kzalloc(dev,
			sizeof(*info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;

	info->num_vi_devices = 0;

	err = of_property_read_u32(dev->of_node, "nvidia,vi-max-channels",
			&info->max_vi_channels);
	if (err || info->max_vi_channels > NUM_VI_CHANNELS) {
		err = -EINVAL;
		goto cleanup;
	}

	if (info->max_vi_channels == 0)
		info->max_vi_channels = DEFAULT_VI_CHANNELS;

	ii = 0U;
	do {
		struct device_node *np;
		struct platform_device *pvidev;

		np = of_parse_phandle(dev->of_node, "nvidia,vi-devices", ii);
		if (np == NULL)
			break;

		if (info->num_vi_devices >= ARRAY_SIZE(info->vi_pdevices)) {
			of_node_put(np);
			err = -EINVAL;
			goto cleanup;
		}

		pvidev = of_find_device_by_node(np);
		of_node_put(np);

		if (pvidev == NULL) {
			dev_warn(dev, "vi node %d has no device\n", ii);
			err = -ENODEV;
			goto cleanup;
		}

		info->vi_pdevices[ii] = pvidev;
		info->num_vi_devices++;
	} while (!check_add_overflow(ii, 1U, &ii));

	if (info->num_vi_devices < 1)
		return -EINVAL;

	platform_set_drvdata(pdev, info);

	if (info->num_vi_devices == 1) {
		dev_dbg(dev, "default 0 vi-unit-id for all csi-stream-ids\n");
	} else {
		/* read mapping table from DT for multiple VIs */
		err = csi_vi_get_mapping_table(pdev);
		if (err) {
			dev_err(dev,
				"%s: reading csi-to-vi mapping failed\n",
				__func__);
			goto cleanup;
		}
	}

	err = vi_channel_drv_register(pdev, info->max_vi_channels);
	if (err) {
		vi_channel_drv_exit();
		goto cleanup;
	}
#ifdef NV_IS_L4T
	info->vi_common.mc_vi.vi = &info->vi_common;
	info->vi_common.mc_vi.fops = &vi5_fops;
	err = tegra_capture_vi_media_controller_init(
			&info->vi_common.mc_vi, pdev);
	if (err) {
		dev_warn(&pdev->dev, "media controller init failed\n");
		err = 0;
	}
#endif
	memset(channels, 0 , sizeof(channels));

	return 0;

cleanup:
	for (ii = 0; ii < info->num_vi_devices; ii++)
		put_device(&info->vi_pdevices[ii]->dev);

	dev_err(dev, "%s: tegra-camrtc-capture-vi probe failed\n", __func__);
	return err;
}

/**
 * @brief Remove function for VI capture driver.
 *
 * This function performs the following operations:
 * - Retrieves capture driver data using @ref platform_get_drvdata().
 * - Releases VI device references using @ref put_device().
 * - Unregisters VI channel driver using @ref vi_channel_drv_unregister().
 * - Cleans up media controller using @ref tegra_vi_media_controller_cleanup().
 * - Exits VI channel driver using @ref vi_channel_drv_exit().
 *
 * @param[in] pdev  Platform device pointer.
 *                  Valid value: non-NULL.
 *
 * @retval 0        Operation completed successfully.
 */
static int capture_vi_remove(struct platform_device *pdev)
{
	struct tegra_capture_vi_data *info;
	uint32_t ii;
	struct device *dev = &pdev->dev;

	dev_dbg(dev, "%s:tegra-camrtc-capture-vi remove\n", __func__);

	info = platform_get_drvdata(pdev);

	for (ii = 0; ii < info->num_vi_devices; ii++)
		put_device(&info->vi_pdevices[ii]->dev);

	vi_channel_drv_unregister(&pdev->dev);
#ifdef NV_IS_L4T
	tegra_vi_media_controller_cleanup(&info->vi_common.mc_vi);
#endif
	vi_channel_drv_exit();

	return 0;
}

static const struct of_device_id capture_vi_of_match[] = {
	{ .compatible = "nvidia,tegra-camrtc-capture-vi" },
	{ },
};
MODULE_DEVICE_TABLE(of, capture_vi_of_match);

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
/**
 * @brief Wrapper function for VI capture driver removal (void return version).
 *
 * This function performs the following operations:
 * - Calls @ref capture_vi_remove() to perform actual driver cleanup.
 * - Used for Linux kernel version 6.11 and later where remove returns void.
 *
 * @param[in] pdev  Platform device pointer.
 *                  Valid value: non-NULL.
 */
static void capture_vi_remove_wrapper(struct platform_device *pdev)
{
	capture_vi_remove(pdev);
}
#else
/**
 * @brief Wrapper function for VI capture driver removal (int return version).
 *
 * This function performs the following operations:
 * - Calls @ref capture_vi_remove() to perform actual driver cleanup.
 * - Used for Linux kernel versions before 6.11 where remove returns int.
 *
 * @param[in] pdev  Platform device pointer.
 *                  Valid value: non-NULL.
 *
 * @retval (int)  Return value from @ref capture_vi_remove().
 */
static int capture_vi_remove_wrapper(struct platform_device *pdev)
{
	return capture_vi_remove(pdev);
}
#endif

static struct platform_driver capture_vi_driver = {
	.probe = capture_vi_probe,
	.remove = capture_vi_remove_wrapper,
	.driver = {
		.owner = THIS_MODULE,
		.name = "tegra-camrtc-capture-vi",
		.of_match_table = capture_vi_of_match
	}
};

/**
 * @brief Module initialization function for VI capture driver.
 *
 * This function performs the following operations:
 * - Initializes VI channel driver subsystem using @ref vi_channel_drv_init().
 * - Registers platform driver using @ref platform_driver_register().
 * - If error, exits VI channel driver subsystem using @ref vi_channel_drv_exit().
 *
 * @retval 0        Operation completed successfully.
 * @retval (int)    Error codes from @ref vi_channel_drv_init() or @ref platform_driver_register().
 */
static int __init capture_vi_init(void)
{
	int err;
	err = vi_channel_drv_init();
	if (err)
		return err;

	err = platform_driver_register(&capture_vi_driver);
	if (err) {
		vi_channel_drv_exit();
		return err;
	}

	return 0;
}

/**
 * @brief Module exit function for VI capture driver.
 *
 * This function performs the following operations:
 * - Exits VI channel driver subsystem using @ref vi_channel_drv_exit().
 * - Unregisters platform driver using @ref platform_driver_unregister().
 */
static void __exit capture_vi_exit(void)
{
	vi_channel_drv_exit();
	platform_driver_unregister(&capture_vi_driver);
}

module_init(capture_vi_init);
module_exit(capture_vi_exit);

#if defined(NV_MODULE_IMPORT_NS_CALLS_STRINGIFY)
MODULE_IMPORT_NS(DMA_BUF);
#else
MODULE_IMPORT_NS("DMA_BUF");
#endif
MODULE_DESCRIPTION("tegra fusa-capture driver");
MODULE_LICENSE("GPL");
