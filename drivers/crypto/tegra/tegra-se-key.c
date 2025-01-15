// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2023-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * Crypto driver file to manage keys of NVIDIA Security Engine.
 */

#include <linux/bitops.h>
#include <linux/module.h>
#include <crypto/aes.h>

#include "tegra-se.h"

#define SE_KEY_FULL_MASK		GENMASK(SE_MAX_KEYSLOT, 0)

/* Reserve keyslot 0, 14, 15 */
#define SE_KEY_RSVD_MASK		(BIT(0) | BIT(14) | BIT(15))
#define SE_KEY_VALID_MASK		(SE_KEY_FULL_MASK & ~SE_KEY_RSVD_MASK)

/* Mutex lock to guard keyslots */
static DEFINE_MUTEX(kslt_lock);

/* Keyslot bitmask (0 = available, 1 = in use/not available) */
static u16 tegra_se_keyslots = SE_KEY_RSVD_MASK;

static u16 tegra_keyslot_alloc(void)
{
	u16 keyid;

	mutex_lock(&kslt_lock);
	/* Check if all key slots are full */
	if (tegra_se_keyslots == GENMASK(SE_MAX_KEYSLOT, 0)) {
		mutex_unlock(&kslt_lock);
		return 0;
	}

	keyid = ffz(tegra_se_keyslots);
	tegra_se_keyslots |= BIT(keyid);

	mutex_unlock(&kslt_lock);

	return keyid;
}

static void tegra_keyslot_free(u16 slot)
{
	mutex_lock(&kslt_lock);
	tegra_se_keyslots &= ~(BIT(slot));
	mutex_unlock(&kslt_lock);
}

static unsigned int tegra_key_prep_ins_cmd(struct tegra_se *se, u32 *cpuvaddr,
					   const u32 *key, u32 keylen, u16 slot, u32 alg)
{
	int i = 0, j;

	cpuvaddr[i++] = host1x_opcode_setpayload(1);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->op);
	cpuvaddr[i++] = SE_AES_OP_WRSTALL | SE_AES_OP_DUMMY;

	cpuvaddr[i++] = host1x_opcode_setpayload(1);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->manifest);
	cpuvaddr[i++] = se->regcfg->manifest(se->owner, alg, keylen);
	cpuvaddr[i++] = host1x_opcode_setpayload(1);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->key_dst);

	cpuvaddr[i++] = SE_AES_KEY_DST_INDEX(slot);

	for (j = 0; j < keylen / 4; j++) {
		/* Set key address */
		cpuvaddr[i++] = host1x_opcode_setpayload(1);
		cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->key_addr);
		cpuvaddr[i++] = j;

		/* Set key data */
		cpuvaddr[i++] = host1x_opcode_setpayload(1);
		cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->key_data);
		cpuvaddr[i++] = key[j];
	}

	cpuvaddr[i++] = host1x_opcode_setpayload(1);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->config);
	cpuvaddr[i++] = SE_CFG_INS;

	cpuvaddr[i++] = host1x_opcode_setpayload(1);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->op);
	cpuvaddr[i++] = SE_AES_OP_WRSTALL | SE_AES_OP_START |
			SE_AES_OP_LASTBUF;

	cpuvaddr[i++] = se_host1x_opcode_nonincr(host1x_uclass_incr_syncpt_r(), 1);
	cpuvaddr[i++] = host1x_uclass_incr_syncpt_cond_f(1) |
			host1x_uclass_incr_syncpt_indx_f(se->syncpt_id);

	dev_dbg(se->dev, "key-slot %u key-manifest %#x\n",
		slot, se->regcfg->manifest(se->owner, alg, keylen));

	return i;
}

