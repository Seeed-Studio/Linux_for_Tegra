/*
 * Copyright (c) 2020-2023, NVIDIA CORPORATION. All rights reserved.
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include "nvargusv4l2_os.h"
#include "nvargusv4l2_nvqueue.h"
#include "nvbufsurface.h"
#include "nvargusv4l2_argus.h"

static ArgusSamples::EGLDisplayHolder eglDisplay(true);

const char* getStatusString(Argus::Status status)
{
    switch (status)
    {
        case Argus::STATUS_OK:               return "OK";
        case Argus::STATUS_INVALID_PARAMS:   return "INVALID_PARAMS";
        case Argus::STATUS_INVALID_SETTINGS: return "INVALID_SETTINGS";
        case Argus::STATUS_UNAVAILABLE:      return "UNAVAILABLE";
        case Argus::STATUS_OUT_OF_MEMORY:    return "OUT_OF_MEMORY";
        case Argus::STATUS_UNIMPLEMENTED:    return "UNIMPLEMENTED";
        case Argus::STATUS_TIMEOUT:          return "TIMEOUT";
        case Argus::STATUS_CANCELLED:        return "CANCELLED";
        case Argus::STATUS_DISCONNECTED:     return "DISCONNECTED";
        case Argus::STATUS_END_OF_STREAM:    return "END_OF_STREAM";
        default:                             return "BAD STATUS";
    }
}

bool v4l2_cam_atomic_read_bool(v4l2_camera_context* ctx, bool *var)
{
    bool value = false;
    NvMutexAcquire(ctx->stats_mutex);
    value = *var;
    NvMutexRelease(ctx->stats_mutex);
    return value;
}

void v4l2_cam_atomic_write_bool(v4l2_camera_context* ctx, bool *var, bool value)
{
    NvMutexAcquire(ctx->stats_mutex);
    *var = value;
    NvMutexRelease(ctx->stats_mutex);
}

uint32_t v4l2_cam_atomic_get(v4l2_camera_context *ctx, uint32_t *var)
{
    uint32_t temp = 0;
    NvMutexAcquire(ctx->stats_mutex);
    temp = *var;
    NvMutexRelease(ctx->stats_mutex);
    return temp;
}

int32_t initialize_outputstream(v4l2_camera_context *ctx, cam_params *argus_params)
{
    int32_t err = 0;
    int32_t best_match = -1;
    std::vector<argusv4l2_sensormode> argusv4l2_sensormode = ctx->argusv4l2_sensormodes;
    DBG_PRINT("CAM_CTX(%p): Initializing Argus::OutputStream\n", ctx);

    if (NvMutexCreate(&ctx->stats_mutex) != 0)
    {
        REL_PRINT("CAM_CTX(%p) Error creating stats mutex\n", ctx);
    }

    err = NvSemaphoreCreate(&ctx->acquirebuf_sema, 0);
    if (err)
    {
        REL_PRINT("CAM_CTX(%p) Failed to create acquirebuf_sema for thread\n", ctx);
        return err;
    }
    /* Initialize streamsettings */
    ctx->m_streamSettings = Argus::UniqueObj<Argus::OutputStreamSettings>(
        ctx->m_iCaptureSession->createOutputStreamSettings(Argus::STREAM_TYPE_BUFFER, &ctx->m_cameraStatus));
    ctx->m_iStreamSettings =
        Argus::interface_cast<Argus::IBufferOutputStreamSettings>(ctx->m_streamSettings);
    if (!ctx->m_iStreamSettings)
    {
        REL_PRINT("CAM_CTX(%p) Failed to get stream settings: %s\n",
                ctx, getStatusString(ctx->m_cameraStatus));
        return -1;
    }
    ctx->m_iStreamSettings->setBufferType(Argus::BUFFER_TYPE_EGL_IMAGE);
    ctx->m_iStreamSettings->setMetadataEnable(true);
    ctx->m_iStreamSettings->setSyncType(Argus::SYNC_TYPE_NONE);
    // TODO: synctype EGL and the significance to make the initialization

    ctx->m_outputStream = Argus::UniqueObj<Argus::OutputStream>(
        ctx->m_iCaptureSession->createOutputStream(ctx->m_streamSettings.get(), &ctx->m_cameraStatus));
    ctx->m_iStream =
        Argus::interface_cast<Argus::IBufferOutputStream>(ctx->m_outputStream);
    if (!ctx->m_iStream)
    {
        REL_PRINT("CAM_CTX(%p) Failed to get Buffer OutputStream: %s\n",
                ctx, getStatusString(ctx->m_cameraStatus));
        return -1;
    }

    ctx->m_request.reset(ctx->m_iCaptureSession->createRequest());
    ctx->m_iRequest = Argus::interface_cast<Argus::IRequest>(ctx->m_request);
    if (!ctx->m_iRequest)
    {
        REL_PRINT("CAM_CTX(%p) Failed to create Request\n", ctx);
        return -1;
    }
    ctx->m_iRequest->enableOutputStream(ctx->m_outputStream.get());

    /* Get source settings pointer */
    ctx->m_iSourceSettings =
        Argus::interface_cast<Argus::ISourceSettings>(ctx->m_iRequest->getSourceSettings());

    /* Get Control Setttings pointer */
    ctx->argus_settings.autoControlSettingsPtr =
        Argus::interface_cast<Argus::IAutoControlSettings>(ctx->m_iRequest->getAutoControlSettings());

    /* Find best match */
    for (uint32_t idx = 0; idx < argusv4l2_sensormode.size(); idx++)
    {
        if (ctx->argus_settings.sensorModeIdx == -1)
        {
            if (argus_params->width <= argusv4l2_sensormode[idx].max_width &&
                argus_params->height <= argusv4l2_sensormode[idx].max_height &&
                ctx->argus_settings.frameDuration >= argusv4l2_sensormode[idx].frame_duration)
            {
                if (best_match == -1)
                    best_match = idx;
                else if (argusv4l2_sensormode[best_match].frame_duration >= argusv4l2_sensormode[idx].frame_duration ||
                        argusv4l2_sensormode[best_match].max_width >= argusv4l2_sensormode[idx].max_width)
                    best_match = idx;
            }
        }
    }
    if (best_match == -1)
    {
        DBG_PRINT("CAM_CTX(%p): Requested settings not supported. Switching to default camera mode\n", ctx);
        ctx->argus_settings.sensorModeIdx = 0;
    }
    else
        ctx->argus_settings.sensorModeIdx = best_match;

    ctx->argus_settings.frameDuration =
        argusv4l2_sensormode[ctx->argus_settings.sensorModeIdx].frame_duration;

    Argus::SensorMode *sensorMode = ctx->m_sensorModes[ctx->argus_settings.sensorModeIdx];
    if (!sensorMode)
    {
        REL_PRINT("CAM_CTX(%p) Invalid SensorMode\n", ctx);
        return -1;
    }
    ctx->m_iSourceSettings->setSensorMode(sensorMode);
    ctx->m_iSourceSettings->setFrameDurationRange(Argus::Range<uint64_t>(ctx->argus_settings.frameDuration));

    return 0;
}

