/*
 * Copyright (c) 2017-2024, NVIDIA CORPORATION. All rights reserved.
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

/**
 * Sample pipeline
 *
 * gst-launch-1.0
 * nvarguscamerasrc !
 * "video/x-raw(memory:NVMM), width=640, height=480, format=NV12, framerate=30/1" !
 * nvoverlaysink -e -v
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <cstdlib>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/gstbasesrc.h>

#include <Argus/Argus.h>
#include <EGLStream/EGLStream.h>
#include <EGLStream/NV/ImageNativeBuffer.h>
#include <iostream>
#include <fstream>
#include <math.h>

#include <pthread.h>
#include <unistd.h> // for useconds_t

#include "Ordered.h"

#include "gstnvarguscamerasrc.hpp"
#include "Error.h"

#define CAPTURE_CAPS \
  "video/x-raw(memory:NVMM), " \
  "width = (int) [ 1, MAX ], " \
  "height = (int) [ 1, MAX ], " \
  "format = (string) { NV12 }, " \
  "framerate = (fraction) [ 0, MAX ];"

#define MIN_BUFFERS 6
#define MAX_BUFFERS 8

// this is gain value applied by ISP, its default range is [1, 256].
#define MIN_DIGITAL_GAIN 1
#define MAX_DIGITAL_GAIN 256

#define GST_NVARGUS_MEMORY_TYPE "nvarguscam"
static const int DEFAULT_FPS        = 30;
static const uint64_t DEFAULT_WAIT_FOR_EVENT_TIMEOUT = 3000000000;
static const uint64_t DEFAULT_ACQUIRE_FRAME_TIMEOUT = 5000000000;

#ifdef __cplusplus
extern "C"
{
#endif

using namespace std;
using namespace Argus;
using namespace EGLStream;

std::mutex g_mtx;
class CameraProviderContainer
{
public:
    CameraProviderContainer()
    {
      m_cameraProvider = CameraProvider::create();
      if (!m_cameraProvider)
        return;

      m_iCameraProvider = interface_cast<Argus::ICameraProvider>(m_cameraProvider);
      m_iCameraProvider->getCameraDevices(&m_cameraDevices);
    }

    ~CameraProviderContainer()
    {
      if (m_cameraProvider)
        m_cameraProvider->destroy();
    }

    ICameraProvider* getICameraProvider();
    std::vector<Argus::CameraDevice*> getCameraDevices();
private:
    CameraProvider *m_cameraProvider = NULL;
    ICameraProvider *m_iCameraProvider = NULL;
    std::vector<Argus::CameraDevice*> m_cameraDevices;
};

ICameraProvider* CameraProviderContainer::getICameraProvider()
{
  if (!m_cameraProvider)
    return NULL;

  return m_iCameraProvider;
}

vector<Argus::CameraDevice*> CameraProviderContainer::getCameraDevices()
{
  return m_cameraDevices;
}

shared_ptr<CameraProviderContainer> g_cameraProvider = make_shared<CameraProviderContainer>();

namespace ArgusSamples
{

ThreadArgus::ThreadArgus()
    : m_doShutdown(false)
    , m_threadID(0)
    , m_threadState(THREAD_INACTIVE)

{
}

ThreadArgus::~ThreadArgus()
{
  (void)shutdown();
}

bool ThreadArgus::initialize(GstNvArgusCameraSrc *src)
{
  if (m_threadID)
      return true;

  this->src = src;

  if (pthread_create(&m_threadID, NULL, threadFunctionStub, this) != 0)
    ORIGINATE_ERROR("Failed to create thread.");

  // wait for the thread to start up
  while (m_threadState == THREAD_INACTIVE)
  {
    usleep(100);
    if (m_threadState == THREAD_FAILED)
      return false;
  }

  return true;
}

bool ThreadArgus::shutdown()
{
  if (m_threadID)
  {
    m_doShutdown = true;
    if (pthread_join(m_threadID, NULL) != 0)
      ORIGINATE_ERROR("Failed to join thread");
    m_threadID = 0;
    m_doShutdown = false;
    m_threadState = THREAD_INACTIVE;
  }

 return true;
}

bool ThreadArgus::waitRunning(useconds_t timeoutUs)
{
  // Can only wait for a thread which is initializing or already running
  if ((m_threadState != THREAD_INITIALIZING) && (m_threadState != THREAD_RUNNING))
    ORIGINATE_ERROR("Invalid thread state %d", m_threadState.get());

  // wait for the thread to run
  const useconds_t sleepTimeUs = 100;
  while (m_threadState != THREAD_RUNNING)
  {
    usleep(sleepTimeUs);
    if (m_threadState == THREAD_FAILED)
    {
      ORIGINATE_ERROR("Invalid thread state %d", m_threadState.get());
    }
#ifdef DEBUG
    // in debug mode wait indefinitely
#else
    if (timeoutUs < sleepTimeUs)
      return false;
    timeoutUs -= sleepTimeUs;
#endif
  }

  return true;
}

/**
 * Thread function stub, calls the real thread function.
 *
 * @param [in] dataPtr  Pointer to user data
 */
/* static */ void *ThreadArgus::threadFunctionStub(void *dataPtr)
{
  ThreadArgus *thread = static_cast<ThreadArgus*>(dataPtr);

  if (!thread->threadFunction(thread->src))
    thread->m_threadState = ThreadArgus::THREAD_FAILED;
  else
    thread->m_threadState = ThreadArgus::THREAD_DONE;

  return NULL;
}

/**
 * Thread function
 */
bool ThreadArgus::threadFunction(GstNvArgusCameraSrc *src)
{
  m_threadState = THREAD_INITIALIZING;

  PROPAGATE_ERROR(threadInitialize(src));

  m_threadState = THREAD_RUNNING;

  while (!m_doShutdown)
  {
    PROPAGATE_ERROR(threadExecute(src));
  }

  PROPAGATE_ERROR(threadShutdown(src));

  return true;
}

}; // namespace ArgusSamples

namespace ArgusCamera
{

// Constants

#define GST_ARGUS_PRINT(...) printf("GST_ARGUS: " __VA_ARGS__)
#define CONSUMER_PRINT(...) printf("CONSUMER: " __VA_ARGS__)
#define GST_ARGUS_ERROR(...) printf("ARGUS_ERROR: Error generated. %s, %s: %d %s", __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)

const char* getStatusString(Argus::Status status);

/*******************************************************************************
 * StreamConsumer thread:
 *   Creates a StreamConsumer object to read frames from the OutputStream just tests
 *   for sanity.
 ******************************************************************************/
class StreamConsumer : public ArgusSamples::ThreadArgus
{
public:
  explicit StreamConsumer(OutputStream* stream) :
      m_stream(stream)
  {
  }
  ~StreamConsumer()
  {
  }

private:
  /** @name Thread methods */
  /**@{*/
  virtual bool threadInitialize(GstNvArgusCameraSrc *);
  virtual bool threadExecute(GstNvArgusCameraSrc *);
  virtual bool threadShutdown(GstNvArgusCameraSrc *);
  /**@}*/

