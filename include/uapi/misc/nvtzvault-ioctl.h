// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 */

#ifndef __UAPI_NVTZVAULT_IOCTL_H
#define __UAPI_NVTZVAULT_IOCTL_H

#include <asm-generic/ioctl.h>

#define NVTZVAULT_IOC_MAGIC	0x99
#define NVTZVAULT_CMDID_OPEN_SESSION	(0x01U)
#define NVTZVAULT_CMDID_INVOKE_CMD	(0x02U)
#define NVTZVAULT_CMDID_CLOSE_SESSION	(0x03U)
#define NVTZVAULT_TA_MAX_PARAMS	(8U)
#define NVTZVAULT_TA_UUID_LEN		(16U)

struct nvtzvault_teec_memref {
	void *buffer;
	size_t size;
};

struct nvtzvault_teec_value {
	uint32_t a;
	uint32_t b;
};

struct nvtzvault_teec_parameter {
	struct nvtzvault_teec_memref memref;
	struct nvtzvault_teec_value value;
};

struct nvtzvault_teec_operation {
	uint32_t started;
	uint32_t param_types;
	struct nvtzvault_teec_parameter params[NVTZVAULT_TA_MAX_PARAMS];
};

struct nvtzvault_open_session_ctl {
	uint8_t uuid[NVTZVAULT_TA_UUID_LEN];
	struct nvtzvault_teec_operation operation;
	uint32_t session_id;
};
#define NVTZVAULT_IOCTL_OPEN_SESSION _IOW(NVTZVAULT_IOC_MAGIC, NVTZVAULT_CMDID_OPEN_SESSION, \
						struct nvtzvault_open_session_ctl)
struct nvtzvault_invoke_cmd_ctl {
	uint32_t session_id;
	uint32_t command_id;
	struct nvtzvault_teec_operation operation;
};
#define NVTZVAULT_IOCTL_INVOKE_CMD _IOW(NVTZVAULT_IOC_MAGIC, NVTZVAULT_CMDID_INVOKE_CMD, \
						struct nvtzvault_invoke_cmd_ctl)

struct nvtzvault_close_session_ctl {
	uint32_t session_id;
};
#define NVTZVAULT_IOCTL_CLOSE_SESSION _IOW(NVTZVAULT_IOC_MAGIC, NVTZVAULT_CMDID_CLOSE_SESSION, \
						struct nvtzvault_close_session_ctl)

#endif
