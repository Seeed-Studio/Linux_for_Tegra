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

#include <gst/gst.h>

#if NV_VIDEO_SINKS_HAS_NV3DSINK
#include "nv3dsink/gstnv3dsink.h"
#endif

#if NV_VIDEO_SINKS_HAS_X11
#include <X11/Xlib.h>
#endif

GST_DEBUG_CATEGORY_STATIC (gst_nvvideosinks_debug);
#define GST_CAT_DEFAULT gst_nvvideosinks_debug

static gboolean
plugin_init (GstPlugin * plugin)
{
#if NV_VIDEO_SINKS_HAS_X11
  XInitThreads ();
#endif

  /* debug category for fltering log messages */
  GST_DEBUG_CATEGORY_INIT (gst_nvvideosinks_debug, "nvvideosinks", 0,
      "Nvidia video sinks");

#if NV_VIDEO_SINKS_HAS_NV3DSINK
  if (!gst_element_register (plugin, "nv3dsink", GST_RANK_SECONDARY,
          GST_TYPE_NV3DSINK)) {
    return FALSE;
  }
#endif

  return TRUE;
}

/* PACKAGE is usually set by autotools but we are not using autotools
 * to compile this code, so set it ourselves. GST_PLUGIN_DEFINE needs
 * PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "gst-plugins-nv-video-sinks"
#endif

/* gstreamer looks for this structure to register plugins */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    nvvideosinks,
    "Nvidia Video Sink Plugins",
    plugin_init, "0.0.1", "Proprietary", "Nvidia Video Sink Plugins",
    "http://nvidia.com/")
