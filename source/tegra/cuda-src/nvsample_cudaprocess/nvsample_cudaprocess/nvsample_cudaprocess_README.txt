/*
 * Copyright (c) 2016-2018, NVIDIA CORPORATION. All rights reserved.
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

a) Install pre-requisites
========================

1. Install following packages on Jetson.
   sudo apt-get install libegl1-mesa-dev libgles2-mesa-dev libglvnd-dev

2. Install the NVIDIA(r) CUDA(r) toolkit. (e.g version 8.0)

   Download package <CUDA(r) Toolkit for L4T> from the following website:
         https://developer.nvidia.com/embedded/downloads
   Ensure that the package name is consistent with the Linux userspace.

   Extract the package with the following command:
   $ sudo dpkg -i <CUDA(r) Toolkit for L4T>

   Install the package with the following commands:
   $ sudo apt-get update
   $ sudo apt-get install cuda-toolkit-<version>

   NOTE: Use proper cuda toolkit version with above installation command. (for e.g. cuda-toolkit-8.0)

b) Build sample cuda sources
========================

   $ tar -xpf nvsample_cudaprocess_src.tbz2
   $ cd nvsample_cudaprocess
   $ make
   $ sudo mv libnvsample_cudaprocess.so /usr/lib/aarch64-linux-gnu/

Alternatively, set LD_LIBRARY_PATH as mentioned below instead of moving the library.
   $ export LD_LIBRARY_PATH=./

c) Run gst-launch-1.0 pipeline
========================

Pre-requisite for gstreamer-1.0: Install gstreamer-1.0 plugin using following command on Jetson

   sudo apt-get install gstreamer1.0-tools gstreamer1.0-alsa gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-good1.0-dev

* Video decode pipeline:

   gst-launch-1.0 filesrc location=<filename.mp4> ! qtdemux ! h264parse ! omxh264dec ! nvivafilter cuda-process=true customer-lib-name="libnvsample_cudaprocess.so" ! 'video/x-raw(memory:NVMM), format=(string)NV12' ! nvoverlaysink display-id=0 -e

* Camera capture pipeline:

   gst-launch-1.0 nvcamerasrc fpsRange="30.0 30.0" ! 'video/x-raw(memory:NVMM), width=(int)3840, height=(int)2160, format=(string)I420, framerate=(fraction)30/1' ! nvtee ! nvivafilter cuda-process=true customer-lib-name="libnvsample_cudaprocess.so" ! 'video/x-raw(memory:NVMM), format=(string)NV12' ! nvoverlaysink display-id=0 -e

NOTE: Make sure the video is larger than 96x96

d) Programming Guide
========================

1. Sample code in nvsample_cudaprocess_src package

    nvsample_cudaprocess.cu -> sample image pre-/post- processing, CUDA processing functions
                               pre-process: draw a 32x32 green block start at (0,0)
                               cuda-process: draw 32x32 green block start at (32,32)
                               post-process: draw 32x32 green block start at (64,64)

    customer_functions.h -> API definition

2. Image processing APIs
    a. Pre-/Post- processing
        i. input parameters:
            void ** sBaseAddr       : mapped pointers array point to different
                                      plane of image.

            unsigned int * smemsize : actually allocated memory size array for
                                      each image plane, no less than plane
                                      width * height.

            unsigned int * swidth   : width array for each image plane

            unsigned int * sheight  : height array for each image plane

            unsigned int * spitch   : actual line width array in memory for
                                      each image plane, no less than plane
                                      width

            ColorFormat * sformat   : color format array, i.e.,
                                      * NV12 image will have:
                                      sformat[0] = COLOR_FORMAT_Y8
                                      sformat[1] = COLOR_FORMAT_U8_V8
                                      * RGBA image will have:
                                      sformat[0] = COLOR_FORMAT_RGBA

            unsigned int nsurfcount : number of planes of current image type

            void ** userPtr         : point to customer allocated buffer in
                                      processing function

        ii. output parameters:
            none

    b. CUDA processing
        i. input parameters
            EGLImageKHR image : Input image data in EGLImage type
            void ** userPtr   : point to customer allocated buffer in
                                processing functions

    c. "init" function
        This function must be named "init", and accept a pointer to
        CustomerFunction structure, which contains 3 function pointers point to
        pre-processing, cuda-processing, and post-processing respectively, for
        details, please refer to customer_functions.h and nvsample_cudaprocess.cu

    d. "deinit" function
        This function must be named "deinit", and is called when the pipeline is
        stopping

    e. notes
        a customer processing lib:
            MUST have an "init" function, which set correspond functions to
                nvivafilter plugin;
            MAY have a pre-processing function, if not implemented, set to NULL
                in "init" function;
            MAY have a cuda-processing function, if not imeplemented, set to
                NULL in "init" function;
            MAY have a post-processing function, if not implemented, set to NULL
                in "init" function.
            MAY have an "deinit" function if customer functions need to do
                deinitialization in stopping the pipeline

3. Processing Steps
    a. nvivafilter plugin input and output
        input : (I420, NV12) NVMM buffer, it's NVIDIA's internal frame format, maybe
                pitch linear or block linear layout.
        output: (NV12, RGBA) NVMM buffer, layout transformed from block linear to pitch linear,
                processed result could inplace stored into this buffer.

    b. nvivafilter plugin properties
        i.   customer-lib-name
            string: absolute path and .so lib name to your lib or just the .so
            lib name if it is in dynamic lib search path.

        ii.  pre-process
            bool: dynamically control whether do pre-process if pre-process
                  function is implemented and set to plugin

        iii. cuda-process
            bool: dynamically control whether to do cuda-process if
                  cuda-process function is implemented and set to plugin

        iv.  post-process
            bool: dynamically control whether to do post-process if
                  post-process function is implemented and set to plugin

    c. processing order
        customer processing functions will be invoked strictly at following
        order if they are implemented and set:
            pre-processing -> cuda-processing -> post-processing
        plugin property pre-process/cuda-process/post-process can be used for
        dynamic enable/disable processing respectively.

