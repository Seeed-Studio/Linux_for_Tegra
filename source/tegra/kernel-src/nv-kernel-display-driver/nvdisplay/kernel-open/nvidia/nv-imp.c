/*
 * SPDX-FileCopyrightText: Copyright (c) 2020-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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



#if defined(NV_SOC_TEGRA_TEGRA_BPMP_H_PRESENT) || IS_ENABLED(CONFIG_TEGRA_BPMP)
#include <soc/tegra/bpmp-abi.h>
#endif

#if IS_ENABLED(CONFIG_TEGRA_BPMP)
#include <soc/tegra/bpmp.h>
#elif defined(NV_SOC_TEGRA_TEGRA_BPMP_H_PRESENT)
#include <soc/tegra/tegra_bpmp.h>
#endif  // IS_ENABLED(CONFIG_TEGRA_BPMP)

#if defined NV_DT_BINDINGS_INTERCONNECT_TEGRA_ICC_ID_H_PRESENT
#include <dt-bindings/interconnect/tegra_icc_id.h>
#endif

#ifdef NV_LINUX_PLATFORM_TEGRA_MC_UTILS_H_PRESENT
#include <linux/platform/tegra/mc_utils.h>
#endif

//
// IMP requires information from various BPMP and MC driver functions.  The
// macro below checks that all of the required functions are present.
//
#define IMP_SUPPORT_FUNCTIONS_PRESENT \
    NV_IS_EXPORT_SYMBOL_PRESENT_dram_clk_to_mc_clk && \
    NV_IS_EXPORT_SYMBOL_PRESENT_get_dram_num_channels && \
    NV_IS_EXPORT_SYMBOL_PRESENT_tegra_dram_types && \
    (defined(NV_SOC_TEGRA_TEGRA_BPMP_H_PRESENT) || \
     IS_ENABLED(CONFIG_TEGRA_BPMP)) && \
    defined(NV_LINUX_PLATFORM_TEGRA_MC_UTILS_H_PRESENT)

//
// Also create a macro to check if all the required ICC symbols are present.
// DT endpoints are defined in dt-bindings/interconnect/tegra_icc_id.h.
//
#define ICC_SUPPORT_FUNCTIONS_PRESENT \
    defined(NV_DT_BINDINGS_INTERCONNECT_TEGRA_ICC_ID_H_PRESENT)

#if IMP_SUPPORT_FUNCTIONS_PRESENT
    static struct mrq_emc_dvfs_latency_response latency_table;
    static struct mrq_emc_dvfs_emchub_response emchub_table;
    static struct cmd_iso_client_get_max_bw_response max_bw_table;



/*!
 * @brief Converts the MC driver dram type to RM format
 * 
 * The MC driver's tegra_dram_types() function returns the dram type as an
 * enum.  We convert it to an NvU32 for better ABI compatibility when stored in
 * the TEGRA_IMP_IMPORT_DATA structure, which is shared between various
 * software components.
 * 
 * @param[in]  dram_type    Dram type (DRAM_TYPE_LPDDRxxx format).
 * 
 * @returns dram type (TEGRA_IMP_IMPORT_DATA_DRAM_TYPE_LPDDRxxx format).
 */
