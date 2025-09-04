/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>

#include "argus_camera_sw_encode.h"

using namespace ArgusSamples;

/* Constant configuration */
const int    MAX_ENCODER_FRAMES = 5;
const int    DEFAULT_FPS        = 30;
enum pix_fmt ENCODER_INPIXFMT   = NV12;

/* This value is tricky.
   Too small value will impact the FPS */
const int    NUM_BUFFERS        = 10;

/* Configurations which can be overrided by cmdline */
int          CAPTURE_TIME = 5; // In seconds.
uint32_t     CAMERA_INDEX = 0;
Size2D<uint32_t> STREAM_SIZE (640, 480);
std::string  OUTPUT_FILENAME ("output.h264");
bool         VERBOSE_ENABLE = false;

EGLDisplay eglDisplay = EGL_NO_DISPLAY;

DmaBuffer* DmaBuffer::create(const Argus::Size2D<uint32_t>& size,
                         NvBufSurfaceColorFormat colorFormat,
                         NvBufSurfaceLayout layout)
{
    DmaBuffer* buffer = new DmaBuffer(size);
    if (!buffer)
        return NULL;

    NvBufSurf::NvCommonAllocateParams cParams;

    cParams.memtag = NvBufSurfaceTag_CAMERA;
    cParams.width = size.width();
    cParams.height = size.height();
    cParams.colorFormat = colorFormat;
    cParams.layout = layout;
    cParams.memType = NVBUF_MEM_SURFACE_ARRAY;

    if (NvBufSurf::NvAllocate(&cParams, 1, &buffer->m_fd))
    {
        delete buffer;
        return NULL;
    }

    return buffer;
}

DmaBuffer* DmaBuffer::fromArgusBuffer(Buffer *buffer)
{
    IBuffer* iBuffer = interface_cast<IBuffer>(buffer);
    const DmaBuffer *dmabuf = static_cast<const DmaBuffer*>(iBuffer->getClientData());

    return const_cast<DmaBuffer*>(dmabuf);
}

ConsumerThread::ConsumerThread(OutputStream* stream) :
        m_stream(stream),
        m_enc_ctx(NULL),
        m_frame(NULL),
        m_pkt(NULL),
        m_outputFile(NULL),
        m_gotError(false),
        m_blitSurf(NULL)
{
}

ConsumerThread::~ConsumerThread()
{
    if (m_outputFile)
        delete m_outputFile;
}

bool ConsumerThread::threadInitialize()
{
    /* Create Video Encoder */
    if (!createVideoEncoder())
        ORIGINATE_ERROR("Failed to create video VideoEncoder");

    /* Create output file */
    m_outputFile = new std::ofstream(OUTPUT_FILENAME.c_str());
    if (!m_outputFile)
        ORIGINATE_ERROR("Failed to open output file.");

    return true;
}