int32_t v4l2_cam_set_argus_settings(v4l2_camera_context *ctx, uint32_t ctrl_id)
{
    DBG_PRINT("CAM_CTX(%p): %s: ENTER\n", ctx, __func__);
    cam_settings *pArgusSettings = &ctx->argus_settings;
    Argus::IAutoControlSettings *pACSettings = pArgusSettings->autoControlSettingsPtr;

    switch (ctrl_id)
    {
        case V4L2_CID_ARGUS_SENSOR_MODE:
        {
            uint32_t sensor_mode_id = pArgusSettings->sensorModeIdx;
            Argus::SensorMode *sensorMode = ctx->m_sensorModes[sensor_mode_id];
            ctx->m_iSourceSettings->setSensorMode(sensorMode);
            if (pArgusSettings->frameDuration < ctx->argusv4l2_sensormodes[sensor_mode_id].frame_duration)
            {
                DBG_PRINT("CAM_CTX(%p): Resetting the min frame duration for mode %d\n", ctx, sensor_mode_id);
                pArgusSettings->frameDuration = ctx->argusv4l2_sensormodes[sensor_mode_id].frame_duration;
                ctx->m_iSourceSettings->setFrameDurationRange(Argus::Range<uint64_t>(pArgusSettings->frameDuration));
            }
        }
            break;
        case V4L2_CID_ARGUS_DENOISE_MODE:
        {
            Argus::IDenoiseSettings *denoiseSettings =
                Argus::interface_cast<Argus::IDenoiseSettings>(ctx->m_request);
            if (!denoiseSettings)
            {
                REL_PRINT("CAM_CTX(%p) Failed to get DenoiseSettings interface\n", ctx);
                return EINVAL;
            }
            pArgusSettings->denoiseSettingsPtr = denoiseSettings;
            switch (pArgusSettings->denoiseMode)
            {
                case V4L2_ARGUS_DENOISE_MODE_OFF:
                    denoiseSettings->setDenoiseMode(Argus::DENOISE_MODE_OFF);
                    break;
                case V4L2_ARGUS_DENOISE_MODE_FAST:
                    denoiseSettings->setDenoiseMode(Argus::DENOISE_MODE_FAST);
                    break;
                case V4L2_ARGUS_DENOISE_MODE_HIGH_QUALITY:
                    denoiseSettings->setDenoiseMode(Argus::DENOISE_MODE_HIGH_QUALITY);
                    break;
                default:
                    REL_PRINT("CAM_CTX(%p) Invalid Value. Setting to Denoise mode 0 as default\n", ctx);
                    denoiseSettings->setDenoiseMode(Argus::DENOISE_MODE_OFF);
                    pArgusSettings->denoiseMode = V4L2_ARGUS_DENOISE_MODE_OFF;
            }
        }
            break;
        case V4L2_CID_ARGUS_DENOISE_STRENGTH:
        {
            Argus::Status errStatus = Argus::STATUS_OK;
            Argus::IDenoiseSettings *denoiseSettings =
                Argus::interface_cast<Argus::IDenoiseSettings>(ctx->m_request);
            if (!denoiseSettings)
            {
                REL_PRINT("CAM_CTX(%p) Failed to get DenoiseSettings interface\n", ctx);
                return EINVAL;
            }
            pArgusSettings->denoiseSettingsPtr = denoiseSettings;
            errStatus = denoiseSettings->setDenoiseStrength(pArgusSettings->denoiseStrength);
            if (errStatus != Argus::STATUS_OK)
            {
                REL_PRINT("CAM_CTX(%p) Failed to get DenoiseSettings interface\n", ctx);
                return EINVAL;
            }
        }
            break;
        case V4L2_CID_ARGUS_EE_MODE:
        {
            Argus::IEdgeEnhanceSettings *eeSettings =
                Argus::interface_cast<Argus::IEdgeEnhanceSettings>(ctx->m_request);
            if (!eeSettings)
            {
                REL_PRINT("CAM_CTX(%p) Failed to get EdgeEnhanceSettings interface\n", ctx);
                return EINVAL;
            }
            pArgusSettings->EESettingsPtr = eeSettings;
            switch (pArgusSettings->edgeEnhanceMode)
            {
                case V4L2_ARGUS_EDGE_ENHANCE_MODE_OFF:
                    eeSettings->setEdgeEnhanceMode(Argus::EDGE_ENHANCE_MODE_OFF);
                    break;
                case V4L2_ARGUS_EDGE_ENHANCE_MODE_FAST:
                    eeSettings->setEdgeEnhanceMode(Argus::EDGE_ENHANCE_MODE_FAST);
                    break;
                case V4L2_ARGUS_EDGE_ENHANCE_MODE_HIGH_QUALITY:
                    eeSettings->setEdgeEnhanceMode(Argus::EDGE_ENHANCE_MODE_HIGH_QUALITY);
                    break;
                default:
                    REL_PRINT("CAM_CTX(%p) Invalid Value. Setting to EE mode 0 as default\n", ctx);
                    eeSettings->setEdgeEnhanceMode(Argus::EDGE_ENHANCE_MODE_OFF);
                    pArgusSettings->edgeEnhanceMode = V4L2_ARGUS_EDGE_ENHANCE_MODE_OFF;
            }
        }
            break;
        case V4L2_CID_ARGUS_EE_STRENGTH:
        {
            Argus::Status errStatus = Argus::STATUS_OK;
            Argus::IEdgeEnhanceSettings *eeSettings =
                Argus::interface_cast<Argus::IEdgeEnhanceSettings>(ctx->m_request);
            if (!eeSettings)
            {
                REL_PRINT("CAM_CTX(%p) Failed to get EdgeEnhanceSettings interface\n", ctx);
                return EINVAL;
            }
            pArgusSettings->EESettingsPtr = eeSettings;
            errStatus = eeSettings->setEdgeEnhanceStrength(pArgusSettings->edgeEnhanceStrength);
            if (errStatus != Argus::STATUS_OK)
            {
                REL_PRINT("CAM_CTX(%p) Failed to get DenoiseSettings interface\n", ctx);
                return EINVAL;
            }
        }
            break;
        case V4L2_CID_ARGUS_AE_ANTIBANDING_MODE:
        {
            switch (pArgusSettings->aeAntibandingMode)
            {
                case V4L2_ARGUS_AE_ANTIBANDING_MODE_OFF:
                    pACSettings->setAeAntibandingMode(Argus::AE_ANTIBANDING_MODE_OFF);
                    break;
                case V4L2_ARGUS_AE_ANTIBANDING_MODE_AUTO:
                    pACSettings->setAeAntibandingMode(Argus::AE_ANTIBANDING_MODE_AUTO);
                    break;
                case V4L2_ARGUS_AE_ANTIBANDING_MODE_50HZ:
                    pACSettings->setAeAntibandingMode(Argus::AE_ANTIBANDING_MODE_50HZ);
                    break;
                case V4L2_ARGUS_AE_ANTIBANDING_MODE_60HZ:
                    pACSettings->setAeAntibandingMode(Argus::AE_ANTIBANDING_MODE_60HZ);
                    break;
                default:
                    pACSettings->setAeAntibandingMode(Argus::AE_ANTIBANDING_MODE_OFF);
                    pArgusSettings->aeAntibandingMode = V4L2_ARGUS_AE_ANTIBANDING_MODE_OFF;
            }
        }
            break;
        case V4L2_CID_ARGUS_AUTO_WHITE_BALANCE_MODE:
        {
            switch (pArgusSettings->awbMode)
            {
                case V4L2_ARGUS_AWB_MODE_OFF:
                    pACSettings->setAwbMode(Argus::AWB_MODE_OFF);
                    break;
                case V4L2_ARGUS_AWB_MODE_AUTO:
                    pACSettings->setAwbMode(Argus::AWB_MODE_AUTO);
                    break;
                case V4L2_ARGUS_AWB_MODE_INCANDESCENT:
                    pACSettings->setAwbMode(Argus::AWB_MODE_INCANDESCENT);
                    break;
                case V4L2_ARGUS_AWB_MODE_FLUORESCENT:
                    pACSettings->setAwbMode(Argus::AWB_MODE_FLUORESCENT);
                    break;
                case V4L2_ARGUS_AWB_MODE_WARM_FLUORESCENT:
                    pACSettings->setAwbMode(Argus::AWB_MODE_WARM_FLUORESCENT);
                    break;
                case V4L2_ARGUS_AWB_MODE_DAYLIGHT:
                    pACSettings->setAwbMode(Argus::AWB_MODE_DAYLIGHT);
                    break;
                case V4L2_ARGUS_AWB_MODE_CLOUDY_DAYLIGHT:
                    pACSettings->setAwbMode(Argus::AWB_MODE_CLOUDY_DAYLIGHT);
                    break;
                case V4L2_ARGUS_AWB_MODE_TWILIGHT:
                    pACSettings->setAwbMode(Argus::AWB_MODE_TWILIGHT);
                    break;
                case V4L2_ARGUS_AWB_MODE_SHADE:
                    pACSettings->setAwbMode(Argus::AWB_MODE_SHADE);
                    break;
                case V4L2_ARGUS_AWB_MODE_MANUAL:
                    pACSettings->setAwbMode(Argus::AWB_MODE_MANUAL);
                    break;
                default:
                    pACSettings->setAwbMode(Argus::AWB_MODE_OFF);
                    pArgusSettings->aeAntibandingMode = V4L2_ARGUS_AWB_MODE_OFF;
            }
            break;
        }
        case V4L2_CID_3A_LOCK:
        {
            if (pArgusSettings->aeLock)
                pACSettings->setAeLock(true);
            else
                pACSettings->setAeLock(false);

            if (pArgusSettings->awbLock)
                pACSettings->setAwbLock(true);
            else
                pACSettings->setAwbLock(false);
        }
            break;
        case V4L2_CID_ARGUS_EXPOSURE_COMPENSATION:
        {
            if (pArgusSettings->exposureCompensation > MAX_EXPOSURE_COMPENSATION)
                pArgusSettings->exposureCompensation = MAX_EXPOSURE_COMPENSATION;

            if (pArgusSettings->exposureCompensation < MIN_EXPOSURE_COMPENSATION)
                pArgusSettings->exposureCompensation = MIN_EXPOSURE_COMPENSATION;

            pACSettings->setExposureCompensation(pArgusSettings->exposureCompensation);
        }
            break;
        case V4L2_CID_ARGUS_ISP_DIGITAL_GAIN_RANGE:
            pACSettings->setIspDigitalGainRange(pArgusSettings->ispDigitalGainRange);
            break;
        case V4L2_CID_ARGUS_COLOR_SATURATION:
        {
            pACSettings->setColorSaturationEnable(pArgusSettings->enableColorSaturation);
            pACSettings->setColorSaturation(pArgusSettings->colorSaturation);
        }
            break;
        case V4L2_CID_ARGUS_GAIN_RANGE:
            ctx->m_iSourceSettings->setGainRange(pArgusSettings->gainRange);
            break;
        case V4L2_CID_ARGUS_EXPOSURE_TIME_RANGE:
            ctx->m_iSourceSettings->setExposureTimeRange(pArgusSettings->exposureTimeRange);
            break;
        default:
            break;
    }
    DBG_PRINT("CAM_CTX(%p) :%s: EXIT \n", ctx, __func__);
    return 0;
}