static inline NvU32
nv_imp_convert_dram_type_to_rm_format
(
    enum dram_types  dram_type
)
{
    NvU32   rm_dram_type;

    switch (dram_type)
    {
        case DRAM_TYPE_LPDDR4_16CH_ECC_1RANK:
        case DRAM_TYPE_LPDDR4_16CH_ECC_2RANK:
        case DRAM_TYPE_LPDDR4_8CH_ECC_1RANK:
        case DRAM_TYPE_LPDDR4_8CH_ECC_2RANK:
        case DRAM_TYPE_LPDDR4_4CH_ECC_1RANK:
        case DRAM_TYPE_LPDDR4_4CH_ECC_2RANK:
        case DRAM_TYPE_LPDDR4_16CH_1RANK:
        case DRAM_TYPE_LPDDR4_16CH_2RANK:
        case DRAM_TYPE_LPDDR4_8CH_1RANK:
        case DRAM_TYPE_LPDDR4_8CH_2RANK:
        case DRAM_TYPE_LPDDR4_4CH_1RANK:
        case DRAM_TYPE_LPDDR4_4CH_2RANK:
            rm_dram_type = TEGRA_IMP_IMPORT_DATA_DRAM_TYPE_LPDDR4;
            break;
        case DRAM_TYPE_LPDDR5_16CH_ECC_1RANK:
        case DRAM_TYPE_LPDDR5_16CH_ECC_2RANK:
        case DRAM_TYPE_LPDDR5_8CH_ECC_1RANK:
        case DRAM_TYPE_LPDDR5_8CH_ECC_2RANK:
        case DRAM_TYPE_LPDDR5_4CH_ECC_1RANK:
        case DRAM_TYPE_LPDDR5_4CH_ECC_2RANK:
        case DRAM_TYPE_LPDDR5_16CH_1RANK:
        case DRAM_TYPE_LPDDR5_16CH_2RANK:
        case DRAM_TYPE_LPDDR5_8CH_1RANK:
        case DRAM_TYPE_LPDDR5_8CH_2RANK:
        case DRAM_TYPE_LPDDR5_4CH_1RANK:
        case DRAM_TYPE_LPDDR5_4CH_2RANK:
            rm_dram_type = TEGRA_IMP_IMPORT_DATA_DRAM_TYPE_LPDDR5;
            break;
        default:
            rm_dram_type = TEGRA_IMP_IMPORT_DATA_DRAM_TYPE_UNKNOWN;
            break;
    }

    return rm_dram_type;
}
#endif  // IMP_SUPPORT_FUNCTIONS_PRESENT

/*!
 * @brief Collects IMP-relevant BPMP data and saves for later
 *
 * @param[in]   nvl     OS-specific device state
 *
 * @returns NV_OK if successful,
 *          NV_ERR_GENERIC if the BPMP API returns an error,
 *          NV_ERR_MISSING_TABLE_ENTRY if the latency table has no entries,
 *          NV_ERR_INVALID_DATA if the number of clock entries in the latency
 *            table does not match the number of entries in the emchub table, or
 *          NV_ERR_NOT_SUPPORTED if the functionality is not available.
 */
