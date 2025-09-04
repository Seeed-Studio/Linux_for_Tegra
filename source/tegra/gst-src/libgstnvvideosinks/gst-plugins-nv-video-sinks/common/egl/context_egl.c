/*
 * Copyright (c) 2018-2023, NVIDIA CORPORATION.  All rights reserved.
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

#include "context_egl.h"
#include "display.h"
#include "display_x11.h"
#include "window.h"
#include "nvbufsurface.h"

#include <EGL/egl.h>

G_GNUC_INTERNAL extern GstDebugCategory *gst_debug_nv_video_context;
#define GST_CAT_DEFAULT gst_debug_nv_video_context

G_DEFINE_TYPE (GstNvVideoContextEgl, gst_nv_video_context_egl,
    GST_TYPE_NV_VIDEO_CONTEXT);

static GstCaps *
gst_nv_video_context_egl_new_template_caps (GstVideoFormat format)
{
  return gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, gst_video_format_to_string (format),
      "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
}

static void
log_egl_error (GstNvVideoContext * context, const char *name)
{
  GST_ERROR_OBJECT (context, "egl error: %s returned %x", name, eglGetError ());
}

static gboolean
gst_nv_video_context_egl_is_surface_changed (GstNvVideoContextEgl * context_egl)
{
  gint w, h;

  eglQuerySurface (context_egl->display, context_egl->surface, EGL_WIDTH, &w);
  eglQuerySurface (context_egl->display, context_egl->surface, EGL_HEIGHT, &h);

  if (context_egl->surface_width != w || context_egl->surface_height != h) {
    context_egl->surface_width = w;
    context_egl->surface_height = h;
    return TRUE;
  }

  return FALSE;
}

static gboolean
gst_nv_video_context_egl_show_frame (GstNvVideoContext * context,
    GstBuffer * buf)
{
  GstNvVideoContextEgl *context_egl = GST_NV_VIDEO_CONTEXT_EGL (context);
  EGLImageKHR image = EGL_NO_IMAGE_KHR;
  GstMemory *mem;
  NvBufSurface *in_surface = NULL;
  gboolean is_cuda_mem = TRUE;

  if (!context_egl->surface) {
    guintptr handle = gst_nv_video_window_get_handle (context->window);

    context_egl->surface =
        eglCreateWindowSurface (context_egl->display, context_egl->config,
        (EGLNativeWindowType) handle, NULL);
    if (context_egl->surface == EGL_NO_SURFACE) {
      log_egl_error (context, "eglCreateWindowSurface");
      return FALSE;
    }

    if (!eglMakeCurrent (context_egl->display, context_egl->surface,
            context_egl->surface, context_egl->context)) {
      log_egl_error (context, "eglMakeCurrent");
      return FALSE;
    }

    GST_DEBUG_OBJECT (context, "egl surface %p created", context_egl->surface);
  }

  if (!context_egl->renderer) {
    context_egl->renderer = gst_nv_video_renderer_new (context, "gl");
    if (!context_egl->renderer) {
      GST_ERROR_OBJECT (context, "renderer creation failed");
      return FALSE;
    }
    if (!gst_nv_video_renderer_setup (context_egl->renderer)) {
      GST_ERROR_OBJECT (context, "renderer setup failed");
      return FALSE;
    }
  }

  if (context->using_NVMM) {
    if (!context->is_cuda_init) {
      if (!gst_nv_video_renderer_cuda_init (context, context_egl->renderer)) {
        GST_ERROR_OBJECT (context, "cuda init failed");
        return FALSE;
      }
    }
  }

  if (gst_nv_video_context_egl_is_surface_changed (context_egl)) {
    GST_DEBUG_OBJECT (context, "surface dimensions changed to %dx%d",
        context_egl->surface_width, context_egl->surface_height);

    gst_nv_video_renderer_update_viewport (context_egl->renderer,
        context_egl->surface_width, context_egl->surface_height);
  }

  if (gst_buffer_n_memory (buf) >= 1 && (mem = gst_buffer_peek_memory (buf, 0))) {
    //Software buffer handling
    if (!context->using_NVMM) {
       if (!gst_nv_video_renderer_fill_texture(context, context_egl->renderer, buf)) {
         GST_ERROR_OBJECT (context, "fill_texture failed");
         return FALSE;
       }
       if (!gst_nv_video_renderer_draw_2D_Texture (context_egl->renderer)) {
         GST_ERROR_OBJECT (context, "draw 2D Texture failed");
         return FALSE;
       }
    }
    else {
      // NvBufSurface support (NVRM and CUDA)
      GstMapInfo map = { NULL, (GstMapFlags) 0, NULL, 0, 0, };
      mem = gst_buffer_peek_memory (buf, 0);
      gst_memory_map (mem, &map, GST_MAP_READ);

      /* Types of Buffers handled -
        *     NvBufSurface
        *                     - NVMM buffer type
        *                     - Cuda buffer type
       */
      /* NvBufSurface type are handled here */
      in_surface = (NvBufSurface*) map.data;
      NvBufSurfaceMemType memType = in_surface->memType;

      if (memType == NVBUF_MEM_DEFAULT) {
#ifdef IS_DESKTOP
        memType = NVBUF_MEM_CUDA_DEVICE;
#else
        memType = NVBUF_MEM_SURFACE_ARRAY;
#endif
      }

      if (memType == NVBUF_MEM_SURFACE_ARRAY || memType == NVBUF_MEM_HANDLE) {
        is_cuda_mem = FALSE;
      }

      if (is_cuda_mem == FALSE) {
        /* NvBufSurface - NVMM buffer type are handled here */
        if (in_surface->batchSize != 1) {
          GST_ERROR_OBJECT (context,"ERROR: Batch size not 1\n");
          return FALSE;
        }
        if (NvBufSurfaceMapEglImage (in_surface, 0) !=0 ) {
          GST_ERROR_OBJECT (context,"ERROR: NvBufSurfaceMapEglImage\n");
          return FALSE;
        }
        image = in_surface->surfaceList[0].mappedAddr.eglImage;
        gst_nv_video_renderer_draw_eglimage (context_egl->renderer, image);
      }
      else {
        /* NvBufSurface - Cuda buffer type are handled here */
        if (!gst_nv_video_renderer_cuda_buffer_copy (context, context_egl->renderer, buf))
        {
          GST_ERROR_OBJECT (context,"cuda buffer copy failed\n");
          return FALSE;
        }
        if (!gst_nv_video_renderer_draw_2D_Texture (context_egl->renderer)) {
          GST_ERROR_OBJECT (context,"draw 2D texture failed");
          return FALSE;
        }
      }
      gst_memory_unmap (mem, &map);
    }
  }


  if (!eglSwapBuffers (context_egl->display, context_egl->surface)) {
    log_egl_error (context, "eglSwapBuffers");
  }

  if (image != EGL_NO_IMAGE_KHR) {
    NvBufSurfaceUnMapEglImage (in_surface, 0);
  }

  GST_TRACE_OBJECT (context, "release %p hold %p", context_egl->last_buf, buf);

  // TODO: We hold buffer used in current drawing till next swap buffer
  // is completed so that decoder won't write it till GL has finished using it.
  // When Triple buffering in X is enabled, this can cause tearing as completion
  // of next swap buffer won't guarantee GL has finished with the buffer used in
  // current swap buffer. This issue will be addresed when we transfer SyncFds
  // from decoder <-> sink.
  if (!context_egl->is_drc_on) {
    gst_buffer_replace (&context_egl->last_buf, buf);
  }
  return TRUE;
}

