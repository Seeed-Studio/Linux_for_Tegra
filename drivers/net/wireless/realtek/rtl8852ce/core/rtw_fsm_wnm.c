/******************************************************************************
 *
 * Copyright(c) 2023 - 2024 Realtek Corporation.
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
#include <drv_types.h>

#ifdef CONFIG_RTW_FSM_BTM

#define BTM_CONNECT_WAIT 2000
#define BTM_DISASSOC_WAIT 1000
#ifdef CONFIG_IEEE80211_BAND_6GHZ
#define BTM_SCAN_TIMEOUT 15000
#else
#define BTM_SCAN_TIMEOUT 10000
#endif
#define BTM_SCAN_DENY_WAIT 500
#define BTM_FT_ACTION_TIMEOUT 1000
#define BTM_SCAN_DWELL_TIME 30
/*
* State machine
*/
enum BTM_STATE_ST {
	BTM_ST_START,
	BTM_ST_SCAN,
	BTM_ST_OTD,
	BTM_ST_ROAM,
	BTM_ST_END,
	BTM_ST_MAX
};

enum BTM_EV_ID {
	BTM_EV_scan_deny_expire,
	BTM_EV_scan_timeout,
	BTM_EV_scan_found_candidate,
	BTM_EV_scan_opch_reported,
	BTM_EV_connect_timeout,
	BTM_EV_disassoc_timeout,
	BTM_EV_ft_action_timeout,
	BTM_EV_ft_action_resp,
	BTM_EV_max
};

static int btm_start_st_hdl(void *obj, u16 event, void *param);
static int btm_scan_st_hdl(void *obj, u16 event, void *param);
static int btm_otd_st_hdl(void *obj, u16 event, void *param);
static int btm_roam_st_hdl(void *obj, u16 event, void *param);
static int btm_end_st_hdl(void *obj, u16 event, void *param);

/* STATE table */
static struct fsm_state_ent btm_state_tbl[] = {
	ST_ENT(BTM_ST_START, btm_start_st_hdl),
	ST_ENT(BTM_ST_SCAN, btm_scan_st_hdl),
	ST_ENT(BTM_ST_OTD, btm_otd_st_hdl),
	ST_ENT(BTM_ST_ROAM, btm_roam_st_hdl),
	ST_ENT(BTM_ST_END, btm_end_st_hdl),
};

/* EVENT table */
static struct fsm_event_ent btm_event_tbl[] = {
	EV_DBG(BTM_EV_scan_deny_expire),
	EV_DBG(BTM_EV_scan_timeout),
	EV_DBG(BTM_EV_scan_found_candidate),
	EV_DBG(BTM_EV_scan_opch_reported),
	EV_DBG(BTM_EV_connect_timeout),
	EV_DBG(BTM_EV_disassoc_timeout),
	EV_DBG(BTM_EV_ft_action_timeout),
	EV_DBG(BTM_EV_ft_action_resp),
	EV_DBG(BTM_EV_max) /* EV_MAX for fsm safety checking */
};

struct btm_obj {

	u32 cid;
	u8 resp_reason;
	u8 roam_reason;
	u8 ft_req_retry_cnt;
	u8 op_ch_reported;
	struct roam_nb_info pnb;
	struct wlan_network *candidate;
	int finish;

	/* obj default */
	struct fsm_obj *obj;
	struct fsm_main *fsm;
	_list list;
};

/*
 * btm sub function
 */
#if 1
/*
 * Search the @param ch in given @param ch_list
 * @ch_list: the given channel list
 * @ch: the given channel number
 *
 * return the index of channel_num in channel_set, -1 if not found
 */
int rtw_chlist_search_ch(struct rtw_ieee80211_channel *ch_list, int len, RT_CHANNEL_INFO *chs)
{
	int i;

	if (chs->ChannelNum == 0)
		return -1;

	for (i = 0; i < len && ch_list[i].hw_value != 0; i++) {
		if (chs->ChannelNum == ch_list[i].hw_value && chs->band == ch_list[i].band)
			return i;
	}

	return -1;
}

/*
 * Sort scan channel as following order:
 * NB list, 5G active, 5G NOIR, 6G active, 6G NOIR, 2G active, 2G NOIR
 */
