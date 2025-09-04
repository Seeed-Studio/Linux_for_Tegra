/*
 * Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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
#  include "config.h"
#endif

#include "nalutils.h"
#include "gstv4l2h26xparser.h"

#include <gst/base/gstbytereader.h>
#include <gst/base/gstbitreader.h>
#include <string.h>
#include <math.h>

GST_DEBUG_CATEGORY_STATIC (h26x_parser_debug);
#define GST_CAT_DEFAULT h26x_parser_debug

static gboolean initialized = FALSE;
#define INITIALIZE_DEBUG_CATEGORY \
  if (!initialized) { \
    GST_DEBUG_CATEGORY_INIT (h26x_parser_debug, "codecparsers_h26x", 0, \
        "h26x parser library"); \
    initialized = TRUE; \
  }

/**** Default scaling_lists according to Table 7-2 *****/
static const guint8 default_4x4_intra[16] = {
  6, 13, 13, 20, 20, 20, 28, 28, 28, 28, 32, 32,
  32, 37, 37, 42
};

static const guint8 default_4x4_inter[16] = {
  10, 14, 14, 20, 20, 20, 24, 24, 24, 24, 27, 27,
  27, 30, 30, 34
};

static const guint8 default_8x8_intra[64] = {
  6, 10, 10, 13, 11, 13, 16, 16, 16, 16, 18, 18,
  18, 18, 18, 23, 23, 23, 23, 23, 23, 25, 25, 25, 25, 25, 25, 25, 27, 27, 27,
  27, 27, 27, 27, 27, 29, 29, 29, 29, 29, 29, 29, 31, 31, 31, 31, 31, 31, 33,
  33, 33, 33, 33, 36, 36, 36, 36, 38, 38, 38, 40, 40, 42
};

static const guint8 default_8x8_inter[64] = {
  9, 13, 13, 15, 13, 15, 17, 17, 17, 17, 19, 19,
  19, 19, 19, 21, 21, 21, 21, 21, 21, 22, 22, 22, 22, 22, 22, 22, 24, 24, 24,
  24, 24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 27, 27, 27, 27, 27, 27, 28,
  28, 28, 28, 28, 30, 30, 30, 30, 32, 32, 32, 33, 33, 35
};

/*****  Utils ****/
#define EXTENDED_SAR 255


static gboolean
h264_parse_nalu_header (H264NalUnit * nalu)
{
  guint8 *data = nalu->data + nalu->offset;

  if (nalu->size < 1)
    return FALSE;

  nalu->type = (data[0] & 0x1f);
  nalu->ref_idc = (data[0] & 0x60) >> 5;
  nalu->idr_pic_flag = (nalu->type == 5 ? 1 : 0);
  nalu->header_bytes = 1;

  nalu->extension_type = H264_NAL_EXTENSION_NONE;

  GST_DEBUG ("Nal type %u, ref_idc %u", nalu->type, nalu->ref_idc);
  return TRUE;
}

static gboolean
h264_sps_copy (H264SPS * dst_sps, const H264SPS * src_sps)
{
  g_return_val_if_fail (dst_sps != NULL, FALSE);
  g_return_val_if_fail (src_sps != NULL, FALSE);

  h264_sps_clear (dst_sps);

  *dst_sps = *src_sps;

  return TRUE;
}

