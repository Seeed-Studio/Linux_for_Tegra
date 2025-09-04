/*
 * Copyright (c) 2020-2024, NVIDIA Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <bl31/ehf.h>
#include <bl31/ea_handle.h>
#include <common/debug.h>
#include <drivers/arm/gic600ae_fmu.h>
#include <lib/bakery_lock.h>
#include <lib/extensions/ras.h>
#include <lib/extensions/ras_arch.h>
#include <lib/utils_def.h>
#include <services/sdei.h>

#include <mce.h>
#include <mce_private.h>
#include <plat/common/platform.h>
#include <platform_def.h>
#include <tegra234_ras_private.h>
#include <tegra_def.h>
#include <tegra_platform.h>
#include <tegra_private.h>

DEFINE_BAKERY_LOCK(tegra234_ras_lock);

/* Forward declarations */
static int32_t tegra234_ras_record_handler(const struct err_record_info *info,
		int probe_data, const struct err_handler_data *const data __unused);
static int32_t tegra234_ras_record_probe(const struct err_record_info *info,
		int *probe_data);
static int32_t tegra234_gic_ras_record_handler(const struct err_record_info *info,
		int probe_data, const struct err_handler_data *const data __unused);
static int32_t tegra234_gic_ras_record_probe(const struct err_record_info *info,
		int *probe_data);

/* Global variables */
static bool ehf_handler_registered;

/*******************************************************************************
 * Tegra234 RAS nodes
 ******************************************************************************/
DEFINE_ONE_RAS_NODE(CCPMU);
DEFINE_ONE_RAS_AUX_DATA(CCPMU);
DEFINE_ONE_RAS_NODE(CMU_CLOCKS);
DEFINE_ONE_RAS_AUX_DATA(CMU_CLOCKS);
DEFINE_ONE_RAS_NODE(IH);
DEFINE_ONE_RAS_AUX_DATA(IH);
DEFINE_ONE_RAS_NODE(IST);
DEFINE_ONE_RAS_AUX_DATA(IST);
DEFINE_ONE_RAS_NODE(IOB);
DEFINE_ONE_RAS_AUX_DATA(IOB);
DEFINE_ONE_RAS_NODE(SNOC);
DEFINE_ONE_RAS_AUX_DATA(SNOC);
DEFINE_ONE_RAS_NODE(SCC);
DEFINE_ONE_RAS_AUX_DATA(SCC);
DEFINE_ONE_RAS_NODE(ACI);
DEFINE_ONE_RAS_AUX_DATA(ACI);
DEFINE_ONE_RAS_NODE(CORE_DCLS);
DEFINE_ONE_RAS_AUX_DATA(CORE_DCLS);
DEFINE_ONE_RAS_NODE(GIC600_FMU);
DEFINE_ONE_RAS_AUX_DATA(GIC600_FMU);

/*
 * We have same probe and handler for each error record group, use a macro to
 * simply the record definition.
 */
#define ADD_ONE_ERR_GROUP(base_addr, size_num_k, aux_data)		\
	ERR_RECORD_MEMMAP_V1((base_addr), (size_num_k),			\
			&tegra234_ras_record_probe,			\
			&tegra234_ras_record_handler,			\
			aux_data)

#define ADD_GICFMU_ERR_GROUP(base_addr, size_num_k, aux_data)		\
	ERR_RECORD_MEMMAP_V1(TEGRA_GICFMU_BASE, 4,			\
		&tegra234_gic_ras_record_probe,				\
		&tegra234_gic_ras_record_handler,			\
		aux_data)

static struct err_record_info tegra234_ras_records_common[] = {
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_CCPMU_BASE, 4, &CCPMU_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_CMU_CLOCKS_BASE, 4, &CMU_CLOCKS_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_IH_BASE, 4, &IH_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_IOB_BASE, 4, &IOB_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_SNOC_BASE, 4, &SNOC_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_SCC_SLICE0_BASE, 4, &SCC_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_SCC_SLICE1_BASE, 4, &SCC_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_SCC_SLICE2_BASE, 4, &SCC_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_SCC_SLICE3_BASE, 4, &SCC_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_SCC_SLICE4_BASE, 4, &SCC_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_SCC_SLICE5_BASE, 4, &SCC_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_SCC_SLICE6_BASE, 4, &SCC_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_SCC_SLICE7_BASE, 4, &SCC_aux_data),
	ADD_GICFMU_ERR_GROUP(TEGRA_GICFMU_BASE, 4, &GIC600_FMU_aux_data)
};

