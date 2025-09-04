/*
 * Copyright (c) 2018-2024, NVIDIA CORPORATION.  All rights reserved.
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

#include "gstnv3dsink.h"
#include "display.h"
#include "context.h"
#include "window.h"

GST_DEBUG_CATEGORY (gst_debug_nv3dsink);
#define GST_CAT_DEFAULT gst_debug_nv3dsink

GST_DEBUG_CATEGORY_EXTERN (GST_CAT_PERFORMANCE);

#define GST_CAPS_FEATURE_MEMORY_NVMM "memory:NVMM"

static void gst_nv3dsink_videooverlay_init (GstVideoOverlayInterface * iface);
static void gst_nv3dsink_set_window_handle (GstVideoOverlay * overlay,
    guintptr id);
static void gst_nv3dsink_expose (GstVideoOverlay * overlay);
static void gst_nv3dsink_handle_events (GstVideoOverlay * overlay,
    gboolean handle_events);
static void gst_nv3dsink_set_render_rectangle (GstVideoOverlay * overlay,
    gint x, gint y, gint width, gint height);

/* Input capabilities. */
static GstStaticPadTemplate gst_nv3dsink_sink_template_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
        //Supported Software buffer caps
        GST_VIDEO_CAPS_MAKE ("{ "
            "RGBA, BGRA, ARGB, ABGR, " "RGBx, BGRx, xRGB, xBGR, "
            "AYUV, Y444, I420, YV12, " "NV12, NV21, Y42B, Y41B, "
            "RGB, BGR, RGB16 }")
            ";"
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES (
            GST_CAPS_FEATURE_MEMORY_NVMM,
            "{ RGBA, I420, NV12 }")
        ));

#define parent_class gst_nv3dsink_parent_class
G_DEFINE_TYPE_WITH_CODE (GstNv3dSink, gst_nv3dsink, GST_TYPE_VIDEO_SINK,
    G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_OVERLAY,
        gst_nv3dsink_videooverlay_init);
    GST_DEBUG_CATEGORY_INIT (gst_debug_nv3dsink, "nv3dsink", 0,
        "Nvidia 3D sink"));

enum
{
  PROP_0,
  PROP_WINDOW_X,
  PROP_WINDOW_Y,
  PROP_WINDOW_WIDTH,
  PROP_WINDOW_HEIGHT
};

/* GObject vmethod implementations */

static void
gst_nv3dsink_videooverlay_init (GstVideoOverlayInterface * iface)
{
  iface->set_window_handle = gst_nv3dsink_set_window_handle;
  iface->expose = gst_nv3dsink_expose;
  iface->handle_events = gst_nv3dsink_handle_events;
  iface->set_render_rectangle = gst_nv3dsink_set_render_rectangle;
}

static void
gst_nv3dsink_set_window_handle (GstVideoOverlay * overlay, guintptr id)
{
  GstNv3dSink *nv3dsink = GST_NV3DSINK (overlay);
  gint width = 0;
  gint height = 0;

  g_return_if_fail (GST_IS_NV3DSINK (nv3dsink));

  g_mutex_lock (&nv3dsink->win_handle_lock);

  GST_DEBUG_OBJECT (nv3dsink, "set_window_handle %" G_GUINT64_FORMAT, id);

  if (gst_nv_video_window_get_handle (nv3dsink->window) == id) {
    g_mutex_unlock (&nv3dsink->win_handle_lock);
    return;
  }

  if (id) {
    gst_nv_video_window_set_handle (nv3dsink->window, id);
    g_mutex_unlock (&nv3dsink->win_handle_lock);
    return;
  }

  if (!GST_VIDEO_SINK_WIDTH (nv3dsink) || !GST_VIDEO_SINK_HEIGHT (nv3dsink)) {
    // window will be created during caps negotiation
    g_mutex_unlock (&nv3dsink->win_handle_lock);
    return;
  }
  // create internal window
  if (nv3dsink->window_width != 0 && nv3dsink->window_height != 0) {
    width = nv3dsink->window_width;
    height = nv3dsink->window_height;
  } else {
    width = GST_VIDEO_SINK_WIDTH (nv3dsink);
    height = GST_VIDEO_SINK_HEIGHT (nv3dsink);
  }
  if (!gst_nv_video_window_create_window (nv3dsink->window,
          nv3dsink->window_x, nv3dsink->window_y, width, height)) {
    g_mutex_unlock (&nv3dsink->win_handle_lock);
    return;
  }

  g_mutex_unlock (&nv3dsink->win_handle_lock);
}

