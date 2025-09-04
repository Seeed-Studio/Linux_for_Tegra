/*
 * Copyright (c) 2018-2020, NVIDIA CORPORATION. All rights reserved.
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

#include "gstv4l2object.h"
#include "gstv4l2h265enc.h"

#include <string.h>
#include <gst/gst-i18n-plugin.h>

GST_DEBUG_CATEGORY_STATIC (gst_v4l2_h265_enc_debug);
#define GST_CAT_DEFAULT gst_v4l2_h265_enc_debug


static GstStaticCaps src_template_caps =
GST_STATIC_CAPS ("video/x-h265, stream-format=(string) byte-stream, "
    "alignment=(string) au");

static GType
gst_v4l2_videnc_profile_get_type (void);

#define GST_TYPE_V4L2_VID_ENC_PROFILE (gst_v4l2_videnc_profile_get_type ())

/* prototypes */
gboolean set_v4l2_h265_encoder_properties (GstVideoEncoder * encoder);
gboolean gst_v4l2_h265_enc_slice_header_spacing (GstV4l2Object * v4l2object,
    guint32 slice_header_spacing, enum v4l2_enc_slice_length_type slice_length_type);
void set_h265_video_enc_property (GstV4l2Object * v4l2object, guint label,
    gint param);

enum
{
  PROP_0,
  V4L2_STD_OBJECT_PROPS,
  PROP_INSERT_SPS_PPS,
  PROP_PROFILE,
  PROP_INSERT_VUI,
  PROP_EXTENDED_COLORFORMAT,
  PROP_INSERT_AUD,
  PROP_BIT_PACKETIZATION,
  PROP_SLICE_HEADER_SPACING,
  PROP_SLICE_INTRA_REFRESH_INTERVAL,
  PROP_TWO_PASS_CBR,
  PROP_ENABLE_MV_META,
  PROP_NUM_BFRAMES,
  PROP_NUM_REFERENCE_FRAMES,
  PROP_ENABLE_LOSSLESS_ENC
};

#define DEFAULT_PROFILE                              V4L2_MPEG_VIDEO_H265_PROFILE_MAIN
#define DEFAULT_BIT_PACKETIZATION                    FALSE
#define DEFAULT_SLICE_HEADER_SPACING                 0
#define DEFAULT_INTRA_REFRESH_FRAME_INTERVAL         60
#define DEFAULT_NUM_B_FRAMES                         0
#define MAX_NUM_B_FRAMES                             2
#define DEFAULT_NUM_REFERENCE_FRAMES                 1
#define MAX_NUM_REFERENCE_FRAMES                     8

#define gst_v4l2_h265_enc_parent_class parent_class
G_DEFINE_TYPE (GstV4l2H265Enc, gst_v4l2_h265_enc, GST_TYPE_V4L2_VIDEO_ENC);

