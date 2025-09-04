/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2020 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

/*!
 * @brief The below defined static const array points to the
 * clock mentioned in enum defined in below file.
 *
 * arch/nvalloc/unix/include/nv.h
 * enum TEGRASOC_WHICH_CLK
 *
 * The order should be maintained/updated together.
 */
static const char *osMapClk[] = {
    [TEGRASOC_WHICH_CLK_NVDISPLAYHUB]      = "nvdisplayhub_clk",
    [TEGRASOC_WHICH_CLK_NVDISPLAY_DISP]    = "nvdisplay_disp_clk",
    [TEGRASOC_WHICH_CLK_NVDISPLAY_P0]      = "nvdisplay_p0_clk",
    [TEGRASOC_WHICH_CLK_NVDISPLAY_P1]      = "nvdisplay_p1_clk",
    [TEGRASOC_WHICH_CLK_DPAUX0]            = "dpaux0_clk",
    [TEGRASOC_WHICH_CLK_FUSE]              = "fuse_clk",
    [TEGRASOC_WHICH_CLK_DSIPLL_VCO]        = "dsipll_vco_clk",
    [TEGRASOC_WHICH_CLK_DSIPLL_CLKOUTPN]   = "dsipll_clkoutpn_clk",
    [TEGRASOC_WHICH_CLK_DSIPLL_CLKOUTA]    = "dsipll_clkouta_clk",
    [TEGRASOC_WHICH_CLK_SPPLL0_VCO]        = "sppll0_vco_clk",
    [TEGRASOC_WHICH_CLK_SPPLL0_CLKOUTPN]   = "sppll0_clkoutpn_clk",
    [TEGRASOC_WHICH_CLK_SPPLL0_CLKOUTA]    = "sppll0_clkouta_clk",
    [TEGRASOC_WHICH_CLK_SPPLL0_CLKOUTB]    = "sppll0_clkoutb_clk",
    [TEGRASOC_WHICH_CLK_SPPLL0_DIV10]      = "sppll0_div10_clk",
    [TEGRASOC_WHICH_CLK_SPPLL0_DIV25]      = "sppll0_div25_clk",
    [TEGRASOC_WHICH_CLK_SPPLL0_DIV27]      = "sppll0_div27_clk",
    [TEGRASOC_WHICH_CLK_SPPLL1_VCO]        = "sppll1_vco_clk",
    [TEGRASOC_WHICH_CLK_SPPLL1_CLKOUTPN]   = "sppll1_clkoutpn_clk",
    [TEGRASOC_WHICH_CLK_SPPLL1_DIV27]      = "sppll1_div27_clk",
    [TEGRASOC_WHICH_CLK_VPLL0_REF]         = "vpll0_ref_clk",
    [TEGRASOC_WHICH_CLK_VPLL0]             = "vpll0_clk",
    [TEGRASOC_WHICH_CLK_VPLL1]             = "vpll1_clk",
    [TEGRASOC_WHICH_CLK_NVDISPLAY_P0_REF]  = "nvdisplay_p0_ref_clk",
    [TEGRASOC_WHICH_CLK_RG0]               = "rg0_clk",
    [TEGRASOC_WHICH_CLK_RG1]               = "rg1_clk",
    [TEGRASOC_WHICH_CLK_DISPPLL]           = "disppll_clk",
    [TEGRASOC_WHICH_CLK_DISPHUBPLL]        = "disphubpll_clk",
    [TEGRASOC_WHICH_CLK_DSI_LP]            = "dsi_lp_clk",
    [TEGRASOC_WHICH_CLK_DSI_CORE]          = "dsi_core_clk",
    [TEGRASOC_WHICH_CLK_DSI_PIXEL]         = "dsi_pixel_clk",
    [TEGRASOC_WHICH_CLK_PRE_SOR0]          = "pre_sor0_clk",
    [TEGRASOC_WHICH_CLK_PRE_SOR1]          = "pre_sor1_clk",
    [TEGRASOC_WHICH_CLK_DP_LINK_REF]       = "dp_link_ref_clk",
    [TEGRASOC_WHICH_CLK_SOR_LINKA_INPUT]   = "sor_linka_input_clk",
    [TEGRASOC_WHICH_CLK_SOR_LINKA_AFIFO]   = "sor_linka_afifo_clk",
    [TEGRASOC_WHICH_CLK_SOR_LINKA_AFIFO_M] = "sor_linka_afifo_m_clk",
    [TEGRASOC_WHICH_CLK_RG0_M]             = "rg0_m_clk",
    [TEGRASOC_WHICH_CLK_RG1_M]             = "rg1_m_clk",
    [TEGRASOC_WHICH_CLK_SOR0_M]            = "sor0_m_clk",
    [TEGRASOC_WHICH_CLK_SOR1_M]            = "sor1_m_clk",
    [TEGRASOC_WHICH_CLK_PLLHUB]            = "pllhub_clk",
    [TEGRASOC_WHICH_CLK_SOR0]              = "sor0_clk",
    [TEGRASOC_WHICH_CLK_SOR1]              = "sor1_clk",
    [TEGRASOC_WHICH_CLK_SOR_PAD_INPUT]     = "sor_pad_input_clk",
    [TEGRASOC_WHICH_CLK_PRE_SF0]           = "pre_sf0_clk",
    [TEGRASOC_WHICH_CLK_SF0]               = "sf0_clk",
    [TEGRASOC_WHICH_CLK_SF1]               = "sf1_clk",
    [TEGRASOC_WHICH_CLK_PRE_SOR0_REF]      = "pre_sor0_ref_clk",
    [TEGRASOC_WHICH_CLK_PRE_SOR1_REF]      = "pre_sor1_ref_clk",
    [TEGRASOC_WHICH_CLK_SOR0_PLL_REF]      = "sor0_ref_pll_clk",
    [TEGRASOC_WHICH_CLK_SOR1_PLL_REF]      = "sor1_ref_pll_clk",
    [TEGRASOC_WHICH_CLK_SOR0_REF]          = "sor0_ref_clk",
    [TEGRASOC_WHICH_CLK_SOR1_REF]          = "sor1_ref_clk",
    [TEGRASOC_WHICH_CLK_DSI_PAD_INPUT]     = "dsi_pad_input_clk",
    [TEGRASOC_WHICH_CLK_OSC]               = "osc_clk",
    [TEGRASOC_WHICH_CLK_DSC]               = "dsc_clk",
    [TEGRASOC_WHICH_CLK_MAUD]              = "maud_clk",
    [TEGRASOC_WHICH_CLK_AZA_2XBIT]         = "aza_2xbit_clk",
    [TEGRASOC_WHICH_CLK_AZA_BIT]           = "aza_bit_clk",
    [TEGRASOC_WHICH_CLK_MIPI_CAL]          = "mipi_cal_clk",
    [TEGRASOC_WHICH_CLK_UART_FST_MIPI_CAL] = "uart_fst_mipi_cal_clk",
    [TEGRASOC_WHICH_CLK_SOR0_DIV]          = "sor0_div_clk",
};

