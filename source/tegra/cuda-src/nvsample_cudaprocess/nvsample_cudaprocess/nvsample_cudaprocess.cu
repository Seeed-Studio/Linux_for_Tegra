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

#include <stdio.h>
#include <stdlib.h>

#include <cuda.h>

#include "customer_functions.h"
#include "cudaEGL.h"
#include "iva_metadata.h"

#define BOX_W 32
#define BOX_H 32

#define CORD_X 64
#define CORD_Y 64
#define MAX_BUFFERS 30
static BBOX rect_data[MAX_BUFFERS];

/**
  * Dummy custom pre-process API implematation.
  * It just access mapped surface userspace pointer &
  * memset with specific pattern modifying pixel-data in-place.
  *
  * @param sBaseAddr  : Mapped Surfaces pointers
  * @param smemsize   : surfaces size array
  * @param swidth     : surfaces width array
  * @param sheight    : surfaces height array
  * @param spitch     : surfaces pitch array
  * @param nsurfcount : surfaces count
  */
static void
pre_process (void **sBaseAddr,
                unsigned int *smemsize,
                unsigned int *swidth,
                unsigned int *sheight,
                unsigned int *spitch,
                ColorFormat  *sformat,
                unsigned int nsurfcount,
                void ** usrptr)
{
  /* add your custom pre-process here
     we draw a green block for demo */
  int x, y;
  char * uv = NULL;
  unsigned char * rgba = NULL;
  if (sformat[1] == COLOR_FORMAT_U8_V8) {
    uv = (char *)sBaseAddr[1];
    for (y = 0; y < BOX_H; ++y) {
      for (x = 0; x < BOX_W; ++x) {
        uv[y * spitch[1] + 2 * x] = 0;
        uv[y * spitch[1] + 2 * x + 1] = 0;
      }
    }
  } else if (sformat[0] == COLOR_FORMAT_RGBA) {
    rgba = (unsigned char *)sBaseAddr[0];
     for (y = 0; y < BOX_H*2; y++) {
      for (x = 0; x < BOX_W*8; x+=4) {
       rgba[x + 0] = 0;
       rgba[x + 1] = 0;
       rgba[x + 2] = 0;
       rgba[x + 3] = 0;
      }
        rgba+=spitch[0];
    }
  }
}

/**
  * Dummy custom post-process API implematation.
  * It just access mapped surface userspace pointer &
  * memset with specific pattern modifying pixel-data in-place.
  *
  * @param sBaseAddr  : Mapped Surfaces pointers
  * @param smemsize   : surfaces size array
  * @param swidth     : surfaces width array
  * @param sheight    : surfaces height array
  * @param spitch     : surfaces pitch array
  * @param nsurfcount : surfaces count
  */
static void
post_process (void **sBaseAddr,
                unsigned int *smemsize,
                unsigned int *swidth,
                unsigned int *sheight,
                unsigned int *spitch,
                ColorFormat  *sformat,
                unsigned int nsurfcount,
                void ** usrptr)
{
  /* add your custom post-process here
     we draw a green block for demo */
  int x, y;
  char * uv = NULL;
  int xoffset = (CORD_X * 4);
  int yoffset = (CORD_Y * 2);
  unsigned char * rgba = NULL;
  if (sformat[1] == COLOR_FORMAT_U8_V8) {
    uv = (char *)sBaseAddr[1];
    for (y = 0; y < BOX_H; ++y) {
      for (x = 0; x < BOX_W; ++x) {
        uv[(y + BOX_H * 2) * spitch[1] + 2 * (x + BOX_W * 2)] = 0;
        uv[(y + BOX_H * 2) * spitch[1] + 2 * (x + BOX_W * 2) + 1] = 0;
      }
    }
  } else if (sformat[0] == COLOR_FORMAT_RGBA) {
    rgba = (unsigned char *)sBaseAddr[0];
    rgba += ((spitch[0] * yoffset) + xoffset);
     for (y = 0; y < BOX_H*2; y++) {
      for (x = 0; x < BOX_W*8; x+=4) {
       rgba[(x + xoffset) + 0] = 0;
       rgba[(x + xoffset) + 1] = 0;
       rgba[(x + xoffset) + 2] = 0;
       rgba[(x + xoffset) + 3] = 0;
      }
        rgba+=spitch[0];
    }
  }
}

