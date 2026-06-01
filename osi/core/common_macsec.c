// SPDX-License-Identifier: MIT
/* SPDX-FileCopyrightText: Copyright (c) 2021-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifdef MACSEC_SUPPORT
#include <osi_macsec.h>
#include "common.h"
#include "eqos_core.h"
#include "core_local.h"

/**
 * @brief osi_init_macsec_ops - macsec initialize operations
 *
 * @note
 * Algorithm:
 *  - If virtualization is enabled initialize virt ops
 *  - Else
 *    - If macsec base is null return -1
 *    - initialize with macsec ops
 *  - Refer to MACSEC column of <<******, (sequence diagram)>> for API details.
 *  - TraceID: ***********
 *
 * @param[in] osi_core: OSI core private data structure. used param macsec_base
 *
 * @pre MACSEC needs to be out of reset and proper clock configured.
 *
 * @note
 * API Group:
 * - Initialization: No
 * - Run time: Yes
 * - De-initialization: No
 *
 * @retval 0 on success
 * @retval -1 on failure
 */
nve32_t osi_init_macsec_ops(struct osi_core_priv_data *const osi_core)
{
	static struct osi_macsec_core_ops virt_macsec_ops;
	nve32_t ret = 0;
	struct core_local *l_core = (struct core_local *)(void *)osi_core;
	static struct osi_macsec_core_ops macsec_ops = {0};

	if (osi_core == OSI_NULL) {
		ret = -1;
		goto exit;
	}

	if (osi_core->use_virtualization == OSI_ENABLE) {
#ifdef OSI_RM_FTRACE
		ethernet_server_entry_log();
#endif
		l_core->macsec_ops = &virt_macsec_ops;
		ivc_init_macsec_ops(l_core->macsec_ops);
#ifdef OSI_RM_FTRACE
		ethernet_server_exit_log();
#endif
	} else {
		if (osi_core->macsec_base == OSI_NULL) {
			ret = -1;
			goto exit;
		}
		l_core->macsec_ops = &macsec_ops;
		macsec_init_ops(l_core->macsec_ops);
	}
exit:
	return ret;
}

/**
 * @brief osi_macsec_init - Initialize the macsec controller
 *
 * @note
 * Algorithm:
 *  - Return -1 if osi core or ops is null
 *  - Configure MTU, controller configs, interrupts, clear all LUT's and
 *    set BYP LUT entries for MKPDU and BC packets
 *  - Refer to MACSEC column of <<******, (sequence diagram)>> for API details.
 *  - TraceID: ***********
 *
 * @param[in] osi_core: OSI core private data structure.
 * @param[in] mtu: mtu to be programmed
 * @param[in] macsec_vf_mac: Pointer to VF MACID
 *
 * @pre MACSEC needs to be out of reset and proper clock configured.
 *
 * @note
 * API Group:
 * - Initialization: No
 * - Run time: Yes
 * - De-initialization: No
 *
 * @retval 0 on success
 * @retval -1 on failure
 */
nve32_t osi_macsec_init(struct osi_core_priv_data *const osi_core,
			nveu32_t mtu, nveu8_t *const macsec_vf_mac)
{
	nve32_t ret = -1;
	const struct core_local *l_core = (struct core_local *)(void *)osi_core;
	void *mac_addr = OSI_NULL;
	nveu32_t value = 0;

	if ((osi_core != OSI_NULL) && (l_core->macsec_ops != OSI_NULL) &&
	    (l_core->macsec_ops->init != OSI_NULL) &&
	    (macsec_vf_mac != OSI_NULL)) {
		mac_addr = osi_core->base;
#ifdef OSI_RM_FTRACE
		ethernet_server_entry_log();
#endif
		if (osi_core->use_virtualization != OSI_ENABLE) {
			/* Update MAC value as per macsec requirement */
			l_core->ops_p->macsec_config_mac(osi_core, OSI_ENABLE);
			if (osi_core->mac_ver == OSI_EQOS_MAC_5_40) {
				value = osi_readla(osi_core, (nveu8_t *)mac_addr + EQOS_MAC_MCR);
				/* Disable MAC Transmit as per suggestion in bug 4456073 */
				value &= ~EQOS_MCR_TE;
				osi_writela(osi_core, value, (nveu8_t *)mac_addr + EQOS_MAC_MCR);
			}
		}
		ret = l_core->macsec_ops->init(osi_core, mtu, macsec_vf_mac);
		if (osi_core->use_virtualization != OSI_ENABLE) {
			if (osi_core->mac_ver == OSI_EQOS_MAC_5_40) {
				value = osi_readla(osi_core, (nveu8_t *)mac_addr + EQOS_MAC_MCR);
				/* Enable MAC Transmit as per suggestion in bug 4456073 */
				value |= EQOS_MCR_TE;
				osi_writela(osi_core, value, (nveu8_t *)mac_addr + EQOS_MAC_MCR);
			}
		}

#ifdef OSI_RM_FTRACE
		ethernet_server_exit_log();
#endif
	}

	return ret;
}

