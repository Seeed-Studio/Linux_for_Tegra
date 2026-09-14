/*
 * DHD CONFIG INI
 *
 * Copyright (C) 2025 Synaptics Incorporated. All rights reserved.
 *
 * This software is licensed to you under the terms of the
 * GNU General Public License version 2 (the "GPL") with Broadcom special exception.
 *
 * INFORMATION CONTAINED IN THIS DOCUMENT IS PROVIDED "AS-IS," AND SYNAPTICS
 * EXPRESSLY DISCLAIMS ALL EXPRESS AND IMPLIED WARRANTIES, INCLUDING ANY
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE,
 * AND ANY WARRANTIES OF NON-INFRINGEMENT OF ANY INTELLECTUAL PROPERTY RIGHTS.
 * IN NO EVENT SHALL SYNAPTICS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, PUNITIVE, OR CONSEQUENTIAL DAMAGES ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OF THE INFORMATION CONTAINED IN THIS DOCUMENT, HOWEVER CAUSED
 * AND BASED ON ANY THEORY OF LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, AND EVEN IF SYNAPTICS WAS ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE. IF A TRIBUNAL OF COMPETENT JURISDICTION
 * DOES NOT PERMIT THE DISCLAIMER OF DIRECT DAMAGES OR ANY OTHER DAMAGES,
 * SYNAPTICS' TOTAL CUMULATIVE LIABILITY TO ANY PARTY SHALL NOT
 * EXCEED ONE HUNDRED U.S. DOLLARS
 *
 * Copyright (C) 2025, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

#include <linux/fs.h>
#include <linux/ctype.h>
#include <typedefs.h>
#include <dhd.h>
#include <dhd_config.h>
#include <dhd_dbg.h>
#include <dhd_linux_priv.h>
#include <wlioctl.h>
#include <wldev_common.h>
#include <wl_cfg80211.h>
#include <wl_cfgvif.h>
#include <bcmstdlib_s.h>

#define strtoul(nptr, endptr, base) bcm_strtoul((nptr), (endptr), (base))
#define LEGACY_RATE_COUNT 8
#define MAX_MASK_PATTERN_FILTER_LEN 64

/* ' '     (0x20)    space (SPC)
	'\t'    (0x09)    horizontal tab (TAB)
	'\v'    (0x0b)    vertical tab (VT)
	'\f'    (0x0c)    feed (FF)
	'\r'    (0x0d)    carriage return (CR)
	'\n'    (0x0a)    newline (LF)
 */
#define IS_LINE_END(ch)        (('\r' == (ch)) || ('\n' == (ch)) || ('\0' == (ch)))
#define IS_WHITE_SPACE(ch)     ((' '  == (ch)) || ('\t' == (ch)) || \
	('\v' == (ch)) || ('\f' == (ch)))
#define IS_SPACE(ch)           ((IS_LINE_END(ch)) || (IS_WHITE_SPACE(ch)))
#define IS_COMMENT_CHAR(ch)    ('#' == (ch))

extern int pattern_atoh_len(char *src, char *dst, int len);

static int
wl_parse_chanspec_vec(char *list_str, chanvec_t *chanspec_list, int chanspec_num)
{
	int num = 0;
	uint8 chan;
	chanspec_t chanspec;
	char *next, str[10];
	size_t len;

	if ((next = list_str) == NULL)
		return BCME_ERROR;

	while ((len = strcspn(next, " ,")) > 0) {
		if (len >= sizeof(str)) {
			DHD_ERROR(("chanlist: string \"%s\" before ',' "
				"or ' ' is too long\n", next));
			return BCME_ERROR;
		}
		strlcpy(str, next, sizeof(str));
		str[len] = 0;
		DHD_ERROR(("chanlist: chan %s len %d\n", str, (int)len));
		chanspec = wf_chspec_aton(str);
		if (chanspec == 0) {
			DHD_ERROR(("chanlist: could not parse chanspec starting at "
				"\"%s\" in list:\n%s\n", str, list_str));
			return BCME_ERROR;
		} else if (!CHSPEC_IS20(chanspec)) {
			DHD_ERROR(("chanlist: Please set 20M channel! starting at"
				"\"%s\" in list:\n%s\n", str, list_str));
			return BCME_ERROR;
		}
		if (num == chanspec_num) {
			DHD_ERROR(("chanlist: too many chanspecs (more than %d) "
				"in chanspec list:\n%s\n", chanspec_num, list_str));
			return BCME_ERROR;
		}
		num++;
		chan = CHSPEC_CHANNEL(chanspec);
		chanspec_list->vec[chan / 8] |= (unsigned char)(1 << (chan % 8));
		next += len;
		next += strspn(next, " ,");
	}

	return num;
}

