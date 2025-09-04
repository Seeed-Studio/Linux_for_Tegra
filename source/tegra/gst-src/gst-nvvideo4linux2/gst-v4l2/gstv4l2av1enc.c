/*
 * Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
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
#include <stdlib.h>

#include "gstv4l2object.h"
#include "gstv4l2av1enc.h"

#include <string.h>
#include <gst/gst-i18n-plugin.h>

GST_DEBUG_CATEGORY_STATIC (gst_v4l2_av1_enc_debug);
#define GST_CAT_DEFAULT gst_v4l2_av1_enc_debug

static GstStaticCaps src_template_caps =
GST_STATIC_CAPS ("video/x-av1");

/* prototypes */
gboolean gst_v4l2_av1_enc_tile_configuration (GstV4l2Object * v4l2object,
    gboolean enable_tile, guint32 log2_tile_rows, guint32 log2_tile_cols);
static gboolean gst_v4l2_video_enc_parse_tile_configuration (GstV4l2Av1Enc * self,
    const gchar * arr);
gboolean set_v4l2_av1_encoder_properties (GstVideoEncoder * encoder);

enum
{
  PROP_0,
  V4L2_STD_OBJECT_PROPS,
  PROP_ENABLE_HEADER,
  PROP_ENABLE_TILE_CONFIG,
  PROP_DISABLE_CDF,
  PROP_ENABLE_SSIMRDO,
  PROP_NUM_REFERENCE_FRAMES,
};

#define DEFAULT_NUM_REFERENCE_FRAMES                 0
#define MAX_NUM_REFERENCE_FRAMES                     4

#define gst_v4l2_av1_enc_parent_class parent_class
G_DEFINE_TYPE (GstV4l2Av1Enc, gst_v4l2_av1_enc, GST_TYPE_V4L2_VIDEO_ENC);

