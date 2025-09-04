/*
 * SPDX-FileCopyrightText: Copyright (c) 1999-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

/**************************************************************************************************************
*
*   Description:
*       UNIX-general, device-independent initialization code for
*       the resource manager.
*
*
**************************************************************************************************************/

#include <nv_ref.h>
#include <nv.h>                 // NV device driver interface
#include <nv-reg.h>
#include <nv-priv.h>
#include <nvos.h>
#include <nvrm_registry.h>
#include <platform/sli/sli.h>
#include <nverror.h>
#include <osapi.h>
#include <core/system.h>
#include <os/os.h>
#include "gpu/gpu.h"
#include <objtmr.h>
#include "nverror.h"
#include <gpu/mem_mgr/mem_mgr.h>
#include <mem_mgr/io_vaspace.h>
#include <gpu/disp/kern_disp.h>

#include <mem_mgr/virt_mem_mgr.h>

#include <rmosxfac.h>
#include <gpu_mgr/gpu_mgr.h>
#include <core/thread_state.h>
#include <core/locks.h>
#include <rmapi/client.h>
#include <ctrl/ctrl2080/ctrl2080gpu.h>
#include <class/cl00f2.h>

#include "gpu_mgr/gpu_db.h"
#include <class/cl0080.h>
#include <class/cl0073.h>
#include <class/cl2080.h>
#include <class/cl402c.h>

#include <gpu/dce_client/dce_client.h>
// RMCONFIG: need definition of REGISTER_ALL_HALS()
#include "g_hal_register.h"

typedef enum
{
   RM_INIT_OK,

   /* general os errors */
   RM_INIT_REG_SETUP_FAILED            =  0x10,
   RM_INIT_SYS_ENVIRONMENT_FAILED,

   /* gpu errors */
   RM_INIT_GPU_GPUMGR_ALLOC_GPU_FAILED =  0x20,
   RM_INIT_GPU_GPUMGR_CREATE_DEV_FAILED,
   RM_INIT_GPU_GPUMGR_ATTACH_GPU_FAILED,
   RM_INIT_GPU_PRE_INIT_FAILED,
   RM_INIT_GPU_STATE_INIT_FAILED,
   RM_INIT_GPU_LOAD_FAILED,
   RM_INIT_GPU_UNIVERSAL_VALIDATION_FAILED,
   RM_INIT_GPU_DMA_CONFIGURATION_FAILED,

   /* vbios errors */
   RM_INIT_VBIOS_FAILED                =  0x30,
   RM_INIT_VBIOS_POST_FAILED,
   RM_INIT_VBIOS_X86EMU_FAILED,

   /* scalability errors */
   RM_INIT_SCALABILITY_FAILED          =  0x40,

   /* general core rm errors */
   RM_INIT_WATCHDOG_FAILED,
   RM_FIFO_GET_UD_BAR1_MAP_INFO_FAILED,
   RM_GPUDB_REGISTER_FAILED,

   RM_INIT_ALLOC_RMAPI_FAILED,
   RM_INIT_GPUINFO_WITH_RMAPI_FAILED,

   /* rm firmware errors */
   RM_INIT_FIRMWARE_POLICY_FAILED      = 0x60,
   RM_INIT_FIRMWARE_FETCH_FAILED,
   RM_INIT_FIRMWARE_VALIDATION_FAILED,
   RM_INIT_FIRMWARE_INIT_FAILED,

   RM_INIT_MAX_FAILURES
} rm_init_status;

typedef rm_init_status RM_INIT_STATUS;

typedef struct {
    RM_INIT_STATUS initStatus;
    NV_STATUS      rmStatus;
    NvU32          line;
} UNIX_STATUS;

#define INIT_UNIX_STATUS   { RM_INIT_OK, NV_OK, 0 }
#define RM_INIT_SUCCESS(init)  ((init) == RM_INIT_OK)

#define RM_SET_ERROR(status, err)  { (status).initStatus = (err); \
                                     (status).line = __LINE__; }


//
// GPU architectures support DMA addressing up to a certain address width,
// above which all other bits in any given DMA address must not vary
// (e.g., all 0). This value is the minimum of the DMA addressing
// capabilities, in number of physical address bits, for all supported
// GPU architectures.
//
#define NV_GPU_MIN_SUPPORTED_DMA_ADDR_WIDTH                36

static inline NvU64 nv_encode_pci_info(nv_pci_info_t *pci_info)
{
    return gpuEncodeDomainBusDevice(pci_info->domain, pci_info->bus, pci_info->slot);
}

static inline NvU32 nv_generate_id_from_pci_info(nv_pci_info_t *pci_info)
{
    return gpuGenerate32BitId(pci_info->domain, pci_info->bus, pci_info->slot);
}

static inline void nv_os_map_kernel_space(nv_state_t *nv, nv_aperture_t *aperture)
{
    NV_ASSERT(aperture->map == NULL);

    // let's start off assuming a standard device and map the registers
    // normally. It is unfortunate to hard-code the register size here, but we don't
    // want to fail trying to map all of a multi-devices' register space
    aperture->map = osMapKernelSpace(aperture->cpu_address,
                                     aperture->size,
                                     NV_MEMORY_UNCACHED,
                                     NV_PROTECT_READ_WRITE);
    aperture->map_u = (nv_phwreg_t)aperture->map;
}

// local prototypes
static void        initUnixSpecificRegistry(OBJGPU *);

NvBool osRmInitRm(OBJOS *pOS)
{
    OBJSYS    *pSys = SYS_GET_INSTANCE();
    NvU64 system_memory_size = (NvU64)-1;

    NV_PRINTF(LEVEL_INFO, "init rm\n");

    if (os_is_efi_enabled())
    {
        pSys->setProperty(pSys, PDB_PROP_SYS_IS_UEFI, NV_TRUE);
    }

    // have to init this before the debug subsystem, which will
    // try to check the value of ResmanDebugLevel
    RmInitRegistry();

    // init the debug subsystem if necessary
    os_dbg_init();
    nvDbgInitRmMsg(NULL);

    // Force nvlog reinit since module params are now available
    NVLOG_UPDATE();

    // Register all supported hals
    if (REGISTER_ALL_HALS() != NV_OK)
    {
        RmDestroyRegistry(NULL);
        return NV_FALSE;
    }

    system_memory_size = NV_RM_PAGES_PER_OS_PAGE * os_get_num_phys_pages();

    // if known, relay the number of system memory pages (in terms of RM page
    // size) to the RM; this is needed for e.g. TurboCache parts.
    if (system_memory_size != (NvU64)-1)
        pOS->SystemMemorySize = system_memory_size;

    // Setup any ThreadState defaults
    threadStateInitSetupFlags(THREAD_STATE_SETUP_FLAGS_ENABLED |
                              THREAD_STATE_SETUP_FLAGS_TIMEOUT_ENABLED |
                              THREAD_STATE_SETUP_FLAGS_SLI_LOGIC_ENABLED |
                              THREAD_STATE_SETUP_FLAGS_DO_NOT_INCLUDE_SLEEP_TIME_ENABLED);

    return NV_TRUE;
}

void RmShutdownRm(void)
{
    NV_PRINTF(LEVEL_INFO, "shutdown rm\n");

    RmDestroyRegistry(NULL);

    // Free objects created with RmInitRm, including the system object
    RmDestroyRm();
}

