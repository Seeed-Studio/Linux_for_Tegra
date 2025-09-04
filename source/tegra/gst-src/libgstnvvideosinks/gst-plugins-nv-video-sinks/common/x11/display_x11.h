/*
 * Copyright (c) 2018, NVIDIA CORPORATION.  All rights reserved.
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

#ifndef __GST_NV_VIDEO_DISPLAY_X11_H__
#define __GST_NV_VIDEO_DISPLAY_X11_H__

#include <X11/Xlib.h>

#include "display.h"

G_BEGIN_DECLS

#define GST_TYPE_NV_VIDEO_DISPLAY_X11 \
    (gst_nv_video_display_x11_get_type())
#define GST_NV_VIDEO_DISPLAY_X11(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_NV_VIDEO_DISPLAY_X11, GstNvVideoDisplayX11))
#define GST_NV_VIDEO_DISPLAY_X11_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_NV_VIDEO_DISPLAY_X11, GstNvVideoDisplayX11Class))
#define GST_IS_NV_VIDEO_DISPLAY_X11(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_NV_VIDEO_DISPLAY_X11))
#define GST_IS_NV_VIDEO_DISPLAY_X11_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_NV_VIDEO_DISPLAY_X11))
#define GST_NV_VIDEO_DISPLAY_X11_CAST(obj) \
    ((GstNvVideoDisplayX11*)(obj))

typedef struct _GstNvVideoDisplayX11 GstNvVideoDisplayX11;
typedef struct _GstNvVideoDisplayX11Class GstNvVideoDisplayX11Class;

struct _GstNvVideoDisplayX11
{
  GstNvVideoDisplay parent;

  Display *dpy;
};

struct _GstNvVideoDisplayX11Class
{
  GstNvVideoDisplayClass parent_class;
};

GST_EXPORT
GstNvVideoDisplayX11 * gst_nv_video_display_x11_new (const gchar * name);

GType gst_nv_video_display_x11_get_type (void);

G_END_DECLS

#endif /* __GST_NV_VIDEO_DISPLAY_X11_H__ */