int
dhd_preinit_config_proc(dhd_pub_t *dhd, int ifidx, char *name, char *value)
{
	wl_country_t cspec = {{0}, -1, {0}};
	uint32  var_int;
	char *  revstr;
	char *  endptr = NULL;
	int     ret = 0;

	/* set MAC address */
	if (!strcasecmp(name, "cur_etheraddr")) {
		struct ether_addr ea;

		bcm_ether_atoe(value, &ea);

		ret = memcmp(&ea.octet, dhd->mac.octet, ETHER_ADDR_LEN);
		if (ret == 0) {
			DHD_ERROR(("%s: Same Macaddr\n", __FUNCTION__));
			return 0;
		}

		DHD_ERROR(("%s: Change Macaddr = %02X:%02X:%02X:%02X:%02X:%02X\n", __FUNCTION__,
		           ea.octet[0], ea.octet[1], ea.octet[2],
		           ea.octet[3], ea.octet[4], ea.octet[5]));

		ret = dhd_iovar(dhd, 0, "cur_etheraddr", (char*)&ea, ETHER_ADDR_LEN, NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s: can't set MAC address , error=%d\n", __FUNCTION__, ret));
			return ret;
		} else {
			(void)memcpy_s(dhd->mac.octet, ETHER_ADDR_LEN, (void *)&ea, ETHER_ADDR_LEN);
			(void)memcpy_s(dhd_linux_get_primary_netdev(dhd)->perm_addr, ETHER_ADDR_LEN,
			               dhd->mac.octet, ETHER_ADDR_LEN);

			return ret;
		}
	}
	/* country code */
	else if (!strcasecmp(name, "country")) {
		revstr = strchr(value, '/');
#if defined(DHD_BLOB_EXISTENCE_CHECK)
		if (dhd->is_blob) {
			cspec.rev = 0;
			memcpy(cspec.country_abbrev, value, WLC_CNTRY_BUF_SZ);
			memcpy(cspec.ccode, value, WLC_CNTRY_BUF_SZ);
		} else
#endif /* DHD_BLOB_EXISTENCE_CHECK */
		{
			if (revstr) {
				cspec.rev = strtoul(revstr + 1, &endptr, 10);
				memcpy(cspec.country_abbrev, value, WLC_CNTRY_BUF_SZ);
				cspec.country_abbrev[2] = '\0';
				memcpy(cspec.ccode, cspec.country_abbrev, WLC_CNTRY_BUF_SZ);
			} else {
				cspec.rev = -1;
				memcpy(cspec.country_abbrev, value, WLC_CNTRY_BUF_SZ);
				memcpy(cspec.ccode, value, WLC_CNTRY_BUF_SZ);
#if defined(CUSTOM_COUNTRY_CODE)
				get_customized_country_code(dhd->info->adapter,
					(char *)&cspec.country_abbrev, &cspec, dhd->dhd_cflags);
#else
				get_customized_country_code(dhd->info->adapter,
					(char *)&cspec.country_abbrev, &cspec);
#endif
			}

		}
		DHD_ERROR(("config country code is country : %s, rev : %d !!\n",
			cspec.country_abbrev, cspec.rev));
		ret = dhd_iovar(dhd, 0, "country", (char*)&cspec, sizeof(cspec), NULL, 0, TRUE);
		return ret;
	} else if (!strcasecmp(name, "autocountry")) {
		var_int = (uint)simple_strtol(value, NULL, 0);
		ret = dhd_iovar(dhd, 0, "autocountry", (char *)&var_int,
		                sizeof(var_int), NULL, 0, TRUE);
		return ret;
	}
	/* band */
	else if (!strcasecmp(name, "band")) {
		if (!strcmp(value, "auto"))
			var_int = WLC_BAND_AUTO;
		else if (!strcmp(value, "a"))
			var_int = WLC_BAND_5G;
		else if (!strcmp(value, "b"))
			var_int = WLC_BAND_2G;
		else if (!strcmp(value, "all"))
			var_int = WLC_BAND_ALL;
		else {
			DHD_ERROR(("band should be one of the a or b or all\n"));
			var_int = WLC_BAND_AUTO;
		}
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_BAND, &var_int,
		     sizeof(var_int), TRUE, 0)) < 0)
			DHD_ERROR(("set band err=%d\n", ret));
		return ret;
	} else if (!strcasecmp(name, "autocountry")) {
		var_int = (uint)simple_strtol(value, NULL, 0);
		ret = dhd_iovar(dhd, 0, "autocountry", (char *)&var_int,
		                sizeof(var_int), NULL, 0, TRUE);
		return ret;
	}
	/* scan parameters */
	else if (!strcasecmp(name, "scan_assoc_time")) {
		var_int = (uint)simple_strtol(value, NULL, 0);
		ret = dhd_iovar(dhd, 0, "scan_assoc_time", (char *)&var_int,
		                sizeof(var_int), NULL, 0, TRUE);
		return ret;
	} else if (!strcasecmp(name, "scan_unassoc_time")) {
		var_int = (uint)simple_strtol(value, NULL, 0);
		ret = dhd_iovar(dhd, 0, "scan_unassoc_time", (char *)&var_int,
		                sizeof(var_int), NULL, 0, TRUE);
		return ret;
	} else if (!strcasecmp(name, "scan_passive_time")) {
		var_int = (uint)simple_strtol(value, NULL, 0);
		ret = dhd_iovar(dhd, 0, "scan_passive_time", (char *)&var_int,
		                sizeof(var_int), NULL, 0, TRUE);
		return ret;
	} else if (!strcasecmp(name, "scan_home_time")) {
		var_int = (uint)simple_strtol(value, NULL, 0);
		ret = dhd_iovar(dhd, 0, "scan_home_time", (char *)&var_int,
		                sizeof(var_int), NULL, 0, TRUE);
		return ret;
	} else if (!strcasecmp(name, "scan_home_away_time")) {
		var_int = (uint)simple_strtol(value, NULL, 0);
		ret = dhd_iovar(dhd, 0, "scan_home_away_time", (char *)&var_int,
		                sizeof(var_int), NULL, 0, TRUE);
		return ret;
	} else if (!strcasecmp(name, "scan_nprobes")) {
		var_int = (uint)simple_strtol(value, NULL, 0);
		ret = dhd_iovar(dhd, 0, "scan_nprobes", (char *)&var_int,
		                sizeof(var_int), NULL, 0, TRUE);
		return ret;
	}
	/* roam parameters */
	else if (!strcasecmp(name, "roam_period")) {
		var_int = (uint)simple_strtol(value, NULL, 0);
		ret = dhd_iovar(dhd, 0, "roamperiod", (char *)&var_int,
		                sizeof(var_int), NULL, 0, TRUE);
		return ret;
	} else if (!strcasecmp(name, "full_roam_period")) {
		var_int = (uint)simple_strtol(value, NULL, 0);
		ret = dhd_iovar(dhd, 0, "fullroamperiod", (char *)&var_int,
		                sizeof(var_int), NULL, 0, TRUE);
		return ret;
	}
	/* beacon monitor */
	else if (!strcasecmp(name, "bcn_timeout")) {
		var_int = (uint)simple_strtol(value, NULL, 0);
		ret = dhd_iovar(dhd, 0, "bcn_timeout", (char *)&var_int,
		                sizeof(var_int), NULL, 0, TRUE);
		return ret;
	} else if (!strcasecmp(name, "bcn_li_dtim")) {
		var_int = (uint)simple_strtol(value, NULL, 0);
		ret = dhd_iovar(dhd, 0, "bcn_li_dtim", (char *)&var_int,
		                sizeof(var_int), NULL, 0, TRUE);
		return ret;
	} else if (!strcasecmp(name, "bcn_li_bcn")) {
		var_int = (uint)simple_strtol(value, NULL, 0);
		ret = dhd_iovar(dhd, 0, "bcn_li_bcn", (char *)&var_int,
		                sizeof(var_int), NULL, 0, TRUE);
		return ret;
	}
	/* roam by low RSSI factor */
	else if (!strcasecmp(name, "roam_delta")) {
		struct {
			int val;
			int band;
		} x;
		x.val = (int)simple_strtol(value, NULL, 0);
		/* x.band = WLC_BAND_AUTO; */
		x.band = WLC_BAND_ALL;
		ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_ROAM_DELTA, &x, sizeof(x), TRUE, 0);
		return ret;
	} else if (!strcasecmp(name, "roam_trigger")) {
		int roam_trigger[2];

		roam_trigger[0] = (int)simple_strtol(value, NULL, 0);
		roam_trigger[1] = WLC_BAND_ALL;
		ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_ROAM_TRIGGER, &roam_trigger,
		                       sizeof(roam_trigger), TRUE, 0);
		return ret;
	} else if (!strcasecmp(name, "roam_trigger_2g")) {
		int roam_trigger[2];

		roam_trigger[0] = (int)simple_strtol(value, NULL, 0);
		roam_trigger[1] = WLC_BAND_2G;
		ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_ROAM_TRIGGER, &roam_trigger,
		                       sizeof(roam_trigger), TRUE, 0);
		return ret;
	} else if (!strcasecmp(name, "roam_trigger_5g")) {
		int roam_trigger[2];

		roam_trigger[0] = (int)simple_strtol(value, NULL, 0);
		roam_trigger[1] = WLC_BAND_5G;
		ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_ROAM_TRIGGER, &roam_trigger,
		                       sizeof(roam_trigger), TRUE, 0);
		return ret;
	}
	/* roam by signal drop step factor */
	else if (!strcasecmp(name, "roam_signal_drop_step")) {
		var_int = (uint)simple_strtol(value, NULL, 0);
		ret = dhd_iovar(dhd, 0, "roam_signal_drop_step", (char *)&var_int,
		                sizeof(var_int), NULL, 0, TRUE);
		return ret;
	} else if (!strcasecmp(name, "roam_signal_drop_block_time")) {
		var_int = (uint)simple_strtol(value, NULL, 0);
		ret = dhd_iovar(dhd, 0, "roam_signal_drop_block_time", (char *)&var_int,
		                sizeof(var_int), NULL, 0, TRUE);
		return ret;
	}
	/* roam by QBSS load (CU) factor */
	else if (!strcasecmp(name, "roam_qbss_load_threshold")) {
		var_int = (uint)simple_strtol(value, NULL, 0);
		ret = dhd_iovar(dhd, 0, "roam_qbss_load_threshold", (char *)&var_int,
		                sizeof(var_int), NULL, 0, TRUE);
		return ret;
	} else if (!strcasecmp(name, "roam_qbss_load_duration")) {
		var_int = (uint)simple_strtol(value, NULL, 0);
		ret = dhd_iovar(dhd, 0, "roam_qbss_load_duration", (char *)&var_int,
		                sizeof(var_int), NULL, 0, TRUE);
		return ret;
	}
	/* scan score compute algorithm */
	else if (!strcasecmp(name, "roam_score_rssi_bounder_a")) {
		var_int = (uint)simple_strtol(value, NULL, 0);
		ret = dhd_iovar(dhd, 0, "roam_score_rssi_bounder_a", (char *)&var_int,
		                sizeof(var_int), NULL, 0, TRUE);
		return ret;
	} else if (!strcasecmp(name, "roam_score_rssi_bounder_b")) {
		var_int = (uint)simple_strtol(value, NULL, 0);
		ret = dhd_iovar(dhd, 0, "roam_score_rssi_bounder_b", (char *)&var_int,
		                sizeof(var_int), NULL, 0, TRUE);
		return ret;
	} else if (!strcasecmp(name, "roam_score_coeffs")) {
		roam_score_coeff_t    coeff = {0};
		char  *endp = NULL;
		int    count = 0;

		coeff.index = (uint)simple_strtol(value, &endp, 0);
		while ((endp) && (count < CONST_WL_ROAM_SCORE_COEFF_QTY)) {
			while (IS_WHITE_SPACE(*endp)) {
				endp++;
			}
			if (IS_LINE_END(*endp)) {
				break;
			}
			coeff.coeff[count] = (uint)simple_strtol(endp, &endp, 0);
			count++;
		}
		coeff.count = count;

		ret = dhd_iovar(dhd, 0, "roam_score_coeffs", (char *)&coeff,
		                sizeof(roam_score_coeff_t), NULL, 0, TRUE);
		return ret;
	}
	/* powersaving related */
	else if (!strcasecmp(name, "PM")) {
		var_int = (int)simple_strtol(value, NULL, 0);
		ret =  dhd_wl_ioctl_cmd(dhd, WLC_SET_PM,
			&var_int, sizeof(var_int), TRUE, 0);
		return ret;
	} else if (!strcasecmp(name, "lpc")) {
		var_int = (int)simple_strtol(value, NULL, 0);
		if (dhd_wl_ioctl_cmd(dhd, WLC_DOWN, NULL, 0, TRUE, 0) < 0) {
			DHD_ERROR(("%s: wl down failed\n", __FUNCTION__));
		}
		ret = dhd_iovar(dhd, 0, "lpc", (char *)&var_int, sizeof(var_int), NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s Set lpc failed  %d\n", __FUNCTION__, ret));
		}
		if (dhd_wl_ioctl_cmd(dhd, WLC_UP, NULL, 0, TRUE, 0) < 0) {
			DHD_ERROR(("%s: wl up failed\n", __FUNCTION__));
		}
		return ret;
	}
	/* 802.11k */
	else if (!strcasecmp(name, "rrm")) {
		struct rrm_cap_ie {
			uint8 cap[DOT11_RRM_CAP_LEN];
		} rrm_cap;
		int len;
		uint high = 0, low = 0;
		/* modify to avoid overlfow warning */
		char hs[4+1] = {0}, ls[8+1] = {0};
		len = strlen(value);
		DHD_INFO(("rrm value len %d\n", len));
		if (len <= 12) {
			if ((value[0] == '0')
				&& ((value[1] == 'x')
					||(value[1] == 'X'))) {
				if (len == 11) {
					strlcpy(hs, value, 4);
					strlcpy(ls, value + 3, 9);
					high = (int)simple_strtol(hs, NULL, 16);
					low = (int)simple_strtol(ls, NULL, 16);
				} else if (len == 12) {
					strlcpy(hs, value, 5);
					strlcpy(ls, value + 4, 9);
					high = (int)simple_strtol(hs, NULL, 16);
					low = (int)simple_strtol(ls, NULL, 16);
				} else {
					low = (int)simple_strtol(value, NULL, 16);
				}
			} else {
				high = 0x0;
				low = (int)simple_strtol(value, NULL, 10);
			}

			rrm_cap.cap[0] = low & 0x000000ff;
			rrm_cap.cap[1] = (low & 0x0000ff00) >> 8;
			rrm_cap.cap[2] = (low & 0x00ff0000) >> 16;
			rrm_cap.cap[3] = (low & 0xff000000) >> 24;
			rrm_cap.cap[4] = high;

			if (dhd_wl_ioctl_cmd(dhd, WLC_DOWN, NULL, 0, TRUE, 0) < 0) {
				DHD_ERROR(("%s: wl down failed\n", __FUNCTION__));
			}
			ret = dhd_iovar(dhd, 0, "rrm", (char *)&rrm_cap, sizeof(rrm_cap), NULL, 0,
				TRUE);
			if (ret < 0) {
				DHD_ERROR(("%s Set rrm failed %d\n", __FUNCTION__, ret));
			}
			if (dhd_wl_ioctl_cmd(dhd, WLC_UP, NULL, 0, TRUE, 0) < 0) {
				DHD_ERROR(("%s: wl up failed\n", __FUNCTION__));
			}
			return ret;
		} else {
			DHD_ERROR(("only allow with prefix 0x or decimal incorrect\n"));
			return BCME_USAGE_ERROR;
		}
	}
	/* BW CAP */
	else if (!strcasecmp(name, "bw_cap_2g")) {
		struct {
			u32 band;
			u32 bw_cap;
		} param = {0, 0};

		var_int = (int)simple_strtol(value, NULL, 0);
		param.band = WLC_BAND_2G;
		param.bw_cap = var_int;

		if (dhd_wl_ioctl_cmd(dhd, WLC_DOWN, NULL, 0, TRUE, 0) < 0) {
			DHD_ERROR(("%s: wl down failed\n", __FUNCTION__));
		}
		ret = dhd_iovar(dhd, 0, "bw_cap", (char *)&param, sizeof(param),
		                NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s Set bw_cap failed %d\n", __FUNCTION__, ret));
		}
		if (dhd_wl_ioctl_cmd(dhd, WLC_UP, NULL, 0, TRUE, 0) < 0) {
			DHD_ERROR(("%s: wl up failed\n", __FUNCTION__));
		}
	} else if (!strcasecmp(name, "bw_cap_5g")) {
		struct {
			u32 band;
			u32 bw_cap;
		} param = {0, 0};

		var_int = (int)simple_strtol(value, NULL, 0);
		param.band = WLC_BAND_5G;
		param.bw_cap = var_int;

		if (dhd_wl_ioctl_cmd(dhd, WLC_DOWN, NULL, 0, TRUE, 0) < 0) {
			DHD_ERROR(("%s: wl down failed\n", __FUNCTION__));
		}
		ret = dhd_iovar(dhd, 0, "bw_cap", (char *)&param, sizeof(param),
		                NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s Set bw_cap failed %d\n", __FUNCTION__, ret));
		}
		if (dhd_wl_ioctl_cmd(dhd, WLC_UP, NULL, 0, TRUE, 0) < 0) {
			DHD_ERROR(("%s: wl up failed\n", __FUNCTION__));
		}
	}
	/* 802.11 mode */
	/* 802.11 mode: 11ax */
	else if (!strcasecmp(name, "he_enab")) {
		var_int = (uint)simple_strtol(value, NULL, 0);
		dhd_80211_mode_update(dhd, ifidx,
		        -1, -1, -1, var_int, -1);
		return ret;
	}
	/* 802.11 mode: 11ac */
	else if (!strcasecmp(name, "vhtmode")) {
		var_int = (uint)simple_strtol(value, NULL, 0);
		dhd_80211_mode_update(dhd, ifidx,
		        -1, -1, var_int, -1, -1);
		return ret;
	}
	/* 802.11 mode: 11n */
	else if (!strcasecmp(name, "nmode")) {
		var_int = (uint)simple_strtol(value, NULL, 0);
		dhd_80211_mode_update(dhd, ifidx,
		        -1, var_int, -1, -1, -1);
		return ret;
	}
	/* 802.11 mode: 11b/g */
	else if (!strcasecmp(name, "gmode")) {
		var_int = (uint)simple_strtol(value, NULL, 0);
		dhd_80211_mode_update(dhd, ifidx,
		        var_int, -1, -1, -1, -1);
		return ret;
	}
	/* 802.11 mode: only mode */
	else if (!strcasecmp(name, "nreqd")) {
		var_int = (uint)simple_strtol(value, NULL, 0);
		if (var_int == 1) {
			dhd_80211_mode_update(dhd, ifidx,
			        -1, -1, -1, -1, OMC_HT);
		}
		return ret;
	}
	else if (!strcasecmp(name, "mode_reqd")) {
		var_int = (uint)simple_strtol(value, NULL, 0);
		dhd_80211_mode_update(dhd, ifidx,
		        -1, -1, -1, -1, var_int);
		return ret;
	}
	/* 802.11 ax features */
	else if (!strcasecmp(name, "he_features")) {
		struct net_device *ndev;
		struct bcm_cfg80211 *cfg;

		if ((value[0] == '0') && ((value[1] == 'x') || (value[1] == 'X'))) {
			var_int = (int)simple_strtol(value, &endptr, 16);
		} else {
			var_int = (int)simple_strtol(value, &endptr, 10);
		}
		if (*endptr != '\0') {
			DHD_ERROR(("Wrong format %s\n", endptr));
			return BCME_USAGE_ERROR;
		} else {
			ndev = dhd_linux_get_primary_netdev(dhd);
			if (!ndev) {
				DHD_ERROR(("%s: Cannot find netdev\n", __FUNCTION__));
				return -ENODEV;
			}
			cfg = wl_get_cfg(ndev);
			if (!cfg) {
				DHD_ERROR(("%s: Cannot find cfg\n", __FUNCTION__));
				return -EINVAL;
			}

			ret = wl_cfg80211_set_he_features(ndev, cfg, ifidx,
				WL_IF_TYPE_STA, var_int);
			if (ret) {
				DHD_ERROR(("failed to set ax features %d\n", ret));

			}
			return ret;
		}
	}
	/* 802.11 ac feature */
	else if (!strcasecmp(name, "vht_features")) {
		var_int = (int)simple_strtol(value, NULL, 0);

		if (dhd_wl_ioctl_cmd(dhd, WLC_DOWN, NULL, 0, TRUE, 0) < 0) {
			DHD_ERROR(("%s: wl down failed\n", __FUNCTION__));
		}
		ret = dhd_iovar(dhd, 0, "vht_features", (char *)&var_int, sizeof(var_int), NULL, 0,
			TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s Set vht_features failed  %d\n", __FUNCTION__, ret));
		}
		if (dhd_wl_ioctl_cmd(dhd, WLC_UP, NULL, 0, TRUE, 0) < 0) {
			DHD_ERROR(("%s: wl up failed\n", __FUNCTION__));
		}
		return ret;
	} else if (!strcasecmp(name, "fbt")) {
		uint powervar = 0;
		var_int = (int)simple_strtol(value, NULL, 0);

		powervar = 0;
		ret = dhd_iovar(dhd, 0, "mpc", (char *)&powervar, sizeof(powervar),
		                NULL, 0, TRUE);
		if (ret) {
			DHD_ERROR(("%s: mpc failed:%d\n", __FUNCTION__, ret));
		}
		ret = dhd_iovar(dhd, 0, "fbt", (char *)&var_int, sizeof(var_int),
		                NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s Set fbt failed %d\n", __FUNCTION__, ret));
		}
		powervar = 1;
		if (dhd_iovar(dhd, 0, "mpc", (char *)&powervar, sizeof(powervar),
		              NULL, 0, TRUE) < 0) {
			DHD_ERROR(("%s: mpc failed:%d\n", __FUNCTION__, ret));
		}
		return ret;
	}
	/* rate set */
	else if (!strcasecmp(name, "rateset_legacy")) {
		int v = 0;
		char *p;
		typedef wl_rateset_args_v2_t _wl_rateset_args_t;
		_wl_rateset_args_t defrs;
		struct net_device *ndev;
		int i, rscount = 0;

		bzero(&defrs, sizeof(_wl_rateset_args_t));
		ndev = dhd_linux_get_primary_netdev(dhd);
		if (!ndev) {
			DHD_ERROR(("%s: Cannot find netdev\n", __FUNCTION__));
			return -ENODEV;
		}
		ret = wldev_iovar_getbuf(ndev, "cur_rateset", NULL, 0,
			&defrs, sizeof(_wl_rateset_args_t), NULL);
		if (ret < 0) {
			WL_ERR(("get rateset failed = %d\n", ret));
			return ret;
		}
		bzero(&defrs.rates, sizeof(defrs.rates));
		v = (int)(simple_strtol(value, &p, 0) * 2);
		for (rscount = 0; rscount < LEGACY_RATE_COUNT; rscount++) {
			if (!strnicmp(p, "b,", 2)) {
				v |= 0x80;
				defrs.rates[rscount] = v;
				p += 2;
			} else if (*p == ',') {
				defrs.rates[rscount] = v;
				p++;
			} else if (!strnicmp(p, "b\0", 2)) {
				v |= 0x80;
				defrs.rates[rscount] = v;
				break;
			} else if (*p == '\0') {
				defrs.rates[rscount] = v;
				break;
			}
			v = (int)(simple_strtol(p, &p, 0) * 2);
		}
		/* check no basic rates */
		for (i = 0; i < rscount; i++) {
			if (defrs.rates[i] & 0x80) {
				break;
			}
		}
		if (i < rscount) {
			if (dhd_wl_ioctl_cmd(dhd, WLC_DOWN, NULL, 0, TRUE, 0) < 0) {
				DHD_ERROR(("%s: wl down failed\n", __FUNCTION__));
			}
			ret = dhd_iovar(dhd, 0, "rateset", (char *)&defrs,
				sizeof(_wl_rateset_args_t), NULL, 0, TRUE);
			if (ret < 0) {
				DHD_ERROR(("%s Set rateset_legacy failed %d\n", __FUNCTION__, ret));
			}
			if (dhd_wl_ioctl_cmd(dhd, WLC_UP, NULL, 0, TRUE, 0) < 0) {
				DHD_ERROR(("%s: wl up failed\n", __FUNCTION__));
			}
		}
		return ret;
	} else if (!strcasecmp(name, "rateset_ht")) {
		typedef wl_rateset_args_v2_t _wl_rateset_args_t;
		uint low = 0, high = 0;
		_wl_rateset_args_t defrs;
		struct net_device *ndev;
		uint8 *rsmcs = NULL;

		bzero(&defrs, sizeof(_wl_rateset_args_t));
		ndev = dhd_linux_get_primary_netdev(dhd);
		if (!ndev) {
			DHD_ERROR(("%s: Cannot find netdev\n", __FUNCTION__));
			return -ENODEV;
		}
		ret = wldev_iovar_getbuf(ndev, "cur_rateset", NULL, 0,
			&defrs, sizeof(_wl_rateset_args_t), NULL);
		if (ret < 0) {
			WL_ERR(("get rateset failed = %d\n", ret));
			return ret;
		}
		var_int = (uint)simple_strtol(value, NULL, 16);
		low = var_int & 0x000000ff;
		high = (var_int & 0x0000ff00) >> 8;

		rsmcs = defrs.mcs;
		rsmcs[0] = low;
		rsmcs[1] = high;
		if (dhd_wl_ioctl_cmd(dhd, WLC_DOWN, NULL, 0, TRUE, 0) < 0) {
			DHD_ERROR(("%s: wl down failed\n", __FUNCTION__));
		}
		ret = dhd_iovar(dhd, 0, "rateset", (char *)&defrs, sizeof(_wl_rateset_args_t),
		                NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s Set rateset_ht failed %d\n", __FUNCTION__, ret));
		}
		if (dhd_wl_ioctl_cmd(dhd, WLC_UP, NULL, 0, TRUE, 0) < 0) {
			DHD_ERROR(("%s: wl up failed\n", __FUNCTION__));
		}
		return ret;
	} else if (!strcasecmp(name, "rateset_he")) {
		typedef wl_rateset_args_v2_t _wl_rateset_args_t;
		uint32 stream1 = 0, stream2 = 0;
		_wl_rateset_args_t defrs;
		struct net_device *ndev;
		uint16 *rshe_mcs = NULL;
		char* endp = NULL;

		bzero(&defrs, sizeof(_wl_rateset_args_t));
		ndev = dhd_linux_get_primary_netdev(dhd);
		if (!ndev) {
			DHD_ERROR(("%s: Cannot find netdev\n", __FUNCTION__));
			return -ENODEV;
		}
		ret = wldev_iovar_getbuf(ndev, "cur_rateset", NULL, 0,
			&defrs, sizeof(_wl_rateset_args_t), NULL);
		if (ret < 0) {
			WL_ERR(("get rateset failed = %d\n", ret));
			return ret;
		}

		stream1 = (uint)simple_strtol(value, &endp, 16);
		if (endp[0] == ',') {
			stream2 = (uint)simple_strtol(endp + 1, NULL, 16);
		}
		if ((stream1 != 0x0000) &&   /* vht disabled */
			(stream1 != WL_HE_CAP_MCS_0_7_MAP) &&   /* he mcs0-7 */
			(stream1 != WL_HE_CAP_MCS_0_9_MAP) &&   /* he mcs0-9 */
			(stream1 != WL_HE_CAP_MCS_0_11_MAP)) {  /* he mcs0-11 */
			DHD_ERROR(("HE rate mask must be 0 (disabled),"
			        " 0xff (MCS0-7), 0x3ff (MCS0-9), or 0xfff (MCS0-11).\n"));
			return BCME_BADARG;
		}
		if ((stream2 != 0x0000) &&   /* vht disabled */
			(stream2 != WL_HE_CAP_MCS_0_7_MAP) &&   /* he mcs0-7 */
			(stream2 != WL_HE_CAP_MCS_0_9_MAP) &&   /* he mcs0-9 */
			(stream2 != WL_HE_CAP_MCS_0_11_MAP)) {  /* he mcs0-11 */
			DHD_ERROR(("HE rate mask must be 0 (disabled), 0xff (MCS0-7)"
			           ", 0x3ff (MCS0-9), or 0xfff (MCS0-11).\n"));
			return BCME_BADARG;
		}

		rshe_mcs = defrs.he_mcs;
		rshe_mcs[0] = stream1;
		rshe_mcs[1] = stream2;
		DHD_INFO(("rateset he %x %x\n", rshe_mcs[0], rshe_mcs[1]));
		if (dhd_wl_ioctl_cmd(dhd, WLC_DOWN, NULL, 0, TRUE, 0) < 0) {
			DHD_ERROR(("%s: wl down failed\n", __FUNCTION__));
		}
		ret = dhd_iovar(dhd, 0, "rateset", (char *)&defrs, sizeof(_wl_rateset_args_t),
			NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("rateset %s Set rateset_he failed %d\n", __FUNCTION__, ret));
		}
		if (dhd_wl_ioctl_cmd(dhd, WLC_UP, NULL, 0, TRUE, 0) < 0) {
			DHD_ERROR(("%s: wl up failed\n", __FUNCTION__));
		}
		return ret;
	}
	/* assoc preference */
	else if (!strcasecmp(name, "assoc_pref")) {
		var_int = (int)simple_strtol(value, NULL, 0);
		ret =  dhd_wl_ioctl_cmd(dhd, WLC_SET_ASSOC_PREFER,
			&var_int, sizeof(var_int), TRUE, 0);
		return ret;
	}
	/* retry times */
	else if (!strcasecmp(name, "srl")) {
		var_int = (uint)simple_strtol(value, NULL, 0);
		ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_SRL, &var_int, sizeof(var_int), TRUE, 0);
		if (ret < 0) {
			DHD_ERROR(("rateset %s Set srl failed %d\n", __FUNCTION__, ret));
		}
		return ret;
	} else if (!strcasecmp(name, "lrl")) {
		var_int = (uint)simple_strtol(value, NULL, 0);
		ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_LRL, &var_int, sizeof(var_int), TRUE, 0);
		if (ret < 0) {
			DHD_ERROR(("rateset %s Set srl failed %d\n", __FUNCTION__, ret));
		}
		return ret;
	}
	/* per-frame retry limit */
	else if (!strcasecmp(name, "pfrt_en")) {
		uint powervar = 0;
		var_int = (int)simple_strtol(value, NULL, 0);

		if (dhd_wl_ioctl_cmd(dhd, WLC_UP, NULL, 0, TRUE, 0) < 0) {
			DHD_ERROR(("%s: wl up failed\n", __FUNCTION__));
		}
		ret = dhd_iovar(dhd, 0, "mpc", (char *)&powervar, sizeof(powervar), NULL,
				0, TRUE);
		if (ret) {
			DHD_ERROR(("%s: mpc failed:%d\n", __FUNCTION__, ret));
		}
		ret = dhd_iovar(dhd, 0, "pfrt_en", (char *)&var_int, sizeof(var_int), NULL, 0,
				TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s Set pfrt_en failed %d\n", __FUNCTION__, ret));
		}
		powervar = 1;
		if (dhd_iovar(dhd, 0, "mpc", (char *)&powervar, sizeof(powervar), NULL,
				0, TRUE) < 0) {
			DHD_ERROR(("%s: mpc failed\n", __FUNCTION__));
		}
		return ret;
	} else if (!strcasecmp(name, "pfrt_auth")) {
		uint powervar = 0;
		var_int = (int)simple_strtol(value, NULL, 0);

		if (dhd_wl_ioctl_cmd(dhd, WLC_UP, NULL, 0, TRUE, 0) < 0) {
			DHD_ERROR(("%s: wl up failed\n", __FUNCTION__));
		}
		ret = dhd_iovar(dhd, 0, "mpc", (char *)&powervar, sizeof(powervar), NULL,
				0, TRUE);
		if (ret) {
			DHD_ERROR(("%s: mpc failed:%d\n", __FUNCTION__, ret));
		}
		ret = dhd_iovar(dhd, 0, "pfrt_auth", (char *)&var_int, sizeof(var_int), NULL, 0,
				TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s Set pfrt_auth failed %d\n", __FUNCTION__, ret));
		}
		powervar = 1;
		if (dhd_iovar(dhd, 0, "mpc", (char *)&powervar, sizeof(powervar), NULL,
				0, TRUE) < 0) {
			DHD_ERROR(("%s: mpc failed\n", __FUNCTION__));
		}
		return ret;
	} else if (!strcasecmp(name, "pfrt_asrq")) {
		uint powervar = 0;
		var_int = (int)simple_strtol(value, NULL, 0);

		if (dhd_wl_ioctl_cmd(dhd, WLC_UP, NULL, 0, TRUE, 0) < 0) {
			DHD_ERROR(("%s: wl up failed\n", __FUNCTION__));
		}
		ret = dhd_iovar(dhd, 0, "mpc", (char *)&powervar, sizeof(powervar), NULL,
				0, TRUE);
		if (ret) {
			DHD_ERROR(("%s: mpc failed:%d\n", __FUNCTION__, ret));
		}
		ret = dhd_iovar(dhd, 0, "pfrt_asrq", (char *)&var_int, sizeof(var_int), NULL, 0,
				TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s Set pfrt_asrq failed %d\n", __FUNCTION__, ret));
		}
		powervar = 1;
		if (dhd_iovar(dhd, 0, "mpc", (char *)&powervar, sizeof(powervar), NULL,
				0, TRUE) < 0) {
			DHD_ERROR(("%s: mpc failed\n", __FUNCTION__));
		}
		return ret;
	} else if (!strcasecmp(name, "pfrt_rasrq")) {
		uint powervar = 0;
		var_int = (int)simple_strtol(value, NULL, 0);

		if (dhd_wl_ioctl_cmd(dhd, WLC_UP, NULL, 0, TRUE, 0) < 0) {
			DHD_ERROR(("%s: wl up failed\n", __FUNCTION__));
		}
		ret = dhd_iovar(dhd, 0, "mpc", (char *)&powervar, sizeof(powervar), NULL,
				0, TRUE);
		if (ret) {
			DHD_ERROR(("%s: mpc failed:%d\n", __FUNCTION__, ret));
		}
		ret = dhd_iovar(dhd, 0, "pfrt_rasrq", (char *)&var_int, sizeof(var_int), NULL, 0,
				TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s Set pfrt_rasrq failed %d\n", __FUNCTION__, ret));
		}
		powervar = 1;
		if (dhd_iovar(dhd, 0, "mpc", (char *)&powervar, sizeof(powervar), NULL,
				0, TRUE) < 0) {
			DHD_ERROR(("%s: mpc failed\n", __FUNCTION__));
		}
		return ret;
	} else if (!strcasecmp(name, "pfrt_gact")) {
		uint powervar = 0;
		var_int = (int)simple_strtol(value, NULL, 0);

		if (dhd_wl_ioctl_cmd(dhd, WLC_UP, NULL, 0, TRUE, 0) < 0) {
			DHD_ERROR(("%s: wl up failed\n", __FUNCTION__));
		}
		ret = dhd_iovar(dhd, 0, "mpc", (char *)&powervar, sizeof(powervar), NULL,
				0, TRUE);
		if (ret) {
			DHD_ERROR(("%s: mpc failed:%d\n", __FUNCTION__, ret));
		}
		ret = dhd_iovar(dhd, 0, "pfrt_gact", (char *)&var_int, sizeof(var_int), NULL, 0,
				TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s Set pfrt_gact failed %d\n", __FUNCTION__, ret));
		}
		powervar = 1;
		if (dhd_iovar(dhd, 0, "mpc", (char *)&powervar, sizeof(powervar), NULL,
				0, TRUE) < 0) {
			DHD_ERROR(("%s: mpc failed\n", __FUNCTION__));
		}
		return ret;
	}
	/* AMPDU control per TID */
	else if (!strcasecmp(name, "ampdu_tid")) {
		int i;
		struct {
			uint8 tid;
			uint8 enable;
		} ampdu_tid = {0, 0};

		var_int = (int)simple_strtol(value, NULL, 16);
		if (var_int <= 0xFF) {
			for (i = 0; i < 8; i++) {
				ampdu_tid.tid = i;
				ampdu_tid.enable = (var_int >> i) & 1;
				ret = dhd_iovar(dhd, 0, "ampdu_tid", (char *)&ampdu_tid,
					sizeof(ampdu_tid), NULL, 0, TRUE);
				if (ret < 0) {
					DHD_ERROR(("%s Set ampdu_tid failed %d\n",
						__FUNCTION__, ret));
				}
			}
		} else {
			DHD_ERROR(("Set wrong value to ampdu_tid\n"));
		}
	}
	/* keep alive */
	else if (!strcasecmp(name, "mkeep_alive")) {
		/* XXX TODO: Handle version 2 */
		uint32 msec = 0;
		wl_mkeep_alive_pkt_v1_t mkeep_alive_pkt;

		bzero(&mkeep_alive_pkt, sizeof(mkeep_alive_pkt));
		msec = (int)simple_strtol(value, NULL, 0);
		mkeep_alive_pkt.period_msec = msec;
		mkeep_alive_pkt.version = htod16(WL_MKEEP_ALIVE_VERSION_1);
		mkeep_alive_pkt.length = htod16(WL_MKEEP_ALIVE_FIXED_LEN);
		/* Setup keep alive zero for null packet generation */
		mkeep_alive_pkt.keep_alive_id = 0;
		mkeep_alive_pkt.len_bytes = 0;
		ret = dhd_iovar(dhd, 0, "mkeep_alive", (char *)&mkeep_alive_pkt,
			sizeof(mkeep_alive_pkt), NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s Set mkeep_alive failed %d\n", __FUNCTION__, ret));
		}
	}
	/* packet filter */
	else if (!strcasecmp(name, "pkt_filter_add")) {
		int32 mask_size, pattern_size;
		char *id, *offset, *bitmask, *pattern;
		struct wl_pkt_filter pkt_filter;
		int i;

		pkt_filter.negate_match = htod32(0);
		pkt_filter.type = htod32(0);
		if ((id = bcmstrtok(&value, ",", 0)) == NULL) {
			DHD_ERROR(("%s(): id not found\n", __FUNCTION__));
			return BCME_ERROR;
		}
		if ((offset = bcmstrtok(&value, ",", 0)) == NULL) {
			DHD_ERROR(("%s(): offset not found\n", __FUNCTION__));
			return BCME_ERROR;
		}
		if ((bitmask = bcmstrtok(&value, ",", 0)) == NULL) {
			DHD_ERROR(("%s(): bitmask not found\n", __FUNCTION__));
			return BCME_ERROR;
		}
		if ((pattern = bcmstrtok(&value, ",", 0)) == NULL) {
			DHD_ERROR(("%s(): pattern not found\n", __FUNCTION__));
			return BCME_ERROR;
		}

		pkt_filter.id = strtoul(id, NULL, 0);
		pkt_filter.u.pattern.offset = strtoul(offset, NULL, 0);
		mask_size = pattern_atoh_len(bitmask,
			(char *)pkt_filter.u.pattern.mask_and_pattern,
			MAX_MASK_PATTERN_FILTER_LEN);
		pattern_size = pattern_atoh_len(pattern,
			(char *)&pkt_filter.u.pattern.mask_and_pattern[mask_size],
			MAX_MASK_PATTERN_FILTER_LEN);

		if (mask_size != pattern_size) {
			DHD_ERROR(("Mask and pattern not the same size\n"));
			ret = -EINVAL;
			return ret;
		}

		pkt_filter.u.pattern.size_bytes = mask_size;
		UNUSED_PARAMETER(i);
		for (i = 0; i < (mask_size + pattern_size); i++) {
			DHD_INFO(("pkt_filter_add mask_and_pattern 0x%x\n",
				pkt_filter.u.pattern.mask_and_pattern[i]));
		}

		ret = dhd_iovar(dhd, 0, "pkt_filter_add", (char *)&pkt_filter,
			sizeof(pkt_filter), NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s() Set pkt_filter_add failed %d\n", __FUNCTION__, ret));
		}
	} else if (!strcasecmp(name, "pkt_filter_enable")) {
		char* endp = NULL;
		wl_pkt_filter_enable_t filter_enable = {0, };

		filter_enable.id = (uint)simple_strtol(value, &endp, 0);
		if (endp[0] == ',') {
			filter_enable.enable = (uint)simple_strtol(endp + 1, NULL, 0);
		}

		ret = dhd_iovar(dhd, 0, "pkt_filter_enable", (char *)&filter_enable,
			sizeof(filter_enable), NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("%s Set pkt_filter_enable failed %d\n", __FUNCTION__, ret));
		}
	} else if (!strcasecmp(name, "customer_chanlist")) {
		chanvec_t chanlist;
		int num = 0;

		bzero(&chanlist, sizeof(chanlist));
		DHD_TRACE(("%s: chanlist value %s\n", __FUNCTION__, value));
		num = wl_parse_chanspec_vec(value, &chanlist, MAXCHANNEL);
		if (num > 0) {
			ret = dhd_iovar(dhd, 0, "customer_chanlist", (char *)&chanlist,
				sizeof(chanlist), NULL, 0, TRUE);
			if (ret < 0) {
				DHD_ERROR(("%s set customer_chanlist failed %d\n",
					__FUNCTION__, ret));
			}
		}
	}
