/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES.
 */

#include <config.h>
#include <crypto/crypto.h>
#include <drivers/tegra/tegra_se_rng.h>
#include <initcall.h>
#include <io.h>
#include <kernel/boot.h>
#include <kernel/delay.h>
#include <kernel/tee_time.h>
#include <kernel/panic.h>
#include <rng_support.h>
#include <stdlib.h>
#include <string.h>
#include <tee/tee_cryp_utl.h>
#include <tegra_driver_srv_intf.h>
#include <tegra_driver_rng1.h>
#include <utee_defines.h>

#define ELP_ENABLE	1
#define ELP_DISABLE	0
#define ELP_FALSE	0
#define ELP_TRUE	1

#define SE_RNG1_CTRL_OFFSET			0xf00

#define SE_RNG1_MODE_OFFSET			0xf04
#define SE_RNG1_MODE_PRED_RESIST_SHIFT		3
#define SE_RNG1_MODE_SEC_ALG			0
#define SE_RNG1_MODE_PRED_RESIST_EN \
	(ELP_ENABLE << SE_RNG1_MODE_PRED_RESIST_SHIFT)
#define SE_RNG1_MODE_SEC_ALG_AES_256 \
	(ELP_TRUE << SE_RNG1_MODE_SEC_ALG)

#define SE_RNG1_SMODE_OFFSET			0xf08
#define SE_RNG1_SMODE_MAX_REJECTS_SHIFT		2
#define SE_RNG1_SMODE_SECURE_SHIFT		1
#define SE_RNG1_SMODE_MAX_REJECTS_DEFAULT \
	(0xa << SE_RNG1_SMODE_MAX_REJECTS_SHIFT)
#define SE_RNG1_SMODE_SECURE_EN \
	(ELP_ENABLE << SE_RNG1_SMODE_SECURE_SHIFT)

#define SE_RNG1_STAT_OFFSET			0xf0c
#define SE_RNG1_STAT_BUSY_SHIFT			31
#define SE_RNG1_STAT_BUSY(x) \
	(x << SE_RNG1_STAT_BUSY_SHIFT)

#define SE_RNG1_IE_OFFSET			0xf10
#define SE_RNG1_INT_ENABLE_OFFSET		0xfc0

#define SE_RNG1_ISTAT_OFFSET			0xf14
#define SE_RNG1_ISTAT_CLEAR			0x1f
#define SE_RNG1_ISTAT_DONE_OFFSET		4
#define SE_RNG1_ISTAT_DONE \
	(ELP_TRUE << SE_RNG1_ISTAT_DONE_OFFSET)

#define SE_RNG1_ALARMS_OFFSET			0xf18
#define SE_RNG1_ALARMS_CLEAR			0x1f

#define SE_RNG1_RAND0_OFFSET			0xf24

#define SE_RNG1_INT_STATUS_OFFSET		0xfc4
#define SE_RNG1_INT_STATUS_CLEAR		0x30000
#define SE_RNG1_INT_STATUS_MUTEX_TIMEOUT	0x20000

#define SE_RNG1_ECTL_OFFSET			0xfc8
#define SE_RNG1_ECTL_RESET_SHIFT		5
#define SE_RNG1_ECTL_RESET(x) \
	(x << SE_RNG1_ECTL_RESET_SHIFT)

#define SE_RNG1_MUTEX_WATCHDOG_COUNTER_OFFSET	0xfd0
#define SE_RNG1_MUTEX_TIMEOUT_ACTION_OFFSET	0xfd4
#define SE_RNG1_MUTEX_TIMEOUT_ACTION		0xb
#define SE_RNG1_MUTEX_REQUEST_RELEASE_OFFSET	0xfd8

/*
 * Number of bytes that are generated each time 'gen_random' is executed.
 * RNG1 generates 128 random bits (16 bytes) at a time.
 */
#define RNG1_NUM_BYTES_PER_GEN	16

