/*
 * Copyright (c) 2013-2019, NVIDIA CORPORATION. All rights reserved.
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

#ifndef __NVGSTPLAYER_H__
#define __NVGSTPLAYER_H__

#include <gst/gst.h>
#include <gst/video/videooverlay.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdlib.h>
#include <math.h>
#include <glib/gstdio.h>
#include <math.h>
#include <pthread.h>
#include <ctype.h>
#include <semaphore.h>
#include <errno.h>
#include "nvgst_x11_common.h"
#include "nvgst_asound_common.h"

#ifdef WITH_GUI

#include "nvgstplayer_gui_interface.h"

#else

gpointer dummy_func (void);
gpointer
dummy_func ()
{
  return NULL;
}

#define GUI 0
#define CALL_GUI_FUNC(func, ...) dummy_func()
#define GET_GUI_CTX() NULL
#define GUI_CALLBACKS "nvgstplayer.h"
#endif

#define APPLICATION_NAME                  "nvgstplayer"
#define NVSTARTPER                        "startper"
#define NVSTART                           "start"
#define NVDURATION                        "duration"
#define NVCXPR                            "cxpr"
#define NVREPEATS                         "repeats"
#define NVAUDIO                           "audio"
#define NVVIDEO                           "video"
#define NVNATIVE_AUDIO                    "native_audio"
#define NVNATIVE_VIDEO                    "native_video"
#define NVNOP                             "nop"
#define NVSYNC                            "sync"
#define NVUSE_BUFFERING                   "use_buffering"
#define NVLOW_PERCENT                     "low_percent"
#define NVHIGH_PERCENT                    "high_percent"
#define NVMAX_SIZE_TIME                   "max_size_time"
#define NVMAX_SIZE_BYTES                  "max_size_bytes"
#define NVMAX_SIZE_BUFFERS                "max_size_buffers"
#define NVIMAGE_DISPLAY_TIME              "image_display_time"
#define NVTAGS                            "tags"

#define NVGST_AUDIO_DEC                   "sad"
#define NVGST_VIDEO_DEC                   "svd"
#define NVGST_AUDIO_CONV                  "sac"
#define NVGST_VIDEO_CONV                  "svc"
#define NVGST_AUDIO_SINK                  "sas"
#define NVGST_VIDEO_SINK                  "svs"
#define NVGST_FILE_SRC                    "sfsrc"
#define NVGST_RTSP_SRC                    "srtsp"
#define NVGST_HTTP_SRC                    "shttp"
#define NVGST_UDP_SRC                     "sudp"

#define NVGST_DEFAULT_AUDIO_CONV          "audioconvert ! audioresample"
#define NVGST_DEFAULT_VIDEO_CONV          "nvvidconv"
#define NVGST_DEFAULT_AUDIO_SINK          "alsasink"
#define NVGST_DEFAULT_VIDEO_SINK          "nv3dsink"
#define NVGST_DEFAULT_VIDEO_DEC           "nvv4l2decoder"
#define NVGST_DEFAULT_FILE_SRC            "filesrc"
#define NVGST_DEFAULT_RTSP_SRC            "rtspsrc"
#define NVGST_DEFAULT_HTTP_SRC            "souphttpsrc"
#define NVGST_DEFAULT_UDP_SRC             "udpsrc"

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

#define INITIAL_FPS_UPDATE_INTERVAL_MS    400
#ifdef WITH_GUI
#define DEFAULT_FPS_UPDATE_INTERVAL_MS    400
#else
#define DEFAULT_FPS_UPDATE_INTERVAL_MS    5000
#endif

#define CALC_RUNNING_AVERAGE(avg,val,size)  (((val) + ((size)-1) * (avg)) / (size))


typedef enum
{
  NVGST_RET_ASYNC = 1,
  NVGST_RET_SUCCESS = 0,
  NVGST_RET_ERR = -1,
  NVGST_RET_END = -2,
  NVGST_RET_INVALID = -3
} NvGstReturn;


typedef enum
{
  NVGST_AUTOPLUG_SELECT_TRY = 0,
  NVGST_AUTOPLUG_SELECT_EXPOSE,
  NVGST_AUTOPLUG_SELECT_SKIP
} NvGstAutoplugSelectResult;


typedef enum
{
  NVGST_PLAY_FLAG_VIDEO = (1 << 0),
  NVGST_PLAY_FLAG_AUDIO = (1 << 1),
  NVGST_PLAY_FLAG_TEXT = (1 << 2),
  NVGST_PLAY_FLAG_VIS = (1 << 3),
  NVGST_PLAY_FLAG_SOFT_VOLUME = (1 << 4),
  NVGST_PLAY_FLAG_NATIVE_AUDIO = (1 << 5),
  NVGST_PLAY_FLAG_NATIVE_VIDEO = (1 << 6),
  NVGST_PLAY_FLAG_DOWNLOAD = (1 << 7),
  NVGST_PLAY_FLAG_BUFFERING = (1 << 8),
  NVGST_PLAY_FLAG_DEINTERLACE = (1 << 9),
  /* added */
  NVGST_PLAY_FLAG_PLAYBIN = (1 << 10)
} NvGstPlayFlags;


