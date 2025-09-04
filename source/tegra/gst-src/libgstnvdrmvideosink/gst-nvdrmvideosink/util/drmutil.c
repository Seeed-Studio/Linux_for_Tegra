/*
 * Copyright (c) 2018-2021, NVIDIA CORPORATION. All rights reserved.
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

#include "drmutil.h"
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <drm_fourcc.h>
#include <unistd.h>
#include <drm_mode.h>
#include <tegra_drm.h>
#include <gst/gst.h>

static const struct util_format
{
  uint32_t drm_format;
  int num_buffers;
  struct
  {
    int w;                      // width divisor from overall fb_width (luma size)
    int h;                      // height divisor from overall fb_height (luma size)
    int bpp;
  } buffers[3];
} util_formats[] = {
  // drm fourcc type     #buffers  w1 h1 bpp1   w2 h2 bpp2  w3 h3 bpp3
  // drm fourcc type     #buffers  w1 h1 bpp1   w2 h2 bpp2  w3 h3 bpp3
  {
    DRM_FORMAT_ARGB8888, 1, { {
    1, 1, 32}, {
    0, 0, 0}, {
    0, 0, 0}}}, {
    DRM_FORMAT_ABGR8888,  1, { {
    1, 1, 32},  {
    0, 0, 0},  {
    0, 0, 0}}}, {
    DRM_FORMAT_NV12, 2, { {
    1, 1, 8}, {
    2, 2, 16}, {
    0, 0, 0}}}, {
    DRM_FORMAT_YUV420, 3, { {
    1, 1, 8}, {
    2, 2, 8}, {
    2, 2, 8}}} , {
    DRM_FORMAT_XRGB8888, 1, { {
    1, 1, 32}, {
    0, 0, 0}, {
    0, 0, 0}}} , {
    DRM_FORMAT_UYVY, 1, { {
    1, 1, 16}, {
    0, 0, 0}, {
    0, 0, 0}}}, {
    DRM_FORMAT_YUYV, 1, { {
    1, 1, 16}, {
    0, 0, 0}, {
    0, 0, 0}}} , {
    DRM_FORMAT_XBGR8888, 1, { {
    1, 1, 32}, {
    0, 0, 0}, {
    0, 0, 0}}} , {
    DRM_FORMAT_NV16, 2, { {
    1, 1, 8}, {
    2, 1, 16}, {
    0, 0, 0}}}, {
    DRM_FORMAT_NV61, 2, { {
    1, 1, 8}, {
    2, 1, 16}, {
    0, 0, 0}}}, {
    DRM_FORMAT_NV24, 2, { {
    1, 1, 8}, {
    1, 1, 16}, {
    0, 0, 0}}}, {
    DRM_FORMAT_YVU420, 3, { {
    1, 1, 8}, {
    2, 2, 8}, {
    2, 2, 8}}}, {
    DRM_FORMAT_YUV444, 3, { {
    1, 1, 8}, {
    1, 1, 8}, {
    1, 1, 8}}}
};

// This routine returns fail if no physical connector available
static int
drm_util_mode_get_resources(int fd, drmModeRes **res_info)
{
  if (res_info == NULL)
    return -1;

  // Obtain DRM-KMS resources
  *res_info = drmModeGetResources (fd);
  if (*res_info == NULL) {
      return -1;
  } else if ((*res_info)->count_connectors < 1) {
      drmModeFreeResources (*res_info);
      *res_info = NULL;
      return -1;
  }

  return 0;
}

static int
drm_util_find_device(drmModeRes **res_info)
{
  DIR *dir;
  struct dirent *dent;
  int fd = -1;

  dir = opendir("/dev/dri");
  if (!dir)
    goto no_dri_device;

  while ((dent = readdir(dir))) {
    char path[265];
    fd = -1;

    if (strncmp(dent->d_name, "card", 4))
      continue;

    snprintf(path, sizeof(path), "/dev/dri/%s", dent->d_name);

    fd = open(path, O_RDWR|O_CLOEXEC);
    if (fd < 0)
      continue;

    if (drm_util_mode_get_resources(fd, res_info) < 0) {
      close(fd);
      continue;
    }

    closedir(dir);
    return fd;
  }

  closedir(dir);

no_dri_device:
  fd = drmOpen("drm-nvdc", NULL);
  if (fd >= 0) {
    if (drm_util_mode_get_resources(fd, res_info) < 0) {
      close(fd);
      return -1;
    }
  }
  return fd;
}

int
drm_util_init (int *fd, drmModeConnector ** conn_info_s, int *conn_id_s,
    int *crtc_id_s, int *plane_id_s, struct vt_info *vtinfo, gint do_vtswitch,
    gint *is_nvidia_drm, gint *is_tegra_drm)
{
  int conn_index = *conn_id_s;
  int plane_index = *plane_id_s;
  int crtc_index = *crtc_id_s;
  uint32_t conn_id = 0;
  uint32_t enc_id;
  uint32_t crtc_id;
  drmModeRes *res_info = NULL;
  drmModePlaneRes *plane_res_info = NULL;
  drmModeEncoder *enc_info = NULL;
  drmModeCrtc *crtc_info = NULL;
  drmModePlane *plane_info = NULL;
  drmVersion *version = NULL;
  int is;
  int ret = 0;

  if (do_vtswitch) {
    if (!acquire_vt(vtinfo)) {
      g_print ("Failed to acquire vt\n");
      return 0;
    }
  }

  // Open the DRM device.
  *fd = drm_util_find_device(&res_info);
  if (*fd < 0) {
      g_print ("Could not open DRM failed \n");
      return 0;
  }

  version = drmGetVersion(*fd);
  if (version == NULL) {
    g_print("Failed to get drm version\n");
    drmClose (*fd);
    goto free_resources;
  }
  *is_nvidia_drm = 0;
  if (!strcmp(version->name, "nvidia-drm")) {
      *is_nvidia_drm = 1;
  }
  *is_tegra_drm = 0;
  if (!strcmp(version->name, "tegra")) {
      *is_tegra_drm = 1;
  }
  drmFreeVersion(version);

  if (drmSetClientCap (*fd, DRM_CLIENT_CAP_ATOMIC, 1)) {
      g_print ("Failed to set atomic cap \n");
      goto free_resources;
  }

  if (drmSetClientCap (*fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1)) {
    g_print ("Failed to set universal planes \n");
    goto free_resources;
  }

  // If user has not mentioned any conn_id
  if (conn_index == INT_MAX) {
    // Find a valid/connected connector
    for (conn_index = 0; conn_index < res_info->count_connectors ; conn_index++) {
        conn_id = res_info->connectors[conn_index];
        *conn_info_s = drmModeGetConnector(*fd, conn_id);
        if ((*conn_info_s) && (*conn_info_s)->connection == DRM_MODE_CONNECTED) {
            *conn_id_s = conn_id;
            break;
        }
        drmModeFreeConnector(*conn_info_s);
    }
  } else if (conn_index < res_info->count_connectors && conn_index >=0){
      conn_id = res_info->connectors[conn_index];
      *conn_id_s = conn_id;
      *conn_info_s = drmModeGetConnector (*fd, conn_id);
  }
  // Check bound
  if ((conn_index >= res_info->count_connectors) || (conn_index < 0)) {
    g_print ("Invalid connector id \n");
    goto free_resources;
  }

  if (!(*conn_info_s)) {
    g_print ("Unable to obtain info for connector id(%d) \n", conn_id);
    goto free_resources;
  }
  // Set Encoder Info
  enc_id = (*conn_info_s)->encoder_id;
  enc_info = drmModeGetEncoder (*fd, enc_id);

  if (!enc_info) {
    // If connector does not have a connected encoder use first one
    enc_id = (*conn_info_s)->encoders[0];
    enc_info = drmModeGetEncoder (*fd, enc_id);

    if (!enc_info) {
      g_print ("Unable to find suitable encoder \n");
      goto free_resources;
    }
  }
  // Set CRTC info
  crtc_id = enc_info->crtc_id;
  crtc_info = drmModeGetCrtc (*fd, crtc_id);

  // If the encoder has no connected CRTC, use the first one supported
  if (!crtc_info && enc_info->possible_crtcs) {
    crtc_id = res_info->crtcs[ffs (enc_info->possible_crtcs) - 1];
    crtc_info = drmModeGetCrtc (*fd, crtc_id);
  }

  *crtc_id_s = crtc_id;
  drmModeFreeEncoder (enc_info);

  if (!crtc_info) {
    g_print ("Unable to find usable crtc for encoder \n");
    goto free_resources;
  }
  // Identify the index of our chosen crtc from the resource list.
  // Index is needed to match planes with crtcs
  crtc_index = -1;
  for (is = 0; is < res_info->count_crtcs; is++) {
    if (res_info->crtcs[is] == crtc_id) {
      crtc_index = is;
      break;
    }
  }
  if (crtc_index == -1) {
    printf ("Unable to locate crtc_id=%d in resource list\n", crtc_id);
    goto free_resources;
  }
  // Set Plane Info
  plane_res_info = drmModeGetPlaneResources (*fd);
  int plane_count = plane_res_info->count_planes;
  if (!plane_res_info) {
    g_print ("Unable to get plane resource info \n");
    goto free_resources;
  }
  // If user has not mentioned any plane_id
  if (plane_index == INT_MAX) {
    plane_index = 0;
  }
  // Check bound
  if ((plane_index >= plane_count) || (plane_index < 0)) {
    g_print ("Invalid plane id \n");
    goto free_resources;
  }

  int i;
  for (i = 0; i < plane_count; i++) {
    int plane_id = plane_res_info->planes[i];
    plane_info = drmModeGetPlane (*fd, plane_id);
    if (!plane_info) {
      printf ("Unable to get plane info\n");
      return 0;
    }
    if (plane_info->possible_crtcs & (1 << crtc_index)) {
      if (plane_index == 0) {
        *plane_id_s = plane_id;
        break;
      } else {
        plane_index--;
      }
    }
    drmModeFreePlane (plane_info);
  }

  if (plane_index != 0) {
    goto free_resources;
  }

  //drm_util_init success
  ret = 1;

// Free the resource information structures
free_resources:
  if (plane_info)
    drmModeFreePlane (plane_info);

  if (plane_res_info)
    drmModeFreePlaneResources (plane_res_info);

  if (crtc_info)
    drmModeFreeCrtc (crtc_info);

  if (res_info)
    drmModeFreeResources (res_info);

  if (!ret){
    if (*conn_info_s) {
      drmModeFreeConnector (*conn_info_s);
    }
    drmClose (*fd);
  }

  return ret;
}

static int
get_format_info (uint32_t drm_format, struct util_format *uf)
{
  int i;
  int fsize = sizeof (util_formats) / sizeof (util_formats[0]);
  for (i = 0; i < fsize; i++) {
    if (util_formats[i].drm_format == drm_format) {
      *uf = util_formats[i];
      return 1;
    }
  }
  return 0;
}

uint8_t *
drm_util_mmap_dumb_bo (int fd, __u32 handle, __u64 size)
{
  struct drm_mode_map_dumb mreq;
  uint8_t *map = NULL;
  drmVersion *version;

  /* prepare buffer for memory mapping */
  memset (&mreq, 0, sizeof (mreq));
  mreq.handle = handle;
  if (drmIoctl (fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) != 0) {
    g_print ("cannot map dumb buffer\n");
    return NULL;
  }

  version = drmGetVersion(fd);
  if (version == NULL) {
    g_print("Failed to get drm version\n");
    return NULL;
  }

  if (!strcmp(version->name, "nvidia-drm") ||
      !strcmp(version->name, "tegra") ||
      !strcmp(version->name, "tegra-udrm")) {
    map = (uint8_t*)mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                         mreq.offset);

    if (map == MAP_FAILED) {
      g_print("cannot mmap dumb buffer\n");
      drmFreeVersion(version);
      return NULL;
    }
  } else {
    map = (uint8_t *) (mreq.offset);
  }

  drmFreeVersion(version);

  return map;
}

