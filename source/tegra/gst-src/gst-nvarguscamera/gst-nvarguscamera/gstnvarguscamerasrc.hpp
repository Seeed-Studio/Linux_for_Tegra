/*
 * Copyright (c) 2017-2023, NVIDIA CORPORATION. All rights reserved.
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

#ifndef __GST_NVARGUSCAMERASRC_H__
#define __GST_NVARGUSCAMERASRC_H__

#include <gst/gst.h>
#include <condition_variable>
#include <chrono>

#include "nvbufsurface.h"
#include "nvbufsurftransform.h"
#include "gstnvarguscamera_utils.h"
#include "gstnvdsbufferpool.h"

G_BEGIN_DECLS

/* #defines don't like whitespacey bits */
#define GST_TYPE_NVARGUSCAMERASRC \
  (gst_nv_argus_camera_src_get_type())
#define GST_NVARGUSCAMERASRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NVARGUSCAMERASRC,GstNvArgusCameraSrc))
#define GST_NVARGUSCAMERASRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_NVARGUSCAMERASRC,GstNvArgusCameraSrcClass))
#define GST_IS_NVARGUSCAMERASRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_NVARGUSCAMERASRC))
#define GST_IS_NVARGUSCAMERASRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_NVARGUSCAMERASRC))

#define NVARGUSCAM_DEFAULT_WB_MODE                   NvArgusCamAwbMode_Auto
#define NVARGUSCAM_DEFAULT_SATURATION                1.0
#define NVARGUSCAM_DEFAULT_EXPOSURE_TIME             "34000 358733000"
#define NVARGUSCAM_DEFAULT_SENSOR_ID                 0
#define NVARGUSCAM_DEFAULT_SENSOR_MODE_STATE         -1
#define NVARGUSCAM_DEFAULT_TOTAL_SENSOR_MODES        0
#define NVARGUSCAM_DEFAULT_GAIN_RANGE                "1 16"
#define NVARGUSCAM_DEFAULT_DIGITAL_GAIN_RANGE        "1 256"
#define NVARGUSCAM_DEFAULT_TNR_MODE                  NvArgusCamNoiseReductionMode_Fast
#define NVARGUSCAM_DEFAULT_TNR_STRENGTH              -1.0
#define NVARGUSCAM_DEFAULT_EE_MODE                   NvArgusCamEdgeEnhancementMode_Fast
#define NVARGUSCAM_DEFAULT_EE_STRENGTH               -1.0
#define NVARGUSCAM_DEFAULT_AEANTIBANDING_MODE        NvArgusCamAeAntibandingMode_Auto
#define NVARGUSCAM_DEFAULT_EXP_COMPENSATION          0.0
#define NVARGUSCAM_DEFAULT_AE_LOCK                   FALSE
#define NVARGUSCAM_DEFAULT_AWB_LOCK                  FALSE

typedef struct _GstNvArgusCameraSrc      GstNvArgusCameraSrc;
typedef struct _GstNvArgusCameraSrcClass GstNvArgusCameraSrcClass;

typedef struct _GstNvArgusCameraSrcBuffer GstNvArgusCameraSrcBuffer;

typedef struct NvArgusCameraRangeRec
{
  /**  Lower limit for the range. */
  gfloat low;
  /**  Upper limit for the range. */
  gfloat high;
} NvArgusCameraRange;

/* NvArgusCameraSrc Controls */
typedef struct NvArgusCamControls
{
  NvArgusCamAwbMode wbmode;
  gfloat saturation;
  NvArgusCameraRange exposureTimeRange;
  NvArgusCameraRange gainRange;
  NvArgusCameraRange ispDigitalGainRange;
  NvArgusCamNoiseReductionMode NoiseReductionMode;
  NvArgusCamEdgeEnhancementMode EdgeEnhancementMode;
  NvArgusCamAeAntibandingMode AeAntibandingMode;
  gfloat NoiseReductionStrength;
  gfloat EdgeEnhancementStrength;
  gfloat ExposureCompensation;
  gboolean AeLock;
  gboolean AwbLock;
  Argus::AcRegion AeRegion;
} NvArgusCamControls;
/* NvArgusCameraSrc buffer */
struct _GstNvArgusCameraSrcBuffer
{
  gint dmabuf_fd;
  GstBuffer *gst_buf;
  NvBufSurface *surf;
};

typedef struct NvArgusFrameInfo
{
  gint fd;
  guint64 frameNum;
  guint64 frameTime;
} NvArgusFrameInfo;

struct _GstNvArgusCameraSrc
{
  GstBaseSrc base_nvarguscamera;

  GstPad *srcpad;

  GThread *consumer_thread;
  GThread *argus_thread;

