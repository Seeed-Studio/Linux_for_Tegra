/*
 * Copyright (c) 2017-2022, NVIDIA CORPORATION. All rights reserved.
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
#include <errno.h>
#include <unistd.h>
#include <dlfcn.h>
#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/video.h>
#include "gstnvdrmvideosink.h"
#include <stdio.h>
#include "util/drmutil.h"
#include <drm_fourcc.h>
#include <stdlib.h>
#include <string.h>
#include <nvbufsurface.h>

#ifdef IS_DESKTOP
#define DEFAULT_NVBUF_API_VERSION_NEW   TRUE
#else
#define DEFAULT_NVBUF_API_VERSION_NEW   FALSE
#endif

//
// Blocklinear parameters
//
#define NVRM_SURFACE_BLOCKLINEAR_GOB_HEIGHT     8
#define NVRM_SURFACE_DEFAULT_BLOCK_HEIGHT_LOG2  4

GST_DEBUG_CATEGORY_STATIC (gst_nv_drm_video_sink_debug);
#define GST_CAT_DEFAULT gst_nv_drm_video_sink_debug
#ifndef PACKAGE
#define PACKAGE "nvdrmvideosink"
#endif

/* Filter signals and args */
enum
{
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_CONN_ID,
  PROP_PLANE_ID,
  PROP_SET_MODE,
  PROP_OFFSET_X,
  PROP_OFFSET_Y,
  PROP_COLOR_RANGE
};

struct pflip_info
{
  guint vrefresh;
  guint refrate;
  struct drm_util_fb drm_fb[2];
  guint front_buf;
  guint crtc_id;
};

typedef enum {
  OUTPUT_COLOR_RANGE_FULL,
  OUTPUT_COLOR_RANGE_LIMITED,
  OUTPUT_COLOR_RANGE_DEFAULT
} ColorRange;

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw,"
        "width = (gint) [ 1 , MAX ],"
        "height = (gint) [ 1 , MAX ]"
        ";" "video/x-raw(memory:NVMM),"
        "width = (gint) [ 1 , MAX ]," "height = (gint) [ 1 , MAX ];")
    );

#define gst_nv_drm_video_sink_parent_class parent_class
G_DEFINE_TYPE (GstNvDrmVideoSink, gst_nv_drm_video_sink, GST_TYPE_VIDEO_SINK);

static void gst_nv_drm_video_sink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_nv_drm_video_sink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static GstFlowReturn gst_nv_drm_video_sink_show_frame (GstVideoSink *
    video_sink, GstBuffer * buf);
static void gst_nv_drm_video_sink_finalize (GObject * object);
static int set_format (GstNvDrmVideoSink * sink);
static int display (GstVideoSink * video_sink);
char *get_format (guint fmt);
int set_color_range (int fd, int crtc_id, uint64_t value);
ColorRange get_color_range (int color_range);

/* Calculation for BlockHeightLog2 is done similar to the calculation done
 * in NvRmSurfaceShrinkBlockDim API
 *
 * This function calculates the largest blockheight possible by shrinking
 * such that the image height is smaller than the proposed blocksize.
 */
static uint32_t
calculateBlockHeightLog2(uint32_t BlockDimLog2, uint32_t ImageDim, uint32_t GobDim)
{
  if (BlockDimLog2 > 0) {
    uint32_t proposedBlockSize = GobDim << (BlockDimLog2 - 1);
    while (proposedBlockSize >= ImageDim)
    {
      BlockDimLog2--;
      if (BlockDimLog2 == 0) {
        break;
      }
      proposedBlockSize /= 2;
    }
  }
  return BlockDimLog2;
}

static int
set_format (GstNvDrmVideoSink * sink)
{
  /* Check formats for NVMM */
  gint checkNVMM = -1;
  switch (sink->videoFormat) {

    case GST_VIDEO_FORMAT_BGRx:
      sink->drm_format = DRM_FORMAT_XRGB8888;
      break;
    case GST_VIDEO_FORMAT_RGBA:
      sink->drm_format = DRM_FORMAT_ABGR8888;
      break;
    case GST_VIDEO_FORMAT_NV12:
      sink->drm_format = DRM_FORMAT_NV12;
      break;
    case GST_VIDEO_FORMAT_I420:
      sink->drm_format = DRM_FORMAT_YUV420;
      break;
    case GST_VIDEO_FORMAT_Y444:
      sink->drm_format = DRM_FORMAT_YUV444;
      break;
    case GST_VIDEO_FORMAT_RGBx:
      sink->drm_format = DRM_FORMAT_XBGR8888;
      break;
    case GST_VIDEO_FORMAT_BGRA:
      sink->drm_format = DRM_FORMAT_ARGB8888;
      break;
    case GST_VIDEO_FORMAT_NV16:
      sink->drm_format = DRM_FORMAT_NV16;
      break;
    case GST_VIDEO_FORMAT_NV61:
      sink->drm_format = DRM_FORMAT_NV61;
      break;
    case GST_VIDEO_FORMAT_YV12:
      sink->drm_format = DRM_FORMAT_YVU420;
      break;
    case GST_VIDEO_FORMAT_UYVY:
      sink->drm_format = DRM_FORMAT_UYVY;
      break;
    case GST_VIDEO_FORMAT_YUY2:
      sink->drm_format = DRM_FORMAT_YUYV;
      break;
    case GST_VIDEO_FORMAT_NV21:
        sink->drm_format = DRM_FORMAT_NV21;
        checkNVMM = 1;
        break;
    case GST_VIDEO_FORMAT_NV24:
        sink->drm_format = DRM_FORMAT_NV24;
        break;
    case GST_VIDEO_FORMAT_Y42B:
        sink->drm_format = DRM_FORMAT_YUV422;
        checkNVMM = 1;
        break;
    default:
      GST_ERROR ("Video format not supported. \n");
      return 0;
  }

  /*
   * Check for formats that are supported only by NvBuffers,
   * and not software buffers
   */
  if (checkNVMM == 1) {
    if (sink->using_NVMM) {
      return 1;
    } else {
      return 0;
    }
  }

  return 1;
}