  OutputStream* m_stream;
  //GstNvArgusCameraSrc *argus_src;
  UniqueObj<FrameConsumer> m_consumer;
};

const char* getStatusString(Argus::Status status)
{
    switch (status)
    {
        case Argus::STATUS_OK:               return "OK";
        case Argus::STATUS_INVALID_PARAMS:   return "INVALID_PARAMS";
        case Argus::STATUS_INVALID_SETTINGS: return "INVALID_SETTINGS";
        case Argus::STATUS_UNAVAILABLE:      return "UNAVAILABLE";
        case Argus::STATUS_OUT_OF_MEMORY:    return "OUT_OF_MEMORY";
        case Argus::STATUS_UNIMPLEMENTED:    return "UNIMPLEMENTED";
        case Argus::STATUS_TIMEOUT:          return "TIMEOUT";
        case Argus::STATUS_CANCELLED:        return "CANCELLED";
        case Argus::STATUS_DISCONNECTED:     return "DISCONNECTED";
        case Argus::STATUS_END_OF_STREAM:    return "END_OF_STREAM";
        default:                             return "BAD STATUS";
    }
}

bool StreamConsumer::threadInitialize(GstNvArgusCameraSrc *src)
{
  // Create the FrameConsumer.
  m_consumer = UniqueObj<FrameConsumer>(FrameConsumer::create(m_stream));
  if (!m_consumer)
    ORIGINATE_ERROR("Failed to create FrameConsumer");

  return true;
}

bool StreamConsumer::threadExecute(GstNvArgusCameraSrc *src)
{
  IEGLOutputStream *iStream = interface_cast<IEGLOutputStream>(m_stream);
  Size2D<uint32_t> streamSize (src->width, src->height);
  IFrameConsumer *iFrameConsumer = interface_cast<IFrameConsumer>(m_consumer);

  // Wait until the producer has connected to the stream.
  CONSUMER_PRINT("Waiting until producer is connected...\n");
  if (iStream->waitUntilConnected() != STATUS_OK)
  {
    src->argus_in_error = TRUE;
    ORIGINATE_ERROR("Stream failed to connect.");
  }
  CONSUMER_PRINT("Producer has connected; continuing.\n");
  IAutoControlSettings* l_iAutoControlSettings_ptr = (IAutoControlSettings *)src->iAutoControlSettings_ptr;
  ICaptureSession* l_iCaptureSession               = (ICaptureSession *)src->iCaptureSession_ptr;
  IDenoiseSettings* l_iDenoiseSettings_ptr         = (IDenoiseSettings *)src->iDenoiseSettings_ptr;
  IEdgeEnhanceSettings* l_iEeSettings_ptr          = (IEdgeEnhanceSettings *)src->iEeSettings_ptr;
  ISourceSettings* l_iRequestSourceSettings_ptr    = (ISourceSettings *)src->iRequestSourceSettings_ptr;
  Request* l_captureRequest                        = (Request*)src->request_ptr;
  IEventProvider *iEventProvider_ptr               = (IEventProvider*)src->iEventProvider_ptr;
  IEventQueue *iEventQueue_ptr                     = (IEventQueue*)src->iEventQueue_ptr;
  Range<float> sensorModeAnalogGainRange;
  Range<float> ispDigitalGainRange;
  Range<uint64_t> limitExposureTimeRange;
  l_iCaptureSession->repeat(l_captureRequest);

  src->frameInfo = g_slice_new(NvArgusFrameInfo);
  src->frameInfo->fd = -1;
  while (true)
  {
    Argus::Status frame_status;
    Argus::EventType frame_event;
    GError *error = NULL;
    Event* event = NULL;
    IEvent* iEvent = NULL;
    static GQuark domain = g_quark_from_static_string ("NvArgusCameraSrc");

    g_mutex_lock(&src->queue_lock);
    if (src->queue.get() == NULL)
    {
      g_mutex_unlock(&src->queue_lock);
      break;
    }

    iEventProvider_ptr->waitForEvents(src->queue.get(), src->event_timeout);
    g_mutex_unlock(&src->queue_lock);

    if (iEventQueue_ptr->getSize() == 0)
    {
      g_mutex_lock (&src->argus_buffers_queue_lock);
      src->stop_requested = TRUE;
      g_mutex_unlock (&src->argus_buffers_queue_lock);
      break;
    }

    event = (Event* )iEventQueue_ptr->getEvent(iEventQueue_ptr->getSize() - 1);
    iEvent = (IEvent*)interface_cast<const IEvent>(event);
    if (!iEvent)
    {
      src->argus_in_error = TRUE;
      ORIGINATE_ERROR("Failed to get IEvent interface");
    }

    // Retrieved event types
    frame_event = iEvent->getEventType();

    if (frame_event == EVENT_TYPE_ERROR)
    {
      if (src->stop_requested == TRUE  || src->timeout_complete == TRUE)
        break;

      src->argus_in_error = TRUE;
      const IEventError* iEventError = interface_cast<const IEventError>(event);
      Argus::Status argusStatus = iEventError->getStatus();
      error = g_error_new_literal (domain, argusStatus, getStatusString(argusStatus));
      GstMessage *message = gst_message_new_error (GST_OBJECT(src), error, "Argus Error Status");
      gst_element_post_message (GST_ELEMENT_CAST(src), message);
      g_mutex_lock (&src->argus_buffers_queue_lock);
      src->stop_requested = TRUE;
      g_mutex_unlock (&src->argus_buffers_queue_lock);
      break;
    }
    else if (frame_event == EVENT_TYPE_CAPTURE_STARTED)
    {
        continue;
    }
    else if (frame_event == EVENT_TYPE_CAPTURE_COMPLETE)
    {
      UniqueObj<Frame> frame(iFrameConsumer->acquireFrame(src->acquire_timeout, &frame_status));

      if (src->stop_requested == TRUE || src->timeout_complete == TRUE)
      {
        break;
      }

      if (frame_status != STATUS_OK)
      {
        src->argus_in_error = TRUE;
        error = g_error_new_literal (domain, frame_status, getStatusString(frame_status));
        GstMessage *message = gst_message_new_error (GST_OBJECT(src), error, "Argus Error Status");
        gst_element_post_message (GST_ELEMENT_CAST(src), message);
        g_mutex_lock (&src->argus_buffers_queue_lock);
        src->stop_requested = TRUE;
        g_mutex_unlock (&src->argus_buffers_queue_lock);
        break;
      }
      if (!frame)
      {
        g_mutex_lock (&src->argus_buffers_queue_lock);
        src->stop_requested = TRUE;
        g_mutex_unlock (&src->argus_buffers_queue_lock);
        break;
      }

      if (src->wbPropSet)
      {
        switch (src->controls.wbmode)
        {
          case NvArgusCamAwbMode_Off:
            l_iAutoControlSettings_ptr->setAwbMode(AWB_MODE_OFF);
            break;
          case NvArgusCamAwbMode_Auto:
            l_iAutoControlSettings_ptr->setAwbMode(AWB_MODE_AUTO);
            break;
          case NvArgusCamAwbMode_Incandescent:
            l_iAutoControlSettings_ptr->setAwbMode(AWB_MODE_INCANDESCENT);
            break;
          case NvArgusCamAwbMode_Fluorescent:
            l_iAutoControlSettings_ptr->setAwbMode(AWB_MODE_FLUORESCENT);
            break;
          case NvArgusCamAwbMode_WarmFluorescent:
            l_iAutoControlSettings_ptr->setAwbMode(AWB_MODE_WARM_FLUORESCENT);
            break;
          case NvArgusCamAwbMode_Daylight:
            l_iAutoControlSettings_ptr->setAwbMode(AWB_MODE_DAYLIGHT);
            break;
          case NvArgusCamAwbMode_CloudyDaylight:
            l_iAutoControlSettings_ptr->setAwbMode(AWB_MODE_CLOUDY_DAYLIGHT);
            break;
          case NvArgusCamAwbMode_Twilight:
            l_iAutoControlSettings_ptr->setAwbMode(AWB_MODE_TWILIGHT);
            break;
          case NvArgusCamAwbMode_Shade:
            l_iAutoControlSettings_ptr->setAwbMode(AWB_MODE_SHADE);
            break;
          case NvArgusCamAwbMode_Manual:
            l_iAutoControlSettings_ptr->setAwbMode(AWB_MODE_MANUAL);
            break;
          default :
            l_iAutoControlSettings_ptr->setAwbMode(AWB_MODE_AUTO);
            break;
        }
        src->wbPropSet = FALSE;
        l_iCaptureSession->repeat(l_captureRequest);
      }

      if (src->saturationPropSet)
      {
        l_iAutoControlSettings_ptr->setColorSaturationEnable(TRUE);
        l_iAutoControlSettings_ptr->setColorSaturation(src->controls.saturation);
        l_iCaptureSession->repeat(l_captureRequest);
        src->saturationPropSet = FALSE;
      }

      if (src->exposureCompensationPropSet)
      {
        l_iAutoControlSettings_ptr->setExposureCompensation(src->controls.ExposureCompensation);
        l_iCaptureSession->repeat(l_captureRequest);
        src->exposureCompensationPropSet = FALSE;
      }

      if(src->aeLockPropSet)
      {
        if(src->controls.AeLock)
          l_iAutoControlSettings_ptr->setAeLock(true);
        else
          l_iAutoControlSettings_ptr->setAeLock(false);
        l_iCaptureSession->repeat(l_captureRequest);
        src->aeLockPropSet = FALSE;
      }

      if(src->awbLockPropSet)
      {
        if(src->controls.AwbLock)
          l_iAutoControlSettings_ptr->setAwbLock(true);
        else
          l_iAutoControlSettings_ptr->setAwbLock(false);
        l_iCaptureSession->repeat(l_captureRequest);
        src->awbLockPropSet = FALSE;
      }

      if(src->tnrModePropSet)
      {
        switch (src->controls.NoiseReductionMode)
        {
          case NvArgusCamNoiseReductionMode_Off:
            l_iDenoiseSettings_ptr->setDenoiseMode(DENOISE_MODE_OFF);
            break;
          case NvArgusCamNoiseReductionMode_Fast:
            l_iDenoiseSettings_ptr->setDenoiseMode(DENOISE_MODE_FAST);
            break;
          case NvArgusCamNoiseReductionMode_HighQuality:
            l_iDenoiseSettings_ptr->setDenoiseMode(DENOISE_MODE_HIGH_QUALITY);
            break;
          default :
            l_iDenoiseSettings_ptr->setDenoiseMode(DENOISE_MODE_OFF);
            break;
        }
        l_iCaptureSession->repeat(l_captureRequest);
        src->tnrModePropSet = FALSE;
      }

      if(src->tnrStrengthPropSet)
      {
        l_iDenoiseSettings_ptr->setDenoiseStrength(src->controls.NoiseReductionStrength);
        l_iCaptureSession->repeat(l_captureRequest);
        src->tnrStrengthPropSet = FALSE;
      }

      if(src->edgeEnhancementModePropSet)
      {
        switch (src->controls.EdgeEnhancementMode)
        {
          case NvArgusCamEdgeEnhancementMode_Off:
            l_iEeSettings_ptr->setEdgeEnhanceMode(EDGE_ENHANCE_MODE_OFF);
            break;
          case NvArgusCamEdgeEnhancementMode_Fast:
            l_iEeSettings_ptr->setEdgeEnhanceMode(EDGE_ENHANCE_MODE_FAST);
            break;
          case NvArgusCamEdgeEnhancementMode_HighQuality:
            l_iEeSettings_ptr->setEdgeEnhanceMode(EDGE_ENHANCE_MODE_HIGH_QUALITY);
            break;
          default :
            l_iEeSettings_ptr->setEdgeEnhanceMode(EDGE_ENHANCE_MODE_OFF);
            break;
        }
        l_iCaptureSession->repeat(l_captureRequest);
        src->edgeEnhancementModePropSet = FALSE;
      }

       if(src->edgeEnhancementStrengthPropSet)
       {
         l_iEeSettings_ptr->setEdgeEnhanceStrength(src->controls.EdgeEnhancementStrength);
         l_iCaptureSession->repeat(l_captureRequest);
         src->edgeEnhancementStrengthPropSet = FALSE;
       }

      if(src->aeAntibandingPropSet)
      {
        switch (src->controls.AeAntibandingMode)
        {
          case NvArgusCamAeAntibandingMode_Off:
            l_iAutoControlSettings_ptr->setAeAntibandingMode(AE_ANTIBANDING_MODE_OFF);
            break;
          case NvArgusCamAeAntibandingMode_Auto:
            l_iAutoControlSettings_ptr->setAeAntibandingMode(AE_ANTIBANDING_MODE_AUTO);
            break;
          case NvArgusCamAeAntibandingMode_50HZ:
            l_iAutoControlSettings_ptr->setAeAntibandingMode(AE_ANTIBANDING_MODE_50HZ);
            break;
          case NvArgusCamAeAntibandingMode_60HZ:
            l_iAutoControlSettings_ptr->setAeAntibandingMode(AE_ANTIBANDING_MODE_60HZ);
            break;
          default :
            l_iAutoControlSettings_ptr->setAeAntibandingMode(AE_ANTIBANDING_MODE_OFF);
            break;
        }
        l_iCaptureSession->repeat(l_captureRequest);
        src->aeAntibandingPropSet = FALSE;
      }

      if(src->gainRangePropSet == TRUE)
      {
        if (src->controls.gainRange.low < sensorModeAnalogGainRange.min())
        {
          GST_ARGUS_PRINT("Invalid min gain value, using default minimum gain: %f instead.\n",
                          sensorModeAnalogGainRange.min());
          src->controls.gainRange.low = sensorModeAnalogGainRange.min();
        }
        if (src->controls.gainRange.high > sensorModeAnalogGainRange.max())
        {
          GST_ARGUS_PRINT("Invalid max gain value, using default maximum gain: %f instead.\n",
                          sensorModeAnalogGainRange.max());
          src->controls.gainRange.high = sensorModeAnalogGainRange.max();
        }

        sensorModeAnalogGainRange.min() = src->controls.gainRange.low;
        sensorModeAnalogGainRange.max() = src->controls.gainRange.high;
        l_iRequestSourceSettings_ptr->setGainRange(sensorModeAnalogGainRange);
        l_iCaptureSession->repeat(l_captureRequest);
        src->gainRangePropSet = FALSE;
      }

      if(src->ispDigitalGainRangePropSet == TRUE)
      {
        ispDigitalGainRange.min() = src->controls.ispDigitalGainRange.low;
        ispDigitalGainRange.max() = src->controls.ispDigitalGainRange.high;
        l_iAutoControlSettings_ptr->setIspDigitalGainRange(ispDigitalGainRange);
        l_iCaptureSession->repeat(l_captureRequest);
        src->ispDigitalGainRangePropSet = FALSE;
      }

      if(src->exposureTimePropSet == TRUE)
      {
        if (src->controls.exposureTimeRange.low < limitExposureTimeRange.min())
        {
          GST_ARGUS_PRINT("Invalid min exp value, using default minimum exp: %ju instead.\n",
                          limitExposureTimeRange.min());
          src->controls.exposureTimeRange.low = limitExposureTimeRange.min();
        }
        if (src->controls.exposureTimeRange.high > limitExposureTimeRange.max())
        {
          GST_ARGUS_PRINT("Invalid max exp value, using default maximum exp: %ju instead.\n",
                          limitExposureTimeRange.max());
          src->controls.exposureTimeRange.high = limitExposureTimeRange.max();
        }

        limitExposureTimeRange.min() = src->controls.exposureTimeRange.low;
        limitExposureTimeRange.max() = src->controls.exposureTimeRange.high;
        l_iRequestSourceSettings_ptr->setExposureTimeRange(limitExposureTimeRange);
        l_iCaptureSession->repeat(l_captureRequest);
        src->exposureTimePropSet = FALSE;
      }

      if(src->aeRegionPropSet == TRUE)
      {
        std::vector < AcRegion > AeRegion;
        AcRegion ae(src->controls.AeRegion.left(), src->controls.AeRegion.top(),
                    src->controls.AeRegion.right(), src->controls.AeRegion.bottom(),
                    src->controls.AeRegion.weight());
        AeRegion.push_back(ae);
        GST_ARGUS_PRINT("Setting AeRegion on the fly: %d %d %d %d %f \n",
                        AeRegion[0].left(), AeRegion[0].top(), AeRegion[0].right(),
                        AeRegion[0].bottom(), AeRegion[0].weight());
        l_iAutoControlSettings_ptr->setAeRegions(AeRegion);
        l_iCaptureSession->repeat(l_captureRequest);
        src->aeRegionPropSet = FALSE;
      }

      // Use the IFrame interface to print out the frame number/timestamp, and
      // to provide access to the Image in the Frame.
      IFrame *iFrame = interface_cast<IFrame>(frame);
      if (!iFrame)
        ORIGINATE_ERROR("Failed to get IFrame interface.");

      // Get the IImageNativeBuffer extension interface and create the fd.
      NV::IImageNativeBuffer *iNativeBuffer =
        interface_cast<NV::IImageNativeBuffer>(iFrame->getImage());
      if (!iNativeBuffer)
        ORIGINATE_ERROR("IImageNativeBuffer not supported by Image.");

      if (src->frameInfo->fd < 0)
      {
        src->frameInfo->fd = iNativeBuffer->createNvBuffer(streamSize,
                NVBUF_COLOR_FORMAT_YUV420,
                NVBUF_LAYOUT_BLOCK_LINEAR);
        if (!src->silent)
          CONSUMER_PRINT("Acquired Frame. %d\n", src->frameInfo->fd);
      }
      else if (iNativeBuffer->copyToNvBuffer(src->frameInfo->fd) != STATUS_OK)
      {
        ORIGINATE_ERROR("IImageNativeBuffer not supported by Image.");
      }

      static const guint64 ground_clk = iFrame->getTime();;

      if (!src->silent)
      {
        guint64 frame_timestamp = iFrame->getTime() - ground_clk;
        guint64 millisec_timestamp = ((frame_timestamp % (1000000000)))/1000000;
        CONSUMER_PRINT("Acquired Frame: %llu, time sec %llu msec %llu\n",
                   static_cast<unsigned long long>(iFrame->getNumber()),
                   static_cast<unsigned long long>(frame_timestamp / (1000000000)),
                   static_cast<unsigned long long>(millisec_timestamp));
      }

      src->frameInfo->frameNum = iFrame->getNumber();
      src->frameInfo->frameTime = iFrame->getTime();

      g_mutex_lock (&src->argus_buffers_queue_lock);
      g_queue_push_tail (src->argus_buffers, (src->frameInfo));
      g_cond_signal (&src->argus_buffers_queue_cond);
      g_mutex_unlock (&src->argus_buffers_queue_lock);

      g_mutex_lock (&src->argus_buffer_consumed_lock);
      while (!src->is_argus_buffer_consumed)
      {
        gint64 until;
        until = g_get_monotonic_time () + G_TIME_SPAN_SECOND;
        g_cond_wait_until (&src->argus_buffer_consumed_cond, &src->argus_buffer_consumed_lock, until);
      }
      src->is_argus_buffer_consumed = FALSE;
      g_mutex_unlock (&src->argus_buffer_consumed_lock);
    } //EVENT_TYPE_CAPTURE_COMPLETE
  }

  if (src->frameInfo->fd)
  {
    int ret = 0;
    NvBufSurface *nvbuf_surf = 0;
    ret = NvBufSurfaceFromFd(src->frameInfo->fd, (void**)(&nvbuf_surf));
    if (ret != 0 || nvbuf_surf == NULL)
      ORIGINATE_ERROR("NvBufSurfaceFromFd Failed.");
    else {
      ret = NvBufSurfaceDestroy(nvbuf_surf);
      if (ret != 0)
        ORIGINATE_ERROR("NvBufSurfaceDestroy Failed.");
    }
  }

  g_slice_free (NvArgusFrameInfo, src->frameInfo);

  if (!src->argus_in_error)
  {
    CONSUMER_PRINT("Done Success\n");
  }
  else
  {
    CONSUMER_PRINT("ERROR OCCURRED\n");
  }
  PROPAGATE_ERROR(requestShutdown());
  return true;
}

bool StreamConsumer::threadShutdown(GstNvArgusCameraSrc *src)
{
  return true;
}

static bool execute(int32_t cameraIndex,
                    int32_t cameraMode,
                    const Size2D<uint32_t>& streamSize,
                    int32_t secToRun,
                    GstNvArgusCameraSrc *src)
{
  gfloat frameRate = 0, duration = 0;
  uint32_t index = 0;
  gint found = 0;
  gint best_match = -1;
  Range<float> sensorModeAnalogGainRange;
  Range<float> ispDigitalGainRange;
  Range<uint64_t> limitExposureTimeRange;
  vector<SensorMode*> modes;
  vector<EventType> eventTypes;
  ICameraProperties *camProps= NULL;
  IRequest *iRequest = NULL;
  ISensorMode *iSensorMode_ptr = NULL;
  IAutoControlSettings* iAutoControlSettings = NULL;
  ISourceSettings *requestSourceSettings = NULL;

  // Create the CameraProvider object
  shared_ptr<CameraProviderContainer> cameraProvider = g_cameraProvider;
  ICameraProvider *iCameraProvider = cameraProvider->getICameraProvider();
  if (!iCameraProvider)
    ORIGINATE_ERROR("Failed to create CameraProvider");

  // Get the camera devices.
  std::vector<CameraDevice*> cameraDevices = cameraProvider->getCameraDevices();
  if (cameraDevices.size() == 0)
    ORIGINATE_ERROR("No cameras available");

  if (static_cast<uint32_t>(cameraIndex) >= cameraDevices.size())
    ORIGINATE_ERROR("Invalid camera device specified %d specified, %d max index",
                                      cameraIndex, static_cast<int32_t>(cameraDevices.size())-1);

  // Create the capture session using the specified device.
  UniqueObj<CaptureSession> captureSession =
      UniqueObj<CaptureSession>(iCameraProvider->createCaptureSession(cameraDevices[cameraIndex]));
  ICaptureSession *iCaptureSession = interface_cast<ICaptureSession>(captureSession);
  if (!iCaptureSession)
    ORIGINATE_ERROR("Failed to create CaptureSession");
  src->iCaptureSession_ptr = iCaptureSession;

  IEventProvider *iEventProvider = interface_cast<IEventProvider>(captureSession);
  if (!iEventProvider)
    ORIGINATE_ERROR("Failed to create Event Provider");
  src->iEventProvider_ptr = iEventProvider;

  // Argus drops EVENT_TYPE_ERROR if all 3 events are not set. Setting all for now
  eventTypes.push_back(EVENT_TYPE_CAPTURE_COMPLETE);
  eventTypes.push_back(EVENT_TYPE_ERROR);
  eventTypes.push_back(EVENT_TYPE_CAPTURE_STARTED);

  src->queue = UniqueObj<EventQueue>(iEventProvider->createEventQueue(eventTypes));
  IEventQueue *iQueue = interface_cast<IEventQueue>(src->queue);
  if (!iQueue)
    ORIGINATE_ERROR("EventQ interface is NULL");
  src->iEventQueue_ptr = iQueue;

  {
    std::unique_lock<std::mutex> g_lck(g_mtx);

    GST_ARGUS_PRINT("Creating output stream\n");
    src->streamSettings = UniqueObj<OutputStreamSettings>(
      iCaptureSession->createOutputStreamSettings(STREAM_TYPE_EGL));
    IEGLOutputStreamSettings *iStreamSettings = interface_cast<IEGLOutputStreamSettings>(src->streamSettings);
    if (iStreamSettings)
    {
      iStreamSettings->setPixelFormat(PIXEL_FMT_YCbCr_420_888);
      iStreamSettings->setResolution(streamSize);
    }
    if (src->streamSettings.get() == NULL)
      ORIGINATE_ERROR("Failed to get streamSettings");

    src->outputStream = UniqueObj<OutputStream>(iCaptureSession->createOutputStream(src->streamSettings.get()));
    IEGLOutputStream *iStream = interface_cast<IEGLOutputStream>(src->outputStream);
    if (!iStream)
      ORIGINATE_ERROR("Failed to create OutputStream");
  }

  StreamConsumer consumerThread(src->outputStream.get());

  PROPAGATE_ERROR(consumerThread.initialize(src));
  // Wait until the consumer is connected to the stream.
  PROPAGATE_ERROR(consumerThread.waitRunning());

  {
    std::unique_lock<std::mutex> g_lck(g_mtx);

    // Create capture request and enable output stream.
    src->request = UniqueObj<Request>(iCaptureSession->createRequest(Argus::CAPTURE_INTENT_PREVIEW));
    iRequest = interface_cast<IRequest>(src->request);
    src->iRequest_ptr = iRequest;
    if (!iRequest)
      ORIGINATE_ERROR("Failed to create Request");
    iRequest->enableOutputStream(src->outputStream.get());

    iAutoControlSettings =
        interface_cast<IAutoControlSettings>(iRequest->getAutoControlSettings());
    if (!iAutoControlSettings)
      ORIGINATE_ERROR("Failed to get AutoControlSettings");
    src->iAutoControlSettings_ptr = iAutoControlSettings;

    camProps = interface_cast<ICameraProperties>(cameraDevices[cameraIndex]);
    if (!camProps)
      ORIGINATE_ERROR("Failed to create camera properties");
    camProps->getAllSensorModes(&modes);

    requestSourceSettings =
                        interface_cast<ISourceSettings>(iRequest->getSourceSettings());
    if (!requestSourceSettings)
      ORIGINATE_ERROR("Failed to get request source settings");
    src->iRequestSourceSettings_ptr = requestSourceSettings;

    if (cameraMode != NVARGUSCAM_DEFAULT_SENSOR_MODE_STATE && static_cast<uint32_t>(cameraMode) >= modes.size())
      ORIGINATE_ERROR("Invalid sensor mode %d selected %d present", cameraMode,
                                                            static_cast<int32_t>(modes.size()));

    src->total_sensor_modes = modes.size();

    GST_ARGUS_PRINT("Available Sensor modes :\n");
    frameRate = src->fps_n/ src->fps_d;
    duration = 1e9 * src->fps_d/ src->fps_n;
    ISensorMode *iSensorMode[modes.size()];
    for (index = 0; index < modes.size(); index++)
    {
      iSensorMode[index] = interface_cast<ISensorMode>(modes[index]);
      if (!iSensorMode[index] || src->argus_in_error)
        ORIGINATE_ERROR("NULL SensorMode interface detected");

      sensorModeAnalogGainRange = iSensorMode[index]->getAnalogGainRange();
      limitExposureTimeRange = iSensorMode[index]->getExposureTimeRange();
      GST_ARGUS_PRINT("%d x %d FR = %f fps Duration = %lu ; Analog Gain range min %f, max %f; Exposure Range min %ju, max %ju;\n\n",
                      iSensorMode[index]->getResolution().width(), iSensorMode[index]->getResolution().height(),
                      (1e9/(iSensorMode[index]->getFrameDurationRange().min())),
                      iSensorMode[index]->getFrameDurationRange().min(),
                      sensorModeAnalogGainRange.min(), sensorModeAnalogGainRange.max(),
                      limitExposureTimeRange.min(), limitExposureTimeRange.max());

      if (cameraMode ==  NVARGUSCAM_DEFAULT_SENSOR_MODE_STATE)
      {
        if (streamSize.width() <= iSensorMode[index]->getResolution().width() &&
            streamSize.height() <= iSensorMode[index]->getResolution().height() &&
            duration >= (iSensorMode[index]->getFrameDurationRange().min()))
        {
          if (best_match == -1 || ((streamSize.width() == iSensorMode[index]->getResolution().width()) &&
                (streamSize.height() == iSensorMode[index]->getResolution().height()) &&
              (iSensorMode[best_match]->getFrameDurationRange().min() >= iSensorMode[index]->getFrameDurationRange().min()))){
               best_match = index;
          }
          else if ((iSensorMode[index]->getResolution().width()) <= iSensorMode[best_match]->getResolution().width()) {
          best_match = index;
          }
          found = 1;
        }
      }
    }
  }

  if (cameraMode == NVARGUSCAM_DEFAULT_SENSOR_MODE_STATE)
  {
    if (0 == found)
    {
      /* As request resolution is not supported, switch to default
        * sensormode Index.
        */
      GST_INFO_OBJECT (src, " Requested resolution W = %d H = %d not supported by Sensor.\n",
          streamSize.width(), streamSize.height());
      cameraMode = 0;
    }
    else {
      cameraMode = best_match;
    }
  }
  /* Update Sensor Mode*/
  iSensorMode_ptr = Argus::interface_cast<Argus::ISensorMode>(modes[cameraMode]);
  src->sensor_mode = cameraMode;

  if (frameRate > round((1e9/(iSensorMode_ptr->getFrameDurationRange().min()))))
  {
    src->argus_in_error = TRUE;
    GST_ARGUS_ERROR("Frame Rate specified is greater than supported\n");
  }

  requestSourceSettings->setSensorMode(modes[cameraMode]);
  if (!src->fps_n)
  {
    frameRate = DEFAULT_FPS;
  }

  requestSourceSettings->setFrameDurationRange(Range<uint64_t>(1e9/frameRate));

  GST_ARGUS_PRINT("Running with following settings:\n"
             "   Camera index = %d \n"
             "   Camera mode  = %d \n"
             "   Output Stream W = %d H = %d \n"
             "   seconds to Run    = %d \n"
             "   Frame Rate = %f \n",
             cameraIndex, cameraMode, iSensorMode_ptr->getResolution().width(),
             iSensorMode_ptr->getResolution().height(), secToRun,
             (1e9/(iSensorMode_ptr->getFrameDurationRange().min())));

  IDenoiseSettings *denoiseSettings = interface_cast<IDenoiseSettings>(src->request);
  if (!denoiseSettings)
    ORIGINATE_ERROR("Failed to get DenoiseSettings interface");
  src->iDenoiseSettings_ptr = denoiseSettings;

  IEdgeEnhanceSettings *eeSettings = interface_cast<IEdgeEnhanceSettings>(src->request);
  if (!eeSettings)
    ORIGINATE_ERROR("Failed to get EdgeEnhancementSettings interface");
  src->iEeSettings_ptr = eeSettings;

  /* Setting Noise Reduction Mode and Strength*/
  if(src->tnrModePropSet)
  {
    switch (src->controls.NoiseReductionMode)
    {
      case NvArgusCamNoiseReductionMode_Off:
        denoiseSettings->setDenoiseMode(DENOISE_MODE_OFF);
        break;
      case NvArgusCamNoiseReductionMode_Fast:
        denoiseSettings->setDenoiseMode(DENOISE_MODE_FAST);
        break;
      case NvArgusCamNoiseReductionMode_HighQuality:
        denoiseSettings->setDenoiseMode(DENOISE_MODE_HIGH_QUALITY);
        break;
      default :
        denoiseSettings->setDenoiseMode(DENOISE_MODE_OFF);
        break;
    }
    src->tnrModePropSet = FALSE;
  }

  if(src->tnrStrengthPropSet)
  {
    denoiseSettings->setDenoiseStrength(src->controls.NoiseReductionStrength);
    src->tnrStrengthPropSet = FALSE;
  }

  /* Setting Edge Enhancement Mode and Strength*/
  if(src->edgeEnhancementModePropSet)
  {
    switch (src->controls.EdgeEnhancementMode)
    {
      case NvArgusCamEdgeEnhancementMode_Off:
        eeSettings->setEdgeEnhanceMode(EDGE_ENHANCE_MODE_OFF);
        break;
      case NvArgusCamEdgeEnhancementMode_Fast:
        eeSettings->setEdgeEnhanceMode(EDGE_ENHANCE_MODE_FAST);
        break;
      case NvArgusCamEdgeEnhancementMode_HighQuality:
        eeSettings->setEdgeEnhanceMode(EDGE_ENHANCE_MODE_HIGH_QUALITY);
        break;
      default :
        eeSettings->setEdgeEnhanceMode(EDGE_ENHANCE_MODE_OFF);
        break;
    }
    src->edgeEnhancementModePropSet = FALSE;
  }

  if(src->edgeEnhancementStrengthPropSet)
  {
    eeSettings->setEdgeEnhanceStrength(src->controls.EdgeEnhancementStrength);
    src->edgeEnhancementStrengthPropSet = FALSE;
  }

  /* Setting AE Antibanding Mode */
  if(src->aeAntibandingPropSet)
  {
    switch (src->controls.AeAntibandingMode)
    {
      case NvArgusCamAeAntibandingMode_Off:
        iAutoControlSettings->setAeAntibandingMode(AE_ANTIBANDING_MODE_OFF);
        break;
      case NvArgusCamAeAntibandingMode_Auto:
        iAutoControlSettings->setAeAntibandingMode(AE_ANTIBANDING_MODE_AUTO);
        break;
      case NvArgusCamAeAntibandingMode_50HZ:
        iAutoControlSettings->setAeAntibandingMode(AE_ANTIBANDING_MODE_50HZ);
        break;
      case NvArgusCamAeAntibandingMode_60HZ:
        iAutoControlSettings->setAeAntibandingMode(AE_ANTIBANDING_MODE_60HZ);
        break;
      default :
        iAutoControlSettings->setAeAntibandingMode(AE_ANTIBANDING_MODE_OFF);
        break;
    }
    src->aeAntibandingPropSet = FALSE;
  }

  if(src->exposureCompensationPropSet)
  {
    iAutoControlSettings->setExposureCompensation(src->controls.ExposureCompensation);
    src->exposureCompensationPropSet = FALSE;
  }

  /* Setting auto white balance lock */
  if(src->awbLockPropSet)
  {
    if(src->controls.AwbLock)
      iAutoControlSettings->setAwbLock(true);
    else
      iAutoControlSettings->setAwbLock(false);
    src->awbLockPropSet = FALSE;
  }

  /* Setting auto exposure lock */
  if(src->aeLockPropSet)
  {
    if(src->controls.AeLock)
      iAutoControlSettings->setAeLock(true);
    else
      iAutoControlSettings->setAeLock(false);
    src->aeLockPropSet = FALSE;
  }

 /* Setting white balance property */
    if (src->wbPropSet)
    {
      switch (src->controls.wbmode)
      {
        case NvArgusCamAwbMode_Off:
          iAutoControlSettings->setAwbMode(AWB_MODE_OFF);
          break;
        case NvArgusCamAwbMode_Auto:
          iAutoControlSettings->setAwbMode(AWB_MODE_AUTO);
          break;
        case NvArgusCamAwbMode_Incandescent:
          iAutoControlSettings->setAwbMode(AWB_MODE_INCANDESCENT);
          break;
        case NvArgusCamAwbMode_Fluorescent:
          iAutoControlSettings->setAwbMode(AWB_MODE_FLUORESCENT);
          break;
        case NvArgusCamAwbMode_WarmFluorescent:
          iAutoControlSettings->setAwbMode(AWB_MODE_WARM_FLUORESCENT);
          break;
        case NvArgusCamAwbMode_Daylight:
          iAutoControlSettings->setAwbMode(AWB_MODE_DAYLIGHT);
          break;
        case NvArgusCamAwbMode_CloudyDaylight:
          iAutoControlSettings->setAwbMode(AWB_MODE_CLOUDY_DAYLIGHT);
          break;
        case NvArgusCamAwbMode_Twilight:
          iAutoControlSettings->setAwbMode(AWB_MODE_TWILIGHT);
          break;
        case NvArgusCamAwbMode_Shade:
          iAutoControlSettings->setAwbMode(AWB_MODE_SHADE);
          break;
        case NvArgusCamAwbMode_Manual:
          iAutoControlSettings->setAwbMode(AWB_MODE_MANUAL);
          break;
        default :
          iAutoControlSettings->setAwbMode(AWB_MODE_AUTO);
          break;
      }
      src->wbPropSet = FALSE;
    }

 /* Setting color saturation property */
    if (src->saturationPropSet)
    {
      iAutoControlSettings->setColorSaturationEnable(TRUE);
      iAutoControlSettings->setColorSaturation(src->controls.saturation);
      src->saturationPropSet = FALSE;
    }

  if(src->exposureTimePropSet == TRUE)
  {
    if (src->controls.exposureTimeRange.low < limitExposureTimeRange.min())
    {
        GST_ARGUS_PRINT("Invalid min exp value, using default minimum exp: %ju instead.\n",
                      limitExposureTimeRange.min());
        src->controls.exposureTimeRange.low = limitExposureTimeRange.min();
    }
    if (src->controls.exposureTimeRange.high > limitExposureTimeRange.max())
    {
        GST_ARGUS_PRINT("Invalid max exp value, using default maximum exp: %ju instead.\n",
                      limitExposureTimeRange.max());
        src->controls.exposureTimeRange.high = limitExposureTimeRange.max();
    }

    limitExposureTimeRange.min() = src->controls.exposureTimeRange.low;
    limitExposureTimeRange.max() = src->controls.exposureTimeRange.high;
    requestSourceSettings->setExposureTimeRange(limitExposureTimeRange);
    src->exposureTimePropSet = FALSE;
  }

  if(src->gainRangePropSet == TRUE)
  {
    if (src->controls.gainRange.low < sensorModeAnalogGainRange.min())
    {
        GST_ARGUS_PRINT("Invalid min gain value, using default minimum gain: %f instead.\n",
                        sensorModeAnalogGainRange.min());
        src->controls.gainRange.low = sensorModeAnalogGainRange.min();
    }
    if (src->controls.gainRange.high > sensorModeAnalogGainRange.max())
    {
        GST_ARGUS_PRINT("Invalid max gain value, using default maximum gain: %f instead.\n",
                        sensorModeAnalogGainRange.max());
        src->controls.gainRange.high = sensorModeAnalogGainRange.max();
    }

    sensorModeAnalogGainRange.min() = src->controls.gainRange.low;
    sensorModeAnalogGainRange.max() = src->controls.gainRange.high;
    requestSourceSettings->setGainRange(sensorModeAnalogGainRange);
    src->gainRangePropSet = FALSE;
  }

  if(src->ispDigitalGainRangePropSet == TRUE)
  {
    ispDigitalGainRange.min() = src->controls.ispDigitalGainRange.low;
    ispDigitalGainRange.max() = src->controls.ispDigitalGainRange.high;
    iAutoControlSettings->setIspDigitalGainRange(ispDigitalGainRange);
    src->ispDigitalGainRangePropSet = FALSE;
  }

  /* Setting Ae Region */
  if(src->aeRegionPropSet == TRUE)
  {
    std::vector < AcRegion > AeRegion;
    AcRegion ae(src->controls.AeRegion.left(), src->controls.AeRegion.top(),
                src->controls.AeRegion.right(), src->controls.AeRegion.bottom(),
                src->controls.AeRegion.weight());
    AeRegion.push_back(ae);
    GST_ARGUS_PRINT("Setting AeRegion: %d %d %d %d %f \n",
                    AeRegion[0].left(), AeRegion[0].top(), AeRegion[0].right(),
                    AeRegion[0].bottom(), AeRegion[0].weight());

    iAutoControlSettings->setAeRegions(AeRegion);
    src->aeRegionPropSet = FALSE;
  }

  GST_ARGUS_PRINT("Setup Complete, Starting captures for %d seconds\n", secToRun);

  GST_ARGUS_PRINT("Starting repeat capture requests.\n");
  Request* captureRequest = src->request.get();
  src->request_ptr = captureRequest;
  iCaptureSession->capture(captureRequest);
  if (iCaptureSession->capture(captureRequest) == 0)
    ORIGINATE_ERROR("Failed to start capture request");

  if (src->argus_in_error)
  {
    GST_ARGUS_ERROR("InvalidState.\n");
    iCaptureSession->cancelRequests();
    src->timeout = 1;
  }
  else if (secToRun != 0)
  {
    unique_lock<mutex> lk(src->mtx);
    src->cv.wait_for(lk, chrono::seconds(secToRun), [src] {return src->stop_requested == TRUE; });
    src->timeout_complete = TRUE;
    iCaptureSession->cancelRequests();
  }
  else
  {
    if (src->stop_requested == FALSE)
    {
      g_mutex_lock (&src->eos_lock);
      g_cond_wait (&src->eos_cond, &src->eos_lock);
      g_mutex_unlock (&src->eos_lock);
    }
  }

  GST_ARGUS_PRINT("Cleaning up\n");

  iCaptureSession->stopRepeat();
  iCaptureSession->waitForIdle();

  // Argus execution completed, signal the buffer consumed cond.
  if (!src->is_argus_buffer_consumed)
  {
    g_mutex_lock (&src->argus_buffer_consumed_lock);
    g_cond_signal (&src->argus_buffer_consumed_cond);
    src->is_argus_buffer_consumed = TRUE;
    g_mutex_unlock (&src->argus_buffer_consumed_lock);
  }

  // Wait for the consumer thread to complete.
  PROPAGATE_ERROR(consumerThread.shutdown());

  if (src->queue.get() !=  NULL)
  {
    g_mutex_lock(&src->queue_lock);
    src->queue.reset();
    g_mutex_unlock(&src->queue_lock);
  }
  // Destroy the output stream. This destroys the EGLStream which causes
  // the GL consumer acquire to fail and the consumer thread to end.
  if (src->streamSettings)
    src->streamSettings.reset();
  if (src->request)
    src->request.reset();
  src->outputStream.reset();

  if (src->argus_in_error)
    return false;

  GST_ARGUS_PRINT("Done Success\n");

  return true;
}

}; // namespace ArgusCamera


