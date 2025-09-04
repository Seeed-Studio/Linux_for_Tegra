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
#include "nvbufsurface.h"
#include "nvargusv4l2_ioctl.h"

#define NVARGUS_CARD_TYPE "NvV4L2 Argus PLugin"
#define NVARGUS_BUS_INFO "NV-ARGUS"
#define NVARGUS_VERSION 1.0

static struct argusv4l2_fmt_struct capture_formats[] = {
    {
        "YUV 4:2:0",
        Argus::PIXEL_FMT_YCbCr_420_888,
        V4L2_PIX_FMT_NV12M,
        2,
        8,
    },
    {
        "",
        Argus::PIXEL_FMT_UNKNOWN,
        0,
        0,
        0,
    },
};

/*To get the v4l2_camera context*/
static v4l2_camera_context* get_v4l2_context_from_fd(int32_t fd)
{
    v4l2_context *v4l2_ctx = v4l2_get_context(fd);
    if (v4l2_ctx)
    {
        v4l2_camera_context *ctx = (v4l2_camera_context *)v4l2_ctx->actual_context;
        if (NULL == ctx)
            return NULL;
        else
            return ctx;
    }
    else
        return NULL;
}

/* To start the argusv4l2 capture thread */
static int32_t argus_capture_thread_start(v4l2_camera_context *ctx)
{
    int32_t error = 0;
    if (!ctx->argus_capture_thread)
    {
        error =
            NvThreadCreate((NvThreadFunction)argus_capture_thread_func, ctx, &ctx->argus_capture_thread);
        if (error)
        {
            REL_PRINT("CAM_CTX(%p) Failed to create Argus capture thread\n", ctx);
            return error;
        }
        NvThreadSetName(ctx->argus_capture_thread, "V4L2_CapThread");
    }
    return error;
}

/* To stop the argusv4l2 thread */
static void argus_capture_thread_shutdown(v4l2_camera_context *ctx)
{
    if (ctx->argus_capture_thread)
    {
        REL_PRINT("CAM_CTX(%p) %s: Wait on join to thread argus_capture_thread_func\n",
                ctx, __func__);
        NvThreadJoin(ctx->argus_capture_thread);
        ctx->argus_capture_thread = NULL;
    }
}

int32_t vidioc_cam_querycap(int32_t fd, struct v4l2_capability *caps)
{
    memset(caps, 0x0, sizeof(struct v4l2_capability));

    caps->device_caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_STREAMING;
    caps->device_caps |= V4L2_CAP_EXT_PIX_FORMAT;
    caps->capabilities = caps->device_caps | V4L2_CAP_DEVICE_CAPS;

    memcpy((char*)caps->driver, g_iCameraProvider->getVersion().c_str(),
                   (strnlen(g_iCameraProvider->getVersion().c_str(), (sizeof(caps->driver) - 1)) + 1));

    strncpy((char*)caps->card, NVARGUS_CARD_TYPE, sizeof(caps->card));
    snprintf((char*)caps->bus_info, sizeof(caps->bus_info), "platform:%s:%f", NVARGUS_BUS_INFO, NVARGUS_VERSION);
    return 0;
}

int32_t vidioc_cam_enum_fmt(int32_t fd, struct v4l2_fmtdesc *fmt)
{
    uint32_t i;
    for (i = 0; ; ++i)
    {
        if (capture_formats[i].fourcc == 0)
            return EINVAL;
        if (i == fmt->index)
        {
            fmt->pixelformat = capture_formats[i].fourcc;
            memcpy((char *)fmt->description, capture_formats[i].name,
                   (strnlen(capture_formats[i].name, (sizeof(fmt->description) - 1)) + 1));

            return 0;
        }
    }
    return 0;
}

int32_t vidioc_cam_enum_framesizes(int32_t fd, struct v4l2_frmsizeenum *framesizes)
{
    v4l2_camera_context *ctx = get_v4l2_context_from_fd(fd);
    if (NULL == ctx)
        return EINVAL;

    std::vector<argusv4l2_sensormode> argusv4l2_sensormode = ctx->argusv4l2_sensormodes;

    uint32_t sensormode_size = argusv4l2_sensormode.size();
    if (framesizes->index >= sensormode_size)
        return EINVAL;

    for (uint32_t i = 0; ; ++i)
    {
        if (capture_formats[i].fourcc == 0)
            return EINVAL;
        if (capture_formats[i].fourcc == framesizes->pixel_format)
        {
            for (uint32_t j = 0; j < sensormode_size; ++j)
            {
                if (j == framesizes->index)
                {
                    framesizes->type = V4L2_FRMSIZE_TYPE_STEPWISE;
                    framesizes->stepwise.min_width = argusv4l2_sensormode[j].min_width;
                    framesizes->stepwise.min_height = argusv4l2_sensormode[j].min_height;
                    framesizes->stepwise.max_width = argusv4l2_sensormode[j].max_width;
                    framesizes->stepwise.max_height = argusv4l2_sensormode[j].max_height;
                    framesizes->stepwise.step_width = 1;
                    framesizes->stepwise.step_height = 1;
                    return 0;
                }
            }
        }
    }

    return 0;
}

int32_t vidioc_cam_enum_frameintervals(int32_t fd, struct v4l2_frmivalenum *frameival)
{
    v4l2_camera_context *ctx = get_v4l2_context_from_fd(fd);
    if (NULL == ctx)
        return EINVAL;

    std::vector<argusv4l2_sensormode> argusv4l2_sensormode = ctx->argusv4l2_sensormodes;

    uint32_t sensormode_size = argusv4l2_sensormode.size();
    if (frameival->index >= sensormode_size)
        return EINVAL;

    for (uint32_t i = 0; ; ++i)
    {
        if (capture_formats[i].fourcc == 0)
            return EINVAL;
        if (capture_formats[i].fourcc == frameival->pixel_format)
        {
            for (uint32_t j = 0; j < sensormode_size; ++j)
            {
                if (argusv4l2_sensormode[j].max_width == frameival->width &&
                    argusv4l2_sensormode[j].max_height == frameival->height)
                {
                    frameival->type = V4L2_FRMIVAL_TYPE_STEPWISE;
                    frameival->stepwise.min.numerator = 1;
                    frameival->stepwise.min.denominator = 1;
                    frameival->stepwise.max.numerator = argusv4l2_sensormode[j].frame_duration;
                    frameival->stepwise.max.denominator = 1e9;
                    frameival->stepwise.step.numerator = 1;
                    frameival->stepwise.step.denominator = 1;
                    return 0;
                }
            }
        }
    }
    return 0;
}