static void
gst_nv3dsink_expose (GstVideoOverlay * overlay)
{
  GstNv3dSink *nv3dsink = GST_NV3DSINK (overlay);

  GST_DEBUG_OBJECT (nv3dsink, "expose unimplemented");
}

static void
gst_nv3dsink_handle_events (GstVideoOverlay * overlay, gboolean handle_events)
{
  GstNv3dSink *nv3dsink = GST_NV3DSINK (overlay);

  GST_DEBUG_OBJECT (nv3dsink, "handle_events unimplemented");
}

static void
gst_nv3dsink_set_render_rectangle (GstVideoOverlay * overlay, gint x, gint y,
    gint width, gint height)
{
  GstNv3dSink *nv3dsink = GST_NV3DSINK (overlay);

  g_return_if_fail (GST_IS_NV3DSINK (nv3dsink));

  GST_DEBUG_OBJECT (nv3dsink, "set_render_rectangle unimplemented");

  return;
}

static void
gst_nv3dsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstNv3dSink *nv3dsink;

  g_return_if_fail (GST_IS_NV3DSINK (object));

  nv3dsink = GST_NV3DSINK (object);

  switch (prop_id) {
    case PROP_WINDOW_X:
      nv3dsink->window_x = g_value_get_uint (value);
      break;
    case PROP_WINDOW_Y:
      nv3dsink->window_y = g_value_get_uint (value);
      break;
    case PROP_WINDOW_WIDTH:
      nv3dsink->window_width = g_value_get_uint (value);
      break;
    case PROP_WINDOW_HEIGHT:
      nv3dsink->window_height = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_nv3dsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstNv3dSink *nv3dsink;

  g_return_if_fail (GST_IS_NV3DSINK (object));

  nv3dsink = GST_NV3DSINK (object);

  switch (prop_id) {
    case PROP_WINDOW_X:
      g_value_set_uint (value, nv3dsink->window_x);
      break;
    case PROP_WINDOW_Y:
      g_value_set_uint (value, nv3dsink->window_y);
      break;
    case PROP_WINDOW_WIDTH:
      g_value_set_uint (value, nv3dsink->window_width);
      break;
    case PROP_WINDOW_HEIGHT:
      g_value_set_uint (value, nv3dsink->window_height);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_nv3dsink_finalize (GObject * object)
{
  GstNv3dSink *nv3dsink;

  g_return_if_fail (GST_IS_NV3DSINK (object));

  nv3dsink = GST_NV3DSINK (object);

  GST_TRACE_OBJECT (nv3dsink, "finalize");

  g_mutex_clear (&nv3dsink->win_handle_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_nv3dsink_start (GstBaseSink * bsink)
{
  GstNv3dSink *nv3dsink = GST_NV3DSINK (bsink);
  GstNvVideoWindow *window;

  GST_TRACE_OBJECT (nv3dsink, "start");

  // TODO: Query display from application/upstream elements if there
  // is such use case.

  if (!nv3dsink->display) {
    if (!gst_nv_video_display_new (&nv3dsink->display)) {
      GST_ERROR_OBJECT (nv3dsink, "failed to create new display");
      return FALSE;
    }
  } else {
    GST_DEBUG_OBJECT (nv3dsink, "using existing display (%p)",
        nv3dsink->display);
  }

  if (!nv3dsink->context) {
    if (!gst_nv_video_display_create_context (nv3dsink->display,
            &nv3dsink->context)) {
      GST_ERROR_OBJECT (nv3dsink, "failed to create new context");
      return FALSE;
    }
  } else {
    GST_DEBUG_OBJECT (nv3dsink, "using existing context (%p)",
        nv3dsink->context);
  }

  if (!nv3dsink->window) {
    window = gst_nv_video_display_create_window (nv3dsink->display);
    if (window == NULL) {
      GST_ERROR_OBJECT (nv3dsink, "failed to create new window");
      return FALSE;
    }
    nv3dsink->window = gst_object_ref (window);
    gst_object_unref (window);
    gst_nv_video_context_set_window (nv3dsink->context, nv3dsink->window);
  } else {
    GST_DEBUG_OBJECT (nv3dsink, "using existing window (%p)", nv3dsink->window);
  }

  return TRUE;
}

static gboolean
gst_nv3dsink_stop (GstBaseSink * bsink)
{
  GstNv3dSink *nv3dsink = GST_NV3DSINK (bsink);

  GST_TRACE_OBJECT (nv3dsink, "stop");

  if (nv3dsink->configured_caps) {
    gst_caps_unref (nv3dsink->configured_caps);
    nv3dsink->configured_caps = NULL;
  }

  if (nv3dsink->context) {
    gst_object_unref (nv3dsink->context);
    nv3dsink->context = NULL;
  }

  if (nv3dsink->window) {
    g_object_unref (nv3dsink->window);
    nv3dsink->window = NULL;
  }

  if (nv3dsink->display) {
    g_object_unref (nv3dsink->display);
    nv3dsink->display = NULL;
  }

  return TRUE;
}

static GstCaps *
gst_nv3dsink_get_caps (GstBaseSink * bsink, GstCaps * filter)
{
  GstNv3dSink *nv3dsink;
  GstCaps *tmp = NULL;
  GstCaps *result = NULL;
  GstCaps *caps = NULL;

  nv3dsink = GST_NV3DSINK (bsink);

  tmp = gst_pad_get_pad_template_caps (GST_BASE_SINK_PAD (bsink));

  if (filter) {
    GST_DEBUG_OBJECT (bsink, "intersecting with filter caps %" GST_PTR_FORMAT,
        filter);

    result = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
  } else {
    result = tmp;
  }

  caps = gst_nv_video_context_get_caps (nv3dsink->context);
  if (caps) {
    tmp = result;
    result = gst_caps_intersect (result, caps);
    gst_caps_unref (caps);
    gst_caps_unref (tmp);
  }

  GST_DEBUG_OBJECT (bsink, "returning caps: %" GST_PTR_FORMAT, result);

  return result;
}

static gboolean
gst_nv3dsink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstNv3dSink *nv3dsink;
  GstVideoInfo info;
  GstCapsFeatures *features;
  gint width = 0;
  gint height = 0;

  nv3dsink = GST_NV3DSINK (bsink);

  if (!nv3dsink->context || !nv3dsink->display) {
    return FALSE;
  }

  GST_DEBUG_OBJECT (bsink, "set caps with %" GST_PTR_FORMAT, caps);

  if (nv3dsink->configured_caps) {
    if (gst_caps_can_intersect (caps, nv3dsink->configured_caps)) {
      return TRUE;
    }
  }

  features = gst_caps_get_features (caps, 0);
  if (gst_caps_features_contains (features, GST_CAPS_FEATURE_MEMORY_NVMM)) {
    nv3dsink->context->using_NVMM = 1;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (nv3dsink, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  nv3dsink->context->configured_info = info;

  int is_res_changed = 0;

  if ((GST_VIDEO_SINK_WIDTH (nv3dsink)!=0 && GST_VIDEO_SINK_HEIGHT (nv3dsink)!=0) && (GST_VIDEO_SINK_WIDTH (nv3dsink) != info.width || GST_VIDEO_SINK_HEIGHT (nv3dsink) != info.height)) {
    is_res_changed = 1;
  }

  if (is_res_changed)
  {
    gst_nv_video_context_handle_tearing(nv3dsink->context);
  }

  GST_VIDEO_SINK_WIDTH (nv3dsink) = info.width;
  GST_VIDEO_SINK_HEIGHT (nv3dsink) = info.height;

  g_mutex_lock (&nv3dsink->win_handle_lock);
  if (!gst_nv_video_window_get_handle (nv3dsink->window)) {
    g_mutex_unlock (&nv3dsink->win_handle_lock);
    gst_video_overlay_prepare_window_handle (GST_VIDEO_OVERLAY (nv3dsink));
  } else {
    g_mutex_unlock (&nv3dsink->win_handle_lock);
  }

  if (GST_VIDEO_SINK_WIDTH (nv3dsink) <= 0
      || GST_VIDEO_SINK_HEIGHT (nv3dsink) <= 0) {
    GST_ERROR_OBJECT (nv3dsink, "invalid size");
    return FALSE;
  }

  g_mutex_lock (&nv3dsink->win_handle_lock);
  if (!gst_nv_video_window_get_handle (nv3dsink->window)) {
    if (nv3dsink->window_width != 0 && nv3dsink->window_height != 0) {
      width = nv3dsink->window_width;
      height = nv3dsink->window_height;
    } else {
      width = GST_VIDEO_SINK_WIDTH (nv3dsink);
      height = GST_VIDEO_SINK_HEIGHT (nv3dsink);
    }
    if (!gst_nv_video_window_create_window (nv3dsink->window,
            nv3dsink->window_x, nv3dsink->window_y, width, height)) {
      g_mutex_unlock (&nv3dsink->win_handle_lock);
      return FALSE;
    }
  }
  g_mutex_unlock (&nv3dsink->win_handle_lock);

  gst_caps_replace (&nv3dsink->configured_caps, caps);

  return TRUE;
}

static gboolean
gst_nv3dsink_propose_allocation (GstBaseSink * bsink, GstQuery * query)
{
  GstNv3dSink *nv3dsink = GST_NV3DSINK (bsink);

  gst_nv_video_context_handle_drc (nv3dsink->context);
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
  gst_query_add_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE, NULL);

  return TRUE;
}

static GstFlowReturn
gst_nv3dsink_show_frame (GstVideoSink * vsink, GstBuffer * buf)
{
  GstNv3dSink *nv3dsink;

  nv3dsink = GST_NV3DSINK (vsink);

  GST_TRACE_OBJECT (nv3dsink, "show buffer %p, window size:%ux%u", buf,
      GST_VIDEO_SINK_WIDTH (nv3dsink), GST_VIDEO_SINK_HEIGHT (nv3dsink));

  if (!gst_nv_video_context_show_frame (nv3dsink->context, buf)) {
    return GST_FLOW_FLUSHING;
  }

  return GST_FLOW_OK;
}

static gboolean
gst_nv3dsink_event (GstBaseSink * bsink, GstEvent * event)
{
  GstNv3dSink *nv3dsink = GST_NV3DSINK (bsink);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      gst_nv_video_context_handle_eos (nv3dsink->context);
      break;

    default:
      break;
  }

  if (GST_BASE_SINK_CLASS (parent_class)->event)
    return GST_BASE_SINK_CLASS (parent_class)->event (bsink, event);
  else
    gst_event_unref (event);

  return TRUE;
}

static GstStateChangeReturn
gst_nv3dsink_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstNv3dSink *nv3dsink = GST_NV3DSINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* call handle_eos to unref last buffer */
      gst_nv_video_context_handle_eos (nv3dsink->context);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
}

/* initialize the plugin's class */
static void
gst_nv3dsink_class_init (GstNv3dSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;
  GstVideoSinkClass *gstvideosink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstvideosink_class = (GstVideoSinkClass *) klass;

  gobject_class->set_property = gst_nv3dsink_set_property;
  gobject_class->get_property = gst_nv3dsink_get_property;

  gst_element_class_set_static_metadata (gstelement_class, "Nvidia 3D sink",
      "Sink/Video", "A videosink based on 3D graphics rendering API",
      "Yogish Kulkarni <yogishk@nvidia.com>");

  gobject_class->finalize = gst_nv3dsink_finalize;

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_nv3dsink_sink_template_factory);

  gstbasesink_class->start = GST_DEBUG_FUNCPTR (gst_nv3dsink_start);
  gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_nv3dsink_stop);
  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_nv3dsink_set_caps);
  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_nv3dsink_get_caps);
  gstbasesink_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_nv3dsink_propose_allocation);

  gstbasesink_class->event = gst_nv3dsink_event;

  gstvideosink_class->show_frame = GST_DEBUG_FUNCPTR (gst_nv3dsink_show_frame);
  gstelement_class->change_state = gst_nv3dsink_change_state;

  g_object_class_install_property (gobject_class, PROP_WINDOW_X,
      g_param_spec_uint ("window-x",
          "Window x coordinate",
          "X coordinate of window", 0, G_MAXINT, 10,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_WINDOW_Y,
      g_param_spec_uint ("window-y",
          "Window y coordinate",
          "Y coordinate of window", 0, G_MAXINT, 10,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_WINDOW_WIDTH,
      g_param_spec_uint ("window-width",
          "Window width",
          "Width of window", 0, G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_WINDOW_HEIGHT,
      g_param_spec_uint ("window-height",
          "Window height",
          "Height of window", 0, G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

/* initialize the new element */
static void
gst_nv3dsink_init (GstNv3dSink * nv3dsink)
{
  GST_TRACE_OBJECT (nv3dsink, "init");

  nv3dsink->display = NULL;
  nv3dsink->context = NULL;
  nv3dsink->window = NULL;
  nv3dsink->window_x = 0;
  nv3dsink->window_y = 0;
  nv3dsink->window_width = 0;
  nv3dsink->window_height = 0;

  nv3dsink->configured_caps = NULL;

  /* mutex to serialize create, set and get window handle calls */
  g_mutex_init (&nv3dsink->win_handle_lock);
}
