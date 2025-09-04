/*
 * Copyright (c) 2014-2023, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef _NV_GST_CAPTURE_H_
#define _NV_GST_CAPTURE_H_

#include <gst/gst.h>
#include <gst/video/videooverlay.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <math.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "gst/pbutils/pbutils.h"
#include "gst/pbutils/encoding-profile.h"
#include "gst/pbutils/encoding-target.h"
#include "nvgst_x11_common.h"

#ifdef WITH_GUI

#include "nvgstcapture_gui_interface.h"

#else

int dummy_func (void);
int dummy_func ()
{
  return 0;
}
#define GUI 0
#define CALL_GUI_FUNC(func, ...) dummy_func()
#define GET_GUI_CTX() NULL

#endif

#define FUNCTION_START() \
    time_t startTime = 0; \
    struct timeval start_tv = {0, 0}; \
    time_t endTime = 0; \
    struct timeval end_tv = {0, 0}; \
    if (gettimeofday(&start_tv, NULL) == 0) \
        startTime = start_tv.tv_sec * 1000000 + start_tv.tv_usec; \
    else \
        startTime = 0;

#define FUNCTION_END() \
    if (gettimeofday(&end_tv, NULL) == 0) \
        endTime = end_tv.tv_sec * 1000000 + end_tv.tv_usec; \
    else \
        endTime = 0; \
    if (app->enableKpiProfile) \
        g_print("\nKPI total time for %s in mS: %ld\n", \
            __func__, ((endTime - startTime) / 1000));

#define GET_TIMESTAMP(PLACE) \
    if (gettimeofday(&app->timeStamp, NULL) == 0) \
        app->timeStampStore[PLACE] = (app->timeStamp.tv_sec * 1000000) + app->timeStamp.tv_usec; \
    else \
        app->timeStampStore[PLACE] = 0;

/* CAPTURE GENERIC */
#define NVGST_DEFAULT_CAPTURE_MODE                CAPTURE_IMAGE
#define NVGST_DEFAULT_FILENAME                    "nvcamtest"
#define NVGST_DEFAULT_FILE_TYPE                   FILE_MP4
#define NVGST_DEFAULT_LOCATION                    "/dev/null"
#define NVGST_DEFAULT_CAPTURE_FORMAT              "I420"
#define NVGST_DEFAULT_CAPTURE_FPS                 30
#define NVGST_DEFAULT_VIDCAP_DEVICE               "/dev/video0"
#define NVGST_DEFAULT_V4L2_NVENC_DEVICE_PATH      "/dev/nvhost-msenc"
#define DEFAULT_LOCATION                          "/dev/null"
#define SUCCESS                                   0

/* PREVIEW */
#define NVGST_DEFAULT_PREVIEW_WIDTH               640
#define NVGST_DEFAULT_PREVIEW_HEIGHT              480
#define NVGST_DEFAULT_RENDER_TARGET               RENDER_OVERLAY

/* IMAGE & VIDEO CAPTURE */
#define NVGST_DEFAULT_VIDEO_MIMETYPE              "video/x-raw"
#define NVGST_DEFAULT_CAPTURE_WIDTH               640
#define NVGST_DEFAULT_CAPTURE_HEIGHT              480
#define NVGST_DEFAULT_480P_ENCODER_BITRATE        4000000
#define NVGST_DEFAULT_720P_ENCODER_BITRATE        8000000
#define NVGST_DEFAULT_1080P_ENCODER_BITRATE       14000000
#define NVGST_DEFAULT_2160P_ENCODER_BITRATE       20000000
#define NVGST_DEFAULT_VIDEO_ENCODER_PROFILE       PROFILE_HIGH
#define NVGST_DEFAULT_VIDEO_ENCODER_CONTROLRATE   CONTROLRATE_VARIABLE
#define NVGST_DEFAULT_VIDEO_ENCODER_TWOPASSCBR    FALSE

