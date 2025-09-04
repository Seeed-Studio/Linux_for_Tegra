/*
 * Copyright (c) 2018-2020, NVIDIA CORPORATION.  All rights reserved.
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

#ifndef __GST_NV_VIDEO_RENDERER_H__
#define __GST_NV_VIDEO_RENDERER_H__

#include <gst/gst.h>
#include <gst/video/video.h>

#include "gstnvvideofwd.h"

G_BEGIN_DECLS

#define GST_TYPE_NV_VIDEO_RENDERER \
    (gst_nv_video_renderer_get_type())
#define GST_NV_VIDEO_RENDERER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_NV_VIDEO_RENDERER, GstNvVideoRenderer))
#define GST_NV_VIDEO_RENDERER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_NV_VIDEO_RENDERER, GstNvVideoRendererClass))
#define GST_IS_NV_VIDEO_RENDERER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_NV_VIDEO_RENDERER))
#define GST_IS_NV_VIDEO_RENDERER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_NV_VIDEO_RENDERER))
#define GST_NV_VIDEO_RENDERER_GET_CLASS(o) \
    (G_TYPE_INSTANCE_GET_CLASS((o), GST_TYPE_NV_VIDEO_RENDERER, GstNvVideoRendererClass))

struct _GstNvVideoRendererClass
{
  GstObjectClass parent_class;

  gboolean (*cuda_init)          (GstNvVideoContext *context, GstNvVideoRenderer * renderer);
  void (*cuda_cleanup)           (GstNvVideoContext *context, GstNvVideoRenderer * renderer);
  gboolean  (*setup)             (GstNvVideoRenderer * renderer);
  void      (*cleanup)           (GstNvVideoRenderer * renderer);
  void      (*update_viewport)   (GstNvVideoRenderer * renderer, int width, int height);
  gboolean  (*fill_texture)     (GstNvVideoContext *context, GstNvVideoRenderer * renderer, GstBuffer * buf);
  gboolean  (*cuda_buffer_copy)     (GstNvVideoContext *context, GstNvVideoRenderer * renderer, GstBuffer * buf);
  gboolean  (*draw_2D_Texture)     (GstNvVideoRenderer * renderer);
  gboolean  (*draw_eglimage)     (GstNvVideoRenderer * renderer, void * image);
};

struct _GstNvVideoRenderer
{
  GstObject parent;

  GstNvVideoContext * context;

  GstVideoFormat format;
};

GST_EXPORT
GstNvVideoRenderer * gst_nv_video_renderer_new (GstNvVideoContext * context, const char *name);

GST_EXPORT
gboolean gst_nv_video_renderer_cuda_init (GstNvVideoContext * context, GstNvVideoRenderer * renderer);

GST_EXPORT
void gst_nv_video_renderer_cuda_cleanup (GstNvVideoContext * context, GstNvVideoRenderer * renderer);

GST_EXPORT
gboolean gst_nv_video_renderer_setup (GstNvVideoRenderer * renderer);

GST_EXPORT
void gst_nv_video_renderer_cleanup (GstNvVideoRenderer * renderer);

GST_EXPORT
void gst_nv_video_renderer_update_viewport (GstNvVideoRenderer * renderer, int width, int height);

GST_EXPORT
gboolean gst_nv_video_renderer_fill_texture (GstNvVideoContext *context, GstNvVideoRenderer * renderer, GstBuffer * buf);

GST_EXPORT
gboolean gst_nv_video_renderer_cuda_buffer_copy (GstNvVideoContext *context, GstNvVideoRenderer * renderer, GstBuffer * buf);

GST_EXPORT
gboolean gst_nv_video_renderer_draw_2D_Texture (GstNvVideoRenderer * renderer);

GST_EXPORT
gboolean gst_nv_video_renderer_draw_eglimage (GstNvVideoRenderer * renderer, void * image);

GType gst_nv_video_renderer_get_type (void);

G_END_DECLS

#endif /* __GST_NV_VIDEO_RENDERER_H__ */