//
// osAttachGpu
//
// This routine is used as a callback by the gpumgrAttachGpu
// interface to allow os-dependent code to set up any state
// before engine construction begins.
//
NV_STATUS osAttachGpu(
    OBJGPU    *pGpu,
    void      *pOsGpuInfo
)
{
    nv_state_t *nv = (nv_state_t *)pOsGpuInfo;
    nv_priv_t  *nvp;

    nvp = NV_GET_NV_PRIV(nv);

    nvp->pGpu = pGpu;

    NV_SET_NV_STATE(pGpu, (void *)nv);

    initUnixSpecificRegistry(pGpu);

    // Assign default values to Registry keys for VGX
    if (os_is_vgx_hyper())
    {
        initVGXSpecificRegistry(pGpu);
    }

    return NV_OK;
}

NV_STATUS osDpcAttachGpu(
    OBJGPU    *pGpu,
    void      *pOsGpuInfo
)
{
    return NV_OK; // Nothing to do for unix
}

void osDpcDetachGpu(
    OBJGPU    *pGpu
)
{
    return; // Nothing to do for unix
}

NV_STATUS
osHandleGpuLost
(
    OBJGPU *pGpu
)
{
    nv_state_t *nv = NV_GET_NV_STATE(pGpu);
    nv_priv_t *nvp = NV_GET_NV_PRIV(nv);
    NvU32 pmc_boot_0;

    // Determine if we've already run the handler
    if (!pGpu->getProperty(pGpu, PDB_PROP_GPU_IS_CONNECTED))
    {
        return NV_OK;
    }

    pmc_boot_0 = NV_PRIV_REG_RD32(nv->regs->map_u, NV_PMC_BOOT_0);
    if (pmc_boot_0 != nvp->pmc_boot_0)
    {
        RM_API *pRmApi = rmapiGetInterface(RMAPI_GPU_LOCK_INTERNAL);
        NV2080_CTRL_GPU_GET_OEM_BOARD_INFO_PARAMS *pBoardInfoParams;
        NV_STATUS status;

        //
        // This doesn't support PEX Reset and Recovery yet.
        // This will help to prevent accessing registers of a GPU
        // which has fallen off the bus.
        //
        nvErrorLog_va((void *)pGpu, ROBUST_CHANNEL_GPU_HAS_FALLEN_OFF_THE_BUS,
                      "GPU has fallen off the bus.");

        NV_DEV_PRINTF(NV_DBG_ERRORS, nv, "GPU has fallen off the bus.\n");

        pBoardInfoParams = portMemAllocNonPaged(sizeof(*pBoardInfoParams));
        if (pBoardInfoParams != NULL)
        {
            portMemSet(pBoardInfoParams, 0, sizeof(*pBoardInfoParams));

            status = pRmApi->Control(pRmApi, nv->rmapi.hClient,
                                     nv->rmapi.hSubDevice,
                                     NV2080_CTRL_CMD_GPU_GET_OEM_BOARD_INFO,
                                     pBoardInfoParams,
                                     sizeof(*pBoardInfoParams));
            if (status == NV_OK)
            {
                NV_DEV_PRINTF(NV_DBG_ERRORS, nv,
                              "GPU serial number is %s.\n",
                              pBoardInfoParams->serialNumber);
            }

            portMemFree(pBoardInfoParams);
        }

        gpuSetDisconnectedProperties(pGpu);

        // Trigger the OS's PCI recovery mechanism
        if (nv_pci_trigger_recovery(nv) != NV_OK)
        {
            //
            // Initiate a crash dump immediately, since the OS doesn't appear
            // to have a mechanism wired up for attempted recovery.
            //
            (void) RmLogGpuCrash(pGpu);
        }
        else
        {
            //
            // Make the SW state stick around until the recovery can start, but
            // don't change the PDB property: this is only used to report to
            // clients whether or not persistence mode is enabled, and we'll
            // need it after the recovery callbacks to restore the correct
            // persistence mode for the GPU.
            //
            osModifyGpuSwStatePersistence(pGpu->pOsGpuInfo, NV_TRUE);
        }

        DBG_BREAKPOINT();
    }

    return NV_OK;
}

/*
 * Initialize the required GPU information by doing RMAPI control calls
 * and store the same in the UNIX specific data structures.
 */
static NV_STATUS
RmInitGpuInfoWithRmApi
(
    OBJGPU *pGpu
)
{
    RM_API     *pRmApi = rmapiGetInterface(RMAPI_GPU_LOCK_INTERNAL);
    nv_state_t *nv     = NV_GET_NV_STATE(pGpu);
    nv_priv_t  *nvp    = NV_GET_NV_PRIV(nv);
    NV2080_CTRL_GPU_GET_INFO_V2_PARAMS *pGpuInfoParams = { 0 };
    NV_STATUS   status;

    // LOCK: acquire GPUs lock
    status = rmGpuLocksAcquire(GPUS_LOCK_FLAGS_NONE, RM_LOCK_MODULES_INIT);
    if (status != NV_OK)
    {
        return status;
    }

    pGpuInfoParams = portMemAllocNonPaged(sizeof(*pGpuInfoParams));
    if (pGpuInfoParams == NULL)
    {
        // UNLOCK: release GPUs lock
        rmGpuLocksRelease(GPUS_LOCK_FLAGS_NONE, NULL);

        return NV_ERR_NO_MEMORY;
    }


    portMemSet(pGpuInfoParams, 0, sizeof(*pGpuInfoParams));

    pGpuInfoParams->gpuInfoListSize = 3;
    pGpuInfoParams->gpuInfoList[0].index = NV2080_CTRL_GPU_INFO_INDEX_4K_PAGE_ISOLATION_REQUIRED;
    pGpuInfoParams->gpuInfoList[1].index = NV2080_CTRL_GPU_INFO_INDEX_MOBILE_CONFIG_ENABLED;
    pGpuInfoParams->gpuInfoList[2].index = NV2080_CTRL_GPU_INFO_INDEX_DMABUF_CAPABILITY;

    status = pRmApi->Control(pRmApi, nv->rmapi.hClient,
                             nv->rmapi.hSubDevice,
                             NV2080_CTRL_CMD_GPU_GET_INFO_V2,
                             pGpuInfoParams, sizeof(*pGpuInfoParams));

    if (status == NV_OK)
    {
        nvp->b_4k_page_isolation_required =
            (pGpuInfoParams->gpuInfoList[0].data ==
             NV2080_CTRL_GPU_INFO_INDEX_4K_PAGE_ISOLATION_REQUIRED_YES);
        nvp->b_mobile_config_enabled =
            (pGpuInfoParams->gpuInfoList[1].data ==
             NV2080_CTRL_GPU_INFO_INDEX_MOBILE_CONFIG_ENABLED_YES);
        nv->dma_buf_supported =
            (pGpuInfoParams->gpuInfoList[2].data ==
             NV2080_CTRL_GPU_INFO_INDEX_DMABUF_CAPABILITY_YES);
    }

    portMemFree(pGpuInfoParams);

    // UNLOCK: release GPUs lock
    rmGpuLocksRelease(GPUS_LOCK_FLAGS_NONE, NULL);

    return status;
}

