#ifndef _G_MEM_MGR_NVOC_H_
#define _G_MEM_MGR_NVOC_H_
#include "nvoc/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SPDX-FileCopyrightText: Copyright (c) 1993-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "g_mem_mgr_nvoc.h"

#ifndef MEM_MGR_H
#define MEM_MGR_H

#include "core/core.h"
#include "core/info_block.h"
#include "gpu/eng_state.h"

#include "gpu/gpu.h"

#include "mem_mgr/mem.h"

#include "gpu/mem_mgr/virt_mem_allocator_common.h"
#include "containers/map.h"
#include "gpu/mem_mgr/heap_base.h"
#include "mem_mgr/vaspace.h"

typedef volatile struct _cl906f_tag1 Nv906fControl;
typedef struct KERNEL_MIG_GPU_INSTANCE KERNEL_MIG_GPU_INSTANCE;

typedef struct
{
    MEMORY_DESCRIPTOR *pMemDesc;
    NvU64              offset;
} TRANSFER_SURFACE;

// Memory transfer engine types.
typedef enum
{
    TRANSFER_TYPE_PROCESSOR = 0,       // CPU/GSP/DPU depending on execution context
    TRANSFER_TYPE_GSP_DMA,             // Dma engine internal to GSP
    TRANSFER_TYPE_CE,                  // Copy Engine HW
} TRANSFER_TYPE;

#define TRANSFER_FLAGS_NONE                   0
#define TRANSFER_FLAGS_DEFER_FLUSH            NVBIT32(0) // Applicable only for write operations
#define TRANSFER_FLAGS_SHADOW_ALLOC           NVBIT32(1) // Applicable only for non-PROCESSOR transfers
#define TRANSFER_FLAGS_SHADOW_INIT_MEM        NVBIT32(2) // Applicable only for non-PROCESSOR transfers
#define TRANSFER_FLAGS_PERSISTENT_CPU_MAPPING NVBIT32(3) // Require long lived PROCESSOR mapping
#define TRANSFER_FLAGS_DESTROY_MAPPING        NVBIT32(4) // Destroy any cached mappings when complete

typedef struct
{
    NvU32   bar1Size;
    NvU32   bar1AvailSize;
    NvU32   bankSwizzleAlignment;
    NvU32   bar1MaxContigAvailSize;
} GETBAR1INFO, *PGETBAR1INFO;

//
// RM Default PTE kind
// Bug #2242255, introducing the RM Default kind to allow sharing memory between
// different architectures especially between Turing+ and Pre Turing chips
//
#define RM_DEFAULT_PTE_KIND                 0x100

typedef enum
{
    FB_IS_KIND_Z,                           // Kind is a Z buffer
    FB_IS_KIND_ZBC,                         // Zero bandwidth clears
    FB_IS_KIND_ZBC_ALLOWS_1,                // ZBC with 1 bit of tag
    FB_IS_KIND_ZBC_ALLOWS_2,                // ZBC with 2 bits of tag
    FB_IS_KIND_ZBC_ALLOWS_4,                // ZBC with 4 bits of tag
    FB_IS_KIND_COMPRESSIBLE,                // Any compressible kind
    FB_IS_KIND_COMPRESSIBLE_1,              // Compressible with 1 comp tag bit
    FB_IS_KIND_COMPRESSIBLE_2,              // Compressible with 2 comp tag bits
    FB_IS_KIND_COMPRESSIBLE_4,              // Compressible with 4 comp tag bits
    FB_IS_KIND_SUPPORTED,                   // Kind is supported
    FB_IS_KIND_DISALLOW_PLC,                // Kind Disallows PLC
} FB_IS_KIND_OP;

// Surface compression parameters
typedef struct COMPR_INFO
{
    // Surface kind; if not compressed, following parameters are ignored
    NvU32  kind;

    // Compression page shift;  0 if kind is uncompressed
    NvU32  compPageShift;

    //
    // Are comptags are determined per-page by PA?
    // If set, following parameters are ignored
    //
    NvBool bPhysBasedComptags;

    // see GMMU_COMPR_INFO
    NvU32  compPageIndexLo;
    NvU32  compPageIndexHi;
    NvU32  compTagLineMin;
    NvU32  compTagLineMultiplier;
} COMPR_INFO;

//
// Fixed Channel Properties for Memutils Object
//

typedef NV_STATUS FbScrubCallback(OBJGPU *);

#define BLOCK_INDEX_FROM_ADDR(addr,size)             ((NvU32)((addr) >> size))
#define BLOCK_ADDR_FROM_INDEX(idx,size)              (((NvU64)(idx)) << size)

#define MEMUTILS_SIZE_PER_BLOCK_INBYTES               (0x68)
#define MEMUTILS_TOTAL_SIZE_PER_BLOCK_INBYTES         (0x60) //(COPY + PB SEMA)
#define MEMUTILS_TD_BLOCKS_PER_CHUNK                   0x40

#define BLOCK_INDEX_FROM_ADDR(addr,size)              ((NvU32)((addr) >> size))
#define BLOCK_ADDR_FROM_INDEX(idx,size)               (((NvU64)(idx)) << size)

#define MEMUTILS_NUM_PAYLOAD_SEMAPHORES               (2)
#define MEMUTILS_NUM_GPFIFIO_ENTRIES                  (32)
// PB size should be a multiple of chunk size
#define MEMUTILS_CHANNEL_PB_SIZE                      (0x10 * MEMUTILS_SIZE_PER_BLOCK_INBYTES  * \
                                                       MEMUTILS_TD_BLOCKS_PER_CHUNK)
#define MEMUTILS_CHANNEL_SEMAPHORE_SIZE               (4 * MEMUTILS_NUM_PAYLOAD_SEMAPHORES)
#define MEMUTILS_CHANNEL_NOTIFIER_SIZE                (sizeof(NvNotification) * 1)

// offset and line length should be a multiple of 4KB
#define MEMUTIL_SCRUB_OFFSET_ALIGNMENT        (4 * 1024)
#define MEMUTIL_SCRUB_LINE_LENGTH_ALIGNMENT   (4 * 1024)

typedef enum {
    SCRUBBER_CHANNEL,
    FAST_SCRUBBER_CHANNEL,
    COPY_CHANNEL,
    MAX_CHANNEL_TYPE
} CHANNEL_KIND;

// This will be moved to a channel object next
typedef struct OBJCHANNEL
{
    NvHandle                        deviceId;           // Device Handle
    NvHandle                        physMemId;          // Memory Handle
    NvHandle                        channelId;          // Channel Handle
    NvHandle                        subdeviceId;        // Subdevice Handle
    NvHandle                        errNotifierIdVirt;
    NvHandle                        errNotifierIdPhys;
    NvHandle                        copyObjectId;
    NvHandle                        eventId;
    NvHandle                        pushBufferId;
    NvHandle                        bitMapSemPhysId;
    NvHandle                        bitMapSemVirtId;
    NvHandle                        hVASpaceId;           // VASpace handle, when scrubber in virtual mode
    NvHandle                        hFbAlias;             // Used only for virtual channels
    NvHandle                        hFbAliasVA;
    // to be moved later

    NvU32                           channelSize;
    NvU32                           channelNumGpFifioEntries;
    NvU32                           channelPbSize;
    NvU32                           channelNotifierSize;
    NvU32                           methodSizePerBlock;
    NvU32                           semaOffset;
    NvU32                           finishPayloadOffset;
    NvU32                           finishPayload;
    NvBool                          isChannelSynchronized;
    NvBool                          isProgressChecked;
//
// RM internal channels are created as privileged channels (physical address access) by default
// For MMU Bug: 2739505, we need to switch to use channels in non-privileged mode.
//
    NvBool                          bUseVasForCeCopy;         // set to NV_TRUE, when scrubber operates in virtual address
    struct RsClient                        *pRsClient;
    struct OBJVASPACE                     *pVAS;
    NvU32                           engineType;
    NvU64                           startFbOffset;
    NvU64                           fbSize;
    NvU64                           fbAliasVA;
    NvU64                           vaStartOffset;
    // to be moved to a separate object later

    NvU32                           *pBlockPendingState;
    NvU32                           *pBlockDoneState;
    NvU32                           blockCount;
    NvHandle                        hClient;
    NvHandle                        hLiteClient;          // Used only for fifo lite channels
    NvBool                          bClientAllocated;
    NvU64                           pbGpuVA;
    NvU64                           pbGpuBitMapVA;
    NvU64                           pbGpuNotifierVA;
    NvU8                            *pbCpuVA;
    NvU8                            *pbBitMapVA;
    Nv906fControl                   *pControlGPFifo;
    NvU32                           classEngineID;
    NVOS10_EVENT_KERNEL_CALLBACK_EX callback;
    NvU32                           state;
    NvU32                           hTdCopyClass;
    NvU32                           minBlockSize;
    NvU32                           maxBlockSize;
    NvU32                           channelPutOffset;
    NvU8                            blockShift;
    NvU32                           lastPayloadPushed;
    NvBool                          isChannelActive;
    NvU32                           workSubmitToken;
    //
    // Work submit token read from notifier memory.
    //
    NvNotification                  *pTokenFromNotifier;
    NvU32                           lastSubmittedEntry;
    NvHandle                        lastAllocatedHandle;
    CHANNEL_KIND                    type;

    // Used for Volta+
    NvHandle                        doorbellRegionHandle;
    NvU8                            *pDoorbellRegion;
    NvU32                           *pDoorbellRegisterOffset;
    NvBool                          bUseDoorbellRegister;
    NvHandle                        hUserD;
    NvBool                          bClientUserd;


    //
    // Used only by suspend resume channel.
    // This denotes whether the channel manages the BAR2 VASpace.
    // Suspend resume happens way before the regular BAR2 init.
    // Channel instmem has to be stored in vidmem due to 40 bit restriction in host on Pascal+ chips.
    // So the suspend resume channel has to setup BAR2 for accessing vidmem.
    //
    NvBool                          bManageBAR2;
    OBJGPU                         *pGpu;
    struct KernelCE                       *pKCe;

    // Used by Partition Scrubber
    KERNEL_MIG_GPU_INSTANCE         *pKernelMIGGpuInstance;
    NvHandle                        hPartitionRef;
} OBJCHANNEL, *POBJCHANNEL;

#define NV_METHOD(SubCh, Method, Num)                        \
    (DRF_DEF(906F, _DMA_INCR, _OPCODE,     _VALUE)         | \
     DRF_NUM(906F, _DMA_INCR, _COUNT,      Num)            | \
     DRF_NUM(906F, _DMA_INCR, _SUBCHANNEL, SubCh)          | \
     DRF_NUM(906F, _DMA_INCR, _ADDRESS,    (Method) >> 2))

#define PUSH_DATA(Data) MEM_WR32(ptr++, (Data))

#define PUSH_PAIR(SubCh, Method, Data)            \
    do                                            \
    {                                             \
        PUSH_DATA(NV_METHOD(SubCh, (Method), 1)); \
        PUSH_DATA((Data));                        \
    } while (0)

//-----------------------------------------------------------------------------

typedef struct
{
    NvU32  lastSubmittedBlock;
    NvBool isTopDownScrubber;
    NvBool isActive;
    NvU32  scrubberState;
    NvU32  currentFbRegion;
    NvU32  startBlock;
    NvU32  endBlock;
    NvU32 *pPendingBitMap;
    NvU32 *pDoneBitMap;
    NvU32  blockCount;
    struct OBJCE *pCe;
    NvBool bCeInUse;
    OBJCHANNEL tdHeapState;
    OBJCHANNEL allocationScrubberState;
} OBJSCRUB, *POBJSCRUB;

typedef struct
{
    NvU64   base;               // Base/start address of the region
    NvU64   limit;              // Last/end address of region
    NvU64   rsvdSize;           // Memory RM may be required to allocate in this region
    NvBool  bRsvdRegion;        // Reserved region -- not publicly usable
    NvU32   performance;        // Relative performance.  Higher is faster
    NvBool  bSupportCompressed; // Support compressed kinds
    NvBool  bSupportISO;        // Support ISO (display, cursor, video) surfaces
    NvBool  bProtected;         // Represents a protected region of memory.
    NvBool  bInternalHeap;      // PMA:Used for internal RM allocations
    NvBool  bLostOnSuspend;     // Not required to be Saved during S/R.
} FB_REGION_DESCRIPTOR, *PFB_REGION_DESCRIPTOR;

#define MAX_FB_REGIONS  16

// Maximum number of contexts created for WHQL test WDDM Max Contexts
#define WHQL_TEST_MAX_CONTEXTS          100

// Object 'get' macros for FB relative object retrievals.
#define MEMORY_MANAGER_GET_HEAP(p)          ((p)->pHeap)

typedef struct _def_fb_mem_node
{
    struct _def_fb_mem_node *pNext;

    NvBool bFreeDescriptor;
    PMEMORY_DESCRIPTOR pMemDesc;

} FB_MEM_NODE, *PFB_MEM_NODE;

