/*
 * Copyright (c) 2019-2020, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef GSTNVV4L2CAMERABUFFERPOOL_H_
#define GSTNVV4L2CAMERABUFFERPOOL_H_

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstNvV4l2CameraBufferPool GstNvV4l2CameraBufferPool;
typedef struct _GstNvV4l2CameraBufferPoolClass GstNvV4l2CameraBufferPoolClass;
typedef struct _GstNvV4l2CameraBufferPoolPrivate GstNvV4l2CameraBufferPoolPrivate;

#define GST_TYPE_NVV4L2CAMERA_BUFFER_POOL      (gst_nvv4l2camera_buffer_pool_get_type())
#define GST_IS_NVV4L2CAMERA_BUFFER_POOL(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_NVV4L2CAMERA_BUFFER_POOL))
#define GST_NVV4L2CAMERA_BUFFER_POOL(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_NVV4L2CAMERA_BUFFER_POOL, GstNvV4l2CameraBufferPool))
#define GST_NVV4L2CAMERA_BUFFER_POOL_CAST(obj) ((GstNvV4l2CameraBufferPool*)(obj))

#define GST_NVV4L2_MEMORY_TYPE "nvV4l2Memory"

struct _GstNvV4l2CameraBufferPool
{
  GstBufferPool bufferpool;
  GstNvV4l2CameraBufferPoolPrivate *priv;
  int video_fd;
};

struct _GstNvV4l2CameraBufferPoolClass
{
  GstBufferPoolClass parent_class;
};

GType gst_nvv4l2camera_buffer_pool_get_type (void);

GstBufferPool* gst_nvv4l2camera_buffer_pool_new (void);

G_END_DECLS

#endif /* GSTNVV4L2CAMERABUFFERPOOL_H_ */