GST_DEBUG_CATEGORY_STATIC (gst_nv_argus_camera_src_debug);
#define GST_CAT_DEFAULT gst_nv_argus_camera_src_debug

/* signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SILENT,
  PROP_TIMEOUT,
  PROP_WHITE_BALANCE,
  PROP_SATURATION,
  PROP_SENSOR_ID,
  PROP_SENSOR_MODE,
  PROP_TOTAL_SENSOR_MODES,
  PROP_EXPOSURE_TIME_RANGE,
  PROP_GAIN_RANGE,
  PROP_DIGITAL_GAIN_RANGE,
  PROP_TNR_STRENGTH,
  PROP_TNR_MODE,
  PROP_EDGE_ENHANCEMENT_MODE,
  PROP_EDGE_ENHANCEMENT_STRENGTH,
  PROP_AEANTIBANDING_MODE,
  PROP_EXPOSURE_COMPENSATION,
  PROP_AE_LOCK,
  PROP_AE_REGION,
  PROP_AWB_LOCK,
  PROP_EVENT_TIMEOUT,
  PROP_ACQUIRE_TIMEOUT
};

typedef struct AuxiliaryData {
  gint64 frame_num;
  gint64 timestamp;
} AuxData;

struct _GstNVArgusMemory
{
  GstMemory mem;
  GstNvArgusCameraSrcBuffer *nvcam_buf;
  /* AuxData will be shared to App, on pad_probe */
  AuxData auxData;
};

