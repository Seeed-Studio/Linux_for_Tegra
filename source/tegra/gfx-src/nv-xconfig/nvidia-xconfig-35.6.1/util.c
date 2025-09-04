/*
 * nvidia-xconfig: A tool for manipulating XF86Config files,
 * specifically for use by the NVIDIA Linux graphics driver.
 *
 * Copyright (C) 2004 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses>.
 *
 *
 * util.c
 */


#include <stdio.h>
#include <stdarg.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <pwd.h>
#include <termios.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include "nvidia-xconfig.h"
#include "msg.h"

Options *__op = NULL;


/*
 * copy_file() - copy the file specified by srcfile to dstfile, using
 * mmap and memcpy.  The destination file is created with the
 * permissions specified by mode.  Roughly based on code presented by
 * Richard Stevens, in Advanced Programming in the Unix Environment,
 * 12.9.
 */

int copy_file(const char *srcfile, const char *dstfile, mode_t mode)
{
    int src_fd = -1, dst_fd = -1;
    struct stat stat_buf;
    char *src, *dst;
    int ret = FALSE;

    if ((src_fd = open(srcfile, O_RDONLY)) == -1) {
        nv_error_msg("Unable to open '%s' for copying (%s)",
                     srcfile, strerror (errno));
        goto done;
    }
    if ((dst_fd = open(dstfile, O_RDWR | O_CREAT | O_TRUNC, mode)) == -1) {
        nv_error_msg("Unable to create '%s' for copying (%s)",
                     dstfile, strerror (errno));
        goto done;
    }
    if (fstat(src_fd, &stat_buf) == -1) {
        nv_error_msg("Unable to determine size of '%s' (%s)",
                     srcfile, strerror (errno));
        goto done;
    }
    if (stat_buf.st_size == 0) {
        /* src file is empty; silently ignore */
        ret = TRUE;
        goto done;
    }
    if (lseek(dst_fd, stat_buf.st_size - 1, SEEK_SET) == -1) {
        nv_error_msg("Unable to set file size for '%s' (%s)",
                     dstfile, strerror (errno));
        goto done;
    }
    if (write(dst_fd, "", 1) != 1) {
        nv_error_msg("Unable to write file size for '%s' (%s)",
                     dstfile, strerror (errno));
        goto done;
    }
    if ((src = mmap(0, stat_buf.st_size, PROT_READ,
                    MAP_SHARED, src_fd, 0)) == (void *) -1) {
        nv_error_msg("Unable to map source file '%s' for "
                     "copying (%s)", srcfile, strerror (errno));
        goto done;
    }
    if ((dst = mmap(0, stat_buf.st_size, PROT_READ | PROT_WRITE,
                    MAP_SHARED, dst_fd, 0)) == (void *) -1) {
        nv_error_msg("Unable to map destination file '%s' for "
                     "copying (%s)", dstfile, strerror (errno));
        goto done;
    }
    
    memcpy(dst, src, stat_buf.st_size);
    
    if (munmap (src, stat_buf.st_size) == -1) {
        nv_error_msg("Unable to unmap source file '%s' after "
                     "copying (%s)", srcfile, strerror (errno));
        goto done;
    }
    if (munmap (dst, stat_buf.st_size) == -1) {
        nv_error_msg("Unable to unmap destination file '%s' after "
                     "copying (%s)", dstfile, strerror (errno));
        goto done;
    }
    
    ret = TRUE;

 done:

    if (src_fd != -1) close(src_fd);
    if (dst_fd != -1) close(dst_fd);
    
    return ret;
    
} /* copy_file() */


/*
 * xconfigPrint() - this is the one entry point that a user of the
 * XF86Config-Parser library must provide.
 */

void xconfigPrint(MsgType t, const char *msg)
{
    typedef struct {
        MsgType msg_type;
        char *prefix;
        FILE *stream;
        int newline;
    } MessageTypeAttributes;

    char *prefix = NULL;
    int i, newline = FALSE;
    FILE *stream = stdout;
    
    const MessageTypeAttributes msg_types[] = {
        { ParseErrorMsg,      "PARSE ERROR: ",      stderr, TRUE  },
        { ParseWarningMsg,    "PARSE WARNING: ",    stderr, TRUE  },
        { ValidationErrorMsg, "VALIDATION ERROR: ", stderr, TRUE  },
        { InternalErrorMsg,   "INTERNAL ERROR: ",   stderr, TRUE  },
        { WriteErrorMsg,      "ERROR: ",            stderr, TRUE  },
        { WarnMsg,            "WARNING: ",          stderr, TRUE  },
        { ErrorMsg,           "ERROR: ",            stderr, TRUE  },
        { DebugMsg,           "DEBUG: ",            stdout, FALSE },
        { UnknownMsg,          NULL,                stdout, FALSE },
    };
    
    for (i = 0; msg_types[i].msg_type != UnknownMsg; i++) {
        if (msg_types[i].msg_type == t) {
            prefix  = msg_types[i].prefix;
            newline = msg_types[i].newline;
            stream  = msg_types[i].stream;
            break;
        }
    }
    
    if (newline) nv_info_msg_to_file(stream, NULL, "");
    nv_info_msg_to_file(stream, prefix, "%s", msg);
    if (newline) nv_info_msg_to_file(stream, NULL, "");
    
} /* xconfigPrint */

/* a round number longer than "PCI:bus@domain:slot:function" */
#define BUS_ID_STRING_LENGTH 32

/*
 * nv_format_busid() - returns a newly allocated formatted string with the PCI
 * Bus ID of the device with the given index, or NULL on failure.
 */
char *nv_format_busid(Options *op, int index)
{
    char buf[BUS_ID_STRING_LENGTH];
    DevicesPtr pDevices;
    NvCfgPciDevice *dev;

    pDevices = find_devices(op);
    if (!pDevices || pDevices->nDevices < 1) {
        nv_error_msg("Unable to find any GPUs in the system.");
        return NULL;
    }
    if (index >= pDevices->nDevices) {
        nv_error_msg("Invalid GPU index value.");
        return NULL;
    }

    dev = &pDevices->devices[index].dev;

    xconfigFormatPciBusString(buf, sizeof(buf),
                              dev->domain, dev->bus, dev->slot, 0);

    return nvstrdup(buf);
}