/*!
 * @brief Get the clock handles.
 *
 * Look up and obtain the clock handles for each display
 * clock at boot-time and later using all those handles
 * for rest of the operations. for example, enable/disable
 * clocks, get current/max frequency of the clock.
 *
 * For more details on CCF functions, please check below file:
 *
 * In the Linux kernel: include/linux/clk.h
 * or
 * https://www.kernel.org/doc/htmldocs/kernel-api/
 *
 * @param[in]  nv      Per gpu linux state
 *
 * @returns NV_STATUS
 */
NV_STATUS NV_API_CALL nv_clk_get_handles(
    nv_state_t *nv)
{
    NV_STATUS status      = NV_OK;
    nv_linux_state_t *nvl = NV_GET_NVL_FROM_NV_STATE(nv);
    NvU32 i, j, clk_count;
#if defined(NV_DEVM_CLK_BULK_GET_ALL_PRESENT)
    struct clk_bulk_data *clks;

    clk_count = devm_clk_bulk_get_all(nvl->dev, &clks);

    if (clk_count == 0)
    {
        nv_printf(NV_DBG_ERRORS,"NVRM: nv_clk_get_handles, failed to get clk handles from devm_clk_bulk_get_all\n");
        return NV_ERR_OBJECT_NOT_FOUND;
    }

    //
    // TEGRASOC_WHICH_CLK_MAX is maximum clock defined in below enum
    // arch/nvalloc/unix/include/nv.h
    // enum TEGRASOC_WHICH_CLK
    //
    for (i = 0U; i < clk_count; i++)
    {
        for (j = 0U; j < TEGRASOC_WHICH_CLK_MAX; j++)
        {
            if (!strcmp(osMapClk[j], clks[i].id))
            {
                nvl->disp_clk_handles.clk[j].handles = clks[i].clk;
                nvl->disp_clk_handles.clk[j].clkName = __clk_get_name(clks[i].clk);
                break;
            }   
        }
        if (j == TEGRASOC_WHICH_CLK_MAX)
        {
            nv_printf(NV_DBG_ERRORS,"NVRM: nv_clk_get_handles, failed to find TEGRA_SOC_WHICH_CLK for %s\n", clks[i].id);
            return NV_ERR_OBJECT_NOT_FOUND;
        }
    }
#else
    nv_printf(NV_DBG_ERRORS, "NVRM: devm_clk_bulk_get_all API is not present\n");
    status = NV_ERR_OBJECT_NOT_FOUND;
#endif

    return status;
}