#define NVGST_DEFAULT_IMAGE_ENCODER               FORMAT_JPEG_HW
#define NVGST_DEFAULT_VIDEO_ENCODER               FORMAT_H264_HW
#define NVGST_DEFAULT_FLIP_METHOD                 0

/* CAPTURE ELEMENTS */
#define NVGST_VIDEO_CAPTURE_SRC_TEST              "videotestsrc"
#define NVGST_VIDEO_CAPTURE_SRC_V4L2              "v4l2src"
#define NVGST_VIDEO_CAPTURE_SRC_CSI_ARGUS         "nvarguscamerasrc"
#define NVGST_EGLSTREAM_CAPTURE_SRC               "nveglstreamsrc"
#define NVGST_VIDEO_SINK                          "nvvideosink"
#define NVGST_DEFAULT_VIDEO_CONVERTER             "videoconvert"
#define NVGST_DEFAULT_VIDEO_CONVERTER_CSI         "nvvidconv"
#define NVGST_DEFAULT_VIDEO_SCALER                "videoscale"
#ifdef WITH_GUI
#define NVGST_DEFAULT_PREVIEW_SINK_CSI            "nveglglessink"
#else
#define NVGST_DEFAULT_PREVIEW_SINK_CSI            "nveglglessink"
#endif
#define NVGST_DEFAULT_PREVIEW_SINK_USB            "xvimagesink"
#define NVGST_DEFAULT_CAPTURE_FILTER              "capsfilter"
#define NVGST_DEFAULT_IMAGE_ENC                   "nvjpegenc"
#define NVGST_DEFAULT_IMAGE_ENC_CONVERTER         "nvvidconv"
#define NVGST_SW_IMAGE_ENC                        "jpegenc"
#define NVGST_DEFAULT_IENC_SINK                   "fakesink"
#define NVGST_DEFAULT_VENC_SINK                   "filesink"
#define NVGST_DEFAULT_VENC_PARSE                  "h264parse"
#define NVGST_PRIMARY_V4L2_H264_VENC              "nvv4l2h264enc"
#define NVGST_PRIMARY_V4L2_VP8_VENC               "nvv4l2vp8enc"
#define NVGST_PRIMARY_V4L2_VP9_VENC               "nvv4l2vp9enc"
#define NVGST_PRIMARY_V4L2_H265_VENC              "nvv4l2h265enc"
#define NVGST_SW_H264_ENC                         "x264enc"
#define NVGST_PRIMARY_H264_PARSER                 "h264parse"
#define NVGST_PRIMARY_H265_PARSER                 "h265parse"
#define NVGST_PRIMARY_MP4_MUXER                   "qtmux"
#define NVGST_PRIMARY_3GP_MUXER                   "3gppmux"
#define NVGST_PRIMARY_MKV_MUXER                   "matroskamux"
#define NVGST_PRIMARY_STREAM_SELECTOR             "tee"
#define NVGST_PRIMARY_QUEUE                       "queue"
#define NVGST_PRIMARY_IDENTITY                    "identity"

#ifdef WITH_STREAMING
#define NVGST_STREAMING_SRC_FILE                  "uridecodebin"
#endif

/* CSI CAMERA DEFAULT PROPERTIES TUNING */

#define NVGST_DEFAULT_WHITEBALANCE                1
#define NVGST_DEFAULT_SATURATION                  1
#define NVGST_DEFAULT_EXPOSURE_COMPENSATION       0
#define NVGST_DEFAULT_TNR_STRENGTH                -1
#define NVGST_DEFAULT_EE_STRENGTH                 -1
#define NVGST_DEFAULT_AEANTIBANDING               0
#define NVGST_DEFAULT_AE_LOCK                     0
#define NVGST_DEFAULT_AWB_LOCK                    0
#define NVGST_DEFAULT_TNR_MODE                    1
#define NVGST_DEFAULT_EE_MODE                     1
#define NVGST_DEFAULT_SENSOR_ID                   0
#define NVGST_DEFAULT_SENSOR_MODE                 -1
#define NVGST_DEFAULT_DISPLAY_ID                  0

