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

#include "display.h"
#include "context.h"
#include "window.h"

#if NV_VIDEO_SINKS_HAS_X11
#include "display_x11.h"
#endif

#define GST_CAT_DEFAULT gst_debug_nv_video_display
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

G_DEFINE_ABSTRACT_TYPE (GstNvVideoDisplay, gst_nv_video_display,
    GST_TYPE_OBJECT);

GstNvVideoDisplayType
gst_nv_video_display_get_handle_type (GstNvVideoDisplay * display)
{
  g_return_val_if_fail (GST_IS_NV_VIDEO_DISPLAY (display),
      GST_NV_VIDEO_DISPLAY_TYPE_NONE);

  return display->type;
}

static void
gst_nv_video_display_init (GstNvVideoDisplay * display)
{

}

gboolean
gst_nv_video_display_create_context (GstNvVideoDisplay * display,
    GstNvVideoContext ** ptr_context)
{
  GstNvVideoContext *context = NULL;

  g_return_val_if_fail (display != NULL, FALSE);
  g_return_val_if_fail (ptr_context != NULL, FALSE);

  context = gst_nv_video_context_new (display);
  if (!context) {
    GST_ERROR ("context creation failed");
    return FALSE;
  }

  if (!gst_nv_video_context_create (context)) {
    return FALSE;
  }

  *ptr_context = context;

  GST_DEBUG_OBJECT (display, "created context %" GST_PTR_FORMAT, context);

  return TRUE;
}

GstNvVideoWindow *
gst_nv_video_display_create_window (GstNvVideoDisplay * display)
{
  return gst_nv_video_window_new (display);
}

static void
gst_nv_video_display_class_init (GstNvVideoDisplayClass * klass)
{
}

gboolean
gst_nv_video_display_new (GstNvVideoDisplay ** display)
{
  static volatile gsize debug_init = 0;
  const gchar *winsys_name = NULL;

  if (g_once_init_enter (&debug_init)) {
    GST_DEBUG_CATEGORY_INIT (gst_debug_nv_video_display, "nvvideodisplay", 0,
        "nvvideodisplay");
    g_once_init_leave (&debug_init, 1);
  }

  winsys_name = g_getenv ("GST_NV_VIDEO_WINSYS");

#if NV_VIDEO_SINKS_HAS_X11
  if (!*display && (!winsys_name || g_strstr_len (winsys_name, 3, "x11"))) {
    *display = GST_NV_VIDEO_DISPLAY (gst_nv_video_display_x11_new (NULL));
  }
#endif

  if (!*display) {
    GST_ERROR ("couldn't create display. GST_NV_VIDEO_WINSYS = %s",
        winsys_name ? winsys_name : NULL);
    return FALSE;
  }

  return TRUE;
}
