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

/* This file implements the context related interface methods.*/

#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "linux/videodev2.h"
#include "nvargusv4l2_os.h"
#include "nvargusv4l2.h"
#include "nvargusv4l2_context.h"
#include "nvargusv4l2_ioctl.h"

static v4l2_context *contexts[MAX_CONTEXTS];
static int32_t  next_free_index;
static int32_t  total_open_instances;
extern NvMutexHandle global_mutex;

Argus::CameraProvider *g_cameraProvider = NULL;
Argus::ICameraProvider *g_iCameraProvider = NULL;
std::vector<Argus::CameraDevice*> g_cameraDevices;          // All devices (sensors)

v4l2_context* v4l2_get_context(int32_t fd)
{
    if (contexts[fd] == NULL)
    {
        REL_PRINT("NULL context on given FD\n");
        return NULL;
    }
    return contexts[fd];
}

int32_t initialize_camera_provider()
{
    if (!g_cameraProvider)
    {
        g_cameraProvider = Argus::CameraProvider::create();
        if (!g_cameraProvider)
        {
            REL_PRINT("Failed to create Argus CameraProvider\n");
            return -1;
        }
    }
    /* Get the core interface. */
    g_iCameraProvider = Argus::interface_cast<Argus::ICameraProvider>(
            g_cameraProvider);
    if (!g_iCameraProvider)
    {
        REL_PRINT("Argus does not support core camera interface\n");
        return -1;
    }

    /* Get the list of CameraDevices to test valid execution. */
    if (g_iCameraProvider->getCameraDevices(&g_cameraDevices) != Argus::STATUS_OK)
    {
        REL_PRINT("Failed to get camera devices\n");
        return -1;
    }

    if (g_cameraDevices.size() == 0)
        return -1;

    return 0;
}

void v4l2_argus_fill_sensor_modes(std::vector<Argus::SensorMode*> sensorModes,
        std::vector<argusv4l2_sensormode> *argusv4l2_sensormodes)
{
    printf("Available Sensor modes :\n");
    for (uint32_t idx = 0; idx < sensorModes.size(); ++idx)
    {
        Argus::ISensorMode *iSensorMode = NULL;
        argusv4l2_sensormode argusv4l2_smidx;
        iSensorMode = Argus::interface_cast<Argus::ISensorMode>(sensorModes[idx]);
        argusv4l2_smidx.min_width = FRAMESIZE_MIN_WIDTH;
        argusv4l2_smidx.min_height = FRAMESIZE_MIN_HEIGHT;
        argusv4l2_smidx.max_width = iSensorMode->getResolution().width();
        argusv4l2_smidx.max_height = iSensorMode->getResolution().height();
        argusv4l2_smidx.frame_duration = iSensorMode->getFrameDurationRange().min();
        argusv4l2_smidx.bitdepth = iSensorMode->getInputBitDepth();
        argusv4l2_smidx.minGainRange = iSensorMode->getAnalogGainRange().min();
        argusv4l2_smidx.maxGainRange = iSensorMode->getAnalogGainRange().max();
        argusv4l2_smidx.minExposureTimeRange = iSensorMode->getExposureTimeRange().min();
        argusv4l2_smidx.maxExposureTimeRange = iSensorMode->getExposureTimeRange().max();
        argusv4l2_sensormodes->push_back(argusv4l2_smidx);
        printf("Resolution: %d x %d ; Framerate = %f; Analog Gain Range Min %f, Max %f, "
            "Exposure Range Min %lu, Max %lu\n\n", iSensorMode->getResolution().width(),
            iSensorMode->getResolution().height(), (1e9/iSensorMode->getFrameDurationRange().min()),
            iSensorMode->getAnalogGainRange().min(), iSensorMode->getAnalogGainRange().max(),
            iSensorMode->getExposureTimeRange().min(), iSensorMode->getExposureTimeRange().max());
    }
}