static gboolean
h264_parser_parse_scaling_list (NalReader * nr,
    guint8 scaling_lists_4x4[6][16], guint8 scaling_lists_8x8[6][64],
    const guint8 fallback_4x4_inter[16], const guint8 fallback_4x4_intra[16],
    const guint8 fallback_8x8_inter[64], const guint8 fallback_8x8_intra[64],
    guint8 n_lists)
{
  guint i;

  static const guint8 *default_lists[12] = {
    default_4x4_intra, default_4x4_intra, default_4x4_intra,
    default_4x4_inter, default_4x4_inter, default_4x4_inter,
    default_8x8_intra, default_8x8_inter,
    default_8x8_intra, default_8x8_inter,
    default_8x8_intra, default_8x8_inter
  };

  GST_DEBUG ("parsing scaling lists");

  for (i = 0; i < 12; i++) {
    gboolean use_default = FALSE;

    if (i < n_lists) {
      guint8 scaling_list_present_flag;

      READ_UINT8 (nr, scaling_list_present_flag, 1);
      if (scaling_list_present_flag) {
        guint8 *scaling_list;
        guint size;
        guint j;
        guint8 last_scale, next_scale;

        if (i < 6) {
          scaling_list = scaling_lists_4x4[i];
          size = 16;
        } else {
          scaling_list = scaling_lists_8x8[i - 6];
          size = 64;
        }

        last_scale = 8;
        next_scale = 8;
        for (j = 0; j < size; j++) {
          if (next_scale != 0) {
            gint32 delta_scale;

            READ_SE (nr, delta_scale);
            next_scale = (last_scale + delta_scale) & 0xff;
          }
          if (j == 0 && next_scale == 0) {
            /* Use default scaling lists (7.4.2.1.1.1) */
            memcpy (scaling_list, default_lists[i], size);
            break;
          }
          last_scale = scaling_list[j] =
              (next_scale == 0) ? last_scale : next_scale;
        }
      } else
        use_default = TRUE;
    } else
      use_default = TRUE;

    if (use_default) {
      switch (i) {
        case 0:
          memcpy (scaling_lists_4x4[0], fallback_4x4_intra, 16);
          break;
        case 1:
          memcpy (scaling_lists_4x4[1], scaling_lists_4x4[0], 16);
          break;
        case 2:
          memcpy (scaling_lists_4x4[2], scaling_lists_4x4[1], 16);
          break;
        case 3:
          memcpy (scaling_lists_4x4[3], fallback_4x4_inter, 16);
          break;
        case 4:
          memcpy (scaling_lists_4x4[4], scaling_lists_4x4[3], 16);
          break;
        case 5:
          memcpy (scaling_lists_4x4[5], scaling_lists_4x4[4], 16);
          break;
        case 6:
          memcpy (scaling_lists_8x8[0], fallback_8x8_intra, 64);
          break;
        case 7:
          memcpy (scaling_lists_8x8[1], fallback_8x8_inter, 64);
          break;
        case 8:
          memcpy (scaling_lists_8x8[2], scaling_lists_8x8[0], 64);
          break;
        case 9:
          memcpy (scaling_lists_8x8[3], scaling_lists_8x8[1], 64);
          break;
        case 10:
          memcpy (scaling_lists_8x8[4], scaling_lists_8x8[2], 64);
          break;
        case 11:
          memcpy (scaling_lists_8x8[5], scaling_lists_8x8[3], 64);
          break;

        default:
          break;
      }
    }
  }

  return TRUE;

error:
  GST_WARNING ("error parsing scaling lists");
  return FALSE;
}

H264NalParser *
h264_nal_parser_new (void)
{
  H264NalParser *nalparser;

  nalparser = g_slice_new0 (H264NalParser);
  INITIALIZE_DEBUG_CATEGORY;

  return nalparser;
}

void
h264_nal_parser_free (H264NalParser * nalparser)
{
  guint i;

  for (i = 0; i < H264_MAX_SPS_COUNT; i++)
    h264_sps_clear (&nalparser->sps[i]);
  g_slice_free (H264NalParser, nalparser);

  nalparser = NULL;
}

H264ParserResult
h264_parser_identify_nalu_unchecked (H264NalParser * nalparser,
    const guint8 * data, guint offset, gsize size, H264NalUnit * nalu)
{
  gint off1;

  memset (nalu, 0, sizeof (*nalu));

  if (size < offset + 4) {
    GST_DEBUG ("Can't parse, buffer has too small size %" G_GSIZE_FORMAT
        ", offset %u", size, offset);
    return H264_PARSER_ERROR;
  }

  off1 = scan_for_start_codes (data + offset, size - offset);

  if (off1 < 0) {
    GST_DEBUG ("No start code prefix in this buffer");
    return H264_PARSER_NO_NAL;
  }

  if (offset + off1 == size - 1) {
    GST_DEBUG ("Missing data to identify nal unit");

    return H264_PARSER_ERROR;
  }

  nalu->sc_offset = offset + off1;


  nalu->offset = offset + off1 + 3;
  nalu->data = (guint8 *) data;
  nalu->size = size - nalu->offset;

  if (!h264_parse_nalu_header (nalu)) {
    GST_WARNING ("error parsing \"NAL unit header\"");
    nalu->size = 0;
    return H264_PARSER_BROKEN_DATA;
  }

  nalu->valid = TRUE;

  /* sc might have 2 or 3 0-bytes */
  if (nalu->sc_offset > 0 && data[nalu->sc_offset - 1] == 00
      && (nalu->type == H264_NAL_SPS || nalu->type == H264_NAL_PPS
          || nalu->type == H264_NAL_AU_DELIMITER))
    nalu->sc_offset--;

  if (nalu->type == H264_NAL_SEQ_END ||
      nalu->type == H264_NAL_STREAM_END) {
    GST_DEBUG ("end-of-seq or end-of-stream nal found");
    nalu->size = 1;
    return H264_PARSER_OK;
  }
  return H264_PARSER_OK;
}