#define MIN_EXPOSURE_COMPENSATION                 -2
#define MAX_EXPOSURE_COMPENSATION                 2
#define MIN_TNR_MODE                              0
#define MAX_TNR_MODE                              2
#define MIN_EE_MODE                               0
#define MAX_EE_MODE                               2
#define MIN_STRENGTH                              -1
#define MAX_STRENGTH                              1
#define MIN_AE_ANTIBANDING_MODE                   0
#define MAX_AE_ANTIBANDING_MODE                   3

/* CSI CAMERA DEFAULT AUTOMATION */

#define NVGST_DEFAULT_AUTOMATION_MODE             FALSE
#define NVGST_DEFAULT_CAP_START_DELAY             5
#define NVGST_DEFAULT_QUIT_TIME                   0
#define NVGST_DEFAULT_ITERATION_COUNT             1
#define NVGST_DEFAULT_CAPTURE_GAP                 250
#define NVGST_DEFAULT_CAPTURE_TIME                10
#define NVGST_DEFAULT_NUM_SENSORS                 2
#define NVGST_DEFAULT_TOGGLE_CAMERA_MODE          FALSE
#define NVGST_DEFAULT_TOGGLE_CAMERA_SENSOR        FALSE
#define NVGST_DEFAULT_TOGGLE_CAMERA_SENSOR_MODES  FALSE
#define NVGST_DEFAULT_ENUMERATE_WHITEBALANCE      FALSE
#define NVGST_DEFAULT_ENUMERATE_SATURATION        FALSE
#define NVGST_DEFAULT_ENUMERATE_CAPTURE_AUTO      FALSE


#define MIN_V4L2_RES                              PR_176x144
#define MAX_V4L2_RES                              PR_1920x1080
#define MIN_CSI_RES                               PR_640x480
#define MAX_CSI_RES                               PR_5632x4224

/* DEBUG LOG LEVEL */
#ifdef NVGST_LOG_LEVEL_DEBUG
#define NVGST_ENTER_FUNCTION()            g_print("%s{", __FUNCTION__)
#define NVGST_EXIT_FUNCTION()             g_print("%s}", __FUNCTION__)
#define NVGST_EXIT_FUNCTION_VIA(s)        g_print("%s}['%s']", __FUNCTION__, s)
#define NVGST_DEBUG_MESSAGE(s)            g_debug("<%s:%d> "s, __FUNCTION__, __LINE__)
#define NVGST_DEBUG_MESSAGE_V(s, ...)     g_debug("<%s:%d> "s, __FUNCTION__, __LINE__, __VA_ARGS__)
#define NVGST_INFO_MESSAGE(s)             g_message("<%s:%d> "s, __FUNCTION__, __LINE__)
#define NVGST_INFO_MESSAGE_V(s, ...)      g_message("<%s:%d> "s, __FUNCTION__, __LINE__, __VA_ARGS__)
#define NVGST_WARNING_MESSAGE(s)          g_warning("<%s:%d> "s, __FUNCTION__, __LINE__)
#define NVGST_WARNING_MESSAGE_V(s, ...)   g_warning("<%s:%d> "s, __FUNCTION__, __LINE__, __VA_ARGS__)
#define NVGST_CRITICAL_MESSAGE(s)         do {\
                                          g_critical("<%s:%d> "s, __FUNCTION__, __LINE__);\
                                          app->return_value = -1;\
                                          } while (0)
#define NVGST_CRITICAL_MESSAGE_V(s, ...)  do {\
                                          g_critical("<%s:%d> "s, __FUNCTION__, __LINE__,__VA_ARGS__);\
                                          app->return_value = -1;\
                                          } while (0)
#define NVGST_ERROR_MESSAGE(s)            g_error("<%s:%d> "s, __FUNCTION__, __LINE__)
#define NVGST_ERROR_MESSAGE_V(s, ...)     g_error("<%s:%d> "s, __FUNCTION__, __LINE__,__VA_ARGS__)