bool ConsumerThread::threadExecute()
{
    IBufferOutputStream* stream = interface_cast<IBufferOutputStream>(m_stream);
    DmaBuffer *dmabuf = NULL;
    Argus::Status status = STATUS_OK;
    if (!stream)
        ORIGINATE_ERROR("Failed to get IBufferOutputStream interface");

    for (int bufferIndex = 0; bufferIndex < MAX_ENCODER_FRAMES; bufferIndex++)
    {
        Buffer* buffer = stream->acquireBuffer(TIMEOUT_INFINITE, &status);
        if (status != STATUS_OK)
        {
            m_gotError = true;
            ORIGINATE_ERROR("Failed to acquire camera buffer");
            break;
        }
        /* Convert Argus::Buffer to DmaBuffer*/
        dmabuf = DmaBuffer::fromArgusBuffer(buffer);

        int dmabuf_fd = dmabuf->getFd();
        if (VERBOSE_ENABLE)
            CONSUMER_PRINT("Acquired Frame:%d fd:%d\n", m_count, dmabuf_fd);
        /* encode the image */
        libavEncode(m_enc_ctx, m_frame, m_pkt, dmabuf_fd);

        /* Release the frame */
        stream->releaseBuffer(buffer);
        if (VERBOSE_ENABLE)
            CONSUMER_PRINT("Released frame:%d fd:%d\n", m_count - 1, dmabuf->getFd());
    }

    /* Keep acquire frames and queue into encoder */
    while (!m_gotError)
    {
        /* Acquire a Buffer from a completed capture request */
        Buffer* buffer = stream->acquireBuffer(TIMEOUT_INFINITE, &status);
        if (status == STATUS_END_OF_STREAM)
        {
            /* Timeout or error happen, exit */
            break;
        }
        else if (status != STATUS_OK)
        {
            ORIGINATE_ERROR("Failed to acquire camera buffer");
            break;
        }

        /* Convert Argus::Buffer to DmaBuffer and get FD */
        dmabuf = DmaBuffer::fromArgusBuffer(buffer);
        int dmabuf_fd = dmabuf->getFd();

        if (VERBOSE_ENABLE)
            CONSUMER_PRINT("Acquired Frame:%d fd:%d\n", m_count, dmabuf_fd);

        /* Push the frame to libavEncoder. */
        CHECK_ERROR(libavEncode(m_enc_ctx, m_frame, m_pkt, dmabuf_fd));

        stream->releaseBuffer(buffer);
        if (VERBOSE_ENABLE)
            CONSUMER_PRINT("Released frame:%d fd:%d\n", m_count - 1, dmabuf->getFd());
    }

    /* Send EOS and flush the encoder */
    CHECK_ERROR(libavEncode(m_enc_ctx, NULL, m_pkt, -1));

    CONSUMER_PRINT("Done.\n");

    requestShutdown();

    return true;
}

bool ConsumerThread::threadShutdown()
{
    /* Destroy Video Encoder */
    if (!destroyVideoEncoder())
        ORIGINATE_ERROR("Failed to destroy video VideoEncoder");

    return true;
}

bool ConsumerThread::createVideoEncoder()
{
    int ret = 0;
    NvBufSurfaceAllocateParams input_params = { 0 };

    codec = avcodec_find_encoder_by_name("libx264");
    if (!codec)
        ORIGINATE_ERROR("Codec 'libx264' not found. Please install x264");

    m_enc_ctx = avcodec_alloc_context3(codec);
    if (!m_enc_ctx)
        ORIGINATE_ERROR("Could not allocate video codec context");

    m_count = 0;
    // Setting common properties of encoder available as part of AVCodecContext
    // Ref: https://libav.org/documentation/doxygen/master/structAVCodecContext.html

    /* put sample parameters */
    m_enc_ctx->bit_rate = 4000000;
    m_enc_ctx->width = STREAM_SIZE.width();
    m_enc_ctx->height = STREAM_SIZE.height();
    /* frames per second */
    m_enc_ctx->time_base = (AVRational){1, 30};
    m_enc_ctx->framerate = (AVRational){30, 1};
    m_enc_ctx->gop_size = 30;
    m_enc_ctx->max_b_frames = 0;
    m_enc_ctx->pix_fmt = (ENCODER_INPIXFMT == NV12) ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P;
    m_enc_ctx->profile = FF_PROFILE_H264_HIGH;

    // Setting some specific properties of libx264
    // Ref: http://www.chaneru.com/Roku/HLS/X264_Settings.htm
    av_opt_set(m_enc_ctx->priv_data, "level", "5", 0);
    av_opt_set(m_enc_ctx->priv_data, "preset", "ultrafast", 0);
    // Set CBR mode in libx264
    av_opt_set(m_enc_ctx->priv_data, "bitrate", "4000000", 0);
    av_opt_set(m_enc_ctx->priv_data, "vbv-maxrate", "4000000", 0);
    av_opt_set(m_enc_ctx->priv_data, "vbv-bufsize", "4000000", 0);
    // Set num of reference to 1
    av_opt_set(m_enc_ctx->priv_data, "ref", "1", 0);

    /* open the encoder */
    ret = avcodec_open2(m_enc_ctx, codec, NULL);
    if (ret < 0)
        ORIGINATE_ERROR("Could not open libx264 encoder");

    m_frame = av_frame_alloc();
    if (!m_frame)
        ORIGINATE_ERROR("Could not allocate video frame");

    m_frame->format = m_enc_ctx->pix_fmt; // Camera input is NV12 format by default
    m_frame->width  = STREAM_SIZE.width();
    m_frame->height = STREAM_SIZE.height();

    ret = av_frame_get_buffer(m_frame, 0);
    if (ret < 0)
        ORIGINATE_ERROR("Could not allocate the video frame data");

    m_pkt = av_packet_alloc();
    if (!m_pkt)
        ORIGINATE_ERROR("Could not allocate video packet");

    if(m_frame->format == AV_PIX_FMT_YUV420P)
    {
        input_params.params.width = m_frame->width;
        input_params.params.height = m_frame->height;
        input_params.params.layout = NVBUF_LAYOUT_BLOCK_LINEAR;
        input_params.params.colorFormat = NVBUF_COLOR_FORMAT_YUV420;
        input_params.params.memType = NVBUF_MEM_SURFACE_ARRAY;
        input_params.memtag = NvBufSurfaceTag_VIDEO_CONVERT;
        ret = NvBufSurfaceAllocate(&m_blitSurf, 1, &input_params);
        if (ret < 0)
            ORIGINATE_ERROR("Could not allocate the yuv420p surf data");
        m_blitSurf->numFilled = 1;
    }

    memset(&input_params, 0, sizeof(input_params));
    input_params.params.width = m_frame->width;
    input_params.params.height = m_frame->height;
    if(m_frame->format == AV_PIX_FMT_YUV420P)
        input_params.params.colorFormat = NVBUF_COLOR_FORMAT_YUV420;
    else
        input_params.params.colorFormat = NVBUF_COLOR_FORMAT_NV12;
    input_params.params.layout = NVBUF_LAYOUT_PITCH;
    input_params.params.memType = NVBUF_MEM_SYSTEM;
    input_params.memtag = NvBufSurfaceTag_NONE;
    input_params.disablePitchPadding = true;
    //Allocate system memory to perform copy from HW to SW
    m_sysBuffers = (NvBufSurface**) malloc(NUM_BUFFERS * sizeof(NvBufSurface*));
    for (int i = 0; i < NUM_BUFFERS; i++)
    {
        ret = NvBufSurfaceAllocate(&m_sysBuffers[i], 1, &input_params);
        if (ret < 0)
            ORIGINATE_ERROR("Could not allocate the system memory buffer");
        m_sysBuffers[i]->numFilled = 1;
    }

    return true;
}