int32_t set_framerate(v4l2_camera_context *ctx, float frame_duration)
{
    if (ctx->argus_settings.frameDuration > frame_duration)
    {
        REL_PRINT("CAM_CTX(%p) Frame rate is greater than supported\n", ctx);
        return EINVAL;
    }

    ctx->m_iSourceSettings->setFrameDurationRange(Argus::Range<uint64_t>(frame_duration));
    ctx->argus_settings.frameDuration = frame_duration;
    return 0;
}

int32_t ReleaseStreamBuffer(v4l2_camera_context *ctx, int32_t index)
{
    Argus::Buffer *buffer = ctx->cap_buffers[index].argus_buffer;
    Argus::Status status = ctx->m_iStream->releaseBuffer(buffer);
    if (status != Argus::STATUS_OK)
    {
        REL_PRINT("CAM_CTX(%p) Failed to release buffer: %s\n",
                ctx, getStatusString(status));
        return EINVAL;
    }
    return 0;
}

Argus::Buffer* AcquireStreamBuffer(v4l2_camera_context *ctx, uint32_t timeout)
{
    Argus::Status status = Argus::STATUS_OK;
    Argus::Buffer *buffer = ctx->m_iStream->acquireBuffer(timeout, &status);
    if (status == Argus::STATUS_END_OF_STREAM)
    {
        REL_PRINT("CAM_CTX(%p) %s signalled from the stream\n",
                ctx, getStatusString(status));
        return NULL;
    }
    if (status != Argus::STATUS_OK)
    {
        REL_PRINT("CAM_CTX(%p) Failed to acquire buffer %s\n",
                ctx, getStatusString(status));
        return NULL;
    }
    return buffer;
}