H264ParserResult
h264_parser_identify_nalu (H264NalParser * nalparser,
    const guint8 * data, guint offset, gsize size, H264NalUnit * nalu)
{
  H264ParserResult res;
  gint off2;

  res =
      h264_parser_identify_nalu_unchecked (nalparser, data, offset, size,
      nalu);

  if (res != H264_PARSER_OK)
    goto beach;

  /* The two NALs are exactly 1 byte size and are placed at the end of an AU,
   * there is no need to wait for the following */
  if (nalu->type == H264_NAL_SEQ_END ||
      nalu->type == H264_NAL_STREAM_END)
    goto beach;

  off2 = scan_for_start_codes (data + nalu->offset, size - nalu->offset);
  if (off2 < 0) {
    GST_DEBUG ("Nal start %d, No end found", nalu->offset);

    return H264_PARSER_NO_NAL_END;
  }

  /* Mini performance improvement:
   * We could have a way to store how many 0s were skipped to avoid
   * parsing them again on the next NAL */
  while (off2 > 0 && data[nalu->offset + off2 - 1] == 00)
    off2--;

  nalu->size = off2;
  if (nalu->size < 2)
    return H264_PARSER_BROKEN_DATA;

  GST_DEBUG ("Complete nal found. Off: %d, Size: %d", nalu->offset, nalu->size);

beach:
  return res;
}

H264ParserResult
h264_parser_parse_sps (H264NalParser * nalparser, H264NalUnit * nalu,
    H264SPS * sps, gboolean parse_vui_params)
{
  H264ParserResult res = h264_parse_sps (nalu, sps, parse_vui_params);

  return res;
  if (res == H264_PARSER_OK) {
    GST_DEBUG ("adding sequence parameter set with id: %d to array", sps->id);

    if (!h264_sps_copy (&nalparser->sps[sps->id], sps))
      return H264_PARSER_ERROR;
    nalparser->last_sps = &nalparser->sps[sps->id];
  }
  return res;
}