static int
display (GstVideoSink * video_sink)
{
  GstNvDrmVideoSink *sink = GST_NVDRMVIDEOSINK (video_sink);

  /* If sink wants to set mode */
  if (sink->set_mode) {
    /* Display NVMM buffer */
    if (sink->using_NVMM) {
      if (drmModePageFlip (sink->fd, sink->crtc_id,
              sink->buf_id[sink->frame_count], DRM_MODE_PAGE_FLIP_EVENT,
              NULL)) {
        g_print ("Failed to page flip \n");
        return 1;
      }
    } else {
      if (drmModePageFlip (sink->fd, sink->crtc_id,
              sink->fb[sink->frame_count].fb_id, DRM_MODE_PAGE_FLIP_EVENT,
              NULL)) {
        g_print ("Failed to page flip \n");
        return 1;
      }
    }

    drmEventContext evctx;

    /* Set up our event handler */
    memset(&evctx, 0, sizeof evctx);
    evctx.version = DRM_EVENT_CONTEXT_VERSION;
    evctx.vblank_handler = NULL;
    evctx.page_flip_handler = NULL;


    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    fd_set fds;
    FD_ZERO (&fds);
    FD_SET (sink->fd, &fds);

    int ret = select (sink->fd + 1, &fds, NULL, NULL, &timeout);

    if (ret > 0) {
      drmHandleEvent(sink->fd, &evctx);
    } else if(ret == 0) {
        g_print ("timeout reached before any flip call occurred\n"); 
    } else {
        g_print ("select failed with error message : %s\n",strerror(errno));
    }
  } else {
    guint crtc_id = sink->crtc_id;
    guint pl_id = sink->plane_id;
    guint nvheight = sink->height;
    guint nvwidth = sink->width;

    if (sink->using_NVMM) {
      if (drmModeSetPlane (sink->fd, pl_id, crtc_id,
              sink->buf_id[sink->frame_count], 0, sink->offset_x,
              sink->offset_y, nvwidth, nvheight, 0, 0, (nvwidth) << 16,
              (nvheight) << 16)) {
        g_print ("Failed to set plane \n");
        return 1;
      }
    } else {
      if (drmModeSetPlane (sink->fd, pl_id, crtc_id,
              sink->fb[sink->frame_count].fb_id, 0, sink->offset_x,
              sink->offset_y, nvwidth, nvheight, 0, 0, nvwidth << 16,
              nvheight << 16)) {
        g_print ("Failed to set plane \n");
        return 1;
      }
    }
  }
  return 0;
}

static gboolean
gst_nvdrmvideosink_propose_allocation (GstBaseSink * bsink, GstQuery * query)
{
  GstNvDrmVideoSink *sink = GST_NVDRMVIDEOSINK (bsink);

  if (sink->last_buf) {
    gst_buffer_unref (sink->last_buf);
    sink->last_buf = NULL;
    sink->is_drc_on = 1;
  }

  return TRUE;
}

