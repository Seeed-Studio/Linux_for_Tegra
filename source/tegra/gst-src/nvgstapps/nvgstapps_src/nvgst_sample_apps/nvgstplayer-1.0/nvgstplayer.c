/*
 * Copyright (c) 2013-2021, NVIDIA CORPORATION. All rights reserved.
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

#include "nvgstplayer.h"

static gboolean parse_spec (const gchar *, const gchar *, gpointer, GError **);
static void destroy_current_track (void);
static void reset_current_track (void);
static NvGstReturn setup_track (void);
gboolean goto_next_track (gpointer);
static gboolean on2_input (gpointer);
static void nvgst_handle_xevents (void);
static gpointer nvgst_x_event_thread (gpointer);
NvGstReturn exec_ops (NvGstOperation);
static gboolean build_cmlist (gchar *, attrs_s *);
static void free_cmlist (attrs_s *, gboolean);
static NvGstReturn get_next_command (attrs_s *, gchar *, gint buf_size, gboolean);
static gchar *get_random_cxpr (void);
void get_elem_cfg (gchar * file);
static void set_sync (GstElement * vsink, gboolean sync);
void quit_app (void);
void set_window_handle (Window window);

static gboolean cintr = FALSE;
static gint timeout_id;
static GMainLoop *loop = NULL;
static guint last_n = 1;
static gint multitrack_instance = 1;

static GThread *trd = NULL;
static gboolean trd_exit = FALSE;

appCtx sapp, *app;
gchar *urifile = NULL, *elemfile = NULL, *cxpr = NULL;
gdouble segment_start = 0, segment_duration = 0, max_size_time = 0;
gint iteration_count = 1 ;

GOptionEntry entries[] = {
  {"version", 0, 0, G_OPTION_ARG_NONE, &sapp.version,
      "Prints the version of Gstreamer used", NULL},
  {"urifile", 'u', 0, G_OPTION_ARG_STRING, &urifile,
      "Path of the file containing the URIs", NULL},
  {"uri", 'i', 0, G_OPTION_ARG_STRING, &sapp.uri, "input URI", NULL},
  {"elemfile", 'e', 0, G_OPTION_ARG_STRING, &elemfile,
      "Element(s) (Properties) file", NULL},
  {"cxpr", 'x', 0, G_OPTION_ARG_STRING, &cxpr, "Command sequence expression",
      NULL},
  {"loop", 'n', 0, G_OPTION_ARG_INT, &sapp.attrs.repeats,
      "Number of times to play the media", NULL},
  {"audio-track", 'c', 0, G_OPTION_ARG_INT, &sapp.attrs.aud_track,
        "If stream have multiple audio tracks, play stream with given track no",
      NULL},
  {"video-track", 'v', 0, G_OPTION_ARG_INT, &sapp.attrs.vid_track,
        "If stream have multiple video tracks, play stream with given track no",
      NULL},
  {"start", 'a', 0, G_OPTION_ARG_DOUBLE, &segment_start,
      "Start of the segment in media in seconds", NULL},
  {"duration", 'd', 0, G_OPTION_ARG_DOUBLE, &segment_duration,
      "Play duration of the segment in media in seconds", NULL},
  {"no-sync", 0, 0, G_OPTION_ARG_NONE, &sapp.attrs.sync, "Disable AV Sync",
      NULL},
  {"disable-dpms", 0, 0, G_OPTION_ARG_NONE, &sapp.disable_dpms,
        "Unconditionally Disable DPMS/ScreenBlanking during operation and re-enable upon exit",
      NULL},
  {"stealth", 0, 0, G_OPTION_ARG_NONE, &sapp.stealth_mode,
      "Operate in stealth mode, alive even when no media is playing", NULL},
  {"bg", 0, 0, G_OPTION_ARG_NONE, &sapp.bg_mode,
        "Operate in background mode, keyboard input will be entirely ignored",
      NULL},
  {"use-playbin", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, parse_spec,
      "Use Playbin", NULL},
  {"no-audio", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, parse_spec,
      "Disable audio", NULL},
  {"no-video", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, parse_spec,
      "Disable video", NULL},
  {"disable-anative", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
      parse_spec, "Disable native audio rendering", NULL},
  {"disable-vnative", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
      parse_spec, "Disable native video rendering", NULL},
  {"use-buffering", 0, 0, G_OPTION_ARG_NONE, &sapp.attrs.use_buffering,
      "Use Buffering", NULL},
  {"low-percent", 'l', 0, G_OPTION_ARG_INT, &sapp.attrs.low_percent,
      "Low threshold for buffering to start, in %", NULL},
  {"high-percent", 'j', 0, G_OPTION_ARG_INT, &sapp.attrs.high_percent,
      "High threshold for buffering to finish, in %", NULL},
  {"loop-forever", 0, 0, G_OPTION_ARG_NONE, &sapp.attrs.loop_forever,
      "Play the URI(s) in loop forever", NULL},
  {"max-size-time", 't', 0, G_OPTION_ARG_DOUBLE, &max_size_time,
      "Max. amount of time in the queue (0=automatic)", NULL},
  {"max-size-bytes", 'y', 0, G_OPTION_ARG_INT, &sapp.attrs.max_size_bytes,
      "Max. amount of bytes in the queue (0=automatic)", NULL},
  {"max-size-buffers", 'b', 0, G_OPTION_ARG_INT, &sapp.attrs.max_size_buffers,
      "Max. amount of buffers in the queue (0=automatic)", NULL},
  {"window-x", 0, 0, G_OPTION_ARG_INT, &sapp.disp.x,
      "X coordinate for player window (for non overlay rendering)", NULL},
  {"window-y", 0, 0, G_OPTION_ARG_INT, &sapp.disp.y,
      "Y coordinate for player window (for non overlay rendering)", NULL},
  {"window-width", 0, 0, G_OPTION_ARG_INT, &sapp.disp.width,
      "Window width (for non overlay rendering)", NULL},
  {"window-height", 0, 0, G_OPTION_ARG_INT, &sapp.disp.height,
      "Window height (for non overlay rendering)", NULL},
  {"disable-fullscreen", 0, 0, G_OPTION_ARG_NONE,
        &sapp.attrs.disable_fullscreen,
      "Play video in non fullscreen mode (for nveglglessink)", NULL},
  {"drop-threshold-pct", 'h', 0, G_OPTION_ARG_INT,
        &sapp.attrs.drop_threshold_pct,
        "Permittable frames drop percentage, to be used with --stats (only for development purpose)",
      NULL},
  {"image-display-time", 'k', 0, G_OPTION_ARG_INT64,
      &sapp.attrs.image_display_time, "Image display time in seconds", NULL},
  {"show-tags", 0, 0, G_OPTION_ARG_NONE, &sapp.attrs.show_tags,
      "shows tags (metadata), if available", NULL},
#ifndef WITH_GUI
  {"stats", 0, 0, G_OPTION_ARG_NONE, &sapp.stats,
      "shows stream statistics, if enabled", NULL},
#endif
  {"stats-file", 0, 0, G_OPTION_ARG_STRING, &sapp.stats_file,
      "File to dump stream statistics, if enabled", NULL},
  {"svd", 0, 0, G_OPTION_ARG_CALLBACK, parse_spec,
      "(=) chain for video decoding", NULL},
  {"sad", 0, 0, G_OPTION_ARG_CALLBACK, parse_spec,
      "(=) chain for audio decoding", NULL},
  {"svc", 0, 0, G_OPTION_ARG_CALLBACK, parse_spec,
      "(=) chain for video postprocessing", NULL},
  {"sac", 0, 0, G_OPTION_ARG_CALLBACK, parse_spec,
      "(=) chain for audio postprocessing", NULL},
#ifndef WITH_GUI
  {"svs", 0, 0, G_OPTION_ARG_CALLBACK, parse_spec,
      "(=) chain for video rendering", NULL},
#endif
  {"sas", 0, 0, G_OPTION_ARG_CALLBACK, parse_spec,
      "(=) chain for audio rendering", NULL},
  {"shttp", 0, 0, G_OPTION_ARG_CALLBACK, parse_spec,
      "(=) chain for http source", NULL},
  {"srtsp", 0, 0, G_OPTION_ARG_CALLBACK, parse_spec,
      "(=) chain for rtsp source", NULL},
  {"sudp", 0, 0, G_OPTION_ARG_CALLBACK, parse_spec,
      "(=) chain for udp source", NULL},
  {"sfsrc", 0, 0, G_OPTION_ARG_CALLBACK, parse_spec,
      "(=) chain for file source", NULL},
  {NULL},
};


static void
fps_init (pfData_s * self)
{
  self->max_fps = -1;
  self->min_fps = -1;
  /* Init counters */
  self->frames_rendered = 0;
  self->frames_dropped = 0;
  self->frames_dropped_decoder = 0;
  self->last_frames_rendered = G_GUINT64_CONSTANT (0);
  self->last_frames_dropped = G_GUINT64_CONSTANT (0);

  /* init time stamps */
  self->start_ts = GST_CLOCK_TIME_NONE;
  self->last_ts = GST_CLOCK_TIME_NONE;

  self->initial_fps = TRUE;
  self->prev_ts = -1;
  self->avg_in_diff = -1;
}


static void
stats_func(gdouble average_fps, guint64 frames_rendered, guint64 frames_dropped)
{

  if (average_fps) {
      printf("\n\nITERATION %d :",iteration_count);
    iteration_count++ ;

    guint64 total_frames;
    gdouble percent_dropped;

    total_frames = frames_rendered + frames_dropped;
    percent_dropped = (gdouble) frames_dropped / (gdouble) total_frames *100;

    g_printf ("\tTotal Frames = %" G_GUINT64_FORMAT ", Frames rendered = %"
        G_GUINT64_FORMAT ", Frames dropped = %" G_GUINT64_FORMAT
        ", Average fps = %.2f", total_frames, frames_rendered, frames_dropped,
        average_fps);
    if (percent_dropped < app->attrs.drop_threshold_pct)
      g_printf
          ("\n\t\tPercentage frames dropped = %.2f%% which is below acceptable limit of %d%%\n\n",
          percent_dropped, app->attrs.drop_threshold_pct);
    else {
      g_printf
          ("\n\t\tPercentage frames dropped = %.2f%% which is above acceptable limit of %d%%\n\n",
          percent_dropped, app->attrs.drop_threshold_pct);
      app->return_value = -1;
    }
  }

}


static gboolean
display_current_fps (gpointer data)
{
  pfData_s *self = (pfData_s *) data;
  gdouble cur_fps, drop_rate, average_fps;
  gchar msg_str[256];
  gdouble diffs, elapsed_time;
  guint64 rendered_frames, dropped_frames;
  GstClockTime current_ts;

  rendered_frames = g_atomic_int_get (&self->frames_rendered);
  dropped_frames = g_atomic_int_get (&self->frames_dropped);

  /* if no QOS event yet */
  if ((rendered_frames + dropped_frames) == 0) {
    return TRUE;
  }

  current_ts = gst_util_get_timestamp ();

  diffs = (gdouble) (current_ts - self->last_ts) / GST_SECOND;
  elapsed_time = (gdouble) (current_ts - self->start_ts) / GST_SECOND;

  cur_fps = (gdouble) (rendered_frames - self->last_frames_rendered) / diffs;
  drop_rate = (gdouble) (dropped_frames - self->last_frames_dropped) / diffs;

  average_fps = (gdouble) rendered_frames / g_timer_elapsed (self->timer, NULL);
  self->average_fps = average_fps;

  if (self->max_fps == -1 || cur_fps > self->max_fps) {
    self->max_fps = cur_fps;
  }
  if (self->min_fps == -1 || cur_fps < self->min_fps) {
    self->min_fps = cur_fps;
  }

  if (drop_rate == 0.0) {
    g_snprintf (msg_str, 255,
        "appox.rend: %" G_GUINT64_FORMAT ", approx.drpd: %" G_GUINT64_FORMAT
        ", curfps: %.2f, avgfps: %.2f, avgtsdiff: %" GST_TIME_FORMAT
        ", rtime: %" GST_TIME_FORMAT,
        rendered_frames, dropped_frames, cur_fps, average_fps,
        GST_TIME_ARGS (self->avg_in_diff), GST_TIME_ARGS (current_ts));
  } else {
    g_snprintf (msg_str, 255,
        "approx.rend: %" G_GUINT64_FORMAT ", approx.drpd: %" G_GUINT64_FORMAT
        ", curfps: %.2f, avgfps: %.2f, avgtsdiff: %" GST_TIME_FORMAT
        ", drate: %.2f" ", rtime: %" GST_TIME_FORMAT, rendered_frames,
        dropped_frames, cur_fps, average_fps, GST_TIME_ARGS (self->avg_in_diff),
        drop_rate, GST_TIME_ARGS (current_ts));
  }

  if (app->pfData.file)
    g_fprintf (self->file, "%s\n", msg_str);

  self->last_frames_rendered = rendered_frames;
  self->last_frames_dropped = dropped_frames;
  self->last_ts = current_ts;

  if (G_UNLIKELY (self->initial_fps && elapsed_time > 5.0)) {
    self->dps_cb = g_timeout_add (DEFAULT_FPS_UPDATE_INTERVAL_MS,
        display_current_fps, self);
    self->initial_fps = FALSE;
    return FALSE;
  }

  return TRUE;
}


static gboolean
on_video_sink_flow (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  pfData_s *self = (pfData_s *) user_data;
  GstEvent *ev = GST_PAD_PROBE_INFO_DATA (info);

  if (GST_EVENT_TYPE (ev) == GST_EVENT_QOS) {
    GstClockTimeDiff jitter;
    GstClockTime ts;

    gst_event_parse_qos (ev, 0, NULL, &jitter, &ts);

    if (G_LIKELY (self->prev_ts != GST_CLOCK_TIME_NONE)) {
      if (G_LIKELY (self->prev_ts < ts)) {
        if (G_UNLIKELY (ts - self->prev_ts > GST_SECOND)) {
          self->prev_ts = -1;
          self->avg_in_diff = -1;
          g_atomic_int_inc (&self->frames_rendered);
        } else {
          if (self->avg_in_diff != GST_CLOCK_TIME_NONE)
            self->avg_in_diff = CALC_RUNNING_AVERAGE (self->avg_in_diff, ts - self->prev_ts, 8);        //windowsize=8
          else
            self->avg_in_diff = ts - self->prev_ts;

          //g_print ("%"G_GINT64_FORMAT" ", jitter);
          //g_print ("%"G_GINT64_FORMAT" ", self->max_latency);
          //g_print ("%"G_GINT64_FORMAT"\n", self->avg_in_diff);

          if (GST_CLOCK_TIME_IS_VALID (self->max_latency) &&
              jitter >
              (GstClockTimeDiff) (self->max_latency + self->avg_in_diff)) {
            g_atomic_int_inc (&self->frames_dropped);
          } else {
            g_atomic_int_inc (&self->frames_rendered);
          }
        }
      } else {
        self->prev_ts = -1;
        self->avg_in_diff = -1;
        g_atomic_int_inc (&self->frames_rendered);
      }

    } else {
      g_atomic_int_inc (&self->frames_rendered);
    }

    self->prev_ts = ts;
  }

  return TRUE;
}


static gboolean
is_valid_number (gchar * str, gboolean neg, gboolean dec)
{
  gboolean ret = FALSE;

  if (str) {
    if (g_ascii_isdigit (*str) ||
        (neg && (*str == '-')) || (dec && (*str == '.'))) {
      gboolean was_digit = FALSE;

      if (*str == '.')
        dec = FALSE;
      else
        was_digit = g_ascii_isdigit (*str);

      str++;
      if (*str != '\0') {
        while (g_ascii_isdigit (*str) || G_UNLIKELY (dec && *str == '.')) {
          if (G_UNLIKELY (*str == '.')) {
            dec = FALSE;
            was_digit = FALSE;
          } else
            was_digit = TRUE;

          str++;
        }

        if (*str == '\0' && was_digit)
          ret = TRUE;

      } else if (was_digit)
        ret = TRUE;
    }
  }

  return ret;
}

void
quit_app ()
{

  NVGST_INFO_MESSAGE ("quitting the app");

  if (!app->bg_mode) {
    if (!trd_exit) {
      trd_exit = TRUE;
      g_thread_join (trd);
    }
  }

  g_main_loop_quit (loop);
}