static void
gst_nv_video_context_egl_handle_tearing (GstNvVideoContext * context)
{
  GstNvVideoContextEgl *context_egl = GST_NV_VIDEO_CONTEXT_EGL (context);
  context_egl->is_drc_on = 0;
  return;
}

static void
gst_nv_video_context_egl_handle_drc (GstNvVideoContext * context)
{
  GstNvVideoContextEgl *context_egl = GST_NV_VIDEO_CONTEXT_EGL (context);

  GST_TRACE_OBJECT (context, "release last frame when resolution changes %p", context_egl->last_buf);

  if (context_egl->last_buf)
    context_egl->is_drc_on = 1;
  gst_buffer_replace (&context_egl->last_buf, NULL);
}

static void
gst_nv_video_context_egl_handle_eos (GstNvVideoContext * context)
{
  GstNvVideoContextEgl *context_egl = GST_NV_VIDEO_CONTEXT_EGL (context);

  GST_TRACE_OBJECT (context, "release last frame %p", context_egl->last_buf);

  gst_buffer_replace (&context_egl->last_buf, NULL);
}

static gboolean
gst_nv_video_context_egl_setup (GstNvVideoContext * context)
{
  GstNvVideoDisplayX11 *display_x11 = (GstNvVideoDisplayX11 *) context->display;
  GstNvVideoContextEgl *context_egl = GST_NV_VIDEO_CONTEXT_EGL (context);
  EGLint major, minor;
  EGLint num_configs;
  EGLint attr[] = {
    EGL_BUFFER_SIZE, 24,
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_NONE
  };
  EGLint attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };

  GST_DEBUG_OBJECT (context, "EGL context setup");

  context_egl->display =
      eglGetDisplay ((EGLNativeDisplayType) display_x11->dpy);

  if (!eglInitialize (context_egl->display, &major, &minor)) {
    log_egl_error (context, "eglInitialize");
    return FALSE;
  }

  GST_INFO_OBJECT (context, "egl version: %d.%d", major, minor);

  eglBindAPI (EGL_OPENGL_ES_API);

  if (!eglChooseConfig (context_egl->display, attr, &context_egl->config, 1,
          &num_configs)) {
    log_egl_error (context, "eglChooseConfig");
  }

  context_egl->context =
      eglCreateContext (context_egl->display, context_egl->config,
      EGL_NO_CONTEXT, attribs);
  if (context_egl->context == EGL_NO_CONTEXT) {
    log_egl_error (context, "eglChooseConfig");
    return FALSE;
  }

  GST_DEBUG_OBJECT (context, "egl context %p created", context_egl->context);

  return TRUE;
}