NV_STATUS
nv_imp_get_bpmp_data
(
    nv_linux_state_t *nvl
)
{
#if IMP_SUPPORT_FUNCTIONS_PRESENT
    NV_STATUS   status = NV_OK;
    int         rc;
    int         i;
    NvBool      bApiTableInvalid = NV_FALSE;
    static const struct iso_max_bw dummy_iso_bw_pairs[] =
        { {  204000U,  1472000U },
          {  533000U,  3520000U },
          {  665000U,  4352000U },
          {  800000U,  5184000U },
          { 1066000U,  6784000U },
          { 1375000U,  8704000U },
          { 1600000U, 10112000U },
          { 1866000U, 11712000U },
          { 2133000U, 13376000U },
          { 2400000U, 15040000U },
          { 2750000U, 17152000U },
          { 3000000U, 18688000U },
          { 3200000U, 20800000U }
        };
#if IS_ENABLED(CONFIG_TEGRA_BPMP)
    struct tegra_bpmp *bpmp;
    struct tegra_bpmp_message msg;
    struct mrq_iso_client_request iso_client_request;

    bpmp = tegra_bpmp_get(nvl->dev);
    if (IS_ERR(bpmp))
    {
        nv_printf(NV_DBG_ERRORS,
                  "NVRM:  Error getting bpmp struct: %s\n",
                  PTR_ERR(bpmp));
        return NV_ERR_GENERIC;
    }
    // Get the table of dramclk / DVFS latency pairs.
    memset(&msg, 0, sizeof(msg));
    msg.mrq = MRQ_EMC_DVFS_LATENCY;
    msg.tx.data = NULL;
    msg.tx.size = 0;
    msg.rx.data = &latency_table;
    msg.rx.size = sizeof(latency_table);

    rc = tegra_bpmp_transfer(bpmp, &msg);
#else
    // Get the table of dramclk / DVFS latency pairs.
    rc = tegra_bpmp_send_receive(MRQ_EMC_DVFS_LATENCY,
                                 NULL,
                                 0,
                                 &latency_table,
                                 sizeof(latency_table));
#endif
    if (rc != 0)
    {
        nv_printf(NV_DBG_ERRORS,
                  "MRQ_EMC_DVFS_LATENCY returns error code %d\n", rc);
        status = NV_ERR_GENERIC;
        goto Cleanup;
    }

    nv_printf(NV_DBG_INFO,
              "MRQ_EMC_DVFS_LATENCY table size = %u\n",
              latency_table.num_pairs);

    if (latency_table.num_pairs == 0U)
    {
        nv_printf(NV_DBG_ERRORS,
                  "MRQ_EMC_DVFS_LATENCY table has no entries\n", rc);
        status = NV_ERR_MISSING_TABLE_ENTRY;
        goto Cleanup;
    }

    // Get the table of dramclk / emchubclk pairs.
#if IS_ENABLED(CONFIG_TEGRA_BPMP)
    memset(&msg, 0, sizeof(msg));
    msg.mrq = MRQ_EMC_DVFS_EMCHUB;
    msg.tx.data = NULL;
    msg.tx.size = 0;
    msg.rx.data = &emchub_table;
    msg.rx.size = sizeof(emchub_table);

    rc = tegra_bpmp_transfer(bpmp, &msg);
#else
    rc = tegra_bpmp_send_receive(MRQ_EMC_DVFS_EMCHUB,
                                 NULL,
                                 0,
                                 &emchub_table,
                                 sizeof(emchub_table));
#endif
    if (rc != 0)
    {
        nv_printf(NV_DBG_ERRORS,
                  "MRQ_EMC_DVFS_EMCHUB returns error code %d\n", rc);
        status = NV_ERR_GENERIC;
        goto Cleanup;
    }

    nv_printf(NV_DBG_INFO,
              "MRQ_EMC_DVFS_EMCHUB table size = %u\n",
              emchub_table.num_pairs);

    if (latency_table.num_pairs != emchub_table.num_pairs)
    {
        nv_printf(NV_DBG_ERRORS,
                  "MRQ_EMC_DVFS_LATENCY table size (%u) does not match MRQ_EMC_DVFS_EMCHUB table size (%u)\n",
                  latency_table.num_pairs,
                  emchub_table.num_pairs);
        status = NV_ERR_INVALID_DATA;
        goto Cleanup;
    }

    // Get the table of dramclk / max ISO BW pairs.
#if IS_ENABLED(CONFIG_TEGRA_BPMP)
    memset(&iso_client_request, 0, sizeof(iso_client_request));
    iso_client_request.cmd = CMD_ISO_CLIENT_GET_MAX_BW;
    iso_client_request.max_isobw_req.id = TEGRA_ICC_DISPLAY;
    msg.mrq = MRQ_ISO_CLIENT;
    msg.tx.data = &iso_client_request;
    msg.tx.size = sizeof(iso_client_request);
    msg.rx.data = &max_bw_table;
    msg.rx.size = sizeof(max_bw_table);

    rc = tegra_bpmp_transfer(bpmp, &msg);
#else
    // Maybe we don't need the old implementation "else" clause cases anymore.
    NV_ASSERT(NV_FALSE);
#endif
    if ((rc != 0) || (max_bw_table.num_pairs == 0U))
    {
        if (rc != 0)
        {
            nv_printf(NV_DBG_ERRORS,
                      "MRQ_ISO_CLIENT returns error code %d\n", rc);
        }
        else
        {
            nv_printf(NV_DBG_ERRORS,
                      "CMD_ISO_CLIENT_GET_MAX_BW table does not contain any entries\n");
        }
        bApiTableInvalid = NV_TRUE;
    }
    else
    {
        //
        // Check for entries with ISO BW = 0.  It's possible that one entry may
        // be zero, but they should not all be zero.  (On simulation, due to bug
        // 3379796, the API is currently not working; it returns 13 entries,
        // each with ISO BW = 0.)
        //
        bApiTableInvalid = NV_TRUE;
        for (i = 0; i < max_bw_table.num_pairs; i++)
        {
            if (max_bw_table.pairs[i].iso_bw != 0U)
            {
                bApiTableInvalid = NV_FALSE;
                break;
            }
        }
    }
    if (bApiTableInvalid)
    {
        //
        // If the table is not returned correctly, for now, fill in a dummy
        // table.
        //
        nv_printf(NV_DBG_ERRORS,
                  "Creating dummy CMD_ISO_CLIENT_GET_MAX_BW table\n");
        max_bw_table.num_pairs = sizeof(dummy_iso_bw_pairs) /
                                 sizeof(dummy_iso_bw_pairs[0]);
        for (i = 0; i < max_bw_table.num_pairs; i++)
        {
            max_bw_table.pairs[i].freq = dummy_iso_bw_pairs[i].freq;
            max_bw_table.pairs[i].iso_bw = dummy_iso_bw_pairs[i].iso_bw;
        }
    }
    nv_printf(NV_DBG_INFO,
              "CMD_ISO_CLIENT_GET_MAX_BW table size = %u\n",
              max_bw_table.num_pairs);

Cleanup:
#if IS_ENABLED(CONFIG_TEGRA_BPMP)
    tegra_bpmp_put(bpmp);
#endif
    return status;
#else   // IMP_SUPPORT_FUNCTIONS_PRESENT
    return NV_ERR_NOT_SUPPORTED;
#endif
}

