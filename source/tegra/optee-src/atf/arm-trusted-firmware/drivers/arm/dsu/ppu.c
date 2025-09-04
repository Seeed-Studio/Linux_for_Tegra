/*
 * Copyright (c) 2022, NVIDIA Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <arch_helpers.h>
#include <assert.h>
#include <inttypes.h>
#include <lib/libc/errno.h>

#include <common/debug.h>
#include <drivers/arm/dsu.h>
#include <lib/xlat_tables/xlat_tables_v2.h>

/* Global variables */
static uint64_t ub_base;
static uint64_t ub_per_socket_mmio_size;
static uint32_t ub_num_cpus_per_socket;

/*
 * Follow the sequence from DSU-110 TRM section 6.6.1 to power on the
 * core. Expect PPU_PWPR settings for the cluster and the core, from
 * the platform.
 */
int dsu_ppu_core_power_on(uint32_t socket, uint32_t core,
	uint32_t cluster_ppu_pwpr, uint32_t core_ppu_pwpr)
{
	uint32_t val;
	uint64_t ub_base_socket;

	assert(ub_num_cpus_per_socket > 0U);

	/* Sanity check core number */
	if (core > (ub_num_cpus_per_socket - 1U)) {
		ERROR("%s: invalid core %" PRIx32 "\n", __func__, core);
		return -EINVAL;
	}

	/* Sanity check base address */
	if (ub_base == U(0)) {
		ERROR("%s: invalid base %" PRIx64 "\n", __func__, ub_base);
		return -ENXIO;
	}

	/* Calculate utility bus offset for the socket */
	ub_base_socket = ub_base + (ub_per_socket_mmio_size * socket);

	/*
	 * Set the static power mode policy to platform provided
	 * value
	 */
	dsu_ppu_cluster_write_32(core, ub_base_socket, PPU_PWPR, cluster_ppu_pwpr);
	isb();

	/*
	 * Poll the cluster PPU_PWSR register until the value
	 * read matches the value written to the PPU_PWPR
	 * register
	 */
	do {
		val = dsu_ppu_cluster_read_32(core, ub_base_socket, PPU_PWSR);
	} while (val != cluster_ppu_pwpr);

	/*
	 * Set the static power mode policy to platform provided
	 * value
	 */
	dsu_ppu_core_write_32(core, ub_base_socket, PPU_PWPR, core_ppu_pwpr);
	isb();

	/*
	 * Poll the core PPU_PWSR register until the value
	 * read matches the value written to the PPU_PWPR
	 * register
	 */
	do {
		val = dsu_ppu_core_read_32(core, ub_base_socket, PPU_PWSR);
	} while (val != core_ppu_pwpr);

	return 0;
}

/*
 * Follow the sequence from DSU-110 TRM section 6.6.2 to power off the
 * core. Expect PPU_PWPR settings for the cluster and the core, from
 * the platform.
 */
int dsu_ppu_core_power_off(uint32_t socket, uint32_t core,
	uint32_t cluster_ppu_pwpr, uint32_t core_ppu_pwpr)
{
	uint64_t ub_base_socket;

	assert(ub_num_cpus_per_socket > 0U);

	/* Sanity check core number */
	if (core > (ub_num_cpus_per_socket - 1U)) {
		return -EINVAL;
	}

	/* Sanity check base address */
	if (ub_base == U(0)) {
		ERROR("%s: invalid base %" PRIx64 "\n", __func__, ub_base);
		return -ENXIO;
	}

	/* Calculate utility bus offset for the socket */
	ub_base_socket = ub_base + (ub_per_socket_mmio_size * socket);

	/*
	 * Set the static power mode policy to platform provided
	 * value
	 */
	dsu_ppu_core_write_32(core, ub_base_socket, PPU_PWPR, core_ppu_pwpr);
	isb();

	/*
	 * This sets the static power mode policy to platform provided
	 * value
	 */
	dsu_ppu_cluster_write_32(core, ub_base_socket, PPU_PWPR, cluster_ppu_pwpr);
	isb();

	return 0;
}

/*
 * Setup PPU driver. Pass the utility bus base address.
 */
int dsu_ppu_setup(uint64_t base, uint64_t per_socket_mmio_size,
		  unsigned int num_cpus_per_socket)
{
	uint32_t num_sockets = PLATFORM_CORE_COUNT / num_cpus_per_socket;
	int ret;

	if (base == U(0)) {
		return -EINVAL;
	}

	/* Ensure that we dont exceed UINT64 boundary */
	if ((UINT64_MAX / per_socket_mmio_size) < num_sockets) {
		ERROR("%s: invalid number of sockets %" PRIx8 "\n", __func__, num_sockets);
		return -ENOMEM;
	}

	/* Ensure that we dont exceed UINT64 boundary */
	if ((UINT64_MAX - (per_socket_mmio_size * num_sockets)) < base) {
		ERROR("%s: invalid number of sockets %" PRIx8 "\n", __func__, num_sockets);
		return -ENOMEM;
	}

	/* save global variables */
	ub_base = base;
	ub_per_socket_mmio_size = per_socket_mmio_size;
	ub_num_cpus_per_socket = num_cpus_per_socket;

	/* map memory for per-socket DSU region */
	for (unsigned int i = 0; i < num_sockets; i++) {
		uint64_t socket_base = base + (i * per_socket_mmio_size);

		ret = mmap_add_dynamic_region(socket_base, socket_base,
			PPU_REGION_SIZE * num_cpus_per_socket,
			MT_DEVICE | MT_RW | MT_SECURE);
		if (ret < 0) {
			ERROR("%s: failed to map PPU region 0x%lx for socket %d - error code %d\n",
				__func__, socket_base, i, ret);
			return ret;
		}
	}

	return 0;
}