// defines for MemoryManager::fbsrReservedRanges
#define MAX_FBSR_RESERVED_REGIONS                   2           // Max. Memory descriptors for RM Instance memory
#define FBSR_RESERVED_INST_MEMORY_BEFORE_BAR2PTE    0
#define FBSR_RESERVED_INST_MEMORY_AFTER_BAR2PTE     1

/*!
 * MemoryManager provides the root memory management of GPU video memory.
 * External entities might provide suballocators on top of MemoryManager.
 *
 * MemoryManager can have static information on the memory system (e.g.: list of
 * kinds, etc), however MemoryManager does not have direct access to the GPU
 * memory system (e.g.: BAR0 registers). It relies on KernelMemorySystem for
 * operations on the memory system.
 *
 * MemoryManager is instantiated in VGPU guest/GSP Client as well as the VGPU
 * host/GSP-RM.
 */

#define MEM_MGR_STUB_ORIN(...) { return __VA_ARGS__; }

#ifdef NVOC_MEM_MGR_H_PRIVATE_ACCESS_ALLOWED
#define PRIVATE_FIELD(x) x
#else
#define PRIVATE_FIELD(x) NVOC_PRIVATE_FIELD(x)
#endif
struct RM_POOL_ALLOC_MEM_RESERVE_INFO;

struct MIG_MEMORY_PARTITIONING_INFO {
    struct NV_RANGE partitionableMemoryRange;
    struct NV_RANGE partitionableBar1Range;
    NvHandle hClient;
    NvHandle hDevice;
    NvHandle hSubdevice;
    NvBool bNonMIGTopLevelScrubber;
};


struct MemoryManager {
    const struct NVOC_RTTI *__nvoc_rtti;
    struct OBJENGSTATE __nvoc_base_OBJENGSTATE;
    struct Object *__nvoc_pbase_Object;
    struct OBJENGSTATE *__nvoc_pbase_OBJENGSTATE;
    struct MemoryManager *__nvoc_pbase_MemoryManager;
    NV_STATUS (*__memmgrReconcileTunableState__)(POBJGPU, struct MemoryManager *, void *);
    NV_STATUS (*__memmgrStateLoad__)(POBJGPU, struct MemoryManager *, NvU32);
    NV_STATUS (*__memmgrStateUnload__)(POBJGPU, struct MemoryManager *, NvU32);
    NV_STATUS (*__memmgrStateInitLocked__)(POBJGPU, struct MemoryManager *);
    NV_STATUS (*__memmgrStatePreLoad__)(POBJGPU, struct MemoryManager *, NvU32);
    NV_STATUS (*__memmgrStatePostUnload__)(POBJGPU, struct MemoryManager *, NvU32);
    void (*__memmgrStateDestroy__)(POBJGPU, struct MemoryManager *);
    NV_STATUS (*__memmgrStatePreUnload__)(POBJGPU, struct MemoryManager *, NvU32);
    NV_STATUS (*__memmgrStateInitUnlocked__)(POBJGPU, struct MemoryManager *);
    void (*__memmgrInitMissing__)(POBJGPU, struct MemoryManager *);
    NV_STATUS (*__memmgrStatePreInitLocked__)(POBJGPU, struct MemoryManager *);
    NV_STATUS (*__memmgrStatePreInitUnlocked__)(POBJGPU, struct MemoryManager *);
    NV_STATUS (*__memmgrGetTunableState__)(POBJGPU, struct MemoryManager *, void *);
    NV_STATUS (*__memmgrCompareTunableState__)(POBJGPU, struct MemoryManager *, void *, void *);
    void (*__memmgrFreeTunableState__)(POBJGPU, struct MemoryManager *, void *);
    NV_STATUS (*__memmgrStatePostLoad__)(POBJGPU, struct MemoryManager *, NvU32);
    NV_STATUS (*__memmgrAllocTunableState__)(POBJGPU, struct MemoryManager *, void **);
    NV_STATUS (*__memmgrSetTunableState__)(POBJGPU, struct MemoryManager *, void *);
    NV_STATUS (*__memmgrConstructEngine__)(POBJGPU, struct MemoryManager *, ENGDESCRIPTOR);
    NvBool (*__memmgrIsPresent__)(POBJGPU, struct MemoryManager *);
    NvBool bFbsrWddmModeEnabled;
    NvBool bFbRegionsSupported;
    NvBool bPmaSupportedOnPlatform;
    NvBool bPmaEnabled;
    NvBool bPmaInitialized;
    NvBool bPmaForcePersistence;
    NvBool bPmaAddrTree;
    NvBool bClientPageTablesPmaManaged;
    NvBool bScanoutSysmem;
    NvBool bMixedDensityFbp;
    NvBool bPreferSlowRegion;
    NvBool bPersistentStandbyBuffer;
    NvBool bEnableFbsrPagedDma;
    NvBool bDisallowSplitLowerMemory;
    NvBool bIgnoreUpperMemory;
    NvBool bLddmReservedMemoryCalculated;
    NvBool bSmallPageCompression;
    NvBool bSysmemCompressionSupportDef;
    NvBool bBug1698088IncreaseRmReserveMemoryWar;
    NvBool bBug2301372IncreaseRmReserveMemoryWar;
    NvBool bEnableFbsrFileMode;
    NvBool bEnableDynamicPageOfflining;
    NvBool bVgpuPmaSupport;
    NvBool bAllowNoncontiguousAllocation;
    NvBool bEccInterleavedVidmemScrub;
    NvBool bScrubberInitialized;
    NvBool bAllowSysmemHugePages;
    NvBool bEccScrubOverride;
    NvU32 sysmemPageSize;
    struct Heap *pHeap;
    NvBool bScrubOnFreeEnabled;
    NvBool bFastScrubberEnabled;
    NvBool bDisableAsyncScrubforMods;
    NvBool bUseVasForCeMemoryOps;
    NvBool bRmExecutingEccScrub;
    NvBool bBug1441072EccScrubWar;
    NvU64 heapStartOffset;
    NvU64 rsvdMemoryBase;
    NvU32 rsvdMemorySize;
    struct RM_POOL_ALLOC_MEM_RESERVE_INFO *pPageLevelReserve;
    struct MIG_MEMORY_PARTITIONING_INFO MIGMemoryPartitioningInfo;
    NvHandle hClient;
    NvHandle hDevice;
    NvHandle hSubdevice;
};

#ifndef __NVOC_CLASS_MemoryManager_TYPEDEF__
#define __NVOC_CLASS_MemoryManager_TYPEDEF__
typedef struct MemoryManager MemoryManager;
#endif /* __NVOC_CLASS_MemoryManager_TYPEDEF__ */

#ifndef __nvoc_class_id_MemoryManager
#define __nvoc_class_id_MemoryManager 0x22ad47
#endif /* __nvoc_class_id_MemoryManager */

extern const struct NVOC_CLASS_DEF __nvoc_class_def_MemoryManager;

#define __staticCast_MemoryManager(pThis) \
    ((pThis)->__nvoc_pbase_MemoryManager)

#ifdef __nvoc_mem_mgr_h_disabled
#define __dynamicCast_MemoryManager(pThis) ((MemoryManager*)NULL)
#else //__nvoc_mem_mgr_h_disabled
#define __dynamicCast_MemoryManager(pThis) \
    ((MemoryManager*)__nvoc_dynamicCast(staticCast((pThis), Dynamic), classInfo(MemoryManager)))
#endif //__nvoc_mem_mgr_h_disabled

#define PDB_PROP_MEMMGR_IS_MISSING_BASE_CAST __nvoc_base_OBJENGSTATE.
#define PDB_PROP_MEMMGR_IS_MISSING_BASE_NAME PDB_PROP_ENGSTATE_IS_MISSING

NV_STATUS __nvoc_objCreateDynamic_MemoryManager(MemoryManager**, Dynamic*, NvU32, va_list);

NV_STATUS __nvoc_objCreate_MemoryManager(MemoryManager**, Dynamic*, NvU32);
#define __objCreate_MemoryManager(ppNewObj, pParent, createFlags) \
    __nvoc_objCreate_MemoryManager((ppNewObj), staticCast((pParent), Dynamic), (createFlags))