/*!
 * @brief Returns IMP-relevant data collected from other modules
 *
 * @param[out]  tegra_imp_import_data   Structure to receive the data
 *
 * @returns NV_OK if successful,
 *          NV_ERR_BUFFER_TOO_SMALL if the array in TEGRA_IMP_IMPORT_DATA is
 *            too small,
 *          NV_ERR_INVALID_DATA if the latency table has different mclk
 *            frequencies, compared with the emchub table, or
 *          NV_ERR_NOT_SUPPORTED if the functionality is not available.
 */
NV_STATUS NV_API_CALL
nv_imp_get_import_data
(
    TEGRA_IMP_IMPORT_DATA *tegra_imp_import_data
)
{
#if IMP_SUPPORT_FUNCTIONS_PRESENT
    NvU32       i;
    NvU32       bwTableIndex = 0U;
    NvU32       dram_clk_freq_khz;
    enum dram_types  dram_type;

    tegra_imp_import_data->num_dram_clk_entries = latency_table.num_pairs;
    if (ARRAY_SIZE(tegra_imp_import_data->dram_clk_instance) <
        latency_table.num_pairs)
    {
        nv_printf(NV_DBG_ERRORS,
                  "ERROR: TEGRA_IMP_IMPORT_DATA struct needs to have at least "
                  "%d dram_clk_instance entries, but only %d are allocated\n",
                  latency_table.num_pairs,
                  ARRAY_SIZE(tegra_imp_import_data->dram_clk_instance));
        return NV_ERR_BUFFER_TOO_SMALL;
    }

    //
    // Copy data that we collected earlier in the BPMP tables into the caller's
    // IMP import structure.
    //
    for (i = 0U; i < latency_table.num_pairs; i++)
    {
        dram_clk_freq_khz = latency_table.pairs[i].freq;
        //
        // For each dramclk frequency, we get some information from the EMCHUB
        // table and some information from the LATENCY table.  We expect both
        // tables to have entries for the same dramclk frequencies.
        //
        if (dram_clk_freq_khz != emchub_table.pairs[i].freq)
        {
            nv_printf(NV_DBG_ERRORS,
                      "MRQ_EMC_DVFS_LATENCY index #%d dramclk freq (%d KHz) does not match "
                      "MRQ_EMC_DVFS_EMCHUB index #%d dramclk freq (%d KHz)\n",
                      i, latency_table.pairs[i].freq,
                      i, emchub_table.pairs[i].freq);
            return NV_ERR_INVALID_DATA;
        }

        // Copy a few values to the caller's table.
        tegra_imp_import_data->dram_clk_instance[i].dram_clk_freq_khz =
            dram_clk_freq_khz;
        tegra_imp_import_data->dram_clk_instance[i].switch_latency_ns =
            latency_table.pairs[i].latency;
        tegra_imp_import_data->dram_clk_instance[i].mc_clk_khz =
            dram_clk_to_mc_clk(dram_clk_freq_khz / 1000U) * 1000U;

        // MC hubclk is 1/2 of scf clk, which is the same as EMCHUB clk.
        tegra_imp_import_data->dram_clk_instance[i].mchub_clk_khz =
            emchub_table.pairs[i].hub_freq / 2U;

        //
        // The ISO BW table may have more entries then the number of dramclk
        // frequencies supported on current chip (i.e., more entries than we
        // have in the EMCHUB and LATENCY tables).  For each dramclk entry that
        // we are filling out, search through the ISO BW table to find the
        // largest dramclk less than or equal to the dramclk frequency for
        // index "i", and use that ISO BW entry.  (We assume all tables have
        // their entries in order of increasing dramclk frequency.)
        // 
        // Note: Some of the dramclk frequencies in the ISO BW table have been
        // observed to be "rounded down" (e.g., 665000 KHz instead of 665600
        // KHz).
        //
        while ((bwTableIndex + 1U < max_bw_table.num_pairs) &&
               (dram_clk_freq_khz >= max_bw_table.pairs[bwTableIndex + 1U].freq))
        {
            nv_printf(NV_DBG_INFO,
                      "Max ISO BW table: index %u, dramclk = %u KHz, max ISO BW = %u KB/sec\n",
                      bwTableIndex,
                      max_bw_table.pairs[bwTableIndex].freq,
                      max_bw_table.pairs[bwTableIndex].iso_bw);
            bwTableIndex++;
        }
        if (dram_clk_freq_khz >= max_bw_table.pairs[bwTableIndex].freq)
        {
            nv_printf(NV_DBG_INFO,
                      "For dramclk = %u KHz, setting max ISO BW = %u KB/sec\n",
                      dram_clk_freq_khz,
                      max_bw_table.pairs[bwTableIndex].iso_bw);
            tegra_imp_import_data->dram_clk_instance[i].max_iso_bw_kbps =
                max_bw_table.pairs[bwTableIndex].iso_bw;
        }
        else
        {
            //
            // Something went wrong.  Maybe the ISO BW table doesn't have any
            // entries with dramclk frequency as small as the frequency in the
            // EMCHUB and LATENCY tables, or maybe the entries are out of
            // order.
            //
            nv_printf(NV_DBG_ERRORS,
                      "Couldn't get max ISO BW for dramclk = %u KHz\n",
                      dram_clk_freq_khz);
            return NV_ERR_INVALID_DATA;
        }
    }

    dram_type = tegra_dram_types();

    tegra_imp_import_data->dram_type =
        nv_imp_convert_dram_type_to_rm_format(dram_type);

    tegra_imp_import_data->num_dram_channels = get_dram_num_channels();

    // Record the overall maximum possible ISO BW.
    i = latency_table.num_pairs - 1U;
    tegra_imp_import_data->max_iso_bw_kbps =
        tegra_imp_import_data->dram_clk_instance[i].max_iso_bw_kbps;

    return NV_OK;
#else   // IMP_SUPPORT_FUNCTIONS_PRESENT
    return NV_ERR_NOT_SUPPORTED;
#endif
}

