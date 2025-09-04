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

#include "display_x11.h"

G_GNUC_INTERNAL extern GstDebugCategory *gst_debug_nv_video_display;
#define GST_CAT_DEFAULT gst_debug_nv_video_display

G_DEFINE_TYPE (GstNvVideoDisplayX11, gst_nv_video_display_x11,
    GST_TYPE_NV_VIDEO_DISPLAY);

static void
gst_nv_video_display_x11_finalize (GObject * object)
{
  GstNvVideoDisplayX11 *display_x11 = GST_NV_VIDEO_DISPLAY_X11 (object);

  GST_DEBUG ("closing X11 display connection, handle=%p", display_x11->dpy);

  if (display_x11->dpy) {
    XCloseDisplay (display_x11->dpy);
  }

  GST_DEBUG ("closed X11 display connection");

  G_OBJECT_CLASS (gst_nv_video_display_x11_parent_class)->finalize (object);
}

static guintptr
gst_nv_video_display_x11_get_handle (GstNvVideoDisplay * display)
{
  return (guintptr) GST_NV_VIDEO_DISPLAY_X11 (display)->dpy;
}

static void
gst_nv_video_display_x11_class_init (GstNvVideoDisplayX11Class * klass)
{
  GST_NV_VIDEO_DISPLAY_CLASS (klass)->get_handle =
      GST_DEBUG_FUNCPTR (gst_nv_video_display_x11_get_handle);
  G_OBJECT_CLASS (klass)->finalize = gst_nv_video_display_x11_finalize;
}

static void
gst_nv_video_display_x11_init (GstNvVideoDisplayX11 * display_x11)
{
  GstNvVideoDisplay *display = (GstNvVideoDisplay *) display_x11;

  display->type = GST_NV_VIDEO_DISPLAY_TYPE_X11;

  GST_DEBUG_OBJECT (display, "init done");
}

GstNvVideoDisplayX11 *
gst_nv_video_display_x11_new (const gchar * name)
{
  GstNvVideoDisplayX11 *ret;

  ret = g_object_new (GST_TYPE_NV_VIDEO_DISPLAY_X11, NULL);
  gst_object_ref_sink (ret);

  ret->dpy = XOpenDisplay (NULL);

  if (!ret->dpy) {
    GST_ERROR ("failed to open X11 display connection");
    gst_object_unref (ret);
    return NULL;
  }

  GST_DEBUG ("opened X11 display connection handle=%p", ret->dpy);

  return ret;
}
