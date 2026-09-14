/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (c) 2014-2024, NVIDIA CORPORATION. All rights reserved.
 */

#ifndef __TEGRA_NVADSP_OS_H
#define __TEGRA_NVADSP_OS_H

#include <linux/firmware.h>
#include "adsp_shared_struct.h"

#include "dev.h"

#define CONFIG_ADSP_DRAM_LOG_WITH_TAG	1
/* enable profiling of load init start */
#define RECORD_STATS			0

#define SYM_NAME_SZ 128

#define OS_LOAD_TIMEOUT		5000 /* ms */
#define ADSP_COM_MBOX_ID	2

/*
 * ADSP OS Config
 *
 * DECOMPRESS (Bit 0)  : Set if ADSP FW needs to be decompressed
 * VIRT CONFIG (Bit 1) : Set if virtualized configuration
 * DMA PAGE (Bits 7:4) : Contains DMA page information
 */

#define ADSP_CONFIG_DECOMPRESS_SHIFT  0
#define ADSP_CONFIG_DECOMPRESS_EN     1
#define ADSP_CONFIG_DECOMPRESS_MASK   (1 << ADSP_CONFIG_DECOMPRESS_SHIFT)

#define ADSP_CONFIG_VIRT_SHIFT        1
#define ADSP_CONFIG_VIRT_EN           1
#define ADSP_CONFIG_VIRT_MASK         (1 << ADSP_CONFIG_VIRT_SHIFT)

#define ADSP_CONFIG_DMA_PAGE_SHIFT    4
#define ADSP_CONFIG_DMA_PAGE_MASK     (0xF << ADSP_CONFIG_DMA_PAGE_SHIFT)

enum adsp_os_cmd {
	ADSP_OS_BOOT_COMPLETE,
	ADSP_OS_SUSPEND,
	ADSP_OS_RESUME,
	ADSP_OS_STOP,
};

#if RECORD_STATS
#define RECORD_STAT(x) \
	(x = ktime_to_ns(ktime_get()) - x)
#define EQUATE_STAT(x, y) \
	(x = y)
#define RECORD_TIMESTAMP(x) \
	(x = nvadsp_get_timestamp_counter())
#else
#define RECORD_STAT(x)
#define EQUATE_STAT(x, y)
#define RECORD_TIMESTAMP(x)
#endif

/**
 * struct global_sym_info - Global Symbol information required by app loader.
 * @name:	Name of the symbol
 * @addr:	Address of the symbol
 * @info:	Type and binding attributes
 */
struct global_sym_info {
	char name[SYM_NAME_SZ];
	uint32_t addr;
	unsigned char info;
};

struct adsp_module {
	const char			*name;
	void				*handle;
	void				*module_ptr;
	uint32_t			adsp_module_ptr;
	size_t				size;
	const struct app_mem_size	mem_size;
	bool				dynamic;
	char				version[16];
};

struct app_load_stats {
	s64 ns_time_load;
	s64 ns_time_service_parse;
	s64 ns_time_module_load;
	s64 ns_time_req_firmware;
	s64 ns_time_layout;
	s64 ns_time_native_load;
	s64 ns_time_load_mbox_send_time;
	s64 ns_time_load_wait_time;
	s64 ns_time_native_load_complete;
	u64 ns_time_adsp_map;
	u64 ns_time_adsp_app_load;
	u64 ns_time_adsp_send_status;
	u64 adsp_receive_timestamp;
	u64 host_send_timestamp;
	u64 host_receive_timestamp;
};

struct app_init_stats {
	s64 ns_time_app_init;
	s64 ns_time_app_alloc;
	s64 ns_time_instance_memory;
	s64 ns_time_native_call;
	u64 ns_time_adsp_app_init;
	u64 ns_time_adsp_mem_instance_map;
	u64 ns_time_adsp_init_call;
	u64 ns_time_adsp_send_status;
	u64 adsp_receive_timestamp;
};

struct app_start_stats {
	s64 ns_time_app_start;
	s64 ns_time_native_call;
	s64 ns_time_adsp_app_start;
	u64 ns_time_app_thread_creation;
	u64 ns_time_app_thread_detach;
	u64 ns_time_app_thread_resume;
	u64 ns_time_adsp_send_status;
	u64 adsp_receive_timestamp;
};

static inline int nvadsp_os_init(struct platform_device *pdev)
{
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(pdev);

	if (drv_data->chip_data->os_init)
		return drv_data->chip_data->os_init(pdev);

	return -EINVAL;
}

int nvadsp_os_probe(struct platform_device *);
int nvadsp_app_module_probe(struct platform_device *);
void *nvadsp_da_to_va_mappings(struct nvadsp_drv_data *, u64 da, int len);
int nvadsp_add_load_mappings(struct nvadsp_drv_data *,
	phys_addr_t pa, void *mapping, int len);
struct elf32_shdr *nvadsp_get_section(const struct firmware *, char *);
struct global_sym_info *find_global_symbol(struct nvadsp_drv_data *,
	const char *);

struct adsp_module *load_adsp_dynamic_module(const char *, const char *,
	struct device *);
struct adsp_module *load_adsp_static_module(const char *,
	struct adsp_shared_app *, struct device *);
void unload_adsp_module(struct adsp_module *);

int load_adsp_static_apps(struct nvadsp_drv_data *);

#endif /* __TEGRA_NVADSP_OS_H */
