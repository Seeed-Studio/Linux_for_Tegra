/*
 * Copyright (c) 2014-2023, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Relation between width, height, PAR(Pixel Aspect Ratio), DAR(Display Aspect Ration):
 *
 *                  dar_n   par_d
 * width = height * ----- * -----
 *                  dar_d   par_n
 *
 *                  dar_d   par_n
 * height = width * ----- * -----
 *                  dar_n   par_d
 *
 * par_n    height   dar_n
 * ----- =  ------ * -----
 * par_d    width    dar_d
 *
 * dar_n   width    par_n
 * ----- = ------ * -----
 * dar_d   height   par_d
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "gstnvvconv.h"

#define NVBUF_MAGIC_NUM 0x70807580

GST_DEBUG_CATEGORY_STATIC (gst_nvvconv_debug);
#define GST_CAT_DEFAULT gst_nvvconv_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

/* Filter properties */
enum
{
  PROP_0,
  PROP_SILENT,
  PROP_FLIP_METHOD,
  PROP_NUM_OUT_BUFS,
  PROP_INTERPOLATION_METHOD,
  PROP_LEFT,
  PROP_RIGHT,
  PROP_TOP,
  PROP_BOTTOM,
  PROP_ENABLE_BLOCKLINEAR_OUTPUT,
  PROP_GPU_DEVICE_ID,
  PROP_COMPUTE_HW,
  PROP_NVBUF_MEMORY_TYPE,
};

#undef MAX_NUM_PLANES
#include "nvbufsurface.h"


#define PROP_FLIP_METHOD_DEFAULT GST_VIDEO_NVFLIP_METHOD_IDENTITY

#define GST_TYPE_VIDEO_NVFLIP_METHOD (gst_video_nvflip_method_get_type())

static const GEnumValue video_nvflip_methods[] = {
  {GST_VIDEO_NVFLIP_METHOD_IDENTITY, "Identity (no rotation)", "none"},
  {GST_VIDEO_NVFLIP_METHOD_90L, "Rotate counter-clockwise 90 degrees",
      "counterclockwise"},
  {GST_VIDEO_NVFLIP_METHOD_180, "Rotate 180 degrees", "rotate-180"},
  {GST_VIDEO_NVFLIP_METHOD_90R, "Rotate clockwise 90 degrees", "clockwise"},
  {GST_VIDEO_NVFLIP_METHOD_HORIZ, "Flip horizontally", "horizontal-flip"},
  {GST_VIDEO_NVFLIP_METHOD_INVTRANS,
      "Flip across upper right/lower left diagonal", "upper-right-diagonal"},
  {GST_VIDEO_NVFLIP_METHOD_VERT, "Flip vertically", "vertical-flip"},
  {GST_VIDEO_NVFLIP_METHOD_TRANS,
      "Flip across upper left/lower right diagonal", "upper-left-diagonal"},
  {0, NULL, NULL},
};

static GType
gst_video_nvflip_method_get_type (void)
{
  static GType video_nvflip_method_type = 0;

  if (!video_nvflip_method_type) {
    video_nvflip_method_type = g_enum_register_static ("GstNvVideoFlipMethod",
        video_nvflip_methods);
  }
  return video_nvflip_method_type;
}

#define GST_TYPE_INTERPOLATION_METHOD (gst_video_interpolation_method_get_type())

static const GEnumValue video_interpolation_methods[] = {
  {GST_INTERPOLATION_NEAREST, "Nearest", "Nearest"},
  {GST_INTERPOLATION_BILINEAR, "Bilinear", "Bilinear"},
  {GST_INTERPOLATION_5_TAP, "5-Tap", "5-Tap"},
  {GST_INTERPOLATION_10_TAP, "10-Tap", "10-Tap"},
  {GST_INTERPOLATION_SMART, "Smart", "Smart"},
  {GST_INTERPOLATION_NICEST, "Nicest", "Nicest"},
  {0, NULL, NULL},
};

static GType
gst_video_interpolation_method_get_type (void)
{
  static GType video_interpolation_method_type = 0;

  if (!video_interpolation_method_type) {
      video_interpolation_method_type = g_enum_register_static ("GstInterpolationMethod",
        video_interpolation_methods);
  }
  return video_interpolation_method_type;
}

#define GST_TYPE_COMPUTE_HW (gst_compute_hw_get_type ())

static const GEnumValue compute_hw[] = {
  {NvBufSurfTransformCompute_Default, "Default, VIC for Jetson", "Default"},
  {NvBufSurfTransformCompute_GPU, "GPU", "GPU"},
  {NvBufSurfTransformCompute_VIC, "VIC", "VIC"},
  {0, NULL, NULL},
};

static GType
gst_compute_hw_get_type (void)
{
  static GType compute_hw_type = 0;

  if(!compute_hw_type) {
    compute_hw_type = g_enum_register_static ("GstNvVidConvComputeHWType",
       compute_hw);
  }

  return compute_hw_type;
}

#define GST_TYPE_NVBUF_MEMORY_TYPE (gst_nvbuf_memory_get_type ())

static const GEnumValue memory_format[] = {
  { NVBUF_MEM_DEFAULT,
    "Default memory allocated, specific to particular platform", "nvbuf-mem-default"},
  { NVBUF_MEM_CUDA_PINNED,
    "Allocate Pinned/Host cuda memory", "nvbuf-mem-cuda-pinned"},
  { NVBUF_MEM_CUDA_DEVICE,
    "Allocate Device cuda memory", "nvbuf-mem-cuda-device"},
  { NVBUF_MEM_SURFACE_ARRAY,
    "Allocate Surface Array memory, applicable for Jetson", "nvbuf-mem-surface-array"},
  {0, NULL, NULL}
};

static GType
gst_nvbuf_memory_get_type (void)
{
  static GType memory_format_type = 0;

  if (!memory_format_type) {
    memory_format_type = g_enum_register_static ("GstNvVidConvBufMemoryType",
        memory_format);
  }

  return memory_format_type;
}

/* capabilities of the inputs and outputs */

/* Input capabilities. */
static GstStaticPadTemplate gst_nvvconv_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_NVMM,
            "{ " "I420, I420_10LE, P010_10LE, I420_12LE, UYVY, YUY2, YVYU, NV12, NV16, NV24, GRAY8, BGRx, RGBA, Y42B, Y444 }") ";" GST_VIDEO_CAPS_MAKE ("{ "
            "I420, UYVY, YUY2, YVYU, NV12, NV16, NV24, P010_10LE, GRAY8, BGRx, RGBA, Y42B, Y444 }")));

/* Output capabilities. */
static GstStaticPadTemplate gst_nvvconv_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_NVMM,
            "{ " "I420, I420_10LE, P010_10LE, UYVY, YUY2, YVYU, NV12, NV16, NV24, GRAY8, BGRx, RGBA, Y42B, Y444 }") ";" GST_VIDEO_CAPS_MAKE ("{ "
            "I420, UYVY, YUY2, YVYU, NV12, NV16, NV24, GRAY8, BGRx, RGBA, Y42B, Y444 }")));

static GstElementClass *gparent_class = NULL;

#define gst_nvvconv_parent_class parent_class
G_DEFINE_TYPE (Gstnvvconv, gst_nvvconv, GST_TYPE_BASE_TRANSFORM);

/* internal methods */
static void gst_nvvconv_init_params (Gstnvvconv * filter);
static gboolean gst_nvvconv_get_pix_fmt (GstVideoInfo * info,
    NvBufSurfaceColorFormat * pix_fmt, gint * isurf_count);
static GstCaps *gst_nvvconv_caps_remove_format_info (GstCaps * caps);
static gboolean gst_nvvconv_do_clearchroma (Gstnvvconv * filter,
    NvBufSurface *surface);
static void gst_nvvconv_free_buf (Gstnvvconv * filter);

/* base transform vmethods */
static gboolean gst_nvvconv_start (GstBaseTransform * btrans);
static gboolean gst_nvvconv_stop (GstBaseTransform * btrans);
static void gst_nvvconv_finalize (GObject * object);
static GstStateChangeReturn gst_nvvconv_change_state (GstElement * element,
    GstStateChange transition);
