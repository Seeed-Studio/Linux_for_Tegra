/*
 * Copyright (c) 2020-2021, NVIDIA CORPORATION. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <arch_helpers.h>
#include <assert.h>
#include <bpmp_ipc.h>
#include <common/debug.h>
#include <drivers/delay_timer.h>
#include <errno.h>
#include <lib/mmio.h>
#include <lib/psci/psci.h>
#include <se.h>
#include <stdbool.h>
#include <tegra_platform.h>

#include "se_private.h"

/*******************************************************************************
 * Constants and Macros
 ******************************************************************************/
#define MAX_TIMEOUT_MS			U(100)	/* Timeout in 100ms */
#define SE_CTX_SAVE			0U	/* Macro for save ctx operation */
#define SE_CTX_RESTORE 			1U	/* Macro for restore ctx operation */
#define SE_SC7_STATUS_ALL_ENGINE_RDY	U(0x5F) /* SC7_STATUS value to denote all engine ready */

/*
 * Check that SE operation has completed after kickoff.
 *
 * This function is invoked after an SE operation has been started,
 * and it checks SE_SC7_STATUS = IDLE
 */
static bool tegra_se_is_operation_complete(void)
{
	uint32_t val = 0U, timeout = 0U;
	int32_t ret = 0;
	bool se_is_busy = false;

	/*
	 * Poll the status register to check if the operation
	 * completed.
	 */
	do {
		val = tegra_se_read_32(SE0_SC7_STATUS);

		se_is_busy = ((val & SE0_SC7_STATUS_BUSY) != 0U);

		/* sleep until SE finishes */
		if (se_is_busy) {
			mdelay(1);
			timeout++;
		}

	} while (se_is_busy && (timeout < MAX_TIMEOUT_MS));

	if (timeout == MAX_TIMEOUT_MS) {
		ERROR("%s: Atomic context save operation failed!\n",
				__func__);
		ret = -ECANCELED;
	}

	/* check for context save/restore errors - START_ERROR/INTEGRITY_ERROR */
	val = tegra_se_read_32(SE0_INT_STATUS);

	if ((val & SE0_SC7_CTX_START_ERROR) || (val & SE0_SC7_CTX_INTEGRITY_ERROR)) {
		ERROR("CTX START_ERROR/INTEGRITY_ERROR 0x%x\n", val);
		ret = -ECANCELED;
	}

	return (ret == 0);
}

/*
 * Wait for SE engine to be idle and clear any pending interrupts, before
 * starting the next SE operation.
 */
static bool tegra_se_is_ready(void)
{
	int32_t ret = 0;
	uint32_t val = 0, timeout = 0;
	bool se_is_ready = false;

	/* Wait for previous operation to finish */
	do {
		val = tegra_se_read_32(SE0_SC7_STATUS);
		se_is_ready = (val == SE_SC7_STATUS_ALL_ENGINE_RDY);

		/* sleep until SE is ready */
		if (!se_is_ready) {
			mdelay(1);
			timeout++;
		}

	} while (!se_is_ready && (timeout < MAX_TIMEOUT_MS));

	if (timeout == MAX_TIMEOUT_MS) {
		ERROR("%s: SE is not ready!\n", __func__);
		ret = -ETIMEDOUT;
	}
	return (ret == 0);
}

/*
 * do softreset for SE0 engine to clear the HW flag before the context save.
 */
static void tegra_se_softreset(void)
{
	uint32_t val = SE0_SOFTRESET_TRUE, timeout = 0U;

	/* trigger SE0 softreset */
	tegra_se_write_32(SE0_SOFTRESET, SE0_SOFTRESET_TRUE);

	/* Wait for softreset to finish */
	do {
		val = tegra_se_read_32(SE0_SOFTRESET);

		/* sleep until SE is ready */
		mdelay(1);
		timeout++;

	} while ((val != SE0_SOFTRESET_FALSE) && (timeout < MAX_TIMEOUT_MS));

	if (timeout == MAX_TIMEOUT_MS) {
		ERROR("%s: SE softreset fail !\n", __func__);
	}
}