/**
 * @brief osi_macsec_coe_config - Configure the COE engine in MACSec controller
 *
 * @note
 * Algorithm:
 *  - Return -1 if osi core or ops is null
 *  - Configure the COE engine based on args provided
 *  - Refer to MACSEC column of <<******, (sequence diagram)>> for API details.
 *  - TraceID: ***********
 *
 * @param[in] osi_core: OSI core private data structure
 * @param[in] coe_enable: Flag variable to enable COE
 * @param[in] coe_hdr_offset: COE header offset
 *
 * @pre MACSEC needs to be out of reset and proper clock configured.
 *
 * @note
 * API Group:
 * - Initialization: No
 * - Run time: Yes
 * - De-initialization: No
 *
 * @retval 0 on success
 * @retval -1 on failure
 */
nve32_t osi_macsec_coe_config(struct osi_core_priv_data *const osi_core,
			nveu32_t coe_enable, nveu32_t coe_hdr_offset)
{
	nve32_t ret = -1;
	const struct core_local *l_core = (struct core_local *)(void *)osi_core;

	if ((osi_core != OSI_NULL) && (l_core->macsec_ops != OSI_NULL) &&
	    (l_core->macsec_ops->coe_config != OSI_NULL)) {
		ret = l_core->macsec_ops->coe_config(osi_core, coe_enable, coe_hdr_offset);
	}

	return ret;
}

/**
 * @brief osi_macsec_coe_lc - Configure the COE engine line counter thresholds
 *
 * @note
 * Algorithm:
 *  - Return -1 if osi core or ops is null
 *  - Configure the COE engine based on args provided
 *  - Refer to MACSEC column of <<******, (sequence diagram)>> for API details.
 *  - TraceID: ***********
 *
 * @param[in] osi_core: OSI core private data structure
 * @param[in] ch: Channel number
 * @param[in] lc1: Line counter threshold 1
 * @param[in] lc2: Line counter threshold 2
 *
 * @pre MACSEC needs to be out of reset and proper clock configured.
 *
 * @note
 * API Group:
 * - Initialization: No
 * - Run time: Yes
 * - De-initialization: No
 *
 * @retval 0 on success
 * @retval -1 on failure
 */
nve32_t osi_macsec_coe_lc(struct osi_core_priv_data *const osi_core,
			nveu32_t ch, nveu32_t lc1, nveu32_t lc2)
{
	nve32_t ret = -1;
	const struct core_local *l_core = (struct core_local *)(void *)osi_core;

	if ((osi_core != OSI_NULL) && (l_core->macsec_ops != OSI_NULL) &&
	    (l_core->macsec_ops->coe_lc != OSI_NULL)) {
		ret = l_core->macsec_ops->coe_lc(osi_core, ch, lc1, lc2);
	}

	return ret;
}

/**
 * @brief osi_macsec_deinit - De-Initialize the macsec controller
 *
 * @note
 * Algorithm:
 *  - Return -1 if osi core or ops is null
 *  - Resets macsec global data structured and restores the mac confirguration
 *  - Refer to MACSEC column of <<******, (sequence diagram)>> for API details.
 *  - TraceID: ***********
 *
 * @param[in] osi_core: OSI core private data structure
 *
 * @pre MACSEC needs to be out of reset and proper clock configured.
 *
 * @note
 * API Group:
 * - Initialization: No
 * - Run time: Yes
 * - De-initialization: No
 *
 * @retval 0 on success
 * @retval -1 on failure
 */