int32_t vidioc_cam_g_fmt(int32_t fd, struct v4l2_format *format)
{
    v4l2_camera_context *ctx = get_v4l2_context_from_fd(fd);
    if (NULL == ctx || NULL == format)
        return EINVAL;

    DBG_PRINT("CAM_CTX(%p): Getting format\n", ctx);
    V4L2_BUFFER_TYPE_SUPPORTED_OR_ERROR(format->type);

    NvMutexAcquire(ctx->context_mutex);
    format->fmt.pix_mp.width = ctx->argus_params.width;
    format->fmt.pix_mp.height = ctx->argus_params.height;
    format->fmt.pix_mp.pixelformat = ctx->argus_params.pixelformat;
    format->fmt.pix_mp.num_planes = 2;
    format->fmt.pix_mp.field = V4L2_FIELD_NONE;
    format->fmt.pix_mp.plane_fmt[0].sizeimage =
        ctx->argus_params.width * ctx->argus_params.height;
    format->fmt.pix_mp.plane_fmt[1].sizeimage =
        (ctx->argus_params.width * ctx->argus_params.height) / 2;
    format->fmt.pix_mp.plane_fmt[0].bytesperline =
        format->fmt.pix_mp.plane_fmt[1].bytesperline = ctx->argus_params.width;

    REL_PRINT("CAM_CTX(%p) VIDIOC_G_FMT returning width %d height %d\n", ctx, format->fmt.pix_mp.width,
            format->fmt.pix_mp.height);
    NvMutexRelease(ctx->context_mutex);
    return 0;
}

int32_t vidioc_cam_s_fmt(int32_t fd, struct v4l2_format *format)
{
    uint32_t i = 0;
    v4l2_camera_context *ctx = get_v4l2_context_from_fd(fd);
    if (NULL == ctx)
        return EINVAL;

    DBG_PRINT("CAM_CTX(%p): Setting format\n", ctx);
    V4L2_BUFFER_TYPE_SUPPORTED_OR_ERROR(format->type);

    /* Check for allowed format */
    for (i = 0; ; i++)
    {
        if (capture_formats[i].fourcc == 0)
        {
            REL_PRINT("CAM_CTX(%p) Cannot set specified format. Using default\n", ctx);
            i = 0;
            break;
        }
        if (capture_formats[i].fourcc == format->fmt.pix_mp.pixelformat)
        {
            REL_PRINT("CAM_CTX(%p) Found allowed pixel format\n", ctx);
            break;
        }
    }
    format->fmt.pix_mp.pixelformat = capture_formats[i].fourcc;
    format->fmt.pix_mp.num_planes = capture_formats[i].num_planes;
    ctx->argus_params.width = format->fmt.pix_mp.width;
    ctx->argus_params.height = format->fmt.pix_mp.height;
    ctx->argus_params.pixelformat = format->fmt.pix_mp.pixelformat;
    ctx->argus_params.argus_format = capture_formats[i].argus_format;
    ctx->argus_params.bitdepth = capture_formats[i].bpp;

    if (initialize_outputstream(ctx, &ctx->argus_params) < 0)
    {
        REL_PRINT("CAM_CTX(%p) Error in initializing OutputStream\n", ctx);
        return EINVAL;
    }

    /*Set the fmt on capture plane here plane-wise */
    /* Plane 0 */
    switch (ctx->argus_params.pixelformat)
    {
        case V4L2_PIX_FMT_NV12M:
        {
            format->fmt.pix_mp.plane_fmt[0].sizeimage = (MEM_ALIGN_16(format->fmt.pix_mp.width)) *
                                                        (MEM_ALIGN_16(format->fmt.pix_mp.height));
            format->fmt.pix_mp.plane_fmt[0].bytesperline = (format->fmt.pix_mp.width + ALIGN_BYTES) & ~ALIGN_BYTES;
        }
            break;
        default:
            return EINVAL;
    }

    /* Plane 1 */
    switch (ctx->argus_params.pixelformat)
    {
        case V4L2_PIX_FMT_NV12M:
        {
            format->fmt.pix_mp.plane_fmt[1].sizeimage =
            ((MEM_ALIGN_16(format->fmt.pix_mp.width) >> 1) * (MEM_ALIGN_16(format->fmt.pix_mp.height) >> 1) << 1);
            format->fmt.pix_mp.plane_fmt[1].bytesperline = (format->fmt.pix_mp.width/2 + ALIGN_BYTES) & ~ALIGN_BYTES;
        }
            break;
        default:
            break;
    }

    format->fmt.pix_mp.width = MEM_ALIGN_16(format->fmt.pix_mp.width);
    format->fmt.pix_mp.height = MEM_ALIGN_16(format->fmt.pix_mp.height);

    for (i = 0; i < format->fmt.pix_mp.num_planes; i++)
    {
        REL_PRINT("CAM_CTX(%p) Bytesperline for %d plane %d\n", ctx,
                i, format->fmt.pix_mp.plane_fmt[i].bytesperline);
    }

    return 0;
}

