/*
 * Copyright (c) 2017-2020, NVIDIA CORPORATION. All rights reserved.
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

#ifndef _DRMUTIL_H
#define _DRMUTIL_H

#include "vt_switch.h"

#include "xf86drm.h"
#include "xf86drmMode.h"

struct drm_util_bo
{
  uint32_t bo_handle;
  int width;
  int height;
  int pitch;
  uint8_t *data;
  size_t size;
};

struct drm_util_fb
{
  uint32_t fb_id;
  int width;
  int height;
  int format;
  struct drm_util_bo bo[4];
  int num_buffers;
};

// Fill drm dumb buffer with a test pattern
int drm_util_fill_data (struct drm_util_fb *util_fb, uint8_t * p,
    uint32_t sizeInput);

int drm_util_init (int *fd, drmModeConnector **conn_info, int *conn_id, int *crtc_id,
                   int *plane_id, struct vt_info *vtinfo, int do_vtswitch,
                   int *is_nvidia_drm, int *is_tegra_drm);

// Returns a DRM buffer_object handle allocated of size (w,h)
int drm_util_create_dumb_bo (int fd, int w, int h,
    int bpp, struct drm_util_bo *util_bo);

uint8_t *drm_util_mmap_dumb_bo (int fd, __u32 handle, __u64 size);

// Set gem buffer parameters
int gem_set_params (int fd, uint32_t nvhandle, uint32_t nvblocksize);

// Returns a DRM framebuffer ID allocated of size (w,h)
int drm_util_create_dumb_fb (int fd, int w, int h,
                             int drm_format, struct drm_util_fb *util_fb);

int count_format_types(void);

// Close GEM buffers
int drm_util_close_gem_bo (int fd, uint32_t bo_handle);

// Destroy an FB and its BOs
int drm_util_destroy_dumb_bo (int fd, struct drm_util_bo *util_bo);
int drm_util_destroy_dumb_fb (int fd, struct drm_util_fb *util_fb);
#endif
