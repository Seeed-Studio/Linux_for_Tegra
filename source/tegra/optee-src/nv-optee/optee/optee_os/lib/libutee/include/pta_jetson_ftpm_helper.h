/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2023-2024, NVIDIA CORPORATION & AFFILIATES.
 */

#ifndef __JETSON_FTPM_HELPER_PTA_H__
#define __JETSON_FTPM_HELPER_PTA_H__

/*
 * Each trusted app UUID should have a unique UUID that is
 * generated from a UUID generator such as
 * https://www.uuidgenerator.net/
 *
 * UUID : {6c879517-2dfc-4663-863d-4896e8ccbe3a}
 */
#define FTPM_HELPER_PTA_UUID \
		{ 0x6c879517, 0x2dfc, 0x4663, \
			{0x86, 0x3d, 0x48, 0x96, 0xe8, 0xcc, 0xbe, 0x3a} }

#define FTPM_HELPER_PTA_NS_STATE_NOT_READY	0xff000001
#define FTPM_HELPER_PTA_NS_STATE_READY		0xff000002

#define FTPM_HELPER_PTA_ECID_LENGTH		8U
#define FTPM_HELPER_PTA_SN_LENGTH		10U
/* Default buffer size for EK Certificate */
#define FTPM_HELPER_PTA_EK_CERT_BUF_SIZE	2048U

/*
 * FTPM_HELPER_PTA_CMD_PING_NS - Ping NS world is ready for TEE services.
 * param[0] out (value) a: normal world status
 * param[1] unused
 * param[2] unused
 * param[3] unused
 */
#define FTPM_HELPER_PTA_CMD_PING_NS		0xffff0001

/*
 * FTPM_HELPER_PTA_CMD_QUERY_SN - Query the device serial number
 * param[0] out (memref) data buffer and size
 * param[1] unused
 * param[2] unused
 * param[3] unused
 */
#define FTPM_HELPER_PTA_CMD_QUERY_SN		0xffff0002

/*
 * FTPM_HELPER_PTA_CMD_QUERY_ECID - Query the device ECID
 * param[0] out (memref) data buffer and size
 * param[1] unused
 * param[2] unused
 * param[3] unused
 */
#define FTPM_HELPER_PTA_CMD_QUERY_ECID		0xffff0003

/*
 * FTPM_HELPER_PTA_CMD_GET_EVT_LOG_SIG_MB2 - Get the signature of the MB2 event log
 * param[0] out (memref) data buffer and size
 * param[1] unused
 * param[2] unused
 * param[3] unused
 */
#define FTPM_HELPER_PTA_CMD_GET_EVT_LOG_SIG_MB2	0xffff0004

/*
 * FTPM_HELPER_PTA_CMD_GET_EVT_LOG_SIG_TOS - Get the signature of the TOS event log
 * param[0] out (memref) data buffer and size
 * param[1] unused
 * param[2] unused
 * param[3] unused
 */
#define FTPM_HELPER_PTA_CMD_GET_EVT_LOG_SIG_TOS	0xffff0005

/*
 * FTPM_HELPER_PTA_CMD_GET_RSA_EK_CERT - Get the fTPM RSA EK Certificate
 * param[0] out (memref) data buffer and size
 * param[1] unused
 * param[2] unused
 * param[3] unused
 */
#define FTPM_HELPER_PTA_CMD_GET_RSA_EK_CERT	0xffff0006

/*
 * FTPM_HELPER_PTA_CMD_GET_EC_EK_CERT - Get the fTPM EC EK Certificate
 * param[0] out (memref) data buffer and size
 * param[1] unused
 * param[2] unused
 * param[3] unused
 */
#define FTPM_HELPER_PTA_CMD_GET_EC_EK_CERT	0xffff0007

/*
 * FTPM_HELPER_PTA_CMD_INJECT_EPS - Set the EPS explicitly from outside
 * param[0] in  (memref) eps buffer and size
 * param[1] unused
 * param[2] unused
 * param[3] unused
 */
#define FTPM_HELPER_PTA_CMD_INJECT_EPS		0xffff0008

#endif /* __JETSON_FTPM_HELPER_PTA_H__ */