/*!
 * @brief Clear the clock handles assigned by nv_clk_get_handles()
 *
 * Clear the clock handle for each display of the clocks at shutdown-time.
 * Since clock handles are obtained by devm managed devm_clk_bulk_get_all()
 * API, devm_clk_bulk_release_all() API is called on all the enumerated
 * clk handles automatically when module gets unloaded. Hence, no need
 * to explicitly free those handles.
 *
 * For more details on CCF functions, please check below file:
 *
 * In the Linux kernel: include/linux/clk.h
 * or
 * https://www.kernel.org/doc/htmldocs/kernel-api/
 *
 * @param[in]  nv      Per gpu linux state
 */
void NV_API_CALL nv_clk_clear_handles(
    nv_state_t *nv)
{
    nv_linux_state_t *nvl = NV_GET_NVL_FROM_NV_STATE(nv);
    NvU32 i;

    //
    // TEGRASOC_WHICH_CLK_MAX is maximum clock defined in below enum
    // arch/nvalloc/unix/include/nv.h
    // enum TEGRASOC_WHICH_CLK
    //
    for (i = 0U; i < TEGRASOC_WHICH_CLK_MAX; i++)
    {
        if (nvl->disp_clk_handles.clk[i].handles != NULL)
        {
            nvl->disp_clk_handles.clk[i].handles = NULL;
        }
    }
}

/*!
 * @brief Enable the clock.
 *
 * Enabling the clock before performing any operation
 * on it. The below function will prepare the clock for use
 * and enable them.
 *
 * for more details on CCF functions, please check below file:
 *
 * In the Linux kernel: include/linux/clk.h
 * or
 * https://www.kernel.org/doc/htmldocs/kernel-api/
 *
 * @param[in] nv          Per gpu linux state
 * @param[in] whichClkOS  Enum value of the target clock
 *
 * @returns NV_STATUS
 */
NV_STATUS NV_API_CALL nv_enable_clk(
    nv_state_t *nv, TEGRASOC_WHICH_CLK whichClkOS)
{
    nv_linux_state_t *nvl = NV_GET_NVL_FROM_NV_STATE(nv);
    NV_STATUS status      = NV_ERR_GENERIC;
    int ret;

    if (nvl->disp_clk_handles.clk[whichClkOS].handles != NULL)
    {
        ret = clk_prepare_enable(nvl->disp_clk_handles.clk[whichClkOS].handles);

        if (ret == 0)
        {
            status = NV_OK;
        }
        else
        {
            status = NV_ERR_FEATURE_NOT_ENABLED;
            nv_printf(NV_DBG_ERRORS, "NVRM: clk_prepare_enable failed with error: %d\n", ret);
        }
    }
    else
    {
        status = NV_ERR_OBJECT_NOT_FOUND;
    }

    return status;
}

/*!
 * @brief Check if clock is enable or not.
 *
 * Checking the clock status if it is enabled or not before
 * enabling or disabling it.
 *
 * for more details on CCF functions, please check below file:
 *
 * In the Linux kernel: include/linux/clk.h
 * or
 * https://www.kernel.org/doc/htmldocs/kernel-api/
 *
 * @param[in] nv          Per gpu linux state
 * @param[in] whichClkOS  Enum value of the target clock
 *
 * @returns clock status.
 */