#define memmgrReconcileTunableState(pGpu, pEngstate, pTunableState) memmgrReconcileTunableState_DISPATCH(pGpu, pEngstate, pTunableState)
#define memmgrStateLoad(pGpu, pEngstate, arg0) memmgrStateLoad_DISPATCH(pGpu, pEngstate, arg0)
#define memmgrStateUnload(pGpu, pEngstate, arg0) memmgrStateUnload_DISPATCH(pGpu, pEngstate, arg0)
#define memmgrStateInitLocked(pGpu, pEngstate) memmgrStateInitLocked_DISPATCH(pGpu, pEngstate)
#define memmgrStatePreLoad(pGpu, pEngstate, arg0) memmgrStatePreLoad_DISPATCH(pGpu, pEngstate, arg0)
#define memmgrStatePostUnload(pGpu, pEngstate, arg0) memmgrStatePostUnload_DISPATCH(pGpu, pEngstate, arg0)
#define memmgrStateDestroy(pGpu, pEngstate) memmgrStateDestroy_DISPATCH(pGpu, pEngstate)
#define memmgrStatePreUnload(pGpu, pEngstate, arg0) memmgrStatePreUnload_DISPATCH(pGpu, pEngstate, arg0)
#define memmgrStateInitUnlocked(pGpu, pEngstate) memmgrStateInitUnlocked_DISPATCH(pGpu, pEngstate)
#define memmgrInitMissing(pGpu, pEngstate) memmgrInitMissing_DISPATCH(pGpu, pEngstate)
#define memmgrStatePreInitLocked(pGpu, pEngstate) memmgrStatePreInitLocked_DISPATCH(pGpu, pEngstate)
#define memmgrStatePreInitUnlocked(pGpu, pEngstate) memmgrStatePreInitUnlocked_DISPATCH(pGpu, pEngstate)
#define memmgrGetTunableState(pGpu, pEngstate, pTunableState) memmgrGetTunableState_DISPATCH(pGpu, pEngstate, pTunableState)
#define memmgrCompareTunableState(pGpu, pEngstate, pTunables1, pTunables2) memmgrCompareTunableState_DISPATCH(pGpu, pEngstate, pTunables1, pTunables2)
#define memmgrFreeTunableState(pGpu, pEngstate, pTunableState) memmgrFreeTunableState_DISPATCH(pGpu, pEngstate, pTunableState)
#define memmgrStatePostLoad(pGpu, pEngstate, arg0) memmgrStatePostLoad_DISPATCH(pGpu, pEngstate, arg0)
#define memmgrAllocTunableState(pGpu, pEngstate, ppTunableState) memmgrAllocTunableState_DISPATCH(pGpu, pEngstate, ppTunableState)
#define memmgrSetTunableState(pGpu, pEngstate, pTunableState) memmgrSetTunableState_DISPATCH(pGpu, pEngstate, pTunableState)
#define memmgrConstructEngine(pGpu, pEngstate, arg0) memmgrConstructEngine_DISPATCH(pGpu, pEngstate, arg0)
#define memmgrIsPresent(pGpu, pEngstate) memmgrIsPresent_DISPATCH(pGpu, pEngstate)
static inline NvU32 memmgrDeterminePageSize_4a4dee(struct MemoryManager *pMemoryManager, NvHandle hClient, NvU64 memSize, NvU32 memFormat, NvU32 pageFormatFlags, NvU32 *pRetAttr, NvU32 *pRetAttr2) {
    return 0;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvU32 memmgrDeterminePageSize(struct MemoryManager *pMemoryManager, NvHandle hClient, NvU64 memSize, NvU32 memFormat, NvU32 pageFormatFlags, NvU32 *pRetAttr, NvU32 *pRetAttr2) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return 0;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrDeterminePageSize(pMemoryManager, hClient, memSize, memFormat, pageFormatFlags, pRetAttr, pRetAttr2) memmgrDeterminePageSize_4a4dee(pMemoryManager, hClient, memSize, memFormat, pageFormatFlags, pRetAttr, pRetAttr2)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrDeterminePageSize_HAL(pMemoryManager, hClient, memSize, memFormat, pageFormatFlags, pRetAttr, pRetAttr2) memmgrDeterminePageSize(pMemoryManager, hClient, memSize, memFormat, pageFormatFlags, pRetAttr, pRetAttr2)

static inline NV_STATUS memmgrScrubInit_56cd7a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return NV_OK;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrScrubInit(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrScrubInit(pGpu, pMemoryManager) memmgrScrubInit_56cd7a(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrScrubInit_HAL(pGpu, pMemoryManager) memmgrScrubInit(pGpu, pMemoryManager)

static inline NV_STATUS memmgrScrubHandlePostSchedulingEnable_46f6a7(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return NV_ERR_NOT_SUPPORTED;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrScrubHandlePostSchedulingEnable(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrScrubHandlePostSchedulingEnable(pGpu, pMemoryManager) memmgrScrubHandlePostSchedulingEnable_46f6a7(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrScrubHandlePostSchedulingEnable_HAL(pGpu, pMemoryManager) memmgrScrubHandlePostSchedulingEnable(pGpu, pMemoryManager)

static inline void memmgrGetScrubState_f2d351(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU64 *arg0, NvU64 *arg1, NvBool *arg2) {
    NV_ASSERT_PRECOMP(0);
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline void memmgrGetScrubState(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU64 *arg0, NvU64 *arg1, NvBool *arg2) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetScrubState(pGpu, pMemoryManager, arg0, arg1, arg2) memmgrGetScrubState_f2d351(pGpu, pMemoryManager, arg0, arg1, arg2)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetScrubState_HAL(pGpu, pMemoryManager, arg0, arg1, arg2) memmgrGetScrubState(pGpu, pMemoryManager, arg0, arg1, arg2)

static inline void memmgrScrubInternalRegions_b3696a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline void memmgrScrubInternalRegions(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrScrubInternalRegions(pGpu, pMemoryManager) memmgrScrubInternalRegions_b3696a(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrScrubInternalRegions_HAL(pGpu, pMemoryManager) memmgrScrubInternalRegions(pGpu, pMemoryManager)

static inline NvBool memmgrEccScrubInProgress_491d52(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return ((NvBool)(0 != 0));
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvBool memmgrEccScrubInProgress(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_FALSE;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrEccScrubInProgress(pGpu, pMemoryManager) memmgrEccScrubInProgress_491d52(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrEccScrubInProgress_HAL(pGpu, pMemoryManager) memmgrEccScrubInProgress(pGpu, pMemoryManager)

static inline void memmgrAsyncScrubRegion_f2d351(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU64 arg0, NvU64 arg1) {
    NV_ASSERT_PRECOMP(0);
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline void memmgrAsyncScrubRegion(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU64 arg0, NvU64 arg1) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrAsyncScrubRegion(pGpu, pMemoryManager, arg0, arg1) memmgrAsyncScrubRegion_f2d351(pGpu, pMemoryManager, arg0, arg1)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrAsyncScrubRegion_HAL(pGpu, pMemoryManager, arg0, arg1) memmgrAsyncScrubRegion(pGpu, pMemoryManager, arg0, arg1)

static inline NV_STATUS memmgrScrubHandlePreSchedulingDisable_46f6a7(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return NV_ERR_NOT_SUPPORTED;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrScrubHandlePreSchedulingDisable(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrScrubHandlePreSchedulingDisable(pGpu, pMemoryManager) memmgrScrubHandlePreSchedulingDisable_46f6a7(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrScrubHandlePreSchedulingDisable_HAL(pGpu, pMemoryManager) memmgrScrubHandlePreSchedulingDisable(pGpu, pMemoryManager)

static inline void memmgrScrubDestroy_b3696a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline void memmgrScrubDestroy(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrScrubDestroy(pGpu, pMemoryManager) memmgrScrubDestroy_b3696a(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrScrubDestroy_HAL(pGpu, pMemoryManager) memmgrScrubDestroy(pGpu, pMemoryManager)

static inline void memmgrScrubMemory_b3696a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, RmPhysAddr arg0, NvU64 arg1) {
    return;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline void memmgrScrubMemory(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, RmPhysAddr arg0, NvU64 arg1) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrScrubMemory(pGpu, pMemoryManager, arg0, arg1) memmgrScrubMemory_b3696a(pGpu, pMemoryManager, arg0, arg1)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrScrubMemory_HAL(pGpu, pMemoryManager, arg0, arg1) memmgrScrubMemory(pGpu, pMemoryManager, arg0, arg1)

static inline NV_STATUS memmgrMemUtilsMemSetBlocking_56cd7a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, OBJCHANNEL *arg0, RmPhysAddr arg1, NvU64 arg2) {
    return NV_OK;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrMemUtilsMemSetBlocking(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, OBJCHANNEL *arg0, RmPhysAddr arg1, NvU64 arg2) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrMemUtilsMemSetBlocking(pGpu, pMemoryManager, arg0, arg1, arg2) memmgrMemUtilsMemSetBlocking_56cd7a(pGpu, pMemoryManager, arg0, arg1, arg2)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrMemUtilsMemSetBlocking_HAL(pGpu, pMemoryManager, arg0, arg1, arg2) memmgrMemUtilsMemSetBlocking(pGpu, pMemoryManager, arg0, arg1, arg2)

static inline NV_STATUS memmgrMemUtilsMemSet_56cd7a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, OBJCHANNEL *arg0, RmPhysAddr arg1, NvU64 arg2, NvU32 arg3, NvU32 *arg4) {
    return NV_OK;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrMemUtilsMemSet(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, OBJCHANNEL *arg0, RmPhysAddr arg1, NvU64 arg2, NvU32 arg3, NvU32 *arg4) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrMemUtilsMemSet(pGpu, pMemoryManager, arg0, arg1, arg2, arg3, arg4) memmgrMemUtilsMemSet_56cd7a(pGpu, pMemoryManager, arg0, arg1, arg2, arg3, arg4)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrMemUtilsMemSet_HAL(pGpu, pMemoryManager, arg0, arg1, arg2, arg3, arg4) memmgrMemUtilsMemSet(pGpu, pMemoryManager, arg0, arg1, arg2, arg3, arg4)

static inline NV_STATUS memmgrMemUtilsMemSetBatched_56cd7a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, OBJCHANNEL *arg0, RmPhysAddr arg1, NvU64 arg2) {
    return NV_OK;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrMemUtilsMemSetBatched(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, OBJCHANNEL *arg0, RmPhysAddr arg1, NvU64 arg2) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrMemUtilsMemSetBatched(pGpu, pMemoryManager, arg0, arg1, arg2) memmgrMemUtilsMemSetBatched_56cd7a(pGpu, pMemoryManager, arg0, arg1, arg2)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrMemUtilsMemSetBatched_HAL(pGpu, pMemoryManager, arg0, arg1, arg2) memmgrMemUtilsMemSetBatched(pGpu, pMemoryManager, arg0, arg1, arg2)

static inline NV_STATUS memmgrMemUtilsMemCopyBatched_56cd7a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, OBJCHANNEL *arg0, RmPhysAddr arg1, NV_ADDRESS_SPACE arg2, NvU32 arg3, RmPhysAddr arg4, NV_ADDRESS_SPACE arg5, NvU32 arg6, NvU64 arg7) {
    return NV_OK;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrMemUtilsMemCopyBatched(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, OBJCHANNEL *arg0, RmPhysAddr arg1, NV_ADDRESS_SPACE arg2, NvU32 arg3, RmPhysAddr arg4, NV_ADDRESS_SPACE arg5, NvU32 arg6, NvU64 arg7) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrMemUtilsMemCopyBatched(pGpu, pMemoryManager, arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7) memmgrMemUtilsMemCopyBatched_56cd7a(pGpu, pMemoryManager, arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrMemUtilsMemCopyBatched_HAL(pGpu, pMemoryManager, arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7) memmgrMemUtilsMemCopyBatched(pGpu, pMemoryManager, arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7)

static inline NV_STATUS memmgrMemUtilsAllocateEccScrubber_56cd7a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, OBJCHANNEL *arg0) {
    return NV_OK;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrMemUtilsAllocateEccScrubber(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, OBJCHANNEL *arg0) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrMemUtilsAllocateEccScrubber(pGpu, pMemoryManager, arg0) memmgrMemUtilsAllocateEccScrubber_56cd7a(pGpu, pMemoryManager, arg0)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrMemUtilsAllocateEccScrubber_HAL(pGpu, pMemoryManager, arg0) memmgrMemUtilsAllocateEccScrubber(pGpu, pMemoryManager, arg0)

static inline NV_STATUS memmgrMemUtilsAllocateEccAllocScrubber_56cd7a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, OBJCHANNEL *arg0) {
    return NV_OK;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrMemUtilsAllocateEccAllocScrubber(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, OBJCHANNEL *arg0) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrMemUtilsAllocateEccAllocScrubber(pGpu, pMemoryManager, arg0) memmgrMemUtilsAllocateEccAllocScrubber_56cd7a(pGpu, pMemoryManager, arg0)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrMemUtilsAllocateEccAllocScrubber_HAL(pGpu, pMemoryManager, arg0) memmgrMemUtilsAllocateEccAllocScrubber(pGpu, pMemoryManager, arg0)

static inline NV_STATUS memmgrMemUtilsChannelInitialize_56cd7a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, OBJCHANNEL *arg0) {
    return NV_OK;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrMemUtilsChannelInitialize(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, OBJCHANNEL *arg0) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrMemUtilsChannelInitialize(pGpu, pMemoryManager, arg0) memmgrMemUtilsChannelInitialize_56cd7a(pGpu, pMemoryManager, arg0)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrMemUtilsChannelInitialize_HAL(pGpu, pMemoryManager, arg0) memmgrMemUtilsChannelInitialize(pGpu, pMemoryManager, arg0)

static inline NV_STATUS memmgrMemUtilsCopyEngineInitialize_56cd7a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, OBJCHANNEL *arg0) {
    return NV_OK;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrMemUtilsCopyEngineInitialize(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, OBJCHANNEL *arg0) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrMemUtilsCopyEngineInitialize(pGpu, pMemoryManager, arg0) memmgrMemUtilsCopyEngineInitialize_56cd7a(pGpu, pMemoryManager, arg0)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrMemUtilsCopyEngineInitialize_HAL(pGpu, pMemoryManager, arg0) memmgrMemUtilsCopyEngineInitialize(pGpu, pMemoryManager, arg0)

static inline NV_STATUS memmgrMemUtilsGetCopyEngineClass_56cd7a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU32 *pClass) {
    return NV_OK;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrMemUtilsGetCopyEngineClass(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU32 *pClass) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrMemUtilsGetCopyEngineClass(pGpu, pMemoryManager, pClass) memmgrMemUtilsGetCopyEngineClass_56cd7a(pGpu, pMemoryManager, pClass)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrMemUtilsGetCopyEngineClass_HAL(pGpu, pMemoryManager, pClass) memmgrMemUtilsGetCopyEngineClass(pGpu, pMemoryManager, pClass)

static inline NV_STATUS memmgrMemUtilsCreateMemoryAlias_56cd7a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, OBJCHANNEL *arg0) {
    return NV_OK;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrMemUtilsCreateMemoryAlias(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, OBJCHANNEL *arg0) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrMemUtilsCreateMemoryAlias(pGpu, pMemoryManager, arg0) memmgrMemUtilsCreateMemoryAlias_56cd7a(pGpu, pMemoryManager, arg0)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrMemUtilsCreateMemoryAlias_HAL(pGpu, pMemoryManager, arg0) memmgrMemUtilsCreateMemoryAlias(pGpu, pMemoryManager, arg0)

static inline NV_STATUS memmgrAllocHal_92bfc3(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, FB_ALLOC_INFO *pFbAllocInfo) {
    NV_ASSERT_PRECOMP(0);
    return NV_ERR_NOT_SUPPORTED;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrAllocHal(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, FB_ALLOC_INFO *pFbAllocInfo) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrAllocHal(pGpu, pMemoryManager, pFbAllocInfo) memmgrAllocHal_92bfc3(pGpu, pMemoryManager, pFbAllocInfo)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrAllocHal_HAL(pGpu, pMemoryManager, pFbAllocInfo) memmgrAllocHal(pGpu, pMemoryManager, pFbAllocInfo)

static inline NV_STATUS memmgrFreeHal_92bfc3(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, FB_ALLOC_INFO *pFbAllocInfo, PRMTIMEOUT pTimeout) {
    NV_ASSERT_PRECOMP(0);
    return NV_ERR_NOT_SUPPORTED;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrFreeHal(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, FB_ALLOC_INFO *pFbAllocInfo, PRMTIMEOUT pTimeout) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrFreeHal(pGpu, pMemoryManager, pFbAllocInfo, pTimeout) memmgrFreeHal_92bfc3(pGpu, pMemoryManager, pFbAllocInfo, pTimeout)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrFreeHal_HAL(pGpu, pMemoryManager, pFbAllocInfo, pTimeout) memmgrFreeHal(pGpu, pMemoryManager, pFbAllocInfo, pTimeout)

static inline NV_STATUS memmgrUpdateSurfaceCompression_5baef9(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, Memory *arg0, NvBool arg1) {
    NV_ASSERT_OR_RETURN_PRECOMP(0, NV_ERR_NOT_SUPPORTED);
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrUpdateSurfaceCompression(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, Memory *arg0, NvBool arg1) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrUpdateSurfaceCompression(pGpu, pMemoryManager, arg0, arg1) memmgrUpdateSurfaceCompression_5baef9(pGpu, pMemoryManager, arg0, arg1)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrUpdateSurfaceCompression_HAL(pGpu, pMemoryManager, arg0, arg1) memmgrUpdateSurfaceCompression(pGpu, pMemoryManager, arg0, arg1)

static inline NV_STATUS memmgrGetBankPlacementData_46f6a7(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU32 *pBankPlacementLowData) {
    return NV_ERR_NOT_SUPPORTED;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrGetBankPlacementData(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU32 *pBankPlacementLowData) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetBankPlacementData(pGpu, pMemoryManager, pBankPlacementLowData) memmgrGetBankPlacementData_46f6a7(pGpu, pMemoryManager, pBankPlacementLowData)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetBankPlacementData_HAL(pGpu, pMemoryManager, pBankPlacementLowData) memmgrGetBankPlacementData(pGpu, pMemoryManager, pBankPlacementLowData)

static inline void memmgrDirtyForPmTest_b3696a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvBool partialDirty) {
    return;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline void memmgrDirtyForPmTest(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvBool partialDirty) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrDirtyForPmTest(pGpu, pMemoryManager, partialDirty) memmgrDirtyForPmTest_b3696a(pGpu, pMemoryManager, partialDirty)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrDirtyForPmTest_HAL(pGpu, pMemoryManager, partialDirty) memmgrDirtyForPmTest(pGpu, pMemoryManager, partialDirty)

static inline NvU32 memmgrGetReservedHeapSizeMb_4a4dee(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return 0;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvU32 memmgrGetReservedHeapSizeMb(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return 0;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetReservedHeapSizeMb(pGpu, pMemoryManager) memmgrGetReservedHeapSizeMb_4a4dee(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetReservedHeapSizeMb_HAL(pGpu, pMemoryManager) memmgrGetReservedHeapSizeMb(pGpu, pMemoryManager)

static inline NV_STATUS memmgrAllocDetermineAlignment_5baef9(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU64 *pMemSize, NvU64 *pAlign, NvU64 alignPad, NvU32 allocFlags, NvU32 retAttr, NvU32 retAttr2, NvU64 hwAlignment) {
    NV_ASSERT_OR_RETURN_PRECOMP(0, NV_ERR_NOT_SUPPORTED);
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrAllocDetermineAlignment(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU64 *pMemSize, NvU64 *pAlign, NvU64 alignPad, NvU32 allocFlags, NvU32 retAttr, NvU32 retAttr2, NvU64 hwAlignment) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrAllocDetermineAlignment(pGpu, pMemoryManager, pMemSize, pAlign, alignPad, allocFlags, retAttr, retAttr2, hwAlignment) memmgrAllocDetermineAlignment_5baef9(pGpu, pMemoryManager, pMemSize, pAlign, alignPad, allocFlags, retAttr, retAttr2, hwAlignment)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrAllocDetermineAlignment_HAL(pGpu, pMemoryManager, pMemSize, pAlign, alignPad, allocFlags, retAttr, retAttr2, hwAlignment) memmgrAllocDetermineAlignment(pGpu, pMemoryManager, pMemSize, pAlign, alignPad, allocFlags, retAttr, retAttr2, hwAlignment)

static inline NV_STATUS memmgrInitFbRegionsHal_56cd7a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return NV_OK;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrInitFbRegionsHal(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrInitFbRegionsHal(pGpu, pMemoryManager) memmgrInitFbRegionsHal_56cd7a(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrInitFbRegionsHal_HAL(pGpu, pMemoryManager) memmgrInitFbRegionsHal(pGpu, pMemoryManager)

static inline NvU64 memmgrGetMaxContextSize_4a4dee(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return 0;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvU64 memmgrGetMaxContextSize(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return 0;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetMaxContextSize(pGpu, pMemoryManager) memmgrGetMaxContextSize_4a4dee(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetMaxContextSize_HAL(pGpu, pMemoryManager) memmgrGetMaxContextSize(pGpu, pMemoryManager)

static inline void memmgrHandleSizeOverrides_b3696a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline void memmgrHandleSizeOverrides(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrHandleSizeOverrides(pGpu, pMemoryManager) memmgrHandleSizeOverrides_b3696a(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrHandleSizeOverrides_HAL(pGpu, pMemoryManager) memmgrHandleSizeOverrides(pGpu, pMemoryManager)

static inline NV_STATUS memmgrFinishHandleSizeOverrides_56cd7a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return NV_OK;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrFinishHandleSizeOverrides(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrFinishHandleSizeOverrides(pGpu, pMemoryManager) memmgrFinishHandleSizeOverrides_56cd7a(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrFinishHandleSizeOverrides_HAL(pGpu, pMemoryManager) memmgrFinishHandleSizeOverrides(pGpu, pMemoryManager)

static inline NV_STATUS memmgrGetBAR1InfoForClient_46f6a7(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvHandle arg0, PGETBAR1INFO bar1Info) {
    return NV_ERR_NOT_SUPPORTED;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrGetBAR1InfoForClient(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvHandle arg0, PGETBAR1INFO bar1Info) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetBAR1InfoForClient(pGpu, pMemoryManager, arg0, bar1Info) memmgrGetBAR1InfoForClient_46f6a7(pGpu, pMemoryManager, arg0, bar1Info)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetBAR1InfoForClient_HAL(pGpu, pMemoryManager, arg0, bar1Info) memmgrGetBAR1InfoForClient(pGpu, pMemoryManager, arg0, bar1Info)

static inline NvU64 memmgrGetFbTaxSize_4a4dee(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return 0;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvU64 memmgrGetFbTaxSize(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return 0;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetFbTaxSize(pGpu, pMemoryManager) memmgrGetFbTaxSize_4a4dee(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetFbTaxSize_HAL(pGpu, pMemoryManager) memmgrGetFbTaxSize(pGpu, pMemoryManager)

static inline NvU64 memmgrGetVgpuHostRmReservedFb_4a4dee(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU32 vgpuTypeId) {
    return 0;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvU64 memmgrGetVgpuHostRmReservedFb(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU32 vgpuTypeId) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return 0;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetVgpuHostRmReservedFb(pGpu, pMemoryManager, vgpuTypeId) memmgrGetVgpuHostRmReservedFb_4a4dee(pGpu, pMemoryManager, vgpuTypeId)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetVgpuHostRmReservedFb_HAL(pGpu, pMemoryManager, vgpuTypeId) memmgrGetVgpuHostRmReservedFb(pGpu, pMemoryManager, vgpuTypeId)

static inline void memmgrScrubRegistryOverrides_b3696a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline void memmgrScrubRegistryOverrides(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrScrubRegistryOverrides(pGpu, pMemoryManager) memmgrScrubRegistryOverrides_b3696a(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrScrubRegistryOverrides_HAL(pGpu, pMemoryManager) memmgrScrubRegistryOverrides(pGpu, pMemoryManager)

static inline NvU64 memmgrGetRsvdSizeForSr_4a4dee(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return 0;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvU64 memmgrGetRsvdSizeForSr(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return 0;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetRsvdSizeForSr(pGpu, pMemoryManager) memmgrGetRsvdSizeForSr_4a4dee(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetRsvdSizeForSr_HAL(pGpu, pMemoryManager) memmgrGetRsvdSizeForSr(pGpu, pMemoryManager)

static inline NvBool memmgrVerifyDepthSurfaceAttrs_cbe027(struct MemoryManager *pMemoryManager, NvU32 arg0, NvU32 arg1) {
    return ((NvBool)(0 == 0));
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvBool memmgrVerifyDepthSurfaceAttrs(struct MemoryManager *pMemoryManager, NvU32 arg0, NvU32 arg1) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_FALSE;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrVerifyDepthSurfaceAttrs(pMemoryManager, arg0, arg1) memmgrVerifyDepthSurfaceAttrs_cbe027(pMemoryManager, arg0, arg1)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrVerifyDepthSurfaceAttrs_HAL(pMemoryManager, arg0, arg1) memmgrVerifyDepthSurfaceAttrs(pMemoryManager, arg0, arg1)

static inline NV_STATUS memmgrAllocMemToSaveVgaWorkspace_5baef9(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, MEMORY_DESCRIPTOR **arg0, MEMORY_DESCRIPTOR **arg1) {
    NV_ASSERT_OR_RETURN_PRECOMP(0, NV_ERR_NOT_SUPPORTED);
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrAllocMemToSaveVgaWorkspace(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, MEMORY_DESCRIPTOR **arg0, MEMORY_DESCRIPTOR **arg1) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrAllocMemToSaveVgaWorkspace(pGpu, pMemoryManager, arg0, arg1) memmgrAllocMemToSaveVgaWorkspace_5baef9(pGpu, pMemoryManager, arg0, arg1)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrAllocMemToSaveVgaWorkspace_HAL(pGpu, pMemoryManager, arg0, arg1) memmgrAllocMemToSaveVgaWorkspace(pGpu, pMemoryManager, arg0, arg1)

static inline NvBool memmgrComparePhysicalAddresses_108313(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU32 arg0, NvU64 arg1, NvU32 arg2, NvU64 arg3) {
    NV_ASSERT_OR_RETURN_PRECOMP(0, ((NvBool)(0 != 0)));
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvBool memmgrComparePhysicalAddresses(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU32 arg0, NvU64 arg1, NvU32 arg2, NvU64 arg3) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_FALSE;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrComparePhysicalAddresses(pGpu, pMemoryManager, arg0, arg1, arg2, arg3) memmgrComparePhysicalAddresses_108313(pGpu, pMemoryManager, arg0, arg1, arg2, arg3)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrComparePhysicalAddresses_HAL(pGpu, pMemoryManager, arg0, arg1, arg2, arg3) memmgrComparePhysicalAddresses(pGpu, pMemoryManager, arg0, arg1, arg2, arg3)

static inline RmPhysAddr memmgrGetInvalidOffset_c732fb(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return 4294967295U;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline RmPhysAddr memmgrGetInvalidOffset(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    RmPhysAddr ret;
    portMemSet(&ret, 0, sizeof(RmPhysAddr));
    return ret;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetInvalidOffset(pGpu, pMemoryManager) memmgrGetInvalidOffset_c732fb(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetInvalidOffset_HAL(pGpu, pMemoryManager) memmgrGetInvalidOffset(pGpu, pMemoryManager)

static inline NvU32 memmgrGetAddrSpaceSizeMB_474d46(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_OR_RETURN_PRECOMP(0, 0);
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvU32 memmgrGetAddrSpaceSizeMB(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return 0;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetAddrSpaceSizeMB(pGpu, pMemoryManager) memmgrGetAddrSpaceSizeMB_474d46(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetAddrSpaceSizeMB_HAL(pGpu, pMemoryManager) memmgrGetAddrSpaceSizeMB(pGpu, pMemoryManager)

static inline NvU32 memmgrGetUsableMemSizeMB_13cd8d(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_PRECOMP(0);
    return 0;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvU32 memmgrGetUsableMemSizeMB(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return 0;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetUsableMemSizeMB(pGpu, pMemoryManager) memmgrGetUsableMemSizeMB_13cd8d(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetUsableMemSizeMB_HAL(pGpu, pMemoryManager) memmgrGetUsableMemSizeMB(pGpu, pMemoryManager)

static inline NV_STATUS memmgrGetSurfacePhysAttr_dffb6f(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, Memory *pMemory, NvU64 *pOffset, NvU32 *pMemAperture, NvU32 *pMemKind, NvU32 *pComprOffset, NvU32 *pComprKind, NvU32 *pLineMin, NvU32 *pLineMax, NvU32 *pZCullId, NvU32 *pGpuCacheAttr, NvU32 *pGpuP2PCacheAttr, NvU64 *contigSegmentSize) {
    NV_ASSERT_PRECOMP(0);
    return NV_OK;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrGetSurfacePhysAttr(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, Memory *pMemory, NvU64 *pOffset, NvU32 *pMemAperture, NvU32 *pMemKind, NvU32 *pComprOffset, NvU32 *pComprKind, NvU32 *pLineMin, NvU32 *pLineMax, NvU32 *pZCullId, NvU32 *pGpuCacheAttr, NvU32 *pGpuP2PCacheAttr, NvU64 *contigSegmentSize) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetSurfacePhysAttr(pGpu, pMemoryManager, pMemory, pOffset, pMemAperture, pMemKind, pComprOffset, pComprKind, pLineMin, pLineMax, pZCullId, pGpuCacheAttr, pGpuP2PCacheAttr, contigSegmentSize) memmgrGetSurfacePhysAttr_dffb6f(pGpu, pMemoryManager, pMemory, pOffset, pMemAperture, pMemKind, pComprOffset, pComprKind, pLineMin, pLineMax, pZCullId, pGpuCacheAttr, pGpuP2PCacheAttr, contigSegmentSize)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetSurfacePhysAttr_HAL(pGpu, pMemoryManager, pMemory, pOffset, pMemAperture, pMemKind, pComprOffset, pComprKind, pLineMin, pLineMax, pZCullId, pGpuCacheAttr, pGpuP2PCacheAttr, contigSegmentSize) memmgrGetSurfacePhysAttr(pGpu, pMemoryManager, pMemory, pOffset, pMemAperture, pMemKind, pComprOffset, pComprKind, pLineMin, pLineMax, pZCullId, pGpuCacheAttr, pGpuP2PCacheAttr, contigSegmentSize)

static inline NvBool memmgrVerifyComprAttrs_cbe027(struct MemoryManager *pMemoryManager, NvU32 arg0, NvU32 arg1, NvU32 arg2) {
    return ((NvBool)(0 == 0));
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvBool memmgrVerifyComprAttrs(struct MemoryManager *pMemoryManager, NvU32 arg0, NvU32 arg1, NvU32 arg2) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_FALSE;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrVerifyComprAttrs(pMemoryManager, arg0, arg1, arg2) memmgrVerifyComprAttrs_cbe027(pMemoryManager, arg0, arg1, arg2)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrVerifyComprAttrs_HAL(pMemoryManager, arg0, arg1, arg2) memmgrVerifyComprAttrs(pMemoryManager, arg0, arg1, arg2)

static inline NvBool memmgrIsKindCompressible_491d52(struct MemoryManager *pMemoryManager, NvU32 arg0) {
    return ((NvBool)(0 != 0));
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvBool memmgrIsKindCompressible(struct MemoryManager *pMemoryManager, NvU32 arg0) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_FALSE;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrIsKindCompressible(pMemoryManager, arg0) memmgrIsKindCompressible_491d52(pMemoryManager, arg0)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrIsKindCompressible_HAL(pMemoryManager, arg0) memmgrIsKindCompressible(pMemoryManager, arg0)

static inline NvBool memmgrIsKindBlocklinear_491d52(struct MemoryManager *pMemoryManager, NvU32 arg0) {
    return ((NvBool)(0 != 0));
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvBool memmgrIsKindBlocklinear(struct MemoryManager *pMemoryManager, NvU32 arg0) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_FALSE;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrIsKindBlocklinear(pMemoryManager, arg0) memmgrIsKindBlocklinear_491d52(pMemoryManager, arg0)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrIsKindBlocklinear_HAL(pMemoryManager, arg0) memmgrIsKindBlocklinear(pMemoryManager, arg0)

static inline NvU32 memmgrGetPteKindBl_4a4dee(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return 0;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvU32 memmgrGetPteKindBl(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return 0;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetPteKindBl(pGpu, pMemoryManager) memmgrGetPteKindBl_4a4dee(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetPteKindBl_HAL(pGpu, pMemoryManager) memmgrGetPteKindBl(pGpu, pMemoryManager)

static inline NvU32 memmgrGetPteKindPitch_4a4dee(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return 0;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvU32 memmgrGetPteKindPitch(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return 0;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetPteKindPitch(pGpu, pMemoryManager) memmgrGetPteKindPitch_4a4dee(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetPteKindPitch_HAL(pGpu, pMemoryManager) memmgrGetPteKindPitch(pGpu, pMemoryManager)

static inline NvU32 memmgrChooseKindZ_474d46(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, FB_ALLOC_PAGE_FORMAT *arg0) {
    NV_ASSERT_OR_RETURN_PRECOMP(0, 0);
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvU32 memmgrChooseKindZ(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, FB_ALLOC_PAGE_FORMAT *arg0) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return 0;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrChooseKindZ(pGpu, pMemoryManager, arg0) memmgrChooseKindZ_474d46(pGpu, pMemoryManager, arg0)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrChooseKindZ_HAL(pGpu, pMemoryManager, arg0) memmgrChooseKindZ(pGpu, pMemoryManager, arg0)

static inline NvU32 memmgrChooseKindCompressZ_474d46(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, FB_ALLOC_PAGE_FORMAT *arg0) {
    NV_ASSERT_OR_RETURN_PRECOMP(0, 0);
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvU32 memmgrChooseKindCompressZ(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, FB_ALLOC_PAGE_FORMAT *arg0) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return 0;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrChooseKindCompressZ(pGpu, pMemoryManager, arg0) memmgrChooseKindCompressZ_474d46(pGpu, pMemoryManager, arg0)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrChooseKindCompressZ_HAL(pGpu, pMemoryManager, arg0) memmgrChooseKindCompressZ(pGpu, pMemoryManager, arg0)

static inline NvU32 memmgrChooseKindCompressC_474d46(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, FB_ALLOC_PAGE_FORMAT *arg0) {
    NV_ASSERT_OR_RETURN_PRECOMP(0, 0);
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvU32 memmgrChooseKindCompressC(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, FB_ALLOC_PAGE_FORMAT *arg0) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return 0;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrChooseKindCompressC(pGpu, pMemoryManager, arg0) memmgrChooseKindCompressC_474d46(pGpu, pMemoryManager, arg0)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrChooseKindCompressC_HAL(pGpu, pMemoryManager, arg0) memmgrChooseKindCompressC(pGpu, pMemoryManager, arg0)

static inline NvU32 memmgrChooseKindCompressCForMS2_4a4dee(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU32 arg0) {
    return 0;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvU32 memmgrChooseKindCompressCForMS2(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU32 arg0) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return 0;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrChooseKindCompressCForMS2(pGpu, pMemoryManager, arg0) memmgrChooseKindCompressCForMS2_4a4dee(pGpu, pMemoryManager, arg0)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrChooseKindCompressCForMS2_HAL(pGpu, pMemoryManager, arg0) memmgrChooseKindCompressCForMS2(pGpu, pMemoryManager, arg0)

static inline NvU32 memmgrGetUncompressedKind_474d46(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU32 kind, NvBool releaseReacquire) {
    NV_ASSERT_OR_RETURN_PRECOMP(0, 0);
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvU32 memmgrGetUncompressedKind(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU32 kind, NvBool releaseReacquire) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return 0;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetUncompressedKind(pGpu, pMemoryManager, kind, releaseReacquire) memmgrGetUncompressedKind_474d46(pGpu, pMemoryManager, kind, releaseReacquire)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetUncompressedKind_HAL(pGpu, pMemoryManager, kind, releaseReacquire) memmgrGetUncompressedKind(pGpu, pMemoryManager, kind, releaseReacquire)

static inline NV_STATUS memmgrGetUncompressedKindForMS2_5baef9(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU32 arg0, NvU32 *arg1) {
    NV_ASSERT_OR_RETURN_PRECOMP(0, NV_ERR_NOT_SUPPORTED);
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrGetUncompressedKindForMS2(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU32 arg0, NvU32 *arg1) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetUncompressedKindForMS2(pGpu, pMemoryManager, arg0, arg1) memmgrGetUncompressedKindForMS2_5baef9(pGpu, pMemoryManager, arg0, arg1)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetUncompressedKindForMS2_HAL(pGpu, pMemoryManager, arg0, arg1) memmgrGetUncompressedKindForMS2(pGpu, pMemoryManager, arg0, arg1)

static inline NV_STATUS memmgrChooseKind_474d46(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, FB_ALLOC_PAGE_FORMAT *arg0, NvU32 arg1, NvU32 *arg2) {
    NV_ASSERT_OR_RETURN_PRECOMP(0, 0);
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrChooseKind(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, FB_ALLOC_PAGE_FORMAT *arg0, NvU32 arg1, NvU32 *arg2) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrChooseKind(pGpu, pMemoryManager, arg0, arg1, arg2) memmgrChooseKind_474d46(pGpu, pMemoryManager, arg0, arg1, arg2)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrChooseKind_HAL(pGpu, pMemoryManager, arg0, arg1, arg2) memmgrChooseKind(pGpu, pMemoryManager, arg0, arg1, arg2)

NvBool memmgrIsKind_TU102(struct MemoryManager *pMemoryManager, FB_IS_KIND_OP arg0, NvU32 arg1);

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvBool memmgrIsKind(struct MemoryManager *pMemoryManager, FB_IS_KIND_OP arg0, NvU32 arg1) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_FALSE;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrIsKind(pMemoryManager, arg0, arg1) memmgrIsKind_TU102(pMemoryManager, arg0, arg1)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrIsKind_HAL(pMemoryManager, arg0, arg1) memmgrIsKind(pMemoryManager, arg0, arg1)

static inline NvU32 memmgrGetMessageKind_4a4dee(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return 0;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvU32 memmgrGetMessageKind(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return 0;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetMessageKind(pGpu, pMemoryManager) memmgrGetMessageKind_4a4dee(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetMessageKind_HAL(pGpu, pMemoryManager) memmgrGetMessageKind(pGpu, pMemoryManager)

static inline NvU32 memmgrGetDefaultPteKindForNoHandle_4a4dee(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return 0;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvU32 memmgrGetDefaultPteKindForNoHandle(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return 0;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetDefaultPteKindForNoHandle(pGpu, pMemoryManager) memmgrGetDefaultPteKindForNoHandle_4a4dee(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetDefaultPteKindForNoHandle_HAL(pGpu, pMemoryManager) memmgrGetDefaultPteKindForNoHandle(pGpu, pMemoryManager)

NvBool memmgrIsSurfaceBlockLinear_TU102(struct MemoryManager *pMemoryManager, Memory *arg0, NvU32 arg1, NvU32 arg2);

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvBool memmgrIsSurfaceBlockLinear(struct MemoryManager *pMemoryManager, Memory *arg0, NvU32 arg1, NvU32 arg2) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_FALSE;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrIsSurfaceBlockLinear(pMemoryManager, arg0, arg1, arg2) memmgrIsSurfaceBlockLinear_TU102(pMemoryManager, arg0, arg1, arg2)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrIsSurfaceBlockLinear_HAL(pMemoryManager, arg0, arg1, arg2) memmgrIsSurfaceBlockLinear(pMemoryManager, arg0, arg1, arg2)

static inline NV_STATUS memmgrGetFlaKind_46f6a7(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU32 *arg0) {
    return NV_ERR_NOT_SUPPORTED;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrGetFlaKind(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU32 *arg0) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetFlaKind(pGpu, pMemoryManager, arg0) memmgrGetFlaKind_46f6a7(pGpu, pMemoryManager, arg0)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetFlaKind_HAL(pGpu, pMemoryManager, arg0) memmgrGetFlaKind(pGpu, pMemoryManager, arg0)

static inline NvU32 memmgrGetHwPteKindFromSwPteKind_6a0a80(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU32 pteKind) {
    return pteKind;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvU32 memmgrGetHwPteKindFromSwPteKind(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU32 pteKind) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return 0;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetHwPteKindFromSwPteKind(pGpu, pMemoryManager, pteKind) memmgrGetHwPteKindFromSwPteKind_6a0a80(pGpu, pMemoryManager, pteKind)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetHwPteKindFromSwPteKind_HAL(pGpu, pMemoryManager, pteKind) memmgrGetHwPteKindFromSwPteKind(pGpu, pMemoryManager, pteKind)

static inline NvU32 memmgrGetSwPteKindFromHwPteKind_6a0a80(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU32 pteKind) {
    return pteKind;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvU32 memmgrGetSwPteKindFromHwPteKind(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU32 pteKind) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return 0;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetSwPteKindFromHwPteKind(pGpu, pMemoryManager, pteKind) memmgrGetSwPteKindFromHwPteKind_6a0a80(pGpu, pMemoryManager, pteKind)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetSwPteKindFromHwPteKind_HAL(pGpu, pMemoryManager, pteKind) memmgrGetSwPteKindFromHwPteKind(pGpu, pMemoryManager, pteKind)

static inline void memmgrGetPteKindForScrubber_f2d351(struct MemoryManager *pMemoryManager, NvU32 *arg0) {
    NV_ASSERT_PRECOMP(0);
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline void memmgrGetPteKindForScrubber(struct MemoryManager *pMemoryManager, NvU32 *arg0) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetPteKindForScrubber(pMemoryManager, arg0) memmgrGetPteKindForScrubber_f2d351(pMemoryManager, arg0)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetPteKindForScrubber_HAL(pMemoryManager, arg0) memmgrGetPteKindForScrubber(pMemoryManager, arg0)

static inline NvU32 memmgrGetCtagOffsetFromParams_1a0c2b(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, FB_ALLOC_INFO *arg0) {
    return -1;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvU32 memmgrGetCtagOffsetFromParams(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, FB_ALLOC_INFO *arg0) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return 0;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetCtagOffsetFromParams(pGpu, pMemoryManager, arg0) memmgrGetCtagOffsetFromParams_1a0c2b(pGpu, pMemoryManager, arg0)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetCtagOffsetFromParams_HAL(pGpu, pMemoryManager, arg0) memmgrGetCtagOffsetFromParams(pGpu, pMemoryManager, arg0)

static inline void memmgrSetCtagOffsetInParams_b3696a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, FB_ALLOC_INFO *arg0, NvU32 arg1) {
    return;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline void memmgrSetCtagOffsetInParams(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, FB_ALLOC_INFO *arg0, NvU32 arg1) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrSetCtagOffsetInParams(pGpu, pMemoryManager, arg0, arg1) memmgrSetCtagOffsetInParams_b3696a(pGpu, pMemoryManager, arg0, arg1)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrSetCtagOffsetInParams_HAL(pGpu, pMemoryManager, arg0, arg1) memmgrSetCtagOffsetInParams(pGpu, pMemoryManager, arg0, arg1)

static inline NvU32 memmgrDetermineComptag_13cd8d(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, RmPhysAddr arg0) {
    NV_ASSERT_PRECOMP(0);
    return 0;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvU32 memmgrDetermineComptag(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, RmPhysAddr arg0) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return 0;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrDetermineComptag(pGpu, pMemoryManager, arg0) memmgrDetermineComptag_13cd8d(pGpu, pMemoryManager, arg0)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrDetermineComptag_HAL(pGpu, pMemoryManager, arg0) memmgrDetermineComptag(pGpu, pMemoryManager, arg0)

static inline void memmgrChannelPushSemaphoreMethodsBlock_b3696a(struct MemoryManager *pMemoryManager, NvU32 arg0, NvU64 arg1, NvU32 arg2, NvU32 **arg3) {
    return;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline void memmgrChannelPushSemaphoreMethodsBlock(struct MemoryManager *pMemoryManager, NvU32 arg0, NvU64 arg1, NvU32 arg2, NvU32 **arg3) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrChannelPushSemaphoreMethodsBlock(pMemoryManager, arg0, arg1, arg2, arg3) memmgrChannelPushSemaphoreMethodsBlock_b3696a(pMemoryManager, arg0, arg1, arg2, arg3)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrChannelPushSemaphoreMethodsBlock_HAL(pMemoryManager, arg0, arg1, arg2, arg3) memmgrChannelPushSemaphoreMethodsBlock(pMemoryManager, arg0, arg1, arg2, arg3)

static inline void memmgrChannelPushAddressMethodsBlock_b3696a(struct MemoryManager *pMemoryManager, NvBool arg0, NvU32 arg1, RmPhysAddr arg2, NvU32 **arg3) {
    return;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline void memmgrChannelPushAddressMethodsBlock(struct MemoryManager *pMemoryManager, NvBool arg0, NvU32 arg1, RmPhysAddr arg2, NvU32 **arg3) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrChannelPushAddressMethodsBlock(pMemoryManager, arg0, arg1, arg2, arg3) memmgrChannelPushAddressMethodsBlock_b3696a(pMemoryManager, arg0, arg1, arg2, arg3)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrChannelPushAddressMethodsBlock_HAL(pMemoryManager, arg0, arg1, arg2, arg3) memmgrChannelPushAddressMethodsBlock(pMemoryManager, arg0, arg1, arg2, arg3)

static inline NV_STATUS memmgrScrubMapDoorbellRegion_56cd7a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, OBJCHANNEL *arg0) {
    return NV_OK;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrScrubMapDoorbellRegion(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, OBJCHANNEL *arg0) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrScrubMapDoorbellRegion(pGpu, pMemoryManager, arg0) memmgrScrubMapDoorbellRegion_56cd7a(pGpu, pMemoryManager, arg0)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrScrubMapDoorbellRegion_HAL(pGpu, pMemoryManager, arg0) memmgrScrubMapDoorbellRegion(pGpu, pMemoryManager, arg0)

static inline NV_STATUS memmgrSetAllocParameters_dffb6f(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, FB_ALLOC_INFO *pFbAllocInfo) {
    NV_ASSERT_PRECOMP(0);
    return NV_OK;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrSetAllocParameters(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, FB_ALLOC_INFO *pFbAllocInfo) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrSetAllocParameters(pGpu, pMemoryManager, pFbAllocInfo) memmgrSetAllocParameters_dffb6f(pGpu, pMemoryManager, pFbAllocInfo)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrSetAllocParameters_HAL(pGpu, pMemoryManager, pFbAllocInfo) memmgrSetAllocParameters(pGpu, pMemoryManager, pFbAllocInfo)

static inline void memmgrCalcReservedFbSpaceForUVM_b3696a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU64 *arg0) {
    return;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline void memmgrCalcReservedFbSpaceForUVM(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU64 *arg0) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrCalcReservedFbSpaceForUVM(pGpu, pMemoryManager, arg0) memmgrCalcReservedFbSpaceForUVM_b3696a(pGpu, pMemoryManager, arg0)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrCalcReservedFbSpaceForUVM_HAL(pGpu, pMemoryManager, arg0) memmgrCalcReservedFbSpaceForUVM(pGpu, pMemoryManager, arg0)

static inline void memmgrCalcReservedFbSpaceHal_b3696a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU64 *arg0, NvU64 *arg1, NvU64 *arg2) {
    return;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline void memmgrCalcReservedFbSpaceHal(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU64 *arg0, NvU64 *arg1, NvU64 *arg2) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrCalcReservedFbSpaceHal(pGpu, pMemoryManager, arg0, arg1, arg2) memmgrCalcReservedFbSpaceHal_b3696a(pGpu, pMemoryManager, arg0, arg1, arg2)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrCalcReservedFbSpaceHal_HAL(pGpu, pMemoryManager, arg0, arg1, arg2) memmgrCalcReservedFbSpaceHal(pGpu, pMemoryManager, arg0, arg1, arg2)

static inline NvU32 memmgrGetGrHeapReservationSize_4a4dee(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return 0;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvU32 memmgrGetGrHeapReservationSize(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return 0;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetGrHeapReservationSize(pGpu, pMemoryManager) memmgrGetGrHeapReservationSize_4a4dee(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetGrHeapReservationSize_HAL(pGpu, pMemoryManager) memmgrGetGrHeapReservationSize(pGpu, pMemoryManager)

static inline NvU32 memmgrGetRunlistEntriesReservedFbSpace_4a4dee(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return 0;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvU32 memmgrGetRunlistEntriesReservedFbSpace(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return 0;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetRunlistEntriesReservedFbSpace(pGpu, pMemoryManager) memmgrGetRunlistEntriesReservedFbSpace_4a4dee(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetRunlistEntriesReservedFbSpace_HAL(pGpu, pMemoryManager) memmgrGetRunlistEntriesReservedFbSpace(pGpu, pMemoryManager)

static inline NvU32 memmgrGetUserdReservedFbSpace_4a4dee(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return 0;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NvU32 memmgrGetUserdReservedFbSpace(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return 0;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetUserdReservedFbSpace(pGpu, pMemoryManager) memmgrGetUserdReservedFbSpace_4a4dee(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetUserdReservedFbSpace_HAL(pGpu, pMemoryManager) memmgrGetUserdReservedFbSpace(pGpu, pMemoryManager)

static inline NV_STATUS memmgrCheckReservedMemorySize_56cd7a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return NV_OK;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrCheckReservedMemorySize(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrCheckReservedMemorySize(pGpu, pMemoryManager) memmgrCheckReservedMemorySize_56cd7a(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrCheckReservedMemorySize_HAL(pGpu, pMemoryManager) memmgrCheckReservedMemorySize(pGpu, pMemoryManager)

static inline NV_STATUS memmgrInitReservedMemory_56cd7a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU64 arg0) {
    return NV_OK;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrInitReservedMemory(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU64 arg0) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrInitReservedMemory(pGpu, pMemoryManager, arg0) memmgrInitReservedMemory_56cd7a(pGpu, pMemoryManager, arg0)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrInitReservedMemory_HAL(pGpu, pMemoryManager, arg0) memmgrInitReservedMemory(pGpu, pMemoryManager, arg0)

static inline NV_STATUS memmgrPreInitReservedMemory_56cd7a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return NV_OK;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrPreInitReservedMemory(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrPreInitReservedMemory(pGpu, pMemoryManager) memmgrPreInitReservedMemory_56cd7a(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrPreInitReservedMemory_HAL(pGpu, pMemoryManager) memmgrPreInitReservedMemory(pGpu, pMemoryManager)

static inline NV_STATUS memmgrReadMmuLock_e133c0(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvBool *pbIsValid, NvU64 *pMmuLockLo, NvU64 *pMmuLockHi) {
    *pbIsValid = ((NvBool)(0 != 0));
    return NV_OK;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrReadMmuLock(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvBool *pbIsValid, NvU64 *pMmuLockLo, NvU64 *pMmuLockHi) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrReadMmuLock(pGpu, pMemoryManager, pbIsValid, pMmuLockLo, pMmuLockHi) memmgrReadMmuLock_e133c0(pGpu, pMemoryManager, pbIsValid, pMmuLockLo, pMmuLockHi)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrReadMmuLock_HAL(pGpu, pMemoryManager, pbIsValid, pMmuLockLo, pMmuLockHi) memmgrReadMmuLock(pGpu, pMemoryManager, pbIsValid, pMmuLockLo, pMmuLockHi)

static inline NV_STATUS memmgrBlockMemLockedMemory_56cd7a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return NV_OK;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrBlockMemLockedMemory(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrBlockMemLockedMemory(pGpu, pMemoryManager) memmgrBlockMemLockedMemory_56cd7a(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrBlockMemLockedMemory_HAL(pGpu, pMemoryManager) memmgrBlockMemLockedMemory(pGpu, pMemoryManager)

static inline NV_STATUS memmgrInsertUnprotectedRegionAtBottomOfFb_56cd7a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU64 *pSize) {
    return NV_OK;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrInsertUnprotectedRegionAtBottomOfFb(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU64 *pSize) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrInsertUnprotectedRegionAtBottomOfFb(pGpu, pMemoryManager, pSize) memmgrInsertUnprotectedRegionAtBottomOfFb_56cd7a(pGpu, pMemoryManager, pSize)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrInsertUnprotectedRegionAtBottomOfFb_HAL(pGpu, pMemoryManager, pSize) memmgrInsertUnprotectedRegionAtBottomOfFb(pGpu, pMemoryManager, pSize)

NV_STATUS memmgrInitBaseFbRegions_FWCLIENT(OBJGPU *pGpu, struct MemoryManager *pMemoryManager);

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrInitBaseFbRegions(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrInitBaseFbRegions(pGpu, pMemoryManager) memmgrInitBaseFbRegions_FWCLIENT(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrInitBaseFbRegions_HAL(pGpu, pMemoryManager) memmgrInitBaseFbRegions(pGpu, pMemoryManager)

static inline void memmgrGetDisablePlcKind_b3696a(struct MemoryManager *pMemoryManager, NvU32 *pteKind) {
    return;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline void memmgrGetDisablePlcKind(struct MemoryManager *pMemoryManager, NvU32 *pteKind) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetDisablePlcKind(pMemoryManager, pteKind) memmgrGetDisablePlcKind_b3696a(pMemoryManager, pteKind)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetDisablePlcKind_HAL(pMemoryManager, pteKind) memmgrGetDisablePlcKind(pMemoryManager, pteKind)

static inline void memmgrEnableDynamicPageOfflining_b3696a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline void memmgrEnableDynamicPageOfflining(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrEnableDynamicPageOfflining(pGpu, pMemoryManager) memmgrEnableDynamicPageOfflining_b3696a(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrEnableDynamicPageOfflining_HAL(pGpu, pMemoryManager) memmgrEnableDynamicPageOfflining(pGpu, pMemoryManager)

static inline NV_STATUS memmgrSetMemDescPageSize_56cd7a(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, PMEMORY_DESCRIPTOR arg0, ADDRESS_TRANSLATION arg1, RM_ATTR_PAGE_SIZE arg2) {
    return NV_OK;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrSetMemDescPageSize(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, PMEMORY_DESCRIPTOR arg0, ADDRESS_TRANSLATION arg1, RM_ATTR_PAGE_SIZE arg2) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrSetMemDescPageSize(pGpu, pMemoryManager, arg0, arg1, arg2) memmgrSetMemDescPageSize_56cd7a(pGpu, pMemoryManager, arg0, arg1, arg2)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrSetMemDescPageSize_HAL(pGpu, pMemoryManager, arg0, arg1, arg2) memmgrSetMemDescPageSize(pGpu, pMemoryManager, arg0, arg1, arg2)

NV_STATUS memmgrSetPartitionableMem_IMPL(OBJGPU *pGpu, struct MemoryManager *pMemoryManager);

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrSetPartitionableMem(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrSetPartitionableMem(pGpu, pMemoryManager) memmgrSetPartitionableMem_IMPL(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrSetPartitionableMem_HAL(pGpu, pMemoryManager) memmgrSetPartitionableMem(pGpu, pMemoryManager)

NV_STATUS memmgrAllocMIGGPUInstanceMemory_PF(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU32 swizzId, NvHandle *phMemory, struct NV_RANGE *pAddrRange, struct Heap **ppMemoryPartitionHeap);

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrAllocMIGGPUInstanceMemory(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU32 swizzId, NvHandle *phMemory, struct NV_RANGE *pAddrRange, struct Heap **ppMemoryPartitionHeap) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrAllocMIGGPUInstanceMemory(pGpu, pMemoryManager, swizzId, phMemory, pAddrRange, ppMemoryPartitionHeap) memmgrAllocMIGGPUInstanceMemory_PF(pGpu, pMemoryManager, swizzId, phMemory, pAddrRange, ppMemoryPartitionHeap)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrAllocMIGGPUInstanceMemory_HAL(pGpu, pMemoryManager, swizzId, phMemory, pAddrRange, ppMemoryPartitionHeap) memmgrAllocMIGGPUInstanceMemory(pGpu, pMemoryManager, swizzId, phMemory, pAddrRange, ppMemoryPartitionHeap)

static inline NV_STATUS memmgrGetBlackListPagesForHeap_46f6a7(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, struct Heap *pHeap) {
    return NV_ERR_NOT_SUPPORTED;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrGetBlackListPagesForHeap(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, struct Heap *pHeap) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetBlackListPagesForHeap(pGpu, pMemoryManager, pHeap) memmgrGetBlackListPagesForHeap_46f6a7(pGpu, pMemoryManager, pHeap)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetBlackListPagesForHeap_HAL(pGpu, pMemoryManager, pHeap) memmgrGetBlackListPagesForHeap(pGpu, pMemoryManager, pHeap)

static inline NV_STATUS memmgrGetBlackListPages_46f6a7(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, BLACKLIST_ADDRESS *pBlAddrs, NvU32 *pCount) {
    return NV_ERR_NOT_SUPPORTED;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrGetBlackListPages(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, BLACKLIST_ADDRESS *pBlAddrs, NvU32 *pCount) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetBlackListPages(pGpu, pMemoryManager, pBlAddrs, pCount) memmgrGetBlackListPages_46f6a7(pGpu, pMemoryManager, pBlAddrs, pCount)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrGetBlackListPages_HAL(pGpu, pMemoryManager, pBlAddrs, pCount) memmgrGetBlackListPages(pGpu, pMemoryManager, pBlAddrs, pCount)

static inline NV_STATUS memmgrDiscoverMIGPartitionableMemoryRange_46f6a7(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, struct NV_RANGE *pMemoryRange) {
    return NV_ERR_NOT_SUPPORTED;
}

#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrDiscoverMIGPartitionableMemoryRange(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, struct NV_RANGE *pMemoryRange) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrDiscoverMIGPartitionableMemoryRange(pGpu, pMemoryManager, pMemoryRange) memmgrDiscoverMIGPartitionableMemoryRange_46f6a7(pGpu, pMemoryManager, pMemoryRange)
#endif //__nvoc_mem_mgr_h_disabled

#define memmgrDiscoverMIGPartitionableMemoryRange_HAL(pGpu, pMemoryManager, pMemoryRange) memmgrDiscoverMIGPartitionableMemoryRange(pGpu, pMemoryManager, pMemoryRange)

static inline NV_STATUS memmgrReconcileTunableState_DISPATCH(POBJGPU pGpu, struct MemoryManager *pEngstate, void *pTunableState) {
    return pEngstate->__memmgrReconcileTunableState__(pGpu, pEngstate, pTunableState);
}

static inline NV_STATUS memmgrStateLoad_DISPATCH(POBJGPU pGpu, struct MemoryManager *pEngstate, NvU32 arg0) {
    return pEngstate->__memmgrStateLoad__(pGpu, pEngstate, arg0);
}

static inline NV_STATUS memmgrStateUnload_DISPATCH(POBJGPU pGpu, struct MemoryManager *pEngstate, NvU32 arg0) {
    return pEngstate->__memmgrStateUnload__(pGpu, pEngstate, arg0);
}

static inline NV_STATUS memmgrStateInitLocked_DISPATCH(POBJGPU pGpu, struct MemoryManager *pEngstate) {
    return pEngstate->__memmgrStateInitLocked__(pGpu, pEngstate);
}

static inline NV_STATUS memmgrStatePreLoad_DISPATCH(POBJGPU pGpu, struct MemoryManager *pEngstate, NvU32 arg0) {
    return pEngstate->__memmgrStatePreLoad__(pGpu, pEngstate, arg0);
}

static inline NV_STATUS memmgrStatePostUnload_DISPATCH(POBJGPU pGpu, struct MemoryManager *pEngstate, NvU32 arg0) {
    return pEngstate->__memmgrStatePostUnload__(pGpu, pEngstate, arg0);
}

static inline void memmgrStateDestroy_DISPATCH(POBJGPU pGpu, struct MemoryManager *pEngstate) {
    pEngstate->__memmgrStateDestroy__(pGpu, pEngstate);
}

static inline NV_STATUS memmgrStatePreUnload_DISPATCH(POBJGPU pGpu, struct MemoryManager *pEngstate, NvU32 arg0) {
    return pEngstate->__memmgrStatePreUnload__(pGpu, pEngstate, arg0);
}

static inline NV_STATUS memmgrStateInitUnlocked_DISPATCH(POBJGPU pGpu, struct MemoryManager *pEngstate) {
    return pEngstate->__memmgrStateInitUnlocked__(pGpu, pEngstate);
}

static inline void memmgrInitMissing_DISPATCH(POBJGPU pGpu, struct MemoryManager *pEngstate) {
    pEngstate->__memmgrInitMissing__(pGpu, pEngstate);
}

static inline NV_STATUS memmgrStatePreInitLocked_DISPATCH(POBJGPU pGpu, struct MemoryManager *pEngstate) {
    return pEngstate->__memmgrStatePreInitLocked__(pGpu, pEngstate);
}

static inline NV_STATUS memmgrStatePreInitUnlocked_DISPATCH(POBJGPU pGpu, struct MemoryManager *pEngstate) {
    return pEngstate->__memmgrStatePreInitUnlocked__(pGpu, pEngstate);
}

static inline NV_STATUS memmgrGetTunableState_DISPATCH(POBJGPU pGpu, struct MemoryManager *pEngstate, void *pTunableState) {
    return pEngstate->__memmgrGetTunableState__(pGpu, pEngstate, pTunableState);
}

static inline NV_STATUS memmgrCompareTunableState_DISPATCH(POBJGPU pGpu, struct MemoryManager *pEngstate, void *pTunables1, void *pTunables2) {
    return pEngstate->__memmgrCompareTunableState__(pGpu, pEngstate, pTunables1, pTunables2);
}

static inline void memmgrFreeTunableState_DISPATCH(POBJGPU pGpu, struct MemoryManager *pEngstate, void *pTunableState) {
    pEngstate->__memmgrFreeTunableState__(pGpu, pEngstate, pTunableState);
}

static inline NV_STATUS memmgrStatePostLoad_DISPATCH(POBJGPU pGpu, struct MemoryManager *pEngstate, NvU32 arg0) {
    return pEngstate->__memmgrStatePostLoad__(pGpu, pEngstate, arg0);
}

static inline NV_STATUS memmgrAllocTunableState_DISPATCH(POBJGPU pGpu, struct MemoryManager *pEngstate, void **ppTunableState) {
    return pEngstate->__memmgrAllocTunableState__(pGpu, pEngstate, ppTunableState);
}

static inline NV_STATUS memmgrSetTunableState_DISPATCH(POBJGPU pGpu, struct MemoryManager *pEngstate, void *pTunableState) {
    return pEngstate->__memmgrSetTunableState__(pGpu, pEngstate, pTunableState);
}

static inline NV_STATUS memmgrConstructEngine_DISPATCH(POBJGPU pGpu, struct MemoryManager *pEngstate, ENGDESCRIPTOR arg0) {
    return pEngstate->__memmgrConstructEngine__(pGpu, pEngstate, arg0);
}

static inline NvBool memmgrIsPresent_DISPATCH(POBJGPU pGpu, struct MemoryManager *pEngstate) {
    return pEngstate->__memmgrIsPresent__(pGpu, pEngstate);
}

static inline NV_STATUS memmgrSavePowerMgmtState(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return NV_OK;
}

static inline NV_STATUS memmgrRestorePowerMgmtState(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    return NV_OK;
}

static inline NV_STATUS memmgrFree(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, struct Heap *arg0, NvHandle arg1, NvHandle arg2, NvHandle arg3, NvU32 arg4, MEMORY_DESCRIPTOR *arg5) {
    return NV_ERR_NOT_SUPPORTED;
}

static inline struct Heap *memmgrGetDeviceSuballocator(struct MemoryManager *pMemoryManager, NvBool bForceSubheap) {
    return ((void *)0);
}

static inline NV_ADDRESS_SPACE memmgrAllocGetAddrSpace(struct MemoryManager *pMemoryManager, NvU32 flags, NvU32 attr) {
    return 2;
}

static inline NvBool memmgrIsScrubOnFreeEnabled(struct MemoryManager *pMemoryManager) {
    return pMemoryManager->bScrubOnFreeEnabled;
}

static inline NvBool memmgrIsFastScrubberEnabled(struct MemoryManager *pMemoryManager) {
    return pMemoryManager->bFastScrubberEnabled;
}

static inline NvBool memmgrUseVasForCeMemoryOps(struct MemoryManager *pMemoryManager) {
    return pMemoryManager->bUseVasForCeMemoryOps;
}

static inline NvBool memmgrRmExecutingEccScrub(struct MemoryManager *pMemoryManager) {
    return pMemoryManager->bRmExecutingEccScrub;
}

static inline NvBool memmgrBug1441072EccScrubWar(struct MemoryManager *pMemoryManager) {
    return pMemoryManager->bBug1441072EccScrubWar;
}

static inline NvBool memmgrIsPmaInitialized(struct MemoryManager *pMemoryManager) {
    return pMemoryManager->bPmaInitialized;
}

static inline void memmgrSetPmaInitialized(struct MemoryManager *pMemoryManager, NvBool val) {
    pMemoryManager->bPmaInitialized = val;
}

static inline NvBool memmgrAreFbRegionsSupported(struct MemoryManager *pMemoryManager) {
    return pMemoryManager->bFbRegionsSupported;
}

static inline NvBool memmgrIsPmaSupportedOnPlatform(struct MemoryManager *pMemoryManager) {
    return pMemoryManager->bPmaSupportedOnPlatform;
}

static inline NvBool memmgrIsPmaEnabled(struct MemoryManager *pMemoryManager) {
    return pMemoryManager->bPmaEnabled;
}

static inline NvBool memmgrIsPmaForcePersistence(struct MemoryManager *pMemoryManager) {
    return pMemoryManager->bPmaForcePersistence;
}

static inline void memmgrSetPmaForcePersistence(struct MemoryManager *pMemoryManager, NvBool val) {
    pMemoryManager->bPmaForcePersistence = val;
}

static inline NvBool memmgrAreClientPageTablesPmaManaged(struct MemoryManager *pMemoryManager) {
    return pMemoryManager->bClientPageTablesPmaManaged;
}

static inline void memmgrSetClientPageTablesPmaManaged(struct MemoryManager *pMemoryManager, NvBool val) {
    pMemoryManager->bClientPageTablesPmaManaged = val;
}

static inline NvBool memmgrIsPmaAddrTree(struct MemoryManager *pMemoryManager) {
    return pMemoryManager->bPmaAddrTree;
}

static inline NvU64 memmgrGetRsvdMemoryBase(struct MemoryManager *pMemoryManager) {
    return pMemoryManager->rsvdMemoryBase;
}

static inline NvU32 memmgrGetRsvdMemorySize(struct MemoryManager *pMemoryManager) {
    return pMemoryManager->rsvdMemorySize;
}

NV_STATUS memmgrAllocResources_IMPL(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, MEMORY_ALLOCATION_REQUEST *pAllocRequest, FB_ALLOC_INFO *pFbAllocInfo);
#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrAllocResources(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, MEMORY_ALLOCATION_REQUEST *pAllocRequest, FB_ALLOC_INFO *pFbAllocInfo) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrAllocResources(pGpu, pMemoryManager, pAllocRequest, pFbAllocInfo) memmgrAllocResources_IMPL(pGpu, pMemoryManager, pAllocRequest, pFbAllocInfo)
#endif //__nvoc_mem_mgr_h_disabled

NV_STATUS memmgrMemCopy_IMPL(struct MemoryManager *pMemoryManager, TRANSFER_SURFACE *pDst, TRANSFER_SURFACE *pSrc, NvU32 size, NvU32 flags);
#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrMemCopy(struct MemoryManager *pMemoryManager, TRANSFER_SURFACE *pDst, TRANSFER_SURFACE *pSrc, NvU32 size, NvU32 flags) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrMemCopy(pMemoryManager, pDst, pSrc, size, flags) memmgrMemCopy_IMPL(pMemoryManager, pDst, pSrc, size, flags)
#endif //__nvoc_mem_mgr_h_disabled

NV_STATUS memmgrMemSet_IMPL(struct MemoryManager *pMemoryManager, TRANSFER_SURFACE *pDst, NvU32 value, NvU32 size, NvU32 flags);
#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrMemSet(struct MemoryManager *pMemoryManager, TRANSFER_SURFACE *pDst, NvU32 value, NvU32 size, NvU32 flags) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrMemSet(pMemoryManager, pDst, value, size, flags) memmgrMemSet_IMPL(pMemoryManager, pDst, value, size, flags)
#endif //__nvoc_mem_mgr_h_disabled

NV_STATUS memmgrMemWrite_IMPL(struct MemoryManager *pMemoryManager, TRANSFER_SURFACE *pDst, void *pBuf, NvU64 size, NvU32 flags);
#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrMemWrite(struct MemoryManager *pMemoryManager, TRANSFER_SURFACE *pDst, void *pBuf, NvU64 size, NvU32 flags) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrMemWrite(pMemoryManager, pDst, pBuf, size, flags) memmgrMemWrite_IMPL(pMemoryManager, pDst, pBuf, size, flags)
#endif //__nvoc_mem_mgr_h_disabled

NV_STATUS memmgrMemRead_IMPL(struct MemoryManager *pMemoryManager, TRANSFER_SURFACE *pSrc, void *pBuf, NvU64 size, NvU32 flags);
#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrMemRead(struct MemoryManager *pMemoryManager, TRANSFER_SURFACE *pSrc, void *pBuf, NvU64 size, NvU32 flags) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrMemRead(pMemoryManager, pSrc, pBuf, size, flags) memmgrMemRead_IMPL(pMemoryManager, pSrc, pBuf, size, flags)
#endif //__nvoc_mem_mgr_h_disabled

NvU8 *memmgrMemBeginTransfer_IMPL(struct MemoryManager *pMemoryManager, TRANSFER_SURFACE *pTransferInfo, NvU64 shadowBufSize, NvU32 flags);
#ifdef __nvoc_mem_mgr_h_disabled
static inline NvU8 *memmgrMemBeginTransfer(struct MemoryManager *pMemoryManager, TRANSFER_SURFACE *pTransferInfo, NvU64 shadowBufSize, NvU32 flags) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NULL;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrMemBeginTransfer(pMemoryManager, pTransferInfo, shadowBufSize, flags) memmgrMemBeginTransfer_IMPL(pMemoryManager, pTransferInfo, shadowBufSize, flags)
#endif //__nvoc_mem_mgr_h_disabled

void memmgrMemEndTransfer_IMPL(struct MemoryManager *pMemoryManager, TRANSFER_SURFACE *pTransferInfo, NvU64 shadowBufSize, NvU32 flags);
#ifdef __nvoc_mem_mgr_h_disabled
static inline void memmgrMemEndTransfer(struct MemoryManager *pMemoryManager, TRANSFER_SURFACE *pTransferInfo, NvU64 shadowBufSize, NvU32 flags) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrMemEndTransfer(pMemoryManager, pTransferInfo, shadowBufSize, flags) memmgrMemEndTransfer_IMPL(pMemoryManager, pTransferInfo, shadowBufSize, flags)
#endif //__nvoc_mem_mgr_h_disabled

NvU8 *memmgrMemDescBeginTransfer_IMPL(struct MemoryManager *pMemoryManager, MEMORY_DESCRIPTOR *pMemDesc, NvU32 flags);
#ifdef __nvoc_mem_mgr_h_disabled
static inline NvU8 *memmgrMemDescBeginTransfer(struct MemoryManager *pMemoryManager, MEMORY_DESCRIPTOR *pMemDesc, NvU32 flags) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NULL;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrMemDescBeginTransfer(pMemoryManager, pMemDesc, flags) memmgrMemDescBeginTransfer_IMPL(pMemoryManager, pMemDesc, flags)
#endif //__nvoc_mem_mgr_h_disabled

void memmgrMemDescEndTransfer_IMPL(struct MemoryManager *pMemoryManager, MEMORY_DESCRIPTOR *pMemDesc, NvU32 flags);
#ifdef __nvoc_mem_mgr_h_disabled
static inline void memmgrMemDescEndTransfer(struct MemoryManager *pMemoryManager, MEMORY_DESCRIPTOR *pMemDesc, NvU32 flags) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrMemDescEndTransfer(pMemoryManager, pMemDesc, flags) memmgrMemDescEndTransfer_IMPL(pMemoryManager, pMemDesc, flags)
#endif //__nvoc_mem_mgr_h_disabled

NV_STATUS memmgrMemDescMemSet_IMPL(struct MemoryManager *pMemoryManager, MEMORY_DESCRIPTOR *pMemDesc, NvU32 value, NvU32 flags);
#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrMemDescMemSet(struct MemoryManager *pMemoryManager, MEMORY_DESCRIPTOR *pMemDesc, NvU32 value, NvU32 flags) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrMemDescMemSet(pMemoryManager, pMemDesc, value, flags) memmgrMemDescMemSet_IMPL(pMemoryManager, pMemDesc, value, flags)
#endif //__nvoc_mem_mgr_h_disabled

NV_STATUS memmgrSetMIGPartitionableBAR1Range_IMPL(OBJGPU *arg0, struct MemoryManager *arg1);
#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrSetMIGPartitionableBAR1Range(OBJGPU *arg0, struct MemoryManager *arg1) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrSetMIGPartitionableBAR1Range(arg0, arg1) memmgrSetMIGPartitionableBAR1Range_IMPL(arg0, arg1)
#endif //__nvoc_mem_mgr_h_disabled

struct NV_RANGE memmgrGetMIGPartitionableBAR1Range_IMPL(OBJGPU *arg0, struct MemoryManager *arg1);
#ifdef __nvoc_mem_mgr_h_disabled
static inline struct NV_RANGE memmgrGetMIGPartitionableBAR1Range(OBJGPU *arg0, struct MemoryManager *arg1) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    struct NV_RANGE ret;
    portMemSet(&ret, 0, sizeof(struct NV_RANGE));
    return ret;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetMIGPartitionableBAR1Range(arg0, arg1) memmgrGetMIGPartitionableBAR1Range_IMPL(arg0, arg1)
#endif //__nvoc_mem_mgr_h_disabled

void memmgrSetMIGPartitionableMemoryRange_IMPL(OBJGPU *arg0, struct MemoryManager *arg1, struct NV_RANGE arg2);
#ifdef __nvoc_mem_mgr_h_disabled
static inline void memmgrSetMIGPartitionableMemoryRange(OBJGPU *arg0, struct MemoryManager *arg1, struct NV_RANGE arg2) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrSetMIGPartitionableMemoryRange(arg0, arg1, arg2) memmgrSetMIGPartitionableMemoryRange_IMPL(arg0, arg1, arg2)
#endif //__nvoc_mem_mgr_h_disabled

struct NV_RANGE memmgrGetMIGPartitionableMemoryRange_IMPL(OBJGPU *arg0, struct MemoryManager *arg1);
#ifdef __nvoc_mem_mgr_h_disabled
static inline struct NV_RANGE memmgrGetMIGPartitionableMemoryRange(OBJGPU *arg0, struct MemoryManager *arg1) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    struct NV_RANGE ret;
    portMemSet(&ret, 0, sizeof(struct NV_RANGE));
    return ret;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetMIGPartitionableMemoryRange(arg0, arg1) memmgrGetMIGPartitionableMemoryRange_IMPL(arg0, arg1)
#endif //__nvoc_mem_mgr_h_disabled

NV_STATUS memmgrFreeMIGGPUInstanceMemory_IMPL(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU32 swizzId, NvHandle hMemory, struct Heap **ppMemoryPartitionHeap);
#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrFreeMIGGPUInstanceMemory(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU32 swizzId, NvHandle hMemory, struct Heap **ppMemoryPartitionHeap) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrFreeMIGGPUInstanceMemory(pGpu, pMemoryManager, swizzId, hMemory, ppMemoryPartitionHeap) memmgrFreeMIGGPUInstanceMemory_IMPL(pGpu, pMemoryManager, swizzId, hMemory, ppMemoryPartitionHeap)
#endif //__nvoc_mem_mgr_h_disabled

NV_STATUS memmgrPageLevelPoolsCreate_IMPL(OBJGPU *pGpu, struct MemoryManager *pMemoryManager);
#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrPageLevelPoolsCreate(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrPageLevelPoolsCreate(pGpu, pMemoryManager) memmgrPageLevelPoolsCreate_IMPL(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

void memmgrPageLevelPoolsDestroy_IMPL(OBJGPU *pGpu, struct MemoryManager *pMemoryManager);
#ifdef __nvoc_mem_mgr_h_disabled
static inline void memmgrPageLevelPoolsDestroy(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrPageLevelPoolsDestroy(pGpu, pMemoryManager) memmgrPageLevelPoolsDestroy_IMPL(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

NV_STATUS memmgrPageLevelPoolsGetInfo_IMPL(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvHandle arg0, struct RM_POOL_ALLOC_MEM_RESERVE_INFO **arg1);
#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrPageLevelPoolsGetInfo(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvHandle arg0, struct RM_POOL_ALLOC_MEM_RESERVE_INFO **arg1) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrPageLevelPoolsGetInfo(pGpu, pMemoryManager, arg0, arg1) memmgrPageLevelPoolsGetInfo_IMPL(pGpu, pMemoryManager, arg0, arg1)
#endif //__nvoc_mem_mgr_h_disabled

NV_STATUS memmgrAllocMIGMemoryAllocationInternalHandles_IMPL(OBJGPU *pGpu, struct MemoryManager *pMemoryManager);
#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrAllocMIGMemoryAllocationInternalHandles(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrAllocMIGMemoryAllocationInternalHandles(pGpu, pMemoryManager) memmgrAllocMIGMemoryAllocationInternalHandles_IMPL(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

void memmgrFreeMIGMemoryAllocationInternalHandles_IMPL(OBJGPU *pGpu, struct MemoryManager *pMemoryManager);
#ifdef __nvoc_mem_mgr_h_disabled
static inline void memmgrFreeMIGMemoryAllocationInternalHandles(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrFreeMIGMemoryAllocationInternalHandles(pGpu, pMemoryManager) memmgrFreeMIGMemoryAllocationInternalHandles_IMPL(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

void memmgrGetFreeMemoryForAllMIGGPUInstances_IMPL(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU64 *pBytes);
#ifdef __nvoc_mem_mgr_h_disabled
static inline void memmgrGetFreeMemoryForAllMIGGPUInstances(OBJGPU *pGpu, struct MemoryManager *pMemoryManager, NvU64 *pBytes) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetFreeMemoryForAllMIGGPUInstances(pGpu, pMemoryManager, pBytes) memmgrGetFreeMemoryForAllMIGGPUInstances_IMPL(pGpu, pMemoryManager, pBytes)
#endif //__nvoc_mem_mgr_h_disabled

void memmgrGetTopLevelScrubberStatus_IMPL(OBJGPU *arg0, struct MemoryManager *arg1, NvBool *pbTopLevelScrubberEnabled, NvBool *pbTopLevelScrubberConstructed);
#ifdef __nvoc_mem_mgr_h_disabled
static inline void memmgrGetTopLevelScrubberStatus(OBJGPU *arg0, struct MemoryManager *arg1, NvBool *pbTopLevelScrubberEnabled, NvBool *pbTopLevelScrubberConstructed) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrGetTopLevelScrubberStatus(arg0, arg1, pbTopLevelScrubberEnabled, pbTopLevelScrubberConstructed) memmgrGetTopLevelScrubberStatus_IMPL(arg0, arg1, pbTopLevelScrubberEnabled, pbTopLevelScrubberConstructed)
#endif //__nvoc_mem_mgr_h_disabled

NV_STATUS memmgrSaveAndDestroyTopLevelScrubber_IMPL(OBJGPU *arg0, struct MemoryManager *arg1);
#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrSaveAndDestroyTopLevelScrubber(OBJGPU *arg0, struct MemoryManager *arg1) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrSaveAndDestroyTopLevelScrubber(arg0, arg1) memmgrSaveAndDestroyTopLevelScrubber_IMPL(arg0, arg1)
#endif //__nvoc_mem_mgr_h_disabled

NV_STATUS memmgrInitSavedTopLevelScrubber_IMPL(OBJGPU *arg0, struct MemoryManager *arg1);
#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrInitSavedTopLevelScrubber(OBJGPU *arg0, struct MemoryManager *arg1) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrInitSavedTopLevelScrubber(arg0, arg1) memmgrInitSavedTopLevelScrubber_IMPL(arg0, arg1)
#endif //__nvoc_mem_mgr_h_disabled

NV_STATUS memmgrReserveMemoryForFsp_IMPL(OBJGPU *pGpu, struct MemoryManager *pMemoryManager);
#ifdef __nvoc_mem_mgr_h_disabled
static inline NV_STATUS memmgrReserveMemoryForFsp(OBJGPU *pGpu, struct MemoryManager *pMemoryManager) {
    NV_ASSERT_FAILED_PRECOMP("MemoryManager was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_mem_mgr_h_disabled
#define memmgrReserveMemoryForFsp(pGpu, pMemoryManager) memmgrReserveMemoryForFsp_IMPL(pGpu, pMemoryManager)
#endif //__nvoc_mem_mgr_h_disabled

#undef PRIVATE_FIELD


#endif // MEM_MGR_H

#ifdef __cplusplus
} // extern "C"
#endif
#endif // _G_MEM_MGR_NVOC_H_
