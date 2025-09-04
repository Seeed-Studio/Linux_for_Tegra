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

#ifndef __GST_NVDRMVIDEOSINK_H__
#define __GST_NVDRMVIDEOSINK_H__

#include <gst/gst.h>
#include "drmutil.h"

#include "vt_switch.h"

G_BEGIN_DECLS
/* #defines don't like whitespacey bits */
#define GST_TYPE_NVDRMVIDEOSINK \
  (gst_nv_drm_video_sink_get_type())
#define GST_NVDRMVIDEOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NVDRMVIDEOSINK,GstNvDrmVideoSink))
#define GST_NVDRMVIDEOSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_NVDRMVIDEOSINK,GstNvDrmVideoSinkClass))
#define GST_IS_NVDRMVIDEOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_NVDRMVIDEOSINK))
#define GST_IS_NVDRMVIDEOSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_NVDRMVIDEOSINK))
typedef struct _GstNvDrmVideoSink GstNvDrmVideoSink;
typedef struct _GstNvDrmVideoSinkClass GstNvDrmVideoSinkClass;

struct _GstNvDrmVideoSink
{
  GstVideoSink parent;
  GstPad *sinkpad;
  GstCaps *outcaps;
  gint width;
  gint height;
  gint fps_n;
  gint fps_d;
  GstVideoFormat videoFormat;
  struct drm_util_fb fb[2];
  struct drm_util_fb dumb;
  gint conn_id;
  gint crtc_id;
  gint plane_id;
  gint offset_x;
  gint offset_y;
  gint color_range;
  guint buf_id[2];
  gint using_NVMM;
  gint frame_count;
  gint fd;
  gint is_drc_on;
  gboolean set_mode;
  drmModeModeInfoPtr mode;
  drmModeCrtcPtr default_crtcProp;
  gint num_modes;
  gint drm_format;
  GstBuffer *last_buf;
  drmModeConnector *conn_info;
  gint drm_bo_handles[2][4];
  struct vt_info vtinfo;
  gint do_vtswitch;
  gboolean nvbuf_api_version_new;
  gint is_nvidia_drm;
  gint is_tegra_drm;
};

struct _GstNvDrmVideoSinkClass
{
  GstVideoSinkClass parent_class;
};

GType gst_nv_drm_video_sink_get_type (void);

G_END_DECLS
#endif /* __GST_NVDRMVIDEOSINK_H__ */