#ifdef SYNA_SAR_CUSTOMER_PARAMETER
	/* FCC */
	else if (!strcasecmp(name, "FCC_sar_dyn")) {
		DHD_TRACE(("%s: FCC_sar_dyn value %s\n", __FUNCTION__, value));
		dhd_sar_init_parameter(dhd, SYNA_COUNTRY_TYPE_FCC, SAR_HEAD, value);
	} else if (!strcasecmp(name, "FCC_sar_grip")) {
		DHD_TRACE(("%s: FCC_sar_grip value %s\n", __FUNCTION__, value));
		dhd_sar_init_parameter(dhd, SYNA_COUNTRY_TYPE_FCC, SAR_GRIP, value);
	} else if (!strcasecmp(name, "FCC_sar_bt")) {
		DHD_TRACE(("%s: FCC_sar_bt value %s\n", __FUNCTION__, value));
		dhd_sar_init_parameter(dhd, SYNA_COUNTRY_TYPE_FCC, SAR_BT, value);
	} else if (!strcasecmp(name, "FCC_sar_hotspot")) {
		DHD_TRACE(("%s: FCC_sar_hotspot value %s\n", __FUNCTION__, value));
		dhd_sar_init_parameter(dhd, SYNA_COUNTRY_TYPE_FCC, SAR_HOTSPOT, value);
	}
	/* EU */
	else if (!strcasecmp(name, "EU_sar_dyn")) {
		DHD_TRACE(("%s: EU_sar_dyn value %s\n", __FUNCTION__, value));
		dhd_sar_init_parameter(dhd, SYNA_COUNTRY_TYPE_EU, SAR_HEAD, value);
	} else if (!strcasecmp(name, "EU_sar_Grip")) {
		DHD_TRACE(("%s: EU_sar_Grip value %s\n", __FUNCTION__, value));
		dhd_sar_init_parameter(dhd, SYNA_COUNTRY_TYPE_EU, SAR_GRIP, value);
	} else if (!strcasecmp(name, "EU_sar_bt")) {
		DHD_TRACE(("%s: EU_sar_bt value %s\n", __FUNCTION__, value));
		dhd_sar_init_parameter(dhd, SYNA_COUNTRY_TYPE_EU, SAR_BT, value);
	} else if (!strcasecmp(name, "EU_sar_hotspot")) {
		DHD_TRACE(("%s: EU_sar_hotspot value %s\n", __FUNCTION__, value));
		dhd_sar_init_parameter(dhd, SYNA_COUNTRY_TYPE_EU, SAR_HOTSPOT, value);
	}
	/* LATAM */
	else if (!strcasecmp(name, "LATAM_sar_dyn")) {
		DHD_TRACE(("%s: LATAM_sar_dyn value %s\n", __FUNCTION__, value));
		dhd_sar_init_parameter(dhd, SYNA_COUNTRY_TYPE_LATAM, SAR_HEAD, value);
	} else if (!strcasecmp(name, "LATAM_sar_grip")) {
		DHD_TRACE(("%s: LATAM_sar_grip value %s\n", __FUNCTION__, value));
		dhd_sar_init_parameter(dhd, SYNA_COUNTRY_TYPE_LATAM, SAR_GRIP, value);
	} else if (!strcasecmp(name, "LATAM_sar_bt")) {
		DHD_TRACE(("%s: LATAM_sar_bt value %s\n", __FUNCTION__, value));
		dhd_sar_init_parameter(dhd, SYNA_COUNTRY_TYPE_LATAM, SAR_BT, value);
	} else if (!strcasecmp(name, "LATAM_sar_hotspot")) {
		DHD_TRACE(("%s: LATAM_sar_hotspot value %s\n", __FUNCTION__, value));
		dhd_sar_init_parameter(dhd, SYNA_COUNTRY_TYPE_LATAM, SAR_HOTSPOT, value);
	}
	/* CANADA */
	else if (!strcasecmp(name, "CANADA_sar_dyn")) {
		DHD_TRACE(("%s: CANADA_sar_dyn value %s\n", __FUNCTION__, value));
		dhd_sar_init_parameter(dhd, SYNA_COUNTRY_TYPE_CANADA, SAR_HEAD, value);
	} else if (!strcasecmp(name, "CANADA_sar_grip")) {
		DHD_TRACE(("%s: CANADA_sar_grip value %s\n", __FUNCTION__, value));
		dhd_sar_init_parameter(dhd, SYNA_COUNTRY_TYPE_CANADA, SAR_GRIP, value);
	} else if (!strcasecmp(name, "CANADA_sar_bt")) {
		DHD_TRACE(("%s: CANADA_sar_bt value %s\n", __FUNCTION__, value));
		dhd_sar_init_parameter(dhd, SYNA_COUNTRY_TYPE_CANADA, SAR_BT, value);
	} else if (!strcasecmp(name, "CANADA_sar_hotspot")) {
		DHD_TRACE(("%s: CANADA_sar_hotspot value %s\n", __FUNCTION__, value));
		dhd_sar_init_parameter(dhd, SYNA_COUNTRY_TYPE_CANADA, SAR_HOTSPOT, value);
	}
	/* JAPAN */
	else if (!strcasecmp(name, "JAPAN_sar_dyn")) {
		DHD_TRACE(("%s: JAPAN_sar_dyn value %s\n", __FUNCTION__, value));
		dhd_sar_init_parameter(dhd, SYNA_COUNTRY_TYPE_JAPAN, SAR_HEAD, value);
	} else if (!strcasecmp(name, "JAPAN_sar_grip")) {
		DHD_TRACE(("%s: JAPAN_sar_grip value %s\n", __FUNCTION__, value));
		dhd_sar_init_parameter(dhd, SYNA_COUNTRY_TYPE_JAPAN, SAR_GRIP, value);
	} else if (!strcasecmp(name, "JAPAN_sar_bt")) {
		DHD_TRACE(("%s: JAPAN_sar_bt value %s\n", __FUNCTION__, value));
		dhd_sar_init_parameter(dhd, SYNA_COUNTRY_TYPE_JAPAN, SAR_BT, value);
	} else if (!strcasecmp(name, "JAPAN_sar_hotspot")) {
		DHD_TRACE(("%s: JAPAN_sar_hotspot value %s\n", __FUNCTION__, value));
		dhd_sar_init_parameter(dhd, SYNA_COUNTRY_TYPE_JAPAN, SAR_HOTSPOT, value);
	}
	/* DEFAULT */
	else if ((!strcasecmp(name, "ROW_sar_dyn")) ||
	         (!strcasecmp(name, "sar_dyn")) ||
	         (!strcasecmp(name, "other_sar_dyn")) ||
	         (!strcasecmp(name, "default_sar_dyn"))) {
		DHD_TRACE(("%s: ROW_sar_dyn value %s\n", __FUNCTION__, value));
		dhd_sar_init_parameter(dhd, SYNA_COUNTRY_TYPE_DEFAULT, SAR_HEAD, value);
	} else if ((!strcasecmp(name, "ROW_sar_grip")) ||
	           (!strcasecmp(name, "sar_grip")) ||
	           (!strcasecmp(name, "other_sar_grip")) ||
	           (!strcasecmp(name, "default_sar_grip"))) {
		DHD_TRACE(("%s: ROW_sar_grip value %s\n", __FUNCTION__, value));
		dhd_sar_init_parameter(dhd, SYNA_COUNTRY_TYPE_DEFAULT, SAR_GRIP, value);
	} else if ((!strcasecmp(name, "ROW_sar_bt")) ||
	           (!strcasecmp(name, "sar_bt")) ||
	           (!strcasecmp(name, "other_sar_bt")) ||
	           (!strcasecmp(name, "default_sar_bt"))) {
		DHD_TRACE(("%s: ROW_sar_bt value %s\n", __FUNCTION__, value));
		dhd_sar_init_parameter(dhd, SYNA_COUNTRY_TYPE_DEFAULT, SAR_BT, value);
	} else if ((!strcasecmp(name, "ROW_sar_hotspot")) ||
	           (!strcasecmp(name, "sar_hotspot")) ||
	           (!strcasecmp(name, "other_sar_hotspot")) ||
	           (!strcasecmp(name, "default_sar_hotspot"))) {
		DHD_TRACE(("%s: ROW_sar_hotspot value %s\n", __FUNCTION__, value));
		dhd_sar_init_parameter(dhd, SYNA_COUNTRY_TYPE_DEFAULT, SAR_HOTSPOT, value);
	}
	/* country flag list */
	else if ((!strcasecmp(name, "default_country_list")) ||
	         (!strcasecmp(name, "row_country_list"))) {
		DHD_TRACE(("%s: default_country_list value %s\n", __FUNCTION__, value));
		syna_country_update_type_list(SYNA_COUNTRY_TYPE_DEFAULT, value);
	} else if (!strcasecmp(name, "FCC_country_list")) {
		DHD_TRACE(("%s: FCC_country_list value %s\n", __FUNCTION__, value));
		syna_country_update_type_list(SYNA_COUNTRY_TYPE_FCC, value);
	} else if (!strcasecmp(name, "EU_country_list")) {
		DHD_TRACE(("%s: EU_country_list value %s\n", __FUNCTION__, value));
		syna_country_update_type_list(SYNA_COUNTRY_TYPE_EU, value);
	} else if (!strcasecmp(name, "LATAM_country_list")) {
		DHD_TRACE(("%s: LATAM_country_list value %s\n", __FUNCTION__, value));
		syna_country_update_type_list(SYNA_COUNTRY_TYPE_LATAM, value);
	} else if (!strcasecmp(name, "CANADA_country_list")) {
		DHD_TRACE(("%s: CANADA_country_list value %s\n", __FUNCTION__, value));
		syna_country_update_type_list(SYNA_COUNTRY_TYPE_CANADA, value);
	} else if (!strcasecmp(name, "JAPAN_country_list")) {
		DHD_TRACE(("%s: JAPAN_country_list value %s\n", __FUNCTION__, value));
		syna_country_update_type_list(SYNA_COUNTRY_TYPE_JAPAN, value);
	}