static void FreeInputBuffers(camerav4l2_buffer *pBuffer, uint32_t buffer_idx,
                v4l2_camera_context *ctx)
{
    NvBufSurfaceParams *pMMSurface = &pBuffer->surf_params;
    NvBufSurfacePlaneParams *pPlanes = &pMMSurface->planeParams;
    int32_t ret = 0;
    uint32_t i = 0;

    /* Surfaces are freed only if we are not in DMABUF memory type */
    if (ctx->capture_memory_type != V4L2_MEMORY_DMABUF)
    {
        for (i = 0; i < pPlanes->num_planes; i++)
        {
            if (ctx->inbuf_mapped_address[buffer_idx][i] != NULL)
            {
                REL_PRINT("CAM_CTX(%p) NvRmMemUnmapping %d bytes surface %d plane %d address %p \n",
                          ctx, (pPlanes->pitch[i] * pPlanes->height[i]),
                          buffer_idx, i,
                          ctx->inbuf_mapped_address[buffer_idx][i]);
                NvBufSurface *nvbuf_surf = 0;

                ret = NvBufSurfaceFromFd((int)pBuffer->buf_fd,
                                         (void **)(&nvbuf_surf));
                if (ret != 0)
                    REL_PRINT("CAM_CTX(%p) Failed to Get NvBufSurface from FD\n", ctx);

                ret = NvBufSurfaceUnMap(nvbuf_surf, 0, i);
                if (ret != 0)
                    REL_PRINT("CAM_CTX(%p) Failed to Unmap NvBufSurface\n", ctx);
                ctx->inbuf_mapped_address[buffer_idx][i] = NULL;

                if (i == (pPlanes->num_planes - 1))
                {
                    ret = NvBufSurfaceDestroy(nvbuf_surf);
                    if (ret != 0)
                        REL_PRINT("CAM_CTX(%p) Failed to destroy NvBufSurface\n", ctx);
                    pBuffer->buf_fd = 0;
                }
            }
        }

        if (pBuffer->buf_fd)
        {
            NvBufSurface *nvbuf_surf = 0;

            ret = NvBufSurfaceFromFd((int)pBuffer->buf_fd,
                                     (void **)(&nvbuf_surf));
            if (ret != 0)
                REL_PRINT("CAM_CTX(%p) Failed to Get NvBufSurface from FD\n", ctx);

            ret = NvBufSurfaceDestroy(nvbuf_surf);
            if (ret != 0)
                REL_PRINT("CAM_CTX(%p) Failed to destroy NvBufSurface\n", ctx);
            pBuffer->buf_fd = 0;
        }
        pMMSurface->bufferDesc = 0;
        /* Delete the allocated Argus::Buffers*/
        if (ctx->cap_buffers[buffer_idx].argus_buffer && ctx->cap_buffers[buffer_idx].allocated)
        {
            REL_PRINT("CAM_CTX(%p) Deleting the Argus Buffer at index %d\n", ctx, buffer_idx);
            ctx->cap_buffers[buffer_idx].argus_buffer->destroy();
        }

    }
    else
    {
        /* Delete the allocated Argus::Buffers*/
        if (ctx->cap_buffers[buffer_idx].argus_buffer && ctx->cap_buffers[buffer_idx].allocated)
        {
            REL_PRINT("CAM_CTX(%p) Deleting the Argus Buffer at index %d\n", ctx, buffer_idx);
            ctx->cap_buffers[buffer_idx].argus_buffer->destroy();
        }
    }
    return;
}