static void RmSetSocDispDeviceMappings(
    GPUATTACHARG *gpuAttachArg,
    nv_state_t *nv
)
{
    gpuAttachArg->socDeviceArgs.deviceMapping[SOC_DEV_MAPPING_DISP].gpuNvAddr = (GPUHWREG*) nv->regs->map;
    gpuAttachArg->socDeviceArgs.deviceMapping[SOC_DEV_MAPPING_DISP].gpuNvPAddr = nv->regs->cpu_address;
    gpuAttachArg->socDeviceArgs.deviceMapping[SOC_DEV_MAPPING_DISP].gpuNvLength = (NvU32) nv->regs->size;
}

static void RmSetSocDpauxDeviceMappings(
    GPUATTACHARG *gpuAttachArg,
    nv_state_t *nv
)
{
}

static void RmSetSocHdacodecDeviceMappings(
    GPUATTACHARG *gpuAttachArg,
    nv_state_t *nv
)
{
}

static void RmSetSocMipiCalDeviceMappings(
    GPUATTACHARG *gpuAttachArg,
    nv_state_t *nv
)
{
    gpuAttachArg->socDeviceArgs.deviceMapping[SOC_DEV_MAPPING_MIPICAL].gpuNvAddr = (GPUHWREG*) nv->mipical_regs->map;
    gpuAttachArg->socDeviceArgs.deviceMapping[SOC_DEV_MAPPING_MIPICAL].gpuNvPAddr = nv->mipical_regs->cpu_address;
    gpuAttachArg->socDeviceArgs.deviceMapping[SOC_DEV_MAPPING_MIPICAL].gpuNvLength = nv->mipical_regs->size;
}

static void
osInitNvMapping(
    nv_state_t *nv,
    NvU32 *pDeviceReference,
    UNIX_STATUS *status
)
{
    OBJGPU *pGpu;
    OBJSYS *pSys = SYS_GET_INSTANCE();
    GPUATTACHARG *gpuAttachArg;
    nv_priv_t *nvp = NV_GET_NV_PRIV(nv);
    NvU32 deviceInstance;
    NvU32 data = 0;

    NV_PRINTF(LEVEL_INFO, "osInitNvMapping:\n");

    // allocate the next available gpu device number
    status->rmStatus = gpumgrAllocGpuInstance(pDeviceReference);
    if (status->rmStatus != NV_OK)
    {
        NV_PRINTF(LEVEL_ERROR, "*** Cannot get valid gpu instance\n");
        RM_SET_ERROR(*status, RM_INIT_GPU_GPUMGR_ALLOC_GPU_FAILED);
        return;
    }

    // RM_BASIC_LOCK_MODEL: allocate GPU lock
    status->rmStatus = rmGpuLockAlloc(*pDeviceReference);
    if (status->rmStatus != NV_OK)
    {
        NV_PRINTF(LEVEL_ERROR, "*** cannot allocate GPU lock\n");
        RM_SET_ERROR(*status, RM_INIT_GPU_GPUMGR_ALLOC_GPU_FAILED);
        // RM_BASIC_LOCK_MODEL: free GPU lock
        rmGpuLockFree(*pDeviceReference);
        return;
    }

    // attach default single-entry broadcast device for this gpu
    status->rmStatus = gpumgrCreateDevice(&deviceInstance, NVBIT(*pDeviceReference), NULL);
    if (status->rmStatus != NV_OK)
    {
        NV_PRINTF(LEVEL_ERROR, "*** Cannot attach bc gpu\n");
        RM_SET_ERROR(*status, RM_INIT_GPU_GPUMGR_CREATE_DEV_FAILED);
        // RM_BASIC_LOCK_MODEL: free GPU lock
        rmGpuLockFree(*pDeviceReference);
        return;
    }

    // init attach state
    gpuAttachArg = portMemAllocNonPaged(sizeof(GPUATTACHARG));
    if (gpuAttachArg == NULL)
    {
        NV_PRINTF(LEVEL_ERROR, "*** Cannot allocate gpuAttachArg\n");
        RM_SET_ERROR(*status, RM_INIT_GPU_GPUMGR_ALLOC_GPU_FAILED);
        // RM_BASIC_LOCK_MODEL: free GPU lock
        rmGpuLockFree(*pDeviceReference);
        return;
    }

    portMemSet(gpuAttachArg, 0, sizeof(GPUATTACHARG));

    if (NV_IS_SOC_DISPLAY_DEVICE(nv))
    {
        gpuAttachArg->socDeviceArgs.specified = NV_TRUE;

        RmSetSocDispDeviceMappings(gpuAttachArg, nv);

        RmSetSocDpauxDeviceMappings(gpuAttachArg, nv);

        RmSetSocHdacodecDeviceMappings(gpuAttachArg, nv);

        RmSetSocMipiCalDeviceMappings(gpuAttachArg, nv);

        gpuAttachArg->socDeviceArgs.socChipId0 = nv->disp_sw_soc_chip_id;

        gpuAttachArg->socDeviceArgs.iovaspaceId = nv->iovaspace_id;
    }
    else
    {
        gpuAttachArg->fbPhysAddr      = nv->fb->cpu_address;
        gpuAttachArg->fbBaseAddr      = (GPUHWREG*) 0; // not mapped
        gpuAttachArg->devPhysAddr     = nv->regs->cpu_address;
        gpuAttachArg->regBaseAddr     = (GPUHWREG*) nv->regs->map;
        gpuAttachArg->intLine         = 0;             // don't know yet
        gpuAttachArg->instPhysAddr    = nv->bars[NV_GPU_BAR_INDEX_IMEM].cpu_address;
        gpuAttachArg->instBaseAddr    = (GPUHWREG*) 0; // not mapped

        gpuAttachArg->regLength       = nv->regs->size;
        gpuAttachArg->fbLength        = nv->fb->size;
        gpuAttachArg->instLength      = nv->bars[NV_GPU_BAR_INDEX_IMEM].size;

        gpuAttachArg->iovaspaceId     = nv->iovaspace_id;
    }

    //
    // we need this to check if we are running on virtual GPU
    // in gpuBindHal function later.
    //
    gpuAttachArg->nvDomainBusDeviceFunc = nv_encode_pci_info(&nv->pci_info);

    gpuAttachArg->bRequestFwClientRm = nv->request_fw_client_rm;

    gpuAttachArg->pOsAttachArg    = (void *)nv;

    // use gpu manager to attach gpu
    status->rmStatus = gpumgrAttachGpu(*pDeviceReference, gpuAttachArg);
    portMemFree(gpuAttachArg);
    if (status->rmStatus != NV_OK)
    {
        gpumgrDestroyDevice(deviceInstance);
        RM_SET_ERROR(*status, RM_INIT_GPU_GPUMGR_ATTACH_GPU_FAILED);
        NV_PRINTF(LEVEL_ERROR, "*** Cannot attach gpu\n");
        // RM_BASIC_LOCK_MODEL: free GPU lock
        rmGpuLockFree(*pDeviceReference);
        return;
    }
    nvp->flags |= NV_INIT_FLAG_GPUMGR_ATTACH;

    pGpu = gpumgrGetGpu(*pDeviceReference);

    sysInitRegistryOverrides(pSys);

    sysApplyLockingPolicy(pSys);

    pGpu->busInfo.IntLine = nv->interrupt_line;
    pGpu->dmaStartAddress = (RmPhysAddr)nv_get_dma_start_address(nv);
    if (nv->fb != NULL)
    {
        pGpu->registerAccess.gpuFbAddr = (GPUHWREG*) nv->fb->map;
        pGpu->busInfo.gpuPhysFbAddr = nv->fb->cpu_address;
    }

    // set default parent gpu
    gpumgrSetParentGPU(pGpu, pGpu);

    NV_PRINTF(LEVEL_INFO, "device instance          : %d\n", *pDeviceReference);
    NV_PRINTF(LEVEL_INFO, "NV regs using linear address  : 0x%p\n",
              pGpu->deviceMappings[SOC_DEV_MAPPING_DISP].gpuNvAddr);
    NV_PRINTF(LEVEL_INFO,
              "NV fb using linear address  : 0x%p\n", pGpu->registerAccess.gpuFbAddr);

    pGpu->setProperty(pGpu, PDB_PROP_GPU_ALTERNATE_TREE_ENABLED, NV_TRUE);
    pGpu->setProperty(pGpu, PDB_PROP_GPU_ALTERNATE_TREE_HANDLE_LOCKLESS, NV_FALSE);

    if (!os_is_vgx_hyper())
    {
        pGpu->setProperty(pGpu, PDB_PROP_GPU_ALLOW_PAGE_RETIREMENT, NV_TRUE);
    }

    if ((osReadRegistryDword(NULL,
                             NV_REG_PRESERVE_VIDEO_MEMORY_ALLOCATIONS,
                             &data) == NV_OK) && data)
    {

        nv->preserve_vidmem_allocations = NV_TRUE;
    }
}