int32_t vidioc_cam_reqbufs(int32_t fd, struct v4l2_requestbuffers *reqbufs)
{
    NvBufSurfacePlaneParams *pSurfParams = NULL;
    uint32_t num_planes = 0;
    int32_t retval = 0;
    v4l2_camera_context *ctx = get_v4l2_context_from_fd(fd);
    if (NULL == ctx)
        return EINVAL;

    V4L2_MEMORY_TYPE_SUPPORTED_OR_ERROR(reqbufs->memory);
    V4L2_BUFFER_TYPE_SUPPORTED_OR_ERROR(reqbufs->type);
    REL_PRINT("CAM_CTX(%p) Request %d buffers for capture plane\n", ctx, reqbufs->count);

    NvMutexAcquire(ctx->context_mutex);
    if (reqbufs->count == 0)
    {
        if ((ctx->stream_on) &&
            !(v4l2_cam_atomic_read_bool(ctx, &ctx->camera_state_stopped)))
        {
            REL_PRINT("CAM_CTX(%p) STREAMON and called REQBUFS\n", ctx);
            NvMutexRelease(ctx->context_mutex);
            return EINVAL;
        }
        REL_PRINT("CAM_CTX(%p) Releasing capture buffers\n", ctx);
        free_cap_buffers(ctx);
        NvMutexRelease(ctx->context_mutex);
        return 0;
    }
    /*Reqbuffer count for the stream */
    if (reqbufs->count > MAX_OP_BUFFERS)
    {
        reqbufs->count = MAX_OP_BUFFERS;
    }
    if (reqbufs->count < MIN_OP_BUFFERS)
    {
        reqbufs->count = MIN_OP_BUFFERS;
    }
    ctx->capture_buffer_count = reqbufs->count;
    ctx->capture_memory_type = reqbufs->memory;

    /* Right now, the supported format is PIX_FMT_NV12M */
    if (ctx->argus_params.pixelformat == V4L2_PIX_FMT_NV12M)
        num_planes = 2;

    retval = allocate_all_capture_queues (ctx);
    if (retval)
    {
        NvMutexRelease(ctx->context_mutex);
        return retval;
    }

    if (ctx->capture_memory_type != V4L2_MEMORY_DMABUF)
    {
        /* Allocate input buffers */
        retval = allocate_capture_buffers(ctx, &ctx->argus_params);
        if (retval)
        {
            NvMutexRelease(ctx->context_mutex);
            return retval;
        }
        for (uint32_t i = 0; i < reqbufs->count; i++)
        {
            pSurfParams = &ctx->cap_buffers[i].surf_params.planeParams;
            for (uint32_t plane = 0; plane < num_planes; plane++)
            {
                NvBufSurface *nvbuf_surf = 0;
                retval = NvBufSurfaceFromFd((int)ctx->cap_buffers[i].buf_fd,
                                            (void **)(&nvbuf_surf));
                if (retval != 0)
                    REL_PRINT("CAM_CTX(%p) Failed to Get NvBufSurface from FD\n", ctx);

                retval = NvBufSurfaceMap(nvbuf_surf, 0, plane, NVBUF_MAP_READ_WRITE);
                if (retval != 0)
                    REL_PRINT("CAM_CTX(%p) Failed to map NvBufSurface\n", ctx);
                ctx->inbuf_mapped_address[i][plane] = (void *)nvbuf_surf->surfaceList[0].mappedAddr.addr[plane];

                DBG_PRINT("CAM_CTX(%p): NvRmMemMapped bytes %d surface_number %d plane %d address %p \n",
                          ctx, (pSurfParams->pitch[plane] * pSurfParams->height[plane]),
                          i, plane, ctx->inbuf_mapped_address[i][plane]);
            }
        }
    }
    else
        REL_PRINT("CAM_CTX(%p) Requested DMABUF memory, queues are created\n", ctx);

    NvMutexRelease(ctx->context_mutex);
    return 0;
}

int32_t vidioc_cam_streamon(int32_t fd, unsigned int *type)
{
    int32_t err_status = 0;
    v4l2_camera_context *ctx = get_v4l2_context_from_fd(fd);
    if (NULL == ctx)
        return EINVAL;

    REL_PRINT("CAM_CTX(%p) STREAM ON\n", ctx);

    V4L2_BUFFER_TYPE_SUPPORTED_OR_ERROR(*type);

    NvMutexAcquire(ctx->context_mutex);
    if (ctx->stream_on)
    {
        NvMutexRelease(ctx->context_mutex);
        return 0;
    }

    ctx->stream_on = true;
    v4l2_cam_atomic_write_bool(ctx, &ctx->camera_state_stopped, false);

    err_status = argus_capture_thread_start(ctx);
    if (err_status)
    {
        REL_PRINT("CAM_CTX(%p) Failed to create thread\n", ctx);
        NvMutexRelease(ctx->context_mutex);
        return EINVAL;
    }

    /* Starting capture requests. Nothing happens since no buffers are available */
    if (ctx->m_iCaptureSession->repeat(ctx->m_request.get()) != Argus::STATUS_OK)
    {
        REL_PRINT("CAM_CTX(%p) Failed to start repeat capture request\n", ctx);
        NvMutexRelease(ctx->context_mutex);
        return EINVAL;
    }
    NvMutexRelease(ctx->context_mutex);

    return 0;
}

int32_t vidioc_cam_streamoff(int32_t fd, unsigned int *type)
{
    q_entry entry = {0};
    v4l2_camera_context *ctx = get_v4l2_context_from_fd(fd);
    if (NULL == ctx)
        return EINVAL;

    REL_PRINT("CAM_CTX(%p) STREAM OFF\n", ctx);

    V4L2_BUFFER_TYPE_SUPPORTED_OR_ERROR(*type);

    NvMutexAcquire(ctx->context_mutex);
    if (!ctx->stream_on)
    {
        v4l2_cam_atomic_write_bool(ctx, &ctx->camera_state_stopped, true);
        argus_capture_thread_shutdown(ctx);
        NvMutexRelease(ctx->context_mutex);
        return 0;
    }

    ctx->stream_on = false;
    v4l2_cam_atomic_write_bool(ctx, &ctx->camera_state_stopped, true);
    NvSemaphoreSignal(ctx->acquirebuf_sema);

    /* Cancel all pending requests and signal eos to the BufferOutputStream */
    ctx->m_iCaptureSession->cancelRequests();
    ctx->m_iStream->endOfStream();

    /* Try to release all buffers */
    while (NvQueueGetNumEntries(ctx->capplane_Q))
    {
        if (NvQueueDeQ(ctx->capplane_Q, &entry) != 0)
        {
            REL_PRINT("CAM_CTX(%p) Error while dequeuing capplane_Q after stream off, entries %d\n",
                    ctx, NvQueueGetNumEntries(ctx->capplane_Q));
            NvMutexRelease(ctx->context_mutex);
            return EINVAL;
        }
    }

    for (uint32_t i = 0; i < ctx->capture_buffer_count; i++)
    {
        ctx->cap_buffers[i].flags = 0;
    }
    NvMutexRelease(ctx->context_mutex);

    argus_capture_thread_shutdown(ctx);

    return 0;
}