static int btm_scan_ch_decision(_adapter *padapter, struct rtw_ieee80211_channel *out,
		u32 out_num, struct rtw_ieee80211_channel *in, u32 in_num)
{
	int i, j, k, l, m, r, amount_ch, chs_len, sz = sizeof(struct rtw_ieee80211_channel);
	int set_idx;
	u8 chan;
	struct rtw_chset *chset = adapter_to_chset(padapter);
	RT_CHANNEL_INFO *chs;
	struct registry_priv *regsty = dvobj_to_regsty(adapter_to_dvobj(padapter));
	struct rtw_ieee80211_channel *out_roam, *out2, *out2_noir, *out5, *out5_noir;
#ifdef CONFIG_IEEE80211_BAND_6GHZ
	struct rtw_ieee80211_channel *out6, *out6_noir;
	int p, q;
#endif
	j = k = l = m = r = amount_ch = 0;
	out_roam = rtw_zmalloc(sz * RTW_MAX_NB_RPT_NUM);
	out2 = rtw_zmalloc(sz * MAX_CHANNEL_NUM_2G);
	out2_noir = rtw_zmalloc(sz * MAX_CHANNEL_NUM_2G);
	out5 = rtw_zmalloc(sz * MAX_CHANNEL_NUM_5G);
	out5_noir = rtw_zmalloc(sz * MAX_CHANNEL_NUM_5G);
#ifdef CONFIG_IEEE80211_BAND_6GHZ
	p = q = 0;
	out6 = rtw_zmalloc(sz * MAX_CHANNEL_NUM_6G);
	out6_noir = rtw_zmalloc(sz * MAX_CHANNEL_NUM_6G);
#endif
	if (!out_roam || !out2 || !out2_noir || !out5 || !out5_noir
#ifdef CONFIG_IEEE80211_BAND_6GHZ
		|| !out6 || !out6_noir
#endif
	) {
		goto exit;
	}

	/* scan channel order NB list, 5G, 5G_NOIR, 6G, 6G_NOIR, 2G, 2G_NOIR */
	/* acquire channels from in */
	for (i = 0; i < in_num; i++) {

		if (0)
			RTW_INFO(FUNC_ADPT_FMT" "CHAN_FMT"\n", FUNC_ADPT_ARG(padapter), CHAN_ARG(&in[i]));

		if (!in[i].hw_value || (in[i].flags & RTW_IEEE80211_CHAN_DISABLED))
			continue;
		if (rtw_mlme_band_check(padapter, in[i].hw_value) == _FALSE)
			continue;

		set_idx = rtw_chset_search_bch(chset, in[i].band, in[i].hw_value);

		if (set_idx < 0)
			continue;

		if (chset->chs[set_idx].band == BAND_ON_5G) {

			/* j,k */
			if (chset->chs[set_idx].flags & (RTW_CHF_NO_IR | RTW_CHF_DFS)) {
				/* 5G DFS and NO_IR */
				_rtw_memcpy(&out5_noir[k], &in[i], sz);
				out5_noir[k].flags |= RTW_IEEE80211_CHAN_PASSIVE_SCAN;
				k++;
			} else {
				/* 5G Active channel */
				_rtw_memcpy(&out5[j], &in[i], sz);
				j++;
			}

		}
#ifdef CONFIG_IEEE80211_BAND_6GHZ
		else if (chset->chs[set_idx].band == BAND_ON_6G) {
			/* p,q */
			if (chset->chs[set_idx].flags & (RTW_CHF_NO_IR | RTW_CHF_DFS)) {
				/* 5G DFS and NO_IR */
				_rtw_memcpy(&out5_noir[q], &in[i], sz);
				out5_noir[q].flags |= RTW_IEEE80211_CHAN_PASSIVE_SCAN;
				q++;
			} else {
				/* 5G Active channel */
				_rtw_memcpy(&out5[p], &in[i], sz);
				p++;
			}

		}
#endif
		else { /* 2G */

			/* l, m */
			if (chset->chs[set_idx].flags & (RTW_CHF_NO_IR | RTW_CHF_DFS)) {
				/* 2G  NO_IR */
				_rtw_memcpy(&out2_noir[m], &in[i], sz);
				out2_noir[m].flags |= RTW_IEEE80211_CHAN_PASSIVE_SCAN;
				m++;
			} else {
				/* 2G Active channel */
				_rtw_memcpy(&out2[l], &in[i], sz);
				l++;
			}
		}
	}

	amount_ch = 0;
	if (j && (RTW_MAX_NB_RPT_NUM >= (amount_ch + j))) {
		_rtw_memcpy(&out_roam[amount_ch], out5, sz);
		amount_ch += j;
	}

	if (k && (RTW_MAX_NB_RPT_NUM >= (amount_ch + k))) {
		_rtw_memcpy(&out_roam[amount_ch], out5_noir, sz);
		amount_ch += k;
	}

#ifdef CONFIG_IEEE80211_BAND_6GHZ
	if (p && (RTW_MAX_NB_RPT_NUM >= (amount_ch + p))) {
		_rtw_memcpy(&out_roam[amount_ch], out6, sz);
		amount_ch += p;
	}

	if (q && (RTW_MAX_NB_RPT_NUM >= (amount_ch + q))) {
		_rtw_memcpy(&out_roam[amount_ch], out6_noir, sz);
		amount_ch += q;
	}
#endif
	if (l && (RTW_MAX_NB_RPT_NUM >= (amount_ch + l))) {
		_rtw_memcpy(&out_roam[amount_ch], out2, sz);
		amount_ch += l;
	}

	if (m && (RTW_MAX_NB_RPT_NUM >= (amount_ch + m))) {
		_rtw_memcpy(&out_roam[amount_ch], out2_noir, sz);
		amount_ch += m;
	}
	r = amount_ch;

	/* reset */
	j = k = l = m = amount_ch = 0;
	_rtw_memset(out2, 0, sz * MAX_CHANNEL_NUM_2G);
	_rtw_memset(out2_noir, 0, sz * MAX_CHANNEL_NUM_2G);
	_rtw_memset(out5, 0, sz * MAX_CHANNEL_NUM_5G);
	_rtw_memset(out5_noir, 0, sz * MAX_CHANNEL_NUM_5G);
#ifdef CONFIG_IEEE80211_BAND_6GHZ
	p = q = 0;
	_rtw_memset(out6, 0, sz * MAX_CHANNEL_NUM_6G);
	_rtw_memset(out6_noir, 0, sz * MAX_CHANNEL_NUM_6G);
#endif

	/* if out is empty, use channel_set as default */
	if (amount_ch == 0) {
		/* 5G */
		chs = chset->chs_of_band[BAND_ON_5G];
		chs_len = chset->chs_len_of_band[BAND_ON_5G];

		for (i = 0; i < chs_len; i++) {
			chan = chs[i].ChannelNum;
			if (rtw_mlme_band_check(padapter, chan) == _TRUE) {
				if (rtw_mlme_ignore_chan(padapter, chan) == _TRUE)
					continue;

				/* skip NB list ch */
				if (rtw_chlist_search_ch(out_roam, r, &chs[i]) != -1)
					continue;
				if (0)
					RTW_INFO(FUNC_ADPT_FMT" ch:%u\n", FUNC_ADPT_ARG(padapter), chan);

				if (chs[i].flags & (RTW_CHF_NO_IR | RTW_CHF_DFS)) {
					/* 5G NOIR */
					out5_noir[k].hw_value = chan;
					out5_noir[k].band = BAND_ON_5G;
					out5_noir[k].flags |= RTW_IEEE80211_CHAN_PASSIVE_SCAN;
					k++;
				} else {
					/* 5G active */
					out5[j].hw_value = chan;
					out5[j].band = BAND_ON_5G;
					j++;
				}
			}
		}

#ifdef CONFIG_IEEE80211_BAND_6GHZ
		/* 6G */
		chs = chset->chs_of_band[BAND_ON_6G];
		chs_len = chset->chs_len_of_band[BAND_ON_6G];

		for (i = 0; i < chs_len; i++) {
			chan = chs[i].ChannelNum;
			if (rtw_mlme_band_check(padapter, chan) == _TRUE) {
				if (rtw_mlme_ignore_chan(padapter, chan) == _TRUE)
					continue;

				/* skip NB list ch */
				if (rtw_chlist_search_ch(out_roam, r, &chs[i]) != -1)
					continue;

				/* only accept PCS(Preferred Scanning Channels) */
				if (chan > 229) /* latest PCS channel */
					break;

				 /* PSC = 16*n+5 */
				if ((chan-5)%16)
					continue;

				if (0)
					RTW_INFO(FUNC_ADPT_FMT" ch:%u\n", FUNC_ADPT_ARG(padapter), chan);

				if (chs[i].flags & (RTW_CHF_NO_IR | RTW_CHF_DFS)) {
					/* 6G NOIR */
					out6_noir[q].hw_value = chan;
					out6_noir[q].band = BAND_ON_6G;
					out6_noir[q].flags |= RTW_IEEE80211_CHAN_PASSIVE_SCAN;
					q++;
				} else {
					/* 6G active */
					out6[p].hw_value = chan;
					out6[p].band = BAND_ON_6G;
					p++;
				}
			}
		}
#endif
		/* 2G */
		chs = chset->chs_of_band[BAND_ON_24G];
		chs_len = chset->chs_len_of_band[BAND_ON_24G];
		for (i = 0; i < chs_len; i++) {

			chan = chs[i].ChannelNum;

			if (rtw_mlme_band_check(padapter, chan) == _TRUE) {
				if (rtw_mlme_ignore_chan(padapter, chan) == _TRUE)
					continue;

				/* skip NB list ch */
				if (rtw_chlist_search_ch(out_roam, r, &chs[i]) != -1)
					continue;

				if (0)
					RTW_INFO(FUNC_ADPT_FMT" ch:%u\n", FUNC_ADPT_ARG(padapter), chan);

				if (chs[i].flags & (RTW_CHF_NO_IR | RTW_CHF_DFS)) {
					/* 2G NOIR */
					out2_noir[m].hw_value = chan;
					out2_noir[m].band = BAND_ON_24G;
					out2_noir[m].flags |= RTW_IEEE80211_CHAN_PASSIVE_SCAN;
					m++;

				} else {
					/* 2G activate */
					out2[l].hw_value = chan;
					out2[l].band = BAND_ON_24G;
					l++;
				}
			}
		}
	}
	amount_ch = 0;
	if (r && (out_num >= (amount_ch + r))) {
		_rtw_memcpy(&out[amount_ch], out_roam, sz * r);
		amount_ch += r;
	}

	if (j && (out_num >= (amount_ch + j))) {
		_rtw_memcpy(&out[amount_ch], out5, sz * j);
		amount_ch += j;
	}

	if (k && (out_num >= (amount_ch + k))) {
		_rtw_memcpy(&out[amount_ch], out5_noir, sz * k);
		amount_ch += k;
	}

#ifdef CONFIG_IEEE80211_BAND_6GHZ
	if (p && (out_num >= (amount_ch + p))) {
		_rtw_memcpy(&out[amount_ch], out6, sz * p);
		amount_ch += p;
	}

	if (q && (out_num >= (amount_ch + q))) {
		_rtw_memcpy(&out[amount_ch], out6_noir, sz * q);
		amount_ch += q;
	}
#endif
	if (l && (out_num >= (amount_ch + l))) {
		_rtw_memcpy(&out[amount_ch], out2, sz * l);
		amount_ch += l;
	}

	if (m && (out_num >= (amount_ch + m))) {
		_rtw_memcpy(&out[amount_ch], out2_noir, sz * m);
		amount_ch += m;
	}
exit:
	if (out_roam) rtw_mfree(out_roam, sz * RTW_MAX_NB_RPT_NUM);
	if (out2) rtw_mfree(out2, sz * MAX_CHANNEL_NUM_2G);
	if (out2_noir) rtw_mfree(out2_noir, sz * MAX_CHANNEL_NUM_2G);
	if (out5) rtw_mfree(out5, sz * MAX_CHANNEL_NUM_5G);
	if (out5_noir) rtw_mfree(out5_noir, sz * MAX_CHANNEL_NUM_5G);
#ifdef CONFIG_IEEE80211_BAND_6GHZ
	if (out6) rtw_mfree(out6, sz * MAX_CHANNEL_NUM_6G);
	if (out6_noir) rtw_mfree(out6_noir, sz * MAX_CHANNEL_NUM_6G);
#endif
	return amount_ch;
}
#endif