static gboolean
on_input (GIOChannel * ichannel, GIOCondition cond, gpointer data)
{
  inAttrs *in = app->input;
  gboolean yes, yes1;
  NvGstReturn res = NVGST_RET_SUCCESS;
  static gchar tbuffer[256];

  yes1 = (!data && in->operation_mode == NVGST_CMD_SCRIPT) || (data
      && in->operation_mode == NVGST_CMD_USER);
  yes = yes1 && app->pipeline && app->running && !app->image_eos
      && !app->got_eos;

  if (data == NULL) {
    res = get_next_command (&in->attrs, tbuffer, sizeof(tbuffer), in->postpone);
    in->postpone = FALSE;

    if (res == NVGST_RET_END) {
      //g_print("\n in->operation_mode = NVGST_CMD_USER\n");
      in->operation_mode = NVGST_CMD_USER;
      goto done;
    }
  } else {
    GQueue *que = (GQueue *) data;
    gchar *str = g_queue_pop_head (que);
    strncpy (tbuffer, str, (sizeof (tbuffer)-1));
    tbuffer[sizeof (tbuffer)-1] = '\0';
    g_free (str);
  }

  NVGST_DEBUG_MESSAGE_V ("\ngot a command %s <%d %d %d>\n", tbuffer,
      app->image_eos, app->got_eos, app->running);

  if (!g_strcmp0 (tbuffer, "h")) {
    g_print ("%s\n", app->extra_options);

  } else if (!g_strcmp0 (tbuffer, "q")) {
    quit_app ();
  } else if (g_str_has_prefix (tbuffer, "w") && yes1) {
    in->interval = atof (tbuffer + 1) * GST_USECOND;
    res = exec_ops (NVGST_OPS_WAIT);

  } else if (g_str_has_prefix (tbuffer, "z") && app->running) {
    in->interval = atof (tbuffer + 1) * GST_USECOND;
    res = exec_ops (NVGST_OPS_STOP);

  } else if (g_str_has_prefix (tbuffer, "u:")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "i:")) {
    g_free (app->uri);

    app->uri = g_strdup (g_strstrip (tbuffer + 2));
    app->uriTotal = 1;
    app->uriCount = 0;

    goto_next_track (app);

  } else if (g_str_has_prefix (tbuffer, "e:")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "x:")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "nos:")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "sth:")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "upb:")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "noa:")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "nov:")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "dan:")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "dvn:")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "ubf:")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "tag:")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "a")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "d")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "n")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "l")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "j")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "t")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "y")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "b")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "k")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "svd:")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "sad:")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "svc:")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "sac:")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "svs:")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "sas:")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "shttp:")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "srtsp:")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "sudp:")) {
    /* TODO */

  } else if (g_str_has_prefix (tbuffer, "sfsrc:")) {
    /* TODO */

  } else if (app->cur_operation == NVGST_OPS_NONE) {
    if (g_str_has_prefix (tbuffer, "c") || g_str_has_prefix (tbuffer, "]") ||
        g_str_has_prefix (tbuffer, "[")) {

      if (g_str_has_prefix (tbuffer, "c")) {
        app->uriCount--;

      } else if (g_str_has_prefix (tbuffer, "[")) {
        app->uriCount -= 2;
      }

      if (app->uriCount < 0)
        app->uriCount = 0;

      goto_next_track (app);
      goto ret;

    } else if (g_str_has_prefix (tbuffer, "r") && yes1 && app->pipeline) {
      GstState state, pending;
      in->interval = atof (tbuffer + 1) * GST_USECOND;

      if (app->buffering) {
        if (in->operation_mode == NVGST_CMD_SCRIPT)
          in->postpone = TRUE;

      } else if (gst_element_get_state (app->pipeline, &state, &pending,
              GST_CLOCK_TIME_NONE)
          != GST_STATE_CHANGE_FAILURE && state < GST_STATE_PLAYING
          && state > GST_STATE_NULL && pending == GST_STATE_VOID_PENDING) {

        if (state == GST_STATE_READY) {
          GstStateChangeReturn ret;
          NVGST_INFO_MESSAGE ("pausing");

          in->pending_play = TRUE;
          app->cur_operation = NVGST_OPS_PAUSE;
          if ((ret = gst_element_set_state (app->pipeline, GST_STATE_PAUSED)) ==
              GST_STATE_CHANGE_FAILURE) {
            NVGST_CRITICAL_MESSAGE_V ("pipeline state change failure to %s",
                gst_element_state_get_name (GST_STATE_PAUSED));
            res = NVGST_RET_ERR;
            CALL_GUI_FUNC (set_playback_status, STATUS_ERROR);
          } else {
            if (ret == GST_STATE_CHANGE_NO_PREROLL)
              app->is_live = TRUE;
            res = NVGST_RET_ASYNC;
          }
        } else {
          res = exec_ops (NVGST_OPS_PLAY);
        }
      } else {
        res = NVGST_RET_INVALID;
      }

    } else if (g_str_has_prefix (tbuffer, "p") && yes1 && app->pipeline) {
      GstState state, pending;
      in->interval = atof (tbuffer + 1) * GST_USECOND;

      if (app->buffering) {
        if (in->operation_mode == NVGST_CMD_SCRIPT)
          in->postpone = TRUE;

      } else if (gst_element_get_state (app->pipeline, &state, &pending,
              GST_CLOCK_TIME_NONE)
          == GST_STATE_CHANGE_SUCCESS && state > GST_STATE_NULL
          && state != GST_STATE_PAUSED && pending == GST_STATE_VOID_PENDING) {
        res = exec_ops (NVGST_OPS_PAUSE);
      } else
        res = NVGST_RET_ERR;

    } else if (!g_strcmp0 (tbuffer, "spos") && app->running) {
      GstClockTimeDiff pos;
      GstFormat format = GST_FORMAT_TIME;

      if (gst_element_query_position (app->pipeline, format, &pos))
        g_print ("Position: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS (pos));
      else
        g_print ("Position: Query Failed\n");

    } else if (!g_strcmp0 (tbuffer, "sdur") && app->running) {
      GstClockTimeDiff dur = GST_CLOCK_TIME_NONE;
      GstFormat format = GST_FORMAT_TIME;

      if (gst_element_query_duration (app->pipeline, format, &dur)
          && GST_CLOCK_TIME_IS_VALID (dur))
        g_print ("Duration: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS (dur));
      else
        g_print ("Duration: Query Failed\n");

    } else if (yes) {
      if (app->buffering && in->operation_mode == NVGST_CMD_SCRIPT) {
        in->postpone = TRUE;

      } else if (g_str_has_prefix (tbuffer, "f") &&
          is_valid_number (tbuffer + 1, TRUE, TRUE)) {
        GstClockTimeDiff pos;
        GstFormat format = GST_FORMAT_TIME;

        if (gst_element_query_position (app->pipeline, format, &pos) &&
            format == GST_FORMAT_TIME) {
          GstClockTimeDiff tpos = atof (tbuffer + 1) * GST_SECOND;

          in->interval = pos + tpos;
          res = exec_ops (NVGST_OPS_SEEK);
        } else {
          g_print ("cannot seek\n");
          res = NVGST_RET_ERR;
        }
      } else if (g_str_has_prefix (tbuffer, "<")) {
        GstClockTimeDiff pos;
        GstFormat format = GST_FORMAT_TIME;

        if (gst_element_query_position (app->pipeline, format, &pos) &&
            format == GST_FORMAT_TIME) {

          if ((GstClockTime) pos > 10000000000ULL)
            in->interval = pos - 10000000000ULL;
          else
            in->interval = 0;

          res = exec_ops (NVGST_OPS_SEEK);
        } else {
          g_print ("cannot seek\n");
          res = NVGST_RET_ERR;
        }
      } else if (g_str_has_prefix (tbuffer, ">")) {
        GstClockTimeDiff pos;
        GstFormat format = GST_FORMAT_TIME;

        if (gst_element_query_position (app->pipeline, format, &pos) &&
            format == GST_FORMAT_TIME) {
          GstClockTimeDiff dur;
          in->interval = pos + 10000000000ULL;

          if (gst_element_query_duration (app->pipeline, format, &dur)
              && GST_CLOCK_TIME_IS_VALID (dur)) {
            if (dur < (GstClockTimeDiff) in->interval) {
              app->got_eos = TRUE;
              goto_next_track (app);
            } else {
              res = exec_ops (NVGST_OPS_SEEK);
            }
          } else {
            res = exec_ops (NVGST_OPS_SEEK);
          }
        } else {
          g_print ("cannot seek\n");
          res = NVGST_RET_ERR;
        }
      } else if (g_str_has_prefix (tbuffer, "s") &&
          is_valid_number (tbuffer + 1, FALSE, TRUE)) {
        in->interval = ABS (atof (tbuffer + 1)) * GST_SECOND;
        res = exec_ops (NVGST_OPS_SEEK);

      } else if (g_str_has_prefix (tbuffer, "v") &&
          is_valid_number (tbuffer + 1, FALSE, FALSE)) {
        GstFormat format = GST_FORMAT_TIME;
        GstClockTimeDiff dur = GST_CLOCK_TIME_NONE;

        in->interval = ABS (atoi (tbuffer + 1));

        if (gst_element_query_duration (app->pipeline, format, &dur)
            && GST_CLOCK_TIME_IS_VALID (dur)) {
          in->interval = gst_util_uint64_scale (dur, in->interval, 100);
          res = exec_ops (NVGST_OPS_SEEK);
        } else {
          g_print ("cannot seek\n");
          res = NVGST_RET_ERR;
        }

      } else {
        res = NVGST_RET_INVALID;
      }
    } else {
      res = NVGST_RET_INVALID;
    }
  } else {
    res = NVGST_RET_INVALID;
  }

done:
  if (res < NVGST_RET_SUCCESS) {
    if (res == NVGST_RET_ERR)
      g_print ("command execution failed\n");
    else if (res != NVGST_RET_END)
      g_print ("cannot process the command, mode: %d, cur_operation = %d\n",
          in->operation_mode, app->cur_operation);

    in->interval = 0;
    if (res != NVGST_RET_INVALID) {
      in->pending_play = FALSE;
      app->cur_operation = NVGST_OPS_NONE;
    }
  }

  if (in->postpone) {
    app->cmd_id = g_timeout_add (2000, on2_input, NULL);
  } else if (res != NVGST_RET_ASYNC) {
    if (in->operation_mode == NVGST_CMD_SCRIPT && !data) {
      app->cmd_id = g_timeout_add (in->interval, on2_input, NULL);
    }
  }

ret:
  return TRUE;
}


NvGstReturn
exec_ops (NvGstOperation operation)
{
  NvGstReturn ret = NVGST_RET_SUCCESS;
  inAttrs *in = app->input;

  app->cur_operation = operation;

  switch (operation) {
    case NVGST_OPS_STOP:{
      reset_current_track ();
    }
      break;

    case NVGST_OPS_SEEK:{
      GstClockTime end = GST_CLOCK_TIME_NONE;
      GstClockTime seekPos;
      GstSeekFlags flags = GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT;

      seekPos = in->interval;
      in->interval = 0;

      NVGST_INFO_MESSAGE_V ("seeking to %" GST_TIME_FORMAT,
          GST_TIME_ARGS (seekPos));

      if (GST_CLOCK_TIME_IS_VALID (in->attrs.segment_duration)) {
        GstClockTimeDiff pos;
        GstFormat format = GST_FORMAT_TIME;

        if (gst_element_query_position (app->pipeline, format, &pos) &&
            format == GST_FORMAT_TIME) {
          flags |= GST_SEEK_FLAG_SEGMENT;
          app->accum_time += (pos - app->last_seek_time);

          NVGST_DEBUG_MESSAGE_V ("segment_duration %" GST_TIME_FORMAT,
              GST_TIME_ARGS (in->attrs.segment_duration));
          NVGST_DEBUG_MESSAGE_V ("accum_time: %" GST_TIME_FORMAT,
              GST_TIME_ARGS (app->accum_time));

          if (app->accum_time < in->attrs.segment_duration) {
            end = seekPos + (in->attrs.segment_duration - app->accum_time);
            app->last_seek_time = seekPos;
          } else {
            end = seekPos + 1;
          }
        }
      }

      NVGST_DEBUG_MESSAGE_V ("end: %" GST_TIME_FORMAT, GST_TIME_ARGS (end));

      ret = gst_element_seek (app->seekElement, 1.0, GST_FORMAT_TIME, flags,
          GST_SEEK_TYPE_SET, seekPos, GST_SEEK_TYPE_SET, end);

      if (!ret) {
        NVGST_CRITICAL_MESSAGE ("seek failed");
        ret = NVGST_RET_ERR;
        CALL_GUI_FUNC (set_playback_status, STATUS_ERROR);
      } else {
        ret = NVGST_RET_ASYNC;
      }
    }
      break;

    case NVGST_OPS_PAUSE:{
      NVGST_INFO_MESSAGE ("pausing");

      if (gst_element_set_state (app->pipeline, GST_STATE_PAUSED) ==
          GST_STATE_CHANGE_FAILURE) {
        NVGST_CRITICAL_MESSAGE_V ("pipeline state change failure to %s",
            gst_element_state_get_name (GST_STATE_PAUSED));
        ret = NVGST_RET_ERR;
        CALL_GUI_FUNC (set_playback_status, STATUS_ERROR);
      } else {
        ret = NVGST_RET_ASYNC;
      }
    }
      break;

    case NVGST_OPS_PLAY:{
      NVGST_INFO_MESSAGE ("playing");

      if (gst_element_set_state (app->pipeline, GST_STATE_PLAYING) ==
          GST_STATE_CHANGE_FAILURE) {
        NVGST_CRITICAL_MESSAGE_V ("pipeline state change failure to %s",
            gst_element_state_get_name (GST_STATE_PLAYING));
        ret = NVGST_RET_ERR;
        CALL_GUI_FUNC (set_playback_status, STATUS_ERROR);
      } else {
        ret = NVGST_RET_ASYNC;

        if (app->stats) {
          pfData_s *self = &app->pfData;
          if (app->stats_file)
            g_fprintf (self->file, "playing from rtime %" GST_TIME_FORMAT "\n",
                GST_TIME_ARGS (gst_util_get_timestamp ()));

          g_assert (app->pfData.dps_cb == 0
              && !GST_CLOCK_TIME_IS_VALID (app->pfData.start_ts));
          self->last_ts = self->start_ts = gst_util_get_timestamp ();
          self->dps_cb = g_timeout_add (INITIAL_FPS_UPDATE_INTERVAL_MS,
              display_current_fps, self);

          if (self->timer)
            g_timer_continue (self->timer);
          else
            self->timer = g_timer_new ();
        }
      }
    }
      break;

    case NVGST_OPS_WAIT:{

    }
      break;

    default:
      g_print ("invalid command\n");
      ret = NVGST_RET_INVALID;
      break;
  }

  if (ret != NVGST_RET_ASYNC)
    app->cur_operation = NVGST_OPS_NONE;

  return ret;
}


gboolean
goto_next_track (gpointer data)
{
  NvGstReturn ret = NVGST_RET_SUCCESS;
  inAttrs *in = app->input;

  if (!app->in_error && app->got_eos && --in->attrs.repeats > 0) {
    NVGST_DEBUG_MESSAGE ("resetting the track");
    app->uriCount--;

    /* WAR for bug 200071832 */
    destroy_current_track ();
    app->cur_operation = NVGST_OPS_NONE;

    app->input->operation_mode = NVGST_CMD_SCRIPT;
    in->attrs.lplist = in->attrs.lplist_head;
    in->attrs.cmlist = in->attrs.cmlist_head;

    in->postpone = FALSE;

  } else {
    NVGST_DEBUG_MESSAGE ("destroying the track");
    destroy_current_track ();
  }

if(app->stats)
  stats_func(app->pfData.average_fps, app->pfData.frames_rendered, app->pfData.frames_dropped);

  NVGST_INFO_MESSAGE_V ("uriCount: %d,  uriTotal: %d", (gint) app->uriCount,
      (gint) app->uriTotal);

  if (app->uriCount++ >= (gint) app->uriTotal) {
    NVGST_INFO_MESSAGE ("done playing all URIs");
    if (app->attrs.loop_forever) {
      NVGST_INFO_MESSAGE ("Looping over the URI List \n");
      app->uriCount = 1;
      ret = setup_track ();
    } else {
      ret = NVGST_RET_END;
    }
  } else {
    ret = setup_track ();
  }

  if (ret == NVGST_RET_END && !GUI) {
    quit_app ();
  } else if (ret == NVGST_RET_ERR) {
    app->cmd_id = g_idle_add (goto_next_track, app);
  }

  return FALSE;
}


static void
_error_msg (GstMessage * message)
{
  GError *error = NULL;
  gchar *elm_name, *debug = NULL;

  elm_name = gst_object_get_path_string (message->src);
  gst_message_parse_error (message, &error, &debug);

  CALL_GUI_FUNC (show_error, error->message);

  g_printerr ("Error by %s: %s\n", elm_name, error->message);
  if (debug != NULL)
    g_printerr ("debug info:\n%s\n", debug);

  g_error_free (error);
  g_free (debug);
  g_free (elm_name);

  return;
}


static void
_tag_info (const GstTagList * list, const gchar * tag, gpointer data)
{
  gint tagCount, index;

  tagCount = gst_tag_list_get_tag_size (list, tag);

  for (index = 0; index < tagCount; index++) {
    gchar *pStr;

    if (gst_tag_get_type (tag) == G_TYPE_STRING) {
      if (!gst_tag_list_get_string_index (list, tag, index, &pStr))
        g_assert_not_reached ();
    } else if (gst_tag_get_type (tag) == GST_TYPE_BUFFER) {
      GstBuffer *buf;

      buf =
          gst_value_get_buffer (gst_tag_list_get_value_index (list, tag,
              index));
      if (buf) {
        pStr =
            g_strdup_printf ("Buffer of %zu bytes", gst_buffer_get_size (buf));
      } else {
        pStr = g_strdup ("NULL buffer");
      }

    } else {
      pStr =
          g_strdup_value_contents (gst_tag_list_get_value_index (list, tag,
              index));
    }

    if (index == 0) {
      g_print ("%16s: %s\n", gst_tag_get_nick (tag), pStr);
    } else {
      g_print ("%16s: %s\n", "", pStr);
    }

    g_free (pStr);
  }

  return;
}


static gboolean
image_stop (gpointer data)
{
  app->got_eos = TRUE;

  goto_next_track (app);

  return FALSE;
}


static gboolean
bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
  inAttrs *in = app->input;
  gboolean in_error = FALSE;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:{
      _error_msg (msg);
      in_error = TRUE;
      app->return_value = -1;
    }

    case GST_MESSAGE_SEGMENT_DONE:{
      if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_SEGMENT_DONE)
        NVGST_DEBUG_MESSAGE ("segment done");
    }

    case GST_MESSAGE_EOS:{
      if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS)
        NVGST_INFO_MESSAGE ("eos, END OF STREAM");
      CALL_GUI_FUNC (set_playback_status, STATUS_STOPPED);
      if (!app->in_error) {
        app->in_error = in_error;
        goto_next_track (app);
      }
    }
      break;

    case GST_MESSAGE_QOS:{
      if (app->stats) {
        GstElement *src = (GstElement *) GST_MESSAGE_SRC (msg);
        GstElementFactory *factory = gst_element_get_factory (src);
        const gchar *klass = gst_element_factory_get_klass (factory);
        if (strstr (klass, "Decode") && strstr (klass, "Video")) {
          guint64 frames_dropped;
          gst_message_parse_qos_stats (msg, NULL, NULL, &frames_dropped);
          if (frames_dropped > app->pfData.frames_dropped_decoder) {
            g_atomic_int_inc (&app->pfData.frames_dropped);
            app->pfData.frames_dropped_decoder++;
          }
        }
      }
      NVGST_DEBUG_MESSAGE ("QoS, frame dropped");
    }
      break;

    case GST_MESSAGE_WARNING:{
      GError *gerror;
      gchar *debug;
      gchar *name = gst_object_get_path_string (GST_MESSAGE_SRC (msg));

      gst_message_parse_warning (msg, &gerror, &debug);
      NVGST_WARNING_MESSAGE_V ("WARNING on bus from %s: %s", name,
          gerror->message);
      if (debug) {
        NVGST_WARNING_MESSAGE_V ("debug info:\n%s\n", debug);
      }
      g_error_free (gerror);
      g_free (debug);
      g_free (name);
    }
      break;

    case GST_MESSAGE_ELEMENT:{
      const GstStructure *str = gst_message_get_structure (msg);

      if (gst_structure_has_name (str, "decoder-status")) {
        guint DecodedMBs, ConcealedMBs, FrameDecodeTime;

        const gchar *decoder_error_str =
            gst_structure_get_string (str, "DecodeErrorString");
        gst_structure_get_uint (str, "DecodedMBs", &DecodedMBs);
        gst_structure_get_uint (str, "ConcealedMBs", &ConcealedMBs);
        gst_structure_get_uint (str, "FrameDecodeTime", &FrameDecodeTime);
        g_print
            ("\n-----> DecodeError = %s, DecodedMBs = %u, ConcealedMBs = %u, FrameDecodeTime = %u <-----\n",
            decoder_error_str, DecodedMBs, ConcealedMBs, FrameDecodeTime);
      }
    }
      break;

    case GST_MESSAGE_TAG:{
      GstTagList *tags;
      gst_message_parse_tag (msg, &tags);
      CALL_GUI_FUNC (handle_stream_tags, tags);
      if (in->attrs.show_tags) {
        gchar *name = gst_object_get_path_string (GST_MESSAGE_SRC (msg));

        g_print ("\n===================== TAGS ======================== \n");
        g_print ("TAG INFO from \"%s\"\n", name);

        gst_tag_list_foreach (tags, _tag_info, NULL);
        g_free (name);
        g_print ("===================================================\n");
      }
      gst_tag_list_free (tags);
    }
      break;

    case GST_MESSAGE_INFO:{
      GError *gerror;
      gchar *debug;
      gchar *name = gst_object_get_path_string (GST_MESSAGE_SRC (msg));

      gst_message_parse_info (msg, &gerror, &debug);
      if (debug) {
        NVGST_INFO_MESSAGE_V ("INFO on bus by %s:\n%s\n", name, debug);
      }

      g_error_free (gerror);
      g_free (debug);
      g_free (name);
    }
      break;

    case GST_MESSAGE_BUFFERING:{
      gint percent;
      gboolean busy = FALSE;

      gst_message_parse_buffering (msg, &percent);
      g_print ("buffering.. %d\r", percent);

      if (app->cur_operation || app->got_eos) {
        busy = TRUE;
      }

      /* live pipeline */
      if (app->is_live)
        break;

      if (percent == 100) {
        app->buffering = FALSE;

        if (!busy && app->target_state == GST_STATE_PLAYING) {
          NVGST_INFO_MESSAGE
              ("buffering complete, setting the pipeline to PLAYING..");

          if (gst_element_set_state (app->pipeline, GST_STATE_PLAYING) ==
              GST_STATE_CHANGE_FAILURE) {
            NVGST_CRITICAL_MESSAGE_V ("pipeline state change failure to %s",
                gst_element_state_get_name (GST_STATE_PLAYING));
          }
        }

        app->target_state = GST_STATE_VOID_PENDING;

      } else {
        if (!busy && app->buffering == FALSE) {
          GstState state, pending;

          if (gst_element_get_state (app->pipeline, &state, &pending,
                  GST_CLOCK_TIME_NONE) == GST_STATE_CHANGE_FAILURE) {
            NVGST_CRITICAL_MESSAGE ("failed to query the pipeline for state");
            /* undefined behaviour follows */

          } else {
            g_assert (state > GST_STATE_READY);
            g_assert (pending == GST_STATE_VOID_PENDING);
            app->target_state = state;

            if (state == GST_STATE_PLAYING) {
              NVGST_INFO_MESSAGE
                  ("buffering start, setting the pipeline to PAUSED..");

              if (gst_element_set_state (app->pipeline, GST_STATE_PAUSED) ==
                  GST_STATE_CHANGE_FAILURE) {
                NVGST_CRITICAL_MESSAGE_V ("pipeline state change failure to %s",
                    gst_element_state_get_name (GST_STATE_PAUSED));
              }
            }
          }
        }

        app->buffering = TRUE;
      }
    }
      break;

    case GST_MESSAGE_LATENCY:{
      NVGST_INFO_MESSAGE ("redistribute the latency...");
      gst_bin_recalculate_latency (GST_BIN (app->pipeline));
    }
      break;

    case GST_MESSAGE_STATE_CHANGED:{
      GstState old, new, pending;
      inAttrs *in = app->input;

      gst_message_parse_state_changed (msg, &old, &new, &pending);

      NVGST_DEBUG_MESSAGE_V
          ("element %s changed state from %s to %s, pending %s",
          GST_OBJECT_NAME (msg->src), gst_element_state_get_name (old),
          gst_element_state_get_name (new),
          gst_element_state_get_name (pending));

      if (GST_MESSAGE_SRC (msg) == GST_OBJECT (app->pipeline) &&
          pending == GST_STATE_VOID_PENDING) {
        if (app->cur_operation) {
          NvGstOperation cur = app->cur_operation;
          gboolean done = FALSE;

          if (app->got_eos) {
            /* leave everything and just wait for the eos to appear on the bus */
            NVGST_WARNING_MESSAGE
                ("***************** VERY RARE SITUATION, got eos while state change");
            cur = app->cur_operation = NVGST_OPS_NONE;
            in->operation_mode = NVGST_CMD_NONE;
          }

          if (cur == NVGST_OPS_PLAY && new == GST_STATE_PLAYING) {
            done = TRUE;

          } else if (cur == NVGST_OPS_PAUSE && new == GST_STATE_PAUSED
              && old == GST_STATE_PLAYING) {
            if (app->stats) {
              g_timer_stop (app->pfData.timer);
              display_current_fps (&app->pfData);
              if (g_main_context_find_source_by_id (NULL, app->pfData.dps_cb))
                g_source_remove (app->pfData.dps_cb);
              app->pfData.dps_cb = 0;

              app->pfData.last_ts = GST_CLOCK_TIME_NONE;
              app->pfData.start_ts = GST_CLOCK_TIME_NONE;
              app->pfData.prev_ts = -1;
              app->pfData.initial_fps = TRUE;
              app->pfData.last_frames_rendered = app->pfData.frames_rendered;
              app->pfData.last_frames_dropped = app->pfData.frames_dropped;
              if (app->pfData.file)
                g_fprintf (app->pfData.file,
                    "paused at rtime %" GST_TIME_FORMAT "\n",
                    GST_TIME_ARGS (gst_util_get_timestamp ()));
            }

            if (app->unpause) {
              g_usleep (2500000);

              NVGST_INFO_MESSAGE ("unpausing");

              if (gst_element_set_state (app->pipeline, GST_STATE_PLAYING) ==
                  GST_STATE_CHANGE_FAILURE) {
                NVGST_CRITICAL_MESSAGE_V ("pipeline state change failure to %s",
                    gst_element_state_get_name (GST_STATE_PLAYING));
              }
            } else
              done = TRUE;

          } else if (cur == NVGST_OPS_PAUSE && new == GST_STATE_PAUSED
              && old == GST_STATE_READY) {
            if (app->no_more_pads || app->is_live) {
              GstFormat format = GST_FORMAT_TIME;
              in->duration = GST_CLOCK_TIME_NONE;
              app->running = TRUE;

              if (app->stats) {
                GstClockTimeDiff duration;
                GstFormat format = GST_FORMAT_TIME;

                fps_init (&app->pfData);

                if (gst_element_query_duration (app->pipeline, format,
                        &duration))
                  if (format == GST_FORMAT_TIME
                      && GST_CLOCK_TIME_IS_VALID (duration) && app->pfData.file)
                    g_fprintf (app->pfData.file,
                        "Duration: %" GST_TIME_FORMAT "\n",
                        GST_TIME_ARGS (duration));

                if (app->vrender_pad && app->pfData.file) {
                  GstCaps *caps = gst_pad_get_current_caps (app->vrender_pad);
                  if (caps) {
                    gchar *str = gst_caps_to_string (caps);
                    g_fprintf (app->pfData.file, "Video Render Format: %s\n",
                        str);
                    g_free (str);
                    gst_caps_unref (caps);
                  }
                }

                if (app->arender_pad && app->pfData.file) {
                  GstCaps *caps = gst_pad_get_current_caps (app->arender_pad);
                  if (caps) {
                    gchar *str = gst_caps_to_string (caps);
                    g_fprintf (app->pfData.file, "Audio Render Format: %s\n",
                        str);
                    g_free (str);
                    gst_caps_unref (caps);
                  }
                }
              }

              if (!app->image_eos) {
                if (!(gst_element_query_duration (app->pipeline, format,
                            &in->duration) && format == GST_FORMAT_TIME
                        && GST_CLOCK_TIME_IS_VALID (in->duration))) {
                  NVGST_WARNING_MESSAGE ("failed to query duration in time");
                  in->duration = GST_CLOCK_TIME_NONE;
                } else if (in->attrs.startPer) {
                  in->attrs.segment_start = gst_util_uint64_scale (in->duration,
                      in->attrs.segment_start, 100);
                }
              } else {
                app->image_eos =
                    g_timeout_add_seconds (in->attrs.image_display_time,
                    image_stop, NULL);
                in->pending_play = FALSE;
                in->attrs.segment_start = 0;
                in->attrs.segment_duration = GST_CLOCK_TIME_NONE;
              }

              if (GST_CLOCK_TIME_IS_VALID (in->attrs.segment_duration)
                  || in->attrs.segment_start > 0) {
                GstSeekFlags flags =
                    GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT;
                GstClockTime end;
                gboolean res;

                if (GST_CLOCK_TIME_IS_VALID (in->attrs.segment_duration)) {
                  flags |= GST_SEEK_FLAG_SEGMENT;
                  end = in->attrs.segment_start + in->attrs.segment_duration;

                } else {
                  end = GST_CLOCK_TIME_NONE;
                }

                app->cur_operation = NVGST_OPS_SEEK;

                res =
                    gst_element_seek (app->seekElement, 1.0, GST_FORMAT_TIME,
                    flags, GST_SEEK_TYPE_SET, in->attrs.segment_start,
                    GST_SEEK_TYPE_SET, end);

                if (!res) {
                  NVGST_CRITICAL_MESSAGE ("seek failed");
                  done = TRUE;
                }

              } else {
                in->attrs.segment_start = 0;
                in->attrs.segment_duration = GST_CLOCK_TIME_NONE;
                done = TRUE;
              }
            }
          } else if (cur == NVGST_OPS_SEEK) {
            if (new > GST_STATE_READY && old == GST_STATE_PAUSED)
              done = TRUE;
            in->interval = 0;
          }

          if (done) {
            app->cur_operation = NVGST_OPS_NONE;

            if (in->pending_play) {
              GstStateChangeReturn rt;

              in->pending_play = FALSE;
              app->cur_operation = NVGST_OPS_PLAY;

              if (app->stats) {
                pfData_s *self = &app->pfData;
                if (app->pfData.file)
                  g_fprintf (self->file,
                      "playing from rtime %" GST_TIME_FORMAT "\n",
                      GST_TIME_ARGS (gst_util_get_timestamp ()));

                g_assert (self->dps_cb == 0
                    && !GST_CLOCK_TIME_IS_VALID (self->start_ts));
                self->last_ts = self->start_ts = gst_util_get_timestamp ();
                self->dps_cb = g_timeout_add (INITIAL_FPS_UPDATE_INTERVAL_MS,
                    display_current_fps, self);

                if (self->timer)
                  g_timer_continue (self->timer);
                else
                  self->timer = g_timer_new ();
              }

              rt = gst_element_set_state (app->pipeline, GST_STATE_PLAYING);

              /* Dump Playing Pipeline into the dot file
               * Set environment variable "export GST_DEBUG_DUMP_DOT_DIR=/tmp"
               * Run nvgstplayer-1.0 and 0.00.00.*-nvgstplayer-1.0-playing.dot
               * file will be generated.
               * Run "dot -Tpng 0.00.00.*-nvgstplayer-1.0-playing.dot > image.png"
               * image.png will display the running pipeline.
               * */
              GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (app->pipeline),
                  GST_DEBUG_GRAPH_SHOW_ALL, "nvgstplayer-1.0-playing");

              if (rt == GST_STATE_CHANGE_FAILURE) {
                NVGST_CRITICAL_MESSAGE_V ("pipeline state change failure to %s",
                    gst_element_state_get_name (GST_STATE_PLAYING));
                app->cur_operation = NVGST_OPS_NONE;
              }
            }

            if (app->cur_operation == NVGST_OPS_NONE) {
              if (in->operation_mode == NVGST_CMD_SCRIPT) {
                app->cmd_id = g_timeout_add (in->interval, on2_input, NULL);
              }
            }
          }
        }
        if (new == GST_STATE_PLAYING)
          CALL_GUI_FUNC (set_playback_status, STATUS_PLAYING);
        else if (new == GST_STATE_PAUSED)
          CALL_GUI_FUNC (set_playback_status, STATUS_PAUSED);
        else
          CALL_GUI_FUNC (set_playback_status, STATUS_STOPPED);
      }
    }
      break;

    case GST_MESSAGE_APPLICATION:{
      const GstStructure *s;
      s = gst_message_get_structure (msg);

      if (gst_structure_has_name (s, "NvGstAppInterrupt")) {
        g_print ("Handling the interrupt ...\n");

        if (!app->bg_mode) {
          if (!trd_exit) {
            trd_exit = TRUE;
            g_thread_join (trd);
          }
        }
        g_main_loop_quit (loop);

      } else if (gst_structure_has_name (s, "NvGstAppVideoBinFailure")) {
        g_print ("Handling Video Bin failure...\n");

        if (!app->bg_mode) {
          if (!trd_exit) {
            trd_exit = TRUE;
            g_thread_join (trd);
          }
        }
        g_main_loop_quit (loop);
      }
    }
      break;

    default:
      break;
  }

  return TRUE;
}