// Create a buffer object of the requested size.
// Return the bo handle and the mapping
int
drm_util_create_dumb_bo (int fd, int width, int height,
    int bpp, struct drm_util_bo *util_bo)
{
  struct drm_mode_create_dumb creq;
  struct drm_mode_destroy_dumb dreq;
  int ret;
  uint8_t *map = NULL;

  /* create dumb buffer */
  memset (&creq, 0, sizeof (creq));
  creq.width = width;
  creq.height = height;
  creq.bpp = bpp;
  ret = drmIoctl (fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
  if (ret < 0) {
    g_print ("cannot create dumb buffer\n");
    return 0;
  }

  map = drm_util_mmap_dumb_bo (fd, creq.handle, creq.size);
  if (map == NULL) {
    goto err_destroy;
  }

  /* clear the buffer object */
  memset (map, 0x00, creq.size);
  util_bo->bo_handle = creq.handle;
  util_bo->width = width;
  util_bo->height = height;
  util_bo->pitch = creq.pitch;
  util_bo->data = map;
  util_bo->size = creq.size;

  return 1;

err_destroy:
  memset (&dreq, 0, sizeof (dreq));
  dreq.handle = creq.handle;
  drmIoctl (fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);

  return 0;
}

int
drm_util_fill_data (struct drm_util_fb *util_fb, uint8_t * p,
    uint32_t sizeInput)
{
  int i, j;
  int w = util_fb->width;
  int h = util_fb->height;
  int fmt = util_fb->format;

  struct util_format uf;
  if (!get_format_info (util_fb->format, &uf)) {
    GST_ERROR ("DRM test helper library can't draw in a FB of type %x\n",
        util_fb->format);
    return 1;
  }

  switch (fmt) {
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_ARGB8888:
    case DRM_FORMAT_XRGB8888:
    case DRM_FORMAT_XBGR8888:
      memcpy (util_fb->bo[0].data, p, sizeInput);
      break;
    case DRM_FORMAT_NV16:
    case DRM_FORMAT_NV61:
    case DRM_FORMAT_NV24:
    case DRM_FORMAT_NV12:
      for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
          uint32_t off_y = util_fb->bo[0].pitch * j + i;
          util_fb->bo[0].data[off_y] = *p++;
        }
      }
      for (j = 0; j < (h / uf.buffers[1].h); j++) {
        for (i = 0; (i < w / uf.buffers[1].w); i++) {
          uint32_t off_u =
              (util_fb->bo[1].pitch * j) + (i * uf.buffers[1].bpp / 8);
          util_fb->bo[1].data[off_u] = *p++;
          util_fb->bo[1].data[off_u + 1] = *p++;
        }
      }
      break;
    case DRM_FORMAT_YVU420:
    case DRM_FORMAT_YUV420:
    case DRM_FORMAT_YUV444:
      for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
          uint32_t off_y = util_fb->bo[0].pitch * j + i;
          util_fb->bo[0].data[off_y] = *p++;
        }
      }
      for (j = 0; j < (h / uf.buffers[1].h); j++) {
        for (i = 0; i < (w / uf.buffers[1].w); i++) {
          uint32_t off_y = util_fb->bo[1].pitch * j + i;
          util_fb->bo[1].data[off_y] = *p++;
        }
      }
      for (j = 0; (j < (h / uf.buffers[1].h)); j++) {
        for (i = 0; (i < (w / uf.buffers[1].w)); i++) {
          uint32_t off_y = util_fb->bo[2].pitch * j + i;
          util_fb->bo[2].data[off_y] = *p++;
        }
      }
      break;
    case DRM_FORMAT_UYVY:
    case DRM_FORMAT_YUYV:
      for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
          uint32_t off_y = util_fb->bo[0].pitch * j + i * 2;
          util_fb->bo[0].data[off_y + 0] = *p++;
          util_fb->bo[0].data[off_y + 1] = *p++;
        }
      }
    break;
    default:
      g_print ("Fill format not defined \n");
      return 1;
  }

  return 0;
}