int32_t vidioc_cam_qbuf(int32_t fd, struct v4l2_buffer *buffer)
{
    q_entry entry;
    int32_t retval = 0;
    v4l2_camera_context *ctx = get_v4l2_context_from_fd(fd);
    if (NULL == ctx)
        return EINVAL;

    V4L2_MEMORY_TYPE_SUPPORTED_OR_ERROR(buffer->memory);
    V4L2_BUFFER_TYPE_SUPPORTED_OR_ERROR(buffer->type);

    if (v4l2_cam_atomic_read_bool(ctx, &ctx->error_flag))
        return EINVAL;

    if (ctx->capture_buffer_count == 0 || ctx->capture_buffer_count <= buffer->index)
        return EINVAL;

    if (ctx->cap_buffers[buffer->index].flags & (V4L2_BUF_FLAG_QUEUED))
    {
        REL_PRINT("CAM_CTX(%p) Output Buffer at %d busy\n", ctx, buffer->index);
        return EBUSY;
    }

    memset(&entry, 0x0, sizeof (q_entry));
    entry.size = 0; // To indicate empty buffer being queued
    entry.index = buffer->index;
    entry.timestamp = buffer->timestamp.tv_sec * 1000 * 1000;
    entry.timestamp += buffer->timestamp.tv_usec;

    if (buffer->memory == V4L2_MEMORY_DMABUF)
        entry.fd = buffer->m.planes[0].m.fd;

    if (buffer->memory == V4L2_MEMORY_DMABUF)
    {
        NvBufSurface *nvbuf_surf = 0;

        if ((NvBufSurfaceFromFd(buffer->m.planes[0].m.fd, (void **)(&nvbuf_surf))) != 0)
        {
            REL_PRINT("CAM_CTX(%p) Unable to extract NvBufSurfaceFromFd\n", ctx);
            return EINVAL;
        }
        memcpy(&ctx->cap_buffers[buffer->index].surf_params,
               &nvbuf_surf->surfaceList[0], sizeof(NvBufSurfaceParams));
    }

    if (buffer->memory == V4L2_MEMORY_MMAP)
        ctx->cap_buffers[buffer->index].flags |= V4L2_BUF_FLAG_MAPPED;

    retval = nvargus_enqueue_instream_buffers_from_capture_inQ(ctx, buffer->index);
    if (retval != 0)
    {
        REL_PRINT("CAM_CTX(%p) Error in Queue\n", ctx);
        return EINVAL;
    }

    ctx->cap_buffers[buffer->index].flags |= V4L2_BUF_FLAG_QUEUED;
    ctx->cap_buffers[buffer->index].flags |= (~V4L2_BUF_FLAG_DONE);
    buffer->flags |= ctx->cap_buffers[buffer->index].flags;
    ctx->cap_buffers[buffer->index].buffer_id = buffer->index;

    REL_PRINT("CAM_CTX(%p) Capturing Q index %d\n", ctx, buffer->index);

    return 0;
}

int32_t vidioc_cam_dqbuf(int32_t fd, struct v4l2_buffer *buffer)
{
    int32_t eErr = 0;
    q_entry entry;
    v4l2_camera_context *ctx = get_v4l2_context_from_fd(fd);
    if (NULL == ctx)
        return EINVAL;

    V4L2_MEMORY_TYPE_SUPPORTED_OR_ERROR(buffer->memory);
    V4L2_BUFFER_TYPE_SUPPORTED_OR_ERROR(buffer->type);

    if (v4l2_cam_atomic_read_bool(ctx, &ctx->error_flag))
        return EINVAL;
    if (ctx->capture_buffer_count == 0)
        return EINVAL;
    if (!ctx->stream_on)
    {
        REL_PRINT("CAM_CTX(%p) DQBUF called during stream off\n", ctx);
        return EPIPE;
    }

    memset(&entry, 0x0, sizeof(q_entry));
    NvSemaphoreWait(ctx->acquirebuf_sema);
    eErr = NvQueueDeQ(ctx->capplane_Q, &entry);
    if (eErr != 0)
    {
        NvMutexAcquire(ctx->context_mutex);
        if (v4l2_cam_atomic_read_bool(ctx, &ctx->error_flag))
        {
            REL_PRINT("CAM_CTX(%p) Error occurred while DQ\n", ctx);
            NvMutexRelease(ctx->context_mutex);
            return EINVAL;
        }
        /* Check if camera stopped */
        if (v4l2_cam_atomic_read_bool(ctx, &ctx->camera_state_stopped))
        {
            REL_PRINT("CAM_CTX(%p) Aborting Capture buffers\n", ctx);
            free_cap_buffers(ctx);
            NvMutexRelease(ctx->context_mutex);
            return EINVAL;
        }
        NvMutexRelease(ctx->context_mutex);
        return EAGAIN;
    }
    buffer->index = entry.index;
    if (buffer->memory == V4L2_MEMORY_DMABUF)
        buffer->m.planes[0].m.fd = entry.fd;

    ctx->cap_buffers[buffer->index].flags &= (~V4L2_BUF_FLAG_QUEUED);
    ctx->cap_buffers[buffer->index].flags &= (V4L2_BUF_FLAG_DONE);
    buffer->flags = ctx->cap_buffers[buffer->index].flags;
    DBG_PRINT("CAM_CTX(%p): %s: DQed Buffer at index %d\n", ctx, __func__, entry.index);

    if (entry.size == 0)
    {
        buffer->bytesused = 0;
        buffer->m.planes[0].bytesused = 0;
        buffer->flags |= V4L2_BUF_FLAG_LAST;
        if (ctx->m_cameraStatus != Argus::STATUS_OK)
        {
            argus_capture_thread_shutdown(ctx);
            REL_PRINT("CAM_CTX(%p) Internal stream error\n", ctx);
            return EIO;
        }
    }
    else
    {
        /* As the buffers are already mapped, arbitrary values are set here */
        buffer->m.planes[0].bytesused = 1234;
        buffer->m.planes[1].bytesused = 1234;
        buffer->timestamp.tv_sec = entry.timestamp / (1000000);
        buffer->timestamp.tv_usec = entry.timestamp % (1000000);
    }
    REL_PRINT("CAM_CTX(%p) CAPTURE_DQ index = %d Out Timestamp sec %ld usec %ld\n",
              ctx, entry.index,
              (uint64_t)buffer->timestamp.tv_sec, (uint64_t)buffer->timestamp.tv_usec);

    REL_PRINT("CAM_CTX(%p) Buffer index= %d Dequeue successful\n", ctx, buffer->index);

    return 0;
}