static void btm_nb_scan_list_set(struct btm_obj *pbtm, struct sitesurvey_parm *pparm)
{
	u32 i;
	struct roam_nb_info *pnb = &pbtm->pnb;
	_adapter *a = obj2adp(pbtm);
	struct mlme_priv *pmlmepriv = &a->mlmepriv;

	if (pmlmepriv->cur_network_scanned) {
		_rtw_memcpy(&pparm->ssid[0], &pmlmepriv->cur_network_scanned->network.Ssid,
			sizeof(struct _NDIS_802_11_SSID));
		pparm->ssid_num = 1;
	}

	for (i = 0; i < pnb->nb_rpt_ch_list_num; i++) {
		_rtw_memcpy(pparm->nb[i].bssid, pnb->nb_rpt[i].bssid, ETH_ALEN);
		pparm->nb[i].ch = pnb->nb_rpt[i].ch_num;
		pparm->nb_num++;
	}

	pparm->scan_mode = RTW_PHL_SCAN_ACTIVE;
	pparm->duration = BTM_SCAN_DWELL_TIME;

	pparm->ch_num = (pnb->nb_rpt_ch_list_num > RTW_MAX_NB_RPT_NUM)?
		(RTW_MAX_NB_RPT_NUM):(pnb->nb_rpt_ch_list_num);
#if 0
	for (i=0; i<pparm->ch_num; i++) {
		pparm->ch[i].hw_value = pnb->nb_rpt_ch_list[i].hw_value;
		pparm->ch[i].band = pnb->nb_rpt_ch_list[i].band;
	}
#endif

	i = btm_scan_ch_decision(a, pparm->ch, RTW_CHANNEL_SCAN_AMOUNT,
		pnb->nb_rpt_ch_list, pnb->nb_rpt_ch_list_num);
	pparm->ch_num = i;
	pnb->nb_rpt_valid = _FALSE;
}