static struct err_record_info tegra234_ras_records_cluster0[] = {
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_IST_CLUSTER0_BASE, 4, &IST_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_ACI_CLUSTER0_BASE, 4, &ACI_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_CL0_CORE0_BASE, 4, &CORE_DCLS_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_CL0_CORE1_BASE, 4, &CORE_DCLS_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_CL0_CORE2_BASE, 4, &CORE_DCLS_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_CL0_CORE3_BASE, 4, &CORE_DCLS_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_CL0_CORE_LS_PAIR1_BASE, 4, &CORE_DCLS_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_CL0_CORE_LS_PAIR2_BASE, 4, &CORE_DCLS_aux_data)
};

static struct err_record_info tegra234_ras_records_cluster1[] = {
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_IST_CLUSTER1_BASE, 4, &IST_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_ACI_CLUSTER1_BASE, 4, &ACI_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_CL1_CORE0_BASE, 4, &CORE_DCLS_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_CL1_CORE1_BASE, 4, &CORE_DCLS_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_CL1_CORE2_BASE, 4, &CORE_DCLS_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_CL1_CORE3_BASE, 4, &CORE_DCLS_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_CL1_CORE_LS_PAIR1_BASE, 4, &CORE_DCLS_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_CL1_CORE_LS_PAIR2_BASE, 4, &CORE_DCLS_aux_data)
};

static struct err_record_info tegra234_ras_records_cluster2[] = {
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_IST_CLUSTER2_BASE, 4, &IST_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_ACI_CLUSTER2_BASE, 4, &ACI_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_CL2_CORE0_BASE, 4, &CORE_DCLS_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_CL2_CORE1_BASE, 4, &CORE_DCLS_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_CL2_CORE2_BASE, 4, &CORE_DCLS_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_CL2_CORE3_BASE, 4, &CORE_DCLS_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_CL2_CORE_LS_PAIR1_BASE, 4, &CORE_DCLS_aux_data),
	ADD_ONE_ERR_GROUP(TEGRA234_RAS_CL2_CORE_LS_PAIR2_BASE, 4, &CORE_DCLS_aux_data)
};

/* Register the common error record to get past RAS driver compilation */
REGISTER_ERR_RECORD_INFO(tegra234_ras_records_common);

/*******************************************************************************
 * Helper functions
 ******************************************************************************/

/* Check if IST is enabled for the platform */
static bool is_ist_enabled(void)
{
	uint32_t val = mmio_read_32(TEGRA_SCRATCH_BASE + SCRATCH_IST_CLK_ENABLE);
	return ((val & BIT(0)) == 1U);
}

/*
 * Helper function to check if the given error record is supported  by
 * the platform
 */
static bool is_err_record_supported(struct err_record_info *record)
{
	const struct ras_aux_data *aux_data =
		(const struct ras_aux_data *)record->aux_data;

	/*
	 * Skip IST, if it is disabled for the platform
	 */
	if (!is_ist_enabled() && !strncmp(aux_data->name, "IST", strlen("IST"))) {
		return false;
	}

	/*
	 * Skip CORE_DCLS nodes if the cluster is running in split
	 * or hybrid mode
	 */
	if ((mce_is_cluster_in_split_mode(0) || mce_is_cluster_in_hybrid_mode(0)) &&
	    (!strncmp(aux_data->name, "CORE_DCLS", strlen("CORE_DCLS")))) {
		return false;
	}
	if ((mce_is_cluster_in_split_mode(1) || mce_is_cluster_in_hybrid_mode(1)) &&
	    (!strncmp(aux_data->name, "CORE_DCLS", strlen("CORE_DCLS")))) {
		return false;
	}
	if ((mce_is_cluster_in_split_mode(2) || mce_is_cluster_in_hybrid_mode(2)) &&
	    (!strncmp(aux_data->name, "CORE_DCLS", strlen("CORE_DCLS")))) {
		return false;
	}

	return true;
}

/*******************************************************************************
 * Error handling functions
 ******************************************************************************/
