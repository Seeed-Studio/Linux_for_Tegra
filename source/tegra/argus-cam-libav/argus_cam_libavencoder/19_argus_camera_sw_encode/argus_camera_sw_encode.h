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

#ifndef __ARGUS_CAMERA_SW_ENCODE_H__
#define __ARGUS_CAMERA_SW_ENCODE_H__

#include "Error.h"
#include "Thread.h"
#include "nvmmapi/NvNativeBuffer.h"
#include "NvBuffer.h"
#include "NvBufSurface.h"

#include <Argus/Argus.h>
#ifdef __cplusplus
extern "C"
{
#endif
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#ifdef __cplusplus
}
#endif

using namespace Argus;

/* Specifies the input pixel format */
enum pix_fmt
{
    NV12 = 1,
    I420
};

/* Debug print macros */
#define PRODUCER_PRINT(...) printf("PRODUCER: " __VA_ARGS__)
#define CONSUMER_PRINT(...) printf("CONSUMER: " __VA_ARGS__)
#define CHECK_ERROR(expr) \
    do { \
        if ((expr) < 0) { \
            abort(); \
            ORIGINATE_ERROR(#expr " failed"); \
        } \
    } while (0);

namespace ArgusSamples
{

/**
 * Helper class to map NvNativeBuffer to Argus::Buffer and vice versa.
 * A reference to DmaBuffer will be saved as client data in each Argus::Buffer.
 * Also DmaBuffer will keep a reference to corresponding Argus::Buffer.
 * This class also extends NvBuffer to act as a share buffer between Argus and encoder.
 */
class DmaBuffer : public NvNativeBuffer, public NvBuffer
{
public:
    /* Always use this static method to create DmaBuffer */
    static DmaBuffer* create(const Argus::Size2D<uint32_t>& size,
                             NvBufSurfaceColorFormat colorFormat,
                             NvBufSurfaceLayout layout = NVBUF_LAYOUT_PITCH);

    /* Help function to convert Argus Buffer to DmaBuffer */
    static DmaBuffer* fromArgusBuffer(Buffer *buffer);

    /* Return DMA buffer handle */
    int getFd() const { return m_fd; }

    /* Get and set reference to Argus buffer */
    void setArgusBuffer(Buffer *buffer) { m_buffer = buffer; }
    Buffer *getArgusBuffer() const { return m_buffer; }

private:
    DmaBuffer(const Argus::Size2D<uint32_t>& size)
        : NvNativeBuffer(size),
          NvBuffer(0, 0),
          m_buffer(NULL)
    {
    }

    Buffer *m_buffer;   /**< Holds the reference to Argus::Buffer */
};

/**
 * Consumer thread:
 *   Acquire frames from BufferOutputStream and extract the DMABUF fd from it.
 *   Provide DMABUF to V4L2 for video encoding. The encoder will save the encoded
 *   stream to disk.
 */
class ConsumerThread : public Thread
{
public:
    explicit ConsumerThread(OutputStream* stream);
    ~ConsumerThread();

    bool isInError()
    {
        return m_gotError;
    }

private:
    /** @name Thread methods */
    /**@{*/
    virtual bool threadInitialize();
    virtual bool threadExecute();
    virtual bool threadShutdown();
    /**@}*/

    /* Initialize and set properties of Libav Encoder */
    bool createVideoEncoder();

    /* Destroy the Libav Encoder */
    bool destroyVideoEncoder();

    /* Perform Encode operation */
    int libavEncode(AVCodecContext *ctx, AVFrame *frame, AVPacket *pkt, int fd);

    /* Abort the encode operation */
    void abort();

    OutputStream* m_stream;    /**< Holds the Output stream */
    const AVCodec* codec;      /**< Holds the encoder type and name */
    AVCodecContext* m_enc_ctx; /**< Holds the libav codec context */
    AVFrame* m_frame;          /**< Holds the libav input frame data and info */
    AVPacket* m_pkt;           /**< Holds the libav output packet data and info */
    std::ofstream* m_outputFile;
    bool m_gotError;
    NvBufSurface* m_blitSurf;     /**< Holds the I420 Buffer if applicable */
    NvBufSurface** m_sysBuffers; /**< Holds the system buffers */
    int m_count;                 /**< Holds the count of buffers sent */
};

/**
 * Argus Producer thread:
 *   Opens the Argus camera driver, creates an BufferOutputStream to output
 *   frames, then performs repeating capture requests for CAPTURE_TIME
 *   seconds before closing the producer and Argus driver.
 */
bool execute();

} /* namespace ArgusSamples */

/* Function to parse commandline arguments */
bool parseCmdline(int argc, char **argv);

/* Function to print help */
void printHelp();

#endif // __ARGUS_CAMERA_SW_ENCODE_H__