int32_t vidioc_cam_querybuf(int32_t fd, v4l2_buffer *buffer)
{
    int32_t retval = 0;
    v4l2_camera_context *ctx = get_v4l2_context_from_fd(fd);
    if (NULL == ctx)
        return EINVAL;
    DBG_PRINT("CAM_CTX(%p): Querybuf for capture\n", ctx);
    V4L2_MEMORY_TYPE_SUPPORTED_OR_ERROR(buffer->memory);
    V4L2_BUFFER_TYPE_SUPPORTED_OR_ERROR(buffer->type);

    if (ctx->capture_memory_type != V4L2_MEMORY_DMABUF)
    {
        NvBufSurfacePlaneParams *plane_ptr =
            &ctx->cap_buffers[buffer->index].surf_params.planeParams;
        if ((0 == ctx->capture_buffer_count || ctx->capture_buffer_count <= buffer->index))
            return EINVAL;

        buffer->m.planes[0].m.mem_offset = plane_ptr->offset[0];
        buffer->m.planes[0].length = plane_ptr->psize[0];
        buffer->m.planes[1].m.mem_offset = plane_ptr->offset[1];
        buffer->m.planes[1].length = plane_ptr->psize[1];
        REL_PRINT("CAM_CTX(%p) Querybuf for buffer index %d length[0] %d"
            " mem_offset[0] %d length[1] %d mem_offset[1] %d\n", ctx, buffer->index,
            buffer->m.planes[0].length, buffer->m.planes[0].m.mem_offset,
            buffer->m.planes[1].length, buffer->m.planes[1].m.mem_offset);
    }
    else
    {
        uint64_t sizes[3] = {0};

        /* Allocate the Argus buffer to map with the DMABUF fd, created in user-space */
        int32_t dmabuf_fd = buffer->m.planes[0].m.fd;
        retval = allocate_argus_buffers(ctx, &ctx->argus_params, dmabuf_fd, buffer->index);
        if (retval != 0)
            return retval;

        retval = query_cam_buffers(ctx, &ctx->argus_params, &sizes[0]);
        if (retval != 0)
            return retval;

        buffer->m.planes[0].length = sizes[0];
        buffer->m.planes[1].length = sizes[1];
        REL_PRINT("CAM_CTX(%p) Querybuf for buffer index %d FD %d\n", ctx, buffer->index,
            buffer->m.planes[0].m.fd);
    }

    buffer->flags = ctx->cap_buffers[buffer->index].flags;
    return 0;
}

int32_t vidioc_cam_expbuf(int32_t fd, struct v4l2_exportbuffer *export_buffer)
{
    v4l2_camera_context *ctx = get_v4l2_context_from_fd(fd);

    if (NULL == ctx || NULL == export_buffer)
        return EINVAL;

    export_buffer->fd = (__s32) ctx->cap_buffers[export_buffer->index].buf_fd;

    REL_PRINT("CAM_CTX(%p) EXP_BUF for capture plane with buffer_index %d plane %d fd returned %d\n",
        ctx, export_buffer->index, export_buffer->plane, export_buffer->fd);

    return 0;
}