static gchar **
get_keys (GstCaps * caps, gchar * str, gchar * xstr)
{
  gchar **keys = NULL;
  if (app->pathCfg) {
    inAttrs *in = app->input;

    if (g_key_file_has_key (app->pathCfg, in->uri, str, NULL)) {
      g_key_file_set_list_separator (app->pathCfg, '!');
      keys =
          g_key_file_get_string_list (app->pathCfg, in->uri, str, NULL, NULL);
      g_key_file_set_list_separator (app->pathCfg, ';');
    }
  }

  if (!keys && xstr) {
    keys = g_strsplit_set (xstr, "!", -1);
  }

  if (!keys && app->elemCfg) {
    if (g_key_file_has_group (app->elemCfg, str)) {
      g_key_file_set_list_separator (app->elemCfg, '!');
      keys = g_key_file_get_string_list (app->elemCfg, str, "pipe", NULL, NULL);
      g_key_file_set_list_separator (app->elemCfg, ';');
    }

    /* Only for Decoders */
    if (!keys && caps) {
      gchar **pgp = app->elem_gps;
      gchar *val;

      while (*pgp) {
        if ((val = g_key_file_get_value (app->elemCfg, *pgp, "type", NULL))
            && (!g_strcmp0 (val, "svd") || !g_strcmp0 (val, "sad"))) {
          GstCaps *pcaps;

          /*TODO* convert | to ! */
          pcaps = gst_caps_from_string (*pgp);

          g_free (val);
          val = NULL;

          if (pcaps) {
            if (gst_caps_can_intersect (pcaps, caps)) {
              g_key_file_set_list_separator (app->elemCfg, '!');
              keys =
                  g_key_file_get_string_list (app->elemCfg, *pgp, "pipe", NULL,
                  NULL);
              g_key_file_set_list_separator (app->elemCfg, ';');
              gst_caps_unref (pcaps);
              break;
            }
            gst_caps_unref (pcaps);
          }
        }
        pgp++;
      }
    }
  }

  if (!keys) {
    gchar *elems = g_hash_table_lookup (app->htable, str);
    if (elems) {
      keys = g_strsplit_set (elems, "!", -1);
    }
  }

  return keys;
}


