/*
 * Copyright (c) 2023, NVIDIA Corporation & AFFILIATES. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef __LUKS_SRV_TA_H__
#define __LUKS_SRV_TA_H__

/*
 * Each trusted app UUID should have a unique UUID that is
 * generated from a UUID generator such as
 * https://www.uuidgenerator.net/
 *
 * UUID : {b83d14a8-7128-49df-9624-35f14f65ca6c}
 */
#define LUKS_SRV_TA_UUID \
	{ 0xb83d14a8, 0x7128, 0x49df, \
		{ 0x96, 0x24, 0x35, 0xf1, 0x4f, 0x65, 0xca, 0x6c } }

#define LUKS_SRV_CONTEXT_STR_LEN	40
#define LUKS_SRV_PASSPHRASE_LEN		16

/*
 * LUKS_SRV_TA_CMD_GET_UNIQUE_PASS - Get unique passphrase
 * param[0] in (memref) context string, length
 * param[1] out (memref) passphrase, size
 * param[2] unused
 * param[3] unused
 */
#define LUKS_SRV_TA_CMD_GET_UNIQUE_PASS		0

/*
 * LUKS_SRV_TA_CMD_SRV_DOWN - Turn off the service
 * param[0] unused
 * param[1] unused
 * param[2] unused
 * param[3] unused
 */
#define LUKS_SRV_TA_CMD_SRV_DOWN		1

/*
 * LUKS_SRV_TA_CMD_GET_GENERIC_PASS - Get generic passphrase
 * param[0] in (memref) context string, length
 * param[1] out (memref) passphrase, size
 * param[2] unused
 * param[3] unused
 */
#define LUKS_SRV_TA_CMD_GET_GENERIC_PASS	2

#endif
