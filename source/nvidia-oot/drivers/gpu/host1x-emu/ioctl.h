/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef __UAPI_LINUX_EMU_HOST1X_SYNCPT_IOCTL_H
#define __UAPI_LINUX_EMU_HOST1X_SYNCPT_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#if !defined(__KERNEL__)
#define __user
#endif

#define HOST1X_EMU_SYNCPT_IOCTL_MAGIC                   'E'
#define HOST1X_EMU_SYNCPT_INVALID_SYNCPOINT             0xFFFFFFFF
#define HOST1X_EMU_SYNCPT_SUBMIT_MAX_NUM_SYNCPT_INCRS   10

struct host1x_emu_ctrl_alloc_syncpt_args {
    /**
     * @id: [out]
     *
     * ID of allocated syncpoint.
     */
    __u32 id;
    __u32 padding;
};

struct host1x_emu_ctrl_free_syncpt_args {
    /**
     * @id: [in]
     *
     * ID of syncpoint to free.
     */
    __u32 id;
    __u32 padding;
};

struct host1x_emu_ctrl_syncpt_read_args {
    /**
     * @id:
     *
     * ID of the syncpoint to read the current value from.
     */
    __u32 id;

    /**
     * @value:
     *
     * The current syncpoint value. Set by the kernel upon successful
     * completion of the IOCTL.
     */
    __u32 value;
};

struct host1x_emu_ctrl_syncpt_incr_args {
    /**
     * @id:
     *
     * ID of the syncpoint to increment.
     */
    __u32 id;

    /**
     * @val:
     *
     * Syncpoint increment value.
     */
    __u32 val;
};

struct host1x_emu_ctrl_syncpt_wait_args {
    /**
     * @timeout: [in]
     *
     * Absolute timestamp at which the wait will time out.
     */
    __s64 timeout_ns;

    /**
     * @id: [in]
     *
     * ID of syncpoint to wait on.
     */
    __u32 id;

    /**
     * @threshold: [in]
     *
     * Threshold to wait for.
     */
    __u32 threshold;

    /**
     * @value: [out]
     *
     * Value of the syncpoint upon wait completion.
     */
    __u32 value;

    /**
     * @timestamp: [out]
     *
     * CLOCK_MONOTONIC timestamp in nanoseconds taken when the wait completes.
     */
    __u64 timestamp;
};

#define HOST1X_EMU_SYNCPT_IOCTL_CTRL_ALLOC_SYNCPT		\
    _IOWR(HOST1X_EMU_SYNCPT_IOCTL_MAGIC, 1, struct host1x_emu_ctrl_alloc_syncpt_args)

#define HOST1X_EMU_SYNCPT_IOCTL_CTRL_FREE_SYNCPT		\
    _IOWR(HOST1X_EMU_SYNCPT_IOCTL_MAGIC, 2, struct host1x_emu_ctrl_free_syncpt_args)

#define HOST1X_EMU_SYNCPT_IOCTL_CTRL_SYNCPT_READ		\
    _IOWR(HOST1X_EMU_SYNCPT_IOCTL_MAGIC, 3, struct host1x_emu_ctrl_syncpt_read_args)

#define HOST1X_EMU_SYNCPT_IOCTL_CTRL_SYNCPT_INCR		\
    _IOW(HOST1X_EMU_SYNCPT_IOCTL_MAGIC, 4,  struct host1x_emu_ctrl_syncpt_incr_args)

#define HOST1X_EMU_SYNCPT_IOCTL_CTRL_SYNCPT_WAIT		\
    _IOW(HOST1X_EMU_SYNCPT_IOCTL_MAGIC, 5, struct host1x_emu_ctrl_syncpt_wait_args)

#define HOST1X_EMU_SYNCPT_IOCTL_CTRL_LAST			\
    _IOC_NR(HOST1X_EMU_SYNCPT_IOCTL_CTRL_SYNCPT_WAIT)

#define HOST1X_EMU_SYNCPT_IOCTL_CTRL_MAX_ARG_SIZE	\
    sizeof(struct host1x_emu_ctrl_syncpt_wait_args)
#endif
