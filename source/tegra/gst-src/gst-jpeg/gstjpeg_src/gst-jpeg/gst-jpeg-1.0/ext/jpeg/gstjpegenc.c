/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2012 Collabora Ltd.
 * Copyright (c) 2014-2023, NVIDIA CORPORATION.  All rights reserved.
 *	Author : Edward Hervey <edward@collabora.com>
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
 */
/**
 * SECTION:element-jpegenc
 *
 * Encodes jpeg images.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 videotestsrc num-buffers=50 ! video/x-raw, framerate='(fraction)'5/1 ! jpegenc ! avimux ! filesink location=mjpeg.avi
 * ]| a pipeline to mux 5 JPEG frames per second into a 10 sec. long motion jpeg
 * avi.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>

#include "gstjpegenc.h"
#include "gstjpeg.h"
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/base/base.h>

#ifdef HAVE_EXIFMETA
#include "gstnvexifmeta.h"
#endif

/* experimental */
/* setting smoothig seems to have no effect in libjepeg
#define ENABLE_SMOOTHING 1
*/

GST_DEBUG_CATEGORY_STATIC (jpegenc_debug);
#define GST_CAT_DEFAULT jpegenc_debug

#define JPEG_DEFAULT_QUALITY 85
#define JPEG_DEFAULT_SMOOTHING 0
#define JPEG_DEFAULT_IDCT_METHOD	JDCT_FASTEST

/* JpegEnc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_QUALITY,
  PROP_SMOOTHING,
  PROP_IDCT_METHOD,
  PROP_OUT_GRAY,
  PROP_PERF_MEASUREMENT
};

static void gst_jpegenc_finalize (GObject * object);

static void gst_jpegenc_resync (GstJpegEnc * jpegenc);
static void gst_jpegenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_jpegenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_jpegenc_start (GstVideoEncoder * benc);
static gboolean gst_jpegenc_stop (GstVideoEncoder * benc);
static gboolean gst_jpegenc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state);
static GstFlowReturn gst_jpegenc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);
static gboolean gst_jpegenc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query);

static GstCaps * gst_jpegenc_get_caps (GstVideoEncoder * encoder,
    GstCaps *filter);

GstCaps *
gst_jpeg_enc_negotiate_caps (GstVideoEncoder * encoder, GstCaps * caps,
    GstCaps * filter);


/* static guint gst_jpegenc_signals[LAST_SIGNAL] = { 0 }; */

#define gst_jpegenc_parent_class parent_class
G_DEFINE_TYPE (GstNvJpegEnc, gst_jpegenc, GST_TYPE_VIDEO_ENCODER);

#define GST_CAPS_FEATURE_MEMORY_RMSURFACE "memory:NVMM"

/* *INDENT-OFF* */
#ifdef USE_TARGET_TEGRA
static GstStaticPadTemplate gst_jpegenc_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
    (GST_CAPS_FEATURE_MEMORY_RMSURFACE, "{ " "I420," "NV12 }") ";"
    /* TODO: Use transform functions to convert from raw->NVMM formats
     * Need to support: YUY2, UYVY, Y41B, Y42B, YVYU, Y444,
     *                  RGB, BGR, RGBx, xRGB, BGRx, xBGR
     */
    GST_VIDEO_CAPS_MAKE
        ("{ I420, YV12, GRAY8 }"))
    );
#elif USE_TARGET_GPU
static GstStaticPadTemplate gst_jpegenc_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
    (GST_CAPS_FEATURE_MEMORY_RMSURFACE, "{ " "I420," "RGB }") )
    );
#endif
/* *INDENT-ON* */

static GstStaticPadTemplate gst_jpegenc_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg, "
        "width = (int) [ 16, 65535 ], "
        "height = (int) [ 16, 65535 ], "
        "framerate = (fraction) [ 0/1, MAX ], "
        "sof-marker = (int) { 0, 1, 2, 9 }")
    );