/* Parse seq_parameter_set_data() */
static gboolean
h264_parse_sps_data (NalReader * nr, H264SPS * sps,
    gboolean parse_vui_params)
{
  gint width, height;
  guint subwc[] = { 1, 2, 2, 1 };
  guint subhc[] = { 1, 2, 1, 1 };

  memset (sps, 0, sizeof (*sps));

  /* set default values for fields that might not be present in the bitstream
     and have valid defaults */
  sps->extension_type = H264_NAL_EXTENSION_NONE;
  sps->chroma_format_idc = 1;
  memset (sps->scaling_lists_4x4, 16, 96);
  memset (sps->scaling_lists_8x8, 16, 384);

  READ_UINT8 (nr, sps->profile_idc, 8);
  READ_UINT8 (nr, sps->constraint_set0_flag, 1);
  READ_UINT8 (nr, sps->constraint_set1_flag, 1);
  READ_UINT8 (nr, sps->constraint_set2_flag, 1);
  READ_UINT8 (nr, sps->constraint_set3_flag, 1);
  READ_UINT8 (nr, sps->constraint_set4_flag, 1);
  READ_UINT8 (nr, sps->constraint_set5_flag, 1);

  /* skip reserved_zero_2bits */
  if (!_skip (nr, 2))
    goto error;

  READ_UINT8 (nr, sps->level_idc, 8);

  READ_UE_MAX (nr, sps->id, H264_MAX_SPS_COUNT - 1);

  if (sps->profile_idc == 100 || sps->profile_idc == 110 ||
      sps->profile_idc == 122 || sps->profile_idc == 244 ||
      sps->profile_idc == 44 || sps->profile_idc == 83 ||
      sps->profile_idc == 86 || sps->profile_idc == 118 ||
      sps->profile_idc == 128) {
    READ_UE_MAX (nr, sps->chroma_format_idc, 3);
    if (sps->chroma_format_idc == 3)
      READ_UINT8 (nr, sps->separate_colour_plane_flag, 1);

    READ_UE_MAX (nr, sps->bit_depth_luma_minus8, 6);
    READ_UE_MAX (nr, sps->bit_depth_chroma_minus8, 6);
    READ_UINT8 (nr, sps->qpprime_y_zero_transform_bypass_flag, 1);

    READ_UINT8 (nr, sps->scaling_matrix_present_flag, 1);
    if (sps->scaling_matrix_present_flag) {
      guint8 n_lists;

      n_lists = (sps->chroma_format_idc != 3) ? 8 : 12;
      if (!h264_parser_parse_scaling_list (nr,
              sps->scaling_lists_4x4, sps->scaling_lists_8x8,
              default_4x4_inter, default_4x4_intra,
              default_8x8_inter, default_8x8_intra, n_lists))
        goto error;
    }
  }

  READ_UE_MAX (nr, sps->log2_max_frame_num_minus4, 12);

  sps->max_frame_num = 1 << (sps->log2_max_frame_num_minus4 + 4);

  READ_UE_MAX (nr, sps->pic_order_cnt_type, 2);
  if (sps->pic_order_cnt_type == 0) {
    READ_UE_MAX (nr, sps->log2_max_pic_order_cnt_lsb_minus4, 12);
  } else if (sps->pic_order_cnt_type == 1) {
    guint i;

    READ_UINT8 (nr, sps->delta_pic_order_always_zero_flag, 1);
    READ_SE (nr, sps->offset_for_non_ref_pic);
    READ_SE (nr, sps->offset_for_top_to_bottom_field);
    READ_UE_MAX (nr, sps->num_ref_frames_in_pic_order_cnt_cycle, 255);

    for (i = 0; i < sps->num_ref_frames_in_pic_order_cnt_cycle; i++)
      READ_SE (nr, sps->offset_for_ref_frame[i]);
  }

  READ_UE (nr, sps->num_ref_frames);
  READ_UINT8 (nr, sps->gaps_in_frame_num_value_allowed_flag, 1);
  READ_UE (nr, sps->pic_width_in_mbs_minus1);
  READ_UE (nr, sps->pic_height_in_map_units_minus1);
  READ_UINT8 (nr, sps->frame_mbs_only_flag, 1);

  if (!sps->frame_mbs_only_flag)
    READ_UINT8 (nr, sps->mb_adaptive_frame_field_flag, 1);

  READ_UINT8 (nr, sps->direct_8x8_inference_flag, 1);
  READ_UINT8 (nr, sps->frame_cropping_flag, 1);
  if (sps->frame_cropping_flag) {
    READ_UE (nr, sps->frame_crop_left_offset);
    READ_UE (nr, sps->frame_crop_right_offset);
    READ_UE (nr, sps->frame_crop_top_offset);
    READ_UE (nr, sps->frame_crop_bottom_offset);
  }

  /* calculate ChromaArrayType */
  if (!sps->separate_colour_plane_flag)
    sps->chroma_array_type = sps->chroma_format_idc;

  /* Calculate  width and height */
  width = (sps->pic_width_in_mbs_minus1 + 1);
  width *= 16;
  height = (sps->pic_height_in_map_units_minus1 + 1);
  height *= 16 * (2 - sps->frame_mbs_only_flag);
  GST_LOG ("initial width=%d, height=%d", width, height);
  if (width < 0 || height < 0) {
    GST_WARNING ("invalid width/height in SPS");
    goto error;
  }

  sps->width = width;
  sps->height = height;

  if (sps->frame_cropping_flag) {
    const guint crop_unit_x = subwc[sps->chroma_format_idc];
    const guint crop_unit_y =
        subhc[sps->chroma_format_idc] * (2 - sps->frame_mbs_only_flag);

    width -= (sps->frame_crop_left_offset + sps->frame_crop_right_offset)
        * crop_unit_x;
    height -= (sps->frame_crop_top_offset + sps->frame_crop_bottom_offset)
        * crop_unit_y;

    sps->crop_rect_width = width;
    sps->crop_rect_height = height;
    sps->crop_rect_x = sps->frame_crop_left_offset * crop_unit_x;
    sps->crop_rect_y = sps->frame_crop_top_offset * crop_unit_y;

    GST_LOG ("crop_rectangle x=%u y=%u width=%u, height=%u", sps->crop_rect_x,
        sps->crop_rect_y, width, height);
  }

  sps->fps_num_removed = 0;
  sps->fps_den_removed = 1;

  return TRUE;

error:
  return FALSE;
}

