/*
 * Copyright (C) 2014-2017 SUMOMO Computer Association
 *     Authors Ayaka <ayaka@soulik.info>
 * Copyright (C) 2017 Collabora Ltd.
 *     Author: Nicolas Dufresne <nicolas.dufresne@collabora.com>
 * Copyright (c) 2018-2023, NVIDIA CORPORATION. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#ifdef USE_V4L2_TARGET_NV
#include <stdlib.h>
#endif

#include "gstv4l2object.h"
#include "gstv4l2videoenc.h"
#include "gstnvdsseimeta.h"
#include "gst-nvcustomevent.h"

#include <string.h>
#include <gst/gst-i18n-plugin.h>

GST_DEBUG_CATEGORY_STATIC (gst_v4l2_video_enc_debug);
#define GST_CAT_DEFAULT gst_v4l2_video_enc_debug
static gboolean enable_latency_measurement = FALSE;

#ifdef USE_V4L2_TARGET_NV
#define OUTPUT_CAPS \
    "video/x-raw(memory:NVMM), " \
    "width = (gint) [ 1, MAX ], " \
    "height = (gint) [ 1, MAX ], " \
    "format = (string) { I420, NV12, P010_10LE, Y444, Y444_10LE, NV24}, " \
    "framerate = (fraction) [ 0, MAX ];"

static GstStaticCaps sink_template_caps =
    GST_STATIC_CAPS (OUTPUT_CAPS);
static GstStaticPadTemplate gst_v4l2enc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (OUTPUT_CAPS));

#endif

typedef struct
{
  gchar *device;
  GstCaps *sink_caps;
  GstCaps *src_caps;
} GstV4l2VideoEncCData;

#ifdef USE_V4L2_TARGET_NV
GstVideoCodecFrame *
gst_v4l2_video_enc_find_nearest_frame (GstV4l2VideoEnc *self,
    GstBuffer * buf, GList * frames);
gboolean set_v4l2_video_encoder_properties (GstVideoEncoder * encoder);
gboolean setQpRange (GstV4l2Object * v4l2object, guint label, guint MinQpI,
    guint MaxQpI, guint MinQpP, guint MaxQpP, guint MinQpB, guint MaxQpB);
gboolean setHWPresetType (GstV4l2Object * v4l2object, guint label,
    enum v4l2_enc_hw_preset_type type);
gint gst_v4l2_trace_file_open (FILE ** file);
void gst_v4l2_trace_file_close (FILE * file);
void gst_v4l2_trace_printf (FILE * file, const gchar *fmt, ...);

static gboolean
gst_v4l2_video_enc_parse_constqp (GstV4l2VideoEnc * self, const gchar * arr);
static gboolean
gst_v4l2_video_enc_parse_initqp (GstV4l2VideoEnc * self, const gchar * arr);
static gboolean
gst_v4l2_video_enc_parse_quantization_range (GstV4l2VideoEnc * self,
    const gchar * arr);
static GType gst_v4l2_videnc_hw_preset_level_get_type (void);
static GType gst_v4l2_videnc_tuning_info_get_type (void);
static void gst_v4l2_video_encoder_forceIDR (GstV4l2VideoEnc * self);

static GType gst_v4l2_videnc_ratecontrol_get_type (void);
enum
{
  /* actions */
  SIGNAL_FORCE_IDR,
  LAST_SIGNAL
};

static guint gst_v4l2_signals[LAST_SIGNAL] = { 0 };

#endif

enum
{
  PROP_0,
  V4L2_STD_OBJECT_PROPS,
#ifdef USE_V4L2_TARGET_NV
  /* Common properties */
  PROP_BITRATE,
  PROP_RATE_CONTROL,
  PROP_INTRA_FRAME_INTERVAL,
  /* Properties exposed on dGPU only */
  PROP_CUDAENC_GPU_ID,
  PROP_CUDAENC_PRESET_ID,
  PROP_CUDAENC_CONSTQP,
  PROP_CUDAENC_INITQP,
  PROP_CUDAENC_TUNING_INFO_ID,
  /* Properties exposed on Tegra only */
  PROP_PEAK_BITRATE,
  PROP_QUANT_I_FRAMES,
  PROP_QUANT_P_FRAMES,
  PROP_QUANT_B_FRAMES,
  PROP_HW_PRESET_LEVEL,
  PROP_QUANT_RANGE,
  PROP_VIRTUAL_BUFFER_SIZE,
  PROP_MEASURE_LATENCY,
  PROP_RC_ENABLE,
  PROP_MAX_PERF,
  PROP_IDR_FRAME_INTERVAL,
  PROP_FORCE_INTRA,
  PROP_COPY_METADATA,
  PROP_FORCE_IDR
#endif
};

#ifdef USE_V4L2_TARGET_NV
/* Defaults */
#define GST_V4L2_VIDEO_ENC_BITRATE_DEFAULT           (4000000)
#define GST_V4L2_VIDEO_ENC_PEAK_BITRATE_DEFAULT      (0)
#define DEFAULT_RATE_CONTROL                         V4L2_MPEG_VIDEO_BITRATE_MODE_CBR
#define DEFAULT_INTRA_FRAME_INTERVAL                 30
#define DEFAULT_CUDAENC_GPU_ID                       0
#define DEFAULT_CUDAENC_PRESET_ID                    1
#define DEFAULT_CUDAENC_TUNING_INFO_ID               3
#define DEFAULT_IDR_FRAME_INTERVAL                   256
#define GST_V4L2_VIDEO_ENC_QUANT_I_FRAMES_DEFAULT    (0xffffffff)
#define GST_V4L2_VIDEO_ENC_QUANT_P_FRAMES_DEFAULT    (0xffffffff)
#define GST_V4L2_VIDEO_ENC_QUANT_B_FRAMES_DEFAULT    (0xffffffff)
#define DEFAULT_HW_PRESET_LEVEL                      V4L2_ENC_HW_PRESET_ULTRAFAST
#define DEFAULT_TUNING_INFO_PRESET                   V4L2_ENC_TUNING_INFO_LOW_LATENCY

#define GST_TYPE_V4L2_VID_ENC_HW_PRESET_LEVEL        (gst_v4l2_videnc_hw_preset_level_get_type ())
#define GST_TYPE_V4L2_VID_ENC_TUNING_INFO_PRESET     (gst_v4l2_videnc_tuning_info_get_type ())
#define GST_TYPE_V4L2_VID_ENC_RATECONTROL            (gst_v4l2_videnc_ratecontrol_get_type())
#define DEFAULT_VBV_SIZE                             4000000
#endif

#define gst_v4l2_video_enc_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstV4l2VideoEnc, gst_v4l2_video_enc,
    GST_TYPE_VIDEO_ENCODER);

static gdouble get_current_system_timestamp(void)
{
  struct timeval t1;
  double elapsedTime = 0;
  gettimeofday(&t1, NULL);
  elapsedTime = (t1.tv_sec) * 1000.0;
  elapsedTime += (t1.tv_usec) / 1000.0;
  return elapsedTime;
}

#ifdef USE_V4L2_TARGET_NV
GType
gst_v4l2_enc_output_io_mode_get_type (void)
{
  static GType v4l2_enc_output_io_mode = 0;

  if (!v4l2_enc_output_io_mode) {
    static const GEnumValue enc_output_io_modes[] = {
      {GST_V4L2_IO_AUTO, "GST_V4L2_IO_AUTO", "auto"},
      {GST_V4L2_IO_MMAP, "GST_V4L2_IO_MMAP", "mmap"},
      {GST_V4L2_IO_DMABUF_IMPORT, "GST_V4L2_IO_DMABUF_IMPORT", "dmabuf-import"},
      {0, NULL, NULL}
    };

    v4l2_enc_output_io_mode = g_enum_register_static ("GstNvV4l2EncOutputIOMode",
        enc_output_io_modes);
  }
  return v4l2_enc_output_io_mode;
}

GType
gst_v4l2_enc_capture_io_mode_get_type (void)
{
  static GType v4l2_enc_capture_io_mode = 0;

  if (!v4l2_enc_capture_io_mode) {
    static const GEnumValue enc_capture_io_modes[] = {
      {GST_V4L2_IO_AUTO, "GST_V4L2_IO_AUTO", "auto"},
      {GST_V4L2_IO_MMAP, "GST_V4L2_IO_MMAP", "mmap"},
      {0, NULL, NULL}
    };

    v4l2_enc_capture_io_mode = g_enum_register_static ("GstNvV4l2EncCaptureIOMode",
        enc_capture_io_modes);
  }
  return v4l2_enc_capture_io_mode;
}
#endif

static void
gst_v4l2_video_enc_set_property_tegra (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (object);

  switch (prop_id) {
    case PROP_CAPTURE_IO_MODE:
      if (!gst_v4l2_object_set_property_helper (self->v4l2capture,
              prop_id, value, pspec)) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      }
      break;

#ifdef USE_V4L2_TARGET_NV
    case PROP_RATE_CONTROL:
      self->ratecontrol = g_value_get_enum (value);
      break;

    case PROP_BITRATE:
      self->bitrate = g_value_get_uint (value);
      if (GST_V4L2_IS_OPEN (self->v4l2output)) {
        if (!set_v4l2_video_mpeg_class (self->v4l2output,
            V4L2_CID_MPEG_VIDEO_BITRATE, self->bitrate)) {
          g_print ("S_EXT_CTRLS for BITRATE failed\n");
        }
      }
      break;

    case PROP_INTRA_FRAME_INTERVAL:
      self->iframeinterval = g_value_get_uint (value);
      break;

    case PROP_PEAK_BITRATE:
      self->peak_bitrate = g_value_get_uint (value);
      break;

    case PROP_QUANT_RANGE:
      gst_v4l2_video_enc_parse_quantization_range (self,
          g_value_get_string (value));
      self->set_qpRange = TRUE;
      break;

    case PROP_QUANT_I_FRAMES:
      self->quant_i_frames = g_value_get_uint (value);
      break;

    case PROP_QUANT_P_FRAMES:
      self->quant_p_frames = g_value_get_uint (value);
      break;

    case PROP_QUANT_B_FRAMES:
      self->quant_b_frames = g_value_get_uint (value);
      break;

    case PROP_HW_PRESET_LEVEL:
      self->hw_preset_level = g_value_get_enum (value);
      break;

    case PROP_VIRTUAL_BUFFER_SIZE:
      self->virtual_buffer_size = g_value_get_uint (value);
      break;

    case PROP_MEASURE_LATENCY:
      self->measure_latency = g_value_get_boolean (value);
      break;

    case PROP_RC_ENABLE:
      self->ratecontrol_enable = g_value_get_boolean (value);
      break;

    case PROP_MAX_PERF:
      self->maxperf_enable = g_value_get_boolean (value);
      break;

    case PROP_IDR_FRAME_INTERVAL:
      self->idrinterval = g_value_get_uint (value);
      break;
#endif

      /* By default, only set on output */
    default:
      if (!gst_v4l2_object_set_property_helper (self->v4l2output,
              prop_id, value, pspec)) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      }
      break;
  }
}

