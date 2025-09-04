/*
 * Copyright (c) 2021, NVIDIA Corporation & AFFILIATES. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef __HWKEY_AGENT_TA_H__
#define __HWKEY_AGENT_TA_H__

/*
 * Each trusted app UUID should have a unique UUID that is
 * generated from a UUID generator such as
 * https://www.uuidgenerator.net/
 *
 * UUID : {82154947-c1bc-4bdf-b89d-04f93c0ea97c}
 */
#define HWKEY_AGENT_TA_UUID \
	{ 0x82154947, 0xc1bc, 0x4bdf, \
		{ 0xb8, 0x9d, 0x04, 0xf9, 0x3c, 0x0e, 0xa9, 0x7c} }

/*
 * HWKEY_AGENT_TA_CMD_ENCRYPTION - Data encryption by EKB USER KEY
 * param[0] in (memref) IV data, IV size
 * param[1] in (memref) payload, payload size
 * param[2] out (memref) output_buf, output_buf size
 * param[3] unused
 */
#define HWKEY_AGENT_TA_CMD_ENCRYPTION	0

/*
 * HWKEY_AGENT_TA_CMD_DECRYPTION - Data decryption by EKB USER KEY
 * param[0] in (memref) IV data, IV size
 * param[1] in (memref) payload, payload size
 * param[2] out (memref) output_buf, output_buf size
 * param[3] unused
 */
#define HWKEY_AGENT_TA_CMD_DECRYPTION	1

/*
 * HWKEY_AGENT_TA_CMD_GET_RANDOM - Get random bytes from RNG
 * param[0] out (memref) RNG data, RNG size
 * param[1] unused
 * param[2] unused
 * param[3] unused
 */
#define HWKEY_AGENT_TA_CMD_GET_RANDOM	2

#endif
