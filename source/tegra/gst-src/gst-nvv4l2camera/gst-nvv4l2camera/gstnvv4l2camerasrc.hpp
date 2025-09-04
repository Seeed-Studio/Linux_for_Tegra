/*
 * Copyright (c) 2019-2022, NVIDIA CORPORATION. All rights reserved.
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

#ifndef __GST_NVV4L2CAMERASRC_H__
#define __GST_NVV4L2CAMERASRC_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/* #defines don't like whitespacey bits */
#define GST_TYPE_NVV4L2CAMERASRC \
  (gst_nv_v4l2_camera_src_get_type())
#define GST_NVV4L2CAMERASRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NVV4L2CAMERASRC,GstNvV4l2CameraSrc))
#define GST_NVV4L2CAMERASRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_NVV4L2CAMERASRC,GstNvV4l2CameraSrcClass))
#define GST_IS_NVV4L2CAMERASRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_NVV4L2CAMERASRC))
#define GST_IS_NVV4L2CAMERASRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_NVV4L2CAMERASRC))

#define DEFAULT_PROP_DEVICE     "/dev/video0"
#define MIN_BUFFERS             6
#define MAX_BUFFERS             16
#define GST_NVV4L2_MEMORY_TYPE  "nvV4l2Memory"
#define DEQUE_TIMEOUT           5


typedef struct _GstNvV4l2CameraSrc      GstNvV4l2CameraSrc;
typedef struct _GstNvV4l2CameraSrcClass GstNvV4l2CameraSrcClass;

typedef struct _GstNvV4l2CameraSrcBuffer GstNvV4l2CameraSrcBuffer;

/* NvV4l2CameraSrc buffer */
struct _GstNvV4l2CameraSrcBuffer
{
  gint dmabuf_fd;
  GstBuffer *gst_buf;
  NvBufSurface *surface;
  struct v4l2_buffer *buffer;
};

struct _GstNvV4l2CameraSrc
{
  GstBaseSrc base_nvv4l2camera;

  GstPad *srcpad;

  /* the video device */
  char *videodev;
  int video_fd;
  struct v4l2_capability *caps;
  struct v4l2_format *fmt;
  struct v4l2_requestbuffers *req;
  gint index;
  guint cap_buf;

  GstBufferPool *pool;

  GstCaps *outcaps;

  gint width;
  gint height;
  gint fps_n;
  gint fps_d;
  gint field_order;

  gboolean stop_requested;
  gboolean unlock_requested;
  gboolean interlaced_flag;
  fd_set read_set;
  struct timeval tv;
};

struct _GstNvV4l2CameraSrcClass
{
  GstBaseSrcClass base_nvv4l2camera_class;
};

GType gst_nv_v4l2_camera_src_get_type (void);


G_END_DECLS

#endif /* __GST_NVV4L2CAMERASRC_H__ */