NvBool NV_API_CALL nv_is_clk_enabled(
    nv_state_t *nv, TEGRASOC_WHICH_CLK whichClkOS)
{
    nv_linux_state_t *nvl = NV_GET_NVL_FROM_NV_STATE(nv);
    bool ret;

    if (nvl->disp_clk_handles.clk[whichClkOS].handles == NULL)
    {
        nv_printf(NV_DBG_ERRORS, "NVRM: clock handle requested not found.\n");
        return NV_FALSE;
    }

    ret = __clk_is_enabled(nvl->disp_clk_handles.clk[whichClkOS].handles);
    return ret == true;
}

/*!
 * @brief Disable the clock.
 *
 * Disabling the clock after performing operation or required
 * work with that clock is done with that particular clock.
 * The below function will unprepare the clock for further use
 * and disable them.
 *
 * Note: make sure to disable clock before clk_put is called.
 *
 * For more details on CCF functions, please check below file:
 *
 * In the Linux kernel: include/linux/clk.h
 * or
 * https://www.kernel.org/doc/htmldocs/kernel-api/
 *
 * @param[in] nv          Per gpu linux state
 * @param[in] whichClkOS  Enum value of the target clock
 */
void NV_API_CALL nv_disable_clk(
    nv_state_t *nv, TEGRASOC_WHICH_CLK whichClkOS)
{
    nv_linux_state_t *nvl = NV_GET_NVL_FROM_NV_STATE(nv);

    clk_disable_unprepare(nvl->disp_clk_handles.clk[whichClkOS].handles);
}

/*!
 * @brief Get current clock frequency.
 *
 * Obtain the current clock rate for a clock source.
 * This is only valid once the clock source has been enabled.
 *
 * For more details on CCF functions, please check below file:
 *
 * In the Linux kernel: include/linux/clk.h
 * or
 * https://www.kernel.org/doc/htmldocs/kernel-api/
 *
 * @param[in] nv            Per gpu linux state
 * @param[in] whichClkOS    Enum value of the target clock
 * @param[out] pCurrFreqKHz Current clock frequency
 */
NV_STATUS NV_API_CALL nv_get_curr_freq(
    nv_state_t *nv, TEGRASOC_WHICH_CLK whichClkOS, NvU32 *pCurrFreqKHz)
{
    nv_linux_state_t *nvl = NV_GET_NVL_FROM_NV_STATE(nv);
    NV_STATUS status      = NV_ERR_GENERIC;
    unsigned long currFreqHz;

    if (nvl->disp_clk_handles.clk[whichClkOS].handles != NULL)
    {
        currFreqHz = clk_get_rate(nvl->disp_clk_handles.clk[whichClkOS].handles);
        *pCurrFreqKHz = currFreqHz / 1000U;

        if (*pCurrFreqKHz > 0U)
        {
            status = NV_OK;
        }
        else
        {
            status = NV_ERR_FEATURE_NOT_ENABLED;
        }
    }
    else
    {
        status = NV_ERR_OBJECT_NOT_FOUND;
    }

    return status;
}

/*!
 * @brief Get maximum clock frequency.
 *
 * Obtain the maximum clock rate a clock source can provide.
 * This is only valid once the clock source has been enabled.
 *
 * For more details on CCF functions, please check below file:
 *
 * In the Linux kernel: include/linux/clk.h
 * or
 * https://www.kernel.org/doc/htmldocs/kernel-api/
 *
 * @param[in] nv           Per gpu linux state
 * @param[in] whichClkOS   Enum value of the target clock
 * @param[out] pMaxFreqKHz Maximum clock frequency
 */
