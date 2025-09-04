/*
 * Copyright (c) 2020, NVIDIA CORPORATION.  All rights reserved.
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
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/vt.h>
#include <linux/kd.h>

#include "vt_switch.h"

/*
 * release_vt - Sets the tty back to TEXT mode.
 */
void release_vt(struct vt_info *info)
{
    int ret;
    int kd_mode;

    if (info->console_fd < 0) {
        return;
    }

    ret = ioctl(info->console_fd, KDSETMODE, KD_TEXT);
    if (ret < 0) {
        printf("KDSETMODE KD_TEXT failed, err=%s\n", strerror(errno));
        goto fail;
    }

    ret = ioctl(info->console_fd, KDGETMODE, &kd_mode);
    if (ret < 0) {
        printf("KDGETMODE failed, err=%s\n", strerror(errno));
        goto fail;
    }

    if (info->active_vt >= 0) {
        ret = ioctl(info->console_fd, VT_ACTIVATE, info->active_vt);
        if (ret < 0) {
            printf("VT_ACTIVATE failed, err=%s\n", strerror(errno));
            goto fail;
        }

        ret = ioctl(info->console_fd, VT_WAITACTIVE, info->active_vt);
        if (ret < 0) {
            printf("VT_WAITACTIVE failed, err= %s\n", strerror(errno));
            goto fail;
        }
    }

fail:
    close(info->console_fd);

    info->console_fd = -1;
    info->active_vt = -1;
}

/*
 * acquire_vt - Sets the tty to GRAPHICAL mode.
 */
int acquire_vt(struct vt_info *info)
{
    int i, ret, fd, vtno, kd_mode;
    struct vt_stat vts;
    const char *vcs[] = { "/dev/vc/%d", "/dev/tty%d", NULL };
    static char vtname[11];

    fd = open("/dev/tty0", O_WRONLY);
    if (fd < 0) {
        printf("Can't open /dev/tty0 err=%s\n", strerror(errno));
        return 0;
    }

    ret = ioctl(fd, VT_OPENQRY, &vtno);
    if (ret < 0) {
        printf("VT_OPENQRY failed, err=%s\n", strerror(errno));
        close(fd);
        return 0;
    }

    if (vtno == -1) {
        printf("Can't find free VT\n");
        close(fd);
        return 0;
    }

    printf("Using VT number %d\n", vtno);
    close(fd);

    i = 0;
    while (vcs[i] != NULL) {
        snprintf(vtname, sizeof(vtname), vcs[i], vtno);
        info->console_fd = open(vtname, O_RDWR | O_NDELAY, 0);
        if (info->console_fd >= 0) {
            break;
        }
        i++;
    }

    if (info->console_fd < 0) {
        printf("Can't open virtual console %d\n", vtno);
        return 0;
    }


    ret = ioctl(info->console_fd, KDGETMODE, &kd_mode);
    if (ret < 0) {
        printf("KDGETMODE failed, err=%s\n", strerror(errno));
        goto fail;
    }

    if (kd_mode != KD_TEXT) {
        printf("%s is already in graphics mode, "
                "seems like some display server running\n", vtname);
    }

    ret = ioctl(info->console_fd, VT_ACTIVATE, vtno);
    if (ret < 0) {
      printf("VT_ACTIVATE failed, err=%s\n", strerror(errno));
      goto fail;
    }

    ret = ioctl(info->console_fd, VT_WAITACTIVE, vtno);
    if (ret < 0) {
        printf("VT_WAITACTIVE failed, err=%s\n", strerror(errno));
        goto fail;
    }

    ret = ioctl(info->console_fd, VT_GETSTATE, &vts);
    if (ret < 0) {
        printf("VT_GETSTATE failed, err=%s\n", strerror(errno));
        goto fail;
    } else {
        info->active_vt = vts.v_active;
    }

    ret = ioctl(info->console_fd, KDSETMODE, KD_GRAPHICS);
    if (ret < 0) {
        printf("KDSETMODE KD_GRAPHICS failed, err=%s\n", strerror(errno));
        goto fail;
    }

    ret = ioctl(info->console_fd, KDGETMODE, &kd_mode);
    if (ret < 0) {
        printf("KDGETMODE failed, err=%s\n", strerror(errno));
        goto fail;
    }

    if (kd_mode != KD_TEXT) {
        printf("%s is in graphics mode\n", vtname);
    }

    return 1;

fail:
    close (info->console_fd);
    return 0;
}