static GstElement *
create_element (GstCaps * caps, gchar * str, gchar * xstr, gchar ** skeys)
{
  GstElement *element = NULL, *previous = NULL;
  GstElement *bin = NULL;
  gchar **vkey = NULL, **keys = NULL;
  gint count = 0;

  if (!skeys) {
    keys = get_keys (caps, str, xstr);
  } else {
    keys = skeys;
  }

  if (keys) {
    vkey = keys;
    while (*vkey) {
      gchar **tokens = g_strsplit_set (*vkey, "#", -1);
      gchar **vtoken = tokens;

      g_strstrip (*vtoken);
      element = gst_element_factory_make (tokens[0], NULL);
      if (!element) {
        g_strfreev (tokens);
        goto fail;
      }

      if (count > 0) {
        if (count == 1) {
          bin = (GstElement *) gst_bin_new (str);

          if (!gst_bin_add ((GstBin *) bin, previous)) {
            gst_object_unref (previous);
            gst_object_unref (element);
            g_strfreev (tokens);
            goto fail;
          }
#if 0
          if (!GST_OBJECT_FLAG_IS_SET (previous, GST_ELEMENT_IS_SOURCE))
#endif
          {
            GstPad *pad = gst_element_get_static_pad (previous, "sink");
            if (!pad)
              pad = gst_element_get_static_pad (previous, "video_sink");
            if (pad) {
              gst_element_add_pad (bin, gst_ghost_pad_new ("sink", pad));
              gst_object_unref (pad);
            } else {
              NVGST_CRITICAL_MESSAGE_V ("failed to get sink pad from %s",
                  GST_ELEMENT_NAME (previous));
              gst_object_unref (element);
              goto fail;
            }
          }
        }

        if (!gst_bin_add ((GstBin *) bin, element)) {
          g_strfreev (tokens);
          gst_object_unref (element);
          goto fail;
        }

        if (!gst_element_link (previous, element)) {
          NVGST_CRITICAL_MESSAGE_V ("failed to link  %s to %s",
              GST_ELEMENT_NAME (previous), GST_ELEMENT_NAME (element));
          goto fail;
        }
      } else {
        bin = element;
      }

      previous = element;
      element = NULL;
      vtoken++;

      while (*vtoken) {
        GParamSpec *param_spec;
        GValue prop_value = { 0 };
        gchar *prop[2];
        gchar *temp;

        prop[0] = g_strdup (*vtoken);
        prop[1] = g_strdup (strstr (*vtoken, "=") + 1);
        if ((temp = strstr (prop[0], "=")))
          *temp = 0;

        g_strstrip (prop[0]);
        g_strstrip (prop[1]);

        param_spec =
            g_object_class_find_property (G_OBJECT_GET_CLASS (previous),
            prop[0]);

        if (!param_spec) {
          NVGST_WARNING_MESSAGE_V
              ("property %s does not exist in element %s, ignoring", prop[0],
              tokens[0]);
          g_strfreev (prop);
          vtoken++;
          continue;
        }

        g_value_init (&prop_value, param_spec->value_type);

        if (prop[1] && gst_value_deserialize (&prop_value, prop[1])) {
          NVGST_INFO_MESSAGE_V
              ("setting property \"%s\" having val=\"%s\" on %s\n", prop[0],
              prop[1], tokens[0]);
          g_object_set_property (G_OBJECT (previous), prop[0], &prop_value);
        } else {
          NVGST_WARNING_MESSAGE_V ("could not read value for property %s\n",
              prop[0]);
        }

        g_free (prop[0]);
        g_free (prop[1]);
        vtoken++;
      }

      g_strfreev (tokens);
      vkey++;
      count++;
    }
  }

  if (previous == NULL)
    goto fail;

  if (app->stats) {
    if (GST_OBJECT_FLAG_IS_SET (previous, GST_ELEMENT_FLAG_SINK)
        && !g_strcmp0 (str, NVGST_VIDEO_SINK)) {
      pfData_s *self = &app->pfData;

      fps_init (self);

      /* FIXME: assuming all videosinks are derived from basesink, which normally are */
      self->max_latency = GST_CLOCK_TIME_NONE;
      g_object_set (G_OBJECT (previous), "qos", TRUE, NULL);
      g_object_get (G_OBJECT (previous), "max-lateness", &self->max_latency,
          NULL);

      app->vrender_pad = gst_element_get_static_pad (previous, "sink");

      gst_pad_add_probe (app->vrender_pad, GST_PAD_PROBE_TYPE_EVENT_BOTH,
          (GstPadProbeCallback) (on_video_sink_flow), (gpointer) self, NULL);

    } else if (GST_OBJECT_FLAG_IS_SET (previous, GST_ELEMENT_FLAG_SINK)
        && !g_strcmp0 (str, NVGST_AUDIO_SINK)) {

      app->arender_pad = gst_element_get_static_pad (previous, "sink");
    }
  }

  if (count > 1 && !GST_OBJECT_FLAG_IS_SET (previous, GST_ELEMENT_FLAG_SINK)) {
    GstPad *pad = gst_element_get_static_pad (previous, "src");
    if (pad) {
      gst_element_add_pad (bin, gst_ghost_pad_new ("src", pad));
      gst_object_unref (pad);
    } else {
      NVGST_CRITICAL_MESSAGE_V ("failed to get src pad from %s",
          GST_ELEMENT_NAME (previous));
      goto fail;
    }
  }

done:
  if (!skeys && keys)
    g_strfreev (keys);

  return bin;

fail:
  if (bin) {
    gst_object_unref (bin);
    bin = NULL;
  }
  goto done;
}


static gboolean
gst_caps_is_raw (GstElement * dbin, GstCaps * icaps)
{
  GstCaps *caps = NULL;
  gboolean res = FALSE;

  g_object_get (dbin, "caps", &caps, NULL);

  if (caps) {
    res = gst_caps_can_intersect (caps, icaps);
    gst_caps_unref (caps);
  }

  return res;
}

static void
set_sync (GstElement * vsink, gboolean sync)
{
  if (GST_IS_BIN (vsink)) {
    GValue vvsink = G_VALUE_INIT;
    GstIterator *it = gst_bin_iterate_sinks (GST_BIN (vsink));
    GObject *obj = NULL;

    if (it && gst_iterator_next (it, &vvsink) == GST_ITERATOR_OK) {
      obj = g_value_get_object (&vvsink);
      g_object_set (obj, "sync", sync, NULL);
    }

    if (obj)
      gst_object_unref (obj);

    if (it)
      gst_iterator_free (it);

  } else {
    g_object_set (G_OBJECT (vsink), "sync", sync, NULL);
  }
}

void
set_window_handle (Window window)
{
  GstElement *vsink = app->vsink;

  if (GST_IS_BIN (vsink)) {
    GValue vvsink = G_VALUE_INIT;
    GstIterator *it = gst_bin_iterate_sinks (GST_BIN (app->vsink));
    GObject *obj = NULL;

    if (it && gst_iterator_next (it, &vvsink) == GST_ITERATOR_OK) {
      obj = g_value_get_object (&vvsink);
      vsink = GST_ELEMENT (obj);
    }

    if (obj)
      gst_object_unref (obj);

    if (it)
      gst_iterator_free (it);
  }

  if (GST_IS_VIDEO_OVERLAY (vsink)) {
    gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (vsink),
        (gulong) window);
    gst_video_overlay_expose (GST_VIDEO_OVERLAY (vsink));
  }
}

static GstElement *
create_video_pipeline (GstCaps * caps, GstPad * dbin_pad)
{
  GstElement *bin = NULL;
  GstElement *vdec = NULL, *gate = NULL;
  GstElement *vsink = NULL, *vconv = NULL;
  GstElement *queue = NULL;
  GstPad *pad;
  NvGstPlayFlags use_conv = app->input->attrs.flags;
  inAttrs *in = app->input;

  if (app->vpipe) {
    bin = app->vpipe;

  } else {
    bin = gst_bin_new ("video_bin");

    if (!gst_caps_is_raw (app->vdbin, caps) && !in->dbin_video_decoders) {
      vdec = create_element (caps, NVGST_VIDEO_DEC, app->svd, in->video_dec);
      if (!vdec) {
        NVGST_CRITICAL_MESSAGE ("failed to create video decoder pipe");
        goto fail;
      }

      if (!gst_bin_add (GST_BIN (bin), vdec)) {
        NVGST_CRITICAL_MESSAGE ("failed to add decoder pipe to video_bin");
        gst_object_unref (vdec);
        goto fail;
      }
    }

    queue = gst_element_factory_make ("queue", NULL);
    if (!queue) {
      NVGST_CRITICAL_MESSAGE ("failed to create element: queue");
      goto fail;
    }

    if (!gst_bin_add (GST_BIN (bin), queue)) {
      NVGST_CRITICAL_MESSAGE ("failed to add queue to video_bin");
      gst_object_unref (queue);
      goto fail;
    }
    vsink = create_element (NULL, NVGST_VIDEO_SINK, app->svs, NULL);
    if (!vsink) {
      NVGST_CRITICAL_MESSAGE_V ("failed to create element: %s",
          NVGST_VIDEO_SINK);
      goto fail;
    }

    app->vsink = vsink;

    if (!gst_bin_add (GST_BIN (bin), vsink)) {
      NVGST_CRITICAL_MESSAGE_V ("failed to add %s to video_bin",
          GST_ELEMENT_NAME (vsink));
      gst_object_unref (vsink);
      goto fail;
    }

    set_sync (vsink, in->attrs.sync);
    GstStructure *str = gst_caps_get_structure (caps, 0);
    const gchar *name = gst_structure_get_name (str);
    if (!(use_conv & NVGST_PLAY_FLAG_NATIVE_VIDEO) && g_strcmp0(name, "image/jpeg")) {
      vconv = create_element (NULL, NVGST_VIDEO_CONV, app->svc, NULL);
      if (!vconv) {
        NVGST_CRITICAL_MESSAGE_V ("failed to create element: %s",
            NVGST_VIDEO_CONV);
        goto fail;
      }

      if (!gst_bin_add (GST_BIN (bin), vconv)) {
        NVGST_CRITICAL_MESSAGE_V ("failed to add %s to video_bin",
            GST_ELEMENT_NAME (vconv));
        gst_object_unref (vconv);
        goto fail;
      }

      if (vdec) {
        gst_element_link_many (vdec, vconv, queue, vsink, NULL);
        gate = vdec;
      } else {
        gst_element_link_many (vconv, queue, vsink, NULL);
        gate = vconv;
      }
    } else {
      if (vdec) {
        if (!gst_element_link_many (vdec, queue, vsink, NULL)) {
          NVGST_CRITICAL_MESSAGE_V
              ("failed to link  %s to %s, try --disable-vnative",
              GST_ELEMENT_NAME (vdec), GST_ELEMENT_NAME (vsink));
          goto fail;
        }
        gate = vdec;
      } else {
        gst_element_link (queue, vsink);
        gate = queue;
      }
    }

    pad = gst_element_get_static_pad (gate, "sink");
    if (!pad && (gate == vconv))
      pad = gst_element_get_static_pad (gate, "video_sink");
    if (pad) {
      gst_element_add_pad (bin, gst_ghost_pad_new ("sink", pad));
      gst_object_unref (pad);
    } else {
      NVGST_CRITICAL_MESSAGE_V ("failed to get sink pad from %s",
          GST_ELEMENT_NAME (gate));
    }

    if (!app->seekElement)
      app->seekElement = gst_object_ref (vsink);
  }

done:
  return GST_ELEMENT (bin);

fail:
  if (bin) {
    gst_object_unref (bin);
    bin = NULL;
  }
  goto done;
}


static GstElement *
create_audio_pipeline (GstCaps * caps)
{
  GstElement *bin = NULL;
  GstElement *adec = NULL, *gate = NULL;
  GstElement *asink = NULL, *aconv = NULL;
  GstElement *cfilter = NULL;
  GstPad *pad, *asink_pad;
  GstCaps *asink_caps, *filtercaps;
  NvGstPlayFlags use_conv = app->input->attrs.flags;
  inAttrs *in = app->input;

  if (app->apipe) {
    bin = app->apipe;

  } else {
    bin = gst_bin_new ("audio_bin");

    if (!gst_caps_is_raw (app->adbin, caps) && !in->dbin_audio_decoders) {
      adec = create_element (caps, NVGST_AUDIO_DEC, app->sad, in->audio_dec);
      if (!adec) {
        NVGST_CRITICAL_MESSAGE ("failed to create audio decoder pipe");
        goto fail;
      }

      if (!gst_bin_add (GST_BIN (bin), adec)) {
        NVGST_CRITICAL_MESSAGE ("failed to add decoder pipe to audio_bin");
        gst_object_unref (adec);
        goto fail;
      }
    }

    asink = create_element (NULL, NVGST_AUDIO_SINK, app->sas, NULL);
    if (!asink) {
      NVGST_CRITICAL_MESSAGE_V ("failed to create element: %s",
          NVGST_AUDIO_SINK);
      goto fail;
    }
    app->asink = asink;

    if (!gst_bin_add (GST_BIN (bin), asink)) {
      NVGST_CRITICAL_MESSAGE_V ("failed to add %s to audio_bin",
          GST_ELEMENT_NAME (asink));
      gst_object_unref (asink);
      goto fail;
    }

    set_sync (asink, in->attrs.sync);

    asink_pad = gst_element_get_static_pad (app->asink, "sink");
    if (asink_pad) {
      gboolean intersect = TRUE;
      asink_caps = gst_pad_get_pad_template_caps (asink_pad);
      intersect = gst_caps_can_intersect (asink_caps, caps);
#ifdef NVGST_TARGET_TEGRA
      GstStructure *str = gst_caps_get_structure (caps, 0);
      const gchar *format = gst_structure_get_string (str, "format");
      guint64 channel_mask = 0x0;
      if (gst_structure_has_field (str, "channel-mask"))
        channel_mask =
            gst_value_get_bitmask (gst_structure_get_value (str,
                "channel-mask"));
      if (format) {
        if (!intersect || (strstr (format, "F")) || (strstr (format, "U"))
            || (channel_mask != 0x3)) {
          use_conv = app->input->attrs.flags &= ~NVGST_PLAY_FLAG_NATIVE_AUDIO;
        }
      }
#else
      if (!intersect) {
        use_conv = app->input->attrs.flags &= ~NVGST_PLAY_FLAG_NATIVE_AUDIO;
      }
#endif
      gst_caps_unref (asink_caps);
    }
    gst_object_unref (asink_pad);

    if (!(use_conv & NVGST_PLAY_FLAG_NATIVE_AUDIO)) {
      aconv = create_element (NULL, NVGST_AUDIO_CONV, app->sac, NULL);
      if (!aconv) {
        g_print ("failed to create element: %s", NVGST_AUDIO_CONV);
        goto fail;
      }

      if (!gst_bin_add (GST_BIN (bin), aconv)) {
        g_print ("failed to add %s to audio_bin", GST_ELEMENT_NAME (aconv));
        goto fail;
      }

      cfilter = gst_element_factory_make ("capsfilter", NULL);
      if (!cfilter) {
        g_print ("failed to create element: capsfilter");
        goto fail;
      }

      filtercaps =
          gst_caps_new_simple ("audio/x-raw", "format", G_TYPE_STRING, "S16LE",
          NULL);

      g_object_set (G_OBJECT (cfilter), "caps", filtercaps, NULL);
      gst_caps_unref (filtercaps);

      if (!gst_bin_add (GST_BIN (bin), cfilter)) {
        g_print ("failed to add cfilter to audio_bin");
        goto fail;
      }

      if (adec) {
        gst_element_link_many (adec, aconv, cfilter, asink, NULL);
        gate = adec;
      } else {
        gst_element_link_many (aconv, cfilter, asink, NULL);
        gate = aconv;
      }
    } else {
      if (adec) {
        if (!gst_element_link (adec, asink)) {
          NVGST_CRITICAL_MESSAGE_V
              ("failed to link  %s to %s, try --disable-anative",
              GST_ELEMENT_NAME (adec), GST_ELEMENT_NAME (asink));
          goto fail;
        }
        gate = adec;
      } else {
        gate = asink;
      }
    }

    pad = gst_element_get_static_pad (gate, "sink");
    if (pad) {
      gst_element_add_pad (bin, gst_ghost_pad_new ("sink", pad));
      gst_object_unref (pad);
    } else {
      NVGST_CRITICAL_MESSAGE_V ("failed to get sink pad from %s",
          GST_ELEMENT_NAME (gate));
    }

    if (!app->seekElement)
      app->seekElement = gst_object_ref (asink);
  }

done:
  return GST_ELEMENT (bin);

fail:
  if (bin) {
    gst_object_unref (bin);
    bin = NULL;
  }
  app->return_value = -1;
  goto done;
}