static void
gst_v4l2_video_enc_set_property_cuvid (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (object);

  switch (prop_id) {
    case PROP_CAPTURE_IO_MODE:
      if (!gst_v4l2_object_set_property_helper (self->v4l2capture,
              prop_id, value, pspec)) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      }
      break;

#ifdef USE_V4L2_TARGET_NV
    case PROP_RATE_CONTROL:
      self->ratecontrol = g_value_get_enum (value);
      break;

    case PROP_BITRATE:
      self->bitrate = g_value_get_uint (value);
      if (GST_V4L2_IS_OPEN (self->v4l2output)) {
        if (!set_v4l2_video_mpeg_class (self->v4l2output,
            V4L2_CID_MPEG_VIDEO_BITRATE, self->bitrate)) {
          g_print ("S_EXT_CTRLS for BITRATE failed\n");
        }
      }
      break;

    case PROP_INTRA_FRAME_INTERVAL:
      self->iframeinterval = g_value_get_uint (value);
      break;

    case PROP_QUANT_RANGE:
      gst_v4l2_video_enc_parse_quantization_range (self,
          g_value_get_string (value));
      self->set_qpRange = TRUE;
      break;

    case PROP_CUDAENC_GPU_ID:
      self->cudaenc_gpu_id = g_value_get_uint (value);
      break;

    case PROP_CUDAENC_PRESET_ID:
      self->cudaenc_preset_id = g_value_get_uint (value);
      break;

    case PROP_CUDAENC_CONSTQP:
      gst_v4l2_video_enc_parse_constqp (self,
          g_value_get_string (value));
      break;

    case PROP_CUDAENC_INITQP:
      gst_v4l2_video_enc_parse_initqp (self,
          g_value_get_string (value));
      break;

    case PROP_CUDAENC_TUNING_INFO_ID:
      self->cudaenc_tuning_info_id = g_value_get_enum (value);
      break;

    case PROP_IDR_FRAME_INTERVAL:
      self->idrinterval = g_value_get_uint (value);
      break;

    case PROP_FORCE_IDR:
      self->force_idr = g_value_get_boolean (value);
      break;

    case PROP_FORCE_INTRA:
      self->force_intra = g_value_get_boolean (value);
      break;

    case PROP_COPY_METADATA:
      self->copy_meta = g_value_get_boolean (value);
      break;
#endif

      /* By default, only set on output */
    default:
      if (!gst_v4l2_object_set_property_helper (self->v4l2output,
              prop_id, value, pspec)) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      }
      break;
  }
}

static void
gst_v4l2_video_enc_get_property_tegra (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (object);

  switch (prop_id) {
    case PROP_CAPTURE_IO_MODE:
      if (!gst_v4l2_object_get_property_helper (self->v4l2capture,
              prop_id, value, pspec)) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      }
      break;

#ifdef USE_V4L2_TARGET_NV
    case PROP_RATE_CONTROL:
      g_value_set_enum (value, self->ratecontrol);
      break;

    case PROP_BITRATE:
      g_value_set_uint (value, self->bitrate);
      break;

    case PROP_INTRA_FRAME_INTERVAL:
      g_value_set_uint (value, self->iframeinterval);
      break;

    case PROP_PEAK_BITRATE:
      g_value_set_uint (value, self->peak_bitrate);
      break;

    case PROP_QUANT_RANGE:
      //    gst_v4l2_video_enc_get_quantization_range (self, value);
      break;

    case PROP_QUANT_I_FRAMES:
      g_value_set_uint (value, self->quant_i_frames);
      break;

    case PROP_QUANT_P_FRAMES:
      g_value_set_uint (value, self->quant_p_frames);
      break;

    case PROP_QUANT_B_FRAMES:
      g_value_set_uint (value, self->quant_b_frames);
      break;

    case PROP_HW_PRESET_LEVEL:
      g_value_set_enum (value, self->hw_preset_level);
      break;

    case PROP_VIRTUAL_BUFFER_SIZE:
      g_value_set_uint (value, self->virtual_buffer_size);
      break;

    case PROP_MEASURE_LATENCY:
      g_value_set_boolean (value, self->measure_latency);
      break;

    case PROP_RC_ENABLE:
      g_value_set_boolean (value, self->ratecontrol_enable);
      break;

    case PROP_MAX_PERF:
      g_value_set_boolean (value, self->maxperf_enable);
      break;

    case PROP_IDR_FRAME_INTERVAL:
      g_value_set_uint (value, self->idrinterval);
      break;
#endif

      /* By default read from output */
    default:
      if (!gst_v4l2_object_get_property_helper (self->v4l2output,
              prop_id, value, pspec)) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      }
      break;
  }
}

static void
gst_v4l2_video_enc_get_property_cuvid (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (object);

  switch (prop_id) {
    case PROP_CAPTURE_IO_MODE:
      if (!gst_v4l2_object_get_property_helper (self->v4l2capture,
              prop_id, value, pspec)) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      }
      break;

#ifdef USE_V4L2_TARGET_NV
    case PROP_RATE_CONTROL:
      g_value_set_enum (value, self->ratecontrol);
      break;

    case PROP_BITRATE:
      g_value_set_uint (value, self->bitrate);
      break;

    case PROP_INTRA_FRAME_INTERVAL:
      g_value_set_uint (value, self->iframeinterval);
      break;

    case PROP_CUDAENC_GPU_ID:
      g_value_set_uint(value, self->cudaenc_gpu_id);
      break;

    case PROP_CUDAENC_PRESET_ID:
      g_value_set_uint(value, self->cudaenc_preset_id);
      break;

    case PROP_QUANT_RANGE:
      //    gst_v4l2_video_enc_get_quantization_range (self, value);
      break;

    case PROP_CUDAENC_CONSTQP:
      break;

    case PROP_CUDAENC_INITQP:
      break;

    case PROP_CUDAENC_TUNING_INFO_ID:
      g_value_set_enum(value, self->cudaenc_tuning_info_id);
      break;

    case PROP_IDR_FRAME_INTERVAL:
      g_value_set_uint (value, self->idrinterval);
      break;

    case PROP_FORCE_IDR:
      g_value_set_boolean (value, self->force_idr);
      break;

    case PROP_FORCE_INTRA:
      g_value_set_boolean (value, self->force_intra);
      break;

    case PROP_COPY_METADATA:
      g_value_set_boolean (value, self->copy_meta);
      break;
#endif

      /* By default read from output */
    default:
      if (!gst_v4l2_object_get_property_helper (self->v4l2output,
              prop_id, value, pspec)) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      }
      break;
  }
}

static gboolean
gst_v4l2_video_enc_open (GstVideoEncoder * encoder)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);
  GstCaps *codec_caps;
#ifdef USE_V4L2_TARGET_NV
  const gchar *mimetype;
  GstStructure *s;
  GstV4l2VideoEncClass *klass = NULL;
  if (is_cuvid == TRUE)
    klass = GST_V4L2_VIDEO_ENC_GET_CLASS (encoder);

#endif

  GST_DEBUG_OBJECT (self, "Opening");

  if (!gst_v4l2_object_open (self->v4l2output))
    goto failure;

  if (!gst_v4l2_object_open_shared (self->v4l2capture, self->v4l2output))
    goto failure;

  self->probed_sinkcaps = gst_v4l2_object_probe_caps (self->v4l2output,
      gst_v4l2_object_get_raw_caps ());

  if (gst_caps_is_empty (self->probed_sinkcaps))
    goto no_raw_format;

  codec_caps = gst_pad_get_pad_template_caps (encoder->srcpad);
  self->probed_srccaps = gst_v4l2_object_probe_caps (self->v4l2capture,
      codec_caps);
  gst_caps_unref (codec_caps);

  if (gst_caps_is_empty (self->probed_srccaps))
    goto no_encoded_format;

#ifdef USE_V4L2_TARGET_NV
  s = gst_caps_get_structure (self->probed_srccaps, 0);
  mimetype = gst_structure_get_name (s);
  if (g_str_equal (mimetype, "video/x-h264") && self->slice_output) {
    gst_structure_remove_field (s, "alignment");
    gst_structure_set (s, "alignment", G_TYPE_STRING, "nal", NULL);
  }

  if (self->measure_latency) {
    if (gst_v4l2_trace_file_open (&self->tracing_file_enc) == 0) {
      g_print ("%s: open trace file successfully\n", __func__);
      self->got_frame_pt = g_queue_new ();
    } else
      g_print ("%s: failed to open trace file\n", __func__);
  }

  if (is_cuvid == TRUE) {
    if (strcmp (klass->codec_name, "H264") == 0
        || strcmp (klass->codec_name, "H265") == 0){
      if (!klass->set_encoder_properties (encoder)) {
        return FALSE;
      }
    }

    if (!set_v4l2_video_encoder_properties (encoder)) {
      return FALSE;
    }
  }
#endif
  return TRUE;

no_encoded_format:
  GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS,
      (_("Encoder on device %s has no supported output format"),
          self->v4l2output->videodev), (NULL));
  goto failure;


no_raw_format:
  GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS,
      (_("Encoder on device %s has no supported input format"),
          self->v4l2output->videodev), (NULL));
  goto failure;

failure:
  if (GST_V4L2_IS_OPEN (self->v4l2output))
    gst_v4l2_object_close (self->v4l2output);

  if (GST_V4L2_IS_OPEN (self->v4l2capture))
    gst_v4l2_object_close (self->v4l2capture);

  gst_caps_replace (&self->probed_srccaps, NULL);
  gst_caps_replace (&self->probed_sinkcaps, NULL);

  return FALSE;
}

static gboolean
gst_v4l2_video_enc_close (GstVideoEncoder * encoder)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Closing");

  gst_v4l2_object_close (self->v4l2output);
  gst_v4l2_object_close (self->v4l2capture);
  gst_caps_replace (&self->probed_srccaps, NULL);
  gst_caps_replace (&self->probed_sinkcaps, NULL);

#ifdef USE_V4L2_TARGET_NV
  if (self->tracing_file_enc) {
    gst_v4l2_trace_file_close (self->tracing_file_enc);
    g_queue_free (self->got_frame_pt);
  }
#endif

  return TRUE;
}

static gboolean
gst_v4l2_video_enc_start (GstVideoEncoder * encoder)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Starting");

  gst_v4l2_object_unlock (self->v4l2output);
  g_atomic_int_set (&self->active, TRUE);
  self->output_flow = GST_FLOW_OK;

  self->hash_pts_systemtime = g_hash_table_new(NULL, NULL);

  return TRUE;
}

static gboolean
gst_v4l2_video_enc_stop (GstVideoEncoder * encoder)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Stopping");

  gst_v4l2_object_unlock (self->v4l2output);
  gst_v4l2_object_unlock (self->v4l2capture);

  /* Wait for capture thread to stop */
  gst_pad_stop_task (encoder->srcpad);

  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);
  self->output_flow = GST_FLOW_OK;
  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

  /* Should have been flushed already */
  g_assert (g_atomic_int_get (&self->active) == FALSE);
  g_assert (g_atomic_int_get (&self->processing) == FALSE);

  gst_v4l2_object_stop (self->v4l2output);
  gst_v4l2_object_stop (self->v4l2capture);

  g_hash_table_destroy (self->hash_pts_systemtime);

  if (self->input_state) {
    gst_video_codec_state_unref (self->input_state);
    self->input_state = NULL;
  }

  GST_DEBUG_OBJECT (self, "Stopped");

  return TRUE;
}

static gboolean
gst_v4l2_encoder_cmd (GstV4l2Object * v4l2object, guint cmd, guint flags)
{
  struct v4l2_encoder_cmd ecmd = { 0, };

  GST_DEBUG_OBJECT (v4l2object->element,
      "sending v4l2 encoder command %u with flags %u", cmd, flags);

  if (!GST_V4L2_IS_OPEN (v4l2object))
    return FALSE;

  ecmd.cmd = cmd;
  ecmd.flags = flags;
  if (v4l2object->ioctl (v4l2object->video_fd, VIDIOC_ENCODER_CMD, &ecmd) < 0)
    goto ecmd_failed;

  return TRUE;

ecmd_failed:
  if (errno == ENOTTY) {
    GST_INFO_OBJECT (v4l2object->element,
        "Failed to send encoder command %u with flags %u for '%s'. (%s)",
        cmd, flags, v4l2object->videodev, g_strerror (errno));
  } else {
    GST_ERROR_OBJECT (v4l2object->element,
        "Failed to send encoder command %u with flags %u for '%s'. (%s)",
        cmd, flags, v4l2object->videodev, g_strerror (errno));
  }
  return FALSE;
}

static GstFlowReturn
gst_v4l2_video_enc_finish (GstVideoEncoder * encoder)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);
  GstFlowReturn ret = GST_FLOW_OK;

  if (gst_pad_get_task_state (encoder->srcpad) != GST_TASK_STARTED)
    goto done;

  GST_DEBUG_OBJECT (self, "Finishing encoding");

  /* drop the stream lock while draining, so remaining buffers can be
   * pushed from the src pad task thread */
  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

