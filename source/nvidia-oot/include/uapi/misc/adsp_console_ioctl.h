/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (c) 2016-2023, NVIDIA CORPORATION. All rights reserved.
 */

#ifndef __UAPI_ADSP_CNSL_IOCTL_H
#define __UAPI_ADSP_CNSL_IOCTL_H
#include <linux/ioctl.h>

#if !defined(NVADSP_NAME_SZ)
#define NVADSP_NAME_SZ 128
#endif

#define NVADSP_NAME_SZ_MAX	(NVADSP_NAME_SZ - 1)

#if !defined(ARGV_SIZE_IN_WORDS)
#define ARGV_SIZE_IN_WORDS 128
#endif

#define NV_ADSP_CONSOLE_MAGIC 'q'

struct adsp_consol_run_app_arg_t {
	uint32_t core_id;
	char app_name[NVADSP_NAME_SZ];
	char app_path[NVADSP_NAME_SZ];
	uint32_t args[ARGV_SIZE_IN_WORDS + 1];
	uint64_t ctx1;
	uint64_t ctx2;
};

#define ADSP_CNSL_LOAD _IO(NV_ADSP_CONSOLE_MAGIC, 0x01)
#define ADSP_CNSL_CLR_BUFFER _IO(NV_ADSP_CONSOLE_MAGIC, 0x02)
#define ADSP_CNSL_PUT_DATA _IOW(NV_ADSP_CONSOLE_MAGIC, 0x03, uint32_t *)
#define ADSP_CNSL_RUN_APP _IOWR(NV_ADSP_CONSOLE_MAGIC, 0x04,\
	struct adsp_consol_run_app_arg_t *)
#define ADSP_CNSL_STOP_APP _IOWR(NV_ADSP_CONSOLE_MAGIC, 0x05,\
	struct adsp_consol_run_app_arg_t *)
#define ADSP_CNSL_OPN_MBX _IOW(NV_ADSP_CONSOLE_MAGIC, 0x06,  void *)
#define ADSP_CNSL_CLOSE_MBX _IO(NV_ADSP_CONSOLE_MAGIC, 0x07)
#define ADSP_CNSL_PUT_MBX _IOW(NV_ADSP_CONSOLE_MAGIC, 0x08, uint32_t *)
#define ADSP_CNSL_GET_MBX _IOR(NV_ADSP_CONSOLE_MAGIC, 0x09, uint32_t *)
#define ADSP_CNSL_SUSPEND _IO(NV_ADSP_CONSOLE_MAGIC, 0x0a)
#define ADSP_CNSL_STOP _IO(NV_ADSP_CONSOLE_MAGIC, 0x0b)
#define ADSP_CNSL_RESUME _IO(NV_ADSP_CONSOLE_MAGIC, 0x0c)

#endif