H264ParserResult
h264_parse_sps (H264NalUnit * nalu, H264SPS * sps,
    gboolean parse_vui_params)
{
  NalReader nr;

  INITIALIZE_DEBUG_CATEGORY;
  GST_DEBUG ("parsing SPS");

  init_nal (&nr, nalu->data + nalu->offset + nalu->header_bytes,
      nalu->size - nalu->header_bytes);

  if (!h264_parse_sps_data (&nr, sps, parse_vui_params))
    goto error;

  sps->valid = TRUE;

  return H264_PARSER_OK;

error:
  GST_WARNING ("error parsing \"Sequence parameter set\"");
  sps->valid = FALSE;
  return H264_PARSER_ERROR;
}

void
h264_sps_clear (H264SPS * sps)
{
  g_return_if_fail (sps != NULL);
}


/**************************     H265      *****************************/


static gboolean
h265_parse_nalu_header (H265NalUnit * nalu)
{
  guint8 *data = nalu->data + nalu->offset;
  GstBitReader br;

  if (nalu->size < 2)
    return FALSE;

  gst_bit_reader_init (&br, data, nalu->size - nalu->offset);

  /* skip the forbidden_zero_bit */
  gst_bit_reader_skip_unchecked (&br, 1);

  nalu->type = gst_bit_reader_get_bits_uint8_unchecked (&br, 6);
  nalu->layer_id = gst_bit_reader_get_bits_uint8_unchecked (&br, 6);
  nalu->temporal_id_plus1 = gst_bit_reader_get_bits_uint8_unchecked (&br, 3);
  nalu->header_bytes = 2;

  return TRUE;
}

/****** Parsing functions *****/

static gboolean
h265_parse_profile_tier_level (H265ProfileTierLevel * ptl,
    NalReader * nr, guint8 maxNumSubLayersMinus1)
{
  guint i, j;
  GST_DEBUG ("parsing \"ProfileTierLevel parameters\"");

  READ_UINT8 (nr, ptl->profile_space, 2);
  READ_UINT8 (nr, ptl->tier_flag, 1);
  READ_UINT8 (nr, ptl->profile_idc, 5);

  for (j = 0; j < 32; j++)
    READ_UINT8 (nr, ptl->profile_compatibility_flag[j], 1);

  READ_UINT8 (nr, ptl->progressive_source_flag, 1);
  READ_UINT8 (nr, ptl->interlaced_source_flag, 1);
  READ_UINT8 (nr, ptl->non_packed_constraint_flag, 1);
  READ_UINT8 (nr, ptl->frame_only_constraint_flag, 1);

  READ_UINT8 (nr, ptl->max_12bit_constraint_flag, 1);
  READ_UINT8 (nr, ptl->max_10bit_constraint_flag, 1);
  READ_UINT8 (nr, ptl->max_8bit_constraint_flag, 1);
  READ_UINT8 (nr, ptl->max_422chroma_constraint_flag, 1);
  READ_UINT8 (nr, ptl->max_420chroma_constraint_flag, 1);
  READ_UINT8 (nr, ptl->max_monochrome_constraint_flag, 1);
  READ_UINT8 (nr, ptl->intra_constraint_flag, 1);
  READ_UINT8 (nr, ptl->one_picture_only_constraint_flag, 1);
  READ_UINT8 (nr, ptl->lower_bit_rate_constraint_flag, 1);
  READ_UINT8 (nr, ptl->max_14bit_constraint_flag, 1);

  /* skip the reserved zero bits */
  if (!_skip (nr, 34))
    goto error;

  READ_UINT8 (nr, ptl->level_idc, 8);
  for (j = 0; j < maxNumSubLayersMinus1; j++) {
    READ_UINT8 (nr, ptl->sub_layer_profile_present_flag[j], 1);
    READ_UINT8 (nr, ptl->sub_layer_level_present_flag[j], 1);
  }

  if (maxNumSubLayersMinus1 > 0) {
    for (i = maxNumSubLayersMinus1; i < 8; i++)
      if (!_skip (nr, 2))
        goto error;
  }

  for (i = 0; i < maxNumSubLayersMinus1; i++) {
    if (ptl->sub_layer_profile_present_flag[i]) {
      READ_UINT8 (nr, ptl->sub_layer_profile_space[i], 2);
      READ_UINT8 (nr, ptl->sub_layer_tier_flag[i], 1);
      READ_UINT8 (nr, ptl->sub_layer_profile_idc[i], 5);

      for (j = 0; j < 32; j++)
        READ_UINT8 (nr, ptl->sub_layer_profile_compatibility_flag[i][j], 1);

      READ_UINT8 (nr, ptl->sub_layer_progressive_source_flag[i], 1);
      READ_UINT8 (nr, ptl->sub_layer_interlaced_source_flag[i], 1);
      READ_UINT8 (nr, ptl->sub_layer_non_packed_constraint_flag[i], 1);
      READ_UINT8 (nr, ptl->sub_layer_frame_only_constraint_flag[i], 1);

      if (!_skip (nr, 44))
        goto error;
    }

    if (ptl->sub_layer_level_present_flag[i])
      READ_UINT8 (nr, ptl->sub_layer_level_idc[i], 8);
  }

  return TRUE;

error:
  GST_WARNING ("error parsing \"ProfileTierLevel Parameters\"");
  return FALSE;
}

