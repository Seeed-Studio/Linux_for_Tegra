/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_HWSEQ_VALIDATE_H
#define PVA_KMD_HWSEQ_VALIDATE_H

#include "pva_api_dma.h"
#include "pva_kmd_device.h"

#define PVA_HWSEQ_RRA_MAX_NOCR 31U
#define PVA_HWSEQ_RRA_MAX_FRAME_COUNT 63U

/**
 * List of valid Addressing Modes in HW Sequencer Header
 */
enum pva_dma_hwseq_fid {
	PVA_DMA_HWSEQ_RRA_MODE = 0xC0DA, /*!< RRA addressing */
	PVA_DMA_HWSEQ_FRAME_MODE = 0xC0DE, /*!< frame addressing */
	PVA_DMA_HWSEQ_DESC_MODE = 0xDEAD /*!< descriptor addressing */
};

/**
 * Combine three headers common in HW Sequencer
 *
 * ----------------------------------------------------------------------------
 * |        | byte 3        | byte 2       | byte 1          | byte 0         |
 * |--------|---------------|--------------|-----------------|----------------|
 * | Head 1 | NOCR          | FR           | FID1            | FID0           |
 * | Head 2 | FO in LP 15:8 | FO in LP 7:0 | TO in P/LP 15:8 | TO in P/LP 7:0 |
 * | Head 3 | padB          | padL         | padT            | padR           |
 * ----------------------------------------------------------------------------
 **/
struct pva_dma_hwseq_hdr {
	//hdr_1
	uint16_t fid; /*!< addressing type: frame or descriptor */
	uint8_t fr; /*!< frame repetition factor */
	uint8_t nocr; /*!< number of descriptor column/row */
	//hdr_2
	int16_t to; /*!< tile offset in pixel/Line Pitch */
	int16_t fo; /*!< frame offset in Line Pitch */
	//hdr_3
	uint8_t padr; /*!< pad right */
	uint8_t padt; /*!< pad top */
	uint8_t padl; /*!< pad left */
	uint8_t padb; /*!< pad bottom */
};

/**
 * A struct which represents Column/Row Header in HW Sequencer
 */
struct pva_dma_hwseq_colrow_hdr {
	uint8_t dec; /*!< descriptor entry count */
	uint8_t crr; /*!< col/row repetition factor */
	int16_t cro; /*!< col/row ofst in pixel/line pitch */
};

/**
 * A struct which represents a DMA Descriptor Header in HW Sequencer
 */
struct pva_dma_hwseq_desc_entry {
	uint8_t did; /*!< desc id */
	uint8_t dr; /*!< desc repetition */
};

/**
 * A struct which represents a Column/Row Header Entry in HW Sequencer
 */
struct pva_dma_hwseq_colrow_entry_hdr {
	struct pva_dma_hwseq_colrow_hdr hdr; /*!< Col/Row Header */
};

/**
 * A struct representing Grid Information
 */
struct pva_hwseq_grid_info {
	/**
	 * tile co-ordinates
	 * In Raster Mode:
	 * 	- tile_x[0] = Tile width of the first tile in HW Seq DMA Transfer
	 * 	- tile_x[1] = Tile width of the last tile in HW Seq DMA Transfer
	 * In Vertical Mining Mode:
	 * 	- tile_x[0] = Tile height of the first tile in HW Seq DMA Transfer
	 * 	- tile_x[1] = Tile height of the last tile in HW Seq DMA Transfer
	 */
	int32_t tile_x[2];
	/**
	 * tile co-ordinates
	 * In Raster Mode:
	 * 	- tile_y[0] = Tile height of the first tile in HW Seq DMA Transfer
	 * 	- tile_y[1] = Tile height of the last tile in HW Seq DMA Transfer
	 * In Vertical Mining Mode:
	 * 	- tile_y[0] = Tile width of the first tile in HW Seq DMA Transfer
	 * 	- tile_y[1] = Tile width of the last tile in HW Seq DMA Transfer
	 */
	int32_t tile_y[2];
	/**
	 * tile co-ordinates
	 * In Tensor Data Flow Mode:
	 */
	int32_t tile_z;
	/**
	 * Padding values
	 * In Raster Mode:
	 * 	- pad_x[0] = Left Padding
	 * 	- pad_x[1] = Right Padding
	 * In Vertical Mining Mode:
	 * 	- pad_x[0] = Top Padding
	 * 	- pad_x[1] = Bottom Padding
	 */
	int32_t pad_x[2];
	/**
	 * Padding values
	 * In Raster Mode:
	 * 	- pad_y[0] = Top Padding
	 * 	- pad_y[1] = Bottom Padding
	 * In Vertical Mining Mode:
	 * 	- pad_y[0] = Left Padding
	 * 	- pad_y[1] = Right Padding
	 */
	int32_t pad_y[2];
	/**
	 * Tiles per packet. Grid size in X dimension
	 */
	uint32_t grid_size_x;
	/**
	 * Repeat Count
	 */
	uint32_t grid_size_y;
	/**
	 * Grid Size in Z dimension for Tensor Data Flow
	 */
	uint32_t grid_size_z;
	/**
	 * Tile Offset as specified in the HW Sequencer Header
	 */
	int32_t grid_step_x;
	/**
	 * Col/Row Offset as specified in the HW Sequencer Col/Row Header
	 */
	int32_t grid_step_y;
	/**
	 * Repetition factor for Head Descriptor in HW Sequencer Blob
	 */
	uint32_t head_tile_count;
	/**
	 * Boolean value to indicate if HW Sequencer has split padding
	 */
	bool is_split_padding;
};