static inline void
RmSetDeviceDmaAddressSize(
    nv_state_t *nv,
    NvU8 numDmaAddressBits
)
{
    nv_set_dma_address_size(nv, numDmaAddressBits);
}

static NV_STATUS
RmInitDeviceDma(
    nv_state_t *nv
)
{
    if (nv->iovaspace_id != NV_IOVA_DOMAIN_NONE)
    {
        OBJSYS *pSys = SYS_GET_INSTANCE();
        POBJVMM pVmm = SYS_GET_VMM(pSys);
        POBJVASPACE pIOVAS;
        NV_STATUS status = vmmCreateVaspace(pVmm, IO_VASPACE_A,
                                            nv->iovaspace_id, 0, 0ULL, ~0ULL,
                                            0ULL, 0ULL,
                                            NULL, VASPACE_FLAGS_ENABLE_VMM,
                                            &pIOVAS);
        if (status != NV_OK)
        {
            return status;
        }
    }

    return NV_OK;
}

static void
RmTeardownDeviceDma(
    nv_state_t *nv
)
{
    if (nv->iovaspace_id != NV_IOVA_DOMAIN_NONE)
    {
        OBJSYS *pSys = SYS_GET_INSTANCE();
        POBJVMM pVmm = SYS_GET_VMM(pSys);
        POBJVASPACE pIOVAS;

        if (NV_OK == vmmGetVaspaceFromId(pVmm, nv->iovaspace_id, IO_VASPACE_A, &pIOVAS))
        {
            vmmDestroyVaspace(pVmm, pIOVAS);
        }
    }
}

static void
RmInitNvDevice(
    NvU32 deviceReference,
    UNIX_STATUS *status
)
{
    // set the device context
    OBJGPU *pGpu = gpumgrGetGpu(deviceReference);
    nv_state_t *nv = NV_GET_NV_STATE(pGpu);
    nv_priv_t *nvp = NV_GET_NV_PRIV(nv);

    NV_PRINTF(LEVEL_INFO, "RmInitNvDevice:\n");

    NV_PRINTF(LEVEL_INFO,
              "device instance          : 0x%08x\n", deviceReference);

    // initialize all engines -- calls back osInitMapping()
    status->rmStatus = gpumgrStatePreInitGpu(pGpu);
    if (status->rmStatus != NV_OK)
    {
        NV_PRINTF(LEVEL_ERROR, "*** Cannot pre-initialize the device\n");
        RM_SET_ERROR(*status, RM_INIT_GPU_PRE_INIT_FAILED);
        return;
    }

    RmSetDeviceDmaAddressSize(nv, gpuGetPhysAddrWidth_HAL(pGpu, ADDR_SYSMEM));

    os_disable_console_access();

    status->rmStatus = gpumgrStateInitGpu(pGpu);
    if (status->rmStatus != NV_OK)
    {
        NV_PRINTF(LEVEL_ERROR, "*** Cannot initialize the device\n");
        RM_SET_ERROR(*status, RM_INIT_GPU_STATE_INIT_FAILED);
        os_enable_console_access();
        return;
    }
    nvp->flags |= NV_INIT_FLAG_GPU_STATE;

    status->rmStatus = gpumgrStateLoadGpu(pGpu, GPU_STATE_DEFAULT);
    if (status->rmStatus != NV_OK)
    {
        NV_PRINTF(LEVEL_ERROR,
                  "*** Cannot load state into the device\n");
        RM_SET_ERROR(*status, RM_INIT_GPU_LOAD_FAILED);
        os_enable_console_access();
        return;
    }
    nvp->flags |= NV_INIT_FLAG_GPU_STATE_LOAD;

    os_enable_console_access();

    status->rmStatus = gpuPerformUniversalValidation_HAL(pGpu);
    if (status->rmStatus != NV_OK)
    {
        NV_PRINTF(LEVEL_ERROR, "*** Failed universal validation\n");
        RM_SET_ERROR(*status, RM_INIT_GPU_UNIVERSAL_VALIDATION_FAILED);
        return;
    }

    return;
}

static void RmTeardownDpauxRegisters(
    nv_state_t *nv
)
{
}

static void RmTeardownHdacodecRegisters(
    nv_state_t *nv
)
{
}

static void RmTeardownMipiCalRegisters(
    nv_state_t *nv
)
{
    if (nv->mipical_regs && nv->mipical_regs->map)
    {
        osUnmapKernelSpace(nv->mipical_regs->map,
                                nv->mipical_regs->size);
        nv->mipical_regs->map = NULL;
    }
}

static NV_STATUS
RmTeardownRegisters(
    nv_state_t *nv
)
{
    NV_DEV_PRINTF(NV_DBG_SETUP, nv, "Tearing down registers\n");

    if (nv->regs && nv->regs->map)
    {
        osUnmapKernelSpace(nv->regs->map, nv->regs->size);
        nv->regs->map = 0;
        nv->regs->map_u = NULL;
    }

    RmTeardownDpauxRegisters(nv);

    RmTeardownHdacodecRegisters(nv);

    RmTeardownMipiCalRegisters(nv);

    return NV_OK;
}

static NV_STATUS
RmSetupDpauxRegisters(
    nv_state_t *nv,
    UNIX_STATUS *status
)
{

    return NV_OK;
}

static NV_STATUS
RmSetupHdacodecRegisters(
    nv_state_t *nv,
    UNIX_STATUS *status
)
{

    return NV_OK;
}