/* Function to handle error from one given node */
static int32_t tegra234_ras_node_handler(uint32_t base, const char *name,
		const struct ras_error *errors, unsigned int num_errors)
{
	bool found = false;
	uint64_t val = 0U;
	uint32_t ierr = 0U, serr = 0U;
	uint64_t status = mmio_read_64(base + ERR_STATUS(0));
	uint64_t addr = mmio_read_64(base + ERR_ADDR(0));
	uint64_t misc0 = mmio_read_64(base + ERR_MISC0(0));
	uint64_t misc1 = mmio_read_64(base + ERR_MISC1(0));
	uint64_t misc2 = mmio_read_64(base + ERR_MISC2(0));
	uint64_t misc3 = mmio_read_64(base + ERR_MISC3(0));

	/*
	 * repeat until we have ERR<n>STATUS value consistent with
	 * the syndrome recorded for that error
	 */
	while (mmio_read_64(base + ERR_STATUS(0)) != status) {
		status = mmio_read_64(base + ERR_STATUS(0));
		addr = mmio_read_64(base + ERR_ADDR(0));
		misc0 = mmio_read_64(base + ERR_MISC0(0));
		misc1 = mmio_read_64(base + ERR_MISC1(0));
		misc2 = mmio_read_64(base + ERR_MISC2(0));
		misc3 = mmio_read_64(base + ERR_MISC3(0));
	}

	/* not a valid error. */
	if (ERR_STATUS_GET_FIELD(status, V) == 0U) {
		return 0;
	}

	/* extract IERR and SERR from ERR<n>STATUS */
	ierr = (uint32_t)ERR_STATUS_GET_FIELD(status, IERR);
	serr = (uint32_t)ERR_STATUS_GET_FIELD(status, SERR);

	/*
	 * POD Reset SERR value should be a 1 indicating an
	 * "implementation defined" error. But it incorrectly
	 * reports 8'd14, which indicates "illegal access (s/w
	 * faults)".
	 *
	 * If IERR = 28, SERR = 14 change the SERR to 1 as a
	 * work around.
	 */
	if (!strncmp(name, "DSU_IFP", strlen("DSU_IFP")) &&
	    ((ierr == U(28)) && (serr == U(14)))) {
		serr = U(1);
	}

	/*
	 * Ideally, IST CREG lock errors should have a SERR value
	 * of 14, but it is incorrectly reported as 1.
	 *
	 * If IERR = 18, SERR = 1 change the SERR to 14 as a
	 * work around.
	 */
	if (!strncmp(name, "IST", strlen("IST")) &&
	    ((ierr == U(18)) && (serr == U(1)))) {
		serr = U(14);
	}

	ERR_STATUS_SET_FIELD(val, V, 1);

	ERROR("**************************************\n");
	ERROR("RAS %s Error in %s, base=0x%x:\n",
		(ERR_STATUS_GET_FIELD(status, UE) != 0U) ? "Uncorrectable" :
		"Corrected", name, base);
	ERROR("\tStatus = 0x%" PRIx64 "\n", status);

	ERROR("SERR = %s: 0x%x\n", ras_serr_to_str(serr), serr);

	/* Print uncorrectable errror information. */
	if (ERR_STATUS_GET_FIELD(status, UE) != 0U) {

		ERR_STATUS_SET_FIELD(val, UE, 1);
		ERR_STATUS_SET_FIELD(val, UET, 1);

		/* IERR to error message */
		for (uint32_t i = 0; i < num_errors; i++) {
			if (ierr == errors[i].error_code) {
				ERROR("\tIERR = %s: 0x%x\n",
					errors[i].error_msg, ierr);

				found = true;
				break;
			}
		}

		if (!found) {
			ERROR("\tUnknown IERR: 0x%x\n", ierr);
		}

		/* Overflow, multiple errors have been detected. */
		if (ERR_STATUS_GET_FIELD(status, OF) != 0U) {
			ERROR("\tOverflow (there may be more errors) - "
				"Uncorrectable\n");
			ERR_STATUS_SET_FIELD(val, OF, 1);
		}

	} else {
		/* For corrected error, simply clear it. */
		ERR_STATUS_SET_FIELD(val, CE, 1);
	}

	/* Miscellaneous Register Valid. */
	if (ERR_STATUS_GET_FIELD(status, MV) != 0U) {
		ERROR("\tMISC0 = 0x%" PRIx64 "\n", misc0);
		ERROR("\tMISC1 = 0x%" PRIx64 "\n", misc1);
		ERROR("\tMISC2 = 0x%" PRIx64 "\n", misc2);
		ERROR("\tMISC3 = 0x%" PRIx64 "\n", misc3);
		ERR_STATUS_SET_FIELD(val, MV, 1);
	}

	/* Address Valid. */
	if (ERR_STATUS_GET_FIELD(status, AV) != 0U) {
		ERROR("\tADDR = 0x%" PRIx64 "\n", addr);
		ERR_STATUS_SET_FIELD(val, AV, 1);
	}

	/* Deferred error */
	if (ERR_STATUS_GET_FIELD(status, DE) != 0U) {
		ERROR("\tDeferred error\n");
		ERR_STATUS_SET_FIELD(val, DE, 1);
	}

	ERROR("**************************************\n");

	/* Write to clear reported error records */
	mmio_write_64(base + ERR_STATUS(0), val);
	mmio_write_64(base + ERR_MISC0(0), 0);
	mmio_write_64(base + ERR_MISC1(0), 0);
	mmio_write_64(base + ERR_ADDR(0), 0);

	/* error handled */
	return 0;
}

