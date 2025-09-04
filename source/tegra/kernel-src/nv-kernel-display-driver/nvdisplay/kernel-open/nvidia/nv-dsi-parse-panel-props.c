/*
 * SPDX-FileCopyrightText: Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
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

#define  __NO_VERSION__

#include "os-interface.h"
#include "nv-linux.h"

#include "os_dsi_panel_props.h"

int bl_name_len;

static DSI_CMD *dsi_parse_command
(
    const struct device_node *node,
    struct property *prop,
    u32 n_cmd
)
{
    DSI_CMD *dsi_cmd = NULL;
    DSI_CMD *temp;
    __be32 *prop_val_ptr;
    u32 count = 0, i = 0;
    u8 arg1 = 0, arg2 = 0, arg3 = 0;
    bool long_pkt = false;

    if (n_cmd == 0)
        return NULL;

    if (!prop)
        return NULL;

    prop_val_ptr = prop->value;

    NV_KMALLOC(dsi_cmd, sizeof(DSI_CMD) * n_cmd);
    if (dsi_cmd == NULL) {
        nv_printf(NV_DBG_ERRORS, "NVRM: DSI cmd memory allocation failed\n");
        return ERR_PTR(-ENOMEM);
    }

    temp = dsi_cmd;

    for (count  = 0; count < n_cmd; count++, temp++) {
        temp->cmd_type = be32_to_cpu(*prop_val_ptr++);
        temp->pdata = NULL;
        if ((temp->cmd_type == DSI_PACKET_CMD) ||
            (temp->cmd_type == DSI_PACKET_VIDEO_VBLANK_CMD)) {
            temp->data_id = be32_to_cpu(*prop_val_ptr++);
            arg1 = be32_to_cpu(*prop_val_ptr++);
            arg2 = be32_to_cpu(*prop_val_ptr++);
            prop_val_ptr++; /* skip ecc */
            long_pkt = (temp->data_id == DSI_GENERIC_LONG_WRITE ||
                temp->data_id == DSI_DCS_LONG_WRITE ||
                temp->data_id == DSI_NULL_PKT_NO_DATA ||
                temp->data_id == DSI_BLANKING_PKT_NO_DATA) ?
                true : false;
            if (!long_pkt && (temp->cmd_type == DSI_PACKET_VIDEO_VBLANK_CMD))
                arg3 = be32_to_cpu(*prop_val_ptr++);
            if (long_pkt) {
                temp->sp_len_dly.data_len = (arg2 << BITS_PER_BYTE) | arg1;
                NV_KMALLOC(temp->pdata, temp->sp_len_dly.data_len);
                for (i = 0; i < temp->sp_len_dly.data_len; i++)
                    (temp->pdata)[i] = be32_to_cpu(*prop_val_ptr++);
                prop_val_ptr += 2; /* skip checksum */
            } else {
                temp->sp_len_dly.sp.data0 = arg1;
                temp->sp_len_dly.sp.data1 = arg2;
                if (temp->cmd_type == DSI_PACKET_VIDEO_VBLANK_CMD)
                    temp->club_cmd = (bool)arg3;
            }
        } else if (temp->cmd_type == DSI_DELAY_MS) {
            temp->sp_len_dly.delay_ms = be32_to_cpu(*prop_val_ptr++);
        } else if (temp->cmd_type == DSI_DELAY_US) {
            temp->sp_len_dly.delay_us = be32_to_cpu(*prop_val_ptr++);
        } else if (temp->cmd_type == DSI_SEND_FRAME) {
            temp->sp_len_dly.frame_cnt = be32_to_cpu(*prop_val_ptr++);
        } else if (temp->cmd_type == DSI_GPIO_SET) {
            temp->sp_len_dly.gpio = be32_to_cpu(*prop_val_ptr++);
            temp->data_id = be32_to_cpu(*prop_val_ptr++);
        }
    }

    return dsi_cmd;
}