static void
cb_newpad (GstElement * decodebin, GstPad * pad, gpointer data)
{
  inAttrs *in = app->input;
  GstCaps *caps = gst_pad_query_caps (pad, NULL);
  const GstStructure *str = gst_caps_get_structure (caps, 0);
  const gchar *name = gst_structure_get_name (str);
  GstElement **dec = NULL;
  GstElement *sink = NULL;
  GstPad *sinkpad;
  gboolean create = FALSE;
  gint *stryp = NULL;
  {
    gchar *str = gst_caps_to_string (caps);
    NVGST_INFO_MESSAGE_V ("creating the pipe for \"%s\"", str);
    g_free (str);
  }

  if (!strncmp (name, "video", 5)) {
    if ((in->attrs.flags & NVGST_PLAY_FLAG_VIDEO) && multitrack_instance) {

      stryp = &app->vstream_select;
      if (app->vstream_select == -1)
        app->vstream_select = app->vstreams - 1;
      app->vpipe = create_video_pipeline (caps, pad);
      dec = &app->vpipe;
      create = TRUE;
      if (app->vstreams > 1)
        multitrack_instance = 0;
    } else if (!GUI) {
      sink = gst_element_factory_make ("fakesink", NULL);
      g_object_set (G_OBJECT (sink), "sync", in->attrs.sync, NULL);
      dec = &sink;
    }

  } else if (!strncmp (name, "image", 5)) {
    if ((in->attrs.flags & NVGST_PLAY_FLAG_VIDEO) && multitrack_instance) {
      stryp = &app->vstream_select;

      if (app->vstream_select == -1)
        app->vstream_select = app->vstreams - 1;
      app->vpipe = create_video_pipeline (caps, pad);
      dec = &app->vpipe;
      create = TRUE;
      if (app->vstreams > 1)
        multitrack_instance = 0;
    } else if (!GUI) {
      sink = gst_element_factory_make ("fakesink", NULL);
      g_object_set (G_OBJECT (sink), "sync", in->attrs.sync, NULL);
      dec = &sink;
    }

  } else if (!strncmp (name, "audio", 5)) {
    if (in->attrs.flags & NVGST_PLAY_FLAG_AUDIO) {
      stryp = &app->astream_select;
      if (app->astream_select == -1) {
        app->apipe = create_audio_pipeline (caps);
        app->astream_select = app->astreams + 1;
        dec = &app->apipe;
        create = TRUE;
      } else if (app->astreams == (app->astream_select - 1)) {
        app->apipe = create_audio_pipeline (caps);
        dec = &app->apipe;
        create = TRUE;
      }

      app->astreams++;
    } else if (!GUI) {
      sink = gst_element_factory_make ("fakesink", NULL);
      g_object_set (G_OBJECT (sink), "sync", in->attrs.sync, NULL);
      dec = &sink;
    }
  }

  if (dec && *dec && stryp) {
    if (g_object_is_floating (*dec)) {

      if (!strncmp (name, "image", 5) || !strncmp (name, "video", 5)) {
        if (gst_element_set_state (*dec, GST_STATE_READY) ==
            GST_STATE_CHANGE_FAILURE) {
          NVGST_CRITICAL_MESSAGE_V ("element: %s state change failure to %s",
              GST_ELEMENT_NAME (*dec),
              gst_element_state_get_name (GST_STATE_PAUSED));

        } else if ((app->disp.mDisplay && app->vsink
                && !app->attrs.disable_fullscreen) || GUI) {
          Window window;
          if (GUI) {
            window = (Window) CALL_GUI_FUNC (get_video_window);
          } else {
            nvgst_create_window (&app->disp, APPLICATION_NAME);
            app->x_event_thread = g_thread_new ("nvgst-window-event-thread",
                nvgst_x_event_thread, app);
            window = app->disp.window;
          }
          set_window_handle (window);

        }
      }
      if (gst_element_set_state (*dec, GST_STATE_PAUSED) ==
          GST_STATE_CHANGE_FAILURE) {
        NVGST_CRITICAL_MESSAGE_V ("element: %s state change failure to %s",
            GST_ELEMENT_NAME (*dec),
            gst_element_state_get_name (GST_STATE_PAUSED));
        gst_object_unref (*dec);
        *dec = NULL;
        if (app->attrs.aud_track == -1)
          *stryp = -1;
        else
          *stryp = -2;

        gst_element_post_message (GST_ELEMENT (app->pipeline),
            gst_message_new_application (GST_OBJECT (app->pipeline),
                gst_structure_new ("NvGstAppVideoBinFailure",
                    "message", G_TYPE_STRING,
                    "Video Pipeline state change to Paused failed", NULL)));

        goto done;
      }

      if (!gst_bin_add (GST_BIN (app->pipeline), *dec)) {
        NVGST_CRITICAL_MESSAGE_V ("could not add element: %s to pipeline",
            GST_ELEMENT_NAME (*dec));
        gst_object_unref (*dec);
        *dec = NULL;
        if (app->attrs.aud_track == -1)
          *stryp = -1;
        else
          *stryp = -2;

        goto done;
      }
    }

    sinkpad = gst_element_get_static_pad (*dec, "sink");
    if (sinkpad) {
      if (GST_PAD_LINK_FAILED (gst_pad_link (pad, sinkpad))) {
        NVGST_CRITICAL_MESSAGE_V
            ("could not link \"%s\" to the (decode+)render pipeline \"%s\"",
            name, GST_ELEMENT_NAME (*dec));
        gst_object_unref (sinkpad);
        gst_element_set_state (GST_ELEMENT (*dec), GST_STATE_NULL);
        gst_bin_remove (GST_BIN (app->pipeline), *dec);
        *dec = NULL;
        if (app->attrs.aud_track == -1)
          *stryp = -1;
        else
          *stryp = -2;

      } else
        gst_object_unref (sinkpad);
    } else {
      NVGST_CRITICAL_MESSAGE_V ("failed to get sink pad from %s",
          GST_ELEMENT_NAME (*dec));
      gst_element_set_state (GST_ELEMENT (*dec), GST_STATE_NULL);
      gst_bin_remove (GST_BIN (app->pipeline), *dec);
      *dec = NULL;
      if (app->attrs.aud_track == -1)
        *stryp = -1;
      else
        *stryp = -2;

    }

  } else {
    NVGST_WARNING_MESSAGE_V ("decoder pipeline for \"%s\" not created <%d>",
        name, create);

    if (create) {
      NVGST_CRITICAL_MESSAGE ("failed to create/activate the decode pipeline");
    }
  }
  if (((app->astreams > 1) || (app->vstreams > 1))) {
    g_print ("\n\nStream have %d audio tracks and %d video tracks. \n"
        "By default it is picking first track \n"
        "If want to switch track then give play a stream with an option \n"
        "--audio-track or --video-track track no.\n\n", app->astreams,
        app->vstreams);
  }

done:
  gst_caps_unref (caps);
}


static void
bin_element_added (GstElement * dbin, GstElement * element, gpointer * app)
{
  GstElementFactory *factory = gst_element_get_factory (element);
  const gchar *klass = gst_element_factory_get_klass (factory);
  if (strstr (klass, "Decode") && strstr (klass, "Video")) {
    g_object_set (G_OBJECT (element), "full-frame", FALSE, NULL);
    g_signal_handlers_disconnect_by_func (dbin, bin_element_added, app);
  }
  return;
}


static NvGstAutoplugSelectResult
autoplug_select (GstElement * dbin, GstPad * pad, GstCaps * caps,
    GstElementFactory * factory, gpointer data)
{
  inAttrs *in = app->input;
  NvGstAutoplugSelectResult ret = NVGST_AUTOPLUG_SELECT_TRY;
  const gchar *klass = gst_element_factory_get_klass (factory);

  if (strstr (klass, "Demux")) {
    app->found_demuxer = TRUE;
    if (!strcmp ((GST_OBJECT_NAME (factory)), "mpegtsdemux")) {
      g_signal_connect (dbin, "element-added", G_CALLBACK (bin_element_added),
          app);
    }
  }

  /* we are only interested in decoders */
  if (strstr (klass, "Decode")) {
    if ((strstr (klass, "Audio"))) {

      CALL_GUI_FUNC (set_decoder_caps, caps);

      if (in->attrs.flags & NVGST_PLAY_FLAG_AUDIO) {
        if (!in->audio_dec && !in->dbin_audio_decoders) {
          in->audio_dec = get_keys (caps, NVGST_AUDIO_DEC, app->sad);
          if (in->audio_dec) {
            in->dbin_audio_decoders = FALSE;
            ret = NVGST_AUTOPLUG_SELECT_EXPOSE;
          } else {
            in->dbin_audio_decoders = TRUE;
          }
        }
      } else {

        ret = NVGST_AUTOPLUG_SELECT_EXPOSE;
      }

      if (app->stats && app->pfData.file) {
        GstCaps *scaps = gst_caps_copy (caps);
        GstStructure *str = gst_caps_get_structure (scaps, 0);
        gst_structure_remove_field (str, "codec_data");
        g_fprintf (app->pfData.file, "Audio Codec: %s\n",
            gst_caps_to_string (scaps));
        gst_caps_unref (scaps);
      }

    } else if ((strstr (klass, "Video")) || (strstr (klass, "Image"))) {

      CALL_GUI_FUNC (set_decoder_caps, caps);

      if ((in->attrs.flags & NVGST_PLAY_FLAG_VIDEO) && app->vstreams < 1) {
        if (!in->video_dec && !in->dbin_video_decoders) {
          GstStructure *str = gst_caps_get_structure (caps, 0);
          const gchar *name = gst_structure_get_name (str);
          /* nvv4l2decoder does not support jpeg/png, h263, wmv format */
          if (!app->svd && g_strcmp0(name, "image/jpeg") &&
                           g_strcmp0(name, "image/png") &&
                           g_strcmp0(name, "video/x-wmv") &&
                           g_strcmp0(name, "video/x-h263"))
            app->svd = g_strconcat (NVGST_DEFAULT_VIDEO_DEC, NULL);
          in->video_dec = get_keys (caps, NVGST_VIDEO_DEC, app->svd);

          if (in->video_dec) {
            in->dbin_video_decoders = FALSE;
            ret = NVGST_AUTOPLUG_SELECT_EXPOSE;
          } else {
            in->dbin_video_decoders = TRUE;
            if (CALL_GUI_FUNC (skip_decoder, factory))
              ret = NVGST_AUTOPLUG_SELECT_SKIP;
          }
        }
      } else {
        ret = NVGST_AUTOPLUG_SELECT_EXPOSE;
      }

      /* image decoders under video class won't be considered, if considered, will race to EOS */
      if (strstr (klass, "Image") && !app->found_demuxer) {
        NVGST_DEBUG_MESSAGE ("standalone image display");
        app->image_eos = 1;
      }

      if (app->stats && app->pfData.file) {
        GstCaps *scaps = gst_caps_copy (caps);
        GstStructure *str = gst_caps_get_structure (scaps, 0);
        gst_structure_remove_field (str, "codec_data");
        g_fprintf (app->pfData.file, "Video Codec: %s\n",
            gst_caps_to_string (scaps));
        gst_caps_unref (scaps);
      }
      app->vstreams++;
    }
  }

  if (ret == NVGST_AUTOPLUG_SELECT_TRY) {
    gchar *str = gst_caps_to_string (caps);
    NVGST_DEBUG_MESSAGE_V ("%s: %s:\n\"%s\"", GST_OBJECT_NAME (factory), klass,
        str);
    g_free (str);
  }

  return ret;
}


static void
no_more_pads (GstElement * element, gpointer data)
{
  NVGST_DEBUG_MESSAGE_V ("last pad: by %s\n", GST_ELEMENT_NAME (element));
  app->no_more_pads = TRUE;

  if ((app->astream_select < 0 && app->vstream_select < 0)
      || ((app->astream_select > app->astreams)
          && (app->vstream_select > app->vstreams))) {
    GST_ELEMENT_ERROR (app->pipeline, STREAM, DECODE,
        ("The media stream is empty, i.e., it has no audio or video to play!"),
        (NULL));
  } else {
    if (app->astream_select == -2)
      NVGST_CRITICAL_MESSAGE ("Failed to create user selected audio track");
    else if (app->astream_select > app->astreams)
      NVGST_CRITICAL_MESSAGE
          ("Creation of audio pipeline failed : User selected audio track number is exceeding total number of audio tracks in the stream");

    if (app->vstream_select == -2)
      NVGST_CRITICAL_MESSAGE ("Failed to create user selected video track");
    else if (app->vstream_select > app->vstreams)
      NVGST_CRITICAL_MESSAGE
          ("Creation of video pipeline failed : User selected video track number is exceeding total number of video tracks in the stream");

  }
}


static void
on_pad_added (GstElement * element, GstPad * pad, gpointer data)
{
  inAttrs *in = app->input;
  GstPad *sinkpad;
  GstElement **dbin = NULL;
  GstStateChangeReturn rt = GST_STATE_CHANGE_SUCCESS;
  GstCaps *caps = gst_pad_query_caps (pad, NULL);
  const GstStructure *str = gst_caps_get_structure (caps, 0);
  const gchar *type = gst_structure_get_string (str, "media");

  if (g_strcmp0 (type, "audio")) {
    dbin = &app->vdbin;
  } else if (g_strcmp0 (type, "video")) {
    dbin = &app->adbin;
  } else {
    NVGST_WARNING_MESSAGE ("unknown rtp payload type");
  }

  g_mutex_lock (&app->dbin_lock);
  if (dbin) {
    if (!*dbin) {
      *dbin = gst_element_factory_make ("decodebin", NULL);
      if (!*dbin) {
        NVGST_CRITICAL_MESSAGE ("failed to create decodebin");
      }

      g_signal_connect (*dbin, "autoplug-select", G_CALLBACK (autoplug_select),
          app);
      if (GUI)
        g_signal_connect (*dbin, "autoplug-sort",
            G_CALLBACK (CALL_GUI_FUNC (get_autoplug_sort_callback)),
            GET_GUI_CTX ());
      g_signal_connect (*dbin, "pad-added", G_CALLBACK (cb_newpad), app);
      g_signal_connect (*dbin, "no-more-pads", G_CALLBACK (no_more_pads), app);
      g_object_set (G_OBJECT (*dbin), "use-buffering", in->attrs.use_buffering,
          "low-percent", in->attrs.low_percent, "high-percent",
          in->attrs.high_percent, "max-size-time", in->attrs.max_size_time,
          "max-size-bytes", in->attrs.max_size_bytes, "max-size-buffers",
          in->attrs.max_size_buffers, NULL);

      rt = gst_element_set_state (*dbin, GST_STATE_PLAYING);
      if (rt == GST_STATE_CHANGE_FAILURE) {
        NVGST_CRITICAL_MESSAGE_V ("pipeline state change failure to %s",
            gst_element_state_get_name (GST_STATE_PLAYING));
        gst_object_unref (*dbin);
        *dbin = NULL;
      }

      if (!gst_bin_add (GST_BIN (app->pipeline), *dbin)) {
        NVGST_CRITICAL_MESSAGE ("could not add decodebin to pipeline");
        gst_object_unref (*dbin);
        *dbin = NULL;
      }
    }

    sinkpad = gst_element_get_static_pad (*dbin, "sink");
    if (sinkpad) {
      if (GST_PAD_LINK_FAILED (gst_pad_link (pad, sinkpad))) {
        if (!gst_pad_is_linked (sinkpad)) {
          NVGST_CRITICAL_MESSAGE_V ("could not link source: %s to decodebin",
              GST_ELEMENT_NAME (element));
          gst_element_set_state (GST_ELEMENT (*dbin), GST_STATE_NULL);
          gst_bin_remove (GST_BIN (app->pipeline), *dbin);
          *dbin = NULL;
        } else {
          NVGST_WARNING_MESSAGE_V
              ("stream has multiple %s tracks; picking first one", type);
        }
        gst_object_unref (sinkpad);
      } else
        gst_object_unref (sinkpad);
    } else if (*dbin) {
      NVGST_CRITICAL_MESSAGE_V ("failed to get sink pad from %s",
          GST_ELEMENT_NAME (*dbin));
      gst_element_set_state (GST_ELEMENT (*dbin), GST_STATE_NULL);
      gst_bin_remove (GST_BIN (app->pipeline), *dbin);
      *dbin = NULL;
    }
  }
  g_mutex_unlock (&app->dbin_lock);

  gst_caps_unref (caps);
  return;
}