static GstFlowReturn
gst_nv_drm_video_sink_show_frame (GstVideoSink * video_sink, GstBuffer * buf)
{
  GstFlowReturn res = GST_FLOW_OK;
  GstNvDrmVideoSink *sink = GST_NVDRMVIDEOSINK (video_sink);
  sink->frame_count ^= 1;
  GST_DEBUG ("New frame received \n");

  GstMemory *mem;
  GstMapInfo map = { NULL, (GstMapFlags) 0, NULL, 0, 0, };
  mem = gst_buffer_peek_memory (buf, 0);
  gst_memory_map (mem, &map, GST_MAP_READ);

  /* Check for type of buffer */
  if ((sink->using_NVMM) == 1) {

    GST_DEBUG_OBJECT (sink, "NVMM buffer processing \n");

    /* Processing frame and rendering using DRM */

    /* Plane information */
    guint nvhmem[NVBUF_MAX_PLANES] = { 0 };
    guint nvblocksize[NVBUF_MAX_PLANES] = { 0 };
    static guint handle[NVBUF_MAX_PLANES] = { 0 };
    static guint nvhandle = 0;
    static guint bo_handles[NVBUF_MAX_PLANES] = { 0 };
    static uint64_t modifiers[NVBUF_MAX_PLANES] = { 0 };
    static guint pitches[NVBUF_MAX_PLANES] = { 0 };
    static guint offsets[NVBUF_MAX_PLANES] = { 0 };
    guint width = 0, height = 0;
    gint num_planes = 0;
    gint ret = -1;
    NvBufSurface *in_surface = NULL;

    in_surface = (NvBufSurface *) map.data;
    NvBufSurfaceMemType memType = in_surface->memType;
    gst_memory_unmap (mem, &map);

    if (memType == NVBUF_MEM_DEFAULT || memType == NVBUF_MEM_SURFACE_ARRAY || memType == NVBUF_MEM_HANDLE) {
      num_planes = (int)in_surface->surfaceList[0].planeParams.num_planes;
      gint hm = 0;
      for (hm = 0; hm < num_planes; hm++) {
        nvhmem[hm] = in_surface->surfaceList[0].bufferDesc;
        nvblocksize[hm] = calculateBlockHeightLog2(NVRM_SURFACE_DEFAULT_BLOCK_HEIGHT_LOG2,
                              in_surface->surfaceList[0].planeParams.height[hm],
                              NVRM_SURFACE_BLOCKLINEAR_GOB_HEIGHT);

        ret = drmPrimeFDToHandle (sink->fd, nvhmem[hm], &nvhandle);
        if (ret < 0) {
          g_print ("drmPrimeFDToHandle call failed \n");
          return GST_FLOW_ERROR;
        } else {
          handle[hm] = nvhandle;
        }

        if (sink->is_tegra_drm) {
          if (in_surface->surfaceList[0].layout == NVBUF_LAYOUT_BLOCK_LINEAR) {
              //Set sector layout bit
              modifiers[hm] = DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(nvblocksize[hm]);
              modifiers[hm] |= 1 << 22;
          } else {
            modifiers[hm] = DRM_FORMAT_MOD_LINEAR;
          }
        } else if (sink->is_nvidia_drm) {
          // nvidia drm is used on T23x or later
          if (in_surface->surfaceList[0].layout == NVBUF_LAYOUT_BLOCK_LINEAR) {
            // The kindGen and block linear kind is as below.
            modifiers[hm] = DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 2, 0x06, nvblocksize[hm]);
          } else {
            modifiers[hm] = DRM_FORMAT_MOD_LINEAR;
          }
        } else {
          if (in_surface->surfaceList[0].layout == NVBUF_LAYOUT_BLOCK_LINEAR) {
            gint gset = gem_set_params (sink->fd, handle[hm], nvblocksize[hm]);
            if (gset < 0) {
              g_print ("Failed to set parameters of block linear data \n");
              return GST_FLOW_ERROR;
            }
          }
        }
      }

      for (hm = 0; hm < num_planes; hm++) {
        bo_handles[hm] = handle[hm];
        /* store previous handles */
        sink->drm_bo_handles[sink->frame_count][hm] = handle[hm];
        pitches[hm] = in_surface->surfaceList[0].planeParams.pitch[hm];
        offsets[hm] = in_surface->surfaceList[0].planeParams.offset[hm];
      }

      width = in_surface->surfaceList[0].planeParams.width[0];
      height = in_surface->surfaceList[0].planeParams.height[0];
    }

    /* Create fb */
    if (sink->is_tegra_drm || sink->is_nvidia_drm) {
      if (drmModeAddFB2WithModifiers (sink->fd, width, height,
              sink->drm_format, bo_handles, pitches, offsets,
              modifiers, &(sink->buf_id[sink->frame_count]),
              DRM_MODE_FB_MODIFIERS)) {
        g_print ("Failed to create frame buffer\n");
        return GST_FLOW_ERROR;
      }
    }
    else {
      if (drmModeAddFB2 (sink->fd, width, height,
              sink->drm_format, bo_handles, pitches, offsets,
              &(sink->buf_id[sink->frame_count]), 0)) {
        g_print ("Failed to create frame buffer\n");
        return GST_FLOW_ERROR;
      }
    }

    /* Display fb */
    if (display (video_sink)) {
      g_print ("Failed to display frame buffer\n");
      res = GST_FLOW_ERROR;
    }

    /* Release the last buffer held */
    if (sink->last_buf) {
      gst_buffer_unref (sink->last_buf);
      sink->last_buf = NULL;
    }

    /* Remove previous frame buffer to avoid overflow */
    if (sink->buf_id[sink->frame_count ^ 1]) {
      /* remove bo handles as well whose ref_count has become 2 */
      gint k = 0;
      for (k = 0; k < num_planes; k++) {
        drm_util_close_gem_bo (sink->fd,
                sink->drm_bo_handles[sink->frame_count ^ 1][k]);
      }
      if (drmModeRmFB (sink->fd, sink->buf_id[sink->frame_count ^ 1])) {
        g_print ("Cannot remove frame buffer \n");
        return FALSE;
      }
    }

    /* Save and hold the current buffer to prevent decoder to over write it */
    if (!sink->is_drc_on) {
      sink->last_buf = buf;
      gst_buffer_ref (sink->last_buf);
    }
  } else {

    GST_DEBUG_OBJECT (sink, "Software buffer processing \n");

    /* Processing frame and rendering using DRM */
    guint8 *inputBuf;
    inputBuf = map.data;
    guint sizeInput = (guint) map.size;
    gst_memory_unmap (mem, &map);

    /* Fill data from input buffer to drm frame buffer */
    if (sizeInput) {
      if (drm_util_fill_data (&(sink->fb[sink->frame_count]), inputBuf,
              sizeInput)) {
        GST_ERROR (" Cannot fill frame buffer \n");
        return GST_FLOW_ERROR;
      }
    } else {
      GST_ERROR (" Input buffer is empty \n");
      return GST_FLOW_ERROR;
    }

    /* display fb */
    if (display (video_sink)) {
      GST_ERROR ("Failed to display frame buffer\n");
      return GST_FLOW_ERROR;
    }
  }
  return res;
}