#ifndef USE_V4L2_TARGET_NV
  if (gst_v4l2_encoder_cmd (self->v4l2capture, V4L2_ENC_CMD_STOP, 0)) {
#else
  if (gst_v4l2_encoder_cmd (self->v4l2capture, V4L2_ENC_CMD_STOP,
          V4L2_DEC_CMD_STOP_TO_BLACK)) {
#endif
    GstTask *task = encoder->srcpad->task;

    /* Wait for the task to be drained */
    GST_OBJECT_LOCK (task);
    while (GST_TASK_STATE (task) == GST_TASK_STARTED)
      GST_TASK_WAIT (task);
    GST_OBJECT_UNLOCK (task);
    ret = GST_FLOW_FLUSHING;
  }

  /* and ensure the processing thread has stopped in case another error
   * occured. */
  gst_v4l2_object_unlock (self->v4l2capture);
#ifdef USE_V4L2_TARGET_NV
  set_v4l2_video_mpeg_class (self->v4l2capture,
              V4L2_CID_MPEG_SET_POLL_INTERRUPT, 0);
#endif
  gst_pad_stop_task (encoder->srcpad);
  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);

  if (ret == GST_FLOW_FLUSHING)
    ret = self->output_flow;

  GST_DEBUG_OBJECT (encoder, "Done draining buffers");

done:
  return ret;
}

#ifdef USE_V4L2_TARGET_NV
gboolean is_drc (GstVideoEncoder *encoder, GstCaps *input_caps)
{
  int curr_width, curr_height, new_width, new_height;
  GstStructure *sink_caps_st, *input_caps_st;
  GstCaps *sink_caps = gst_caps_make_writable(gst_pad_get_current_caps(encoder->sinkpad));
  sink_caps_st = gst_caps_get_structure(sink_caps, 0);
  input_caps_st = gst_caps_get_structure(input_caps, 0);

  gst_structure_get_int(sink_caps_st, "width", &curr_width);
  gst_structure_get_int(sink_caps_st, "height", &curr_height);
  gst_structure_get_int(input_caps_st, "width", &new_width);
  gst_structure_get_int(input_caps_st, "height", &new_height);

  GST_INFO_OBJECT(encoder, "curr resolution: [%dx%d], new resolution: [%dx%d]", curr_width, curr_height, new_width, new_height);
  if ((curr_width != new_width) || (curr_height != new_height))
    return TRUE;

  gst_caps_unref(sink_caps);
  return FALSE;
}

void set_encoder_src_caps (GstVideoEncoder *encoder, GstCaps *input_caps)
{
  GstStructure *src_caps_st, *input_caps_st;
  const GValue *framerate = NULL;
  GstCaps *src_caps = gst_caps_make_writable(gst_pad_get_current_caps(encoder->srcpad));
  src_caps_st = gst_caps_get_structure(src_caps, 0);
  input_caps_st = gst_caps_get_structure(input_caps, 0);
  framerate = gst_structure_get_value(input_caps_st, "framerate");
  if (framerate)
    gst_structure_set_value(src_caps_st, "framerate", framerate);

  GST_DEBUG_OBJECT(encoder, "enc_src_caps: %s", gst_caps_to_string(src_caps));
  gst_pad_set_caps(encoder->srcpad, src_caps);
  gst_caps_unref(src_caps);
}

gboolean
reconfigure_fps (GstVideoEncoder *encoder, GstCaps *input_caps, guint label)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);
  GstV4l2Object *v4l2object = self->v4l2output;
  struct v4l2_ext_control control;
  struct v4l2_ext_controls ctrls;
  v4l2_ctrl_video_framerate enc_config;
  gint curr_fps_n = 0, curr_fps_d = 0;
  gint new_fps_n = 0, new_fps_d = 0;
  gint ret = 0;

  /*Check if current fps is same as in newly received caps */
  GstStructure *sink_pad_st, *input_caps_st;
  GstCaps *sink_caps = gst_pad_get_current_caps(encoder->sinkpad);
  sink_pad_st = gst_caps_get_structure(sink_caps, 0);
  input_caps_st = gst_caps_get_structure(input_caps, 0);
  gst_structure_get_fraction (sink_pad_st, "framerate", &curr_fps_n, &curr_fps_d);
  gst_structure_get_fraction (input_caps_st, "framerate", &new_fps_n, &new_fps_d);
  GST_INFO_OBJECT(encoder, "old framerate:[%d/%d], new framerate:[%d/%d]", curr_fps_n, curr_fps_d, new_fps_n, new_fps_d);
  if ((curr_fps_n != new_fps_n) || (curr_fps_d != new_fps_d)) {
    enc_config.fps_n = new_fps_n;
    enc_config.fps_d = new_fps_d;
  } else {
    GST_DEBUG_OBJECT(encoder, "No change in framerate");
    return TRUE;
  }
  memset (&control, 0, sizeof (control));
  memset (&ctrls, 0, sizeof (ctrls));

  ctrls.count = 1;
  ctrls.controls = &control;
  ctrls.ctrl_class = V4L2_CTRL_CLASS_MPEG;

  control.id = label;
  control.string = (gchar *) &enc_config;

  ret = v4l2object->ioctl (v4l2object->video_fd, VIDIOC_S_EXT_CTRLS, &ctrls);
  if (ret < 0) {
    GST_WARNING_OBJECT (encoder, "Error in reconfiguring fps\n");
    return FALSE;
  }

  return TRUE;
}
#endif

static gboolean
gst_v4l2_video_enc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  gboolean ret = TRUE;
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);
  GstV4l2Error error = GST_V4L2_ERROR_INIT;
  GstCaps *outcaps;
  GstVideoCodecState *output;
#ifdef USE_V4L2_TARGET_NV
  const gchar *mimetype;
  GstStructure *s;
#endif

  GST_DEBUG_OBJECT (self, "Setting format: %" GST_PTR_FORMAT, state->caps);

  if (self->input_state) {
    if (is_cuvid == FALSE) {
      if (gst_v4l2_object_caps_equal(self->v4l2output, state->caps)) {
        GST_DEBUG_OBJECT(self, "Compatible caps");
        return TRUE;
      }
    }
#ifdef USE_V4L2_TARGET_NV
    if (is_cuvid == TRUE) {
      if (is_drc (encoder, state->caps)) {
        /*TODO: Reset encoder to allocate new buffer size at encoder output plane*/
      } else {
        GST_DEBUG_OBJECT (self, "Not DRC. Reconfigure encoder with new fps if required");
        if (!reconfigure_fps(encoder, state->caps, V4L2_CID_MPEG_VIDEOENC_RECONFIG_FPS))
          GST_WARNING_OBJECT(self, "S_EXT_CTRLS for RECONFIG_FPS failed\n");
        /* set encoder src caps */
        set_encoder_src_caps(encoder, state->caps);
        return TRUE;
      }
    }
#endif

    if (gst_v4l2_video_enc_finish (encoder) != GST_FLOW_OK)
      return FALSE;

    gst_v4l2_object_stop (self->v4l2output);
    gst_v4l2_object_stop (self->v4l2capture);

    gst_video_codec_state_unref (self->input_state);
    self->input_state = NULL;
  }

  outcaps = gst_pad_get_pad_template_caps (encoder->srcpad);
  outcaps = gst_caps_make_writable (outcaps);

#ifdef USE_V4L2_TARGET_NV
  s = gst_caps_get_structure (outcaps, 0);
  mimetype = gst_structure_get_name (s);
  if (g_str_equal (mimetype, "video/x-h264")) {
    gst_structure_remove_field (s, "alignment");
    if (self->slice_output) {
      gst_structure_set (s, "alignment", G_TYPE_STRING, "nal", NULL);
    } else {
      gst_structure_set (s, "alignment", G_TYPE_STRING, "au", NULL);
    }
  }
#endif

  output = gst_video_encoder_set_output_state (encoder, outcaps, state);
  gst_video_codec_state_unref (output);

#ifdef USE_V4L2_TARGET_NV
  outcaps = gst_caps_fixate (outcaps);
#endif
  if (!gst_video_encoder_negotiate (encoder))
    return FALSE;

  if (!gst_v4l2_object_set_format (self->v4l2output, state->caps, &error)) {
    gst_v4l2_error (self, &error);
    return FALSE;
  }

  /* activating a capture pool will also call STREAMON. CODA driver will
   * refuse to configure the output if the capture is stremaing. */
  if (!gst_buffer_pool_set_active (GST_BUFFER_POOL (self->v4l2capture->pool),
          TRUE)) {
    GST_WARNING_OBJECT (self, "Could not activate capture buffer pool.");
    return FALSE;
  }

  self->input_state = gst_video_codec_state_ref (state);

  GST_DEBUG_OBJECT (self, "output caps: %" GST_PTR_FORMAT, state->caps);

  return ret;
}

static gboolean
gst_v4l2_video_enc_flush (GstVideoEncoder * encoder)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Flushing");

  /* Ensure the processing thread has stopped for the reverse playback
   * iscount case */
  if (g_atomic_int_get (&self->processing)) {
    GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

    gst_v4l2_object_unlock_stop (self->v4l2output);
    gst_v4l2_object_unlock_stop (self->v4l2capture);
#ifdef USE_V4L2_TARGET_NV
    set_v4l2_video_mpeg_class (self->v4l2capture,
              V4L2_CID_MPEG_SET_POLL_INTERRUPT, 0);
#endif
    gst_pad_stop_task (encoder->srcpad);

    GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

  }

  self->output_flow = GST_FLOW_OK;

  gst_v4l2_object_unlock_stop (self->v4l2output);
  gst_v4l2_object_unlock_stop (self->v4l2capture);

  return TRUE;
}

struct ProfileLevelCtx
{
  GstV4l2VideoEnc *self;
  const gchar *profile;
  const gchar *level;
};

static gboolean
get_string_list (GstStructure * s, const gchar * field, GQueue * queue)
{
  const GValue *value;

  value = gst_structure_get_value (s, field);

  if (!value)
    return FALSE;

  if (GST_VALUE_HOLDS_LIST (value)) {
    guint i;

    if (gst_value_list_get_size (value) == 0)
      return FALSE;

    for (i = 0; i < gst_value_list_get_size (value); i++) {
      const GValue *item = gst_value_list_get_value (value, i);

      if (G_VALUE_HOLDS_STRING (item))
        g_queue_push_tail (queue, g_value_dup_string (item));
    }
  } else if (G_VALUE_HOLDS_STRING (value)) {
    g_queue_push_tail (queue, g_value_dup_string (value));
  }

  return TRUE;
}