NV_STATUS NV_API_CALL nv_get_max_freq(
    nv_state_t *nv, TEGRASOC_WHICH_CLK whichClkOS, NvU32 *pMaxFreqKHz)
{
    nv_linux_state_t *nvl = NV_GET_NVL_FROM_NV_STATE(nv);
    NV_STATUS status      = NV_ERR_GENERIC;
    long ret;

    if (nvl->disp_clk_handles.clk[whichClkOS].handles != NULL)
    {
        //
        // clk_round_rate(struct clk *clk, rate);
        // rate is the maximum possible rate we give,
        // it returns rounded clock rate in Hz, i.e.,
        // maximum clock rate the source clock can
        // support or negative errno.
        // Here, rate = NV_S64_MAX
        // 0 < currFreq < maxFreq < NV_S64_MAX
        // clk_round_rate() round of and return the
        // nearest freq what a clock can provide.
        // sending NV_S64_MAX will return maxFreq.
        //
        ret = clk_round_rate(nvl->disp_clk_handles.clk[whichClkOS].handles, NV_U32_MAX);

        if (ret >= 0)
        {
            *pMaxFreqKHz = (NvU32) (ret / 1000);
            status = NV_OK;
        }
        else
        {
            status = NV_ERR_FEATURE_NOT_ENABLED;
            nv_printf(NV_DBG_ERRORS, "NVRM: clk_round_rate failed with error: %ld\n", ret);
        }
    }
    else
    {
        status = NV_ERR_OBJECT_NOT_FOUND;
    }

    return status;
}

/*!
 * @brief Get minimum clock frequency.
 *
 * Obtain the minimum clock rate a clock source can provide.
 * This is only valid once the clock source has been enabled.
 *
 * For more details on CCF functions, please check below file:
 *
 * In the Linux kernel: include/linux/clk.h
 * or
 * https://www.kernel.org/doc/htmldocs/kernel-api/
 *
 * @param[in] nv           Per gpu linux state
 * @param[in] whichClkOS   Enum value of the target clock
 * @param[out] pMinFreqKHz Minimum clock frequency
 */
NV_STATUS NV_API_CALL nv_get_min_freq(
    nv_state_t *nv, TEGRASOC_WHICH_CLK whichClkOS, NvU32 *pMinFreqKHz)
{
    nv_linux_state_t *nvl = NV_GET_NVL_FROM_NV_STATE(nv);
    NV_STATUS status      = NV_ERR_GENERIC;
    long ret;

    if (nvl->disp_clk_handles.clk[whichClkOS].handles != NULL)
    {
        //
        // clk_round_rate(struct clk *clk, rate);
        // rate is the minimum possible rate we give,
        // it returns rounded clock rate in Hz, i.e.,
        // minimum clock rate the source clock can
        // support or negative errno.
        // Here, rate = NV_S64_MAX
        // 0 < minFreq currFreq < maxFreq < NV_S64_MAX
        // clk_round_rate() round of and return the
        // nearest freq what a clock can provide.
        // sending 0 will return minFreq.
        //
        ret = clk_round_rate(nvl->disp_clk_handles.clk[whichClkOS].handles, 0);

        if (ret >= 0)
        {
            *pMinFreqKHz = (NvU32) (ret / 1000);
            status = NV_OK;
        }
        else
        {
            status = NV_ERR_FEATURE_NOT_ENABLED;
            nv_printf(NV_DBG_ERRORS, "NVRM: clk_round_rate failed with error: %ld\n", ret);
        }
    }
    else
    {
        status = NV_ERR_OBJECT_NOT_FOUND;
    }

    return status;
}


/*!
 * @brief set clock frequency.
 *
 * Setting the frequency of clock source.
 * This is only valid once the clock source has been enabled.
 *
 * For more details on CCF functions, please check below file:
 *
 * In the Linux kernel: include/linux/clk.h
 * or
 * https://www.kernel.org/doc/htmldocs/kernel-api/
 *
 * @param[in] nv           Per gpu linux state
 * @param[in] whichClkOS   Enum value of the target clock
 * @param[in] reqFreqKHz   Required frequency
 */
NV_STATUS NV_API_CALL nv_set_freq(
    nv_state_t *nv, TEGRASOC_WHICH_CLK whichClkOS, NvU32 reqFreqKHz)
{
    nv_linux_state_t *nvl = NV_GET_NVL_FROM_NV_STATE(nv);
    NV_STATUS status      = NV_ERR_GENERIC;
    int ret;

    if (nvl->disp_clk_handles.clk[whichClkOS].handles != NULL)
    {
        ret = clk_set_rate(nvl->disp_clk_handles.clk[whichClkOS].handles,
                           reqFreqKHz * 1000U);
        if (ret == 0)
        {
            status = NV_OK;
        }
        else
        {
            status = NV_ERR_INVALID_REQUEST;
            nv_printf(NV_DBG_ERRORS, "NVRM: clk_set_rate failed with error: %d\n", ret);
        }
    }
    else
    {
        status = NV_ERR_OBJECT_NOT_FOUND;
    }

    return status;
}