int ConsumerThread::libavEncode(AVCodecContext *ctx, AVFrame *frame, AVPacket *pkt, int fd)
{
    int ret = 0;
    NvBufSurface *surf;
    int nplanes = 2;
    int width = 0, height = 0, size = 0;

    if (frame)
    {
        av_init_packet(pkt);
        pkt->data = NULL;  // packet data will be allocated by the encoder
        pkt->size = 0;
        /* make sure the frame data is writable */
        ret = av_frame_make_writable(frame);
        if (ret < 0)
        {
            av_log(ctx, AV_LOG_ERROR, "AV Frame is not writable\n");
            return ret;
        }

        ret = NvBufSurfaceFromFd(fd, (void**)&surf);
        if (ret)
        {
            ORIGINATE_ERROR("%s: NvBufSurfaceFromFd failed", __func__);
            return ret;
        }

        // NOTE: An additional Transform is required if input pix_fmt is I420
        // Conversion to NV12 -> I420 is performed using VIC.
        if (frame->format == AV_PIX_FMT_YUV420P)
        {
            nplanes = 3;
            NvBufSurfTransformRect src_rect, dest_rect;
            src_rect.top = dest_rect.top = 0;
            src_rect.left = dest_rect.left = 0;
            src_rect.width = dest_rect.width = frame->width;
            src_rect.height = dest_rect.height = frame->height;

            NvBufSurfTransformParams transform_params;
            memset(&transform_params,0,sizeof (transform_params));

            transform_params.transform_flag = NVBUFSURF_TRANSFORM_FILTER;
            transform_params.transform_flip = NvBufSurfTransform_None;
            transform_params.transform_filter = NvBufSurfTransformInter_Algo3;
            transform_params.src_rect = &src_rect;
            transform_params.dst_rect = &dest_rect;
            ret = NvBufSurfTransform(surf, m_blitSurf, &transform_params);
            if (ret < 0)
            {
                ORIGINATE_ERROR("Could not transform nv12 to yuv420p surf data");
                return ret;
            }
        }

        // NOTE: A HW to SW copy is required due to alignment contraints
        // The given input is HW buffer. Call NvBufSurfaceCopy to copy to raw AVFrame buffer.
        ret = NvBufSurfaceCopy((nplanes == 2 ? surf : m_blitSurf), m_sysBuffers[m_count % NUM_BUFFERS]);
        if (ret < 0)
        {
            ORIGINATE_ERROR("NvBufSurfaceCopy Failed");
            return ret;
        }

        uint8_t *data = (uint8_t *)m_sysBuffers[m_count % NUM_BUFFERS]->surfaceList[0].dataPtr;
        // Set the avframe data
        for (int plane = 0; plane < nplanes; plane++)
        {
            frame->data[plane] = (uint8_t *)(data + size);
            frame->linesize[plane] = m_sysBuffers[m_count % NUM_BUFFERS]->surfaceList[0].planeParams.pitch[plane];
            size += m_sysBuffers[m_count % NUM_BUFFERS]->surfaceList[0].planeParams.psize[plane];
        }

        frame->pts = m_count;
        m_count++;
    }

    ret = avcodec_send_frame(m_enc_ctx, frame);
    if (ret < 0)
    {
        av_log(ctx, AV_LOG_ERROR, "Error while encoding frame\n");
        return ret;
    }

EOS:
    ret = avcodec_receive_packet(m_enc_ctx, pkt);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        return 0;
    else if (ret < 0)
    {
        av_log(ctx, AV_LOG_ERROR, "Error while encoding frame\n");
        return ret;
    }

    if (pkt->size > 0)
    {
        this->m_outputFile->write((char *) pkt->data, pkt->size);
        av_packet_unref(pkt);
        // Loop back to retrieve remaining encoded packets
        if (frame == NULL)
            goto EOS;
    }

    return ret;
}