static NV_STATUS
RmSetupMipiCalRegisters(
    nv_state_t *nv,
    UNIX_STATUS *status
)
{
    if (nv->mipical_regs != NULL)
    {
        nv_os_map_kernel_space(nv, nv->mipical_regs);
        if (nv->mipical_regs->map == NULL)
        {
            NV_DEV_PRINTF(NV_DBG_ERRORS, nv, "Failed to map mipical registers!!\n");
            RM_SET_ERROR(*status, RM_INIT_REG_SETUP_FAILED);
            status->rmStatus   = NV_ERR_OPERATING_SYSTEM;
            return NV_ERR_GENERIC;
        }
    }

    if (nv->mipical_regs != NULL)
    {
        NV_DEV_PRINTF(NV_DBG_SETUP, nv, "    MIPICAL: " NvP64_fmt " " NvP64_fmt " 0x%p\n",
                nv->mipical_regs->cpu_address, nv->mipical_regs->size, nv->mipical_regs->map);
    }

    return NV_OK;
}

static void
RmSetupRegisters(
    nv_state_t *nv,
    UNIX_STATUS *status
)
{
    NV_STATUS ret;

    NV_DEV_PRINTF(NV_DBG_SETUP, nv, "RmSetupRegisters for 0x%x:0x%x\n",
              nv->pci_info.vendor_id, nv->pci_info.device_id);
    NV_DEV_PRINTF(NV_DBG_SETUP, nv, "pci config info:\n");
    NV_DEV_PRINTF(NV_DBG_SETUP, nv, "   registers look  like: " NvP64_fmt " " NvP64_fmt,
              nv->regs->cpu_address, nv->regs->size);

    if (nv->fb != NULL)
    {
        NV_DEV_PRINTF(NV_DBG_SETUP, nv, "   fb        looks like: " NvP64_fmt " " NvP64_fmt,
                nv->fb->cpu_address, nv->fb->size);
    }

    {
        nv_os_map_kernel_space(nv, nv->regs);
    }

    if (nv->regs->map == NULL)
    {
        NV_DEV_PRINTF(NV_DBG_ERRORS, nv, "Failed to map regs registers!!\n");
        RM_SET_ERROR(*status, RM_INIT_REG_SETUP_FAILED);
        status->rmStatus   = NV_ERR_OPERATING_SYSTEM;
        return;
    }
    NV_DEV_PRINTF(NV_DBG_SETUP, nv, "Successfully mapped framebuffer and registers\n");
    NV_DEV_PRINTF(NV_DBG_SETUP, nv, "final mappings:\n");
    NV_DEV_PRINTF(NV_DBG_SETUP, nv, "    regs: " NvP64_fmt " " NvP64_fmt " 0x%p\n",
              nv->regs->cpu_address, nv->regs->size, nv->regs->map);

    ret = RmSetupDpauxRegisters(nv, status);
    if (ret != NV_OK)
        goto err_unmap_disp_regs;

    ret = RmSetupHdacodecRegisters(nv, status);
    if (ret != NV_OK)
    {
        RmTeardownDpauxRegisters(nv);
        goto err_unmap_disp_regs;
    }

    ret = RmSetupMipiCalRegisters(nv, status);
    if (ret != NV_OK)
    {
        RmTeardownHdacodecRegisters(nv);
        RmTeardownDpauxRegisters(nv);
        goto err_unmap_disp_regs;
    }

    return;

err_unmap_disp_regs:
    if (nv->regs && nv->regs->map)
    {
        osUnmapKernelSpace(nv->regs->map, nv->regs->size);
        nv->regs->map = 0;
    }

    return;
}

NvBool RmInitPrivateState(
    nv_state_t *pNv
)
{
    nv_priv_t *nvp;
    NvU32 gpuId;
    NvU32 pmc_boot_0 = 0;
    NvU32 pmc_boot_42 = 0;

    NV_SET_NV_PRIV(pNv, NULL);

    if (!NV_IS_SOC_DISPLAY_DEVICE(pNv) && !NV_IS_SOC_IGPU_DEVICE(pNv))
    {
        pNv->regs->map_u = os_map_kernel_space(pNv->regs->cpu_address,
                                               os_page_size,
                                               NV_MEMORY_UNCACHED);
        if (pNv->regs->map_u == NULL)
        {
            NV_PRINTF(LEVEL_ERROR,
                      "failed to map GPU registers (DISABLE_INTERRUPTS).\n");
            return NV_FALSE;
        }

        pmc_boot_0 = NV_PRIV_REG_RD32(pNv->regs->map_u, NV_PMC_BOOT_0);
        pmc_boot_42 = NV_PRIV_REG_RD32(pNv->regs->map_u, NV_PMC_BOOT_42);

        os_unmap_kernel_space(pNv->regs->map_u, os_page_size);
        pNv->regs->map_u = NULL;
    }

    if (os_alloc_mem((void **)&nvp, sizeof(*nvp)) != NV_OK)
    {
        NV_PRINTF(LEVEL_ERROR,
                  "failed to allocate private device state.\n");
        return NV_FALSE;
    }

    gpuId = nv_generate_id_from_pci_info(&pNv->pci_info);

    if (gpumgrRegisterGpuId(gpuId, nv_encode_pci_info(&pNv->pci_info)) != NV_OK)
    {
        NV_PRINTF(LEVEL_ERROR,
                  "failed to register GPU with GPU manager.\n");
        os_free_mem(nvp);
        return NV_FALSE;
    }

    pNv->gpu_id = gpuId;

    pNv->iovaspace_id = nv_requires_dma_remap(pNv) ? gpuId :
                                                     NV_IOVA_DOMAIN_NONE;

    //
    // Set up a reasonable default DMA address size, based on the minimum
    // possible on currently supported GPUs.
    //
    RmSetDeviceDmaAddressSize(pNv, NV_GPU_MIN_SUPPORTED_DMA_ADDR_WIDTH);

    os_mem_set(nvp, 0, sizeof(*nvp));
    nvp->status = NV_ERR_INVALID_STATE;
    nvp->pmc_boot_0 = pmc_boot_0;
    nvp->pmc_boot_42 = pmc_boot_42;
    NV_SET_NV_PRIV(pNv, nvp);

    return NV_TRUE;
}

void RmClearPrivateState(
    nv_state_t *pNv
)
{
    nv_priv_t *nvp = NV_GET_NV_PRIV(pNv);
    NvU32 status;
    void *pVbiosCopy = NULL;
    void *pRegistryCopy = NULL;
    NvU32 vbiosSize;
    nv_i2c_adapter_entry_t i2c_adapters[MAX_I2C_ADAPTERS];
    nv_dynamic_power_t dynamicPowerCopy;
    NvU32 x = 0;
    NvU32 pmc_boot_0, pmc_boot_42;

    //
    // Do not clear private state after GPU resets, it is used while
    // recovering the GPU. Only clear the pGpu pointer, which is
    // restored during next initialization cycle.
    //
    if (pNv->flags & NV_FLAG_IN_RECOVERY)
    {
        nvp->pGpu = NULL;
    }

    status = nvp->status;
    pVbiosCopy = nvp->pVbiosCopy;
    vbiosSize = nvp->vbiosSize;
    pRegistryCopy = nvp->pRegistry;
    dynamicPowerCopy = nvp->dynamic_power;
    pmc_boot_0 = nvp->pmc_boot_0;
    pmc_boot_42 = nvp->pmc_boot_42;

    for (x = 0; x < MAX_I2C_ADAPTERS; x++)
    {
        i2c_adapters[x] = nvp->i2c_adapters[x];
    }

    portMemSet(nvp, 0, sizeof(nv_priv_t));

    nvp->status = status;
    nvp->pVbiosCopy = pVbiosCopy;
    nvp->vbiosSize = vbiosSize;
    nvp->pRegistry = pRegistryCopy;
    nvp->dynamic_power = dynamicPowerCopy;
    nvp->pmc_boot_0 = pmc_boot_0;
    nvp->pmc_boot_42 = pmc_boot_42;

    for (x = 0; x < MAX_I2C_ADAPTERS; x++)
    {
        nvp->i2c_adapters[x] = i2c_adapters[x];
    }

    nvp->flags |= NV_INIT_FLAG_PUBLIC_I2C;
}