H265Parser *
h265_parser_new (void)
{
  H265Parser *parser;

  parser = g_slice_new0 (H265Parser);
  INITIALIZE_DEBUG_CATEGORY;

  return parser;
}

void
h265_parser_free (H265Parser * parser)
{
  g_slice_free (H265Parser, parser);
  parser = NULL;
}

H265ParserResult
h265_parser_identify_nalu_unchecked (H265Parser * parser,
    const guint8 * data, guint offset, gsize size, H265NalUnit * nalu)
{
  gint off1;

  memset (nalu, 0, sizeof (*nalu));

  if (size < offset + 4) {
    GST_DEBUG ("Can't parse, buffer has too small size %" G_GSIZE_FORMAT
        ", offset %u", size, offset);
    return H265_PARSER_ERROR;
  }

  off1 = scan_for_start_codes (data + offset, size - offset);

  if (off1 < 0) {
    GST_DEBUG ("No start code prefix in this buffer");
    return H265_PARSER_NO_NAL;
  }

  if (offset + off1 == size - 1) {
    GST_DEBUG ("Missing data to identify nal unit");

    return H265_PARSER_ERROR;
  }

  nalu->sc_offset = offset + off1;

  /* sc might have 2 or 3 0-bytes */
  if (nalu->sc_offset > 0 && data[nalu->sc_offset - 1] == 00)
    nalu->sc_offset--;

  nalu->offset = offset + off1 + 3;
  nalu->data = (guint8 *) data;
  nalu->size = size - nalu->offset;

  if (!h265_parse_nalu_header (nalu)) {
    GST_WARNING ("error parsing \"NAL unit header\"");
    nalu->size = 0;
    return H265_PARSER_BROKEN_DATA;
  }

  nalu->valid = TRUE;

  if (nalu->type == H265_NAL_EOS || nalu->type == H265_NAL_EOB) {
    GST_DEBUG ("end-of-seq or end-of-stream nal found");
    nalu->size = 2;
    return H265_PARSER_OK;
  }

  return H265_PARSER_OK;
}

