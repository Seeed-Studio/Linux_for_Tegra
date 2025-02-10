/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2023 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

/*
 * Unit: Direct Memory Access Driver Unit
 * SWUD Document:
 * p4sw-swarm.nvidia.com/view/sw/embedded/docs/projects/active/DRIVE_6.0/QNX/PLC_Work_Products/Element_WPs/Autonomous_Middleware/PVA/04_Unit_Design/PVA_FW/SWE-PVAFW-006-SWUD.pdf
 */
/**
 * @file pva-sys-dma.h
 *
 * @brief Types and constants related to PVA DMA setup and DMA
 * descriptors.
 */

#ifndef PVA_SYS_DMA_H
#define PVA_SYS_DMA_H

#include <stdint.h>
#include <pva-bit.h>
#include <pva-packed.h>

#include "pva_fw_dma_hw_interface.h"

/**
 * @brief The version number of the current DMA info structure.
 * This is used for detecting the DMA info updates for future
 * HW releases.
 */
#define PVA_DMA_INFO_VERSION_ID (1U)

/**
 * @brief Number of DMA done masks in DMA info structure,
 * corresponding to the number of DMA_COMMON_DMA_OUTPUT_ENABLEx
 * registers in the HW.
 */
#define PVA_SYS_DMA_NUM_TRIGGERS (9U)

/* NOTE : This must be kept as 15 for build to be
 * successful, because in pva_fw_test we configure
 * 15 channel, but internally we check if the
 * number of channels requested is less than the
 * maximum number of available channels */
/**
 * @brief Maximum Number of DMA channel configurations
 * in DMA info structure.
 */
#define PVA_SYS_DMA_NUM_CHANNELS (15U)

/**
 * @brief Maximum number of DMA descriptors allowed
 * for use for VPU for T23x
 */
#define PVA_SYS_DMA_MAX_DESCRIPTORS_T23X (60U)
/**
 * @brief Maximum number of DMA descriptors allowed
 * for use for VPU for T26x
 */
#define PVA_SYS_DMA_MAX_DESCRIPTORS_T26X (92U)

/**
 * @brief DMA registers for VPU0 and VPU1 which are primarily
 * used by DMA config and R5 initialization.
 *
 * For more information refer to section 3.4 in PVA Cluster IAS
 * document (Document 11 in Supporting Documentation and References)
 */
/**
 * @brief DMA channel base register for VPU0.
 */
#define PVA_DMA0_REG_CH_0 PVA_OFFSET(NV_ADDRESS_MAP_PVA0_DMA0_REG_CH_0_BASE)
/**
 * @brief DMA common base register for VPU0.
 */
#define PVA_DMA0_COMMON PVA_OFFSET(NV_ADDRESS_MAP_PVA0_DMA0_COMMON_BASE)
/**
 * @brief DMA DESCRAM base register for VPU0.
 */
#define PVA_DMA0_DESCRAM PVA_OFFSET(NV_ADDRESS_MAP_PVA0_DMA0_DESCRAM_BASE)
/**
 * @brief DMA channel base register for VPU1.
 */
#define PVA_DMA1_REG_CH_0 PVA_OFFSET(NV_ADDRESS_MAP_PVA0_DMA1_REG_CH_0_BASE)
/**
 * @brief DMA common base register for VPU1.
 */
#define PVA_DMA1_COMMON PVA_OFFSET(NV_ADDRESS_MAP_PVA0_DMA1_COMMON_BASE)
/**
 * @brief DMA DESCRAM base register for VPU1.
 */
#define PVA_DMA1_DESCRAM PVA_OFFSET(NV_ADDRESS_MAP_PVA0_DMA1_DESCRAM_BASE)
/** @} */

/**
 *
 * @brief DMA channel configuration for a user task.
 *
 * The DMA channel structure contains the set-up of a
 * PVA DMA channel used for the VPU app.
 *
 * This VPU app should configure the channel information
 * in this format
 *
 * @note : For more information on channel configuration, refer section 4.1.2 and 6.4 in
 * the DMA IAS document (Document 6 in Supporting Documentation and References)
 */
