/*
 * Copyright (c) 2014-2020, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "gstnvtee.h"

#include <string.h>
#include <stdio.h>

#define SINK_CAPS \
    "video/x-raw(memory:NVMM), " \
    "width = (int) [ 1, MAX ], " \
    "height = (int) [ 1, MAX ], " \
    "format = (string) { I420, NV12 }, " \
    "format = (string) { I420, NV12, UYVY }, " \
    "framerate = (fraction) [ 0, MAX ];"

#define PREVIEW_CAPS \
    "video/x-raw(memory:NVMM), " \
    "width = (int) [ 1, MAX ], " \
    "height = (int) [ 1, MAX ], " \
    "format = (string) { I420, NV12, UYVY }, " \
    "framerate = (fraction) [ 0, MAX ];"

#define VIDEO_CAPS \
    "video/x-raw(memory:NVMM), " \
    "width = (int) [ 1, MAX ], " \
    "height = (int) [ 1, MAX ], " \
    "format = (string) { I420, NV12, UYVY }, " \
    "framerate = (fraction) [ 0, MAX ];"

#define IMAGE_CAPS \
    "video/x-raw(memory:NVMM), " \
    "width = (int) [ 1, MAX ], " \
    "height = (int) [ 1, MAX ], " \
    "format = (string) { I420, NV12, UYVY }, " \
    "framerate = (fraction) [ 0, MAX ];"

#define VIDEO_SNAP_CAPS \
    "video/x-raw(memory:NVMM), " \
    "width = (int) [ 1, MAX ], " \
    "height = (int) [ 1, MAX ], " \
    "format = (string) { I420, NV12, UYVY }, " \
    "framerate = (fraction) [ 0, MAX ];"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SINK_CAPS));

static GstStaticPadTemplate pre_src_template =
GST_STATIC_PAD_TEMPLATE ("pre_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (PREVIEW_CAPS));

static GstStaticPadTemplate img_src_template =
GST_STATIC_PAD_TEMPLATE ("img_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (IMAGE_CAPS));

static GstStaticPadTemplate vid_src_template =
GST_STATIC_PAD_TEMPLATE ("vid_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_CAPS));

static GstStaticPadTemplate vid_snap_template =
GST_STATIC_PAD_TEMPLATE ("vsnap_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_SNAP_CAPS));

GST_DEBUG_CATEGORY_STATIC (gst_nvtee_debug);
#define GST_CAT_DEFAULT gst_nvtee_debug

#define GST_TYPE_NVCAM_MODE (gst_nvcam_mode_get_type())
static GType
gst_nvcam_mode_get_type (void)
{
  static gsize mode_type = 0;
  static const GEnumValue mode[] = {
    {GST_NVCAM_MODE_IMAGE, "GST_NVCAM_MODE_IMAGE", "mode-image"},
    {GST_NVCAM_MODE_VIDEO, "GST_NVCAM_MODE_VIDEO", "mode-video"},
    {0, NULL, NULL}
  };

  if (g_once_init_enter (&mode_type)) {
    GType tmp = g_enum_register_static ("GstNvCamMode", mode);
    g_once_init_leave (&mode_type, tmp);
  }
  return (GType) mode_type;
}

enum
{
  PROP_0,
  PROP_NVCAM_MODE,
};

enum
{
  /* actions */
  SIGNAL_START_CAPTURE,
  SIGNAL_STOP_CAPTURE,
  SIGNAL_VIDEO_SNAP,
  LAST_SIGNAL
};

static guint gst_nvtee_signals[LAST_SIGNAL] = { 0 };

#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_nvtee_debug, "nvtee", 0, "nvtee element");
#define gst_nvtee_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstNvTee, gst_nvtee, GST_TYPE_ELEMENT, _do_init);

static void gst_nvtee_start_capture (GstNvTee * tee);
static void gst_nvtee_stop_capture (GstNvTee * tee);
static void gst_nvtee_do_vsnap (GstNvTee *tee);

static void gst_nvtee_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_nvtee_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_nvtee_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);

static gboolean gst_nvtee_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_nvtee_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

static void
gst_nvtee_start_capture (GstNvTee * tee)
{
  GST_OBJECT_LOCK (tee);
  tee->processing = TRUE;

  if (tee->mode == GST_NVCAM_MODE_VIDEO)
    tee->has_pending_segment = TRUE;

  GST_OBJECT_UNLOCK (tee);
  return;
}

