/*
 * Copyright (c) 2018-2020, NVIDIA CORPORATION.  All rights reserved.
 * Copyright (C) 2012 Collabora Ltd.
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

#include <string.h>
#include "renderer.h"
#include "renderer_gl.h"

#include "nvbufsurface.h"

#include <cuda.h>
#include <cudaGL.h>
#include <cuda_runtime.h>

#include <EGL/egl.h> // for eglGetProcAddress

/* *INDENT-OFF* */
//Vertex shader for 2D textures
static const char *vert_COPY_prog = {
  "attribute vec3 position;\n"
  "attribute vec2 texpos;\n"
  "varying vec2 opos;\n"
  "void main(void)\n"
  "{\n"
  "     opos = texpos;\n"
  "     gl_Position = vec4(position, 1.0);\n"
  "}\n"
};

static const char *vert_COPY_prog_no_tex = {
  "attribute vec3 position;\n"
  "void main(void)\n"
  "{\n"
  " gl_Position = vec4(position, 1.0);\n"
  "}\n"
};

static const char *vert_source = {
  "attribute vec3 position;\n"
  "attribute vec2 tcoord;\n"
  "varying vec2 vtcoord;\n"
  "void main(void)\n"
  "{\n"
  "     vtcoord = tcoord;\n"
  "     gl_Position = vec4(position, 1.0);\n"
  "}\n"
};

//Fragment shader for 2D textures
static const char *frag_COPY_prog = {
  "precision mediump float;\n"
  "varying vec2 opos;\n"
  "uniform sampler2D tex;\n"
  "uniform vec2 tex_scale0;\n"
  "uniform vec2 tex_scale1;\n"
  "uniform vec2 tex_scale2;\n"
  "void main(void)\n"
  "{\n"
  "  vec4 t = texture2D(tex, opos/tex_scale0);\n"
  "  gl_FragColor = vec4(t.rgb, 1.0);\n"
  "}\n"
};

/* Channel reordering for XYZ <-> ZYX conversion */
static const char *frag_REORDER_prog = {
  "precision mediump float;"
  "varying vec2 opos;"
  "uniform sampler2D tex;"
  "uniform vec2 tex_scale0;"
  "uniform vec2 tex_scale1;"
  "uniform vec2 tex_scale2;"
  "void main(void)"
  "{"
  " vec4 t = texture2D(tex, opos / tex_scale0);"
  " gl_FragColor = vec4(t.%c, t.%c, t.%c, 1.0);"
  "}"
};

/* Packed YUV converters */

/** AYUV to RGB conversion */
static const char *frag_AYUV_prog = {
  "precision mediump float;"
  "varying vec2 opos;"
  "uniform sampler2D tex;"
  "uniform vec2 tex_scale0;"
  "uniform vec2 tex_scale1;"
  "uniform vec2 tex_scale2;"
  "const vec3 offset = vec3(-0.0625, -0.5, -0.5);"
  "const vec3 rcoeff = vec3(1.164, 0.000, 1.596);"
  "const vec3 gcoeff = vec3(1.164,-0.391,-0.813);"
  "const vec3 bcoeff = vec3(1.164, 2.018, 0.000);"
  "void main(void) {"
  "  float r,g,b;"
  "  vec3 yuv;"
  "  yuv  = texture2D(tex,opos / tex_scale0).gba;"
  "  yuv += offset;"
  "  r = dot(yuv, rcoeff);"
  "  g = dot(yuv, gcoeff);"
  "  b = dot(yuv, bcoeff);"
  "  gl_FragColor=vec4(r,g,b,1.0);"
  "}"
};

/* Planar YUV converters */

/** YUV to RGB conversion */
static const char *frag_PLANAR_YUV_prog = {
  "precision mediump float;"
  "varying vec2 opos;"
  "uniform sampler2D Ytex,Utex,Vtex;"
  "uniform vec2 tex_scale0;"
  "uniform vec2 tex_scale1;"
  "uniform vec2 tex_scale2;"
  "const vec3 offset = vec3(-0.0625, -0.5, -0.5);"
  "const vec3 rcoeff = vec3(1.164, 0.000, 1.596);"
  "const vec3 gcoeff = vec3(1.164,-0.391,-0.813);"
  "const vec3 bcoeff = vec3(1.164, 2.018, 0.000);"
  "void main(void) {"
  "  float r,g,b;"
  "  vec3 yuv;"
  "  yuv.x=texture2D(Ytex,opos / tex_scale0).r;"
  "  yuv.y=texture2D(Utex,opos / tex_scale1).r;"
  "  yuv.z=texture2D(Vtex,opos / tex_scale2).r;"
  "  yuv += offset;"
  "  r = dot(yuv, rcoeff);"
  "  g = dot(yuv, gcoeff);"
  "  b = dot(yuv, bcoeff);"
  "  gl_FragColor=vec4(r,g,b,1.0);"
  "}"
};

/** NV12/NV21 to RGB conversion */
static const char *frag_NV12_NV21_prog = {
  "precision mediump float;"
  "varying vec2 opos;"
  "uniform sampler2D Ytex,UVtex;"
  "uniform vec2 tex_scale0;"
  "uniform vec2 tex_scale1;"
  "uniform vec2 tex_scale2;"
  "const vec3 offset = vec3(-0.0625, -0.5, -0.5);"
  "const vec3 rcoeff = vec3(1.164, 0.000, 1.596);"
  "const vec3 gcoeff = vec3(1.164,-0.391,-0.813);"
  "const vec3 bcoeff = vec3(1.164, 2.018, 0.000);"
  "void main(void) {"
  "  float r,g,b;"
  "  vec3 yuv;"
  "  yuv.x=texture2D(Ytex,opos / tex_scale0).r;"
  "  yuv.yz=texture2D(UVtex,opos / tex_scale1).%c%c;"
  "  yuv += offset;"
  "  r = dot(yuv, rcoeff);"
  "  g = dot(yuv, gcoeff);"
  "  b = dot(yuv, bcoeff);"
  "  gl_FragColor=vec4(r,g,b,1.0);"
  "}"
};

/* Paint all black */
static const char *frag_BLACK_prog = {
  "precision mediump float;\n"
  "void main(void)\n"
  "{\n"
  " gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);\n"
  "}\n"
};