typedef struct PVA_PACKED {
	/**
         * @brief HW DMA channel number from 1 to @ref PVA_NUM_DMA_CHANNELS.
         */
	uint8_t ch_number;
	/**
         * @brief Padding bytes of 3 added to align the next
         * field of 4 bytes
         */
	uint8_t pad_dma_channel1[3];
	/**
         * @brief The value to be written to DMA channel
         * control 0 register
         */
	uint32_t cntl0;
	/**
         * @brief The value to be written to DMA channel
         * control 1 register
         */
	uint32_t cntl1;
	/**
         * @brief The value to be written to DMA channel
         * boundary pad register
         */
	uint32_t boundary_pad;
	/**
         * @brief This value to be written to DMA HW sequence
         * control register.
         */
	uint32_t hwseqcntl;
	/**
         * @brief This field is unused in t19x and T23x.
         * It contains the value to be written to DMA
         * channel HWSEQFSCNTL register.
         */
	uint32_t hwseqfscntl;
	/**
         * @brief Output enable mask
         */
	uint32_t outputEnableMask;
	/**
         * @brief Padding 8 bytes to align the whole structure
         * to 32 byte boundary
         */
	uint32_t pad_dma_channel0[1];
} pva_dma_ch_config_t;

/**
 *
 * @brief DMA info for an application. The app maybe a VPU app which
 * runs an algorithm on VPU or a DMA app which just has DMA configuration
 * to move certain data. In both cases the application should
 * configure the DMA information in this structure format
 *
 */
typedef struct PVA_PACKED {
	/**
         * @brief The size of the dma_info structure.
         * Should be populated with value sizeof(pva_dma_info_t)
         * This is used to validate that the DRAM location populated
         * by KMD is valid
         */
	uint16_t dma_info_size;
	/**
         * @brief This field is used to populate the DMA Info version
         * In case we need to create a new
         * DMA version structure then the FW can distinguish the DMA
         * info structure. Currently it should be populated with value
         * @ref PVA_DMA_INFO_VERSION_ID
         */
	uint16_t dma_info_version;

	/**
         * @brief The number of used channels. This field can
         * be populated with values from 0 to
         * @ref PVA_NUM_DMA_CHANNELS both inclusive.
         */
	uint8_t num_channels;
	/**
         * @brief Number of used descriptors.
         *
         * Note: In generations of PVA where the reserved descriptor range lies
         *       in the middle of the entire descriptor range, when the range of
         *       descriptors requested by the user crosses over the reserved descriptor
         *       range, 'num_descriptors' will include the number of the reserved
         *       descriptors as well.
         *       E.g., if reserved descriptors are at indices 60-63 and user application
         *             needs 70 descriptors, 'num_descriptor' will equal 74. However,
         *             if user application needs 30 descriptors, 'num_descriptors' will be 30.
         *
         * On T19x and T23x, the field can be populated
         * with values from 0 inclusive to less than
         * @ref PVA_SYS_DMA_MAX_DESCRIPTORS
         *
         * On T26x, the field can be populated with values from 0 inclusive to
         * @ref PVA_SYS_DMA_MAX_DESCRIPTORS + @ref PVA_NUM_RESERVED_DESCRIPTORS
         */
	uint8_t num_descriptors;
	/**
         * @brief The number of bytes used in HW sequencer
         */
	uint16_t num_hwseq;

	/**
         * @brief The First HW descriptor ID used.
         *
         * On T19x and T23x, the field can be populated
         * with values from 0 inclusive to less than
         * @ref PVA_SYS_DMA_MAX_DESCRIPTORS
         *
         * On T26x, the field can be populated with values from 0 inclusive to
         * @ref PVA_SYS_DMA_MAX_DESCRIPTORS + @ref PVA_NUM_RESERVED_DESCRIPTORS
         */
	uint8_t descriptor_id;
	/**
         * @brief Padding for alignment of next element
         */
	uint8_t pva_dma_info_pad_0[3];

	/**
         * @brief DMA done triggers used by the VPU app.
         * Correspond to COMMON_DMA_OUTPUT_ENABLE registers.
         */
	uint32_t dma_triggers[PVA_SYS_DMA_NUM_TRIGGERS];
	/**
         * @brief DMA channel config used by the VPU app.
         * One app can have upto @ref PVA_NUM_DMA_CHANNELS
         * DMA channel configurations. The size of the array
	 * is @ref PVA_SYS_DMA_NUM_CHANNELS for additional
	 * configuration required for future products.
         */
	pva_dma_ch_config_t dma_channels[PVA_SYS_DMA_NUM_CHANNELS];
	/**
         * @brief Value to be set in DMA common configuration register.
         */
	uint32_t dma_common_config;
	/**
         * @brief IOVA to an array of @ref pva_dtd_t, aligned at 64 bytes
         * which holds the DMA descriptors used by the VPU app
         */
	pva_iova dma_descriptor_base;
	/**
         * @brief HW sequencer configuration base address.
         */
	pva_iova dma_hwseq_base;
	/**
         * @brief IOVA to a structure of @ref pva_dma_misr_config_t,
         * location where DMA MISR configuration information is stored.
         */
	pva_iova dma_misr_base;
} pva_dma_info_t;