int
gem_set_params (int fd, uint32_t nvhandle, uint32_t nvblocksize)
{
  struct drm_tegra_gem_set_tiling args;
  memset (&args, 0, sizeof (args));
  args.handle = nvhandle;
  args.mode = DRM_TEGRA_GEM_TILING_MODE_BLOCK;
  args.value = nvblocksize;

  int ret = drmIoctl (fd, DRM_IOCTL_TEGRA_GEM_SET_TILING, &args);
  if (ret < 0) {
    g_print ("Failed to set tiling parameters \n");
    return -1;
  }
  return 1;
}


int
drm_util_destroy_dumb_bo (int fd, struct drm_util_bo *util_bo)
{
  struct drm_mode_destroy_dumb dreq;

  munmap (util_bo->data, util_bo->size);

  memset (&dreq, 0, sizeof (dreq));
  dreq.handle = util_bo->bo_handle;
  drmIoctl (fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);

  return 1;
}

// Destroy an FB and its BOs
int
drm_util_destroy_dumb_fb (int fd, struct drm_util_fb *util_fb)
{
  int i = 0;
  for (i = 0; i < util_fb->num_buffers; i++) {
    drm_util_destroy_dumb_bo (fd, &util_fb->bo[i]);
  }
  util_fb->num_buffers = 0;

  drmModeRmFB (fd, util_fb->fb_id);

  return 1;
}