static void
gst_v4l2_h265_enc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstV4l2H265Enc *self = GST_V4L2_H265_ENC (object);
  GstV4l2VideoEnc *video_enc = GST_V4L2_VIDEO_ENC (object);

  switch (prop_id) {
    case PROP_INSERT_SPS_PPS:
      self->insert_sps_pps = g_value_get_boolean (value);
      break;
    case PROP_PROFILE:
      self->profile = g_value_get_enum (value);
      if (GST_V4L2_IS_OPEN(video_enc->v4l2output)) {
        if (!set_v4l2_video_mpeg_class (video_enc->v4l2output,
            V4L2_CID_MPEG_VIDEO_H265_PROFILE, self->profile)) {
          g_print ("S_EXT_CTRLS for H265_PROFILE failed\n");
        }
      }
      break;
    case PROP_INSERT_AUD:
      self->insert_aud = g_value_get_boolean (value);
      break;
    case PROP_INSERT_VUI:
      self->insert_vui = g_value_get_boolean (value);
      break;
    /* extended-colorformat property is available for cuvid path only*/
    case PROP_EXTENDED_COLORFORMAT:
      self->extended_colorformat = g_value_get_boolean (value);
      break;
    case PROP_BIT_PACKETIZATION:
      self->bit_packetization = g_value_get_boolean (value);
      break;
    case PROP_SLICE_HEADER_SPACING:
      self->slice_header_spacing = g_value_get_uint64 (value);
      break;
    case PROP_SLICE_INTRA_REFRESH_INTERVAL:
      self->SliceIntraRefreshInterval = g_value_get_uint (value);
      break;
    case PROP_TWO_PASS_CBR:
      self->EnableTwopassCBR = g_value_get_boolean (value);
      break;
    case PROP_ENABLE_MV_META:
      self->EnableMVBufferMeta = g_value_get_boolean (value);
      video_enc->v4l2capture->enableMVBufferMeta = g_value_get_boolean (value);
      break;
    case PROP_NUM_BFRAMES:
      self->nBFrames = g_value_get_uint (value);
      if (self->nBFrames && (self->nRefFrames == DEFAULT_NUM_REFERENCE_FRAMES)) {
        // Minimum 2 Ref-Frames are required for B-frames encoding
        self->nRefFrames = 2;
      }
      break;
    case PROP_NUM_REFERENCE_FRAMES:
      self->nRefFrames = g_value_get_uint (value);
      break;
    case PROP_ENABLE_LOSSLESS_ENC:
      self->enableLossless = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_v4l2_h265_enc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstV4l2H265Enc *self = GST_V4L2_H265_ENC (object);

  switch (prop_id) {
    case PROP_INSERT_SPS_PPS:
      g_value_set_boolean (value, self->insert_sps_pps);
      break;
    case PROP_PROFILE:
      g_value_set_enum (value, self->profile);
      break;
    case PROP_INSERT_AUD:
      g_value_set_boolean (value, self->insert_aud);
      break;
    case PROP_INSERT_VUI:
      g_value_set_boolean (value, self->insert_vui);
      break;
    /* extended-colorformat property is available for cuvid path only*/
    case PROP_EXTENDED_COLORFORMAT:
      g_value_set_boolean (value, self->extended_colorformat);
      break;
    case PROP_BIT_PACKETIZATION:
      g_value_set_boolean (value, self->bit_packetization);
      break;
    case PROP_SLICE_HEADER_SPACING:
      g_value_set_uint64 (value, self->slice_header_spacing);
      break;
    case PROP_SLICE_INTRA_REFRESH_INTERVAL:
      g_value_set_uint (value, self->SliceIntraRefreshInterval);
      break;
    case PROP_TWO_PASS_CBR:
      g_value_set_boolean (value, self->EnableTwopassCBR);
      break;
    case PROP_ENABLE_MV_META:
      g_value_set_boolean (value, self->EnableMVBufferMeta);
      break;
    case PROP_NUM_BFRAMES:
      g_value_set_uint (value, self->nBFrames);
      break;
    case PROP_NUM_REFERENCE_FRAMES:
      g_value_set_uint (value, self->nRefFrames);
      break;
    case PROP_ENABLE_LOSSLESS_ENC:
      g_value_set_boolean (value, self->enableLossless);
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

  if (g_str_equal (profile, "main")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H265_PROFILE_MAIN;
  } else if (g_str_equal (profile, "main10")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H265_PROFILE_MAIN10;
  } else if (g_str_equal (profile, "mainstillpicture")) {
    v4l2_profile = V4L2_MPEG_VIDEO_H265_PROFILE_MAINSTILLPICTURE;
  } else if (g_str_equal (profile, "frext")) {
      v4l2_profile = V4L2_MPEG_VIDEO_H265_PROFILE_FREXT;
  } else {
    GST_WARNING ("Unsupported profile string '%s'", profile);
  }
  return v4l2_profile;
}

static const gchar *
v4l2_profile_to_string (gint v4l2_profile)
{
  switch (v4l2_profile) {
    case V4L2_MPEG_VIDEO_H265_PROFILE_MAIN:
      return "main";
    case V4L2_MPEG_VIDEO_H265_PROFILE_MAIN10:
      return "main10";
    case V4L2_MPEG_VIDEO_H265_PROFILE_MAINSTILLPICTURE:
      return "mainstillpicture";
    case V4L2_MPEG_VIDEO_H265_PROFILE_FREXT:
      return "frext";
    default:
      GST_WARNING ("Unsupported V4L2 profile %i", v4l2_profile);
      break;
  }
  return NULL;
}

static gint
v4l2_level_from_string (const gchar * level)
{
  gint v4l2_level = -1;

  //TODO : Since videodev2 file does not list H265 profiles
  //we need to add profiles inside v4l2_nv_extensions.h
  //and use them here.

  return v4l2_level;
}

static const gchar *
v4l2_level_to_string (gint v4l2_level)
{
  return NULL;
}

static void
gst_v4l2_h265_enc_init (GstV4l2H265Enc * self)
{
  self->insert_sps_pps = FALSE;
  self->profile = DEFAULT_PROFILE;
  self->insert_aud = FALSE;
  self->insert_vui = FALSE;
  self->extended_colorformat = FALSE;
  self->bit_packetization = DEFAULT_BIT_PACKETIZATION;
  self->slice_header_spacing = DEFAULT_SLICE_HEADER_SPACING;
  self->nRefFrames = 1;
  self->nBFrames = 0;
  self->enableLossless = FALSE;
}

static void
gst_v4l2_h265_enc_class_init (GstV4l2H265EncClass * klass)
{
  GstElementClass *element_class;
  GObjectClass *gobject_class;
  GstV4l2VideoEncClass *baseclass;

  parent_class = g_type_class_peek_parent (klass);

  element_class = (GstElementClass *) klass;
  gobject_class = (GObjectClass *) klass;
  baseclass = (GstV4l2VideoEncClass *) (klass);

  GST_DEBUG_CATEGORY_INIT (gst_v4l2_h265_enc_debug, "v4l2h265enc", 0,
      "V4L2 H.265 Encoder");

  gst_element_class_set_static_metadata (element_class,
      "V4L2 H.265 Encoder",
      "Codec/Encoder/Video",
      "Encode H.265 video streams via V4L2 API",
      "Viranjan Pagar <vpagar@nvidia.com>, Amit Pandya <apandya@nvidia.com>");

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_v4l2_h265_enc_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_v4l2_h265_enc_get_property);

#ifdef USE_V4L2_TARGET_NV
  g_object_class_install_property (gobject_class, PROP_PROFILE,
    g_param_spec_enum ("profile", "profile",
        "Set profile for v4l2 encode",
        GST_TYPE_V4L2_VID_ENC_PROFILE, DEFAULT_PROFILE,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
        GST_PARAM_MUTABLE_READY));

  if (is_cuvid == TRUE) {
    g_object_class_install_property (gobject_class, PROP_EXTENDED_COLORFORMAT,
        g_param_spec_boolean ("extended-colorformat",
            "Set Extended ColorFormat",
            "Set Extended ColorFormat pixel values 0 to 255 in VUI info",
            FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  } else if (is_cuvid == FALSE) {
    g_object_class_install_property (gobject_class, PROP_INSERT_SPS_PPS,
        g_param_spec_boolean ("insert-sps-pps",
            "Insert H.265 SPS, PPS",
            "Insert H.265 SPS, PPS at every IDR frame",
            FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, PROP_INSERT_VUI,
        g_param_spec_boolean ("insert-vui",
            "Insert H.265 VUI",
            "Insert H.265 VUI(Video Usability Information) in SPS",
            FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, PROP_INSERT_AUD,
        g_param_spec_boolean ("insert-aud",
            "Insert H.265 AUD",
            "Insert H.265 Access Unit Delimiter(AUD)",
            FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, PROP_BIT_PACKETIZATION,
        g_param_spec_boolean ("bit-packetization", "Bit Based Packetization",
            "Whether or not Packet size is based upon Number Of bits",
            DEFAULT_BIT_PACKETIZATION,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
            GST_PARAM_MUTABLE_READY));

    g_object_class_install_property (gobject_class, PROP_SLICE_HEADER_SPACING,
        g_param_spec_uint64 ("slice-header-spacing", "Slice Header Spacing",
            "Slice Header Spacing number of macroblocks/bits in one packet",
            0, G_MAXUINT64, DEFAULT_SLICE_HEADER_SPACING,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
            GST_PARAM_MUTABLE_READY));

    g_object_class_install_property (gobject_class, PROP_ENABLE_MV_META,
        g_param_spec_boolean ("EnableMVBufferMeta",
            "Enable Motion Vector Meta data",
            "Enable Motion Vector Meta data for encoding",
            FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
            GST_PARAM_MUTABLE_READY));

    g_object_class_install_property (gobject_class,
        PROP_SLICE_INTRA_REFRESH_INTERVAL,
        g_param_spec_uint ("SliceIntraRefreshInterval",
            "SliceIntraRefreshInterval", "Set SliceIntraRefreshInterval", 0,
            G_MAXUINT, DEFAULT_INTRA_REFRESH_FRAME_INTERVAL,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
            GST_PARAM_MUTABLE_READY));

    g_object_class_install_property (gobject_class, PROP_TWO_PASS_CBR,
        g_param_spec_boolean ("EnableTwopassCBR",
            "Enable Two pass CBR",
            "Enable two pass CBR while encoding",
            FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
            GST_PARAM_MUTABLE_READY));

    g_object_class_install_property (gobject_class, PROP_NUM_BFRAMES,
        g_param_spec_uint ("num-B-Frames",
            "B Frames between two reference frames",
            "Number of B Frames between two reference frames (not recommended)(Supported only on Xavier)",
            0, MAX_NUM_B_FRAMES, DEFAULT_NUM_B_FRAMES,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
            GST_PARAM_MUTABLE_READY));

    g_object_class_install_property (gobject_class, PROP_NUM_REFERENCE_FRAMES,
        g_param_spec_uint ("num-Ref-Frames",
            "Sets the number of reference frames for encoder",
            "Number of Reference Frames for encoder",
            0, MAX_NUM_REFERENCE_FRAMES, DEFAULT_NUM_REFERENCE_FRAMES,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
            GST_PARAM_MUTABLE_READY));

    g_object_class_install_property (gobject_class, PROP_ENABLE_LOSSLESS_ENC,
        g_param_spec_boolean ("enable-lossless",
            "Enable Lossless encoding",
            "Enable lossless encoding for YUV444",
            FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
            GST_PARAM_MUTABLE_READY));
  }
#endif

  baseclass->codec_name = "H265";
  baseclass->profile_cid = V4L2_CID_MPEG_VIDEO_H265_PROFILE;
  baseclass->profile_to_string = v4l2_profile_to_string;
  baseclass->profile_from_string = v4l2_profile_from_string;
  //baseclass->level_cid = V4L2_CID_MPEG_VIDEO_H265_LEVEL;
  baseclass->level_to_string = v4l2_level_to_string;
  baseclass->level_from_string = v4l2_level_from_string;
  baseclass->set_encoder_properties = set_v4l2_h265_encoder_properties;
}

/* Probing functions */
gboolean
gst_v4l2_is_h265_enc (GstCaps * sink_caps, GstCaps * src_caps)
{
  return gst_v4l2_is_video_enc (sink_caps, src_caps,
      gst_static_caps_get (&src_template_caps));
}

void
gst_v4l2_h265_enc_register (GstPlugin * plugin, const gchar * basename,
    const gchar * device_path, GstCaps * sink_caps, GstCaps * src_caps)
{
  gst_v4l2_video_enc_register (plugin, GST_TYPE_V4L2_H265_ENC,
      "h265", basename, device_path, sink_caps,
      gst_static_caps_get (&src_template_caps), src_caps);
}

static GType
gst_v4l2_videnc_profile_get_type (void)
{
  static volatile gsize profile = 0;
  static const GEnumValue profile_type[] = {
    {V4L2_MPEG_VIDEO_H265_PROFILE_MAIN,
          "GST_V4L2_H265_VIDENC_MAIN_PROFILE", "Main"},
    {V4L2_MPEG_VIDEO_H265_PROFILE_MAIN10,
          "GST_V4L2_H265_VIDENC_MAIN10_PROFILE", "Main10"},
    {V4L2_MPEG_VIDEO_H265_PROFILE_FREXT,
          "GST_V4L2_H265_VIDENC_FREXT_PROFILE", "FREXT"},
    {0, NULL, NULL}
  };

  if (g_once_init_enter (&profile)) {
    GType tmp =
        g_enum_register_static ("GstV4L2VideoEncProfileType", profile_type);
    g_once_init_leave (&profile, tmp);
  }
  return (GType) profile;
}

gboolean
gst_v4l2_h265_enc_slice_header_spacing (GstV4l2Object * v4l2object,
    guint32 slice_header_spacing, enum v4l2_enc_slice_length_type slice_length_type)
{
  struct v4l2_ext_control control;
  struct v4l2_ext_controls ctrls;
  gint ret;
  v4l2_enc_slice_length_param param =
      { slice_length_type, slice_header_spacing };

  memset (&control, 0, sizeof (control));
  memset (&ctrls, 0, sizeof (ctrls));

  ctrls.count = 1;
  ctrls.controls = &control;
  ctrls.ctrl_class = V4L2_CTRL_CLASS_MPEG;

  control.id = V4L2_CID_MPEG_VIDEOENC_SLICE_LENGTH_PARAM;
  control.string = (gchar *) &param;

  ret = v4l2object->ioctl (v4l2object->video_fd, VIDIOC_S_EXT_CTRLS, &ctrls);
  if (ret < 0) {
    g_print ("Error while setting spacing and packetization\n");
    return FALSE;
  }
  return TRUE;
}

gboolean
set_v4l2_h265_encoder_properties (GstVideoEncoder * encoder)
{
  GstV4l2H265Enc *self = GST_V4L2_H265_ENC (encoder);
  GstV4l2VideoEnc *video_enc = GST_V4L2_VIDEO_ENC (encoder);

  if (!GST_V4L2_IS_OPEN (video_enc->v4l2output)) {
    g_print ("V4L2 device is not open\n");
    return FALSE;
  }

  if (self->insert_sps_pps) {
    if (!set_v4l2_video_mpeg_class (video_enc->v4l2output,
        V4L2_CID_MPEG_VIDEOENC_INSERT_SPS_PPS_AT_IDR, 1)) {
      g_print ("S_EXT_CTRLS for INSERT_SPS_PPS_AT_IDR failed\n");
      return FALSE;
    }
  }

  if (self->profile) {
    if (!set_v4l2_video_mpeg_class (video_enc->v4l2output,
        V4L2_CID_MPEG_VIDEO_H265_PROFILE, self->profile)) {
      g_print ("S_EXT_CTRLS for H265_PROFILE failed\n");
      return FALSE;
    }
  }

  if (self->insert_vui) {
    if (!set_v4l2_video_mpeg_class (video_enc->v4l2output,
        V4L2_CID_MPEG_VIDEOENC_INSERT_VUI, 1)) {
      g_print ("S_EXT_CTRLS for INSERT_VUI failed\n");
      return FALSE;
    }
  }

  if (self->extended_colorformat) {
    if (!set_v4l2_video_mpeg_class (video_enc->v4l2output,
        V4L2_CID_MPEG_VIDEOENC_EXTEDED_COLORFORMAT, 1)) {
      g_print ("S_EXT_CTRLS for EXTENDED_COLORFORMAT failed\n");
      return FALSE;
    }
  }

  if (self->insert_aud) {
    if (!set_v4l2_video_mpeg_class (video_enc->v4l2output,
        V4L2_CID_MPEG_VIDEOENC_INSERT_AUD, 1)) {
      g_print ("S_EXT_CTRLS for INSERT_AUD failed\n");
      return FALSE;
    }
  }

  if (self->slice_header_spacing) {
    enum v4l2_enc_slice_length_type slice_length_type = V4L2_ENC_SLICE_LENGTH_TYPE_MBLK;
    if (self->bit_packetization) {
      slice_length_type = V4L2_ENC_SLICE_LENGTH_TYPE_BITS;
    }
    if (!gst_v4l2_h265_enc_slice_header_spacing (video_enc->v4l2output,
        self->slice_header_spacing, slice_length_type)) {
      g_print ("S_EXT_CTRLS for SLICE_LENGTH_PARAM failed\n");
      return FALSE;
    }
  }

  if (self->EnableMVBufferMeta) {
    if (!set_v4l2_video_mpeg_class (video_enc->v4l2output,
        V4L2_CID_MPEG_VIDEOENC_ENABLE_METADATA_MV,
        self->EnableMVBufferMeta)) {
      g_print ("S_EXT_CTRLS for ENABLE_METADATA_MV failed\n");
      return FALSE;
    }
  }

  if (self->SliceIntraRefreshInterval) {
    if (!set_v4l2_video_mpeg_class (video_enc->v4l2output,
        V4L2_CID_MPEG_VIDEOENC_SLICE_INTRAREFRESH_PARAM,
        self->SliceIntraRefreshInterval)) {
      g_print ("S_EXT_CTRLS for SLICE_INTRAREFRESH_PARAM failed\n");
      return FALSE;
    }
  }

  if (self->EnableTwopassCBR) {
    if (!set_v4l2_video_mpeg_class (video_enc->v4l2output,
        V4L2_CID_MPEG_VIDEOENC_TWO_PASS_CBR, 1)) {
      g_print ("S_EXT_CTRLS for TWO_PASS_CBR failed\n");
      return FALSE;
    }
  }

  if (self->nBFrames) {
    if (!set_v4l2_video_mpeg_class (video_enc->v4l2output,
        V4L2_CID_MPEG_VIDEOENC_NUM_BFRAMES,
        self->nBFrames)) {
      g_print ("S_EXT_CTRLS for NUM_BFRAMES failed\n");
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

  if (self->enableLossless) {
    if (!set_v4l2_video_mpeg_class (video_enc->v4l2output,
        V4L2_CID_MPEG_VIDEOENC_ENABLE_LOSSLESS, self->enableLossless)) {
      g_print ("S_EXT_CTRLS for ENABLE_LOSSLESS failed\n");
      return FALSE;
    }
  }

  return TRUE;
}