int32_t vidioc_cam_s_extctrls(int32_t fd, struct v4l2_ext_controls *ctrl)
{
    int32_t retval = 0;
    v4l2_camera_context *ctx = get_v4l2_context_from_fd(fd);
    if (NULL == ctx)
        return EINVAL;
    NvMutexAcquire(ctx->context_mutex);
    cam_settings *argus_settings = &ctx->argus_settings;
    if (ctrl->ctrl_class == V4L2_CTRL_CLASS_CAMERA)
    {
        for (uint32_t i = 0; i < ctrl->count; i++)
        {
            switch (ctrl->controls[i].id)
            {
                case V4L2_CID_ARGUS_SENSOR_MODE:
                {
                    if (ctrl->controls[i].value < 0 ||
                            static_cast<uint32_t>(ctrl->controls[i].value) >= ctx->argusv4l2_sensormodes.size())
                    {
                        REL_PRINT("CAM_CTX(%p) Invalid sensor-mode value. Keeping it default\n", ctx);
                        retval = EINVAL;
                        goto cleanup_sctl;
                    }
                    argus_settings->sensorModeIdx = ctrl->controls[i].value;
                    v4l2_cam_set_argus_settings(ctx, V4L2_CID_ARGUS_SENSOR_MODE);
                    REL_PRINT("CAM_CTX(%p) Sensor mode set to %d\n", ctx, argus_settings->sensorModeIdx);
                }
                    break;
                case V4L2_CID_ARGUS_DENOISE_STRENGTH:
                {
                    v4l2_argus_denoise_strength *settings = (v4l2_argus_denoise_strength *)ctrl->controls[i].string;
                    if (settings->DenoiseStrength > MAX_DENOISE_STRENGTH ||
                        settings->DenoiseStrength < MIN_DENOISE_STRENGTH)
                    {
                        REL_PRINT("CAM_CTX(%p) Invalid Denoise Strength specified\n", ctx);
                        retval = ERANGE;
                        goto cleanup_sctl;
                    }
                    argus_settings->denoiseStrength = settings->DenoiseStrength;
                    if (v4l2_cam_set_argus_settings(ctx, V4L2_CID_ARGUS_DENOISE_STRENGTH) != 0)
                    {
                        retval = EINVAL;
                        goto cleanup_sctl;
                    }
                    REL_PRINT("CAM_CTX(%p) Denoise Strength set successfully to %f\n",
                        ctx, argus_settings->denoiseStrength);
                }
                    break;
                case V4L2_CID_ARGUS_DENOISE_MODE:
                {
                    argus_settings->denoiseMode = ctrl->controls[i].value;
                    if (v4l2_cam_set_argus_settings(ctx, V4L2_CID_ARGUS_DENOISE_MODE) != 0)
                    {
                        retval = EINVAL;
                        goto cleanup_sctl;
                    }
                    REL_PRINT("CAM_CTX(%p) Denoise Mode set successfully to %d\n",
                        ctx, argus_settings->denoiseMode);
                }
                    break;
                case V4L2_CID_ARGUS_EE_STRENGTH:
                {
                    v4l2_argus_edge_enhance_strength *settings =
                        (v4l2_argus_edge_enhance_strength *)ctrl->controls[i].string;
                    if (settings->EdgeEnhanceStrength > MAX_EE_STRENGTH ||
                        settings->EdgeEnhanceStrength < MIN_EE_STRENGTH)
                    {
                        REL_PRINT("CAM_CTX(%p) Invalid EE Strength specified\n", ctx);
                        retval = ERANGE;
                        goto cleanup_sctl;
                    }
                    argus_settings->edgeEnhanceStrength = settings->EdgeEnhanceStrength;
                    if (v4l2_cam_set_argus_settings(ctx, V4L2_CID_ARGUS_EE_STRENGTH) != 0)
                    {
                        retval = EINVAL;
                        goto cleanup_sctl;
                    }
                    REL_PRINT("CAM_CTX(%p) Edge Enhancement Strength set successfully to %f\n",
                        ctx, argus_settings->edgeEnhanceStrength);
                }
                    break;
                case V4L2_CID_ARGUS_EE_MODE:
                {
                    argus_settings->edgeEnhanceMode = ctrl->controls[i].value;
                    if (v4l2_cam_set_argus_settings(ctx, V4L2_CID_ARGUS_EE_MODE) != 0)
                    {
                        retval = EINVAL;
                        goto cleanup_sctl;
                    }
                    REL_PRINT("CAM_CTX(%p) Edge Enhancement Mode set successfully to %d\n",
                        ctx, argus_settings->edgeEnhanceMode);
                }
                    break;
                case V4L2_CID_ARGUS_AE_ANTIBANDING_MODE:
                {
                    argus_settings->aeAntibandingMode = ctrl->controls[i].value;
                    v4l2_cam_set_argus_settings(ctx, V4L2_CID_ARGUS_AE_ANTIBANDING_MODE);
                    REL_PRINT("CAM_CTX(%p) Setting AE AntiBanding mode to %d\n",
                        ctx, argus_settings->aeAntibandingMode);
                }
                    break;
                case V4L2_CID_ARGUS_AUTO_WHITE_BALANCE_MODE:
                {
                    argus_settings->awbMode = ctrl->controls[i].value;
                    v4l2_cam_set_argus_settings(ctx, V4L2_CID_ARGUS_AUTO_WHITE_BALANCE_MODE);
                    REL_PRINT("CAM_CTX(%p) Setting AWB mode to %d\n",
                        ctx, argus_settings->awbMode);
                }
                    break;
                case V4L2_CID_3A_LOCK:
                {
                    argus_settings->aeLock = ctrl->controls[i].value & V4L2_LOCK_EXPOSURE;
                    argus_settings->awbLock = ctrl->controls[i].value & V4L2_LOCK_WHITE_BALANCE;
                    if (ctrl->controls[i].value & V4L2_LOCK_FOCUS)
                    {
                        REL_PRINT("CAM_CTX(%p) Auto Focus Lock not supported\n", ctx);
                        retval = EINVAL;
                        goto cleanup_sctl;
                    }
                    v4l2_cam_set_argus_settings(ctx, V4L2_CID_3A_LOCK);
                    REL_PRINT("CAM_CTX(%p) Setting Auto Exposure lock to %d\n",
                        ctx, argus_settings->aeLock);
                    REL_PRINT("CAM_CTX(%p) Setting Auto White Balance lock to %d\n",
                        ctx, argus_settings->awbLock);
                }
                    break;
                case V4L2_CID_ARGUS_EXPOSURE_COMPENSATION:
                {
                    v4l2_argus_exposure_compensation *excomp =
                        (v4l2_argus_exposure_compensation *)ctrl->controls[i].string;
                    argus_settings->exposureCompensation = excomp->ExposureCompensation;
                    v4l2_cam_set_argus_settings(ctx, V4L2_CID_ARGUS_EXPOSURE_COMPENSATION);
                    REL_PRINT("CAM_CTX(%p) Setting Exposure Compensation to %f\n",
                        ctx, argus_settings->exposureCompensation);
                }
                    break;
                case V4L2_CID_ARGUS_ISP_DIGITAL_GAIN_RANGE:
                {
                    v4l2_argus_ispdigital_gainrange *digital_gainrange =
                        (v4l2_argus_ispdigital_gainrange *)ctrl->controls[i].string;
                    if (digital_gainrange->MinISPDigitalGainRange < MIN_DIGITAL_GAIN ||
                        digital_gainrange->MaxISPDigitalGainRange > MAX_DIGITAL_GAIN)
                    {
                        REL_PRINT("CAM_CTX(%p) Invalid Digital Gain Range specified\n", ctx);
                        retval = ERANGE;
                        goto cleanup_sctl;
                    }
                    argus_settings->ispDigitalGainRange.min() = digital_gainrange->MinISPDigitalGainRange;
                    argus_settings->ispDigitalGainRange.max() = digital_gainrange->MaxISPDigitalGainRange;
                    v4l2_cam_set_argus_settings(ctx, V4L2_CID_ARGUS_ISP_DIGITAL_GAIN_RANGE);
                    REL_PRINT("CAM_CTX(%p) Set Digital Gain Range \"%f %f\"\n", ctx,
                        argus_settings->ispDigitalGainRange.min(), argus_settings->ispDigitalGainRange.max());
                }
                    break;
                case V4L2_CID_ARGUS_COLOR_SATURATION:
                {
                    v4l2_argus_color_saturation *color_sat =
                        (v4l2_argus_color_saturation *)ctrl->controls[i].string;
                    if (argus_settings->colorSaturation > MAX_COLOR_SATURATION ||
                        argus_settings->colorSaturation < MIN_COLOR_SATURATION)
                    {
                        REL_PRINT("CAM_CTX(%p) Invalid Color Saturation specified\n", ctx);
                        retval = ERANGE;
                        goto cleanup_sctl;
                    }
                    argus_settings->enableColorSaturation = color_sat->EnableSaturation;
                    argus_settings->colorSaturation = color_sat->ColorSaturation;
                    v4l2_cam_set_argus_settings(ctx, V4L2_CID_ARGUS_COLOR_SATURATION);
                    REL_PRINT("CAM_CTX(%p) Set Color Saturation %f\n",
                        ctx, argus_settings->colorSaturation);
                }
                    break;
                case V4L2_CID_ARGUS_GAIN_RANGE:
                {
                    v4l2_argus_gainrange *gainrange =
                        (v4l2_argus_gainrange *)ctrl->controls[i].string;
                    float min_gainrange =
                        ctx->argusv4l2_sensormodes[argus_settings->sensorModeIdx].minGainRange;
                    float max_gainrange =
                        ctx->argusv4l2_sensormodes[argus_settings->sensorModeIdx].maxGainRange;
                    if (gainrange->MinGainRange < min_gainrange ||
                        gainrange->MaxGainRange > max_gainrange)
                    {
                        REL_PRINT("CAM_CTX(%p) Invalid Gain Range specified\n", ctx);
                        retval = ERANGE;
                        goto cleanup_sctl;
                    }
                    argus_settings->gainRange.min() = gainrange->MinGainRange;
                    argus_settings->gainRange.max() = gainrange->MaxGainRange;
                    v4l2_cam_set_argus_settings(ctx, V4L2_CID_ARGUS_GAIN_RANGE);
                    REL_PRINT("CAM_CTX(%p) Set Gain Range \"%f %f\"\n", ctx,
                        argus_settings->gainRange.min(), argus_settings->gainRange.max());
                }
                    break;
                case V4L2_CID_ARGUS_EXPOSURE_TIME_RANGE:
                {
                    v4l2_argus_exposure_timerange *exptimerange =
                        (v4l2_argus_exposure_timerange *)ctrl->controls[i].string;
                    float min_exptimerange =
                        ctx->argusv4l2_sensormodes[argus_settings->sensorModeIdx].minExposureTimeRange;
                    float max_exptimerange =
                        ctx->argusv4l2_sensormodes[argus_settings->sensorModeIdx].maxExposureTimeRange;
                    if (exptimerange->MinExposureTimeRange < min_exptimerange ||
                        exptimerange->MaxExposureTimeRange > max_exptimerange)
                    {
                        REL_PRINT("CAM_CTX(%p) Invalid Exposure Time Range specified\n", ctx);
                        retval = ERANGE;
                        goto cleanup_sctl;
                    }
                    argus_settings->exposureTimeRange.min() = exptimerange->MinExposureTimeRange;
                    argus_settings->exposureTimeRange.max() = exptimerange->MaxExposureTimeRange;
                    v4l2_cam_set_argus_settings(ctx, V4L2_CID_ARGUS_EXPOSURE_TIME_RANGE);
                    REL_PRINT("CAM_CTX(%p) Set Exposure Time Range \"%lu %lu\" \n", ctx,
                        argus_settings->exposureTimeRange.min(), argus_settings->exposureTimeRange.max());
                }
                    break;
                default:
                    REL_PRINT("CAM_CTX(%p) Invalid Control ID passed\n", ctx);
                    goto cleanup_sctl;
            }
        }
    }
cleanup_sctl:
    NvMutexRelease(ctx->context_mutex);
    return retval;
}

