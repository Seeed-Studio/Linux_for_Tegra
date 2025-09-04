/*
 * Copyright (c) 2014-2020, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __GST_NVTEE_H__
#define __GST_NVTEE_H__

#include <gst/gst.h>

G_BEGIN_DECLS
#define GST_TYPE_NVTEE \
  (gst_nvtee_get_type())
#define GST_NVTEE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NVTEE,GstNvTee))
#define GST_NVTEE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_NVTEE,GstNvTeeClass))
#define GST_IS_NVTEE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_NVTEE))
#define GST_IS_NVTEE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_NVTEE))
#define GST_NVTEE_CAST(obj) ((GstNvTee*) obj)
typedef struct _GstNvTee GstNvTee;
typedef struct _GstNvTeeClass GstNvTeeClass;

typedef enum
{
  GST_NVCAM_MODE_IMAGE = 1,
  GST_NVCAM_MODE_VIDEO = 2,
} GstNvCamMode;

/**
 * GstNvTee:
 *
 * Opaque #GstNvTee data structure.
 */
struct _GstNvTee
{
  GstElement element;

  GstPad *sinkpad;
  GstPad *pre_pad;
  GstPad *img_pad;
  GstPad *vid_pad;
  GstPad *vsnap_pad;

  GstNvCamMode mode;
  gboolean processing;
  gboolean has_pending_segment;
  gboolean do_vsnap;
  GQuark gst_buffer_nvtee_usecase_quark;
};

struct _GstNvTeeClass
{
  GstElementClass parent_class;

  void (*start_capture) (GstNvTee *);
  void (*stop_capture) (GstNvTee *);
  void (*take_vsnap) (GstNvTee *);
};

G_GNUC_INTERNAL GType gst_nvtee_get_type (void);

G_END_DECLS
#endif
