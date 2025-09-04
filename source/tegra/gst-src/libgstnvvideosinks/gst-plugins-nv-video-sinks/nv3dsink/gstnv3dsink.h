/*
 * Copyright (c) 2018-2022, NVIDIA CORPORATION.  All rights reserved.
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

#ifndef __GST_NV3DSINK_H__
#define __GST_NV3DSINK_H__

#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/video.h>

#include "gstnvvideofwd.h"

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (gst_debug_nv3dsink);

#define GST_TYPE_NV3DSINK \
        (gst_nv3dsink_get_type())
#define GST_NV3DSINK(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_NV3DSINK, GstNv3dSink))
#define GST_NV3DSINK_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_NV3DSINK, GstNv3dSinkClass))
#define GST_IS_NV3DSINK(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_NV3DSINK))
#define GST_IS_NV3DSINK_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_NV3DSINK))

typedef struct _GstNv3dSink GstNv3dSink;
typedef struct _GstNv3dSinkClass GstNv3dSinkClass;

struct _GstNv3dSink
{
  GstVideoSink parent;

  GstNvVideoDisplay *display;
  GstNvVideoContext *context;
  GstNvVideoWindow *window;
  gint window_x;
  gint window_y;
  gint window_width;
  gint window_height;

  GMutex win_handle_lock;

  GstCaps *configured_caps;
};

struct _GstNv3dSinkClass
{
  GstVideoSinkClass parent_class;
};

GType gst_nv3dsink_get_type (void);

G_END_DECLS

#endif /* __GST_NV3DSINK_H__ */
