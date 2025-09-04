// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES.
 */

#include <io.h>
#include <kernel/delay.h>
#include <mm/core_memprot.h>
#include <tegra_se_mgnt.h>
#include <trace.h>

#define TEGRA_SE_AES_BLOCK_SIZE		16
#define TEGRA_SE_AES_IV_SIZE		16
#define TEGRA_SE_KEY_128_SIZE		16
#define TEGRA_SE_KEY_256_SIZE		32

#define SE_INT_STATUS_REG_OFFSET	0x2f0 // SE0_AES0_INT_STATUS_0

#define SE_CRYPTO_KEY_INDEX_SHIFT	24 // SE0_AES0_CRYPTO_CONFIG_0_KEY_INDEX_SHIFT
#define SE_CRYPTO_KEY_INDEX(x)		(x << SE_CRYPTO_KEY_INDEX_SHIFT)

#define SE_CRYPTO_CORE_SEL_SHIFT	9 // SE0_AES0_CRYPTO_CONFIG_0_CORE_SEL_SHIFT
#define CORE_ENCRYPT			1
#define SE_CRYPTO_CORE_SEL(x)		(x << SE_CRYPTO_CORE_SEL_SHIFT)

#define SE_CRYPTO_IV_SEL_SHIFT		7
#define IV_ORIGINAL			0
#define IV_UPDATED			1
#define SE_CRYPTO_IV_SEL(x)		(x << SE_CRYPTO_IV_SEL_SHIFT)

#define SE_CRYPTO_VCTRAM_SEL_SHIFT	5 // SE0_AES0_CRYPTO_CONFIG_0_VCTRAM_SEL_SHIFT
#define VCTRAM_AESOUT			2
#define SE_CRYPTO_VCTRAM_SEL(x)		(x << SE_CRYPTO_VCTRAM_SEL_SHIFT)

#define SE_CRYPTO_XOR_POS_SHIFT		1 // SE0_AES0_CRYPTO_CONFIG_0_XOR_POS_SHIFT
#define XOR_BYPASS			0
#define XOR_TOP				2
#define SE_CRYPTO_XOR_POS(x)		(x << SE_CRYPTO_XOR_POS_SHIFT)

#define SE_CRYPTO_HASH_SHIFT		0  // SE0_AES0_CRYPTO_CONFIG_0_HASH_ENB_SHIFT
#define HASH_DISABLE			0
#define HASH_ENABLE			1
#define SE_CRYPTO_HASH(x)		(x << SE_CRYPTO_HASH_SHIFT)

#define SE_CONFIG_ENC_MODE_SHIFT	24  // SE0_AES0_CONFIG_0_ENC_MODE_SHIFT
#define AES_MODE_KEY128			0   // SE_MODE_PKT_AESAES_MODE_KEY128
#define AES_MODE_KEY256			2   // SE_MODE_PKT_AESAES_MODE_KEY256
#define SE_CONFIG_ENC_MODE(x)		(x << SE_CONFIG_ENC_MODE_SHIFT)

#define SE_CONFIG_ENC_ALG_SHIFT		12 // SE0_AES0_CONFIG_0_ENC_ALG_SHIFT
#define SE_CONFIG_DEC_ALG_SHIFT		8  // SE0_AES0_CONFIG_0_DEC_ALG_SHIFT
#define ALG_NOP				0
#define ALG_AES_ENC			1
#define SE_CONFIG_ENC_ALG(x)		(x << SE_CONFIG_ENC_ALG_SHIFT)
#define SE_CONFIG_DEC_ALG(x)		(x << SE_CONFIG_DEC_ALG_SHIFT)

#define SE_CONFIG_DST_SHIFT		2   // SE0_AES0_CONFIG_0_DST_SHIFT
#define DST_MEMORY			0
#define SE_CONFIG_DST(x)		(x << SE_CONFIG_DST_SHIFT)

#define SE_CONFIG_REG_OFFSET		0x204 // SE0_AES0_CONFIG_0
#define SE_CRYPTO_REG_OFFSET		0x208 // SE0_AES0_CRYPTO_CONFIG_0

#define SE0_AES0_IN_ADDR_0		0x20c
#define SE0_AES0_IN_ADDR_HI_0		0x210
#define SE0_AES0_OUT_ADDR_0		0x214
#define SE0_AES0_OUT_ADDR_HI_0		0x218
#define MSB_SHIFT			24

#define SE_BLOCK_COUNT_REG_OFFSET	0x22c // SE0_AES0_CRYPTO_LAST_BLOCK_0
#define SE_OPERATION_REG_OFFSET		0x238 // SE0_AES0_OPERATION_0
#define LASTBUF_TRUE			16
#define SE_OPERATION_SHIFT		0
#define OP_START			(1 | (1 << LASTBUF_TRUE))
#define SE_OPERATION(x)			(x << SE_OPERATION_SHIFT)

#define SE_STATUS			0x2f4 // SE0_AES0_STATUS_0

#define SE0_MUTEX_REQUEST_RELEASE 0x50
#define SE0_MUTEX_REQUEST_RELEASE_0_LOCK_TRUE 0x1
#define SE0_MUTEX_STATUS 0x64

#define MAX_POLL_ATTEMPTS_NO_SLEEP 100
#define MUTEX_POLL_WAIT_INTERVAL_ZERO 0