static GstFlowReturn gst_nvvconv_transform (GstBaseTransform * btrans,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_nvvconv_set_caps (GstBaseTransform * btrans,
    GstCaps * incaps, GstCaps * outcaps);
static GstCaps *gst_nvvconv_transform_caps (GstBaseTransform * btrans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_nvvconv_accept_caps (GstBaseTransform * btrans,
    GstPadDirection direction, GstCaps * caps);
static gboolean gst_nvvconv_transform_size (GstBaseTransform * btrans,
    GstPadDirection direction, GstCaps * caps, gsize size,
    GstCaps * othercaps, gsize * othersize);
static gboolean gst_nvvconv_get_unit_size (GstBaseTransform * btrans,
    GstCaps * caps, gsize * size);
static GstCaps *gst_nvvconv_fixate_caps (GstBaseTransform * btrans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean gst_nvvconv_decide_allocation (GstBaseTransform * btrans,
    GstQuery * query);

static void gst_nvvconv_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_nvvconv_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

#define GST_NV_FILTER_MEMORY_TYPE "nvfilter"

/* NvFilter memory allocator Implementation */

typedef struct _GstNvFilterMemory GstNvFilterMemory;
typedef struct _GstNvFilterMemoryAllocator GstNvFilterMemoryAllocator;
typedef struct _GstNvFilterMemoryAllocatorClass GstNvFilterMemoryAllocatorClass;

struct _GstNvFilterMemory
{
  GstMemory mem;
  GstNvvConvBuffer *buf;
};

struct _GstNvFilterMemoryAllocator
{
  GstAllocator parent;
};

struct _GstNvFilterMemoryAllocatorClass
{
  GstAllocatorClass parent_class;
};

/**
  * implementation that acquire memory.
  *
  * @param allocator : gst memory allocatot object
  * @param size      : memory size
  * @param params    : allovcation params
  */
static GstMemory *
gst_nv_filter_memory_allocator_alloc_dummy (GstAllocator * allocator,
    gsize size, GstAllocationParams * params)
{
  g_assert_not_reached ();
  return NULL;
}

/**
  * implementation that releases memory.
  *
  * @param allocator : gst memory allocatot object
  * @param mem       : gst memory
  */
static void
gst_nv_filter_memory_allocator_free (GstAllocator * allocator, GstMemory * mem)
{
  gint ret = 0;
  GstNvFilterMemory *omem = (GstNvFilterMemory *) mem;
  GstNvvConvBuffer *nvbuf = omem->buf;

  ret = NvBufSurfaceDestroy (nvbuf->surface);
  if (ret != 0) {
    GST_ERROR ("%s: NvBufSurfaceDestroy Failed \n", __func__);
    goto error;
  }

error:
  g_slice_free (GstNvvConvBuffer, nvbuf);
  g_slice_free (GstNvFilterMemory, omem);
}

/**
  * memory map function.
  *
  * @param mem     : gst memory
  * @param maxsize : memory max size
  * @param flags   : Flags for wrapped memory
  */
static gpointer
gst_nv_filter_memory_map (GstMemory * mem, gsize maxsize, GstMapFlags flags)
{
  GstNvFilterMemory *omem = (GstNvFilterMemory *) mem;

  if (!omem) {
    g_print ("%s: GstNvFilterMemory object ptr is NULL\n", __func__);
    return NULL;
  }

  return (gpointer)(omem->buf->surface);
}

/**
  * memory unmap function.
  *
  * @param mem : gst memory
  */
static void
gst_nv_filter_memory_unmap (GstMemory * mem)
{
}

/**
  * memory share function.
  *
  * @param mem : gst memory
  */
static GstMemory *
gst_nv_filter_memory_share (GstMemory * mem, gssize offset, gssize size)
{
  g_assert_not_reached ();
  return NULL;
}

GType gst_nv_filter_memory_allocator_get_type (void);
G_DEFINE_TYPE (GstNvFilterMemoryAllocator, gst_nv_filter_memory_allocator,
    GST_TYPE_ALLOCATOR);

#define GST_TYPE_NV_FILTER_MEMORY_ALLOCATOR   (gst_nv_filter_memory_allocator_get_type())
#define GST_IS_NV_FILTER_MEMORY_ALLOCATOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_NV_FILTER_MEMORY_ALLOCATOR))

/**
  * initialize the nvfilter allocator's class.
  *
  * @param klass : nvfilter memory allocator objectclass
  */
static void
gst_nv_filter_memory_allocator_class_init (GstNvFilterMemoryAllocatorClass *
    klass)
{
  GstAllocatorClass *allocator_class;

  allocator_class = GST_ALLOCATOR_CLASS (klass);

  allocator_class->alloc = gst_nv_filter_memory_allocator_alloc_dummy;
  allocator_class->free = gst_nv_filter_memory_allocator_free;
}

/**
  * nvfilter allocator init function.
  *
  * @param allocator : nvfilter allocator object instance
  */
static void
gst_nv_filter_memory_allocator_init (GstNvFilterMemoryAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = GST_NV_FILTER_MEMORY_TYPE;
  alloc->mem_map = gst_nv_filter_memory_map;
  alloc->mem_unmap = gst_nv_filter_memory_unmap;
  alloc->mem_share = gst_nv_filter_memory_share;

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

/**
  * custom memory allocation.
  *
  * @param allocator : nvfilter bufferpool allocator
  * @param flags     : Flags for wrapped memory
  * @param space     : nvvidconv object instance
  */
static GstMemory *
gst_nv_filter_memory_allocator_alloc (GstAllocator * allocator,
    GstMemoryFlags flags, Gstnvvconv * space)
{
  gint ret = 0;
  GstNvFilterMemory *mem = NULL;
  GstNvvConvBuffer *nvbuf = NULL;

  NvBufSurfaceAllocateParams input_params = {0};

  mem = g_slice_new0 (GstNvFilterMemory);
  nvbuf = g_slice_new0 (GstNvvConvBuffer);

  input_params.params.width = space->to_width;
  input_params.params.height = space->to_height;
  /*
   * GPU supports only pitch linear memory for output
   */
  if (space->compute_hw == NvBufSurfTransformCompute_GPU) {
    input_params.params.layout = NVBUF_LAYOUT_PITCH;
  }
  else {
    if (space->enable_blocklinear_output &&
       (space->out_pix_fmt == NVBUF_COLOR_FORMAT_NV12 ||
        space->out_pix_fmt == NVBUF_COLOR_FORMAT_NV12_10LE))
      input_params.params.layout = NVBUF_LAYOUT_BLOCK_LINEAR;
    else
      input_params.params.layout = NVBUF_LAYOUT_PITCH;
  }

  input_params.params.colorFormat = space->out_pix_fmt;
  /*
   * GPU supports CUDA memories as well as surface array
   * memory through EGL mapping where as VIC only supports
   * surface array memory
   */
  if (space->compute_hw == NvBufSurfTransformCompute_GPU) {
    input_params.params.memType = space->nvbuf_mem_type;
    input_params.params.gpuId = space->gpu_id;
  } else {
    if (space->nvbuf_mem_type == NVBUF_MEM_CUDA_DEVICE ||
        space->nvbuf_mem_type == NVBUF_MEM_CUDA_PINNED) {
      GST_DEBUG ("VIC don't support CUDA memory so taking surface array memory");
    }
    input_params.params.memType = NVBUF_MEM_SURFACE_ARRAY;
  }
  input_params.memtag = NvBufSurfaceTag_VIDEO_CONVERT;
  if (space->interlaced_flag)
    input_params.displayscanformat = NVBUF_DISPLAYSCANFORMAT_INTERLACED;

  ret = NvBufSurfaceAllocate(&nvbuf->surface, 1, &input_params);
  if (ret != 0) {
    GST_ERROR ("%s: NvBufSurfaceAllocate Failed \n", __func__);
    goto error;
  }
  nvbuf->surface->numFilled = 1;
  nvbuf->dmabuf_fd = nvbuf->surface->surfaceList[0].bufferDesc;

  flags |= GST_MEMORY_FLAG_NO_SHARE;

  /* Check for init params */
  gst_memory_init (GST_MEMORY_CAST (mem), flags, allocator, NULL,
      sizeof(NvBufSurface), 0 /* Alignment */,
      0, sizeof(NvBufSurface));
  mem->buf = nvbuf;
  mem->buf->dmabuf_fd = nvbuf->dmabuf_fd;

  return GST_MEMORY_CAST (mem);

error:
  g_slice_free (GstNvvConvBuffer, nvbuf);
  g_slice_free (GstNvFilterMemory, mem);

  return NULL;
}

/* nvfilter Buffer Pool for nvmm buffers */

GQuark gst_nv_filter_data_quark = 0;
typedef struct _GstNvFilterBufferPool GstNvFilterBufferPool;
typedef struct _GstNvFilterBufferPoolClass GstNvFilterBufferPoolClass;
#define GST_NV_FILTER_BUFFER_POOL(pool)  ((GstNvFilterBufferPool *) pool)

struct _GstNvFilterBufferPool
{
  GstBufferPool parent;

  GstElement *element;

  GstCaps *caps;
  gboolean add_videometa;
  GstVideoInfo video_info;

  GstAllocator *allocator;

  guint current_buffer_index;
};

struct _GstNvFilterBufferPoolClass
{
  GstBufferPoolClass parent_class;
};

GType gst_nv_filter_buffer_pool_get_type (void);

G_DEFINE_TYPE (GstNvFilterBufferPool, gst_nv_filter_buffer_pool,
    GST_TYPE_BUFFER_POOL);

#define GST_TYPE_NV_FILTER_BUFFER_POOL (gst_nv_filter_buffer_pool_get_type())

/**
  * object class finallize.
  *
  * @param object : object
  */
static void
gst_nv_filter_buffer_pool_finalize (GObject * object)
{
  GstNvFilterBufferPool *pool = GST_NV_FILTER_BUFFER_POOL (object);

  if (pool->element)
    gst_object_unref (pool->element);
  pool->element = NULL;

  if (pool->allocator)
    gst_object_unref (pool->allocator);
  pool->allocator = NULL;

  if (pool->caps)
    gst_caps_unref (pool->caps);
  pool->caps = NULL;

  G_OBJECT_CLASS (gst_nv_filter_buffer_pool_parent_class)->finalize (object);
}

/**
  * start the bufferpool.
  *
  * @param bpool : nvfilter bufferpool object
  */
static gboolean
gst_nv_filter_buffer_pool_start (GstBufferPool * bpool)
{
  GstNvFilterBufferPool *pool = GST_NV_FILTER_BUFFER_POOL (bpool);

  GST_DEBUG_OBJECT (pool, "start");

  GST_OBJECT_LOCK (pool);
  /* Start the pool only if we have component attached to it. */
  if (!pool->element) {
    GST_OBJECT_UNLOCK (pool);
    return FALSE;
  }
  GST_OBJECT_UNLOCK (pool);

  return
      GST_BUFFER_POOL_CLASS (gst_nv_filter_buffer_pool_parent_class)->start
      (bpool);
}

/**
  * stop the bufferpool.
  *
  * @param bpool : nvfilter bufferpool object
  */
static gboolean
gst_nv_filter_buffer_pool_stop (GstBufferPool * bpool)
{
  GstNvFilterBufferPool *pool = GST_NV_FILTER_BUFFER_POOL (bpool);

  GST_DEBUG_OBJECT (pool, "stop");

  if (pool->caps)
    gst_caps_unref (pool->caps);

  pool->caps = NULL;
  pool->add_videometa = FALSE;

  return
      GST_BUFFER_POOL_CLASS (gst_nv_filter_buffer_pool_parent_class)->stop
      (bpool);
}


/**
  * get a list of options supported by this pool.
  *
  * @param bpool : nvfilter bufferpool object
  */
static const gchar **
gst_nv_filter_buffer_pool_get_options (GstBufferPool * bpool)
{
  static const gchar *video_options[] =
      { GST_BUFFER_POOL_OPTION_VIDEO_META, NULL };

   /* Currently, we are only providing VIDEO_META option by default. */

  return video_options;
}

/**
  * apply the bufferpool configuration.
  *
  * @param bpool  : nvfilter bufferpool object
  * @param config : config parameters
  */
static gboolean
gst_nv_filter_buffer_pool_set_config (GstBufferPool * bpool,
    GstStructure * config)
{
  GstNvFilterBufferPool *pool = GST_NV_FILTER_BUFFER_POOL (bpool);
  GstCaps *caps;

  GST_DEBUG_OBJECT (pool, "set_config");

  GST_OBJECT_LOCK (pool);

  if (!gst_buffer_pool_config_get_params (config, &caps, NULL, NULL, NULL))
    goto wrong_config;

  if (caps == NULL)
    goto no_caps;

  GstVideoInfo info;

  /* now parse the caps from the config */
  if (!gst_video_info_from_caps (&info, caps))
    goto wrong_video_caps;

  /* enable metadata based on config of the pool */
  pool->add_videometa =
      gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

  pool->video_info = info;

  if (pool->caps)
    gst_caps_unref (pool->caps);
  pool->caps = gst_caps_ref (caps);

  GST_OBJECT_UNLOCK (pool);

  return
      GST_BUFFER_POOL_CLASS (gst_nv_filter_buffer_pool_parent_class)->set_config
      (bpool, config);

  /* ERRORS */
wrong_config:
  {
    GST_OBJECT_UNLOCK (pool);
    GST_WARNING_OBJECT (pool, "invalid config");
    return FALSE;
  }
no_caps:
  {
    GST_OBJECT_UNLOCK (pool);
    GST_WARNING_OBJECT (pool, "no caps in config");
    return FALSE;
  }
wrong_video_caps:
  {
    GST_OBJECT_UNLOCK (pool);
    GST_WARNING_OBJECT (pool,
        "failed getting geometry from caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
}

/**
  * allocate a buffer.
  *
  * @param bpool  : nvfilter bufferpool object
  * @param buffer : GstBuffer of pool
  * @param params : pool acquire parameters
  */
static GstFlowReturn
gst_nv_filter_buffer_pool_alloc_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstNvFilterBufferPool *pool = GST_NV_FILTER_BUFFER_POOL (bpool);
  Gstnvvconv *space = GST_NVVCONV (pool->element);
  GstBuffer *buf = NULL;
  GstMemory *mem = NULL;

  GST_DEBUG_OBJECT (pool, "alloc_buffer");

  mem = gst_nv_filter_memory_allocator_alloc (pool->allocator, 0, space);
  g_return_val_if_fail (mem, GST_FLOW_ERROR);

  buf = gst_buffer_new ();
  gst_buffer_append_memory (buf, mem);

  if (pool->add_videometa) {
    /* TODO : Add video meta to buffer */
  }

  gst_mini_object_set_qdata (GST_MINI_OBJECT_CAST (mem),
      gst_nv_filter_data_quark, buf, NULL);

  *buffer = buf;

  pool->current_buffer_index++;

  return GST_FLOW_OK;
}


/**
  * free a buffer.
  *
  * @param bpool  : nvfilter bufferpool object
  * @param buffer : GstBuffer of pool
  */
static void
gst_nv_filter_buffer_pool_free_buffer (GstBufferPool * bpool,
    GstBuffer * buffer)
{
  GstMemory *memory;
  GstNvFilterBufferPool *pool = GST_NV_FILTER_BUFFER_POOL (bpool);

  GST_DEBUG_OBJECT (pool, "free_buffer");

  memory = gst_buffer_peek_memory (buffer, 0);

  gst_mini_object_set_qdata (GST_MINI_OBJECT_CAST (memory),
      gst_nv_filter_data_quark, NULL, NULL);

  GST_BUFFER_POOL_CLASS (gst_nv_filter_buffer_pool_parent_class)->free_buffer
      (bpool, buffer);
}

/**
  * get a new buffer from the nvfilter bufferpool.
  *
  * @param bpool  : nvfilter bufferpool object
  * @param buffer : GstBuffer of pool
  * @param params : pool acquire parameters
  */
static GstFlowReturn
gst_nv_filter_buffer_pool_acquire_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstFlowReturn ret;
  GstNvFilterBufferPool *pool = GST_NV_FILTER_BUFFER_POOL (bpool);

  GST_DEBUG_OBJECT (pool, "acquire_buffer");

  ret =
      GST_BUFFER_POOL_CLASS (gst_nv_filter_buffer_pool_parent_class)->
      acquire_buffer (bpool, buffer, params);

  return ret;
}

/**
  * release a buffer back in the nvfilter bufferpool.
  *
  * @param bpool  : nvfilter bufferpool object
  * @param buffer : GstBuffer of pool
  */
static void
gst_nv_filter_buffer_pool_release_buffer (GstBufferPool * bpool,
    GstBuffer * buffer)
{
  GstNvFilterBufferPool *pool = GST_NV_FILTER_BUFFER_POOL (bpool);

  GST_DEBUG_OBJECT (pool, "release_buffer");

  GST_BUFFER_POOL_CLASS (gst_nv_filter_buffer_pool_parent_class)->release_buffer
      (bpool, buffer);
}

/**
  * initialize the nvfilter bufferpool's class.
  *
  * @param klass : nvfilter bufferpool objectclass
  */
static void
gst_nv_filter_buffer_pool_class_init (GstNvFilterBufferPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

  gst_nv_filter_data_quark =
      g_quark_from_static_string ("GstNvFilterBufferData");

  gobject_class->finalize = gst_nv_filter_buffer_pool_finalize;
  gstbufferpool_class->start = gst_nv_filter_buffer_pool_start;
  gstbufferpool_class->stop = gst_nv_filter_buffer_pool_stop;
  gstbufferpool_class->get_options = gst_nv_filter_buffer_pool_get_options;
  gstbufferpool_class->set_config = gst_nv_filter_buffer_pool_set_config;
  gstbufferpool_class->alloc_buffer = gst_nv_filter_buffer_pool_alloc_buffer;
  gstbufferpool_class->free_buffer = gst_nv_filter_buffer_pool_free_buffer;
  gstbufferpool_class->acquire_buffer =
      gst_nv_filter_buffer_pool_acquire_buffer;
  gstbufferpool_class->release_buffer =
      gst_nv_filter_buffer_pool_release_buffer;
}

/**
  * nvfilter bufferpool init function.
  *
  * @param pool : nvfilter bufferpool object instance
  */
static void
gst_nv_filter_buffer_pool_init (GstNvFilterBufferPool * pool)
{
  pool->allocator =
      g_object_new (gst_nv_filter_memory_allocator_get_type (), NULL);
  pool->current_buffer_index = 0;
}

/**
  * Create nvfilter bufferpool object instance.
  *
  * @param element : GstElement object instance
  */
static GstBufferPool *
gst_nv_filter_buffer_pool_new (GstElement * element)
{
  GstNvFilterBufferPool *pool;

  pool = g_object_new (GST_TYPE_NV_FILTER_BUFFER_POOL, NULL);
  pool->element = gst_object_ref (element);

  return GST_BUFFER_POOL (pool);
}

/**
  * copies the given caps.
  *
  * @param caps : given pad caps
  */
static GstCaps *
gst_nvvconv_caps_remove_format_info (GstCaps * caps)
{
  GstStructure *str;
  GstCapsFeatures *features;
  gint i, n;
  GstCaps *ret;

  ret = gst_caps_new_empty ();

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    str = gst_caps_get_structure (caps, i);
    features = gst_caps_get_features (caps, i);

    /* If this is already expressed by the existing caps
     * skip this structure */
    if (i > 0 && gst_caps_is_subset_structure_full (ret, str, features))
      continue;

    str = gst_structure_copy (str);
    /* Only remove format info for the cases when we can actually convert */
    {
      gst_structure_remove_fields (str, "format", "colorimetry", "chroma-site",
          NULL);

      gst_structure_set (str, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

      /* if pixel aspect ratio, make a range */
      if (gst_structure_has_field (str, "pixel-aspect-ratio")) {
        gst_structure_set (str, "pixel-aspect-ratio",
            GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1, NULL);
      }
    }
    gst_caps_append_structure_full (ret, str,
        gst_caps_features_copy (features));
  }

  return ret;
}

/**
  * Determine pixel format.
  *
  * @param info        : Information describing frame properties
  * @param pix_fmt     : pixel format
  * @param isurf_count : intermediate surface count
  */
static gboolean
gst_nvvconv_get_pix_fmt (GstVideoInfo * info,
    NvBufSurfaceColorFormat * pix_fmt, gint * isurf_count)
{
  gboolean ret = TRUE;

  if (GST_VIDEO_INFO_IS_YUV (info)) {
    switch (GST_VIDEO_FORMAT_INFO_FORMAT (info->finfo)) {
      case GST_VIDEO_FORMAT_I420:
        *pix_fmt = NVBUF_COLOR_FORMAT_YUV420;
        break;
      case GST_VIDEO_FORMAT_UYVY:
        *pix_fmt = NVBUF_COLOR_FORMAT_UYVY;
        *isurf_count = 1;
        break;
      case GST_VIDEO_FORMAT_YUY2:
        *pix_fmt = NVBUF_COLOR_FORMAT_YUYV;
        *isurf_count = 1;
        break;
      case GST_VIDEO_FORMAT_Y42B:
        *pix_fmt = NVBUF_COLOR_FORMAT_YUV422;
        *isurf_count=3;
        break;
      case GST_VIDEO_FORMAT_YVYU:
        *pix_fmt = NVBUF_COLOR_FORMAT_YVYU;
        *isurf_count = 1;
        break;
      case GST_VIDEO_FORMAT_NV12:
        *pix_fmt = NVBUF_COLOR_FORMAT_NV12;
        *isurf_count = 2;
        break;
      case GST_VIDEO_FORMAT_NV16:
        *pix_fmt = NVBUF_COLOR_FORMAT_NV16;
        *isurf_count = 2;
        break;
      case GST_VIDEO_FORMAT_NV24:
        *pix_fmt = NVBUF_COLOR_FORMAT_NV24;
        *isurf_count = 2;
        break;
      case GST_VIDEO_FORMAT_I420_10LE:
      case GST_VIDEO_FORMAT_P010_10LE:
        *pix_fmt = NVBUF_COLOR_FORMAT_NV12_10LE;
        *isurf_count = 2;
        break;
      case GST_VIDEO_FORMAT_I420_12LE:
        *pix_fmt = NVBUF_COLOR_FORMAT_NV12_12LE;
        *isurf_count = 2;
        break;
      case GST_VIDEO_FORMAT_Y444:
        *pix_fmt = NVBUF_COLOR_FORMAT_YUV444;
        *isurf_count = 3;
        break;
      default:
        ret = FALSE;
        break;
    }
  } else if (GST_VIDEO_INFO_IS_GRAY (info)) {
    switch (GST_VIDEO_FORMAT_INFO_BITS (info->finfo)) {
      case 8:
        *pix_fmt = NVBUF_COLOR_FORMAT_GRAY8;
        *isurf_count = 1;
        break;
      default:
        ret = FALSE;
        break;
    }
  } else if (GST_VIDEO_INFO_IS_RGB (info)) {
    switch (GST_VIDEO_FORMAT_INFO_FORMAT (info->finfo)) {
      case GST_VIDEO_FORMAT_BGRx:
        *pix_fmt = NVBUF_COLOR_FORMAT_BGRx;
        *isurf_count = 1;
        break;
      case GST_VIDEO_FORMAT_RGBA:
        *pix_fmt = NVBUF_COLOR_FORMAT_RGBA;
        *isurf_count = 1;
        break;
      default:
        ret = FALSE;
        break;
    }
  }

  return ret;
}

/**
  * Initialize nvvconv instance structure members.
  *
  * @param filter : Gstnvvconv object instance
  */
static void
gst_nvvconv_init_params (Gstnvvconv * filter)
{
  filter->silent = FALSE;
  filter->to_width = 0;
  filter->to_height = 0;
  filter->from_width = 0;
  filter->from_height = 0;
  filter->tsurf_width = 0;
  filter->tsurf_height = 0;

  filter->inbuf_type = BUF_NOT_SUPPORTED;
  filter->inbuf_memtype = BUF_MEM_SW;
  filter->outbuf_memtype = BUF_MEM_SW;

  memset(&filter->src_rect, 0, sizeof(NvBufSurfTransformRect));
  memset(&filter->transform_params, 0, sizeof(NvBufSurfTransformParams));
  filter->in_pix_fmt = NVBUF_COLOR_FORMAT_INVALID;
  filter->out_pix_fmt = NVBUF_COLOR_FORMAT_INVALID;

  filter->do_scaling = FALSE;
  filter->need_intersurf = FALSE;
  filter->isurf_flag = FALSE;
  filter->nvfilterpool = FALSE;

  filter->insurf_count = 0;
  filter->isurf_count = 0;
  filter->tsurf_count = 0;
  filter->ibuf_count = 0;

  filter->silent = FALSE;
  filter->no_dimension = FALSE;
  filter->do_flip = FALSE;
  filter->flip_method = GST_VIDEO_NVFLIP_METHOD_IDENTITY;
  filter->interpolation_method = GST_INTERPOLATION_NEAREST;
  filter->negotiated = FALSE;
  filter->num_output_buf = NVFILTER_MAX_BUF;
  filter->enable_blocklinear_output = TRUE;

  filter->interlaced_flag = FALSE;

  filter->do_cropping = FALSE;
  filter->crop_right = 0;
  filter->crop_left = 0;
  filter->crop_top = 0;
  filter->crop_bottom = 0;

  filter->compute_hw = NvBufSurfTransformCompute_Default;
  filter->gpu_id = 0;
  filter->nvbuf_mem_type = NVBUF_MEM_DEFAULT;

  filter->sinkcaps =
      gst_static_pad_template_get_caps (&gst_nvvconv_sink_template);
  filter->srccaps =
      gst_static_pad_template_get_caps (&gst_nvvconv_src_template);

  g_mutex_init (&filter->flow_lock);
}

/**
  * clear the chroma
  *
  * @param filter    : Gstnvvconv object instance
  * @param dmabuf_fd : process buffer fd
  */
static gboolean
gst_nvvconv_do_clearchroma (Gstnvvconv * filter, NvBufSurface *surface)
{
  gint ret = 0;
  guint i, size;

  for (i = 1; i < filter->tsurf_count; i++) {
    ret = NvBufSurfaceMap (surface, 0, i, NVBUF_MAP_READ_WRITE);
    if (ret != 0) {
      g_print ("%s: NvBufSurfaceMap Failed \n", __func__);
      return FALSE;
    }

    ret = NvBufSurfaceSyncForCpu (surface, 0, i);
    if (ret != 0) {
      g_print ("%s: NvBufSurfaceSyncForCpu Failed \n", __func__);
      return FALSE;
    }

    size = surface->surfaceList[0].planeParams.height[i] * surface->surfaceList[0].planeParams.pitch[i];
    memset (surface->surfaceList[0].mappedAddr.addr[i], 0x80, size);

    ret = NvBufSurfaceSyncForDevice (surface, 0, i);
    if (ret != 0) {
      GST_ERROR ("%s: NvBufSurfaceSyncForDevice Failed \n", __func__);
      return FALSE;
    }

    ret = NvBufSurfaceUnMap (surface, 0, i);
    if (ret != 0) {
      g_print ("%s: NvBufSurfaceUnMap Failed \n", __func__);
      return FALSE;
    }
  }

  return TRUE;
}

/* GObject vmethod implementations */

/**
  * initialize the nvvconv's class.
  *
  * @param klass : Gstnvvconv objectclass
  */
static void
gst_nvvconv_class_init (GstnvvconvClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *gstbasetransform_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasetransform_class = (GstBaseTransformClass *) klass;

  gparent_class = g_type_class_peek_parent (gstbasetransform_class);

  gobject_class->set_property = gst_nvvconv_set_property;
  gobject_class->get_property = gst_nvvconv_get_property;
  gobject_class->finalize = gst_nvvconv_finalize;

  gstelement_class->change_state = gst_nvvconv_change_state;

  gstbasetransform_class->set_caps = GST_DEBUG_FUNCPTR (gst_nvvconv_set_caps);
  gstbasetransform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_nvvconv_transform_caps);
  gstbasetransform_class->accept_caps =
      GST_DEBUG_FUNCPTR (gst_nvvconv_accept_caps);
  gstbasetransform_class->transform_size =
      GST_DEBUG_FUNCPTR (gst_nvvconv_transform_size);
  gstbasetransform_class->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_nvvconv_get_unit_size);
  gstbasetransform_class->transform = GST_DEBUG_FUNCPTR (gst_nvvconv_transform);
  gstbasetransform_class->start = GST_DEBUG_FUNCPTR (gst_nvvconv_start);
  gstbasetransform_class->stop = GST_DEBUG_FUNCPTR (gst_nvvconv_stop);
  gstbasetransform_class->fixate_caps =
      GST_DEBUG_FUNCPTR (gst_nvvconv_fixate_caps);
  gstbasetransform_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_nvvconv_decide_allocation);

  gstbasetransform_class->passthrough_on_same_caps = TRUE;

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_FLIP_METHOD,
      g_param_spec_enum ("flip-method", "Flip-Method", "video flip methods",
          GST_TYPE_VIDEO_NVFLIP_METHOD, PROP_FLIP_METHOD_DEFAULT,
          GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_CONSTRUCT |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_NUM_OUT_BUFS,
      g_param_spec_uint ("output-buffers", "Output-Buffers",
          "number of output buffers",
          1, G_MAXUINT, NVFILTER_MAX_BUF,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));

  g_object_class_install_property (gobject_class, PROP_INTERPOLATION_METHOD,
      g_param_spec_enum ("interpolation-method", "Interpolation-method", "Set interpolation methods",
          GST_TYPE_INTERPOLATION_METHOD, GST_INTERPOLATION_NEAREST,
          GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_CONSTRUCT |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LEFT,
      g_param_spec_int ("left", "left", "Pixels to crop at left",
          0, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RIGHT,
      g_param_spec_int ("right", "right", "Pixels to crop at right",
          0, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TOP,
      g_param_spec_int ("top", "top", "Pixels to crop at top",
          0, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BOTTOM,
      g_param_spec_int ("bottom", "bottom", "Pixels to crop at bottom",
          0, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ENABLE_BLOCKLINEAR_OUTPUT,
      g_param_spec_boolean ("bl-output", " Enable BlockLinear output",
      "Blocklinear output, applicable only for memory:NVMM NV12 format output buffer when compute hw is VIC",
          TRUE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_COMPUTE_HW,
      g_param_spec_enum ("compute-hw", "compute-hw", "Compute Scaling HW",
        GST_TYPE_COMPUTE_HW, NvBufSurfTransformCompute_Default,
        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
        GST_PARAM_CONTROLLABLE | G_PARAM_CONSTRUCT)));

  g_object_class_install_property (gobject_class, PROP_GPU_DEVICE_ID,
      g_param_spec_uint ("gpu-id", "Set GPU Device ID for operation",
        "Set GPU Device ID for operation", 0, G_MAXUINT, 0,
         (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
         GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_NVBUF_MEMORY_TYPE,
      g_param_spec_enum ("nvbuf-memory-type", "Type of NvBufSurface memory allocated",
        "Type of NvBufSurface Memory to be allocated for output buffers when compute hw is GPU and memory is NVMM",
        GST_TYPE_NVBUF_MEMORY_TYPE, NVBUF_MEM_DEFAULT,
        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

  gst_element_class_set_details_simple (gstelement_class,
      "NvVidConv Plugin",
      "Filter/Converter/Video/Scaler",
      "Converts video from one colorspace to another & Resizes",
      "amit pandya <apandya@nvidia.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_nvvconv_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_nvvconv_sink_template));
}

/**
  * initialize nvvconv instance structure.
  *
  * @param filter : Gstnvvconv object instance
  */
static void
gst_nvvconv_init (Gstnvvconv * filter)
{
  gst_nvvconv_init_params (filter);
}

static void
get_NvBufferTransform(Gstnvvconv * filter)
{
  switch (filter->flip_method)
  {
    case GST_VIDEO_NVFLIP_METHOD_IDENTITY:
        filter->transform_params.transform_flip = NvBufSurfTransform_None;
      break;
    case GST_VIDEO_NVFLIP_METHOD_90L:
        filter->transform_params.transform_flip = NvBufSurfTransform_Rotate90;
      break;
    case GST_VIDEO_NVFLIP_METHOD_180:
        filter->transform_params.transform_flip = NvBufSurfTransform_Rotate180;
      break;
    case GST_VIDEO_NVFLIP_METHOD_90R:
        filter->transform_params.transform_flip = NvBufSurfTransform_Rotate270;
      break;
    case GST_VIDEO_NVFLIP_METHOD_HORIZ:
        filter->transform_params.transform_flip = NvBufSurfTransform_FlipX;
      break;
    case GST_VIDEO_NVFLIP_METHOD_VERT:
        filter->transform_params.transform_flip = NvBufSurfTransform_FlipY;
      break;
    case GST_VIDEO_NVFLIP_METHOD_TRANS:
        filter->transform_params.transform_flip = NvBufSurfTransform_Transpose;
      break;
    case GST_VIDEO_NVFLIP_METHOD_INVTRANS:
        filter->transform_params.transform_flip = NvBufSurfTransform_InvTranspose;
      break;
    default:
      break;
  }
}

static void
get_NvBufferTransform_filter(Gstnvvconv * filter)
{
  switch(filter->interpolation_method)
  {
    case GST_INTERPOLATION_NEAREST:
        filter->transform_params.transform_filter = NvBufSurfTransformInter_Nearest;
      break;
    case GST_INTERPOLATION_BILINEAR:
        filter->transform_params.transform_filter = NvBufSurfTransformInter_Bilinear;
      break;
    case GST_INTERPOLATION_5_TAP:
        filter->transform_params.transform_filter = NvBufSurfTransformInter_Algo1;
      break;
    case GST_INTERPOLATION_10_TAP:
        filter->transform_params.transform_filter = NvBufSurfTransformInter_Algo2;
      break;
    case GST_INTERPOLATION_SMART:
        filter->transform_params.transform_filter = NvBufSurfTransformInter_Algo3;
      break;
    case GST_INTERPOLATION_NICEST:
        filter->transform_params.transform_filter = NvBufSurfTransformInter_Algo4;
      break;
    default:
        filter->transform_params.transform_filter = NvBufSurfTransformInter_Algo3;
      break;
  }
}

/**
  * initialize nvvconv instance structure.
  *
  * @param filter : Gstnvvconv object instance
  */
static void
gst_nvvconv_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Gstnvvconv *filter = GST_NVVCONV (object);

  switch (prop_id) {
    case PROP_SILENT:
      filter->silent = g_value_get_boolean (value);
      break;
    case PROP_FLIP_METHOD:
      filter->transform_params.transform_flag |= NVBUFSURF_TRANSFORM_FLIP;
      filter->do_flip = TRUE;
      filter->flip_method = g_value_get_enum (value);
      get_NvBufferTransform (filter);
      gst_base_transform_reconfigure_src (GST_BASE_TRANSFORM(filter));
      break;
    case PROP_NUM_OUT_BUFS:
      filter->num_output_buf = g_value_get_uint (value);
      break;
    case PROP_INTERPOLATION_METHOD:
      filter->transform_params.transform_flag |= NVBUFSURF_TRANSFORM_FILTER;
      filter->interpolation_method = g_value_get_enum (value);
      get_NvBufferTransform_filter (filter);
      break;
    case PROP_LEFT:
      filter->transform_params.transform_flag |= NVBUFSURF_TRANSFORM_CROP_SRC;
      filter->do_cropping = TRUE;
      filter->crop_left = g_value_get_int (value);
      filter->src_rect.left = filter->crop_left;
      filter->transform_params.src_rect = &filter->src_rect;
      break;
    case PROP_RIGHT:
      filter->transform_params.transform_flag |= NVBUFSURF_TRANSFORM_CROP_SRC;
      filter->do_cropping = TRUE;
      filter->crop_right = g_value_get_int (value);
      filter->src_rect.width = (filter->crop_right - filter->crop_left);
      filter->transform_params.src_rect = &filter->src_rect;
      break;
    case PROP_TOP:
      filter->transform_params.transform_flag |= NVBUFSURF_TRANSFORM_CROP_SRC;
      filter->do_cropping = TRUE;
      filter->crop_top = g_value_get_int (value);
      filter->src_rect.top = filter->crop_top;
      filter->transform_params.src_rect = &filter->src_rect;
      break;
    case PROP_BOTTOM:
      filter->transform_params.transform_flag |= NVBUFSURF_TRANSFORM_CROP_SRC;
      filter->do_cropping = TRUE;
      filter->crop_bottom = g_value_get_int (value);
      filter->src_rect.height = (filter->crop_bottom - filter->crop_top);
      filter->transform_params.src_rect = &filter->src_rect;
      break;
    case PROP_ENABLE_BLOCKLINEAR_OUTPUT:
      filter->enable_blocklinear_output = g_value_get_boolean (value);
      break;
    case PROP_COMPUTE_HW:
      filter->compute_hw = g_value_get_enum (value);
      break;
    case PROP_GPU_DEVICE_ID:
      filter->gpu_id = g_value_get_uint (value);
      break;
    case PROP_NVBUF_MEMORY_TYPE:
      filter->nvbuf_mem_type = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
  * initialize nvvconv instance structure.
  *
  * @param filter : Gstnvvconv object instance
  */
static void
gst_nvvconv_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gstnvvconv *filter = GST_NVVCONV (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, filter->silent);
      break;
    case PROP_FLIP_METHOD:
      g_value_set_enum (value, filter->flip_method);
      break;
    case PROP_NUM_OUT_BUFS:
      g_value_set_uint (value, filter->num_output_buf);
      break;
    case PROP_INTERPOLATION_METHOD:
      g_value_set_enum (value, filter->interpolation_method);
      break;
    case PROP_LEFT:
      g_value_set_int (value, filter->crop_left);
      break;
    case PROP_RIGHT:
      g_value_set_int (value, filter->crop_right);
      break;
    case PROP_TOP:
      g_value_set_int (value, filter->crop_top);
      break;
    case PROP_BOTTOM:
      g_value_set_int (value, filter->crop_bottom);
      break;
    case PROP_ENABLE_BLOCKLINEAR_OUTPUT:
      g_value_set_boolean (value, filter->enable_blocklinear_output);
      break;
    case PROP_COMPUTE_HW:
      g_value_set_enum (value, filter->compute_hw);
      break;
    case PROP_GPU_DEVICE_ID:
      g_value_set_uint (value, filter->gpu_id);
      break;
    case PROP_NVBUF_MEMORY_TYPE:
      g_value_set_enum (value, filter->nvbuf_mem_type);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
  * Free all allocated resources(Rmsurface).
  *
  * @param filter : Gstnvvconv object instance
  */
static void
gst_nvvconv_free_buf (Gstnvvconv * filter)
{
  gint ret;

  if (filter->input_interbuf.isurface) {
    ret = NvBufSurfaceDestroy (filter->input_interbuf.isurface);
    if (ret != 0) {
      GST_ERROR ("%s: intermediate NvBufferDestroy Failed \n", __func__);
    }
    filter->input_interbuf.isurface = NULL;
    filter->ibuf_count--;
  }
  if (filter->output_interbuf.isurface) {
    ret = NvBufSurfaceDestroy (filter->output_interbuf.isurface);
    if (ret != 0) {
      GST_ERROR ("%s: intermediate NvBufferDestroy Failed \n", __func__);
    }
    filter->output_interbuf.isurface = NULL;
    filter->ibuf_count--;
  }
  filter->isurf_count = 0;
  filter->ibuf_count = 0;
}

/**
  * nvvidconv element state change function.
  *
  * @param element    : Gstnvvconv element instance
  * @param transition : state transition
  */
static GstStateChangeReturn
gst_nvvconv_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn result = GST_STATE_CHANGE_SUCCESS;
  Gstnvvconv *space;

  space = GST_NVVCONV (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:{
    }
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:{
    }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:{
    }
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:{
    }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:{
    }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:{
    }
      break;
    case GST_STATE_CHANGE_NULL_TO_NULL:{
    }
      break;
    case GST_STATE_CHANGE_READY_TO_READY:{
    }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PAUSED:{
    }
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PLAYING:{
    }
      break;
  }

  GST_ELEMENT_CLASS (gparent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:{
    }
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:{
    }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:{
    }
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:{
    }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:{
      gst_nvvconv_free_buf (space);
    }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:{
    }
      break;
    case GST_STATE_CHANGE_NULL_TO_NULL:{
    }
      break;
    case GST_STATE_CHANGE_READY_TO_READY:{
    }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PAUSED:{
    }
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PLAYING:{
    }
      break;
  }

  return result;
}

/**
  * nvvidconv element finalize function.
  *
  * @param object : element object instance
  */
static void
gst_nvvconv_finalize (GObject * object)
{
  Gstnvvconv *filter;

  filter = GST_NVVCONV(object);

  if (filter->sinkcaps) {
    gst_caps_unref (filter->sinkcaps);
    filter->sinkcaps = NULL;
  }

  if (filter->srccaps) {
    gst_caps_unref (filter->srccaps);
    filter->sinkcaps = NULL;
  }

  g_mutex_clear (&filter->flow_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* GstBaseTransform vmethod implementations */

/**
  * notified of the actual caps set.
  *
  * @param btrans  : basetransform  object instance
  * @param incaps  : input capabilities
  * @param outcaps : output capabilities
  */
static gboolean
gst_nvvconv_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  gboolean ret = TRUE;
  Gstnvvconv *space;
  gint from_dar_n, from_dar_d, to_dar_n, to_dar_d;
  GstVideoInfo in_info, out_info;
  GstBufferPool *newpool, *oldpool;
  GstStructure *config;
  gint min, surf_count = 0;
  GstCapsFeatures *ift = NULL;
  GstCapsFeatures *oft = NULL;

  space = GST_NVVCONV (btrans);

  /* input caps */
  if (!gst_video_info_from_caps (&in_info, incaps))
    goto invalid_caps;

  /* output caps */
  if (!gst_video_info_from_caps (&out_info, outcaps))
    goto invalid_caps;

  space->in_info = in_info;
  space->out_info = out_info;

  space->from_width = GST_VIDEO_INFO_WIDTH (&in_info);
  space->from_height = GST_VIDEO_INFO_HEIGHT (&in_info);

  space->to_width = GST_VIDEO_INFO_WIDTH (&out_info);
  space->to_height = GST_VIDEO_INFO_HEIGHT (&out_info);

  if ((space->from_width != space->to_width)
      || (space->from_height != space->to_height))
    space->do_scaling = TRUE;

  /* get input pixel format */
  ret =
      gst_nvvconv_get_pix_fmt (&in_info, &space->in_pix_fmt,
      &surf_count);
  if (ret != TRUE)
    goto invalid_pix_fmt;

  if (space->compute_hw == NvBufSurfTransformCompute_GPU) {
    if ((space->in_pix_fmt == NVBUF_COLOR_FORMAT_YUV422) ||
        (space->in_pix_fmt == NVBUF_COLOR_FORMAT_NV24) ||
        (space->in_pix_fmt == NVBUF_COLOR_FORMAT_NV16) ||
        (space->in_pix_fmt == NVBUF_COLOR_FORMAT_UYVY) ||
        (space->in_pix_fmt == NVBUF_COLOR_FORMAT_YVYU) ||
        (space->in_pix_fmt == NVBUF_COLOR_FORMAT_YUYV)) {
      g_print ("Transform not supported using GPU for provided input format\n");
      goto invalid_pix_fmt;
    }
  }

  /* get output pixel format */
  ret =
      gst_nvvconv_get_pix_fmt (&out_info, &space->out_pix_fmt,
      &surf_count);
  if (ret != TRUE)
    goto invalid_pix_fmt;

  if (space->compute_hw == NvBufSurfTransformCompute_GPU) {
    if ((space->out_pix_fmt == NVBUF_COLOR_FORMAT_YUV422) ||
        (space->out_pix_fmt == NVBUF_COLOR_FORMAT_NV24) ||
        (space->out_pix_fmt == NVBUF_COLOR_FORMAT_NV16) ||
        (space->out_pix_fmt == NVBUF_COLOR_FORMAT_UYVY) ||
        (space->out_pix_fmt == NVBUF_COLOR_FORMAT_YVYU) ||
        (space->out_pix_fmt == NVBUF_COLOR_FORMAT_YUYV)) {
      g_print ("Transform not supported using GPU for provided output format\n");
      goto invalid_pix_fmt;
    }
  }

  ift = gst_caps_get_features (incaps, 0);
  if (gst_caps_features_contains (ift, GST_CAPS_FEATURE_MEMORY_NVMM))
    space->inbuf_memtype = BUF_MEM_HW;

  oft = gst_caps_get_features (outcaps, 0);
  if (gst_caps_features_contains (oft, GST_CAPS_FEATURE_MEMORY_NVMM))
    space->outbuf_memtype = BUF_MEM_HW;

  if (gst_caps_features_is_equal (ift, oft) &&
      (space->in_pix_fmt == space->out_pix_fmt) &&
      (!space->do_scaling) &&
      (!space->do_cropping) &&
      (!space->flip_method) &&
      (space->enable_blocklinear_output)) {
    /* We are not processing input buffer. Initializations/allocations in this
       function can be skipped */
    gst_base_transform_set_passthrough (btrans, TRUE);
    return TRUE;
  }

  switch (space->in_pix_fmt) {
    case NVBUF_COLOR_FORMAT_YUV420:
    case NVBUF_COLOR_FORMAT_YUV422:
    case NVBUF_COLOR_FORMAT_YUV444:
      space->inbuf_type = BUF_TYPE_YUV;
      space->insurf_count = 3;
      break;
    case NVBUF_COLOR_FORMAT_NV12:
    case NVBUF_COLOR_FORMAT_NV16:
    case NVBUF_COLOR_FORMAT_NV24:
    case NVBUF_COLOR_FORMAT_NV12_10LE:
    case NVBUF_COLOR_FORMAT_NV12_12LE:
      space->inbuf_type = BUF_TYPE_YUV;
      space->insurf_count = 2;
      break;
    case NVBUF_COLOR_FORMAT_UYVY:
    case NVBUF_COLOR_FORMAT_YUYV:
    case NVBUF_COLOR_FORMAT_YVYU:
      space->inbuf_type = BUF_TYPE_YUV;
      space->insurf_count = 1;
      break;
    case NVBUF_COLOR_FORMAT_BGRx:
    case NVBUF_COLOR_FORMAT_RGBA:
      space->inbuf_type = BUF_TYPE_RGB;
      space->insurf_count = 1;
      break;
    case NVBUF_COLOR_FORMAT_GRAY8:
      space->inbuf_type = BUF_TYPE_GRAY;
      space->insurf_count = 1;
      break;
    default:
      goto not_supported_inbuf;
      break;
  }

  min = space->num_output_buf;

  space->tsurf_width = space->to_width;
  space->tsurf_height = space->to_height;

  switch (space->out_pix_fmt) {
    case NVBUF_COLOR_FORMAT_YUV420:
    case NVBUF_COLOR_FORMAT_YUV422:
    case NVBUF_COLOR_FORMAT_YUV444:
      space->tsurf_count = 3;
      break;
    case NVBUF_COLOR_FORMAT_NV12:
    case NVBUF_COLOR_FORMAT_NV16:
    case NVBUF_COLOR_FORMAT_NV24:
    case NVBUF_COLOR_FORMAT_NV12_10LE:
      space->tsurf_count = 2;
      break;
    case NVBUF_COLOR_FORMAT_UYVY:
    case NVBUF_COLOR_FORMAT_YUYV:
    case NVBUF_COLOR_FORMAT_YVYU:
      space->tsurf_count = 1;
      break;
    case NVBUF_COLOR_FORMAT_BGRx:
    case NVBUF_COLOR_FORMAT_RGBA:
    case NVBUF_COLOR_FORMAT_GRAY8:
      space->tsurf_count = 1;
      break;
    default:
      goto not_supported_outbuf;
      break;
  }

  if ((space->in_pix_fmt != space->out_pix_fmt) ||
      (space->do_scaling) ||
      (space->flip_method) ||
      (!gst_caps_features_is_equal (ift, oft)) ||
      (space->do_cropping)) {
    space->need_intersurf = TRUE;
    space->isurf_flag = TRUE;
  }

  if (!gst_util_fraction_multiply (in_info.width,
          in_info.height, in_info.par_n, in_info.par_d, &from_dar_n,
          &from_dar_d)) {
    from_dar_n = from_dar_d = -1;
  }

  if (!gst_util_fraction_multiply (out_info.width,
          out_info.height, out_info.par_n, out_info.par_d, &to_dar_n,
          &to_dar_d)) {
    to_dar_n = to_dar_d = -1;
  }

  if (to_dar_n != from_dar_n || to_dar_d != from_dar_d) {
    GST_WARNING_OBJECT (space, "Cannot keep DAR");
  }

  /* check for outcaps feature */
  ift = gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_NVMM, NULL);
  if (gst_caps_features_is_equal (gst_caps_get_features (outcaps, 0), ift)) {
    space->nvfilterpool = TRUE;
  }
  gst_caps_features_free (ift);

  if (space->nvfilterpool) {
    g_mutex_lock (&space->flow_lock);
    newpool = gst_nv_filter_buffer_pool_new (GST_ELEMENT_CAST (space));

    config = gst_buffer_pool_get_config (newpool);
    gst_buffer_pool_config_set_params (config, outcaps, sizeof (NvBufSurface), min, min);
    gst_buffer_pool_config_set_allocator (config,
        ((GstNvFilterBufferPool *) newpool)->allocator, NULL);
    if (!gst_buffer_pool_set_config (newpool, config))
      goto config_failed;

    oldpool = space->pool;
    space->pool = newpool;

    g_mutex_unlock (&space->flow_lock);

    /* unref the old nvfilter bufferpool */
    if (oldpool) {
      gst_object_unref (oldpool);
    }
  }

  gst_base_transform_set_passthrough (btrans, FALSE);

  GST_DEBUG_OBJECT (space, "from=%dx%d (par=%d/%d dar=%d/%d), size %"
      G_GSIZE_FORMAT " -> to=%dx%d (par=%d/%d dar=%d/%d), "
      "size %" G_GSIZE_FORMAT,
      in_info.width, in_info.height, in_info.par_n, in_info.par_d,
      from_dar_n, from_dar_d, in_info.size, out_info.width,
      out_info.height, out_info.par_n, out_info.par_d, to_dar_n, to_dar_d,
      out_info.size);

  space->negotiated = ret;

  return ret;

  /* ERRORS */
config_failed:
  {
    GST_ERROR ("failed to set config on bufferpool");
    g_mutex_unlock (&space->flow_lock);
    return FALSE;
  }
not_supported_inbuf:
  {
    GST_ERROR ("input buffer type not supported");
    return FALSE;
  }
not_supported_outbuf:
  {
    GST_ERROR ("output buffer type not supported");
    return FALSE;
  }
invalid_pix_fmt:
  {
    GST_ERROR ("could not configure for input/output format");
    space->in_pix_fmt = NVBUF_COLOR_FORMAT_INVALID;
    space->out_pix_fmt = NVBUF_COLOR_FORMAT_INVALID;
    return FALSE;
  }
invalid_caps:
  {
    GST_ERROR ("invalid caps");
    space->negotiated = FALSE;
    return FALSE;
  }
}

/**
  * Open external resources.
  *
  * @param btrans  : basetransform  object instance
  */
static gboolean
gst_nvvconv_start (GstBaseTransform * btrans)
{
  Gstnvvconv *space;

  space = GST_NVVCONV (btrans);

  space->session_created = 0;

  return TRUE;
}

/**
  * Close external resources.
  *
  * @param btrans : basetransform  object instance
  */
static gboolean
gst_nvvconv_stop (GstBaseTransform * btrans)
{
  Gstnvvconv *space;

  space = GST_NVVCONV (btrans);
  if (space->pool) {
    gst_object_unref (space->pool);
    space->pool = NULL;
  }

  if (space->session_created == 1) {
    if (space->config_params.cuda_stream)
      cudaStreamDestroy(space->config_params.cuda_stream);
    space->config_params.cuda_stream = 0;
    space->session_created = 0;
  }

  return TRUE;
}

/**
  * calculate the size in bytes of a buffer on the other pad
  * with the given other caps, output size only depends on the caps,
  * not on the input caps.
  *
  * @param btrans    : basetransform object instance
  * @param direction : pad direction
  * @param caps      : input caps
  * @param size      : input buffer size
  * @param othercaps : other caps
  * @param othersize : otherpad buffer size
  */
static gboolean
gst_nvvconv_transform_size (GstBaseTransform * btrans,
    GstPadDirection direction, GstCaps * caps, gsize size,
    GstCaps * othercaps, gsize * othersize)
{
  gboolean ret = TRUE;
  GstVideoInfo vinfo;

  /* size of input buffer cannot be zero */
  g_assert (size);

  ret = gst_video_info_from_caps (&vinfo, othercaps);
  if (ret) {
    *othersize = vinfo.size;
  }

  GST_DEBUG_OBJECT (btrans, "Othersize %" G_GSIZE_FORMAT " bytes"
      "for othercaps %" GST_PTR_FORMAT, *othersize, othercaps);

  return ret;
}

/**
  * Get the size in bytes of one unit for the given caps.
  *
  * @param btrans : basetransform object instance
  * @param caps   : given caps
  * @param size   : size of one unit
  */
static gboolean
gst_nvvconv_get_unit_size (GstBaseTransform * btrans, GstCaps * caps,
    gsize * size)
{
  gboolean ret = TRUE;
  GstVideoInfo vinfo;

  if (!gst_video_info_from_caps (&vinfo, caps)) {
    GST_WARNING_OBJECT (btrans, "Parsing failed for caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  *size = vinfo.size;

  GST_DEBUG_OBJECT (btrans, "size %" G_GSIZE_FORMAT " bytes"
      "for caps %" GST_PTR_FORMAT, *size, caps);

  return ret;
}

/**
  * Given the pad in direction and the given caps,
  * fixate the caps on the other pad.
  *
  * @param btrans    : basetransform object instance
  * @param direction : pad direction
  * @param caps      : given caps
  * @param othercaps : other caps
  */
static GstCaps *
gst_nvvconv_fixate_caps (GstBaseTransform * btrans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  Gstnvvconv *space;
  gint tt_width = 0, tt_height = 0;
  GstStructure *in_struct, *out_struct;
  const GValue *from_pix_ar, *to_pix_ar;
  const gchar *from_fmt = NULL, *to_fmt = NULL;
  const gchar *from_interlace_mode = NULL;
  const gchar *to_interlace_mode = NULL;
  GValue from_par = { 0, }, to_par = {
  0,};
  gint n, i, index = 0;
  GstCapsFeatures *features = NULL;
  gboolean have_nvfeature = FALSE;

  space = GST_NVVCONV (btrans);
  GstCapsFeatures *ift = NULL;
  ift = gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_NVMM, NULL);

  n = gst_caps_get_size (othercaps);
  for (i = 0; i < n; i++) {
    features = gst_caps_get_features (othercaps, i);
    if (gst_caps_features_is_equal (features, ift)) {
      index = i;
      have_nvfeature = TRUE;
    }
  }
  gst_caps_features_free (ift);

  if (have_nvfeature) {
    while (index > 0) {
      gst_caps_remove_structure (othercaps, 0);
      index--;
    }
  }

  othercaps = gst_caps_truncate (othercaps);
  othercaps = gst_caps_make_writable (othercaps);

  GST_DEBUG_OBJECT (space, "trying to fixate othercaps %" GST_PTR_FORMAT
      " based on caps %" GST_PTR_FORMAT, othercaps, caps);

  in_struct = gst_caps_get_structure (caps, 0);
  out_struct = gst_caps_get_structure (othercaps, 0);

  from_pix_ar = gst_structure_get_value (in_struct, "pixel-aspect-ratio");
  to_pix_ar = gst_structure_get_value (out_struct, "pixel-aspect-ratio");

  from_fmt = gst_structure_get_string (in_struct, "format");
  to_fmt = gst_structure_get_string (out_struct, "format");

  if (!to_fmt) {
    /* Output format not fixed */
    if (!gst_structure_fixate_field_string (out_struct, "format", from_fmt)) {
      GST_ERROR_OBJECT (space, "Failed to fixate output format");
      goto finish;
    }
  }

  if (gst_structure_has_field (out_struct, "interlace-mode")) {
    /* interlace-mode present */
    to_interlace_mode = gst_structure_get_string (out_struct, "interlace-mode");
    if (!to_interlace_mode) {
      /* interlace-mode not fixed */
      space->interlaced_flag = FALSE;
      from_interlace_mode = gst_structure_get_string (in_struct, "interlace-mode");
      if (from_interlace_mode)
        gst_structure_fixate_field_string (out_struct, "interlace-mode", from_interlace_mode);
      else
        gst_structure_fixate_field_string (out_struct, "interlace-mode", "progessive");
    }
    else if (!g_strcmp0 (gst_structure_get_string (out_struct, "interlace-mode"), "interleaved"))
      space->interlaced_flag = TRUE;
  }

  /* If fixating from the sinkpad always set the PAR and
   * assume that missing PAR on the sinkpad means 1/1 and
   * missing PAR on the srcpad means undefined
   */
  if (direction == GST_PAD_SINK) {
    if (!from_pix_ar) {
      g_value_init (&from_par, GST_TYPE_FRACTION);
      gst_value_set_fraction (&from_par, 1, 1);
      from_pix_ar = &from_par;
    }
    if (!to_pix_ar) {
      g_value_init (&to_par, GST_TYPE_FRACTION_RANGE);
      gst_value_set_fraction_range_full (&to_par, 1, G_MAXINT, G_MAXINT, 1);
      to_pix_ar = &to_par;
    }
  } else {
    if (!to_pix_ar) {
      g_value_init (&to_par, GST_TYPE_FRACTION);
      gst_value_set_fraction (&to_par, 1, 1);
      to_pix_ar = &to_par;

      gst_structure_set (out_struct, "pixel-aspect-ratio", GST_TYPE_FRACTION, 1,
          1, NULL);
    }
    if (!from_pix_ar) {
      g_value_init (&from_par, GST_TYPE_FRACTION);
      gst_value_set_fraction (&from_par, 1, 1);
      from_pix_ar = &from_par;
    }
  }

  /* have both PAR but they might not be fixated */
  {
    gint f_width, f_height, f_par_n, f_par_d, t_par_n, t_par_d;
    gint t_width = 0, t_height = 0;
    gint f_dar_n, f_dar_d;
    gint numerator, denominator;

    /* from_pix_ar should be fixed */
    g_return_val_if_fail (gst_value_is_fixed (from_pix_ar), othercaps);

    f_par_n = gst_value_get_fraction_numerator (from_pix_ar);
    f_par_d = gst_value_get_fraction_denominator (from_pix_ar);

    gst_structure_get_int (in_struct, "width", &f_width);
    gst_structure_get_int (in_struct, "height", &f_height);

    gst_structure_get_int (out_struct, "width", &t_width);
    gst_structure_get_int (out_struct, "height", &t_height);

    /* if both width and height are already fixed, can't do anything
     * about it anymore */
    if (t_width && t_height) {
      guint num, den;

      GST_DEBUG_OBJECT (space, "dimensions already set to %dx%d, not fixating",
          t_width, t_height);
      if (!gst_value_is_fixed (to_pix_ar)) {
        if (gst_video_calculate_display_ratio (&num, &den, f_width, f_height,
                f_par_n, f_par_d, t_width, t_height)) {
          GST_DEBUG_OBJECT (space, "fixating to_pix_ar to %dx%d", num, den);
          if (gst_structure_has_field (out_struct, "pixel-aspect-ratio")) {
            gst_structure_fixate_field_nearest_fraction (out_struct,
                "pixel-aspect-ratio", num, den);
          } else if (num != den) {
            gst_structure_set (out_struct, "pixel-aspect-ratio",
                GST_TYPE_FRACTION, num, den, NULL);
          }
        }
      }
      goto finish;
    }

    /* Calc input DAR */
    if (!gst_util_fraction_multiply (f_width, f_height, f_par_n, f_par_d,
            &f_dar_n, &f_dar_d)) {
      GST_ERROR_OBJECT (space, "calculation of the output"
          "scaled size error");
      goto finish;
    }

    GST_DEBUG_OBJECT (space, "Input DAR: %d / %d", f_dar_n, f_dar_d);

    /* If either w or h are fixed either except choose a height or
     * width and PAR that matches the DAR as near as possible
     */
    if (t_width) {
      /* width is already fixed */
      gint set_par_n;
      gint set_par_d;

      gint s_height = 0;
      GstStructure *tmp_struct = NULL;

      /* Choose the height nearest to
       * height with same DAR, as PAR is fixed */
      if (gst_value_is_fixed (to_pix_ar)) {
        /* get PAR denominator */
        t_par_d = gst_value_get_fraction_denominator (to_pix_ar);
        /* get PAR numerator */
        t_par_n = gst_value_get_fraction_numerator (to_pix_ar);

        if (!gst_util_fraction_multiply (f_dar_n, f_dar_d,
                t_par_d, t_par_n,
                &numerator, &denominator)) {
          GST_ERROR_OBJECT (space, "calculation of the output"
              "scaled size error");
          goto finish;
        }

        /* calc height */
        t_height = (guint) gst_util_uint64_scale_int (t_width,
                   denominator, numerator);
        /* set height */
        gst_structure_fixate_field_nearest_int (out_struct, "height", t_height);

        goto finish;
      }

      /* The PAR is not fixed set arbitrary PAR. */

      /* can keep the input height check  */
      tmp_struct = gst_structure_copy (out_struct);
      gst_structure_fixate_field_nearest_int (tmp_struct, "height", f_height);
      gst_structure_get_int (tmp_struct, "height", &s_height);

      /* May failed but try to keep the DAR however by
       * adjusting the PAR */
      if (!gst_util_fraction_multiply (f_dar_n, f_dar_d, s_height, t_width,
              &t_par_n, &t_par_d)) {
        GST_ERROR_OBJECT (space, "calculation of the output"
            "scaled size error");
        gst_structure_free (tmp_struct);
        goto finish;
      }

      if (!gst_structure_has_field (tmp_struct, "pixel-aspect-ratio")) {
        gst_structure_set_value (tmp_struct, "pixel-aspect-ratio", to_pix_ar);
      }

      /* set fixate PAR */
      if (gst_structure_fixate_field_nearest_fraction (tmp_struct,
            "pixel-aspect-ratio",
            t_par_n, t_par_d)) {
        /* get PAR */
        if (gst_structure_get_field_type (tmp_struct, "pixel-aspect-ratio") !=
            G_TYPE_INVALID) {
          if(!gst_structure_get_fraction (tmp_struct,
              "pixel-aspect-ratio",
              &set_par_n, &set_par_d))
            GST_ERROR_OBJECT (space, "PAR values set failed");
        }
        /* values set correctly */
        if (tmp_struct) {
          gst_structure_free (tmp_struct);
          tmp_struct = NULL;
        }
      }

      if (set_par_n == t_par_n) {
        if (set_par_d == t_par_d) {
          /* Check for PAR field */
          if (gst_structure_has_field (out_struct, "pixel-aspect-ratio") ||
              !(set_par_n == set_par_d)) {
            /* set height & PAR */
            gst_structure_set (out_struct,
                "height", G_TYPE_INT, s_height,
                "pixel-aspect-ratio", GST_TYPE_FRACTION,
                set_par_n, set_par_d,
                NULL);
          }
          goto finish;
        }
      }

      if (!gst_util_fraction_multiply (f_dar_n, f_dar_d,
           set_par_d, set_par_n,
           &numerator, &denominator)) {
        GST_ERROR_OBJECT (space, "calculation of the output"
            "scaled size error");
        goto finish;
      }

      /* Calc height */
      t_height = (guint) gst_util_uint64_scale_int (t_width,
                    denominator, numerator);
      /* Set height */
      gst_structure_fixate_field_nearest_int (out_struct, "height", t_height);

      /* If struct has field PAR then set PAR */
      if (gst_structure_has_field (out_struct, "pixel-aspect-ratio") ||
          !(set_par_n == set_par_d)) {
        /* set PAR */
        gst_structure_set (out_struct, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d,
            NULL);
      }
      goto finish;
    } else if (t_height) {
      /* height is already fixed */
      gint set_par_n;
      gint set_par_d;

      gint s_width = 0;
      GstStructure *tmp_struct = NULL;

      /* Choose the width nearest to the
       * width with same DAR, as PAR is fixed */
      if (gst_value_is_fixed (to_pix_ar)) {
        /* get PAR denominator */
        t_par_d = gst_value_get_fraction_denominator (to_pix_ar);
        /* get PAR numerator */
        t_par_n = gst_value_get_fraction_numerator (to_pix_ar);

        if (!gst_util_fraction_multiply (f_dar_n, f_dar_d, t_par_d,
                t_par_n, &numerator, &denominator)) {
          GST_ERROR_OBJECT (space, "calculation of the output"
              "scaled size error");
          goto finish;
        }

        /* calc width */
        t_width =
            (guint) gst_util_uint64_scale_int (t_height, numerator,
            denominator);
        /* set width */
        gst_structure_fixate_field_nearest_int (out_struct, "width", t_width);

        goto finish;
      }

      /* PAR is not fixed set arbitrary PAR */

      tmp_struct = gst_structure_copy (out_struct);
      gst_structure_fixate_field_nearest_int (tmp_struct, "width", f_width);
      gst_structure_get_int (tmp_struct, "width", &s_width);

      /* May failed but try to keep the DAR however by
       * adjusting the PAR */
      if (!gst_util_fraction_multiply (f_dar_n, f_dar_d, t_height, s_width,
              &t_par_n, &t_par_d)) {
        GST_ERROR_OBJECT (space, "calculation of the output"
            "scaled size error");
        gst_structure_free (tmp_struct);
        goto finish;
      }

      if (!gst_structure_has_field (tmp_struct, "pixel-aspect-ratio")) {
        gst_structure_set_value (tmp_struct, "pixel-aspect-ratio", to_pix_ar);
      }

      /* set fixate PAR */
      if (gst_structure_fixate_field_nearest_fraction (tmp_struct,
            "pixel-aspect-ratio",
            t_par_n, t_par_d)) {
        if (gst_structure_get_field_type (tmp_struct, "pixel-aspect-ratio") !=
            G_TYPE_INVALID) {
          /* get PAR */
          if (!gst_structure_get_fraction (tmp_struct,
               "pixel-aspect-ratio",
               &set_par_n, &set_par_d))
            GST_ERROR_OBJECT (space, "PAR values set failed");
        }
        /* values set correctly */
        if (tmp_struct) {
          gst_structure_free (tmp_struct);
          tmp_struct = NULL;
        }
      }

      if (set_par_n == t_par_n) {
        if (set_par_d == t_par_d) {
          /* check for PAR field */
          if (gst_structure_has_field (out_struct, "pixel-aspect-ratio") ||
              !(set_par_n == set_par_d)) {
            /* set width & PAR */
            gst_structure_set (out_struct,
                "width", G_TYPE_INT, s_width,
                "pixel-aspect-ratio", GST_TYPE_FRACTION,
                set_par_n, set_par_d,
                NULL);
          }
          goto finish;
        }
      }

      if (!gst_util_fraction_multiply (f_dar_n, f_dar_d,
              set_par_d, set_par_n,
              &numerator, &denominator)) {
        GST_ERROR_OBJECT (space, "calculation of the output"
            "scaled size error");
        goto finish;
      }

      /* Calc width */
      t_width = (guint) gst_util_uint64_scale_int (t_height,
                 numerator, denominator);

      /* Set width */
      gst_structure_fixate_field_nearest_int (out_struct, "width", t_width);

      /* If struct has field PAR then set PAR */
      if (gst_structure_has_field (out_struct, "pixel-aspect-ratio") ||
          !(set_par_n == set_par_d)) {
        /* set PAR*/
        gst_structure_set (out_struct, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d,
            NULL);
      }

      goto finish;
    } else if (gst_value_is_fixed (to_pix_ar)) {

      gint s_height = 0;
      gint s_width = 0;
      gint from_hight = 0;
      gint from_width = 0;
      GstStructure *tmp_struct = NULL;

      /* Get PAR denominator */
      t_par_d = gst_value_get_fraction_denominator (to_pix_ar);
      /* Get PAR numerator */
      t_par_n = gst_value_get_fraction_numerator (to_pix_ar);

      /* find scale factor for change in PAR */
      if (!gst_util_fraction_multiply (f_dar_n, f_dar_d,
              t_par_n, t_par_d,
              &numerator, &denominator)) {
        GST_ERROR_OBJECT (space, "calculation of the output"
            "scaled size error");
        goto finish;
      }

      tmp_struct = gst_structure_copy (out_struct);

      gst_structure_fixate_field_nearest_int (tmp_struct, "height", f_height);
      gst_structure_get_int (tmp_struct, "height", &s_height);

      /* This may failed but however scale the width to keep DAR */
      t_width =
          (guint) gst_util_uint64_scale_int (s_height, numerator, denominator);
      gst_structure_fixate_field_nearest_int (tmp_struct, "width", t_width);
      gst_structure_get_int (tmp_struct, "width", &s_width);
      gst_structure_free (tmp_struct);

      /* kept DAR and the height is nearest to the original height */
      if (s_width == t_width) {
        gst_structure_set (out_struct, "width", G_TYPE_INT, s_width, "height",
            G_TYPE_INT, s_height, NULL);
        goto finish;
      }

      from_hight = s_height;
      from_width = s_width;

      /* If former failed, try to keep the input width at least */
      tmp_struct = gst_structure_copy (out_struct);
      gst_structure_fixate_field_nearest_int (tmp_struct, "width", f_width);
      gst_structure_get_int (tmp_struct, "width", &s_width);

      /* This may failed but however try to scale the width to keep DAR */
      t_height =
          (guint) gst_util_uint64_scale_int (s_width, denominator, numerator);
      gst_structure_fixate_field_nearest_int (tmp_struct, "height", t_height);
      gst_structure_get_int (tmp_struct, "height", &s_height);
      gst_structure_free (tmp_struct);

      /* We kept the DAR and the width is nearest to the original width */
      if (s_height == t_height) {
        gst_structure_set (out_struct, "width", G_TYPE_INT, s_width, "height",
            G_TYPE_INT, s_height, NULL);
        goto finish;
      }

      /* If all failed, keep the height that nearest to the orignal
       * height and the nearest possible width.
       */
      gst_structure_set (out_struct, "width", G_TYPE_INT, from_width, "height",
          G_TYPE_INT, from_hight, NULL);
      goto finish;
    } else {
      gint tmp_struct2;
      gint set_par_n;
      gint set_par_d;
      gint s_height = 0;
      gint s_width = 0;
      GstStructure *tmp_struct = NULL;

      /* width, height and PAR are not fixed though passthrough impossible */

      /* keep height and width as fine as possible & scale PAR */
      tmp_struct = gst_structure_copy (out_struct);

      if (gst_structure_fixate_field_nearest_int (tmp_struct, "height", f_height))
        gst_structure_get_int (tmp_struct, "height", &s_height);

      if (gst_structure_fixate_field_nearest_int (tmp_struct, "width", f_width))
        gst_structure_get_int (tmp_struct, "width", &s_width);

      if (!gst_util_fraction_multiply (f_dar_n, f_dar_d,
              s_height, s_width,
              &t_par_n, &t_par_d)) {
        GST_ERROR_OBJECT (space, "calculation of the output"
            "scaled size error");
        goto finish;
      }

      if (!gst_structure_has_field (tmp_struct, "pixel-aspect-ratio")) {
        gst_structure_set_value (tmp_struct, "pixel-aspect-ratio", to_pix_ar);
      }

      if (gst_structure_fixate_field_nearest_fraction (tmp_struct, "pixel-aspect-ratio",
          t_par_n, t_par_d)) {
        gst_structure_get_fraction (tmp_struct, "pixel-aspect-ratio",
            &set_par_n, &set_par_d);
      }
      gst_structure_free (tmp_struct);

      if (set_par_n == t_par_n) {
        if (set_par_d == t_par_d) {
          gst_structure_set (out_struct,
              "width", G_TYPE_INT, s_width,
              "height", G_TYPE_INT, s_height,
              NULL);

          if (gst_structure_has_field (out_struct, "pixel-aspect-ratio") ||
              !(set_par_n == set_par_d))
            gst_structure_set (out_struct, "pixel-aspect-ratio", GST_TYPE_FRACTION,
                set_par_n, set_par_d,
                NULL);
          space->no_dimension = TRUE;
          goto finish;
        }
      }

      /* Or scale width to keep the DAR with the set
       * PAR and height */
      if (!gst_util_fraction_multiply (f_dar_n, f_dar_d,
              set_par_d, set_par_n,
              &numerator, &denominator)) {
        GST_ERROR_OBJECT (space, "calculation of the output"
            "scaled size error");
        goto finish;
      }

      t_width =
          (guint) gst_util_uint64_scale_int (s_height, numerator, denominator);
      tmp_struct = gst_structure_copy (out_struct);

      if (gst_structure_fixate_field_nearest_int (tmp_struct, "width", t_width)) {
        gst_structure_get_int (tmp_struct, "width", &tmp_struct2);
      }
      gst_structure_free (tmp_struct);

      if (tmp_struct2 == t_width) {
        gst_structure_set (out_struct,
            "width", G_TYPE_INT, tmp_struct2,
            "height", G_TYPE_INT, s_height,
            NULL);
        if (gst_structure_has_field (out_struct, "pixel-aspect-ratio")
            || (set_par_n != set_par_d))
          gst_structure_set (out_struct, "pixel-aspect-ratio",
              GST_TYPE_FRACTION, set_par_n, set_par_d,
              NULL);
        space->no_dimension = TRUE;
        goto finish;
      }

      t_height =
          (guint) gst_util_uint64_scale_int (s_width, denominator, numerator);
      tmp_struct = gst_structure_copy (out_struct);

      if (gst_structure_fixate_field_nearest_int (tmp_struct, "height", t_height)) {
        gst_structure_get_int (tmp_struct, "height", &tmp_struct2);
      }
      gst_structure_free (tmp_struct);

      if (tmp_struct2 == t_height) {
        gst_structure_set (out_struct,
            "width", G_TYPE_INT, s_width,
            "height", G_TYPE_INT, tmp_struct2,
            NULL);
        if (gst_structure_has_field (out_struct, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (out_struct, "pixel-aspect-ratio",
              GST_TYPE_FRACTION, set_par_n, set_par_d,
              NULL);
        space->no_dimension = TRUE;
        goto finish;
      }

      /* If all failed can't keep DAR & take nearest values for all */
      gst_structure_set (out_struct,
          "width", G_TYPE_INT, s_width,
          "height", G_TYPE_INT, s_height,
          NULL);
      if (gst_structure_has_field (out_struct, "pixel-aspect-ratio") ||
          (set_par_n != set_par_d))
        gst_structure_set (out_struct, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d,
            NULL);
      space->no_dimension = TRUE;
    }
  }

finish:
  if (space->no_dimension && space->do_flip) {
    switch (space->flip_method) {
      case GST_VIDEO_NVFLIP_METHOD_90R:
      case GST_VIDEO_NVFLIP_METHOD_90L:
      case GST_VIDEO_NVFLIP_METHOD_INVTRANS:
      case GST_VIDEO_NVFLIP_METHOD_TRANS:
        if (gst_structure_get_int (out_struct, "width", &tt_width) &&
            gst_structure_get_int (out_struct, "height", &tt_height)) {
          gst_structure_set (out_struct, "width", G_TYPE_INT, tt_height,
            "height", G_TYPE_INT, tt_width, NULL);
        }
        break;
      case GST_VIDEO_NVFLIP_METHOD_IDENTITY:
      case GST_VIDEO_NVFLIP_METHOD_180:
      case GST_VIDEO_NVFLIP_METHOD_HORIZ:
      case GST_VIDEO_NVFLIP_METHOD_VERT:
        break;
      default:
        g_assert_not_reached ();
        break;
    }
  }

  GST_DEBUG_OBJECT (space, "fixated othercaps to %" GST_PTR_FORMAT, othercaps);

  if (from_pix_ar == &from_par)
    g_value_unset (&from_par);
  if (to_pix_ar == &to_par)
    g_value_unset (&to_par);

  return othercaps;
}

/**
  * Given the pad in direction and the given caps,
  * provide allowed caps on the other pad.
  *
  * @param btrans    : basetransform object instance
  * @param direction : pad direction
  * @param caps      : given caps
  * @param filter    : other caps
  */
static GstCaps *
gst_nvvconv_transform_caps (GstBaseTransform * btrans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *ret = NULL;
  GstCaps *tmp1, *tmp2;
  GstCapsFeatures *features = NULL;

  GST_DEBUG_OBJECT (btrans,
      "Transforming caps %" GST_PTR_FORMAT " in direction %s", caps,
      (direction == GST_PAD_SINK) ? "sink" : "src");

  /* Get all possible caps that we can transform into */
  tmp1 = gst_nvvconv_caps_remove_format_info (caps);

  if (filter) {
    if (direction == GST_PAD_SRC) {
      GstCapsFeatures *ift = NULL;
      ift = gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_NVMM, NULL);
      features = gst_caps_get_features (filter, 0);
      if (!gst_caps_features_is_equal (features, ift)) {
        gint n, i;
        GstCapsFeatures *tft;
        n = gst_caps_get_size (tmp1);
        for (i = 0; i < n; i++) {
          tft = gst_caps_get_features (tmp1, i);
          if (gst_caps_features_get_size (tft))
            gst_caps_features_remove (tft, GST_CAPS_FEATURE_MEMORY_NVMM);
        }
      }
      gst_caps_features_free (ift);
    }

    tmp2 = gst_caps_intersect_full (filter, tmp1, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp1);
    tmp1 = tmp2;
  }

  if (gst_caps_is_empty(tmp1)) {
    ret = gst_caps_copy(filter);
    gst_caps_unref (tmp1);
  } else {
    ret = tmp1;
  }

  if (!filter) {
    GstStructure *str;
    str = gst_structure_copy (gst_caps_get_structure (ret, 0));

    GstCapsFeatures *ift;
    ift = gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_NVMM, NULL);

    gst_caps_append_structure_full (ret, str, ift);

    str = gst_structure_copy (gst_caps_get_structure (ret, 0));
    gst_caps_append_structure_full (ret, str, NULL);
  }

  GST_DEBUG_OBJECT (btrans, "transformed %" GST_PTR_FORMAT " into %"
      GST_PTR_FORMAT, caps, ret);

  return ret;
}

/**
  * check if caps can be handled by the element.
  *
  * @param btrans    : basetransform object instance
  * @param direction : pad direction
  * @param caps      : given caps
  */
static gboolean
gst_nvvconv_accept_caps (GstBaseTransform * btrans,
    GstPadDirection direction, GstCaps * caps)
{
  gboolean ret = TRUE;
  Gstnvvconv *space = NULL;
  GstCaps *allowed = NULL;

  space = GST_NVVCONV (btrans);

  GST_DEBUG_OBJECT (btrans, "accept caps %" GST_PTR_FORMAT, caps);

  /* get all the formats we can handle on this pad */
  if (direction == GST_PAD_SINK)
    allowed = space->sinkcaps;
  else
    allowed = space->srccaps;

  if (!allowed) {
    GST_DEBUG_OBJECT (btrans, "failed to get allowed caps");
    goto no_transform_possible;
  }

  GST_DEBUG_OBJECT (btrans, "allowed caps %" GST_PTR_FORMAT, allowed);

  /* intersect with the requested format */
  ret = gst_caps_is_subset (caps, allowed);
  if (!ret) {
    goto no_transform_possible;
  }

done:
  return ret;

  /* ERRORS */
no_transform_possible:
  {
    GST_DEBUG_OBJECT (btrans,
        "could not transform %" GST_PTR_FORMAT " in anything we support", caps);
    ret = FALSE;
    goto done;
  }
}

/**
  * Setup the allocation parameters for allocating output buffers.
  *
  * @param btrans : basetransform object instance
  * @param query  : downstream allocation query
  */
static gboolean
gst_nvvconv_decide_allocation (GstBaseTransform * btrans, GstQuery * query)
{
  guint j, metas_no;
  Gstnvvconv *space = NULL;
  GstCaps *outcaps = NULL;
  GstCaps *myoutcaps = NULL;
  GstBufferPool *pool = NULL;
  guint size, minimum, maximum;
  GstAllocator *allocator = NULL;
  GstAllocationParams params = { 0, 0, 0, 0 };
  GstStructure *config = NULL;
  GstVideoInfo info;
  gboolean modify_allocator;

  space = GST_NVVCONV (btrans);

  metas_no = gst_query_get_n_allocation_metas (query);
  for (j = 0; j < metas_no; j++) {
    gboolean remove_meta;
    GType meta_api;
    const GstStructure *param_str = NULL;

    meta_api = gst_query_parse_nth_allocation_meta (query, j, &param_str);

    if (gst_meta_api_type_has_tag (meta_api, GST_META_TAG_MEMORY)) {
      /* Different memory will get allocated for input and output.
         remove all memory dependent metadata */
      GST_DEBUG_OBJECT (space, "remove memory specific metadata %s",
          g_type_name (meta_api));
      remove_meta = TRUE;
    } else {
      /* Default remove all metadata */
      GST_DEBUG_OBJECT (space, "remove metadata %s", g_type_name (meta_api));
      remove_meta = TRUE;
    }

    if (remove_meta) {
      gst_query_remove_nth_allocation_meta (query, j);
      j--;
      metas_no--;
    }
  }

  gst_query_parse_allocation (query, &outcaps, NULL);
  if (outcaps == NULL)
    goto no_caps;

  /* Use nvfilter custom buffer pool */
  if (space->nvfilterpool) {
    g_mutex_lock (&space->flow_lock);
    pool = space->pool;
    if (pool)
      gst_object_ref (pool);
    g_mutex_unlock (&space->flow_lock);

    if (pool != NULL) {
      config = gst_buffer_pool_get_config (pool);
      gst_buffer_pool_config_get_params (config, &myoutcaps, &size, NULL, NULL);

      GST_DEBUG_OBJECT (space, "we have a pool with caps %" GST_PTR_FORMAT,
          myoutcaps);

      if (!gst_caps_is_equal (outcaps, myoutcaps)) {
        /* different caps, we can't use current pool */
        GST_DEBUG_OBJECT (space, "pool has different caps");
        gst_object_unref (pool);
        pool = NULL;
      }
      gst_structure_free (config);
    }

    if (pool == NULL) {
      if (!gst_video_info_from_caps (&info, outcaps))
        goto invalid_caps;

      size = info.size;
      minimum = space->num_output_buf;

      GST_DEBUG_OBJECT (space, "create new pool");

      g_mutex_lock (&space->flow_lock);
      pool = gst_nv_filter_buffer_pool_new (GST_ELEMENT_CAST (space));

      config = gst_buffer_pool_get_config (pool);
      gst_buffer_pool_config_set_params (config, outcaps,  sizeof (NvBufSurface), minimum, minimum);
      gst_buffer_pool_config_set_allocator (config,
          ((GstNvFilterBufferPool *) pool)->allocator, NULL);
      if (!gst_buffer_pool_set_config (pool, config))
        goto config_failed;

      space->pool = gst_object_ref (pool);

      g_mutex_unlock (&space->flow_lock);
    }

    if (pool) {
      config = gst_buffer_pool_get_config (pool);
      gst_buffer_pool_config_get_allocator (config, &allocator, &params);
      gst_buffer_pool_config_get_params (config, &myoutcaps, &size, &minimum, &maximum);

      /* Add check, params may be empty e.g. fakesink */
      if (gst_query_get_n_allocation_params (query) > 0) {
        /* Set allocation params */
        gst_query_set_nth_allocation_param (query, 0, allocator, &params);
      } else {
        /* Add allocation params */
        gst_query_add_allocation_param (query, allocator, &params);
      }

      /* Set allocation pool */
      if (gst_query_get_n_allocation_pools (query) > 0) {
        gst_query_set_nth_allocation_pool (query, 0, pool, size, minimum, maximum);
      } else {
        gst_query_add_allocation_pool (query, pool, size, minimum, maximum);
      }

      gst_structure_free (config);
      gst_object_unref (pool);
    }
  } else {
    /* Use oss buffer pool */
    if (gst_query_get_n_allocation_params (query) > 0) {
      /* Get allocation params */
      gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
      modify_allocator = TRUE;
    } else {
      allocator = NULL;
      gst_allocation_params_init (&params);
      modify_allocator = FALSE;
    }

    if (gst_query_get_n_allocation_pools (query) > 0) {
      /* Parse pool to get size, min & max  */
      gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &minimum, &maximum);
      if (pool == NULL) {
        GST_DEBUG_OBJECT (btrans, "no pool available, creating new oss pool");
        pool = gst_buffer_pool_new ();
      }
    } else {
      pool = NULL;
      size = 0;
      minimum = 0;
      maximum = 0;
    }

    if (pool) {
      config = gst_buffer_pool_get_config (pool);
      /* Set params on config */
      gst_buffer_pool_config_set_params (config, outcaps, size, minimum, maximum);
      /* Set allocator on config */
      gst_buffer_pool_config_set_allocator (config, allocator, &params);
      /* Set config on pool */
      gst_buffer_pool_set_config (pool, config);
    }

    if (modify_allocator) {
      /* Set allocation params */
      gst_query_set_nth_allocation_param (query, 0, allocator, &params);
    } else {
      /* Add allocation params */
      gst_query_add_allocation_param (query, allocator, &params);
    }

    if (allocator) {
      gst_object_unref (allocator);
    }

    if (pool) {
      gst_query_set_nth_allocation_pool (query, 0, pool, size, minimum, maximum);
      gst_object_unref (pool);
    }
  }

  return TRUE;
/* ERROR */
no_caps:
  {
    GST_ERROR ("no caps specified");
    return FALSE;
  }
invalid_caps:
  {
    GST_ERROR ("invalid caps specified");
    return FALSE;
  }
config_failed:
  {
    GST_ERROR ("failed to set config on bufferpool");
    g_mutex_unlock (&space->flow_lock);
    return FALSE;
  }
}

static gint
get_bytes_per_pix_from_color (NvBufSurfaceColorFormat pix_fmt, gint plane_id)
{
  gint bytes_per_pix = 1;
  switch (pix_fmt) {
    case NVBUF_COLOR_FORMAT_YUV420:
    case NVBUF_COLOR_FORMAT_YUV422:
    case NVBUF_COLOR_FORMAT_YUV420_709:
    case NVBUF_COLOR_FORMAT_YUV420_709_ER:
    case NVBUF_COLOR_FORMAT_YUV420_ER:
    case NVBUF_COLOR_FORMAT_YUV444:
      bytes_per_pix = 1;
      break;
    case NVBUF_COLOR_FORMAT_NV12:
    case NVBUF_COLOR_FORMAT_NV12_709:
    case NVBUF_COLOR_FORMAT_NV12_709_ER:
    case NVBUF_COLOR_FORMAT_NV12_ER:
    case NVBUF_COLOR_FORMAT_NV21:
    case NVBUF_COLOR_FORMAT_NV16:
    case NVBUF_COLOR_FORMAT_NV24:
      if (plane_id == 0)
        bytes_per_pix = 1;
      else
        bytes_per_pix = 2;
      break;
    case NVBUF_COLOR_FORMAT_NV12_12LE:
    case NVBUF_COLOR_FORMAT_NV12_10LE:
    case NVBUF_COLOR_FORMAT_NV12_10LE_709:
    case NVBUF_COLOR_FORMAT_NV12_10LE_2020:
    case NVBUF_COLOR_FORMAT_NV12_10LE_ER:
    case NVBUF_COLOR_FORMAT_NV12_10LE_709_ER:
      if (plane_id == 0)
        bytes_per_pix = 2;
      else
        bytes_per_pix = 4;
      break;
    case NVBUF_COLOR_FORMAT_UYVY:
    case NVBUF_COLOR_FORMAT_YUYV:
    case NVBUF_COLOR_FORMAT_YVYU:
      bytes_per_pix = 2;
      break;
    case NVBUF_COLOR_FORMAT_BGRx:
    case NVBUF_COLOR_FORMAT_RGBA:
      bytes_per_pix = 4;
      break;
    case NVBUF_COLOR_FORMAT_BGR:
    case NVBUF_COLOR_FORMAT_RGB:
      bytes_per_pix = 3;
      break;
    case NVBUF_COLOR_FORMAT_GRAY8:
    case NVBUF_COLOR_FORMAT_R8_G8_B8:
    case NVBUF_COLOR_FORMAT_B8_G8_R8:
      bytes_per_pix = 1;
      break;
    case NVBUF_COLOR_FORMAT_R32F_G32F_B32F:
    case NVBUF_COLOR_FORMAT_B32F_G32F_R32F:
      bytes_per_pix = sizeof (float);
      break;
    default:
      break;
  }

  return bytes_per_pix;
}

static void
surface_list_init (Gstnvvconv * space, NvBufSurface * surf,
    GstMapInfo * surf_list_map, gboolean flag)
{
  gpointer surf_list_data = surf_list_map->data;
  guint bytesPerPixel;
  gsize surf_list_size = surf_list_map->size;

  surf->gpuId = space->gpu_id;
  surf->batchSize = 1;
  surf->numFilled = 1;
  surf->memType = NVBUF_MEM_SYSTEM;
  GstVideoInfo *surf_list_info = NULL;
  /* Flag is set as 1 if input surface needs to be populated and to 0 if output
   * surface has to be populated.
   */
  if (flag) {
    bytesPerPixel = get_bytes_per_pix_from_color (space->in_pix_fmt, 0);
    surf->surfaceList->planeParams.num_planes = space->in_info.finfo->n_planes;
    surf->surfaceList->pitch = space->in_info.stride[0];
    surf->surfaceList->colorFormat = space->in_pix_fmt;
    surf->surfaceList->planeParams.offset[0] = space->in_info.offset[0];
    surf->surfaceList->width = space->from_width;
    surf->surfaceList->height = space->from_height;
    surf->surfaceList->planeParams.width[0] = space->from_width;
    surf->surfaceList->planeParams.height[0] = space->from_height;
    surf->surfaceList->planeParams.psize[0] =
        space->from_height * surf->surfaceList->pitch;
    surf_list_info = &(space->in_info);
  } else {
    bytesPerPixel = get_bytes_per_pix_from_color (space->out_pix_fmt, 0);
    surf->surfaceList->planeParams.num_planes = space->out_info.finfo->n_planes;
    surf->surfaceList->pitch = space->out_info.stride[0];
    surf->surfaceList->colorFormat = space->out_pix_fmt;
    surf->surfaceList->planeParams.offset[0] = space->out_info.offset[0];
    surf->surfaceList->width = space->to_width;
    surf->surfaceList->height = space->to_height;
    surf->surfaceList->planeParams.width[0] = space->to_width;
    surf->surfaceList->planeParams.height[0] = space->to_height;
    surf->surfaceList->planeParams.psize[0] =
        space->to_height * surf->surfaceList->pitch;
    surf_list_info = &(space->out_info);
  }
  surf->surfaceList->dataSize = surf_list_size; // size of allocated hw mem
  surf->surfaceList->dataPtr = surf_list_data;
  surf->surfaceList->layout = NVBUF_LAYOUT_PITCH;
  surf->surfaceList->planeParams.pitch[0] = surf->surfaceList->pitch;
  surf->surfaceList->planeParams.bytesPerPix[0] = bytesPerPixel;

  for (uint32_t j = 1; j < surf_list_info->finfo->n_planes; j++) {
    guint comp_width = GST_VIDEO_INFO_COMP_WIDTH (surf_list_info, j);
    guint comp_height = GST_VIDEO_INFO_COMP_HEIGHT (surf_list_info, j);
    guint comp_pitch = surf_list_info->stride[j];
    surf->surfaceList->planeParams.height[j] = comp_height;
    surf->surfaceList->planeParams.pitch[j] = comp_pitch;
    surf->surfaceList->planeParams.offset[j] = surf_list_info->offset[j];
    surf->surfaceList->planeParams.psize[j] = comp_pitch * comp_height;
    bytesPerPixel =
        get_bytes_per_pix_from_color (surf->surfaceList->colorFormat, j);
    surf->surfaceList->planeParams.width[j] = comp_width;
    surf->surfaceList->planeParams.bytesPerPix[j] = bytesPerPixel;
  }
}

/**
  * Transforms one incoming buffer to one outgoing buffer.
  *
  * @param inbuf  : input buffer
  * @param outbuf : output buffer
  */
static GstFlowReturn
gst_nvvconv_transform (GstBaseTransform * btrans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  gint retn = 0;
  gboolean ret = TRUE;
  GstFlowReturn flow_ret = GST_FLOW_OK;

  Gstnvvconv *space = NULL;

  GstMemory *inmem = NULL;
  GstMemory *outmem = NULL;
  GstNvFilterMemory *omem = NULL;

  GstMapInfo inmap = GST_MAP_INFO_INIT;
  GstMapInfo outmap = GST_MAP_INFO_INIT;

  NvBufSurfaceAllocateParams input_params = {0};

  gpointer data = NULL;

  space = GST_NVVCONV (btrans);
  char context[100];
  sprintf(context,"gst_nvvconv_transform()_ctx=%p",space);
  NvBufSurfTransform_Error err = NvBufSurfTransformError_Success;
  cudaError_t CUerr = cudaSuccess;

  if (G_UNLIKELY (!space->negotiated))
    goto unknown_format;

  inmem = gst_buffer_peek_memory (inbuf, 0);
  if (!inmem)
    goto no_memory;

  outmem = gst_buffer_peek_memory (outbuf, 0);
  if (!outmem)
    goto no_memory;
  omem = (GstNvFilterMemory *) outmem;

  if (!gst_buffer_map (inbuf, &inmap, GST_MAP_READ))
    goto invalid_inbuf;

  if (!gst_buffer_map (outbuf, &outmap, GST_MAP_WRITE))
    goto invalid_outbuf;

  if (!gst_buffer_copy_into (outbuf, inbuf, GST_BUFFER_COPY_META, 0, -1)) {
    GST_DEBUG ("Buffer metadata copy failed \n");
  }

  if (space->compute_hw == NvBufSurfTransformCompute_GPU) {
    CUerr = cudaSetDevice (space->gpu_id);
    if (CUerr != cudaSuccess) {
      g_print ("Unable to set cuda device\n");
      goto cuda_errors;
    }
  }

  data = gst_mini_object_get_qdata ((GstMiniObject *)inbuf, g_quark_from_static_string("NV_BUF"));

  if(data == (gpointer)NVBUF_MAGIC_NUM)
  {
    space->inbuf_memtype = BUF_MEM_HW;
  }

  if (space->session_created == 0)
  {
    space->config_params.compute_mode = space->compute_hw;
    if (space->compute_hw == NvBufSurfTransformCompute_GPU) {
      space->config_params.gpu_id = space->gpu_id;
      cudaStreamCreateWithFlags (&(space->config_params.cuda_stream), cudaStreamNonBlocking);
    }
    space->session_created = 1;
  }

  err = NvBufSurfTransformSetSessionParams (&space->config_params);
  if (err != NvBufSurfTransformError_Success) {
    g_print ("Set session params failed \n");
    flow_ret = GST_FLOW_ERROR;
    goto done;
  }

  switch (space->inbuf_type) {
    case BUF_TYPE_YUV:
    case BUF_TYPE_GRAY:
    case BUF_TYPE_RGB:
      if ((space->in_pix_fmt == NVBUF_COLOR_FORMAT_GRAY8) &&
          ((space->out_pix_fmt != NVBUF_COLOR_FORMAT_YUV420) &&
           (space->out_pix_fmt != NVBUF_COLOR_FORMAT_GRAY8))) {
        g_print ("%s: Transform not supported \n", __func__);
        flow_ret = GST_FLOW_ERROR;
        goto done;
      }

      if (space->inbuf_memtype == BUF_MEM_HW && space->outbuf_memtype == BUF_MEM_SW) {
        NvBufSurface *surf = ((NvBufSurface *) inmap.data);
        NvBufSurface op_surf;
        NvBufSurfaceParams surfaceList;
        memset (&surfaceList, 0, sizeof (surfaceList));
        op_surf.surfaceList = &surfaceList;
        surface_list_init (space, &op_surf, &outmap, 0);

        if (space->need_intersurf || space->do_scaling || space->flip_method) {
          if (space->isurf_flag == TRUE && space->ibuf_count < 1) {
            input_params.params.width = GST_ROUND_UP_2 (space->to_width);;
            input_params.params.height = GST_ROUND_UP_2 (space->to_height);;
            input_params.params.layout = NVBUF_LAYOUT_PITCH;
            input_params.params.colorFormat = space->out_pix_fmt;
            if (space->compute_hw == NvBufSurfTransformCompute_GPU) {
              input_params.params.gpuId = space->gpu_id;
              input_params.params.memType = NVBUF_MEM_CUDA_DEVICE;
            } else {
              input_params.params.memType = NVBUF_MEM_SURFACE_ARRAY;
            }
            input_params.memtag = NvBufSurfaceTag_VIDEO_CONVERT;

            retn = NvBufSurfaceAllocate(&space->output_interbuf.isurface, 1, &input_params);
            if (retn != 0) {
              GST_ERROR ("%s: intermediate NvBufSurfaceAllocate Failed \n", __func__);
              flow_ret = GST_FLOW_ERROR;
              goto done;
            }
            space->output_interbuf.isurface->numFilled = 1;
            space->output_interbuf.idmabuf_fd = space->output_interbuf.isurface->surfaceList[0].bufferDesc;

            space->ibuf_count += 1;
            space->isurf_flag = FALSE;
          }

          retn = NvBufSurfTransform (surf, space->output_interbuf.isurface, &space->transform_params);
          if (retn != 0) {
            g_print ("%s: NvBufSurfTransform Failed \n", __func__);
            flow_ret = GST_FLOW_ERROR;
            goto done;
          }

          NvBufSurfaceCopy (space->output_interbuf.isurface, &op_surf);

        } else {
          NvBufSurfaceCopy (surf, &op_surf);
        }
      } else if (space->inbuf_memtype == BUF_MEM_SW && space->outbuf_memtype == BUF_MEM_HW) {
        NvBufSurface *op_surf = (NvBufSurface *) outmap.data;
        NvBufSurface ip_surf;
        NvBufSurfaceParams surfaceList;
        memset (&surfaceList, 0, sizeof (surfaceList));
        ip_surf.surfaceList = &surfaceList;
        surface_list_init (space, &ip_surf, &inmap, 1);

        if (space->need_intersurf || space->do_scaling || space->flip_method) {
          if (space->isurf_flag == TRUE && space->ibuf_count < 1) {
          /* TODO : Check for PayloadInfo.TimeStamp = gst_util_uint64_scale (GST_BUFFER_PTS (inbuf), GST_MSECOND * 10, GST_SECOND); */
            input_params.params.width = GST_ROUND_UP_2 (space->from_width);;
            input_params.params.height = GST_ROUND_UP_2 (space->from_height);;
            input_params.params.layout = NVBUF_LAYOUT_PITCH;
            input_params.params.colorFormat = space->in_pix_fmt;
            if (space->compute_hw == NvBufSurfTransformCompute_GPU) {
              input_params.params.gpuId = space->gpu_id;
              input_params.params.memType = NVBUF_MEM_CUDA_DEVICE;
            } else {
              input_params.params.memType = NVBUF_MEM_SURFACE_ARRAY;
            }
            input_params.memtag = NvBufSurfaceTag_VIDEO_CONVERT;

            retn = NvBufSurfaceAllocate(&space->input_interbuf.isurface, 1, &input_params);
            if (retn != 0) {
              GST_ERROR ("%s: intermediate NvBufSurfaceAllocate Failed \n", __func__);
              flow_ret = GST_FLOW_ERROR;
              goto done;
            }
            space->input_interbuf.isurface->numFilled = 1;
            space->input_interbuf.idmabuf_fd = space->input_interbuf.isurface->surfaceList[0].bufferDesc;

            space->isurf_flag = FALSE;
            space->ibuf_count += 1;
          }

          NvBufSurfaceCopy (&ip_surf, space->input_interbuf.isurface);

          retn = NvBufSurfTransform (space->input_interbuf.isurface, op_surf, &space->transform_params);
          if (retn != 0) {
            g_print ("%s: NvBufSurfTransform Failed \n", __func__);
            flow_ret = GST_FLOW_ERROR;
            goto done;
          }

          /*
           * Ideally this should not be required at application level
           * but keeping it for VIC due to legacy code
           */
          if (space->compute_hw == NvBufSurfTransformCompute_VIC) {
            if ((space->in_pix_fmt == NVBUF_COLOR_FORMAT_GRAY8) &&
                (space->out_pix_fmt == NVBUF_COLOR_FORMAT_YUV420)) {
              ret = gst_nvvconv_do_clearchroma (space, omem->buf->surface);
              if (ret != TRUE) {
                GST_ERROR ("%s: Clear chroma failed \n", __func__);
                flow_ret = GST_FLOW_ERROR;
                goto done;
              }
            }
          }
        } else {
          NvBufSurfaceCopy (&ip_surf, op_surf);
          op_surf->numFilled = 1;
        }
      } else if (space->inbuf_memtype == BUF_MEM_HW && space->outbuf_memtype == BUF_MEM_HW) {
        NvBufSurface *surf = ((NvBufSurface *) inmap.data);
        /* TODO : Check for PayloadInfo.TimeStamp = gst_util_uint64_scale (GST_BUFFER_PTS (inbuf), GST_MSECOND * 10, GST_SECOND); */
        if (space->need_intersurf || space->do_scaling || space->flip_method || 
            surf->surfaceList->layout != omem->buf->surface->surfaceList->layout) {
          retn = NvBufSurfTransform (surf, omem->buf->surface, &space->transform_params);
          if (retn != 0) {
            g_print ("%s: NvBufSurfTransform Failed \n", __func__);
            flow_ret = GST_FLOW_ERROR;
            goto done;
          }

          /*
           * Ideally this should not be required at application level
           * but keeping it for VIC due to legacy code
           */
          if (space->compute_hw == NvBufSurfTransformCompute_VIC) {
            if ((space->in_pix_fmt == NVBUF_COLOR_FORMAT_GRAY8) &&
                (space->out_pix_fmt == NVBUF_COLOR_FORMAT_YUV420)) {
              ret = gst_nvvconv_do_clearchroma (space, omem->buf->surface);
              if (ret != TRUE) {
                GST_ERROR ("%s: Clear chroma failed \n", __func__);
                flow_ret = GST_FLOW_ERROR;
                goto done;
              }
            }
          }
        } else {
          NvBufSurfaceCopy (surf, omem->buf->surface);
        }
      } else if (space->inbuf_memtype == BUF_MEM_SW && space->outbuf_memtype == BUF_MEM_SW) {
        /* input surface initialisations */
        NvBufSurface ip_surf;
        NvBufSurfaceParams surfaceListIp;
        memset (&surfaceListIp, 0, sizeof (surfaceListIp));
        ip_surf.surfaceList = &surfaceListIp;
        surface_list_init (space, &ip_surf, &inmap, 1);

        /* output surface initialisations */
        NvBufSurface op_surf;
        NvBufSurfaceParams surfaceListOp;
        memset (&surfaceListOp, 0, sizeof (surfaceListOp));
        op_surf.surfaceList = &surfaceListOp;
        surface_list_init (space, &op_surf, &outmap, 0);

        if (space->need_intersurf || space->do_scaling || space->flip_method) {
          if (space->isurf_flag == TRUE) {

            NvBufSurfaceAllocateParams input_params = { 0 };

            if (space->ibuf_count < 1) {
              input_params.params.width = GST_ROUND_UP_2 (space->from_width);;
              input_params.params.height = GST_ROUND_UP_2 (space->from_height);;
              input_params.params.layout = NVBUF_LAYOUT_PITCH;
              input_params.params.colorFormat = space->in_pix_fmt;
              if (space->compute_hw == NvBufSurfTransformCompute_GPU) {
                input_params.params.gpuId = space->gpu_id;
                input_params.params.memType = NVBUF_MEM_CUDA_DEVICE;
              } else {
                input_params.params.memType = NVBUF_MEM_SURFACE_ARRAY;
              }
              input_params.memtag = NvBufSurfaceTag_VIDEO_CONVERT;

              retn = NvBufSurfaceAllocate(&space->input_interbuf.isurface, 1, &input_params);
              if (retn != 0) {
                GST_ERROR ("%s: intermediate NvBufSurfaceAllocate Failed \n", __func__);
                flow_ret = GST_FLOW_ERROR;
                goto done;
              }
              space->input_interbuf.isurface->numFilled = 1;
              space->input_interbuf.idmabuf_fd = space->input_interbuf.isurface->surfaceList[0].bufferDesc;

              space->ibuf_count += 1;
            }

            if (space->ibuf_count < 2) {
              input_params.params.width = GST_ROUND_UP_2 (space->to_width);;
              input_params.params.height = GST_ROUND_UP_2 (space->to_height);;
              input_params.params.layout = NVBUF_LAYOUT_PITCH;
              input_params.params.colorFormat = space->out_pix_fmt;
              if (space->compute_hw == NvBufSurfTransformCompute_GPU) {
                input_params.params.gpuId = space->gpu_id;
                input_params.params.memType = NVBUF_MEM_CUDA_DEVICE;
              } else {
                input_params.params.memType = NVBUF_MEM_SURFACE_ARRAY;
              }
              input_params.memtag = NvBufSurfaceTag_VIDEO_CONVERT;

              retn = NvBufSurfaceAllocate(&space->output_interbuf.isurface, 1, &input_params);
              if (retn != 0) {
                GST_ERROR ("%s: intermediate NvBufSurfaceAllocate Failed \n", __func__);
                flow_ret = GST_FLOW_ERROR;
                goto done;
              }
              space->output_interbuf.isurface->numFilled = 1;
              space->output_interbuf.idmabuf_fd = space->output_interbuf.isurface->surfaceList[0].bufferDesc;

              space->ibuf_count += 1;
            }

            space->isurf_flag = FALSE;
          }

          NvBufSurfaceCopy (&ip_surf, space->input_interbuf.isurface);

          retn = NvBufSurfTransform (space->input_interbuf.isurface, space->output_interbuf.isurface, &space->transform_params);
          if (retn != 0) {
            g_print ("%s: NvBufSurfTransform Failed \n", __func__);
            flow_ret = GST_FLOW_ERROR;
            goto done;
          }

          NvBufSurfaceCopy (space->output_interbuf.isurface, &op_surf);

        } else {
          NvBufSurfaceCopy (&ip_surf, &op_surf);
        }
      } else {
        flow_ret = GST_FLOW_ERROR;
        goto done;
      }
      break;

    default:
      GST_ERROR ("%s: Unsupported input buffer \n", __func__);
      flow_ret = GST_FLOW_ERROR;
      goto done;
      break;
  }

done:
  gst_buffer_unmap (inbuf, &inmap);
  gst_buffer_unmap (outbuf, &outmap);

  return flow_ret;

  /* ERRORS */
no_memory:
  {
    GST_ERROR ("no memory block");
    return GST_FLOW_ERROR;
  }
unknown_format:
  {
    GST_ERROR ("unknown format");
    return GST_FLOW_NOT_NEGOTIATED;
  }
invalid_inbuf:
  {
    GST_ERROR ("input buffer mapinfo failed");
    return GST_FLOW_ERROR;
  }
invalid_outbuf:
  {
    GST_ERROR ("output buffer mapinfo failed");
    gst_buffer_unmap (inbuf, &inmap);
    return GST_FLOW_ERROR;
  }
cuda_errors:
  {
    gst_buffer_unmap (inbuf, &inmap);
    gst_buffer_unmap (outbuf, &outmap);
    return GST_FLOW_ERROR;
  }
}

/**
  * nvvconv plugin init.
  *
  * @param nvvconv : plugin instance
  */
static gboolean
nvvconv_init (GstPlugin * nvvconv)
{
  GST_DEBUG_CATEGORY_INIT (gst_nvvconv_debug, "nvvidconv",
      0, "nvvidconv plugin");

  return gst_element_register (nvvconv, "nvvidconv", GST_RANK_PRIMARY,
      GST_TYPE_NVVCONV);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    nvvidconv,
    PACKAGE_DESCRIPTION,
    nvvconv_init, VERSION, PACKAGE_LICENSE, PACKAGE_NAME, PACKAGE_URL)
