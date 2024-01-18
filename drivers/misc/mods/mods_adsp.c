// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2014-2024, NVIDIA CORPORATION.  All rights reserved. */

#include <linux/uaccess.h>
#include "mods_internal.h"
#include <linux/tegra_nvadsp.h>

int esc_mods_adsp_load(struct mods_client *client,
			struct MODS_ADSP_INIT_INFO *p)
{
	struct nvadsp_handle *handle = nvadsp_get_handle(p->node);

	return handle->os_load(handle);
}

int esc_mods_adsp_start(struct mods_client *client,
			struct MODS_ADSP_INIT_INFO *p)
{
	struct nvadsp_handle *handle = nvadsp_get_handle(p->node);

	return handle->os_start(handle);
}

int esc_mods_adsp_stop(struct mods_client *client,
			struct MODS_ADSP_INIT_INFO *p)
{
	struct nvadsp_handle *handle = nvadsp_get_handle(p->node);

	return handle->os_suspend(handle);
}

int esc_mods_adsp_run_app(struct mods_client *client,
			struct MODS_ADSP_RUN_APP_INFO *p)
{
	int rc = -1;
	int ret = 0;
	int max_retry = 3;
	int rcount = 0;
	nvadsp_app_handle_t app_handle;
	nvadsp_app_info_t *p_app_info;
	nvadsp_app_args_t app_args;

	struct nvadsp_handle *handle = nvadsp_get_handle(p->node);

	app_handle = handle->app_load(handle, p->app_name,  p->app_file_name);
	if (!app_handle) {
		cl_error("load adsp app fail");
		return -1;
	}

	if (p->argc > 0 && p->argc <= MODS_ADSP_APP_MAX_PARAM) {
		app_args.argc = p->argc;
		memcpy(app_args.argv, p->argv, p->argc * sizeof(__u32));
		p_app_info = handle->app_init(handle, app_handle, &app_args);
	} else
		p_app_info = handle->app_init(handle, app_handle, NULL);

	if (!p_app_info) {
		cl_error("init adsp app fail");
		handle->app_unload(handle, app_handle);
		return -1;
	}

	rc = handle->app_start(handle, p_app_info);
	if (rc) {
		cl_error("start adsp app fail");
		goto failed;
	}

	while (rcount++ < max_retry) {
		rc = handle->wait_for_app_complete_timeout(handle, p_app_info,
						msecs_to_jiffies(p->timeout));
		if (rc == -ERESTARTSYS)
			continue;
		else if (rc == 0) {
			cl_error("app timeout(%d)", p->timeout);
			rc = -1;
		} else if (rc < 0) {
			cl_error("run app failed, err=%d\n", rc);
			rc = -1;
		} else
			rc = 0;
		break;
	}

	ret = p_app_info->return_status;
	if (ret < 0) {
		cl_error("Test failed, err=%d\n", ret);
		rc = -1;
	}

failed:
	handle->app_deinit(handle, p_app_info);
	handle->app_unload(handle, app_handle);

	return rc;
}