#elif defined NVGST_LOG_LEVEL_INFO
#define NVGST_ENTER_FUNCTION()            G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_EXIT_FUNCTION()             G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_EXIT_FUNCTION_VIA(s)        G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_DEBUG_MESSAGE(s)            G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_DEBUG_MESSAGE_V(s, ...)     G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_INFO_MESSAGE(s)             g_message("<%s:%d> "s, __FUNCTION__, __LINE__)
#define NVGST_INFO_MESSAGE_V(s, ...)      g_message("<%s:%d> "s, __FUNCTION__, __LINE__, __VA_ARGS__)
#define NVGST_WARNING_MESSAGE(s)          g_warning("<%s:%d> "s, __FUNCTION__, __LINE__)
#define NVGST_WARNING_MESSAGE_V(s, ...)   g_warning("<%s:%d> "s, __FUNCTION__, __LINE__, __VA_ARGS__)
#define NVGST_CRITICAL_MESSAGE(s)         do {\
                                          g_critical("<%s:%d> "s, __FUNCTION__, __LINE__);\
                                          app->return_value = -1;\
                                          } while (0)
#define NVGST_CRITICAL_MESSAGE_V(s, ...)  do {\
                                          g_critical("<%s:%d> "s, __FUNCTION__, __LINE__, __VA_ARGS__);\
                                          app->return_value = -1;\
                                          } while (0)
#define NVGST_ERROR_MESSAGE(s)            g_error("<%s:%d> "s, __FUNCTION__, __LINE__)
#define NVGST_ERROR_MESSAGE_V(s, ...)     g_error("<%s:%d> "s, __FUNCTION__, __LINE__, __VA_ARGS__)

#elif defined NVGST_LOG_LEVEL_WARNING
#define NVGST_ENTER_FUNCTION()            G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_EXIT_FUNCTION()             G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_EXIT_FUNCTION_VIA(s)        G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_DEBUG_MESSAGE(s)            G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_DEBUG_MESSAGE_V(s, ...)     G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_INFO_MESSAGE(s)             G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_INFO_MESSAGE_V(s, ...)      G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_WARNING_MESSAGE(s)          g_warning("<%s:%d> "s, __FUNCTION__, __LINE__)
#define NVGST_WARNING_MESSAGE_V(s, ...)   g_warning("<%s:%d> "s, __FUNCTION__, __LINE__, __VA_ARGS__)
#define NVGST_CRITICAL_MESSAGE(s)         do {\
                                          g_critical("<%s:%d> "s, __FUNCTION__, __LINE__);\
                                          app->return_value = -1;\
                                          } while (0)
#define NVGST_CRITICAL_MESSAGE_V(s, ...)  do {\
                                          g_critical("<%s:%d> "s, __FUNCTION__, __LINE__, __VA_ARGS__);\
                                          app->return_value = -1;\
                                          } while (0)
#define NVGST_ERROR_MESSAGE(s)            g_error("<%s:%d> "s, __FUNCTION__, __LINE__)
#define NVGST_ERROR_MESSAGE_V(s, ...)     g_error("<%s:%d> "s, __FUNCTION__, __LINE__, __VA_ARGS__)

#elif defined NVGST_LOG_LEVEL_CRITICAL
#define NVGST_ENTER_FUNCTION()            G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_EXIT_FUNCTION()             G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_EXIT_FUNCTION_VIA(s)        G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_DEBUG_MESSAGE(s)            G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_DEBUG_MESSAGE_V(s, ...)     G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_INFO_MESSAGE(s)             G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_INFO_MESSAGE_V(s, ...)      G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_WARNING_MESSAGE(s)          G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_WARNING_MESSAGE_V(s, ...)   G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_CRITICAL_MESSAGE(s)         do {\
                                          g_critical("<%s:%d> "s, __FUNCTION__, __LINE__);\
                                          app->return_value = -1;\
                                          } while (0)
#define NVGST_CRITICAL_MESSAGE_V(s, ...)  do {\
                                          g_critical("<%s:%d> "s, __FUNCTION__, __LINE__, __VA_ARGS__);\
                                          app->return_value = -1;\
                                          } while (0)