static gboolean
negotiate_profile_and_level (GstCapsFeatures * features, GstStructure * s,
    gpointer user_data)
{
  struct ProfileLevelCtx *ctx = user_data;
  GstV4l2VideoEncClass *klass = GST_V4L2_VIDEO_ENC_GET_CLASS (ctx->self);
  GstV4l2Object *v4l2object = GST_V4L2_VIDEO_ENC (ctx->self)->v4l2output;
  GQueue profiles = G_QUEUE_INIT;
  GQueue levels = G_QUEUE_INIT;
  gboolean failed = FALSE;

  if (klass->profile_cid && get_string_list (s, "profile", &profiles)) {
    GList *l;

    for (l = profiles.head; l; l = l->next) {
      struct v4l2_control control = { 0, };
      gint v4l2_profile;
      const gchar *profile = l->data;

      GST_TRACE_OBJECT (ctx->self, "Trying profile %s", profile);

      control.id = klass->profile_cid;
      control.value = v4l2_profile = klass->profile_from_string (profile);

      if (control.value < 0)
        continue;

      if (v4l2object->ioctl (v4l2object->video_fd, VIDIOC_S_CTRL, &control) < 0) {
        GST_WARNING_OBJECT (ctx->self, "Failed to set %s profile: '%s'",
            klass->codec_name, g_strerror (errno));
        break;
      }

      profile = klass->profile_to_string (control.value);

      if (control.value == v4l2_profile) {
        ctx->profile = profile;
        break;
      }

      if (g_list_find_custom (l, profile, g_str_equal)) {
        ctx->profile = profile;
        break;
      }
    }

    if (profiles.length && !ctx->profile)
      failed = TRUE;

    g_queue_foreach (&profiles, (GFunc) g_free, NULL);
    g_queue_clear (&profiles);
  }

  if (!failed && klass->level_cid && get_string_list (s, "level", &levels)) {
    GList *l;

    for (l = levels.head; l; l = l->next) {
      struct v4l2_control control = { 0, };
      gint v4l2_level;
      const gchar *level = l->data;

      GST_TRACE_OBJECT (ctx->self, "Trying level %s", level);

      control.id = klass->level_cid;
      control.value = v4l2_level = klass->level_from_string (level);

      if (control.value < 0)
        continue;

      if (v4l2object->ioctl (v4l2object->video_fd, VIDIOC_S_CTRL, &control) < 0) {
        GST_WARNING_OBJECT (ctx->self, "Failed to set %s level: '%s'",
            klass->codec_name, g_strerror (errno));
        break;
      }

      level = klass->level_to_string (control.value);

      if (control.value == v4l2_level) {
        ctx->level = level;
        break;
      }

      if (g_list_find_custom (l, level, g_str_equal)) {
        ctx->level = level;
        break;
      }
    }

    if (levels.length && !ctx->level)
      failed = TRUE;

    g_queue_foreach (&levels, (GFunc) g_free, NULL);
    g_queue_clear (&levels);
  }

  /* If it failed, we continue */
  return failed;
}

static gboolean
gst_v4l2_video_enc_negotiate (GstVideoEncoder * encoder)
{
  GstV4l2VideoEncClass *klass = GST_V4L2_VIDEO_ENC_GET_CLASS (encoder);
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);
#ifndef USE_V4L2_TARGET_NV
  GstV4l2Object *v4l2object = self->v4l2output;
#endif
  GstCaps *allowed_caps;
  struct ProfileLevelCtx ctx = { self, NULL, NULL };
  GstVideoCodecState *state;
  GstStructure *s;

  GST_DEBUG_OBJECT (self, "Negotiating %s profile and level.",
      klass->codec_name);

  /* Only renegotiate on upstream changes */
  if (self->input_state)
    return TRUE;

  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));

  if (allowed_caps) {

    if (gst_caps_is_empty (allowed_caps))
      goto not_negotiated;

    allowed_caps = gst_caps_make_writable (allowed_caps);

    /* negotiate_profile_and_level() will return TRUE on failure to keep
     * iterating, if gst_caps_foreach() returns TRUE it means there was no
     * compatible profile and level in any of the structure */
    if (gst_caps_foreach (allowed_caps, negotiate_profile_and_level, &ctx)) {
      goto no_profile_level;
    }
  }

#ifndef USE_V4L2_TARGET_NV
  if (klass->profile_cid && !ctx.profile) {
    struct v4l2_control control = { 0, };

    control.id = klass->profile_cid;

    if (v4l2object->ioctl (v4l2object->video_fd, VIDIOC_G_CTRL, &control) < 0)
      goto g_ctrl_failed;

    ctx.profile = klass->profile_to_string (control.value);
  }

  if (klass->level_cid && !ctx.level) {
    struct v4l2_control control = { 0, };

    control.id = klass->level_cid;

    if (v4l2object->ioctl (v4l2object->video_fd, VIDIOC_G_CTRL, &control) < 0)
      goto g_ctrl_failed;

    ctx.level = klass->level_to_string (control.value);
  }
#endif

  if (allowed_caps)
    gst_caps_unref (allowed_caps);

  GST_DEBUG_OBJECT (self, "Selected %s profile %s at level %s",
      klass->codec_name, ctx.profile, ctx.level);

  state = gst_video_encoder_get_output_state (encoder);
  s = gst_caps_get_structure (state->caps, 0);

  if (klass->profile_cid)
    gst_structure_set (s, "profile", G_TYPE_STRING, ctx.profile, NULL);

  if (klass->level_cid)
    gst_structure_set (s, "level", G_TYPE_STRING, ctx.level, NULL);

  gst_video_codec_state_unref (state);

  if (!GST_VIDEO_ENCODER_CLASS (parent_class)->negotiate (encoder))
    return FALSE;

  return TRUE;
#ifndef USE_V4L2_TARGET_NV
g_ctrl_failed:
  GST_WARNING_OBJECT (self, "Failed to get %s profile and level: '%s'",
      klass->codec_name, g_strerror (errno));
  goto not_negotiated;
#endif

no_profile_level:
  GST_WARNING_OBJECT (self, "No compatible level and profile in caps: %"
      GST_PTR_FORMAT, allowed_caps);
  goto not_negotiated;

not_negotiated:
  if (allowed_caps)
    gst_caps_unref (allowed_caps);
  return FALSE;
}

#ifdef USE_V4L2_TARGET_NV
GstVideoCodecFrame *
gst_v4l2_video_enc_find_nearest_frame (GstV4l2VideoEnc *self,
    GstBuffer * buf, GList * frames)
{
  GstVideoCodecFrame *best = NULL;
  GstClockTimeDiff best_diff = G_MAXINT64;
  GstClockTime timestamp;
  GList *l;

  timestamp = buf->pts;

  for (l = frames; l; l = l->next) {
    GstVideoCodecFrame *tmp = l->data;
    GstClockTimeDiff diff = ABS (GST_CLOCK_DIFF (timestamp, tmp->pts));

    if (diff < best_diff) {
      best = tmp;
      best_diff = diff;

      if (diff == 0)
        break;
    }
  }

  /* For slice output mode, video encoder will ouput multi buffer with
   * one input buffer. Which will cause frames list haven't any entry.
   * So the best will be NULL. Video bit stream will be discard in
   * function gst_v4l2_video_enc_loop() when the best is NULL.
   * Here reuse previous frame when the best is NULL to handle discard
   * bit stream issue when slice output mode enabled. */
  /* Video encoder will output the same PTS for slices. Reuse previous
   * frame for the same PTS slices */
  if (self->slice_output && self->best_prev
          && GST_CLOCK_TIME_IS_VALID (buf->pts)
          && GST_CLOCK_TIME_IS_VALID (self->buf_pts_prev)
          && buf->pts == self->buf_pts_prev)
      best = NULL;
  self->buf_pts_prev = buf->pts;

  if (best) {
    gst_video_codec_frame_ref (best);
    if (self->slice_output) {
      if (self->best_prev)
        gst_video_codec_frame_unref (self->best_prev);
      self->best_prev = gst_video_codec_frame_ref (best);
    }
  } else if (self->slice_output) {
    best = gst_video_codec_frame_ref (self->best_prev);
    /* Presentation_frame_number == 0 means discontinues. Need avoid it */
    if (best->presentation_frame_number == 0)
      best->presentation_frame_number = 1;
  }

  g_list_foreach (frames, (GFunc) gst_video_codec_frame_unref, NULL);
  g_list_free (frames);

  return best;
}
#else
static GstVideoCodecFrame *
gst_v4l2_video_enc_get_oldest_frame (GstVideoEncoder * encoder)
{
  GstVideoCodecFrame *frame = NULL;
  GList *frames, *l;
  gint count = 0;

  frames = gst_video_encoder_get_frames (encoder);

  for (l = frames; l != NULL; l = l->next) {
    GstVideoCodecFrame *f = l->data;

    if (!frame || frame->pts > f->pts)
      frame = f;

    count++;
  }

  if (frame) {
    GST_LOG_OBJECT (encoder,
        "Oldest frame is %d %" GST_TIME_FORMAT
        " and %d frames left",
        frame->system_frame_number, GST_TIME_ARGS (frame->pts), count - 1);
    gst_video_codec_frame_ref (frame);
  }

  g_list_free_full (frames, (GDestroyNotify) gst_video_codec_frame_unref);

  return frame;
}
#endif

static void
gst_v4l2_video_enc_loop (GstVideoEncoder * encoder)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);
  GstVideoCodecFrame *frame;
  GstBuffer *buffer = NULL;
  GstFlowReturn ret;
#ifdef USE_V4L2_TARGET_NV
  struct timeval ts;
  guint64 done_time;
  guint64 *in_time_pt;
#endif

  GST_LOG_OBJECT (encoder, "Allocate output buffer");

  buffer = gst_video_encoder_allocate_output_buffer (encoder,
      self->v4l2capture->info.size);

  if (NULL == buffer) {
    ret = GST_FLOW_FLUSHING;
    goto beach;
  }

#ifdef USE_V4L2_TARGET_NV
  if (is_cuvid == TRUE && self->force_idr) {
      if (!set_v4l2_video_mpeg_class (self->v4l2output,
                  V4L2_CID_MPEG_VIDEOENC_FORCE_IDR_FRAME, 1)) {
          g_print ("S_EXT_CTRLS for FORCE_IDR_FRAME failed\n");
          return;
      }
      self->force_idr = FALSE;
  }
  if (is_cuvid == TRUE && self->force_intra) {
      if (!set_v4l2_video_mpeg_class (self->v4l2output,
                  V4L2_CID_MPEG_VIDEOENC_FORCE_INTRA_FRAME, 1)) {
          g_print ("S_EXT_CTRLS for FORCE_INTRA_FRAME failed\n");
          return;
      }
      self->force_intra = FALSE;
  }
#endif

  /* FIXME Check if buffer isn't the last one here */

  GST_LOG_OBJECT (encoder, "Process output buffer");
  ret =
      gst_v4l2_buffer_pool_process (GST_V4L2_BUFFER_POOL
      (self->v4l2capture->pool), &buffer);

  if (ret != GST_FLOW_OK)
    goto beach;

#ifdef USE_V4L2_TARGET_NV
  frame = gst_v4l2_video_enc_find_nearest_frame (self, buffer,
          gst_video_encoder_get_frames (GST_VIDEO_ENCODER (self)));
#else
  frame = gst_v4l2_video_enc_get_oldest_frame (encoder);
#endif

  if (frame) {
    frame->output_buffer = buffer;
    buffer = NULL;

    if(enable_latency_measurement) /* TODO with better option */
    {
      gpointer in_time = g_hash_table_lookup (self->hash_pts_systemtime,
              &frame->pts);
      gdouble input_time = *((gdouble*)in_time);
      gdouble output_time = get_current_system_timestamp ();
      if (output_time < input_time)
      {
          gdouble time = G_MAXDOUBLE - input_time;
          g_print ("Encode Latency = %f \n", output_time + time);
      }
      else
      {
          g_print ("Encode Latency = %f \n", (output_time - input_time));
      }
      GstCaps *reference = gst_caps_new_simple ("video/x-raw",
          "component_name", G_TYPE_STRING, GST_ELEMENT_NAME(self),
          /*"frame_num", G_TYPE_INT, self->frame_num++,*/
          "in_timestamp", G_TYPE_DOUBLE, input_time,
          "out_timestamp", G_TYPE_DOUBLE, output_time,
          NULL);
      GstReferenceTimestampMeta * enc_meta =
        gst_buffer_add_reference_timestamp_meta (frame->output_buffer, reference,
            0, 0);
      if(enc_meta == NULL)
      {
        GST_DEBUG_OBJECT (encoder, "enc_meta: %p", enc_meta);
      }
      gst_caps_unref(reference);
    }

#ifdef USE_V4L2_TARGET_NV

    if (self->copy_meta == TRUE)
    {
        if (!gst_buffer_copy_into (frame->output_buffer, frame->input_buffer,
                    (GstBufferCopyFlags)GST_BUFFER_COPY_METADATA, 0, -1)) {
            GST_DEBUG_OBJECT (encoder, "Buffer metadata copy failed \n");
        }
    }

    if (self->tracing_file_enc) {
      gettimeofday (&ts, NULL);
      done_time = ((gint64) ts.tv_sec * 1000000 + ts.tv_usec) / 1000;

      in_time_pt = g_queue_pop_head (self->got_frame_pt);
      gst_v4l2_trace_printf (self->tracing_file_enc,
          "KPI: v4l2: frameNumber= %lld encoder= %lld ms pts= %lld\n",
          frame->system_frame_number, done_time - *in_time_pt, frame->pts);

      g_free (in_time_pt);
    }
#endif

    ret = gst_video_encoder_finish_frame (encoder, frame);

    if (ret != GST_FLOW_OK)
      goto beach;
  } else {
    GST_WARNING_OBJECT (encoder, "Encoder is producing too many buffers");
    gst_buffer_unref (buffer);
  }

  return;

