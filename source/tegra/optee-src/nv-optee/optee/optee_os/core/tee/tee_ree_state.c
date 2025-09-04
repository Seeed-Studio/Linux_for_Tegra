/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES.
 */

#include <tee/tee_ree_state.h>

static tee_ree_state ree_state = TEE_REE_STATE_BOOT;

TEE_Result tee_set_ree_state(tee_ree_state state)
{
	if ((state < ree_state) || (state > TEE_REE_STATE_REE_SUPP))
		return TEE_ERROR_BAD_STATE;

	ree_state = state;

	return TEE_SUCCESS;
}

tee_ree_state tee_get_ree_state(void)
{
	return ree_state;
}