static void
reset_current_track (void)
{
  app->running = FALSE;

  if (app->image_eos) {
    if (g_main_context_find_source_by_id (NULL, app->image_eos))
      g_source_remove (app->image_eos);
    app->image_eos = 0;
  }

  if (app->pipeline) {
    GstStateChangeReturn ret;

    ret = gst_element_set_state (app->pipeline, GST_STATE_READY);
    g_assert (ret != GST_STATE_CHANGE_ASYNC);

    if (app->stats) {
      g_timer_stop (app->pfData.timer);
      display_current_fps (&app->pfData);

      if (g_main_context_find_source_by_id (NULL, app->pfData.dps_cb))
        g_source_remove (app->pfData.dps_cb);
      app->pfData.dps_cb = 0;
      if (app->pfData.file) {
        g_fprintf (app->pfData.file, "Total Running Time: %f seconds\n",
            g_timer_elapsed (app->pfData.timer, NULL));
        fflush (app->pfData.file);
      }
      g_timer_destroy (app->pfData.timer);
      app->pfData.timer = NULL;
    }

    g_usleep (500000);
  }

  g_mutex_lock (&app->window_lock);
  if (app->disp.window)
    nvgst_destroy_window (&app->disp);
  g_mutex_unlock (&app->window_lock);

  if (app->x_event_thread)
    g_thread_join (app->x_event_thread);
  app->x_event_thread = NULL;

  app->in_error = FALSE;
  app->got_eos = FALSE;
  app->no_more_pads = FALSE;
  app->found_demuxer = FALSE;
  app->is_live = FALSE;
  app->buffering = FALSE;
  app->image_eos = 0;
  app->pre_dbin_lp = FALSE;
  app->last_seek_time = 0;
  app->accum_time = 0;
  app->vstreams = 0;
  app->astreams = 0;

  return;
}


static void
destroy_current_track (void)
{
  inAttrs *in = app->input;
  if (multitrack_instance == 0)
    multitrack_instance = 1;

  app->running = FALSE;

  if (app->bus_id) {
    if (g_main_context_find_source_by_id (NULL, app->bus_id))
      g_source_remove (app->bus_id);
    app->bus_id = 0;
  }

  if (app->cmd_id) {
    if (g_main_context_find_source_by_id (NULL, app->cmd_id))
      g_source_remove (app->cmd_id);
    app->cmd_id = 0;
  }

  reset_current_track ();
  in->postpone = FALSE;

  app->cur_operation = NVGST_OPS_NONE;

  if (app->seekElement) {
    gst_object_unref (app->seekElement);
    app->seekElement = NULL;
  }

  if (app->vrender_pad) {
    gst_object_unref (app->vrender_pad);
    app->vrender_pad = NULL;
  }

  if (app->arender_pad) {
    gst_object_unref (app->arender_pad);
    app->arender_pad = NULL;
  }

  if (app->pipeline) {
    GstStateChangeReturn ret;
    CALL_GUI_FUNC (set_current_pipeline, NULL);
    ret = gst_element_set_state (app->pipeline, GST_STATE_NULL);
    g_assert (ret != GST_STATE_CHANGE_ASYNC);
    gst_object_unref (app->pipeline);
    app->pipeline = NULL;
    app->vpipe = NULL;
    app->apipe = NULL;
    app->source = NULL;
    app->vsink = NULL;
    app->asink = NULL;
    app->vdbin = NULL;
    app->adbin = NULL;
  }

  g_strfreev (in->audio_dec);
  in->audio_dec = NULL;

  g_strfreev (in->video_dec);
  in->video_dec = NULL;

  in->dbin_audio_decoders = FALSE;
  in->dbin_video_decoders = FALSE;
  app->astream_select = -1;
  app->vstream_select = -1;

  g_free (in->uri);
  in->uri = NULL;

  free_cmlist (&in->attrs, in->selfexpr);

  memset (in, 0, sizeof (inAttrs));

  return;
}


static void
get_uri_details (gint i)
{
  GKeyFile *kf = app->pathCfg;
  inAttrs *in = app->input;
  gchar **gp = app->uriGroups;
  gchar *str;
  static int loop_count = 0;

  app->unpause = FALSE;
  in->attrs = app->attrs;
  in->selfexpr = FALSE;
  in->operation_mode = NVGST_CMD_SCRIPT;

  if (app->uri) {
    in->attrs.repeats = in->attrs.repeats - loop_count;
    loop_count++;
    in->uri = g_strdup (app->uri);
  } else if (!kf) {
    in->uri = g_strdup (gp[i]);
  } else {
    gint key = 0, repeats = 0;
    gdouble keyd;
    gdouble start, dur;
    GError *err = NULL;

    in->uri = g_strdup (gp[i]);

    NVGST_INFO_MESSAGE_V ("\n\n\n loading the configuration for uri: %s\n",
        in->uri);

    str = g_key_file_get_string (kf, gp[i], NVCXPR, &err);
    if (err) {
      NVGST_DEBUG_MESSAGE_V ("error while reading %s from keyfile: %s", NVCXPR,
          err->message);
      g_error_free (err);
      err = NULL;
    } else if (str) {
      in->attrs.lplist_head = in->attrs.cmlist_head = NULL;
      if ('*' == *str) {
        g_free (str);
        str = get_random_cxpr ();
      }
      if (build_cmlist (str, &in->attrs)) {
        in->selfexpr = TRUE;
      } else {
        in->attrs = app->attrs;
        in->selfexpr = FALSE;
      }
      g_free (str);
    }

    key = g_key_file_get_integer (kf, gp[i], NVNOP, &err);
    if (err) {
      NVGST_DEBUG_MESSAGE_V ("error while reading %s from keyfile: %s", NVNOP,
          err->message);
      g_error_free (err);
      err = NULL;
    } else {
      if (key) {
        free_cmlist (&in->attrs, in->selfexpr);
        build_cmlist ("r", &in->attrs);
        in->selfexpr = TRUE;
      }
    }

    key = g_key_file_get_integer (kf, gp[i], NVSTARTPER, &err);
    if (err) {
      NVGST_DEBUG_MESSAGE_V ("error while reading %s from keyfile: %s",
          NVSTARTPER, err->message);
      g_error_free (err);
      err = NULL;
    } else {
      in->attrs.startPer = key;
    }

    start = g_key_file_get_double (kf, gp[i], NVSTART, &err);
    if (err) {
      NVGST_DEBUG_MESSAGE_V ("error while reading %s from keyfile: %s", NVSTART,
          err->message);
      g_error_free (err);
      err = NULL;
    } else {
      if (in->attrs.startPer)
        in->attrs.segment_start = ABS (start);
      else
        in->attrs.segment_start = ABS (start) * GST_SECOND;
    }

    dur = g_key_file_get_double (kf, gp[i], NVDURATION, &err);
    if (err) {
      NVGST_DEBUG_MESSAGE_V ("error while reading %s from keyfile: %s",
          NVDURATION, err->message);
      g_error_free (err);
      err = NULL;
    } else {
      in->attrs.segment_duration = ABS (dur) * GST_SECOND;
      if (!in->attrs.segment_duration)
        in->attrs.segment_duration = GST_CLOCK_TIME_NONE;
    }

    repeats = g_key_file_get_integer (kf, gp[i], NVREPEATS, NULL);
    repeats = repeats > 0 ? repeats : app->attrs.repeats;
    in->attrs.repeats = repeats;

    in->attrs.repeats = in->attrs.repeats - loop_count;
    loop_count++;
    if (repeats - loop_count == 0)
      loop_count = 0;

    key = g_key_file_get_integer (kf, gp[i], NVAUDIO, &err);
    if (err) {
      NVGST_DEBUG_MESSAGE_V ("error while reading %s from keyfile: %s", NVAUDIO,
          err->message);
      g_error_free (err);
      err = NULL;
    } else {
      if (key && app->have_alsa_sinks)
        in->attrs.flags |= NVGST_PLAY_FLAG_AUDIO;
      else
        in->attrs.flags &= ~NVGST_PLAY_FLAG_AUDIO;
    }

    key = g_key_file_get_integer (kf, gp[i], NVVIDEO, &err);
    if (err) {
      NVGST_DEBUG_MESSAGE_V ("error while reading %s from keyfile: %s", NVVIDEO,
          err->message);
      g_error_free (err);
      err = NULL;
    } else {
      if (key)
        in->attrs.flags |= NVGST_PLAY_FLAG_VIDEO;
      else
        in->attrs.flags &= ~NVGST_PLAY_FLAG_VIDEO;
    }

    key = g_key_file_get_integer (kf, gp[i], NVNATIVE_AUDIO, &err);
    if (err) {
      NVGST_DEBUG_MESSAGE_V ("error while reading %s from keyfile: %s",
          NVNATIVE_AUDIO, err->message);
      g_error_free (err);
      err = NULL;
    } else {
      if (key)
        in->attrs.flags |= NVGST_PLAY_FLAG_NATIVE_AUDIO;
      else
        in->attrs.flags &= ~NVGST_PLAY_FLAG_NATIVE_AUDIO;
    }

    key = g_key_file_get_integer (kf, gp[i], NVNATIVE_VIDEO, &err);
    if (err) {
      NVGST_DEBUG_MESSAGE_V ("error while reading %s from keyfile: %s",
          NVNATIVE_VIDEO, err->message);
      g_error_free (err);
      err = NULL;
    } else {
      if (key)
        in->attrs.flags |= NVGST_PLAY_FLAG_NATIVE_VIDEO;
      else
        in->attrs.flags &= ~NVGST_PLAY_FLAG_NATIVE_VIDEO;
    }

    key = g_key_file_get_integer (kf, gp[i], NVSYNC, &err);
    if (err) {
      NVGST_DEBUG_MESSAGE_V ("error while reading %s from keyfile: %s", NVSYNC,
          err->message);
      g_error_free (err);
      err = NULL;
    } else {
      in->attrs.sync = key;
    }

    key = g_key_file_get_integer (kf, gp[i], NVUSE_BUFFERING, &err);
    if (err) {
      NVGST_DEBUG_MESSAGE_V ("error while reading %s from keyfile: %s",
          NVUSE_BUFFERING, err->message);
      g_error_free (err);
      err = NULL;
    } else {
      in->attrs.sync = key;
    }

    key = g_key_file_get_integer (kf, gp[i], NVLOW_PERCENT, &err);
    if (err) {
      NVGST_DEBUG_MESSAGE_V ("error while reading %s from keyfile: %s",
          NVLOW_PERCENT, err->message);
      g_error_free (err);
      err = NULL;
    } else {
      in->attrs.low_percent = (ABS (key) < 99) ? ABS (key) : 10;
    }

    key = g_key_file_get_integer (kf, gp[i], NVHIGH_PERCENT, &err);
    if (err) {
      NVGST_DEBUG_MESSAGE_V ("error while reading %s from keyfile: %s",
          NVHIGH_PERCENT, err->message);
      g_error_free (err);
      err = NULL;
    } else {
      in->attrs.high_percent =
          (ABS (key) > in->attrs.low_percent) ? ABS (key) : 99;
    }

    keyd = g_key_file_get_double (kf, gp[i], NVMAX_SIZE_TIME, &err);
    if (err) {
      NVGST_DEBUG_MESSAGE_V ("error while reading %s from keyfile: %s",
          NVMAX_SIZE_TIME, err->message);
      g_error_free (err);
      err = NULL;
    } else {
      in->attrs.max_size_time = ABS (keyd) * GST_SECOND;
    }

    key = g_key_file_get_integer (kf, gp[i], NVMAX_SIZE_BYTES, &err);
    if (err) {
      NVGST_DEBUG_MESSAGE_V ("error while reading %s from keyfile: %s",
          NVMAX_SIZE_BYTES, err->message);
      g_error_free (err);
      err = NULL;
    } else {
      in->attrs.max_size_bytes = ABS (key);
    }

    key = g_key_file_get_integer (kf, gp[i], NVMAX_SIZE_BUFFERS, &err);
    if (err) {
      NVGST_DEBUG_MESSAGE_V ("error while reading %s from keyfile: %s",
          NVMAX_SIZE_BUFFERS, err->message);
      g_error_free (err);
      err = NULL;
    } else {
      in->attrs.max_size_buffers = ABS (key);
    }

    key = g_key_file_get_integer (kf, gp[i], NVIMAGE_DISPLAY_TIME, &err);
    if (err) {
      NVGST_DEBUG_MESSAGE_V ("error while reading %s from keyfile: %s",
          NVIMAGE_DISPLAY_TIME, err->message);
      g_error_free (err);
      err = NULL;
    } else {
      in->attrs.image_display_time = (ABS (key) > 1) ? ABS (key) : 2;
    }

    key = g_key_file_get_integer (kf, gp[i], NVTAGS, &err);
    if (err) {
      NVGST_DEBUG_MESSAGE_V ("error while reading %s from keyfile: %s", NVTAGS,
          err->message);
      g_error_free (err);
      err = NULL;
    } else {
      in->attrs.show_tags = key;
    }
  }
  app->astream_select = app->attrs.aud_track;
  app->vstream_select = app->attrs.vid_track;
}


static GstBusSyncReply
bus_sync_handler (GstBus * bus, GstMessage * msg, gpointer data)
{
  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS) {
    NVGST_DEBUG_MESSAGE ("got eos from pipeline <streaming thread>");
    app->got_eos = TRUE;
  }

  return GST_BUS_PASS;
}