/**
 * A struct representing a valid Frame Information
 */
struct pva_hwseq_frame_info {
	/**
	 * X co-ordinate of start of Frame
	 */
	int64_t start_x;
	/**
	 * Y co-ordinate of start of Frame
	 */
	int64_t start_y;
	/**
	 * Z co-ordinates of starte of Frame
	 */
	int64_t start_z;
	/**
	 * X co-ordinate of end of Frame
	 */
	int64_t end_x;
	/**
	 * Y co-ordinate of end of Frame
	 */
	int64_t end_y;
	/**
	 * Z co-ordinate of end of Frame
	 */
	int64_t end_z;
};

/**
 * Struct which holds the HW Sequencer Buffer as received from User Space
 */
struct pva_hwseq_buffer {
	/**
	 * Pointer to HW Sequencer Blob in Buffer
	 */
	const uint8_t *data;
	/**
	 * Number of bytes left to be read from the data buffer
	 */
	uint32_t bytes_left;
};

/**
 * @struct hw_seq_blob_entry
 * @brief Structure to hold information about a hardware sequence blob entry.
 *
 * This structure is used to store the details of a DMA channel and the range of hardware sequencer
 * associated with it, along with the number of frames involved.
 */
struct hw_seq_blob_entry {
	/**
	 * Pointer to a const \ref pva_dma_channel which holds the current DMA Channel Information
	 * in which current HW Sequencer Blob is present
	 */
	struct pva_dma_channel const *ch;
	/**
	 * The starting index of the hardware sequencer.
	 */
	uint16_t hwseq_start;
	/**
	 * The ending index of the hardware sequencer.
	 */
	uint16_t hwseq_end;
	/**
	 * The number of frames associated with the hardware sequencer.
	 */
	uint32_t num_frames;
};

/**
 * TODO: Separate out pva_hwseq_priv to be more modular
 *
 * Items in pva_hwseq_main
 * 	- dma_config
 * 	- hw_gen
 * 	- blob
 * 	- num_hwseq_words
 * Items per segment of main i.e. pva_hwseq_segment
 * 	- hwseq_start, hwseq_end
 * 	- channel id
 * 	- hwseq_header,
 *  - desc_count
 * 	- num_frames
 * 	- head_desc, tail_desc
 * 	- is_split_padding
 * 	- is_raster_scan
 */

/**
 * A struct holding private data to HW Sequencer Blob being parsed
 */
struct pva_hwseq_priv {
	/**
	 * Number of descriptors in the HW Sequencer Blob
	 */
	uint32_t desc_count;
	/**
	 * Number of tiles in the packet
	 * This is the sum total of descriptor repetition factors
	 * present in the HW Sequencer Blob
	 */
	uint32_t tiles_per_packet;
	int32_t max_tx;
	int32_t max_ty;

	/**
	 * Struct that holds the entry info of HW Sequencer Blob
	 */
	struct hw_seq_blob_entry entry;

	/**
	 * Struct that holds HW Sequencer Blob to be read
	 */
	struct pva_hwseq_buffer blob;

	/**
	 * Boolean to indicate if split padding is present in the HW Sequener Blob
	 */
	bool is_split_padding;
	/**
	 * Bool to indicate if HW Sequencer uses raster scan or Vertical mining
	 * TRUE: Raster Scan
	 * FALSE: Vertical Mining
	 */
	bool is_raster_scan;

	/**
	 * @brief Indicates the generation of PVA HW.
	 * Allowed values: 0 (GEN 1), 1 (GEN 2), 2 (GEN 3)
	 */
	enum pva_hw_gen hw_gen;

	/**
	 * @brief Pointer to the DMA configuration header.
	 */
	const struct pva_dma_config *dma_config;

	/**
	 * Pointer to \ref pva_dma_hwseq_hdr_t which holds the HW Sequencer Header
	 */
	const struct pva_dma_hwseq_hdr *hdr;
	/**
	 * Pointer to \ref pva_dma_hwseq_colrow_hdr_t which holds the Header of the
	 * Col/Row inside HW Sequencer
	 */
	const struct pva_dma_hwseq_colrow_hdr *colrow;

	/**
	 * Pointer to the Head Descriptor of type \ref nvpva_dma_descriptor in the HW Sequencer
	 */
	const struct pva_dma_descriptor *head_desc;
	/**
	 * Pointer to the Tail Descriptor of type \ref nvpva_dma_descriptor in the HW Sequencer
	 */
	const struct pva_dma_descriptor *tail_desc;
	/**
	 * DMA Descriptor information obtained from HW Sequencer Blob of type
	 * \ref pva_dma_hwseq_desc_entry_t
	 */
	struct pva_dma_hwseq_desc_entry dma_descs[2];
	/**
	 * Access Sizes are calculated and stored here from HW Sequencer Blob
	 */
	struct pva_kmd_dma_access *access_sizes;
};

struct pva_hwseq_per_frame_info {
	uint32_t seq_tile_count;
	uint32_t vmem_tiles_per_frame;
};

enum pva_error validate_hwseq(struct pva_dma_config const *dma_config,
			      struct pva_kmd_hw_constants const *hw_consts,
			      struct pva_kmd_dma_access *access_sizes,
			      uint64_t *hw_dma_descs_mask);

#endif
