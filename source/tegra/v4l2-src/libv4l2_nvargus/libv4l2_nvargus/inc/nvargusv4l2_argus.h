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

#ifndef __NVARGUSV4L2_HELPER_H__
#define __NVARGUSV4L2_HELPER_H__

#include "nvargusv4l2_context.h"
#include "nvargusv4l2_ioctl.h"

#define TIMEOUT_FOUR_SECONDS  	4000000000
#define TIMEOUT_TWO_SECONDS  	2000000000

extern Argus::CameraProvider *g_cameraProvider;
extern Argus::ICameraProvider *g_iCameraProvider;

/* Returns the Argus status code in string */
const char* getStatusString(Argus::Status status);

/* Reads boolean variable atomically */
bool v4l2_cam_atomic_read_bool(v4l2_camera_context* ctx, bool *var);

/* Writes boolean variable atomically */
void v4l2_cam_atomic_write_bool(v4l2_camera_context* ctx, bool *var, bool value);

/* Return the value of the variable atomically */
uint32_t v4l2_cam_atomic_get(v4l2_camera_context *ctx, uint32_t *var);

/* Release Argus::Buffer with the given index to the stream */
int32_t ReleaseStreamBuffer(v4l2_camera_context *ctx, int32_t index);

/* Recieves pointer to the filled Argus::Buffer from the stream */
Argus::Buffer* AcquireStreamBuffer(v4l2_camera_context *ctx, uint32_t timeout);

/* Initializes the Argus::OutputStream */
int32_t initialize_outputstream(v4l2_camera_context *ctx, cam_params *argus_params);

/* Set all the Argus related properties and settings */
int32_t v4l2_cam_set_argus_settings(v4l2_camera_context *ctx, uint32_t ctrl_id);

/* Sets Frame Rate */
int32_t set_framerate(v4l2_camera_context *ctx, float frame_duration);

/* Sets Denoise Settings */
int32_t v4l2_cam_set_denoise_settings(v4l2_camera_context *ctx, uint32_t ctrl_id,
                                        struct denoise_values *ctrl_value);

/* Allocates all capture queues */
int32_t allocate_all_capture_queues(v4l2_camera_context *ctx);

/* Allocates buffers in the memory (MMAP) */
int32_t allocate_capture_buffers(v4l2_camera_context *ctx, cam_params *argus_params);

/* Allocate argus buffers for DMABUF memory */
int32_t allocate_argus_buffers(v4l2_camera_context *ctx, cam_params *argus_params, int32_t dmabuf_fd, uint32_t buffer_idx);

/* Deallocates the previously alloacted buffers */
void free_cap_buffers(v4l2_camera_context *ctx);

/* Query the buffer */
int32_t query_cam_buffers(v4l2_camera_context *ctx, cam_params *argus_params, uint64_t *psizes);

/* Close camera context */
void v4l2_close_argus_context(v4l2_context *v4l2_ctx);

/* Queues the empty capture plane buffers to Argus */
int32_t nvargus_enqueue_instream_buffers_from_capture_inQ(v4l2_camera_context *ctx, int32_t idx);

/* Callback function to the argus_thread */
void argus_capture_thread_func(void *args);

#endif
