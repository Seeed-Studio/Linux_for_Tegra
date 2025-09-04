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

#include <dlfcn.h>
#include <unistd.h>
#include "nvgstcapture.h"

#define EGL_PRODUCER_LIBRARY "libnveglstreamproducer.so"

#ifdef WITH_STREAMING
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include "gstnvrtspserver.h"
static NvGstRtspFunctions nvgst_rtsp_functions;

static GstFlowReturn
rtsp_video_appsink_new_sample (GstAppSink * appsink, gpointer user_data);
#endif

static gboolean check_capture_params (void);
gboolean create_capture_pipeline (void);
static gboolean create_native_capture_pipeline (void);
void destroy_capture_pipeline (void);
static void capture_init_params (void);
static void set_encoder_bitrate (guint bitrate);
static void set_encoder_profile (H264EncProfileType profile);
void set_capture_device_node (void);
static void print_help (void);
static void set_new_file_name (int muxer_type);
void restart_capture_pipeline (void);
static gboolean create_svs_bin (void);
static gboolean create_cap_bin (void);
static gboolean create_vid_enc_bin (void);
static gboolean create_img_enc_bin (void);

static gboolean get_image_encoder (GstElement ** iencoder);
static gboolean get_video_encoder (GstElement ** vencoder);
static gboolean get_muxer (GstElement ** muxer);
static void
cam_image_captured (GstElement * fsink,
    GstBuffer * buffer, GstPad * pad, gpointer udata);
static gboolean
parse_spec (const gchar * option_name,
    const gchar * value, gpointer data, GError ** error);
static GstPadProbeReturn
prev_buf_prob (GstPad * pad, GstPadProbeInfo * info, gpointer u_data);
static GstPadProbeReturn
enc_buf_prob (GstPad * pad, GstPadProbeInfo * info, gpointer u_data);
gboolean get_preview_resolution (gint res);
static gboolean get_image_capture_resolution (gint res);
static gboolean get_video_capture_resolution (gint res);
static gboolean camera_need_reconfigure (int new_res,
    CapturePadType current_pad);

static CamCtx capp;
CamCtx *app;
static GMainLoop *loop;
static gboolean cintr = FALSE;
gboolean recording = FALSE;
static gboolean snapshot = FALSE;

/* EGLStream Producer */
typedef gint (*start_eglstream_producer_func)
  (int producer_index, EGLDisplay * display, EGLStreamKHR * stream,
   int width, int height);
typedef gint (*stop_eglstream_producer_func) (int producer_index);

static int is_user_bitrate = 0;

void set_saturation (gfloat dval);
void set_whitebalance (gint val);
void set_timeout(gint val);
void set_mode (gint newMode);
void set_exposure_saturation (gfloat dval);

gboolean set_preview_resolution (int new_res);
gboolean set_image_resolution (int new_res);
gboolean set_video_resolution (int new_res);

void start_video_capture (void);
void stop_video_capture (void);
void trigger_vsnap_capture (void);
void trigger_image_capture (void);
gboolean exit_capture (gpointer data);

#if !GUI
static void nvgst_handle_xevents (void);
static gpointer nvgst_x_event_thread (gpointer);
#endif

#define WIDTH_RES   176, 320, 640, 1280, 1920, 2104, 2592, 2616, 3840, 3896, 4208, 5632, 5632
#define HEIGHT_RES  144, 240, 480, 720,  1080, 1560, 1944, 1472, 2160, 2192, 3120, 3168, 4224

gint prevres_width[] = { WIDTH_RES };
gint prevres_height[] = { HEIGHT_RES };
static gint image_capture_width[] = { WIDTH_RES };
static gint image_capture_height[] = { HEIGHT_RES };
static gint video_capture_width[] = { WIDTH_RES };
static gint video_capture_height[] = { HEIGHT_RES };

/* MetaData structure returned by nvcamerasrc */
typedef struct AuxBufferData {
  gint64 frame_num;
  gint64 timestamp;
  void * sensor_data;
} AuxData;


/**
  * a GOptionArgFunc callback function
  *
  * @param option_name : The name of the option being parsed
  * @param value       : The value to be parsed.
  * @param data        : User data added to the GOptionGroup
  * @param error       : A return location for errors
  */
static gboolean
parse_spec (const gchar * option_name,
    const gchar * value, gpointer data, GError ** error)
{
  if (!g_strcmp0 ("--prev-res", option_name)) {
    if (TRUE != get_preview_resolution (atoi (value)))
      return FALSE;
  } else if (!g_strcmp0 ("--image-res", option_name)) {
    if (TRUE != get_image_capture_resolution (atoi (value)))
      return FALSE;
  } else if (!g_strcmp0 ("--video-res", option_name)) {
    if (TRUE != get_video_capture_resolution (atoi (value)))
      return FALSE;
  } else if (!g_strcmp0 ("--cus-prev-res", option_name)) {
    gchar *str = NULL;
    app->capres.cus_prev_width = atoi (value);
    str = g_strrstr (value, "x");
    if (str) {
      app->capres.cus_prev_height = atoi (str + 1);
      app->use_cus_res = TRUE;
    } else {
      g_print ("\nInvalid custom preview resolution! Setting to prev_res.\n");
      app->capres.cus_prev_width = app->capres.preview_width;
      app->capres.cus_prev_height = app->capres.preview_height;
    }
  } else if (!g_strcmp0 ("--svs", option_name)) {
    app->svs = g_strdup (value);
  } else if (!g_strcmp0 ("--saturation", option_name)) {
    app->saturation = atof (value);
  } else if (!g_strcmp0 ("--cap-dev-node", option_name)) {
    g_free (app->cap_dev_node);
    app->cap_dev_node = g_strdup (value);
    set_capture_device_node ();
  } else if (!g_strcmp0 ("--eglstream-id", option_name)) {
    app->eglstream_producer_id = atoi (value);
  } else if (!g_strcmp0 ("--overlayConfig", option_name)) {
    g_free (app->overlayConfig);
    app->overlayConfig = g_strdup (value);
  } else if (!g_strcmp0 ("--eglConfig", option_name)) {
    g_free (app->eglConfig);
    app->eglConfig = g_strdup (value);
  } else if (!g_strcmp0 ("--exposuretimerange", option_name)) {
    g_free (app->exposure_timerange);
    app->exposure_timerange = g_strdup (value);
  } else if (!g_strcmp0 ("--gainrange", option_name)) {
    g_free (app->gain_range);
    app->gain_range = g_strdup (value);
  } else if (!g_strcmp0 ("--ispdigitalgainrange", option_name)) {
    g_free (app->isp_digital_gainrange);
    app->isp_digital_gainrange = g_strdup (value);
  } else if (!g_strcmp0 ("--exposurecompensation", option_name)) {
    app->exposure_compensation = atof (value);
  } else if (!g_strcmp0 ("--tnr-strength", option_name)) {
    app->tnr_strength = atof (value);
  } else if (!g_strcmp0 ("--ee-strength", option_name)) {
    app->ee_strength = atof (value);
  }
  return TRUE;
}

/**
  * get the max capture resolutions
  *
  * @param res : resolution index
  */
static void
get_max_resolution (gint res, gint * width, gint * height)
{
  if (app->use_cus_res) {
    *width = app->capres.cus_prev_width;
    *height = app->capres.cus_prev_height;
  } else {
    *width = image_capture_width[res];
    *height = image_capture_height[res];
  }
}

/**
  * get the preview capture resolutions
  *
  * @param res : resolution index
  */
gboolean
get_preview_resolution (gint res)
{
  gboolean ret = TRUE;

  if ( (app->cam_src == NV_CAM_SRC_CSI) ||
      (app->cam_src == NV_CAM_SRC_EGLSTREAM) ) {
    if ((res < MIN_CSI_RES) || (res > MAX_CSI_RES)) {
      g_print ("Invalid preview resolution\n");
      return FALSE;
    }
  } else {
    if ((res < PR_176x144) || (res > PR_1920x1080)) {
      g_print ("Invalid preview resolution\n");
      return FALSE;
    }
  }

  app->capres.preview_width = prevres_width[res];
  app->capres.preview_height = prevres_height[res];
  app->capres.prev_res_index = res;
  return ret;
}

static gboolean
get_image_capture_resolution (gint res)
{
  gboolean ret = TRUE;

  if ( (app->cam_src == NV_CAM_SRC_CSI) ||
      (app->cam_src == NV_CAM_SRC_EGLSTREAM) ) {
    if ((res < IR_640x480) || (res > IR_5632x4224)) {
      g_print ("Invalid image capture resolution\n");
      return FALSE;
    }
  } else {
    if ((res < IR_176x144) || (res > IR_1920x1080)) {
      g_print ("Invalid image capture resolution\n");
      return FALSE;
    }
  }
  app->capres.image_cap_width = image_capture_width[res];
  app->capres.image_cap_height = image_capture_height[res];
  app->capres.img_res_index = res;

  return ret;
}

static gboolean
get_video_capture_resolution (gint res)
{
  gboolean ret = TRUE;

  if ( (app->cam_src == NV_CAM_SRC_CSI) ||
      (app->cam_src == NV_CAM_SRC_EGLSTREAM) ){
    if ((res < VR_640x480) || (res > VR_3896x2192)) {
      g_print ("Invalid video capture resolution\n");
      return FALSE;
    }
  } else {
    if ((res < VR_176x144) || (res > VR_1280x720)) {
      g_print ("Invalid video capture resolution\n");
      return FALSE;
    }
  }
  app->capres.video_cap_width = video_capture_width[res];
  app->capres.video_cap_height = video_capture_height[res];
  app->capres.vid_res_index = res;

  return ret;
}

static gpointer
reset_elements (gpointer data)
{
  gst_element_set_state (app->ele.venc_q, GST_STATE_READY);
  gst_element_set_state (app->ele.vid_bin, GST_STATE_READY);
  gst_element_set_state (app->ele.svc_vidbin, GST_STATE_READY);

  gst_element_sync_state_with_parent (app->ele.venc_q);
  gst_element_sync_state_with_parent (app->ele.vid_bin);
  gst_element_sync_state_with_parent (app->ele.svc_vidbin);

  return NULL;
}

/**
  * handler on the bus
  *
  * @param bus  : a GstBus
  * @param msg  : the GstMessage
  * @param data : user data that has been given
  */
static GstBusSyncReply
bus_sync_handler (GstBus * bus, GstMessage * msg, gpointer data)
{
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ELEMENT:
      if (GST_MESSAGE_SRC (msg) == GST_OBJECT (app->ele.camera)) {
        const GstStructure *structure;
        structure = gst_message_get_structure (msg);
        if (gst_structure_has_name (structure, "image-done")) {
          NVGST_INFO_MESSAGE ("image-capture-done");
          g_mutex_lock (app->lock);

          recording = FALSE;
          g_cond_signal (app->cond);
          g_mutex_unlock (app->lock);

        } else if (gst_structure_has_name (structure, "video-done")) {
          NVGST_INFO_MESSAGE ("video-capture-done");
        } else if (gst_structure_has_name (structure, "GstBinForwarded")) {
          GstMessage *child_msg;

          if (gst_structure_has_field (structure, "message")) {
            const GValue *val = gst_structure_get_value (structure, "message");
            if (G_VALUE_TYPE (val) == GST_TYPE_MESSAGE) {
              child_msg = (GstMessage *) g_value_get_boxed (val);
              if (GST_MESSAGE_TYPE (child_msg) == GST_MESSAGE_EOS &&
                  GST_MESSAGE_SRC (child_msg) == GST_OBJECT (app->ele.vid_bin))
              {
                if (app->reset_thread)
                  g_thread_unref (app->reset_thread);
                app->reset_thread = g_thread_new (NULL, reset_elements, NULL);

              }
            }
          }

        }
      }
      return GST_BUS_PASS;

    default:
      return GST_BUS_PASS;
  }
}

/**
  * Handle received message
  *
  * @param bus  : a GstBus
  * @param msg  : the GstMessage
  * @param data : user data that has been given
  */
static gboolean
bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:
    {
      GError *err = NULL;
      gchar *name, *debug = NULL;

      name = gst_object_get_path_string (msg->src);
      gst_message_parse_error (msg, &err, &debug);

      g_printerr ("ERROR on bus: by %s: %s\n", name, err->message);
      if (debug != NULL)
        g_printerr ("debug info:\n%s\n", debug);

      g_error_free (err);
      g_free (debug);
      g_free (name);

      app->return_value = -1;
      g_main_loop_quit (loop);
    }
      break;

    case GST_MESSAGE_STATE_CHANGED:
    {
      GstState old, new_state, pending;

      gst_message_parse_state_changed (msg, &old, &new_state, &pending);

      GST_DEBUG_OBJECT (GST_OBJECT (msg->src),
          "changed state from %s to %s, pending %s\n",
          gst_element_state_get_name (old), gst_element_state_get_name (new_state),
          gst_element_state_get_name (pending));
      if (GST_MESSAGE_SRC (msg) == GST_OBJECT (app->ele.camera)
          && pending == GST_STATE_VOID_PENDING && old == GST_STATE_PAUSED
          && new_state == GST_STATE_PLAYING) {
      }
    }
      break;

    case GST_MESSAGE_EOS:
    {
      if (app->mode == CAPTURE_VIDEO && app->cam_src != NV_CAM_SRC_CSI)
        restart_capture_pipeline ();

      if (app->mode == CAPTURE_IMAGE) {
        g_mutex_lock (app->lock);

        recording = FALSE;
        g_cond_signal (app->cond);

        g_mutex_unlock (app->lock);
      }
    }
      break;

    case GST_MESSAGE_APPLICATION:
    {
      const GstStructure *s;
      s = gst_message_get_structure (msg);

      if (gst_structure_has_name (s, "NvGstAppInterrupt")) {
        g_print ("Terminating the camera pipeline ...\n");
        g_main_loop_quit (loop);
      }
    }
      break;

    case GST_MESSAGE_ELEMENT:
      break;

    default:
      break;
  }
  return TRUE;
}

static void
set_egl_window_config (gchar *str)
{
  gchar **tokens;
  gchar **temp;
  gchar *token;
  GArray *array;
  guint val, x_pos, y_pos;

  if (!str) {
    g_print ("Invalid Input\n");
    return;
  }
  array = g_array_new (FALSE, FALSE, sizeof (guint));
  tokens = g_strsplit_set (str, " \"\'", -1);
  temp = tokens;
  while (*temp) {
    token = *temp++;
    if (!g_strcmp0 (token, ""))
      continue;

    val = atoi (token);
    g_array_append_val (array, val);
  }

  if (array->len == 2) {
    x_pos = g_array_index (array, guint, 0);
    y_pos = g_array_index (array, guint, 1);
    app->disp.x = x_pos;
    app->disp.y = y_pos;
  } else
    g_print ("Need two values (x, y) for EGL window coordinates\n");

  g_array_free (array, FALSE);
  g_strfreev (tokens);
}

static void
set_overlay_configuration (gchar * str)
{
  gchar **tokens;
  gchar **temp;
  gchar *token;
  GArray *array;
  guint val;

  if (!str) {
    g_print ("Invalid Input\n");
    return;
  }
  array = g_array_new (FALSE, FALSE, sizeof (gfloat));
  tokens = g_strsplit_set (str, " \"\',", -1);
  temp = tokens;
  while (*temp) {
    token = *temp++;
    if (!g_strcmp0 (token, ""))
      continue;

    val = atoi (token);
    g_array_append_val (array, val);
  }

  if (array->len == 5) {
    app->overlay_index = g_array_index (array, guint, 0);
    app->overlay_x_pos = g_array_index (array, guint, 1);
    app->overlay_y_pos = g_array_index (array, guint, 2);
    app->overlay_width = g_array_index (array, guint, 3);
    app->overlay_height = g_array_index (array, guint, 4);
  } else
    g_print ("Need five values for overlay configuration\n");

  g_array_free (array, FALSE);
  g_strfreev (tokens);
}

static void
write_vsnap_buffer (GstElement * fsink,
    GstBuffer * buffer, GstPad * pad, gpointer udata)
{
  GstMapInfo info;

  if (gst_buffer_map (buffer, &info, GST_MAP_READ)) {
    if (info.size) {
      FILE *fp = NULL;
      gchar outfile[50];

      memset (outfile, 0, sizeof (outfile));
      sprintf (outfile, "snapshot_%ld_s%02d_%05d.jpg", (long) getpid(),
          app->sensor_id, app->capture_count++);

      CALL_GUI_FUNC (show_text, "Image saved to %s", outfile);

      fp = fopen (outfile, "wb");
      if (fp == NULL) {
        g_print ("Can't open file for Image Capture!\n");
        app->cap_success = FALSE;
      } else {
        if (info.size != fwrite (info.data, 1, info.size, fp)) {
          g_print ("Can't write data in file!\n");
          app->cap_success = FALSE;
          fclose (fp);
          if (remove (outfile) != 0)
            g_print ("Unable to delete the file\n");
        } else {
          app->cap_success = TRUE;
          fclose (fp);
        }
      }
    }

    gst_buffer_unmap (buffer, &info);
    g_mutex_lock (app->lock);
    snapshot = FALSE;
    g_cond_signal (app->cond);
    g_mutex_unlock (app->lock);
  } else {
    NVGST_WARNING_MESSAGE ("video snapshot buffer map failed\n");
  }
}


void
trigger_vsnap_capture (void)
{
  if (app->mode != CAPTURE_VIDEO || recording == FALSE) {
    g_print ("snapshot is only possible while recording video\n");
    return;
  }

  if (app->cam_src != NV_CAM_SRC_CSI) {
    g_print ("Video snapshot is supported for CSI camera only\n");
    return;
  }

  snapshot = TRUE;
  app->cap_success = FALSE;
  gst_element_set_state (app->ele.img_sink, GST_STATE_NULL);
  /* Set Video Snapshot Mode */
  g_signal_emit_by_name (G_OBJECT (app->ele.cap_tee), "take-vsnap");

  g_mutex_lock (app->lock);
  while (snapshot) {
    g_cond_wait (app->cond, app->lock);
  }
  g_mutex_unlock (app->lock);

  /* Back to Video Mode */
  if (app->cap_success == TRUE)
    g_print ("Video Snapshot Captured \n");
}

void
trigger_image_capture (void)
{
  g_mutex_lock (app->lock);
  recording = TRUE;
  app->cap_success = FALSE;

  app->capcount = 0;
  app->native_record = GST_PAD_PROBE_OK;

  if (app->cam_src == NV_CAM_SRC_CSI) {
    gst_element_set_state (app->ele.vsnap_sink, GST_STATE_NULL);
    g_signal_emit_by_name (G_OBJECT (app->ele.cap_tee), "start-capture");
  }

  while (recording) {
    g_cond_wait (app->cond, app->lock);
  }

  g_mutex_unlock (app->lock);

  if (app->cap_success == TRUE)
    g_print ("Image Captured \n");
}


/**
* Reset KPI flags to start
* new measurements
*/
static void reset_kpi_flags (void)
{
  app->frameCount = 0;
  app->currentFrameTime = 0;
  app->prevFrameTime = 0;
  app->encFrameCount = 0;
  app->currentEncFrameTime = 0;
  app->prevEncFrameTime = 0;
}

/**
 * Compute preview frame rate
 * @param void
 */
static void compute_frame_rate (void)
{
  gfloat avgFrameTime = 0;
  gfloat frameRate = 0;

  if (app->enableKpiNumbers) {

    if (app->mode != CAPTURE_VIDEO) {
      if (app->frameCount > 0) {
        app->frameCount--;
      }

      avgFrameTime = (app->frameCount == 0) ? 0 :
                     ((gfloat)(app->accumulator) / (gfloat)(app->frameCount));
      frameRate = (avgFrameTime == 0) ? 0 : (gfloat)(1000 / avgFrameTime);
      g_print("\nKPI average frame rate: %.2lf\n", frameRate);
    }
    else {
      if (app->encFrameCount > 0) {
        app->encFrameCount--;
      }

      avgFrameTime = (app->encFrameCount == 0) ? 0 :
                     ((gfloat)(app->encAccumulator) / (gfloat)(app->encFrameCount));
      frameRate = (avgFrameTime == 0) ? 0 : (gfloat)(1000 / avgFrameTime);
      g_print("\nKPI average frame rate: %.2lf\n", frameRate);
    }
  }
}

void
start_video_capture (void)
{
  reset_kpi_flags ();
  set_new_file_name (app->file_type);
  recording = TRUE;
  app->native_record = GST_PAD_PROBE_OK;
  if (app->cam_src == NV_CAM_SRC_CSI) {
    /* Set Video Mode */
    g_signal_emit_by_name (G_OBJECT (app->ele.cap_tee), "start-capture");
  }
  CALL_GUI_FUNC (start_record);
}

void
stop_video_capture (void)
{
  if (!app->aut.automate) {
    compute_frame_rate ();
  }
  recording = FALSE;
  app->native_record = GST_PAD_PROBE_DROP;
  if (app->cam_src == NV_CAM_SRC_CSI) {
    g_signal_emit_by_name (G_OBJECT (app->ele.cap_tee), "stop-capture");
    gst_pad_send_event (gst_element_get_static_pad (app->ele.venc_q, "sink"),
        gst_event_new_eos ());
  } else {
    gst_pad_send_event (gst_element_get_static_pad (app->ele.venc_q, "sink"),
        gst_event_new_eos ());
    gst_pad_send_event (gst_element_get_static_pad (app->ele.vsink, "sink"),
        gst_event_new_eos ());
  }
  g_print ("\nRecording Stopped\n");
  CALL_GUI_FUNC (stop_record);
}