static GstCaps *
gst_nvdrmvideosink_get_caps (GstBaseSink * base_sink, GstCaps * filter)
{
  GstNvDrmVideoSink *sink = (GstNvDrmVideoSink *) (base_sink);
  GstCaps *tmp = NULL;
  GstCaps *result = NULL;

  GST_DEBUG_OBJECT (sink, "Received caps %" GST_PTR_FORMAT, filter);

  tmp = gst_pad_get_pad_template_caps (GST_BASE_SINK_PAD (base_sink));

  if (filter) {
    GST_DEBUG_OBJECT (base_sink,
        "Intersecting with filter caps %" GST_PTR_FORMAT, filter);

    result = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
  } else {
    result = tmp;
  }

  if (sink->outcaps) {
    tmp = result;
    result = gst_caps_intersect (tmp, sink->outcaps);
    gst_caps_unref (tmp);
  }

  GST_DEBUG_OBJECT (base_sink, "Returning caps: %" GST_PTR_FORMAT, result);

  return result;

}

static gboolean
gst_nvdrmvideosink_set_caps (GstBaseSink * base_sink, GstCaps * caps)
{
  gboolean ret = TRUE;

  GstNvDrmVideoSink *sink = (GstNvDrmVideoSink *) (base_sink);
  GstVideoInfo info;
  GstCapsFeatures *features;

  GST_DEBUG_OBJECT (sink, "Received caps %" GST_PTR_FORMAT, caps);

  /* Extract information from the source */
  if (!gst_video_info_from_caps (&info, caps))
    goto invalid_format;

  features = gst_caps_get_features (caps, 0);

  if (gst_caps_features_contains (features, "memory:NVMM")) {
    sink->using_NVMM = 1;
  } else {
    sink->using_NVMM = 0;
  }

  gint is_res_changed = 0;
  if ((sink->width != 0 && sink->height != 0) && (sink->width != info.width && sink->height != info.height)) {
    is_res_changed = 1;
  }

  if (is_res_changed) {
    sink->is_drc_on = 0;
  }

  sink->width = info.width;
  sink->height = info.height;
  sink->fps_n = info.fps_n;
  sink->fps_d = info.fps_d;
  sink->videoFormat = GST_VIDEO_FORMAT_INFO_FORMAT (info.finfo);

  /* Convert gst video format to drm format */
  gint fret = set_format (sink);

  if (!fret) {
    GST_ERROR ("Video format not supported. \n");
    return GST_FLOW_NOT_SUPPORTED;
  }

  gint m;
  /* create fb */
  if (!(sink->using_NVMM)) {
    for (m = 0; m < 2; m++) {
      if (!drm_util_create_dumb_fb (sink->fd,
              sink->width, sink->height, sink->drm_format, &(sink->fb[m]))) {
        g_print ("Cannot create frame buffer \n");
        return FALSE;
      }
    }
  }

  /* If user wants to set mode */
  if (sink->set_mode) {
    //Add color range property
    ColorRange output_color_range = get_color_range(sink->color_range);
    if (!set_color_range (sink->fd, sink->crtc_id, output_color_range)) {
      GST_ERROR ("unable to set color range property\n");
    }
    /* Store default ctrc props */
    sink->default_crtcProp = drmModeGetCrtc (sink->fd, sink->crtc_id);

    /* Check if incoming caps intersects with the caps created with modes */
    if (gst_caps_can_intersect (caps, sink->outcaps)) {
      gint m;
      drmModeModeInfoPtr mode = NULL;
      for (m = 0; m < sink->num_modes; m++) {
        mode = &(sink->conn_info)->modes[m];
        if ((info.width == mode->hdisplay) && (info.height == mode->vdisplay)) {
          sink->mode = mode;
          break;
        }
      }

      if (m == sink->num_modes) {
        GST_ERROR_OBJECT (sink, "Mode not found\n");
        return FALSE;
      }
    } else {
      GST_ERROR_OBJECT (sink, "Mode not supported by connector \n");
      return FALSE;
    }

    guint conn_id = sink->conn_id;
    guint fb_id = sink->fb[0].fb_id;

    if (sink->using_NVMM) {
        if (!drm_util_create_dumb_fb (sink->fd,
             sink->mode->hdisplay, sink->mode->vdisplay, sink->drm_format, &(sink->dumb))) {
            g_print ("Cannot create frame buffer \n");
            return FALSE;
        }
        fb_id = sink->dumb.fb_id;
    }

    /* Mode setting */
    if (drmModeSetCrtc (sink->fd,
        sink->crtc_id, fb_id, sink->is_nvidia_drm ? 0 : sink->offset_x,
        sink->is_nvidia_drm ? 0 : sink->offset_y, &conn_id, 1, sink->mode)) {
      GST_ERROR_OBJECT (sink, "Set crtc failed \n");
      return FALSE;
    } else {
      GST_DEBUG_OBJECT (sink, "Set crtc passed \n");
    }
    return ret;
  } else {

    /* TODO: For a different resolution video, aspect ratio can be calculated and handled later. */
    GST_DEBUG ("Do nothing \n");


    if (sink->is_nvidia_drm) {
        /* we need a mode to be set for upstream DRM driver */
        drmModeModeInfoPtr mode = NULL;
        int area = 0, current_area = 0;

        /* Find the mode with highest resolution */
        for (int i = 0; i < sink->conn_info->count_modes; i++) {
            drmModeModeInfoPtr current_mode = &(sink->conn_info)->modes[i];
            current_area = current_mode->hdisplay * current_mode->vdisplay;
            if (current_area > area) {
                mode = current_mode;
                area = current_area;
            }
        }

        guint conn_id = sink->conn_id;

        if (!drm_util_create_dumb_fb (sink->fd,
             mode->hdisplay, mode->vdisplay, sink->drm_format, &(sink->dumb))) {
            g_print ("Cannot create frame buffer \n");
            return FALSE;
        }

        if (drmModeSetCrtc (sink->fd,
            sink->crtc_id, sink->dumb.fb_id, 0, 0, &conn_id, 1, mode)) {
            GST_ERROR_OBJECT (sink, "Set crtc failed \n");
            return FALSE;
        }
    }

    /* Retrieve the default mode of connector */
    drmModeCrtcPtr crtcProp = drmModeGetCrtc (sink->fd, sink->crtc_id);
    sink->mode = &(crtcProp->mode);

    if (sink->color_range != 2) {
      g_print ("color_range can be set only with set-mode enabled. Please try with set-mode=1 property set\n");
    }

    drmModeFreeCrtc (crtcProp);

    return ret;
  }

invalid_format:
  GST_ERROR_OBJECT (sink, "caps invalid");

  return ret;
}

