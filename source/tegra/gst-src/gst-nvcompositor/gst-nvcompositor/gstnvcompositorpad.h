/*
 * Copyright (c) 2017-2022, NVIDIA CORPORATION. All rights reserved.
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

#ifndef __GST_NVCOMPOSITOR_PAD_H__
#define __GST_NVCOMPOSITOR_PAD_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoaggregator.h>

G_BEGIN_DECLS
#define GST_TYPE_NVCOMPOSITOR_PAD (gst_nvcompositor_pad_get_type())
#define GST_NVCOMPOSITOR_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NVCOMPOSITOR_PAD, GstNvCompositorPad))
#define GST_NVCOMPOSITOR_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_NVCOMPOSITOR_PAD, GstNvCompositorPadClass))
#define GST_IS_NVCOMPOSITOR_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_NVCOMPOSITOR_PAD))
#define GST_IS_NVCOMPOSITOR_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_NVCOMPOSITOR_PAD))

typedef struct _GstNvCompositorPad GstNvCompositorPad;
typedef struct _GstNvCompositorPadClass GstNvCompositorPadClass;

/**
 * GstNvCompositorPad:
 */
struct _GstNvCompositorPad
{
  GstVideoAggregatorConvertPad parent;

  /* nvcompositor pad properties */
  gint xpos;
  gint ypos;
  gint width;
  gint height;
  gdouble alpha;
  gint interpolation_method;

  gint input_width;
  gint input_height;

  GstVideoInfo conversion_info;
  NvBufSurfaceColorFormat comppad_pix_fmt;
};

struct _GstNvCompositorPadClass
{
  GstVideoAggregatorConvertPadClass parent_class;
};

GType gst_nvcompositor_pad_get_type (void);

G_END_DECLS
#endif /* __GST_NVCOMPOSITOR_PAD_H__ */