void
set_mode (gint newMode)
{
  if (newMode != 1 && newMode != 2) {
    newMode = NVGST_DEFAULT_CAPTURE_MODE;
    g_print ("Invalid input mode, setting mode to image-capture = 1 \n");
  }
  g_print ("Changing capture mode to %d\n", newMode);
  g_print ("(1): image\n(2): video\n");

  if (app->cam_src == NV_CAM_SRC_CSI) {
    g_object_set (app->ele.cap_tee, "mode", newMode, NULL);
  } else {
    destroy_capture_pipeline ();
    g_usleep (250000);
    app->mode = newMode;
    if (!create_capture_pipeline ()) {
      app->return_value = -1;
      g_main_loop_quit (loop);
    }
  }
  app->mode = newMode;
}

gboolean
set_preview_resolution (int new_res)
{
  GstCaps *caps = NULL;
  gint width = 0, height = 0;
  if (new_res == app->capres.prev_res_index) {
    g_print ("\nAlready on same preview resolution\n");
    return TRUE;
  }
  if (!get_preview_resolution (new_res))
    return FALSE;

  g_object_get (app->ele.svc_prevconv_out_filter, "caps", &caps, NULL);
  caps = gst_caps_make_writable (caps);

  gst_caps_set_simple (caps, "width", G_TYPE_INT,
      app->capres.preview_width, "height", G_TYPE_INT,
      app->capres.preview_height, NULL);

  g_object_set (app->ele.svc_prevconv_out_filter, "caps", caps, NULL);
  gst_caps_unref (caps);

  if (camera_need_reconfigure (new_res, CAPTURE_PAD_PREV)) {
    g_object_get (app->ele.cap_filter, "caps", &caps, NULL);
    caps = gst_caps_make_writable (caps);

    get_max_resolution (app->capres.current_max_res, &width, &height);
    gst_caps_set_simple (caps, "width", G_TYPE_INT,
        width, "height", G_TYPE_INT, height, NULL);

    g_object_set (app->ele.cap_filter, "caps", caps, NULL);
    gst_caps_unref (caps);
  }

#if !GUI
{
  GstElement *vsink = app->ele.vsink;

  if (vsink && GST_IS_VIDEO_OVERLAY (vsink)) {
    if (app->capres.preview_width < app->disp.display_width
        || app->capres.preview_height < app->disp.display_height) {
      app->disp.width = app->capres.preview_width;
      app->disp.height = app->capres.preview_height;
    } else {
      app->disp.width = app->disp.display_width;
      app->disp.height = app->disp.display_height;
    }
    g_mutex_lock (app->lock);

    if (app->disp.window)
      nvgst_destroy_window (&app->disp);
    nvgst_create_window (&app->disp, "nvgstcapture-1.0");
    gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (vsink),
        (gulong) app->disp.window);
    gst_video_overlay_expose (GST_VIDEO_OVERLAY (vsink));

    g_mutex_unlock (app->lock);
 }
}
#endif
  g_print ("Preview resolution = %d x %d\n",
      app->capres.preview_width, app->capres.preview_height);

  return TRUE;
}

gboolean
set_image_resolution (int new_res)
{
  GstCaps *caps = NULL;
  gint width = 0, height = 0;
  if (new_res == app->capres.img_res_index) {
    g_print ("\nAlready on same image capture resolution\n");
    return TRUE;
  }
  if (!get_image_capture_resolution (new_res))
    return FALSE;

  //configure image
  g_object_get (app->ele.svc_imgvconv_out_filter, "caps", &caps, NULL);
  caps = gst_caps_make_writable (caps);

  gst_caps_set_simple (caps, "width", G_TYPE_INT,
      app->capres.image_cap_width, "height", G_TYPE_INT,
      app->capres.image_cap_height, NULL);

  g_object_set (app->ele.svc_imgvconv_out_filter, "caps", caps, NULL);
  gst_caps_unref (caps);

  if (camera_need_reconfigure (new_res, CAPTURE_PAD_IMAGE)) {
    g_object_get (app->ele.cap_filter, "caps", &caps, NULL);
    caps = gst_caps_make_writable (caps);

    get_max_resolution (app->capres.current_max_res, &width, &height);
    gst_caps_set_simple (caps, "width", G_TYPE_INT,
        width, "height", G_TYPE_INT, height, NULL);

    g_object_set (app->ele.cap_filter, "caps", caps, NULL);
    gst_caps_unref (caps);
  }

  g_print ("Image Capture Resolution = %d x %d\n",
      app->capres.image_cap_width, app->capres.image_cap_height);
  return TRUE;
}

gboolean
set_video_resolution (int new_res)
{
  GstCaps *caps = NULL;
  gint width = 0, height = 0;
  if (new_res == app->capres.vid_res_index) {
    g_print ("\nAlready on same video capture resolution\n");
    return TRUE;
  }
  if (!get_video_capture_resolution (new_res))
    return FALSE;

  //configure video
  g_object_get (app->ele.svc_vidvconv_out_filter, "caps", &caps, NULL);
  caps = gst_caps_make_writable (caps);

  gst_caps_set_simple (caps, "width", G_TYPE_INT,
      app->capres.video_cap_width, "height", G_TYPE_INT,
      app->capres.video_cap_height, NULL);

  g_object_set (app->ele.svc_vidvconv_out_filter, "caps", caps, NULL);
  gst_caps_unref (caps);

  if (camera_need_reconfigure (new_res, CAPTURE_PAD_VIDEO)) {
    g_object_get (app->ele.cap_filter, "caps", &caps, NULL);
    caps = gst_caps_make_writable (caps);

    get_max_resolution (app->capres.current_max_res, &width, &height);
    gst_caps_set_simple (caps, "width", G_TYPE_INT,
        width, "height", G_TYPE_INT, height, NULL);

    g_object_set (app->ele.cap_filter, "caps", caps, NULL);
    gst_caps_unref (caps);
  }
  g_print ("Video Capture Resolution = %d x %d\n",
      app->capres.video_cap_width, app->capres.video_cap_height);
  return TRUE;
}

void
set_saturation (gfloat dval)
{
  app->saturation = dval;
  g_object_set (G_OBJECT (app->ele.vsrc), "saturation", dval, NULL);
}

void
set_exposure_saturation (gfloat dval)
{
  app->exposure_compensation = dval;
  g_object_set (G_OBJECT (app->ele.vsrc), "exposurecompensation", dval, NULL);
}

void
set_whitebalance (gint val)
{
  app->whitebalance = val;
  g_object_set (G_OBJECT (app->ele.vsrc), "wbmode", val, NULL);
}

void
set_timeout(gint val)
{
  app->timeout = val;
  g_object_set (G_OBJECT (app->ele.vsrc), "timeout", val, NULL);
}

static void
set_flip (gint val)
{
  app->flip_method = val;
  g_object_set (G_OBJECT (app->ele.svc_prevconv), "flip-method", val, NULL);
  g_object_set (G_OBJECT (app->ele.svc_imgvconv), "flip-method", val, NULL);
  g_object_set (G_OBJECT (app->ele.svc_vidvconv), "flip-method", val, NULL);
  g_object_set (G_OBJECT (app->ele.svc_snapconv), "flip-method", val, NULL);
}

/**
  * Handle on input commands.
  *
  * @param ichannel : a GIOChannel
  * @param cond     : the condition to watch for
  * @param data     : user data passed
  */