nve32_t osi_macsec_deinit(struct osi_core_priv_data *const osi_core)
{
	nve32_t ret = -1;
	const struct core_local *l_core = (struct core_local *)(void *)osi_core;

	if ((osi_core != OSI_NULL) && (l_core->macsec_ops != OSI_NULL) &&
	    (l_core->macsec_ops->deinit != OSI_NULL)) {
#ifdef OSI_RM_FTRACE
		ethernet_server_entry_log();
#endif
		ret = l_core->macsec_ops->deinit(osi_core);
		if (osi_core->use_virtualization != OSI_ENABLE) {
			/* Update MAC value as per macsec requirement */
			l_core->ops_p->macsec_config_mac(osi_core, OSI_DISABLE);
		}
#ifdef OSI_RM_FTRACE
		ethernet_server_exit_log();
#endif
	}
	return ret;
}

/**
 * @brief osi_macsec_isr - macsec irq handler
 *
 * @note
 * Algorithm:
 *  - Return -1 if osi core or ops is null
 *  - handles macsec interrupts
 *  - Refer to MACSEC column of <<******, (sequence diagram)>> for API details.
 *  - TraceID: ***********
 *
 * @param[in] osi_core: OSI core private data structure
 *
 * @pre MACSEC needs to be out of reset and proper clock configured.
 *
 * @note
 * API Group:
 * - Initialization: No
 * - Run time: Yes
 * - De-initialization: No
 */
void osi_macsec_isr(struct osi_core_priv_data *const osi_core)
{
	const struct core_local *l_core = (struct core_local *)(void *)osi_core;

	if ((osi_core != OSI_NULL) && (l_core->macsec_ops != OSI_NULL) &&
	    (l_core->macsec_ops->handle_irq != OSI_NULL)) {
#ifdef OSI_RM_FTRACE
		ethernet_server_entry_log();
#endif
		l_core->macsec_ops->handle_irq(osi_core);
#ifdef OSI_RM_FTRACE
		ethernet_server_exit_log();
#endif
	}
}

/**
 * @brief osi_macsec_config_lut - Read or write to macsec LUTs
 *
 * @note
 * Algorithm:
 *  - Return -1 if osi core or ops is null
 *  - Reads or writes to MACSEC LUTs
 *  - Refer to MACSEC column of <<******, (sequence diagram)>> for API details.
 *  - TraceID: ***********
 *
 * @param[in] osi_core: OSI core private data structure
 * @param[out] lut_config: Pointer to the lut configuration
 *
 * @pre MACSEC needs to be out of reset and proper clock configured.
 *
 * @note
 * API Group:
 * - Initialization: No
 * - Run time: Yes
 * - De-initialization: No
 *
 * @retval 0 on success
 * @retval -1 on failure
 */
nve32_t osi_macsec_config_lut(struct osi_core_priv_data *const osi_core,
			  struct osi_macsec_lut_config *const lut_config)
{
	nve32_t ret = -1;
	const struct core_local *l_core = (struct core_local *)(void *)osi_core;

	if ((osi_core != OSI_NULL) && (l_core->macsec_ops != OSI_NULL) &&
	    (l_core->macsec_ops->lut_config != OSI_NULL) &&
	    (lut_config != OSI_NULL)) {
#ifdef OSI_RM_FTRACE
		ethernet_server_entry_log();
#endif
		ret = l_core->macsec_ops->lut_config(osi_core, lut_config);
#ifdef OSI_RM_FTRACE
		ethernet_server_exit_log();
#endif
	}

	return ret;
}

