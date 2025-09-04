/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES.
 */

#ifndef __TEE_REE_STATE_H__
#define __TEE_REE_STATE_H__

#include <tee_api_types.h>

/*
 * Helper functions for OT-TEE to tracking the normal world state.
 */

typedef enum {
	TEE_REE_STATE_UNKNOWN,
	TEE_REE_STATE_BOOT,	/* Early boot stage. */
	TEE_REE_STATE_REE_OS,	/* OP-TEE driver ready. */
	TEE_REE_STATE_REE_SUPP,	/* tee-supplicant ready. */
} tee_ree_state;

#ifdef CFG_REE_STATE
TEE_Result tee_set_ree_state(tee_ree_state state);
tee_ree_state tee_get_ree_state(void);
#else
static inline TEE_Result tee_set_ree_state(tee_ree_state state __unused)
{
	return TEE_SUCCESS;
}

static inline tee_ree_state tee_get_ree_state(void)
{
	return TEE_REE_STATE_UNKNOWN;
}
#endif
#endif