static void argus_cleanup_main(v4l2_camera_context *ctx, cam_params *params)
{
    REL_PRINT("CAM_CTX(%p) Cleaning up Argus \n", ctx);
    if (ctx)
    {
        /* Cancel requests, explicitly stopping repeat captures */
        if (ctx->m_iCaptureSession)
        {
            ctx->m_iCaptureSession->cancelRequests();
            ctx->m_iCaptureSession->stopRepeat();
            ctx->m_iCaptureSession->waitForIdle();
        }

        /* Destroy the stream */
        if (ctx->m_streamSettings)
            ctx->m_streamSettings.reset();
        if (ctx->m_request)
            ctx->m_request.reset();
        if (ctx->m_outputStream)
            ctx->m_outputStream.reset();
        if (ctx->m_eventQueue)
            ctx->m_eventQueue.reset();

        /* Shutdown Argus */
        if (ctx->m_captureSession)
            ctx->m_captureSession.reset();

        ctx->m_sensorModes.clear();
        ctx->m_eventTypes.clear();

        ctx->m_iCaptureSession      = NULL;
        ctx->m_iEventProvider       = NULL;
        ctx->m_iEventQueue          = NULL;
        ctx->m_iCameraProperties    = NULL;
        ctx->m_iStreamSettings      = NULL;
        ctx->m_iStream              = NULL;
        ctx->m_iRequest             = NULL;
        ctx->m_iSourceSettings      = NULL;
    }
}

void v4l2_close_argus_context(v4l2_context *v4l2_ctx)
{
    v4l2_camera_context *ctx = (v4l2_camera_context *)v4l2_ctx->actual_context;
    REL_PRINT("CAM_CTX(%p) Cleaning up camera context \n", ctx);

    NvMutexAcquire(ctx->context_mutex);
    eglDisplay.cleanup();
    free_cap_buffers(ctx);
    argus_cleanup_main(ctx, &ctx->argus_params);
    NvQueueDestroy(&ctx->capplane_Q);

    NvSemaphoreDestroy(ctx->acquirebuf_sema);

    NvMutexRelease(ctx->context_mutex);
    NvMutexDestroy(ctx->context_mutex);
    NvMutexDestroy(ctx->stats_mutex);
    free(ctx);

    REL_PRINT("CAM_CTX(%p) Done deleting camera context \n", ctx);
}

int32_t allocate_all_capture_queues(v4l2_camera_context *ctx)
{
    int32_t err = 0;

    DBG_PRINT("CAM_CTX(%p): Allocating capture queues\n", ctx);

    if (ctx->capplane_Q)
        NvQueueDestroy(&ctx->capplane_Q);

    err = NvQueueCreate(&ctx->capplane_Q, ctx->capture_buffer_count,
                sizeof(q_entry));
    if (err)
    {
        REL_PRINT("CAM_CTX(%p) Failed to create capture OutQ\n", ctx);
        return EINVAL;
    }

    return 0;
}

/*
 * Allocates the requested number of capture plane buffers along with argus buffers.
 * It also destroys previously allocated buffers, if any, which exceeds
 * the requested number.
 */

int32_t allocate_capture_buffers(v4l2_camera_context *ctx, cam_params *argus_params)
{
    uint32_t i = 0;
    int32_t ret = 0;
    NvBufSurfaceAllocateParams alloc_params = {{0}};
    NvBufSurface *nvbuf_surf = NULL;
    Argus::Status buffer_status = Argus::STATUS_OK;

    if (eglDisplay.get() == EGL_NO_DISPLAY)
    {
        if (!eglDisplay.initialize())
        {
            REL_PRINT("CAM_CTX(%p) Failed to initialize EGLDisplay\n", ctx);
            return EINVAL;
        }
    }

    /* Get Buffer Setttings for BufferOutputStream */
    Argus::UniqueObj<Argus::BufferSettings> settings(ctx->m_iStream->createBufferSettings());
    Argus::IEGLImageBufferSettings *iBufferSettings =
                            Argus::interface_cast<Argus::IEGLImageBufferSettings>(settings);
    if (!iBufferSettings)
    {
        REL_PRINT("CAM_CTX(%p) Failed to create Buffer Settings\n", ctx);
        return EINVAL;
    }

    for (i = 0; i < MAX_OP_BUFFERS; ++i)
    {
        if (i < ctx->capture_buffer_count)
        {
            /* To check if mapped in userspace*/
            if ((ctx->cap_buffers[i].flags) & (V4L2_BUF_FLAG_MAPPED))
            {
                REL_PRINT("CAM_CTX(%p) Buffer %d already mapped in userspace\n", ctx, i);
                return EBUSY;
            }
            if (ctx->cap_buffers[i].allocated &&
                !(v4l2_cam_atomic_read_bool(ctx, &ctx->camera_state_stopped)))
            {
                REL_PRINT("CAM_CTX(%p) Buffer %d is in use\n", ctx, i);
                return EBUSY;
            }
            if (!ctx->cap_buffers[i].allocated)
            {
                alloc_params.params.width = argus_params->width;
                alloc_params.params.height = argus_params->height;
                alloc_params.params.layout = NVBUF_LAYOUT_PITCH;
                alloc_params.params.colorFormat = NVBUF_COLOR_FORMAT_NV12;
                alloc_params.params.memType = NVBUF_MEM_SURFACE_ARRAY;
                alloc_params.memtag = NvBufSurfaceTag_CAMERA;

                ret = NvBufSurfaceAllocate(&nvbuf_surf, 1, &alloc_params);
                if (ret != 0)
                {
                    REL_PRINT("CAM_CTX(%p) Failed to create NvBufSurface\n", ctx);
                    return ENOMEM;
                }
                nvbuf_surf->numFilled = 1;
                memcpy(&ctx->cap_buffers[i].surf_params,
                       &nvbuf_surf->surfaceList[0], sizeof(NvBufSurfaceParams));
                ctx->cap_buffers[i].buf_fd = nvbuf_surf->surfaceList[0].bufferDesc;

                NvBufSurfaceMapEglImage(nvbuf_surf, 0);
                EGLImageKHR eglImage = nvbuf_surf->surfaceList->mappedAddr.eglImage;

                if (eglImage == EGL_NO_IMAGE_KHR)
                {
                    REL_PRINT("CAM_CTX(%p) Failed to create EGLImage from NvBuffer\n", ctx);
                    goto error_nomem;
                }
                iBufferSettings->setEGLImage(eglImage);
                ctx->cap_buffers[i].argus_buffer = ctx->m_iStream->createBuffer(settings.get(), &buffer_status);

                /* The EGLImage may be destroyed once the Buffer has been created. */
                NvBufSurfaceUnMapEglImage(nvbuf_surf, 0);

                if (buffer_status != Argus::STATUS_OK ||
                    (!Argus::interface_cast<Argus::IEGLImageBuffer>(ctx->cap_buffers[i].argus_buffer)))
                {
                    REL_PRINT("CAM_CTX(%p) Failed to create Argus Buffer\n", ctx);
                    goto error_nomem;
                }

                Argus::IBuffer *iBuffer = Argus::interface_cast<Argus::IBuffer>(ctx->cap_buffers[i].argus_buffer);

                /* Map Argus::Buffer to NvBuffer */
                iBuffer->setClientData(&ctx->cap_buffers[i]);
                ctx->cap_buffers[i].allocated = 1;
            }
        }
        else
        {
            /* Destroy the allocated buffer if exceeds the capture_buffer_count. */
            if (ctx->cap_buffers[i].allocated)
            {
                FreeInputBuffers(&ctx->cap_buffers[i], i, ctx);
                ctx->cap_buffers[i].allocated = 0;
            }
        }
    }

    return 0;
error_nomem:
    if (nvbuf_surf)
        NvBufSurfaceDestroy(nvbuf_surf);
    return ENOMEM;
}

