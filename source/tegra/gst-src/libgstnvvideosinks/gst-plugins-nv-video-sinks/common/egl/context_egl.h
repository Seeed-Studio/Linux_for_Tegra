/*
 * Copyright (c) 2018, NVIDIA CORPORATION.  All rights reserved.
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

#ifndef __GST_NV_VIDEO_CONTEXT_EGL_H__
#define __GST_NV_VIDEO_CONTEXT_EGL_H__

#include "context.h"
#include "renderer.h"

G_BEGIN_DECLS

#define GST_TYPE_NV_VIDEO_CONTEXT_EGL \
    (gst_nv_video_context_egl_get_type())
#define GST_NV_VIDEO_CONTEXT_EGL(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_NV_VIDEO_CONTEXT_EGL, GstNvVideoContextEgl))
#define GST_NV_VIDEO_CONTEXT_EGL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_NV_VIDEO_CONTEXT_EGL, GstNvVideoContextEglClass))
#define GST_IS_NV_VIDEO_CONTEXT_EGL(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_NV_VIDEO_CONTEXT_EGL))
#define GST_IS_NV_VIDEO_CONTEXT_EGL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_NV_VIDEO_CONTEXT_EGL))
#define GST_NV_VIDEO_CONTEXT_EGL_CAST(obj) \
    ((GstNvVideoContextEgl*)(obj))

typedef struct _GstNvVideoContextEgl GstNvVideoContextEgl;
typedef struct _GstNvVideoContextEglClass GstNvVideoContextEglClass;

struct _GstNvVideoContextEgl
{
  GstNvVideoContext parent;

  gpointer context;
  gpointer display;
  gpointer surface;
  gpointer config;

  gint surface_width;
  gint surface_height;

  GstNvVideoRenderer *renderer;

  GstCaps *caps;

  GstBuffer *last_buf;

  gint is_drc_on;
};

struct _GstNvVideoContextEglClass
{
  GstNvVideoContextClass parent_class;
};

G_GNUC_INTERNAL
GstNvVideoContextEgl * gst_nv_video_context_egl_new (GstNvVideoDisplay * display);

GType gst_nv_video_context_egl_get_type (void);

G_END_DECLS

#endif /* __GST_NV_VIDEO_CONTEXT_EGL_H__ */