int32_t vidic_cam_g_extctrls(int32_t fd, struct v4l2_ext_controls *ctrl)
{
    int32_t retval = 0;
    v4l2_camera_context *ctx = get_v4l2_context_from_fd(fd);
    if (NULL == ctx)
        return EINVAL;
    struct v4l2_ext_control *control =  NULL;
    for (uint32_t i = 0; i < ctrl->count; i++)
    {
        control = &(ctrl->controls[i]);
        if (control->id == V4L2_CID_ARGUS_METADATA)
        {
            if (!ctx->stream_on)
            {
                return EBUSY;
            }
            argusframe_metadata *src;
            v4l2_argus_ctrl_metadata *dest =
                (v4l2_argus_ctrl_metadata *)(control->string);
            if (ctx->frame_metadata[dest->BufferIndex] != NULL)
            {

                src = ctx->frame_metadata[dest->BufferIndex];
                dest->AeLocked = src->aeLocked;

                if (src->aeState == Argus::AE_STATE_INACTIVE)
                    dest->AEState = V4L2_ARGUS_AE_STATE_INACTIVE;
                else if (src->aeState == Argus::AE_STATE_SEARCHING)
                    dest->AEState = V4L2_ARGUS_AE_STATE_SEARCHING;
                else if (src->aeState == Argus::AE_STATE_CONVERGED)
                    dest->AEState = V4L2_ARGUS_AE_STATE_CONVERGED;
                else if (src->aeState == Argus::AE_STATE_FLASH_REQUIRED)
                    dest->AEState = V4L2_ARGUS_AE_STATE_FLASH_REQUIRED;
                else if (src->aeState == Argus::AE_STATE_TIMEOUT)
                    dest->AEState = V4L2_ARGUS_AE_STATE_TIMEOUT;
                else
                    dest->AEState = V4L2_ARGUS_AeState_Unknown;

                dest->FocuserPosition = src->focuserPosition;
                dest->AwbCCT = src->awbCct;

                if (src->awbState == Argus::AWB_STATE_INACTIVE)
                    dest->AWBState = V4L2_ARGUS_AWB_STATE_INACTIVE;
                else if (src->awbState == Argus::AWB_STATE_SEARCHING)
                    dest->AWBState = V4L2_ARGUS_AWB_STATE_SEARCHING;
                else if (src->awbState == Argus::AWB_STATE_CONVERGED)
                    dest->AWBState = V4L2_ARGUS_AWB_STATE_CONVERGED;
                else if (src->awbState == Argus::AWB_STATE_LOCKED)
                    dest->AWBState = V4L2_ARGUS_AWB_STATE_LOCKED;
                else
                    dest->AWBState = V4L2_ARGUS_AwbState_Unknown;

                dest->FrameDuration = src->frameDuration;
                dest->IspDigitalGain = src->ispDigitalGain;
                dest->FrameReadoutTime = src->frameReadoutTime;
                dest->SceneLux = src->sceneLux;
                dest->SensorAnalogGain = src->sensorAnalogGain;
                dest->SensorExposureTime = src->sensorExposureTime;
                dest->SensorSensitivity = src->sensorSensitivity;
                dest->ValidFrameStatus = true;
            }
            else
            {
                REL_PRINT("CAM_CTX(%p) No metadata recieved for given Buffer\n", ctx);
                dest->ValidFrameStatus = false;
                retval = EACCES;
                break;
            }
        }
        else
            retval = EINVAL;
    }
    return retval;
}

