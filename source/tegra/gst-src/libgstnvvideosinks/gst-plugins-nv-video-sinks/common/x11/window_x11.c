/**
 * Copyright (c) 2018, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#include "context.h"
#include "display_x11.h"
#include "window_x11.h"
#include <X11/Xutil.h>

G_GNUC_INTERNAL extern GstDebugCategory *gst_debug_nv_video_window;
#define GST_CAT_DEFAULT gst_debug_nv_video_window

#define gst_nv_video_window_x11_parent_class parent_class
G_DEFINE_TYPE (GstNvVideoWindowX11, gst_nv_video_window_x11,
    GST_TYPE_NV_VIDEO_WINDOW);

static void
gst_nv_video_window_x11_destroy (GstNvVideoWindow * window)
{
  GstNvVideoWindowX11 *window_x11 = GST_NV_VIDEO_WINDOW_X11 (window);
  GstNvVideoDisplayX11 *display_x11 = (GstNvVideoDisplayX11 *) window->display;

  if (window_x11->internal_window) {
    GST_DEBUG_OBJECT (window, "destroy internal window %" G_GUINTPTR_FORMAT,
        window_x11->handle);

    XUnmapWindow (display_x11->dpy, window_x11->handle);
    XDestroyWindow (display_x11->dpy, window_x11->handle);
    XSync (display_x11->dpy, FALSE);
    window_x11->internal_window = FALSE;
    window_x11->handle = 0;
  } else {
    GST_DEBUG_OBJECT (window, "unset foreign window handle %" G_GUINTPTR_FORMAT,
        window_x11->handle);
    window_x11->handle = 0;
  }
}

static void
gst_nv_video_window_x11_finalize (GObject * object)
{
  GstNvVideoWindow *window = GST_NV_VIDEO_WINDOW (object);

  GST_DEBUG_OBJECT (window, "finalize begin");

  gst_nv_video_window_x11_destroy (window);

  G_OBJECT_CLASS (gst_nv_video_window_x11_parent_class)->finalize (object);

  GST_DEBUG_OBJECT (window, "finalize end");
}

static guintptr
gst_nv_video_window_x11_get_handle (GstNvVideoWindow * window)
{
  GstNvVideoWindowX11 *window_x11 = GST_NV_VIDEO_WINDOW_X11 (window);

  return window_x11->handle;
}

static gboolean
gst_nv_video_window_x11_set_handle (GstNvVideoWindow * window, guintptr id)
{
  GstNvVideoWindowX11 *window_x11 = GST_NV_VIDEO_WINDOW_X11 (window);

  gst_nv_video_window_x11_destroy (window);
  window_x11->handle = id;

  GST_DEBUG_OBJECT (window, "set window handle to %" G_GUINTPTR_FORMAT, id);

  return FALSE;
}

static gboolean
gst_nv_video_window_x11_create (GstNvVideoWindow * window, gint x,
    gint y, gint width, gint height)
{
  GstNvVideoWindowX11 *window_x11 = GST_NV_VIDEO_WINDOW_X11 (window);
  GstNvVideoDisplayX11 *display_x11 = (GstNvVideoDisplayX11 *) window->display;
  Display *dpy = display_x11->dpy;
  int screen = DefaultScreen (dpy);

  XSizeHints    hints = {0};
  hints.flags  = PPosition ;
  hints.x = x;
  hints.y = y;
  // GstNvVideoWindow doesn't have destroy_winow method (like create_window)
  // and GstNvVideoWindow object can't have multiple X windows. So if
  // upper layer has existing window (foreign or internal), unset/destroy it.
  //
  // TODO: In case of existing internal window, we might able to re-use it
  // with XResizeWindow.
  gst_nv_video_window_x11_destroy (window);

  window_x11->handle = XCreateSimpleWindow (dpy, RootWindow (dpy, screen),
      hints.x, hints.y, width, height, 1,
      BlackPixel (dpy, screen), WhitePixel (dpy, screen));

  if (!window_x11->handle) {
    GST_ERROR_OBJECT (window, "failed to create internal window\n");
    return FALSE;
  }

  window_x11->internal_window = TRUE;

  XSetWindowBackgroundPixmap (dpy, window_x11->handle, None);
  XSetNormalHints(dpy, window_x11->handle, &hints);
  XMapRaised (dpy, window_x11->handle);
  XSync (dpy, FALSE);

  GST_DEBUG_OBJECT (window,
      "created internal window %dx%d, handle=%" G_GUINTPTR_FORMAT, width,
      height, window_x11->handle);

  return TRUE;
}

static void
gst_nv_video_window_x11_class_init (GstNvVideoWindowX11Class * klass)
{
  GstNvVideoWindowClass *window_class = (GstNvVideoWindowClass *) klass;

  window_class->create_window =
      GST_DEBUG_FUNCPTR (gst_nv_video_window_x11_create);
  window_class->get_handle =
      GST_DEBUG_FUNCPTR (gst_nv_video_window_x11_get_handle);
  window_class->set_handle =
      GST_DEBUG_FUNCPTR (gst_nv_video_window_x11_set_handle);

  G_OBJECT_CLASS (klass)->finalize = gst_nv_video_window_x11_finalize;
}

static void
gst_nv_video_window_x11_init (GstNvVideoWindowX11 * window)
{
  window->handle = 0;
  window->internal_window = FALSE;

  GST_DEBUG_OBJECT (window, "init done");
}

GstNvVideoWindowX11 *
gst_nv_video_window_x11_new (const gchar * name)
{
  GstNvVideoWindowX11 *ret;

  ret = g_object_new (GST_TYPE_NV_VIDEO_WINDOW_X11, NULL);
  gst_object_ref_sink (ret);

  return ret;
}