/*!
 * @brief Tells BPMP whether or not RFL is valid
 * 
 * Display HW generates an ok_to_switch signal which asserts when mempool
 * occupancy is high enough to be able to turn off memory long enough to
 * execute a dramclk frequency switch without underflowing display output.
 * ok_to_switch drives the RFL ("request for latency") signal in the memory
 * unit, and the switch sequencer waits for this signal to go active before
 * starting a dramclk switch.  However, if the signal is not valid (e.g., if
 * display HW or SW has not been initialized yet), the switch sequencer ignores
 * the signal.  This API tells BPMP whether or not the signal is valid.
 *
 * @param[in] nv        Per GPU Linux state
 * @param[in] bEnable   True if RFL will be valid; false if invalid
 *
 * @returns NV_OK if successful,
 *          NV_ERR_NOT_SUPPORTED if the functionality is not available, or
 *          NV_ERR_GENERIC if some other kind of error occurred.
 */
NV_STATUS NV_API_CALL
nv_imp_enable_disable_rfl
(
    nv_state_t *nv,
    NvBool bEnable
)
{
    NV_STATUS status = NV_ERR_NOT_SUPPORTED;
#if IMP_SUPPORT_FUNCTIONS_PRESENT
#if IS_ENABLED(CONFIG_TEGRA_BPMP)
    nv_linux_state_t *nvl = NV_GET_NVL_FROM_NV_STATE(nv);
    struct tegra_bpmp *bpmp = tegra_bpmp_get(nvl->dev);
    struct tegra_bpmp_message msg;
    struct mrq_emc_disp_rfl_request emc_disp_rfl_request;
    int rc;

    memset(&emc_disp_rfl_request, 0, sizeof(emc_disp_rfl_request));
    emc_disp_rfl_request.mode = bEnable ? EMC_DISP_RFL_MODE_ENABLED :
                                          EMC_DISP_RFL_MODE_DISABLED;
    msg.mrq = MRQ_EMC_DISP_RFL;
    msg.tx.data = &emc_disp_rfl_request;
    msg.tx.size = sizeof(emc_disp_rfl_request);
    msg.rx.data = NULL;
    msg.rx.size = 0;

    rc = tegra_bpmp_transfer(bpmp, &msg);
    if (rc == 0)
    {
        nv_printf(NV_DBG_INFO,
                  "\"Wait for RFL\" is %s via MRQ_EMC_DISP_RFL\n",
                  bEnable ? "enabled" : "disabled");
        status = NV_OK;
    }
    else
    {
        nv_printf(NV_DBG_ERRORS,
                  "MRQ_EMC_DISP_RFL failed to %s \"Wait for RFL\" (error code = %d)\n",
                  bEnable ? "enable" : "disable",
                  rc);
        status = NV_ERR_GENERIC;
    }
#else
    // Maybe we don't need the old implementation "else" clause cases anymore.
    NV_ASSERT(NV_FALSE);
#endif
#endif
    return status;
}