static int btm_roam_scan(struct btm_obj *pbtm)
{
	_adapter *a = obj2adp(pbtm);
	struct sitesurvey_parm *parm;
	u8 ret = _FAIL;

	parm = (struct sitesurvey_parm*)rtw_zmalloc(sizeof(*parm));

	if (!parm)
		return ret;

	rtw_init_sitesurvey_parm(a, parm);
	btm_nb_scan_list_set(pbtm, parm);
	parm->scan_type = RTW_SCAN_ROAM;
	parm->psta = obj2sta(pbtm);

	ret = rtw_sitesurvey_cmd(a, parm);
	rtw_mfree(parm, sizeof(*parm));

	return ret;
}

static int btm_del_obj(struct btm_obj *pbtm)
{
	struct btm_priv *btmpriv = obj2priv(pbtm);
	struct sta_info *psta = obj2sta(pbtm);

	psta->btm = NULL;
	btmpriv->btm = NULL;

	rtw_fsm_deactivate_obj(pbtm);

	return _SUCCESS;
}

/*
 * BTM state handler
 */
static int btm_start_st_hdl(void *obj, u16 event, void *param)
{
	struct btm_obj *pbtm = (struct btm_obj *)obj;
	_adapter *a = obj2adp(pbtm);
	struct btm_priv *btmpriv = (struct btm_priv *)obj2priv(pbtm);
	struct roam_nb_info *pnb = &pbtm->pnb;
	u8 ssc_chk;
	int i;

	switch (event) {
	case FSM_EV_STATE_IN:
		pbtm->resp_reason = 1; /* unspecified reject reason */
		for (i = 0; i < pnb->last_nb_rpt_entries; i++)
			FSM_INFO(pbtm, "roam list "MAC_FMT"\n",
				MAC_ARG(pnb->nb_rpt[i].bssid));
		fallthrough;
	case BTM_EV_scan_deny_expire:
		ssc_chk = rtw_sitesurvey_condition_check(a, _FALSE);
		if (ssc_chk == SS_DENY_BUSY_TRAFFIC) {
			FSM_INFO(pbtm, "need to roam, don't care BusyTraffic\n");
		} else if (ssc_chk != SS_ALLOW) {
			rtw_fsm_set_alarm(pbtm, BTM_SCAN_DENY_WAIT, BTM_EV_scan_deny_expire);
			return _SUCCESS;
		}
		/* Able to scan */
		rtw_fsm_st_goto(pbtm, BTM_ST_SCAN);
		break;
	case FSM_EV_ABORT:
	case FSM_EV_DISCONNECTED:
		if (pbtm->roam_reason & RTW_ROAM_BTM)
			rtw_wnm_issue_action(a, &pbtm->pnb, RTW_WLAN_ACTION_WNM_BTM_RSP,
				pbtm->resp_reason, pbtm->pnb.btm_cache.dialog_token);
		rtw_fsm_st_goto(pbtm, BTM_ST_END);
		break;
	case FSM_EV_STATE_OUT:
		rtw_fsm_cancel_alarm(pbtm);
		break;
	default:
		break;
	}

	return _SUCCESS;
}