static void
gst_nvtee_stop_capture (GstNvTee * tee)
{
  GST_OBJECT_LOCK (tee);
  tee->processing = FALSE;
  GST_OBJECT_UNLOCK (tee);
  return;
}

static void
gst_nvtee_do_vsnap (GstNvTee *tee)
{
  GST_OBJECT_LOCK (tee);

  if (tee->mode == GST_NVCAM_MODE_VIDEO)
    tee->do_vsnap = TRUE;
  else
    GST_DEBUG_OBJECT (tee, "Video sanpshot is possible only in video mode");

  GST_OBJECT_UNLOCK (tee);
  return;
}

static void
gst_nvtee_class_init (GstNvTeeClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_nvtee_set_property;
  gobject_class->get_property = gst_nvtee_get_property;

  g_object_class_install_property (gobject_class, PROP_NVCAM_MODE,
      g_param_spec_enum ("mode", "Capture Mode",
          "Capture Mode (still image or video record)", GST_TYPE_NVCAM_MODE,
          GST_NVCAM_MODE_VIDEO, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (gstelement_class,
      "NvTee",
      "Generic",
      "Convert single stream to three",
      "Jitendra Kumar <jitendrak@nvidia.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&pre_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&img_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&vid_src_template));
  gst_element_class_add_pad_template (gstelement_class,
        gst_static_pad_template_get (&vid_snap_template));

  /* Signals */
  gst_nvtee_signals[SIGNAL_START_CAPTURE] =
      g_signal_new ("start-capture",
      G_TYPE_FROM_CLASS (klass),
      (GSignalFlags) (G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
      G_STRUCT_OFFSET (GstNvTeeClass, start_capture),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  gst_nvtee_signals[SIGNAL_STOP_CAPTURE] =
      g_signal_new ("stop-capture",
      G_TYPE_FROM_CLASS (klass),
      (GSignalFlags) (G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
      G_STRUCT_OFFSET (GstNvTeeClass, stop_capture),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  gst_nvtee_signals[SIGNAL_VIDEO_SNAP] =
      g_signal_new ("take-vsnap",
      G_TYPE_FROM_CLASS (klass),
      (GSignalFlags) (G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
      G_STRUCT_OFFSET (GstNvTeeClass, take_vsnap),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  klass->start_capture = gst_nvtee_start_capture;
  klass->stop_capture = gst_nvtee_stop_capture;
  klass->take_vsnap = gst_nvtee_do_vsnap;
}

static void
gst_nvtee_init (GstNvTee * tee)
{
  tee->processing = FALSE;
  tee->mode = GST_NVCAM_MODE_VIDEO;
  tee->do_vsnap = FALSE;

  tee->sinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");
  tee->vid_pad =
      gst_pad_new_from_static_template (&pre_src_template, "vid_src");
  tee->img_pad =
      gst_pad_new_from_static_template (&img_src_template, "img_src");
  tee->pre_pad =
      gst_pad_new_from_static_template (&vid_src_template, "pre_src");
  tee->vsnap_pad =
        gst_pad_new_from_static_template (&vid_snap_template, "vsnap_src");

  gst_pad_set_event_function (tee->sinkpad,
      GST_DEBUG_FUNCPTR (gst_nvtee_sink_event));
  gst_pad_set_query_function (tee->sinkpad,
      GST_DEBUG_FUNCPTR (gst_nvtee_sink_query));
  gst_pad_set_chain_function (tee->sinkpad,
      GST_DEBUG_FUNCPTR (gst_nvtee_chain));

  /* Keep it for time being until query function is implemented. */
  GST_OBJECT_FLAG_SET (tee->sinkpad, GST_PAD_FLAG_PROXY_CAPS);

  gst_element_add_pad (GST_ELEMENT (tee), tee->sinkpad);
  gst_element_add_pad (GST_ELEMENT (tee), tee->pre_pad);
  gst_element_add_pad (GST_ELEMENT (tee), tee->img_pad);
  gst_element_add_pad (GST_ELEMENT (tee), tee->vid_pad);
  gst_element_add_pad (GST_ELEMENT (tee), tee->vsnap_pad);

  tee->gst_buffer_nvtee_usecase_quark = g_quark_from_static_string ("GstBufferUseCase");
}

static void
gst_nvtee_set_mode (GstNvTee * tee, GstNvCamMode mode)
{
  GST_OBJECT_LOCK (tee);
  if (tee->mode == mode)
    goto done;

  if (tee->processing)
    tee->processing = FALSE;    /* Stop capturing before changing the Mode */

  tee->mode = mode;

done:
  GST_OBJECT_UNLOCK (tee);
  return;
}

static void
gst_nvtee_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstNvTee *tee = GST_NVTEE (object);

  switch (prop_id) {
    case PROP_NVCAM_MODE:
      gst_nvtee_set_mode (tee, (GstNvCamMode) g_value_get_enum (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_nvtee_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstNvTee *tee = GST_NVTEE (object);

  switch (prop_id) {
    case PROP_NVCAM_MODE:
      g_value_set_enum (value, tee->mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_nvtee_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res;

  switch (GST_EVENT_TYPE (event)) {
    default:
      res = gst_pad_event_default (pad, parent, event);
      break;
  }

  return res;
}

static gboolean
gst_nvtee_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res;

  switch (GST_QUERY_TYPE (query)) {
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }
  return res;
}

static void
gst_nvtee_send_new_segment (GstPad * pad, gpointer data)
{
  GstClockTime ts;
  GstSegment segment;
  gboolean ret;

  ts = GST_BUFFER_TIMESTAMP (GST_BUFFER_CAST (data));
  if (!GST_CLOCK_TIME_IS_VALID (ts))
    ts = 0;
  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.start = ts;

  ret = gst_pad_push_event (pad, gst_event_new_segment (&segment));
  if (!ret)
    g_print ("--- Couldn't send New SEGMENT Event ------ \n");
}

static GstFlowReturn
gst_nvtee_handle_data (GstNvTee * tee, gpointer data)
{
  GstFlowReturn ret;
  GstPad *pad = NULL;
  gint *use_case =  NULL;

  use_case = (gint *) gst_mini_object_get_qdata (
      GST_MINI_OBJECT_CAST (GST_BUFFER_CAST (data)),
      tee->gst_buffer_nvtee_usecase_quark);

  if (tee->processing) {
    GST_OBJECT_LOCK (tee);
    if (tee->mode == GST_NVCAM_MODE_IMAGE && (use_case == NULL || 2 == *use_case))
    {
        /* Forward this buffer to image pad as it has quark set */
        pad = tee->img_pad;
    }
    else {
      if (use_case == NULL || 3 == *use_case || 4 == *use_case)
      {
        pad = tee->vid_pad;
        if (tee->has_pending_segment) {
          gst_nvtee_send_new_segment (pad, data);
          tee->has_pending_segment = FALSE;
        }
      }
    }
    GST_OBJECT_UNLOCK (tee);

    if (pad)
    {
      ret = gst_pad_push (pad, gst_buffer_ref (GST_BUFFER_CAST (data)));
      if (G_UNLIKELY (ret != GST_FLOW_OK && ret != GST_FLOW_NOT_LINKED))
        goto error;

      /* Send single buffer per image capture request. */
      if ((tee->mode == GST_NVCAM_MODE_IMAGE) && (pad == tee->img_pad))
        tee->processing = FALSE;
    }

    if (tee->do_vsnap) {
      ret = gst_pad_push (tee->vsnap_pad, gst_buffer_ref (GST_BUFFER_CAST (data)));
      if (G_UNLIKELY (ret != GST_FLOW_OK && ret != GST_FLOW_NOT_LINKED))
        goto error;

      /* Send single buffer per video snapshot request */
      tee->do_vsnap = FALSE;
    }
  }

  ret = gst_pad_push (tee->pre_pad, gst_buffer_ref (GST_BUFFER_CAST (data)));
  if (G_UNLIKELY (ret != GST_FLOW_OK && ret != GST_FLOW_NOT_LINKED))
    goto error;

  gst_mini_object_unref (GST_MINI_OBJECT_CAST (data));
  return ret;

error:
  GST_DEBUG_OBJECT (tee, "received error %s", gst_flow_get_name (ret));
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (data));
  return ret;
}

static GstFlowReturn
gst_nvtee_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstFlowReturn res;
  GstNvTee *tee;

  tee = GST_NVTEE_CAST (parent);

  res = gst_nvtee_handle_data (tee, buffer);

  GST_DEBUG_OBJECT (tee, "handled buffer %s", gst_flow_get_name (res));

  return res;
}

#ifndef PACKAGE
#define PACKAGE "nvtee"
#endif

static gboolean
nvtee_init (GstPlugin * plugin)
{

  if (!gst_element_register (plugin, "nvtee", GST_RANK_PRIMARY, GST_TYPE_NVTEE)) {
    return FALSE;
  }
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    nvtee,
    "Nvidia Video Capture Component ",
    nvtee_init, "1.2.0", "Proprietary", "Nvtee", "http://nvidia.com")
