/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Copyright (c) 2022 NVIDIA Corporation */

#ifndef _UAPI__LINUX_HOST1X_FENCE_H
#define _UAPI__LINUX_HOST1X_FENCE_H

#include <linux/ioctl.h>
#include <linux/types.h>

#if defined(__cplusplus)
extern "C" {
#endif

struct host1x_create_fence {
	/**
	 * @id: [in]
	 *
	 * ID of the syncpoint to create a fence for.
	 */
	__u32 id;

	/**
	 * @threshold: [in]
	 *
	 * When the syncpoint reaches this value, the fence will be signaled.
	 * The syncpoint is considered to have reached the threshold when the
	 * following condition is true:
	 *
	 * 	((value - threshold) & 0x80000000U) == 0U
	 *
	 */
	__u32 threshold;

	/**
	 * @fence_fd: [out]
	 *
	 * New sync_file file descriptor containing the created fence.
	 */
	__s32 fence_fd;

	__u32 reserved[1];
};

struct host1x_fence_extract_fence {
	__u32 id;
	__u32 threshold;
};

struct host1x_fence_extract {
	/**
	 * @fence_fd: [in]
	 *
	 * sync_file file descriptor
	 */
	__s32 fence_fd;

	/**
	 * @num_fences: [in,out]
	 *
	 * In: size of the `fences_ptr` array counted in elements.
	 * Out: required size of the `fences_ptr` array counted in elements.
	 */
	__u32 num_fences;

	/**
	 * @fences_ptr: [in]
	 *
	 * Pointer to array of `struct host1x_fence_extract_fence`.
	 */
	__u64 fences_ptr;

	__u32 reserved[2];
};

struct host1x_create_pollfd {
	__s32 fd;
	__u32 reserved;
};

struct host1x_trigger_pollfd {
	__s32 fd;
	__u32 id;
	__u32 threshold;
	__u32 reserved;
};

#define HOST1X_IOCTL_CREATE_FENCE        _IOWR('X', 0x02, struct host1x_create_fence)
#define HOST1X_IOCTL_FENCE_EXTRACT       _IOWR('X', 0x05, struct host1x_fence_extract)
#define HOST1X_IOCTL_CREATE_POLLFD       _IOWR('X', 0x10, struct host1x_create_pollfd)
#define HOST1X_IOCTL_TRIGGER_POLLFD      _IOWR('X', 0x11, struct host1x_trigger_pollfd)

#if defined(__cplusplus)
}
#endif

#endif
