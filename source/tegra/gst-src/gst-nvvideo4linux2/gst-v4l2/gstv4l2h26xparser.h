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

#ifndef __H26X_PARSER_H__
#define __H26X_PARSER_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define H264_MAX_SPS_COUNT   32

typedef enum
{
  H264_NAL_UNKNOWN      = 0,
  H264_NAL_SLICE        = 1,
  H264_NAL_SLICE_DPA    = 2,
  H264_NAL_SLICE_DPB    = 3,
  H264_NAL_SLICE_DPC    = 4,
  H264_NAL_SLICE_IDR    = 5,
  H264_NAL_SEI          = 6,
  H264_NAL_SPS          = 7,
  H264_NAL_PPS          = 8,
  H264_NAL_AU_DELIMITER = 9,
  H264_NAL_SEQ_END      = 10,
  H264_NAL_STREAM_END   = 11,
  H264_NAL_FILLER_DATA  = 12,
  H264_NAL_SPS_EXT      = 13,
  H264_NAL_PREFIX_UNIT  = 14,
  H264_NAL_SUBSET_SPS   = 15,
  H264_NAL_DEPTH_SPS    = 16,
  H264_NAL_SLICE_AUX    = 19,
  H264_NAL_SLICE_EXT    = 20,
  H264_NAL_SLICE_DEPTH  = 21
} H264NalUnitType;

typedef enum
{
  H264_NAL_EXTENSION_NONE = 0,
  H264_NAL_EXTENSION_SVC,
  H264_NAL_EXTENSION_MVC,
} H264NalUnitExtensionType;

typedef enum
{
  H264_PARSER_OK,
  H264_PARSER_BROKEN_DATA,
  H264_PARSER_BROKEN_LINK,
  H264_PARSER_ERROR,
  H264_PARSER_NO_NAL,
  H264_PARSER_NO_NAL_END
} H264ParserResult;

typedef enum
{
  H264_FRAME_PACKING_NONE                           = 6,
  H264_FRAME_PACKING_CHECKERBOARD_INTERLEAVING      = 0,
  H264_FRAME_PACKING_COLUMN_INTERLEAVING            = 1,
  H264_FRAME_PACKING_ROW_INTERLEAVING               = 2,
  H264_FRAME_PACKING_SIDE_BY_SIDE                   = 3,
  H264_FRMAE_PACKING_TOP_BOTTOM                     = 4,
  H264_FRAME_PACKING_TEMPORAL_INTERLEAVING          = 5
} H264FramePackingType;


typedef enum
{
  H264_P_SLICE    = 0,
  H264_B_SLICE    = 1,
  H264_I_SLICE    = 2,
  H264_SP_SLICE   = 3,
  H264_SI_SLICE   = 4,
  H264_S_P_SLICE  = 5,
  H264_S_B_SLICE  = 6,
  H264_S_I_SLICE  = 7,
  H264_S_SP_SLICE = 8,
  H264_S_SI_SLICE = 9
} H264SliceType;

typedef enum
{
  H264_CT_TYPE_PROGRESSIVE = 0,
  H264_CT_TYPE_INTERLACED = 1,
  H264_CT_TYPE_UNKNOWN = 2,
} CtType;

typedef struct _H264NalParser              H264NalParser;

typedef struct _H264NalUnit                H264NalUnit;

typedef struct _H264SPS                    H264SPS;

struct _H264NalUnit
{
  guint16 ref_idc;
  guint16 type;

  /* calculated values */
  guint8 idr_pic_flag;
  guint size;
  guint offset;
  guint sc_offset;
  gboolean valid;

  guint8 *data;

  guint8 header_bytes;
  guint8 extension_type;
};

struct _H264SPS
{
  gint id;

  guint8 profile_idc;
  guint8 constraint_set0_flag;
  guint8 constraint_set1_flag;
  guint8 constraint_set2_flag;
  guint8 constraint_set3_flag;
  guint8 constraint_set4_flag;
  guint8 constraint_set5_flag;
  guint8 level_idc;

  guint8 chroma_format_idc;
  guint8 separate_colour_plane_flag;
  guint8 bit_depth_luma_minus8;
  guint8 bit_depth_chroma_minus8;
  guint8 qpprime_y_zero_transform_bypass_flag;

  guint8 scaling_matrix_present_flag;
  guint8 scaling_lists_4x4[6][16];
  guint8 scaling_lists_8x8[6][64];

  guint8 log2_max_frame_num_minus4;
  guint8 pic_order_cnt_type;

  /* if pic_order_cnt_type == 0 */
  guint8 log2_max_pic_order_cnt_lsb_minus4;

  /* else if pic_order_cnt_type == 1 */
  guint8 delta_pic_order_always_zero_flag;
  gint32 offset_for_non_ref_pic;
  gint32 offset_for_top_to_bottom_field;
  guint8 num_ref_frames_in_pic_order_cnt_cycle;
  gint32 offset_for_ref_frame[255];

  guint32 num_ref_frames;
  guint8 gaps_in_frame_num_value_allowed_flag;
  guint32 pic_width_in_mbs_minus1;
  guint32 pic_height_in_map_units_minus1;
  guint8 frame_mbs_only_flag;

