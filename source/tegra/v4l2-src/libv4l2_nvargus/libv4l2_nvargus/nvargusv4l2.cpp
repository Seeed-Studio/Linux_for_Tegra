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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <dlfcn.h>

#include "linux/videodev2.h"
#include "nvargusv4l2_os.h"
#include "nvargusv4l2.h"
#include "nvargusv4l2_context.h"
#include "nvargusv4l2_ioctl.h"

/* Default node path
 */
#define V4L2_ARGUS_DEV_NODE "/dev/video0"

/* A global mutex used to protect simultaneous access
 * while opening or closing the contexts
 */
NvMutexHandle global_mutex = NULL;

bool runtime_logs_enabled = false;
static void *hLibHandle = NULL;

void __attribute__((constructor)) nvargus_init(void);
void __attribute__((destructor)) nvargus_deinit(void);

void __attribute__((constructor)) nvargus_init(void)
{
    struct stat file_stats;
    if (stat("/tmp/argusv4l2_logs", &file_stats) == 0)
     runtime_logs_enabled = true;

    hLibHandle = dlopen("libnvargus.so", RTLD_LAZY);
    if (NULL == hLibHandle)
        REL_PRINT("Error opening libnvargus.so in %s\n", __FUNCTION__);

    if (NvMutexCreate(&global_mutex) != 0)
        REL_PRINT("Error creating the global mutex\n");
}

void __attribute__((destructor)) nvargus_deinit(void)
{
    NvMutexDestroy(global_mutex);
    if (hLibHandle)
        dlclose(hLibHandle);
}

int32_t ArgusV4L2_Open(const int camera_index, int32_t flags)
{
    DBG_PRINT("Enter %s Camera Index %d\n", __FUNCTION__, camera_index);

    return v4l2_open_camera_context(camera_index, flags);
}

int32_t ArgusV4L2_Close(int32_t fd)
{
    DBG_PRINT("Enter %s FD %d \n", __FUNCTION__, fd);
    v4l2_close_camera_context(fd);
    return 0;
}

int32_t ArgusV4L2_Ioctl(int32_t fd, unsigned long cmd, ...)
{
    void *arg;
    v4l2_context *ctx = v4l2_get_context(fd);
    va_list listPointer;

    va_start(listPointer, cmd);
    arg = va_arg(listPointer, void *);
    va_end(listPointer);

    if ((ctx == NULL) || (arg == NULL))
    {
        errno = EINVAL;
        return -1;
    }

    NvMutexAcquire(ctx->ioctl_mutex);
    errno = nvargus_ioctl(fd, cmd, arg);
    NvMutexRelease(ctx->ioctl_mutex);

    if (errno != 0)
        return -1;
    return errno;
}