/**
 * @brief DMA descriptor.
 *
 * PVA DMA Descriptor in packed HW format.
 * The individual fields can be found from
 * the DMA IAS document (Document 6 in Supporting Documentation and References)
 * section 4.1.3.2
 */
typedef struct PVA_PACKED {
	/** @brief TRANSFER_CONTROL0 byte has DSTM in lower 2 bits, SRC_TF in 3rd bit,
         *  DDTM in 4th to 6th bit,DST_TF in 7th bit */
	uint8_t transfer_control0;
	/** @brief Next descriptor ID to be executed*/
	uint8_t link_did;
	/** @brief Highest 8 bits of the 40 bit source address*/
	uint8_t src_adr1;
	/** @brief Highest 8 bits of the 40 bit destination address*/
	uint8_t dst_adr1;
	/** @brief Lower 32 bits of the 40 bit source address*/
	uint32_t src_adr0;
	/** @brief Lower 32 bits of the 40 bit destination address*/
	uint32_t dst_adr0;
	/** @brief Length of tile line*/
	uint16_t tx;
	/** @brief Number of tile lines*/
	uint16_t ty;
	/** @brief Source Line pitch to advance to every line of 2D tile.*/
	uint16_t slp_adv;
	/** @brief Destination Line Pitch to advance to every line of 2D tile.*/
	uint16_t dlp_adv;
	/** @brief SRC PT1 CNTL has st1_adv in low 24 bits and ns1_adv in high 8 bits. */
	uint32_t srcpt1_cntl;
	/** @brief DST PT1 CNTL has dt1_adv in low 24 bits and nd1_adv in high 8 bits. */
	uint32_t dstpt1_cntl;
	/** @brief SRC PT2 CNTL has st2_adv in low 24 bits and ns2_adv in high 8 bits. */
	uint32_t srcpt2_cntl;
	/** @brief DST PT2 CNTL has dt2_adv in low 24 bits and nd2_adv in high 8 bits. */
	uint32_t dstpt2_cntl;
	/** @brief SRC PT3 CNTL has st3_adv in low 24 bits and ns3_adv in high 8 bits. */
	uint32_t srcpt3_cntl;
	/** @brief DST PT3 CNTL has dt3_adv in low 24 bits and nd3_adv in high 8 bits. */
	uint32_t dstpt3_cntl;
	/** @brief Source circular buffer Start address offset */
	uint16_t sb_start;
	/** @brief Destination circular buffer Start address offset*/
	uint16_t db_start;
	/** @brief Source buffer size in bytes for circular buffer mode from Source address.*/
	uint16_t sb_size;
	/** @brief Destination buffer size in bytes for circular buffer mode from destination address.*/
	uint16_t db_size;
	/** @brief currently reserved*/
	uint16_t trig_ch_events;
	/** @brief SW or HW events used for triggering the channel*/
	uint16_t hw_sw_trig_events;
	/** @brief Tile x coordinates, for boundary padding in pixels*/
	uint8_t px;
	/** @brief Tile y coordinates, for boundary padding in pixels*/
	uint8_t py;
	/** @brief Transfer control byte has lower 2 bits as BPP data, bit 2 with PXDIR, bit 3 as PYDIR,
         *  bit 4 as BPE, bit 5 as TTS, bit 6 RSVD, Bit 7 ITC.
         */
	uint8_t transfer_control1;
	/** @brief Transfer control 2 gas bit 0 as PREFEN, bit 1 as DCBM, bit 2 as SCBM, Bit 3 to 3 as SBADR.*/
	uint8_t transfer_control2;
	/** @brief Circular buffer upper bits for start address and size*/
	uint8_t cb_ext;
	/** @brief Reserved*/
	uint8_t rsvd;
	/** @brief Full replicated destination base address in VMEM aligned to 64 byte atom*/
	uint16_t frda;
} pva_dtd_t;

/**
 *
 * @brief DMA MISR configuration information. This information is used by R5
 * to program MISR registers if a task requests MISR computation on its
 * output DMA channels.
 *
 */