static const u32 *dsi_parse_pkt_seq
(
    struct device_node *node,
    struct property    *prop
)
{
    __be32 *prop_val_ptr;
    u32 *pkt_seq;
    int line, i;

#define LINE_STOP 0xff

    if (!prop)
        return NULL;

    NV_KMALLOC(pkt_seq, (sizeof(u32) * NUMOF_PKT_SEQ));
    if (pkt_seq == NULL) {
        nv_printf(NV_DBG_ERRORS, "NVRM: Memory allocation for DSI pkt sequence failed\n");
        return ERR_PTR(-ENOMEM);
    }

    prop_val_ptr = prop->value;

    for (line = 0; line < NUMOF_PKT_SEQ; line += 2) {
        /* compute line value from dt line */
        for (i = 0;; i += 2) {
            u32 cmd = be32_to_cpu(*prop_val_ptr++);
            if (cmd == LINE_STOP)
                break;
            else if (cmd == PKT_LP)
                pkt_seq[line] |= PKT_LP;
            else {
                u32 len = be32_to_cpu(*prop_val_ptr++);
                if (i == 0) /* PKT_ID0 */
                    pkt_seq[line] |=
                        PKT_ID0(cmd) | PKT_LEN0(len);
                if (i == 2) /* PKT_ID1 */
                    pkt_seq[line] |=
                        PKT_ID1(cmd) | PKT_LEN1(len);
                if (i == 4) /* PKT_ID2 */
                    pkt_seq[line] |=
                        PKT_ID2(cmd) | PKT_LEN2(len);
                if (i == 6) /* PKT_ID3 */
                    pkt_seq[line + 1] |=
                        PKT_ID3(cmd) | PKT_LEN3(len);
                if (i == 8) /* PKT_ID4 */
                    pkt_seq[line + 1] |=
                        PKT_ID4(cmd) | PKT_LEN4(len);
                if (i == 10) /* PKT_ID5 */
                    pkt_seq[line + 1] |=
                        PKT_ID5(cmd) | PKT_LEN5(len);
            }
        }
    }

#undef LINE_STOP

     return pkt_seq;
}

static int dsi_get_panel_timings(struct device_node *np_panel, DSI_PANEL_INFO *panelInfo)
{
    struct device_node *np = NULL;
    NvU32 temp;
    DSITIMINGS *modes = &panelInfo->dsiTimings;

    // Get Panel Node from active-panel phandle
    np = of_parse_phandle(np_panel, "nvidia,panel-timings", 0);
    if (!np) {
            nv_printf(NV_DBG_ERRORS, "NVRM: could not find panel timings node for DSI Panel\n");
            return -ENOENT;
    }

    if (!of_property_read_u32(np, "clock-frequency", &temp)) {
            modes->pixelClkRate = temp;
        } else {
            goto parse_mode_timings_fail;
        }
        if (!of_property_read_u32(np, "hsync-len", &temp)) {
            modes->hSyncWidth = temp;
        } else {
            goto parse_mode_timings_fail;
        }
        if (!of_property_read_u32(np, "vsync-len", &temp)) {
            modes->vSyncWidth = temp;
        } else {
            goto parse_mode_timings_fail;
        }
        if (!of_property_read_u32(np, "hback-porch", &temp)) {
            modes->hBackPorch = temp;
        } else {
            goto parse_mode_timings_fail;
        }
        if (!of_property_read_u32(np, "vback-porch", &temp)) {
            modes->vBackPorch = temp;
        } else {
            goto parse_mode_timings_fail;
        }
        if (!of_property_read_u32(np, "hactive", &temp)) {
            modes->hActive = temp;
        } else {
            goto parse_mode_timings_fail;
        }
        if (!of_property_read_u32(np, "vactive", &temp)) {
            modes->vActive = temp;
        } else {
            goto parse_mode_timings_fail;
        }
        if (!of_property_read_u32(np, "hfront-porch", &temp)) {
            modes->hFrontPorch = temp;
        } else {
            goto parse_mode_timings_fail;
        }
        if (!of_property_read_u32(np, "vfront-porch", &temp)) {
            modes->vFrontPorch = temp;
        } else {
            goto parse_mode_timings_fail;
        }

    of_node_put(np);
    return 0U;

parse_mode_timings_fail:
    nv_printf(NV_DBG_ERRORS, "NVRM: One of the mode timings is missing in DSI Panel mode-timings!\n");
    of_node_put(np);
    return -ENOENT;
}