static unsigned int tegra_key_prep_mov_cmd(struct tegra_se *se, u32 *cpuvaddr,
					u32 src_keyid, u32 tgt_keyid)
{
	int i = 0;

	cpuvaddr[i++] = host1x_opcode_setpayload(1);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->op);
	cpuvaddr[i++] = SE_AES_OP_WRSTALL | SE_AES_OP_DUMMY;

	cpuvaddr[i++] = host1x_opcode_setpayload(2);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->src_kslt);
	cpuvaddr[i++] = src_keyid;
	cpuvaddr[i++] = tgt_keyid;

	cpuvaddr[i++] = host1x_opcode_setpayload(1);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->config);
	cpuvaddr[i++] = SE_CFG_MOV;

	cpuvaddr[i++] = host1x_opcode_setpayload(1);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->op);
	cpuvaddr[i++] = SE_AES_OP_WRSTALL | SE_AES_OP_START |
			SE_AES_OP_LASTBUF;

	cpuvaddr[i++] = se_host1x_opcode_nonincr(host1x_uclass_incr_syncpt_r(), 1);
	cpuvaddr[i++] = host1x_uclass_incr_syncpt_cond_f(1) |
			host1x_uclass_incr_syncpt_indx_f(se->syncpt_id);

	dev_dbg(se->dev, "keymov: src keyid %u target keyid %u\n", src_keyid, tgt_keyid);

	return i;
}

static unsigned int tegra_key_prep_invld_cmd(struct tegra_se *se, u32 *cpuvaddr, u32 keyid)
{
	int i = 0;

	cpuvaddr[i++] = host1x_opcode_setpayload(1);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->op);
	cpuvaddr[i++] = SE_AES_OP_WRSTALL | SE_AES_OP_DUMMY;

	cpuvaddr[i++] = host1x_opcode_setpayload(1);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->tgt_kslt);
	cpuvaddr[i++] = keyid;

	cpuvaddr[i++] = host1x_opcode_setpayload(1);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->config);
	cpuvaddr[i++] = SE_CFG_INVLD;

	cpuvaddr[i++] = host1x_opcode_setpayload(1);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->op);
	cpuvaddr[i++] = SE_AES_OP_WRSTALL | SE_AES_OP_START |
			SE_AES_OP_LASTBUF;

	cpuvaddr[i++] = se_host1x_opcode_nonincr(host1x_uclass_incr_syncpt_r(), 1);
	cpuvaddr[i++] = host1x_uclass_incr_syncpt_cond_f(1) |
			host1x_uclass_incr_syncpt_indx_f(se->syncpt_id);

	dev_dbg(se->dev, "invalidate keyid %u\n", keyid);

	return i;
}

static bool tegra_key_in_kslt(u32 keyid)
{
	bool ret;

	if (keyid > SE_MAX_KEYSLOT)
		return false;

	mutex_lock(&kslt_lock);
	ret = ((BIT(keyid) & SE_KEY_VALID_MASK) &&
		(BIT(keyid) & tegra_se_keyslots));
	mutex_unlock(&kslt_lock);

	return ret;
}

static int tegra_key_insert(struct tegra_se *se, const u8 *key,
			    u32 keylen, u16 slot, u32 alg)
{
	const u32 *keyval = (u32 *)key;
	u32 *addr = se->keybuf->addr, size;
	int ret;

	mutex_lock(&kslt_lock);

	size = tegra_key_prep_ins_cmd(se, addr, keyval, keylen, slot, alg);
	ret = tegra_se_host1x_submit(se, se->keybuf, size);

	mutex_unlock(&kslt_lock);

	return ret;
}

static int tegra_key_move_to_kds(struct tegra_se *se, u32 slot, u32 kds_id)
{
	u32 src_keyid, size;
	int ret;

	src_keyid = SE_KSLT_REGION_ID_SYM | slot;
	size = tegra_key_prep_mov_cmd(se, se->cmdbuf->addr, src_keyid, kds_id);

	ret = tegra_se_host1x_submit(se, se->keybuf, size);
	if (ret)
		return ret;

	return 0;
}

