/*
 * Copyright (C) 2014 SUMOMO Computer Association.
 *     Author: ayaka <ayaka@soulik.info>
 * Copyright (c) 2018-2022, NVIDIA CORPORATION. All rights reserved.
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

#ifndef __GST_V4L2_VIDEO_ENC_H__
#define __GST_V4L2_VIDEO_ENC_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoencoder.h>
#include <gst/video/gstvideometa.h>

#include <gstv4l2object.h>
#include <gstv4l2bufferpool.h>

G_BEGIN_DECLS
#define GST_TYPE_V4L2_VIDEO_ENC \
  (gst_v4l2_video_enc_get_type())
#define GST_V4L2_VIDEO_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_V4L2_VIDEO_ENC,GstV4l2VideoEnc))
#define GST_V4L2_VIDEO_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_V4L2_VIDEO_ENC,GstV4l2VideoEncClass))
#define GST_IS_V4L2_VIDEO_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_V4L2_VIDEO_ENC))
#define GST_IS_V4L2_VIDEO_ENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_V4L2_VIDEO_ENC))
#define GST_V4L2_VIDEO_ENC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_V4L2_VIDEO_ENC, GstV4l2VideoEncClass))

typedef struct _GstV4l2VideoEnc GstV4l2VideoEnc;
typedef struct _GstV4l2VideoEncClass GstV4l2VideoEncClass;

struct _GstV4l2VideoEnc
{
  GstVideoEncoder parent;
#ifdef USE_V4L2_TARGET_NV
  guint32 ratecontrol;
  guint32 bitrate;
  guint32 peak_bitrate;
  guint32 idrinterval;
  guint32 iframeinterval;
  guint32 quant_i_frames;
  guint32 quant_p_frames;
  guint32 quant_b_frames;
  guint32 MinQpI;
  guint32 MaxQpI;
  guint32 MinQpP;
  guint32 MaxQpP;
  guint32 MinQpB;
  guint32 MaxQpB;
  guint32 constQpI;
  guint32 constQpP;
  guint32 constQpB;
  guint32 IInitQP;
  guint32 PInitQP;
  guint32 BInitQP;
  gboolean set_qpRange;
  guint32 hw_preset_level;
  guint virtual_buffer_size;
  gboolean measure_latency;
  gboolean ratecontrol_enable;
  gboolean force_idr;
  gboolean force_intra;
  gboolean maxperf_enable;
  FILE *tracing_file_enc;
  GQueue *got_frame_pt;
  guint32 cudaenc_gpu_id;
  guint32 cudaenc_preset_id;
  guint32 cudaenc_tuning_info_id;
  gboolean slice_output;
  GstVideoCodecFrame *best_prev;
  GstClockTime buf_pts_prev;
  gdouble buffer_in_time;
  GHashTable* hash_pts_systemtime;
  gboolean copy_meta;
#endif

  /* < private > */
  GstV4l2Object *v4l2output;
  GstV4l2Object *v4l2capture;

  /* pads */
  GstCaps *probed_srccaps;
  GstCaps *probed_sinkcaps;

  /* State */
  GstVideoCodecState *input_state;
  gboolean active;
  gboolean processing;
  GstFlowReturn output_flow;

};

struct _GstV4l2VideoEncClass
{
  GstVideoEncoderClass parent_class;

  gchar *default_device;
  const char *codec_name;

  guint32 profile_cid;
  const gchar *(*profile_to_string) (gint v4l2_profile);
  gint (*profile_from_string) (const gchar * profile);

#ifdef USE_V4L2_TARGET_NV
  gboolean (*set_encoder_properties) (GstVideoEncoder * encoder);
  gboolean (*set_video_encoder_properties) (GstVideoEncoder * encoder);
#endif
  guint32 level_cid;
  const gchar *(*level_to_string) (gint v4l2_level);
  gint (*level_from_string) (const gchar * level);

#ifdef USE_V4L2_TARGET_NV
  void (*force_IDR) (GstV4l2VideoEnc *);
#endif
};

GType gst_v4l2_video_enc_get_type (void);


gboolean gst_v4l2_is_video_enc (GstCaps * sink_caps, GstCaps * src_caps,
    GstCaps * codec_caps);

void gst_v4l2_video_enc_register (GstPlugin * plugin, GType type,
    const char *codec, const gchar * basename, const gchar * device_path,
    GstCaps * sink_caps, GstCaps * codec_caps, GstCaps * src_caps);

#ifdef USE_V4L2_TARGET_NV
void set_encoder_src_caps (GstVideoEncoder *encoder, GstCaps *input_caps);
gboolean is_drc (GstVideoEncoder *encoder, GstCaps *input_caps);
gboolean reconfigure_fps (GstVideoEncoder *encoder, GstCaps *input_caps, guint label);
#endif

G_END_DECLS
#endif /* __GST_V4L2_VIDEO_ENC_H__ */