static NvGstReturn
setup_track (void)
{
  GstBus *bus;
  GstElement *pipeline = NULL, *source = NULL, *dbin = NULL;
  inAttrs *input = app->input;
  NvGstReturn ret = NVGST_RET_SUCCESS;

  CALL_GUI_FUNC (setup_new_track, app->uriCount);

  if (!app->pipeline) {
    get_uri_details (app->uriCount - 1);

    if (input->attrs.flags & NVGST_PLAY_FLAG_PLAYBIN) {
      GstElement *vsink = NULL, *asink = NULL;
      gint buf_size;
      GstClockTime buf_time;
      gchar *uri = NULL;

      pipeline = gst_element_factory_make ("playbin", NULL);
      if (!pipeline) {
        NVGST_CRITICAL_MESSAGE ("failed to create playbin");
        goto fail;
      }
      app->pipeline = pipeline;

      CALL_GUI_FUNC (set_current_pipeline, pipeline);

      vsink = create_element (NULL, NVGST_VIDEO_SINK, app->svs, NULL);
      if (!vsink) {
        NVGST_WARNING_MESSAGE_V ("failed to create %s", NVGST_VIDEO_SINK);
      }
      app->vsink = vsink;

      asink = create_element (NULL, NVGST_AUDIO_SINK, app->sas, NULL);
      if (!asink) {
        NVGST_WARNING_MESSAGE_V ("failed to create %s", NVGST_AUDIO_SINK);
      }
      app->asink = asink;


      if (!g_str_has_prefix (input->uri, "file://")) {
        if (!(g_str_has_prefix (input->uri, "rtsp://") ||
                g_str_has_prefix (input->uri, "http://") ||
                g_str_has_prefix (input->uri, "udp://"))) {
          uri = g_strconcat ("file://", input->uri, NULL);
        } else
          uri = g_strdup (input->uri);
      } else
        uri = g_strdup (input->uri);

      if (input->attrs.use_buffering) {

        input->attrs.flags |= NVGST_PLAY_FLAG_BUFFERING;
      }
      buf_size =
          (input->attrs.max_size_bytes > 0) ? input->attrs.max_size_bytes : -1;

      buf_time = (GST_CLOCK_TIME_IS_VALID (input->attrs.max_size_time)
          && input->attrs.max_size_time) ? input->attrs.
          max_size_time : (GstClockTime) - 1;

      g_object_set (G_OBJECT (pipeline), "video-sink", vsink, "flags",
          input->attrs.flags & (NVGST_PLAY_FLAG_PLAYBIN - 1), "audio-sink",
          asink, "uri", uri, "buffer-size", buf_size, "buffer-duration",
          buf_time, NULL);

      g_free (uri);
      asink = NULL;
      app->seekElement = gst_object_ref (pipeline);
      app->no_more_pads = TRUE;
    } else {
      pipeline = gst_pipeline_new ("player");
      if (!pipeline) {
        NVGST_CRITICAL_MESSAGE ("failed to create pipeline");
        goto fail;
      }
      app->pipeline = pipeline;
      CALL_GUI_FUNC (set_current_pipeline, pipeline);

      if (g_str_has_prefix (input->uri, "http://")) {
        source = create_element (NULL, NVGST_HTTP_SRC, app->shttp, NULL);
      } else if (g_str_has_prefix (input->uri, "rtsp://")) {
        source = create_element (NULL, NVGST_RTSP_SRC, app->srtsp, NULL);
      } else if (g_str_has_prefix (input->uri, "udp://")) {
        source = create_element (NULL, NVGST_UDP_SRC, app->sudp, NULL);
      } else {
        source = create_element (NULL, NVGST_FILE_SRC, app->sfsrc, NULL);
      }

      if (!source) {
        NVGST_CRITICAL_MESSAGE ("failed to create source\n");
        goto fail;
      } else
        app->source = source;

      if (g_str_has_prefix (input->uri, "rtsp://")) {
        g_signal_connect (source, "pad-added", G_CALLBACK (on_pad_added), app);

        app->pre_dbin_lp = TRUE;

        g_object_set (G_OBJECT (source), "location", input->uri, NULL);
        g_object_set (G_OBJECT (source), "buffer-mode", BUFFER_MODE_SLAVE,
            NULL);

        if (!gst_bin_add (GST_BIN (pipeline), source)) {
          NVGST_CRITICAL_MESSAGE_V ("failed to add source: %s to pipeline",
              GST_ELEMENT_NAME (source));
          gst_object_unref (source);
          goto fail;
        }

      } else {
        if (g_str_has_prefix (input->uri, "udp://"))
          g_object_set (G_OBJECT (source), "uri", input->uri, NULL);
        else if (g_str_has_prefix (input->uri, "file://"))
          g_object_set (G_OBJECT (source), "location", input->uri + 7, NULL);
        else
          g_object_set (G_OBJECT (source), "location", input->uri, NULL);

        if (!gst_bin_add (GST_BIN (pipeline), source)) {
          NVGST_CRITICAL_MESSAGE_V ("failed to add source: %s to pipeline",
              GST_ELEMENT_NAME (source));
          gst_object_unref (source);
          goto fail;
        }

        dbin = gst_element_factory_make ("decodebin", NULL);
        if (!dbin) {
          NVGST_CRITICAL_MESSAGE ("failed to create decodebin");
          goto fail;
        }

        if (!gst_bin_add (GST_BIN (pipeline), dbin)) {
          NVGST_CRITICAL_MESSAGE ("failed to add decodebin to pipeline");
          gst_object_unref (dbin);
          goto fail;
        }
        g_signal_connect (dbin, "autoplug-select", G_CALLBACK (autoplug_select),
            app);
        if (GUI)
          g_signal_connect (dbin, "autoplug-sort",
              G_CALLBACK (CALL_GUI_FUNC (get_autoplug_sort_callback)),
              GET_GUI_CTX ());
        g_signal_connect (dbin, "pad-added", G_CALLBACK (cb_newpad), app);
        g_signal_connect (dbin, "no-more-pads", G_CALLBACK (no_more_pads), app);
        g_object_set (G_OBJECT (dbin), "use-buffering",
            input->attrs.use_buffering, "low-percent", input->attrs.low_percent,
            "high-percent", input->attrs.high_percent, "max-size-time",
            input->attrs.max_size_time, "max-size-bytes",
            input->attrs.max_size_bytes, "max-size-buffers",
            input->attrs.max_size_buffers, NULL);

        if (!gst_element_link (source, dbin)) {
          NVGST_CRITICAL_MESSAGE_V ("failed to link source %s to decodebin",
              GST_ELEMENT_NAME (source));
          goto fail;
        }
      }

    }
    app->adbin = app->vdbin = dbin;

    bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
    gst_bus_set_sync_handler (bus, NULL, app, NULL);
    gst_bus_set_sync_handler (bus, bus_sync_handler, app, NULL);
    app->bus_id = gst_bus_add_watch (bus, bus_call, app);
    gst_object_unref (bus);

  } else {
    pipeline = app->pipeline;
  }

  if (app->stats && app->pfData.file) {
    gchar *u = input->uri;
    g_fprintf (app->pfData.file, "\n\n\n%s\n", input->uri);
    while (*u++ != '\0')
      fputc ('-', app->pfData.file);
    fputc ('\n', app->pfData.file);
  }

  if (gst_element_set_state (pipeline, GST_STATE_READY) !=
      GST_STATE_CHANGE_SUCCESS) {
    ret = NVGST_RET_ERR;
  } else
    app->cmd_id = g_timeout_add (0, on2_input, NULL);


done:
  return ret;

fail:
  ret = NVGST_RET_ERR;
  goto done;
}


static void
_intr_handler (int signum)
{
  struct sigaction action;

  g_print ("User Interrupted.. \n");
  app->return_value = -1;

  memset (&action, 0, sizeof (action));
  action.sa_handler = SIG_DFL;

  sigaction (SIGINT, &action, NULL);

  cintr = TRUE;
}


static gboolean
check_for_interrupt (gpointer data)
{
  if (cintr) {
    cintr = FALSE;

    if (app->pipeline) {
      gst_element_post_message (GST_ELEMENT (app->pipeline),
          gst_message_new_application (GST_OBJECT (app->pipeline),
              gst_structure_new ("NvGstAppInterrupt",
                  "message", G_TYPE_STRING, "Pipeline interrupted", NULL)));
    } else {
      /** Hackish **/
      GstMessage *msg = gst_message_new_custom (GST_MESSAGE_APPLICATION,
          NULL, gst_structure_new ("NvGstAppInterrupt",
              "message", G_TYPE_STRING, "Pipeline interrupted", NULL));
      bus_call (NULL, msg, NULL);
      gst_message_unref (msg);
    }

    return FALSE;
  }

  return TRUE;
}


static void
_intr_setup (void)
{
  struct sigaction action;

  memset (&action, 0, sizeof (action));
  action.sa_handler = _intr_handler;

  sigaction (SIGINT, &action, NULL);
}


static gboolean
parse_spec (const gchar * option_name, const gchar * value,
    gpointer data, GError ** error)
{
  if (!g_strcmp0 ("--use-playbin", option_name)) {
    app->attrs.flags |= NVGST_PLAY_FLAG_PLAYBIN;
  } else if (!g_strcmp0 ("--no-audio", option_name)) {
    app->attrs.flags &= ~NVGST_PLAY_FLAG_AUDIO;
  } else if (!g_strcmp0 ("--no-video", option_name)) {
    app->attrs.flags &= ~NVGST_PLAY_FLAG_VIDEO;
  } else if (!g_strcmp0 ("--disable-anative", option_name)) {
    app->attrs.flags &= ~NVGST_PLAY_FLAG_NATIVE_AUDIO;
  } else if (!g_strcmp0 ("--disable-vnative", option_name)) {
    app->attrs.flags &= ~NVGST_PLAY_FLAG_NATIVE_VIDEO;
  } else if (!g_strcmp0 ("--sas", option_name)) {
    g_free (app->sas);
    app->sas = g_strdup (value);

  } else if (!g_strcmp0 ("--svs", option_name)) {
    g_free (app->svs);
    app->svs = g_strdup (value);

  } else if (!g_strcmp0 ("--sac", option_name)) {
    g_free (app->sac);
    app->sac = g_strdup (value);

  } else if (!g_strcmp0 ("--svc", option_name)) {
    g_free (app->svc);
    app->svc = g_strdup (value);

  } else if (!g_strcmp0 ("--shttp", option_name)) {
    g_free (app->shttp);
    app->shttp = g_strdup (value);

  } else if (!g_strcmp0 ("--srtsp", option_name)) {
    g_free (app->srtsp);
    app->srtsp = g_strdup (value);

  } else if (!g_strcmp0 ("--sfsrc", option_name)) {
    g_free (app->sfsrc);
    app->sfsrc = g_strdup (value);

  } else if (!g_strcmp0 ("--sad", option_name)) {
    g_free (app->sad);
    app->sad = g_strdup (value);

  } else if (!g_strcmp0 ("--svd", option_name)) {
    g_free (app->svd);
    app->svd = g_strdup (value);
  }

  return TRUE;
}

static int
kbhit (void)
{
  struct timeval tv;
  fd_set rdfs;

  tv.tv_sec = 0;
  tv.tv_usec = 300000;

  FD_ZERO (&rdfs);
  FD_SET (STDIN_FILENO, &rdfs);

  select (STDIN_FILENO + 1, &rdfs, NULL, NULL, &tv);
  return FD_ISSET (STDIN_FILENO, &rdfs);
}

static void
changemode (int dir)
{
  static struct termios oldt, newt;

  if (dir == 1) {
    tcgetattr (STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON);
    tcsetattr (STDIN_FILENO, TCSANOW, &newt);
  } else
    tcsetattr (STDIN_FILENO, TCSANOW, &oldt);
}

static gboolean
on2_input (gpointer data)
{
  on_input (NULL, 0, data);

  return FALSE;
}

static void
nvgst_handle_xevents ()
{
  XEvent e;
  Atom wm_delete;
  displayCtx *dpyCtx = &app->disp;

  /* Handle Display events */
  while (XPending (dpyCtx->mDisplay)) {
    XNextEvent (dpyCtx->mDisplay, &e);
    switch (e.type) {
      case ClientMessage:
        wm_delete = XInternAtom (dpyCtx->mDisplay, "WM_DELETE_WINDOW", 1);
        if (wm_delete != None && wm_delete == (Atom) e.xclient.data.l[0]) {
          GST_ELEMENT_ERROR (app->pipeline, RESOURCE, NOT_FOUND,
              ("Output window was closed"), (NULL));
        }
        if (app->attrs.loop_forever)
          app->attrs.loop_forever = 0;
    }
  }
}

static gpointer
nvgst_x_event_thread (gpointer data)
{
  g_mutex_lock (&app->window_lock);
  while (app->disp.window) {
    nvgst_handle_xevents ();
    g_mutex_unlock (&app->window_lock);
    g_usleep (G_USEC_PER_SEC / 20);
    g_mutex_lock (&app->window_lock);
  }
  g_mutex_unlock (&app->window_lock);
  return NULL;
}


static gpointer
on_input_thread (gpointer data)
{
  GQueue *que = g_queue_new ();
  gchar *buffer = NULL;
  int i = 0;

  changemode (1);

  while (!trd_exit) {
    if (kbhit ()) {
      if (buffer == NULL)
        buffer = g_malloc (256);

      buffer[i] = getchar ();

      if (buffer[i] == 27) {
        i = 0;
        /* TODO: check the ip queue first */
        NVGST_DEBUG_MESSAGE ("\nESC: awaiting one more press to quit\n");
        buffer[0] = getchar ();
        if (buffer[0] == 91) {
          buffer[0] = getchar ();
          switch (buffer[0]) {
            case 65:
              buffer[0] = ']';
              break;
            case 66:
              buffer[0] = '[';
              break;
            case 67:
              buffer[0] = '>';
              break;
            case 68:
              buffer[0] = '<';
              break;
          }
        } else if (buffer[0] == 27) {
          buffer[0] = 'q';
        }
      }

      if (buffer[i] != 127) {
        if (buffer[i] == 10 ||
            (!i && (buffer[0] == 'h' || buffer[0] == 'q'
                    || buffer[0] == 'c' || buffer[0] == 'r'
                    || buffer[0] == 'p' || buffer[0] == 'z'
                    || buffer[0] == '[' || buffer[0] == ']'
                    || buffer[0] == '<' || buffer[0] == '>'))) {
          if (buffer[i] == 10)
            buffer[i] = 0;
          else
            buffer[++i] = 0;
          i = 0;

          if (g_queue_is_empty (que)) {
            g_queue_push_tail (que, buffer);
            g_timeout_add (20, on2_input, que);
          }

          buffer = NULL;

        } else
          i++;

      } else {
        if (i > 0)
          i--;
      }
    }
  }

  changemode (0);

  while ((buffer = g_queue_pop_head (que))) {
    g_free (buffer);
  }

  g_queue_free (que);

  return NULL;
}

static void
build_hash_table (GHashTable * htable)
{
  g_hash_table_insert (htable, NVGST_AUDIO_CONV, NVGST_DEFAULT_AUDIO_CONV);
  g_hash_table_insert (htable, NVGST_VIDEO_CONV, NVGST_DEFAULT_VIDEO_CONV);
  g_hash_table_insert (htable, NVGST_AUDIO_SINK, NVGST_DEFAULT_AUDIO_SINK);
  g_hash_table_insert (htable, NVGST_VIDEO_SINK, NVGST_DEFAULT_VIDEO_SINK);
  g_hash_table_insert (htable, NVGST_FILE_SRC, NVGST_DEFAULT_FILE_SRC);
  g_hash_table_insert (htable, NVGST_RTSP_SRC, NVGST_DEFAULT_RTSP_SRC);
  g_hash_table_insert (htable, NVGST_HTTP_SRC, NVGST_DEFAULT_HTTP_SRC);
  g_hash_table_insert (htable, NVGST_UDP_SRC, NVGST_DEFAULT_UDP_SRC);
}

static NvGstReturn
get_next_command (attrs_s * t, gchar * buffer, gint buf_size, gboolean reuse)
{
  NvGstReturn ret = NVGST_RET_SUCCESS;
  GList *cml = t->cmlist;

  if (cml == NULL) {
    ret = NVGST_RET_END;

  } else {
    sCm *cm = (sCm *) cml->data;
    strncpy (buffer, cm->id, buf_size - 1);
  }

  if (!reuse) {
    if (cml) {
      sCm *cm = (sCm *) cml->data;

      if (cm->list) {
        GList *list = cm->list;

        do {
          sLp *lp = list->data;

          if (++lp->x > lp->n) {
            lp->x = 1;
            list = g_list_next (list);
            if (list == NULL) {
              cml = g_list_next (cml);
            }

          } else {
            cml = lp->c;
            list = NULL;
          }
        } while (list);

      } else {
        cml = g_list_next (cml);
      }
    }
  }

  t->cmlist = cml;

  return ret;
}


static guint
parse_symbol (GScanner * scanner, attrs_s * t)
{
  guint symbol, next_token;
  GList *l_lplist, *l_cmlist;
  sLp *lp;
  sCm *cm;

  /* expect a valid symbol */
  g_scanner_get_next_token (scanner);
  symbol = scanner->token;

  switch (symbol) {
    case G_TOKEN_LEFT_CURLY:{
      lp = g_new0 (sLp, 1);
      lp->n = last_n;
      lp->x = 1;
      lp->c = NULL;
      last_n = 1;
      t->lplist_head = g_list_append (t->lplist_head, lp);

      next_token = g_scanner_peek_next_token (scanner);
      if (next_token) {
        //TODO: return G_TOKEN_ERROR;
      }
    }
      break;

    case G_TOKEN_RIGHT_CURLY:{
      if (t->lplist_head) {
        l_lplist = g_list_last (t->lplist_head);
        if (l_lplist && t->cmlist_head) {
          l_cmlist = g_list_last (t->cmlist_head);

          if (l_cmlist) {
            sCm *cm = (sCm *) l_cmlist->data;
            cm->list = g_list_append (cm->list, l_lplist->data);
            l_lplist->data = NULL;
            t->lplist_head = g_list_delete_link (t->lplist_head, l_lplist);
          } else {
            return G_TOKEN_ERROR;
          }
        } else {
          return G_TOKEN_ERROR;
        }
      } else {
        return G_TOKEN_ERROR;
      }

      next_token = g_scanner_peek_next_token (scanner);
      if (next_token) {
        //TODO: return G_TOKEN_ERROR;
      }
    }
      break;

    case G_TOKEN_FLOAT:{
      last_n = scanner->value.v_float;
      next_token = g_scanner_peek_next_token (scanner);
      if (next_token) {
        //TODO:  return G_TOKEN_ERROR;
      }
    }
      break;

    case G_TOKEN_IDENTIFIER:{
      cm = g_new0 (sCm, 1);
      cm->id = g_strdup (scanner->value.v_identifier);

      t->cmlist_head = g_list_append (t->cmlist_head, cm);
      l_cmlist = g_list_last (t->cmlist_head);

      if (t->lplist_head) {
        l_lplist = g_list_last (t->lplist_head);
        while (l_lplist && ((sLp *) l_lplist->data)->c == NULL) {
          ((sLp *) l_lplist->data)->c = l_cmlist;
          l_lplist = g_list_previous (l_lplist);
        }
      }

      next_token = g_scanner_peek_next_token (scanner);
      if (next_token) {
        //TODO: return G_TOKEN_ERROR;
      }
    }
      break;

    default:
      return G_TOKEN_ERROR;
  }

  return G_TOKEN_NONE;
}


