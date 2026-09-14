// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2022-2023, NVIDIA CORPORATION.  All rights reserved. */

#include "mods_internal.h"

#include <linux/limits.h>
#include <linux/tee_drv.h>
#include <linux/uuid.h>

static const uuid_t mods_ta_uuid =
	UUID_INIT(0xf14e9858, 0x1526, 0x4935,
		  0xb1, 0x92, 0x4d, 0xf3, 0x86, 0x4f, 0x1f, 0xf9);

static int optee_ctx_match(struct tee_ioctl_version_data *ver, const void *data)
{
	if (ver->impl_id == TEE_IMPL_ID_OPTEE)
		return 1;
	else
		return 0;
}

int esc_mods_invoke_optee_ta(struct mods_client *client,
			     struct MODS_OPTEE_PARAMS *p)
{
	int ret;
	u32 session_id;
	u8 *temp_buf;
	struct tee_context *ctx;
	struct tee_ioctl_open_session_arg sess_arg;
	struct tee_ioctl_invoke_arg invoke_arg;
	struct tee_shm *shm;
	struct tee_param params[4];

	/* Open context with TEE driver */
	ctx = tee_client_open_context(NULL, optee_ctx_match, NULL, NULL);
	if (IS_ERR(ctx)) {
		ret = -ENODEV;
		goto fail;
	}

	/* Open session with TA */
	memset(&sess_arg, 0, sizeof(sess_arg));
	export_uuid(sess_arg.uuid, &mods_ta_uuid);
	sess_arg.clnt_login = TEE_IOCTL_LOGIN_PUBLIC;
	sess_arg.num_params = 0;

	ret = tee_client_open_session(ctx, &sess_arg, NULL);
	if ((ret < 0) || (sess_arg.ret != 0)) {
		cl_info("tee_client_open_session failed, err: %x\n", sess_arg.ret);
		ret = -EINVAL;
		goto out_ctx;
	}
	session_id = sess_arg.session;

	/* Allocate dynamic shared memory with TA */
	shm = tee_shm_alloc_kernel_buf(ctx, p->buf_size);
	if (IS_ERR(shm)) {
		cl_info("tee_shm_alloc_kernel_buf failed\n");
		ret = -ENOMEM;
		goto out_session;
	}

	/* Invoke comannd of TA */
	memset(&invoke_arg, 0, sizeof(invoke_arg));
	memset(&params, 0, sizeof(params));

	invoke_arg.func = p->command_id;
	invoke_arg.session = session_id;
	invoke_arg.num_params = 4;

	params[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT;
	params[0].u.memref.shm = shm;
	params[0].u.memref.size = p->buf_size;
	params[0].u.memref.shm_offs = 0;
	params[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT;

	temp_buf = tee_shm_get_va(shm, 0);
	if (IS_ERR(temp_buf)) {
		cl_info("tee_shm_get_va failed\n");
		ret = PTR_ERR(temp_buf);
		goto out_shm;
	}
	memmove(temp_buf, p->buf, p->buf_size);

	ret = tee_client_invoke_func(ctx, &invoke_arg, params);
	if (ret < 0) {
		cl_info("tee_client_invoke_func failed.\n");
		goto out_shm;
	}

	/*
	 * OP-TEE complies with GlobalPlatform API specification.
	 * Its output value(TEEC_VALUE) should be in the u32 range.
	 */
	if (params[1].u.value.a > U32_MAX || params[1].u.value.b > U32_MAX) {
		ret = EOVERFLOW;
		goto out_shm;
	} else {
		p->out_a = (__u32)params[1].u.value.a;
		p->out_b = (__u32)params[1].u.value.b;
	}

	memmove(p->buf, temp_buf, p->buf_size);
	p->tee_ret = invoke_arg.ret;

out_shm:
	tee_shm_free(shm);
out_session:
	tee_client_close_session(ctx, session_id);
out_ctx:
	tee_client_close_context(ctx);
fail:
	return ret;
}
