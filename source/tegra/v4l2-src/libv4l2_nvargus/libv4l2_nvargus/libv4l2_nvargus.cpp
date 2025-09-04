/*
 * SPDX-FileCopyrightText: Copyright (c) 2020-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <iostream>
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string>

#include "libv4l-plugin.h"
#include "nvargusv4l2.h"
#include "nvargusv4l2_context.h"
#include "nvargusv4l2_ioctl.h"

#if HAVE_VISIBILITY
#define PLUGIN_PUBLIC __attribute__ ((visibility("default")))
#else
#define PLUGIN_PUBLIC
#endif

#define NVARGUS_DEV_STRING "/dev/video"
#define MAX_CAMERA_NODE_LIMITS 64

#define LOG_Q_DQ(fmt, args...) \
    do { \
        if (profile_enabled_flag) \
        { \
            struct timeval start; \
            struct timezone tzp; \
            gettimeofday(&start, &tzp); \
            FILE* pFile = fopen("/tmp/profile_argusbuffer_logs.txt", "a"); \
            fprintf(pFile, "\n%x: %lu: "fmt, (unsigned int) pthread_self(), (1000000 * start.tv_sec + start.tv_usec),##args); \
            fclose(pFile); \
        } \
    } while (0)

struct nvargus_plugin_ctx
{
    int nvv4l2argus_fd;
    int fd_blocking_mode;
};

uint32_t profile_enabled_flag = 0;

void __attribute__((constructor)) libv4l2_nvargus_init(void);
void __attribute__((destructor)) libv4l2_nvargus_deinit(void);

void *plugin_init(int fd);
void plugin_close(void *dev_ops_priv);
int plugin_ioctl(void *dev_ops_priv, int fd,
    unsigned long int cmd, void *arg);

#ifdef _DEBUG
std::string fcc2s(unsigned int val);
#endif

void __attribute__((constructor)) libv4l2_nvargus_init(void)
{
    struct stat file_stats;

    if(stat("/tmp/libv4l2argusPlugin_profile", &file_stats) == 0)
        profile_enabled_flag = 1;
    else
        profile_enabled_flag = 0;
}

void __attribute__((destructor)) libv4l2_nvargus_deinit(void)
{
}

typedef struct
{
    int32_t device_index;
    int32_t argus_index;
    bool is_nonbayer;
} camera_type;

class CameraNode
{
public:
    static CameraNode& GetCameraNode()
    {
        static CameraNode s_instance;
        return s_instance;
    }

    static int GetIndex(int node_index)
    {
        return GetCameraNode().GetIndexInternal(node_index);
    }
    void operator=(CameraNode const&) = delete;

private:
    CameraNode() {}

    int GetIndexInternal(int node_index);

    int QueryNode(int node_index, char* device_path);

    camera_type m_cameraNodes[MAX_CAMERA_NODE_LIMITS] =
                                    {{-1, -1, false}, {-1, -1, false}, {-1, -1, false}, {-1, -1, false},
                                    {-1, -1, false}, {-1, -1, false}, {-1, -1, false}, {-1, -1, false},
                                    {-1, -1, false}, {-1, -1, false}, {-1, -1, false}, {-1, -1, false},
                                    {-1, -1, false}, {-1, -1, false}, {-1, -1, false}, {-1, -1, false},
                                    {-1, -1, false}, {-1, -1, false}, {-1, -1, false}, {-1, -1, false},
                                    {-1, -1, false}, {-1, -1, false}, {-1, -1, false}, {-1, -1, false},
                                    {-1, -1, false}, {-1, -1, false}, {-1, -1, false}, {-1, -1, false},
                                    {-1, -1, false}, {-1, -1, false}, {-1, -1, false}, {-1, -1, false},
                                    {-1, -1, false}, {-1, -1, false}, {-1, -1, false}, {-1, -1, false},
                                    {-1, -1, false}, {-1, -1, false}, {-1, -1, false}, {-1, -1, false},
                                    {-1, -1, false}, {-1, -1, false}, {-1, -1, false}, {-1, -1, false},
                                    {-1, -1, false}, {-1, -1, false}, {-1, -1, false}, {-1, -1, false},
                                    {-1, -1, false}, {-1, -1, false}, {-1, -1, false}, {-1, -1, false},
                                    {-1, -1, false}, {-1, -1, false}, {-1, -1, false}, {-1, -1, false},
                                    {-1, -1, false}, {-1, -1, false}, {-1, -1, false}, {-1, -1, false},
                                    {-1, -1, false}, {-1, -1, false}, {-1, -1, false}, {-1, -1, false},
                                };

};

int CameraNode::GetIndexInternal(int node_index)
{
    int index;
    int argus_index = 0;

    if (node_index < 0)
        return -1;

    for (index = 0; index <= node_index; index++)
    {
        if (m_cameraNodes[index].device_index == -1)
        {
            char camera_path[128];
            m_cameraNodes[index].device_index = index;
            snprintf(camera_path, sizeof(camera_path), "%s%d", NVARGUS_DEV_STRING, index);
            if (QueryNode(m_cameraNodes[index].device_index, camera_path) != 0)
                continue;
            if (!m_cameraNodes[index].is_nonbayer)
            {
                m_cameraNodes[index].argus_index = argus_index++;
            }
        }
    }
    if (!m_cameraNodes[node_index].is_nonbayer)
        return m_cameraNodes[node_index].argus_index;

    return -1;
}

#ifdef _DEBUG
std::string fcc2s(unsigned int val)
{
    std::string s;

    s += val & 0x7f;
    s += (val >> 8) & 0x7f;
    s += (val >> 16) & 0x7f;
    s += (val >> 24) & 0x7f;
    if (val & (1 << 31))
        s += "-BE";
    return s;
}
#endif

int CameraNode::QueryNode(int node_index, char* device_path)
{
    struct v4l2_capability v4l2_caps;
    struct v4l2_fmtdesc fmt;
    char usb[] = "usb";
    const char* bayer = "Bayer";

    int32_t camera_fd = open(device_path, O_RDWR);
    if (camera_fd < 0)
    {
        fprintf(stderr, "Failed to open %s: %s\n", device_path,
        strerror(errno));
        return -1;
    }

    memset(&v4l2_caps, 0, sizeof(struct v4l2_capability));
    if (ioctl(camera_fd, VIDIOC_QUERYCAP, &v4l2_caps))
    {
        fprintf(stderr, "%s: not a v4l2 node\n", device_path);
        errno = ENOTTY;
        close(camera_fd);
        return -1;
    }

    if (strstr((char *)v4l2_caps.bus_info, usb) != NULL)
        m_cameraNodes[node_index].is_nonbayer = true;

    memset(&fmt, 0, sizeof(struct v4l2_fmtdesc));
    fmt.type = V4L2_CAP_VIDEO_CAPTURE;
    while (ioctl(camera_fd, VIDIOC_ENUM_FMT, &fmt) == 0)
    {
        if (strcasestr((char *)fmt.description, bayer) != NULL)
        {
            m_cameraNodes[node_index].is_nonbayer = false;
        }
        else
        {
            m_cameraNodes[node_index].is_nonbayer = true;
        }

#ifdef _DEBUG
        printf("\tIndex       : %d\n", fmt.index);
        printf("\tPixel Format: '%s'\n", fcc2s(fmt.pixelformat).c_str());
        printf("\tName        : %s\n", fmt.description);
        printf("\n");
#endif
        fmt.index++;
    }

    close(camera_fd);
    return 0;
}

void *plugin_init(int fd)
{
    struct stat sb;
    char str_proc_fd[512];
    char device_path[512];
    int index = 0;
    int flags = 0;
    int camera_index = -1;
    ssize_t nbytes;
    struct nvargus_plugin_ctx *plugin = NULL;

    memset(str_proc_fd, 0, sizeof(str_proc_fd));
    memset(device_path, 0, sizeof(device_path));

    if (fstat(fd, &sb) == -1)
    {
        perror("stat");
        errno = EINVAL;
        return 0;
    }

    snprintf(str_proc_fd, sizeof(str_proc_fd), "/proc/self/fd/%d", fd);
    nbytes = readlink(str_proc_fd, device_path, sizeof(device_path));
    if (nbytes == -1)
    {
        perror("readlink");
        errno = EINVAL;
        return NULL;
    }

    flags = fcntl(fd, F_GETFL, 0);

    /* Check the valid camera node "/dev/videox" */
    if (strncmp(NVARGUS_DEV_STRING, device_path, 10) == 0)
    {
        /* To know the type of video node being opened
         * by the application, an additional call is made to
         * the camera driver. For USB or other unsupported devices,
         * we return without initializing the argus instance.
         */

        std::string argus_pathname = device_path;

        try {
            camera_index = stoi(argus_pathname.substr(10,
                    argus_pathname.length() - 10));
        }
        catch (...) {
            perror("Error in getting camera_index\n");
            return NULL;
        }

        int32_t argus_index = CameraNode::GetIndex(camera_index);
        if (argus_index == -1)
            return NULL;

        // For Blocking implementation
        if (flags != -1)
        {
            printf("Opening in BLOCKING MODE\n");
        }

        index = ArgusV4L2_Open(argus_index, flags);
        if (index == -1)
        {
            perror("ArgusV4L2_Open failed");
            errno = EINVAL;
            return NULL;
        }
        plugin = (nvargus_plugin_ctx *)calloc(1, sizeof(*plugin));
        if (!plugin)
        {
            perror("Unable to allocate memory for plugin");
            errno = ENOMEM;
            return NULL;
        }
        // CI for Blocking mode only
        plugin->fd_blocking_mode = 1;
        plugin->nvv4l2argus_fd = index;
    }
    else
        return NULL;

    return (void *)plugin;
}

int plugin_ioctl(void *dev_ops_priv, int fd,
        unsigned long int cmd, void *arg)
{
    int ret_val;
    struct nvargus_plugin_ctx *plugin;

    plugin = (struct nvargus_plugin_ctx *)dev_ops_priv;

    ret_val = ArgusV4L2_Ioctl(plugin->nvv4l2argus_fd, cmd, arg);

    return ret_val;
}

void plugin_close(void *dev_ops_priv)
{
    if (dev_ops_priv == NULL)
        return;
    struct nvargus_plugin_ctx *ctx = (struct nvargus_plugin_ctx *)dev_ops_priv;
    ArgusV4L2_Close(ctx->nvv4l2argus_fd);
    free(ctx);
}

#ifdef __cplusplus
extern "C"
#endif
PLUGIN_PUBLIC const struct libv4l_dev_ops libv4l2_plugin =
{
    .init = &plugin_init,
    .close = &plugin_close,
    .ioctl = &plugin_ioctl,
};
