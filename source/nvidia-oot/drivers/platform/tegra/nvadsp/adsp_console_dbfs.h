/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (c) 2014-2024, NVIDIA CORPORATION. All rights reserved.
 */

#ifndef ADSP_CNSL_DBFS_H
#define ADSP_CNSL_DBFS_H

#define ADSP_APP_CTX_MAX    32

struct nvadsp_cnsl {
	struct device *dev;
	struct nvadsp_mbox shl_snd_mbox;
	struct nvadsp_mbox app_mbox;
	int open_cnt;
	uint64_t adsp_app_ctx_vals[ADSP_APP_CTX_MAX];
};

int
adsp_create_cnsl(struct dentry *adsp_debugfs_root, struct nvadsp_cnsl *cnsl);

#endif /* ADSP_CNSL_DBFS_H */
