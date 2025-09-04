// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES.
 */


#ifndef __JETSON_USER_KEY_PTA_FTPM_H__
#define __JETSON_USER_KEY_PTA_FTPM_H__

typedef enum {
	EKB_FTPM_SN = 91,
	EKB_FTPM_EPS_SEED,
	EKB_FTPM_RSA_EK_CERT,
	EKB_FTPM_EC_EK_CERT,
	EKB_FTPM_ID_MAX,
} ftpm_ekb_id_t;

/* Query fTPM EKB properties */
TEE_Result jetson_user_key_pta_query_ftpm_prop(uint32_t ftpm_ekb_id,
					       uint8_t **data,
					       uint32_t *len);

#endif /* __JETSON_USER_KEY_PTA_FTPM_H__ */