__global__ void addLabelsKernel(int* pDevPtr, int pitch){
  int row = blockIdx.y*blockDim.y + threadIdx.y + BOX_H;
  int col = blockIdx.x*blockDim.x + threadIdx.x + BOX_W;
  char * pElement = (char*)pDevPtr + row * pitch + col * 2;
  pElement[0] = 0;
  pElement[1] = 0;
  return;
}

static int addLabels(CUdeviceptr pDevPtr, int pitch){
    dim3 threadsPerBlock(BOX_W, BOX_H);
    dim3 blocks(1,1);
    addLabelsKernel<<<blocks,threadsPerBlock>>>((int*)pDevPtr, pitch);
    return 0;
}

static void add_metadata(void ** usrptr)
{
    /* User need to fill rectangle data based on their requirement.
     * Here rectangle data is filled for demonstration purpose only */

    int i;
    static int index = 0;

    rect_data[index].framecnt = index;
    rect_data[index].objectcnt = index;

    for(i=0; i < NUM_LOCATIONS; i++)
    {
        rect_data[index].location_list[i].x1 = index;
        rect_data[index].location_list[i].x2 = index;
        rect_data[index].location_list[i].y1 = index;
        rect_data[index].location_list[i].y2 = index;
    }
    *usrptr = &rect_data[index];
    index++;
    if(!(index % MAX_BUFFERS))
    {
        index = 0;
    }
}

/**
  * Performs CUDA Operations on egl image.
  *
  * @param image : EGL image
  */
static void
gpu_process (EGLImageKHR image, void ** usrptr)
{
  CUresult status;
  CUeglFrame eglFrame;
  CUgraphicsResource pResource = NULL;

  cudaFree(0);
  status = cuGraphicsEGLRegisterImage(&pResource, image, CU_GRAPHICS_MAP_RESOURCE_FLAGS_NONE);
  if (status != CUDA_SUCCESS) {
    printf("cuGraphicsEGLRegisterImage failed : %d \n", status);
    return;
  }

  status = cuGraphicsResourceGetMappedEglFrame( &eglFrame, pResource, 0, 0);
  if (status != CUDA_SUCCESS) {
    printf ("cuGraphicsSubResourceGetMappedArray failed\n");
  }

  status = cuCtxSynchronize();
  if (status != CUDA_SUCCESS) {
    printf ("cuCtxSynchronize failed \n");
  }

  if (eglFrame.frameType == CU_EGL_FRAME_TYPE_PITCH) {
    if (eglFrame.eglColorFormat == CU_EGL_COLOR_FORMAT_ABGR) {
    /* Rectangle label in plane RGBA, you can replace this with any cuda algorithms */
      addLabels((CUdeviceptr) eglFrame.frame.pPitch[0], eglFrame.pitch);
    } else if (eglFrame.eglColorFormat == CU_EGL_COLOR_FORMAT_YUV420_SEMIPLANAR) {
    /* Rectangle label in plan UV , you can replace this with any cuda algorithms */
      addLabels((CUdeviceptr) eglFrame.frame.pPitch[1], eglFrame.pitch);
    } else
      printf ("Invalid eglcolorformat\n");
  }

  add_metadata(usrptr);

  status = cuCtxSynchronize();
  if (status != CUDA_SUCCESS) {
    printf ("cuCtxSynchronize failed after memcpy \n");
  }

  status = cuGraphicsUnregisterResource(pResource);
  if (status != CUDA_SUCCESS) {
    printf("cuGraphicsEGLUnRegisterResource failed: %d \n", status);
  }
}

extern "C" void
init (CustomerFunction * pFuncs)
{
  pFuncs->fPreProcess = pre_process;
  pFuncs->fGPUProcess = gpu_process;
  pFuncs->fPostProcess = post_process;
}

extern "C" void
deinit (void)
{
  /* deinitialization */
}