/*! 
 * @brief Obtains a handle for the display data path
 * 
 * If a handle is obtained successfully, it is not returned to the caller; it
 * is saved for later use by subsequent nv_imp_icc_set_bw calls.
 * nv_imp_icc_get must be called prior to calling nv_imp_icc_set_bw.
 *
 * @param[out] nv   Per GPU Linux state
 *
 * @returns NV_OK if successful,
 *          NV_ERR_NOT_SUPPORTED if the functionality is not available, or
 *          NV_ERR_GENERIC if some other error occurred.
 */
NV_STATUS NV_API_CALL
nv_imp_icc_get
(
    nv_state_t *nv
)
{
#if ICC_SUPPORT_FUNCTIONS_PRESENT
    nv_linux_state_t *nvl = NV_GET_NVL_FROM_NV_STATE(nv);
    NV_STATUS status = NV_OK;

#if defined(NV_ICC_GET_PRESENT)
    struct device_node *np;
    nvl->nv_imp_icc_path = NULL;
    // Check if ICC is present in the device tree, and enabled.
    np = of_find_node_by_path("/icc");
    if (np != NULL)
    {
        if (of_device_is_available(np))
        {
            // Get the ICC data path.
            nvl->nv_imp_icc_path =
                icc_get(nvl->dev, TEGRA_ICC_DISPLAY, TEGRA_ICC_PRIMARY);
        }
        of_node_put(np);
    }
#else
    nv_printf(NV_DBG_ERRORS, "NVRM: icc_get() not present\n");
    return NV_ERR_NOT_SUPPORTED;
#endif

    if (nvl->nv_imp_icc_path == NULL)
    {
        nv_printf(NV_DBG_INFO, "NVRM: icc_get disabled\n");
        status = NV_ERR_NOT_SUPPORTED;
    }
    else if (IS_ERR(nvl->nv_imp_icc_path))
    {
        nv_printf(NV_DBG_ERRORS, "NVRM: invalid path = %s\n",
                  PTR_ERR(nvl->nv_imp_icc_path));
        nvl->nv_imp_icc_path = NULL;
        status = NV_ERR_GENERIC;
    }
    return status;
#else
    return NV_ERR_NOT_SUPPORTED;
#endif
}