/*!
 * @brief set parent clock.
 *
 * Setting the parent clock of clock source.
 * This is only valid once the clock source and the parent
 * clock have been enabled.
 *
 * For more details on CCF functions, please check below file:
 *
 * In the Linux kernel: include/linux/clk.h
 * or
 * https://www.kernel.org/doc/htmldocs/kernel-api/
 *
 * @param[in] nv                Per gpu linux state
 * @param[in] whichClkOSsource  Enum value of the source clock
 * @param[in] whichClkOSparent  Enum value of the parent clock
 */
NV_STATUS NV_API_CALL nv_set_parent
(
    nv_state_t *nv,
    TEGRASOC_WHICH_CLK whichClkOSsource,
    TEGRASOC_WHICH_CLK whichClkOSparent
)
{
    nv_linux_state_t *nvl = NV_GET_NVL_FROM_NV_STATE(nv);
    NV_STATUS status      = NV_ERR_GENERIC;
    int ret;

    if ((nvl->disp_clk_handles.clk[whichClkOSsource].handles != NULL) &&
        (nvl->disp_clk_handles.clk[whichClkOSparent].handles != NULL))
    {
        ret = clk_set_parent(nvl->disp_clk_handles.clk[whichClkOSsource].handles,
                             nvl->disp_clk_handles.clk[whichClkOSparent].handles);
        if (ret == 0)
        {
            status = NV_OK;
        }
        else
        {
            status = NV_ERR_INVALID_REQUEST;
            nv_printf(NV_DBG_ERRORS, "NVRM: clk_set_parent failed with error: %d\n", ret);
        }
    }
    else
    {
        status = NV_ERR_OBJECT_NOT_FOUND;
    }

    return status;
}

/*!
 * @brief get parent clock.
 *
 * Getting the parent clock of clock source.
 * This is only valid once the clock source and the parent
 * clock have been enabled.
 *
 * For more details on CCF functions, please check below file:
 *
 * In the Linux kernel: include/linux/clk.h
 * or
 * https://www.kernel.org/doc/htmldocs/kernel-api/
 *
 * @param[in] nv                 Per gpu linux state
 * @param[in] whichClkOSsource   Enum value of the source clock
 * @param[in] pWhichClkOSparent  Enum value of the parent clock
 */
NV_STATUS NV_API_CALL nv_get_parent
(
    nv_state_t          *nv,
    TEGRASOC_WHICH_CLK  whichClkOSsource,
    TEGRASOC_WHICH_CLK  *pWhichClkOSparent
)
{
    nv_linux_state_t *nvl = NV_GET_NVL_FROM_NV_STATE(nv);
    NV_STATUS status;
    struct clk *ret;;
    NvU32 i;

    if (nvl->disp_clk_handles.clk[whichClkOSsource].handles != NULL)
    {
        ret = clk_get_parent(nvl->disp_clk_handles.clk[whichClkOSsource].handles);
        if (!IS_ERR(ret))
        {
            const char *parentClkName = __clk_get_name(ret);
            //
            // TEGRASOC_WHICH_CLK_MAX is maximum clock defined in below enum
            // arch/nvalloc/unix/include/nv.h
            // enum TEGRASOC_WHICH_CLK
            //
            for (i = 0U; i < TEGRASOC_WHICH_CLK_MAX; i++)
            {
                if (!strcmp(nvl->disp_clk_handles.clk[i].clkName, parentClkName))
                {
                    *pWhichClkOSparent = i;
                    return NV_OK;
                }
            }
            nv_printf(NV_DBG_ERRORS, "NVRM: unexpected parent clock ref addr: %p\n", ret);
            return NV_ERR_INVALID_OBJECT_PARENT;
        }
        else
        {
            nv_printf(NV_DBG_ERRORS, "NVRM: clk_get_parent failed with error: %ld\n", PTR_ERR(ret));
            return NV_ERR_INVALID_POINTER;
        }
    }

    nv_printf(NV_DBG_ERRORS, "NVRM: invalid source clock requested\n");
    return NV_ERR_OBJECT_NOT_FOUND;
}