int32_t vidioc_cam_sparm(int32_t fd, struct v4l2_streamparm *streamparms)
{
    v4l2_camera_context *ctx = get_v4l2_context_from_fd(fd);
    if (NULL == ctx)
        return EINVAL;

    V4L2_BUFFER_TYPE_SUPPORTED_OR_ERROR(streamparms->type);

    NvMutexAcquire(ctx->context_mutex);
    float frameduration = 1e9 * (streamparms->parm.capture.timeperframe.numerator)/
                            (streamparms->parm.capture.timeperframe.denominator);

    if (set_framerate(ctx, frameduration) != 0)
    {
        REL_PRINT("CAM_CTX(%p) Failed to set the specified Frame rate\n", ctx);
        NvMutexRelease(ctx->context_mutex);
        return EINVAL;
    }

    REL_PRINT("CAM_CTX(%p) Setting framerate as %d/%d\n", ctx,
        streamparms->parm.capture.timeperframe.denominator,
        streamparms->parm.capture.timeperframe.numerator);
    NvMutexRelease(ctx->context_mutex);
    return 0;
}

int32_t nvargus_ioctl(int32_t fd, unsigned long cmd, void *arg)
{
    switch (cmd)
    {
        case VIDIOC_QUERYCAP:
            return vidioc_cam_querycap(fd, (struct v4l2_capability *)arg);
        case VIDIOC_ENUM_FMT:
            return vidioc_cam_enum_fmt(fd, (struct v4l2_fmtdesc *)arg);
        case VIDIOC_G_FMT:
            return vidioc_cam_g_fmt(fd, (struct v4l2_format *)arg);
        case VIDIOC_S_FMT:
            return vidioc_cam_s_fmt(fd, (struct v4l2_format *)arg);
        case VIDIOC_QBUF:
            return vidioc_cam_qbuf(fd, (struct v4l2_buffer *)arg);
        case VIDIOC_DQBUF:
            return vidioc_cam_dqbuf(fd, (struct v4l2_buffer *)arg);
        case VIDIOC_REQBUFS:
            return vidioc_cam_reqbufs(fd, (struct v4l2_requestbuffers *)arg);
        case VIDIOC_STREAMON:
            return vidioc_cam_streamon(fd, (unsigned int*)arg);
        case VIDIOC_STREAMOFF:
            return vidioc_cam_streamoff(fd, (unsigned int*)arg);
        case VIDIOC_QUERYBUF:
            return vidioc_cam_querybuf(fd, (struct v4l2_buffer *)arg);
        case VIDIOC_S_EXT_CTRLS:
            return vidioc_cam_s_extctrls(fd, (struct v4l2_ext_controls *)arg);
        case VIDIOC_TRY_FMT:
        case VIDIOC_G_EXT_CTRLS:
            return vidic_cam_g_extctrls(fd, (struct v4l2_ext_controls *)arg);
        case VIDIOC_S_CTRL:
        case VIDIOC_G_CTRL:
        case VIDIOC_DQEVENT:
        case VIDIOC_SUBSCRIBE_EVENT:
        case VIDIOC_S_CROP:
        case VIDIOC_G_CROP:
        case VIDIOC_G_PARM:
            printf("Not Implemented\n");
            return EINVAL;
        case VIDIOC_S_PARM:
            return vidioc_cam_sparm(fd, (struct v4l2_streamparm *)arg);
        case VIDIOC_ENUM_FRAMESIZES:
            return vidioc_cam_enum_framesizes(fd, (struct v4l2_frmsizeenum *)arg);
        case VIDIOC_ENUM_FRAMEINTERVALS:
            return vidioc_cam_enum_frameintervals(fd, (struct v4l2_frmivalenum *)arg);
        case VIDIOC_EXPBUF:
            return vidioc_cam_expbuf(fd, (struct v4l2_exportbuffer *)arg);
        default:
            printf("DEFAULT no IOCTL called\n");
            return EINVAL;
    }
    return 0;
}
