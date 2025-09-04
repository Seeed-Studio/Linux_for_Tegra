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

#ifndef __GST_NV_VIDEO_DISPLAY_H__
#define __GST_NV_VIDEO_DISPLAY_H__

#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/video.h>

#include "gstnvvideofwd.h"

G_BEGIN_DECLS

#define GST_TYPE_NV_VIDEO_DISPLAY \
    (gst_nv_video_display_get_type())
#define GST_NV_VIDEO_DISPLAY(obj)\
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_NV_VIDEO_DISPLAY, GstNvVideoDisplay))
#define GST_NV_VIDEO_DISPLAY_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_NV_VIDEO_DISPLAY, GstNvVideoDisplayClass))
#define GST_IS_NV_VIDEO_DISPLAY(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_NV_VIDEO_DISPLAY))
#define GST_IS_NV_VIDEO_DISPLAY_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_NV_VIDEO_DISPLAY))
#define GST_NV_VIDEO_DISPLAY_CAST(obj) \
    ((GstNvVideoDisplay*)(obj))
#define GST_NV_VIDEO_DISPLAY_GET_CLASS(o) \
    (G_TYPE_INSTANCE_GET_CLASS((o), GST_TYPE_NV_VIDEO_DISPLAY, GstNvVideoDisplayClass))

struct _GstNvVideoDisplayClass
{
  GstObjectClass parent_class;

  guintptr  (*get_handle)   (GstNvVideoDisplay * display);
};

typedef enum
{
  GST_NV_VIDEO_DISPLAY_TYPE_NONE = 0,
  GST_NV_VIDEO_DISPLAY_TYPE_X11 = (1 << 0),

  GST_NV_VIDEO_DISPLAY_TYPE_ANY = G_MAXUINT32
} GstNvVideoDisplayType;

struct _GstNvVideoDisplay
{
  GstObject parent;

  GstNvVideoDisplayType type;
};

GST_EXPORT
gboolean gst_nv_video_display_new (GstNvVideoDisplay ** display);
GST_EXPORT
gboolean gst_nv_video_display_create_context (GstNvVideoDisplay * display, GstNvVideoContext ** ptr_context);
GST_EXPORT
GstNvVideoDisplayType gst_nv_video_display_get_handle_type (GstNvVideoDisplay * display);
GST_EXPORT
GstNvVideoWindow *gst_nv_video_display_create_window (GstNvVideoDisplay * display);

GType gst_nv_video_display_get_type (void);

G_END_DECLS

#endif /* __GST_NV_VIDEO_DISPLAY_H__ */