/*
 * Allocates an argus buffer for the corresponding queried DMA buffer.
 * The user interface is responsible for destroying the FD in case of error.
 */

int32_t allocate_argus_buffers(v4l2_camera_context *ctx, cam_params *argus_params,
                               int32_t dmabuf_fd, uint32_t buffer_idx)
{
    int32_t ret = 0;
    NvBufSurface *nvbuf_surf = 0;
    Argus::Status buffer_status = Argus::STATUS_OK;

    /* Querybuf can be called multiple times. We just ignore argus allocation call at that time. */
    if (ctx->cap_buffers[buffer_idx].allocated &&
        !(v4l2_cam_atomic_read_bool(ctx, &ctx->camera_state_stopped)))
    {
        DBG_PRINT("CAM_CTX(%p): Buffer %d already allocated\n", ctx, buffer_idx);
        return 0;
    }

    /* Get Buffer Setttings for BufferOutputStream */
    Argus::UniqueObj<Argus::BufferSettings> settings(ctx->m_iStream->createBufferSettings());
    Argus::IEGLImageBufferSettings *iBufferSettings =
                            Argus::interface_cast<Argus::IEGLImageBufferSettings>(settings);
    if (!iBufferSettings)
    {
        REL_PRINT("CAM_CTX(%p) Failed to create Buffer Settings\n", ctx);
        return EINVAL;
    }

    if (!ctx->cap_buffers[buffer_idx].allocated)
    {
        ret = NvBufSurfaceFromFd(dmabuf_fd, (void **)(&nvbuf_surf));
        if (ret != 0)
        {
            REL_PRINT("CAM_CTX(%p) Failed to get buffer from FD\n", ctx);
            return EINVAL;
        }
        memcpy(&ctx->cap_buffers[buffer_idx].surf_params,
               &nvbuf_surf->surfaceList[0], sizeof(NvBufSurfaceParams));
        ctx->cap_buffers[buffer_idx].buf_fd = dmabuf_fd;
        ctx->cap_buffers[buffer_idx].buffer_id = buffer_idx;

        NvBufSurfaceMapEglImage(nvbuf_surf, 0);
        EGLImageKHR eglImage = nvbuf_surf->surfaceList->mappedAddr.eglImage;
        if (eglImage == EGL_NO_IMAGE_KHR)
        {
            REL_PRINT("CAM_CTX(%p) Failed to create EGLImage from NvBuffer\n", ctx);
            return EINVAL;
        }
        iBufferSettings->setEGLImage(eglImage);
        ctx->cap_buffers[buffer_idx].argus_buffer = ctx->m_iStream->createBuffer(settings.get(), &buffer_status);

        // The EGLImage may be destroyed once the Buffer has been created.
        NvBufSurfaceUnMapEglImage(nvbuf_surf, 0);

        if (buffer_status != Argus::STATUS_OK ||
            (!Argus::interface_cast<Argus::IEGLImageBuffer>(ctx->cap_buffers[buffer_idx].argus_buffer)))
        {
            REL_PRINT("CAM_CTX(%p) Failed to create Argus Buffer\n", ctx);
            return EINVAL;
        }

        Argus::IBuffer *iBuffer =
            Argus::interface_cast<Argus::IBuffer>(ctx->cap_buffers[buffer_idx].argus_buffer);

        /* Map Argus::Buffer to NvBuffer */
        iBuffer->setClientData(&ctx->cap_buffers[buffer_idx]);
        ctx->cap_buffers[buffer_idx].allocated = 1;
    }

    return 0;
}

void free_cap_buffers(v4l2_camera_context *ctx)
{
    uint32_t i = 0;
    REL_PRINT("CAM_CTX(%p) Releasing buffers of the camera ctx\n", ctx);
    for (i = 0; i < ctx->capture_buffer_count; i++)
    {
        if (ctx->cap_buffers[i].allocated)
        {
            FreeInputBuffers(&ctx->cap_buffers[i], i, ctx);
            ctx->cap_buffers[i].allocated = 0;
        }
        if (ctx->frame_metadata[i])
        {
            free(ctx->frame_metadata[i]);
            ctx->frame_metadata[i] = NULL;
        }
    }
}