// Create an FB of the requested size.  Return the fb_id and the mapping
int
drm_util_create_dumb_fb (int fd, int width, int height,
    int drm_format, struct drm_util_fb *util_fb)
{
  int buf_count;
  int ret = 1;
  int i = 0;
  struct util_format uf;
  if (!get_format_info (drm_format, &uf)) {
    g_print ("DRM test helper library can't make a FB of type %d\n",
        drm_format);
    return 0;
  }
  buf_count = uf.num_buffers;
  uint32_t buf_id;
  uint32_t bo_handles[4] = { 0 };
  uint32_t pitches[4] = { 0 };
  uint32_t offsets[4] = { 0 };

  util_fb->num_buffers = 0;

  /* create dumb buffers */
  for (i = 0; i < buf_count; i++) {
    struct drm_util_bo *util_bo = &(util_fb->bo[i]);
    ret = drm_util_create_dumb_bo (fd, width / uf.buffers[i].w,
        height / uf.buffers[i].h, uf.buffers[i].bpp, util_bo);
    if (ret <= 0) {
      g_print ("cannot create dumb buffer\n");
      goto err_destroy;
    }

    bo_handles[i] = util_fb->bo[i].bo_handle;
    pitches[i] = util_fb->bo[i].pitch;
    offsets[i] = 0;
    util_fb->num_buffers++;

  }

  /* create framebuffer object for the dumb-buffer */
  ret = drmModeAddFB2 (fd, width, height, drm_format, bo_handles,
      pitches, offsets, &buf_id, 0);
  if (ret) {
    g_print ("cannot create framebuffer\n");
    goto err_destroy;
  }

  util_fb->fb_id = buf_id;
  util_fb->width = width;
  util_fb->height = height;
  util_fb->format = drm_format;

  return 1;

err_destroy:
  drm_util_destroy_dumb_fb (fd, util_fb);
  return 0;

}

// Close GEM buffers
int
drm_util_close_gem_bo (int fd, uint32_t bo_handle)
{
  struct drm_gem_close gemCloseArgs;

  memset (&gemCloseArgs, 0, sizeof (gemCloseArgs));
  gemCloseArgs.handle = bo_handle;
  int ret = drmIoctl (fd, DRM_IOCTL_GEM_CLOSE, &gemCloseArgs);
  if (ret < 0) {
    return 0;
  }
  return 1;
}