bool ConsumerThread::destroyVideoEncoder()
{
    if (m_frame && m_frame->format == AV_PIX_FMT_YUV420P)
    {
        NvBufSurfaceDestroy(m_blitSurf);
        m_blitSurf = NULL;
    }
    for (int i = 0; i < NUM_BUFFERS; i++)
    {
        NvBufSurfaceDestroy(m_sysBuffers[i]);
        m_sysBuffers[i] = NULL;
    }
    free(m_sysBuffers);
    m_sysBuffers = NULL;
    if (m_pkt)
    {
        av_packet_free(&m_pkt);
        m_pkt = NULL;
    }
    if (m_frame)
    {
        av_frame_free(&m_frame);
        m_frame = NULL;
    }
    if (m_enc_ctx)
    {
        //avcodec_close(&m_enc_ctx);
        avcodec_free_context(&m_enc_ctx);
        m_enc_ctx = NULL;
    }
    return true;
}

void ConsumerThread::abort()
{
    destroyVideoEncoder();
    m_gotError = true;
}

bool ArgusSamples::execute()
{
    NvBufSurface *surf[NUM_BUFFERS] = {0};

    /* Create the CameraProvider object and get the core interface */
    UniqueObj<CameraProvider> cameraProvider = UniqueObj<CameraProvider>(CameraProvider::create());
    ICameraProvider *iCameraProvider = interface_cast<ICameraProvider>(cameraProvider);
    if (!iCameraProvider)
        ORIGINATE_ERROR("Failed to create CameraProvider");

    /* Get the camera devices */
    std::vector<CameraDevice*> cameraDevices;
    iCameraProvider->getCameraDevices(&cameraDevices);
    if (cameraDevices.size() == 0)
        ORIGINATE_ERROR("No cameras available");

    if (CAMERA_INDEX >= cameraDevices.size())
    {
        PRODUCER_PRINT("CAMERA_INDEX out of range. Fall back to 0\n");
        CAMERA_INDEX = 0;
    }

    /* Create the capture session using the first device and get the core interface */
    UniqueObj<CaptureSession> captureSession(
            iCameraProvider->createCaptureSession(cameraDevices[CAMERA_INDEX]));
    ICaptureSession *iCaptureSession = interface_cast<ICaptureSession>(captureSession);
    if (!iCaptureSession)
        ORIGINATE_ERROR("Failed to get ICaptureSession interface");

    /* Create the OutputStream */
    PRODUCER_PRINT("Creating output stream\n");
    UniqueObj<OutputStreamSettings> streamSettings(
        iCaptureSession->createOutputStreamSettings(STREAM_TYPE_BUFFER));
    IBufferOutputStreamSettings *iStreamSettings =
        interface_cast<IBufferOutputStreamSettings>(streamSettings);
    if (!iStreamSettings)
        ORIGINATE_ERROR("Failed to get IBufferOutputStreamSettings interface");

    /* Configure the OutputStream to use the EGLImage BufferType */
    iStreamSettings->setBufferType(BUFFER_TYPE_EGL_IMAGE);

    /* Create the OutputStream */
    UniqueObj<OutputStream> outputStream(iCaptureSession->createOutputStream(streamSettings.get()));
    IBufferOutputStream *iBufferOutputStream = interface_cast<IBufferOutputStream>(outputStream);

    /* Allocate native buffers */
    DmaBuffer* nativeBuffers[NUM_BUFFERS];

    for (uint32_t i = 0; i < NUM_BUFFERS; i++)
    {
        nativeBuffers[i] = DmaBuffer::create(STREAM_SIZE, NVBUF_COLOR_FORMAT_NV12,
                                                NVBUF_LAYOUT_BLOCK_LINEAR);
        if (!nativeBuffers[i])
            ORIGINATE_ERROR("Failed to allocate NativeBuffer");
    }

    /* Create EGLImages from the native buffers */
    EGLImageKHR eglImages[NUM_BUFFERS];
    for (uint32_t i = 0; i < NUM_BUFFERS; i++)
    {
        int ret = 0;

        ret = NvBufSurfaceFromFd(nativeBuffers[i]->getFd(), (void**)(&surf[i]));
        if (ret)
            ORIGINATE_ERROR("%s: NvBufSurfaceFromFd failed", __func__);

        ret = NvBufSurfaceMapEglImage (surf[i], 0);
        if (ret)
            ORIGINATE_ERROR("%s: NvBufSurfaceMapEglImage failed", __func__);

        eglImages[i] = surf[i]->surfaceList[0].mappedAddr.eglImage;
        if (eglImages[i] == EGL_NO_IMAGE_KHR)
            ORIGINATE_ERROR("Failed to create EGLImage");
    }

    /* Create the BufferSettings object to configure Buffer creation */
    UniqueObj<BufferSettings> bufferSettings(iBufferOutputStream->createBufferSettings());
    IEGLImageBufferSettings *iBufferSettings =
        interface_cast<IEGLImageBufferSettings>(bufferSettings);
    if (!iBufferSettings)
        ORIGINATE_ERROR("Failed to create BufferSettings");

    /* Create the Buffers for each EGLImage (and release to
       stream for initial capture use) */
    UniqueObj<Buffer> buffers[NUM_BUFFERS];
    for (uint32_t i = 0; i < NUM_BUFFERS; i++)
    {
        iBufferSettings->setEGLImage(eglImages[i]);
        iBufferSettings->setEGLDisplay(eglDisplay);
        buffers[i].reset(iBufferOutputStream->createBuffer(bufferSettings.get()));
        IBuffer *iBuffer = interface_cast<IBuffer>(buffers[i]);

        /* Reference Argus::Buffer and DmaBuffer each other */
        iBuffer->setClientData(nativeBuffers[i]);
        nativeBuffers[i]->setArgusBuffer(buffers[i].get());

        if (!interface_cast<IEGLImageBuffer>(buffers[i]))
            ORIGINATE_ERROR("Failed to create Buffer");
        if (iBufferOutputStream->releaseBuffer(buffers[i].get()) != STATUS_OK)
            ORIGINATE_ERROR("Failed to release Buffer for capture use");
    }

    /* Launch the FrameConsumer thread to consume frames from the OutputStream */
    PRODUCER_PRINT("Launching consumer thread\n");
    ConsumerThread frameConsumerThread(outputStream.get());
    PROPAGATE_ERROR(frameConsumerThread.initialize());

    /* Wait until the consumer is connected to the stream */
    PROPAGATE_ERROR(frameConsumerThread.waitRunning());

    /* Create capture request and enable output stream */
    UniqueObj<Request> request(iCaptureSession->createRequest());
    IRequest *iRequest = interface_cast<IRequest>(request);
    if (!iRequest)
        ORIGINATE_ERROR("Failed to create Request");
    iRequest->enableOutputStream(outputStream.get());

    ISourceSettings *iSourceSettings = interface_cast<ISourceSettings>(iRequest->getSourceSettings());
    if (!iSourceSettings)
        ORIGINATE_ERROR("Failed to get ISourceSettings interface");
    iSourceSettings->setFrameDurationRange(Range<uint64_t>(1e9/DEFAULT_FPS));

    /* Submit capture requests */
    PRODUCER_PRINT("Starting repeat capture requests.\n");
    if (iCaptureSession->repeat(request.get()) != STATUS_OK)
        ORIGINATE_ERROR("Failed to start repeat capture request");

    /* Wait for CAPTURE_TIME seconds */
    for (int i = 0; i < CAPTURE_TIME && !frameConsumerThread.isInError(); i++)
        sleep(1);

    /* Stop the repeating request and wait for idle */
    iCaptureSession->stopRepeat();
    iBufferOutputStream->endOfStream();
    iCaptureSession->waitForIdle();

    /* Wait for the consumer thread to complete */
    PROPAGATE_ERROR(frameConsumerThread.shutdown());

    /* Destroy the output stream to end the consumer thread */
    outputStream.reset();

    /* Destroy the EGLImages */
    for (uint32_t i = 0; i < NUM_BUFFERS; i++)
        NvBufSurfaceUnMapEglImage (surf[i], 0);

    /* Destroy the native buffers */
    for (uint32_t i = 0; i < NUM_BUFFERS; i++)
        delete nativeBuffers[i];

    PRODUCER_PRINT("Done -- exiting.\n");

    return true;
}

