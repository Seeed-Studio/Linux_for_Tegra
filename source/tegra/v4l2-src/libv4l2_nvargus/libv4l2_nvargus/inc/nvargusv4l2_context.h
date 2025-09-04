/*
 * Copyright (c) 2020-2023, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __NVARGUSV4L2_CONTEXT_H__
#define __NVARGUSV4L2_CONTEXT_H__

#include <pthread.h>
#include "nvbufsurface.h"
#include "nvargusv4l2_os.h"
#include "nvargusv4l2_nvqueue.h"
#include "linux/videodev2.h"
#include "v4l2_nv_extensions.h"
#include "Thread.h"
#include <Argus/Argus.h>
#include <EGLStream/EGLStream.h>
#include "EGLGlobal.h"

#define MAX_CONTEXTS 32
#define FRAMESIZE_MIN_WIDTH 48
#define FRAMESIZE_MIN_HEIGHT 48
#define MAX_OP_BUFFERS 32
#define MIN_OP_BUFFERS 6
#define MAX_PLANES 4

#ifdef _DEBUG
#define DEBUG_LOGS 0
#else
#define DEBUG_LOGS 0
#endif

#define DBG_PRINT(...) \
        do { \
            if (DEBUG_LOGS) \
                fprintf(stderr, "LIBV4L2ARGUS: " __VA_ARGS__); \
        } while (0)

#define REL_PRINT(...) \
        do { \
            if (runtime_logs_enabled) \
            { \
                fprintf(stderr, "(tid) : %x ", (unsigned int) pthread_self()); \
                fprintf(stderr, "LIBV4L2ARGUS: " __VA_ARGS__); \
            } \
        } while (0)

#define ERROR_PRINT(...) \
        do { \
            fprintf(stderr, "LIBV4L2ARGUS ERROR: " __VA_ARGS__); \
        } while (0)

#define V4L2_BUFFER_TYPE_SUPPORTED_OR_ERROR(buffer_type) \
        if (buffer_type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) { \
            printf("Unsupported buffer type\n"); \
            return EINVAL; }

#define V4L2_MEMORY_TYPE_SUPPORTED_OR_ERROR(memory_type) \
        if (memory_type != V4L2_MEMORY_MMAP && memory_type != V4L2_MEMORY_DMABUF) { \
            printf("Unsupported memory type\n"); \
            return EINVAL; }

extern Argus::CameraProvider *g_cameraProvider;
extern Argus::ICameraProvider *g_iCameraProvider;
extern std::vector<Argus::CameraDevice*> g_cameraDevices;

/* This structure identifies an instance which is created when
 * argusv4l2_open() is called
 */
typedef struct v4l2_context_rec {
    NvMutexHandle ioctl_mutex;

    /* Pointer to the camera context */
    void *actual_context;
} v4l2_context;

/* Structure to store Argus color format information */
typedef struct {
    uint32_t width, height;
    /* V4L2 fourcc */
    uint32_t pixelformat;
    Argus::PixelFormat argus_format;
    /* color depth */
    uint32_t bitdepth;
} cam_params;

/* The structure store the information needed for V4L2_ENUM_FRAMESIZES.
 * This is used to store minimum & maximum width height, frame duration
 * supported for a given format.
 */
typedef struct {
    uint32_t min_width;
    uint32_t min_height;
    uint32_t max_width;
    uint32_t max_height;
    uint32_t bitdepth;
    float frame_duration;
    float minGainRange;
    float maxGainRange;
    uint64_t minExposureTimeRange;
    uint64_t maxExposureTimeRange;
} argusv4l2_sensormode;

/* Defines v4l2_buffer */
typedef struct camerav4l2_buffer_rec {
    /* Buffer Index*/
    int32_t buffer_id;
    /* Surface List params for allocated buffer.*/
    NvBufSurfaceParams surf_params;
    /* Represent state of buffer*/
    uint32_t flags;
    /* Repesent that buffer structure is allocated or not*/
    uint32_t allocated;
    /* Stores the buf_fd returned by nvbuf_utils API */
    uint32_t buf_fd;
    /* Memory representation of argus buffer mapped to the NvMMBuffer FD */
    Argus::Buffer* argus_buffer;
} camerav4l2_buffer;

/* The strcuture stores metadata retrieved from acquired buffer */
struct argusframe_metadata {
    bool            aeLocked;
    bool            colorCorrectionMatrixEnable;
    bool            toneMapCurveEnabled;
    float           ispDigitalGain;
    float           sceneLux;
    float           sensorAnalogGain;
    int32_t         focuserPosition;
    uint32_t        awbCct;
    uint32_t        id;
    uint32_t        sensorSensitivity;
    uint64_t        frameDuration;
    uint64_t        frameReadoutTime;
    uint64_t        sensorExposureTime;
    uint64_t        sensorTimestamp;
    Argus::AeState  aeState;
    Argus::AwbState awbState;