static int dsi_get_panel_gpio(struct device_node *node, DSI_PANEL_INFO *panel)
{
    int count;
    char *label = NULL;

    // If gpios are already populated, just return
    if (panel->panel_gpio_populated)
         return 0;

    if (!node) {
         nv_printf(NV_DBG_ERRORS, "NVRM: DSI Panel node not available\n");
         return -ENOENT;
    }

#if defined(NV_OF_GET_NAME_GPIO_PRESENT)
    panel->panel_gpio[DSI_GPIO_LCD_RESET] =
        of_get_named_gpio(node, "nvidia,panel-rst-gpio", 0);

    panel->panel_gpio[DSI_GPIO_PANEL_EN] =
        of_get_named_gpio(node, "nvidia,panel-en-gpio", 0);

    panel->panel_gpio[DSI_GPIO_PANEL_EN_1] =
        of_get_named_gpio(node, "nvidia,panel-en-1-gpio", 0);

    panel->panel_gpio[DSI_GPIO_BL_ENABLE] =
        of_get_named_gpio(node, "nvidia,panel-bl-en-gpio", 0);

    panel->panel_gpio[DSI_GPIO_BL_PWM] =
        of_get_named_gpio(node, "nvidia,panel-bl-pwm-gpio", 0);

    panel->panel_gpio[DSI_GPIO_TE] =
        of_get_named_gpio(node, "nvidia,te-gpio", 0);

    panel->panel_gpio[DSI_GPIO_AVDD_AVEE_EN] =
        of_get_named_gpio(node, "nvidia,avdd-avee-en-gpio", 0);

    panel->panel_gpio[DSI_GPIO_VDD_1V8_LCD_EN] =
        of_get_named_gpio(node, "nvidia,vdd-1v8-lcd-en-gpio", 0);

    panel->panel_gpio[DSI_GPIO_BRIDGE_EN_0] =
        of_get_named_gpio(node, "nvidia,panel-bridge-en-0-gpio", 0);

    panel->panel_gpio[DSI_GPIO_BRIDGE_EN_1] =
        of_get_named_gpio(node, "nvidia,panel-bridge-en-1-gpio", 0);

    panel->panel_gpio[DSI_GPIO_BRIDGE_REFCLK_EN] =
        of_get_named_gpio(node, "nvidia,panel-bridge-refclk-en-gpio", 0);


    for (count = 0; count < DSI_N_GPIO_PANEL; count++) {
        if (gpio_is_valid(panel->panel_gpio[count])) {
            switch (count) {
            case DSI_GPIO_LCD_RESET:
                label = "dsi-panel-reset";
                break;
            case DSI_GPIO_PANEL_EN:
                label = "dsi-panel-en";
                break;
            case DSI_GPIO_PANEL_EN_1:
                label = "dsi-panel-en-1";
                break;
            case DSI_GPIO_BL_ENABLE:
                label = "dsi-panel-bl-enable";
                break;
            case DSI_GPIO_BL_PWM:
                label = "dsi-panel-pwm";
                break;
            case DSI_GPIO_TE:
                if (panel->dsiEnVRR != NV_TRUE) {
                    panel->panel_gpio[count] = -1;
                } else {
                    label = "dsi-panel-te";
                    panel->dsiVrrPanelSupportsTe = NV_TRUE;
                }
                break;
            case DSI_GPIO_AVDD_AVEE_EN:
                label = "dsi-panel-avdd-avee-en";
                break;
            case DSI_GPIO_VDD_1V8_LCD_EN:
                label = "dsi-panel-vdd-1v8-lcd-en";
                break;
            case DSI_GPIO_BRIDGE_EN_0:
                label = "dsi-panel-bridge-en-0";
                break;
            case DSI_GPIO_BRIDGE_EN_1:
                label = "dsi-panel-bridge-en-1";
                break;
            case DSI_GPIO_BRIDGE_REFCLK_EN:
                label = "dsi-panel-bridge-refclk-en";
                break;
            default:
                nv_printf(NV_DBG_INFO, "NVRM: DSI Panel invalid gpio entry at index %d\n", count);
            }
            if (label) {
                gpio_request(panel->panel_gpio[count], label);
                label = NULL;
            }
        }
    }

    panel->panel_gpio_populated = true;
    return 0U;
#else
    return -EINVAL;
#endif
}