static int btm_scan_st_hdl(void *obj, u16 event, void *param)
{
	struct btm_obj *pbtm = (struct btm_obj *)obj;
	_adapter *a = obj2adp(pbtm);
	struct mlme_priv *pmlmepriv = &a->mlmepriv;
	struct roam_nb_info *pnb = &pbtm->pnb;
	struct wlan_network *pnetwork = NULL;
	struct wlan_network *cnetwork = pmlmepriv->cur_network_scanned;
	struct roam_nb_info *mlmepriv_pnb = &(pmlmepriv->nb_info);

	switch (event) {
	case FSM_EV_STATE_IN:
		if (!btm_roam_scan(pbtm)) {
			rtw_fsm_st_goto(pbtm, BTM_ST_END);
			return _SUCCESS;
		}
		pbtm->op_ch_reported = 0;
		/* Set scan timeout */
		rtw_fsm_set_alarm(pbtm, BTM_SCAN_TIMEOUT, BTM_EV_scan_timeout);
		break;
	case BTM_EV_scan_opch_reported:
		pbtm->op_ch_reported = 1;
		break;
	case BTM_EV_scan_found_candidate:
		pnetwork = (struct wlan_network *)param;

		FSM_DBG(pbtm, "Recv %s ("MAC_FMT")\n",pnetwork->network.Ssid.Ssid,
			MAC_ARG(pnetwork->network.MacAddress));

		if ((pnetwork->network.Configuration.DSConfig ==\
			cnetwork->network.Configuration.DSConfig) &&
			(pnetwork->network.Configuration.Band ==\
			 cnetwork->network.Configuration.Band)) {

			if (pbtm->op_ch_reported) /* already reported */
				return _SUCCESS;
			rtw_fsm_set_alarm_ext(pbtm, 100, BTM_EV_scan_opch_reported, 1, NULL);
		}

		if (pbtm->roam_reason & RTW_ROAM_ACTIVE)
			rtw_check_roaming_candidate(pmlmepriv, &pbtm->candidate, pnetwork);
		else if (pbtm->roam_reason & RTW_ROAM_BTM)
			pbtm->candidate = rtw_wnm_btm_candidate_check(a, &pbtm->pnb, pnetwork);
		else if (pbtm->roam_reason & RTW_ROAM_ON_RESUME)
			rtw_check_join_candidate(pmlmepriv, &pbtm->candidate, pnetwork);
		else
			return _SUCCESS;

		if (!pbtm->candidate)
			return _SUCCESS;

		a->mlmepriv.roam_network = pbtm->candidate;
		rtw_scan_abort_no_wait(a);
		/* wait scan done */
		break;
	case FSM_EV_SCAN_DONE:
		rtw_fsm_cancel_alarm(pbtm);

		if (pbtm->roam_reason & RTW_ROAM_BTM) {

			pbtm->resp_reason = rtw_wmn_btm_rsp_reason_decision(a, pnb);
			if (pbtm->resp_reason > 0) {
				pbtm->candidate = NULL;
				rtw_fsm_st_goto(pbtm, BTM_ST_END);
				return _SUCCESS;
			}
			if (!pbtm->candidate) {
				FSM_INFO(pbtm, "Pick a candidate from scan queue\n");
#ifdef DBG_WNM_UNIT_TEST
				pbtm->candidate = rtw_wnm_btm_candidate_select(a, pnb);
#else
				if (!rtw_mbo_wifi_logo_test(a))
					pbtm->candidate = rtw_select_roaming_candidate(pmlmepriv);
				else
					pbtm->candidate = rtw_wnm_btm_candidate_select(a, pnb);
#endif
			}
			if (pbtm->candidate) {
				_rtw_memcpy(pnb->roam_target_addr,
					(u8 *)&pbtm->candidate->network.MacAddress,
					ETH_ALEN);

				/* for ReAssocreq check in rtw_wnm_btm_reassoc_req() */
				/* TODO Do NOT use global variable */
				_rtw_memcpy(mlmepriv_pnb->roam_target_addr,
					(u8 *)&pbtm->candidate->network.MacAddress,
					ETH_ALEN);
			}

		} else if (pbtm->roam_reason & RTW_ROAM_ON_RESUME) {
			if (rtw_select_and_join_from_scanned_queue(pmlmepriv))
				pbtm->finish = _SUCCESS;

			rtw_fsm_st_goto(pbtm, BTM_ST_END);
			return _SUCCESS;
		}

		/* RTW_ROAM_ACTIVE */
		if (!pbtm->candidate) {
			if (pbtm->resp_reason == 0)
				pbtm->resp_reason = 7;

			rtw_fsm_st_goto(pbtm, BTM_ST_END);
			return _SUCCESS;
		}
		a->mlmepriv.roam_network = pbtm->candidate;

		if (rtw_ft_otd_roam(a))
			rtw_fsm_st_goto(pbtm, BTM_ST_OTD); /* Over The DS */
		else
			rtw_fsm_st_goto(pbtm, BTM_ST_ROAM);
		break;
	case BTM_EV_scan_timeout:
	case FSM_EV_ABORT:
	case FSM_EV_DISCONNECTED:
		rtw_scan_abort_no_wait(a);
		rtw_fsm_st_goto(pbtm, BTM_ST_END);
		break;
	case FSM_EV_STATE_OUT:
		rtw_fsm_cancel_alarm(pbtm);
		rtw_fsm_cancel_alarm_ext(pbtm, 1);
		if (pbtm->roam_reason & RTW_ROAM_BTM)
			rtw_wnm_issue_action(a, pnb, RTW_WLAN_ACTION_WNM_BTM_RSP,
				pbtm->resp_reason, pnb->btm_cache.dialog_token);
		break;
	default:
		break;
	}

	return _SUCCESS;
}

