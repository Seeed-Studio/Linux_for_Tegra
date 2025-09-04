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

#include "window.h"

#if NV_VIDEO_SINKS_HAS_X11
#include "window_x11.h"
#endif

#define GST_CAT_DEFAULT gst_debug_nv_video_window
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

#define gst_nv_video_window_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstNvVideoWindow, gst_nv_video_window, GST_TYPE_OBJECT);

static void
gst_nv_video_window_finalize (GObject * object)
{
  GstNvVideoWindow *window = GST_NV_VIDEO_WINDOW (object);

  g_weak_ref_clear (&window->context);
  gst_object_unref (window->display);

  G_OBJECT_CLASS (gst_nv_video_window_parent_class)->finalize (object);
}

static void
gst_nv_video_window_init (GstNvVideoWindow * window)
{
  g_weak_ref_init (&window->context, NULL);
}

static void
gst_nv_video_window_class_init (GstNvVideoWindowClass * klass)
{
  G_OBJECT_CLASS (klass)->finalize = gst_nv_video_window_finalize;
}

GstNvVideoWindow *
gst_nv_video_window_new (GstNvVideoDisplay * display)
{
  GstNvVideoWindow *window = NULL;
  static volatile gsize debug_init = 0;
  const gchar *winsys_name = NULL;

  if (g_once_init_enter (&debug_init)) {
    GST_DEBUG_CATEGORY_INIT (gst_debug_nv_video_window, "nvvideowindow", 0,
        "nvvideowindow");
    g_once_init_leave (&debug_init, 1);
  }

  winsys_name = g_getenv ("GST_NV_VIDEO_WINSYS");

#if NV_VIDEO_SINKS_HAS_X11
  if (!window && (!winsys_name || g_strstr_len (winsys_name, 3, "x11"))) {
    window = GST_NV_VIDEO_WINDOW (gst_nv_video_window_x11_new (NULL));
  }
#endif

  if (!window) {
    GST_ERROR ("couldn't create window. GST_NV_VIDEO_WINSYS = %s",
        winsys_name ? winsys_name : NULL);
    return NULL;
  }

  window->display = gst_object_ref (display);

  GST_DEBUG_OBJECT (window, "created window for display %" GST_PTR_FORMAT,
      display);

  return window;
}

/* create new window handle after destroying existing */
gboolean
gst_nv_video_window_create_window (GstNvVideoWindow * window, gint x,
    gint y, gint width, gint height)
{
  GstNvVideoWindowClass *window_class;

  window_class = GST_NV_VIDEO_WINDOW_GET_CLASS (window);

  return window_class->create_window (window, x, y, width, height);
}

gboolean
gst_nv_video_window_set_handle (GstNvVideoWindow * window, guintptr id)
{
  GstNvVideoWindowClass *window_class;

  window_class = GST_NV_VIDEO_WINDOW_GET_CLASS (window);

  return window_class->set_handle (window, id);
}

guintptr
gst_nv_video_window_get_handle (GstNvVideoWindow * window)
{
  GstNvVideoWindowClass *window_class;
  window_class = GST_NV_VIDEO_WINDOW_GET_CLASS (window);

  return window_class->get_handle (window);
}

GstNvVideoContext *
gst_nv_video_window_get_context (GstNvVideoWindow * window)
{
  g_return_val_if_fail (GST_IS_NV_VIDEO_WINDOW (window), NULL);

  return (GstNvVideoContext *) g_weak_ref_get (&window->context);
}