struct _GstNVArgusMemoryAllocator
{
  GstAllocator parent;
  GstNvArgusCameraSrc *owner;
};

struct _GstNVArgusMemoryAllocatorClass
{
  GstAllocatorClass parent_class;
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (CAPTURE_CAPS)
  );

typedef struct _GstNVArgusMemory GstNVArgusMemory;
typedef struct _GstNVArgusMemoryAllocator GstNVArgusMemoryAllocator;
typedef struct _GstNVArgusMemoryAllocatorClass GstNVArgusMemoryAllocatorClass;

GType gst_nv_memory_allocator_get_type (void);
#define GST_TYPE_NV_MEMORY_ALLOCATOR   (gst_nv_memory_allocator_get_type())

#define gst_nv_argus_camera_src_parent_class parent_class
G_DEFINE_TYPE (GstNvArgusCameraSrc, gst_nv_argus_camera_src, GST_TYPE_BASE_SRC);
G_DEFINE_TYPE (GstNVArgusMemoryAllocator, gst_nv_memory_allocator, GST_TYPE_ALLOCATOR);

#define GST_NVMEMORY_ALLOCATOR(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NV_MEMORY_ALLOCATOR,GstNVArgusMemoryAllocator))

static gpointer consumer_thread (gpointer src_base);