static unsigned int tegra_kac_get_from_kds(struct tegra_se *se, u32 keyid, u16 slot)
{
	u32 tgt_keyid, size;
	int ret;

	tgt_keyid = SE_KSLT_REGION_ID_SYM | slot;
	size = tegra_key_prep_mov_cmd(se, se->cmdbuf->addr, keyid, tgt_keyid);

	ret = tegra_se_host1x_submit(se, se->cmdbuf, size);
	if (ret)
		tegra_keyslot_free(slot);

	return ret;
}

static void tegra_key_kds_invalidate(struct tegra_se *se, u32 keyid)
{
	unsigned int size;

	size = tegra_key_prep_invld_cmd(se, se->cmdbuf->addr, keyid);
	tegra_se_host1x_submit(se, se->keybuf, size);
	tegra_kds_free_id(keyid);
}

unsigned int tegra_key_get_idx(struct tegra_se *se, u32 keyid)
{
	u16 slot;

	if (tegra_key_in_kslt(keyid))
		return keyid;

	if (!tegra_key_in_kds(keyid))
		return 0;

	slot = tegra_keyslot_alloc();
	if (!slot)
		return 0;

	tegra_kac_get_from_kds(se, keyid, slot);

	return slot;
}

void tegra_key_invalidate(struct tegra_se *se, u32 keyid, u32 alg)
{
	u8 zkey[AES_MAX_KEY_SIZE] = {0};

	if (!keyid)
		return;

	if (tegra_key_in_kds(keyid)) {
		tegra_key_kds_invalidate(se, keyid);
	} else {
		tegra_key_insert(se, zkey, AES_MAX_KEY_SIZE, keyid, alg);
		tegra_keyslot_free(keyid);
	}
}

int tegra_key_submit(struct tegra_se *se, const u8 *key, u32 keylen, u32 alg, u32 *keyid)
{
	u32 kds_id, orig_id = *keyid;
	int ret;

	/* Use the existing slot if it is already allocated */
	if (!tegra_key_in_kslt(*keyid)) {
		*keyid = tegra_keyslot_alloc();
		if (!(*keyid)) {
			dev_err(se->dev, "failed to allocate key slot\n");
			return -ENOMEM;
		}
	}

	ret = tegra_key_insert(se, key, keylen, *keyid, alg);
	if (ret)
		return ret;

	if (!se->hw->support_kds)
		return 0;

	/*
	 * Move the key to KDS and free the slot if HW supports.
	 * The key will have to be brought back to local KSLT for any task.
	 */

	/* If it is a valid key, invalidate it */
	if (tegra_key_in_kds(orig_id))
		tegra_key_kds_invalidate(se, orig_id);

	kds_id = tegra_kds_get_id();
	if (!kds_id) {
		/* Not a fatal error. Key can still reside in KSLT */
		dev_err(se->dev, "Failed to get KDS slot.The key is in local key slot\n");
		return 0;
	}

	ret = tegra_key_move_to_kds(se, *keyid, kds_id);
	if (ret) {
		/* Not a fatal error. Key can still reside in KSLT */
		dev_err(se->dev, "Failed to move key to KDS. The key is in local key slot\n");
		tegra_kds_free_id(kds_id);
		return 0;
	}

	/* Free the local keyslot. */
	tegra_key_invalidate(se, *keyid, alg);
	*keyid = kds_id;

	return 0;
}

void tegra_key_invalidate_reserved(struct tegra_se *se, u32 keyid, u32 alg)
{
	u8 zkey[AES_MAX_KEY_SIZE] = {0};

	if (!keyid)
		return;

	/* Overwrite the key with 0s */
	tegra_key_insert(se, zkey, AES_MAX_KEY_SIZE, keyid, alg);
}

inline int tegra_key_submit_reserved(struct tegra_se *se, const u8 *key,
				     u32 keylen, u32 alg, u32 *keyid)
{
	return tegra_key_insert(se, key, keylen, *keyid, alg);
}
