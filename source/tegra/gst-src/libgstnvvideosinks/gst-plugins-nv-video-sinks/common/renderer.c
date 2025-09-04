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

#include <gst/base/gstdataqueue.h>

#include "renderer.h"
#include "context.h"

#if NV_VIDEO_SINKS_HAS_GL
#include "renderer_gl.h"
#endif

#define GST_CAT_DEFAULT gst_debug_nv_video_renderer
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

G_DEFINE_ABSTRACT_TYPE (GstNvVideoRenderer, gst_nv_video_renderer,
    GST_TYPE_OBJECT);

static void
gst_nv_video_renderer_init (GstNvVideoRenderer * renderer)
{
}

static void
gst_nv_video_renderer_class_init (GstNvVideoRendererClass * klass)
{
}

GstNvVideoRenderer *
gst_nv_video_renderer_new (GstNvVideoContext * context, const char *name)
{
  GstNvVideoRenderer *renderer = NULL;
  static volatile gsize debug_init = 0;

  if (g_once_init_enter (&debug_init)) {
    GST_DEBUG_CATEGORY_INIT (gst_debug_nv_video_renderer, "nvvideorenderer", 0,
        "nvvideorenderer");
    g_once_init_leave (&debug_init, 1);
  }

  if (!name) {
    GST_ERROR ("renderer name not valid");
  }

#if NV_VIDEO_SINKS_HAS_GL
  if (g_strstr_len (name, 2, "gl")) {
    renderer = GST_NV_VIDEO_RENDERER (gst_nv_video_renderer_gl_new (context));
  }
#endif

  if (!renderer) {
    GST_ERROR ("couldn't create renderer name = %s", name);
    return NULL;
  }

  renderer->format = context->configured_info.finfo->format;

  GST_DEBUG_OBJECT (renderer, "created %s renderer for context %" GST_PTR_FORMAT, name, context);

  return renderer;
}

gboolean
gst_nv_video_renderer_cuda_init (GstNvVideoContext * context, GstNvVideoRenderer * renderer)
{
  GstNvVideoRendererClass *renderer_class;

  renderer_class = GST_NV_VIDEO_RENDERER_GET_CLASS (renderer);

  return renderer_class->cuda_init (context, renderer);
}

void
gst_nv_video_renderer_cuda_cleanup (GstNvVideoContext * context, GstNvVideoRenderer * renderer)
{
  GstNvVideoRendererClass *renderer_class;

  renderer_class = GST_NV_VIDEO_RENDERER_GET_CLASS (renderer);

  renderer_class->cuda_cleanup (context, renderer);
}

void
gst_nv_video_renderer_cleanup (GstNvVideoRenderer * renderer)
{
  GstNvVideoRendererClass *renderer_class;

  renderer_class = GST_NV_VIDEO_RENDERER_GET_CLASS (renderer);

  renderer_class->cleanup (renderer);
}

gboolean
gst_nv_video_renderer_setup (GstNvVideoRenderer * renderer)
{
  GstNvVideoRendererClass *renderer_class;

  renderer_class = GST_NV_VIDEO_RENDERER_GET_CLASS (renderer);

  return renderer_class->setup (renderer);
}

void
gst_nv_video_renderer_update_viewport (GstNvVideoRenderer * renderer, int width, int height)
{
  GstNvVideoRendererClass *renderer_class;

  renderer_class = GST_NV_VIDEO_RENDERER_GET_CLASS (renderer);

  renderer_class->update_viewport (renderer, width, height);
}

gboolean
gst_nv_video_renderer_fill_texture (GstNvVideoContext *context, GstNvVideoRenderer * renderer, GstBuffer * buf)
{
  GstNvVideoRendererClass *renderer_class;

  renderer_class = GST_NV_VIDEO_RENDERER_GET_CLASS (renderer);

  return renderer_class->fill_texture (context, renderer, buf);
}

gboolean
gst_nv_video_renderer_cuda_buffer_copy (GstNvVideoContext *context, GstNvVideoRenderer * renderer, GstBuffer * buf)
{
  GstNvVideoRendererClass *renderer_class;

  renderer_class = GST_NV_VIDEO_RENDERER_GET_CLASS (renderer);

  return renderer_class->cuda_buffer_copy (context, renderer, buf);
}

gboolean
gst_nv_video_renderer_draw_2D_Texture (GstNvVideoRenderer * renderer)
{
  GstNvVideoRendererClass *renderer_class;

  renderer_class = GST_NV_VIDEO_RENDERER_GET_CLASS (renderer);

  return renderer_class->draw_2D_Texture (renderer);
}

gboolean
gst_nv_video_renderer_draw_eglimage (GstNvVideoRenderer * renderer, void * image)
{
  GstNvVideoRendererClass *renderer_class;

  renderer_class = GST_NV_VIDEO_RENDERER_GET_CLASS (renderer);

  return renderer_class->draw_eglimage (renderer, image);
}