static const char *frag_source = {
  "#extension GL_OES_EGL_image_external : require\n"
  "precision mediump float;\n"
  "varying vec2 vtcoord;\n"
  "uniform samplerExternalOES tex;\n"
  "void main(void)\n"
  "{\n"
  "     gl_FragColor = texture2D(tex, vtcoord);\n"
  "}\n"
};

/* *INDENT-ON* */
static const GLfloat vertices_2d[] = {
  1.0f, 1.0f, 0.0f, 1.0f, 0.0f,
  1.0f, -1.0f, 0.0f, 1.0f, 1.0f,
  -1.0f, 1.0f, 0.0f, 0.0f, 0.0f,
  -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,

  1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
  1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
  -1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
  -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,

  1.0f, 1.0f, 0.0f, 0.0f, 0.0f,
  1.0f, 1.0f, 0.0f, 0.0f, 0.0f,
  -1.0f, 1.0f, 0.0f, 0.0f, 0.0f,
  -1.0f, 1.0f, 0.0f, 0.0f, 0.0f,

  1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
  1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
  -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
  -1.0f, -1.0f, 0.0f, 0.0f, 0.0f
};

static const GLushort indices_2d[] = {0,1,2,3};
static const GLfloat vertices[] = {
  1.0f, 1.0f, 0.0f, 1.0f, 0.0f,
  -1.0f, 1.0f, 0.0f, 0.0f, 0.0f,
  -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,
  1.0f, -1.0f, 0.0f, 1.0f, 1.0f
};

static const GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

G_GNUC_INTERNAL extern GstDebugCategory *gst_debug_nv_video_renderer;
#define GST_CAT_DEFAULT gst_debug_nv_video_renderer

G_DEFINE_TYPE (GstNvVideoRendererGl, gst_nv_video_renderer_gl,
    GST_TYPE_NV_VIDEO_RENDERER);

static gboolean
check_gl_error (GstNvVideoRenderer * renderer, const char *func)
{
  GLuint error = GL_NO_ERROR;

  if ((error = glGetError ()) != GL_NO_ERROR) {
    GST_ERROR_OBJECT (renderer, "%s returned GL error 0x%x", func, error);
    return TRUE;
  }

  return FALSE;
}

static GLuint
gst_nv_video_renderer_gl_compile_shader (GLenum shader_type, const char *source)
{
  GLint obj, status;

  obj = glCreateShader (shader_type);
  glShaderSource (obj, 1, &source, NULL);
  glCompileShader (obj);
  glGetShaderiv (obj, GL_COMPILE_STATUS, &status);
  if (status != GL_TRUE) {
    glDeleteShader (obj);
    return 0;
  }

  return obj;
}

static gboolean
create_shader_program (GstNvVideoRenderer * renderer, GLint *prog, GLint *vert, GLint *frag, const gchar * vert_shader, const gchar * frag_shader) {
  GLint status;

  *vert = gst_nv_video_renderer_gl_compile_shader (GL_VERTEX_SHADER, vert_shader);
  if (!*vert) {
    GST_DEBUG_OBJECT (renderer, "failed to compile vertex shader");
    goto fail;
  }

  *frag =
      gst_nv_video_renderer_gl_compile_shader (GL_FRAGMENT_SHADER, frag_shader);
  if (!*frag) {
    GST_DEBUG_OBJECT (renderer, "failed to compile fragment shader");
    goto fail;
  }

  *prog = glCreateProgram ();
  if (!*prog) {
    GST_ERROR_OBJECT (renderer, "failed to create GL program object");
    goto fail;
  }

  glAttachShader (*prog, *vert);
  glAttachShader (*prog, *frag);
  glLinkProgram (*prog);
  glGetProgramiv (*prog, GL_LINK_STATUS, &status);
  if (status != GL_TRUE) {
    GST_ERROR_OBJECT (renderer, "failed to link GL program");
    goto fail;
  }
  return TRUE;

fail:
  {
    if (*frag && *prog)
      glDetachShader (*prog, *frag);
    if (*vert && *prog)
      glDetachShader (*prog, *vert);
    if (*prog)
      glDeleteProgram (*prog);
    if (*frag)
      glDeleteShader (*frag);
    if (*vert)
      glDeleteShader (*vert);
    *prog = 0;
    *frag = 0;
    *vert = 0;
    return FALSE;
  }
}

void
gst_nv_video_renderer_gl_process_shaders (GstNvVideoRenderer * renderer, gchar ** frag_prog, const gchar *texnames[], GstVideoFormat format)
{
  GstNvVideoRendererGl *renderer_gl = GST_NV_VIDEO_RENDERER_GL (renderer);

  switch (format) {
    case GST_VIDEO_FORMAT_AYUV:
      *frag_prog = (gchar *) frag_AYUV_prog;
      renderer_gl->num_textures_2d = 1;
      texnames[0] = "tex";
      break;
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:
      *frag_prog = (gchar *) frag_PLANAR_YUV_prog;
      renderer_gl->num_textures_2d = 3;
      texnames[0] = "Ytex";
      texnames[1] = "Utex";
      texnames[2] = "Vtex";
      break;
    case GST_VIDEO_FORMAT_NV12:
      *frag_prog = g_strdup_printf (frag_NV12_NV21_prog, 'r', 'a');
      renderer_gl->num_textures_2d = 2;
      texnames[0] = "Ytex";
      texnames[1] = "UVtex";
      break;
    case GST_VIDEO_FORMAT_NV21:
      *frag_prog = g_strdup_printf (frag_NV12_NV21_prog, 'a', 'r');
      renderer_gl->num_textures_2d = 2;
      texnames[0] = "Ytex";
      texnames[1] = "UVtex";
      break;
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_BGRA:
      *frag_prog = g_strdup_printf (frag_REORDER_prog, 'b', 'g', 'r');
      renderer_gl->num_textures_2d = 1;
      texnames[0] = "tex";
      break;
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_ARGB:
      *frag_prog = g_strdup_printf (frag_REORDER_prog, 'g', 'b', 'a');
      renderer_gl->num_textures_2d = 1;
      texnames[0] = "tex";
      break;
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_ABGR:
      *frag_prog = g_strdup_printf (frag_REORDER_prog, 'a', 'b', 'g');
      renderer_gl->num_textures_2d = 1;
      texnames[0] = "tex";
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGB16:
      *frag_prog = (gchar *) frag_COPY_prog;
      renderer_gl->num_textures_2d = 1;
      texnames[0] = "tex";
      break;
    default:
      g_assert_not_reached ();
      break;
  }
  return;
}

