#ifndef NNT_PCICONF_DEFS_H
#define NNT_PCICONF_DEFS_H

#define ADDRESS_SPACE_CR_SPACE  0x2
#define ADDRESS_SPACE_ICMD      0x3
#define ADDRESS_SPACE_SEMAPHORE 0xa

#define PCI_CONTROL_OFFSET      0x04
#define PCI_COUNTER_OFFSET      0x08

#define SEMAPHORE_MAX_RETRIES   0x1000

#define PCI_SPACE_BIT_OFFSET        0
#define PCI_SPACE_BIT_LENGTH        16
#define	PCI_STATUS_BIT_OFFSET       29
#define PCI_STATUS_BIT_LEN          3
#define PCI_FLAG_BIT_OFFSET         31

#define READ_OPERATION              0
#define WRITE_OPERATION             1

#define IFC_MAX_RETRIES             0x10000
#define SEMAPHORE_MAX_RETRIES       0x1000


typedef enum nnt_space_address {
    NNT_SPACE_ICMD =                0x1,
    NNT_SPACE_CR_SPACE =            0x2,
    NNT_SPACE_ALL_ICMD =            0x3,
    NNT_SPACE_NODNIC_INIT_SEG =     0x4,
    NNT_SPACE_EXPANSION_ROM =       0x5,
    NNT_SPACE_ND_CR_SPACE =         0x6,
    NNT_SPACE_SCAN_CR_SPACE =       0x7,
    NNT_SPACE_GLOBAL_SEMAPHORE =    0xa,
    NNT_SPACE_MAC =                 0xf
} NNT_SPACE_ADDRESS;


typedef enum nnt_vsec_capability {
    NNT_VSEC_INITIALIZED = 0,
    NNT_VSEC_ICMD_SPACE_SUPPORTED,
    NNT_VSEC_CRSPACE_SPACE_SUPPORTED,
    NNT_VSEC_ALL_ICMD_SPACE_SUPPORTED,
    NNT_VSEC_NODNIC_INIT_SEG_SPACE_SUPPORTED,
    NNT_VSEC_EXPANSION_ROM_SPACE_SUPPORTED,
    NNT_VSEC_ND_CRSPACE_SPACE_SUPPORTED,
    NNT_VSEC_SCAN_CRSPACE_SPACE_SUPPORTED,
    NNT_VSEC_GLOBAL_SEMAPHORE_SPACE_SUPPORTED,
    NNT_VSEC_MAC_SPACE_SUPPORTED,
} NNT_VSEC_CAPABILITY;


// BIT Slicing macros
#define ONES32(size)                        ((size)?(0xffffffff>>(32-(size))):0)
#define MASK32(offset, size)             (  ONES32(size)<<(offset))

#define EXTRACT_C(source, offset, size)     ((((unsigned int)(source))>>(offset)) & ONES32(size))
#define EXTRACT(src, start, len)            (((len) == 32)?(src):EXTRACT_C(src, start, len))

#define MERGE_C(rsrc1, rsrc2, start, len)   ((((rsrc2)<<(start)) & (MASK32((start), (len)))) | ((rsrc1) & (~MASK32((start), (len)))))
#define MERGE(rsrc1, rsrc2, start, len)     (((len) == 32)?(rsrc2):MERGE_C(rsrc1, rsrc2, start, len))


#endif
