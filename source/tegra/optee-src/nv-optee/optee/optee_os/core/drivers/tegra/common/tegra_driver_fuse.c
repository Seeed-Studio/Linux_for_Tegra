/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2022-2024, NVIDIA CORPORATION & AFFILIATES.
 */

#include <config.h>
#include <initcall.h>
#include <io.h>
#include <kernel/boot.h>
#include <kernel/dt.h>
#include <libfdt.h>
#include <stdlib.h>
#include <tegra_driver_common.h>
#include <tegra_driver_fuse.h>
#include <mm/core_mmu.h>

TEE_Result fuse_generate_ecid(vaddr_t fuse_va_base, fuse_ecid_t *ecid_128, uint64_t *ecid_64)
{
	uint32_t vendor, fab, wafer;
	uint32_t lot, lot0, lot1;
	uint32_t i, x, y, digit;
	uint32_t rsvd1;
	uint32_t reg;

	if (fuse_va_base == 0)
		return TEE_ERROR_BAD_PARAMETERS;
	if (ecid_128 == NULL || ecid_64 == NULL)
		return TEE_ERROR_BAD_PARAMETERS;

	reg = io_read32(fuse_va_base + FUSE_OPT_VENDOR_CODE_0);
	vendor = reg & OPT_VENDOR_CODE_MASK;

	reg = io_read32(fuse_va_base + FUSE_OPT_FAB_CODE_0);
	fab = reg & OPT_FAB_CODE_MASK;

	lot0 = io_read32(fuse_va_base + FUSE_OPT_LOT_CODE_0_0);

	lot1 = 0;
	reg = io_read32(fuse_va_base + FUSE_OPT_LOT_CODE_1_0);
	lot1 = reg & OPT_LOT_CODE_1_MASK;

	reg = io_read32(fuse_va_base + FUSE_OPT_WAFER_ID_0);
	wafer = reg & OPT_WAFER_ID_MASK;

	reg = io_read32(fuse_va_base + FUSE_OPT_X_COORDINATE_0);
	x = reg & OPT_X_COORDINATE_MASK;

	reg = io_read32(fuse_va_base + FUSE_OPT_Y_COORDINATE_0);
	y = reg & OPT_Y_COORDINATE_MASK;

	reg = io_read32(fuse_va_base + FUSE_OPT_OPS_RESERVED_0);
	rsvd1 = reg & OPT_OPS_RESERVED_MASK;

	reg = 0;
	reg |= rsvd1 & ECID_ECID0_0_RSVD1_MASK;
	reg |= (y & ECID_ECID0_0_Y_MASK) << ECID_ECID0_0_Y_RANGE;
	reg |= (x & ECID_ECID0_0_X_MASK) << ECID_ECID0_0_X_RANGE;
	reg |= (wafer & ECID_ECID0_0_WAFER_MASK) << ECID_ECID0_0_WAFER_RANGE;
	reg |= (lot1 & ECID_ECID0_0_LOT1_MASK) << ECID_ECID0_0_LOT1_RANGE;
	ecid_128->ecid[0] = reg;

	lot1 >>= 2;

	reg = 0;
	reg |= lot1 & ECID_ECID1_0_LOT1_MASK;
	reg |= (lot0 & ECID_ECID1_0_LOT0_MASK) << ECID_ECID1_0_LOT0_RANGE;
	ecid_128->ecid[1] = reg;

	lot0 >>= 6;

	reg = 0;
	reg |= lot0 & ECID_ECID2_0_LOT0_MASK;
	reg |= (fab & ECID_ECID2_0_FAB_MASK) << ECID_ECID2_0_FAB_RANGE;
	ecid_128->ecid[2] = reg;

	reg = 0;
	reg |= vendor & ECID_ECID3_0_VENDOR_MASK;
	ecid_128->ecid[3] = reg;

	/* Start to get the 64-bit version ECID */
	/* Lot code must be re-encoded from a 5 digit base-36 'BCD' number
         * to a binary number.
         */
	lot = 0;
	reg = io_read32(fuse_va_base + FUSE_OPT_LOT_CODE_0_0);
	reg = reg << 2;
	for (i = 0; i < 5; ++i) {
		digit = (reg & 0xFC000000) >> 26;
		if (digit >=36)
			return TEE_ERROR_GENERIC;

		lot *= 36;
		lot += digit;
		reg <<= 6;
	}

	/* The 64-bit version ECID format:
	 *      Field    Bits  Position Data
	 *      -------  ----  -------- ----------------------------------------
	 *      CID        4     60     Chip id
	 *      VENDOR     4     56     Vendor code
	 *      FAB        6     50     FAB code
	 *      LOT       26     24     Lot code (5-digit base-36-coded-decimal,
	 *                              re-encoded to 26 bits binary)
	 *      WAFER      6     18     Wafer id
	 *      X          9      9     Wafer X-coordinate
	 *      Y          9      0     Wafer Y-coordinate
	 *      -------  ----
	 *      Total     64
	 */
	*ecid_64 = (TEGRA_INT_CID << 60ull)
		| ((unsigned long long)vendor << 56ull)
		| ((unsigned long long)fab << 50ull)
		| ((unsigned long long)lot << 24ull)
		| ((unsigned long long)wafer << 18ull)
		| ((unsigned long long)x << 9ull)
		| ((unsigned long long)y << 0ull);

	return TEE_SUCCESS;
}