static gboolean
gst_nv_video_renderer_gl_setup (GstNvVideoRenderer * renderer)
{
  GstNvVideoRendererGl *renderer_gl = GST_NV_VIDEO_RENDERER_GL (renderer);
  GLint prog_obj = 0;
  GLint vert_obj = 0;
  GLint frag_obj = 0;

  /* Setup of 2D textures */
  g_assert (!renderer_gl->prog_obj[1]);
  g_assert (!renderer_gl->vert_obj[1]);
  g_assert (!renderer_gl->frag_obj[1]);
  const gchar *texnames[3] = { NULL, };
  gchar *frag_prog = NULL;

  gst_nv_video_renderer_gl_process_shaders (renderer, &frag_prog, texnames, renderer->format);

  if (!create_shader_program (renderer, &prog_obj, &vert_obj, &frag_obj, vert_COPY_prog, frag_prog))
  {
    GST_DEBUG_OBJECT (renderer, "failed to compile shaders");
    goto fail;
  }
  renderer_gl->prog_obj[1] = prog_obj;
  renderer_gl->vert_obj[1] = vert_obj;
  renderer_gl->frag_obj[1] = frag_obj;

  renderer_gl->position_loc[0] = glGetAttribLocation (renderer_gl->prog_obj[1], "position");
  renderer_gl->texpos_loc[0] = glGetAttribLocation (renderer_gl->prog_obj[1], "texpos");
  renderer_gl->tex_scale_loc[0][0] = glGetUniformLocation (renderer_gl->prog_obj[1], "tex_scale0");
  renderer_gl->tex_scale_loc[0][1] = glGetUniformLocation (renderer_gl->prog_obj[1], "tex_scale1");
  renderer_gl->tex_scale_loc[0][2] = glGetUniformLocation (renderer_gl->prog_obj[1], "tex_scale2");

  for (int i=0; i < renderer_gl->num_textures_2d; i++) {
    renderer_gl->tex_loc[0][i] = glGetUniformLocation (renderer_gl->prog_obj[1], texnames[i]);
  }

  // Build shader for black borders
  prog_obj = 0;
  vert_obj = 0;
  frag_obj = 0;
  g_assert (!renderer_gl->prog_obj[2]);
  g_assert (!renderer_gl->vert_obj[2]);
  g_assert (!renderer_gl->frag_obj[2]);

  if (!create_shader_program (renderer, &prog_obj, &vert_obj, &frag_obj, vert_COPY_prog_no_tex, frag_BLACK_prog))
  {
    GST_DEBUG_OBJECT (renderer, "failed to compile shaders");
    goto fail;
  }

  renderer_gl->prog_obj[2] = prog_obj;
  renderer_gl->vert_obj[2] = vert_obj;
  renderer_gl->frag_obj[2] = frag_obj;

  renderer_gl->position_loc[1] = glGetAttribLocation (renderer_gl->prog_obj[2], "position");

  //Generate Textures
  glGenTextures (renderer_gl->num_textures_2d, renderer_gl->textures_2d);
  if (check_gl_error (renderer, "glGenTextures2d")) {
    renderer_gl->num_textures = 0;
    goto fail;
  }

  for (int i=0; i < renderer_gl->num_textures_2d; i++) {
    glBindTexture (GL_TEXTURE_2D, renderer_gl->textures_2d[i]);
    if (check_gl_error (renderer, "glBindTextures")) {
      goto fail;
    }

    /* Set 2D resizing params */
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (check_gl_error (renderer, "glTexParameteri")) {
      goto fail;
    }
  }

  glGenBuffers (1, &renderer_gl->vertex_buffer_2d);
  glBindBuffer (GL_ARRAY_BUFFER, renderer_gl->vertex_buffer_2d);
  glBufferData (GL_ARRAY_BUFFER, sizeof (vertices_2d), vertices_2d,
      GL_STATIC_DRAW);

  glGenBuffers (1, &renderer_gl->index_buffer_2d);
  glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, renderer_gl->index_buffer_2d);
  glBufferData (GL_ELEMENT_ARRAY_BUFFER, sizeof (indices_2d), indices_2d,
      GL_STATIC_DRAW);

  glUseProgram(0);

  /* Setup for GL_OES Texture */
  prog_obj = 0;
  vert_obj = 0;
  frag_obj = 0;
  int i;

  g_assert (!renderer_gl->prog_obj[0]);
  g_assert (!renderer_gl->vert_obj[0]);
  g_assert (!renderer_gl->frag_obj[0]);

  if (!create_shader_program (renderer, &prog_obj, &vert_obj, &frag_obj, vert_source, frag_source))
  {
    GST_DEBUG_OBJECT (renderer, "failed to compile shaders");
    goto fail;
  }

  renderer_gl->prog_obj[0] = prog_obj;
  renderer_gl->vert_obj[0] = vert_obj;
  renderer_gl->frag_obj[0] = frag_obj;

  renderer_gl->pos = glGetAttribLocation (renderer_gl->prog_obj[0], "position");
  renderer_gl->tex_pos = glGetAttribLocation (renderer_gl->prog_obj[0], "tcoord");
  renderer_gl->tex_sampler = glGetUniformLocation (renderer_gl->prog_obj[0], "tex");
  if (check_gl_error (renderer, "glGetUniformLocation")) {
    goto fail;
  }

  renderer_gl->num_textures = RENDERER_NUM_GL_TEXTURES;
  glGenTextures (renderer_gl->num_textures, renderer_gl->textures);
  if (check_gl_error (renderer, "glGenTextures")) {
    renderer_gl->num_textures = 0;
    goto fail;
  }

  for (i = 0; i < renderer_gl->num_textures; i++) {
    glBindTexture (GL_TEXTURE_EXTERNAL_OES, renderer_gl->textures[i]);
    if (check_gl_error (renderer, "glBindTexture")) {
      goto fail;
    }

    glTexParameteri (GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (check_gl_error (renderer, "glTexParameteri")) {
      goto fail;
    }
  }

  glUseProgram (renderer_gl->prog_obj[0]);
  if (check_gl_error (renderer, "glUseProgram")) {
    goto fail;
  }

  glUniform1i (renderer_gl->tex_sampler, 0);

  glGenBuffers (1, &renderer_gl->vertex_buffer);
  glBindBuffer (GL_ARRAY_BUFFER, renderer_gl->vertex_buffer);
  glBufferData (GL_ARRAY_BUFFER, 4 * 5 * sizeof (GLfloat), vertices,
      GL_STATIC_DRAW);

  glGenBuffers (1, &renderer_gl->index_buffer);
  glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, renderer_gl->index_buffer);
  glBufferData (GL_ELEMENT_ARRAY_BUFFER, sizeof (indices), indices,
      GL_STATIC_DRAW);

  renderer_gl->glEGLImageTargetTexture2DOES =
      (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
      eglGetProcAddress ("glEGLImageTargetTexture2DOES");

  glUseProgram(0);

  return TRUE;

fail:
  GST_ERROR_OBJECT (renderer, "Gl renderer setup failed");

  //Failed in 2D texture part
  for (i = 0; i < renderer_gl->num_textures_2d; i++) {
    glDeleteTextures (renderer_gl->num_textures_2d, renderer_gl->textures_2d);
  }

  renderer_gl->num_textures_2d = 0;

  if (prog_obj) {
    glDetachShader (prog_obj, vert_obj);
    glDetachShader (prog_obj, frag_obj);
    glDeleteProgram (prog_obj);
  }

  if (vert_obj) {
    glDeleteShader (vert_obj);
  }

  if (frag_obj) {
    glDeleteShader (frag_obj);
  }

  //Failed in EGL OES Texture part
  for (i = 0; i < renderer_gl->num_textures; i++) {
    glDeleteTextures (renderer_gl->num_textures, renderer_gl->textures);
  }

  renderer_gl->num_textures = 0;

  return FALSE;
}