static int parse_dsi_properties(const struct device_node *np_dsi, DSI_PANEL_INFO *dsi)
{
    u32 temp;
    int ret = 0;
    const __be32 *p;
    struct property *prop;
    struct device_node *np_dsi_panel;

    // Get Panel Node from active-panel phandle
    np_dsi_panel = of_parse_phandle(np_dsi, "nvidia,active-panel", 0);
    if (np_dsi_panel == NULL)
    {
        nv_printf(NV_DBG_ERRORS, "NVRM: None of the dsi panel nodes enabled in DT!\n");
        return -EINVAL;
    }

    if (!of_property_read_u32(np_dsi_panel,
            "nvidia,enable-hs-clk-in-lp-mode", &temp))
        dsi->enable_hs_clock_on_lp_cmd_mode = (u8)temp;

    if (of_property_read_bool(np_dsi_panel,
            "nvidia,set-max-dsi-timeout"))
        dsi->set_max_timeout = true;

    if (of_property_read_bool(np_dsi_panel,
            "nvidia,use-legacy-dphy-core"))
        dsi->use_legacy_dphy_core = true;

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-refresh-rate-adj", &temp))
        dsi->refresh_rate_adj = (u8)temp;

    if (!of_property_read_u32(np_dsi_panel,
            "nvidia,dsi-n-data-lanes", &temp))
        dsi->n_data_lanes = (u8)temp;

    if (of_property_read_bool(np_dsi_panel,
            "nvidia,swap-data-lane-polarity"))
        dsi->swap_data_lane_polarity = true;

    if (of_property_read_bool(np_dsi_panel,
            "nvidia,swap-clock-lane-polarity"))
        dsi->swap_clock_lane_polarity = true;

    if (of_property_read_bool(np_dsi_panel,
            "nvidia,reverse-clock-polarity"))
        dsi->reverse_clock_polarity = true;

    if (!of_property_read_u32_array(np_dsi_panel,
             "nvidia,lane-xbar-ctrl",
             dsi->lane_xbar_ctrl, dsi->n_data_lanes))
        dsi->lane_xbar_exists = true;

    if (!of_property_read_u32(np_dsi_panel,
            "nvidia,dsi-phy-type", &temp))
    {
        dsi->dsiPhyType = (u8)temp;
        if ((temp != DSI_DPHY) &&
            (temp != DSI_CPHY))
        {
            nv_printf(NV_DBG_ERRORS,"NVRM: invalid dsi phy type 0x%x\n", temp);
            ret = -EINVAL;
            goto parse_dsi_settings_fail;
        }
    }

    if (!of_property_read_u32(np_dsi_panel,
            "nvidia,dsi-video-burst-mode", &temp))
    {
        dsi->video_burst_mode = (u8)temp;
        if ((temp != DSI_VIDEO_NON_BURST_MODE) &&
            (temp != DSI_VIDEO_NON_BURST_MODE_WITH_SYNC_END) &&
            (temp != DSI_VIDEO_BURST_MODE_LOWEST_SPEED) &&
            (temp != DSI_VIDEO_BURST_MODE_LOW_SPEED) &&
            (temp != DSI_VIDEO_BURST_MODE_MEDIUM_SPEED) &&
            (temp != DSI_VIDEO_BURST_MODE_FAST_SPEED) &&
            (temp != DSI_VIDEO_BURST_MODE_FASTEST_SPEED))
        {
            nv_printf(NV_DBG_ERRORS,"NVRM: invalid dsi video burst mode\n");
            ret = -EINVAL;
            goto parse_dsi_settings_fail;
        }
    }
    if (!of_property_read_u32(np_dsi_panel,
            "nvidia,dsi-pixel-format", &temp))
    {
        dsi->pixel_format = (u8)temp;
        if ((temp != DSI_PIXEL_FORMAT_16BIT_P) &&
            (temp != DSI_PIXEL_FORMAT_18BIT_P) &&
            (temp != DSI_PIXEL_FORMAT_18BIT_NP) &&
            (temp != DSI_PIXEL_FORMAT_24BIT_P) &&
            (temp != DSI_PIXEL_FORMAT_30BIT_P) &&
            (temp != DSI_PIXEL_FORMAT_36BIT_P) &&
            (temp != DSI_PIXEL_FORMAT_8BIT_DSC) &&
            (temp != DSI_PIXEL_FORMAT_10BIT_DSC) &&
            (temp != DSI_PIXEL_FORMAT_12BIT_DSC) &&
            (temp != DSI_PIXEL_FORMAT_16BIT_DSC))
        {
            nv_printf(NV_DBG_ERRORS,"NVRM: invalid dsi pixel format\n");
            ret = -EINVAL;
            goto parse_dsi_settings_fail;
        }
    }
    if (!of_property_read_u32(np_dsi_panel,
            "nvidia,dsi-refresh-rate", &temp))
        dsi->refresh_rate = (u8)temp;

    if (!of_property_read_u32(np_dsi_panel,
            "nvidia,dsi-rated-refresh-rate", &temp))
        dsi->rated_refresh_rate = (u8)temp;

    if (!of_property_read_u32(np_dsi_panel,
            "nvidia,dsi-virtual-channel", &temp))
    {
        dsi->virtual_channel = (u8)temp;
        if ((temp != DSI_VIRTUAL_CHANNEL_0) &&
            (temp != DSI_VIRTUAL_CHANNEL_1) &&
            (temp != DSI_VIRTUAL_CHANNEL_2) &&
            (temp != DSI_VIRTUAL_CHANNEL_3))
        {
            nv_printf(NV_DBG_ERRORS,"NVRM: invalid dsi virtual channel\n");
            ret = -EINVAL;
            goto parse_dsi_settings_fail;
        }
    }

    if (!of_property_read_u32(np_dsi_panel, "nvidia,dsi-instance", &temp))
        dsi->dsi_instance = (u8)temp;

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-panel-reset", &temp))
        dsi->panel_reset = (u8)temp;

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-te-polarity-low", &temp))
        dsi->te_polarity_low = (u8)temp;

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-lp00-pre-panel-wakeup", &temp))
        dsi->lp00_pre_panel_wakeup = (u8)temp;

    if (of_find_property(np_dsi_panel,
        "nvidia,dsi-bl-name", &bl_name_len))
    {
        NV_KMALLOC(dsi->bl_name, sizeof(u8) * bl_name_len);
        if (!of_property_read_string(np_dsi_panel,
            "nvidia,dsi-bl-name",
            (const char **)&dsi->bl_name)) {
        } else {
            nv_printf(NV_DBG_ERRORS, "NVRM: dsi error parsing bl name\n");
            NV_KFREE(dsi->bl_name, sizeof(u8) * bl_name_len);
        }
    }

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-ganged-type", &temp)) {
        dsi->ganged_type = (u8)temp;
        /* Set pixel width to 1 by default for even-odd split */
        if (dsi->ganged_type == DSI_GANGED_SYMMETRIC_EVEN_ODD)
            dsi->even_odd_split_width = 1;
    }

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-even-odd-pixel-width", &temp))
        dsi->even_odd_split_width = temp;

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-ganged-overlap", &temp)) {
        dsi->ganged_overlap = (u16)temp;
        if (!dsi->ganged_type)
            nv_printf(NV_DBG_ERRORS, "NVRM: specified ganged overlap, but no ganged type\n");
    }

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-ganged-swap-links", &temp)) {
        dsi->ganged_swap_links = (bool)temp;
        if (!dsi->ganged_type)
            nv_printf(NV_DBG_ERRORS, "NVRM: specified ganged swapped links, but no ganged type\n");
    }

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-ganged-write-to-all-links", &temp)) {
        dsi->ganged_write_to_all_links = (bool)temp;
        if (!dsi->ganged_type)
            nv_printf(NV_DBG_ERRORS, "NVRM: specified ganged write to all links, but no ganged type\n");
    }

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-split-link-type", &temp))
        dsi->split_link_type = (u8)temp;

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-suspend-aggr", &temp))
        dsi->suspend_aggr = (u8)temp;

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-edp-bridge", &temp))
        dsi->dsi2edp_bridge_enable = (bool)temp;

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-lvds-bridge", &temp))
        dsi->dsi2lvds_bridge_enable = (bool)temp;

    of_property_for_each_u32(np_dsi_panel, "nvidia,dsi-dpd-pads", prop, p, temp)
        dsi->dpd_dsi_pads |= (u32)temp;

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-power-saving-suspend", &temp))
        dsi->power_saving_suspend = (bool)temp;

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-ulpm-not-support", &temp))
        dsi->ulpm_not_supported = (bool)temp;

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-video-data-type", &temp)) {
        dsi->video_data_type = (u8)temp;
        if ((temp != DSI_VIDEO_TYPE_VIDEO_MODE) &&
            (temp != DSI_VIDEO_TYPE_COMMAND_MODE))
        {
            nv_printf(NV_DBG_ERRORS, "NVRM: invalid dsi video data type\n");
            ret = -EINVAL;
            goto parse_dsi_settings_fail;
        }
    }

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-video-clock-mode", &temp)) {
        dsi->video_clock_mode = (u8)temp;
        if ((temp != DSI_VIDEO_CLOCK_CONTINUOUS) &&
           (temp != DSI_VIDEO_CLOCK_TX_ONLY))
        {
            nv_printf(NV_DBG_ERRORS, "NVRM: invalid dsi video clk mode\n");
            ret = -EINVAL;
            goto parse_dsi_settings_fail;
        }
    }

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,enable-vrr", &temp))
        dsi->dsiEnVRR = (u8)temp;

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,vrr-force-set-te-pin", &temp))
        dsi->dsiForceSetTePin = (u8)temp;

    if (of_property_read_bool(np_dsi_panel,
            "nvidia,send-init-cmds-early"))
        dsi->sendInitCmdsEarly = true;

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-n-init-cmd", &temp)) {
        dsi->n_init_cmd = (u16)temp;
    }
    dsi->dsi_init_cmd =
        dsi_parse_command(np_dsi_panel,
            of_find_property(np_dsi_panel,
            "nvidia,dsi-init-cmd", NULL),
            dsi->n_init_cmd);
    if (dsi->n_init_cmd &&
        IS_ERR_OR_NULL(dsi->dsi_init_cmd)) {
        nv_printf(NV_DBG_ERRORS, "NVRM: DSI init cmd parsing from DT failed\n");
        ret = -EINVAL;
        goto parse_dsi_settings_fail;
    };

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-n-postvideo-cmd", &temp)) {
        dsi->n_postvideo_cmd = (u16)temp;
    }
    dsi->dsi_postvideo_cmd =
        dsi_parse_command(np_dsi_panel,
            of_find_property(np_dsi_panel,
            "nvidia,dsi-postvideo-cmd", NULL),
            dsi->n_postvideo_cmd);
    if (dsi->n_postvideo_cmd &&
        IS_ERR_OR_NULL(dsi->dsi_postvideo_cmd)) {
        nv_printf(NV_DBG_ERRORS, "NVRM: DSI postvideo cmd parsing from DT failed\n");
        goto parse_dsi_settings_fail;
    };

    if (!of_property_read_u32(np_dsi_panel,
            "nvidia,dsi-n-suspend-cmd", &temp)) {
        dsi->n_suspend_cmd = (u16)temp;
    }
    dsi->dsi_suspend_cmd =
        dsi_parse_command(np_dsi_panel,
            of_find_property(np_dsi_panel,
            "nvidia,dsi-suspend-cmd", NULL),
            dsi->n_suspend_cmd);
    if (dsi->n_suspend_cmd &&
        IS_ERR_OR_NULL(dsi->dsi_suspend_cmd)) {
        nv_printf(NV_DBG_ERRORS, "NVRM: DSI suspend cmd parsing from DT failed\n");
        ret = -EINVAL;
        goto parse_dsi_settings_fail;
    };

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-n-early-suspend-cmd", &temp)) {
        dsi->n_early_suspend_cmd = (u16)temp;
    }
    dsi->dsi_early_suspend_cmd =
        dsi_parse_command(np_dsi_panel,
            of_find_property(np_dsi_panel,
            "nvidia,dsi-early-suspend-cmd", NULL),
            dsi->n_early_suspend_cmd);
    if (dsi->n_early_suspend_cmd &&
        IS_ERR_OR_NULL(dsi->dsi_early_suspend_cmd)) {
        nv_printf(NV_DBG_ERRORS, "NVRM: DSI early suspend cmd parsing from DT failed\n");
        ret = -EINVAL;
        goto parse_dsi_settings_fail;
    };

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-suspend-stop-stream-late", &temp)) {
        dsi->suspend_stop_stream_late = (bool)temp;
    }

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-n-late-resume-cmd", &temp)) {
        dsi->n_late_resume_cmd = (u16)temp;
    }
    dsi->dsi_late_resume_cmd =
        dsi_parse_command(np_dsi_panel,
            of_find_property(np_dsi_panel,
            "nvidia,dsi-late-resume-cmd", NULL),
            dsi->n_late_resume_cmd);
    if (dsi->n_late_resume_cmd &&
        IS_ERR_OR_NULL(dsi->dsi_late_resume_cmd)) {
        nv_printf(NV_DBG_ERRORS, "NVRM: DSI late resume cmd parsing from DT failed\n");
        ret = -EINVAL;
        goto parse_dsi_settings_fail;
    };

    dsi->pktSeq = dsi_parse_pkt_seq(np_dsi_panel,
                of_find_property(np_dsi_panel,
                "nvidia,dsi-pkt-seq", NULL));
    if (IS_ERR(dsi->pktSeq)) {
        nv_printf(NV_DBG_ERRORS, "NVRM: DSI packet seq parsing from DT fail\n");
        ret = -EINVAL;
        goto parse_dsi_settings_fail;
    }

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-phy-hsdexit", &temp))
        dsi->phyTimingNs.t_hsdexit_ns = (u16)temp;

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-phy-hstrail", &temp))
        dsi->phyTimingNs.t_hstrail_ns = (u16)temp;

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-phy-datzero", &temp))
        dsi->phyTimingNs.t_datzero_ns = (u16)temp;

    if (!of_property_read_u32(np_dsi_panel,
            "nvidia,dsi-phy-hsprepare", &temp))
        dsi->phyTimingNs.t_hsprepare_ns = (u16)temp;

    if (!of_property_read_u32(np_dsi_panel,
            "nvidia,dsi-phy-hsprebegin", &temp))
        dsi->phyTimingNs.t_hsprebegin_ns = (u16)temp;

    if (!of_property_read_u32(np_dsi_panel,
            "nvidia,dsi-phy-hspost", &temp))
        dsi->phyTimingNs.t_hspost_ns = (u16)temp;

    if (!of_property_read_u32(np_dsi_panel,
            "nvidia,dsi-phy-clktrail", &temp))
        dsi->phyTimingNs.t_clktrail_ns = (u16)temp;

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-phy-clkpost", &temp))
        dsi->phyTimingNs.t_clkpost_ns = (u16)temp;

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-phy-clkzero", &temp))
        dsi->phyTimingNs.t_clkzero_ns = (u16)temp;

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-phy-tlpx", &temp))
        dsi->phyTimingNs.t_tlpx_ns = (u16)temp;

    if (!of_property_read_u32(np_dsi_panel,
            "nvidia,dsi-phy-clkprepare", &temp))
        dsi->phyTimingNs.t_clkprepare_ns = (u16)temp;

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-phy-clkpre", &temp))
        dsi->phyTimingNs.t_clkpre_ns = (u16)temp;

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-phy-wakeup", &temp))
        dsi->phyTimingNs.t_wakeup_ns = (u16)temp;

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-phy-taget", &temp))
        dsi->phyTimingNs.t_taget_ns = (u16)temp;

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-phy-tasure", &temp))
        dsi->phyTimingNs.t_tasure_ns = (u16)temp;

    if (!of_property_read_u32(np_dsi_panel,
        "nvidia,dsi-phy-tago", &temp))
        dsi->phyTimingNs.t_tago_ns = (u16)temp;

    if (of_property_read_bool(np_dsi_panel,
            "nvidia,enable-link-compression"))
        dsi->dsiDscEnable = true;

    if (of_property_read_bool(np_dsi_panel,
            "nvidia,enable-dual-dsc"))
        dsi->dsiDscEnDualDsc = true;

    if (of_property_read_bool(np_dsi_panel,
            "nvidia,enable-block-pred"))
        dsi->dsiDscEnBlockPrediction = true;

    if (!of_property_read_u32(np_dsi_panel,
            "nvidia,slice-height", &temp))
        dsi->dsiDscSliceHeight = (u32)temp;

    if (!of_property_read_u32(np_dsi_panel,
            "nvidia,num-of-slices", &temp))
        dsi->dsiDscNumSlices = (u8)temp;

    if (!of_property_read_u32(np_dsi_panel,
            "nvidia,comp-rate", &temp))
        dsi->dsiDscBpp = (u8)temp;

    if (of_property_read_bool(np_dsi, "nvidia,dsi-csi-loopback"))
        dsi->dsi_csi_loopback = 1;

    ret = dsi_get_panel_timings(np_dsi_panel, dsi);
    if (ret != NV_OK) {
        nv_printf(NV_DBG_ERRORS, "NVRM: Parsing DSI Panel Timings failed\n");
        goto parse_dsi_settings_fail;
    }

    ret = dsi_get_panel_gpio(np_dsi_panel, dsi);
    if (ret != NV_OK) {
        nv_printf(NV_DBG_ERRORS, "NVRM: Parsing DSI Panel GPIOs failed\n");
        goto parse_dsi_settings_fail;
    }