/*
 * Maximum number of bytes allowed in one se_rng1_get_random() call.
 */
#define RNG1_MAX_BYTES		(1024 * 32)

#define NUM_CTS_PER_GEN		(0x0150 * 4)
#define NUM_EXTRA_CTS		(0x0ff000 * 4)
#define MUTEX_POLL_INTERVAL_US	10
#define MUTEX_POLL_MAX_ATTEMPTS	20

typedef enum {
	RNG1_CMD_NOP = 0,
	RNG1_CMD_GEN_NOISE = 1,
	RNG1_CMD_GEN_NONCE = 2,
	RNG1_CMD_CREATE_STATE = 3,
	RNG1_CMD_RENEW_STATE = 4,
	RNG1_CMD_REFRESH_ADDIN = 5,
	RNG1_CMD_GEN_RANDOM = 6,
	RNG1_CMD_ADVANCE_STATE = 7,
	RNG1_CMD_KAT = 8,
	RNG1_CMD_ZEROIZE = 15
} se_rng1_cmd_t;

/* Internal functions */
static TEE_Result se_rng1_accquire_mutex(uint32_t mutex_timeout);
static void se_rng1_release_mutex(void);
static TEE_Result se_rng1_check_alarms(void);
static void se_rng1_clear_alarms(void);
static TEE_Result se_rng1_wait_for_idle(void);
static TEE_Result se_rng1_write_settings(void);
static TEE_Result se_rng1_execute_cmd(se_rng1_cmd_t cmd);

static vaddr_t rng1_va_base = 0;

static TEE_Result se_rng1_accquire_mutex(uint32_t mutex_timeout)
{
	uint32_t i;

	if (!rng1_va_base) {
		EMSG("%s: RNG1 is not available.", __func__);
		return TEE_ERROR_ACCESS_DENIED;
	}

	for (i = 0; i < MUTEX_POLL_MAX_ATTEMPTS; i++) {
		uint32_t val;

		val = io_read32(rng1_va_base +
				SE_RNG1_MUTEX_REQUEST_RELEASE_OFFSET);

		if (val == 0x01) {
			io_write32(rng1_va_base +
				   SE_RNG1_MUTEX_TIMEOUT_ACTION_OFFSET,
				   SE_RNG1_MUTEX_TIMEOUT_ACTION);
			io_write32(rng1_va_base + SE_RNG1_INT_STATUS_OFFSET,
				   SE_RNG1_INT_STATUS_CLEAR);
			io_write32(rng1_va_base +
				   SE_RNG1_MUTEX_WATCHDOG_COUNTER_OFFSET,
				   mutex_timeout);
			return TEE_SUCCESS;
		}

		udelay(MUTEX_POLL_INTERVAL_US);
	}

	EMSG("%s: ERROR: RNG1 could not acquire mutex.", __func__);
	return TEE_ERROR_BUSY;
}

static void se_rng1_release_mutex(void)
{
	io_write32(rng1_va_base + SE_RNG1_MUTEX_REQUEST_RELEASE_OFFSET, 0x01);
}

static TEE_Result se_rng1_wait_for_idle(void)
{
	uint32_t stat;

	stat = io_read32(rng1_va_base + SE_RNG1_STAT_OFFSET);

	while (stat & SE_RNG1_STAT_BUSY(ELP_TRUE)) {
		uint32_t int_stat;

		int_stat = io_read32(rng1_va_base + SE_RNG1_INT_STATUS_OFFSET);
		if (int_stat & SE_RNG1_INT_STATUS_MUTEX_TIMEOUT) {
			EMSG("%s: ERROR: RNG1 operation time out.", __func__);
			/* Reset RNG1 */
			io_write32(rng1_va_base + SE_RNG1_ECTL_OFFSET,
				   SE_RNG1_ECTL_RESET(ELP_TRUE));

			return TEE_ERROR_BUSY;
		}

		stat = io_read32(rng1_va_base + SE_RNG1_STAT_OFFSET);
	}

	return TEE_SUCCESS;
}