static void
gst_nv_video_renderer_gl_cleanup (GstNvVideoRenderer * renderer)
{
  GstNvVideoRendererGl *renderer_gl = GST_NV_VIDEO_RENDERER_GL (renderer);
  int i;

  for (i = 0; i < 3; i++)
  {
    if (renderer_gl->prog_obj[i] && renderer_gl->vert_obj[i]) {
      glDetachShader (renderer_gl->prog_obj[i], renderer_gl->vert_obj[i]);
    }
    if (renderer_gl->prog_obj[i] && renderer_gl->frag_obj[i]) {
      glDetachShader (renderer_gl->prog_obj[i], renderer_gl->frag_obj[i]);
    }
    if (renderer_gl->prog_obj[i]) {
      glDeleteProgram (renderer_gl->prog_obj[i]);
      renderer_gl->prog_obj[i] = 0;
    }
    if (renderer_gl->vert_obj[i]) {
      glDeleteShader (renderer_gl->vert_obj[i]);
      renderer_gl->vert_obj[i] = 0;
    }
    if (renderer_gl->frag_obj[i]) {
      glDeleteShader (renderer_gl->frag_obj[i]);
      renderer_gl->frag_obj[i] = 0;
    }
  }

  if (renderer_gl->vertex_buffer) {
    glDeleteBuffers (1, &renderer_gl->vertex_buffer);
    renderer_gl->vertex_buffer = 0;
  }
  if (renderer_gl->vertex_buffer_2d) {
    glDeleteBuffers (1, &renderer_gl->vertex_buffer_2d);
    renderer_gl->vertex_buffer_2d = 0;
  }

  if (renderer_gl->index_buffer) {
    glDeleteBuffers (1, &renderer_gl->index_buffer);
    renderer_gl->index_buffer = 0;
  }
  if (renderer_gl->index_buffer_2d) {
    glDeleteBuffers (1, &renderer_gl->index_buffer_2d);
    renderer_gl->index_buffer_2d = 0;
  }

  for (i = 0; i < renderer_gl->num_textures; i++) {
    glDeleteTextures (renderer_gl->num_textures, renderer_gl->textures);
  }
  for (i = 0; i < renderer_gl->num_textures_2d; i++) {
    glDeleteTextures (renderer_gl->num_textures_2d, renderer_gl->textures_2d);
  }

  renderer_gl->num_textures = 0;
  renderer_gl->num_textures_2d = 0;

  GST_DEBUG_OBJECT (renderer, "Gl renderer cleanup done");

  return;
}

gboolean
gst_nv_video_renderer_gl_cuda_init (GstNvVideoContext * context, GstNvVideoRenderer * renderer)
{
  GstNvVideoRendererGl *renderer_gl = GST_NV_VIDEO_RENDERER_GL (renderer);
  CUcontext pctx;
  CUresult result;
  GLenum error;
  int i;
  guint width, height, pstride;
  GstVideoFormat videoFormat;

  cuInit(0);
  result = cuCtxCreate(&pctx, 0, 0);
  if (result != CUDA_SUCCESS) {
    g_print ("cuCtxCreate failed with error(%d) %s\n", result, __func__);
    return FALSE;
  }

  context->cuContext = pctx;

  width = context->configured_info.width;
  height = context->configured_info.height;

  videoFormat = context->configured_info.finfo->format;

  switch (videoFormat) {
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGB: {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, renderer_gl->textures_2d[0]);
      if (videoFormat == GST_VIDEO_FORMAT_RGB ||
          videoFormat == GST_VIDEO_FORMAT_BGR) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
      } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      }
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      error = glGetError();
      if (error != GL_NO_ERROR) {
        g_print("glerror %x error %d\n", error, __LINE__);
        return FALSE;
      }
      result = cuGraphicsGLRegisterImage(&(context->cuResource[0]), renderer_gl->textures_2d[0], GL_TEXTURE_2D, 0);
      if (result != CUDA_SUCCESS) {
        g_print ("cuGraphicsGLRegisterBuffer failed with error(%d) %s texture = %x\n", result, __func__, renderer_gl->textures_2d[0]);
        return FALSE;
      }
    }
    break;
    case GST_VIDEO_FORMAT_I420: {
      for (i = 0; i < 3; i++) {
        if (i == 0)
          glActiveTexture (GL_TEXTURE0);
        else if (i == 1)
          glActiveTexture (GL_TEXTURE1);
        else if (i == 2)
          glActiveTexture (GL_TEXTURE2);

        width = GST_VIDEO_INFO_COMP_WIDTH(&(context->configured_info), i);
        height = GST_VIDEO_INFO_COMP_HEIGHT(&(context->configured_info), i);

        glBindTexture(GL_TEXTURE_2D, renderer_gl->textures_2d[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width, height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        error = glGetError();
        if (error != GL_NO_ERROR) {
          g_print("glerror %x error %d\n", error, __LINE__);
          return FALSE;
        }
        result = cuGraphicsGLRegisterImage(&(context->cuResource[i]), renderer_gl->textures_2d[i], GL_TEXTURE_2D, 0);
        if (result != CUDA_SUCCESS) {
          g_print ("cuGraphicsGLRegisterBuffer failed with error(%d) %s texture = %x\n", result, __func__, renderer_gl->textures_2d[i]);
          return FALSE;
        }
      }
    }
    break;
    case GST_VIDEO_FORMAT_NV12: {
      for (i = 0; i < 2; i++) {
        if (i == 0)
          glActiveTexture (GL_TEXTURE0);
        else if (i == 1)
          glActiveTexture (GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, renderer_gl->textures_2d[i]);

        width = GST_VIDEO_INFO_COMP_WIDTH(&(context->configured_info), i);
        height = GST_VIDEO_INFO_COMP_HEIGHT(&(context->configured_info), i);
        pstride = GST_VIDEO_INFO_COMP_PSTRIDE(&(context->configured_info), i);

        if (i == 0)
          glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width*pstride, height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
        else if ( i == 1)
          glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, width*pstride, height, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        error = glGetError();
        if (error != GL_NO_ERROR) {
          g_print("glerror %x error %d\n", error, __LINE__);
          return FALSE;
        }
        result = cuGraphicsGLRegisterImage(&(context->cuResource[i]), renderer_gl->textures_2d[i], GL_TEXTURE_2D, 0);
        if (result != CUDA_SUCCESS) {
          g_print ("cuGraphicsGLRegisterBuffer failed with error(%d) %s texture = %x\n", result, __func__, renderer_gl->textures_2d[i]);
          return FALSE;
        }
      }
    }
    break;
    default:
      g_print("buffer format not supported\n");
      return FALSE;
  }
  context->is_cuda_init = TRUE;
  return TRUE;
}