#define NVGST_ERROR_MESSAGE(s)            g_error("<%s:%d> "s, __FUNCTION__, __LINE__)
#define NVGST_ERROR_MESSAGE_V(s, ...)     g_error("<%s:%d> "s, __FUNCTION__, __LINE__, __VA_ARGS__)

#else
#define NVGST_ENTER_FUNCTION()            G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_EXIT_FUNCTION()             G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_EXIT_FUNCTION_VIA(s)        G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_DEBUG_MESSAGE(s)            G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_DEBUG_MESSAGE_V(s, ...)     G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_INFO_MESSAGE(s)             G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_INFO_MESSAGE_V(s, ...)      G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_WARNING_MESSAGE(s)          G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_WARNING_MESSAGE_V(s, ...)   G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_CRITICAL_MESSAGE(s)         G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_CRITICAL_MESSAGE_V(s, ...)  G_STMT_START{ (void)0; }G_STMT_END
#define NVGST_ERROR_MESSAGE(s)            g_error("<%s:%d> "s, __FUNCTION__, __LINE__)
#define NVGST_ERROR_MESSAGE_V(s, ...)     g_error("<%s:%d> "s, __FUNCTION__, __LINE__, __VA_ARGS__)
#endif
#define INVALID_SELECTION_ARGUS                    "Not a valid option for ARGUS Plugin\n"

/* CAMERA KPI PARAMS */
typedef enum
{
  FIRST_FRAME = 0,
  APP_LAUNCH,
  CURRENT_EVENT,
  KPI_EVENT_SIZE,
} KpiEvents;

/* CAMERA AUTOMATION PARAMS */
typedef struct
{
  gint capture_start_time;
  gint quit_time;
  gint iteration_count;
  gint capture_gap;
  gint capture_time;
  gint num_sensors;
  gboolean automate;
  gboolean toggle_mode;
  gboolean toggle_sensor;
  gboolean toggle_sensor_modes;
  gboolean enum_wb;
  gboolean enum_st;
  gboolean capture_auto;
} Automate;

/* PREVIEW RESOLUTION */
typedef enum
{
  PR_176x144 = 0,
  PR_320x240,
  PR_640x480,
  PR_1280x720,
  PR_1920x1080,
  PR_2104x1560,
  PR_2592x1944,
  PR_2616x1472,
  PR_3840x2160,
  PR_3896x2192,
  PR_4208x3120,
  PR_5632x3168,
  PR_5632x4224,
} Prev_Res;

/* IMAGE CAPTURE RESOLUTION */
typedef enum
{
  IR_176x144 = 0,
  IR_320x240,
  IR_640x480,
  IR_1280x720,
  IR_1920x1080,
  IR_2104x1560,
  IR_2592x1944,
  IR_2616x1472,
  IR_3840x2160,
  IR_3896x2192,
  IR_4208x3120,
  IR_5632x3168,
  IR_5632x4224,
} Icap_Res;

/* VIDEO CAPTURE RESOLUTION */
typedef enum
{
  VR_176x144 = 0,
  VR_320x240,
  VR_640x480,
  VR_1280x720,
  VR_1920x1080,
  VR_2104x1560,
  VR_2592x1944,
  VR_2616x1472,
  VR_3840x2160,
  VR_3896x2192,
  VR_4208x3120,
  VR_5632x3168,
  VR_5632x4224,
} Vcap_Res;

#define RESOLUTION_STRINGS {"176 x 144", "320 x 240", "640 x 480", \
    "1280 x 720", "1920 x 1080", "2104 x 1560", "2592 x 1944", \
    "2616 x 1472", "3840 x 2160", "3896 x 2192", "4208 x 3120", \
    "5632 x 3168", "5632 x 4224", NULL};

/* CAPTURE CONTAINER TYPE */
typedef enum
{
  FILE_MP4 = 0,
  FILE_3GP,
  FILE_MKV,
  FILE_H265
} FileType;

#define FILE_TYPE_STRINGS {"MP4", "3GP", "MKV", "H.265", NULL};