int32_t query_cam_buffers(v4l2_camera_context *ctx, cam_params *argus_params,
                        uint64_t *psizes)
{
    int32_t ret = 0;
    NvBufSurface *nvbuf_surf = NULL;
    NvBufSurfaceAllocateParams alloc_params = {{0}};

    if (!argus_params->width || !argus_params->height)
    {
        ERROR_PRINT("CAM_CTX(%p) Invalid width= %d height= %d\n",
                    ctx, argus_params->width, argus_params->height);
        return EINVAL;
    }

    alloc_params.params.width = argus_params->width;
    alloc_params.params.height = argus_params->height;
    alloc_params.params.layout = NVBUF_LAYOUT_PITCH;
    alloc_params.params.colorFormat = NVBUF_COLOR_FORMAT_NV12;
    alloc_params.params.memType = NVBUF_MEM_SURFACE_ARRAY;
    alloc_params.memtag = NvBufSurfaceTag_CAMERA;

    ret = NvBufSurfaceAllocate(&nvbuf_surf, 1, &alloc_params);
    if (ret != 0)
    {
        ERROR_PRINT("CAM_CTX(%p) Unable to allocate HW buffer structure\n", ctx);
        return ENOMEM;
    }
    nvbuf_surf->numFilled = 1;
    NvBufSurfaceParams *surface_params = &nvbuf_surf->surfaceList[0];
    NvBufSurfacePlaneParams *pPlanes = &surface_params->planeParams;

    /* Determine the size of all planes. DMABUF require no mem_offsets information.*/
    for (uint32_t j = 0; j < pPlanes->num_planes; j++)
    {
        psizes[j] = pPlanes->psize[j];
    }

    NvBufSurfaceDestroy(nvbuf_surf);
    nvbuf_surf = NULL;

    return 0;
}

int32_t nvargus_enqueue_instream_buffers_from_capture_inQ(v4l2_camera_context *ctx, int32_t buffer_idx)
{
    DBG_PRINT("CAM_CTX(%p): %s : Enter\n", ctx, __func__);

    if (ctx->m_cameraStatus == Argus::STATUS_OK)
    {
        if (ReleaseStreamBuffer(ctx, buffer_idx) != 0)
        {
            REL_PRINT("CAM_CTX(%p) Error in releasing buffer Argus\n", ctx);
            v4l2_cam_atomic_write_bool(ctx, &ctx->error_flag, true);
            return EINVAL;
        }

        ctx->num_queued_capture_buffers++;
    }
    else
    {
        REL_PRINT("CAM_CTX(%p) Argus in error : %s\n", ctx, getStatusString(ctx->m_cameraStatus));
        v4l2_cam_atomic_write_bool(ctx, &ctx->error_flag, true);
        return EINVAL;
    }

    DBG_PRINT("CAM_CTX(%p): %s : Buffer successfully enqueued\n", ctx, __func__);
    return 0;
}