#endif /* SYNA_SAR_CUSTOMER_PARAMETER */
#ifdef BCMPCIE
	/* bus related */
	else if (!strcasecmp(name, "l1ss_init_enab")) {
		var_int = (int)simple_strtol(value, NULL, 0);
		DHD_ERROR(("%s: set the L1SS initialize value to be %d\n", __FUNCTION__, var_int));
		dhd_bus_l1ss_enable_rc_ep(dhd->bus, var_int);
	} else if (!strcasecmp(name, "aspm_init_enab")) {
		var_int = (int)simple_strtol(value, NULL, 0);
		DHD_ERROR(("%s: set the ASPM initialize value to be %d\n", __FUNCTION__, var_int));
		dhd_bus_aspm_enable_rc_ep(dhd->bus, var_int);
	}
#endif /* BCMPCIE */
	/* other default */
	else {
		/* wlu_iovar_setint */
		var_int = (int)simple_strtol(value, NULL, 0);

		/* Setup timeout bcn_timeout from dhd driver 4.217.48 */
		if (!strcmp(name, "roam_off")) {
			/* Setup timeout if Beacons are lost to report link down */
			if (var_int) {
				uint bcn_timeout = 2;
				ret = dhd_iovar(dhd, 0, "bcn_timeout", (char *)&bcn_timeout,
				                sizeof(bcn_timeout), NULL, 0, TRUE);
				if (ret < 0) {
					DHD_ERROR(("%s set bcn_timeout failed %d\n",
					           __FUNCTION__, ret));
				}
			}
		} else {
			DHD_INFO(("%s:[%s]=[%d]\n", __FUNCTION__, name, var_int));

			return dhd_iovar(dhd, 0, name, (char *)&var_int,
					sizeof(var_int), NULL, 0, TRUE);
		}
	}

	return 0;
}

