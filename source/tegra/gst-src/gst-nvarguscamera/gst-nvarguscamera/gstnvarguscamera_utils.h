/*
 * Copyright (c) 2017-2020, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef GSTNVARGUSCAMERA_UTILS_H_
#define GSTNVARGUSCAMERA_UTILS_H_

#include <gst/gst.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
   NvArgusCamAwbMode_Off = 0,
   NvArgusCamAwbMode_Auto,
   NvArgusCamAwbMode_Incandescent,
   NvArgusCamAwbMode_Fluorescent,
   NvArgusCamAwbMode_WarmFluorescent,
   NvArgusCamAwbMode_Daylight,
   NvArgusCamAwbMode_CloudyDaylight,
   NvArgusCamAwbMode_Twilight,
   NvArgusCamAwbMode_Shade,
   NvArgusCamAwbMode_Manual,

} NvArgusCamAwbMode;

typedef enum
{
   NvArgusCamNoiseReductionMode_Off = 0,
   NvArgusCamNoiseReductionMode_Fast,
   NvArgusCamNoiseReductionMode_HighQuality

} NvArgusCamNoiseReductionMode;

typedef enum
{
   NvArgusCamEdgeEnhancementMode_Off = 0,
   NvArgusCamEdgeEnhancementMode_Fast,
   NvArgusCamEdgeEnhancementMode_HighQuality

} NvArgusCamEdgeEnhancementMode;

typedef enum
{
   NvArgusCamAeAntibandingMode_Off = 0,
   NvArgusCamAeAntibandingMode_Auto,
   NvArgusCamAeAntibandingMode_50HZ,
   NvArgusCamAeAntibandingMode_60HZ

} NvArgusCamAeAntibandingMode;

GType gst_nvarguscam_white_balance_mode_get_type (void);
#define GST_TYPE_NVARGUSCAM_WB_MODE (gst_nvarguscam_white_balance_mode_get_type())

GType gst_nvarguscam_tnr_mode_get_type (void);
#define GST_TYPE_NVARGUSCAM_TNR_MODE (gst_nvarguscam_tnr_mode_get_type())

GType gst_nvarguscam_edge_enhancement_mode_get_type (void);
#define GST_TYPE_NVARGUSCAM_EDGE_ENHANCEMENT_MODE (gst_nvarguscam_edge_enhancement_mode_get_type())

GType gst_nvarguscam_aeantibanding_mode_get_type (void);
#define GST_TYPE_NVARGUSCAM_AEANTIBANDING_MODE (gst_nvarguscam_aeantibanding_mode_get_type())

#ifdef __cplusplus
}
#endif

#endif /* GSTNVARGUSCAMERA_UTILS_H_ */