static void
add_format_list (GValue * list, char *s)
{
  GValue item = { 0, };
  g_value_init (&item, G_TYPE_STRING);
  g_value_set_string (&item, s);
  gst_value_list_append_value (list, &item);
  g_value_unset (&item);
}

static void
add_mode_resolution_list (GValue * list, gint i)
{
  GValue item = { 0, };
  g_value_init (&item, G_TYPE_INT);
  g_value_set_int (&item, i);
  gst_value_list_append_value (list, &item);
  g_value_unset (&item);
}

char *
get_format (guint fmt)
{
  switch (fmt) {

    case (DRM_FORMAT_ARGB1555):
      return ("ARGB");

    case (DRM_FORMAT_XRGB8888):
      return ("BGRx");

    case (DRM_FORMAT_XBGR8888):
      return ("RGBx");

    case (DRM_FORMAT_ARGB8888):
      return ("BGRA");

    case (DRM_FORMAT_ABGR8888):
      return ("RGBA");

    case (DRM_FORMAT_NV12):
      return ("NV12");

    case (DRM_FORMAT_NV21):
      return ("NV21");

    case (DRM_FORMAT_NV16):
      return ("NV16");

    case (DRM_FORMAT_NV61):
      return ("NV61");

    case (DRM_FORMAT_NV24):
      return ("NV24");

    case (DRM_FORMAT_YUV420):
      return ("I420");

    case (DRM_FORMAT_YVU420):
      return ("YV12");

    case (DRM_FORMAT_YUV422):
      return ("YUV422");

    case (DRM_FORMAT_YVU422):
      return ("YVU420");

    case (DRM_FORMAT_YUV444):
      return ("Y444");

    case (DRM_FORMAT_UYVY):
      return ("UYVY");

    case (DRM_FORMAT_YUYV):
      return ("YUY2");

    default:
      return NULL;
  }
}

ColorRange get_color_range (int color_range)
{
  switch(color_range) {
    case 0:
      return OUTPUT_COLOR_RANGE_FULL;
    case 1:
      return OUTPUT_COLOR_RANGE_LIMITED;
    default:
      return OUTPUT_COLOR_RANGE_DEFAULT;
  }
}