static TEE_Result se_rng1_check_alarms(void)
{
	uint32_t val;

	val = io_read32(rng1_va_base + SE_RNG1_ALARMS_OFFSET);
	if (val) {
		EMSG("%s: ERROR: RNG1 ALARMS error.", __func__);
		se_rng1_clear_alarms();
		return TEE_ERROR_GENERIC;
	}

	return TEE_SUCCESS;
}

static void se_rng1_clear_alarms(void)
{
	io_write32(rng1_va_base + SE_RNG1_ALARMS_OFFSET, SE_RNG1_ALARMS_CLEAR);
}

static TEE_Result se_rng1_write_settings(void)
{
	TEE_Result rc = TEE_SUCCESS;

	/* Clear ISTAT and ALARMS */
	se_rng1_clear_alarms();
	io_write32(rng1_va_base + SE_RNG1_ISTAT_OFFSET, SE_RNG1_ISTAT_CLEAR);

	/* Disable all interrupts */
	io_write32(rng1_va_base + SE_RNG1_IE_OFFSET, 0x0);
	io_write32(rng1_va_base + SE_RNG1_INT_ENABLE_OFFSET, 0x0);

	rc = se_rng1_wait_for_idle();
	if (rc != TEE_SUCCESS)
		return rc;

	/* Enable secure mode */
	io_write32(rng1_va_base + SE_RNG1_SMODE_OFFSET,
		   SE_RNG1_SMODE_MAX_REJECTS_DEFAULT | SE_RNG1_SMODE_SECURE_EN);

	/* Configure RNG1 mode */
	io_write32(rng1_va_base + SE_RNG1_MODE_OFFSET,
		   SE_RNG1_MODE_PRED_RESIST_EN | SE_RNG1_MODE_SEC_ALG_AES_256);

	return TEE_SUCCESS;
}

static TEE_Result se_rng1_execute_cmd(se_rng1_cmd_t cmd)
{
	TEE_Result rc;

	/* Write cmd to the CTRL register */
	io_write32(rng1_va_base + SE_RNG1_CTRL_OFFSET, cmd);

	rc = se_rng1_wait_for_idle();
	if (rc != TEE_SUCCESS) {
		EMSG("%s: ERROR: command = %x.", __func__, cmd);
		return rc;
	}

	io_write32(rng1_va_base + SE_RNG1_ISTAT_OFFSET, SE_RNG1_ISTAT_DONE);

	return TEE_SUCCESS;
}