static gpointer argus_thread (gpointer src_base);

static gpointer
gst_nv_memory_map (GstMemory * mem, gsize maxsize, GstMapFlags flags)
{
  GstNVArgusMemory *nvmm_mem = (GstNVArgusMemory *) mem;
  NvBufSurface *nvbuf_surf = 0;
  gint ret = 0;
  ret = NvBufSurfaceFromFd(nvmm_mem->nvcam_buf->dmabuf_fd, (void**)(&nvbuf_surf));
  if (ret != 0 || nvbuf_surf == NULL) {
    GST_ERROR ("%s: NvBufSurfaceFromFd Failed \n", __func__);
    goto error;
  }

  return (gpointer)(nvbuf_surf);

error:
  return NULL;
}

static void
gst_nv_memory_unmap (GstMemory * mem)
{
  /* Nothing needs to be done */
}

static GstMemory *
gst_nv_memory_share (GstMemory * mem, gssize offset, gssize size)
{
  g_assert_not_reached ();
  return NULL;
}

static GstMemory *
gst_nv_memory_allocator_alloc (GstAllocator * allocator,
    gsize size, GstAllocationParams * params)
{
  gint ret = 0;
  GstNVArgusMemory *mem = NULL;
  GstNvArgusCameraSrcBuffer *nvbuf = NULL;
  GstMemoryFlags flags = GST_MEMORY_FLAG_NO_SHARE;
  NvBufSurfaceAllocateParams input_params = {{0}};

  GstNVArgusMemoryAllocator *nvmm_allocator = GST_NVMEMORY_ALLOCATOR (allocator);
  GstNvArgusCameraSrc *self = (GstNvArgusCameraSrc*) nvmm_allocator->owner;

  mem = g_slice_new0 (GstNVArgusMemory);
  nvbuf = g_slice_new0 (GstNvArgusCameraSrcBuffer);


  {
    input_params.params.width = self->width;
    input_params.params.height = self->height;
    input_params.params.layout = NVBUF_LAYOUT_BLOCK_LINEAR;
    input_params.params.colorFormat = NVBUF_COLOR_FORMAT_NV12;
    input_params.params.memType = NVBUF_MEM_SURFACE_ARRAY;
    input_params.memtag = NvBufSurfaceTag_CAMERA;

    NvBufSurface *nvbuf_surf = 0;
    ret = NvBufSurfaceAllocate (&nvbuf_surf, 1, &input_params);
    if (ret != 0) {
      GST_ERROR ("%s: NvBufSurfaceAllocate Failed \n", __func__);
      goto error;
    }
    nvbuf_surf->numFilled = 1;
    nvbuf->dmabuf_fd = nvbuf_surf->surfaceList[0].bufferDesc;

    gst_memory_init (GST_MEMORY_CAST (mem), flags, allocator, NULL,
        sizeof(NvBufSurface), 0 /* Alignment */,
        0, sizeof(NvBufSurface));
    mem->nvcam_buf = nvbuf;
    mem->nvcam_buf->dmabuf_fd = nvbuf->dmabuf_fd;
  }
  return GST_MEMORY_CAST (mem);

error:
  g_slice_free (GstNvArgusCameraSrcBuffer, nvbuf);
  g_slice_free (GstNVArgusMemory, mem);

  return NULL;
}

static void
gst_nv_memory_allocator_free (GstAllocator * allocator, GstMemory * mem)
{
  gint ret = 0;
  GstNVArgusMemory *nv_mem = (GstNVArgusMemory *) mem;
  GstNvArgusCameraSrcBuffer *nvbuf = nv_mem->nvcam_buf;
  NvBufSurface *nvbuf_surf = 0;
  ret = NvBufSurfaceFromFd(nvbuf->dmabuf_fd, (void**)(&nvbuf_surf));
  if (ret != 0 || nvbuf_surf == NULL)
    GST_ERROR ("%s: NvBufSurfaceFromFd Failed \n", __func__);
  else
  {
    ret = NvBufSurfaceDestroy(nvbuf_surf);
    if (ret != 0) {
      GST_ERROR ("%s: NvBufSurfaceDestroy Failed \n", __func__);
      goto error;
    }
  }

error:
  g_slice_free (GstNvArgusCameraSrcBuffer, nvbuf);
  g_slice_free (GstNVArgusMemory, nv_mem);
}

static void
gst_nv_memory_allocator_class_init (GstNVArgusMemoryAllocatorClass * klass)
{
  GstAllocatorClass *allocator_class;
  allocator_class = (GstAllocatorClass *) klass;

  allocator_class->alloc = gst_nv_memory_allocator_alloc;
  allocator_class->free = gst_nv_memory_allocator_free;
}