parse_dsi_settings_fail:
    return ret;
}

NvBool
nv_dsi_is_panel_connected
(
    nv_state_t      *nv
)
{
    nv_linux_state_t *nvl            = NV_GET_NVL_FROM_NV_STATE(nv);
    struct device_node *np_dsi       = NULL;
    struct device_node *np_dsi_panel = NULL;
    NvBool ret                       = NV_TRUE;

    np_dsi = of_get_child_by_name(nvl->dev->of_node, "dsi");

    if (np_dsi && !of_device_is_available(np_dsi)) {
        ret = NV_FALSE;
        goto fail;
    }

    np_dsi_panel = of_parse_phandle(np_dsi, "nvidia,active-panel", 0);
    if (np_dsi_panel == NULL)
    {
        ret = NV_FALSE;
    }

fail:
    of_node_put(np_dsi_panel);
    of_node_put(np_dsi);
    return ret;
}

NV_STATUS
nv_dsi_parse_panel_props
(
    nv_state_t      *nv,
    void            *dsiPanelInfo
)
{
    int ret                     = NV_OK;
    struct device_node *np_dsi  = NULL;
    nv_linux_state_t *nvl       = NV_GET_NVL_FROM_NV_STATE(nv);

    np_dsi = of_get_child_by_name(nvl->dev->of_node, "dsi");

    if (np_dsi && !of_device_is_available(np_dsi)) {
        nv_printf(NV_DBG_ERRORS, "NVRM: dsi node not enabled in DT\n");
        of_node_put(np_dsi);
        return NV_ERR_NOT_SUPPORTED;
    }

    ret = parse_dsi_properties(np_dsi, (DSI_PANEL_INFO *)dsiPanelInfo);

    return ret;
}