/* IMAGE ENCODE TYPE */
typedef enum
{
  FORMAT_JPEG_SW = 0,
  FORMAT_JPEG_HW
} ImageEncFormatType;

#define IMAGE_ENCODER_STRINGS {"SW JPEG", "HW JPEG", NULL};

/* VIDEO ENCODE TYPE */
typedef enum
{
  FORMAT_H264_HW = 0,
  FORMAT_VP8_HW,
  FORMAT_H265_HW,
  FORMAT_VP9_HW,
  FORMAT_H264_SW
} VideoEncFormatType;

#define VIDEO_ENC_STRINGS {"H.264 (HW)", "VP8 (HW)", "H.265 (HW)", "VP9 (HW)", "H264 (SW)", NULL};

/* H264 ENCODE PROFILE TYPE */
typedef enum
{
  PROFILE_BASELINE = 0,
  PROFILE_MAIN,
  PROFILE_HIGH
} H264EncProfileType;

/* ENCODER BITRATE CONTROL METHOD */
typedef enum
{
  CONTROLRATE_DISABLE,
  CONTROLRATE_VARIABLE,
  CONTROLRATE_CONSTANT
} EncControlRateType;

/* CAPTURE MODE */
typedef enum
{
  CAPTURE_NONE = 0,
  CAPTURE_IMAGE,
  CAPTURE_VIDEO
} CaptureType;

/* CAPTURE COLOR FORMAT */
typedef enum
{
  CAPTURE_I420,
  CAPTURE_NV12,
  CAPTURE_YUY2,
  CAPTURE_UYVY,
} CaptureColorFormat;

/* CAPTURE PAD TYPE */
typedef enum
{
  CAPTURE_PAD_PREV = 0,
  CAPTURE_PAD_IMAGE,
  CAPTURE_PAD_VIDEO
} CapturePadType;

typedef enum
{
  NV_CAM_SRC_V4L2,
  NV_CAM_SRC_CSI,
  NV_CAM_SRC_TEST,
  NV_CAM_SRC_EGLSTREAM
} NvCamSrcType;

typedef enum
{
  HW_V4L2_ENC = 1
} HardwareEncoderType;

/* CAMERA CAPTURE RESOLUTIONS */
typedef struct
{
  gint preview_width;
  gint preview_height;
  gint cus_prev_width;
  gint cus_prev_height;
  gint prev_res_index;
  gint image_cap_width;
  gint image_cap_height;
  gint img_res_index;
  gint video_cap_width;
  gint video_cap_height;
  gint vid_res_index;
  gint current_max_res;
} CamRes;

/* CAMERA ENCODER PARAMS */
typedef struct
{
  gint image_enc;
  VideoEncFormatType video_enc;
  HardwareEncoderType hw_enc_type;
  guint bitrate;
  gboolean enabletwopassCBR;
  EncControlRateType controlrate;
  H264EncProfileType video_enc_profile;
} EncSet;

/* CAPTURE PIPELINE ELEMENTS */
typedef struct
{
  GstElement *camera;
  GstElement *vsrc;
  GstElement *vsink;
  GstElement *colorspace_conv;
  GstElement *cap_filter;
  GstElement *cap_tee;
  GstElement *prev_q;
  GstElement *ienc_q;
  GstElement *venc_q;
  GstElement *vid_enc;
  GstElement *img_enc;
  GstElement *img_enc_conv;
  GstElement *parser;
  GstElement *muxer;
  GstElement *img_sink;
  GstElement *video_sink;
  GstElement *capbin;
  GstElement *vid_bin;
  GstElement *img_bin;
  GstElement *svsbin;
  GstElement *vid_enc_conv;
  GstElement *vid_enc_cap_filter;

  /* Elements for EGLStreamProducer */
  GstElement *eglproducer_pipeline;
  GstElement *eglproducer_bin;
  GstElement *eglproducer_videosink;
  GstElement *eglproducer_nvvideosink;
  GstElement *eglproducer_videotestsrc;
  GstElement *eglproducer_capsfilter;
  GstElement *eglproducer_videoconvert;

  /* Scaling elements for preview, image and video */
  GstElement *svc_prebin;
  GstElement *svc_prevconv;
  GstElement *svc_prevconv_out_filter;
  GstElement *svc_imgbin;
  GstElement *svc_imgvconv;
  GstElement *svc_imgvconv_out_filter;
  GstElement *svc_vidbin;
  GstElement *svc_vidvconv;
  GstElement *svc_vidvconv_out_filter;

  /* Elements for video snapshot */
  GstElement *vsnap_q;
  GstElement *vsnap_bin;
  GstElement *vsnap_enc;
  GstElement *vsnap_sink;
  GstElement *svc_snapconv;
  GstElement *svc_snapconv_out_filter;
} CamPipe;

