/*
 * Copyright (c) 2014-2023, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __GST_NVVCONV_H__
#define __GST_NVVCONV_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/gstbasetransform.h>

#include "nvbufsurface.h"
#include "nvbufsurftransform.h"

#include <cuda.h>
#include <cuda_runtime.h>

G_BEGIN_DECLS
#define GST_TYPE_NVVCONV \
  (gst_nvvconv_get_type())
#define GST_NVVCONV(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NVVCONV,Gstnvvconv))
#define GST_NVVCONV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_NVVCONV,GstnvvconvClass))
#define GST_IS_NVVCONV(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_NVVCONV))
#define GST_IS_NVVCONV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_NVVCONV))

/* Name of package */
#define PACKAGE "gstreamer-nvvconv-plugin"
/* Define to the full name of this package. */
#define PACKAGE_NAME "GStreamer nvvconv Plugin"
/* Define to the full name and version of this package. */
#define PACKAGE_STRING "GStreamer nvvconv 1.2.3"
/* Information about the purpose of the plugin. */
#define PACKAGE_DESCRIPTION "video Colorspace conversion & scaler"
/* Define to the home page for this package. */
#define PACKAGE_URL "http://nvidia.com/"
/* Define to the version of this package. */
#define PACKAGE_VERSION "1.2.3"
/* Define under which licence the package has been released */
#define PACKAGE_LICENSE "Proprietary"
/* Version number of package */
#define VERSION "1.2.3"

#define NVRM_MAX_SURFACES                 3
#define NVFILTER_MAX_BUF                  4
#define GST_CAPS_FEATURE_MEMORY_NVMM      "memory:NVMM"

typedef struct _Gstnvvconv Gstnvvconv;
typedef struct _GstnvvconvClass GstnvvconvClass;

typedef struct _GstNvvConvBuffer GstNvvConvBuffer;
typedef struct _GstNvInterBuffer GstNvInterBuffer;

/**
 * BufType:
 *
 * Buffer type enum.
 */
typedef enum
{
  BUF_TYPE_YUV,
  BUF_TYPE_GRAY,
  BUF_TYPE_RGB,
  BUF_NOT_SUPPORTED
} BufType;

/**
 * BufMemType:
 *
 * Buffer memory type enum.
 */
typedef enum
{
  BUF_MEM_SW,
  BUF_MEM_HW
} BufMemType;

/**
 * GstVideoFlipMethods:
 *
 * Video flip methods type enum.
 */
typedef enum
{
  GST_VIDEO_NVFLIP_METHOD_IDENTITY,
  GST_VIDEO_NVFLIP_METHOD_90L,
  GST_VIDEO_NVFLIP_METHOD_180,
  GST_VIDEO_NVFLIP_METHOD_90R,
  GST_VIDEO_NVFLIP_METHOD_HORIZ,
  GST_VIDEO_NVFLIP_METHOD_INVTRANS,
  GST_VIDEO_NVFLIP_METHOD_VERT,
  GST_VIDEO_NVFLIP_METHOD_TRANS
} GstVideoFlipMethods;

/**
 * GstInterpolationMethods:
 *
 * Interpolation methods type enum.
 */
typedef enum
{
  GST_INTERPOLATION_NEAREST,
  GST_INTERPOLATION_BILINEAR,
  GST_INTERPOLATION_5_TAP,
  GST_INTERPOLATION_10_TAP,
  GST_INTERPOLATION_SMART,
  GST_INTERPOLATION_NICEST,
} GstInterpolationMethods;

/**
 * GstNvvConvBuffer:
 *
 * Nvfilter buffer.
 */
struct _GstNvvConvBuffer
{
  gint dmabuf_fd;
  GstBuffer *gst_buf;
  NvBufSurface *surface;
};

/**
 * GstNvInterBuffer:
 *
 * Intermediate transform buffer.
 */
struct _GstNvInterBuffer
{
  gint idmabuf_fd;
  NvBufSurface *isurface;
};

/**
 * Gstnvvconv:
 *
 * Opaque object data structure.
 */
struct _Gstnvvconv
{
  GstBaseTransform element;

  /* source and sink pad caps */
  GstCaps *sinkcaps;
  GstCaps *srccaps;

  gint to_width;
  gint to_height;
  gint from_width;
  gint from_height;
  gint tsurf_width;
  gint tsurf_height;

  gint crop_left;
  gint crop_right;
  gint crop_top;
  gint crop_bottom;

  BufType inbuf_type;
  BufMemType inbuf_memtype;
  BufMemType outbuf_memtype;

  NvBufSurfTransformParams transform_params;
  NvBufSurfaceColorFormat in_pix_fmt;
  NvBufSurfaceColorFormat out_pix_fmt;
  NvBufSurfTransformRect src_rect;

  guint insurf_count;
  guint tsurf_count;
  guint isurf_count;
  guint ibuf_count;
  gint flip_method;
  guint num_output_buf;
  gint interpolation_method;

  gboolean silent;
  gboolean no_dimension;
  gboolean do_scaling;
  gboolean do_flip;
  gboolean do_cropping;
  gboolean need_intersurf;
  gboolean isurf_flag;
  gboolean negotiated;
  gboolean nvfilterpool;
  gboolean enable_blocklinear_output;

  GstBufferPool *pool;
  GMutex flow_lock;

  GstNvInterBuffer input_interbuf;
  GstNvInterBuffer output_interbuf;

  gint compute_hw;
  gint gpu_id;
  gint nvbuf_mem_type;
  guint session_created;
  GstVideoInfo in_info;
  GstVideoInfo out_info;
  NvBufSurfTransformConfigParams config_params;
  gboolean interlaced_flag;
};

struct _GstnvvconvClass
{
  GstBaseTransformClass parent_class;
};

GType gst_nvvconv_get_type (void);

G_END_DECLS
#endif /* __GST_NVVCONV_H__ */