beach:
  GST_DEBUG_OBJECT (encoder, "Leaving output thread");

  gst_buffer_replace (&buffer, NULL);
  self->output_flow = ret;
  g_atomic_int_set (&self->processing, FALSE);
  gst_v4l2_object_unlock (self->v4l2output);
  gst_pad_pause_task (encoder->srcpad);
}

static void
gst_v4l2_video_enc_loop_stopped (GstV4l2VideoEnc * self)
{
  if (g_atomic_int_get (&self->processing)) {
    GST_DEBUG_OBJECT (self, "Early stop of encoding thread");
    self->output_flow = GST_FLOW_FLUSHING;
    g_atomic_int_set (&self->processing, FALSE);
  }

  GST_DEBUG_OBJECT (self, "Encoding task destroyed: %s",
      gst_flow_get_name (self->output_flow));

}

static GstFlowReturn
gst_v4l2_video_enc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);
  GstFlowReturn ret = GST_FLOW_OK;
  GstTaskState task_state;
#ifdef USE_V4L2_TARGET_NV
  guint64 *in_time;
  struct timeval ts;
#endif

  GST_DEBUG_OBJECT (self, "Handling frame %d", frame->system_frame_number);

#ifdef USE_V4L2_TARGET_NV
  if (self->tracing_file_enc) {
    gettimeofday (&ts, NULL);
    in_time = g_malloc (sizeof (guint64));
    *in_time = ((gint64) ts.tv_sec * 1000000 + ts.tv_usec) / 1000;
    g_queue_push_tail (self->got_frame_pt, in_time);
  }
#endif

  if (enable_latency_measurement)
  {
      self->buffer_in_time = get_current_system_timestamp ();
      g_hash_table_insert (self->hash_pts_systemtime, &frame->pts, &self->buffer_in_time);
  }

  if (G_UNLIKELY (!g_atomic_int_get (&self->active)))
    goto flushing;

  task_state = gst_pad_get_task_state (GST_VIDEO_DECODER_SRC_PAD (self));
  if (task_state == GST_TASK_STOPPED || task_state == GST_TASK_PAUSED) {
    GstBufferPool *pool = GST_BUFFER_POOL (self->v4l2output->pool);

    /* It possible that the processing thread stopped due to an error */
    if (self->output_flow != GST_FLOW_OK &&
        self->output_flow != GST_FLOW_FLUSHING &&
        self->output_flow != GST_V4L2_FLOW_LAST_BUFFER) {
      GST_DEBUG_OBJECT (self, "Processing loop stopped with error, leaving");
      ret = self->output_flow;
      goto drop;
    }

    /* Ensure input internal pool is active */
    if (!gst_buffer_pool_is_active (pool)) {
      GstStructure *config = gst_buffer_pool_get_config (pool);
      guint min = MAX (self->v4l2output->min_buffers, GST_V4L2_MIN_BUFFERS);

      gst_buffer_pool_config_set_params (config, self->input_state->caps,
          self->v4l2output->info.size, min, min);

      /* There is no reason to refuse this config */
      if (!gst_buffer_pool_set_config (pool, config))
        goto activate_failed;

      if (!gst_buffer_pool_set_active (pool, TRUE))
        goto activate_failed;
    }

#ifdef USE_V4L2_TARGET_NV
    set_v4l2_video_mpeg_class (self->v4l2capture,
              V4L2_CID_MPEG_SET_POLL_INTERRUPT, 1);
#endif

    GST_DEBUG_OBJECT (self, "Starting encoding thread");

    /* Start the processing task, when it quits, the task will disable input
     * processing to unlock input if draining, or prevent potential block */
    if (!gst_pad_start_task (encoder->srcpad,
            (GstTaskFunction) gst_v4l2_video_enc_loop, self,
            (GDestroyNotify) gst_v4l2_video_enc_loop_stopped))
      goto start_task_failed;
  }

  if (frame->input_buffer) {
    
    GstVideoSEIMeta *meta =
        (GstVideoSEIMeta *) gst_buffer_get_meta (frame->input_buffer,
                GST_VIDEO_SEI_META_API_TYPE);
    if (!meta)
    {
        GST_DEBUG ("NO META RETRIEVED BY ENCODER\n");
    }
    else
    {
        uint32_t total_metadata_size = meta->sei_metadata_size;
        GST_DEBUG_OBJECT (self, "total metadata size = %d\n", total_metadata_size);
        self->v4l2output->sei_payload_size = 0;
        self->v4l2output->sei_payload = NULL;
        if (meta->sei_metadata_type == (guint)GST_USER_SEI_META)
        {
            self->v4l2output->sei_payload_size = meta->sei_metadata_size;
            self->v4l2output->sei_payload = (void *) meta->sei_metadata_ptr;
        }
    }

    GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);
    ret =
        gst_v4l2_buffer_pool_process (GST_V4L2_BUFFER_POOL
        (self->v4l2output->pool), &frame->input_buffer);
    GST_VIDEO_ENCODER_STREAM_LOCK (encoder);

    if (ret == GST_FLOW_FLUSHING) {
      if (gst_pad_get_task_state (GST_VIDEO_DECODER_SRC_PAD (self)) !=
          GST_TASK_STARTED)
        ret = self->output_flow;
      goto drop;
    } else if (ret != GST_FLOW_OK) {
      goto process_failed;
    }
  }

  gst_video_codec_frame_unref (frame);
  return ret;

  /* ERRORS */
activate_failed:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS,
        (_("Failed to allocate required memory.")),
        ("Buffer pool activation failed"));
    return GST_FLOW_ERROR;

  }
flushing:
  {
    ret = GST_FLOW_FLUSHING;
    goto drop;
  }
start_task_failed:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        (_("Failed to start encoding thread.")), (NULL));
    g_atomic_int_set (&self->processing, FALSE);
    ret = GST_FLOW_ERROR;
    goto drop;
  }
process_failed:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        (_("Failed to process frame.")),
        ("Maybe be due to not enough memory or failing driver"));
    ret = GST_FLOW_ERROR;
    goto drop;
  }
drop:
  {
    gst_video_encoder_finish_frame (encoder, frame);
    return ret;
  }
}

static gboolean
gst_v4l2_video_enc_decide_allocation (GstVideoEncoder *
    encoder, GstQuery * query)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);
  GstVideoCodecState *state = gst_video_encoder_get_output_state (encoder);
  GstV4l2Error error = GST_V4L2_ERROR_INIT;
  GstClockTime latency;
  gboolean ret = FALSE;

#ifdef USE_V4L2_TARGET_NV
  if (self->v4l2capture->active) {
    if (self->v4l2capture->pool) {
      GST_DEBUG_OBJECT (self->v4l2capture->dbg_obj, "deactivating pool");
        gst_buffer_pool_set_active (self->v4l2capture->pool, FALSE);
    }
    GST_V4L2_SET_INACTIVE (self->v4l2capture);
  }
#endif

  /* We need to set the format here, since this is called right after
   * GstVideoEncoder have set the width, height and framerate into the state
   * caps. These are needed by the driver to calculate the buffer size and to
   * implement bitrate adaptation. */
  if (!gst_v4l2_object_set_format (self->v4l2capture, state->caps, &error)) {
    gst_v4l2_error (self, &error);
    ret = FALSE;
    goto done;
  }

  if (gst_v4l2_object_decide_allocation (self->v4l2capture, query)) {
    GstVideoEncoderClass *enc_class = GST_VIDEO_ENCODER_CLASS (parent_class);
    ret = enc_class->decide_allocation (encoder, query);
  }

  /* FIXME This may not be entirely correct, as encoder may keep some
   * observation withouth delaying the encoding. Linux Media API need some
   * more work to explicitly expressed the decoder / encoder latency. This
   * value will then become max latency, and the reported driver latency would
   * become the min latency. */
  latency = self->v4l2capture->min_buffers * self->v4l2capture->duration;
  gst_video_encoder_set_latency (encoder, latency, latency);

done:
  gst_video_codec_state_unref (state);
  return ret;
}

static gboolean
gst_v4l2_video_enc_propose_allocation (GstVideoEncoder *
    encoder, GstQuery * query)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);
  gboolean ret = FALSE;

  GST_DEBUG_OBJECT (self, "called");

  if (query == NULL)
    ret = TRUE;
  else
    ret = gst_v4l2_object_propose_allocation (self->v4l2output, query);

  if (ret)
    ret = GST_VIDEO_ENCODER_CLASS (parent_class)->propose_allocation (encoder,
        query);

  return ret;
}

static gboolean
gst_v4l2_video_enc_src_query (GstVideoEncoder * encoder, GstQuery * query)
{
  gboolean ret = TRUE;
  switch (GST_QUERY_TYPE (query)) {
#ifndef USE_V4L2_TARGET_NV
    case GST_QUERY_CAPS:{
      GstCaps *filter, *result = NULL;
      GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);
      GstPad *pad = GST_VIDEO_ENCODER_SRC_PAD (encoder);

      gst_query_parse_caps (query, &filter);

      /* FIXME Try and not probe the entire encoder, but only the implement
       * subclass format */
      if (self->probed_srccaps) {
        GstCaps *tmpl = gst_pad_get_pad_template_caps (pad);
        result = gst_caps_intersect (tmpl, self->probed_srccaps);
        gst_caps_unref (tmpl);
      } else
        result = gst_pad_get_pad_template_caps (pad);

      if (filter) {
        GstCaps *tmp = result;
        result =
            gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref (tmp);
      }

      GST_DEBUG_OBJECT (self, "Returning src caps %" GST_PTR_FORMAT, result);

      gst_query_set_caps_result (query, result);
      gst_caps_unref (result);
      break;
    }
#endif

   /*
    * Drop upstream "GST_QUERY_SEEKING" query from h264parse element.
    * This is a WAR to avoid memory leaks from h264parse element
    */
    case GST_QUERY_SEEKING:{
       return ret;
    }

    default:
      ret = GST_VIDEO_ENCODER_CLASS (parent_class)->src_query (encoder, query);
      break;
  }

  return ret;
}

static gboolean
gst_v4l2_video_enc_sink_query (GstVideoEncoder * encoder, GstQuery * query)
{
  gboolean ret = TRUE;

  switch (GST_QUERY_TYPE (query)) {
#ifndef USE_V4L2_TARGET_NV
    case GST_QUERY_CAPS:{
      GstCaps *filter, *result = NULL;
      GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);
      GstPad *pad = GST_VIDEO_ENCODER_SINK_PAD (encoder);

      gst_query_parse_caps (query, &filter);

      if (self->probed_sinkcaps)
        result = gst_caps_ref (self->probed_sinkcaps);
      else
        result = gst_pad_get_pad_template_caps (pad);

      if (filter) {
        GstCaps *tmp = result;
        result =
            gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref (tmp);
      }

      GST_DEBUG_OBJECT (self, "Returning sink caps %" GST_PTR_FORMAT, result);

      gst_query_set_caps_result (query, result);
      gst_caps_unref (result);
      break;
    }
#endif

    default:
      ret = GST_VIDEO_ENCODER_CLASS (parent_class)->sink_query (encoder, query);
      break;
  }

  return ret;
}