static int32_t nvargus_copy_outstream_buffers_to_capplane_Q(v4l2_camera_context *ctx)
{
    q_entry cap_entry;
    uint32_t index = 0;
    argusframe_metadata *frame_metadata = NULL;
    frame_metadata = (argusframe_metadata *)malloc(sizeof(argusframe_metadata));
    if (!frame_metadata)
    {
        REL_PRINT("Unable to allocate memory for Metadata\n");
    }

    DBG_PRINT("CAM_CTX(%p): %s: Enter\n", ctx, __func__);

    if (NULL == ctx)
        return 0;

    while (ctx->m_cameraStatus == Argus::STATUS_OK)
    {
        memset((void *)frame_metadata, 0x0, sizeof(struct argusframe_metadata));
        memset(&cap_entry, 0, sizeof(q_entry));

        /* Two seconds timeout to wait for an event. Core-SCF fix in Bug 200636333 */
        ctx->m_iEventProvider->waitForEvents(ctx->m_eventQueue.get(), TIMEOUT_FOUR_SECONDS);
        if (ctx->m_iEventQueue->getSize() == 0)
        {
            if (ctx->stream_on)
            {
                REL_PRINT("CAM_CTX(%p) %s: Timeout. No events queued\n", ctx, __func__);
                v4l2_cam_atomic_write_bool(ctx, &ctx->camera_state_stopped, true);
                NvSemaphoreSignal(ctx->acquirebuf_sema);
            }
            if (frame_metadata)
                free(frame_metadata);
            return 0;
        }

        Argus::Event *event =
            (Argus::Event *)(ctx->m_iEventQueue->getEvent(ctx->m_iEventQueue->getSize() - 1));
        Argus::IEvent *iEvent = (Argus::IEvent *)Argus::interface_cast<const Argus::IEvent>(event);
        if (!iEvent)
        {
            v4l2_cam_atomic_write_bool(ctx, &ctx->error_flag, true);
            if (frame_metadata)
                free(frame_metadata);
            return EINVAL;
        }

        if (iEvent->getEventType() == Argus::EVENT_TYPE_ERROR)
        {
            if (v4l2_cam_atomic_read_bool(ctx, &ctx->camera_state_stopped))
            {
                if (frame_metadata)
                    free(frame_metadata);
                return 0;
            }

            const Argus::IEventError* iEventError = Argus::interface_cast<const Argus::IEventError>(event);
            ctx->m_cameraStatus = iEventError->getStatus();
            ERROR_PRINT("CAM_CTX(%p) ERROR Generated. ARGUS Status : %s\n",
                    ctx, getStatusString(ctx->m_cameraStatus));
            cap_entry.size = 0;
            cap_entry.index = index;
            cap_entry.fd = 0;
            if (NvQueueEnQ(ctx->capplane_Q, &cap_entry) != 0)
            {
                REL_PRINT("CAM_CTX(%p) Error while enqueuing into captureplane_outQ\n", ctx);
                v4l2_cam_atomic_write_bool(ctx, &ctx->error_flag, true);
            }
            v4l2_cam_atomic_write_bool(ctx, &ctx->camera_state_stopped, true);
            v4l2_cam_atomic_write_bool(ctx, &ctx->error_flag, true);
            NvSemaphoreSignal(ctx->acquirebuf_sema);
            if (frame_metadata)
                free(frame_metadata);
            return EINVAL;
        }

        /* We only check for EVENT_TYPE_ERROR event and proceed with acquireBuffer*/
        Argus::Buffer *argus_buffer = AcquireStreamBuffer(ctx, TIMEOUT_TWO_SECONDS);
        if (!argus_buffer)
        {
            REL_PRINT("CAM_CTX(%p) %s : NULL Stream Buffer\n", ctx, __func__);
            NvSemaphoreSignal(ctx->acquirebuf_sema);
            if (frame_metadata)
                free(frame_metadata);
            return EINVAL;
        }

        auto argus_iBuffer = Argus::interface_cast<Argus::IBuffer>(argus_buffer);
        const camerav4l2_buffer *data_buffer =
            static_cast<const camerav4l2_buffer *>(argus_iBuffer->getClientData());
        if (!data_buffer)
        {
            REL_PRINT("CAM_CTX(%p) %s : NULL NvBuffer\n", ctx, __func__);
            NvSemaphoreSignal(ctx->acquirebuf_sema);
            if (frame_metadata)
                free(frame_metadata);
            return EINVAL;
        }

        index = data_buffer->buffer_id;

        if (!(ctx->cap_buffers[data_buffer->buffer_id].flags &
              V4L2_BUF_FLAG_QUEUED))
        {
            REL_PRINT("CAM_CTX(%p) Acquired buffer not queued in stream\n", ctx);
            NvSemaphoreSignal(ctx->acquirebuf_sema);
            if (frame_metadata)
             free(frame_metadata);
            return EINVAL;
        }

        if (index >= ctx->capture_buffer_count)
        {
            REL_PRINT("CAM_CTX(%p) Received wrong index in the capture_entry %d \n", ctx, index);
            NvSemaphoreSignal(ctx->acquirebuf_sema);
            if (frame_metadata)
                free(frame_metadata);
            return EINVAL;
        }

        /* Get Buffer metadata */
        ctx->m_captureMetadata = argus_iBuffer->getMetadata();
        const Argus::ICaptureMetadata *iMetadata =
            Argus::interface_cast<const Argus::ICaptureMetadata>(ctx->m_captureMetadata);

        if (!iMetadata)
        {
            REL_PRINT("CAM_CTX(%p) Failed to get Capture Metadata for buffer idx %d\n",
                      ctx, data_buffer->buffer_id);
            NvSemaphoreSignal(ctx->acquirebuf_sema);
            if (frame_metadata)
                free(frame_metadata);
            return EINVAL;
        }

        if (frame_metadata)
        {
            frame_metadata->id = iMetadata->getCaptureId();
            frame_metadata->aeLocked = iMetadata->getAeLocked();
            frame_metadata->aeState = iMetadata->getAeState();
            frame_metadata->focuserPosition = iMetadata->getFocuserPosition();
            frame_metadata->awbCct = iMetadata->getAwbCct();
            frame_metadata->awbState = iMetadata->getAwbState();
            frame_metadata->colorCorrectionMatrixEnable = iMetadata->getColorCorrectionMatrixEnable();
            frame_metadata->frameDuration = iMetadata->getFrameDuration();
            frame_metadata->ispDigitalGain = iMetadata->getIspDigitalGain();
            frame_metadata->frameReadoutTime = iMetadata->getFrameReadoutTime();
            frame_metadata->sceneLux = iMetadata->getSceneLux();
            frame_metadata->sensorAnalogGain = iMetadata->getSensorAnalogGain();
            frame_metadata->sensorExposureTime = iMetadata->getSensorExposureTime();
            frame_metadata->sensorSensitivity = iMetadata->getSensorSensitivity();

            if (ctx->frame_metadata[index] == NULL)
            {
                ctx->frame_metadata[index] = (argusframe_metadata *)malloc(sizeof(argusframe_metadata));
                memset((void *)ctx->frame_metadata[index], 0x0, sizeof(struct argusframe_metadata));
            }

            *ctx->frame_metadata[index] = *frame_metadata;

            DBG_PRINT("CAM_CTX(%p): Metadata ID %d AeLock %d FrameDuration %ld "
                "DigitalGainRange %f SceneLux %f SensorAnalogGain %f SensorExpTime %ld\n",
                ctx, frame_metadata->id, frame_metadata->aeLocked,
                frame_metadata->frameDuration, frame_metadata->ispDigitalGain,
                frame_metadata->sceneLux, frame_metadata->sensorAnalogGain,
                frame_metadata->sensorExposureTime);
        }
        cap_entry.size = data_buffer->surf_params.planeParams.psize[0];
        cap_entry.index = index;
        cap_entry.fd = data_buffer->buf_fd;
        static const uint64_t ground_clk = iMetadata->getSensorTimestamp();
        cap_entry.timestamp = iMetadata->getSensorTimestamp() - ground_clk;

        DBG_PRINT("CAM_CTX(%p): %s : capplane_Q index = %d Timestamp sec %ld usec %ld\n",
                ctx, __func__, cap_entry.index,
        (uint64_t)(cap_entry.timestamp / (1000000)), (uint64_t)(cap_entry.timestamp % (1000000)));

        if (NvQueueEnQ(ctx->capplane_Q, &cap_entry) != 0)
        {
            REL_PRINT("CAM_CTX(%p) Error while enqueuing into captureplane_outQ\n", ctx);
            NvSemaphoreSignal(ctx->acquirebuf_sema);
            free(frame_metadata);
            return EINVAL;
        }
        NvSemaphoreSignal(ctx->acquirebuf_sema);
        break;

    }

    if (frame_metadata)
        free(frame_metadata);
    return 0;
}

void argus_capture_thread_func(void *args)
{
    v4l2_camera_context *ctx = (v4l2_camera_context *)args;
    DBG_PRINT("CAM_CTX(%p): argus_capture_thread created\n", ctx);

    while (!v4l2_cam_atomic_read_bool(ctx, &ctx->camera_state_stopped))
    {
        NvMutexAcquire(ctx->context_mutex);

        if (v4l2_cam_atomic_read_bool(ctx, &ctx->error_flag))
        {
            REL_PRINT("CAM_CTX(%p) Error encountered. Exiting capture\n", ctx);
            NvSemaphoreSignal(ctx->acquirebuf_sema);
            break;
        }
        if (ctx->stream_on)
        {
            nvargus_copy_outstream_buffers_to_capplane_Q(ctx);
            // Check for polling
        }

        NvMutexRelease(ctx->context_mutex);
    }
    DBG_PRINT("CAM_CTX(%p): Exiting from argus_capture_thread\n", ctx);
}