NV_STATUS
nv_dsi_panel_enable
(
    nv_state_t      *nv,
    void            *dsiPanelInfo
)
{
    int ret = NV_OK;
    DSI_PANEL_INFO *panelInfo = dsiPanelInfo;

    if (gpio_is_valid(panelInfo->panel_gpio[DSI_GPIO_VDD_1V8_LCD_EN])) {
        gpio_direction_output(panelInfo->panel_gpio[DSI_GPIO_VDD_1V8_LCD_EN], 1);
    }

    mdelay(10); //Required 1ms delay

    if (gpio_is_valid(panelInfo->panel_gpio[DSI_GPIO_AVDD_AVEE_EN])) {
        gpio_direction_output(panelInfo->panel_gpio[DSI_GPIO_AVDD_AVEE_EN], 1);
    }

    mdelay(20); //Required 10ms delay

    // If backlight enable gpio is specified, set it to output direction and pull high
    if (gpio_is_valid(panelInfo->panel_gpio[DSI_GPIO_BL_ENABLE])) {
        gpio_direction_output(panelInfo->panel_gpio[DSI_GPIO_BL_ENABLE], 1);
    }

    mdelay(10);

    if (gpio_is_valid(panelInfo->panel_gpio[DSI_GPIO_PANEL_EN])) {
        gpio_direction_output(panelInfo->panel_gpio[DSI_GPIO_PANEL_EN], 1);
    }

    mdelay(20); // Requied 10ms

    return ret;
}