static gboolean
gst_v4l2_video_enc_sink_event (GstVideoEncoder * encoder, GstEvent * event)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);
  gboolean ret;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      GST_DEBUG_OBJECT (self, "flush start");
      gst_v4l2_object_unlock (self->v4l2output);
      gst_v4l2_object_unlock (self->v4l2capture);
      break;
    default:
      break;
  }

  ret = GST_VIDEO_ENCODER_CLASS (parent_class)->sink_event (encoder, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
#ifdef USE_V4L2_TARGET_NV
    set_v4l2_video_mpeg_class (self->v4l2capture,
              V4L2_CID_MPEG_SET_POLL_INTERRUPT, 0);
#endif
      gst_pad_stop_task (encoder->srcpad);
      GST_DEBUG_OBJECT (self, "flush start done");
    default:
      break;
  }

#ifdef USE_V4L2_TARGET_NV
  if ((GstNvCustomEventType)GST_EVENT_TYPE (event) == GST_NVEVENT_ENC_BITRATE_UPDATE) {
    gchar* stream_id = NULL;
    gst_nvevent_parse_enc_bitrate_update (event, &stream_id, &self->bitrate);
    if (GST_V4L2_IS_OPEN (self->v4l2output)) {
      if (!set_v4l2_video_mpeg_class (self->v4l2output,
          V4L2_CID_MPEG_VIDEO_BITRATE, self->bitrate)) {
        g_print ("S_EXT_CTRLS for BITRATE failed\n");
      }
    }
  }

  if ((GstNvCustomEventType)GST_EVENT_TYPE (event) == GST_NVEVENT_ENC_FORCE_IDR) {
    gchar* stream_id = NULL;
    gst_nvevent_parse_enc_force_idr (event, &stream_id, &self->force_idr);
  }

  if ((GstNvCustomEventType)GST_EVENT_TYPE (event) == GST_NVEVENT_ENC_FORCE_INTRA) {
    gchar* stream_id = NULL;
    gst_nvevent_parse_enc_force_intra (event, &stream_id, &self->force_intra);
  }

  if ((GstNvCustomEventType)GST_EVENT_TYPE (event) == GST_NVEVENT_ENC_IFRAME_INTERVAL_UPDATE) {
    gchar* stream_id = NULL;
    gst_nvevent_parse_enc_iframeinterval_update (event, &stream_id, &self->iframeinterval);
    if (!set_v4l2_video_mpeg_class (self->v4l2output,
      V4L2_CID_MPEG_VIDEO_GOP_SIZE, self->iframeinterval)) {
      g_print ("S_EXT_CTRLS for GOP_SIZE failed\n");
      return FALSE;
    }
  }
#endif

  return ret;
}

static GstStateChangeReturn
gst_v4l2_video_enc_change_state (GstElement * element,
    GstStateChange transition)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (element);

  if (transition == GST_STATE_CHANGE_PAUSED_TO_READY) {
    g_atomic_int_set (&self->active, FALSE);
    gst_v4l2_object_unlock (self->v4l2output);
    gst_v4l2_object_unlock (self->v4l2capture);

#ifdef USE_V4L2_TARGET_NV
  set_v4l2_video_mpeg_class (self->v4l2capture,
              V4L2_CID_MPEG_SET_POLL_INTERRUPT, 0);
#endif

  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}


static void
gst_v4l2_video_enc_dispose (GObject * object)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (object);

  gst_caps_replace (&self->probed_sinkcaps, NULL);
  gst_caps_replace (&self->probed_srccaps, NULL);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_v4l2_video_enc_finalize (GObject * object)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (object);

  gst_v4l2_object_destroy (self->v4l2capture);
  gst_v4l2_object_destroy (self->v4l2output);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_v4l2_video_enc_init (GstV4l2VideoEnc * self)
{
  /* V4L2 object are created in subinstance_init */
#ifdef USE_V4L2_TARGET_NV
  self->ratecontrol = DEFAULT_RATE_CONTROL;
  self->bitrate = GST_V4L2_VIDEO_ENC_BITRATE_DEFAULT;
  self->peak_bitrate = GST_V4L2_VIDEO_ENC_PEAK_BITRATE_DEFAULT;
  self->idrinterval = DEFAULT_IDR_FRAME_INTERVAL;
  self->iframeinterval = DEFAULT_INTRA_FRAME_INTERVAL;
  self->quant_i_frames = GST_V4L2_VIDEO_ENC_QUANT_I_FRAMES_DEFAULT;
  self->quant_p_frames = GST_V4L2_VIDEO_ENC_QUANT_P_FRAMES_DEFAULT;
  self->quant_b_frames = GST_V4L2_VIDEO_ENC_QUANT_B_FRAMES_DEFAULT;
  self->MinQpI = (guint) - 1;
  self->MaxQpI = (guint) - 1;
  self->MinQpP = (guint) - 1;
  self->MaxQpP = (guint) - 1;
  self->MinQpB = (guint) - 1;
  self->MaxQpB = (guint) - 1;
  self->set_qpRange = FALSE;
  self->force_idr = FALSE;
  self->force_intra = FALSE;
  self->hw_preset_level = DEFAULT_HW_PRESET_LEVEL;
  self->virtual_buffer_size = DEFAULT_VBV_SIZE;
  self->ratecontrol_enable = TRUE;
  self->maxperf_enable = FALSE;
  self->measure_latency = FALSE;
  self->slice_output = FALSE;
  self->best_prev = NULL;
  self->buf_pts_prev = GST_CLOCK_STIME_NONE;
  if (is_cuvid == TRUE)
  {
    self->cudaenc_gpu_id = DEFAULT_CUDAENC_GPU_ID;
    self->cudaenc_preset_id = DEFAULT_CUDAENC_PRESET_ID;
    self->cudaenc_tuning_info_id = DEFAULT_TUNING_INFO_PRESET;
  }

  const gchar * latency = g_getenv("NVDS_ENABLE_LATENCY_MEASUREMENT");
  if(latency)
  {
    enable_latency_measurement = TRUE;
  }

#endif
}

static void
gst_v4l2_video_enc_subinstance_init (GTypeInstance * instance, gpointer g_class)
{
  GstV4l2VideoEncClass *klass = GST_V4L2_VIDEO_ENC_CLASS (g_class);
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (instance);

  self->v4l2output = gst_v4l2_object_new (GST_ELEMENT (self),
      GST_OBJECT (GST_VIDEO_ENCODER_SINK_PAD (self)),
      V4L2_BUF_TYPE_VIDEO_OUTPUT, klass->default_device,
      gst_v4l2_get_output, gst_v4l2_set_output, NULL);
  self->v4l2output->no_initial_format = TRUE;
  self->v4l2output->keep_aspect = FALSE;

  self->v4l2capture = gst_v4l2_object_new (GST_ELEMENT (self),
      GST_OBJECT (GST_VIDEO_ENCODER_SRC_PAD (self)),
      V4L2_BUF_TYPE_VIDEO_CAPTURE, klass->default_device,
      gst_v4l2_get_input, gst_v4l2_set_input, NULL);
  self->v4l2capture->no_initial_format = TRUE;
  self->v4l2output->keep_aspect = FALSE;
}

static void
gst_v4l2_video_enc_class_init (GstV4l2VideoEncClass * klass)
{
  GstElementClass *element_class;
  GObjectClass *gobject_class;
  GstVideoEncoderClass *video_encoder_class;

  parent_class = g_type_class_peek_parent (klass);

  element_class = (GstElementClass *) klass;
  gobject_class = (GObjectClass *) klass;
  video_encoder_class = (GstVideoEncoderClass *) klass;

  GST_DEBUG_CATEGORY_INIT (gst_v4l2_video_enc_debug, "v4l2videoenc", 0,
      "V4L2 Video Encoder");

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_finalize);
  if (is_cuvid == FALSE) {
    gobject_class->set_property =
        GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_set_property_tegra);
    gobject_class->get_property =
        GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_get_property_tegra);
  } else if (is_cuvid == TRUE) {
    gobject_class->set_property =
        GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_set_property_cuvid);
    gobject_class->get_property =
        GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_get_property_cuvid);
  }
