/*
 * Copyright (c) 2017-2023, NVIDIA CORPORATION. All rights reserved.
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

#ifndef __GST_NVCOMPOSITOR_H__
#define __GST_NVCOMPOSITOR_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoaggregator.h>

#include "nvbufsurface.h"
#include "nvbufsurftransform.h"

G_BEGIN_DECLS
#define GST_TYPE_NVCOMPOSITOR (gst_nvcompositor_get_type())
#define GST_NVCOMPOSITOR(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NVCOMPOSITOR, GstNvCompositor))
#define GST_NVCOMPOSITOR_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_NVCOMPOSITOR, GstNvCompositorClass))
#define GST_IS_NVCOMPOSITOR(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_NVCOMPOSITOR))
#define GST_IS_NVCOMPOSITOR_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_NVCOMPOSITOR))

/* Name of package */
#define PACKAGE "gstreamer-nvcompositor-plugin"
/* Define to the full name of this package. */
#define PACKAGE_NAME "GStreamer NvCompositor Plugin"
/* Define to the full name and version of this package. */
#define PACKAGE_STRING "GStreamer NvComositor 1.16.3"
/* Information about the purpose of the plugin. */
#define PACKAGE_DESCRIPTION "Video Compositor"
/* Define to the home page for this package. */
#define PACKAGE_ORIGIN "http://nvidia.com/"
/* Define to the version of this package. */
#define PACKAGE_VERSION "1.16.3"
/* Define under which licence the package has been released */
#define PACKAGE_LICENSE "Proprietary"

#define MAX_INPUT_FRAME 16
#define GST_CAPS_FEATURE_MEMORY_NVMM  "memory:NVMM"

typedef struct _GstNvCompositor GstNvCompositor;
typedef struct _GstNvCompositorClass GstNvCompositorClass;

typedef struct _GstNvCompositorBuffer GstNvCompositorBuffer;
typedef struct _GstNvCompositorBgcolor GstNvCompBgcolor;

/* Backgrounds for compositor to blend over */
typedef enum
{
  NVCOMPOSITOR_BACKGROUND_BLACK,
  NVCOMPOSITOR_BACKGROUND_RED,
  NVCOMPOSITOR_BACKGROUND_GREEN,
  NVCOMPOSITOR_BACKGROUND_BLUE,
  NVCOMPOSITOR_BACKGROUND_WHITE,
} GstNvCompositorBackground;

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
 * Nv Compositor Buffer:
 */
struct _GstNvCompositorBuffer
{
  gint dmabuf_fd;
  GstBuffer *gst_buf;
  NvBufSurface *surface;
};

/**
 *  Background color:
 */
struct _GstNvCompositorBgcolor
{
  gfloat r;
  gfloat g;
  gfloat b;
};

/**
 * GstNvCompositor:
 */
struct _GstNvCompositor
{
  GstVideoAggregator videoaggregator;
  gboolean silent;

  gint out_width;
  gint out_height;
  NvBufSurfaceColorFormat out_pix_fmt;

  GstNvCompBgcolor bg;
  GstNvCompositorBackground background;
  NvBufSurfTransformCompositeBlendParamsEx comp_params;

  gboolean nvcomppool;
  GstBufferPool *pool;
};

struct _GstNvCompositorClass
{
  GstVideoAggregatorClass parent_class;
};

GType gst_nvcompositor_get_type (void);

G_END_DECLS
#endif /* __GST_NVCOMPOSITOR_H__ */