H265ParserResult
h265_parser_identify_nalu (H265Parser * parser,
    const guint8 * data, guint offset, gsize size, H265NalUnit * nalu)
{
  H265ParserResult res;
  gint off2;

  res =
      h265_parser_identify_nalu_unchecked (parser, data, offset, size,
      nalu);

  if (res != H265_PARSER_OK)
    goto beach;

  /* The two NALs are exactly 2 bytes size and are placed at the end of an AU,
   * there is no need to wait for the following */
  if (nalu->type == H265_NAL_EOS || nalu->type == H265_NAL_EOB)
    goto beach;

  off2 = scan_for_start_codes (data + nalu->offset, size - nalu->offset);
  if (off2 < 0) {
    GST_DEBUG ("Nal start %d, No end found", nalu->offset);

    return H265_PARSER_NO_NAL_END;
  }

  /* Mini performance improvement:
   * We could have a way to store how many 0s were skipped to avoid
   * parsing them again on the next NAL */
  while (off2 > 0 && data[nalu->offset + off2 - 1] == 00)
    off2--;

  nalu->size = off2;
  if (nalu->size < 3)
    return H265_PARSER_BROKEN_DATA;

  GST_DEBUG ("Complete nal found. Off: %d, Size: %d", nalu->offset, nalu->size);

beach:
  return res;
}

H265ParserResult
h265_parser_identify_nalu_hevc (H265Parser * parser,
    const guint8 * data, guint offset, gsize size, guint8 nal_length_size,
    H265NalUnit * nalu)
{
  GstBitReader br;

  memset (nalu, 0, sizeof (*nalu));

  if (size < offset + nal_length_size) {
    GST_DEBUG ("Can't parse, buffer has too small size %" G_GSIZE_FORMAT
        ", offset %u", size, offset);
    return H265_PARSER_ERROR;
  }

  size = size - offset;
  gst_bit_reader_init (&br, data + offset, size);

  nalu->size = gst_bit_reader_get_bits_uint32_unchecked (&br,
      nal_length_size * 8);
  nalu->sc_offset = offset;
  nalu->offset = offset + nal_length_size;

  if (size < nalu->size + nal_length_size) {
    nalu->size = 0;

    return H265_PARSER_NO_NAL_END;
  }

  nalu->data = (guint8 *) data;

  if (!h265_parse_nalu_header (nalu)) {
    GST_WARNING ("error parsing \"NAL unit header\"");
    nalu->size = 0;
    return H265_PARSER_BROKEN_DATA;
  }

  if (nalu->size < 2)
    return H265_PARSER_BROKEN_DATA;

  nalu->valid = TRUE;

  return H265_PARSER_OK;
}

H265ParserResult
h265_parser_parse_sps (H265Parser * parser, H265NalUnit * nalu,
    H265SPS * sps, gboolean parse_vui_params)
{
  H265ParserResult res =
      h265_parse_sps (parser, nalu, sps, parse_vui_params);
  return res;
  if (res == H265_PARSER_OK) {
    GST_DEBUG ("adding sequence parameter set with id: %d to array", sps->id);

    parser->sps[sps->id] = *sps;
    parser->last_sps = &parser->sps[sps->id];
  }

  return res;
}