#ifdef USE_V4L2_TARGET_NV
  g_object_class_install_property (gobject_class, PROP_RATE_CONTROL,
      g_param_spec_enum ("control-rate", "ratecontrol",
          "Set control rate for v4l2 encode",
          GST_TYPE_V4L2_VID_ENC_RATECONTROL, DEFAULT_RATE_CONTROL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_BITRATE,
      g_param_spec_uint ("bitrate", "Target Bitrate",
          "Set bitrate for v4l2 encode",
          0, G_MAXUINT, GST_V4L2_VIDEO_ENC_BITRATE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_INTRA_FRAME_INTERVAL,
      g_param_spec_uint ("iframeinterval", "Intra Frame interval",
          "Encoding Intra Frame occurance frequency",
          0, G_MAXUINT, DEFAULT_INTRA_FRAME_INTERVAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_QUANT_RANGE,
        g_param_spec_string ("qp-range", "qpp-range",
            "Qunatization range for P, I and B frame,\n"
            "\t\t\t Use string with unsigned integer values of Qunatization Range \n"
            "\t\t\t in MinQpP,MaxQpP:MinQpI,MaxQpI:MinQpB,MaxQpB order, to set the property.",
            "-1,-1:-1,-1:-1,-1",
            (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_IDR_FRAME_INTERVAL,
        g_param_spec_uint ("idrinterval", "IDR Frame interval",
            "Encoding IDR Frame occurance frequency",
            0, G_MAXUINT, DEFAULT_IDR_FRAME_INTERVAL,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
            GST_PARAM_MUTABLE_READY));

  if (is_cuvid == TRUE) {
    g_object_class_install_property (gobject_class, PROP_CUDAENC_GPU_ID,
        g_param_spec_uint ("gpu-id",
            "GPU Device ID",
            "Set to GPU Device ID for Encoder ",
            0,
            G_MAXUINT, DEFAULT_CUDAENC_GPU_ID,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

    g_object_class_install_property (gobject_class, PROP_CUDAENC_PRESET_ID,
        g_param_spec_uint ("preset-id",
            "CUVID Encoder Preset ID",
            "For each tuning info, seven presets from P1 (highest performance) to P7 (lowest performance) \n"
            "\t\t\thave been provided to control performance/quality trade off. Using these presets will\n"
            "\t\t\tautomatically set all relevant encoding parameters for the selected tuning info ",
            1,
            7, DEFAULT_CUDAENC_PRESET_ID,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

    g_object_class_install_property (gobject_class, PROP_CUDAENC_CONSTQP,
        g_param_spec_string ("constqp", "CUVID Encoder constqp values",
            "Set unsigned integer values for constqp, control-rate should be set to GST_V4L2_VIDENC_CONSTANT_QP,\n"
            "\t\t\tUse string with values of constQP in constQpI:constQpP:constQpB order, to set the property.",
            "0:0:0",
            (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property (gobject_class, PROP_CUDAENC_INITQP,
        g_param_spec_string ("initqp", "CUVID Encoder initqp values",
            "Set unsigned integer values for initqp,\n"
            "\t\t\tUse string with values of initQP in IInitQP:PInitQP:BInitQP order, to set the property.\n"
	    "\t\t\tThis provides rough hint to encoder to influence the qp difference between I, P and B",
            "0:0:0",
            (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property (gobject_class, PROP_FORCE_IDR,
        g_param_spec_boolean ("force-idr",
            "Force an IDR frame",
            "Force an IDR frame",
            FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
            GST_PARAM_MUTABLE_READY));

    g_object_class_install_property (gobject_class, PROP_CUDAENC_TUNING_INFO_ID,
        g_param_spec_enum ("tuning-info-id", "TuningInfoforHWEncoder",
            "Tuning Info Preset for encoder",
            GST_TYPE_V4L2_VID_ENC_TUNING_INFO_PRESET,
            DEFAULT_TUNING_INFO_PRESET,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
            GST_PARAM_MUTABLE_READY));

    g_object_class_install_property (gobject_class, PROP_FORCE_INTRA,
        g_param_spec_boolean ("force-intra",
            "Force an INTRA frame",
            "Force an INTRA frame",
            FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
            GST_PARAM_MUTABLE_READY));

    g_object_class_install_property (gobject_class, PROP_COPY_METADATA,
        g_param_spec_boolean ("copy-meta",
            "Copies input metadata on output buffer",
            "Copies input metadata on output buffer",
            FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
            GST_PARAM_MUTABLE_READY));

  } else if (is_cuvid == FALSE) {
    g_object_class_install_property (gobject_class, PROP_PEAK_BITRATE,
        g_param_spec_uint ("peak-bitrate", "Peak Bitrate",
            "Peak bitrate in variable control-rate\n"
            "\t\t\t The value must be >= bitrate\n"
            "\t\t\t (1.2*bitrate) is set by default(Default: 0)",
            0, G_MAXUINT, GST_V4L2_VIDEO_ENC_PEAK_BITRATE_DEFAULT,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
            GST_PARAM_MUTABLE_PLAYING));

    g_object_class_install_property (gobject_class, PROP_QUANT_I_FRAMES,
        g_param_spec_uint ("quant-i-frames", "I-Frame Quantization",
            "Quantization parameter for I-frames (0xffffffff=component default),\n"
            "\t\t\t use with ratecontrol-enable = 0\n"
            "\t\t\t and preset-level = 0",
            0, G_MAXUINT, GST_V4L2_VIDEO_ENC_QUANT_I_FRAMES_DEFAULT,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
            GST_PARAM_MUTABLE_READY));

    g_object_class_install_property (gobject_class, PROP_QUANT_P_FRAMES,
        g_param_spec_uint ("quant-p-frames", "P-Frame Quantization",
            "Quantization parameter for P-frames (0xffffffff=component default),\n"
            "\t\t\t use with ratecontrol-enable = 0\n"
            "\t\t\t and preset-level = 0",
            0, G_MAXUINT, GST_V4L2_VIDEO_ENC_QUANT_P_FRAMES_DEFAULT,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
            GST_PARAM_MUTABLE_READY));

    g_object_class_install_property (gobject_class, PROP_QUANT_B_FRAMES,
        g_param_spec_uint ("quant-b-frames", "B-Frame Quantization",
            "Quantization parameter for B-frames (0xffffffff=component default),\n"
            "\t\t\t use with ratecontrol-enable = 0\n"
            "\t\t\t and preset-level = 0",
            0, G_MAXUINT, GST_V4L2_VIDEO_ENC_QUANT_B_FRAMES_DEFAULT,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
            GST_PARAM_MUTABLE_READY));

    g_object_class_install_property (gobject_class, PROP_HW_PRESET_LEVEL,
        g_param_spec_enum ("preset-level", "HWpresetlevelforencoder",
            "HW preset level for encoder",
            GST_TYPE_V4L2_VID_ENC_HW_PRESET_LEVEL,
            DEFAULT_HW_PRESET_LEVEL,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
            GST_PARAM_MUTABLE_READY));

    g_object_class_install_property (gobject_class, PROP_VIRTUAL_BUFFER_SIZE,
        g_param_spec_uint ("vbv-size", "vb size attribute",
            "virtual buffer size ",
            0, G_MAXUINT, DEFAULT_VBV_SIZE,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
            GST_PARAM_MUTABLE_READY));

    g_object_class_install_property (gobject_class, PROP_MEASURE_LATENCY,
        g_param_spec_boolean ("MeasureEncoderLatency",
            "Enable Measure Encoder Latency",
            "Enable Measure Encoder latency Per Frame",
            FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
            GST_PARAM_MUTABLE_READY));

    g_object_class_install_property (gobject_class, PROP_RC_ENABLE,
        g_param_spec_boolean ("ratecontrol-enable",
            "Enable or Disable rate control mode",
            "Enable or Disable rate control mode",
            TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
            GST_PARAM_MUTABLE_READY));

    g_object_class_install_property (gobject_class, PROP_MAX_PERF,
        g_param_spec_boolean ("maxperf-enable",
            "Enable or Disable Max Performance mode",
            "Enable or Disable Max Performance mode",
            FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
            GST_PARAM_MUTABLE_READY));

    /* Signals */
    gst_v4l2_signals[SIGNAL_FORCE_IDR] =
        g_signal_new ("force-IDR",
       G_TYPE_FROM_CLASS (video_encoder_class),
        (GSignalFlags) (G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
        G_STRUCT_OFFSET (GstV4l2VideoEncClass, force_IDR),
        NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

    klass->force_IDR = gst_v4l2_video_encoder_forceIDR;
  }
#endif

  video_encoder_class->open = GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_open);
  video_encoder_class->close = GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_close);
  video_encoder_class->start = GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_start);
  video_encoder_class->stop = GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_stop);
  video_encoder_class->finish = GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_finish);
  video_encoder_class->flush = GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_flush);
  video_encoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_set_format);
  video_encoder_class->negotiate =
      GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_negotiate);
  video_encoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_decide_allocation);
  video_encoder_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_propose_allocation);
  video_encoder_class->sink_query =
      GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_sink_query);
  video_encoder_class->src_query =
      GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_src_query);
  video_encoder_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_sink_event);
  video_encoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_handle_frame);

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_change_state);
#ifdef USE_V4L2_TARGET_NV
  klass->set_video_encoder_properties = set_v4l2_video_encoder_properties;
#endif

  gst_v4l2_object_install_m2m_properties_helper (gobject_class);
#ifdef USE_V4L2_TARGET_NV
  if (is_cuvid == FALSE)
    gst_v4l2_object_install_m2m_enc_iomode_properties_helper (gobject_class);
#endif
}

#ifndef USE_V4L2_TARGET_NV
static void
gst_v4l2_video_enc_subclass_init (gpointer g_class, gpointer data)
{
  GstV4l2VideoEncClass *klass = GST_V4L2_VIDEO_ENC_CLASS (g_class);
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstV4l2VideoEncCData *cdata = data;

  klass->default_device = cdata->device;

  /* Note: gst_pad_template_new() take the floating ref from the caps */
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          cdata->sink_caps));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          cdata->src_caps));

  gst_caps_unref (cdata->sink_caps);
  gst_caps_unref (cdata->src_caps);

  g_free (cdata);
}
#else
static void
gst_v4l2_video_enc_subclass_init (gpointer g_class, gpointer data)
{
  GstV4l2VideoEncClass *klass = GST_V4L2_VIDEO_ENC_CLASS (g_class);
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstV4l2VideoEncCData *cdata = data;

  klass->default_device = cdata->device;

  /* Note: gst_pad_template_new() take the floating ref from the caps */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_v4l2enc_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          cdata->src_caps));

  g_free (cdata);
}
#endif

/* Probing functions */
gboolean
gst_v4l2_is_video_enc (GstCaps * sink_caps, GstCaps * src_caps,
    GstCaps * codec_caps)
{
  gboolean ret = FALSE;
  gboolean (*check_caps) (const GstCaps *, const GstCaps *);

  if (codec_caps) {
    check_caps = gst_caps_can_intersect;
  } else {
    codec_caps = gst_v4l2_object_get_codec_caps ();
    check_caps = gst_caps_is_subset;
  }

  if (gst_caps_is_subset (sink_caps, gst_v4l2_object_get_raw_caps ())
      && check_caps (src_caps, codec_caps))
    ret = TRUE;

  return ret;
}

void
gst_v4l2_video_enc_register (GstPlugin * plugin, GType type,
    const char *codec, const gchar * basename, const gchar * device_path,
    GstCaps * sink_caps, GstCaps * codec_caps, GstCaps * src_caps)
{
  GTypeQuery type_query;
  GTypeInfo type_info = { 0, };
  GType subtype;
  gchar *type_name;
  GstV4l2VideoEncCData *cdata;
#ifndef USE_V4L2_TARGET_NV
  GstCaps *filtered_caps;

  filtered_caps = gst_caps_intersect (src_caps, codec_caps);
#endif

  cdata = g_new0 (GstV4l2VideoEncCData, 1);
  cdata->device = g_strdup (device_path);
#ifndef USE_V4L2_TARGET_NV
  cdata->sink_caps = gst_caps_ref (sink_caps);
  cdata->src_caps = gst_caps_ref (filtered_caps);
#else
  cdata->sink_caps = gst_caps_ref (gst_static_caps_get(&sink_template_caps));
  cdata->src_caps = gst_caps_ref (codec_caps);
#endif

  g_type_query (type, &type_query);
  memset (&type_info, 0, sizeof (type_info));
  type_info.class_size = type_query.class_size;
  type_info.instance_size = type_query.instance_size;
  type_info.class_init = gst_v4l2_video_enc_subclass_init;
  type_info.class_data = cdata;
  type_info.instance_init = gst_v4l2_video_enc_subinstance_init;

  /* The first encoder to be registered should use a constant name, like
   * v4l2h264enc, for any additional encoders, we create unique names. Encoder
   * names may change between boots, so this should help gain stable names for
   * the most common use cases. */
#ifndef USE_V4L2_TARGET_NV
  type_name = g_strdup_printf ("v4l2%senc", codec);

  if (g_type_from_name (type_name) != 0) {
    g_free (type_name);
    type_name = g_strdup_printf ("v4l2%s%senc", basename, codec);
  }
#else
  type_name = g_strdup_printf ("nvv4l2%senc", codec);
#endif

  subtype = g_type_register_static (type, type_name, &type_info, 0);

  if (!gst_element_register (plugin, type_name, GST_RANK_PRIMARY + 1, subtype))
    GST_WARNING ("Failed to register plugin '%s'", type_name);

  g_free (type_name);
}


#ifdef USE_V4L2_TARGET_NV
gint
gst_v4l2_trace_file_open (FILE ** TracingFile)
{
  gchar buf[4096] = { };

  const gchar *homedir = g_getenv ("HOME");
  if (!homedir)
    homedir = g_get_home_dir ();

  if (homedir == NULL)
    return -1;

  snprintf (buf, sizeof (buf) - 1, "%s/gst_v4l2_enc_latency_%d.log",
      homedir, (gint) getpid ());

  *TracingFile = fopen (buf, "w");

  if (*TracingFile == NULL) {
    return -1;
  }
  return 0;
}

void
gst_v4l2_trace_file_close (FILE * TracingFile)
{
  if (TracingFile == NULL)
    return;
  fclose (TracingFile);
  TracingFile = NULL;
}

void
gst_v4l2_trace_printf (FILE * TracingFile, const gchar *fmt, ...)
{
  va_list ap;

  if (TracingFile != NULL) {
    va_start (ap, fmt);
    vfprintf (TracingFile, fmt, ap);
    fprintf (TracingFile, "\n");
    fflush (TracingFile);
    va_end (ap);
  }
}

static GType
gst_v4l2_videnc_tuning_info_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      /*{V4L2_ENC_TUNING_INFO_UNDEFINED,
          "No Tuning Info", "UndefinedTuningInfo"},*/
      {V4L2_ENC_TUNING_INFO_HIGH_QUALITY,
          "Tuning Preset for High Quality", "HighQualityPreset"},
      {V4L2_ENC_TUNING_INFO_LOW_LATENCY,
          "Tuning Preset for Low Latency", "LowLatencyPreset"},
      {V4L2_ENC_TUNING_INFO_ULTRA_LOW_LATENCY,
          "Tuning Preset for Low Latency", "UltraLowLatencyPreset"},
      {V4L2_ENC_TUNING_INFO_LOSSLESS,
          "Tuning Preset for Lossless", "LosslessPreset"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstV4L2VideoEncTuingInfoPreset", values);
  }
  return qtype;
}

static GType
gst_v4l2_videnc_hw_preset_level_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {V4L2_ENC_HW_PRESET_DISABLE, "Disable HW-Preset",
          "DisablePreset"},
      {V4L2_ENC_HW_PRESET_ULTRAFAST, "UltraFastPreset for high perf",
          "UltraFastPreset"},
      {V4L2_ENC_HW_PRESET_FAST, "FastPreset", "FastPreset"},
      {V4L2_ENC_HW_PRESET_MEDIUM, "MediumPreset", "MediumPreset"},
      {V4L2_ENC_HW_PRESET_SLOW, "SlowPreset", "SlowPreset"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstV4L2VideoEncHwPreset", values);
  }
  return qtype;
}