static void
_freelp_func (gpointer data, gpointer udata)
{
  g_free (data);
  return;
}


static void
_freecm_func (gpointer data, gpointer udata)
{
  sCm *cm = (sCm *) data;

  g_free (cm->id);
  g_free (cm);

  return;
}


static void
free_cmlist (attrs_s * attrs, gboolean force)
{
  if (force) {
    g_list_foreach (attrs->cmlist_head, _freecm_func, NULL);
    g_list_foreach (attrs->lplist_head, _freelp_func, NULL);
  }

  attrs->cmlist_head = NULL;
  attrs->lplist_head = NULL;

  attrs->cmlist = NULL;
  attrs->lplist = NULL;

  return;
}


static gboolean
build_cmlist (gchar * text, attrs_s * attrs)
{
  GScanner *data;
  guint result;
  gboolean res = TRUE;

  data = g_scanner_new (NULL);

  data->config->numbers_2_int = TRUE;
  data->config->int_2_float = TRUE;
  data->config->scan_identifier_1char = TRUE;
  data->config->symbol_2_token = TRUE;

  data->config->cset_skip_characters =
      g_strconcat (data->config->cset_skip_characters, " ", NULL);
  data->config->cset_identifier_nth =
      g_strconcat (data->config->cset_identifier_nth, ".", "-", "[", "]",
      "<", ">", NULL);
  data->config->cset_identifier_first =
      g_strconcat (data->config->cset_identifier_first, "[", "]", "<", ">",
      NULL);

  g_scanner_input_text (data, text, strlen (text));

  data->input_name = text;

  do {
    result = parse_symbol (data, attrs);

    g_scanner_peek_next_token (data);
  }
  while (result == G_TOKEN_NONE &&
      data->next_token != G_TOKEN_EOF && data->next_token != G_TOKEN_ERROR);

  if (result != G_TOKEN_NONE) {
    g_scanner_unexp_token (data, result, NULL, "symbol", NULL, NULL, TRUE);
    res = FALSE;
    /* TODO free all list */
  }

  attrs->lplist = attrs->lplist_head;
  attrs->cmlist = attrs->cmlist_head;

  /* finish parsing */
  g_scanner_destroy (data);

#if 1
  {
    int k = 0;
    GList *cml = attrs->cmlist;
    while (cml) {
      sCm *cm = (sCm *) cml->data;

      printf ("%d %s\n", ++k, cm->id);

      if (cm->list) {
        GList *list = cm->list;

        do {
          sLp *lp = list->data;

          if (++lp->x > lp->n) {
            lp->x = 1;
            list = g_list_next (list);
            if (list == NULL) {
              cml = g_list_next (cml);
            }

          } else {
            cml = lp->c;
            list = NULL;
          }
        } while (list);

      } else {
        cml = g_list_next (cml);
      }
    }
  }
#endif
  return res;
}

gchar *
get_random_cxpr (void)
{
  GRand *cxpr_rand = NULL;
  gint32 cmd_num = 0;
  gdouble time_num = 0;
  GString *rand_string = NULL;

  /*Local macros defines only for this function */
#define MAX_RANDOM_STR_LENGTH 100
#define MIN_SEEK_RANGE 0.0
#define MAX_SEEK_RANGE 50.0

#define MIN_TIME_RANGE 5.0
#define MAX_TIME_RANGE 20.0

  cxpr_rand = g_rand_new ();
  if (!cxpr_rand) {
    NVGST_CRITICAL_MESSAGE ("Error in allocating memory !!! ");
    return NULL;
  }

  rand_string = g_string_new ("r");
  if (!rand_string) {
    NVGST_CRITICAL_MESSAGE ("Error in allocating memory !!! ");
    return NULL;
  }


  while (rand_string->len < MAX_RANDOM_STR_LENGTH) {

    rand_string = g_string_append (rand_string, " ");
    cmd_num = g_rand_int_range (cxpr_rand, 1, 11);

    switch (cmd_num) {
      case 1:                  //maps to resume
        rand_string = g_string_append (rand_string, "r");
        break;
      case 2:                  //maps to pause
        rand_string = g_string_append (rand_string, "p");
        break;

      case 3:                  //maps to stop
        rand_string = g_string_append (rand_string, "z");
        break;

      case 4:                  //maps to play-seek (r s<val> w<val>)
        time_num =
            g_rand_double_range (cxpr_rand, MIN_SEEK_RANGE, MAX_SEEK_RANGE);
        g_string_append_printf (rand_string, "r s%.2f", time_num);

        time_num =
            g_rand_double_range (cxpr_rand, MIN_TIME_RANGE, MAX_TIME_RANGE);
        g_string_append_printf (rand_string, " w%.2f", time_num);
        break;

      case 5:                  //maps to wait w<val>
        time_num =
            g_rand_double_range (cxpr_rand, MIN_TIME_RANGE, MAX_TIME_RANGE);
        g_string_append_printf (rand_string, "w%.2f", time_num);
        break;

      case 6:                  //maps to play-seek-percentage (r v<val> w<val>)
        time_num = g_rand_double_range (cxpr_rand, 0.0, 100.0);
        g_string_append_printf (rand_string, "r v%.2f", time_num);

        time_num =
            g_rand_double_range (cxpr_rand, MIN_TIME_RANGE, MAX_TIME_RANGE);
        g_string_append_printf (rand_string, " w%.2f", time_num);
        break;

      case 7:                  //maps to absolute seek s<val>
        time_num =
            g_rand_double_range (cxpr_rand, MIN_SEEK_RANGE, MAX_SEEK_RANGE);
        g_string_append_printf (rand_string, "s%.2f", time_num);
        break;

      case 8:                  //maps to percentage seek v<val>
        time_num = g_rand_double_range (cxpr_rand, 0.0, 100.0);
        g_string_append_printf (rand_string, "v%.2f", time_num);
        break;

      case 9:                  //maps to >
        rand_string = g_string_append (rand_string, ">");
        break;

      case 10:                 //maps to <
        rand_string = g_string_append (rand_string, "<");
        break;

      default:
        continue;
    }
  }

  /*End it with r */
  rand_string = g_string_append (rand_string, " r");

  NVGST_INFO_MESSAGE_V ("Random expression generated is %s\n",
      rand_string->str);

  g_rand_free (cxpr_rand);
  return g_string_free (rand_string, FALSE);
}

void
get_elem_cfg (gchar * file)
{
  GError *error = NULL;

  g_free (app->elem_file);
  app->elem_file = NULL;

  if (app->elemCfg)
    g_key_file_free (app->elemCfg);
  app->elemCfg = NULL;

  if (file) {
    const GKeyFileFlags flags = G_KEY_FILE_KEEP_COMMENTS
        | G_KEY_FILE_KEEP_TRANSLATIONS;
    app->elemCfg = g_key_file_new ();
    if (!g_key_file_load_from_file (app->elemCfg, file, flags, &error)) {
      NVGST_WARNING_MESSAGE_V ("failed to load elem file: err: %s",
          error->message);
      g_error_free (error);
      g_key_file_free (app->elemCfg);
      app->elemCfg = NULL;
      app->return_value = -1;
    }

    g_strfreev (app->elem_gps);
    app->elem_gps = g_key_file_get_groups (app->elemCfg, NULL);
    app->elem_file = strdup (file);
  }
}

int
main (int argc, char *argv[])
{
  char stats[50];
  GOptionContext *ctx = NULL;
  GOptionGroup *group = NULL;
  GError *error = NULL;
  char *alsa_device = NULL;

  app = &sapp;
  memset (app, 0, sizeof (appCtx));
  app->extra_options = g_strdup ("Runtime Commands:\n \
      " "  q                                quit the application\n \
      " "  h                                print help\n \
      "
      "  Up Key, ]                        goto next track\n \
      "
      "  c                                restart current track\n \
      "
      "  Down Key, [                      goto previous track\n \
      "
      "  spos                             query for position\n \
      " "  sdur                             query for duration\n \
      " "  s<val>                           seek to <val> position in seconds, eg \"s5.120\"\n \
      " "  v<val>                           seek to <val> percent of the duration, eg \"v54\"\n \
      " "  f<val>                           seek by <val> seconds, relative to current position eg \"f23.901\"\n \
      "
      "  Left Key, <                      seek backwards by 10 seconds\n \
      "
      "  Right Key, >                     seek forward by 10 seconds\n \
      "
      "  p                                pause playback\n \
      " "  r                                start/resume the playback\n \
      " "  z                                stop the playback\n \
      " "  i:<val>                          enter a single URI\n");

  ctx = g_option_context_new ("Nvidia GStreamer Model Test");
  group = g_option_group_new ("Cotigao", NULL, NULL, NULL, NULL);
  g_option_group_add_entries (group, entries);
  g_option_context_set_description (ctx, app->extra_options);
  g_option_context_set_main_group (ctx, group);
  g_option_context_add_group (ctx, gst_init_get_option_group ());

  app->attrs.flags = NVGST_PLAY_FLAG_AUDIO;
  app->attrs.flags |= NVGST_PLAY_FLAG_VIDEO;
  app->attrs.flags |= NVGST_PLAY_FLAG_NATIVE_AUDIO;
  app->attrs.flags |= NVGST_PLAY_FLAG_NATIVE_VIDEO;
  app->attrs.repeats = 1;
  app->attrs.segment_duration = GST_CLOCK_TIME_NONE;
  app->attrs.low_percent = 10;
  app->attrs.high_percent = 99;
  app->attrs.image_display_time = 5;
  app->stealth_mode = FALSE;
  app->bg_mode = FALSE;
  app->disable_dpms = FALSE;
  app->disp.mDisplay = NULL;
  app->attrs.aud_track = -1;
  app->attrs.vid_track = -1;
  app->attrs.disable_fullscreen = FALSE;
  app->version = FALSE;
  g_mutex_init (&app->dbin_lock);
  /* Default permittable frames droped percentage is 2 incase not specified at run-time */
  app->attrs.drop_threshold_pct = 2;

  g_mutex_init (&app->window_lock);

  if (!g_option_context_parse (ctx, &argc, &argv, &error)) {
    g_option_context_free (ctx);
    g_print ("ERROR-<%d>: %s\n", (int) strlen (app->extra_options),
        error->message);
    goto done;
  }

  if (app->version) {
    g_print ("\nGstreamer Version ==> %s\n\n", gst_version_string ());
    goto done;
  }

  alsa_device = nvgst_asound_get_device ();
  if (!alsa_device) {
    g_print
        ("No audio playback devices found. Audio playback through alsa has been disabled\n");
    if ((app->sas && strstr (app->sas, "alsasink")) || (!app->sas
            && !strncmp (NVGST_DEFAULT_AUDIO_SINK, "alsasink", 8))) {
      app->attrs.flags &= ~NVGST_PLAY_FLAG_AUDIO;
    }
    app->have_alsa_sinks = FALSE;
  } else if (g_strcmp0 (alsa_device, "default")) {
    gchar sas[256];
    if (app->sas && strstr (app->sas, "alsasink")
        && !strstr (app->sas, "device")) {
      g_sprintf (sas, "%s # device = %s", app->sas, alsa_device);
      g_free (app->sas);
      app->sas = g_strdup (sas);
    } else if (!app->sas && !strncmp (NVGST_DEFAULT_AUDIO_SINK, "alsasink", 8)) {
      g_sprintf (sas, "%s # device = %s", NVGST_DEFAULT_AUDIO_SINK,
          alsa_device);
      app->sas = g_strdup (sas);
    }
    free (alsa_device);
    app->have_alsa_sinks = TRUE;
  }

  app->disp.mDisplay = nvgst_x11_init (&app->disp);

  g_set_application_name (APPLICATION_NAME);

  g_print ("%s\n", app->extra_options);

  g_option_context_free (ctx);

  app->input = g_malloc0 (sizeof (inAttrs));

  loop = g_main_loop_new (NULL, FALSE);

  if (app->stats) {
    if (app->stats_file)
      app->pfData.file = fopen (app->stats_file, "w");
    else {
      snprintf (stats, sizeof(stats)-1, "gst_statistics_%ld.txt", (long) getpid());
      stats[sizeof (stats)-1] = '\0';
      app->pfData.file = fopen (stats, "w");
    }
    if (app->pfData.file == NULL) {
      g_print ("File can not be opened for stats : %s\n", strerror (errno));
      app->return_value = -1;
      goto done;
    }
  }

  if (app->disable_dpms && app->disp.mDisplay) {
    saver_off (&app->disp);
  }

  if (app->uri) {
    app->uriTotal = 1;

  } else if (urifile) {
    const GKeyFileFlags flags =
        G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS;

    app->pathCfg = g_key_file_new ();
    if (!g_key_file_load_from_file (app->pathCfg, urifile, flags, &error)) {
      NVGST_WARNING_MESSAGE_V ("failed to load uri file: err: %s",
          error->message);
      g_error_free (error);
      g_key_file_free (app->pathCfg);
      app->pathCfg = NULL;
      app->return_value = -1;
      goto done;
    } else {
      app->uriGroups = g_key_file_get_groups (app->pathCfg, &app->uriTotal);
    }
  }

  g_free (urifile);

  if (elemfile) {
    get_elem_cfg (elemfile);
    g_free (elemfile);
  }

  timeout_id = g_timeout_add (400, check_for_interrupt, app);

  app->astream_select = app->attrs.aud_track;
  app->vstream_select = app->attrs.vid_track;
  app->attrs.repeats = app->attrs.repeats > 0 ? app->attrs.repeats : 1;
  app->attrs.sync = !app->attrs.sync;
  app->attrs.low_percent =
      (ABS (app->attrs.low_percent) < 99) ? ABS (app->attrs.low_percent) : 10;
  app->attrs.high_percent =
      (ABS (app->attrs.high_percent) >
      app->attrs.low_percent) ? ABS (app->attrs.high_percent) : 99;
  app->attrs.max_size_time = ABS (max_size_time) * GST_SECOND;
  app->attrs.max_size_bytes = ABS (app->attrs.max_size_bytes);
  app->attrs.max_size_buffers = ABS (app->attrs.max_size_buffers);
  app->attrs.image_display_time =
      app->attrs.image_display_time > 4 ? app->attrs.image_display_time : 5;
  app->attrs.segment_duration = ABS (segment_duration) * GST_SECOND;
  if (!app->attrs.segment_duration)
    app->attrs.segment_duration = GST_CLOCK_TIME_NONE;
  if (app->attrs.startPer)
    app->attrs.segment_start = ABS (segment_start);
  else
    app->attrs.segment_start = ABS (segment_start) * GST_SECOND;


  if (cxpr) {
    if ('*' == *cxpr) {
      g_free (cxpr);
      cxpr = get_random_cxpr ();
    }
    build_cmlist (cxpr, &app->attrs);
    g_free (cxpr);
  } else {
    g_assert (build_cmlist ("r", &app->attrs));
  }


  _intr_setup ();

  app->htable = g_hash_table_new (g_str_hash, g_str_equal);
  build_hash_table (app->htable);
  if (!app->bg_mode)
    trd = g_thread_new ("on-input-thread", on_input_thread, app);

  CALL_GUI_FUNC (init, argc, argv);

  /* Start rolling! */
  g_idle_add (goto_next_track, app);

  NVGST_INFO_MESSAGE ("iterating...");

  g_main_loop_run (loop);

  /* Out of the main loop, clean up nicely */
  CALL_GUI_FUNC (destroy);

  destroy_current_track ();

  g_print ("Playback completed!\n");
done:

  if (app->pfData.file)
    fclose (app->pfData.file);

  if (app->pathCfg)
    g_key_file_free (app->pathCfg);

  if (app->elemCfg) {
    g_key_file_free (app->elemCfg);

    g_strfreev (app->elem_gps);
  }

  g_free (app->uri);

  g_strfreev (app->uriGroups);

  app->astream_select = -1;
  app->vstream_select = -1;

  g_free (app->svd);
  g_free (app->sad);
  g_free (app->svc);
  g_free (app->sac);
  g_free (app->svs);
  g_free (app->sas);
  g_free (app->shttp);
  g_free (app->srtsp);
  g_free (app->sudp);
  g_free (app->sfsrc);

  if (app->attrs.cmlist)
    free_cmlist (&app->attrs, TRUE);

  if (app->htable)
    g_hash_table_unref (app->htable);

  g_free (app->extra_options);
  g_free (app->input);

  if (loop)
    g_main_loop_unref (loop);

  if (app->disable_dpms)
    saver_on (&app->disp);

  if (app->disp.mDisplay)
    nvgst_x11_uninit (&app->disp);

  g_mutex_clear (&app->window_lock);
  g_mutex_clear (&app->dbin_lock);

  g_print ("Application will now exit!\n");

  return ((app->return_value == -1) ? -1 : 0);
}