/**
 * @brief osi_macsec_get_sc_lut_key_index - API to get key index for a given SCI
 *
 * @note
 * Algorithm:
 *  - Return -1 if osi core or ops is null
 *  - gets the key index for the given sci
 *  - Refer to MACSEC column of <<******, (sequence diagram)>> for API details.
 *  - TraceID: ***********
 *
 * @param[in] osi_core: OSI core private data structure
 * @param[in] sci: Pointer to sci that needs to be found
 * @param[out] key_index: Pointer to key_index
 * @param[in] ctlr: macsec controller selected
 *
 * @pre MACSEC needs to be out of reset and proper clock configured.
 *
 * @note
 * API Group:
 * - Initialization: No
 * - Run time: Yes
 * - De-initialization: No
 *
 * @retval 0 on success
 * @retval -1 on failure
 */
nve32_t osi_macsec_get_sc_lut_key_index(struct osi_core_priv_data *const osi_core,
					nveu8_t *sci, nveu32_t *key_index,
					nveu16_t ctlr)
{
	nve32_t ret = -1;
	const struct core_local *l_core = (struct core_local *)(void *)osi_core;

	if ((osi_core != OSI_NULL) && (l_core->macsec_ops != OSI_NULL) &&
	    (l_core->macsec_ops->get_sc_lut_key_index != OSI_NULL) &&
	    (sci != OSI_NULL) && (key_index != OSI_NULL) && (ctlr <= OSI_CTLR_SEL_MAX)) {
#ifdef OSI_RM_FTRACE
		ethernet_server_entry_log();
#endif
		ret = l_core->macsec_ops->get_sc_lut_key_index(osi_core, sci, key_index,
								  ctlr);
#ifdef OSI_RM_FTRACE
		ethernet_server_exit_log();
#endif
	}

	return ret;
}

#ifdef MACSEC_KEY_PROGRAM
/**
 * @brief osi_macsec_config_kt - API to read or update the keys
 *
 * @note
 * Algorithm:
 *  - Return -1 if osi core or ops is null
 *  - Read or write the keys
 *  - Refer to MACSEC column of <<******, (sequence diagram)>> for API details.
 *  - TraceID: ***********
 *
 * @param[in] osi_core: OSI core private data structure
 * @param[in] kt_config: Keys that needs to be programmed
 *
 * @pre MACSEC needs to be out of reset and proper clock configured.
 *
 * @note
 * API Group:
 * - Initialization: No
 * - Run time: Yes
 * - De-initialization: No
 *
 * @retval 0 on success
 * @retval -1 on failure
 */
nve32_t osi_macsec_config_kt(struct osi_core_priv_data *const osi_core,
			 struct osi_macsec_kt_config *const kt_config)
{
	nve32_t ret = -1;
	const struct core_local *l_core = (struct core_local *)(void *)osi_core;

	if ((osi_core != OSI_NULL) && (l_core->macsec_ops != OSI_NULL) &&
	    (l_core->macsec_ops->kt_config != OSI_NULL) &&
	    (kt_config != OSI_NULL)) {
		ret = l_core->macsec_ops->kt_config(osi_core, kt_config);
	}

	return ret;
}
#endif /* MACSEC_KEY_PROGRAM */

/**
 * @brief osi_macsec_cipher_config - API to update the cipher
 *
 * @note
 * Algorithm:
 *  - Return -1 if osi core or ops is null
 *  - Updates cipher to use
 *  - Refer to MACSEC column of <<******, (sequence diagram)>> for API details.
 *  - TraceID: ***********
 *
 * @param[in] osi_core: OSI core private data structure
 * @param[in] cipher: Cipher suit to be used
 *
 * @pre MACSEC needs to be out of reset and proper clock configured.
 *
 * @note
 * API Group:
 * - Initialization: No
 * - Run time: Yes
 * - De-initialization: No
 *
 * @retval 0 on success
 * @retval -1 on failure
 */
nve32_t osi_macsec_cipher_config(struct osi_core_priv_data *const osi_core,
			      nveu32_t cipher)
{
	nve32_t ret = -1;
	const struct core_local *l_core = (struct core_local *)(void *)osi_core;

	if ((osi_core != OSI_NULL) && (l_core->macsec_ops != OSI_NULL) &&
	    (l_core->macsec_ops->cipher_config != OSI_NULL)) {
#ifdef OSI_RM_FTRACE
		ethernet_server_entry_log();
#endif
		ret = l_core->macsec_ops->cipher_config(osi_core, cipher);
#ifdef OSI_RM_FTRACE
		ethernet_server_exit_log();
#endif
	}

	return ret;
}