static void
gst_nv_video_renderer_gl_cuda_cleanup (GstNvVideoContext * context, GstNvVideoRenderer * renderer)
{
  CUresult result;
  guint i;

  for (i = 0; i < 3; i++) {
    if (context->cuResource[i])
      cuGraphicsUnregisterResource (context->cuResource[i]);
  }

  if (context->cuContext) {
    result = cuCtxDestroy(context->cuContext);
    if (result != CUDA_SUCCESS) {
      g_print ("cuCtxDestroy failed with error(%d) %s\n", result, __func__);
    }
  }
}

static void
gst_nv_video_renderer_gl_update_viewport (GstNvVideoRenderer * renderer,
    int width, int height)
{
  glClearColor (0.0f, 0.0f, 0.0f, 0.0f);
  glClear (GL_COLOR_BUFFER_BIT);
  glViewport (0, 0, width, height);
}

static gboolean
gst_nv_video_renderer_gl_fill_texture (GstNvVideoContext *context, GstNvVideoRenderer * renderer, GstBuffer * buf)
{
  GstNvVideoRendererGl *renderer_gl = GST_NV_VIDEO_RENDERER_GL (renderer);
  GstVideoFrame vframe;
  gint w, h;

  memset (&vframe, 0, sizeof (vframe));

  if (!gst_video_frame_map (&vframe, &context->configured_info, buf,
          GST_MAP_READ)) {
    GST_ERROR_OBJECT (context, "Couldn't map frame");
    goto HANDLE_ERROR;
  }

  w = GST_VIDEO_FRAME_WIDTH (&vframe);
  h = GST_VIDEO_FRAME_HEIGHT (&vframe);

  GST_DEBUG_OBJECT (context,
      "Got buffer %p: %dx%d size %" G_GSIZE_FORMAT, buf, w, h,
      gst_buffer_get_size (buf));

  gint stride;
  gint stride_width;
  gint c_w;

  switch (context->configured_info.finfo->format) {
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGB:{
      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_WIDTH (&vframe);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w * 3) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w * 3) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (GST_ROUND_UP_2 (c_w * 3) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else if (c_w * 3 == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width * 3) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width * 3) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (GST_ROUND_UP_2 (stride_width * 3) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else if (stride_width * 3 == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        } else {
          GST_ERROR_OBJECT (context, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (check_gl_error (renderer,"glPixelStorei"))
        goto HANDLE_ERROR;

      renderer_gl->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, renderer_gl->textures_2d[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB, stride_width, h, 0, GL_RGB,
          GL_UNSIGNED_BYTE, GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0));
      break;
    }
    case GST_VIDEO_FORMAT_RGB16:{
      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_WIDTH (&vframe);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w * 2) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w * 2) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (c_w * 2 == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width * 4) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width * 2) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (stride_width * 2 == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else {
          GST_ERROR_OBJECT (context, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (check_gl_error (renderer,"glPixelStorei"))
        goto HANDLE_ERROR;

      renderer_gl->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, renderer_gl->textures_2d[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB, stride_width, h, 0, GL_RGB,
          GL_UNSIGNED_SHORT_5_6_5, GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0));
      break;
    }
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:{
      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width= c_w = GST_VIDEO_FRAME_WIDTH (&vframe);
      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w * 4) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (c_w * 4 == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else {
        stride_width = stride;
        if (GST_ROUND_UP_8 (stride_width * 4) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (stride_width * 4 == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else {
          GST_ERROR_OBJECT (context, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (check_gl_error (renderer,"glPixelStorei"))
        goto HANDLE_ERROR;

      renderer_gl->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, renderer_gl->textures_2d[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, stride_width, h, 0,
          GL_RGBA, GL_UNSIGNED_BYTE, GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0));

      break;
    }
    case GST_VIDEO_FORMAT_AYUV:{
      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_WIDTH (&vframe);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w * 4) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (c_w * 4 == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width * 4) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (stride_width * 4 == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else {
          GST_ERROR_OBJECT (context, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (check_gl_error (renderer,"glPixelStorei"))
        goto HANDLE_ERROR;

      renderer_gl->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, renderer_gl->textures_2d[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, stride_width, h, 0,
          GL_RGBA, GL_UNSIGNED_BYTE, GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0));
      break;
    }
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:{
      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, 0);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (GST_ROUND_UP_2 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else if (c_w == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (GST_ROUND_UP_2 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else if (stride_width == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        } else {
          GST_ERROR_OBJECT (context, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (check_gl_error (renderer,"glPixelStorei"))
        goto HANDLE_ERROR;

      renderer_gl->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, renderer_gl->textures_2d[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
          stride_width,
          GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, 0),
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          GST_VIDEO_FRAME_COMP_DATA (&vframe, 0));


      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 1);
      stride_width = c_w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, 1);

      glActiveTexture (GL_TEXTURE1);

      if (GST_ROUND_UP_8 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (GST_ROUND_UP_2 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else if (c_w == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (GST_ROUND_UP_2 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else if (stride_width == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        } else {
          GST_ERROR_OBJECT (context, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (check_gl_error (renderer,"glPixelStorei"))
        goto HANDLE_ERROR;

      renderer_gl->stride[1] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, renderer_gl->textures_2d[1]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
          stride_width,
          GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, 1),
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          GST_VIDEO_FRAME_COMP_DATA (&vframe, 1));


      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 2);
      stride_width = c_w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, 2);

      glActiveTexture (GL_TEXTURE2);

      if (GST_ROUND_UP_8 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (GST_ROUND_UP_2 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else if (c_w == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (GST_ROUND_UP_2 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else if (stride_width == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        } else {
          GST_ERROR_OBJECT (context, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (check_gl_error (renderer,"glPixelStorei"))
        goto HANDLE_ERROR;

      renderer_gl->stride[2] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, renderer_gl->textures_2d[2]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
          stride_width,
          GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, 2),
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          GST_VIDEO_FRAME_COMP_DATA (&vframe, 2));
      break;
    }
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:{
      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, 0);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (GST_ROUND_UP_2 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else if (c_w == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (GST_ROUND_UP_2 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else if (stride_width == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        } else {
          GST_ERROR_OBJECT (context, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (check_gl_error (renderer,"glPixelStorei"))
        goto HANDLE_ERROR;

      renderer_gl->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, renderer_gl->textures_2d[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
          stride_width,
          GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, 0),
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0));

      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 1);
      stride_width = c_w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, 1);

      glActiveTexture (GL_TEXTURE1);

      if (GST_ROUND_UP_8 (c_w * 2) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w * 2) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (c_w * 2 == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else {
        stride_width = stride / 2;

        if (GST_ROUND_UP_8 (stride_width * 2) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width * 2) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (stride_width * 2 == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else {
          GST_ERROR_OBJECT (context, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (check_gl_error (renderer,"glPixelStorei"))
        goto HANDLE_ERROR;

      renderer_gl->stride[1] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, renderer_gl->textures_2d[1]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA,
          stride_width,
          GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, 1),
          0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE,
          GST_VIDEO_FRAME_PLANE_DATA (&vframe, 1));
      break;
    }
    default:
      g_assert_not_reached ();
      break;
  }

  if (check_gl_error (renderer,"glTexImage2D"))
    goto HANDLE_ERROR;


  gst_video_frame_unmap (&vframe);

  return TRUE;

  HANDLE_ERROR:
  {
    if (vframe.buffer)
      gst_video_frame_unmap (&vframe);

    return FALSE;
  }
}

static gboolean
gst_nv_video_renderer_gl_cuda_buffer_copy (GstNvVideoContext *context, GstNvVideoRenderer * renderer, GstBuffer * buf)
{
  GstNvVideoRendererGl *renderer_gl = GST_NV_VIDEO_RENDERER_GL (renderer);
  CUarray dpArray;
  CUresult result;
  guint width, height;
  GstMapInfo info = GST_MAP_INFO_INIT;
  GstVideoFormat videoFormat;
  NvBufSurface *in_surface = NULL;

  width = context->configured_info.width;
  height = context->configured_info.height;

  result = cuCtxSetCurrent(context->cuContext);
  if (result != CUDA_SUCCESS) {
    g_print ("cuCtxSetCurrent failed with error(%d) %s\n", result, __func__);
    return FALSE;
  }
  gst_buffer_map (buf, &info, GST_MAP_READ);
  in_surface = (NvBufSurface*) info.data;
  gst_buffer_unmap (buf, &info);

  if (in_surface->batchSize != 1) {
    GST_ERROR_OBJECT (context,"ERROR: Batch size not 1\n");
    return FALSE;
  }

  NvBufSurfaceMemType memType = in_surface->memType;
  gboolean is_device_memory = FALSE;
  gboolean is_host_memory = FALSE;

  if (memType == NVBUF_MEM_DEFAULT || memType == NVBUF_MEM_CUDA_DEVICE || memType == NVBUF_MEM_CUDA_UNIFIED) {
    is_device_memory = TRUE;
  }
  else if (memType == NVBUF_MEM_CUDA_PINNED) {
    is_host_memory = TRUE;
  }

  CUDA_MEMCPY2D m = { 0 };

  videoFormat = context->configured_info.finfo->format;
  switch (videoFormat) {
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGB: {
      gint bytesPerPix = 4;
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, renderer_gl->textures_2d[0]);

      result = cuGraphicsMapResources(1, &(context->cuResource[0]), 0);
      if (result != CUDA_SUCCESS) {
        g_print ("cuGraphicsMapResources failed with error(%d) %s\n", result, __func__);
        return FALSE;
      }
      result = cuGraphicsSubResourceGetMappedArray(&dpArray, context->cuResource[0], 0, 0);
      if (result != CUDA_SUCCESS) {
        g_print ("cuGraphicsResourceGetMappedPointer failed with error(%d) %s\n", result, __func__);
        goto HANDLE_ERROR;
      }

      if (is_device_memory) {
        m.srcDevice = (CUdeviceptr) in_surface->surfaceList[0].dataPtr;
        m.srcMemoryType = CU_MEMORYTYPE_DEVICE;
      }
      else if (is_host_memory) {
        m.srcHost = (void *)in_surface->surfaceList[0].dataPtr;
        m.srcMemoryType = CU_MEMORYTYPE_HOST;
      }

      if (videoFormat == GST_VIDEO_FORMAT_BGR ||
          videoFormat == GST_VIDEO_FORMAT_RGB) {
        bytesPerPix = 3;
      }

      m.srcPitch = in_surface->surfaceList[0].planeParams.pitch[0];

      m.dstPitch = width * bytesPerPix;
      m.WidthInBytes = width * bytesPerPix;

      m.dstMemoryType = CU_MEMORYTYPE_ARRAY;
      m.dstArray = dpArray;
      m.Height = height;

      result = cuMemcpy2D(&m);
      if (result != CUDA_SUCCESS) {
        g_print ("cuMemcpy2D failed with error(%d) %s\n", result, __func__);
        goto HANDLE_ERROR;
      }

      result = cuGraphicsUnmapResources(1, &(context->cuResource[0]), 0);
      if (result != CUDA_SUCCESS) {
        g_print ("cuGraphicsUnmapResources failed with error(%d) %s\n", result, __func__);
        goto HANDLE_ERROR;
      }

      renderer_gl->stride[0] = 1;
      renderer_gl->stride[1] = 1;
      renderer_gl->stride[2] = 1;
     } // case RGBA
     break;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_NV12: {
      uint8_t *ptr;
      int i, pstride;
      int num_planes = (int)in_surface->surfaceList[0].planeParams.num_planes;

      for ( i = 0; i < num_planes; i ++) {
        if (i == 0)
          glActiveTexture (GL_TEXTURE0);
        else if (i == 1)
          glActiveTexture (GL_TEXTURE1);
        else if (i == 2)
          glActiveTexture (GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, renderer_gl->textures_2d[i]);

        result = cuGraphicsMapResources(1, &(context->cuResource[i]), 0);
        if (result != CUDA_SUCCESS) {
          g_print ("cuGraphicsMapResources failed with error(%d) %s\n", result, __func__);
          return FALSE;
        }
        result = cuGraphicsSubResourceGetMappedArray(&dpArray, context->cuResource[i], 0, 0);
        if (result != CUDA_SUCCESS) {
          g_print ("cuGraphicsResourceGetMappedPointer failed with error(%d) %s\n", result, __func__);
          goto HANDLE_ERROR;
        }

        ptr = (uint8_t *)in_surface->surfaceList[0].dataPtr + in_surface->surfaceList[0].planeParams.offset[i];
        if (is_device_memory) {
          m.srcDevice = (CUdeviceptr) ptr;
          m.srcMemoryType = CU_MEMORYTYPE_DEVICE;
        }
        else if (is_host_memory) {
          m.srcHost = (void *)ptr;
          m.srcMemoryType = CU_MEMORYTYPE_HOST;
        }

        width = GST_VIDEO_INFO_COMP_WIDTH(&(context->configured_info), i);
        height = GST_VIDEO_INFO_COMP_HEIGHT(&(context->configured_info), i);
        pstride = GST_VIDEO_INFO_COMP_PSTRIDE(&(context->configured_info), i);
        m.srcPitch = in_surface->surfaceList[0].planeParams.pitch[i];

        m.dstMemoryType = CU_MEMORYTYPE_ARRAY;
        m.dstArray = dpArray;
        m.WidthInBytes = width*pstride;
        m.Height = height;

        result = cuMemcpy2D(&m);
        if (result != CUDA_SUCCESS) {
          g_print ("cuMemcpy2D failed with error(%d) %s %d\n", result, __func__, __LINE__);
          goto HANDLE_ERROR;
        }

        result = cuGraphicsUnmapResources(1, &(context->cuResource[i]), 0);
        if (result != CUDA_SUCCESS) {
          g_print ("cuGraphicsUnmapResources failed with error(%d) %s\n", result, __func__);
          goto HANDLE_ERROR;
        }

        renderer_gl->stride[i] = pstride;
      }
    }// case I420 or NV12
    break;
    default:
      g_print("buffer format not supported\n");
      return FALSE;
    break;
  } //switch
  return TRUE;

HANDLE_ERROR:
    if (context->cuResource[0])
      cuGraphicsUnmapResources(1, &(context->cuResource[0]), 0);
    if (context->cuResource[1])
      cuGraphicsUnmapResources(1, &(context->cuResource[0]), 0);
    if (context->cuResource[2])
      cuGraphicsUnmapResources(1, &(context->cuResource[0]), 0);
    return FALSE;
}

static gboolean
gst_nv_video_renderer_gl_draw_2D_Texture (GstNvVideoRenderer * renderer)
{
  GstNvVideoRendererGl *renderer_gl = GST_NV_VIDEO_RENDERER_GL (renderer);

  glBindBuffer (GL_ARRAY_BUFFER, renderer_gl->vertex_buffer_2d);
  glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, renderer_gl->index_buffer_2d);

  //Draw black border 1
  glUseProgram (renderer_gl->prog_obj[2]);
  glEnableVertexAttribArray (renderer_gl->position_loc[1]);
  if (check_gl_error (renderer, "glEnableVertexAttribArray")) {
    goto HANDLE_ERROR;
  }
  glVertexAttribPointer (renderer_gl->position_loc[1], 3,
      GL_FLOAT, GL_FALSE, 5 * sizeof(GL_FLOAT), (gpointer) (8 * sizeof(GL_FLOAT)));
  if (check_gl_error (renderer, "glVertexAttribPointer")) {
    goto HANDLE_ERROR;
  }
  glDrawElements (GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, 0);
  if (check_gl_error (renderer, "glDrawElements")) {
    goto HANDLE_ERROR;
  }

  //Draw black border 2
  glVertexAttribPointer (renderer_gl->position_loc[1], 3,
      GL_FLOAT, GL_FALSE, 5 * sizeof(GL_FLOAT), (gpointer) (12 * sizeof(GL_FLOAT)));
  if (check_gl_error (renderer, "glVertexAttribPointer")) {
    goto HANDLE_ERROR;
  }
  glDrawElements (GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, 0);
  if (check_gl_error (renderer, "glDrawElements")) {
    goto HANDLE_ERROR;
  }

  glDisableVertexAttribArray (renderer_gl->position_loc[1]);

  //Draw Video frame
  glUseProgram (renderer_gl->prog_obj[1]);

  glUniform2f (renderer_gl->tex_scale_loc[0][0], renderer_gl->stride[0], 1);
  glUniform2f (renderer_gl->tex_scale_loc[0][1], renderer_gl->stride[1], 1);
  glUniform2f (renderer_gl->tex_scale_loc[0][2], renderer_gl->stride[2], 1);

  for (int i=0; i < renderer_gl->num_textures_2d; i++)
  {
    glUniform1i (renderer_gl->tex_loc[0][i], i);
    if (check_gl_error (renderer, "glUniform1i")) {
      goto HANDLE_ERROR;
    }
  }

  glEnableVertexAttribArray (renderer_gl->position_loc[0]);
  if (check_gl_error (renderer, "glEnableVertexAttribArray")) {
    goto HANDLE_ERROR;
  }
  glEnableVertexAttribArray (renderer_gl->texpos_loc[0]);
  if (check_gl_error (renderer, "glEnableVertexAttribArray")) {
    goto HANDLE_ERROR;
  }

  // TODO: Orientation needed to be taken care of.
  glVertexAttribPointer (renderer_gl->position_loc[0], 3,
      GL_FLOAT, GL_FALSE, 5* sizeof (GL_FLOAT), (gpointer) (0 * sizeof (GL_FLOAT)));
  if (check_gl_error (renderer, "glVertexAttribPointer")) {
    goto HANDLE_ERROR;
  }
  glVertexAttribPointer (renderer_gl->texpos_loc[0], 2,
      GL_FLOAT, GL_FALSE, 5* sizeof (GL_FLOAT), (gpointer) (3 * sizeof (GL_FLOAT)));
  if (check_gl_error (renderer, "glVertexAttribPointer")) {
    goto HANDLE_ERROR;
  }

  glDrawElements (GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, 0);
  if (check_gl_error (renderer, "glDrawElements")) {
    goto HANDLE_ERROR;
  }

  glBindBuffer (GL_ARRAY_BUFFER, 0);
  glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
  glDisableVertexAttribArray (renderer_gl->position_loc[0]);
  glDisableVertexAttribArray (renderer_gl->texpos_loc[0]);

  glUseProgram (0);

  return TRUE;

HANDLE_ERROR:
  glDisableVertexAttribArray (renderer_gl->position_loc[0]);
  glDisableVertexAttribArray (renderer_gl->texpos_loc[0]);
  glDisableVertexAttribArray (renderer_gl->position_loc[1]);

  return FALSE;
}

static gboolean
gst_nv_video_renderer_gl_draw_eglimage (GstNvVideoRenderer * renderer,
    void *image)
{
  GstNvVideoRendererGl *renderer_gl = GST_NV_VIDEO_RENDERER_GL (renderer);

  glActiveTexture (GL_TEXTURE0);
  glBindTexture (GL_TEXTURE_EXTERNAL_OES, renderer_gl->textures[0]);

  renderer_gl->glEGLImageTargetTexture2DOES (GL_TEXTURE_EXTERNAL_OES,
      (GLeglImageOES) image);

  glBindBuffer (GL_ARRAY_BUFFER, renderer_gl->vertex_buffer);
  glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, renderer_gl->index_buffer);
  glUseProgram (renderer_gl->prog_obj[0]);
  glVertexAttribPointer (renderer_gl->pos, 3, GL_FLOAT, GL_FALSE,
      5 * sizeof (GLfloat), (void *) 0);
  glVertexAttribPointer (renderer_gl->tex_pos, 2, GL_FLOAT, GL_FALSE,
      5 * sizeof (GLfloat), (void *) (3 * sizeof (GLfloat)));
  glEnableVertexAttribArray (renderer_gl->pos);
  glEnableVertexAttribArray (renderer_gl->tex_pos);

  glDrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

  glBindBuffer (GL_ARRAY_BUFFER, 0);
  glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
  glDisableVertexAttribArray (renderer_gl->pos);
  glDisableVertexAttribArray (renderer_gl->tex_pos);

  glUseProgram (0);

  return TRUE;
}

static void
gst_nv_video_renderer_gl_class_init (GstNvVideoRendererGlClass * klass)
{
  GstNvVideoRendererClass *renderer_class = (GstNvVideoRendererClass *) klass;

  renderer_class->cuda_init =
      GST_DEBUG_FUNCPTR (gst_nv_video_renderer_gl_cuda_init);
  renderer_class->cuda_cleanup =
      GST_DEBUG_FUNCPTR (gst_nv_video_renderer_gl_cuda_cleanup);
  renderer_class->setup = GST_DEBUG_FUNCPTR (gst_nv_video_renderer_gl_setup);
  renderer_class->cleanup =
      GST_DEBUG_FUNCPTR (gst_nv_video_renderer_gl_cleanup);
  renderer_class->update_viewport =
      GST_DEBUG_FUNCPTR (gst_nv_video_renderer_gl_update_viewport);
  renderer_class->fill_texture =
      GST_DEBUG_FUNCPTR (gst_nv_video_renderer_gl_fill_texture);
  renderer_class->cuda_buffer_copy =
      GST_DEBUG_FUNCPTR (gst_nv_video_renderer_gl_cuda_buffer_copy);
  renderer_class->draw_2D_Texture =
      GST_DEBUG_FUNCPTR (gst_nv_video_renderer_gl_draw_2D_Texture);
  renderer_class->draw_eglimage =
      GST_DEBUG_FUNCPTR (gst_nv_video_renderer_gl_draw_eglimage);
}

static void
gst_nv_video_renderer_gl_init (GstNvVideoRendererGl * renderer_gl)
{
  for (int i =0 ; i < 3 ; i++) {
    renderer_gl->prog_obj[i] = 0;
    renderer_gl->vert_obj[i] = 0;
    renderer_gl->frag_obj[i] = 0;
  }
  renderer_gl->num_textures = 0;
  renderer_gl->num_textures_2d = 0;
  renderer_gl->vertex_buffer = 0;
  renderer_gl->vertex_buffer_2d = 0;
  renderer_gl->index_buffer = 0;
  renderer_gl->index_buffer_2d = 0;
}

GstNvVideoRendererGl *
gst_nv_video_renderer_gl_new (GstNvVideoContext * context)
{
  GstNvVideoRendererGl *ret;

  // We need EGL context for GL renderer
  if ((gst_nv_video_context_get_handle_type (context) &
          GST_NV_VIDEO_CONTEXT_TYPE_EGL)
      == 0) {
    return NULL;
  }

  ret = g_object_new (GST_TYPE_NV_VIDEO_RENDERER_GL, NULL);
  gst_object_ref_sink (ret);

  return ret;
}