int set_color_range (int fd, int crtc_id, uint64_t value)
{
  if (value == OUTPUT_COLOR_RANGE_DEFAULT) {
    g_print ("color_range if supported, to be set to default by DRM\n");
    return 1;
  }
  drmModeObjectPropertiesPtr crtc_props;
  drmModePropertyRes *prop = NULL;
  crtc_props = drmModeObjectGetProperties (fd, crtc_id, DRM_MODE_OBJECT_CRTC);
  uint32_t i = 0;

  if (!crtc_props) {
    goto fail;
  }

  for (i = 0; i < crtc_props->count_props; i++) {
    prop = drmModeGetProperty(fd, crtc_props->props[i]);
    if (!prop) {
      continue;
    }
    if(!strncmp(prop->name, "OutputColorRange", DRM_PROP_NAME_LEN)) {
      break;
    }
    drmModeFreeProperty(prop);
  }

  if (i == crtc_props->count_props) {
    GST_ERROR ("Output color range not supported");
    goto fail;
  }

  drmModeFreeObjectProperties(crtc_props);

  if (drmModeObjectSetProperty (fd, crtc_id, DRM_MODE_OBJECT_CRTC, prop->prop_id, (uint64_t) value)) {
    GST_ERROR ("Failed to set property");
    goto fail;
  }

  if (prop) {
    drmModeFreeProperty(prop);
  }

  return 1;

fail:
  drmModeFreeObjectProperties (crtc_props);
  if (prop) {
    drmModeFreeProperty(prop);
  }
  return 0;
}


static gboolean
gst_nvdrmvideosink_start (GstBaseSink * base_sink)
{
  GstNvDrmVideoSink *sink = (GstNvDrmVideoSink *) (base_sink);

  sink->vtinfo.console_fd = -1;
  sink->vtinfo.active_vt = -1;
  /* Load libdrm.so and read in all resources; populate info */
  if (!drm_util_init (&sink->fd, &sink->conn_info, &sink->conn_id,
          &sink->crtc_id, &sink->plane_id, &sink->vtinfo, sink->do_vtswitch,
          &sink->is_nvidia_drm, &sink->is_tegra_drm)) {
    GST_ERROR ("drm_util_init failed\n");
    if (sink->do_vtswitch) {
      release_vt(&sink->vtinfo);
    }
    return FALSE;
  } else {
    GST_DEBUG ("drm_util_init passed\n");
  }

  sink->frame_count = 0;
  sink->num_modes = sink->conn_info->count_modes;
  drmModeModeInfoPtr mode = NULL;
  GstCaps *caps_fin = NULL, *copy1;

  drmModePlanePtr plane_info = drmModeGetPlane (sink->fd, sink->plane_id);

  if (!plane_info) {
    GST_ERROR ("Unable to get plane info\n");
    return 0;
  }

  /* Create new caps for available modes of connector
   * only if user wants to set mode
   */
  if (sink->set_mode) {
    caps_fin = gst_caps_new_empty ();
    GstCaps *caps_tmp = gst_caps_new_simple ("video/x-raw",
        "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1,
        NULL);

    GValue list = { 0, }, list_w = { 0, }, list_h = { 0, };
    g_value_init (&list, GST_TYPE_LIST);
    g_value_init (&list_w, GST_TYPE_LIST);
    g_value_init (&list_h, GST_TYPE_LIST);

    guint f = 0;
    for (f = 0; f < plane_info->count_formats; f++) {
      guint tmp_fmt = *(&plane_info->formats[f]);
      add_format_list (&list, get_format (tmp_fmt));
    }
    GstStructure *structure = gst_caps_get_structure (caps_tmp, 0);
    gst_structure_set_value (structure, "format", &list);

    gint m = 0;
    for (m = 0; m < sink->num_modes; m++) {
      mode = &(sink->conn_info)->modes[m];
      add_mode_resolution_list (&list_w, mode->hdisplay);
      add_mode_resolution_list (&list_h, mode->vdisplay);
    }
    gst_structure_set_value (structure, "width", &list_w);
    gst_structure_set_value (structure, "height", &list_h);

    gst_caps_append (caps_fin, caps_tmp);

    copy1 = gst_caps_copy (caps_fin);

    gint n = gst_caps_get_size (caps_fin);
    gint ic;
    for (ic = 0; ic < n; ic++) {
      GstCapsFeatures *tmp_f = gst_caps_features_new ("memory:NVMM", NULL);
      gst_caps_set_features (caps_fin, ic, tmp_f);
    }
    gst_caps_append (caps_fin, copy1);

    /* Save created caps */
    sink->outcaps = gst_caps_copy (caps_fin);

    /* Free resources */
    g_value_unset (&list);
    g_value_unset (&list_w);
    g_value_unset (&list_h);
  }
  /* Free resources */
  drmModeFreePlane (plane_info);
  if (caps_fin) {
    gst_caps_unref (caps_fin);
  }

  return TRUE;
}

static GstStateChangeReturn
gst_nvdrmvideosink_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstNvDrmVideoSink *sink = NULL;

  sink = GST_NVDRMVIDEOSINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (sink->using_NVMM) {
        if (sink->last_buf) {
          gst_buffer_unref(sink->last_buf);
          sink->last_buf = NULL;
        }

        const GstVideoFormatInfo *videoFormatInfo = gst_video_format_get_info(sink->videoFormat);
        guint k = 0;
        for (k = 0; k < videoFormatInfo->n_planes; k++) {
          drm_util_close_gem_bo (sink->fd,
                  sink->drm_bo_handles[sink->frame_count][k]);
        }
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return ret;
}