#ifdef DEBUG_MACSEC
/**
 * @brief osi_macsec_loopback - API to enable/disable macsec loopback
 *
 * @note
 * Algorithm:
 *  - Return -1 if osi core or ops is null
 *  - Enables/disables macsec loopback
 *  - Refer to MACSEC column of <<******, (sequence diagram)>> for API details.
 *  - TraceID: ***********
 *
 * @param[in] osi_core: OSI core private data structure
 * @param[in] enable: parameter to enable or disable
 *
 * @pre MACSEC needs to be out of reset and proper clock configured.
 *
 * @note
 * API Group:
 * - Initialization: No
 * - Run time: Yes
 * - De-initialization: No
 *
 * @retval 0 on success
 * @retval -1 on failure
 */
nve32_t osi_macsec_loopback(struct osi_core_priv_data *const osi_core,
			nveu32_t enable)
{
	nve32_t ret = -1;
	const struct core_local *l_core = (struct core_local *)(void *)osi_core;

	if ((osi_core != OSI_NULL) && (l_core->macsec_ops != OSI_NULL) &&
	    (l_core->macsec_ops->loopback_config != OSI_NULL)) {
		ret = l_core->macsec_ops->loopback_config(osi_core, enable);
	}

	return ret;
}
#endif /* DEBUG_MACSEC */

/**
 * @brief osi_macsec_config - Updates SC or SA in the macsec
 *
 * @note
 * Algorithm:
 *  - Return -1 if passed params are invalid
 *  - Return -1 if osi core or ops is null
 *  - Update/add/delete SC/SA
 *  - Refer to MACSEC column of <<******, (sequence diagram)>> for API details.
 *  - TraceID: ***********
 *
 * @param[in] osi_core: OSI core private data structure
 * @param[in] sc: Pointer to the sc that needs to be added/deleted/updated
 * @param[in] enable: enable or disable
 * @param[in] ctlr: Controller selected
 * @param[out] kt_idx: Pointer to the kt_index passed to OSD
 *
 * @pre MACSEC needs to be out of reset and proper clock configured.
 *
 * @note
 * API Group:
 * - Initialization: No
 * - Run time: Yes
 * - De-initialization: No
 *
 * @retval 0 on success
 * @retval -1 on failure
 */
nve32_t osi_macsec_config(struct osi_core_priv_data *const osi_core,
		      struct osi_macsec_sc_info *const sc,
		      nveu32_t enable, nveu16_t ctlr,
		      nveu16_t *kt_idx)
{
	nve32_t ret = -1;
	const struct core_local *l_core = (struct core_local *)(void *)osi_core;

	if (((enable != OSI_ENABLE) && (enable != OSI_DISABLE)) ||
	    (ctlr > OSI_CTLR_SEL_MAX) || (kt_idx == OSI_NULL)) {
		goto exit;
	}

	if ((osi_core != OSI_NULL) && (l_core->macsec_ops != OSI_NULL) &&
	    (l_core->macsec_ops->config != OSI_NULL) && (sc != OSI_NULL) &&
	    (sc->curr_an < OSI_MAX_NUM_SA)) {
#ifdef OSI_RM_FTRACE
		ethernet_server_entry_log();
#endif
		ret = l_core->macsec_ops->config(osi_core, sc,
						    enable, ctlr, kt_idx);
#ifdef OSI_RM_FTRACE
		ethernet_server_exit_log();
#endif
	}
exit:
	return ret;
}

/**
 * @brief osi_macsec_read_mmc - Updates the mmc counters
 *
 * @note
 * Algorithm:
 *  - Return -1 if osi core or ops is null
 *  - Updates the mcc counters in osi_core structure
 *  - Refer to MACSEC column of <<******, (sequence diagram)>> for API details.
 *  - TraceID: ***********
 *
 * @param[out] osi_core: OSI core private data structure
 *
 * @pre MACSEC needs to be out of reset and proper clock configured.
 *
 * @note
 * API Group:
 * - Initialization: No
 * - Run time: Yes
 * - De-initialization: No
 *
 * @retval 0 on success
 * @retval -1 on failure
 */
