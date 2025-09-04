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

#ifndef __GST_NV_VIDEO_WINDOW_H__
#define __GST_NV_VIDEO_WINDOW_H__

#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/video.h>

#include "gstnvvideofwd.h"

G_BEGIN_DECLS

#define GST_TYPE_NV_VIDEO_WINDOW \
    (gst_nv_video_window_get_type())
#define GST_NV_VIDEO_WINDOW(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NV_VIDEO_WINDOW, GstNvVideoWindow))
#define GST_NV_VIDEO_WINDOW_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_NV_VIDEO_WINDOW, GstNvVideoWindowClass))
#define GST_IS_NV_VIDEO_WINDOW(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_NV_VIDEO_WINDOW))
#define GST_IS_NV_VIDEO_WINDOW_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_NV_VIDEO_WINDOW))
#define GST_NV_VIDEO_WINDOW_CAST(obj) \
    ((GstNvVideoWindow*)(obj))
#define GST_NV_VIDEO_WINDOW_GET_CLASS(o) \
    (G_TYPE_INSTANCE_GET_CLASS((o), GST_TYPE_NV_VIDEO_WINDOW, GstNvVideoWindowClass))

struct _GstNvVideoWindowClass
{
  GstObjectClass parent_class;

  guintptr (*get_handle)    (GstNvVideoWindow * window);
  gboolean (*set_handle)    (GstNvVideoWindow * window, guintptr id);
  gboolean (*create_window) (GstNvVideoWindow * window, gint x, gint y, gint width, gint height);
  gboolean (*draw)          (GstNvVideoWindow * window, GstBuffer * buf);
};

struct _GstNvVideoWindow
{
  GstObject parent;

  GstNvVideoDisplay *display;

  GWeakRef context;
};

GST_EXPORT
GstNvVideoWindow *gst_nv_video_window_new (GstNvVideoDisplay * display);
GST_EXPORT
gboolean gst_nv_video_window_create_window (GstNvVideoWindow * window, gint x, gint y, gint width, gint height);
GST_EXPORT
gboolean gst_nv_video_window_set_handle (GstNvVideoWindow * window, guintptr id);
GST_EXPORT
guintptr gst_nv_video_window_get_handle (GstNvVideoWindow * window);
GST_EXPORT
GstNvVideoContext *gst_nv_video_window_get_context (GstNvVideoWindow * window);

GType gst_nv_video_window_get_type (void);

G_END_DECLS

#endif /* __GST_NV_VIDEO_WINDOW_H__ */
