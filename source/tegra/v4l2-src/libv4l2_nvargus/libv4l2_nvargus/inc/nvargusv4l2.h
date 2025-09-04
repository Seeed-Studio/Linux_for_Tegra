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

/* This file is the public interface for using the libargusV4L2.
libargusv4l2 implements the V4L2 behavior in userspace and hence
uses all the structure/macro definitions from standard V4L2 header file
i.e videodev2.h
Standard system calls like open(), ioctl() and close() are replaced
with below APIs.
*/

#ifndef __NVARGUSV4L2_H__
#define __NVARGUSV4L2_H__

/* Equivalent to the standard open() system call on a V4L2 Device.
   Camera indices do not have one-to-one map with devnode indices.
   Creates an instance and returns a fd equivalent to the client if successful,
   -1 if error.
*/
int ArgusV4L2_Open(const int camera_index, int flags);

/* Equivalent to the standard close() system call on a V4L2 Device.*/
int ArgusV4L2_Close(int fd);

/* Equivalent to the standard ioctl() system call on a V4L2 Device.*/
int ArgusV4L2_Ioctl(int fd, unsigned long cmd, ...);

#endif /* __NVARGUSV4L2_H__ */
