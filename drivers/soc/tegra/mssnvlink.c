/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <linux/of.h>

#ifdef EINVAL
#undef EINVAL
#endif
#define EINVAL 5 /* using arm toolchain value, bug 4876728 */

#define HVC_NR_MSSNVLINK_CALL		0x8006U

#define MSSNVLINK_SUB_CALL_UPDATE_PROT	0x0U
#define HVC_NVL_UPDATE_PROT_OP_MAP 0xE7D46524U

static __attribute__((always_inline)) inline void hyp_call66(uint16_t const id, uint64_t args[6]) {
	register uint64_t x0 asm("x0") = args[0];
	register uint64_t x1 asm("x1") = args[1];
	register uint64_t x2 asm("x2") = args[2];
	register uint64_t x3 asm("x3") = args[3];
	register uint64_t x4 asm("x4") = args[4];
	register uint64_t x5 asm("x5") = args[5];

	asm volatile("HVC %[imm16]"
		: "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x5)
		: [imm16] "i" ((uint32_t)id));

	args[0] = x0;
	args[1] = x1;
	args[2] = x2;
	args[3] = x3;
	args[4] = x4;
	args[5] = x5;
}

/**
 * @brief Update MSSNVLINK protection table providing IPA range to map or unmap
 * 		for iGPU
 *
 * Operation HVC_NVL_UPDATE_PROT_OP_MAP sets the bits of MSSNVLINK protection
 * table accordingly and grant the access while operation HVC_NVL_UPDATE_PROT_OP_UNMAP
 * clears the bits and revoke the access right.
 * There is one lock for each 32bit word of MSSNVLINK protection table. Multiple
 * hypercalls can happen in parallel. The hypercall returns immediately once
 * fail to trying getting lock. Since hypervisor is non-preemptible, the
 * hypercall needs to spend minimal time in hypervisor kernel. The driver
 * defines maximal IPA length a single hypercall can process.
 *
 * @param[in] ipa The start address of IPA range for which MSSNVLINK protection
 * 		table needs to be updated. ipa and underlying PA must be aligned to the
 *      granularity of MSSNVLINK protection table.
 * @param[in] len The length of IPA range for which MSSNVLINK protection table
 * 		needs to be updated. Must be aligned to the granularity of MSSNVLINK
 *      protection table.
 * @param[in] op MSSNVLINK protection table update operation type. Must be
 * 		HVC_NVL_UPDATE_PROT_OP_MAP or HVC_NVL_UPDATE_PROT_OP_UNMAP
 * @param[out] lenReturn Memory region length from start IPA that has been
 * 		successfully processed. lenReturn might be smaller than len.
 *
 * @return Whether MSSNVLINK protection table update succeeded
 * @retval 0 for success
 * @retval -EAGAIN for fail to acquire exclusive resource, the caller should
 *      retry calling it again.
 * @retval other negative values for error
 */
static inline int hyp_nvlink_update_prot(uint64_t ipa,
		uint64_t len, uint64_t op, uint64_t *lenReturn)
{
	uint64_t args[6] = { MSSNVLINK_SUB_CALL_UPDATE_PROT,
		0U, ipa, len, op, 0U };

	hyp_call66(HVC_NR_MSSNVLINK_CALL, args);
	if ((args[0] == 0U) && (lenReturn != NULL)) {
		*lenReturn = args[3];
	}
	return (int) args[0];
}

#define TEGRA_NVL_MAX_RETRIES	10

static int __init tegra_nvl_update_prot_ipa(u64 ipa, u64 len,
			  u32 op, u64 *len_processed)
{
	int ret = -EINVAL;
	int retries = 0;
	u64 len_rem = len;

	*len_processed = 0U;
	do {
		u64 len_ret = 0U;

		ret = hyp_nvlink_update_prot(ipa, len_rem, op, &len_ret);
		if (len_rem < len_ret) {
			pr_err("update prot len ret invalid ");
			ret = -EINVAL;
			break;
		}
		if (ret == 0) {
			len_rem -= len_ret;
			if (len_rem == 0U) {
				break;
			} else {
				ipa += len_ret;
			}
			retries = 0;
		}
		if (ret == -EAGAIN) {
			if (retries >= TEGRA_NVL_MAX_RETRIES) {
				ret = -ETIMEDOUT;
				goto fail;
			}
			retries++;
		}
	} while ((ret == -EAGAIN) || (ret == 0));

	*len_processed = len - len_rem;

fail:
	return ret;
}

static int __init map(u64 ipa, u64 len, u64 *len_processed)
{
	*len_processed = 0U;
	return tegra_nvl_update_prot_ipa(ipa, len,
		HVC_NVL_UPDATE_PROT_OP_MAP, len_processed);
}

static int __init map_whole_vm_memory(void)
{
	const u64 init_granu = 0x400000000; // 16GB
	const u64 min_granu = 0x800000; // Orin MSSNVLINK granularity
	u64 granu = init_granu;
	const u64 ipa_start = 0x80000000;
	u64 ipa = ipa_start;
	u64 len_processed;
	int err;

	while (true) {
		if (granu < min_granu) {
			err = 0; // mapped the whole vm
			break;
		}

		err = map(ipa, granu, &len_processed);
		ipa += len_processed;
		if (err == -EINVAL) {
			granu >>= 1;
		} else if (err != 0) {
			pr_err("mssnvlink map failed err=%d\n", err);
			break;
		}
	}

	return err;
}

static inline bool is_tegra_hypervisor_mode(void)
{
	return of_property_read_bool(of_chosen,
		"nvidia,tegra-hypervisor-mode");
}

int __init mssnvlink_init(void)
{
	if (!of_machine_is_compatible("nvidia,tegra234") ||
		!is_tegra_hypervisor_mode())
		return 0;

	return map_whole_vm_memory();
}
device_initcall(mssnvlink_init);