/*
 * Copyright (c) 2018-2022, NVIDIA CORPORATION.  All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License, version 2.1, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#ifndef __GST_NV_VIDEO_CONTEXT_H__
#define __GST_NV_VIDEO_CONTEXT_H__

#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/video.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "gstnvvideofwd.h"

#include <cuda.h>
#include <cudaGL.h>
#include <cuda_runtime.h>

G_BEGIN_DECLS

#define GST_TYPE_NV_VIDEO_CONTEXT \
    (gst_nv_video_context_get_type())
#define GST_NV_VIDEO_CONTEXT(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_NV_VIDEO_CONTEXT, GstNvVideoContext))
#define GST_NV_VIDEO_CONTEXT_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_NV_VIDEO_CONTEXT, GstNvVideoContextClass))
#define GST_IS_NV_VIDEO_CONTEXT(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_NV_VIDEO_CONTEXT))
#define GST_IS_NV_VIDEO_CONTEXT_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_NV_VIDEO_CONTEXT))
#define GST_NV_VIDEO_CONTEXT_GET_CLASS(o) \
    (G_TYPE_INSTANCE_GET_CLASS((o), GST_TYPE_NV_VIDEO_CONTEXT, GstNvVideoContextClass))

typedef enum
{
  GST_NV_VIDEO_CONTEXT_TYPE_NONE = 0,
  GST_NV_VIDEO_CONTEXT_TYPE_EGL = (1 << 0),

  GST_NV_VIDEO_CONTEXT_TYPE_ANY = G_MAXUINT32
} GstNvVideoContextType;

struct _GstNvVideoContextClass
{
  GstObjectClass parent_class;

  gboolean  (*create)       (GstNvVideoContext * context);
  gboolean  (*setup)        (GstNvVideoContext * context);
  void      (*cleanup)      (GstNvVideoContext * context);
  GstCaps   *(*get_caps)    (GstNvVideoContext * context);
  gboolean  (*show_frame)   (GstNvVideoContext * context, GstBuffer * buf);
  void      (*handle_eos)   (GstNvVideoContext * context);
  void      (*handle_drc)   (GstNvVideoContext * context);
  void      (*handle_tearing)   (GstNvVideoContext * context);
};

struct _GstNvVideoContext
{
  GstObject parent;

  GstNvVideoDisplay *display;
  GstNvVideoWindow *window;

  GstNvVideoContextType type;

  GstNvVideoContextPrivate *priv;

  guint using_NVMM;
  GstVideoInfo configured_info;

  gboolean is_cuda_init;
  CUcontext cuContext;
  CUgraphicsResource cuResource[3];
  unsigned int gpu_id;

};

GST_EXPORT
GstNvVideoContext * gst_nv_video_context_new (GstNvVideoDisplay * display);

GST_EXPORT
gboolean gst_nv_video_context_create (GstNvVideoContext * context);
GST_EXPORT
GstCaps * gst_nv_video_context_get_caps (GstNvVideoContext * context);
GST_EXPORT
gboolean gst_nv_video_context_set_window (GstNvVideoContext * context, GstNvVideoWindow * window);
GST_EXPORT
gboolean gst_nv_video_context_show_frame (GstNvVideoContext * context, GstBuffer * buf);
GST_EXPORT
void gst_nv_video_context_handle_eos (GstNvVideoContext * context);
GST_EXPORT
void gst_nv_video_context_handle_drc (GstNvVideoContext * context);
GST_EXPORT
void gst_nv_video_context_handle_tearing (GstNvVideoContext * context);
GST_EXPORT
gboolean gst_nv_video_context_create_render_thread (GstNvVideoContext * context);
GST_EXPORT
void gst_nv_video_context_destroy_render_thread (GstNvVideoContext * context);
GST_EXPORT
GstNvVideoContextType gst_nv_video_context_get_handle_type (GstNvVideoContext * context);

GType gst_nv_video_context_get_type (void);

G_END_DECLS

#endif /* __GST_NV_VIDEO_CONTEXT_H__ */