/* Function to perform the context save/restore sequence */
static int32_t tegra_se_program_ctx_ops(uint32_t ctx_op)
{
	int32_t ret = -ECANCELED;
	uint32_t se_int_state, val;

	/* Sequence for context save is as below -
	 * 0. set SE0_SOFTRESET to true and polling it until it change to false.
	 *    This will clear the HW flag to default
	 * 1. Start NVRNG engine by setting SW_ENGINE_ENABLED in NV_NVRNG_R_CTRL0_0
	 * 2. Check if all SE engine is ready by checking SE0_SC7_STATUS_0==0x5F,
	 *    if not by 100ms report timeout.
	 * 3. Enable SC7_CTX_START_ERROR and SC7_CTX_INTEGRITY_ERROR in SE0_INT_ENABLE register.
	 * 4. Issue CTX_SAVE by setting SE0_SC7_CTRL bit 0 to 0.
	 * 5. Check  if SE0_SC7_STATUS_0.bit31 shows busy (or)
	 *    SE0_INT_STATUS.SC7_CTX_START_ERROR (or)
	 *    SE0_INT_STATUS.C7_CTX_INTEGRITY_ERROR then report error.
	 * 6. Clear SE0_INT_STATUS and restore  SE0_INT_ENABLE settings.
	 * Sequence for context restore is same as above but in set#4,
	 * issue CTX_RESTORE by setting SE0_SC7_CTRL bit 0 to 1
	 */

	if (ctx_op == SE_CTX_SAVE) {
		tegra_se_softreset();
	}

	/* Start NVRNG engine */
	val = tegra_rng1_read_32(NV_NVRNG_R_CTRL);
	tegra_rng_write_32(NV_NVRNG_R_CTRL, (val | SW_ENGINE_ENABLED));

	/* Check if SE is in idle state */
	if (!tegra_se_is_ready())
		return ret;

	/* Save SE0_INT_ENABLE and enable SC7 ctx related error report */
	se_int_state = tegra_se_read_32(SE0_INT_ENABLE);
	val = SE0_SC7_CTX_START_ERROR | SE0_SC7_CTX_INTEGRITY_ERROR;
	tegra_se_write_32(SE0_INT_ENABLE, val);

	/* Perform ctx save/restore */
	if (ctx_op == SE_CTX_SAVE) {
		/* Issue context save command */
		tegra_se_write_32(SE0_SC7_CTRL, SE0_SC7_CMD_START_CTX_SAVE);
	} else {
		/* Issue context restore command */
		tegra_se_write_32(SE0_SC7_CTRL, SE0_SC7_CMD_START_CTX_RESTORE);
	}

	/* Check if operation complete and for errors*/
	if (tegra_se_is_operation_complete()) {
		INFO("%s: context save/restore done.\n", __func__);
		ret = 0;
	} else {
		ERROR("%s: context save/restore failed.\n", __func__);
	}

	/* Clear SE0_INT_STATUS and restore SE0_INT_ENABLE */
	tegra_se_write_32(SE0_INT_STATUS, 0);
	tegra_se_write_32(SE0_INT_ENABLE, se_int_state);

	return ret;
}

static int32_t tegra_se_save_context(void)
{
	return tegra_se_program_ctx_ops(SE_CTX_SAVE);
}

static int32_t tegra_se_restore_context(void)
{
	return tegra_se_program_ctx_ops(SE_CTX_RESTORE);
}

/*
 * Handler to power down the SE hardware blocks. This
 * needs to be called only during System Suspend.
 */
int32_t tegra_se_suspend(void)
{
	int32_t ret = 0;

	/* initialise communication channel with BPMP */
	assert(tegra_bpmp_ipc_init() == 0);

	/* Enable SE clock before SE context save */
	ret = tegra_bpmp_ipc_enable_clock(TEGRA_CLK_SE);
	assert(ret == 0);

	/* Issue SE context save */
	ret = tegra_se_save_context();
	assert(ret == 0);

	/* Disable SE clock after SE context save */
	ret = tegra_bpmp_ipc_disable_clock(TEGRA_CLK_SE);
	assert(ret == 0);

	return ret;
}

/*
 * Handler to power up the SE hardware block(s) during System Resume.
 */
void tegra_se_resume(void)
{
	int32_t ret = 0;

	/* initialise communication channel with BPMP */
	assert(tegra_bpmp_ipc_init() == 0);

	/* Enable SE clock before SE context restore */
	ret = tegra_bpmp_ipc_enable_clock(TEGRA_CLK_SE);
	assert(ret == 0);

	/* Issue SE context restore */
	ret = tegra_se_restore_context();
	assert(ret == 0);

	/* Disable SE clock after SE context restore */
	ret = tegra_bpmp_ipc_disable_clock(TEGRA_CLK_SE);
	assert(ret == 0);
}
