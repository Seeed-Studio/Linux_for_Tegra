/**
 * Copyright (c) 2018, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#ifndef __GST_NV_VIDEO_WINDOW_X11_H__
#define __GST_NV_VIDEO_WINDOW_X11_H__

#include "window.h"

G_BEGIN_DECLS

#define GST_TYPE_NV_VIDEO_WINDOW_X11 \
    (gst_nv_video_window_x11_get_type())
#define GST_NV_VIDEO_WINDOW_X11(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_NV_VIDEO_WINDOW_X11, GstNvVideoWindowX11))
#define GST_NV_VIDEO_WINDOW_X11_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_NV_VIDEO_WINDOW_X11, GstNvVideoWindowX11Class))
#define GST_IS_NV_VIDEO_WINDOW_X11(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_NV_VIDEO_WINDOW_X11))
#define GST_IS_NV_VIDEO_WINDOW_X11_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_NV_VIDEO_WINDOW_X11))
#define GST_NV_VIDEO_WINDOW_X11_CAST(obj) \
    ((GstNvVideoWindowX11*)(obj))

typedef struct _GstNvVideoWindowX11 GstNvVideoWindowX11;
typedef struct _GstNvVideoWindowX11Class GstNvVideoWindowX11Class;

struct _GstNvVideoWindowX11
{
  GstNvVideoWindow parent;

  guintptr handle;
  gboolean internal_window;
};

struct _GstNvVideoWindowX11Class
{
  GstNvVideoWindowClass parent_class;
};

GST_EXPORT
GstNvVideoWindowX11 *gst_nv_video_window_x11_new (const gchar * name);

GType gst_nv_video_window_x11_get_type (void);

G_END_DECLS

#endif /* __GST_NV_VIDEO_WINDOW_X11_H__ */