static int btm_otd_st_hdl(void *obj, u16 event, void *param)
{
	struct btm_obj *pbtm = (struct btm_obj *)obj;
	_adapter *a = obj2adp(pbtm);
	struct mlme_priv *pmlmepriv = &a->mlmepriv;
	struct ft_roam_info *pft_roam = &(pmlmepriv->ft_roam);

	switch (event) {
	case FSM_EV_STATE_IN:
		FSM_INFO(pbtm, "FT OTD roam\n");
		fallthrough;
	case BTM_EV_ft_action_timeout:
		if (pbtm->ft_req_retry_cnt++ >= RTW_FT_ACTION_REQ_LMT) {
			FSM_WARN(pbtm, "RX FT action timeout, try OTA roam\n");
			rtw_ft_clr_flags(a, RTW_FT_PEER_OTD_EN);
			rtw_fsm_st_goto(pbtm, BTM_ST_ROAM);
			return _SUCCESS;
		}
		/* send ft action frame */
		rtw_ft_issue_action_req(a, pbtm->candidate->network.MacAddress);
		rtw_fsm_set_alarm(pbtm, BTM_FT_ACTION_TIMEOUT, BTM_EV_ft_action_timeout);
		break;
	case BTM_EV_ft_action_resp:
		rtw_fsm_cancel_alarm(pbtm);
		rtw_fsm_st_goto(pbtm, BTM_ST_ROAM);
		break;
	case FSM_EV_STATE_OUT:
		rtw_fsm_cancel_alarm(pbtm);
		break;
	case FSM_EV_DISCONNECTED:
	case FSM_EV_ABORT:
		rtw_fsm_st_goto(pbtm, BTM_ST_END);
		break;
	default:
		break;
	}
	return _SUCCESS;
}