TEE_Result fuse_generate_sn(vaddr_t fuse_va_base, uint8_t *sn, uint32_t *size)
{
	uint32_t odm_id_0, odm_id_1, odm_info;
	uint16_t tmp = 0;

	if (fuse_va_base == 0)
		return TEE_ERROR_BAD_PARAMETERS;
	if (sn == NULL || size == NULL)
		return TEE_ERROR_BAD_PARAMETERS;
	if (*size < FUSE_SN_SIZE)
		return TEE_ERROR_BAD_PARAMETERS;

	odm_id_0 = io_read32(fuse_va_base + FUSE_ODMID0_0);
	odm_id_1 = io_read32(fuse_va_base + FUSE_ODMID1_0);
	odm_info = io_read32(fuse_va_base + FUSE_ODM_INFO_0);

	/*
	 * All 4-byte or less Tegra fuses are stored in little-endian so we need
	 * to convert the first 2 bytes of SN here because odm_info is a 4-byte fuse.
	 * Besides PSC_BL1, all usages of SN assume it is saved in big-endian.
	 * For odm_id_0 and odm_id_1, they're treated as a 8-byte fuse so their values
	 * are always stored in big-endian.
	 */
	memcpy(&tmp, &odm_info, sizeof(tmp));
	tmp = __builtin_bswap16(tmp);

	memcpy(sn, &tmp, 2);
	memcpy(sn + 2, &odm_id_0, 4);
	memcpy(sn + 6, &odm_id_1, 4);

	*size = FUSE_SN_SIZE;
	return TEE_SUCCESS;
}

#ifdef CFG_DT
static const char *const tegra_fuse_dt_match_table[] = {
	"nvidia,tegra194-efuse",
	"nvidia,tegra234-efuse",
};

static TEE_Result tegra_fuse_dt_init(vaddr_t *va, size_t *size)
{
	void *fdt = NULL;
	int node = -1;
	uint32_t i = 0;
	size_t map_size;
	enum dt_map_dev_directive mapping = DT_MAP_SECURE;

	if (va == NULL || size == NULL)
		return TEE_ERROR_BAD_PARAMETERS;

	fdt = get_dt();
	if (!fdt) {
		EMSG("%s: DTB is not present.", __func__);
		return TEE_ERROR_ITEM_NOT_FOUND;
	}

	for (i = 0; i < ARRAY_SIZE(tegra_fuse_dt_match_table); i++) {
		node = fdt_node_offset_by_compatible(fdt, 0,
						tegra_fuse_dt_match_table[i]);
		if (node >= 0)
			break;
	}

	if (node < 0) {
		EMSG("%s: DT not found (%x).", __func__, node);
		return TEE_ERROR_ITEM_NOT_FOUND;
	}

	if (dt_map_dev(fdt, node, va, &map_size, mapping) < 0) {
		EMSG("%s: DT unable to map device address.", __func__);
		return TEE_ERROR_GENERIC;
	}

	*size = map_size;
	return TEE_SUCCESS;
}
#else
static TEE_Result tegra_fuse_dt_init(vaddr_t *va)
{
	(void) va;
	return TEE_ERROR_NOT_SUPPORTED;
}
#endif

TEE_Result tegra_fuse_map_regs(vaddr_t *va, size_t *size)
{
	TEE_Result rc = TEE_SUCCESS;

	if (va == NULL || size == NULL)
		return TEE_ERROR_BAD_PARAMETERS;

	if (IS_ENABLED(CFG_DT)) {
		rc = tegra_fuse_dt_init(va, size);
	} else {
		rc = iomap_pa2va(TEGRA_FUSE_BASE, TEGRA_FUSE_SIZE, va);
		if (rc == TEE_SUCCESS)
			*size = TEGRA_FUSE_SIZE;
	}

	return rc;
}

TEE_Result tegra_fuse_unmap_regs(vaddr_t va, size_t size)
{
	return core_mmu_remove_mapping(MEM_AREA_IO_SEC, (void *)va, size);
}