NV_STATUS
nv_dsi_panel_reset
(
    nv_state_t      *nv,
    void            *dsiPanelInfo
)
{
    int ret = NV_OK;
    int en_panel_rst = -1;
    DSI_PANEL_INFO *panelInfo = dsiPanelInfo;

    // Assert and deassert Panel reset GPIO
    if (gpio_is_valid(panelInfo->panel_gpio[DSI_GPIO_LCD_RESET])) {
        en_panel_rst = panelInfo->panel_gpio[DSI_GPIO_LCD_RESET];
    } else {
        nv_printf(NV_DBG_ERRORS, "DSI Panel reset gpio invalid\n");
        goto fail;
    }

    ret = gpio_direction_output(en_panel_rst, 1);
    if (ret < 0) {
        nv_printf(NV_DBG_ERRORS, "Deasserting DSI panel reset gpio failed\n");
        goto fail;
    }

    mdelay(10);

    ret = gpio_direction_output(en_panel_rst, 0);
    if (ret < 0) {
        nv_printf(NV_DBG_ERRORS, "Asserting DSI panel reset gpio failed\n");
        goto fail;
    }

    mdelay(10);

    ret = gpio_direction_output(en_panel_rst, 1);
    if (ret < 0) {
        nv_printf(NV_DBG_ERRORS, "Deasserting Dsi panel reset gpio after asserting failed\n");
        goto fail;
    }

fail:
    return ret;
}