nve32_t osi_macsec_read_mmc(struct osi_core_priv_data *const osi_core)
{
	nve32_t ret = -1;
	const struct core_local *l_core = (struct core_local *)(void *)osi_core;

	if ((osi_core != OSI_NULL) && (l_core->macsec_ops != OSI_NULL) &&
	    (l_core->macsec_ops->read_mmc != OSI_NULL)) {
#ifdef OSI_RM_FTRACE
		ethernet_server_entry_log();
#endif
		l_core->macsec_ops->read_mmc(osi_core);
		ret = 0;
#ifdef OSI_RM_FTRACE
		ethernet_server_exit_log();
#endif
	}
	return ret;
}

#ifdef DEBUG_MACSEC
/**
 * @brief osi_macsec_config_dbg_buf - Reads the debug buffer captured
 *
 * @note
 * Algorithm:
 *  - Return -1 if osi core or ops is null
 *  - Reads the dbg buffers captured
 *  - Refer to MACSEC column of <<******, (sequence diagram)>> for API details.
 *  - TraceID: ***********
 *
 * @param[in] osi_core: OSI core private data structure
 * @param[out] dbg_buf_config: dbg buffer data captured
 *
 * @pre MACSEC needs to be out of reset and proper clock configured.
 *
 * @note
 * API Group:
 * - Initialization: No
 * - Run time: Yes
 * - De-initialization: No
 *
 * @retval 0 on success
 * @retval -1 on failure
 */
nve32_t osi_macsec_config_dbg_buf(
		struct osi_core_priv_data *const osi_core,
		struct osi_macsec_dbg_buf_config *const dbg_buf_config)
{
	nve32_t ret = -1;
	const struct core_local *l_core = (struct core_local *)(void *)osi_core;

	if ((osi_core != OSI_NULL) && (l_core->macsec_ops != OSI_NULL) &&
	    (l_core->macsec_ops->dbg_buf_config != OSI_NULL) &&
	    (dbg_buf_config != OSI_NULL) &&
	    (osi_core->macsec != OSI_MACSEC_T26X)) {
		ret = l_core->macsec_ops->dbg_buf_config(osi_core,
							dbg_buf_config);
	}

	return ret;
}

/**
 * @brief osi_macsec_dbg_events_config - Enables debug buffer events
 *
 * @note
 * Algorithm:
 *  - Return -1 if osi core or ops is null
 *  - Enables specific events to capture debug buffers
 *  - Refer to MACSEC column of <<******, (sequence diagram)>> for API details.
 *  - TraceID: ***********
 *
 * @param[in] osi_core: OSI core private data structure
 * @param[in] dbg_buf_config: dbg buffer data captured
 *
 * @pre MACSEC needs to be out of reset and proper clock configured.
 *
 * @note
 * API Group:
 * - Initialization: No
 * - Run time: Yes
 * - De-initialization: No
 *
 * @retval 0 on success
 * @retval -1 on failure
 */
nve32_t osi_macsec_dbg_events_config(
		struct osi_core_priv_data *const osi_core,
		struct osi_macsec_dbg_buf_config *const dbg_buf_config)
{
	nve32_t ret = -1;
	const struct core_local *l_core = (struct core_local *)(void *)osi_core;

	if ((osi_core != OSI_NULL) && (l_core->macsec_ops != OSI_NULL) &&
	    (l_core->macsec_ops->dbg_events_config != OSI_NULL) &&
	    (dbg_buf_config != OSI_NULL) &&
	    (osi_core->macsec != OSI_MACSEC_T26X)) {
		ret = l_core->macsec_ops->dbg_events_config(osi_core,
							dbg_buf_config);
	}

	return ret;
}

#endif /* DEBUG_MACSEC */
#endif /* MACSEC_SUPPORT */
