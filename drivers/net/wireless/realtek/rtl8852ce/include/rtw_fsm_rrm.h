/******************************************************************************
 *
 * Copyright(c) 2019 - 2023 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#ifndef __RTW_FSM_RRM_H__
#define __RTW_FSM_RRM_H__

#ifdef CONFIG_RTW_FSM_RRM
u8 rm_post_event_hdl(_adapter *padapter, u8 *pbuf);

struct fsm_priv;
struct rrm_obj;

/* Header file for application to invoke rrm service */

struct rrm_priv {
	u8 enable;
	_queue rm_queue;

	u8 rm_en_cap_def[5];
	u8 rm_en_cap_assoc[5];
	u8 meas_token;

	struct fsm_main *fsm;
	/* rm debug */
	void *prm_sel;
};

int rtw_rrm_reg_fsm(struct fsm_priv *fsmpriv);

u8 rm_add_nb_req(_adapter *a, struct sta_info *psta);
void rtw_ap_parse_sta_rm_en_cap(_adapter *a,
	struct sta_info *psta, struct rtw_ieee802_11_elems *elem);
unsigned int rrm_on_action(_adapter *a, union recv_frame *precv_frame);

void rrm_update_cap(u8 *frame_head, _adapter *pa, u32 pktlen, int offset);
void RM_IE_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE);

#endif /* CONFIG_RTW_FSM_RRM */
#endif /* __RTW_FSM_RRM_H__ */

