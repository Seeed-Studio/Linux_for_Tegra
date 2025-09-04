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

#ifndef __GST_NV_VIDEO_FWD_H__
#define __GST_NV_VIDEO_FWD_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstNvVideoDisplay GstNvVideoDisplay;
typedef struct _GstNvVideoDisplayClass GstNvVideoDisplayClass;

typedef struct _GstNvVideoWindow GstNvVideoWindow;
typedef struct _GstNvVideoWindowClass GstNvVideoWindowClass;

typedef struct _GstNvVideoContext GstNvVideoContext;
typedef struct _GstNvVideoContextClass GstNvVideoContextClass;
typedef struct _GstNvVideoContextPrivate GstNvVideoContextPrivate;

typedef struct _GstNvVideoRenderer GstNvVideoRenderer;
typedef struct _GstNvVideoRendererClass GstNvVideoRendererClass;

G_END_DECLS

#endif /* __GST_NV_VIDEO_FWD_H__ */