static GType
gst_v4l2_videnc_ratecontrol_get_type (void)
{
  static volatile gsize ratecontrol = 0;
  if (is_cuvid == false) {
      static const GEnumValue rc_type[] = {
          {V4L2_MPEG_VIDEO_BITRATE_MODE_VBR, "GST_V4L2_VIDENC_VARIABLE_BITRATE",
              "variable_bitrate"},
          {V4L2_MPEG_VIDEO_BITRATE_MODE_CBR, "GST_V4L2_VIDENC_CONSTANT_BITRATE",
              "constant_bitrate"},
          {0, NULL, NULL}
      };

      if (g_once_init_enter (&ratecontrol)) {
          GType tmp =
              g_enum_register_static ("GstV4l2VideoEncRateControlType", rc_type);
          g_once_init_leave (&ratecontrol, tmp);
      }
  } else {
      static const GEnumValue rc_type_cuvid[] = {
          {V4L2_MPEG_VIDEO_BITRATE_MODE_VBR, "GST_V4L2_VIDENC_VARIABLE_BITRATE",
              "variable_bitrate"},
          {V4L2_MPEG_VIDEO_BITRATE_MODE_CBR, "GST_V4L2_VIDENC_CONSTANT_BITRATE",
              "constant_bitrate"},
          {V4L2_MPEG_VIDEO_BITRATE_MODE_CONSTQP, "GST_V4L2_VIDENC_CONSTANT_QP",
              "constantQP"},
          {0, NULL, NULL}
      };

      if (g_once_init_enter (&ratecontrol)) {
          GType tmp =
              g_enum_register_static ("GstV4l2VideoEncRateControlType", rc_type_cuvid);
          g_once_init_leave (&ratecontrol, tmp);
      }
  }
  return (GType) ratecontrol;
}

static gboolean
gst_v4l2_video_enc_parse_constqp (GstV4l2VideoEnc * self,
    const gchar * arr)
{
  gchar *str;
  self->constQpI = atoi (arr);
  str = g_strstr_len (arr, -1, ":") + 1;
  self->constQpP = atoi (str);
  str = g_strstr_len (str, -1, ":") + 1;
  self->constQpB = atoi (str);

  return TRUE;
}

static gboolean
gst_v4l2_video_enc_parse_initqp (GstV4l2VideoEnc * self,
    const gchar * arr)
{
  gchar *str;
  self->IInitQP = atoi (arr);
  str = g_strstr_len (arr, -1, ":") + 1;
  self->PInitQP = atoi (str);
  str = g_strstr_len (str, -1, ":") + 1;
  self->BInitQP = atoi (str);

  return TRUE;
}

static gboolean
gst_v4l2_video_enc_parse_quantization_range (GstV4l2VideoEnc * self,
    const gchar * arr)
{
  gchar *str;
  self->MinQpP = atoi (arr);
  str = g_strstr_len (arr, -1, ",");
  self->MaxQpP = atoi (str + 1);
  str = g_strstr_len (str, -1, ":");
  self->MinQpI = atoi (str + 1);
  str = g_strstr_len (str, -1, ",");
  self->MaxQpI = atoi (str + 1);
  str = g_strstr_len (str, -1, ":");
  self->MinQpB = atoi (str + 1);
  str = g_strstr_len (str, -1, ",");
  self->MaxQpB = atoi (str + 1);

  return TRUE;
}

gboolean
setHWPresetType (GstV4l2Object * v4l2object, guint label,
    enum v4l2_enc_hw_preset_type type)
{
  struct v4l2_ext_control control;
  struct v4l2_ext_controls ctrls;
  gint ret;

  memset (&control, 0, sizeof (control));
  memset (&ctrls, 0, sizeof (ctrls));

  ctrls.count = 1;
  ctrls.controls = &control;
  ctrls.ctrl_class = V4L2_CTRL_CLASS_MPEG;

  control.id = label;
  control.value = type;

  ret = v4l2object->ioctl (v4l2object->video_fd, VIDIOC_S_EXT_CTRLS, &ctrls);
  if (ret < 0) {
    g_print ("Error while setting control rate\n");
    return FALSE;
  }

  return TRUE;
}

gboolean
setQpRange (GstV4l2Object * v4l2object, guint label, guint MinQpI, guint MaxQpI,
    guint MinQpP, guint MaxQpP, guint MinQpB, guint MaxQpB)
{
  v4l2_ctrl_video_qp_range qprange;
  struct v4l2_ext_control control;
  struct v4l2_ext_controls ctrls;
  gint ret;

  memset (&control, 0, sizeof (control));
  memset (&ctrls, 0, sizeof (ctrls));

  qprange.MinQpI = MinQpI;
  qprange.MaxQpI = MaxQpI;
  qprange.MinQpP = MinQpP;
  qprange.MaxQpP = MaxQpP;
  qprange.MinQpB = MinQpB;
  qprange.MaxQpB = MaxQpB;

  ctrls.count = 1;
  ctrls.controls = &control;
  ctrls.ctrl_class = V4L2_CTRL_CLASS_MPEG;

  control.id = V4L2_CID_MPEG_VIDEOENC_QP_RANGE;
  control.string = (gchar *) &qprange;

  ret = v4l2object->ioctl (v4l2object->video_fd, VIDIOC_S_EXT_CTRLS, &ctrls);
  if (ret < 0) {
    g_print ("Error while setting qp range\n");
    return FALSE;
  }
  return TRUE;
}

static void
gst_v4l2_video_encoder_forceIDR (GstV4l2VideoEnc * self)
{
  GstV4l2Object *v4l2object = self->v4l2output;
  struct v4l2_ext_control control;
  struct v4l2_ext_controls ctrls;
  gint ret;

  memset (&control, 0, sizeof (control));
  memset (&ctrls, 0, sizeof (ctrls));

  ctrls.count = 1;
  ctrls.controls = &control;
  ctrls.ctrl_class = V4L2_CTRL_CLASS_MPEG;

  control.id = V4L2_CID_MPEG_MFC51_VIDEO_FORCE_FRAME_TYPE;

  if (!GST_V4L2_IS_OPEN (v4l2object))
    g_print ("V4L2 device is not open\n");
  ret = v4l2object->ioctl (v4l2object->video_fd, VIDIOC_S_EXT_CTRLS, &ctrls);
  if (ret < 0)
    g_print ("Error while signalling force IDR\n");
}

gboolean
set_v4l2_video_encoder_properties (GstVideoEncoder * encoder)
{
  GstV4l2VideoEnc *video_enc = GST_V4L2_VIDEO_ENC (encoder);

  if (!GST_V4L2_IS_OPEN (video_enc->v4l2output)) {
    g_print ("V4L2 device is not open\n");
    return FALSE;
  }

  if (video_enc->ratecontrol == V4L2_MPEG_VIDEO_BITRATE_MODE_VBR
      && video_enc->peak_bitrate == GST_V4L2_VIDEO_ENC_PEAK_BITRATE_DEFAULT)
    video_enc->peak_bitrate = 1.2f * video_enc->bitrate;
  else if (video_enc->ratecontrol == V4L2_MPEG_VIDEO_BITRATE_MODE_VBR
      && video_enc->peak_bitrate <= video_enc->bitrate)
    video_enc->peak_bitrate = video_enc->bitrate;
  else if (video_enc->ratecontrol == V4L2_MPEG_VIDEO_BITRATE_MODE_CBR)
    video_enc->peak_bitrate = video_enc->bitrate;

  if (!set_v4l2_video_mpeg_class (video_enc->v4l2output,
      V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE, video_enc->ratecontrol_enable)) {
    g_print ("S_EXT_CTRLS for FRAME_RC_ENABLE failed\n");
    return FALSE;
  }

  if (video_enc->ratecontrol_enable) {
    if (!set_v4l2_video_mpeg_class (video_enc->v4l2output,
        V4L2_CID_MPEG_VIDEO_BITRATE_MODE, video_enc->ratecontrol)) {
      g_print ("S_EXT_CTRLS for BITRATE_MODE failed\n");
      return FALSE;
    }
  }

  if (!set_v4l2_video_mpeg_class (video_enc->v4l2output,
      V4L2_CID_MPEG_VIDEO_BITRATE, video_enc->bitrate)) {
    g_print ("S_EXT_CTRLS for BITRATE failed\n");
    return FALSE;
  }

  if (!set_v4l2_video_mpeg_class (video_enc->v4l2output,
      V4L2_CID_MPEG_VIDEO_BITRATE_PEAK, video_enc->peak_bitrate)) {
    g_print ("S_EXT_CTRLS for PEAK_BITRATE failed\n");
    return FALSE;
  }

  if (video_enc->idrinterval) {
    if (!set_v4l2_video_mpeg_class (video_enc->v4l2output,
        V4L2_CID_MPEG_VIDEO_IDR_INTERVAL, video_enc->idrinterval)) {
      g_print ("S_EXT_CTRLS for IDR_INTERVAL failed\n");
      return FALSE;
    }
  }

  if (video_enc->iframeinterval) {
    if (!set_v4l2_video_mpeg_class (video_enc->v4l2output,
        V4L2_CID_MPEG_VIDEO_GOP_SIZE, video_enc->iframeinterval)) {
      g_print ("S_EXT_CTRLS for GOP_SIZE failed\n");
      return FALSE;
    }
  }

  if (video_enc->hw_preset_level) {
    if (!setHWPresetType (video_enc->v4l2output,
        V4L2_CID_MPEG_VIDEOENC_HW_PRESET_TYPE_PARAM,
        video_enc->hw_preset_level)) {
      g_print ("S_EXT_CTRLS for HW_PRESET_TYPE_PARAM failed\n");
      return FALSE;
    }
  }

  if (video_enc->set_qpRange) {
    if (!setQpRange (video_enc->v4l2output, V4L2_CID_MPEG_VIDEOENC_QP_RANGE,
        video_enc->MinQpI, video_enc->MaxQpI, video_enc->MinQpP,
        video_enc->MaxQpP, video_enc->MinQpB, video_enc->MaxQpB)) {
      g_print ("S_EXT_CTRLS for QP_RANGE failed\n");
      return FALSE;
    }
  }

  if (video_enc->quant_i_frames != 0xffffffff && !video_enc->ratecontrol_enable) {
    if (!set_v4l2_video_mpeg_class (video_enc->v4l2output,
        V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP, video_enc->quant_i_frames)) {
      g_print ("S_EXT_CTRLS for H264_I_FRAME_QP failed\n");
      return FALSE;
    }
  }

  if (video_enc->quant_p_frames != 0xffffffff && !video_enc->ratecontrol_enable) {
    if (!set_v4l2_video_mpeg_class (video_enc->v4l2output,
        V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP, video_enc->quant_p_frames)) {
      g_print ("S_EXT_CTRLS for H264_P_FRAME_QP failed\n");
      return FALSE;
    }
  }

  if (video_enc->quant_b_frames != 0xffffffff && !video_enc->ratecontrol_enable) {
    if (!set_v4l2_video_mpeg_class (video_enc->v4l2output,
        V4L2_CID_MPEG_VIDEO_H264_B_FRAME_QP, video_enc->quant_b_frames)) {
      g_print ("S_EXT_CTRLS for H264_B_FRAME_QP failed\n");
      return FALSE;
    }
  }

  if (video_enc->maxperf_enable) {
    if (!set_v4l2_video_mpeg_class (video_enc->v4l2output,
        V4L2_CID_MPEG_VIDEO_MAX_PERFORMANCE, video_enc->maxperf_enable)) {
      g_print ("S_EXT_CTRLS for MAX_PERFORMANCE failed\n");
      return FALSE;
    }
  }

  if (!set_v4l2_video_mpeg_class (video_enc->v4l2output,
      V4L2_CID_MPEG_VIDEOENC_VIRTUALBUFFER_SIZE,
      video_enc->virtual_buffer_size)) {
    g_print ("S_EXT_CTRLS for VIRTUALBUFFER_SIZE failed\n");
    return FALSE;
  }

  return TRUE;
}
#endif
