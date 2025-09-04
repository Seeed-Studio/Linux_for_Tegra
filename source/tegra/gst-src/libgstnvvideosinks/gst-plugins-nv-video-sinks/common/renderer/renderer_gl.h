/*
 * Copyright (c) 2018-2020, NVIDIA CORPORATION.  All rights reserved.
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

#ifndef __GST_NV_VIDEO_RENDERER_GL_H__
#define __GST_NV_VIDEO_RENDERER_GL_H__

#include "context.h"

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

G_BEGIN_DECLS

#define GST_TYPE_NV_VIDEO_RENDERER_GL \
    (gst_nv_video_renderer_gl_get_type())
#define GST_NV_VIDEO_RENDERER_GL(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_NV_VIDEO_RENDERER_GL, GstNvVideoRendererGl))
#define GST_NV_VIDEO_RENDERER_GL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_NV_VIDEO_RENDERER_GL, GstNvVideoRendererGlClass))
#define GST_IS_NV_VIDEO_RENDERER_GL(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_NV_VIDEO_RENDERER_GL))
#define GST_IS_NV_VIDEO_RENDERER_GL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_NV_VIDEO_RENDERER_GL))
#define GST_NV_VIDEO_RENDERER_GL_CAST(obj) \
    ((GstNvVideoRendererGl*)(obj))

typedef struct _GstNvVideoRendererGl GstNvVideoRendererGl;
typedef struct _GstNvVideoRendererGlClass GstNvVideoRendererGlClass;

#define RENDERER_NUM_GL_TEXTURES 1

struct _GstNvVideoRendererGl
{
  GstNvVideoRenderer parent;

  GLuint vert_obj[3]; /* EGL frame, 2D frame, 2D frame border*/
  GLuint frag_obj[3]; /* EGL frame, 2D frame, 2D frame border*/
  GLuint prog_obj[3]; /* EGL frame, 2D frame, 2D frame border*/

  GLint pos;
  GLint tex_pos;
  GLint tex_sampler;
  GLsizei num_textures;
  GLuint textures[RENDERER_NUM_GL_TEXTURES];
  unsigned int vertex_buffer;
  unsigned int index_buffer;

  //Defining different attribs and uniforms for 2D textures
  GLuint position_loc[2]; /* Frame and Border */
  GLuint texpos_loc[1]; /* Frame */
  GLuint tex_scale_loc[1][3]; /* [frame] RGB/Y, U/UV, V */
  GLuint tex_loc[1][3]; /* [frame] RGB/Y, U/UV, V */
  unsigned int vertex_buffer_2d;
  unsigned int index_buffer_2d;
  gint num_textures_2d;
  GLuint textures_2d[3];
  GLuint stride[3];

  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
};

struct _GstNvVideoRendererGlClass
{
  GstNvVideoRendererClass parent_class;
};

G_GNUC_INTERNAL
GstNvVideoRendererGl * gst_nv_video_renderer_gl_new (GstNvVideoContext * context);
void
gst_nv_video_renderer_gl_process_shaders (GstNvVideoRenderer * renderer, gchar ** frag_prog, const gchar *texnames[], GstVideoFormat format);

gboolean
gst_nv_video_renderer_gl_cuda_init (GstNvVideoContext * context, GstNvVideoRenderer * renderer);

GType gst_nv_video_renderer_gl_get_type (void);

G_END_DECLS

#endif /* __GST_NV_VIDEO_RENDERER_GL_H__ */