/* Function to handle one error node from an error record group. */
static int32_t tegra234_ras_record_handler(const struct err_record_info *info,
		int probe_data, const struct err_handler_data *data __unused)
{
	const struct ras_aux_data *aux_data = info->aux_data;
	uint32_t base = info->memmap.base_addr;

	/* make sure that we have valid error record info */
	assert(aux_data->error_records != NULL);

	return tegra234_ras_node_handler(base, aux_data->name,
			aux_data->error_records, aux_data->num_err_records);
}

/* Function to probe an error from error record group. */
static int32_t tegra234_ras_record_probe(const struct err_record_info *info,
		int *probe_data)
{
	return ser_probe_memmap(info->memmap.base_addr, info->memmap.size_num_k, probe_data);
}

/* Function to handle one error node from an error record group. */
static int32_t tegra234_gic_ras_record_handler(const struct err_record_info *info,
		int probe_data, const struct err_handler_data *data __unused)
{
	return gic600_fmu_ras_handler(info->memmap.base_addr, probe_data);
}

/* Function to probe an error from error record group. */
static int32_t tegra234_gic_ras_record_probe(const struct err_record_info *info,
		int *probe_data)
{
	return gic600_fmu_probe(info->memmap.base_addr, probe_data);
}

/*
 * Given an RAS interrupt number, locate the registered handler and call it. If
 * no handler was found for the interrupt number, this function panics.
 */
static int tegra234_ras_handler(uint32_t intr_raw, uint32_t flags,
		void *handle, void *cookie)
{
	uint64_t intr_status = ih_read_ras_intstatus(plat_my_core_pos());
	struct err_record_info *selected = NULL;
	int probe_data = 0;
	int ret;
	uint32_t num_records = 0U;

	const struct err_handler_data err_data = {
		.version = ERR_HANDLER_VERSION,
		.interrupt = intr_raw,
		.flags = flags,
		.cookie = cookie,
		.handle = handle
	};

	VERBOSE("%s: irq=%d, intr_status=0x%" PRIx64 "\n", __func__, intr_raw, intr_status);

	/* INTR_STATUS will be '0' for secondary cores */
	if (intr_status == 0)
		return 0;

	if ((intr_status & TEGRA234_IH_STATUS_MASK_CMU) != 0U) {
		selected = tegra234_ras_records_common;
		num_records = ARRAY_SIZE(tegra234_ras_records_common);
	} else if ((intr_status & TEGRA234_IH_STATUS_MASK_SCF) != 0U) {
		selected = tegra234_ras_records_common;
		num_records = ARRAY_SIZE(tegra234_ras_records_common);
	} else if ((intr_status & TEGRA234_IH_STATUS_MASK_CL0) != 0U) {
		selected = tegra234_ras_records_cluster0;
		num_records = ARRAY_SIZE(tegra234_ras_records_cluster0);
	} else if ((intr_status & TEGRA234_IH_STATUS_MASK_CL1) != 0U) {
		selected = tegra234_ras_records_cluster1;
		num_records = ARRAY_SIZE(tegra234_ras_records_cluster1);
	} else if ((intr_status & TEGRA234_IH_STATUS_MASK_CL2) != 0U) {
		selected = tegra234_ras_records_cluster2;
		num_records = ARRAY_SIZE(tegra234_ras_records_cluster2);
	}

	assert(selected != NULL);

	/* check all the error records */
	while (num_records-- > 0U) {

		/* Check if the platform supports this error record */
		if (!is_err_record_supported(selected)) {
			/* next error record */
			selected++;
			continue;
		}

		/* serialize RAS record accesses across CPUs */
		bakery_lock_get(&tegra234_ras_lock);

		/* Is this record valid? */
		ret = selected->probe(selected, &probe_data);

		/* Call error handler for the record group */
		if (ret != 0) {
			/* Invoke handler for the RAS node */
			selected->handler(selected, probe_data, &err_data);

			/* Clear interrupt status */
			ih_write_intstatus_clr(plat_my_core_pos(), intr_status);

			/*
			 * Do an EOI of the RAS interrupt. This allows
			 * the sdei event to be dispatched at the SDEI
			 * event's priority.
			 */
			plat_ic_end_of_interrupt(intr_raw);

			ret = sdei_dispatch_event(TEGRA_SDEI_EP_EVENT_0 +
				plat_my_core_pos());
			if (ret != 0)
				ERROR("sdei_dispatch_event returned %d\n", ret);
		}

		bakery_lock_release(&tegra234_ras_lock);

		/* next error record */
		selected++;
	}

	return 0;
}