static gboolean
on_input (GIOChannel * ichannel, GIOCondition cond, gpointer data)
{
  static gchar buf[256];
  int bytes_read;
  gint fd;
  gint val;
  guint br;
  gfloat dval;
  gchar *gval;

  fd = g_io_channel_unix_get_fd (ichannel);
  bytes_read = read (fd, buf, 256);
  buf[bytes_read - 1] = '\0';

  if (g_str_has_prefix (buf, "h")) {
    print_help ();

  } else if (buf[0] == 'q') {
    if (app->mode != CAPTURE_VIDEO) {
      compute_frame_rate ();
    }
    g_main_loop_quit (loop);

  } else if (buf[0] == '1' && app->mode == CAPTURE_VIDEO && recording == FALSE) {
    start_video_capture ();
    g_print
        ("\nRecording Started, Enter (0) to stop OR (2) to take snapshot \n");

  } else if (buf[0] == 'f' && app->mode == CAPTURE_VIDEO && recording == TRUE) {
    g_print ("Forcing IDR on video encoder\n");
    g_signal_emit_by_name (G_OBJECT (app->ele.vid_enc), "force-IDR");

  } else if (buf[0] == '2' && app->mode == CAPTURE_VIDEO && recording == TRUE) {
    trigger_vsnap_capture ();

  } else if (buf[0] == '0' && recording == TRUE) {
    stop_video_capture ();

  } else if (buf[0] == 'j' && app->mode == CAPTURE_IMAGE && recording == FALSE) {
    gint n = 0;
    gint count = 1;
    gchar *str;
    gint stime = 0;

    str = g_strrstr (buf, ":");
    if (str) {
      count = atoi (str + 1);
    }
    str = g_strrstr (buf, "x");
    if (str) {
      stime = atoi (str + 1);
      if (stime < 500)
        stime = 500;
      g_usleep (stime * 1000 - 500000);
    }

    while (n++ < count) {
      trigger_image_capture ();
      if (app->cap_success != TRUE)
        break;
      if (app->return_value == -1)
        break;
      g_usleep (250000);
    }
  } else if (recording == FALSE) {
    if (g_str_has_prefix (buf, "mo:")) {
      gint newMode;
      newMode = atoi (buf + 3);
      if (newMode == app->mode) {
        g_print ("Already in this mode\n");
        return TRUE;
      }
      set_mode (newMode);
    } else if (!g_strcmp0 (buf, "gmo")) {
      g_print ("mo = %d\n", app->mode);
      g_print ("(1): image\n(2): video\n");
    } else if (g_str_has_prefix (buf, "pcr:")) {
        if (app->cam_src == NV_CAM_SRC_CSI)
          g_print ("-------> Not supported <------ \n");
        else {
          if (TRUE != get_preview_resolution (atoi (buf + 4)))
            goto invalid_input;
          g_print ("w = %d, h = %d\n", app->capres.preview_width,
            app->capres.preview_height);
          restart_capture_pipeline ();
      }
    } else if (!g_strcmp0 (buf, "gpcr")) {
      g_print ("w = %d, h = %d\n", app->capres.preview_width,
          app->capres.preview_height);
    } else if (!g_strcmp0 (buf, "gicr")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        g_print ("w = %d, h = %d\n", app->capres.image_cap_width,
            app->capres.image_cap_height);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (!g_strcmp0 (buf, "gvcr")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        g_print ("w = %d, h = %d\n", app->capres.video_cap_width,
            app->capres.video_cap_height);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (g_str_has_prefix (buf, "so:")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        val = atoi (buf + 3);
        if (val < 0 || val > 3) {
          val = NVGST_DEFAULT_FLIP_METHOD;
          g_print ("Invalid input value of sensor orientation, setting orientation"
              " to default = 2 \n");
        }
        g_print ("sensor orientation = %d\n", val);
        set_flip (val);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (!g_strcmp0 (buf, "gso")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        g_print ("sensor orientation = %d\n", app->flip_method);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (g_str_has_prefix (buf, "wb:")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        val = atoi (buf + 3);
        if (val < 0 || val > 9) {
          val = NVGST_DEFAULT_WHITEBALANCE;
          g_print ("Invalid input value of white balance, setting white-balance"
              " to auto-value =1 \n");
        }
        g_print ("whitebalance = %d\n", val);
        set_whitebalance (val);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (!g_strcmp0 (buf, "gwb")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        g_object_get (G_OBJECT (app->ele.vsrc), "wbmode", &val, NULL);
        app->whitebalance = val;
        g_print ("whitebalance = %d\n", app->whitebalance);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (g_str_has_prefix (buf, "st:")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        dval = atof (buf + 3);
        if (dval < 0 || dval > 2) {
          dval = NVGST_DEFAULT_SATURATION;
          g_print ("Invalid input value of saturation, setting saturation"
              " to default = 1 \n");
        }
        g_print ("saturation = %f\n", dval);
        set_saturation (dval);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (!g_strcmp0 (buf, "gst")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        g_object_get (G_OBJECT (app->ele.vsrc), "saturation", &dval, NULL);
        app->saturation = dval;
        g_print ("saturation = %f\n", app->saturation);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (g_str_has_prefix (buf, "ec:")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        dval = atof (buf + 3);
        if (dval < MIN_EXPOSURE_COMPENSATION || dval > MAX_EXPOSURE_COMPENSATION) {
          dval = NVGST_DEFAULT_EXPOSURE_COMPENSATION;
          g_print ("Invalid input value of exposure compensation, setting "
              " to default = 0 \n");
        }
        g_print ("exposure compensation = %f\n", dval);
        set_exposure_saturation (dval);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (!g_strcmp0 (buf, "gec")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        g_object_get (G_OBJECT (app->ele.vsrc), "exposurecompensation", &dval, NULL);
        app->exposure_compensation = dval;
        g_print ("exposure compensation = %f\n", app->exposure_compensation);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (g_str_has_prefix (buf, "ael:")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        val = atoi (buf + 4);
        if (val < 0 || val > 1) {
          val = NVGST_DEFAULT_AE_LOCK;
          g_print ("Invalid input value of AE LOCK, setting "
              " to default = 0 \n");
        }
        g_print ("AE Lock = %d\n", val);
        app->enableAeLock = val;
        g_object_set (G_OBJECT (app->ele.vsrc), "aelock", app->enableAeLock, NULL);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (!g_strcmp0 (buf, "gael")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        g_object_get (G_OBJECT (app->ele.vsrc), "aelock", &val, NULL);
        app->enableAeLock = val;
        g_print ("AE Lock = %d\n", app->enableAeLock);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (g_str_has_prefix (buf, "awbl:")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        val = atoi (buf + 5);
        if (val < 0 || val > 1) {
          val = NVGST_DEFAULT_AWB_LOCK;
          g_print ("Invalid input value of AWB LOCK, setting "
              " to default = 0 \n");
        }
        g_print ("AWB Lock = %d\n", val);
        app->enableAwbLock = val;
        g_object_set (G_OBJECT (app->ele.vsrc), "awblock", app->enableAwbLock, NULL);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (!g_strcmp0 (buf, "gawbl")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        g_object_get (G_OBJECT (app->ele.vsrc), "awblock", &val, NULL);
        app->enableAwbLock = val;
        g_print ("AWB Lock = %d\n", app->enableAwbLock);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (g_str_has_prefix (buf, "tnrm:")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        val = atoi (buf + 5);
        if (val < MIN_TNR_MODE || val > MAX_TNR_MODE) {
          val = NVGST_DEFAULT_TNR_MODE;
          g_print ("Invalid input value of TNR Mode, setting "
              " to default = 0 \n");
        }
        g_print ("TNR Mode = %d\n", val);
        app->tnr_mode = val;
        g_object_set (G_OBJECT (app->ele.vsrc), "tnr-mode", app->tnr_mode , NULL);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (!g_strcmp0 (buf, "gtnrm")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        g_object_get (G_OBJECT (app->ele.vsrc), "tnr-mode", &val, NULL);
        app->tnr_mode = val;
        g_print ("TNR Mode = %d\n", app->tnr_mode);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (g_str_has_prefix (buf, "tnrs:")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        dval = atof (buf + 5);
        if (dval < MIN_STRENGTH || dval > MAX_STRENGTH) {
          dval = NVGST_DEFAULT_TNR_STRENGTH;
          g_print ("Invalid input value of TNR Strength, setting "
              " to default = 0 \n");
        }
        g_print ("TNR Strength = %f\n", dval);
        app->tnr_strength = dval;
        g_object_set (G_OBJECT (app->ele.vsrc), "tnr-strength", app->tnr_strength , NULL);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (!g_strcmp0 (buf, "gtnrs")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        g_object_get (G_OBJECT (app->ele.vsrc), "tnr-strength", &dval, NULL);
        app->tnr_strength = dval;
        g_print ("TNR Strength = %f\n", app->tnr_strength);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (g_str_has_prefix (buf, "eem:")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        val = atoi (buf + 4);
        if (val < MIN_EE_MODE || val > MAX_EE_MODE) {
          val = NVGST_DEFAULT_EE_MODE;
          g_print ("Invalid input value of EE Mode, setting "
              " to default = 0 \n");
        }
        g_print ("EE Mode = %d\n", val);
        app->ee_mode = val;
        g_object_set (G_OBJECT (app->ele.vsrc), "ee-mode", app->ee_mode , NULL);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (!g_strcmp0 (buf, "geem")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        g_object_get (G_OBJECT (app->ele.vsrc), "ee-mode", &val, NULL);
        app->ee_mode = val;
        g_print ("EE Mode = %d\n", app->ee_mode);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (g_str_has_prefix (buf, "ees:")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        dval = atof (buf + 4);
        if (dval < MIN_STRENGTH || dval > MAX_STRENGTH) {
          dval = NVGST_DEFAULT_EE_STRENGTH;
          g_print ("Invalid input value of EE Strength, setting "
              " to default = 0 \n");
        }
        g_print ("EE Strength = %f\n", dval);
        app->ee_strength = dval;
        g_object_set (G_OBJECT (app->ele.vsrc), "ee-strength", app->ee_strength , NULL);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (!g_strcmp0 (buf, "gees")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        g_object_get (G_OBJECT (app->ele.vsrc), "ee-strength", &dval, NULL);
        app->ee_strength = dval;
        g_print ("EE Strength = %f\n", app->ee_strength);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (g_str_has_prefix (buf, "aeab:")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        val = atoi (buf + 5);
        if (val < MIN_AE_ANTIBANDING_MODE || val > MAX_AE_ANTIBANDING_MODE) {
          val = NVGST_DEFAULT_AEANTIBANDING;
          g_print ("Invalid input value of AE AntiBanding Mode, setting "
              " to default = 0 \n");
        }
        g_print ("AE Anti Banding Mode = %d\n", val);
        app->ae_antibanding = val;
        g_object_set (G_OBJECT (app->ele.vsrc), "aeantibanding", app->ae_antibanding , NULL);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (!g_strcmp0 (buf, "gaeab")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        g_object_get (G_OBJECT (app->ele.vsrc), "aeantibanding", &val, NULL);
        app->ae_antibanding = val;
        g_print ("AE Anti Banding Mode = %d\n", app->ae_antibanding);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (g_str_has_prefix (buf, "gr:")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        app->gain_range = g_strdup (buf+3);
        g_print (" Setting Gain Range = %s\n", app->gain_range);
        g_object_set (G_OBJECT (app->ele.vsrc), "gainrange", app->gain_range , NULL);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (!g_strcmp0 (buf, "ggr")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        g_object_get (G_OBJECT (app->ele.vsrc), "gainrange", &gval, NULL);
        g_print ("Gain Range = %s\n", gval);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (g_str_has_prefix (buf, "etr:")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        app->exposure_timerange = g_strdup (buf+4);
        g_print (" Setting Exposure Time Range = %s\n", app->exposure_timerange);
        g_object_set (G_OBJECT (app->ele.vsrc), "exposuretimerange", app->exposure_timerange , NULL);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (!g_strcmp0 (buf, "getr")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        g_object_get (G_OBJECT (app->ele.vsrc), "exposuretimerange", &gval, NULL);
        g_print ("Exposure Time Range = %s\n", gval);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    }  else if (g_str_has_prefix (buf, "dgr:")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        app->isp_digital_gainrange = g_strdup (buf+4);
        g_print (" Setting ISP Digital Gain Range = %s\n", app->isp_digital_gainrange);
        g_object_set (G_OBJECT (app->ele.vsrc), "ispdigitalgainrange", app->isp_digital_gainrange , NULL);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (!g_strcmp0 (buf, "gdgr")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        g_object_get (G_OBJECT (app->ele.vsrc), "ispdigitalgainrange", &gval, NULL);
        g_print ("ISP Digital Gain Range = %s\n", gval);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (g_str_has_prefix (buf, "br:")) {
      is_user_bitrate = 1;
      dval = atof (buf + 3);
      set_encoder_bitrate (dval);
    } else if (!g_strcmp0 (buf, "gbr")) {
      g_object_get (G_OBJECT (app->ele.vid_enc), "bitrate", &br, NULL);
      app->encset.bitrate = br;
      g_print ("br = %u\n", app->encset.bitrate);
    } else if (g_str_has_prefix (buf, "cdn:")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        g_print ("-------> Not supported <------ \n");
      } else {
        g_free (app->cap_dev_node);
        app->cap_dev_node = g_strdup (buf + 4);
        set_capture_device_node ();
        g_print ("cdn = %s\n", app->vidcap_device);
        restart_capture_pipeline ();
      }
    } else if (!g_strcmp0 (buf, "gcdn")) {
      g_print ("cdn = %s\n", app->vidcap_device);
    } else if (g_str_has_prefix (buf, "sid:")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        val = atoi (buf + 4);
        if (val < 0) {
          g_print ("Invalid value for Sensor ID, using default\n");
          val = 0;
        }
        if (app->sensor_id != (guint) val) {
          g_print ("sensor id = %d\n", val);
          app->sensor_id = val;
          restart_capture_pipeline ();
        } else {
          g_print ("sensor id %d is already set\n", val);
        }
      }
        else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (!g_strcmp0 (buf, "gsid")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        g_object_get (G_OBJECT (app->ele.vsrc), "sensor-id", &val, NULL);
        app->sensor_id = val;
        g_print ("Active Sensor ID = %d\n", app->sensor_id);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (g_str_has_prefix (buf, "smo:")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        val = atoi (buf + 4);
        if (val < NVGST_DEFAULT_SENSOR_MODE) {
          g_print ("Invalid selection for sensor mode, using default\n");
          val = NVGST_DEFAULT_SENSOR_MODE;
        }
        if (app->sensor_mode != (guint) val) {
          g_print ("sensor mode = %d\n", val);
          app->sensor_mode = val;
          restart_capture_pipeline ();
        } else {
          g_print ("sensor mode %d is already set\n", val);
        }
      }
      else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (!g_strcmp0 (buf, "gsmo")) {
      if (app->cam_src == NV_CAM_SRC_CSI) {
        g_object_get (G_OBJECT (app->ele.vsrc), "sensor-mode", &val, NULL);
        app->sensor_mode = val;
        g_print ("Active Sensor Mode = %d\n", app->sensor_mode);
      } else {
        g_print ("-------> Not supported <------ \n");
      }
    } else if (g_str_has_prefix (buf, "ep:")) {
      gint newEp;
      newEp = atoi (buf + 3);
      set_encoder_profile (newEp);
    } else if (!g_strcmp0 (buf, "gep")) {
      if (app->encset.video_enc == FORMAT_H264_HW) {
        switch(app->encset.video_enc_profile) {
          case PROFILE_BASELINE:
            g_print("Encoder Profile = Baseline\n");
            break;
          case PROFILE_MAIN:
            g_print("Encoder Profile = Main\n");
            break;
          case PROFILE_HIGH:
            g_print("Encoder Profile = High\n");
            break;
        }
      } else {
        g_print("Profile only supported with H.264\n");
      }
    }
  }
  CALL_GUI_FUNC (trigger_refresh);
  return TRUE;

invalid_input:
  g_print ("Invalid input\n");
  return TRUE;
}

void
set_capture_device_node (void)
{
  gchar *fname = g_strndup ("/dev/video", 12);
  fname = strcat (fname, app->cap_dev_node);

  if (app->vidcap_device && access (fname, F_OK) != -1) {
    g_free (app->vidcap_device);
    app->vidcap_device = NULL;
    app->vidcap_device = g_strndup ("/dev/video", 12);
    app->vidcap_device = strcat (app->vidcap_device, app->cap_dev_node);
  } else {
    g_print ("%s does not exist\n",fname);
  }
  g_free (fname);
}

static void
set_encoder_profile (H264EncProfileType profile)
{
  const gchar * profile_name;
  guint profile_id;

  if (profile < PROFILE_BASELINE || profile > PROFILE_HIGH) {
    g_print("Invalid value for profile\n");
    return;
  }

  if (app->encset.video_enc != FORMAT_H264_HW) {
    g_print("Profile only supported for H.264 encoder\n");
    return;
  }

  if (app->mode == CAPTURE_VIDEO && recording) {
    g_print("Cannot set profile while recording video\n");
    return;
  }

  switch(profile) {
    case PROFILE_BASELINE:
      profile_id = 0;
      profile_name = "Baseline";
      break;
    case PROFILE_MAIN:
      profile_id = 2;
      profile_name = "Main";
      break;
    case PROFILE_HIGH:
      profile_id = 4;
      profile_name = "High";
      break;
  }

  if (app->ele.vid_enc) {
    g_object_set(G_OBJECT(app->ele.vid_enc), "profile", profile_id, NULL);
  }

  app->encset.video_enc_profile = profile;

  g_print("Encoder Profile = %s\n", profile_name);
}

static void
set_encoder_bitrate (guint bitrate)
{
  if (!app->ele.vid_enc)
    g_print ("Encoder null, cannot set bitrate!\n");
  else {
    if (!bitrate) {             /* Set default bitrates only if user has not set anything */
      if (app->capres.vid_res_index < VR_1280x720)
        bitrate = NVGST_DEFAULT_480P_ENCODER_BITRATE;
      else if (app->capres.vid_res_index >= VR_1280x720
          && app->capres.vid_res_index < VR_1920x1080)
        bitrate = NVGST_DEFAULT_720P_ENCODER_BITRATE;
      else if (app->capres.vid_res_index >= VR_1920x1080
          && app->capres.vid_res_index < VR_3840x2160)
        bitrate = NVGST_DEFAULT_1080P_ENCODER_BITRATE;
      else if (app->capres.vid_res_index >= VR_3840x2160)
        bitrate = NVGST_DEFAULT_2160P_ENCODER_BITRATE;
    }
    if (app->encset.video_enc == FORMAT_H264_SW)
    {
      bitrate /= 1000; // x264enc requires kbits/sec
    }
    app->encset.bitrate = bitrate;
    g_print ("bitrate = %u\n", app->encset.bitrate);
    g_object_set (G_OBJECT (app->ele.vid_enc), "bitrate", app->encset.bitrate,
        NULL);
#ifdef WITH_STREAMING
    if (app->streaming_mode)
      g_object_set (G_OBJECT (app->ele.colorspace_conv), "bitrate",
          app->encset.bitrate, NULL);
#endif
  }
}

/**
  * Print runtime command options.
  *
  * @param void
  */
static void
print_help (void)
{
  /* Camera Runtime Options */
  if (app->cam_src == NV_CAM_SRC_CSI)
    g_print ("%s\n", app->csi_options_argus);
  else if (app->cam_src == NV_CAM_SRC_V4L2)
    g_print ("%s\n", app->usb_options);
  else if (app->cam_src == NV_CAM_SRC_TEST)
    g_print ("%s\n", app->usb_options);
   g_print ("%s\n", app->encoder_options);
}

/**
  * Set encode file name.
  *
  * @param muxer_type : container type
  */
static void
set_new_file_name (int muxer_type)
{
  gchar filename[100];
  gchar * file_ext = NULL;
  switch (app->mode) {
    case CAPTURE_VIDEO:
      switch (muxer_type) {
        case FILE_MP4:
          file_ext = "mp4";
          break;
        case FILE_3GP:
          file_ext = "3gp";
          break;
        case FILE_MKV:
          file_ext = "mkv";
          break;
        case FILE_H265:
          file_ext = "h265";
          break;
        default:
          file_ext = "mp4";
          break;
      }
      break;
    case CAPTURE_IMAGE:
      switch (muxer_type) {
        case FORMAT_JPEG_SW:
        case FORMAT_JPEG_HW:
        default:
          file_ext = "jpg";
          break;
      }
      break;
    default:
      g_print ("Invalid capture Mode, cannot set filename\n");
      break;
  }

  sprintf (filename, "%s_%ld_s%02d_%05d.%s", app->file_name, (long) getpid(),
    app->sensor_id, app->capture_count++, file_ext);

  CALL_GUI_FUNC(set_video_file_name, filename);

  gst_element_set_state (app->ele.video_sink, GST_STATE_NULL);
  g_object_set (G_OBJECT (app->ele.video_sink), "location", filename, NULL);
  gst_element_set_locked_state (app->ele.video_sink, FALSE);
  gst_element_set_state (app->ele.video_sink, GST_STATE_PLAYING);
}

/**
  * Create image encoder element.
  *
  * @param iencoder : image encoder type
  */
static gboolean
get_image_encoder (GstElement ** iencoder)
{
  switch (app->encset.image_enc) {
    case FORMAT_JPEG_SW:
      *iencoder = gst_element_factory_make (NVGST_SW_IMAGE_ENC, NULL);
      break;
    case FORMAT_JPEG_HW:
      *iencoder = gst_element_factory_make (NVGST_DEFAULT_IMAGE_ENC, NULL);
      break;
    default:
      *iencoder = gst_element_factory_make (NVGST_DEFAULT_IMAGE_ENC, NULL);
      break;
  }

  if (!(*iencoder)) {
    app->return_value = -1;
    NVGST_ERROR_MESSAGE ("Can't Create image encoder element\n");
    return FALSE;
  }

  return TRUE;
}

/**
  * Create video encoder element.
  *
  * @param vencoder : video encoder type
  */
static gboolean
get_video_encoder (GstElement ** vencoder)
{
  switch (app->encset.video_enc) {
    case FORMAT_H264_HW:
      *vencoder = gst_element_factory_make (NVGST_PRIMARY_V4L2_H264_VENC, NULL);
      set_encoder_bitrate (app->encset.bitrate);
      set_encoder_profile (app->encset.video_enc_profile);
      break;
    case FORMAT_VP8_HW:
      *vencoder = gst_element_factory_make (NVGST_PRIMARY_V4L2_VP8_VENC, NULL);
      set_encoder_bitrate (app->encset.bitrate);
      break;
    case FORMAT_H265_HW:
      *vencoder = gst_element_factory_make (NVGST_PRIMARY_V4L2_H265_VENC, NULL);
      set_encoder_bitrate (app->encset.bitrate);
      break;
    case FORMAT_VP9_HW:
      *vencoder = gst_element_factory_make (NVGST_PRIMARY_V4L2_VP9_VENC, NULL);
      set_encoder_bitrate (app->encset.bitrate);
      break;
    case FORMAT_H264_SW:
      *vencoder = gst_element_factory_make(NVGST_SW_H264_ENC, NULL);
      set_encoder_bitrate(app->encset.bitrate);
      break;
    default:
      *vencoder = gst_element_factory_make (NVGST_PRIMARY_V4L2_H264_VENC, NULL);
      break;
  }

  if (app->encset.video_enc != FORMAT_H264_SW)
  {
      g_object_set(*vencoder, "control-rate", app->encset.controlrate, NULL);
      g_print("Encoder control-rate = %u\n", app->encset.controlrate);
  }
  if (app->encset.enabletwopassCBR)
  {
    g_object_set (*vencoder, "EnableTwopassCBR", app->encset.enabletwopassCBR, NULL);
    g_print("Encoder EnableTwopassCBR = %d\n", app->encset.enabletwopassCBR);
  }

  if (!(*vencoder)) {
    app->return_value = -1;
    NVGST_ERROR_MESSAGE ("Can't Create video encoder element\n");
    return FALSE;
  }

  return TRUE;
}

static gboolean
get_parser (GstElement ** parser)
{
  switch (app->encset.video_enc) {
    case FORMAT_H264_HW:
      *parser = gst_element_factory_make(NVGST_PRIMARY_H264_PARSER, NULL);
      break;
    case FORMAT_H264_SW:
      *parser = gst_element_factory_make(NVGST_PRIMARY_H264_PARSER, NULL);
      break;
    case FORMAT_H265_HW:
      *parser = gst_element_factory_make (NVGST_PRIMARY_H265_PARSER, NULL);
      break;
    default:
      *parser = gst_element_factory_make (NVGST_PRIMARY_IDENTITY, NULL);
      break;
  }

  return TRUE;
}

/**
  * Create muxer element.
  *
  * @param muxer : mux file type
  */
static gboolean
get_muxer (GstElement ** muxer)
{
  if (app->encset.video_enc == FORMAT_VP9_HW) {
    if (app->file_type != FILE_MKV) {
        NVGST_WARNING_MESSAGE
            ("VP9 is only supported format with MKV in current GST version. "
            "Selecting MKV as container\n");
        app->file_type = FILE_MKV;
    }
  }

  app->muxer_is_identity = FALSE;

  switch (app->file_type) {
    case FILE_MP4:
      *muxer = gst_element_factory_make (NVGST_PRIMARY_MP4_MUXER, NULL);
      break;
    case FILE_3GP:
      *muxer = gst_element_factory_make (NVGST_PRIMARY_3GP_MUXER, NULL);
      break;
    case FILE_MKV:
      *muxer = gst_element_factory_make (NVGST_PRIMARY_MKV_MUXER, NULL);
      break;
    case FILE_H265:
      *muxer = gst_element_factory_make (NVGST_PRIMARY_IDENTITY, NULL);
      app->muxer_is_identity = TRUE;
      break;
    default:
      *muxer = gst_element_factory_make (NVGST_PRIMARY_MP4_MUXER, NULL);
      break;
  }

  if (!(*muxer)) {
    app->return_value = -1;
    NVGST_ERROR_MESSAGE ("Can't Create muxer element\n");
    return FALSE;
  }

  return TRUE;
}

static gboolean
camera_need_reconfigure (int new_res, CapturePadType current_pad)
{
  int preview, video, image, temp;
  if (new_res == app->capres.current_max_res) {
    return FALSE;
  }

  if (new_res > app->capres.current_max_res) {
    app->capres.current_max_res = new_res;
    return TRUE;
  }

  preview = app->capres.prev_res_index;
  video = app->capres.vid_res_index;
  image = app->capres.img_res_index;

  temp = MAX (preview, MAX (video, image));

  if (temp < app->capres.current_max_res) {
    app->capres.current_max_res = temp;
    return TRUE;
  }
  return FALSE;
}

/**
  * Initialize capture parameters.
  *
  * @param void
  */
static void
capture_init_params (void)
{
  app->mode = NVGST_DEFAULT_CAPTURE_MODE;
  app->file_type = NVGST_DEFAULT_FILE_TYPE;

  app->cam_src = NV_CAM_SRC_CSI;
  app->cap_success = FALSE;
  app->use_cus_res = FALSE;
  app->svs = NULL;

  app->first_frame = FALSE;
  app->enableKpiProfile = FALSE;
  app->enableKpiNumbers = FALSE;
  app->enableMeta = FALSE;
  app->flip_method = NVGST_DEFAULT_FLIP_METHOD;

  get_preview_resolution (PR_640x480);
  get_image_capture_resolution (IR_640x480);
  get_video_capture_resolution (VR_640x480);

  app->encset.image_enc = NVGST_DEFAULT_IMAGE_ENCODER;
  app->encset.video_enc = NVGST_DEFAULT_VIDEO_ENCODER;
  app->encset.hw_enc_type = HW_V4L2_ENC;
  set_encoder_bitrate (NVGST_DEFAULT_480P_ENCODER_BITRATE);
  set_encoder_profile (NVGST_DEFAULT_VIDEO_ENCODER_PROFILE);
  app->encset.controlrate = NVGST_DEFAULT_VIDEO_ENCODER_CONTROLRATE;
  app->encset.enabletwopassCBR = NVGST_DEFAULT_VIDEO_ENCODER_TWOPASSCBR;

  app->lock = malloc(sizeof(*(app->lock)));
  g_mutex_init (app->lock);
  app->cond = malloc(sizeof(*(app->cond)));
  g_cond_init (app->cond);
  app->x_cond = malloc(sizeof(*(app->x_cond)));
  g_cond_init (app->x_cond);

  app->native_record = GST_PAD_PROBE_DROP;
  app->file_name = g_strdup (NVGST_DEFAULT_FILENAME);
  app->vidcap_device = g_strdup (NVGST_DEFAULT_VIDCAP_DEVICE);
  app->overlayConfig = NULL;
  app->eglstream_producer_id = EGLSTREAM_PRODUCER_ID_SCF_CAMERA;
  app->eglConfig = NULL;
  app->color_format = -1;
  app->color_format_csi = CAPTURE_NV12;
  app->color_format_v4l2 = CAPTURE_YUY2;

  /* CSI Camera Default Property Values */
  app->whitebalance = NVGST_DEFAULT_WHITEBALANCE;
  app->saturation = NVGST_DEFAULT_SATURATION;
  app->sensor_id = NVGST_DEFAULT_SENSOR_ID;
  app->sensor_mode = NVGST_DEFAULT_SENSOR_MODE;
  app->display_id = NVGST_DEFAULT_DISPLAY_ID;
  app->exposure_timerange = NULL;
  app->gain_range = NULL;
  app->isp_digital_gainrange = NULL;
  app->enableAeLock = FALSE;
  app->enableAwbLock = FALSE;
  app->exposure_compensation = NVGST_DEFAULT_EXPOSURE_COMPENSATION;
  app->ae_antibanding = NVGST_DEFAULT_AEANTIBANDING;
  app->tnr_mode = NVGST_DEFAULT_TNR_MODE;
  app->ee_mode = NVGST_DEFAULT_EE_MODE;
  app->ee_strength = NVGST_DEFAULT_EE_STRENGTH;
  app->tnr_strength = NVGST_DEFAULT_TNR_STRENGTH;
  app->framerate = NVGST_DEFAULT_CAPTURE_FPS;

  /* Automation initialization */
  app->aut.automate = NVGST_DEFAULT_AUTOMATION_MODE;
  app->aut.capture_start_time = NVGST_DEFAULT_CAP_START_DELAY;
  app->aut.quit_time = NVGST_DEFAULT_QUIT_TIME;
  app->aut.iteration_count = NVGST_DEFAULT_ITERATION_COUNT;
  app->aut.capture_gap = NVGST_DEFAULT_CAPTURE_GAP;
  app->aut.capture_time = NVGST_DEFAULT_CAPTURE_TIME;
  app->aut.toggle_mode = NVGST_DEFAULT_TOGGLE_CAMERA_MODE;
  app->aut.toggle_sensor = NVGST_DEFAULT_TOGGLE_CAMERA_SENSOR;
  app->aut.toggle_sensor_modes = NVGST_DEFAULT_TOGGLE_CAMERA_SENSOR_MODES;
  app->aut.num_sensors = NVGST_DEFAULT_NUM_SENSORS;
  app->aut.enum_wb = NVGST_DEFAULT_ENUMERATE_WHITEBALANCE;
  app->aut.enum_st = NVGST_DEFAULT_ENUMERATE_SATURATION;
  app->aut.capture_auto = NVGST_DEFAULT_ENUMERATE_CAPTURE_AUTO;

  app->csi_options_argus = g_strdup ("Supported resolutions in case of ARGUS Camera\n"
      "  (2) : 640x480\n"
      "  (3) : 1280x720\n"
      "  (4) : 1920x1080\n"
      "  (5) : 2104x1560\n"
      "  (6) : 2592x1944\n"
      "  (7) : 2616x1472\n"
      "  (8) : 3840x2160\n"
      "  (9) : 3896x2192\n"
      "  (10): 4208x3120\n"
      "  (11): 5632x3168\n"
      "  (12): 5632x4224\n"
      "\nRuntime ARGUS Camera Commands:\n\n"
      "  Help : 'h'\n"
      "  Quit : 'q'\n"
      "  Set Capture Mode:\n"
      "      mo:<val>\n"
      "          (1): image\n"
      "          (2): video\n"
      "  Get Capture Mode:\n"
      "      gmo\n"
      "  Set sensor orientation:\n"
      "      so:<val>\n"
      "          (0): none\n"
      "          (1): Rotate counter-clockwise 90 degrees\n"
      "          (2): Rotate 180 degrees\n"
      "          (3): Rotate clockwise 90 degrees\n"
      "  Get sensor orientation:\n"
      "      gso\n"
      "  Set sensor mode:\n"
      "      smo:<val> e.g., smo:1\n"
      "  Get sensor mode:\n"
      "      gsmo\n"
      "  Set Whitebalance Mode:\n"
      "      wb:<val>\n"
      "          (0): off\n"
      "          (1): auto\n"
      "          (2): incandescent\n"
      "          (3): fluorescent\n"
      "          (4): warm-fluorescent\n"
      "          (5): daylight\n"
      "          (6): cloudy-daylight\n"
      "          (7): twilight\n"
      "          (8): shade\n"
      "          (9): manual\n"
      "  Get Whitebalance Mode:\n"
      "      gwb\n"
      "  Set Saturation (0 to 2):\n"
      "      st:<val> e.g., st:1.25\n"
      "  Get Saturation:\n"
      "      gst\n"
      "  Set Exposure Compensation (-2 to 2):\n"
      "      ec:<val> e.g., ec:-2\n"
      "  Get Exposure Compensation:\n"
      "      gec\n"
      "  Set Auto Whitebalance Lock:\n"
      "      awbl:<val> e.g., awbl:0\n"
      "  Get Auto Whitebalance Lock:\n"
      "      awbl\n"
      "  Set Auto Exposure Lock:\n"
      "      ael:<val> e.g., ael:0\n"
      "  Get Auto Exposure Lock:\n"
      "      gael\n"
      "  Set TNR Mode:\n"
      "      tnrm:<val> e.g., tnrm:1\n"
      "          (0): OFF\n"
      "          (1): FAST\n"
      "          (2): HIGH QUALITY\n"
      "  Get TNR Mode:\n"
      "      gtnrm\n"
      "  Set TNR Strength (-1 to 1):\n"
      "      tnrs:<val> e.g., tnrs:0.5\n"
      "  Get TNR Strength:\n"
      "      gtnrs\n"
      "  Set EE Mode:\n"
      "      eem:<val> e.g., eem:1\n"
      "          (0): OFF\n"
      "          (1): FAST\n"
      "          (2): HIGH QUALITY\n"
      "  Get EE Mode:\n"
      "      geem\n"
      "  Set EE Strength (-1 to 1):\n"
      "      ees:<val> e.g., ees:0.5\n"
      "  Get EE Strength:\n"
      "      gees\n"
      "  Set Auto Exposure Anti-Banding (0 to 3):\n"
      "      aeab:<val> e.g., aeab:2\n"
      "          (0): OFF\n"
      "          (1): MODE AUTO\n"
      "          (2): MODE 50HZ\n"
      "          (3): MODE 60HZ\n"
      "  Get Auto Exposure Anti-Banding:\n"
      "      gaeab\n"
      "  Set Gain Range:\n"
      "      gr:<val><space><val> e.g., gr:1 16\n"
      "  Get Gain Range:\n"
      "      ggr\n"
      "  Set Exposure Time Range:\n"
      "      etr:<val><space><val> e.g., etr:34000 35000\n"
      "  Get Exposure Time Range:\n"
      "      getr\n"
      "  Set ISP Digital Gain Range:\n"
      "      dgr:<val><space><val> e.g., dgr:2 152\n"
      "  Get ISP Digital Gain Range:\n"
      "      gdgr\n"
      "  Capture: enter 'j' OR\n"
      "           followed by a timer (e.g., jx5000, capture after 5 seconds) OR\n"
      "           followed by multishot count (e.g., j:6, capture 6 images)\n"
      "           timer/multihot values are optional, capture defaults to single shot with timer=0s\n"
      "  Start Recording : enter '1'\n"
      "  Stop Recording  : enter '0'\n"
      "  Video snapshot  : enter '2' (While recording video)\n"
      "  Get Preview Resolution:\n" "      gpcr\n"
      "  Get Image Capture Resolution:\n" "      gicr\n"
      "  Get Video Capture Resolution:\n" "      gvcr\n\n");
  app->csi_resolution = g_strdup ("Supported resolutions in case of NvArgusCamera\n"
      "  (2) : 640x480\n"
      "  (3) : 1280x720\n"
      "  (4) : 1920x1080\n"
      "  (5) : 2104x1560\n"
      "  (6) : 2592x1944\n"
      "  (7) : 2616x1472\n"
      "  (8) : 3840x2160\n"
      "  (9) : 3896x2192\n"
      "  (10): 4208x3120\n"
      "  (11): 5632x3168\n"
      "  (12): 5632x4224\n");

  app->usb_options = g_strdup ("Runtime USB Camera Commands:\n\n"
      "  Help : 'h'\n"
      "  Quit : 'q'\n"
      "  Set Capture Mode:\n"
      "      mo:<val>\n"
      "          (1): image\n"
      "          (2): video\n"
      "  Get Capture Mode:\n"
      "      gmo\n"
      "  Capture: enter 'j' OR\n"
      "           followed by a timer (e.g., jx5000, capture after 5 seconds) OR\n"
      "           followed by multishot count (e.g., j:6, capture 6 images)\n"
      "           timer/multihot values are optional, capture defaults to single shot with timer=0s\n"
      "  Start Recording : enter '1'\n"
      "  Stop Recording  : enter '0'\n"
      "  Set Preview Resolution:\n"
      "      pcr:<val> e.g., pcr:2\n"
      "          (0) : 176x144\n"
      "          (1) : 320x240\n"
      "          (2) : 640x480\n"
      "          (3) : 1280x720\n"
      "          (4) : 1920x1080\n"
      "  NOTE: Preview/Encode resolution will be same as Capture resolution for USB-Camera\n"
      "  Get Preview Resolution:\n" "      gpcr\n"
      "  Get Image Capture Resolution:\n" "      gicr\n"
      "  Get Video Capture Resolution:\n" "      gvcr\n"
      "  Set Capture Device Node:\n"
      "      cdn:<val> e.g., cdn:0\n"
      "          (0): /dev/video0\n"
      "          (1): /dev/video1\n"
      "          (2): /dev/video2\n"
      "  Get Capture Device Node:\n" "      gcdn\n\n");

  app->encoder_options = g_strdup ("Runtime encoder configuration options:\n\n"
      "  Set Encoding Bit-rate(in bytes):\n"
      "      br:<val> e.g., br:4000000\n"
      "  Get Encoding Bit-rate(in bytes):\n" "      gbr\n"
      "  Set Encoding Profile(only for H.264):\n"
      "      ep:<val> e.g., ep:1\n"
      "          (0): Baseline\n"
      "          (1): Main\n"
      "          (2): High\n"
      "  Get Encoding Profile(only for H.264):\n" "      gep\n"
      "  Force IDR Frame on video Encoder(only for H.264):\n"
      "      Enter 'f' \n\n");
}

/**
  * Verification for capture parameters.
  *
  * @param void
  */
static gboolean
check_capture_params (void)
{
  gboolean ret = TRUE;

  if ((app->mode < 0) ||
      (app->capres.preview_width < 176) ||
      (app->capres.preview_height < 144) ||
      (app->encset.video_enc < FORMAT_H264_HW) ||
      (app->encset.image_enc < FORMAT_JPEG_SW))
    ret = FALSE;

  return ret;
}

/**
  * Write encoded image to file.
  *
  * @param fsink  : image sink
  * @param buffer : gst buffer
  * @param pad    : element pad
  * @param udata  : the gpointer to user data
  */
static void
cam_image_captured (GstElement * fsink,
    GstBuffer * buffer, GstPad * pad, gpointer udata)
{
  GstMapInfo info;

  if (app->capcount == 0) {
    if (gst_buffer_map (buffer, &info, GST_MAP_READ)) {
      if (info.size) {
        FILE *fp = NULL;
        gchar outfile[100];
        gchar temp[100];
        memset (outfile, 0, sizeof (outfile));
        memset (temp, 0, sizeof (temp));

        strncat (outfile, app->file_name, sizeof(outfile) - 1);
        sprintf (outfile + strlen(outfile), "_%ld", (long) getpid());
        sprintf (temp, "_s%02d_%05d.jpg", app->sensor_id, app->capture_count++);
        strcat (outfile, temp);

        CALL_GUI_FUNC (show_text, "Image saved to %s", outfile);

        fp = fopen (outfile, "wb");
        if (fp == NULL) {
          g_print ("Can't open file for Image Capture!\n");
          app->cap_success = FALSE;
        } else {
          if (info.size != fwrite (info.data, 1, info.size, fp)) {
            g_print ("Can't write data in file, No Space left on Device!\n");
            app->cap_success = FALSE;
            fclose (fp);
            if (remove (outfile) != 0)
              g_print ("Unable to delete the file\n");
          } else {
            app->cap_success = TRUE;
            fclose (fp);
          }
        }
      }

      app->capcount++;
      app->native_record = GST_PAD_PROBE_DROP;
      gst_buffer_unmap (buffer, &info);

      g_mutex_lock (app->lock);
      recording = FALSE;
      g_cond_signal (app->cond);
      g_mutex_unlock (app->lock);
    } else {
      NVGST_WARNING_MESSAGE ("image buffer probe failed\n");
    }
  }
}

/**
  * Buffer probe on preview.
  *
  * @param pad   : the GstPad that is blocked
  * @param info  : GstPadProbeInfo
  * @param udata : the gpointer to user data
  */
static GstPadProbeReturn
prev_buf_prob (GstPad * pad, GstPadProbeInfo * info, gpointer u_data)
{
  GstBuffer * buffer = NULL;
  buffer = (GstBuffer *) info->data;
  AuxData *sensor_metadata = NULL;
  GQuark gst_buffer_metadata_quark = 0;

  gst_buffer_metadata_quark = g_quark_from_static_string ("GstBufferMetaData");
  sensor_metadata = (AuxData *) gst_mini_object_get_qdata (GST_MINI_OBJECT_CAST (buffer),
        gst_buffer_metadata_quark);

  if(sensor_metadata && app->enableMeta)
    GST_INFO_OBJECT (pad, "nvgstcapture: Frame %" G_GINT64_FORMAT "Timestamp %" G_GINT64_FORMAT"\n",
        sensor_metadata->frame_num, sensor_metadata->timestamp);

  if (!app->first_frame && app->enableKpiNumbers) {
    GET_TIMESTAMP(FIRST_FRAME);
    app->first_frame = TRUE;
    time_t launch_time = app->timeStampStore[FIRST_FRAME] - app->timeStampStore[APP_LAUNCH];
    g_print("\nKPI launch time in mS: %ld\n", (launch_time / 1000));
  }

  if (app->enableKpiNumbers) {

    if (app->currentFrameTime != 0) {
      app->prevFrameTime = app->currentFrameTime;
    }

    GET_TIMESTAMP(CURRENT_EVENT);
    app->currentFrameTime = app->timeStampStore[CURRENT_EVENT];

    if (app->prevFrameTime != 0) {
      app->accumulator += (app->currentFrameTime - app->prevFrameTime) / 1000;
    }
    app->frameCount++;
  }

  return GST_PAD_PROBE_OK;
}

/**
  * Buffer probe on preview.
  *
  * @param pad   : the GstPad that is blocked
  * @param info  : GstPadProbeInfo
  * @param udata : the gpointer to user data
  */
static GstPadProbeReturn
enc_buf_prob (GstPad * pad, GstPadProbeInfo * info, gpointer u_data)
{
  if (app->enableKpiNumbers) {

    if (app->currentEncFrameTime != 0) {
      app->prevEncFrameTime = app->currentEncFrameTime;
    }

    GET_TIMESTAMP(CURRENT_EVENT);
    app->currentEncFrameTime = app->timeStampStore[CURRENT_EVENT];

    if (app->prevEncFrameTime != 0) {
      app->encAccumulator += (app->currentEncFrameTime - app->prevEncFrameTime) / 1000;
    }
    app->encFrameCount++;
  }

  return app->native_record;
}

static gboolean
create_csi_cap_bin (void)
{
  GstPad *pad = NULL;
  GstCaps *caps = NULL;
  GstCapsFeatures *feature = NULL;
  gint width = 0, height = 0;
  gchar *str_color = NULL;

  app->use_eglstream = 0;

  if (app->cam_src == NV_CAM_SRC_CSI)
  {
    /* Create the capture source element */
    app->ele.vsrc = gst_element_factory_make (NVGST_VIDEO_CAPTURE_SRC_CSI_ARGUS, NULL);
    if (!app->ele.vsrc) {
      NVGST_ERROR_MESSAGE_V ("Element %s creation failed \n",
          NVGST_VIDEO_CAPTURE_SRC_CSI_ARGUS);
      goto fail;
    }

    /*CSI camera properties tuning */
    g_object_set (G_OBJECT (app->ele.vsrc), "wbmode", app->whitebalance, NULL);
    g_object_set (G_OBJECT (app->ele.vsrc), "timeout", app->timeout, NULL);
    g_object_set (G_OBJECT (app->ele.vsrc), "saturation", app->saturation, NULL);
    g_object_set (G_OBJECT (app->ele.vsrc), "sensor-id", app->sensor_id, NULL);
    g_object_set (G_OBJECT (app->ele.vsrc), "sensor-mode", app->sensor_mode, NULL);
    g_object_set (G_OBJECT (app->ele.vsrc), "aelock", app->enableAeLock, NULL);
    g_object_set (G_OBJECT (app->ele.vsrc), "awblock", app->enableAwbLock, NULL);
    g_object_set (G_OBJECT (app->ele.vsrc), "exposurecompensation", app->exposure_compensation, NULL);
    g_object_set (G_OBJECT (app->ele.vsrc), "aeantibanding", app->ae_antibanding, NULL);
    g_object_set (G_OBJECT (app->ele.vsrc), "tnr-mode", app->tnr_mode , NULL);
    g_object_set (G_OBJECT (app->ele.vsrc), "ee-mode", app->ee_mode , NULL);
    g_object_set (G_OBJECT (app->ele.vsrc), "tnr-strength", app->tnr_strength, NULL);
    g_object_set (G_OBJECT (app->ele.vsrc), "ee-strength", app->ee_strength, NULL);

    if (app->exposure_timerange != NULL)
      g_object_set (G_OBJECT (app->ele.vsrc), "exposuretimerange", app->exposure_timerange, NULL);

    if (app->gain_range != NULL)
      g_object_set (G_OBJECT (app->ele.vsrc), "gainrange", app->gain_range, NULL);

    if (app->isp_digital_gainrange != NULL)
      g_object_set (G_OBJECT (app->ele.vsrc), "ispdigitalgainrange", app->isp_digital_gainrange, NULL);

    if (app->overlayConfig)
      set_overlay_configuration (app->overlayConfig);
    if (app->timeout > 0) {
        /* Extra 3 seconds are required for ARGUS clean up */
        g_timeout_add_seconds (app->timeout + 3, exit_capture, NULL);
    }
    if (app->color_format != -1)
      app->color_format_csi = app->color_format;
    switch(app->color_format_csi){
      case CAPTURE_NV12:
        str_color = "NV12";
        break;
      case CAPTURE_I420:
      default:
        g_print("Color format not supported for NV_CAMERA_ARGUS camera\n");
        goto fail;
    }
  }
  else if (app->cam_src == NV_CAM_SRC_EGLSTREAM)
  {
    /* Create the capture source element */
    app->ele.vsrc = gst_element_factory_make (NVGST_EGLSTREAM_CAPTURE_SRC, NULL);
    if (!app->ele.vsrc) {
      NVGST_ERROR_MESSAGE_V ("Element %s creation failed \n",
          NVGST_VIDEO_CAPTURE_SRC_TEST);
      goto fail;
    }

    // Pass display and stream
    g_object_set (G_OBJECT (app->ele.vsrc), "display", app->display, NULL);
    g_object_set (G_OBJECT (app->ele.vsrc), "eglstream", app->stream, NULL);
    app->use_eglstream = 1;
    app->cam_src = NV_CAM_SRC_CSI;
    g_print ("Setting display=%p and EGLStream=%p EGLStream_Producer_ID=%d\n",
        app->display, app->stream, app->eglstream_producer_id);

    if (app->overlayConfig)
      set_overlay_configuration (app->overlayConfig);
  }

  if (app->color_format != -1)
    app->color_format_csi = app->color_format;
  switch(app->color_format_csi){
     case CAPTURE_I420:
       str_color = "I420";
       break;
     case CAPTURE_NV12:
       str_color = "NV12";
       break;
     default:
      g_print("Color format not supported for CSI camera\n");
      goto fail;
  }

  app->ele.cap_filter =
    gst_element_factory_make (NVGST_DEFAULT_CAPTURE_FILTER, NULL);
  if (!app->ele.cap_filter) {
    NVGST_ERROR_MESSAGE_V ("Element %s creation failed \n",
        NVGST_DEFAULT_CAPTURE_FILTER);
    goto fail;
  }

  app->capres.current_max_res =
    MAX (app->capres.prev_res_index, MAX (app->capres.vid_res_index,
          app->capres.img_res_index));
  get_max_resolution (app->capres.current_max_res, &width, &height);
  caps =
    gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING, str_color,
        "width", G_TYPE_INT, width, "height", G_TYPE_INT, height, "framerate",
        GST_TYPE_FRACTION, app->framerate, 1, NULL);

  feature = gst_caps_features_new ("memory:NVMM", NULL);
  gst_caps_set_features (caps, 0, feature);

  /* Set capture caps on capture filter */
  g_object_set (app->ele.cap_filter, "caps", caps, NULL);
  gst_caps_unref (caps);

  /* Create capture pipeline bin */
  app->ele.capbin = gst_bin_new ("cap_bin");
  gst_bin_add_many (GST_BIN (app->ele.capbin), app->ele.vsrc,
    app->ele.cap_filter, NULL);

  if ((gst_element_link (app->ele.vsrc, app->ele.cap_filter)) != TRUE) {
    NVGST_ERROR_MESSAGE ("Elements could not link nvcamsrc & caps filter\n");
    goto fail;
  }

  pad = gst_element_get_static_pad (app->ele.cap_filter, "src");
  if (!pad) {
    NVGST_ERROR_MESSAGE ("can't get static src pad of capture filter\n");
    goto fail;
  }
  gst_element_add_pad (app->ele.capbin, gst_ghost_pad_new ("src", pad));
  gst_object_unref (GST_OBJECT (pad));

  return TRUE;

fail:
  app->return_value = -1;
  return FALSE;
}


/**
  * Create capture bin
  *
  * @param void :
  */
static gboolean
create_cap_bin (void)
{
  GstPad *pad = NULL;
  GstCaps *caps = NULL;
  gchar *str_color = NULL;
  /* Create the capture source element */
  if (app->cam_src == NV_CAM_SRC_TEST) {
    app->ele.vsrc =
        gst_element_factory_make (NVGST_VIDEO_CAPTURE_SRC_TEST, NULL);
    if (!app->ele.vsrc) {
      NVGST_ERROR_MESSAGE_V ("Element %s creation failed \n",
          NVGST_VIDEO_CAPTURE_SRC_TEST);
      goto fail;
    }
    g_object_set (G_OBJECT (app->ele.vsrc), "is-live", TRUE, NULL);
  } else {
    app->ele.vsrc =
        gst_element_factory_make (NVGST_VIDEO_CAPTURE_SRC_V4L2, NULL);
    if (!app->ele.vsrc) {
      NVGST_ERROR_MESSAGE_V ("Element %s creation failed \n",
          NVGST_VIDEO_CAPTURE_SRC_V4L2);
      goto fail;
    }
    g_object_set (G_OBJECT (app->ele.vsrc), "device", app->vidcap_device, NULL);
  }

  app->ele.cap_filter =
      gst_element_factory_make (NVGST_DEFAULT_CAPTURE_FILTER, NULL);
  if (!app->ele.cap_filter) {
    NVGST_ERROR_MESSAGE_V ("Element %s creation failed \n",
        NVGST_DEFAULT_CAPTURE_FILTER);
    goto fail;
  }
  if (app->color_format != -1)
    app->color_format_v4l2 = app->color_format;

  switch(app->color_format_v4l2){
    case CAPTURE_I420 :
      str_color = "I420";
      break;
    case CAPTURE_YUY2 :
      str_color = "YUY2";
      break;
    case CAPTURE_UYVY :
      str_color = "UYVY";
      break;
    default :
      g_print("Color format is not supported on V4L2 \n");
      goto fail;
  }

  caps = gst_caps_new_simple (NVGST_DEFAULT_VIDEO_MIMETYPE,
      "format", G_TYPE_STRING, str_color,
      "width", G_TYPE_INT, app->capres.preview_width,
      "height", G_TYPE_INT, app->capres.preview_height, NULL);

  /* Set capture caps on capture filter */
  g_object_set (app->ele.cap_filter, "caps", caps, NULL);
  gst_caps_unref (caps);

  /* Create capture pipeline bin */
  app->ele.capbin = gst_bin_new ("cap_bin");
  gst_bin_add_many (GST_BIN (app->ele.capbin), app->ele.vsrc,
      app->ele.cap_filter, NULL);
  if (!gst_element_link_many (app->ele.vsrc, app->ele.cap_filter,
          NULL)) {
    NVGST_ERROR_MESSAGE_V ("Element link fail between %s & %s \n",
        NVGST_VIDEO_CAPTURE_SRC_V4L2, NVGST_DEFAULT_CAPTURE_FILTER);
    goto fail;
  }

  pad = gst_element_get_static_pad (app->ele.cap_filter, "src");
  if (!pad) {
    NVGST_ERROR_MESSAGE ("can't get static src pad of capture filter\n");
    goto fail;
  }
  gst_element_add_pad (app->ele.capbin, gst_ghost_pad_new ("src", pad));
  gst_object_unref (GST_OBJECT (pad));

  return TRUE;

fail:
  app->return_value = -1;
  return FALSE;
}

/**
  * Create video sink bin
  *
  * @param void :
  */
static gboolean
create_svs_bin (void)
{
  GstPad *pad = NULL;

  /* Create render pipeline bin */
  app->ele.svsbin = gst_bin_new ("svs_bin");

#if GUI
  app->svs = NULL;
#endif

  if (app->svs == NULL) {
    switch (app->cam_src) {
      case NV_CAM_SRC_CSI:
        app->svs = NVGST_DEFAULT_PREVIEW_SINK_CSI;
        break;
      case NV_CAM_SRC_V4L2:
        app->svs = NVGST_DEFAULT_PREVIEW_SINK_USB;
        break;
      case NV_CAM_SRC_TEST:
        app->svs = NVGST_DEFAULT_PREVIEW_SINK_USB;
        break;
      default:
        g_print ("Invalid camera source, svs not set.\n");
    }
  }

  app->ele.vsink = gst_element_factory_make (app->svs, NULL);
  if (!app->ele.vsink) {
    NVGST_ERROR_MESSAGE_V ("Element %s creation failed \n", app->svs);
    goto fail;
  }
  g_object_set (G_OBJECT (app->ele.vsink), "async", FALSE, NULL);
  g_object_set (G_OBJECT (app->ele.vsink), "sync", FALSE, NULL);

  /* Create the colorspace converter element */
  if (!g_strcmp0 ("ximagesink", app->svs) ||
      !g_strcmp0 ("xvimagesink", app->svs)) {
    app->ele.colorspace_conv =
        gst_element_factory_make (NVGST_DEFAULT_VIDEO_CONVERTER, NULL);
    if (!app->ele.colorspace_conv) {
      NVGST_ERROR_MESSAGE_V ("Element %s creation failed \n",
          NVGST_DEFAULT_VIDEO_CONVERTER);
      goto fail;
    }
    gst_bin_add_many (GST_BIN (app->ele.svsbin), app->ele.colorspace_conv,
        app->ele.vsink, NULL);
    if (!gst_element_link (app->ele.colorspace_conv, app->ele.vsink)) {
      NVGST_ERROR_MESSAGE_V ("Element link fail between %s & %s \n",
          NVGST_DEFAULT_VIDEO_CONVERTER, app->svs);
      goto fail;
    }
    pad = gst_element_get_static_pad (app->ele.colorspace_conv, "sink");
  } else if (!g_strcmp0 ("nveglglessink", app->svs)) {
    app->ele.colorspace_conv =
      gst_element_factory_make ("nvegltransform", NULL);

    if (!app->ele.colorspace_conv) {
      NVGST_ERROR_MESSAGE ("Element nvegltransform creation failed \n");
      goto fail;
    }
    gst_bin_add_many (GST_BIN (app->ele.svsbin), app->ele.colorspace_conv,
        app->ele.vsink, NULL);
    if (!gst_element_link (app->ele.colorspace_conv, app->ele.vsink)) {
      NVGST_ERROR_MESSAGE_V ("Element link fail between %s & %s \n",
          NVGST_DEFAULT_VIDEO_CONVERTER, app->svs);
      goto fail;
    }
    pad = gst_element_get_static_pad (app->ele.colorspace_conv, "sink");

    if (app->eglConfig) {
      set_egl_window_config (app->eglConfig);
    }
  } else {
    gst_bin_add (GST_BIN (app->ele.svsbin), app->ele.vsink);
    pad = gst_element_get_static_pad (app->ele.vsink, "sink");

    if (app->overlayConfig) {
      g_object_set (G_OBJECT (app->ele.vsink), "overlay", app->overlay_index,
          NULL);
      g_object_set (G_OBJECT (app->ele.vsink), "overlay-x", app->overlay_x_pos,
          NULL);
      g_object_set (G_OBJECT (app->ele.vsink), "overlay-y", app->overlay_y_pos,
          NULL);
      g_object_set (G_OBJECT (app->ele.vsink), "overlay-w", app->overlay_width,
          NULL);
      g_object_set (G_OBJECT (app->ele.vsink), "overlay-h", app->overlay_height,
          NULL);
    }
  }

#if GUI
  gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (app->ele.vsink),
      CALL_GUI_FUNC (get_video_window));
#else
{
  GstElement *vsink = app->ele.vsink;
  if (vsink && GST_IS_VIDEO_OVERLAY (vsink)) {
    if (!app->disp.mDisplay) {
      nvgst_x11_init (&app->disp);
    }

    if (app->capres.preview_width < app->disp.display_width
        || app->capres.preview_height < app->disp.display_height) {
      app->disp.width = app->capres.preview_width;
      app->disp.height = app->capres.preview_height;
    } else {
      app->disp.width = app->disp.display_width;
      app->disp.height = app->disp.display_height;
    }
    g_mutex_lock (app->lock);

    if (app->disp.window)
      nvgst_destroy_window (&app->disp);
    nvgst_create_window (&app->disp, "nvgstcapture-1.0");
    app->x_event_thread = g_thread_new ("nvgst-window-event-thread",
        nvgst_x_event_thread, app);
    gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (vsink),
        (gulong) app->disp.window);
    gst_video_overlay_expose (GST_VIDEO_OVERLAY (vsink));

    g_mutex_unlock (app->lock);
 }
}
#endif

  if (!pad) {
    NVGST_ERROR_MESSAGE ("can't get static sink pad\n");
    goto fail;
  }
  gst_element_add_pad (app->ele.svsbin, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (GST_OBJECT (pad));

  return TRUE;

fail:
  app->return_value = -1;
  return FALSE;
}

/**
  * Create preview scaling bin
  *
  * @param void :
  */
static gboolean
create_preview_scaling_bin (void)
{
  GstPad *sinkpad = NULL;
  GstPad *srcpad = NULL;
  GstCaps *caps = NULL;
  GstCapsFeatures *feature = NULL;

  /* Create scaling pipeline bin */
  app->ele.svc_prebin = gst_bin_new ("svc_prev_bin");

  app->ele.svc_prevconv =
      gst_element_factory_make (NVGST_DEFAULT_VIDEO_CONVERTER_CSI, NULL);
  if (!app->ele.svc_prevconv) {
    NVGST_ERROR_MESSAGE_V ("svc_prev_bin Element %s creation failed \n",
        NVGST_DEFAULT_VIDEO_CONVERTER_CSI);
    goto fail;
  }

  g_object_set (app->ele.svc_prevconv , "flip-method", app->flip_method, NULL);

  /* Create the capsfilter element */
  {
    app->ele.svc_prevconv_out_filter =
        gst_element_factory_make (NVGST_DEFAULT_CAPTURE_FILTER, NULL);
    if (!app->ele.svc_prevconv_out_filter) {
      NVGST_ERROR_MESSAGE_V ("svc_prev_bin Element %s creation failed \n",
          NVGST_DEFAULT_CAPTURE_FILTER);
      goto fail;
    }

    caps =
        gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING, "I420",
        "width", G_TYPE_INT, app->capres.preview_width, "height", G_TYPE_INT,
        app->capres.preview_height, NULL);

    if (!g_strcmp0 (app->svs, "nvhdmioverlaysink") ||
#ifdef WITH_STREAMING
        app->streaming_mode ||
#endif
        !g_strcmp0 (app->svs, "nveglglessink")) {
      feature = gst_caps_features_new ("memory:NVMM", NULL);
      gst_caps_set_features (caps, 0, feature);
    }

    /* Set capture caps on capture filter */
    g_object_set (app->ele.svc_prevconv_out_filter, "caps", caps, NULL);
    gst_caps_unref (caps);

    gst_bin_add_many (GST_BIN (app->ele.svc_prebin),
        app->ele.svc_prevconv_out_filter, app->ele.svc_prevconv, NULL);
    if (!gst_element_link_many (app->ele.svc_prevconv,
            app->ele.svc_prevconv_out_filter, NULL)) {
      NVGST_ERROR_MESSAGE_V
          ("svc_prev_bin Element link fail between %s & %s \n",
          NVGST_DEFAULT_CAPTURE_FILTER, NVGST_DEFAULT_VIDEO_CONVERTER_CSI);
      goto fail;
    }
    sinkpad = gst_element_get_static_pad (app->ele.svc_prevconv, "sink");
    srcpad =
        gst_element_get_static_pad (app->ele.svc_prevconv_out_filter, "src");
  }

  if (!sinkpad || !srcpad) {
    NVGST_ERROR_MESSAGE ("svc_prev_bin can't get static sink/src pad\n");
    goto fail;
  }
  gst_element_add_pad (app->ele.svc_prebin, gst_ghost_pad_new ("sink",
          sinkpad));
  gst_element_add_pad (app->ele.svc_prebin, gst_ghost_pad_new ("src", srcpad));
  gst_object_unref (GST_OBJECT (sinkpad));
  gst_object_unref (GST_OBJECT (srcpad));

  return TRUE;

fail:
  app->return_value = -1;
  return FALSE;
}

/**
  * Create image scaling bin
  *
  * @param void :
  */
static gboolean
create_image_scaling_bin (void)
{
  GstPad *sinkpad = NULL;
  GstPad *srcpad = NULL;
  GstCaps *caps = NULL;
  GstCapsFeatures *feature = NULL;

  /* Create image scaling pipeline bin */
  app->ele.svc_imgbin = gst_bin_new ("svc_img_bin");

  app->ele.svc_imgvconv =
      gst_element_factory_make (NVGST_DEFAULT_VIDEO_CONVERTER_CSI, NULL);
  if (!app->ele.svc_imgvconv) {
    NVGST_ERROR_MESSAGE_V ("Element %s creation failed \n",
        NVGST_DEFAULT_VIDEO_CONVERTER_CSI);
    goto fail;
  }

  g_object_set (app->ele.svc_imgvconv , "flip-method", app->flip_method, NULL);

  /* Create the capsfilter element */
  {
    app->ele.svc_imgvconv_out_filter =
        gst_element_factory_make (NVGST_DEFAULT_CAPTURE_FILTER, NULL);
    if (!app->ele.svc_imgvconv_out_filter) {
      NVGST_ERROR_MESSAGE_V ("svc_img_bin Element %s creation failed \n",
          NVGST_DEFAULT_CAPTURE_FILTER);
      goto fail;
    }

    caps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "I420",
        "width", G_TYPE_INT, app->capres.image_cap_width,
        "height", G_TYPE_INT, app->capres.image_cap_height, NULL);

    if (app->encset.image_enc == FORMAT_JPEG_HW) {
      feature = gst_caps_features_new ("memory:NVMM", NULL);
      gst_caps_set_features (caps, 0, feature);
    }

    /* Set capture caps on capture filter */
    g_object_set (app->ele.svc_imgvconv_out_filter, "caps", caps, NULL);
    gst_caps_unref (caps);

    gst_bin_add_many (GST_BIN (app->ele.svc_imgbin),
        app->ele.svc_imgvconv_out_filter, app->ele.svc_imgvconv, NULL);
    if (!gst_element_link_many (app->ele.svc_imgvconv,
            app->ele.svc_imgvconv_out_filter, NULL)) {
      NVGST_ERROR_MESSAGE_V ("svc_img_bin Element link fail between %s & %s \n",
          NVGST_DEFAULT_CAPTURE_FILTER, NVGST_DEFAULT_VIDEO_CONVERTER_CSI);
      goto fail;
    }
    sinkpad = gst_element_get_static_pad (app->ele.svc_imgvconv, "sink");
    srcpad =
        gst_element_get_static_pad (app->ele.svc_imgvconv_out_filter, "src");
  }

  if (!sinkpad || !srcpad) {
    NVGST_ERROR_MESSAGE ("svc_img_bin can't get static sink/src pad\n");
    goto fail;
  }
  gst_element_add_pad (app->ele.svc_imgbin, gst_ghost_pad_new ("sink",
          sinkpad));
  gst_element_add_pad (app->ele.svc_imgbin, gst_ghost_pad_new ("src", srcpad));
  gst_object_unref (GST_OBJECT (sinkpad));
  gst_object_unref (GST_OBJECT (srcpad));

  return TRUE;

fail:
  app->return_value = -1;
  return FALSE;
}

/**
  * Create video scaling bin
  *
  * @param void :
  */
static gboolean
create_video_scaling_bin (void)
{
  GstPad *sinkpad = NULL;
  GstPad *srcpad = NULL;
  GstCaps *caps = NULL;
  GstCapsFeatures *feature = NULL;

  /* Create scaling pipeline bin */
  app->ele.svc_vidbin = gst_bin_new ("svc_vid_bin");

  app->ele.svc_vidvconv =
      gst_element_factory_make (NVGST_DEFAULT_VIDEO_CONVERTER_CSI, NULL);
  if (!app->ele.svc_vidvconv) {
    NVGST_ERROR_MESSAGE_V ("svc_vid_bin Element %s creation failed \n",
        NVGST_DEFAULT_VIDEO_CONVERTER_CSI);
    goto fail;
  }

  g_object_set (app->ele.svc_vidvconv , "flip-method", app->flip_method, NULL);

  /* Create the capsfilter element */
  {
    app->ele.svc_vidvconv_out_filter =
        gst_element_factory_make (NVGST_DEFAULT_CAPTURE_FILTER, NULL);
    if (!app->ele.svc_vidvconv_out_filter) {
      NVGST_ERROR_MESSAGE_V ("svc_vid_bin Element %s creation failed \n",
          NVGST_DEFAULT_CAPTURE_FILTER);
      goto fail;
    }

    caps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12",
        "width", G_TYPE_INT, app->capres.video_cap_width,
        "height", G_TYPE_INT, app->capres.video_cap_height, NULL);

    if (app->encset.video_enc != FORMAT_H264_SW)
    {
      feature = gst_caps_features_new("memory:NVMM", NULL);
      gst_caps_set_features (caps, 0, feature);
    }

    /* Set capture caps on capture filter */
    g_object_set (app->ele.svc_vidvconv_out_filter, "caps", caps, NULL);
    gst_caps_unref (caps);

    gst_bin_add_many (GST_BIN (app->ele.svc_vidbin),
        app->ele.svc_vidvconv_out_filter, app->ele.svc_vidvconv, NULL);
    if (!gst_element_link_many (app->ele.svc_vidvconv,
            app->ele.svc_vidvconv_out_filter, NULL)) {
      NVGST_ERROR_MESSAGE_V ("svc_vid_bin Element link fail between %s & %s \n",
          NVGST_DEFAULT_CAPTURE_FILTER, NVGST_DEFAULT_VIDEO_CONVERTER_CSI);
      goto fail;
    }
    sinkpad = gst_element_get_static_pad (app->ele.svc_vidvconv, "sink");
    srcpad =
        gst_element_get_static_pad (app->ele.svc_vidvconv_out_filter, "src");
  }

  if (!sinkpad || !srcpad) {
    NVGST_ERROR_MESSAGE ("svc_vid_bin can't get static sink/src pad\n");
    goto fail;
  }
  gst_element_add_pad (app->ele.svc_vidbin, gst_ghost_pad_new ("sink",
          sinkpad));
  gst_element_add_pad (app->ele.svc_vidbin, gst_ghost_pad_new ("src", srcpad));
  gst_object_unref (GST_OBJECT (sinkpad));
  gst_object_unref (GST_OBJECT (srcpad));

  return TRUE;

fail:
  app->return_value = -1;
  return FALSE;
}

/**
  * Create encode bin
  *
  * @param void :
  */

static gboolean
create_img_enc_bin (void)
{
  GstPad *pad = NULL;

  app->ele.img_bin = gst_bin_new ("img_bin");

  /* Create image encode chain elements */
  app->ele.img_enc_conv = gst_element_factory_make (NVGST_DEFAULT_IMAGE_ENC_CONVERTER, NULL);
  if (!app->ele.img_enc_conv) {
    NVGST_ERROR_MESSAGE ("nvvidconv element could not be created for image encode.\n");
    goto fail;
  }

  if (!get_image_encoder (&app->ele.img_enc)) {
    NVGST_ERROR_MESSAGE ("Image encoder element could not be created.\n");
    goto fail;
  }

  app->ele.img_sink = gst_element_factory_make (NVGST_DEFAULT_IENC_SINK, NULL);
  if (!app->ele.img_sink) {
    NVGST_ERROR_MESSAGE ("Image sink element could be created.\n");
    goto fail;
  }
  g_object_set (G_OBJECT (app->ele.img_sink), "signal-handoffs", TRUE, NULL);
  g_signal_connect (G_OBJECT (app->ele.img_sink), "handoff",
      G_CALLBACK (cam_image_captured), NULL);

  gst_bin_add_many (GST_BIN (app->ele.img_bin), app->ele.img_enc_conv,
      app->ele.img_enc, app->ele.img_sink, NULL);

  if ((gst_element_link (app->ele.img_enc_conv, app->ele.img_enc)) != TRUE) {
    NVGST_ERROR_MESSAGE ("Elements could not link iconverter & iencoder\n");
    goto fail;
  }

  if ((gst_element_link (app->ele.img_enc, app->ele.img_sink)) != TRUE) {
    NVGST_ERROR_MESSAGE ("Elements could not link iencoder & image_sink\n");
    goto fail;
  }

  pad = gst_element_get_static_pad (app->ele.img_enc_conv, "sink");
  if (!pad) {
    NVGST_ERROR_MESSAGE ("can't get static sink pad of iconverter\n");
    goto fail;
  }
  gst_element_add_pad (app->ele.img_bin, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (GST_OBJECT (pad));

  return TRUE;

fail:
  app->return_value = -1;
  return FALSE;
}

#ifdef WITH_STREAMING
static void
rtsp_video_stream_new (GObject * media)
{
  GstCaps *appsrc_caps;
  GstElement *bin;
  GstElement *appsrc;

  create_capture_pipeline ();

  g_object_get (media, "element", &bin, NULL);

  appsrc = gst_bin_get_by_name_recurse_up (GST_BIN (bin), "mysrc");
  app->video_streaming_ctx.appsrc = appsrc;

  gst_util_set_object_arg (G_OBJECT (appsrc), "format", "time");
  g_object_set (G_OBJECT (appsrc), "is-live", TRUE, NULL);

  switch (app->encset.video_enc) {
    case FORMAT_H264_HW:
      appsrc_caps =
          gst_caps_from_string
          ("video/x-h264, stream-format=byte-stream, alignment=au");
      break;
    case FORMAT_VP8_HW:
      appsrc_caps = gst_caps_from_string ("video/x-vp8");
      break;
    case FORMAT_H265_HW:
      appsrc_caps = gst_caps_from_string ("video/x-h265");
      break;
    case FORMAT_VP9_HW:
      appsrc_caps = gst_caps_from_string ("video/x-vp9");
      break;
    case FORMAT_H264_SW:
      appsrc_caps =
          gst_caps_from_string("video/x-h264, stream-format=byte-stream, alignment=au");
      break;
    default:
      appsrc_caps = gst_caps_from_string ("video/x-h264");
      break;
  }
  g_object_set (G_OBJECT (appsrc), "caps", appsrc_caps, NULL);

  gst_caps_unref (appsrc_caps);
}

static void
rtsp_video_stream_start (void)
{
}

static void
rtsp_video_stream_pause (void)
{
  if (app->streaming_mode == 2)
    stop_video_capture ();
}

static void
rtsp_video_stream_resume (void)
{
  restart_capture_pipeline ();
  if (app->streaming_mode == 2)
    start_video_capture ();
}

static void
rtsp_video_stream_stop (void)
{
  if (app->streaming_mode == 2) {
    stop_video_capture ();
    g_usleep(100000);
  }
  destroy_capture_pipeline ();
}

static GstFlowReturn
rtsp_video_appsink_new_sample (GstAppSink * appsink, gpointer user_data)
{
  GstSample *sample = NULL;
  GstBuffer *buffer;
  GstAppSrc *appsrc = GST_APP_SRC (app->video_streaming_ctx.appsrc);

  sample = gst_app_sink_pull_sample (GST_APP_SINK (app->ele.vsink));

  buffer = gst_sample_get_buffer (sample);
  if (!buffer)
    return GST_FLOW_OK;

  if (!appsrc) {
    gst_sample_unref (sample);
    return GST_FLOW_OK;
  }

  gst_buffer_ref(buffer);
  gst_sample_unref(sample);

  return gst_app_src_push_buffer (GST_APP_SRC (appsrc), buffer);
}

static void
cb_streaming_dbin_newpad (GstElement * decodebin, GstPad * pad, gpointer data)
{
  GstCaps *caps = gst_pad_query_caps (pad, NULL);
  const GstStructure *str = gst_caps_get_structure (caps, 0);
  const gchar *name = gst_structure_get_name (str);

  if (!strncmp (name, "video", 5)) {
    GstPad *sinkpad = gst_element_get_static_pad (
                app->video_streaming_ctx.streaming_file_src_conv,
                "sink");
    if (!sinkpad) {
      NVGST_ERROR_MESSAGE ("could not get pads to link uridecodebin & nvvidconv\n");
      goto done;
    }

    if (GST_PAD_LINK_FAILED (gst_pad_link (pad, sinkpad))) {
      NVGST_ERROR_MESSAGE ("Failed to link uridecodebin & nvvidconv\n");
      goto done;
    }

    gst_element_set_state (app->ele.vsink, GST_STATE_PLAYING);
    gst_object_unref (sinkpad);
    goto done;
  }
done:
  gst_caps_unref (caps);
}

static gboolean
create_streaming_file_src_bin (void)
{
  int width, height;
  gchar file_loc[256];
  GstCaps *caps;
  GstCapsFeatures * feature;
  GstPad *pad;

  app->ele.vsrc = gst_element_factory_make (NVGST_STREAMING_SRC_FILE, NULL);
  if (!app->ele.vsrc) {
    NVGST_ERROR_MESSAGE_V ("Element %s creation failed \n",
        NVGST_STREAMING_SRC_FILE);
    goto fail;
  }

  g_snprintf(file_loc, 255, "file://%s",
          app->video_streaming_ctx.streaming_src_file);
  g_object_set(G_OBJECT(app->ele.vsrc), "uri", file_loc, NULL);
  g_signal_connect (app->ele.vsrc, "pad-added",
          G_CALLBACK (cb_streaming_dbin_newpad), app);

  app->video_streaming_ctx.streaming_file_src_conv =
      gst_element_factory_make (NVGST_DEFAULT_VIDEO_CONVERTER_CSI, NULL);
  if (!app->video_streaming_ctx.streaming_file_src_conv) {
    NVGST_ERROR_MESSAGE_V ("Element %s creation failed \n",
        NVGST_DEFAULT_VIDEO_CONVERTER_CSI);
    goto fail;
  }

  app->ele.cap_filter =
    gst_element_factory_make (NVGST_DEFAULT_CAPTURE_FILTER, NULL);
  if (!app->ele.cap_filter) {
    NVGST_ERROR_MESSAGE_V ("Element %s creation failed \n",
        NVGST_DEFAULT_CAPTURE_FILTER);
    goto fail;
  }

  app->capres.current_max_res =
    MAX (app->capres.prev_res_index, MAX (app->capres.vid_res_index,
          app->capres.img_res_index));
  get_max_resolution (app->capres.current_max_res, &width, &height);

  caps =
    gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING, "I420",
        "width", G_TYPE_INT, width, "height", G_TYPE_INT, height, NULL);

  if (app->encset.video_enc != FORMAT_H264_SW)
  {
    feature = gst_caps_features_new("memory:NVMM", NULL);
    gst_caps_set_features (caps, 0, feature);
  }

  g_object_set (app->ele.cap_filter, "caps", caps, NULL);
  gst_caps_unref (caps);

  app->ele.capbin = gst_bin_new ("cap_bin");
  gst_bin_add_many (GST_BIN (app->ele.capbin), app->ele.vsrc,
      app->video_streaming_ctx.streaming_file_src_conv,
      app->ele.cap_filter, NULL);

  if (!gst_element_link_many (app->video_streaming_ctx.streaming_file_src_conv,
              app->ele.cap_filter, NULL)) {
    NVGST_ERROR_MESSAGE_V ("Element link fail between %s & %s \n",
        NVGST_DEFAULT_VIDEO_CONVERTER_CSI, NVGST_DEFAULT_CAPTURE_FILTER);
    goto fail;
  }

  pad = gst_element_get_static_pad (app->ele.cap_filter, "src");
  if (!pad) {
    NVGST_ERROR_MESSAGE ("can't get static src pad of capture filter\n");
    goto fail;
  }
  gst_element_add_pad (app->ele.capbin, gst_ghost_pad_new ("src", pad));
  gst_object_unref (GST_OBJECT (pad));

  return TRUE;

fail:
  app->return_value = -1;
  return FALSE;
}

static gboolean
create_streaming_enc_bin (void)
{
  GstPad *pad = NULL;

  GstCaps *appsink_caps;
  GstAppSinkCallbacks callbacks = {
    NULL, NULL, rtsp_video_appsink_new_sample
  };

  app->ele.svsbin = gst_bin_new ("streaming_bin");

  app->ele.vsink = gst_element_factory_make ("appsink", NULL);
  if (!app->ele.vsink) {
    NVGST_ERROR_MESSAGE ("video sink element could not be created.\n");
    goto fail;
  }

  g_object_set (G_OBJECT (app->ele.vsink),
      "sync", FALSE, "async", FALSE, NULL);
  gst_util_set_object_arg (G_OBJECT (app->ele.vsink), "format", "time");

  switch (app->encset.video_enc) {
    case FORMAT_H264_HW:
      appsink_caps =
          gst_caps_from_string
          ("video/x-h264, stream-format=byte-stream, alignment=au");
      break;
    case FORMAT_VP8_HW:
      appsink_caps = gst_caps_from_string ("video/x-vp8");
      break;
    case FORMAT_H265_HW:
      appsink_caps = gst_caps_from_string ("video/x-h265");
      break;
    case FORMAT_VP9_HW:
      appsink_caps = gst_caps_from_string ("video/x-vp9");
      break;
    case FORMAT_H264_SW:
      appsink_caps =
          gst_caps_from_string
          ("video/x-h264, stream-format=byte-stream, alignment=au");
      break;
    default:
      appsink_caps = gst_caps_from_string ("video/x-h264");
      break;
  }
  g_object_set (G_OBJECT (app->ele.vsink), "caps", appsink_caps, NULL);
  gst_caps_unref (appsink_caps);

  gst_app_sink_set_callbacks (GST_APP_SINK (app->ele.vsink), &callbacks,
      NULL, NULL);

  if (!get_video_encoder (&app->ele.colorspace_conv)) {
    NVGST_ERROR_MESSAGE ("Video encoder element could not be created.\n");
    goto fail;
  }

  gst_bin_add_many (GST_BIN (app->ele.vid_bin), app->ele.colorspace_conv,
      app->ele.vsink, NULL);

  if ((gst_element_link (app->ele.colorspace_conv, app->ele.vsink)) != TRUE) {
    NVGST_ERROR_MESSAGE ("Elements could not link encoder & appsink\n");
    goto fail;
  }

  pad = gst_element_get_static_pad (app->ele.colorspace_conv, "sink");
  if (!pad) {
    NVGST_ERROR_MESSAGE ("can't get static sink pad of encoder\n");
    goto fail;
  }
  gst_element_add_pad (app->ele.svsbin, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (GST_OBJECT (pad));

  return TRUE;

fail:
  app->return_value = -1;
  return FALSE;
}
#endif

static gboolean
create_vid_enc_bin (void)
{
  GstPad *pad = NULL;
  GstPad *srcpad = NULL;
  GstPad *sinkpad = NULL;
  GstCapsFeatures *feature = NULL;
  GstCaps *caps = NULL;

  app->ele.vid_bin = gst_bin_new ("vid_bin");

  if (app->cam_src != NV_CAM_SRC_CSI)
  {
    app->ele.vid_enc_conv = gst_element_factory_make ("nvvidconv", "nvvidconv");
    if (!app->ele.vid_enc_conv) {
    NVGST_ERROR_MESSAGE ("nvvidconv element could not be created.\n");
    goto fail;
    }
    app->ele.vid_enc_cap_filter =
      gst_element_factory_make (NVGST_DEFAULT_CAPTURE_FILTER, NULL);
    caps =
      gst_caps_new_simple ("video/x-raw","format", G_TYPE_STRING, "NV12", NULL);

    if (app->encset.video_enc != FORMAT_H264_SW) {
      feature = gst_caps_features_new("memory:NVMM", NULL);
      gst_caps_set_features(caps, 0, feature);
    }

    g_object_set (G_OBJECT (app->ele.vid_enc_cap_filter), "caps", caps, NULL);
    gst_caps_unref (caps);
  }

  if ((app->cam_src == NV_CAM_SRC_CSI) && (app->encset.video_enc == FORMAT_H264_SW))
  {
    app->ele.vid_enc_conv = gst_element_factory_make("nvvidconv", "nvvidconv");
    if (!app->ele.vid_enc_conv)
    {
      NVGST_ERROR_MESSAGE("nvvidconv element could not be created.\n");
      goto fail;
    }
    app->ele.vid_enc_cap_filter =
        gst_element_factory_make(NVGST_DEFAULT_CAPTURE_FILTER, NULL);
    caps =
        gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "NV12", NULL);

    g_object_set(G_OBJECT(app->ele.vid_enc_cap_filter), "caps", caps, NULL);
    gst_caps_unref(caps);
  }

  app->ele.video_sink =
      gst_element_factory_make (NVGST_DEFAULT_VENC_SINK, NULL);
  if (!app->ele.video_sink) {
    NVGST_ERROR_MESSAGE ("video sink element could not be created.\n");
    goto fail;
  }
  g_object_set (G_OBJECT (app->ele.video_sink),
      "location", DEFAULT_LOCATION, "async", FALSE, "sync", FALSE, NULL);

  if (!get_video_encoder (&app->ele.vid_enc)) {
    NVGST_ERROR_MESSAGE ("Video encoder element could not be created.\n");
    goto fail;
  }

  if (!get_parser (&app->ele.parser)) {
    NVGST_ERROR_MESSAGE ("Video parser element could not be created.\n");
    goto fail;
  }

  if (!get_muxer (&app->ele.muxer)) {
    NVGST_ERROR_MESSAGE ("Video muxer element could not be created.\n");
    goto fail;
  }

  if(app->cam_src != NV_CAM_SRC_CSI)
  {
    gst_bin_add_many (GST_BIN (app->ele.vid_bin), app->ele.vid_enc_conv, app->ele.vid_enc_cap_filter, app->ele.vid_enc,
        app->ele.parser, app->ele.muxer, app->ele.video_sink, NULL);

    if ((gst_element_link (app->ele.vid_enc_conv, app->ele.vid_enc_cap_filter)) != TRUE) {
      NVGST_ERROR_MESSAGE ("Elements could not link nvvidconv & caps filter\n");
      goto fail;
    }

      if ((gst_element_link (app->ele.vid_enc_cap_filter, app->ele.vid_enc)) != TRUE) {
      NVGST_ERROR_MESSAGE ("Elements could not link caps filter & encoder \n");
      goto fail;
    }
  }
  else
  {
    if (app->encset.video_enc != FORMAT_H264_SW) {
      gst_bin_add_many(GST_BIN(app->ele.vid_bin), app->ele.vid_enc,
                       app->ele.parser, app->ele.muxer, app->ele.video_sink, NULL);
    }
    else {
      gst_bin_add_many(GST_BIN(app->ele.vid_bin), app->ele.vid_enc_conv,
          app->ele.vid_enc_cap_filter, app->ele.vid_enc,
          app->ele.parser, app->ele.muxer, app->ele.video_sink, NULL);

      if ((gst_element_link(app->ele.vid_enc_conv, app->ele.vid_enc_cap_filter)) != TRUE) {
        NVGST_ERROR_MESSAGE("Elements could not link nvvidconv & caps filter\n");
        goto fail;
      }

      if ((gst_element_link(app->ele.vid_enc_cap_filter, app->ele.vid_enc)) != TRUE) {
        NVGST_ERROR_MESSAGE("Elements could not link caps filter & encoder \n");
        goto fail;
      }
    }
  }

  if ((gst_element_link (app->ele.vid_enc, app->ele.parser)) != TRUE) {
    NVGST_ERROR_MESSAGE ("Elements could not link encoder & parser\n");
    goto fail;
  }

  srcpad = gst_element_get_static_pad (app->ele.parser, "src");

  if (app->muxer_is_identity)
  {
    sinkpad = gst_element_get_static_pad (app->ele.muxer, "sink");
  }
  else
  {
    sinkpad = gst_element_get_request_pad (app->ele.muxer, "video_%u");
  }

  if (!sinkpad || !srcpad) {
    NVGST_ERROR_MESSAGE ("could not get pads to link enc & muxer\n");
    goto fail;
  }
  if (GST_PAD_LINK_OK != gst_pad_link (srcpad, sinkpad)) {
    NVGST_ERROR_MESSAGE ("could not link enc & muxer\n");
    goto fail;
  }
  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);

  if ((gst_element_link (app->ele.muxer, app->ele.video_sink)) != TRUE) {
    NVGST_ERROR_MESSAGE ("Elements could not link muxer & video_sink\n");
    goto fail;
  }

  if((app->cam_src != NV_CAM_SRC_CSI) || (app->encset.video_enc == FORMAT_H264_SW))
  {
    pad = gst_element_get_static_pad (app->ele.vid_enc_conv, "sink");
    if (!pad) {
      NVGST_ERROR_MESSAGE ("can't get static sink pad of nvvidconv\n");
      goto fail;
    }
  }
  else
  {
    pad = gst_element_get_static_pad (app->ele.vid_enc, "sink");
    if (!pad) {
      NVGST_ERROR_MESSAGE ("can't get static sink pad of encoder\n");
      goto fail;
    }
  }

  gst_element_add_pad (app->ele.vid_bin, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (GST_OBJECT (pad));

  return TRUE;

fail:
  app->return_value = -1;
  return FALSE;
}

static gboolean
create_video_snap_bin (void)
{
  GstPad *pad = NULL;
  GstCaps *caps = NULL;
  GstCapsFeatures *feature = NULL;

  app->ele.vsnap_bin = gst_bin_new ("vsnap_bin");

  if (!get_image_encoder (&app->ele.vsnap_enc)) {
    NVGST_ERROR_MESSAGE ("Image encoder element could not be created.\n");
    goto fail;
  }

  app->ele.vsnap_sink =
      gst_element_factory_make (NVGST_DEFAULT_IENC_SINK, NULL);
  if (!app->ele.vsnap_sink) {
    NVGST_ERROR_MESSAGE ("Image sink element could be created.\n");
    goto fail;
  }
  g_object_set (G_OBJECT (app->ele.vsnap_sink), "signal-handoffs", TRUE, NULL);
  g_signal_connect (G_OBJECT (app->ele.vsnap_sink), "handoff",
      G_CALLBACK (write_vsnap_buffer), NULL);

  app->ele.svc_snapconv =
      gst_element_factory_make (NVGST_DEFAULT_VIDEO_CONVERTER_CSI, NULL);
  if (!app->ele.svc_snapconv) {
    NVGST_ERROR_MESSAGE_V ("Element %s creation failed \n",
        NVGST_DEFAULT_VIDEO_CONVERTER_CSI);
    goto fail;
  }
  g_object_set (app->ele.svc_snapconv , "flip-method", app->flip_method, NULL);

  app->ele.svc_snapconv_out_filter =
      gst_element_factory_make (NVGST_DEFAULT_CAPTURE_FILTER, NULL);
  if (!app->ele.svc_snapconv_out_filter) {
    NVGST_ERROR_MESSAGE_V ("Element %s creation failed \n",
        NVGST_DEFAULT_CAPTURE_FILTER);
    goto fail;
  }

  caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "I420",
      "width", G_TYPE_INT, app->capres.video_cap_width,
      "height", G_TYPE_INT, app->capres.video_cap_height, NULL);

  if (app->encset.image_enc == FORMAT_JPEG_HW) {
    feature = gst_caps_features_new ("memory:NVMM", NULL);
    gst_caps_set_features (caps, 0, feature);
  }

  /* Set capture caps on capture filter */
  g_object_set (app->ele.svc_snapconv_out_filter, "caps", caps, NULL);
  gst_caps_unref (caps);


  gst_bin_add_many (GST_BIN (app->ele.vsnap_bin),
      app->ele.svc_snapconv, app->ele.svc_snapconv_out_filter,
      app->ele.vsnap_enc, app->ele.vsnap_sink, NULL);

  if (!gst_element_link_many (app->ele.svc_snapconv,
          app->ele.svc_snapconv_out_filter, app->ele.vsnap_enc,
          app->ele.vsnap_sink, NULL)) {
    NVGST_ERROR_MESSAGE ("vsnap_bin: Element link fail \n");
    goto fail;
  }

  pad = gst_element_get_static_pad (app->ele.svc_snapconv, "sink");
  if (!pad) {
    NVGST_ERROR_MESSAGE ("can't get static sink pad of converter \n");
    goto fail;
  }

  gst_element_add_pad (app->ele.vsnap_bin, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (GST_OBJECT (pad));

  return TRUE;

fail:
  app->return_value = -1;
  return FALSE;
}

static gboolean
create_eglstream_producer_pipeline (void)
{
  GstBus *bus = NULL;
  GstCaps *caps = NULL;
  gchar *str_color = NULL;
  GstCapsFeatures *feature = NULL;
  app->ele.eglproducer_bin = gst_bin_new ("eglproducer_bin");

  app->ele.eglproducer_pipeline = gst_pipeline_new ("capture_native_pipeline");;
  if (!app->ele.eglproducer_pipeline) {
    NVGST_ERROR_MESSAGE ("capture native pipeline creation failed \n");
    goto fail;
  }
  bus = gst_pipeline_get_bus (GST_PIPELINE (app->ele.eglproducer_pipeline));
  gst_bus_set_sync_handler (bus, bus_sync_handler,
      app->ele.eglproducer_pipeline, NULL);
  gst_bus_add_watch (bus, bus_call, NULL);
  gst_object_unref (bus);

  g_object_set (app->ele.eglproducer_pipeline, "message-forward", TRUE, NULL);

  app->ele.eglproducer_videotestsrc =
    gst_element_factory_make (NVGST_VIDEO_CAPTURE_SRC_TEST, NULL);
  if (!app->ele.eglproducer_videotestsrc) {
    NVGST_ERROR_MESSAGE ("eglproducer_bin VideoTestSrc Element creation failed.\n");
    goto fail;
  }

  app->ele.eglproducer_capsfilter =
    gst_element_factory_make (NVGST_DEFAULT_CAPTURE_FILTER, NULL);
  if (!app->ele.eglproducer_capsfilter) {
    NVGST_ERROR_MESSAGE ("eglproducer_bin capsfilter Element creation failed. \n");
    goto fail;
  }

  if (app->color_format != -1)
    app->color_format_csi = app->color_format;
  switch(app->color_format_csi){
     case CAPTURE_I420:
       str_color = "I420";
       break;
     case CAPTURE_NV12:
       str_color = "NV12";
       break;
     default:
      g_print("Color format not supported for CSI camera\n");
      goto fail;
  }

  caps = gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING, str_color,
              "width", G_TYPE_INT, 640, "height", G_TYPE_INT, 480, NULL);

  g_object_set (app->ele.eglproducer_capsfilter, "caps", caps, NULL);
  gst_caps_unref (caps);

  app->ele.eglproducer_videoconvert =
    gst_element_factory_make (NVGST_DEFAULT_VIDEO_CONVERTER_CSI, NULL);
  if (!app->ele.eglproducer_videoconvert) {
    NVGST_ERROR_MESSAGE ("eglproducer_bin videoconvert Element creation failed. \n");
    goto fail;
  }

  app->ele.eglproducer_nvvideosink =
    gst_element_factory_make (NVGST_VIDEO_SINK, NULL);

  if (!app->ele.eglproducer_nvvideosink) {
    NVGST_ERROR_MESSAGE ("eglproducer_bin nvvideosink Element creation failed \n");
    goto fail;
  }
  gst_bin_add_many (GST_BIN (app->ele.eglproducer_bin),
      app->ele.eglproducer_videotestsrc, app->ele.eglproducer_capsfilter,
      app->ele.eglproducer_videoconvert, app->ele.eglproducer_nvvideosink, NULL);

  if ((gst_element_link_many (app->ele.eglproducer_videotestsrc, app->ele.eglproducer_capsfilter,
          app->ele.eglproducer_videoconvert, app->ele.eglproducer_nvvideosink, NULL)) != TRUE)
  {
    NVGST_ERROR_MESSAGE ("eglproducer_bin Elements could not be linked\n");
    goto fail;
  }

  caps = gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING, str_color,
              "width", G_TYPE_INT, 640, "height", G_TYPE_INT, 480, NULL);
  feature = gst_caps_features_new ("memory:NVMM", NULL);
  gst_caps_set_features (caps, 0, feature);

  g_object_set (app->ele.eglproducer_nvvideosink, "outcaps", caps, NULL);
  gst_caps_unref (caps);

  // get display and stream
  g_object_get (G_OBJECT (app->ele.eglproducer_nvvideosink),
      "display", &app->display, NULL);
  g_object_get (G_OBJECT (app->ele.eglproducer_nvvideosink),
      "stream", &app->stream, NULL);
  g_print ("GET display=%p and EGLStream=%p \n", app->display, app->stream);

  /* Add elements to camera pipeline */
  gst_bin_add_many (GST_BIN (app->ele.eglproducer_pipeline),
      app->ele.eglproducer_bin, NULL);

  return TRUE;

fail:
  return FALSE;
}

static gboolean
create_csi_capture_pipeline (void)
{
  GstBus *bus = NULL;
  GstPad *sinkpad = NULL;
  GstPad *srcpad = NULL;

  /* Create the camera pipeline */
  app->ele.camera = gst_pipeline_new ("capture_native_pipeline");;
  if (!app->ele.camera) {
    NVGST_ERROR_MESSAGE ("capture native pipeline creation failed \n");
    goto fail;
  }
  bus = gst_pipeline_get_bus (GST_PIPELINE (app->ele.camera));
  gst_bus_set_sync_handler (bus, bus_sync_handler, app->ele.camera, NULL);
  gst_bus_add_watch (bus, bus_call, NULL);
  gst_object_unref (bus);

  g_object_set (app->ele.camera, "message-forward", TRUE, NULL);

#ifdef WITH_STREAMING
  if (app->streaming_mode && app->video_streaming_ctx.streaming_src_file) {
    /* Create capture chain elements */
    if (!create_streaming_file_src_bin ()) {
      NVGST_ERROR_MESSAGE ("cap bin creation failed \n");
      goto fail;
    }
  } else {
#else
  {
#endif
    /* Create capture chain elements */
    if (!create_csi_cap_bin ()) {
      NVGST_ERROR_MESSAGE ("cap bin creation failed \n");
      goto fail;
    }
  }

  /* Create encode chain elements */
  if (!create_vid_enc_bin ()) {
    NVGST_ERROR_MESSAGE ("encode bin creation failed \n");
    goto fail;
  }

  if (!create_img_enc_bin ()) {
    NVGST_ERROR_MESSAGE ("encode bin creation failed \n");
    goto fail;
  }

  if (!create_video_snap_bin ()) {
    NVGST_ERROR_MESSAGE ("video snapshot bin creation failed \n");
    goto fail;
  }

#ifdef WITH_STREAMING
  if (app->streaming_mode) {
    if (!create_streaming_enc_bin ()) {
      NVGST_ERROR_MESSAGE ("encode bin creation failed \n");
      goto fail;
    }
  } else {
#else
  {
#endif
    /* Create preview chain elements */
    if (!create_svs_bin ()) {
      NVGST_ERROR_MESSAGE ("svs bin creation failed \n");
      goto fail;
    }
  }

  /* Create preview scaling elements */
  if (!create_preview_scaling_bin ()) {
    NVGST_ERROR_MESSAGE ("preview scaling bin creation failed \n");
    goto fail;
  }

  /* Create image scaling elements */
  if (!create_image_scaling_bin ()) {
    NVGST_ERROR_MESSAGE ("image scaling bin creation failed \n");
    goto fail;
  }

  /* Create video scaling elements */
  if (!create_video_scaling_bin ()) {
    NVGST_ERROR_MESSAGE ("video scaling bin creation failed \n");
    goto fail;
  }

  /* Create capture tee for capture streams */
  app->ele.cap_tee =
      gst_element_factory_make ("nvtee" /*NVGST_PRIMARY_STREAM_SELECTOR */ ,
      NULL);
  if (!app->ele.cap_tee) {
    NVGST_ERROR_MESSAGE ("capture nvtee creation failed \n");
    goto fail;
  }

  g_object_set (G_OBJECT (app->ele.cap_tee), "name", "cam_t", NULL);
  g_object_set (G_OBJECT (app->ele.cap_tee), "mode", app->mode, NULL);

  /* Create preview & encode queue */
  app->ele.prev_q = gst_element_factory_make (NVGST_PRIMARY_QUEUE, NULL);
  app->ele.ienc_q = gst_element_factory_make (NVGST_PRIMARY_QUEUE, NULL);
  app->ele.venc_q = gst_element_factory_make (NVGST_PRIMARY_QUEUE, NULL);
  app->ele.vsnap_q = gst_element_factory_make (NVGST_PRIMARY_QUEUE, NULL);
  if (!app->ele.prev_q || !app->ele.ienc_q || !app->ele.venc_q
      || !app->ele.vsnap_q) {
    NVGST_ERROR_MESSAGE ("preview/encode queue creation failed \n");
    goto fail;
  }

  /* Add elements to camera pipeline */
  gst_bin_add_many (GST_BIN (app->ele.camera),
      app->ele.capbin, app->ele.vid_bin, app->ele.img_bin, app->ele.svsbin,
      app->ele.svc_prebin, app->ele.svc_imgbin, app->ele.svc_vidbin,
      app->ele.cap_tee, app->ele.prev_q, app->ele.ienc_q, app->ele.venc_q,
      app->ele.vsnap_q, app->ele.vsnap_bin, NULL);

  /* Manually link the Tee with preview queue */
  srcpad = gst_element_get_static_pad (app->ele.cap_tee, "pre_src");
  sinkpad = gst_element_get_static_pad (app->ele.prev_q, "sink");
  if (!sinkpad || !srcpad) {
    NVGST_ERROR_MESSAGE ("fail to get pads from cap_tee & prev_q\n");
    goto fail;
  }
  if (GST_PAD_LINK_OK != gst_pad_link (srcpad, sinkpad)) {
    NVGST_ERROR_MESSAGE ("fail to link cap_tee & prev_q\n");
    goto fail;
  }

  app->prev_probe_id = gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_BUFFER,
                                     prev_buf_prob, NULL, NULL);

  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);

  /* Manually link the queue with preview scaling bin */
  srcpad = gst_element_get_static_pad (app->ele.prev_q, "src");
  sinkpad = gst_element_get_static_pad (app->ele.svc_prebin, "sink");
  if (!sinkpad || !srcpad) {
    NVGST_ERROR_MESSAGE ("fail to get pads from prev_q & svc_prebin\n");
    goto fail;
  }
  if (GST_PAD_LINK_OK != gst_pad_link (srcpad, sinkpad)) {
    NVGST_ERROR_MESSAGE ("fail to link svc_prebin & prev_q\n");
    goto fail;
  }
  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);

  /* Manually link the Tee with video queue */
  srcpad = gst_element_get_static_pad (app->ele.cap_tee, "vid_src");
  sinkpad = gst_element_get_static_pad (app->ele.venc_q, "sink");
  if (!sinkpad || !srcpad) {
    NVGST_ERROR_MESSAGE ("fail to get pads from cap_tee & enc_q\n");
    goto fail;
  }
  if (GST_PAD_LINK_OK != gst_pad_link (srcpad, sinkpad)) {
    NVGST_ERROR_MESSAGE ("fail to link cap_tee & enc_q\n");
    goto fail;
  }
  app->enc_probe_id = gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_BUFFER,
                                     enc_buf_prob, NULL, NULL);

  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);

  /* Manually link the video queue with video scaling */
  srcpad = gst_element_get_static_pad (app->ele.venc_q, "src");
  sinkpad = gst_element_get_static_pad (app->ele.svc_vidbin, "sink");
  if (!sinkpad || !srcpad) {
    NVGST_ERROR_MESSAGE ("fail to get pads from video queue & video scaling\n");
    goto fail;
  }
  if (GST_PAD_LINK_OK != gst_pad_link (srcpad, sinkpad)) {
    NVGST_ERROR_MESSAGE ("fail to link video queue & video scaling\n");
    goto fail;
  }
  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);

  /* Manually link the Tee with image queue */
  srcpad = gst_element_get_static_pad (app->ele.cap_tee, "img_src");
  sinkpad = gst_element_get_static_pad (app->ele.ienc_q, "sink");
  if (!sinkpad || !srcpad) {
    NVGST_ERROR_MESSAGE ("fail to get pads from cap_tee & enc_q\n");
    goto fail;
  }
  if (GST_PAD_LINK_OK != gst_pad_link (srcpad, sinkpad)) {
    NVGST_ERROR_MESSAGE ("fail to link cap_tee & enc_q\n");
    goto fail;
  }
  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);

  /* Manually link the image queue with image scaling */
  srcpad = gst_element_get_static_pad (app->ele.ienc_q, "src");
  sinkpad = gst_element_get_static_pad (app->ele.svc_imgbin, "sink");
  if (!sinkpad || !srcpad) {
    NVGST_ERROR_MESSAGE ("fail to get pads from image queue & image scaling\n");
    goto fail;
  }
  if (GST_PAD_LINK_OK != gst_pad_link (srcpad, sinkpad)) {
    NVGST_ERROR_MESSAGE ("fail to link image queue & image scaling\n");
    goto fail;
  }
  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);


  /* Manually link the Tee with video snapshot queue */
  srcpad = gst_element_get_static_pad (app->ele.cap_tee, "vsnap_src");
  sinkpad = gst_element_get_static_pad (app->ele.vsnap_q, "sink");
  if (!sinkpad || !srcpad) {
    NVGST_ERROR_MESSAGE ("fail to get pads from cap_tee & enc_q\n");
    goto fail;
  }
  if (GST_PAD_LINK_OK != gst_pad_link (srcpad, sinkpad)) {
    NVGST_ERROR_MESSAGE ("fail to link cap_tee & enc_q\n");
    goto fail;
  }
  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);

  /* Manually link video snapshot queue with video snapshot bin  */
  srcpad = gst_element_get_static_pad (app->ele.vsnap_q, "src");
  sinkpad = gst_element_get_static_pad (app->ele.vsnap_bin, "sink");
  if (!sinkpad || !srcpad) {
    NVGST_ERROR_MESSAGE ("fail to get pads from video snapshot queue & bin\n");
    goto fail;
  }
  if (GST_PAD_LINK_OK != gst_pad_link (srcpad, sinkpad)) {
    NVGST_ERROR_MESSAGE ("fail to link video snapshot queue & bin \n");
    goto fail;
  }
  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);

  /* link the capture bin with Tee */
  if (!gst_element_link (app->ele.capbin, app->ele.cap_tee)) {
    NVGST_ERROR_MESSAGE ("fail to link capbin & cap_tee\n");
    goto fail;
  }

  /* link the preview scaling bin with svs bin */
  if (!gst_element_link (app->ele.svc_prebin, app->ele.svsbin)) {
    NVGST_ERROR_MESSAGE ("fail to link svc_prebin & svsbin\n");
    goto fail;
  }

  /* link the video scaling bin with encode bin */
  if (!gst_element_link (app->ele.svc_vidbin, app->ele.vid_bin)) {
    NVGST_ERROR_MESSAGE ("fail to link svc_vidbin & vidbin\n");
    goto fail;
  }

  /* link the image scaling bin with encode bin */
  if (!gst_element_link (app->ele.svc_imgbin, app->ele.img_bin)) {
    NVGST_ERROR_MESSAGE ("fail to link svc_imgbin & imgbin\n");
    goto fail;
  }

  return TRUE;

fail:
  app->return_value = -1;
  return FALSE;
}

/**
  * Create native capture pipeline.
  *
  * @param void
  */
static gboolean
create_native_capture_pipeline (void)
{
  GstBus *bus = NULL;
  GstPad *sinkpad = NULL;
  GstPad *tee_prev_pad = NULL;
  GstPad *tee_vid_pad = NULL;
  GstElement *encbin = NULL;
  GstElement *enc_q = NULL;
  GstPadTemplate *tee_src_pad_template = NULL;

  /* Create the camera pipeline */
  app->ele.camera = gst_pipeline_new ("capture_native_pipeline");;
  if (!app->ele.camera) {
    NVGST_ERROR_MESSAGE ("capture native pipeline creation failed \n");
    goto fail;
  }
  bus = gst_pipeline_get_bus (GST_PIPELINE (app->ele.camera));
  gst_bus_set_sync_handler (bus, bus_sync_handler, app->ele.camera, NULL);
  gst_bus_add_watch (bus, bus_call, NULL);
  gst_object_unref (bus);

  /* Create encode chain elements */
  if (app->mode == CAPTURE_VIDEO) {
    if (!create_vid_enc_bin ()) {
      NVGST_ERROR_MESSAGE ("encode bin creation failed \n");
      goto fail;
    }

    app->ele.venc_q = gst_element_factory_make (NVGST_PRIMARY_QUEUE, NULL);
    if (!app->ele.venc_q) {
      NVGST_ERROR_MESSAGE ("video encode queue creation failed \n");
      goto fail;
    }

    encbin = app->ele.vid_bin;
    enc_q = app->ele.venc_q;
  } else {
    if (!create_img_enc_bin ()) {
      NVGST_ERROR_MESSAGE ("encode bin creation failed \n");
      goto fail;
    }

    app->ele.ienc_q = gst_element_factory_make (NVGST_PRIMARY_QUEUE, NULL);
    if (!app->ele.ienc_q) {
      NVGST_ERROR_MESSAGE ("image encode queue creation failed \n");
      goto fail;
    }
    encbin = app->ele.img_bin;
    enc_q = app->ele.ienc_q;
  }

  /* Create capture chain elements */
  if (!create_cap_bin ()) {
    NVGST_ERROR_MESSAGE ("cap bin creation failed \n");
    goto fail;
  }

  /* Create preview chain elements */
  if (!create_svs_bin ()) {
    NVGST_ERROR_MESSAGE ("svs bin creation failed \n");
    goto fail;
  }

  /* Create capture tee for capture streams */
  app->ele.cap_tee =
      gst_element_factory_make (NVGST_PRIMARY_STREAM_SELECTOR, NULL);
  if (!app->ele.cap_tee) {
    NVGST_ERROR_MESSAGE ("capture tee creation failed \n");
    goto fail;
  }

  g_object_set (G_OBJECT (app->ele.cap_tee), "name", "cam_t", NULL);

  /* Create preview & encode queue */
  app->ele.prev_q = gst_element_factory_make (NVGST_PRIMARY_QUEUE, NULL);

  if (!app->ele.prev_q) {
    NVGST_ERROR_MESSAGE ("preview queue creation failed \n");
    goto fail;
  }
  g_object_set (G_OBJECT (app->ele.prev_q), "max-size-time", (guint64) 0,
      "max-size-bytes", 0, "max-size-buffers", 1, NULL);

  /* Add elements to camera pipeline */
  gst_bin_add_many (GST_BIN (app->ele.camera),
      app->ele.capbin, encbin, app->ele.svsbin,
      app->ele.cap_tee, app->ele.prev_q, enc_q, NULL);

  tee_src_pad_template =
    gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (app->
        ele.cap_tee), "src_%u");
  if (!tee_src_pad_template) {
    NVGST_ERROR_MESSAGE ("fail to get pads template from cap_tee\n");
    goto fail;
  }

  /* Manually link the Tee with preview queue */
  tee_prev_pad =
    gst_element_request_pad (app->ele.cap_tee, tee_src_pad_template, NULL,
        NULL);
  sinkpad = gst_element_get_static_pad (app->ele.prev_q, "sink");
  if (!sinkpad || !tee_prev_pad) {
    NVGST_ERROR_MESSAGE ("fail to get pads from cap_tee & prev_q\n");
    goto fail;
  }
  if (GST_PAD_LINK_OK != gst_pad_link (tee_prev_pad, sinkpad)) {
    NVGST_ERROR_MESSAGE ("fail to link cap_tee & prev_q\n");
    goto fail;
  }
  app->prev_probe_id = gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_BUFFER,
                                     prev_buf_prob, NULL, NULL);

  gst_object_unref (sinkpad);
  gst_object_unref (tee_prev_pad);

  /* Manually link the Tee with capture queue */
  tee_vid_pad =
    gst_element_request_pad (app->ele.cap_tee, tee_src_pad_template, NULL,
        NULL);
  sinkpad = gst_element_get_static_pad (enc_q, "sink");
  if (!sinkpad || !tee_vid_pad) {
    NVGST_ERROR_MESSAGE ("fail to get pads from cap_tee & enc_q\n");
    goto fail;
  }
  if (GST_PAD_LINK_OK != gst_pad_link (tee_vid_pad, sinkpad)) {
    NVGST_ERROR_MESSAGE ("fail to link cap_tee & enc_q\n");
    goto fail;
  }

  gst_object_unref (sinkpad);
  gst_object_unref (tee_vid_pad);

  /* link the capture bin with Tee */
  if (!gst_element_link (app->ele.capbin, app->ele.cap_tee)) {
    NVGST_ERROR_MESSAGE ("fail to link capbin & cap_tee\n");
    goto fail;
  }

  /* link the preview queue with svs bin */
  if (!gst_element_link (app->ele.prev_q, app->ele.svsbin)) {
    NVGST_ERROR_MESSAGE ("fail to link prev_q & svsbin\n");
    goto fail;
  }

  /* link the capture queue with encode bin */
  if (!gst_element_link (enc_q, encbin)) {
    NVGST_ERROR_MESSAGE ("fail to link enc_q & endbin\n");
    goto fail;
  }

  /* Add buffer probe on capture queue sink pad */
  sinkpad = gst_element_get_static_pad (enc_q, "sink");
  app->enc_probe_id =
    gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_BUFFER, enc_buf_prob,
      NULL, NULL);
  gst_object_unref (sinkpad);

  return TRUE;

fail:
  app->return_value = -1;
  return FALSE;
}

/**
  * Destroy capture pipeline.
  *
  * @param void
  */
void
destroy_capture_pipeline (void)
{
  recording = FALSE;
  GstPad *sinkpad = NULL;

  if (!app->ele.camera)
    return;

  if (GST_STATE_CHANGE_FAILURE == gst_element_set_state (app->ele.camera,
          GST_STATE_NULL)) {
    g_warning ("can't set camera pipeline to null\n");
  }

  if (app->use_eglstream) {
    if (GST_STATE_CHANGE_FAILURE ==
        gst_element_set_state (app->ele.eglproducer_pipeline, GST_STATE_NULL)) {
      g_warning ("can't set nvvideosink eglproducer pipeline "
          "to null\n");
    }
  }

  if (app->cam_src != NV_CAM_SRC_CSI) {
    /* Remove buffer probe from encode queue sink pad */
    if (app->mode == CAPTURE_VIDEO)
      sinkpad = gst_element_get_static_pad (app->ele.venc_q, "sink");
    else
      sinkpad = gst_element_get_static_pad (app->ele.ienc_q, "sink");
    gst_pad_remove_probe (sinkpad, app->enc_probe_id);
    gst_object_unref (sinkpad);
  }

  if (app->cam_src == NV_CAM_SRC_CSI) {
    sinkpad = gst_element_get_static_pad (app->ele.venc_q, "sink");
    gst_pad_remove_probe (sinkpad, app->enc_probe_id);
    gst_object_unref (sinkpad);
  }

  sinkpad = gst_element_get_static_pad (app->ele.prev_q, "sink");
  gst_pad_remove_probe (sinkpad, app->prev_probe_id);
  gst_object_unref (sinkpad);

  if (app->reset_thread)
    g_thread_unref (app->reset_thread);

  app->reset_thread = NULL;

  gst_object_unref (GST_OBJECT (app->ele.camera));
  app->ele.camera = NULL;
  app->ele.vsrc = NULL;
  app->ele.vsink = NULL;
  app->ele.cap_filter = NULL;
  app->ele.cap_tee = NULL;
  app->ele.prev_q = NULL;
  app->ele.venc_q = NULL;
  app->ele.ienc_q = NULL;
  app->ele.img_enc = NULL;
  app->ele.vid_enc = NULL;
  app->ele.muxer = NULL;
  app->ele.img_sink = NULL;
  app->ele.video_sink = NULL;

  app->ele.capbin = NULL;
  app->ele.vid_bin = NULL;
  app->ele.img_bin = NULL;
  app->ele.svsbin = NULL;

  app->ele.vid_enc_conv = NULL;
  app->ele.vid_enc_cap_filter = NULL;

}

/**
  * Restart capture pipeline.
  *
  * @param void
  */
void
restart_capture_pipeline (void)
{
  destroy_capture_pipeline ();

  g_usleep (250000);

  if (!create_capture_pipeline ()) {
    app->return_value = -1;
    g_main_loop_quit (loop);
  }
}


/**
  * Create capture pipeline.
  *
  * @param void
  */
gboolean
create_capture_pipeline (void)
{
  gboolean ret = TRUE;

  FUNCTION_START();

  /* Check for capture parameters */
  if (!check_capture_params ()) {
    NVGST_ERROR_MESSAGE ("Invalid capture parameters \n");
    goto fail;
  }

  if (app->cam_src == NV_CAM_SRC_EGLSTREAM)
  {
    /* nvvideosink acting as EGLStreamProducer */
    if (!create_eglstream_producer_pipeline ()) {
      NVGST_ERROR_MESSAGE ("eglstream_producer pipeline creation failed \n");
      goto fail;
    }
    ret = create_csi_capture_pipeline ();
  }
  else
  {
    /* Create capture pipeline elements */
    if (app->cam_src == NV_CAM_SRC_CSI)
      ret = create_csi_capture_pipeline ();
    else
      ret = create_native_capture_pipeline ();
  }

  if (!ret) {
    NVGST_ERROR_MESSAGE ("can't create capture pipeline\n");
    goto fail;
  }

  /* Capture pipeline created, now start capture */
  GST_INFO_OBJECT (app->ele.camera, "camera ready");

  if (GST_STATE_CHANGE_FAILURE ==
      gst_element_set_state (app->ele.camera, GST_STATE_PLAYING)) {
    NVGST_CRITICAL_MESSAGE ("can't set camera to playing\n");
    goto fail;
  }

  if (app->use_eglstream)
  {
    /* EGLStreamProducer pipeline created, now start EGLStreamConsumer pipeline */
    GST_INFO_OBJECT (app->ele.eglproducer_pipeline, "nvvideosink eglproducer ready");

    if (GST_STATE_CHANGE_FAILURE ==
        gst_element_set_state (app->ele.eglproducer_pipeline, GST_STATE_PLAYING)) {
      NVGST_CRITICAL_MESSAGE ("can't set nvvideosink eglproducer pipeline "
          "to playing\n");
      goto fail;
    }
  }

  FUNCTION_END();

  /* Dump Capture - Playing Pipeline into the dot file
   * Set environment variable "export GST_DEBUG_DUMP_DOT_DIR=/tmp"
   * Run nvgstcapture-1.0 and 0.00.00.*-nvgstcapture-1.0-playing.dot
   * file will be generated.
   * Run "dot -Tpng 0.00.00.*-nvgstcapture-1.0-playing.dot > image.png"
   * image.png will display the running capture pipeline.
   * */
  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN(app->ele.camera),
      GST_DEBUG_GRAPH_SHOW_ALL, "nvgstcapture-1.0-playing");

  return ret;

fail:
  app->return_value = -1;
  FUNCTION_END();
  return FALSE;
}

gboolean
exit_capture (gpointer data)
{
  compute_frame_rate ();
  g_main_loop_quit (loop);

  return FALSE;
}

#if !GUI
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
          GST_ELEMENT_ERROR (app->ele.camera, RESOURCE, NOT_FOUND,
              ("Output window was closed"), (NULL));
        }
    }
  }
}

static gpointer
nvgst_x_event_thread (gpointer data)
{
  g_mutex_lock (app->lock);
  while (app->disp.window) {
    nvgst_handle_xevents ();
    g_mutex_unlock(app->lock);
    g_usleep(G_USEC_PER_SEC / 20);
    g_mutex_lock(app->lock);
  }
  g_mutex_unlock(app->lock);
  return NULL;
}
#endif

static gboolean
auto_capture (gpointer data)
{
  gint count = 0;
  gfloat f_count = 0;
  guint sensor_modsize;

  g_print ("\nStarting automation...\n");

  if (app->aut.toggle_mode) {
    while (app->aut.iteration_count-- > 0) {
      g_usleep (1000000);
      app->mode = (CAPTURE_VIDEO + 1) - app->mode;
      g_object_set (app->ele.cap_tee, "mode", app->mode, NULL);
      g_print ("Mode changed to : %d\n", app->mode);
      g_usleep (1000000);
    }
    goto automation_done;
  }

  if (app->aut.toggle_sensor_modes) {
    g_object_get (G_OBJECT (app->ele.vsrc), "total-sensor-modes", &sensor_modsize, NULL);
    count = app->aut.iteration_count;
    while (count-- > 0) {
      guint u_count = 0;
      while (u_count < sensor_modsize) {
        g_usleep (3000000);
        app->sensor_mode = u_count;
        g_print ("Sensor-mode changed to : %d\n", app->sensor_mode);
        restart_capture_pipeline ();
        g_usleep (3000000);
        u_count++;
      }
    }
  }

  if (app->aut.toggle_sensor) {
    while (app->aut.iteration_count-- > 0) {
      g_usleep (3000000);           //increased sleep time so sensor change can be perceived
      app->sensor_id = ( app->sensor_id + 1 ) % app->aut.num_sensors;
      g_print ("Sensor-id changed to : %d\n", app->sensor_id);
      if (app->aut.toggle_sensor_modes) {
        g_object_get (G_OBJECT (app->ele.vsrc), "total-sensor-modes", &sensor_modsize, NULL);
        guint u_count = 0;
        while (u_count < sensor_modsize) {
          g_usleep (3000000);
          app->sensor_mode = u_count;
          g_print ("Sensor-mode changed to : %d\n", app->sensor_mode);
          restart_capture_pipeline ();
          g_usleep (3000000);
          u_count++;
        }
      }
      else {
        restart_capture_pipeline ();
        g_usleep (3000000);
      }
    }
    goto automation_done;
  }

  if (app->aut.enum_wb) {
    while (app->aut.iteration_count-- > 0) {
      count = 0;
      while (count < 9) {
        g_usleep (1000000);
        g_object_set (G_OBJECT (app->ele.vsrc), "wbmode", count, NULL);
        g_print ("Whitebalance set to : %d\n", count);
        g_usleep (1000000);
        count++;
      }
    }
    goto automation_done;
  }

  if (app->aut.enum_st) {
    while (app->aut.iteration_count-- > 0) {
      f_count = 0;
      count = 0;
      while (count < 20) {      //Range is from 0 to 2
        g_usleep (1000000);
        g_object_set (G_OBJECT (app->ele.vsrc), "saturation", f_count, NULL);
        g_print ("Saturation set to : %f\n", f_count);
        g_usleep (1000000);
        f_count = f_count + (gfloat) 0.1;       //step is 0.1
        count++;
      }
    }
    goto automation_done;
  }

  if (app->aut.capture_auto) {
    while (app->aut.iteration_count-- > 0) {

      if (app->return_value == -1)
        break;

      if (app->mode == CAPTURE_IMAGE && recording == FALSE) {
        trigger_image_capture ();

      } else if (app->mode == CAPTURE_VIDEO && recording == FALSE) {
        {
          gint i;
          start_video_capture ();
          g_print ("\nRecording Started for %d seconds\n",
              app->aut.capture_time);

          for (i = 0 ; i < app->aut.capture_time; i++)
            g_usleep (1000000);

          stop_video_capture ();
        }
      }
      g_usleep (1000 * app->aut.capture_gap);
    }
  }

automation_done:

  g_timeout_add_seconds (app->aut.quit_time, exit_capture, NULL);
  return FALSE;
}

static void
_intr_handler (int signum)
{
  struct sigaction action;

  NVGST_INFO_MESSAGE ("User Interrupted.. \n");
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

    gst_element_post_message (GST_ELEMENT (app->ele.camera),
        gst_message_new_application (GST_OBJECT (app->ele.camera),
            gst_structure_new ("NvGstAppInterrupt",
                "message", G_TYPE_STRING, "Pipeline interrupted", NULL)));

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

int
main (int argc, char *argv[])
{
  GOptionContext *ctx;
  GOptionGroup *group_argus;
  GError *error = NULL;

#ifdef WITH_STREAMING
  void *nvgst_rtsp_lib = NULL;
#endif

  GIOChannel *channel = NULL;

  app = &capp;
  memset (app, 0, sizeof (CamCtx));

  /* Initialize capture params */
  capture_init_params ();

  GOptionEntry options_argus[] = {
    {"prev-res", 0, 0, G_OPTION_ARG_CALLBACK, parse_spec,
          "Preview width & height."
          "Range: 2 to 12 (5632x4224) e.g., --prev-res=3",
        NULL}
    ,
    {"cus-prev-res", 0, 0, G_OPTION_ARG_CALLBACK, parse_spec,
          "Custom Preview width & height e.g., --cus-prev-res=1920x1080",
        NULL}
    ,
    {"image-res", 0, 0, G_OPTION_ARG_CALLBACK, parse_spec,
          "Image width & height. Range: 2 to 12 (5632x4224) e.g., --image-res=3",
        NULL}
    ,
    {"video-res", 0, 0, G_OPTION_ARG_CALLBACK, parse_spec,
          "Video width & height. Range: 2 to 9 (3896x2192) e.g., --video-res=3",
        NULL}
    ,
    {"camsrc", 0, 0, G_OPTION_ARG_INT, &app->cam_src,
        "Camera Source to use (0=v4l2, 1=csi[default], 2=videotest, 3=eglstream)", NULL}
    ,
    {"mode", 'm', 0, G_OPTION_ARG_INT, &app->mode,
        "Capture mode value (1=still 2=video)", NULL}
    ,
    {"video-enc", 'v', 0, G_OPTION_ARG_INT, &app->encset.video_enc,
          "Video encoder type (0=h264[HW] 1=vp8[HW] 2=h265[HW] 3=vp9[HW] 4=h264[SW])",
        NULL}
    ,
    {"hw-enc-path", 'p', 0, G_OPTION_ARG_INT, &app->encset.hw_enc_type,
          "Frame Work type (1=V4L2 [Default])",
        NULL}
    ,
    {"enc-bitrate", 'b', 0, G_OPTION_ARG_INT, &app->encset.bitrate,
        "Video encoding Bit-rate(in bytes) e.g., --enc-bitrate=4000000", NULL}
    ,
    {"enc-controlrate", 0, 0, G_OPTION_ARG_INT, &app->encset.controlrate,
        "Video encoding Bit-rate control method 0 = Disable, 1 = variable(Default), 2 = constant "
        "e.g., --enc-controlrate=1", NULL}
    ,
    {"enc-EnableTwopassCBR", 0, 0, G_OPTION_ARG_INT, &app->encset.enabletwopassCBR,
        "Enable two pass CBR while encoding 0 = Disable, 1 = Enable "
        "e.g., --enc-EnableTwopassCBR=1", NULL}
    ,
    {"enc-profile", 0, 0, G_OPTION_ARG_INT, &app->encset.video_enc_profile,
          "Video encoder profile For H.264: 0=Baseline, 1=Main, 2=High",
        NULL}
    ,
    {"image-enc", 'J', 0, G_OPTION_ARG_INT, &app->encset.image_enc,
        "Image encoder type (0=jpeg_SW[jpegenc] 1=jpeg_HW[nvjpegenc])", NULL}
    ,
    {"file-type", 'k', 0, G_OPTION_ARG_INT, &app->file_type,
        "Container file type (0=mp4 1=3gp 2=mkv)", NULL}
    ,
    {"file-name", 0, 0, G_OPTION_ARG_STRING, &app->file_name,
        "Captured file name. nvcamtest is used by default", NULL}
    ,
     {"color-format", 0, 0,G_OPTION_ARG_INT, &app->color_format,
        "Color format to use (0=I420,"
        "1=NV12[For CSI only and default for CSI], 2=YUY2 and 3=UYVY [For V4L2 only, default YUY2 for v4l2])",
        NULL}
    ,
    {"enable-meta", 0, 0, G_OPTION_ARG_NONE, &app->enableMeta,
          "Enable Sensor MetaData reporting", NULL}
     ,
    {"app-profile", 0, 0, G_OPTION_ARG_NONE, &app->enableKpiProfile,
          "Enable KPI profiling",
        NULL}
    ,
    {"kpi-numbers", 0, 0, G_OPTION_ARG_NONE, &app->enableKpiNumbers,
          "Enable KPI measurement",
        NULL}
    ,
    {"cap-dev-node", 0, 0, G_OPTION_ARG_CALLBACK, parse_spec,
        "Video capture device node (0=/dev/video0[default], 1=/dev/video1, 2=/dev/video2) "
          "e.g., --cap-dev-node=0", NULL}
    ,
    {"svs", 0, 0, G_OPTION_ARG_CALLBACK, parse_spec,
          "[For USB] (=) chain for video Preview. [For CSI only] use \"nveglglessink\"",
        NULL}
    ,
    {"eglConfig", 0, 0, G_OPTION_ARG_CALLBACK, parse_spec,
        "EGL window Coordinates (x_pos y_pos) in that order "
        " e.g., --eglConfig=\"50 100\"",
        NULL}
    ,
    {"orientation", 0, 0, G_OPTION_ARG_INT, &app->flip_method,
        "Camera sensor orientation value", NULL}
    ,
    {"whitebalance", 'w', 0, G_OPTION_ARG_INT, &app->whitebalance,
        "Capture whitebalance value", NULL}
    ,
    {"timeout", 0, 0, G_OPTION_ARG_INT, &app->timeout,
        "Capture timeout value", NULL}
    ,
    {"saturation", 0, 0, G_OPTION_ARG_CALLBACK, parse_spec,
        "Camera Saturation value", NULL}
    ,
    {"sensor-id", 0, 0, G_OPTION_ARG_INT, &app->sensor_id,
        "Camera Sensor ID value", NULL}
    ,
    {"sensor-mode", 0, 0, G_OPTION_ARG_INT, &app->sensor_mode,
        "Camera Sensor Mode value", NULL}
    ,
    {"framerate", 0, 0, G_OPTION_ARG_INT, &app->framerate,
        "FrameRate of sensor mode (use with --framerate)", NULL}
    ,
    {"exposuretimerange", 0, 0, G_OPTION_ARG_CALLBACK, parse_spec,
        "Property to adjust exposure time range in nanoseconds"
        " e.g., --exposuretimerange=\"34000 358733000\"",
        NULL}
    ,
    {"gainrange", 0, 0, G_OPTION_ARG_CALLBACK, parse_spec,
        "Property to adjust gain range"
        " e.g., --gainrange=\"1 16\"",
        NULL}
    ,
    {"ispdigitalgainrange", 0, 0, G_OPTION_ARG_CALLBACK, parse_spec,
        "Property to adjust digital gain range"
        " e.g., --ispdigitalgainrange=\"1 8\"",
        NULL}
    ,
    {"aelock", 0, 0, G_OPTION_ARG_INT, &app->enableAeLock,
          "Enable AE Lock, default is disabled",
        NULL}
    ,
    {"awblock", 0, 0, G_OPTION_ARG_INT, &app->enableAwbLock,
          "Enable AWB Lock, default is disabled",
        NULL}
    ,
    {"exposurecompensation", 0, 0, G_OPTION_ARG_CALLBACK, parse_spec,
        "Property to adjust exposure compensation"
        " e.g., --exposurecompensation=0.5", NULL}
    ,
    {"aeantibanding", 0, 0, G_OPTION_ARG_INT, &app->ae_antibanding,
        "Property to set the auto exposure antibanding mode"
        " e.g., --aeantibanding=2", NULL}
    ,
    {"tnr-mode", 0, 0, G_OPTION_ARG_INT, &app->tnr_mode,
        "Property to select temporal noise reduction mode"
        " e.g., --tnr-mode=2", NULL}
    ,
    {"tnr-strength", 0, 0, G_OPTION_ARG_CALLBACK, parse_spec,
        "Property to adjust temporal noise reduction strength"
        " e.g., --tnr-strength=0.5", NULL}
    ,
    {"ee-mode", 0, 0, G_OPTION_ARG_INT, &app->ee_mode,
        "Property to select edge enhancement mode"
        " e.g., --ee-mode=2", NULL}
    ,
    {"ee-strength", 0, 0, G_OPTION_ARG_CALLBACK, parse_spec,
        "Property to adjust edge enhancement strength"
        " e.g., --ee-strength=0.5", NULL}
    ,
    {"overlayConfig", 0, 0, G_OPTION_ARG_CALLBACK, parse_spec,
          "Overlay Configuration Options index and coordinates in (index, x_pos, y_pos, width, height) order "
          " e.g. --overlayConfig=\"0, 0, 0, 1280, 720\"",
        NULL}
    ,
    {"automate", 'A', 0, G_OPTION_ARG_NONE, &app->aut.automate,
        "Run application in automation mode", NULL}
    ,
    {"start-time", 'S', 0, G_OPTION_ARG_INT, &app->aut.capture_start_time,
          "Start capture after specified time in seconds. Default = 5 sec (use with --automate or -A only)",
        NULL}
    ,
    {"quit-after", 'Q', 0, G_OPTION_ARG_INT, &app->aut.quit_time,
          "Quit application once automation is done after specified time in seconds. Default = 0 sec (use with --automate or -A only)",
        NULL}
    ,
    {"count", 'C', 0, G_OPTION_ARG_INT, &app->aut.iteration_count,
          "Number of iterations of automation testcase. Default = 1 (use with --automate or -A only)",
        NULL}
    ,
    {"num-sensors", 'N', 0, G_OPTION_ARG_INT, &app->aut.num_sensors,
          "Number of sensors  (use with --automate or -A only)",
        NULL}
    ,
    {"capture-gap", 0, 0, G_OPTION_ARG_INT, &app->aut.capture_gap,
          "Number of milliseconds between successive image/video capture. Default = 250 msec (use with --automate and --capture-auto only)",
        NULL}
    ,
    {"capture-time", 0, 0, G_OPTION_ARG_INT, &app->aut.capture_time,
          "Capture video for specified time in seconds. Default = 10 sec (use with --automate and --capture-auto only)",
        NULL}
    ,
    {"toggle-mode", 0, 0, G_OPTION_ARG_NONE, &app->aut.toggle_mode,
          "Toggle between still and video capture modes for count number of times (use with --automate or -A only)",
        NULL}
    ,
    {"capture-auto", 0, 0, G_OPTION_ARG_NONE, &app->aut.capture_auto,
          "Do image/video capture in automation mode for count number of times(use with --automate or -A only)",
        NULL}
    ,
    {"toggle-sensor", 0, 0, G_OPTION_ARG_NONE, &app->aut.toggle_sensor,
          "Toggle between num_sensors if given otherwise between sensor-id 0 and 1 (use with --automate or -A only)",
        NULL}
    ,
    {"toggle-sensor-modes", 0, 0, G_OPTION_ARG_NONE, &app->aut.toggle_sensor_modes,
          "Toggle between all sensor modes if given. (use with --automate or -A only)",
        NULL}
    ,
    {"enum-wb", 0, 0, G_OPTION_ARG_NONE, &app->aut.enum_wb,
          "Enumerate all white-balance modes for count number of times (use with --automate or -A only)",
        NULL}
    ,
    {"enum-st", 0, 0, G_OPTION_ARG_NONE, &app->aut.enum_st,
          "Enumerate saturation value through 0 to 2 by a step of 0.1 for count number of times (use with --automate or -A only)",
        NULL}
    ,
    {NULL}};

  ctx = g_option_context_new ("Nvidia GStreamer Camera Model Test");
  g_option_context_set_description (ctx, app->csi_resolution);
  group_argus = g_option_group_new ("nvarguscamersrc", "Application Options [NVArgusCamera, USB, V4L2 ONLY]:", "Show nvarguscamerasrc Options", NULL, NULL);
  g_option_group_add_entries (group_argus, options_argus);
  g_option_context_set_main_group (ctx, group_argus);
  g_option_context_add_group (ctx, gst_init_get_option_group ());

  if (!g_option_context_parse (ctx, &argc, &argv, &error)) {
    g_option_context_free (ctx);
    NVGST_ERROR_MESSAGE_V ("option parsing failed: %s", error->message);
    goto done;
  }
  if(app->encset.bitrate != 0)
    is_user_bitrate = 1;

  FILE *fp;
  fp = fopen(NVGST_DEFAULT_V4L2_NVENC_DEVICE_PATH, "r");
  if (fp == NULL)
  {
    if (app->encset.video_enc != FORMAT_H264_SW)
      g_print("Codec not supported. Falling back to opensrc H264 encoder\n");
    app->encset.video_enc = FORMAT_H264_SW;
  }
  if (fp)
    fclose(fp);

  g_option_context_free (ctx);

  if (!app->aut.automate)
    print_help ();

  gst_init (&argc, &argv);

  GET_TIMESTAMP(APP_LAUNCH);

  loop = g_main_loop_new (NULL, FALSE);

#ifdef WITH_STREAMING
  if (app->streaming_mode) {
    char *payloader;
    char *parser;
    char pipeline[256];

    NvGstRtspStreamCallbacks rtspcallbacks = { rtsp_video_stream_new,
      rtsp_video_stream_start, rtsp_video_stream_pause,
      rtsp_video_stream_resume, rtsp_video_stream_stop
    };

    nvgst_rtsp_lib = dlopen (NVGST_RTSP_LIBRARY, RTLD_NOW);
    if (!nvgst_rtsp_lib) {
      NVGST_ERROR_MESSAGE_V ("Error opening " NVGST_RTSP_LIBRARY ": %s",
          dlerror ());
      goto done;
    }

    nvgst_rtsp_server_init_fcn nvgst_rtsp_init =
        (nvgst_rtsp_server_init_fcn) dlsym (nvgst_rtsp_lib,
        "nvgst_rtsp_server_init");

    if (!nvgst_rtsp_init (&nvgst_rtsp_functions)) {
      NVGST_ERROR_MESSAGE ("Could not initialize nvgst_rtsp library");
      goto done;
    }

    switch (app->encset.video_enc) {
      case FORMAT_H264_HW:
        payloader = "rtph264pay";
        parser = "h264parse";
        break;
      case FORMAT_VP8_HW:
        payloader = "rtpvp8pay";
        parser = "identity";
        break;
      case FORMAT_H265_HW:
        payloader = "rtph265pay";
        parser = "h265parse";
        break;
      case FORMAT_VP9_HW:
        payloader = "rtpvp9pay";
        parser = "identity";
        break;
      case FORMAT_H264_SW:
        payloader = "rtph264pay";
        parser = "h264parse";
        break;
      default:
        NVGST_ERROR_MESSAGE ("Unsupported codec for streaming");
        goto done;
    }

    snprintf(pipeline, sizeof(pipeline) - 1,
        "appsrc name=mysrc is-live=0 do-timestamp=1 ! %s ! %s name=pay0 pt=96",
        parser, payloader);

    app->video_streaming_ctx.media_factory =
        nvgst_rtsp_functions.create_stream ("/test", pipeline, &rtspcallbacks);
    if (!app->video_streaming_ctx.media_factory) {
      NVGST_ERROR_MESSAGE ("Could not create rtsp video stream");
      goto done;
    }

    g_object_set (G_OBJECT(app->video_streaming_ctx.media_factory), "shared", TRUE, NULL);
  }
#endif

  if (!app->aut.automate) {
    channel = g_io_channel_unix_new (0);
    g_io_add_watch (channel, G_IO_IN, on_input, NULL);
  }

  _intr_setup ();
  g_timeout_add (400, check_for_interrupt, NULL);

  /* Automation stuff */
  if (app->aut.automate) {

    if (app->aut.capture_start_time < 0) {
      g_print ("Invalid capture start time. Can't go back in time!/"
          "Not even Gstreamer! Setting default time.\n");
      app->aut.capture_start_time = NVGST_DEFAULT_CAP_START_DELAY;
    }

    if (app->aut.quit_time < 0) {
      g_print ("Invalid quit after time. Setting default quit time = 0.\n");
      app->aut.quit_time = NVGST_DEFAULT_QUIT_TIME;
    }

    if (app->aut.capture_gap < 0) {
      g_print
          ("Invalid capture gap time. Setting default capture gap = 250 ms\n");
      app->aut.capture_gap = NVGST_DEFAULT_CAPTURE_GAP;
    }

    if (app->aut.capture_time < 0) {
      g_print ("Invalid capture time. Setting default capture time = 10 s\n");
      app->aut.capture_time = NVGST_DEFAULT_CAPTURE_TIME;
    }

    if (app->aut.iteration_count < 1) {
      g_print ("Invalid iteration count. Setting to default count = 1.\n");
      app->aut.iteration_count = NVGST_DEFAULT_ITERATION_COUNT;
    }

    g_timeout_add_seconds (app->aut.capture_start_time, auto_capture, NULL);

  }

  CALL_GUI_FUNC (init, &GET_GUI_CTX (), &argc, &argv);

  /* Start capture pipeline */
#ifdef WITH_STREAMING
  if (app->streaming_mode) {
    app->mode = CAPTURE_VIDEO;
    g_main_loop_run (loop);
  } else
#endif
  if (create_capture_pipeline ()) {
    NVGST_INFO_MESSAGE ("iterating capture loop ....");
    g_main_loop_run (loop);
  } else
    NVGST_CRITICAL_MESSAGE ("Capture Pipeline creation failed");

  /* Out of the main loop, now clean up */
  CALL_GUI_FUNC (finish);

  destroy_capture_pipeline ();

  NVGST_INFO_MESSAGE ("Capture completed");

done:
  if (channel)
    g_io_channel_unref (channel);

#ifdef WITH_STREAMING
  if (nvgst_rtsp_lib)
    dlclose (nvgst_rtsp_lib);
#endif

  if (loop)
    g_main_loop_unref (loop);

#if !GUI
  g_mutex_lock (app->lock);
  if (app->disp.window)
    nvgst_destroy_window (&app->disp);
  g_mutex_unlock (app->lock);

  g_thread_join(app->x_event_thread);

  if (app->disp.mDisplay)
    nvgst_x11_uninit (&app->disp);
#endif

  if (app->lock) {
    g_mutex_clear (app->lock);
    app->lock = NULL;
  }

  if (app->cond) {
    g_cond_clear (app->cond);
    app->cond = NULL;
  }

  if (app->x_cond) {
    g_cond_clear (app->x_cond);
    app->x_cond = NULL;
  }

  g_free (app->vidcap_device);
  g_free (app->cap_dev_node);
  g_free (app->file_name);
  g_free (app->csi_options_argus);
  g_free (app->overlayConfig);
  g_free (app->eglConfig);
  g_free (app->lock);
  g_free (app->cond);
  g_free (app->x_cond);

  NVGST_INFO_MESSAGE ("Camera application will now exit");

  return ((app->return_value == -1) ? -1 : 0);
}