static gboolean
gst_nvdrmvideosink_stop (GstBaseSink * base_sink)
{
  GstNvDrmVideoSink *sink = (GstNvDrmVideoSink *) (base_sink);

  /* remove frame buffers to avoid overflow */
  if (sink->using_NVMM) {
    drmModeRmFB (sink->fd, sink->buf_id[sink->frame_count]);
    drmModeRmFB (sink->fd, sink->buf_id[sink->frame_count ^ 1]);
  } else {
    drmModeRmFB (sink->fd, sink->fb[sink->frame_count].fb_id);
    drmModeRmFB (sink->fd, sink->fb[sink->frame_count ^ 1].fb_id);
  }

  if (sink->dumb.fb_id != 0) {
      drm_util_destroy_dumb_fb(sink->fd, &(sink->dumb));
  }

  if (sink->outcaps) {
    gst_caps_unref (sink->outcaps);
  }
  /* Set NULL window on EOS */
  if (sink->set_mode && sink->default_crtcProp) {
    /* Restore mode to the default mode of the connector */
    sink->mode = &((sink->default_crtcProp)->mode);
    drmModeFreeCrtc (sink->default_crtcProp);

    guint conn_id = sink->conn_id;

    if (drmModeSetCrtc (sink->fd, sink->crtc_id, 0, 0, 0, &conn_id, 1, sink->mode)) {
      GST_ERROR_OBJECT (sink, "Set crtc to NULL failed\n");
      return FALSE;
    } else {
      GST_DEBUG_OBJECT (sink, "Set crtc to NULL passed \n");
    }
  } else {
    guint crtc_id = sink->crtc_id;
    guint pl_id = sink->plane_id;
    guint nvheight = sink->height;
    guint nvwidth = sink->width;

    if (drmModeSetPlane (sink->fd, pl_id, crtc_id,
            0, 0, 0, 0, nvwidth,
            nvheight, 0, 0, (nvwidth) << 16, (nvheight) << 16)) {
      GST_ERROR_OBJECT (sink, "Set plane to NULL failed\n");
      return FALSE;
    } else {
      GST_DEBUG_OBJECT (sink, "Set plane to NULL passed \n");
    }
  }

  drmModeFreeConnector (sink->conn_info);
  drmClose (sink->fd);

  if (sink->do_vtswitch) {
    release_vt(&sink->vtinfo);
  }

  return TRUE;
}

static gboolean
gst_nv_drm_video_sink_event (GstBaseSink * gst_base, GstEvent * event)
{
  GstNvDrmVideoSink *sink = GST_NVDRMVIDEOSINK (gst_base);
  gboolean ret = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      /* release last frame */
      if (sink->last_buf) {
        gst_buffer_unref (sink->last_buf);
        sink->last_buf = NULL;
      }
      break;
    default:
      break;
  }

  if (GST_BASE_SINK_CLASS (parent_class)->event) {
    ret = GST_BASE_SINK_CLASS (parent_class)->event (gst_base, event);
  } else {
    gst_event_unref (event);
  }

  return ret;
}

static void
gst_nv_drm_video_sink_class_init (GstNvDrmVideoSinkClass * klass)
{

  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseSinkClass *base_sink_class = GST_BASE_SINK_CLASS (klass);
  GstVideoSinkClass *videosink_class = GST_VIDEO_SINK_CLASS (klass);

  gobject_class->set_property = gst_nv_drm_video_sink_set_property;
  gobject_class->get_property = gst_nv_drm_video_sink_get_property;
  gobject_class->finalize = gst_nv_drm_video_sink_finalize;

  gstelement_class->change_state = gst_nvdrmvideosink_change_state;

  base_sink_class->set_caps = GST_DEBUG_FUNCPTR (gst_nvdrmvideosink_set_caps);
  base_sink_class->get_caps = GST_DEBUG_FUNCPTR (gst_nvdrmvideosink_get_caps);

  base_sink_class->start = GST_DEBUG_FUNCPTR (gst_nvdrmvideosink_start);
  base_sink_class->stop = GST_DEBUG_FUNCPTR (gst_nvdrmvideosink_stop);
  base_sink_class->event = GST_DEBUG_FUNCPTR (gst_nv_drm_video_sink_event);

  base_sink_class->propose_allocation = GST_DEBUG_FUNCPTR (gst_nvdrmvideosink_propose_allocation);

  videosink_class->show_frame =
      GST_DEBUG_FUNCPTR (gst_nv_drm_video_sink_show_frame);

  g_object_class_install_property (gobject_class, PROP_CONN_ID,
      g_param_spec_int ("conn_id", "CONN_ID",
          "Sets CONN ID.", 0, INT_MAX, INT_MAX,
          (GParamFlags) (G_PARAM_READWRITE)));

  g_object_class_install_property (gobject_class, PROP_PLANE_ID,
      g_param_spec_int ("plane_id", "PLANE_ID",
          "Sets PLANE ID", 0, INT_MAX, INT_MAX,
          (GParamFlags) (G_PARAM_READWRITE)));

  g_object_class_install_property (gobject_class, PROP_SET_MODE,
      g_param_spec_boolean ("set_mode", "SET_MODE",
          "Selects whether user wants to choose the default mode which is \n"
          "\t\t\talready set by connector (set_mode = 0) or wants to select the mode \n"
          "\t\t\tof the video stream (set_mode = 1). In the latter case, error is \n"
          "\t\t\tthrown when the input stream resolution does not match with \n"
          "\t\t\tthe supported modes of the connector.  ",
          FALSE, (GParamFlags) (G_PARAM_READWRITE)));

  g_object_class_install_property (gobject_class, PROP_OFFSET_X,
       g_param_spec_int ("offset_x", "OFFSET_X",
       "Sets offset x", 0, INT_MAX, INT_MAX,
       (GParamFlags) (G_PARAM_READWRITE)));

  g_object_class_install_property (gobject_class, PROP_OFFSET_Y,
       g_param_spec_int ("offset_y", "OFFSET_Y",
       "Sets offset y", 0, INT_MAX, INT_MAX,
       (GParamFlags) (G_PARAM_READWRITE)));

  g_object_class_install_property (gobject_class, PROP_COLOR_RANGE,
       g_param_spec_int ("color_range", "COLOR_RANGE",
       "Sets color range only when set-mode = 1\n"
       "\t\t\t color_range = 0 - FULL\n"
       "\t\t\t color_range = 1 - LIMITED\n"
       "\t\t\t color_range = 2 - DEFAULT\n",
       0, 2, 2,
       (GParamFlags) (G_PARAM_READWRITE)));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Nvidia Drm Video Sink",
      "Video Sink",
      "Nvidia Drm Video Sink", "Ashwini Munje <amunje@nvidia.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));
}

