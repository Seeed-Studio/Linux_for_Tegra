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

#ifndef __NVARGUSV4L2_IOCTL_H__
#define __NVARGUSV4L2_IOCTL_H__

#include "nvargusv4l2_context.h"
#include "nvargusv4l2_argus.h"

#define ALIGN_BYTES     255
#define MEM_ALIGN_16(X) (((X) + 0xF)&~0xF)

#define MIN_EXPOSURE_COMPENSATION  -2.0
#define MAX_EXPOSURE_COMPENSATION   2.0
#define MIN_EE_STRENGTH            -1.0
#define MAX_EE_STRENGTH             1.0
#define MIN_DENOISE_STRENGTH       -1.0
#define MAX_DENOISE_STRENGTH        1.0
#define MIN_DIGITAL_GAIN            1
#define MAX_DIGITAL_GAIN            256
#define MIN_COLOR_SATURATION        0.0
#define MAX_COLOR_SATURATION        2.0

extern Argus::CameraProvider *g_cameraProvider;
extern Argus::ICameraProvider *g_iCameraProvider;

/* Structure to map corresponding v4l2 format and
 * Argus pixel format.
 */
struct argusv4l2_fmt_struct
{
    /* Color format description */
    char name[32];
    /* Argus pixel format */
    const Argus::PixelFormat argus_format;
    /* V4L2 fourcc */
    uint32_t fourcc;
    /* Number of planes */
    uint32_t num_planes;
    /* bytes per pixel */
    uint32_t bpp;
};

/* Entry structure which is added/removed in the queues */
typedef struct _q_entry
{
    /* Index of the buffer */
    uint32_t index;
    /* Size of the buffer */
    uint32_t size;
    /* Timestamp */
    uint64_t timestamp;
    /* FD */
    uint32_t fd;
    /* Stores the config store id of this buffer */
    uint32_t config_store;
} q_entry;

/* IOCTL calls for Argus camera */
int32_t nvargus_ioctl(int32_t fd, unsigned long cmd, void *arg);

int32_t vidioc_cam_querycap(int32_t fd, struct v4l2_capability *caps);

int32_t vidioc_cam_enum_fmt(int32_t fd, struct v4l2_fmtdesc *fmt);

int32_t vidioc_cam_enum_framesizes(int32_t fd, struct v4l2_frmsizeenum *frame);

int32_t vidioc_cam_enum_frameintervals(int32_t fd, struct v4l2_frmivalenum *frameival);

int32_t vidioc_cam_g_fmt(int32_t fd, struct v4l2_format *format);

int32_t vidioc_cam_s_fmt(int32_t fd, struct v4l2_format *format);

int32_t vidioc_cam_reqbufs(int32_t fd, struct v4l2_requestbuffers *reqbufs);

int32_t vidioc_cam_streamon(int32_t fd, unsigned int *type);

int32_t vidioc_cam_streamoff(int32_t fd, unsigned int *type);

int32_t vidioc_cam_qbuf(int32_t fd, struct v4l2_buffer *buffer);

int32_t vidioc_cam_dqbuf(int32_t fd, struct v4l2_buffer *buffer);

int32_t vidioc_cam_querybuf(int32_t fd, v4l2_buffer *buffer);

int32_t vidioc_cam_expbuf(int32_t fd, struct v4l2_exportbuffer *export_buffer);

int32_t vidioc_cam_s_extctrls(int32_t fd, struct v4l2_ext_controls *ctrl);

int32_t vidic_cam_g_extctrls(int32_t fd, struct v4l2_ext_controls *ctrl);

int32_t vidioc_cam_sparm(int32_t fd, struct v4l2_streamparm *streamparms);

#endif /* __NVARGUSV4L2_IOCTL_H__ */