  guint8 mb_adaptive_frame_field_flag;

  guint8 direct_8x8_inference_flag;

  guint8 frame_cropping_flag;

  /* if frame_cropping_flag */
  guint32 frame_crop_left_offset;
  guint32 frame_crop_right_offset;
  guint32 frame_crop_top_offset;
  guint32 frame_crop_bottom_offset;

  guint8 vui_parameters_present_flag;

  /* calculated values */
  guint8 chroma_array_type;
  guint32 max_frame_num;
  gint width, height;
  gint crop_rect_width, crop_rect_height;
  gint crop_rect_x, crop_rect_y;
  gint fps_num_removed, fps_den_removed; /* FIXME: remove */
  gboolean valid;

  /* Subset SPS extensions */
  guint8 extension_type;
};

struct _H264NalParser
{
  /*< private >*/
  H264SPS sps[H264_MAX_SPS_COUNT];
  H264SPS *last_sps;
};


H264NalParser *h264_nal_parser_new             (void);


H264ParserResult h264_parser_identify_nalu     (H264NalParser *nalparser,
                                                       const guint8 *data, guint offset,
                                                       gsize size, H264NalUnit *nalu);


H264ParserResult h264_parser_identify_nalu_unchecked (H264NalParser *nalparser,
                                                       const guint8 *data, guint offset,
                                                       gsize size, H264NalUnit *nalu);


H264ParserResult h264_parser_parse_sps         (H264NalParser *nalparser, H264NalUnit *nalu,
                                                       H264SPS *sps, gboolean parse_vui_params);


void h264_nal_parser_free                         (H264NalParser *nalparser);


H264ParserResult h264_parse_sps                (H264NalUnit *nalu,
                                                       H264SPS *sps, gboolean parse_vui_params);


void                h264_sps_clear                (H264SPS *sps);


#define H265_MAX_SUB_LAYERS   8
#define H265_MAX_SPS_COUNT   16


typedef enum
{
  H265_NAL_SLICE_TRAIL_N    = 0,
  H265_NAL_SLICE_TRAIL_R    = 1,
  H265_NAL_SLICE_TSA_N      = 2,
  H265_NAL_SLICE_TSA_R      = 3,
  H265_NAL_SLICE_STSA_N     = 4,
  H265_NAL_SLICE_STSA_R     = 5,
  H265_NAL_SLICE_RADL_N     = 6,
  H265_NAL_SLICE_RADL_R     = 7,
  H265_NAL_SLICE_RASL_N     = 8,
  H265_NAL_SLICE_RASL_R     = 9,
  H265_NAL_SLICE_BLA_W_LP   = 16,
  H265_NAL_SLICE_BLA_W_RADL = 17,
  H265_NAL_SLICE_BLA_N_LP   = 18,
  H265_NAL_SLICE_IDR_W_RADL = 19,
  H265_NAL_SLICE_IDR_N_LP   = 20,
  H265_NAL_SLICE_CRA_NUT    = 21,
  H265_NAL_VPS              = 32,
  H265_NAL_SPS              = 33,
  H265_NAL_PPS              = 34,
  H265_NAL_AUD              = 35,
  H265_NAL_EOS              = 36,
  H265_NAL_EOB              = 37,
  H265_NAL_FD               = 38,
  H265_NAL_PREFIX_SEI       = 39,
  H265_NAL_SUFFIX_SEI       = 40
} H265NalUnitType;

typedef enum
{
  H265_PARSER_OK,
  H265_PARSER_BROKEN_DATA,
  H265_PARSER_BROKEN_LINK,
  H265_PARSER_ERROR,
  H265_PARSER_NO_NAL,
  H265_PARSER_NO_NAL_END
} H265ParserResult;


typedef struct _H265Parser                   H265Parser;

typedef struct _H265NalUnit                  H265NalUnit;

typedef struct _H265SPS                      H265SPS;
typedef struct _H265ProfileTierLevel         H265ProfileTierLevel;

struct _H265NalUnit
{
  guint8 type;
  guint8 layer_id;
  guint8 temporal_id_plus1;

  /* calculated values */
  guint size;
  guint offset;
  guint sc_offset;
  gboolean valid;

  guint8 *data;
  guint8 header_bytes;
};

struct _H265ProfileTierLevel {
  guint8 profile_space;
  guint8 tier_flag;
  guint8 profile_idc;

  guint8 profile_compatibility_flag[32];

  guint8 progressive_source_flag;
  guint8 interlaced_source_flag;
  guint8 non_packed_constraint_flag;
  guint8 frame_only_constraint_flag;

  guint8 max_12bit_constraint_flag;
  guint8 max_10bit_constraint_flag;
  guint8 max_8bit_constraint_flag;
  guint8 max_422chroma_constraint_flag;
  guint8 max_420chroma_constraint_flag;
  guint8 max_monochrome_constraint_flag;
  guint8 intra_constraint_flag;
  guint8 one_picture_only_constraint_flag;
  guint8 lower_bit_rate_constraint_flag;
  guint8 max_14bit_constraint_flag;