void RmFreePrivateState(
    nv_state_t *pNv
)
{
    nv_priv_t *nvp = NV_GET_NV_PRIV(pNv);

    gpumgrUnregisterGpuId(pNv->gpu_id);

    RmDestroyRegistry(pNv);

    if (nvp != NULL)
    {
        portMemFree(nvp->pVbiosCopy);
        os_free_mem(nvp);
    }

    NV_SET_NV_PRIV(pNv, NULL);
}

NvBool RmPartiallyInitAdapter(
    nv_state_t *nv
)
{
    NV_PRINTF(LEVEL_INFO, "%s: %04x:%02x:%02x.0\n", __FUNCTION__,
              nv->pci_info.domain, nv->pci_info.bus, nv->pci_info.slot);

    nv_start_rc_timer(nv);

    return NV_TRUE;
}

static NV_STATUS
RmInitX86Emu(
    OBJGPU *pGpu
)
{
    NV_STATUS status = NV_OK;
    nv_state_t *nv = NV_GET_NV_STATE(pGpu);
    PORT_UNREFERENCED_VARIABLE(nv);

#if NVCPU_IS_X86_64
    status = RmInitX86EmuState(pGpu);
#else
    // We don't expect a "primary VGA" adapter on non-amd64 platforms
    NV_ASSERT(!NV_PRIMARY_VGA(nv));
#endif

    return status;
}

static NV_STATUS RmRegisterGpudb(
    OBJGPU       *pGpu
)
{
    NV_STATUS     rmStatus;
    const NvU8   *pGid;
    nv_state_t   *pNv = NV_GET_NV_STATE(pGpu);

    pGid = RmGetGpuUuidRaw(pNv);
    if (pGid == NULL)
    {
        NV_PRINTF(LEVEL_ERROR, "Failed to get UUID\n");
        return NV_ERR_OPERATING_SYSTEM;
    }

    rmStatus = gpudbRegisterGpu(pGid, &pGpu->gpuClData.upstreamPort.addr,
                                pGpu->busInfo.nvDomainBusDeviceFunc);
    if (rmStatus != NV_OK)
    {
        NV_PRINTF(LEVEL_ERROR, "Failed to register GPU with GPU data base\n");
    }

    return rmStatus;
}

static void RmUnixFreeRmApi(
    nv_state_t *nv
)
{
    RM_API *pRmApi = rmapiGetInterface(RMAPI_API_LOCK_INTERNAL);

    if (nv->rmapi.hClient != 0)
    {
        pRmApi->Free(pRmApi, nv->rmapi.hClient, nv->rmapi.hClient);
    }

    portMemSet(&nv->rmapi, 0, sizeof(nv->rmapi));
}

static NvBool RmUnixAllocRmApi(
    nv_state_t *nv,
    NvU32 deviceId
)
{
    NV0080_ALLOC_PARAMETERS deviceParams = { 0 };
    NV2080_ALLOC_PARAMETERS subDeviceParams = { 0 };
    RM_API *pRmApi = rmapiGetInterface(RMAPI_API_LOCK_INTERNAL);

    portMemSet(&nv->rmapi, 0, sizeof(nv->rmapi));

    if (pRmApi->AllocWithHandle(
                    pRmApi,
                    NV01_NULL_OBJECT,
                    NV01_NULL_OBJECT,
                    NV01_NULL_OBJECT,
                    NV01_ROOT,
                    &nv->rmapi.hClient) != NV_OK)
    {
        goto fail;
    }

    //
    // Any call to rmapiDelPendingDevices() will internally delete the UNIX OS
    // layer RMAPI handles. Set this flag to preserve these handles. These
    // handles will be freed explicitly by RmUnixFreeRmApi().
    //
    if (!rmclientSetClientFlagsByHandle(nv->rmapi.hClient,
                                        RMAPI_CLIENT_FLAG_RM_INTERNAL_CLIENT))
    {
        goto fail;
    }

    deviceParams.deviceId = deviceId;

    if (pRmApi->Alloc(
                    pRmApi,
                    nv->rmapi.hClient,
                    nv->rmapi.hClient,
                    &nv->rmapi.hDevice,
                    NV01_DEVICE_0,
                    &deviceParams) != NV_OK)
    {
        goto fail;
    }

    subDeviceParams.subDeviceId = 0;

    if (pRmApi->Alloc(
                    pRmApi,
                    nv->rmapi.hClient,
                    nv->rmapi.hDevice,
                    &nv->rmapi.hSubDevice,
                    NV20_SUBDEVICE_0,
                    &subDeviceParams) != NV_OK)
    {
        goto fail;
    }

    //
    // The NV40_I2C allocation expected to fail, if it is disabled
    // with RM config.
    //
    if (pRmApi->Alloc(
                    pRmApi,
                    nv->rmapi.hClient,
                    nv->rmapi.hSubDevice,
                    &nv->rmapi.hI2C,
                    NV40_I2C,
                    NULL) != NV_OK)
    {
        nv->rmapi.hI2C = 0;
    }

    //
    // The NV04_DISPLAY_COMMON allocation expected to fail for displayless
    // system. nv->rmapi.hDisp value needs to be checked before doing display
    // related control calls.
    //
    if (pRmApi->Alloc(
                    pRmApi,
                    nv->rmapi.hClient,
                    nv->rmapi.hDevice,
                    &nv->rmapi.hDisp,
                    NV04_DISPLAY_COMMON,
                    NULL) != NV_OK)
    {
        nv->rmapi.hDisp = 0;
    }

    return NV_TRUE;

fail:
    RmUnixFreeRmApi(nv);
    return NV_FALSE;
}