void v4l2_argus_set_defaults(cam_params *argus_defaults, cam_settings *argus_settings)
{
    /* Argus Defaults */
    argus_defaults->width = 640;
    argus_defaults->height = 480;
    argus_defaults->pixelformat = V4L2_PIX_FMT_NV12M;
    argus_defaults->argus_format = Argus::PIXEL_FMT_YCbCr_420_888;
    argus_defaults->bitdepth = 8;
    argus_settings->sensorModeIdx = -1;
    argus_settings->frameDuration = 1e9/30;
    argus_settings->awbLock = false;
    argus_settings->aeLock = false;
    argus_settings->aeAntibandingMode = V4L2_ARGUS_AE_ANTIBANDING_MODE_AUTO;
    argus_settings->awbMode = V4L2_ARGUS_AWB_MODE_AUTO;
    argus_settings->edgeEnhanceMode = V4L2_ARGUS_EDGE_ENHANCE_MODE_FAST;
    argus_settings->edgeEnhanceStrength = -1.0;
    argus_settings->denoiseMode = V4L2_ARGUS_DENOISE_MODE_FAST;
    argus_settings->denoiseStrength = -1.0;
    argus_settings->exposureCompensation = 0.0;
    argus_settings->ispDigitalGainRange = Argus::Range<float>(1.0, 256.0);
    argus_settings->enableColorSaturation = true;
    argus_settings->colorSaturation = 1.0;
    argus_settings->gainRange = Argus::Range<float>(1.0, 16.0);
}

// This call checks if we can register the plugin under Argus
int32_t initialize_arguscamera(v4l2_camera_context *ctx, uint32_t camera_index)
{
    /* Create the CameraProvider. */
    if (initialize_camera_provider() != 0)
    {
        REL_PRINT("Failed to initialize camera devices\n");
        goto error;
    }

    ctx->m_captureSession = Argus::UniqueObj<Argus::CaptureSession>(
        g_iCameraProvider->createCaptureSession(g_cameraDevices[camera_index]));
    ctx->m_iCaptureSession =
        Argus::interface_cast<Argus::ICaptureSession>(ctx->m_captureSession);

    if (!ctx->m_iCaptureSession)
    {
        REL_PRINT("Failed to get ICaptureSession interface\n");
        goto error;
    }
    ctx->m_iEventProvider =
        Argus::interface_cast<Argus::IEventProvider>(ctx->m_captureSession);
    if (!ctx->m_iEventProvider)
    {
        REL_PRINT("Failed to get IEventProvider interface\n");
        goto error;
    }
    /* Argus drops EVENT_TYPE_ERROR if all 3 events are not subscribed. Setting all for now */
    ctx->m_eventTypes.push_back(Argus::EVENT_TYPE_CAPTURE_COMPLETE);
    ctx->m_eventTypes.push_back(Argus::EVENT_TYPE_CAPTURE_STARTED);
    ctx->m_eventTypes.push_back(Argus::EVENT_TYPE_ERROR);

    ctx->m_eventQueue = Argus::UniqueObj<Argus::EventQueue>(
        ctx->m_iEventProvider->createEventQueue(ctx->m_eventTypes));
    ctx->m_iEventQueue = Argus::interface_cast<Argus::IEventQueue>(ctx->m_eventQueue);
    if (!ctx->m_iEventQueue)
    {
        REL_PRINT("Failed to get EventQueue interface\n");
        goto error;
    }

    ctx->m_iCameraProperties = Argus::interface_cast<Argus::ICameraProperties>(
        g_cameraDevices[camera_index]);
    if (!ctx->m_iCameraProperties)
    {
        REL_PRINT("Failed to create camera properties\n");
        goto error;
    }
    ctx->m_iCameraProperties->getAllSensorModes(&ctx->m_sensorModes);
    if (ctx->m_sensorModes.size() == 0)
    {
        REL_PRINT("No Sensor Mode found associated with the camera node\n");
        goto error;
    }

    v4l2_argus_fill_sensor_modes(ctx->m_sensorModes, &ctx->argusv4l2_sensormodes);
    v4l2_argus_set_defaults(&ctx->argus_params, &ctx->argus_settings);

    return 0;
error:
    if (g_cameraProvider)
        g_cameraProvider->destroy();
    if (ctx->m_captureSession)
        ctx->m_captureSession.reset();
    if (ctx->m_eventQueue)
        ctx->m_eventQueue.reset();
    g_cameraDevices.clear();
    ctx->m_sensorModes.clear();
    ctx->m_eventTypes.clear();
    g_iCameraProvider      = NULL;
    ctx->m_iCaptureSession      = NULL;
    ctx->m_iEventProvider       = NULL;
    ctx->m_iEventQueue          = NULL;
    ctx->m_iCameraProperties    = NULL;
    return -1;
}