static void
gst_nv_video_context_egl_cleanup (GstNvVideoContext * context)
{
  GstNvVideoContextEgl *context_egl = GST_NV_VIDEO_CONTEXT_EGL (context);

  GST_DEBUG_OBJECT (context, "egl cleanup display=%p surface=%p context=%p",
      context_egl->display, context_egl->surface, context_egl->context);

  if (context_egl->renderer) {
    if (context->using_NVMM) {
      gst_nv_video_renderer_cuda_cleanup (context, context_egl->renderer);
    }
    gst_nv_video_renderer_cleanup (context_egl->renderer);
    gst_object_unref (context_egl->renderer);
    context_egl->renderer = NULL;
  }

  if (!eglMakeCurrent (context_egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
          EGL_NO_CONTEXT)) {
    log_egl_error (context, "eglMakeCurrent");
  }

  if (context_egl->surface) {
    eglDestroySurface (context_egl->display, context_egl->surface);
    context_egl->surface = NULL;
  }

  if (context_egl->context) {
    eglDestroyContext (context_egl->display, context_egl->context);
    context_egl->context = NULL;
  }

  eglTerminate (context_egl->display);
  context_egl->display = NULL;

  GST_DEBUG_OBJECT (context, "egl cleanup done");

  return;
}

static GstCaps *
gst_nv_video_context_egl_getcaps (GstNvVideoContext * context)
{
  GstNvVideoContextEgl *context_egl = GST_NV_VIDEO_CONTEXT_EGL (context);

  GST_LOG_OBJECT (context, "context add_caps  %" GST_PTR_FORMAT,
      context_egl->caps);

  return gst_caps_copy (context_egl->caps);
}

static gboolean
gst_nv_video_context_egl_create (GstNvVideoContext * context)
{
  return gst_nv_video_context_create_render_thread (context);
}

static void
gst_nv_video_context_egl_finalize (GObject * object)
{
  GstNvVideoContext *context = GST_NV_VIDEO_CONTEXT (object);
  GstNvVideoContextEgl *context_egl = GST_NV_VIDEO_CONTEXT_EGL (context);

  GST_DEBUG_OBJECT (context, "finalize begin");

  gst_nv_video_context_destroy_render_thread (context);

  if (context_egl->caps) {
    gst_caps_unref (context_egl->caps);
  }

  G_OBJECT_CLASS (gst_nv_video_context_egl_parent_class)->finalize (object);

  GST_DEBUG_OBJECT (context, "finalize end");
}

static void
gst_nv_video_context_egl_class_init (GstNvVideoContextEglClass * klass)
{
  GstNvVideoContextClass *context_class = (GstNvVideoContextClass *) klass;

  context_class->create = GST_DEBUG_FUNCPTR (gst_nv_video_context_egl_create);
  context_class->setup = GST_DEBUG_FUNCPTR (gst_nv_video_context_egl_setup);
  context_class->get_caps =
      GST_DEBUG_FUNCPTR (gst_nv_video_context_egl_getcaps);
  context_class->show_frame =
      GST_DEBUG_FUNCPTR (gst_nv_video_context_egl_show_frame);
  context_class->handle_eos =
      GST_DEBUG_FUNCPTR (gst_nv_video_context_egl_handle_eos);
  context_class->handle_drc =
      GST_DEBUG_FUNCPTR (gst_nv_video_context_egl_handle_drc);
  context_class->handle_tearing =
      GST_DEBUG_FUNCPTR (gst_nv_video_context_egl_handle_tearing);
  context_class->cleanup = GST_DEBUG_FUNCPTR (gst_nv_video_context_egl_cleanup);

  G_OBJECT_CLASS (klass)->finalize = gst_nv_video_context_egl_finalize;
}