static void
gst_jpegenc_class_init (GstJpegEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstVideoEncoderClass *venc_class;

  gobject_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;
  venc_class = (GstVideoEncoderClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_jpegenc_finalize;
  gobject_class->set_property = gst_jpegenc_set_property;
  gobject_class->get_property = gst_jpegenc_get_property;

  g_object_class_install_property (gobject_class, PROP_QUALITY,
      g_param_spec_int ("quality", "Quality", "Quality of encoding",
          0, 100, JPEG_DEFAULT_QUALITY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

#ifndef GPU_ACCELERATE
#ifdef ENABLE_SMOOTHING
  /* disabled, since it doesn't seem to work */
  g_object_class_install_property (gobject_class, PROP_SMOOTHING,
      g_param_spec_int ("smoothing", "Smoothing", "Smoothing factor",
          0, 100, JPEG_DEFAULT_SMOOTHING,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif

  g_object_class_install_property (gobject_class, PROP_IDCT_METHOD,
      g_param_spec_enum ("idct-method", "IDCT Method",
          "The IDCT algorithm to use", GST_TYPE_NV_IDCT_METHOD,
          JPEG_DEFAULT_IDCT_METHOD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PERF_MEASUREMENT,
          g_param_spec_boolean ("Enableperf",
              "Enable encode time measurement",
              "Enable encode time measurement",
              FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY));
#endif

#ifdef GPU_ACCELERATE
  g_object_class_install_property (gobject_class, PROP_OUT_GRAY,
          g_param_spec_boolean ("OutputGray",
              "Output JPEG image in GRAYSCALE, needs I420 INPUT",
              "Output JPEG image in GRAYSCALE, needs I420 INPUT",
              FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY));
#endif

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_jpegenc_sink_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_jpegenc_src_pad_template));
  gst_element_class_set_static_metadata (element_class, "JPEG image encoder",
      "Codec/Encoder/Image",
      "Encode images in JPEG format", "Wim Taymans <wim.taymans@tvd.be>");

  venc_class->start = gst_jpegenc_start;
  venc_class->stop = gst_jpegenc_stop;
  venc_class->set_format = gst_jpegenc_set_format;
  venc_class->handle_frame = gst_jpegenc_handle_frame;
  venc_class->propose_allocation = gst_jpegenc_propose_allocation;
  venc_class->getcaps = gst_jpegenc_get_caps;

  GST_DEBUG_CATEGORY_INIT (jpegenc_debug, "jpegenc", 0,
      "JPEG encoding element");
}

static void
gst_jpegenc_init_destination (j_compress_ptr cinfo)
{
  GST_DEBUG ("gst_jpegenc_chain: init_destination");
}

static void
ensure_memory (GstJpegEnc * jpegenc)
{
  GstMemory *new_memory;
  GstMapInfo map;
  gsize old_size, desired_size, new_size;
  guint8 *new_data;
  static GstAllocationParams params = { 0, 3, 0, 0, };

  old_size = jpegenc->output_map.size;
  if (old_size == 0)
    desired_size = jpegenc->bufsize;
  else
    desired_size = old_size * 2;

  /* Our output memory wasn't big enough.
   * Make a new memory that's twice the size, */
  new_memory = gst_allocator_alloc (NULL, desired_size, &params);
  gst_memory_map (new_memory, &map, GST_MAP_READWRITE);
  new_data = map.data;
  new_size = map.size;

  /* copy previous data if any */
  if (jpegenc->output_mem) {
    memcpy (new_data, jpegenc->output_map.data, old_size);
    gst_memory_unmap (jpegenc->output_mem, &jpegenc->output_map);
    gst_memory_unref (jpegenc->output_mem);
  }

  /* drop it into place, */
  jpegenc->output_mem = new_memory;
  jpegenc->output_map = map;

  /* and last, update libjpeg on where to work. */
  jpegenc->jdest.next_output_byte = new_data + old_size;
  jpegenc->jdest.free_in_buffer = new_size - old_size;
}

static boolean
gst_jpegenc_flush_destination (j_compress_ptr cinfo)
{
  GstJpegEnc *jpegenc = (GstJpegEnc *) (cinfo->client_data);

  GST_DEBUG_OBJECT (jpegenc,
      "gst_jpegenc_chain: flush_destination: buffer too small");

  ensure_memory (jpegenc);

  return TRUE;
}

static void
gst_jpegenc_term_destination (j_compress_ptr cinfo)
{
  GstBuffer *outbuf;
  GstJpegEnc *jpegenc = (GstJpegEnc *) (cinfo->client_data);
  gsize memory_size = jpegenc->output_map.size - jpegenc->jdest.free_in_buffer;
  GstByteReader reader =
      GST_BYTE_READER_INIT (jpegenc->output_map.data, memory_size);
  guint16 marker;
  gint sof_marker = -1;

  GST_DEBUG_OBJECT (jpegenc, "gst_jpegenc_chain: term_source");

  /* Find the SOF marker */
  while (gst_byte_reader_get_uint16_be (&reader, &marker)) {
    /* SOF marker */
    if (marker >> 4 == 0x0ffc) {
      sof_marker = marker & 0x4;
      break;
    }
  }

  gst_memory_unmap (jpegenc->output_mem, &jpegenc->output_map);
  /* Trim the buffer size. we will push it in the chain function */
  gst_memory_resize (jpegenc->output_mem, 0, memory_size);
  jpegenc->output_map.data = NULL;
  jpegenc->output_map.size = 0;

  if (jpegenc->sof_marker != sof_marker) {
    GstVideoCodecState *output;
    output =
        gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (jpegenc),
        gst_caps_new_simple ("image/jpeg", "sof-marker", G_TYPE_INT, sof_marker,
            NULL), jpegenc->input_state);
    gst_video_codec_state_unref (output);
    jpegenc->sof_marker = sof_marker;
  }

  outbuf = gst_buffer_new ();
  gst_buffer_copy_into (outbuf, jpegenc->current_frame->input_buffer,
      GST_BUFFER_COPY_METADATA, 0, -1);
  gst_buffer_append_memory (outbuf, jpegenc->output_mem);
  jpegenc->output_mem = NULL;

  jpegenc->current_frame->output_buffer = outbuf;

  if(!jpegenc->cinfo.IsVendorbuf)
  {
      gst_video_frame_unmap (&jpegenc->current_vframe);
  }
  GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (jpegenc->current_frame);

  jpegenc->res = gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (jpegenc),
      jpegenc->current_frame);
  jpegenc->current_frame = NULL;
}

static void
gst_jpegenc_init (GstJpegEnc * jpegenc)
{
  /* setup jpeglib */
  memset (&jpegenc->cinfo, 0, sizeof (jpegenc->cinfo));
  memset (&jpegenc->jerr, 0, sizeof (jpegenc->jerr));
  jpegenc->cinfo.err = jpeg_std_error (&jpegenc->jerr);
  jpeg_create_compress (&jpegenc->cinfo);

  jpegenc->jdest.init_destination = gst_jpegenc_init_destination;
  jpegenc->jdest.empty_output_buffer = gst_jpegenc_flush_destination;
  jpegenc->jdest.term_destination = gst_jpegenc_term_destination;
  jpegenc->cinfo.dest = &jpegenc->jdest;
  jpegenc->cinfo.client_data = jpegenc;

  /* init properties */
  jpegenc->quality = JPEG_DEFAULT_QUALITY;
  jpegenc->smoothing = JPEG_DEFAULT_SMOOTHING;
  jpegenc->idct_method = JPEG_DEFAULT_IDCT_METHOD;
  jpegenc->bMeasure_ImageProcessTime = FALSE;
}

static void
gst_jpegenc_finalize (GObject * object)
{
  GstJpegEnc *filter = GST_JPEGENC (object);

  jpeg_destroy_compress (&filter->cinfo);

  if (filter->input_state)
    gst_video_codec_state_unref (filter->input_state);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_video_enc_check_nvfeatures (GstVideoEncoder * encoder, GstVideoCodecState * state)
{
    GstJpegEnc *enc = GST_JPEGENC (encoder);
    GstCapsFeatures *feature;
    feature = gst_caps_get_features (state->caps, 0);

    if (gst_caps_features_contains (feature, "memory:NVMM"))
    {
        GST_DEBUG_OBJECT (encoder, "Setting encoder to use Nvmm buffer");
        enc->cinfo.IsVendorbuf = TRUE;
    }
}

static gboolean
gst_jpegenc_set_format (GstVideoEncoder * encoder, GstVideoCodecState * state)
{
  GstJpegEnc *enc = GST_JPEGENC (encoder);
  guint i;
  GstVideoInfo *info = &state->info;

  gst_video_enc_check_nvfeatures (encoder, state);

  if (enc->input_state)
    gst_video_codec_state_unref (enc->input_state);
  enc->input_state = gst_video_codec_state_ref (state);

  /* prepare a cached image description  */
  enc->channels = GST_VIDEO_INFO_N_COMPONENTS (info);

  /* ... but any alpha is disregarded in encoding */
  if (GST_VIDEO_INFO_IS_GRAY (info))
    enc->channels = 1;

  enc->h_max_samp = 0;
  enc->v_max_samp = 0;
  for (i = 0; i < enc->channels; ++i) {
    enc->cwidth[i] = GST_VIDEO_INFO_COMP_WIDTH (info, i);
    enc->cheight[i] = GST_VIDEO_INFO_COMP_HEIGHT (info, i);
    enc->inc[i] = GST_VIDEO_INFO_COMP_PSTRIDE (info, i);
    enc->h_samp[i] =
        GST_ROUND_UP_4 (GST_VIDEO_INFO_WIDTH (info)) / enc->cwidth[i];
    enc->h_max_samp = MAX (enc->h_max_samp, enc->h_samp[i]);
    enc->v_samp[i] =
        GST_ROUND_UP_4 (GST_VIDEO_INFO_HEIGHT (info)) / enc->cheight[i];
    enc->v_max_samp = MAX (enc->v_max_samp, enc->v_samp[i]);
  }
  /* samp should only be 1, 2 or 4 */
  g_assert (enc->h_max_samp <= 4);
  g_assert (enc->v_max_samp <= 4);

  /* now invert */
  /* maximum is invariant, as one of the components should have samp 1 */
  for (i = 0; i < enc->channels; ++i) {
    GST_DEBUG ("%d %d", enc->h_samp[i], enc->h_max_samp);
    enc->h_samp[i] = enc->h_max_samp / enc->h_samp[i];
    enc->v_samp[i] = enc->v_max_samp / enc->v_samp[i];
  }
  enc->planar = (enc->inc[0] == 1 && enc->inc[1] == 1 && enc->inc[2] == 1);

  gst_jpegenc_resync (enc);

  return TRUE;
}

static void
gst_jpegenc_resync (GstJpegEnc * jpegenc)
{
  GstVideoInfo *info;
  gint width, height;
  guint i;
  gint j;

  GST_DEBUG_OBJECT (jpegenc, "resync");

  if (!jpegenc->input_state)
    return;

  info = &jpegenc->input_state->info;

  jpegenc->cinfo.image_width = width = GST_VIDEO_INFO_WIDTH (info);
  jpegenc->cinfo.image_height = height = GST_VIDEO_INFO_HEIGHT (info);
  jpegenc->cinfo.input_components = jpegenc->channels;

  GST_DEBUG_OBJECT (jpegenc, "width %d, height %d", width, height);
  GST_DEBUG_OBJECT (jpegenc, "format %d", GST_VIDEO_INFO_FORMAT (info));

  if (GST_VIDEO_INFO_IS_RGB (info)) {
    GST_DEBUG_OBJECT (jpegenc, "RGB");
    jpegenc->cinfo.in_color_space = JCS_RGB;
  } else if (GST_VIDEO_INFO_IS_GRAY (info)) {
    GST_DEBUG_OBJECT (jpegenc, "gray");
    jpegenc->cinfo.in_color_space = JCS_GRAYSCALE;
  } else {
    GST_DEBUG_OBJECT (jpegenc, "YUV");
    jpegenc->cinfo.in_color_space = JCS_YCbCr;
  }

  /* input buffer size as max output */
  jpegenc->bufsize = GST_VIDEO_INFO_SIZE (info);
  jpeg_set_defaults (&jpegenc->cinfo);
  jpegenc->cinfo.raw_data_in = TRUE;
  /* duh, libjpeg maps RGB to YUV ... and don't expect some conversion */
  if (jpegenc->cinfo.in_color_space == JCS_RGB)
    jpeg_set_colorspace (&jpegenc->cinfo, JCS_RGB);

  GST_DEBUG_OBJECT (jpegenc, "h_max_samp=%d, v_max_samp=%d",
      jpegenc->h_max_samp, jpegenc->v_max_samp);
  /* image dimension info */
  for (i = 0; i < jpegenc->channels; i++) {
    GST_DEBUG_OBJECT (jpegenc, "comp %i: h_samp=%d, v_samp=%d", i,
        jpegenc->h_samp[i], jpegenc->v_samp[i]);
    jpegenc->cinfo.comp_info[i].h_samp_factor = jpegenc->h_samp[i];
    jpegenc->cinfo.comp_info[i].v_samp_factor = jpegenc->v_samp[i];
    g_free (jpegenc->line[i]);
    jpegenc->line[i] = g_new (guchar *, jpegenc->v_max_samp * DCTSIZE);
    if (!jpegenc->planar) {
      for (j = 0; j < jpegenc->v_max_samp * DCTSIZE; j++) {
        g_free (jpegenc->row[i][j]);
        jpegenc->row[i][j] = g_malloc (width);
        jpegenc->line[i][j] = jpegenc->row[i][j];
      }
    }
  }

  /* guard against a potential error in gst_jpegenc_term_destination
     which occurs iff bufsize % 4 < free_space_remaining */
  jpegenc->bufsize = GST_ROUND_UP_4 (jpegenc->bufsize);

  jpeg_suppress_tables (&jpegenc->cinfo, TRUE);

  GST_DEBUG_OBJECT (jpegenc, "resync done");
}

static GstFlowReturn
gst_jpegenc_handle_frame (GstVideoEncoder * encoder, GstVideoCodecFrame * frame)
{
  GstJpegEnc *jpegenc;
  guint height;
  guchar *base[3], *end[3];
  guint stride[3];
  guint i, k;
  gint j;
  static GstAllocationParams params = { 0, 0, 0, 3, };
#ifdef HAVE_EXIFMETA
  GstNvExifMeta *meta = NULL;
#endif

  jpegenc = GST_JPEGENC (encoder);

  GST_LOG_OBJECT (jpegenc, "got new frame");

  if(!jpegenc->cinfo.IsVendorbuf)
  {
    if (!gst_video_frame_map (&jpegenc->current_vframe,
          &jpegenc->input_state->info, frame->input_buffer, GST_MAP_READ))
      goto invalid_frame;
  }
  jpegenc->current_frame = frame;

  height = GST_VIDEO_INFO_HEIGHT (&jpegenc->input_state->info);


  if(jpegenc->cinfo.IsVendorbuf)
  {
      GstMapInfo map = GST_MAP_INFO_INIT;

      gst_buffer_map(frame->input_buffer, &map, GST_MAP_READ);
      jpegenc->cinfo.pVendor_buf = map.data; /* TODO:Try to copy to base[i] and line instead of pNv_buf*/
      gst_buffer_unmap(frame->input_buffer, &map);

#ifdef HAVE_EXIFMETA
      meta = gst_buffer_get_nvexif_meta (frame->input_buffer);
      if (meta != NULL)
        jpegenc->cinfo.exif_data = TRUE;
      else
        jpegenc->cinfo.exif_data = FALSE;
#endif
  }
  else
  {
      for (i = 0; i < jpegenc->channels; i++) {
          base[i] = GST_VIDEO_FRAME_COMP_DATA (&jpegenc->current_vframe, i);
          stride[i] = GST_VIDEO_FRAME_COMP_STRIDE (&jpegenc->current_vframe, i);
          end[i] =
              base[i] + GST_VIDEO_FRAME_COMP_HEIGHT (&jpegenc->current_vframe,
                      i) * stride[i];
      }
  }

  jpegenc->res = GST_FLOW_OK;

  /* extra 512 Kbytes are required for some case encoded bitstream exceeds
     input buffer size
     ex: gst-launch-1.0 videotestsrc num-buffers=1 pattern=1 ! \
         'video/x-raw,width=(int)5300,height=(int)4000' ! \
         nvvidconv ! 'video/x-raw(memory:NVMM),format=I420' ! \
         nvjpegenc quality=100 ! filesink location=test.jpg                  */
  jpegenc->cinfo.outputBuffSize = jpegenc->bufsize + (512 << 10);

  jpegenc->output_mem = gst_allocator_alloc (NULL, jpegenc->cinfo.outputBuffSize, &params);
  gst_memory_map (jpegenc->output_mem, &jpegenc->output_map, GST_MAP_READWRITE);
  jpegenc->jdest.next_output_byte = jpegenc->output_map.data;
  jpegenc->jdest.free_in_buffer = jpegenc->output_map.size;

  /* prepare for raw input */
#if JPEG_LIB_VERSION >= 70
  jpegenc->cinfo.do_fancy_downsampling = FALSE;
#endif
  jpegenc->cinfo.smoothing_factor = jpegenc->smoothing;
  jpegenc->cinfo.dct_method = jpegenc->idct_method;
  jpeg_set_quality (&jpegenc->cinfo, jpegenc->quality, TRUE);
#ifdef GPU_ACCELERATE
  jpegenc->cinfo.grayscale = jpegenc->out_gray;
#endif
#ifdef USE_TARGET_TEGRA
  jpegenc->cinfo.bMeasure_ImageProcessTime = jpegenc->bMeasure_ImageProcessTime;
#endif

#ifdef HAVE_EXIFMETA
  /* If exif_data is TRUE, we will get EXIF header and data along with SOI*/
  if (jpegenc->cinfo.exif_data)
  {
      jpegenc->cinfo.skip_SOI = TRUE;

      /* copy exif info */
      if (meta)
        memcpy(jpegenc->jdest.next_output_byte, (guchar *)meta->exifHeader, meta->length);

      /* Insert dummy byte as SOF should start from even address as per
       * gst_byte_reader_get_uint16_be(). Otherwise SOF marker detection fails.
       * This seems to be a bug in OSS. Need to analyze in more detail.
       */

      jpegenc->jdest.next_output_byte+= meta->length + (meta->length & 1);
      jpegenc->cinfo.header_len = meta->length + (meta->length & 1);
  }
#endif

  jpeg_start_compress (&jpegenc->cinfo, TRUE);

  GST_LOG_OBJECT (jpegenc, "compressing");

  /* accelerated libjpeg library accepets complete frame as input */
  /* Do not call jpeg_write_raw_data() on mcu row basis */
  if(jpegenc->cinfo.IsVendorbuf)
  {
#ifdef USE_TARGET_TEGRA
      jpeg_write_raw_data (&jpegenc->cinfo, jpegenc->line,
          jpegenc->v_max_samp * DCTSIZE);
#endif
  }
  else
  {
    if (jpegenc->planar) {
      for (i = 0; i < height; i += jpegenc->v_max_samp * DCTSIZE) {
        for (k = 0; k < jpegenc->channels; k++) {
          for (j = 0; j < jpegenc->v_samp[k] * DCTSIZE; j++) {
            jpegenc->line[k][j] = base[k];
            if (base[k] + stride[k] < end[k])
              base[k] += stride[k];
          }
        }
        jpeg_write_raw_data (&jpegenc->cinfo, jpegenc->line,
            jpegenc->v_max_samp * DCTSIZE);
      }
    } else {
      for (i = 0; i < height; i += jpegenc->v_max_samp * DCTSIZE) {
        for (k = 0; k < jpegenc->channels; k++) {
          for (j = 0; j < jpegenc->v_samp[k] * DCTSIZE; j++) {
            guchar *src, *dst;
            gint l;

            /* ouch, copy line */
            src = base[k];
            dst = jpegenc->line[k][j];
            for (l = jpegenc->cwidth[k]; l > 0; l--) {
              *dst = *src;
              src += jpegenc->inc[k];
              dst++;
            }
            if (base[k] + stride[k] < end[k])
              base[k] += stride[k];
          }
        }
        jpeg_write_raw_data (&jpegenc->cinfo, jpegenc->line,
            jpegenc->v_max_samp * DCTSIZE);
      }
    }
  }

  /* This will ensure that gst_jpegenc_term_destination is called */
  jpeg_finish_compress (&jpegenc->cinfo);
  GST_LOG_OBJECT (jpegenc, "compressing done");

  return jpegenc->res;

invalid_frame:
    {
      GST_WARNING_OBJECT (jpegenc, "invalid frame received");
      return gst_video_encoder_finish_frame (encoder, frame);
    }
}

static gboolean
gst_jpegenc_propose_allocation (GstVideoEncoder * encoder, GstQuery * query)
{
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->propose_allocation (encoder,
      query);
}

GstCaps *
gst_jpeg_enc_negotiate_caps (GstVideoEncoder * encoder, GstCaps * caps,
    GstCaps * filter)
{
  GstCaps *templ_caps;
  GstCaps *allowed;
  GstCaps *fcaps, *filter_caps;
  GstCapsFeatures *feature;
  guint i, j;

  /* Allow downstream to specify width/height/framerate/PAR constraints
   * and forward them upstream for video converters to handle
   */
  templ_caps =
    caps ? gst_caps_ref (caps) :
    gst_pad_get_pad_template_caps (encoder->sinkpad);
  allowed = gst_pad_get_allowed_caps (encoder->srcpad);

  if (!allowed || gst_caps_is_empty (allowed) || gst_caps_is_any (allowed)) {
    fcaps = templ_caps;
    goto done;
  }

  GST_LOG_OBJECT (encoder, "template caps %" GST_PTR_FORMAT, templ_caps);
  GST_LOG_OBJECT (encoder, "allowed caps %" GST_PTR_FORMAT, allowed);

  filter_caps = gst_caps_new_empty ();

  for (i = 0; i < gst_caps_get_size (templ_caps); i++) {
    GQuark q_name = gst_structure_get_name_id (gst_caps_get_structure (templ_caps, i));
    gchar *f_name = gst_caps_features_to_string (gst_caps_get_features (templ_caps, i));

    for (j = 0; j < gst_caps_get_size (allowed); j++) {
      const GstStructure *allowed_s = gst_caps_get_structure (allowed, j);
      const GValue *val;
      GstStructure *s;

      s = gst_structure_new_id_empty (q_name);
      feature = gst_caps_features_new (f_name, NULL);
      if ((val = gst_structure_get_value (allowed_s, "width")))
        gst_structure_set_value (s, "width", val);
      if ((val = gst_structure_get_value (allowed_s, "height")))
        gst_structure_set_value (s, "height", val);
      if ((val = gst_structure_get_value (allowed_s, "framerate")))
        gst_structure_set_value (s, "framerate", val);
      if ((val = gst_structure_get_value (allowed_s, "pixel-aspect-ratio")))
        gst_structure_set_value (s, "pixel-aspect-ratio", val);

      filter_caps = gst_caps_merge_structure_full (filter_caps, s, feature);
    }
  }

  fcaps = gst_caps_intersect (filter_caps, templ_caps);
  gst_caps_unref (filter_caps);
  gst_caps_unref (templ_caps);

  if (filter) {
    GST_LOG_OBJECT (encoder, "intersecting with %" GST_PTR_FORMAT, filter);
    filter_caps = gst_caps_intersect (fcaps, filter);
    gst_caps_unref (fcaps);
    fcaps = filter_caps;
  }

done:
  gst_caps_replace (&allowed, NULL);

  GST_LOG_OBJECT (encoder, "proxy caps %" GST_PTR_FORMAT, fcaps);

  return fcaps;
}

static GstCaps * gst_jpegenc_get_caps (GstVideoEncoder * encoder,
    GstCaps *filter)
{
    GstCaps *ret;
    ret = gst_jpeg_enc_negotiate_caps (encoder, NULL, filter);
    return ret;
}

static void
gst_jpegenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstJpegEnc *jpegenc = GST_JPEGENC (object);

  GST_OBJECT_LOCK (jpegenc);

  switch (prop_id) {
    case PROP_QUALITY:
      jpegenc->quality = g_value_get_int (value);
      break;
#ifdef ENABLE_SMOOTHING
    case PROP_SMOOTHING:
      jpegenc->smoothing = g_value_get_int (value);
      break;
#endif
    case PROP_IDCT_METHOD:
      jpegenc->idct_method = g_value_get_enum (value);
      break;
    case PROP_OUT_GRAY:
      jpegenc->out_gray = g_value_get_boolean (value);
      break;
    default:
    case PROP_PERF_MEASUREMENT:
      jpegenc->bMeasure_ImageProcessTime = g_value_get_boolean (value);
      break;
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (jpegenc);
}

static void
gst_jpegenc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstJpegEnc *jpegenc = GST_JPEGENC (object);

  GST_OBJECT_LOCK (jpegenc);

  switch (prop_id) {
    case PROP_QUALITY:
      g_value_set_int (value, jpegenc->quality);
      break;
#ifdef ENABLE_SMOOTHING
    case PROP_SMOOTHING:
      g_value_set_int (value, jpegenc->smoothing);
      break;
#endif
    case PROP_IDCT_METHOD:
      g_value_set_enum (value, jpegenc->idct_method);
      break;
    case PROP_OUT_GRAY:
      g_value_set_boolean (value, jpegenc->out_gray);
      break;
    case PROP_PERF_MEASUREMENT:
      g_value_set_boolean (value, jpegenc->bMeasure_ImageProcessTime);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (jpegenc);
}

static gboolean
gst_jpegenc_start (GstVideoEncoder * benc)
{
  GstJpegEnc *enc = (GstJpegEnc *) benc;

  enc->line[0] = NULL;
  enc->line[1] = NULL;
  enc->line[2] = NULL;
  enc->sof_marker = -1;

  return TRUE;
}

static gboolean
gst_jpegenc_stop (GstVideoEncoder * benc)
{
  GstJpegEnc *enc = (GstJpegEnc *) benc;
  gint i, j;

  g_free (enc->line[0]);
  g_free (enc->line[1]);
  g_free (enc->line[2]);
  enc->line[0] = NULL;
  enc->line[1] = NULL;
  enc->line[2] = NULL;
  for (i = 0; i < 3; i++) {
    for (j = 0; j < 4 * DCTSIZE; j++) {
      g_free (enc->row[i][j]);
      enc->row[i][j] = NULL;
    }
  }

  return TRUE;
}