void printHelp()
{
    printf("Usage: camera_recording [OPTIONS]\n"
           "Options:\n"
           "  -r        Set output resolution WxH [Default 640x480]\n"
           "  -f        Set output filename [Default output.h264]\n"
           "  -d        Set capture duration [Default 5 seconds]\n"
           "  -i        Set camera index [Default 0]\n"
           "  -v        Enable verbose message\n"
           "  -p        Encoder input format 1 - NV12 [Default] 2 - I420\n"
           "  -h        Print this help\n"
           "Note: Only H.264 format is supported for SW encoding.\n"
           "If encoder input format I420 is selected, additional transform to convert input to I420 is done.\n");
}

bool parseCmdline(int argc, char **argv)
{
    int c, w, h;
    bool haveFilename = false;
    while ((c = getopt(argc, argv, "r:f:t:d:i:p:s::v::h")) != -1)
    {
        switch (c)
        {
            case 'r':
                if (sscanf(optarg, "%dx%d", &w, &h) != 2)
                    return false;
                STREAM_SIZE.width() = w;
                STREAM_SIZE.height() = h;
                break;
            case 'f':
                OUTPUT_FILENAME = optarg;
                haveFilename = true;
                break;
            case 'd':
                CAPTURE_TIME = atoi(optarg);
                break;
            case 'i':
                CAMERA_INDEX = atoi(optarg);
                break;
            case 'v':
                VERBOSE_ENABLE = true;
                break;
            case 'p':
                if (2 == atoi(optarg))
                    ENCODER_INPIXFMT = I420;
                else
                    ENCODER_INPIXFMT = NV12;
                break;
            default:
                return false;
        }
    }
    return true;
}

int main(int argc, char *argv[])
{
    if (!parseCmdline(argc, argv))
    {
        printHelp();
        return EXIT_FAILURE;
    }

    /* Get default EGL display */
    eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (eglDisplay == EGL_NO_DISPLAY)
    {
        printf("Cannot get EGL display.\n");
    }

    if (!ArgusSamples::execute())
        return EXIT_FAILURE;

    /* Terminate EGL display */
    eglTerminate(eglDisplay);

    return EXIT_SUCCESS;
}