static void
gst_nv_video_context_egl_init (GstNvVideoContextEgl * context_egl)
{
  GstNvVideoContext *context = (GstNvVideoContext *) context_egl;

  context->type = GST_NV_VIDEO_CONTEXT_TYPE_EGL;

  context_egl->context = NULL;
  context_egl->display = NULL;
  context_egl->surface = NULL;
  context_egl->config = NULL;

  context_egl->surface_width = 0;
  context_egl->surface_height = 0;

  context_egl->last_buf = NULL;

  context_egl->is_drc_on = 0;
}

GstNvVideoContextEgl *
gst_nv_video_context_egl_new (GstNvVideoDisplay * display)
{
  GstNvVideoContextEgl *ret;
  GstCaps *caps = NULL;
  guint i, n;

  // for now we need x11 display for EGL context.
  if ((gst_nv_video_display_get_handle_type (display) &
          GST_NV_VIDEO_DISPLAY_TYPE_X11)
      == 0) {
    return NULL;
  }

  ret = g_object_new (GST_TYPE_NV_VIDEO_CONTEXT_EGL, NULL);
  gst_object_ref_sink (ret);

  // TODO: query from egl
  caps = gst_caps_new_empty ();
  // Software buffer caps
  gst_caps_append (caps,
      gst_nv_video_context_egl_new_template_caps (GST_VIDEO_FORMAT_RGBA));
  gst_caps_append (caps,
      gst_nv_video_context_egl_new_template_caps (GST_VIDEO_FORMAT_BGRA));
  gst_caps_append (caps,
      gst_nv_video_context_egl_new_template_caps (GST_VIDEO_FORMAT_ARGB));
  gst_caps_append (caps,
      gst_nv_video_context_egl_new_template_caps (GST_VIDEO_FORMAT_ABGR));
  gst_caps_append (caps,
      gst_nv_video_context_egl_new_template_caps (GST_VIDEO_FORMAT_RGBx));
  gst_caps_append (caps,
      gst_nv_video_context_egl_new_template_caps (GST_VIDEO_FORMAT_BGRx));
  gst_caps_append (caps,
      gst_nv_video_context_egl_new_template_caps (GST_VIDEO_FORMAT_xRGB));
  gst_caps_append (caps,
      gst_nv_video_context_egl_new_template_caps (GST_VIDEO_FORMAT_xBGR));
  gst_caps_append (caps,
      gst_nv_video_context_egl_new_template_caps (GST_VIDEO_FORMAT_AYUV));
  gst_caps_append (caps,
      gst_nv_video_context_egl_new_template_caps (GST_VIDEO_FORMAT_Y444));
  gst_caps_append (caps,
      gst_nv_video_context_egl_new_template_caps (GST_VIDEO_FORMAT_RGB));
  gst_caps_append (caps,
      gst_nv_video_context_egl_new_template_caps (GST_VIDEO_FORMAT_BGR));
  gst_caps_append (caps,
      gst_nv_video_context_egl_new_template_caps (GST_VIDEO_FORMAT_I420));
  gst_caps_append (caps,
      gst_nv_video_context_egl_new_template_caps (GST_VIDEO_FORMAT_YV12));
  gst_caps_append (caps,
      gst_nv_video_context_egl_new_template_caps (GST_VIDEO_FORMAT_NV12));
  gst_caps_append (caps,
      gst_nv_video_context_egl_new_template_caps (GST_VIDEO_FORMAT_NV21));
  gst_caps_append (caps,
      gst_nv_video_context_egl_new_template_caps (GST_VIDEO_FORMAT_Y42B));
  gst_caps_append (caps,
      gst_nv_video_context_egl_new_template_caps (GST_VIDEO_FORMAT_Y41B));
  gst_caps_append (caps,
      gst_nv_video_context_egl_new_template_caps (GST_VIDEO_FORMAT_RGB16));

  n = gst_caps_get_size(caps);
  // NVMM buffer caps
  gst_caps_append (caps,
      gst_nv_video_context_egl_new_template_caps (GST_VIDEO_FORMAT_NV12));
  gst_caps_append (caps,
      gst_nv_video_context_egl_new_template_caps (GST_VIDEO_FORMAT_I420));
  gst_caps_append (caps,
      gst_nv_video_context_egl_new_template_caps (GST_VIDEO_FORMAT_RGBA));
  for (i = n; i < n + 3; i++) {
    GstCapsFeatures *features = gst_caps_features_new ("memory:NVMM", NULL);
    gst_caps_set_features (caps, i, features);
  }
  gst_caps_replace (&ret->caps, caps);
  gst_caps_unref (caps);

  return ret;
}