int
dhd_preinit_config(dhd_pub_t *dhd, int ifidx, char * config_file_path)
{
#ifdef DHD_LINUX_STD_FW_API
	const struct firmware *fw = NULL;
#else
#ifdef get_fs
	mm_segment_t old_fs;
#endif /* get_fs */
	struct file *fp = NULL;
	unsigned int len;
#endif /* DHD_LINUX_STD_FW_API */
	long file_len = 0;
	char *buf = NULL, *p, *q, *name, *value;
	int ret = 0;

	if ((config_file_path == NULL) || (config_file_path[0] == '\0')) {
		DHD_ERROR(("%s: *error, config_file_path can't be read. \n", __FUNCTION__));
		return -1;
	}

#ifdef DHD_LINUX_STD_FW_API
	ret = dhd_os_get_img_fwreq(&fw, config_file_path);
	if (ret < 0) {
		DHD_ERROR(("Request Firmware API Error %d\n",
			ret));
		ret = -2;
		goto err;
	}
	file_len = fw->size;
#else
#ifdef get_fs
	old_fs = get_fs();
	set_fs(KERNEL_DS);
#endif /* get_fs */

	/* modify to avoid warning */
	if (!(fp = dhd_os_open_image1(dhd, config_file_path))) {
		DHD_ERROR(("%s: *warning, skip since %s may not exist\n",
		           __FUNCTION__, config_file_path));
		ret = -2;
		goto err;
	}

	file_len = dhd_vfs_size_read(fp);
#endif /* DHD_LINUX_STD_FW_API */
	if (file_len == 0) {
		DHD_ERROR(("%s: done since file %s length is 0\n", __FUNCTION__, config_file_path));
		ret = 0;
		goto err;
	} else if (0 > file_len) {
		DHD_ERROR(("%s: *error, Failed to get %s information (%ld)\n",
		           __FUNCTION__, config_file_path, file_len));
		ret = -3;
		goto err;
	}

	if (!(buf = MALLOC(dhd->osh, file_len + 1))) {
		DHD_ERROR(("%s: *error, Failed to allocate memory %ld bytes\n",
		           __FUNCTION__, file_len));
		ret = -4;
		goto err;
	}
	memset(buf, 0x0, file_len + 1);
	DHD_INFO(("%s: config path : %s \n", __FUNCTION__, config_file_path));

#ifdef DHD_LINUX_STD_FW_API
	ret = memcpy_s(buf, file_len, (char *)(fw->data), file_len);
	if (ret) {
		DHD_ERROR(("%s: *Error - mem copy len = %ld\n",
		           __FUNCTION__, file_len));
		ret = -6;
		goto err;
	}
#else
	/* modify to avoid warning */
	if (0 >= (len = dhd_os_get_image_block(buf, file_len, fp))) {
		DHD_ERROR(("%s: *Error, can't open file %s\n", __FUNCTION__, config_file_path));
		ret = -5;
		goto err;
	}

	if (len != file_len) {
		DHD_ERROR(("%s: *Error - read length mismatched len = %d\n",
		           __FUNCTION__, len));
		ret = -6;
		goto err;
	}
#endif /* DHD_LINUX_STD_FW_API */

	buf[file_len] = '\0';
	for (p = buf; p < (buf + file_len); p++) {
		if (IS_SPACE(*p))  continue;  /* remove all prefix space of 'name' */

		DHD_INFO(("%s-%d: searching item '%s'\n", __FUNCTION__, __LINE__, p));
		if (IS_COMMENT_CHAR(*p)) {
			DHD_INFO(("%s-%d: found #, *p='%c' '%s'\n", __FUNCTION__, __LINE__, *p, p));
			do {
				p++;
			} while (!(IS_LINE_END(*p)));
			DHD_INFO(("%s-%d: done sarch, *p='%c' '%s'\n",
				__FUNCTION__, __LINE__, *p, p));
			continue;
		}
		DHD_INFO(("%s-%d: spliting item '%s'\n", __FUNCTION__, __LINE__, p));

		/* search name */
		for (name = p; *p; p++) {
			if (IS_WHITE_SPACE(*p)) {  /* remove suffix space of 'name' */
				*p = '\0';
			} else if (IS_COMMENT_CHAR(*p)) {
				p--;  /* use the comment char for next time searching */
				break;
			} else if (IS_LINE_END(*p)) {
				break;
			} else if ('=' == *p) {
				/* remove prefix space of 'value' */
				*p = '\0';
				do { p++; } while ((*p) && (IS_WHITE_SPACE(*p)));

				/* search value end */
				for (value = p; !IS_LINE_END(*p); p++) {
					DHD_INFO(("%s-%d:   probe with ch='%c',  0x%p\n",
					         __FUNCTION__, __LINE__, *p, p));
					if (IS_COMMENT_CHAR(*p)) {
						*p = '\0';
					}
				}
				/* remove suffix space of 'value' */
				q = p;
				do {
					*q-- = '\0';
				} while ((q > value) && IS_WHITE_SPACE(*q));
				/* go for processing */
				DHD_ERROR(("%s-%d: name='%s', value='%s'\n",
				          __FUNCTION__, __LINE__, name, value));
				if ((ret = dhd_preinit_config_proc(dhd, ifidx, name, value)) < 0) {
					DHD_ERROR(("%s: %s=%s\n", bcmerrorstr(ret), name, value));
				}

				/* go for next round/line */
				break;
			}
		}
	}

	ret = 0;

err:
	if (buf)
		MFREE(dhd->osh, buf, file_len+1);

#ifdef DHD_LINUX_STD_FW_API
	if (fw) {
		dhd_os_close_img_fwreq(fw);
	}
#else
	if (!IS_ERR_OR_NULL(fp))
		dhd_os_close_image1(dhd, fp);

#ifdef get_fs
	set_fs(old_fs);
#endif /* get_fs */
#endif /* DHD_LINUX_STD_FW_API */

	return ret;
}