NvBool RmInitAdapter(
    nv_state_t *nv
)
{
    NvU32           devicereference = 0;
    UNIX_STATUS     status = INIT_UNIX_STATUS;
    nv_priv_t      *nvp;
    NvBool          retVal = NV_FALSE;
    OBJSYS         *pSys;
    OBJGPU         *pGpu = NULL;
    OBJOS          *pOS;
    KernelDisplay  *pKernelDisplay;
    const void     *gspFwHandle = NULL;
    const void     *gspFwLogHandle = NULL;

    NV_DEV_PRINTF(NV_DBG_SETUP, nv, "RmInitAdapter\n");

    nv->flags &= ~NV_FLAG_PASSTHRU;

    RmSetupRegisters(nv, &status);
    if (! RM_INIT_SUCCESS(status.initStatus) )
        goto failed;

    nvp = NV_GET_NV_PRIV(nv);
    nvp->status = NV_ERR_OPERATING_SYSTEM;

    status.rmStatus = RmInitDeviceDma(nv);
    if (status.rmStatus != NV_OK)
    {
        NV_PRINTF(LEVEL_ERROR, "Cannot configure the device for DMA\n");
        RM_SET_ERROR(status, RM_INIT_GPU_DMA_CONFIGURATION_FAILED);
        goto shutdown;
    }

    nvp->flags |= NV_INIT_FLAG_DMA;

    pSys = SYS_GET_INSTANCE();

    //
    // WAR: If the below UEFI property is set, display RM will attempt to read
    // the state cache during RM init in order to retrieve a snapshot of the
    // display state that the UEFI driver has already programmed. On Orin
    // (T234D), the UEFI boot flow is being enabled on Linux, but our UEFI
    // driver doesn't have any display support right now. As such, our UEFI
    // driver won't allocate any of the display channels, which means that RM
    // will attempt to read the state cache for uninitialized channels. WAR this
    // issue by un-setting the below UEFI property for now.
    //
    // JIRA task TDS-5094 tracks adding display support to the UEFI driver.
    //
    if (NV_IS_SOC_DISPLAY_DEVICE(nv)) {
        pSys->setProperty(pSys, PDB_PROP_SYS_IS_UEFI, NV_FALSE);
    }

    //
    // Get firmware from the OS, if requested, and decide if RM will run as a
    // firmware client.
    //
    if (nv->request_firmware)
    {
        nv->request_fw_client_rm = NV_TRUE;
    }

    // initialize the RM device register mapping
    osInitNvMapping(nv, &devicereference, &status);
    if (! RM_INIT_SUCCESS(status.initStatus) )
    {
        switch (status.rmStatus)
        {
            case NV_ERR_NOT_SUPPORTED:
                nvp->status = NV_ERR_NOT_SUPPORTED;
                break;
        }
        NV_PRINTF(LEVEL_ERROR,
                  "osInitNvMapping failed, bailing out of RmInitAdapter\n");
        goto shutdown;
    }

    //
    // now we can have a pdev for the first time...
    //
    pGpu   = gpumgrGetGpu(devicereference);

    pOS    = SYS_GET_OS(pSys);

    RmInitAcpiMethods(pOS, pSys, pGpu);

    if (IS_GSP_CLIENT(pGpu) && IsT234D(pGpu))
    {
        status.rmStatus = dceclientDceRmInit(pGpu, GPU_GET_DCECLIENTRM(pGpu), NV_TRUE);
        if (status.rmStatus != NV_OK)
        {
            NV_PRINTF(LEVEL_ERROR, "Cannot initialize DCE firmware RM\n");
            RM_SET_ERROR(status, RM_INIT_FIRMWARE_INIT_FAILED);
            goto shutdown;
        }
    }

    pKernelDisplay = GPU_GET_KERNEL_DISPLAY(pGpu);
    if (pKernelDisplay != NULL)
    {
        kdispSetWarPurgeSatellitesOnCoreFree(pKernelDisplay, NV_TRUE);
    }

    if (IS_PASSTHRU(pGpu))
        nv->flags |= NV_FLAG_PASSTHRU;

    // finally, initialize the device
    RmInitNvDevice(devicereference, &status);
    if (! RM_INIT_SUCCESS(status.initStatus) )
    {
        NV_PRINTF(LEVEL_ERROR,
                  "RmInitNvDevice failed, bailing out of RmInitAdapter\n");
        switch (status.rmStatus)
        {
            case NV_ERR_INSUFFICIENT_POWER:
                nvp->status = NV_ERR_INSUFFICIENT_POWER;
                NV_DEV_PRINTF(NV_DBG_ERRORS, nv,
                    "GPU does not have the necessary power cables connected.\n");
                break;
        }
        goto shutdown;
    }

    // LOCK: acquire GPUs lock
    status.rmStatus = rmGpuLocksAcquire(GPUS_LOCK_FLAGS_NONE,
                                        RM_LOCK_MODULES_INIT);
    if (status.rmStatus != NV_OK)
    {
        goto shutdown;
    }

    status.rmStatus = osVerifySystemEnvironment(pGpu);

    // UNLOCK: release GPUs lock
    rmGpuLocksRelease(GPUS_LOCK_FLAGS_NONE, NULL);

    if (status.rmStatus != NV_OK)
    {
        RM_SET_ERROR(status, RM_INIT_SYS_ENVIRONMENT_FAILED);
        switch (status.rmStatus)
        {
            case NV_ERR_IRQ_NOT_FIRING:
                nvp->status = NV_ERR_IRQ_NOT_FIRING;
                break;
        }
        NV_PRINTF(LEVEL_ERROR, "RmVerifySystemEnvironment failed, bailing!\n");
        goto shutdown;
    }

    nv_start_rc_timer(nv);

    nvp->status = NV_OK;

    if (!RmUnixAllocRmApi(nv, devicereference)) {
        RM_SET_ERROR(status, RM_INIT_ALLOC_RMAPI_FAILED);
        status.rmStatus = NV_ERR_GENERIC;
        goto shutdown;
    }

    status.rmStatus = RmInitGpuInfoWithRmApi(pGpu);
    if (status.rmStatus != NV_OK)
    {
        RM_SET_ERROR(status, RM_INIT_GPUINFO_WITH_RMAPI_FAILED);
        goto shutdown;
    }

    status.rmStatus = RmInitX86Emu(pGpu);
    if (status.rmStatus != NV_OK)
    {
        RM_SET_ERROR(status, RM_INIT_VBIOS_X86EMU_FAILED);
        NV_PRINTF(LEVEL_ERROR,
                  "RmInitX86Emu failed, bailing out of RmInitAdapter\n");
        goto shutdown;
    }

    // i2c only on master device??
    RmI2cAddGpuPorts(nv);
    nvp->flags |= NV_INIT_FLAG_PUBLIC_I2C;

    nv->flags &= ~NV_FLAG_IN_RECOVERY;

    pOS->setProperty(pOS, PDB_PROP_OS_SYSTEM_EVENTS_SUPPORTED, NV_TRUE);

    RmInitS0ixPowerManagement(nv);
    RmInitDeferredDynamicPowerManagement(nv);

    if (!NV_IS_SOC_DISPLAY_DEVICE(nv) && !NV_IS_SOC_IGPU_DEVICE(nv))
    {
        status.rmStatus = RmRegisterGpudb(pGpu);
        if (status.rmStatus != NV_OK)
        {
            RM_SET_ERROR(status, RM_GPUDB_REGISTER_FAILED);
            goto shutdown;
        }
    }

    if (nvp->b_mobile_config_enabled)
    {
        NvU32 ac_plugged = 0;
        if (nv_acpi_get_powersource(&ac_plugged) == NV_OK)
        {
            // LOCK: acquire GPU lock
            if (rmGpuLocksAcquire(GPUS_LOCK_FLAGS_NONE, RM_LOCK_MODULES_NONE) == NV_OK)
            {
                //
                // As we have already acquired the API Lock here, we are
                // calling RmSystemEvent directly instead of rm_system_event.
                //
                RmSystemEvent(nv, NV_SYSTEM_ACPI_BATTERY_POWER_EVENT, !ac_plugged);

                // UNLOCK: release GPU lock
                rmGpuLocksRelease(GPUS_LOCK_FLAGS_NONE, NULL);
            }
        }
    }

    NV_DEV_PRINTF(NV_DBG_SETUP, nv, "RmInitAdapter succeeded!\n");

    retVal = NV_TRUE;
    goto done;

 shutdown:
    nv->flags &= ~NV_FLAG_IN_RECOVERY;

    // call ShutdownAdapter to undo anything we've done above
    RmShutdownAdapter(nv);

 failed:
    NV_DEV_PRINTF(NV_DBG_ERRORS, nv, "RmInitAdapter failed! (0x%x:0x%x:%d)\n",
        status.initStatus, status.rmStatus, status.line);

done:
    nv_put_firmware(gspFwHandle);
    nv_put_firmware(gspFwLogHandle);

    return retVal;
}