/*******************************************************************************
 * This function is invoked by the RAS framework for the platform to handle an
 * External Abort received at EL3.
 ******************************************************************************/
void plat_ea_handler(unsigned int ea_reason, uint64_t syndrome, void *cookie,
		void *handle, uint64_t flags)
{
	ERROR("Exception reason=%u syndrome=0x%" PRIx64 "\n", ea_reason, syndrome);

#if RAS_EXTENSION
	tegra234_ras_handler(0, flags, handle, cookie);
#else
	ERROR("Unhandled External Abort received on 0x%lx at EL3!\n",
			read_mpidr_el1());
	panic();
#endif
}

/*******************************************************************************
 * This function is invoked when an External Abort is received while executing
 * in EL3.
 ******************************************************************************/
void plat_handle_el3_ea(void)
{
	uint64_t syndrome = read_esr_el3();
	uint32_t flags = read_scr_el3() & SCR_NS_BIT;

	ERROR("Exception reason=%u syndrome=0x%" PRIx64 "\n", ERROR_EA_ASYNC, syndrome);

#if RAS_EXTENSION
	tegra234_ras_handler(0, flags, NULL, NULL);
#else
	ERROR("Unhandled External Abort received on 0x%lx at EL3!\n",
			read_mpidr_el1());
	panic();
#endif
}

static void enable_error_detection_for_group(struct err_record_info *group,
		uint32_t num_of_entries)
{
	struct err_record_info *info;

	for (info = group; num_of_entries > 0; num_of_entries--, info++) {

		/* Check if the platform supports this error record */
		if (!is_err_record_supported(info)) {
			continue;
		}

		uint32_t base = info->memmap.base_addr;
		const struct ras_aux_data *aux_data = (const struct ras_aux_data *)info->aux_data;
		/* ERR<n>CTLR register value. */
		uint64_t err_ctlr = 0ULL;
		/* all supported errors for this node. */
		uint64_t err_fr;
		/* uncorrectable errors */
		uint64_t uncorr_errs;
		/* correctable errors */
		uint64_t corr_errs;

		/*
		 * Skip GIC600_FMU init here, as it is done later by
		 * the platform
		 */
		if (!strncmp(aux_data->name, "GIC600_FMU", strlen("GIC600_FMU"))) {
			continue;
		}

		/*
		 * Catch error if something is wrong with the RAS aux data
		 * record table.
		 */
		assert(aux_data != NULL);
		assert(aux_data->get_ue_mask != NULL);
		assert(aux_data->get_ce_mask != NULL);

		/* Read the errors supported by the node */
		err_fr = mmio_read_64(base + ERR_FR(0)) & ERR_FR_EN_BITS_MASK;
		uncorr_errs = aux_data->get_ue_mask();
		corr_errs = aux_data->get_ce_mask();

		VERBOSE("[%s] ERR<n>FR=0x%" PRIx64 ", expected=0x%" PRIx64 "\n", aux_data->name,
			err_fr, uncorr_errs | corr_errs);

		/* enable error reporting and logging */
		ERR_CTLR_ENABLE_FIELD(err_ctlr, ED);

		/* uncorrected error recovery interrupt enable */
		if ((uncorr_errs & err_fr) != 0ULL) {
			ERR_CTLR_ENABLE_FIELD(err_ctlr, UI);
		}

		/* fault handling interrupt for corrected errors enable */
		if ((corr_errs & err_fr) != 0ULL) {
			ERR_CTLR_ENABLE_FIELD(err_ctlr, CFI);
		}

		/* enable all the supported errors */
		err_ctlr |= (err_fr & (uncorr_errs | corr_errs));

		/* enable specified errors, or set to 0 if no supported error */
		VERBOSE("[%s] ERR<n>CTLR=0x%" PRIx64 "\n", aux_data->name, err_ctlr);
		mmio_write_64(base + ERR_CTLR(0), err_ctlr);

		/* Assert if all errors have not been enabled */
		err_ctlr &= ERR_FR_EN_BITS_MASK;
		assert((mmio_read_64(base + ERR_CTLR(0)) & ERR_FR_EN_BITS_MASK) == err_ctlr);
	}
}