  gboolean silent;

  GstBufferPool *pool;

  GstCaps *outcaps;

  gint width;
  gint height;
  gint fps_n;
  gint fps_d;
  gint sensor_id;
  gint sensor_mode;

  guint total_sensor_modes;
  guint timeout;
  guint secToRun;
  guint64 event_timeout;
  guint64 acquire_timeout;
  gchar *exposureTimeString;
  gchar *gainRangeString;
  gchar *ispDigitalGainRangeString;
  gchar *aeRegionString;

  GQueue *nvmm_buffers;
  GMutex nvmm_buffers_queue_lock;
  GCond nvmm_buffers_queue_cond;

  gboolean stop_requested;
  gboolean unlock_requested;
  gboolean timeout_complete;

  NvBufSurfTransformParams transform_params;

  GQueue *argus_buffers;
  GMutex argus_buffers_queue_lock;
  GCond argus_buffers_queue_cond;

  GMutex argus_buffer_consumed_lock;
  GCond argus_buffer_consumed_cond;
  gboolean is_argus_buffer_consumed;

  GMutex eos_lock;
  GCond eos_cond;
  GMutex queue_lock;

  std::condition_variable cv;
  std::mutex mtx;

  NvArgusCamControls controls;
  gboolean wbPropSet;
  gboolean saturationPropSet;
  gboolean exposureTimePropSet;
  gboolean gainRangePropSet;
  gboolean ispDigitalGainRangePropSet;
  gboolean tnrStrengthPropSet;
  gboolean tnrModePropSet;
  gboolean edgeEnhancementStrengthPropSet;
  gboolean edgeEnhancementModePropSet;
  gboolean aeAntibandingPropSet;
  gboolean exposureCompensationPropSet;
  gboolean aeLockPropSet;
  gboolean aeRegionPropSet;
  gboolean awbLockPropSet;
  gboolean argus_in_error;
  void *iRequest_ptr;
  void *iCaptureSession_ptr;
  void *AeRegion_ptr;
  void *iEventProvider_ptr;
  void *iEventQueue_ptr;
  void *iAutoControlSettings_ptr;
  void *request_ptr;
  void *outRequest_ptr;
  void *iDenoiseSettings_ptr;
  void *iEeSettings_ptr;
  void *iRequestSourceSettings_ptr;
  Argus::UniqueObj<Argus::EventQueue> queue;
  Argus::UniqueObj<Argus::OutputStream> outputStream;
  Argus::UniqueObj<Argus::OutputStreamSettings> streamSettings;
  Argus::UniqueObj<Argus::Request> request;
  NvArgusFrameInfo *frameInfo;
};

struct _GstNvArgusCameraSrcClass
{
  GstBaseSrcClass base_nvarguscamera_class;
};

GType gst_nv_argus_camera_src_get_type (void);

namespace ArgusSamples
{

/**
 * Base class for threads. Derived classes need to implement 'threadInitialize', 'threadExecute'
 * and 'threadShutdown'. This class handles the transition between the thread states.
 */
class ThreadArgus
{
public:
  ThreadArgus();
  virtual ~ThreadArgus();

  /**
   * Initialize
   */
  bool initialize(GstNvArgusCameraSrc *);
  /**
   * Shutdown
   */
  bool shutdown();

  /**
   * Wait until the thread is in 'running' state
   *
   * @param timeout [in] timeout in us
   */
  bool waitRunning(useconds_t timeoutUs = 5 * 1000 * 1000);

  GstNvArgusCameraSrc *src;

protected:
  virtual bool threadInitialize(GstNvArgusCameraSrc *) = 0;
  virtual bool threadExecute(GstNvArgusCameraSrc *) = 0;
  virtual bool threadShutdown(GstNvArgusCameraSrc *) = 0;

  /**
   * Request thread shutdown
   */
  bool requestShutdown()
  {
    m_doShutdown = true;
    return true;
  }

  Ordered<bool> m_doShutdown; ///< set to request shutdown of the thread

private:
  pthread_t m_threadID;       ///< thread ID

  /**
   * Thread states
   */
  enum ThreadState
  {
    THREAD_INACTIVE,        ///< is inactive
    THREAD_INITIALIZING,    ///< is initializing
    THREAD_RUNNING,         ///< is running
    THREAD_FAILED,          ///< has failed
    THREAD_DONE,            ///< execution done
  };
  Ordered<ThreadState> m_threadState;

  bool threadFunction(GstNvArgusCameraSrc *);

  static void *threadFunctionStub(void *dataPtr);
};

} // namespace ArgusSamples

G_END_DECLS

#endif /* __GST_NVARGUSCAMERASRC_H__ */