void RmShutdownAdapter(
    nv_state_t *nv
)
{
    nv_priv_t *nvp = NV_GET_NV_PRIV(nv);
    OBJGPU *pGpu = NV_GET_NV_PRIV_PGPU(nv);
    NV_STATUS rmStatus;

    if ((pGpu != NULL) && (nvp->flags & NV_INIT_FLAG_GPUMGR_ATTACH))
    {
        NvU32 gpuInstance    = gpuGetInstance(pGpu);
        NvU32 deviceInstance = gpuGetDeviceInstance(pGpu);
        OBJSYS         *pSys = SYS_GET_INSTANCE();

        RmUnixFreeRmApi(nv);

        nv->ud.cpu_address = 0;
        nv->ud.size = 0;

        // LOCK: acquire GPUs lock
        if (rmGpuLocksAcquire(GPUS_LOCK_FLAGS_NONE, RM_LOCK_MODULES_DESTROY) == NV_OK)
        {
            RmDestroyDeferredDynamicPowerManagement(nv);

            gpuFreeEventHandle(pGpu);

            rmapiSetDelPendingClientResourcesFromGpuMask(NVBIT(gpuInstance));
            rmapiDelPendingDevices(NVBIT(gpuInstance));

            os_disable_console_access();

            if (nvp->flags & NV_INIT_FLAG_GPU_STATE_LOAD)
            {
                rmStatus = gpuStateUnload(pGpu, GPU_STATE_DEFAULT);
                NV_ASSERT(rmStatus == NV_OK);
            }

            if (IS_GSP_CLIENT(pGpu) && IsT234D(pGpu))
            {
                rmStatus = dceclientDceRmInit(pGpu, GPU_GET_DCECLIENTRM(pGpu), NV_FALSE);
                if (rmStatus != NV_OK)
                {
                    NV_PRINTF(LEVEL_ERROR, "DCE firmware RM Shutdown failure\n");
                }
            }

            if (nvp->flags & NV_INIT_FLAG_GPU_STATE)
            {
                rmStatus = gpuStateDestroy(pGpu);
                NV_ASSERT(rmStatus == NV_OK);
            }

            os_enable_console_access();

            //if (nvp->flags & NV_INIT_FLAG_HAL)
              //  destroyHal(pDev);

#if NVCPU_IS_X86_64
            RmFreeX86EmuState(pGpu);
#endif

            gpumgrDetachGpu(gpuInstance);
            gpumgrDestroyDevice(deviceInstance);

            if (nvp->flags & NV_INIT_FLAG_DMA)
            {
                RmTeardownDeviceDma(nv);
            }

            RmClearPrivateState(nv);

            RmUnInitAcpiMethods(pSys);

            // UNLOCK: release GPUs lock
            rmGpuLocksRelease(GPUS_LOCK_FLAGS_NONE, NULL);

            // RM_BASIC_LOCK_MODEL: free GPU lock
            rmGpuLockFree(deviceInstance);
        }
    }
    else
    {
        RmClearPrivateState(nv);
    }

    RmTeardownRegisters(nv);
}

void RmPartiallyDisableAdapter(
    nv_state_t *nv
)
{
    NV_PRINTF(LEVEL_INFO, "%s: RM is in SW Persistence mode\n", __FUNCTION__);

    nv_stop_rc_timer(nv);
}

void RmDisableAdapter(
    nv_state_t *nv
)
{
    NV_STATUS  rmStatus;
    OBJGPU    *pGpu = NV_GET_NV_PRIV_PGPU(nv);
    NvU32      gpuMask;
    nv_priv_t *nvp  = NV_GET_NV_PRIV(nv);

    if (pGpu->getProperty(pGpu, PDB_PROP_GPU_IN_SECONDARY_BUS_RESET) ||
        pGpu->getProperty(pGpu, PDB_PROP_GPU_IN_FULLCHIP_RESET))
    {
        pGpu->setProperty(pGpu, PDB_PROP_GPU_IN_TIMEOUT_RECOVERY, NV_TRUE);
        nv->flags |= NV_FLAG_IN_RECOVERY;
    }

    //
    // Free the client allocated resources.
    //
    // This needs to happen prior to tearing down SLI state when SLI is enabled.
    //
    // Note this doesn't free RM internal resource allocations. Those are
    // freed during (gpumgrUpdateSLIConfig->...->)gpuStateUnload.
    //
    // We need to free resources for all GPUs linked in a group as
    // gpumgrUpdateSLIConfig will teardown GPU state for the entire set.
    //
    gpuMask = gpumgrGetGpuMask(pGpu);

    rmapiSetDelPendingClientResourcesFromGpuMask(gpuMask);
    rmapiDelPendingDevices(gpuMask);

    // LOCK: acquire GPUs lock
    if (rmGpuLocksAcquire(GPUS_LOCK_FLAGS_NONE, RM_LOCK_MODULES_DESTROY) == NV_OK)
    {
        nv_stop_rc_timer(nv);

        os_disable_console_access();

        if (nvp->flags & NV_INIT_FLAG_GPU_STATE_LOAD)
        {
            rmStatus = gpuStateUnload(pGpu, GPU_STATE_DEFAULT);
            NV_ASSERT(rmStatus == NV_OK);
            nvp->flags &= ~NV_INIT_FLAG_GPU_STATE_LOAD;
        }

        os_enable_console_access();

        // UNLOCK: release GPUs lock
        rmGpuLocksRelease(GPUS_LOCK_FLAGS_NONE, NULL);
    }
}

NV_STATUS RmGetAdapterStatus(
    nv_state_t *pNv,
    NvU32      *pStatus
)
{
    //
    // This status is determined in RmInitAdapter(); the glue layer
    // requests it when the adapter failed to initialize to learn
    // more about the error condition. This is currently limited to
    // osVerifySystemEnvironment() failures.
    //
    nv_priv_t *nvp;

    nvp = NV_GET_NV_PRIV(pNv);
    if (nvp == NULL)
    {
        return NV_ERR_INVALID_STATE;
    }

    *pStatus = nvp->status;
    return NV_OK;
}

static void initUnixSpecificRegistry(
    OBJGPU *pGpu
)
{
    // By default, enable GPU reset on Unix
    osWriteRegistryDword(pGpu, "RMSecBusResetEnable", 1);
    osWriteRegistryDword(pGpu, "RMForcePcieConfigSave", 1);

}

void
osRemoveGpu(
    NvU32 domain,
    NvU8 bus,
    NvU8 device
)
{
    void   *handle;

    handle = os_pci_init_handle(domain, bus, device, 0, NULL, NULL);
    if (handle != NULL)
    {
        os_pci_remove(handle);
    }
}

NV_STATUS RmExcludeAdapter(
    nv_state_t *nv
)
{
    return NV_ERR_NOT_SUPPORTED;
}