    argusframe_metadata()
        : aeState(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "AE_STATE_UNKNOWN")
        , awbState(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "AWB_STATE_UNKNOWN")
    {}
};

/* The structure stores the Argus controls values */
typedef struct argus_settings_rec {
    bool awbLock;
    bool aeLock;
    bool enableColorSaturation;
    int32_t sensorModeIdx;
    uint32_t denoiseMode;
    uint32_t edgeEnhanceMode;
    uint32_t aeAntibandingMode;
    uint32_t awbMode;
    float frameDuration;
    float edgeEnhanceStrength;
    float denoiseStrength;
    float exposureCompensation;
    float colorSaturation;
    Argus::Range<float> ispDigitalGainRange;
    Argus::Range<float> gainRange;
    Argus::Range<uint64_t> exposureTimeRange;
    Argus::IAutoControlSettings *autoControlSettingsPtr;
    Argus::IDenoiseSettings *denoiseSettingsPtr;
    Argus::IEdgeEnhanceSettings *EESettingsPtr;
} cam_settings;

/* This structure defines the camera context structure.
*/
typedef struct v4l2_camera_context_rec {

    /* Context mutex, used for simultaneous access from multiple threads.*/
    NvMutexHandle context_mutex;
    /* Stats mutex, used for atomic update/read/access statistics */
    NvMutexHandle stats_mutex;
    /* Semaphore for dqbuf on capture plane */
    NvSemaphoreHandle acquirebuf_sema;

    /* Queue for buffer indices which are returned from Argus,
       ready to be DQBUF'ed on capture plane */
    NvQueueHandle capplane_Q;
    /* Thread for argus capture processing */
    NvThreadHandle argus_capture_thread;

    bool blocking_mode;
    bool stream_on;
    bool error_flag;
    /* This flag if set indicates that Argus is in STOP state */
    bool camera_state_stopped;
    /* Number of buffers on capture plane for V4L2 */
    uint32_t capture_buffer_count;
    /* Number of queued buffer */
    uint32_t num_queued_capture_buffers;
    /* Either DMABUF or MMAP */
    uint32_t capture_memory_type;

    cam_params argus_params;
    cam_settings argus_settings;
    camerav4l2_buffer cap_buffers[MAX_OP_BUFFERS];
    argusframe_metadata *frame_metadata[MAX_OP_BUFFERS];
    void* inbuf_mapped_address[MAX_OP_BUFFERS][MAX_PLANES];

    /* Argus Members */
    Argus::Status m_cameraStatus;
    Argus::UniqueObj<Argus::CaptureSession> m_captureSession;
    Argus::ICaptureSession *m_iCaptureSession;
    Argus::IEventProvider *m_iEventProvider;
    Argus::IEventQueue *m_iEventQueue;
    Argus::UniqueObj<Argus::EventQueue> m_eventQueue;
    Argus::ICameraProperties *m_iCameraProperties;
    Argus::UniqueObj<Argus::OutputStreamSettings> m_streamSettings;
    Argus::IBufferOutputStreamSettings *m_iStreamSettings;
    Argus::UniqueObj<Argus::OutputStream> m_outputStream;
    Argus::IBufferOutputStream *m_iStream;
    Argus::UniqueObj<Argus::Request> m_request;
    Argus::IRequest *m_iRequest;
    Argus::ISourceSettings *m_iSourceSettings;
    const Argus::CaptureMetadata *m_captureMetadata;
    std::vector<argusv4l2_sensormode> argusv4l2_sensormodes;
    std::vector<Argus::SensorMode*> m_sensorModes;
    std::vector<Argus::EventType> m_eventTypes;

} v4l2_camera_context;

/* Returns a context from a fd */
v4l2_context* v4l2_get_context(int32_t fd);

int initialize_camera_provider();

/* Queries and fills the data of all avaiable sensor modes from Argus */
void v4l2_argus_fill_sensor_modes(std::vector<Argus::SensorMode*> sensorModes,
        std::vector<argusv4l2_sensormode> *argusv4l2_sensormodes);

/* Initializes argus environment when a valid context gets created */
int32_t initialize_arguscamera(v4l2_camera_context *camera_ctx, uint32_t camera_index);

/* Sets argus default parameters */
void v4l2_argus_set_defaults(cam_params *argus_defaults, cam_settings *argus_settings);

/* Opens a new context */
int32_t v4l2_open_camera_context(uint32_t camera_index, int flags);

/* Closes the contexts and destroys all the memory associated with it.*/
void v4l2_close_camera_context(int32_t fd);

/* Tries to acquire the global mutex to avoid two open/close contexts at a time */
void v4l2_lock_global_mutex(void);

/* Releases the global mutex to avoid two open/close contexts at a time */
void v4l2_unlock_global_mutex(void);

/* This flag is set if /tmp/argusv4l2_logs file exists. This is checked
   when the library is loaded. This mechanism provides conditional logging
   in the same binary based on environment parameters.*/
extern bool runtime_logs_enabled;

#endif /* __NVARGUSV4L2_CONTEXT_H__ */