static void
gst_nv_drm_video_sink_init (GstNvDrmVideoSink * sink)
{
  sink->width = 0;
  sink->height = 0;
  sink->fps_n = 0;
  sink->fps_d = 0;
  sink->videoFormat = GST_VIDEO_FORMAT_UNKNOWN;
  sink->plane_id = INT_MAX;
  sink->conn_id = INT_MAX;
  sink->crtc_id = INT_MAX;
  sink->set_mode = FALSE;
  sink->offset_x = 0;
  sink->offset_y = 0;
  sink->color_range = 2;
  sink->using_NVMM = -1;
  sink->outcaps = NULL;
  sink->do_vtswitch = 0;
  return;
}

static void
gst_nv_drm_video_sink_set_property (GObject * object, guint prop_id,
const GValue * value, GParamSpec * pspec)
{

  GstNvDrmVideoSink *sink = GST_NVDRMVIDEOSINK (object);
  switch (prop_id) {
    case PROP_CONN_ID:
      sink->conn_id = g_value_get_int (value);
      GST_DEBUG_OBJECT (sink, "CONN ID : %d\n", sink->conn_id);
      break;
    case PROP_PLANE_ID:
      sink->plane_id = g_value_get_int (value);
      GST_DEBUG_OBJECT (sink, "PLANE ID : %d\n", sink->plane_id);
      break;
    case PROP_SET_MODE:
      sink->set_mode = g_value_get_boolean (value);
      GST_DEBUG_OBJECT (sink, "MODE_SET : %d\n", sink->set_mode);
      break;
    case PROP_OFFSET_X:
      sink->offset_x = g_value_get_int (value);
      GST_DEBUG_OBJECT (sink, "OFFSET_X : %d\n", sink->offset_x);
      break;
    case PROP_OFFSET_Y:
      sink->offset_y = g_value_get_int (value);
      GST_DEBUG_OBJECT (sink, "OFFSET_Y : %d\n", sink->offset_y);
      break;
    case PROP_COLOR_RANGE:
      sink->color_range = g_value_get_int (value);
      sink->do_vtswitch = 1;
      GST_DEBUG_OBJECT (sink, "COLOR_RANGE : %d\n", sink->color_range);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  return;
}

static void
gst_nv_drm_video_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstNvDrmVideoSink *sink = GST_NVDRMVIDEOSINK (object);

  switch (prop_id) {

    case PROP_CONN_ID:
      g_value_set_int (value, sink->conn_id);
      break;
    case PROP_PLANE_ID:
      g_value_set_int (value, sink->plane_id);
      break;
    case PROP_SET_MODE:
      g_value_set_boolean (value, sink->set_mode);
      break;
    case PROP_OFFSET_X:
      g_value_set_int (value, sink->offset_x);
      break;
    case PROP_OFFSET_Y:
      g_value_set_int (value, sink->offset_y);
      break;
    case PROP_COLOR_RANGE:
      g_value_set_int (value, sink->color_range);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_nv_drm_video_sink_finalize (GObject * object)
{
  G_OBJECT_CLASS (gst_nv_drm_video_sink_parent_class)->finalize (object);
}

static gboolean
nvdrmvideosink_init (GstPlugin * nvdrmvideosink)
{
  GST_DEBUG_CATEGORY_INIT (gst_nv_drm_video_sink_debug, "nvdrmvideosink",
      0, "Template nvdrmvideosink");

  return gst_element_register (nvdrmvideosink, "nvdrmvideosink",
      GST_RANK_NONE, GST_TYPE_NVDRMVIDEOSINK);
}

/*
 * TODO: Fix bug 200495960 that merges drm code into
 * nvvideosink plugin code and then update license
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    nvdrmvideosink,
    "nvidia Drm Video Sink Component",
    nvdrmvideosink_init,
    "0.0.1", "Proprietary", "NvDrmVideoSink", "http://nvidia.com/")