#ifdef WITH_STREAMING
typedef struct
{
  GObject *media_factory;
  GstElement *appsrc;
  GstElement *streaming_file_src_conv;
  gchar *streaming_src_file;
} RTSPStreamingCtx;
#endif

/* EGLStream Producer ID */
typedef enum
{
  EGLSTREAM_PRODUCER_ID_SCF_CAMERA = 0,
  EGLSTREAM_PRODUCER_ID_MAX,
} EGLStream_Producer_ID;

/* CAMERA CONTEX PARAMS */
typedef struct
{
  gint mode;
  gint file_type;
  gint capture_count;
  gint return_value;
  gint capcount;
  gint color_format;
  gint color_format_csi;
  gint color_format_v4l2;
  gboolean muxer_is_identity;

  /*CSI camera features */
  gint whitebalance;
  gint ae_antibanding;
  gint tnr_mode;
  gint ee_mode;
  gint timeout;
  gfloat saturation;
  gfloat exposure_compensation;
  gfloat tnr_strength;
  gfloat ee_strength;
  guint sensor_id;
  guint sensor_mode;
  guint framerate;
  guint flip_method;
  guint display_id;
  guint overlay_index;
  guint overlay_x_pos;
  guint overlay_y_pos;
  guint overlay_width;
  guint overlay_height;

  GstPadProbeReturn native_record;

  gchar *svs;
  gchar *file_name;
  gchar *csi_options_argus;
  gchar *csi_resolution;
  gchar *usb_options;
  gchar *encoder_options;
  gchar *vidcap_device;
  gchar *cap_dev_node;
  gchar *overlayConfig;
  gchar *eglConfig;
  gchar *exposure_timerange;
  gchar *gain_range;
  gchar *isp_digital_gainrange;

  NvCamSrcType cam_src;
  gboolean cap_success;
  gboolean use_cus_res;
  gboolean use_eglstream;

  gboolean first_frame;
  time_t timeStampStore[KPI_EVENT_SIZE];
  struct timeval timeStamp;
  gboolean enableKpiProfile;
  gboolean enableKpiNumbers;
  gboolean enableMeta;
  gboolean enableAeLock;
  gboolean enableAwbLock;
  gulong prev_probe_id;
  gulong enc_probe_id;
  time_t currentFrameTime;
  time_t prevFrameTime;
  gulong frameCount;
  time_t accumulator;
  time_t currentEncFrameTime;
  time_t prevEncFrameTime;
  gulong encFrameCount;
  time_t encAccumulator;

  GMutex *lock;
  GCond *cond;
  GCond *x_cond;
  GThread *reset_thread;
  GThread *x_event_thread;

  CamRes capres;
  EncSet encset;
  CamPipe ele;
  displayCtx disp;

  /* EGLStream */
  EGLStream_Producer_ID eglstream_producer_id;
  EGLDisplay display;
  EGLStreamKHR stream;

  /* EGLStream Producer */
  guint fifosize;
  gboolean enable_fifo;

  /* AUTOMATION */
  Automate aut;

#ifdef WITH_STREAMING
  gint streaming_mode;
  RTSPStreamingCtx video_streaming_ctx;
#endif
} CamCtx;

#endif