void nv_dsi_panel_disable
(
    nv_state_t      *nv,
    void            *dsiPanelInfo
)
{
    DSI_PANEL_INFO *panelInfo = dsiPanelInfo;

    if (gpio_is_valid(panelInfo->panel_gpio[DSI_GPIO_BL_ENABLE])) {
        gpio_direction_output(panelInfo->panel_gpio[DSI_GPIO_BL_ENABLE], 0);
    }

    mdelay(10);

    if (gpio_is_valid(panelInfo->panel_gpio[DSI_GPIO_PANEL_EN])) {
        gpio_direction_output(panelInfo->panel_gpio[DSI_GPIO_PANEL_EN], 0);
    }

    // Assert Panel reset GPIO
    if (gpio_is_valid(panelInfo->panel_gpio[DSI_GPIO_LCD_RESET])) {
        gpio_direction_output(panelInfo->panel_gpio[DSI_GPIO_LCD_RESET], 0);
    }

    mdelay(20);

    if (gpio_is_valid(panelInfo->panel_gpio[DSI_GPIO_AVDD_AVEE_EN])) {
        gpio_direction_output(panelInfo->panel_gpio[DSI_GPIO_AVDD_AVEE_EN], 0);
    }

    mdelay(10);

    if (gpio_is_valid(panelInfo->panel_gpio[DSI_GPIO_VDD_1V8_LCD_EN])) {
        gpio_direction_output(panelInfo->panel_gpio[DSI_GPIO_VDD_1V8_LCD_EN], 0);
    }
}

static void dsi_panel_command_cleanup
(
    DSI_CMD *dsi_cmd,
    u32     n_cmd
)
{
    int i;
    DSI_CMD *temp = dsi_cmd;

    if (dsi_cmd == NULL)
        return;

    for (i = 0; i < n_cmd; i++, temp++) {
        if (temp->pdata != NULL)
            NV_KFREE(temp->pdata, temp->sp_len_dly.data_len);
    }

    NV_KFREE(dsi_cmd, sizeof(DSI_CMD) * n_cmd);
}

void nv_dsi_panel_cleanup
(
    nv_state_t      *nv,
    void            *dsiPanelInfo
)
{
    int count;
    DSI_PANEL_INFO *panelInfo = dsiPanelInfo;

    dsi_panel_command_cleanup(panelInfo->dsi_init_cmd, panelInfo->n_init_cmd);

    dsi_panel_command_cleanup(panelInfo->dsi_postvideo_cmd, panelInfo->n_postvideo_cmd);

    dsi_panel_command_cleanup(panelInfo->dsi_suspend_cmd, panelInfo->n_suspend_cmd);

    dsi_panel_command_cleanup(panelInfo->dsi_early_suspend_cmd, panelInfo->n_early_suspend_cmd);

    dsi_panel_command_cleanup(panelInfo->dsi_late_resume_cmd, panelInfo->n_late_resume_cmd);

    if (panelInfo->pktSeq != NULL) {
        NV_KFREE((u32 *)panelInfo->pktSeq, sizeof(u32) * NUMOF_PKT_SEQ);
    }

    if (panelInfo->bl_name != NULL) {
        NV_KFREE(panelInfo->bl_name, sizeof(u8) * bl_name_len);
    }

    for (count = 0; count < DSI_N_GPIO_PANEL; count++) {
        if (gpio_is_valid(panelInfo->panel_gpio[count])) {
            gpio_free(panelInfo->panel_gpio[count]);
        }
    }
    panelInfo->panel_gpio_populated = false;
}