H265ParserResult
h265_parse_sps (H265Parser * parser, H265NalUnit * nalu,
    H265SPS * sps, gboolean parse_vui_params)
{
  NalReader nr;
  guint8 vps_id;
  guint i;
  guint subwc[] = { 1, 2, 2, 1, 1 };
  guint subhc[] = { 1, 2, 1, 1, 1 };

  INITIALIZE_DEBUG_CATEGORY;
  GST_DEBUG ("parsing SPS");

  init_nal (&nr, nalu->data + nalu->offset + nalu->header_bytes,
      nalu->size - nalu->header_bytes);

  memset (sps, 0, sizeof (*sps));

  READ_UINT8 (&nr, vps_id, 4);

  READ_UINT8 (&nr, sps->max_sub_layers_minus1, 3);
  READ_UINT8 (&nr, sps->temporal_id_nesting_flag, 1);

  if (!h265_parse_profile_tier_level (&sps->profile_tier_level, &nr,
          sps->max_sub_layers_minus1))
    goto error;

  READ_UE_MAX (&nr, sps->id, H265_MAX_SPS_COUNT - 1);

  READ_UE_MAX (&nr, sps->chroma_format_idc, 3);
  if (sps->chroma_format_idc == 3)
    READ_UINT8 (&nr, sps->separate_colour_plane_flag, 1);

  READ_UE_ALLOWED (&nr, sps->pic_width_in_luma_samples, 1, 16888);
  READ_UE_ALLOWED (&nr, sps->pic_height_in_luma_samples, 1, 16888);

  READ_UINT8 (&nr, sps->conformance_window_flag, 1);
  if (sps->conformance_window_flag) {
    READ_UE (&nr, sps->conf_win_left_offset);
    READ_UE (&nr, sps->conf_win_right_offset);
    READ_UE (&nr, sps->conf_win_top_offset);
    READ_UE (&nr, sps->conf_win_bottom_offset);
  }

  READ_UE_MAX (&nr, sps->bit_depth_luma_minus8, 6);
  READ_UE_MAX (&nr, sps->bit_depth_chroma_minus8, 6);
  READ_UE_MAX (&nr, sps->log2_max_pic_order_cnt_lsb_minus4, 12);

  READ_UINT8 (&nr, sps->sub_layer_ordering_info_present_flag, 1);
  for (i =
      (sps->sub_layer_ordering_info_present_flag ? 0 :
          sps->max_sub_layers_minus1); i <= sps->max_sub_layers_minus1; i++) {
    READ_UE_MAX (&nr, sps->max_dec_pic_buffering_minus1[i], 16);
    READ_UE_MAX (&nr, sps->max_num_reorder_pics[i],
        sps->max_dec_pic_buffering_minus1[i]);
    READ_UE_MAX (&nr, sps->max_latency_increase_plus1[i], G_MAXUINT32 - 1);
  }
  /* setting default values if sps->sub_layer_ordering_info_present_flag is zero */
  if (!sps->sub_layer_ordering_info_present_flag && sps->max_sub_layers_minus1) {
    for (i = 0; i <= (guint)(sps->max_sub_layers_minus1 - 1); i++) {
      sps->max_dec_pic_buffering_minus1[i] =
          sps->max_dec_pic_buffering_minus1[sps->max_sub_layers_minus1];
      sps->max_num_reorder_pics[i] =
          sps->max_num_reorder_pics[sps->max_sub_layers_minus1];
      sps->max_latency_increase_plus1[i] =
          sps->max_latency_increase_plus1[sps->max_sub_layers_minus1];
    }
  }

  /* The limits are calculted based on the profile_tier_level constraint
   * in Annex-A: CtbLog2SizeY = 4 to 6 */
  READ_UE_MAX (&nr, sps->log2_min_luma_coding_block_size_minus3, 3);
  READ_UE_MAX (&nr, sps->log2_diff_max_min_luma_coding_block_size, 6);
  READ_UE_MAX (&nr, sps->log2_min_transform_block_size_minus2, 3);
  READ_UE_MAX (&nr, sps->log2_diff_max_min_transform_block_size, 3);
  READ_UE_MAX (&nr, sps->max_transform_hierarchy_depth_inter, 4);
  READ_UE_MAX (&nr, sps->max_transform_hierarchy_depth_intra, 4);

  /* Calculate  width and height */
  sps->width = sps->pic_width_in_luma_samples;
  sps->height = sps->pic_height_in_luma_samples;
  if (sps->width < 0 || sps->height < 0) {
    GST_WARNING ("invalid width/height in SPS");
    goto error;
  }

  if (sps->conformance_window_flag) {
    const guint crop_unit_x = subwc[sps->chroma_format_idc];
    const guint crop_unit_y = subhc[sps->chroma_format_idc];

    sps->crop_rect_width = sps->width -
        (sps->conf_win_left_offset + sps->conf_win_right_offset) * crop_unit_x;
    sps->crop_rect_height = sps->height -
        (sps->conf_win_top_offset + sps->conf_win_bottom_offset) * crop_unit_y;
    sps->crop_rect_x = sps->conf_win_left_offset * crop_unit_x;
    sps->crop_rect_y = sps->conf_win_top_offset * crop_unit_y;

    GST_LOG ("crop_rectangle x=%u y=%u width=%u, height=%u", sps->crop_rect_x,
        sps->crop_rect_y, sps->crop_rect_width, sps->crop_rect_height);
  }

  sps->fps_num = 0;
  sps->fps_den = 1;

  sps->valid = TRUE;

  return H265_PARSER_OK;

error:
  GST_WARNING ("error parsing \"Sequence parameter set\"");
  sps->valid = FALSE;
  return H265_PARSER_ERROR;
}