static TEE_Result se_rng1_get_random(void *data, uint32_t data_len)
{
	TEE_Result rc = TEE_SUCCESS, err;
	uint8_t *data_buf = data;
	uint32_t num_gens, num_extra_bytes, mutex_timeout, i;

	if ((data_buf == NULL) ||
	    (data_len == 0) ||
	    (data_len > RNG1_MAX_BYTES))
		return TEE_ERROR_BAD_PARAMETERS;

	num_gens = data_len / RNG1_NUM_BYTES_PER_GEN;
	num_extra_bytes = data_len % RNG1_NUM_BYTES_PER_GEN;
	mutex_timeout = (num_gens * NUM_CTS_PER_GEN) + NUM_EXTRA_CTS;

	rc = se_rng1_accquire_mutex(mutex_timeout);
	if (rc != TEE_SUCCESS)
		return rc;

	/* Configure RNG1 */
	rc = se_rng1_write_settings();
	if (rc != TEE_SUCCESS)
		goto exit;

	/* Zeroize to reset the state */
	rc = se_rng1_execute_cmd(RNG1_CMD_ZEROIZE);
	if (rc != TEE_SUCCESS)
		goto exit;

	/* Generate a random seed and instantiate a new state */
	rc = se_rng1_execute_cmd(RNG1_CMD_GEN_NOISE);
	if (rc != TEE_SUCCESS)
		goto exit;
	rc = se_rng1_execute_cmd(RNG1_CMD_CREATE_STATE);
	if (rc != TEE_SUCCESS)
		goto exit_zeroize;

	/* Re-generate the seed then renew the stat */
	rc = se_rng1_execute_cmd(RNG1_CMD_GEN_NOISE);
	if (rc != TEE_SUCCESS)
		goto exit_zeroize;
	rc = se_rng1_execute_cmd(RNG1_CMD_RENEW_STATE);
	if (rc != TEE_SUCCESS)
		goto exit_zeroize;

	/* Loop until we've generated a sufficient number of random bytes */
	for (i = 0; i < num_gens; i++) {
		rc = se_rng1_execute_cmd(RNG1_CMD_GEN_RANDOM);
		if (rc != TEE_SUCCESS)
			goto exit_zeroize;

		memcpy((data_buf + RNG1_NUM_BYTES_PER_GEN * i),
		       (void*)(rng1_va_base + SE_RNG1_RAND0_OFFSET), RNG1_NUM_BYTES_PER_GEN);

		rc = se_rng1_execute_cmd(RNG1_CMD_ADVANCE_STATE);
		if (rc != TEE_SUCCESS)
			goto exit_zeroize;
		rc = se_rng1_execute_cmd(RNG1_CMD_GEN_NOISE);
		if (rc != TEE_SUCCESS)
			goto exit_zeroize;
		rc = se_rng1_execute_cmd(RNG1_CMD_RENEW_STATE);
		if (rc != TEE_SUCCESS)
			goto exit_zeroize;
	}

	if (num_extra_bytes != 0) {
		rc = se_rng1_execute_cmd(RNG1_CMD_GEN_RANDOM);
		if (rc != TEE_SUCCESS)
			goto exit_zeroize;

		memcpy((data_buf + RNG1_NUM_BYTES_PER_GEN * num_gens),
		       (void*)(rng1_va_base + SE_RNG1_RAND0_OFFSET), num_extra_bytes);
	}

exit_zeroize:
	/* Zeroize for security purpose */
	err = se_rng1_execute_cmd(RNG1_CMD_ZEROIZE);
	if (rc == TEE_SUCCESS)
		rc = err;

exit:
	err = se_rng1_check_alarms();
	if (rc == TEE_SUCCESS)
		rc = err;

	se_rng1_release_mutex();

	return rc;
}

static TEE_Result se_rng1_init(void)
{
	TEE_Result rc = TEE_SUCCESS;

	rc = se_rng1_accquire_mutex(NUM_EXTRA_CTS);
	if (rc != TEE_SUCCESS)
		return rc;

	rc = se_rng1_wait_for_idle();
	if (rc != TEE_SUCCESS)
		goto exit;

	rc = se_rng1_check_alarms();
	if (rc != TEE_SUCCESS)
		goto exit;

	/* Clear ISTAT register */
	io_write32(rng1_va_base + SE_RNG1_ISTAT_OFFSET, SE_RNG1_ISTAT_CLEAR);

exit:
	se_rng1_release_mutex();

	return rc;
}

static TEE_Result tegra_se_rng1_init(void)
{
	TEE_Result rc;

	rc = tegra_rng1_map_regs(&rng1_va_base);
	if (rc != TEE_SUCCESS)
		return rc;

	rc = se_rng1_init();

	return rc;
}

/* Override the weak plat_rng_init */
void plat_rng_init(void)
{
	TEE_Result rc = TEE_SUCCESS;

	rc = tegra_se_rng1_init();

	if (rc == TEE_SUCCESS) {
		tegra_drv_srv_intf_t *drv_srv_intf;

		drv_srv_intf = tegra_drv_srv_intf_get();
		if (drv_srv_intf != NULL)
			drv_srv_intf->rng_get_random = se_rng1_get_random;
	} else {
		EMSG("%s: Failed to initialize RNG (%x).", __func__, rc);
		panic();
	}
}