static void
gst_v4l2_av1_enc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstV4l2Av1Enc *self = GST_V4L2_AV1_ENC (object);
  GstV4l2VideoEnc *video_enc = GST_V4L2_VIDEO_ENC (object);

  switch (prop_id) {
    case PROP_ENABLE_HEADER:
      self->EnableHeaders = g_value_get_boolean (value);
      video_enc->v4l2capture->Enable_headers = g_value_get_boolean (value);
      break;
    case PROP_ENABLE_TILE_CONFIG:
      gst_v4l2_video_enc_parse_tile_configuration (self,
          g_value_get_string (value));
      self->EnableTileConfig = TRUE;
      break;
    case PROP_DISABLE_CDF:
      self->DisableCDFUpdate = g_value_get_boolean (value);
      break;
    case PROP_ENABLE_SSIMRDO:
      self->EnableSsimRdo = g_value_get_boolean (value);
      break;
    case PROP_NUM_REFERENCE_FRAMES:
      self->nRefFrames = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_v4l2_av1_enc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstV4l2Av1Enc *self = GST_V4L2_AV1_ENC (object);

  switch (prop_id) {
    case PROP_ENABLE_HEADER:
      g_value_set_boolean (value, self->EnableHeaders);
      break;
    case PROP_ENABLE_TILE_CONFIG:
      break;
    case PROP_DISABLE_CDF:
      g_value_set_boolean (value, self->DisableCDFUpdate);
      break;
    case PROP_ENABLE_SSIMRDO:
      g_value_set_boolean (value, self->EnableSsimRdo);
      break;
    case PROP_NUM_REFERENCE_FRAMES:
      g_value_set_uint (value, self->nRefFrames);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gint
v4l2_profile_from_string (const gchar * profile)
{
  gint v4l2_profile = -1;

  if (g_str_equal (profile, "0"))
    v4l2_profile = 0;
  else if (g_str_equal (profile, "1"))
    v4l2_profile = 1;
  else if (g_str_equal (profile, "2"))
    v4l2_profile = 2;
  else if (g_str_equal (profile, "3"))
    v4l2_profile = 3;
  else
    GST_WARNING ("Unsupported profile string '%s'", profile);

  return v4l2_profile;
}

static const gchar *
v4l2_profile_to_string (gint v4l2_profile)
{
  switch (v4l2_profile) {
    case 0:
      return "0";
    case 1:
      return "1";
    case 2:
      return "2";
    case 3:
      return "3";
    default:
      GST_WARNING ("Unsupported V4L2 profile %i", v4l2_profile);
      break;
  }

  return NULL;
}

static gboolean
gst_v4l2_video_enc_parse_tile_configuration (GstV4l2Av1Enc * self,
    const gchar * arr)
{
  gchar *str;
  self->Log2TileRows = atoi (arr);
  str = g_strstr_len (arr, -1, ",");
  self->Log2TileCols = atoi (str + 1);
  return TRUE;
}

gboolean
gst_v4l2_av1_enc_tile_configuration (GstV4l2Object * v4l2object,
    gboolean enable_tile, guint32 log2_tile_rows, guint32 log2_tile_cols)
{
  struct v4l2_ext_control control;
  struct v4l2_ext_controls ctrls;
  gint ret;

  v4l2_enc_av1_tile_config param =
      {enable_tile, log2_tile_rows, log2_tile_cols};

  memset (&control, 0, sizeof (control));
  memset (&ctrls, 0, sizeof (ctrls));

  ctrls.count = 1;
  ctrls.controls = &control;
  ctrls.ctrl_class = V4L2_CTRL_CLASS_MPEG;

  control.id = V4L2_CID_MPEG_VIDEOENC_AV1_TILE_CONFIGURATION;
  control.string = (gchar *) &param;

  ret = v4l2object->ioctl (v4l2object->video_fd, VIDIOC_S_EXT_CTRLS, &ctrls);
  if (ret < 0) {
    g_print ("Error while setting tile configuration\n");
    return FALSE;
  }

  return TRUE;
}

gboolean
set_v4l2_av1_encoder_properties (GstVideoEncoder * encoder)
{
  GstV4l2Av1Enc *self = GST_V4L2_AV1_ENC (encoder);
  GstV4l2VideoEnc *video_enc = GST_V4L2_VIDEO_ENC (encoder);

  if (!GST_V4L2_IS_OPEN (video_enc->v4l2output)) {
    g_print ("V4L2 device is not open\n");
    return FALSE;
  }

  if (self->EnableTileConfig) {
    if (!gst_v4l2_av1_enc_tile_configuration (video_enc->v4l2output,
        self->EnableTileConfig, self->Log2TileRows, self->Log2TileCols)) {
      g_print ("S_EXT_CTRLS for Tile Configuration failed\n");
      return FALSE;
    }
  }

  if (!set_v4l2_video_mpeg_class (video_enc->v4l2output,
      V4L2_CID_MPEG_VIDEOENC_AV1_DISABLE_CDF_UPDATE, self->DisableCDFUpdate)) {
    g_print ("S_EXT_CTRLS for DisableCDF Update failed\n");
    return FALSE;
  }

  if (self->EnableSsimRdo) {
    if (!set_v4l2_video_mpeg_class (video_enc->v4l2output,
        V4L2_CID_MPEG_VIDEOENC_AV1_ENABLE_SSIMRDO, self->EnableSsimRdo)) {
      g_print ("S_EXT_CTRLS for SSIM RDO failed\n");
      return FALSE;
    }
  }

  if (self->nRefFrames) {
    if (!set_v4l2_video_mpeg_class (video_enc->v4l2output,
        V4L2_CID_MPEG_VIDEOENC_NUM_REFERENCE_FRAMES,
        self->nRefFrames)) {
      g_print ("S_EXT_CTRLS for NUM_REFERENCE_FRAMES failed\n");
      return FALSE;
    }
  }

  return TRUE;
}

static void
gst_v4l2_av1_enc_init (GstV4l2Av1Enc * self)
{
  self->EnableTileConfig = FALSE;
  self->DisableCDFUpdate = TRUE;
  self->EnableSsimRdo = FALSE;
  self->Log2TileRows= 0;
  self->Log2TileCols= 0;
}

static void
gst_v4l2_av1_enc_class_init (GstV4l2Av1EncClass * klass)
{
  GstElementClass *element_class;
  GObjectClass *gobject_class;
  GstV4l2VideoEncClass *baseclass;

  parent_class = g_type_class_peek_parent (klass);

  element_class = (GstElementClass *) klass;
  gobject_class = (GObjectClass *) klass;
  baseclass = (GstV4l2VideoEncClass *) (klass);

  GST_DEBUG_CATEGORY_INIT (gst_v4l2_av1_enc_debug, "v4l2av1enc", 0,
      "V4L2 AV1 Encoder");

  gst_element_class_set_static_metadata (element_class,
      "V4L2 AV1 Encoder",
      "Codec/Encoder/Video",
      "Encode AV1 video streams via V4L2 API",
      "Anuma Rathore <arathore@nvidia.com>");

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_v4l2_av1_enc_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_v4l2_av1_enc_get_property);

  g_object_class_install_property (gobject_class, PROP_ENABLE_HEADER,
      g_param_spec_boolean ("enable-headers", "Enable AV1 headers",
          "Enable AV1 file and frame headers, if enabled, dump elementary stream",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject_class, PROP_ENABLE_TILE_CONFIG,
      g_param_spec_string ("tiles", "AV1 Log2 Tile Configuration",
          "Use string with values of Tile Configuration"
          "in Log2Rows:Log2Cols. Eg: \"1,0\"",
          "0,0", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject_class, PROP_DISABLE_CDF,
      g_param_spec_boolean ("disable-cdf", "Disable CDF Update",
          "Flag to control Disable CDF Update, enabled by default",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject_class, PROP_ENABLE_SSIMRDO,
      g_param_spec_boolean ("enable-srdo", "Enable SSIM RDO",
          "Enable SSIM RDO",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject_class, PROP_NUM_REFERENCE_FRAMES,
        g_param_spec_uint ("num-Ref-Frames",
            "Sets the number of reference frames for encoder",
            "Number of Reference Frames for encoder, default set by encoder",
            0, MAX_NUM_REFERENCE_FRAMES, DEFAULT_NUM_REFERENCE_FRAMES,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
            GST_PARAM_MUTABLE_READY));

  baseclass->codec_name = "AV1";
  baseclass->profile_cid = 0; /* Only single profile supported */
  baseclass->profile_to_string = v4l2_profile_to_string;
  baseclass->profile_from_string = v4l2_profile_from_string;
  baseclass->set_encoder_properties = set_v4l2_av1_encoder_properties;
}

/* Probing functions */
gboolean
gst_v4l2_is_av1_enc (GstCaps * sink_caps, GstCaps * src_caps)
{
  return gst_v4l2_is_video_enc (sink_caps, src_caps,
      gst_static_caps_get (&src_template_caps));
}

void
gst_v4l2_av1_enc_register (GstPlugin * plugin, const gchar * basename,
    const gchar * device_path, GstCaps * sink_caps, GstCaps * src_caps)
{
  gst_v4l2_video_enc_register (plugin, GST_TYPE_V4L2_AV1_ENC,
      "av1", basename, device_path, sink_caps,
      gst_static_caps_get (&src_template_caps), src_caps);
}