static TEE_Result acquire_sap_hw_mutex(vaddr_t se_base, uint32_t sleep_time,
				       uint32_t poll_attempts)
{
	uint32_t count = 0;
	uint32_t data = 0;

	while (count < poll_attempts) {
		/*
		 * When MUTEX_REQUEST_RELEASE register is 0, read the
		 * register will set the LOCK to be 1 (atomic), then it means
		 * the MUTEX had been gained by the read request.
		 *
		 * If the mutex is already locked, then any read request to this
		 * register will get the return value of 0, even from the MUTEX
		 * owner.
		 */
		if ((io_read32(se_base + SE0_MUTEX_REQUEST_RELEASE) &
		    SE0_MUTEX_REQUEST_RELEASE_0_LOCK_TRUE) == 1)
		    return TEE_SUCCESS;

		if (sleep_time)
			udelay(sleep_time);
		count++;
	}

	/* Unsuccessful */
	data = io_read32(se_base + SE0_MUTEX_STATUS);
	EMSG("%s: ERROR. Acquire mutex failed."
	     "mutex status: 0x%x\n", __func__, data);

	return TEE_ERROR_BUSY;
}

static void release_sap_hw_mutex(vaddr_t se_base)
{
	io_write32(se_base + SE0_MUTEX_REQUEST_RELEASE,
		   SE0_MUTEX_REQUEST_RELEASE_0_LOCK_TRUE);
}

TEE_Result se_mutex_acquire(vaddr_t se_base)
{
	if (!se_base) {
		EMSG("%s: SE is not available.", __func__);
		return TEE_ERROR_ACCESS_DENIED;
	}

	return acquire_sap_hw_mutex(se_base, MUTEX_POLL_WAIT_INTERVAL_ZERO,
				    MAX_POLL_ATTEMPTS_NO_SLEEP);
}

void se_mutex_release(vaddr_t se_base)
{
	uint32_t intstat;

	/* Clear SE interrupt stat after the usage */
	intstat = io_read32(se_base + SE_INT_STATUS_REG_OFFSET);
	io_write32(se_base + SE_INT_STATUS_REG_OFFSET, intstat);
	/* Readback to flush */
	intstat = io_read32(se_base + SE_INT_STATUS_REG_OFFSET);

	release_sap_hw_mutex(se_base);
}

uint32_t se_aes_get_config(se_aes_op_mode mode, uint32_t keylen)
{
	uint32_t val = 0;

	switch (mode) {
	case SE_AES_OP_MODE_CBC:
	case SE_AES_OP_MODE_CMAC:
	case SE_AES_OP_MODE_ECB:
		val = SE_CONFIG_ENC_ALG(ALG_AES_ENC) |
		      SE_CONFIG_DEC_ALG(ALG_NOP);
		break;
	default:
		break;
	};

	if (keylen == TEGRA_SE_KEY_256_SIZE)
		val |= SE_CONFIG_ENC_MODE(AES_MODE_KEY256);
	else
		val |= SE_CONFIG_ENC_MODE(AES_MODE_KEY128);

	val |= SE_CONFIG_DST(DST_MEMORY);

	return val;
}

uint32_t se_aes_get_crypto_config(se_aes_op_mode mode, uint32_t keyslot,
				  bool org_iv)
{
	uint32_t val = 0;

	switch (mode) {
	case SE_AES_OP_MODE_CBC:
	case SE_AES_OP_MODE_CMAC:
		val = SE_CRYPTO_VCTRAM_SEL(VCTRAM_AESOUT) |
		      SE_CRYPTO_XOR_POS(XOR_TOP) |
		      SE_CRYPTO_CORE_SEL(CORE_ENCRYPT);
		break;
	case SE_AES_OP_MODE_ECB:
		val = SE_CRYPTO_VCTRAM_SEL(VCTRAM_AESOUT) |
		      SE_CRYPTO_XOR_POS(XOR_BYPASS) |
		      SE_CRYPTO_CORE_SEL(CORE_ENCRYPT);
		break;
	default:
		break;
	};

	val |= SE_CRYPTO_KEY_INDEX(keyslot) |
	       (org_iv ? SE_CRYPTO_IV_SEL(IV_ORIGINAL) :
		SE_CRYPTO_IV_SEL(IV_UPDATED));

	if (mode == SE_AES_OP_MODE_CMAC)
		val |= SE_CRYPTO_HASH(HASH_ENABLE);
	else
		val |= SE_CRYPTO_HASH(HASH_DISABLE);

	return val;
}

TEE_Result se_aes_start_operation(vaddr_t se_base,
				  uint32_t config, uint32_t crypto_config,
				  uint8_t *src, size_t src_len,
				  uint8_t *dst, size_t dst_len)
{
	paddr_t phy_src, phy_dst;

	phy_src = virt_to_phys((void *)src);
	phy_dst = virt_to_phys((void *)dst);
	if (!phy_src || !phy_dst) {
		EMSG("%s: virt_to_phys failure.", __func__);
		return TEE_ERROR_GENERIC;
	}

	io_write32(se_base + SE_CONFIG_REG_OFFSET, config);
	io_write32(se_base + SE_CRYPTO_REG_OFFSET, crypto_config);

	io_write32(se_base + SE0_AES0_IN_ADDR_0, (uint32_t)phy_src);
	io_write32(se_base + SE0_AES0_IN_ADDR_HI_0,
		   ((uint32_t)(phy_src >> 32) << MSB_SHIFT) | src_len);
	io_write32(se_base + SE0_AES0_OUT_ADDR_0, (uint32_t)phy_dst);
	io_write32(se_base + SE0_AES0_OUT_ADDR_HI_0,
		   ((uint32_t)(phy_dst >> 32) << MSB_SHIFT) | dst_len);

	io_write32(se_base + SE_BLOCK_COUNT_REG_OFFSET, src_len/16 - 1);
	io_write32(se_base + SE_OPERATION_REG_OFFSET, SE_OPERATION(OP_START));

	while (io_read32(se_base + SE_STATUS))
		;

	return TEE_SUCCESS;
}