/*
 * Function to enable reporting for RAS error records common to all clusters
 *
 * Uncorrectable and correctged errors are set to report as interrupts.
 */
void tegra234_ras_init_common(void)
{
	uint32_t i;

	/* RAS is only supported by limited platforms */
	if (!tegra_platform_is_fpga() && !tegra_platform_is_silicon()) {
		return;
	}

	/* all FHI/ERI bits must be enabled for all cores */
	for (i = 0; i < PLATFORM_CORE_COUNT; i++) {
		ih_write_ras_general_enb(i, ENB_ALL_INTR_SOURCES);
		ih_write_ras_general_mask(i, 0);
	}

	/* Enable RAS error detection and reporting for common group */
	enable_error_detection_for_group(tegra234_ras_records_common,
		ARRAY_SIZE(tegra234_ras_records_common));

	/* install common interrupt handler */
	if (!ehf_handler_registered) {
		ehf_register_priority_handler(PLAT_RAS_PRI, tegra234_ras_handler);
		ehf_handler_registered = true;
	}

	VERBOSE("RAS handling enabled for common nodes\n");
}

/* Struct to describe the RAS error records for CPU clusters */
typedef struct err_record_desc {
	struct err_record_info *record;
	uint32_t num_entries;
} err_record_desc_t;

/*
 * Function to enable reporting for RAS error records for a given cluster
 *
 * Uncorrectable and correctged errors are set to report as interrupts.
 */
void tegra234_ras_init_my_cluster(void)
{
	int id = MPIDR_AFFLVL2_VAL(read_mpidr());
	err_record_desc_t list[] = {
		{
			.record = tegra234_ras_records_cluster0,
			.num_entries = ARRAY_SIZE(tegra234_ras_records_cluster0)
		},
		{
			.record = tegra234_ras_records_cluster1,
			.num_entries = ARRAY_SIZE(tegra234_ras_records_cluster1)
		},
		{
			.record = tegra234_ras_records_cluster2,
			.num_entries = ARRAY_SIZE(tegra234_ras_records_cluster2)
		}
	};

	assert(id < ARRAY_SIZE(list));

	/* RAS is only supported by limited platforms */
	if (!tegra_platform_is_fpga() && !tegra_platform_is_silicon()) {
		return;
	}

	/* Enable RAS error detection and reporting for the cluster */
	if (mce_is_cluster_present(id)) {
		enable_error_detection_for_group(list[id].record, list[id].num_entries);
	}

	VERBOSE("RAS handling enabled for cluster%d\n", id);
}

#if DEBUG

#define ERR_MISC0_CEC_SHIFT		U(32)
#define ERR_MISC0_CEC_VALUE		ULL(0x7F7F)
#define ERR_PFGCTL			U(0x808)
#define ERR_PFGCDN			U(0x810)

int tegra234_ras_inject_fault(uint64_t base, uint64_t pfgcdn, uint64_t pfgctl)
{
	uint64_t errctlr, val;

	if ((base < TEGRA234_RAS_CCPMU_BASE) ||
	    (base > TEGRA234_RAS_CL2_DSU_IFP_BASE)) {
		/* No record found */
		return -EINVAL;
	}

	/* Set ERR<n>MISC0.CEC bits */
	val = mmio_read_64(base + ERR_MISC0(0));
	val |= (ERR_MISC0_CEC_VALUE << ERR_MISC0_CEC_SHIFT);
	mmio_write_64(base + ERR_MISC0(0), val);

	errctlr = mmio_read_64(base + ERR_CTLR(0));
	errctlr &= ERR_FR_EN_BITS_MASK;

	if ((pfgctl & errctlr) == 0U)
		return -ENOTSUP;

	VERBOSE("%s: PFGCDN=0x%" PRIx64 ", PFGCTL=0x%" PRIx64 "\n", __func__,
		pfgcdn, pfgctl);
	mmio_write_64(base + ERR_PFGCDN, pfgcdn);
	mmio_write_64(base + ERR_PFGCTL, pfgctl);

	return 0;
}
#endif