typedef enum
{
  NVGST_OPS_NONE,
  NVGST_OPS_PAUSE,
  NVGST_OPS_PLAY,
  NVGST_OPS_SEEK,
  NVGST_OPS_WAIT,
  NVGST_OPS_STOP
} NvGstOperation;


typedef enum
{
  NVGST_CMD_NONE,
  NVGST_CMD_SCRIPT,
  NVGST_CMD_USER
} NvGstOpMode;


typedef enum
{
  BUFFER_MODE_NONE = 0,
  BUFFER_MODE_SLAVE = 1,
  BUFFER_MODE_BUFFER = 2,
  BUFFER_MODE_AUTO = 3
} JitterBufferMode;


typedef struct
{
  gint repeats;
  gboolean startPer;
  GstClockTimeDiff segment_start;
  GstClockTimeDiff segment_duration;
  NvGstPlayFlags flags;
  gboolean sync;
  gboolean use_buffering;
  gboolean disable_fullscreen;
  gint low_percent;
  gint high_percent;
  gint aud_track;
  gint vid_track;
  gint drop_threshold_pct;
  gboolean loop_forever;
  gint max_size_buffers;
  gint max_size_bytes;
  GstClockTime max_size_time;
  gint image_display_time;
  gboolean show_tags;
  GList *lplist_head;
  GList *cmlist_head;
  GList *lplist;
  GList *cmlist;
} attrs_s;


typedef struct
{
  gchar *uri;
  NvGstOpMode operation_mode;
  GstClockTimeDiff duration;
  gchar **audio_dec;
  gchar **video_dec;
  attrs_s attrs;
  gboolean selfexpr;
  gboolean pending_play;
  guint64 interval;
  gboolean postpone;
  gboolean dbin_audio_decoders;
  gboolean dbin_video_decoders;
} inAttrs;


typedef struct
{
  FILE *file;

  guint frames_rendered, frames_dropped, frames_dropped_decoder;
  guint64 last_frames_rendered, last_frames_dropped;

  GstClockTime start_ts;
  GstClockTime last_ts;

  gdouble max_fps;
  gdouble min_fps;
  gdouble average_fps;

  GTimer *timer;
  guint dps_cb;
  GstClockTime prev_ts;
  GstClockTime avg_in_diff;
  GstClockTime max_latency;
  gboolean initial_fps;
} pfData_s;


typedef struct
{
  inAttrs *input;
  gchar *extra_options;
  GKeyFile *pathCfg;
  GKeyFile *elemCfg;
  GHashTable *htable;
  NvGstOperation cur_operation;
  attrs_s attrs;
  GstElement *pipeline;
  GstElement *source;
  GstElement *vpipe;
  GstElement *apipe;
  GstElement *vsink;
  GstElement *asink;
  GstElement *adbin;
  GstElement *vdbin;
  GstElement *seekElement;
  gboolean no_more_pads;
  gint cmd_id;
  gint bus_id;
  gint uriCount;
  gsize uriTotal;
  gchar **uriGroups;
  gboolean version;
  gboolean have_alsa_sinks;
  gboolean found_demuxer;
  gboolean got_eos;
  gboolean is_live;
  gboolean in_error;
  gboolean pre_dbin_lp;
  gboolean unpause;
  gboolean buffering;
  gboolean running;
  guint image_eos;
  gint return_value;
  GstState target_state;
  GstClockTimeDiff last_seek_time;
  GstClockTimeDiff accum_time;
  GstClockTime seekPos;
  gchar *uri;
  gchar **elem_gps;
  gboolean stealth_mode;
  gboolean bg_mode;
  gchar *svd;
  gchar *sad;
  gchar *svc;
  gchar *sac;
  gchar *svs;
  gchar *sas;
  gchar *shttp;
  gchar *srtsp;
  gchar *sudp;
  gchar *sfsrc;
  gint astreams;
  gint vstreams;
  gint astream_select;
  gint vstream_select;
  gboolean disable_dpms;
  displayCtx disp;
  GThread *x_event_thread;
  GMutex window_lock;

  /* stats */
  gboolean stats;
  gchar *stats_file;
  pfData_s pfData;
  GstPad *vrender_pad;
  GstPad *arender_pad;
  gchar *elem_file;
  GMutex dbin_lock;

} appCtx;


typedef struct
{
  guint x;
  guint n;
  GList *c;
} sLp;


typedef struct
{
  gchar *id;
  GList *list;
} sCm;
#endif