/*!
 * @brief Releases the handle obtained by nv_imp_icc_get
 * 
 * @param[in] nv    Per GPU Linux state
 */
void
nv_imp_icc_put
(
    nv_state_t *nv
)
{
#if ICC_SUPPORT_FUNCTIONS_PRESENT
    nv_linux_state_t *nvl = NV_GET_NVL_FROM_NV_STATE(nv);
#if defined(NV_ICC_PUT_PRESENT)
    if (nvl->nv_imp_icc_path != NULL)
    {
        icc_put(nvl->nv_imp_icc_path);
    }
#else
    nv_printf(NV_DBG_ERRORS, "icc_put() not present\n");
#endif
    nvl->nv_imp_icc_path = NULL;
#endif
}

/*!
 * @brief Allocates a specified amount of ISO memory bandwidth for display
 * 
 * floor_bw_kbps is the minimum required (i.e., floor) dramclk frequency
 * multiplied by the width of the pipe over which the display data will travel.
 * (It is understood that the bandwidth calculated by multiplying the clock
 * frequency by the pipe width will not be realistically achievable, due to
 * overhead in the memory subsystem.  ICC will not actually use the bandwidth
 * value, except to reverse the calculation to get the required dramclk
 * frequency.)
 *
 * nv_imp_icc_get must be called prior to calling this function.
 *
 * @param[in]   nv              Per GPU Linux state
 * @param[in]   avg_bw_kbps     Amount of ISO memory bandwidth requested
 * @param[in]   floor_bw_kbps   Min required dramclk freq * pipe width
 *
 * @returns NV_OK if successful,
 *          NV_ERR_INSUFFICIENT_RESOURCES if one of the bandwidth values is too
 *            high, and bandwidth cannot be allocated,
 *          NV_ERR_NOT_SUPPORTED if the functionality is not available, or
 *          NV_ERR_GENERIC if some other kind of error occurred.
 */
NV_STATUS NV_API_CALL
nv_imp_icc_set_bw
(
    nv_state_t *nv,
    NvU32       avg_bw_kbps,
    NvU32       floor_bw_kbps
)
{
#if ICC_SUPPORT_FUNCTIONS_PRESENT
    nv_linux_state_t *nvl = NV_GET_NVL_FROM_NV_STATE(nv);
    int rc;
    NV_STATUS status = NV_OK;
 
    //
    // avg_bw_kbps can be either ISO bw request or NISO bw request.
    // Use floor_bw_kbps to make floor requests.
    //
#if defined(NV_ICC_SET_BW_PRESENT)
    //
    // nv_imp_icc_path will be NULL on AV + L systems because ICC is disabled.
    // In this case, skip the allocation call, and just return a success
    // status.
    //
    if (nvl->nv_imp_icc_path == NULL)
    {
        return NV_OK;
    }
    rc = icc_set_bw(nvl->nv_imp_icc_path, avg_bw_kbps, floor_bw_kbps);
#else
    nv_printf(NV_DBG_ERRORS, "icc_set_bw() not present\n");
    return NV_ERR_NOT_SUPPORTED;
#endif

    if (rc < 0)
    {
        // A negative return value indicates an error.
        if (rc == -ENOMEM)
        {
            status = NV_ERR_INSUFFICIENT_RESOURCES;
        }
        else
        {
            status = NV_ERR_GENERIC;
        }
    }
    return status;
#else
    return NV_ERR_NOT_SUPPORTED;
#endif
}


