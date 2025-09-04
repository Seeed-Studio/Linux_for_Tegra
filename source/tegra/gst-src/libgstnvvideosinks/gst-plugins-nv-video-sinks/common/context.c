/*
 * Copyright (c) 2018-2021, NVIDIA CORPORATION.  All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License, version 2.1, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <gst/base/gstdataqueue.h>

#include "context.h"
#include "window.h"

#if NV_VIDEO_SINKS_HAS_EGL
#include "context_egl.h"
#endif

#define GST_CAT_DEFAULT gst_debug_nv_video_context
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

struct _GstNvVideoContextPrivate
{
  GstDataQueue *queue;

  GThread *render_thread;
  gboolean render_thread_active;
  gboolean eos_handled;
  GstFlowReturn last_ret;

  GMutex render_lock;
  GCond create_cond;
  GCond quit_cond;
  GCond eos_cond;
};

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstNvVideoContext, gst_nv_video_context,
    GST_TYPE_OBJECT, G_ADD_PRIVATE(GstNvVideoContext));

GstNvVideoContextType
gst_nv_video_context_get_handle_type (GstNvVideoContext * context)
{
  g_return_val_if_fail (GST_IS_NV_VIDEO_CONTEXT (context),
      GST_NV_VIDEO_CONTEXT_TYPE_NONE);

  return context->type;
}

static gpointer
gst_nv_video_context_render_thread_func (GstNvVideoContext * context)
{
  GstNvVideoContextClass *context_class;
  GstDataQueueItem *item = NULL;
  GstFlowReturn ret = GST_FLOW_OK;

  context_class = GST_NV_VIDEO_CONTEXT_GET_CLASS (context);

  GST_DEBUG_OBJECT (context, "render thread started");

  context_class->setup (context);

  cudaError_t CUerr = cudaSuccess;
  GST_LOG_OBJECT (context, "SETTING CUDA DEVICE = %d in func=%s\n", context->gpu_id, __func__);
  CUerr = cudaSetDevice(context->gpu_id);
  if (CUerr != cudaSuccess) {
    GST_LOG_OBJECT (context,"\n *** Unable to set device in %s Line %d\n", __func__, __LINE__);
    return NULL;
  }

  g_mutex_lock (&context->priv->render_lock);
  context->priv->render_thread_active = TRUE;
  context->priv->last_ret = ret;
  g_cond_signal (&context->priv->create_cond);
  g_mutex_unlock (&context->priv->render_lock);

  while (gst_data_queue_pop (context->priv->queue, &item)) {
    GstMiniObject *object = item->object;
    GstBuffer *buf = NULL;

    GST_TRACE_OBJECT (context,
        "render thread: got data queue item %" GST_PTR_FORMAT, object);

    ret = GST_FLOW_ERROR;

    if (GST_IS_BUFFER (object)) {
      buf = GST_BUFFER_CAST (item->object);

      if (context_class->show_frame (context, buf)) {
        ret = GST_FLOW_OK;
      }
    } else if (!object) {
      GST_TRACE_OBJECT (context, "render thread: handle EOS");

      context_class->handle_eos (context);

      g_mutex_lock (&context->priv->render_lock);
      g_cond_signal (&context->priv->eos_cond);
      context->priv->eos_handled = TRUE;
      g_mutex_unlock (&context->priv->render_lock);

      GST_TRACE_OBJECT (context, "render thread: handled EOS");
    } else {
      g_assert_not_reached ();
    }

    item->destroy (item);

    g_mutex_lock (&context->priv->render_lock);
    context->priv->last_ret = ret;
    g_mutex_unlock (&context->priv->render_lock);

    if (ret != GST_FLOW_OK) {
      break;
    }

    GST_TRACE_OBJECT (context, "render thread: handled");
  }

  GST_DEBUG_OBJECT (context, "tearing down render thread");

  context_class->cleanup (context);

  g_mutex_lock (&context->priv->render_lock);
  g_cond_signal (&context->priv->quit_cond);
  context->priv->render_thread_active = FALSE;
  g_mutex_unlock (&context->priv->render_lock);

  GST_DEBUG_OBJECT (context, "render thread exit");

  return NULL;
}

static void
gst_nv_video_context_queue_free_item (GstDataQueueItem * item)
{
  GstDataQueueItem *data = item;
  if (data->object)
    gst_mini_object_unref (data->object);
  g_slice_free (GstDataQueueItem, data);
}

static gboolean
gst_nv_video_context_render_thread_show_frame (GstNvVideoContext * context,
    GstBuffer * buf)
{
  gboolean last_ret;
  GstDataQueueItem *item;
  GstMiniObject *obj = GST_MINI_OBJECT_CAST (buf);

  g_assert (obj);

  g_mutex_lock (&context->priv->render_lock);
  last_ret = context->priv->last_ret;
  g_mutex_unlock (&context->priv->render_lock);

  if (last_ret != GST_FLOW_OK) {
    return FALSE;
  }

  item = g_slice_new (GstDataQueueItem);
  item->destroy = (GDestroyNotify) gst_nv_video_context_queue_free_item;
  item->object = gst_mini_object_ref (obj);
  item->size = 0;
  item->duration = GST_CLOCK_TIME_NONE;
  item->visible = TRUE;

  if (!gst_data_queue_push (context->priv->queue, item)) {
    item->destroy (item);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_nv_video_context_queue_check_full (GstDataQueue * queue, guint visible,
    guint bytes, guint64 time, gpointer checkdata)
{
  return FALSE;
}

static void
gst_nv_video_context_finalize (GObject * object)
{
  GstNvVideoContext *context = GST_NV_VIDEO_CONTEXT (object);

  GST_DEBUG_OBJECT (context, "finalize begin");

  if (context->priv->queue) {
    g_object_unref (context->priv->queue);
    context->priv->queue = NULL;
  }

  if (context->priv->render_thread) {
    g_thread_unref (context->priv->render_thread);
    context->priv->render_thread = NULL;
  }

  if (context->window) {
    gst_object_unref (context->window);
    context->window = NULL;
  }

  if (context->display) {
    gst_object_unref (context->display);
    context->display = NULL;
  }

  g_mutex_clear (&context->priv->render_lock);
  g_cond_clear (&context->priv->create_cond);
  g_cond_clear (&context->priv->quit_cond);
  g_cond_clear (&context->priv->eos_cond);

  GST_DEBUG_OBJECT (context, "finalize done");

  G_OBJECT_CLASS (gst_nv_video_context_parent_class)->finalize (object);
}

static void
gst_nv_video_context_init (GstNvVideoContext * context)
{
  GstNvVideoContext *self = GST_NV_VIDEO_CONTEXT (context);
  context->priv = (GstNvVideoContextPrivate *)gst_nv_video_context_get_instance_private (self);

  g_mutex_init (&context->priv->render_lock);
  g_cond_init (&context->priv->create_cond);
  g_cond_init (&context->priv->quit_cond);
  g_cond_init (&context->priv->eos_cond);

  context->priv->queue = NULL;
  context->priv->render_thread = NULL;
  context->priv->render_thread_active = FALSE;
  context->priv->eos_handled = FALSE;

  context->using_NVMM = 0;
  context->cuContext = NULL;
  context->cuResource[0] = NULL;
  context->cuResource[1] = NULL;
  context->cuResource[2] = NULL;
  context->gpu_id = 0;

  GST_DEBUG_OBJECT (context, "init done");
}

static void
gst_nv_video_context_class_init (GstNvVideoContextClass * klass)
{
  G_OBJECT_CLASS (klass)->finalize = gst_nv_video_context_finalize;
}

GstNvVideoContext *
gst_nv_video_context_new (GstNvVideoDisplay * display)
{
  GstNvVideoContext *context = NULL;
  static volatile gsize debug_init = 0;
  const gchar *context_name = NULL;

  if (g_once_init_enter (&debug_init)) {
    GST_DEBUG_CATEGORY_INIT (gst_debug_nv_video_context, "nvvideocontext", 0,
        "nvvideocontext");
    g_once_init_leave (&debug_init, 1);
  }

  context_name = g_getenv ("GST_NV_VIDEO_CONTEXT");

#if NV_VIDEO_SINKS_HAS_EGL
  if (!context && (!context_name || g_strstr_len (context_name, 3, "egl"))) {
    context = GST_NV_VIDEO_CONTEXT (gst_nv_video_context_egl_new (display));
  }
#endif

  if (!context) {
    GST_ERROR ("couldn't create context. GST_NV_VIDEO_CONTEXT = %s",
        context_name ? context_name : NULL);
    return NULL;
  }

  context->display = gst_object_ref (display);

  GST_DEBUG_OBJECT (context, "created context for display %" GST_PTR_FORMAT,
      display);

  return context;
}

gboolean
gst_nv_video_context_show_frame (GstNvVideoContext * context, GstBuffer * buf)
{
  g_mutex_lock (&context->priv->render_lock);
  if (context->priv->render_thread_active) {
    g_mutex_unlock (&context->priv->render_lock);
    return gst_nv_video_context_render_thread_show_frame (context, buf);
  }
  g_mutex_unlock (&context->priv->render_lock);

  return FALSE;
}

void
gst_nv_video_context_handle_tearing (GstNvVideoContext * context)
{
  GstNvVideoContextClass *context_class =
      GST_NV_VIDEO_CONTEXT_GET_CLASS (context);
  context_class->handle_tearing (context);
  return;
}

void
gst_nv_video_context_handle_drc (GstNvVideoContext * context)
{
  GstNvVideoContextClass *context_class =
      GST_NV_VIDEO_CONTEXT_GET_CLASS (context);

  context_class->handle_drc (context);
  return;
}

void
gst_nv_video_context_handle_eos (GstNvVideoContext * context)
{
  GstNvVideoContextClass *context_class =
      GST_NV_VIDEO_CONTEXT_GET_CLASS (context);
  GstDataQueueItem *item;

  g_mutex_lock (&context->priv->render_lock);

  if (!context->priv->render_thread_active) {
    g_mutex_unlock (&context->priv->render_lock);
    context_class->handle_eos (context);
    return;
  }
  // Push NULL object in queue to indicate EOS and wait till it is handled.
  item = g_slice_new (GstDataQueueItem);
  item->destroy = (GDestroyNotify) gst_nv_video_context_queue_free_item;
  item->object = NULL;
  item->size = 0;
  item->duration = GST_CLOCK_TIME_NONE;
  item->visible = TRUE;

  if (!gst_data_queue_push (context->priv->queue, item)) {
    GST_ERROR_OBJECT (context, "faild to send EOS to render thread");
    item->destroy (item);
    g_mutex_unlock (&context->priv->render_lock);
    return;
  }
  GST_TRACE_OBJECT (context, "wait for render thread to handle EOS");
  while (context->priv->render_thread_active && !context->priv->eos_handled) {
    gint64 end = g_get_monotonic_time () + G_TIME_SPAN_SECOND;
    g_cond_wait_until (&context->priv->eos_cond, &context->priv->render_lock, end);
  }
  GST_TRACE_OBJECT (context, "wait for render thread to handle EOS is done");
  context->priv->eos_handled = FALSE;
  g_mutex_unlock (&context->priv->render_lock);
}

GstCaps *
gst_nv_video_context_get_caps (GstNvVideoContext * context)
{
  GstNvVideoContextClass *context_class;

  if (!context) {
    return NULL;
  }

  context_class = GST_NV_VIDEO_CONTEXT_GET_CLASS (context);

  return context_class->get_caps (context);
}

gboolean
gst_nv_video_context_set_window (GstNvVideoContext * context,
    GstNvVideoWindow * window)
{
  if (context->window) {
    gst_object_unref (context->window);
  }

  if (window) {
    // Before the object's GObjectClass.dispose method is called, every
    // GWeakRef associated with becomes empty.
    g_weak_ref_set (&window->context, context);
  }

  context->window = window ? gst_object_ref (window) : NULL;

  return TRUE;
}

void
gst_nv_video_context_destroy_render_thread (GstNvVideoContext * context)
{
  if (context->priv->queue) {
    gst_data_queue_set_flushing (context->priv->queue, TRUE);
    gst_data_queue_flush (context->priv->queue);
  }

  g_mutex_lock (&context->priv->render_lock);

  if (context->priv->render_thread_active) {
    GST_DEBUG_OBJECT (context, "destroying render thread");
    while (context->priv->render_thread_active) {
      g_cond_wait (&context->priv->quit_cond, &context->priv->render_lock);
    }
    g_thread_join (context->priv->render_thread);
    GST_DEBUG_OBJECT (context, "render thread destroyed");
  }

  g_mutex_unlock (&context->priv->render_lock);
}

gboolean
gst_nv_video_context_create_render_thread (GstNvVideoContext * context)
{
  g_mutex_lock (&context->priv->render_lock);

  if (!context->priv->render_thread) {
    g_assert (context->priv->queue == NULL);

    context->priv->queue =
        gst_data_queue_new (gst_nv_video_context_queue_check_full, NULL, NULL,
        NULL);

    if (!context->priv->queue) {
      g_mutex_unlock (&context->priv->render_lock);
      return FALSE;
    }

    gst_data_queue_set_flushing (context->priv->queue, FALSE);
    gst_data_queue_flush (context->priv->queue);

    context->priv->render_thread =
        g_thread_new ("NvVideoRenderThread",
        (GThreadFunc) gst_nv_video_context_render_thread_func, context);

    while (!context->priv->render_thread_active) {
      g_cond_wait (&context->priv->create_cond, &context->priv->render_lock);
    }

    if (context->priv->last_ret != GST_FLOW_OK) {
      g_object_unref (context->priv->queue);
      context->priv->queue = NULL;
      g_mutex_unlock (&context->priv->render_lock);
      return FALSE;
    }

    GST_INFO_OBJECT (context, "render thread created");
  }

  g_mutex_unlock (&context->priv->render_lock);

  return TRUE;
}

gboolean
gst_nv_video_context_create (GstNvVideoContext * context)
{
  GstNvVideoContextClass *context_class;

  context_class = GST_NV_VIDEO_CONTEXT_GET_CLASS (context);

  return context_class->create (context);
}