int32_t v4l2_open_camera_context(uint32_t camera_index, int32_t flags)
{
    int32_t index = 0, loop_cnt = 0;
    v4l2_context *ctx = NULL;
    v4l2_camera_context *camera_ctx = NULL;

    v4l2_lock_global_mutex();
    index = next_free_index;
    for (loop_cnt = 0; loop_cnt < MAX_CONTEXTS; ++loop_cnt)
    {
        if (contexts[index] == NULL)
        {
            contexts[index] = (v4l2_context *)malloc(sizeof(v4l2_context));
            ctx = contexts[index];
            memset(contexts[index], 0x0, sizeof(v4l2_context));
            REL_PRINT("Allocated ctx %p at idx %d\n", contexts[index], index);

            if (NvMutexCreate(&ctx->ioctl_mutex) != 0)
            {
                REL_PRINT("Error in creating ioctl_mutex\n");
                free(contexts[index]);
                goto ctx_error;
            }

            camera_ctx = (v4l2_camera_context *)malloc(sizeof(v4l2_camera_context));
            if (camera_ctx == NULL)
            {
                REL_PRINT("Error allocating argus context\n");
                free(contexts[index]);
                goto ctx_error;
            }
            memset((void*)camera_ctx, 0x0, sizeof(v4l2_camera_context));

            if (initialize_arguscamera(camera_ctx, camera_index) < 0)
            {
                REL_PRINT("Failed to initialize camera\n");
                free(camera_ctx);
                free(contexts[index]);
                goto ctx_error;
            }

            ctx->actual_context = (void *)camera_ctx;
            total_open_instances++;

            if (NvMutexCreate(&camera_ctx->context_mutex) != 0)
            {
                REL_PRINT("Error creating context mutex\n");
                free(camera_ctx);
                free(contexts[index]);
                goto ctx_error;
            }
            camera_ctx->blocking_mode = ! (flags & O_NONBLOCK);
            next_free_index = index + 1;
            if (next_free_index == 128)
                next_free_index = 0;
            v4l2_unlock_global_mutex();
            return index;
        }
        index++;
        if (index == MAX_CONTEXTS)
            index = 0;
    }
    REL_PRINT("No free index left , something is wrong \n");
ctx_error:
    v4l2_unlock_global_mutex();
    return -1;
}

void v4l2_close_camera_context(int32_t fd)
{
    v4l2_context *ctx = v4l2_get_context(fd);

    REL_PRINT("CAM_CTX(%p) Closing the context %s\n", ctx->actual_context, __func__);
    /* Close the camera context */
    NvMutexAcquire(ctx->ioctl_mutex);

    v4l2_lock_global_mutex();
    total_open_instances--;
    v4l2_close_argus_context(ctx);

    NvMutexRelease(ctx->ioctl_mutex);
    NvMutexDestroy(ctx->ioctl_mutex);
    free(ctx);
    contexts[fd] = NULL;
    if (total_open_instances == 0)
    {
        if (g_cameraProvider)
            g_cameraProvider->destroy();
        g_cameraDevices.clear();
        g_iCameraProvider           = NULL;
    }
    REL_PRINT("Total Opened instances: %d\n", total_open_instances);
    v4l2_unlock_global_mutex();
}

void v4l2_lock_global_mutex(void)
{
    assert(global_mutex != NULL);
    NvMutexAcquire(global_mutex);
}

void v4l2_unlock_global_mutex(void)
{
    assert(global_mutex != NULL);
    NvMutexRelease(global_mutex);
}
