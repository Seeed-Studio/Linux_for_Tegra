/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_DEBUGFS_H
#define PVA_KMD_DEBUGFS_H
#include "pva_kmd.h"
#include "pva_kmd_shim_debugfs.h"
#include "pva_kmd_fw_profiler.h"

#define NUM_VPU_BLOCKS 2U

struct pva_kmd_file_ops {
	int (*open)(struct pva_kmd_device *dev);
	int (*release)(struct pva_kmd_device *dev);
	int64_t (*read)(struct pva_kmd_device *dev, void *file_data,
			uint8_t *data, uint64_t offset, uint64_t size);
	int64_t (*write)(struct pva_kmd_device *dev, void *file_data,
			 const uint8_t *data, uint64_t offset, uint64_t size);
	void *pdev;
	void *file_data;
};

struct pva_kmd_debugfs_context {
	bool stats_enable;
	bool vpu_debug;
	bool vpu_print_enable;
	bool entered_sc7;
	char *allowlist_path;
	uint32_t profiling_level;
	struct pva_kmd_file_ops vpu_fops;
	struct pva_kmd_file_ops allowlist_ena_fops;
	struct pva_kmd_file_ops allowlist_path_fops;
	struct pva_kmd_file_ops hwpm_fops;
	struct pva_kmd_file_ops profiling_level_fops;
	void *data_hwpm;
	struct pva_kmd_file_ops vpu_ocd_fops[NUM_VPU_BLOCKS];
	struct pva_kmd_fw_profiling_config g_fw_profiling_config;
	struct pva_kmd_file_ops fw_trace_level_fops;
	struct pva_kmd_file_ops simulate_sc7_fops;
	struct pva_kmd_file_ops r5_ocd_fops;
	void *r5_ocd_stage_buffer;
};

enum pva_error pva_kmd_debugfs_create_nodes(struct pva_kmd_device *dev);
void pva_kmd_debugfs_destroy_nodes(struct pva_kmd_device *dev);

uint64_t pva_kmd_read_from_buffer_to_user(void *to, uint64_t count,
					  uint64_t offset, const void *from,
					  uint64_t available);

#endif //PVA_KMD_DEBUGFS_H