static int btm_roam_st_hdl(void *obj, u16 event, void *param)
{
	struct btm_obj *pbtm = (struct btm_obj *)obj;
	_adapter *a = obj2adp(pbtm);
	struct mlme_priv *pmlmepriv = &a->mlmepriv;
	struct mlme_ext_info *pmlmeinfo = &a->mlmeextpriv.mlmext_info;

	switch (event) {
	case FSM_EV_STATE_IN:
		RTW_INFO("roaming from %s("MAC_FMT"), length:%d\n",
			pmlmepriv->dev_cur_network.network.Ssid.Ssid,
			MAC_ARG(pmlmepriv->dev_cur_network.network.MacAddress),
			pmlmepriv->dev_cur_network.network.Ssid.SsidLength);

		if (pbtm->pnb.btm_cache.req_mode & DISASSOC_IMMINENT) {
			/* indicates that the STA is to be disassociated
			 * from the current AP */
			rtw_fsm_set_alarm(pbtm, BTM_DISASSOC_WAIT, BTM_EV_disassoc_timeout);
			return _SUCCESS;
		}
		fallthrough;
	case BTM_EV_disassoc_timeout:
		if (rtw_ft_otd_roam(a)) {
			pmlmeinfo->disconnect_code = DISCONNECTION_BY_DRIVER_DUE_TO_FT;
		} else {
			rtw_set_to_roam(a, a->registrypriv.max_roaming_times);
			pmlmeinfo->disconnect_code = DISCONNECTION_BY_DRIVER_DUE_TO_ROAMING;
		}

		receive_disconnect(a, pmlmepriv->dev_cur_network.network.MacAddress
			, WLAN_REASON_ACTIVE_ROAM, _FALSE);
		break;
	case FSM_EV_DISCONNECTED:
		FSM_INFO(pbtm, "roaming to %s(" MAC_FMT ")\n",
			pbtm->candidate->network.Ssid.Ssid,
			MAC_ARG(pbtm->candidate->network.MacAddress));

		_rtw_memcpy(&pmlmepriv->assoc_ssid,
			&pmlmepriv->dev_cur_network.network.Ssid, sizeof(NDIS_802_11_SSID));
		pmlmepriv->assoc_ch = 0;
		pmlmepriv->assoc_band = BAND_MAX;
		pmlmepriv->assoc_by_bssid = _FALSE;

		/* Start to connect */
		if (!rtw_do_join(a)) {
			rtw_fsm_st_goto(pbtm, BTM_ST_END);
		}
		rtw_fsm_set_alarm(pbtm, BTM_CONNECT_WAIT, BTM_EV_connect_timeout);
		break;
	case FSM_EV_CONNECTED:
		pbtm->finish = _SUCCESS;
		rtw_set_to_roam(a, 0);
		rtw_fsm_st_goto(pbtm, BTM_ST_END);
		break;
	case FSM_EV_CONNECT_FAIL:
		/* don't trigger disconnect. Connect module will do it! */
		pbtm->finish = _SUCCESS;
		fallthrough;
	case FSM_EV_ABORT:
	case BTM_EV_connect_timeout:
		rtw_fsm_st_goto(pbtm, BTM_ST_END);
		break;
	case FSM_EV_STATE_OUT:
		rtw_fsm_cancel_alarm(pbtm);
		break;
	default:
		break;
	}

	return _SUCCESS;
}

static int btm_end_st_hdl(void *obj, u16 event, void *param)
{
	struct btm_obj *pbtm = (struct btm_obj *)obj;
	_adapter *a = obj2adp(pbtm);

	switch (event) {
	case FSM_EV_STATE_IN:

		if (pbtm->finish == _FAIL) {
			rtw_set_to_roam(a, 0);
			rtw_ft_reset_status(a);
			if (pbtm->candidate) /* driver disconnected */
				rtw_indicate_disconnect(a, 0, _FALSE);
		}
		btm_del_obj(pbtm);
		break;
	case FSM_EV_ABORT:
		break;
	case FSM_EV_STATE_OUT:
		break;
	default:
		break;
	}

	return _SUCCESS;
}