  guint8 level_idc;

  guint8 sub_layer_profile_present_flag[6];
  guint8 sub_layer_level_present_flag[6];

  guint8 sub_layer_profile_space[6];
  guint8 sub_layer_tier_flag[6];
  guint8 sub_layer_profile_idc[6];
  guint8 sub_layer_profile_compatibility_flag[6][32];
  guint8 sub_layer_progressive_source_flag[6];
  guint8 sub_layer_interlaced_source_flag[6];
  guint8 sub_layer_non_packed_constraint_flag[6];
  guint8 sub_layer_frame_only_constraint_flag[6];
  guint8 sub_layer_level_idc[6];
};

struct _H265SPS
{
  guint8 id;

  guint8 max_sub_layers_minus1;
  guint8 temporal_id_nesting_flag;

  H265ProfileTierLevel profile_tier_level;

  guint8 chroma_format_idc;
  guint8 separate_colour_plane_flag;
  guint16 pic_width_in_luma_samples;
  guint16 pic_height_in_luma_samples;

  guint8 conformance_window_flag;
  /* if conformance_window_flag */
  guint32 conf_win_left_offset;
  guint32 conf_win_right_offset;
  guint32 conf_win_top_offset;
  guint32 conf_win_bottom_offset;

  guint8 bit_depth_luma_minus8;
  guint8 bit_depth_chroma_minus8;
  guint8 log2_max_pic_order_cnt_lsb_minus4;

  guint8 sub_layer_ordering_info_present_flag;
  guint8 max_dec_pic_buffering_minus1[H265_MAX_SUB_LAYERS];
  guint8 max_num_reorder_pics[H265_MAX_SUB_LAYERS];
  guint8 max_latency_increase_plus1[H265_MAX_SUB_LAYERS];

  guint8 log2_min_luma_coding_block_size_minus3;
  guint8 log2_diff_max_min_luma_coding_block_size;
  guint8 log2_min_transform_block_size_minus2;
  guint8 log2_diff_max_min_transform_block_size;
  guint8 max_transform_hierarchy_depth_inter;
  guint8 max_transform_hierarchy_depth_intra;

  guint8 scaling_list_enabled_flag;
  /* if scaling_list_enabled_flag */
  guint8 scaling_list_data_present_flag;

  guint8 amp_enabled_flag;
  guint8 sample_adaptive_offset_enabled_flag;
  guint8 pcm_enabled_flag;
  /* if pcm_enabled_flag */
  guint8 pcm_sample_bit_depth_luma_minus1;
  guint8 pcm_sample_bit_depth_chroma_minus1;
  guint8 log2_min_pcm_luma_coding_block_size_minus3;
  guint8 log2_diff_max_min_pcm_luma_coding_block_size;
  guint8 pcm_loop_filter_disabled_flag;

  guint8 num_short_term_ref_pic_sets;

  guint8 long_term_ref_pics_present_flag;
  /* if long_term_ref_pics_present_flag */
  guint8 num_long_term_ref_pics_sps;
  guint16 lt_ref_pic_poc_lsb_sps[32];
  guint8 used_by_curr_pic_lt_sps_flag[32];

  guint8 temporal_mvp_enabled_flag;
  guint8 strong_intra_smoothing_enabled_flag;
  guint8 vui_parameters_present_flag;

  /* if vui_parameters_present_flat */
  guint8 sps_extension_flag;

  /* calculated values */
  guint8 chroma_array_type;
  gint width, height;
  gint crop_rect_width, crop_rect_height;
  gint crop_rect_x, crop_rect_y;
  gint fps_num, fps_den;
  gboolean valid;
};

struct _H265Parser
{
  /*< private >*/
  H265SPS sps[H265_MAX_SPS_COUNT];
  H265SPS *last_sps;
};

H265Parser *     h265_parser_new               (void);


H265ParserResult h265_parser_identify_nalu      (H265Parser  * parser,
                                                        const guint8   * data,
                                                        guint            offset,
                                                        gsize            size,
                                                        H265NalUnit * nalu);


H265ParserResult h265_parser_identify_nalu_unchecked (H265Parser * parser,
                                                        const guint8   * data,
                                                        guint            offset,
                                                        gsize            size,
                                                        H265NalUnit * nalu);


H265ParserResult h265_parser_identify_nalu_hevc (H265Parser  * parser,
                                                        const guint8   * data,
                                                        guint            offset,
                                                        gsize            size,
                                                        guint8           nal_length_size,
                                                        H265NalUnit * nalu);



H265ParserResult h265_parser_parse_sps       (H265Parser   * parser,
                                                     H265NalUnit  * nalu,
                                                     H265SPS      * sps,
                                                     gboolean          parse_vui_params);

void                h265_parser_free            (H265Parser  * parser);



H265ParserResult h265_parse_sps              (H265Parser  * parser,
                                                     H265NalUnit * nalu,
                                                     H265SPS     * sps,
                                                     gboolean         parse_vui_params);


G_END_DECLS
#endif