static void
gst_nv_memory_allocator_init (GstNVArgusMemoryAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = GST_NVARGUS_MEMORY_TYPE;
  alloc->mem_map = gst_nv_memory_map;
  alloc->mem_unmap = gst_nv_memory_unmap;
  alloc->mem_share = gst_nv_memory_share;

  /* default copy & is_span */
  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

static void gst_nv_argus_camera_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_nv_argus_camera_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_nv_argus_camera_src_finalize (GObject *object);

/* GObject vmethod implementations */

static GstCaps * gst_nv_argus_camera_fixate (GstBaseSrc *src, GstCaps *caps)
{
  GstStructure *structure = NULL;

  caps = gst_caps_make_writable (caps);

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_fixate_field_nearest_int (structure, "width", 1920);
  gst_structure_fixate_field_nearest_int (structure, "height", 1080);
  gst_structure_fixate_field_nearest_fraction (structure, "framerate", 30, 1);
  caps = GST_BASE_SRC_CLASS (gst_nv_argus_camera_src_parent_class)->fixate (src, caps);

  return caps;
}

static gboolean gst_nv_argus_camera_set_caps (GstBaseSrc *base, GstCaps *caps)
{
  GstVideoInfo info;
  GstCaps *old;
  GstNvArgusCameraSrc *src = GST_NVARGUSCAMERASRC (base);
  // write own allocator here

  GST_DEBUG_OBJECT (src, "Received caps %" GST_PTR_FORMAT, caps);

  if (!gst_video_info_from_caps (&info, caps))
  {
    GST_ERROR_OBJECT (src, "caps invalid");
    return FALSE;
  }

  src->width  = info.width;
  src->height = info.height;
  src->fps_n  = info.fps_n;
  src->fps_d  = info.fps_d;

  if ((old = src->outcaps) != caps)
  {
    if (caps)
      src->outcaps = gst_caps_copy (caps);
    else
      src->outcaps = NULL;
    if (old)
      gst_caps_unref (old);
  }

  if (src->pool) {
    gst_object_unref (src->pool);
    src->pool = NULL;
  }
  src->pool = gst_nvds_buffer_pool_new();
  GstStructure *config = gst_buffer_pool_get_config (src->pool);
  gst_buffer_pool_config_set_params (config, src->outcaps, sizeof(NvBufSurface), MIN_BUFFERS, MAX_BUFFERS);
  gst_structure_set (config,
        "memtype", G_TYPE_UINT, NVBUF_MEM_DEFAULT,
        "memtag", G_TYPE_UINT, NvBufSurfaceTag_CAMERA,
        "gpu-id", G_TYPE_UINT, 0,
        "batch-size", G_TYPE_UINT, 1, NULL);
  gst_buffer_pool_set_config (src->pool, config);
  src->argus_buffers = g_queue_new ();
  src->nvmm_buffers = g_queue_new ();
  gst_buffer_pool_set_active (src->pool, TRUE);

  src->consumer_thread = g_thread_new ("consumer_thread", consumer_thread, src);

  src->argus_thread = g_thread_new ("argus_thread", argus_thread, src);

  if (src->argus_in_error)
  {
    return FALSE;
  }

  return TRUE;
}

static gboolean gst_nv_argus_camera_start (GstBaseSrc * src_base)
{
  GstNvArgusCameraSrc *self = (GstNvArgusCameraSrc *) src_base;
  self->stop_requested = FALSE;

  return TRUE;
}

static gboolean gst_nv_argus_camera_unlock (GstBaseSrc *src)
{
  GstNvArgusCameraSrc *self = (GstNvArgusCameraSrc *) src;

  self->unlock_requested = TRUE;

  return TRUE;
}

static gboolean gst_nv_argus_camera_unlock_stop (GstBaseSrc *src)
{
  GstNvArgusCameraSrc *self = (GstNvArgusCameraSrc *) src;

  self->unlock_requested = FALSE;

  return TRUE;
}

static gboolean gst_nv_argus_camera_stop (GstBaseSrc * src_base)
{
  GstNvArgusCameraSrc *src = (GstNvArgusCameraSrc *) src_base;
  GstBuffer *buf;
  src->stop_requested = TRUE;
  src->cv.notify_one();

  if(!src->timeout)
  {
    g_mutex_lock (&src->eos_lock);
    g_cond_signal (&src->eos_cond);
    g_mutex_unlock (&src->eos_lock);

    ICaptureSession* l_iCaptureSession = (ICaptureSession *)src->iCaptureSession_ptr;

    if (l_iCaptureSession)
    {
      l_iCaptureSession->cancelRequests();
      l_iCaptureSession->stopRepeat();
      l_iCaptureSession->waitForIdle();
    }
  }

  g_thread_join(src->argus_thread);

  if (src->pool) {
    if (gst_buffer_pool_is_active (src->pool))
      gst_buffer_pool_set_active (src->pool, false);

    gst_object_unref (src->pool);
    src->pool = NULL;
  }
  g_thread_join(src->consumer_thread);

  while (!g_queue_is_empty (src->nvmm_buffers)) {
    buf = (GstBuffer *) g_queue_pop_head (src->nvmm_buffers);
    gst_buffer_unref (buf);
  }

  g_queue_free(src->argus_buffers);
  g_queue_free(src->nvmm_buffers);
  return TRUE;
}

static gpointer argus_thread (gpointer src_base)
{
  GstNvArgusCameraSrc *src = (GstNvArgusCameraSrc *) src_base;

  bool ret_val = 0;
  int32_t cameraIndex         = src->sensor_id;
  int32_t cameraMode          = src->sensor_mode;
  Size2D<uint32_t> streamSize (src->width, src->height);
  src->secToRun = src->timeout;
  ret_val = ArgusCamera::execute(cameraIndex, cameraMode, streamSize, src->secToRun, src);

  if (ret_val == false)
  {
    src->iCaptureSession_ptr          = NULL;
    src->iEventProvider_ptr           = NULL;
    src->iEventQueue_ptr              = NULL;
    src->iRequest_ptr                 = NULL;
    src->iAutoControlSettings_ptr     = NULL;
    src->request_ptr                  = NULL;
    src->outRequest_ptr               = NULL;
    src->iDenoiseSettings_ptr         = NULL;
    src->iEeSettings_ptr              = NULL;
    src->iRequestSourceSettings_ptr   = NULL;
  }

  if (src->outputStream)
    src->outputStream.reset();
  if (src->streamSettings)
    src->streamSettings.reset();
  if (src->request)
    src->request.reset();

  if (src->queue.get() != NULL)
  {
    g_mutex_lock(&src->queue_lock);
    src->queue.reset();
    g_mutex_unlock(&src->queue_lock);
  }

  src->stop_requested = TRUE;

  g_mutex_lock (&src->argus_buffers_queue_lock);
  g_cond_signal (&src->argus_buffers_queue_cond);
  g_mutex_unlock (&src->argus_buffers_queue_lock);

  g_mutex_lock (&src->nvmm_buffers_queue_lock);
  g_cond_signal (&src->nvmm_buffers_queue_cond);
  g_mutex_unlock (&src->nvmm_buffers_queue_lock);

  GST_DEBUG_OBJECT (src, "%s: stop_requested=%d\n", __func__, src->stop_requested);
  return src_base;
}

static gpointer
consumer_thread (gpointer src_base)
{
  gint retn = 0;
  GstBuffer *buffer = NULL;
  GstMemory *mem = NULL;
  NvArgusFrameInfo *consumerFrameInfo = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  static GQuark gst_buffer_metadata_quark = 0;
  gst_buffer_metadata_quark = g_quark_from_static_string ("GstBufferMetaData");

  GstNvArgusCameraSrc *src = (GstNvArgusCameraSrc *) src_base;

  while (FALSE == src->stop_requested)
  {
    g_mutex_lock (&src->argus_buffers_queue_lock);
    if (src->stop_requested || src->timeout_complete)
    {
      g_mutex_unlock (&src->argus_buffers_queue_lock);
      goto done;
    }
    while (g_queue_is_empty (src->argus_buffers) && src->argus_in_error == FALSE)
    {
      gint64 until;
      gboolean ret_val = false;
      until = g_get_monotonic_time () + G_TIME_SPAN_SECOND;
      ret_val = g_cond_wait_until (&src->argus_buffers_queue_cond, &src->argus_buffers_queue_lock, until);
      if (ret_val)
        break;
      else
      {
        if (src->stop_requested || src->timeout_complete)
        {
          g_mutex_unlock(&src->argus_buffers_queue_lock);
          goto done;
        }
      }
    }

    consumerFrameInfo = (NvArgusFrameInfo *) g_queue_pop_head (src->argus_buffers);
    g_mutex_unlock (&src->argus_buffers_queue_lock);
    if(&consumerFrameInfo->fd == NULL)
    {
      goto done;
    }
    ret = gst_buffer_pool_acquire_buffer (src->pool, &buffer, NULL);

    if (ret != GST_FLOW_OK)
    {
      if (!src->stop_requested || !src->timeout_complete)
      {
        GST_ERROR_OBJECT(src, "Error in pool acquire buffer");
      }
      goto done;
    }

    mem = gst_buffer_peek_memory (buffer, 0);
    GstMapInfo outmap = GST_MAP_INFO_INIT;
    if (!mem) {
      GST_ERROR_OBJECT(src, "no memory block");
      goto done;
    }
    gst_buffer_map (buffer, &outmap, GST_MAP_WRITE);
    NvBufSurface*  surf = (NvBufSurface *)outmap.data;

    NvBufSurface *nvbuf_surf = 0;
    retn = NvBufSurfaceFromFd(consumerFrameInfo->fd, (void**)(&nvbuf_surf));
    if (retn != 0) {
      GST_ERROR_OBJECT(src, "NvBufSurfaceFromFd Failed");
      goto done;
    }
    retn = NvBufSurfTransform(nvbuf_surf, surf, &src->transform_params);
    g_mutex_lock (&src->argus_buffer_consumed_lock);
    g_cond_signal (&src->argus_buffer_consumed_cond);
    src->is_argus_buffer_consumed = TRUE;
    g_mutex_unlock (&src->argus_buffer_consumed_lock);
    if (retn != 0) {
      GST_ERROR_OBJECT(src, "NvBufSurfTransform Failed");
      /* TODO: Check if need to set ->stop_requested flag in error condition */
      goto done;
    }

    gst_buffer_unmap (buffer, &outmap);

    g_mutex_lock (&src->nvmm_buffers_queue_lock);
    g_queue_push_tail (src->nvmm_buffers, buffer);
    g_cond_signal (&src->nvmm_buffers_queue_cond);
    g_mutex_unlock (&src->nvmm_buffers_queue_lock);
  }
done:
  GST_DEBUG_OBJECT (src, "%s: stop_requested=%d\n", __func__, src->stop_requested);
  if (buffer)
  {
    GstMiniObject *mobj = NULL;
    mobj = GST_MINI_OBJECT_CAST (buffer);
    if (gst_mini_object_is_writable(mobj))
      gst_mini_object_set_qdata (mobj, gst_buffer_metadata_quark, NULL, NULL);
  }
  return NULL;
}

static GstFlowReturn
gst_nv_argus_camera_create (GstBaseSrc * src_base,
    guint64 offset, guint size, GstBuffer ** buf)
{
  GstNvArgusCameraSrc *self = GST_NVARGUSCAMERASRC (src_base);
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *gst_buf = NULL;

  if (self->stop_requested || self->unlock_requested || self->timeout_complete)
    return GST_FLOW_EOS;

  g_mutex_lock (&self->nvmm_buffers_queue_lock);

  while (!self->stop_requested && !self->unlock_requested && g_queue_is_empty (self->nvmm_buffers)
    && self->argus_in_error == FALSE && !self->timeout_complete)
  {
    gint64 until;
    gboolean ret_val = false;
    until = g_get_monotonic_time () + G_TIME_SPAN_SECOND;
    ret_val = g_cond_wait_until (&self->nvmm_buffers_queue_cond, &self->nvmm_buffers_queue_lock, until);
    if (ret_val)
      break;
  }


  if (self->stop_requested || self->unlock_requested || self->timeout_complete)
  {
    g_mutex_unlock (&self->nvmm_buffers_queue_lock);
    return GST_FLOW_EOS;
  }

  gst_buf = (GstBuffer *) g_queue_pop_head (self->nvmm_buffers);
  if (gst_buf == NULL)
  {
    g_mutex_unlock (&self->nvmm_buffers_queue_lock);
    return GST_FLOW_ERROR;
  }
  *buf = gst_buf;

  g_mutex_unlock (&self->nvmm_buffers_queue_lock);

  return ret;
}

static gboolean set_range (GstNvArgusCameraSrc *src, guint prop_id)
{
  gchar **tokens;
  gchar **temp;
  gchar *token;
  gfloat array[2];
  gint index = 0;
  gfloat val;
  gboolean ret;
  gchar *str;
  NvArgusCameraRange range;

  if(prop_id == PROP_GAIN_RANGE)
  {
    str = src->gainRangeString;
    GST_ARGUS_PRINT("NvArgusCameraSrc: Setting Gain Range : %s\n", str);
  }
  else if(prop_id == PROP_EXPOSURE_TIME_RANGE)
  {
    str = src->exposureTimeString;
    GST_ARGUS_PRINT("NvArgusCameraSrc: Setting Exposure Time Range : %s\n", str);
  }
  else if(prop_id == PROP_DIGITAL_GAIN_RANGE)
  {
    str = src->ispDigitalGainRangeString;
    GST_ARGUS_PRINT("NvArgusCameraSrc: Setting ISP Digital Gain Range : %s\n", str);
  }
  else
  {
    GST_ARGUS_PRINT("NvArgusCameraSrc: property not defined\n");
    return FALSE;
  }

  if (!str)
    return FALSE;

  tokens = g_strsplit_set (str, " \"\'", -1);
  temp = tokens;
  while (*temp) {
    token = *temp++;
    if (!g_strcmp0 (token, ""))
      continue;

    if (index == 2)
    {
      GST_ARGUS_PRINT ("Invalid Range Input\n");
      ret = FALSE;
      goto done;
    }

    val = atof (token);
    array[index++] = val;
  }
  if (index == 2)
  {
     if(prop_id == PROP_GAIN_RANGE)
     {
       range.low = array[0];
       range.high = array[1];
       src->controls.gainRange = range;
     }
     else if(prop_id == PROP_EXPOSURE_TIME_RANGE)
     {
       range.low = array[0];
       range.high = array[1];
       src->controls.exposureTimeRange = range;
     }
     else if(prop_id == PROP_DIGITAL_GAIN_RANGE)
     {
       if (array[0] < MIN_DIGITAL_GAIN || array[1] > MAX_DIGITAL_GAIN) {
         GST_ARGUS_PRINT("Invalid ISP Digital Gain Range Input\n");
         ret = FALSE;
         goto done;
       }
       range.low = array[0];
       range.high = array[1];
       src->controls.ispDigitalGainRange = range;
     }
     else
     {
       GST_ARGUS_PRINT("NvArgusCameraSrc: property not defined\n");
       return FALSE;
     }
  }
  else
  {
    GST_ARGUS_PRINT ("Need two values to set range\n");
    ret = FALSE;
    goto done;
  }
  ret = TRUE;
done:
  g_strfreev (tokens);
  return ret;
}

static gboolean set_ac_region (GstNvArgusCameraSrc *src)
{
  gchar **tokens;
  gchar **temp;
  gchar *token;
  gfloat array[5];
  gint index = 0;
  gfloat val;
  gboolean ret;
  gchar *str;

  if (!src)
    return FALSE;

  str = src->aeRegionString;
  GST_ARGUS_PRINT("NvArgusCameraSrc: Setting AE REGION : %s\n", str);

  if (!str)
    return FALSE;

  tokens = g_strsplit_set (str, " \"\'", -1);
  temp = tokens;
  while (*temp) {
    token = *temp++;
    if (!g_strcmp0 (token, ""))
      continue;
    if (index == 5)
    {
      GST_ARGUS_PRINT ("Invalid Range Input\n");
      ret = FALSE;
      goto done;
    }

    val = atof (token);
    array[index++] = val;
  }
  if (index == 5)
  {
       uint32_t left = array[0];
       uint32_t top = array[1];
       uint32_t right = array[2];
       uint32_t bottom = array[3];
       float weight = array[4];
       src->controls.AeRegion = AcRegion(left, top, right, bottom, weight);
  }
  else
  {
    GST_ARGUS_PRINT ("Need five values to set range\n");
    ret = FALSE;
    goto done;
  }
  ret = TRUE;
done:
  g_strfreev (tokens);
  return ret;
}

/* initialize the nvarguscamerasrc's class */
static void
gst_nv_argus_camera_src_class_init (GstNvArgusCameraSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *base_src_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  base_src_class = (GstBaseSrcClass*) klass;

  gobject_class->set_property = gst_nv_argus_camera_src_set_property;
  gobject_class->get_property = gst_nv_argus_camera_src_get_property;
  gobject_class->finalize = gst_nv_argus_camera_src_finalize;

  base_src_class->set_caps = GST_DEBUG_FUNCPTR (gst_nv_argus_camera_set_caps);
  base_src_class->fixate = GST_DEBUG_FUNCPTR (gst_nv_argus_camera_fixate);
  base_src_class->start = GST_DEBUG_FUNCPTR (gst_nv_argus_camera_start);
  base_src_class->stop = GST_DEBUG_FUNCPTR (gst_nv_argus_camera_stop);
  base_src_class->create = GST_DEBUG_FUNCPTR (gst_nv_argus_camera_create);
  base_src_class->unlock = GST_DEBUG_FUNCPTR (gst_nv_argus_camera_unlock);
  base_src_class->unlock_stop = GST_DEBUG_FUNCPTR (gst_nv_argus_camera_unlock_stop);

  g_object_class_install_property (gobject_class, PROP_WHITE_BALANCE,
      g_param_spec_enum ("wbmode", "white balance mode",
          "White balance affects the color temperature of the photo",
          GST_TYPE_NVARGUSCAM_WB_MODE, NVARGUSCAM_DEFAULT_WB_MODE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_SATURATION,
      g_param_spec_float ("saturation", "saturation",
          "Property to adjust saturation value",
          0.0, 2.0, NVARGUSCAM_DEFAULT_SATURATION,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, (GParamFlags) G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_TIMEOUT,
      g_param_spec_uint ("timeout", "timeout",
          "timeout to capture in seconds (Either specify timeout or num-buffers, not both)",
          0, G_MAXINT, 0, (GParamFlags) G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SENSOR_ID,
      g_param_spec_int ("sensor-id", "Sensor ID",
          "Set the id of camera sensor to use. Default 0.",
          0, G_MAXUINT8, NVARGUSCAM_DEFAULT_SENSOR_ID,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_SENSOR_MODE,
      g_param_spec_int ("sensor-mode", "Sensor Mode",
          "Set the camera sensor mode to use. Default -1 (Select the best match)",
          -1, G_MAXUINT8, NVARGUSCAM_DEFAULT_SENSOR_MODE_STATE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_TOTAL_SENSOR_MODES,
      g_param_spec_int ("total-sensor-modes", "Total Sensor Modes",
          "Query the number of sensor modes available. Default 0",
          0, G_MAXUINT8, NVARGUSCAM_DEFAULT_TOTAL_SENSOR_MODES,
          (GParamFlags) (G_PARAM_READABLE)));

  g_object_class_install_property (gobject_class, PROP_EXPOSURE_TIME_RANGE,
      g_param_spec_string ("exposuretimerange", "Exposure Time Range",
          "Property to adjust exposure time range in nanoseconds\n"
          "\t\t\tUse string with values of Exposure Time Range (low, high)\n"
          "\t\t\tin that order, to set the property.\n"
          "\t\t\teg: exposuretimerange=\"34000 358733000\"",
          NVARGUSCAM_DEFAULT_EXPOSURE_TIME, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_AE_REGION,
      g_param_spec_string ("aeregion", "AE Region",
          "Property to set region of interest for auto exposure\n"
          "\t\t\twith values of ROI coordinates (left, top, right, bottom)\n"
          "\t\t\tand weight (float number) in that order, to set the property\n"
          "\t\t\tuse for example: aeregion=\"0 0 256 256 1\"",
          NULL, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_GAIN_RANGE,
      g_param_spec_string ("gainrange", "Gain Range",
          "Property to adjust gain range\n"
          "\t\t\tUse string with values of Gain Time Range (low, high)\n"
          "\t\t\tin that order, to set the property.\n"
          "\t\t\teg: gainrange=\"1 16\"",
          NVARGUSCAM_DEFAULT_GAIN_RANGE, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_DIGITAL_GAIN_RANGE,
      g_param_spec_string ("ispdigitalgainrange", "ISP Digital Gain Range",
          "Property to adjust digital gain range\n"
          "\t\t\tUse string with values of ISP Digital Gain Range (low, high)\n"
          "\t\t\tin that order, to set the property.\n"
          "\t\t\teg: ispdigitalgainrange=\"1 8\"",
          NVARGUSCAM_DEFAULT_DIGITAL_GAIN_RANGE, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_TNR_STRENGTH,
      g_param_spec_float ("tnr-strength", "TNR Strength",
          "property to adjust temporal noise reduction strength",
          -1.0, 1.0, NVARGUSCAM_DEFAULT_TNR_STRENGTH,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_TNR_MODE,
      g_param_spec_enum ("tnr-mode", "TNR mode",
          "property to select temporal noise reduction mode",
          GST_TYPE_NVARGUSCAM_TNR_MODE, NVARGUSCAM_DEFAULT_TNR_MODE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_EDGE_ENHANCEMENT_STRENGTH,
      g_param_spec_float ("ee-strength", "TNR Strength",
          "property to adjust edge enhancement strength",
          -1.0, 1.0, NVARGUSCAM_DEFAULT_EE_STRENGTH,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_EDGE_ENHANCEMENT_MODE,
      g_param_spec_enum ("ee-mode", "Edge Enhancement",
          "property to select edge enhnacement mode",
          GST_TYPE_NVARGUSCAM_EDGE_ENHANCEMENT_MODE, NVARGUSCAM_DEFAULT_EE_MODE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_AEANTIBANDING_MODE,
      g_param_spec_enum ("aeantibanding", "Auto Exposure Antibanding Mode",
          "property to set the auto exposure antibanding mode",
          GST_TYPE_NVARGUSCAM_AEANTIBANDING_MODE, NVARGUSCAM_DEFAULT_AEANTIBANDING_MODE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_EXPOSURE_COMPENSATION,
      g_param_spec_float ("exposurecompensation", "Exposure Compensation",
          "property to adjust exposure compensation",
          -2.0, 2.0, NVARGUSCAM_DEFAULT_EXP_COMPENSATION,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_AE_LOCK,
      g_param_spec_boolean ("aelock", "AE Lock",
          "set or unset the auto exposure lock",
          FALSE, (GParamFlags) G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_AWB_LOCK,
      g_param_spec_boolean ("awblock", "AWB Lock",
          "set or unset the auto white balance lock",
          FALSE, (GParamFlags) G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_EVENT_TIMEOUT,
      g_param_spec_uint64 ("event-wait", "Event Timeout (in nanosec)",
          "Set the waiting time for the event before timeout. Default 3000000000 (in nanosec)",
          DEFAULT_WAIT_FOR_EVENT_TIMEOUT, G_MAXUINT64, DEFAULT_WAIT_FOR_EVENT_TIMEOUT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_ACQUIRE_TIMEOUT,
      g_param_spec_uint64 ("acquire-wait", "Acquire Frame Timeout (in nanosec)",
          "Set the waiting time for the acquireFrame before timeout. Default 5000000000 (in nanosec)",
          DEFAULT_ACQUIRE_FRAME_TIMEOUT, G_MAXUINT64, DEFAULT_ACQUIRE_FRAME_TIMEOUT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_details_simple(gstelement_class,
    "NvArgusCameraSrc",
    "Video/Capture",
    "nVidia ARGUS Camera Source",
    "Viranjan Pagar <vpagar@nvidia.com>, Amit Pandya <apandya@nvidia.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_nv_argus_camera_src_init (GstNvArgusCameraSrc * src)
{
  src->width = 1920;
  src->height = 1080;
  src->fps_n = 30;
  src->fps_d = 1;
  src->stop_requested = FALSE;
  src->unlock_requested = FALSE;
  src->silent = TRUE;
  src->outcaps = NULL;
  src->timeout = 0;
  src->event_timeout = DEFAULT_WAIT_FOR_EVENT_TIMEOUT;
  src->acquire_timeout = DEFAULT_ACQUIRE_FRAME_TIMEOUT;
  src->argus_in_error = FALSE;
  src->timeout_complete = FALSE;
  src->sensor_id = NVARGUSCAM_DEFAULT_SENSOR_ID;
  src->sensor_mode = NVARGUSCAM_DEFAULT_SENSOR_MODE_STATE;
  src->total_sensor_modes = NVARGUSCAM_DEFAULT_TOTAL_SENSOR_MODES;
  src->controls.NoiseReductionStrength = NVARGUSCAM_DEFAULT_TNR_STRENGTH;
  src->controls.NoiseReductionMode = NVARGUSCAM_DEFAULT_TNR_MODE;
  src->controls.wbmode = NVARGUSCAM_DEFAULT_WB_MODE;
  src->controls.saturation = NVARGUSCAM_DEFAULT_SATURATION;
  src->controls.EdgeEnhancementStrength = NVARGUSCAM_DEFAULT_EE_STRENGTH;
  src->controls.EdgeEnhancementMode = NVARGUSCAM_DEFAULT_EE_MODE;
  src->controls.AeAntibandingMode = NVARGUSCAM_DEFAULT_AEANTIBANDING_MODE;
  src->controls.AeLock = NVARGUSCAM_DEFAULT_AE_LOCK;
  src->controls.AwbLock = NVARGUSCAM_DEFAULT_AWB_LOCK;

  g_mutex_init (&src->argus_buffers_queue_lock);
  g_cond_init (&src->argus_buffers_queue_cond);

  g_mutex_init (&src->argus_buffer_consumed_lock);
  g_cond_init (&src->argus_buffer_consumed_cond);

  g_mutex_init (&src->nvmm_buffers_queue_lock);
  g_cond_init (&src->nvmm_buffers_queue_cond);

  memset(&src->transform_params, 0, sizeof(NvBufSurfTransformParams));

  g_mutex_init (&src->eos_lock);
  g_cond_init (&src->eos_cond);

  g_mutex_init(&src->queue_lock);

  gst_base_src_set_live (GST_BASE_SRC (src), TRUE);
  gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);
  gst_base_src_set_do_timestamp (GST_BASE_SRC (src), TRUE);
}


static void gst_nv_argus_camera_src_finalize (GObject *object)
{
  GstNvArgusCameraSrc *src= GST_NVARGUSCAMERASRC (object);
  GST_DEBUG_OBJECT (src, "finalize");
  g_mutex_clear (&src->nvmm_buffers_queue_lock);
  g_cond_clear (&src->nvmm_buffers_queue_cond);
  g_mutex_clear (&src->argus_buffers_queue_lock);
  g_cond_clear (&src->argus_buffers_queue_cond);
  g_mutex_clear (&src->argus_buffer_consumed_lock);
  g_cond_clear (&src->argus_buffer_consumed_cond);
  g_mutex_clear (&src->eos_lock);
  g_cond_clear (&src->eos_cond);
  g_mutex_clear(&src->queue_lock);
  if(src->exposureTimeString) {
    g_free (src->exposureTimeString);
    src->exposureTimeString = NULL;
  }
  if(src->gainRangeString) {
    g_free (src->gainRangeString);
    src->gainRangeString = NULL;
  }
  if(src->ispDigitalGainRangeString) {
    g_free (src->ispDigitalGainRangeString);
    src->ispDigitalGainRangeString = NULL;
  }
  if(src->outcaps) {
    gst_caps_unref (src->outcaps);
    src->outcaps = NULL;
  }
}

static void
gst_nv_argus_camera_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstNvArgusCameraSrc *src = GST_NVARGUSCAMERASRC (object);

  switch (prop_id)
  {
    case PROP_SILENT:
      src->silent = g_value_get_boolean (value);
      break;
    case PROP_TIMEOUT:
      src->timeout = g_value_get_uint (value);
      break;
    case PROP_WHITE_BALANCE:
      src->controls.wbmode = (NvArgusCamAwbMode) g_value_get_enum (value);
      src->wbPropSet = TRUE;
      break;
    case PROP_SATURATION:
      src->controls.saturation = g_value_get_float (value);
      src->saturationPropSet = TRUE;
      break;
    case PROP_SENSOR_ID:
      src->sensor_id = g_value_get_int (value);
      break;
    case PROP_SENSOR_MODE:
      src->sensor_mode = g_value_get_int (value);
      break;
    case PROP_EXPOSURE_TIME_RANGE: {
      gchar *prev_exposureTime = NULL;
      prev_exposureTime = src->exposureTimeString;

      src->exposureTimeString = (gchar *)g_value_dup_string(value);
      if (!set_range (src, prop_id)) {
        g_free (src->exposureTimeString);
        src->exposureTimeString = prev_exposureTime;
      } else {
        g_free (prev_exposureTime);
        src->exposureTimePropSet = TRUE;
      }
    }
      break;
    case PROP_AE_REGION: {
      gchar *prev_aeregion = NULL;
      prev_aeregion = src->aeRegionString;

      src->aeRegionString = (gchar *)g_value_dup_string(value);
      if (!set_ac_region (src)) {
        g_free (src->aeRegionString);
        src->aeRegionString = prev_aeregion;

      } else {
        g_free (prev_aeregion);
        src->aeRegionPropSet = TRUE;
      }
    }
      break;
    case PROP_GAIN_RANGE: {
      gchar *prev_gainrange = NULL;
      prev_gainrange = src->gainRangeString;

      src->gainRangeString = (gchar *)g_value_dup_string(value);
      if (!set_range (src, prop_id)) {
        g_free (src->gainRangeString);
        src->gainRangeString = prev_gainrange;
      } else {
        g_free (prev_gainrange);
        src->gainRangePropSet = TRUE;
      }
    }
      break;
    case PROP_DIGITAL_GAIN_RANGE: {
      gchar *prev_ispdigitalgainrange = NULL;
      prev_ispdigitalgainrange = src->ispDigitalGainRangeString;

      src->ispDigitalGainRangeString = (gchar *)g_value_dup_string(value);
      if (!set_range (src, prop_id)) {
        g_free (src->ispDigitalGainRangeString);
        src->ispDigitalGainRangeString = prev_ispdigitalgainrange;
      } else {
        g_free (prev_ispdigitalgainrange);
        src->ispDigitalGainRangePropSet = TRUE;
      }
    }
      break;
    case PROP_TNR_STRENGTH:
      src->controls.NoiseReductionStrength = g_value_get_float (value);
      src->tnrStrengthPropSet = TRUE;
      break;
    case PROP_TNR_MODE:
      src->controls.NoiseReductionMode =
          (NvArgusCamNoiseReductionMode) g_value_get_enum (value);
      src->tnrModePropSet = TRUE;
      break;
    case PROP_EDGE_ENHANCEMENT_STRENGTH:
      src->controls.EdgeEnhancementStrength = g_value_get_float (value);
      src->edgeEnhancementStrengthPropSet = TRUE;
      break;
    case PROP_EDGE_ENHANCEMENT_MODE:
      src->controls.EdgeEnhancementMode =
          (NvArgusCamEdgeEnhancementMode) g_value_get_enum (value);
      src->edgeEnhancementModePropSet = TRUE;
      break;
    case PROP_AEANTIBANDING_MODE:
      src->controls.AeAntibandingMode =
          (NvArgusCamAeAntibandingMode) g_value_get_enum (value);
      src->aeAntibandingPropSet = TRUE;
      break;
    case PROP_EXPOSURE_COMPENSATION:
      src->controls.ExposureCompensation = g_value_get_float (value);
      src->exposureCompensationPropSet = TRUE;
      break;
    case PROP_AE_LOCK:
      src->controls.AeLock = g_value_get_boolean (value);
      src->aeLockPropSet = TRUE;
      break;
    case PROP_AWB_LOCK:
      src->controls.AwbLock = g_value_get_boolean (value);
      src->awbLockPropSet = TRUE;
      break;
    case PROP_EVENT_TIMEOUT:
      src->event_timeout = g_value_get_uint64(value);
      break;
    case PROP_ACQUIRE_TIMEOUT:
      src->acquire_timeout = g_value_get_uint64(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_nv_argus_camera_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstNvArgusCameraSrc *src = GST_NVARGUSCAMERASRC (object);

  switch (prop_id)
  {
    case PROP_SILENT:
      g_value_set_boolean (value, src->silent);
      break;
    case PROP_TIMEOUT:
      g_value_set_uint (value, src->timeout);
      break;
    case PROP_WHITE_BALANCE:
      g_value_set_enum (value, src->controls.wbmode);
      break;
    case PROP_SATURATION:
      g_value_set_float (value, src->controls.saturation);
      break;
    case PROP_SENSOR_ID:
      g_value_set_int (value, src->sensor_id);
      break;
    case PROP_SENSOR_MODE:
      g_value_set_int (value, src->sensor_mode);
      break;
    case PROP_TOTAL_SENSOR_MODES:
      g_value_set_int (value, src->total_sensor_modes);
      break;
    case PROP_EXPOSURE_TIME_RANGE:
      g_value_set_string (value, src->exposureTimeString);
      break;
    case PROP_AE_REGION:
      g_value_set_string (value, src->aeRegionString);
      break;
    case PROP_GAIN_RANGE:
      g_value_set_string (value, src->gainRangeString);
      break;
    case PROP_DIGITAL_GAIN_RANGE:
      g_value_set_string (value, src->ispDigitalGainRangeString);
      break;
    case PROP_TNR_STRENGTH:
      g_value_set_float (value, src->controls.NoiseReductionStrength);
      break;
    case PROP_TNR_MODE:
      g_value_set_enum (value, src->controls.NoiseReductionMode);
      break;
    case PROP_EDGE_ENHANCEMENT_MODE:
      g_value_set_enum (value, src->controls.EdgeEnhancementMode);
      break;
    case PROP_EDGE_ENHANCEMENT_STRENGTH:
      g_value_set_float (value, src->controls.EdgeEnhancementStrength);
      break;
    case PROP_AEANTIBANDING_MODE:
      g_value_set_enum (value, src->controls.AeAntibandingMode);
      break;
    case PROP_EXPOSURE_COMPENSATION:
      g_value_set_float (value, src->controls.ExposureCompensation);
      break;
    case PROP_AE_LOCK:
      g_value_set_boolean (value, src->controls.AeLock);
      break;
    case PROP_AWB_LOCK:
      g_value_set_boolean (value, src->controls.AwbLock);
      break;
    case PROP_EVENT_TIMEOUT:
      g_value_set_uint64 (value, src->event_timeout);
      break;
    case PROP_ACQUIRE_TIMEOUT:
      g_value_set_uint64 (value, src->acquire_timeout);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
nvarguscamerasrc_init (GstPlugin * nvarguscamerasrc)
{
  /* debug category for fltering log messages
   *
   * exchange the string 'Template nvarguscamerasrc' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_nv_argus_camera_src_debug, "nvarguscamerasrc",
    0, "nvarguscamerasrc");

  return gst_element_register (nvarguscamerasrc, "nvarguscamerasrc", GST_RANK_PRIMARY,
    GST_TYPE_NVARGUSCAMERASRC);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "nvarguscamerasrc"
#endif

/* gstreamer looks for this structure to register nvarguscamerasrcs
 *
 * exchange the string 'Template nvarguscamerasrc' with your nvarguscamerasrc description
 */
GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  nvarguscamerasrc,
  "nVidia ARGUS Source Component",
  nvarguscamerasrc_init,
  "1.0.0",
  "Proprietary",
  "NvARGUSCameraSrc",
  "http://nvidia.com/"
)

#ifdef __cplusplus
}
#endif