static void btm_dump_obj_cb(void *obj, char *p, int *sz)
{
	/* nothing to do for now */
}

static void btm_dump_fsm_cb(void *fsm, char *p, int *sz)
{
	/* nothing to do for now */
}

static int btm_init_priv_cb(void *priv)
{
	struct btm_priv *pbtmpriv = (struct btm_priv *)priv;

	return _SUCCESS;
}

static int btm_deinit_priv_cb(void *priv)
{
	struct btm_priv *pbtmpriv = (struct btm_priv *)priv;

	return _SUCCESS;
}

static void btm_debug(void *obj, char input[][MAX_ARGV], u32 input_num,
	char *output, u32 *out_len)
{
	return;
}

/* For EXTERNAL application to create a btm object
 * return
 * btm_obj: ptr to new btm object
 */
int rtw_btm_new_obj(_adapter *a, struct sta_info *psta,
	struct roam_nb_info *pnb, u8 roam_reason)
{
	struct btm_priv *btmpriv = &a->fsmpriv.btmpriv;
	struct fsm_main *fsm = btmpriv->fsm;
	struct fsm_obj *obj;
	struct btm_obj *pbtm;
	struct mlme_priv *pmlmepriv = &a->mlmepriv;
	u16 cid = 0;

	if (btmpriv->btm) {
		FSM_WARN_(fsm, "BTM request is on going ...\n");
		return _FAIL;
	}

	if (!psta)
		psta = rtw_get_stainfo(&a->stapriv, get_bssid(pmlmepriv));

	if (!psta)
		psta = rtw_get_stainfo(&a->stapriv, a->mac_addr);

	if (!psta) {
		FSM_WARN_(fsm, "psta NOT found\n");
		return _FAIL;
	}

	if (roam_reason == RTW_ROAM_BTM)
		cid = pnb->btm_cache.dialog_token;

	obj = rtw_fsm_new_obj(fsm, psta, cid, (void **)&pbtm, sizeof(*pbtm));

	if (pbtm == NULL) {
		FSM_ERR_(fsm, "malloc obj fail\n");
		return _FAIL;
	}
	pbtm->fsm = fsm;
	pbtm->obj = obj;
	pbtm->cid = cid;
	pbtm->roam_reason = roam_reason;
	pbtm->finish = _FAIL;

	_rtw_memcpy(&pbtm->pnb, pnb, sizeof(*pnb));

	/* activate it immediately */
	rtw_fsm_activate_obj(pbtm);
	pmlmepriv->need_to_roam = _TRUE;
	btmpriv->btm = pbtm;
	psta->btm = pbtm;

	return _SUCCESS;
}

void rtw_btm_notify_action_resp(struct btm_obj *pbtm)
{
	rtw_fsm_gen_msg(pbtm, NULL, 0, BTM_EV_ft_action_resp);
}

void rtw_btm_notify_scan_found_candidate(struct btm_obj *pbtm, struct wlan_network *pnetwork)
{
	if (!pnetwork)
		return;
	rtw_fsm_gen_msg(pbtm, (void *)pnetwork, 0, BTM_EV_scan_found_candidate);
}

/* For EXTERNAL application to create RRM FSM */
int rtw_btm_reg_fsm(struct fsm_priv *fsmpriv)
{
	struct fsm_root *root = fsmpriv->root;
	struct fsm_main *fsm = NULL;
	struct rtw_fsm_tb tb;
	struct btm_priv *btmpriv = &fsmpriv->btmpriv;

	memset(&tb, 0, sizeof(tb));
	tb.max_state = sizeof(btm_state_tbl)/sizeof(btm_state_tbl[0]);
	tb.max_event = sizeof(btm_event_tbl)/sizeof(btm_event_tbl[0]);
	tb.state_tbl = btm_state_tbl;
	tb.evt_tbl = btm_event_tbl;
	tb.priv = btmpriv;
	tb.init_priv = btm_init_priv_cb;
	tb.deinit_priv = btm_deinit_priv_cb;
	tb.dump_obj = btm_dump_obj_cb;
	tb.dump_fsm = btm_dump_fsm_cb;
	tb.dbg_level = FSM_LV_INFO;
	tb.evt_level = FSM_LV_INFO;
	tb.debug = btm_debug;

	fsm = rtw_fsm_register_fsm(root, "wnm", &tb);
	btmpriv->fsm = fsm;

	if (!fsm)
		return _FAIL;

	return _SUCCESS;
}
#endif /* CONFIG_RTW_FSM_BTM */