typedef struct PVA_PACKED {
	/** @brief Reference value for CRC computed on write addresses, i.e., MISR 1 */
	uint32_t ref_addr;
	/** @brief Seed value for address CRC*/
	uint32_t seed_crc0;
	/** @brief Reference value for CRC computed on first 256-bits of AXI write data */
	uint32_t ref_data_1;
	/** @brief Seed value for write data CRC*/
	uint32_t seed_crc1;
	/** @brief Reference value for CRC computed on second 256-bits of AXI write data */
	uint32_t ref_data_2;
	/**
     * @brief MISR timeout value configured in DMA common register
     * @ref PVA_DMA_COMMON_MISR_ENABLE. Timeout is calculated as
     * number of AXI clock cycles.
     */
	uint32_t misr_timeout;
} pva_dma_misr_config_t;

/**
 * @defgroup PVA_DMA_TC0_BITS
 *
 * @brief PVA Transfer Control 0 Bitfields
 *
 * @{
 */
/**
 * @brief The shift value for extracting DSTM field
 */
#define PVA_DMA_TC0_DSTM_SHIFT (0U)
/**
 * @brief The mask to be used to extract DSTM field
 */
#define PVA_DMA_TC0_DSTM_MASK (7U)

/**
 * @brief The shift value for extracting DDTM field
 */
#define PVA_DMA_TC0_DDTM_SHIFT (4U)
/**
 * @brief The mask to be used to extract DDTM field
 */
#define PVA_DMA_TC0_DDTM_MASK (7U)
/** @} */

/**
 * @defgroup PVA_DMA_TM
 *
 * @brief DMA Transfer Modes. These can be used for both
 * Source (DSTM) and Destination (DDTM) transfer modes
 *
 * @note : For more information on transfer modes, refer section 4.1.3.1 in
 * the DMA IAS document (Document 6 in Supporting Documentation and References)
 *
 * @{
 */
/**
 * @brief To indicate invalid transfer mode
 */
#define PVA_DMA_TM_INVALID (0U)
/**
 * @brief To indicate MC transfer mode
 */
#define PVA_DMA_TM_MC (1U)
/**
 * @brief To indicate VMEM transfer mode
 */
#define PVA_DMA_TM_VMEM (2U)
#if ENABLE_UNUSED == 1U
#define PVA_DMA_TM_CVNAS (3U)
#endif
/**
 * @brief To indicate L2SRAM transfer mode
 */
#define PVA_DMA_TM_L2RAM (3U)
/**
 * @brief To indicate TCM transfer mode
 */
#define PVA_DMA_TM_TCM (4U)
/**
 * @brief To indicate MMIO transfer mode
 */
#define PVA_DMA_TM_MMIO (5U)
/**
 * @brief To indicate Reserved transfer mode
 */
#define PVA_DMA_TM_RSVD (6U)
/**
 * @brief To indicate VPU configuration transfer mode.
 * This is only available in Source transfer mode or
 * (DSTM). In Destination transfer mode, this value is
 * reserved.
 */
#define PVA_DMA_TM_VPU (7U)
/** @} */

#if (ENABLE_UNUSED == 1U)
/**
 * @brief The macro defines the number of
 * bits to shift right to get the PXDIR field
 * in Transfer Control 1 register in DMA
 * Descriptor
 */
#define PVA_DMA_TC1_PXDIR_SHIFT (2U)

/**
 * @brief The macro defines the number of
 * bits to shift right to get the PYDIR field
 * in Transfer Control 1 register in DMA
 * Descriptor
 */
#define PVA_DMA_TC1_PYDIR_SHIFT (3U)
#endif
/**
 * @defgroup PVA_DMA_BPP
 *
 * @brief PVA DMA Bits per Pixel
 *
 * @{
 */
/**
 * @brief To indicate that the size of pixel data
 * is 1 byte
 */
#define PVA_DMA_BPP_INT8 (0U)
#if ENABLE_UNUSED == 1U
#define PVA_DMA_BPP_INT16 (1U)
#endif
/** @} */

/**
 * @brief PVA DMA Pad X direction set to right
 */
#define PVA_DMA_PXDIR_RIGHT (1U)

/**
 * @brief PVA DMA Pad Y direction set to bottom
 */
#define PVA_DMA_PYDIR_BOT (1U)

#endif /* PVA_SYS_DMA_H */
