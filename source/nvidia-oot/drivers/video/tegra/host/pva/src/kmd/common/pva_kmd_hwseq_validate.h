/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_HWSEQ_VALIDATE_H
#define PVA_KMD_HWSEQ_VALIDATE_H

#include "pva_api_dma.h"
#include "pva_kmd_device.h"

/**
 * @brief Maximum number of column/row descriptors in RRA mode
 *
 * @details Maximum allowed value for Number of Column/Row descriptors
 * in RRA (Rectangular Region Access) addressing mode.
 * This constant defines the upper bound for column/row descriptor
 * validation in hardware sequencer configurations.
 */
#define PVA_HWSEQ_RRA_MAX_NOCR 31U

/**
 * @brief Maximum frame count in hardware sequencer
 *
 * @details Maximum allowed frame count for hardware sequencer operations.
 * This limits the number of frames that can be processed in a single
 * hardware sequencer configuration to ensure proper resource management
 * and validation.
 */
#define PVA_HWSEQ_RRA_MAX_FRAME_COUNT 63U

/**
 * @brief Enumeration of hardware sequencer addressing modes
 *
 * @details This enumeration defines the valid addressing modes supported
 * by the hardware sequencer for DMA operations. Each mode uses a specific
 * Frame ID (FID) value to identify the addressing type in the hardware
 * sequencer header. The addressing mode determines how the hardware
 * sequencer processes and organizes DMA operations.
 */
enum pva_dma_hwseq_fid {
	/** @brief RRA (Rectangular Region Access) addressing mode */
	PVA_DMA_HWSEQ_RRA_MODE = 0xC0DA,
	/** @brief Frame-based addressing mode */
	PVA_DMA_HWSEQ_FRAME_MODE = 0xC0DE,
	/** @brief Descriptor-based addressing mode */
	PVA_DMA_HWSEQ_DESC_MODE = 0xDEAD
};

/**
 * @brief Hardware sequencer header structure
 *
 * @details This structure contains the header information for hardware
 * sequencer configurations. The header defines addressing type, repetition
 * factors, offsets, and padding values that control DMA operation patterns.
 * The structure provides the necessary metadata for hardware sequencer
 * operation configuration and validation.
 */
struct pva_dma_hwseq_hdr {
	/**
	 * @brief Frame ID indicating addressing type
	 * Valid values: @ref pva_dma_hwseq_fid enumeration values
	 */
	uint16_t fid;

	/**
	 * @brief Frame repetition factor
	 * Valid range: [1 .. 255]
	 */
	uint8_t fr;

	/**
	 * @brief Number of descriptor columns/rows
	 * Valid range: [0 .. PVA_HWSEQ_RRA_MAX_NOCR]
	 */
	uint8_t nocr;

	/**
	 * @brief Tile offset in pixels or line pitch
	 * Valid range: [INT16_MIN .. INT16_MAX]
	 */
	int16_t to;

	/**
	 * @brief Frame offset in line pitch
	 * Valid range: [INT16_MIN .. INT16_MAX]
	 */
	int16_t fo;

	/**
	 * @brief Padding for right edge
	 * Valid range: [0 .. 255]
	 */
	uint8_t padr;

	/**
	 * @brief Padding for top edge
	 * Valid range: [0 .. 255]
	 */
	uint8_t padt;

	/**
	 * @brief Padding for left edge
	 * Valid range: [0 .. 255]
	 */
	uint8_t padl;

	/**
	 * @brief Padding for bottom edge
	 * Valid range: [0 .. 255]
	 */
	uint8_t padb;
};

/**
 * @brief Hardware sequencer column/row header structure
 *
 * @details This structure represents the column/row header information
 * in hardware sequencer configurations. It controls descriptor entry
 * counts, repetition factors, and offset values for column/row operations
 * within the hardware sequencer processing pipeline.
 */
struct pva_dma_hwseq_colrow_hdr {
	/**
	 * @brief Descriptor entry count
	 * Valid range: [1 .. 255]
	 */
	uint8_t dec;

	/**
	 * @brief Column/row repetition factor
	 * Valid range: [1 .. 255]
	 */
	uint8_t crr;

	/**
	 * @brief Column/row offset in pixels or line pitch
	 * Valid range: [INT16_MIN .. INT16_MAX]
	 */
	int16_t cro;
};

/**
 * @brief Hardware sequencer DMA descriptor entry
 *
 * @details This structure represents a single DMA descriptor entry
 * in the hardware sequencer. It contains the descriptor ID and its
 * repetition count for the DMA operation, enabling efficient
 * descriptor reuse and pattern-based DMA processing.
 */
struct pva_dma_hwseq_desc_entry {
	/**
	 * @brief Descriptor ID referencing a DMA descriptor
	 * Valid range: [0 .. MAX_DESC_ID]
	 */
	uint8_t did;

	/**
	 * @brief Descriptor repetition count
	 * Valid range: [1 .. 255]
	 */
	uint8_t dr;
};

/**
 * @brief Hardware sequencer column/row entry header
 *
 * @details This structure wraps the column/row header to represent
 * a complete column/row entry in the hardware sequencer configuration.
 * It provides structural organization for column/row operations
 * within the hardware sequencer processing pipeline.
 */
struct pva_dma_hwseq_colrow_entry_hdr {
	/** @brief Column/row header information */
	struct pva_dma_hwseq_colrow_hdr hdr;
};

/**
 * @brief Hardware sequencer grid information structure
 *
 * @details This structure contains comprehensive grid information for
 * hardware sequencer operations. It includes tile coordinates, padding
 * values, grid dimensions, and processing modes. The interpretation
 * of coordinates varies between different processing modes to support
 * various data layout patterns and access patterns.
 */
struct pva_hwseq_grid_info {
	/**
	 * @brief Tile X-coordinates for first and last tiles
	 *
	 * @details Interpretation varies by processing mode:
	 * - Raster Mode: tile_x[0] = first tile width, tile_x[1] = last tile width
	 * - Vertical Mining Mode: tile_x[0] = first tile height, tile_x[1] = last tile height
	 * Valid range for each element: [INT32_MIN .. INT32_MAX]
	 */
	int32_t tile_x[2];

	/**
	 * @brief Tile Y-coordinates for first and last tiles
	 *
	 * @details Interpretation varies by processing mode:
	 * - Raster Mode: tile_y[0] = first tile height, tile_y[1] = last tile height
	 * - Vertical Mining Mode: tile_y[0] = first tile width, tile_y[1] = last tile width
	 * Valid range for each element: [INT32_MIN .. INT32_MAX]
	 */
	int32_t tile_y[2];

	/**
	 * @brief Tile Z-coordinate for Tensor Data Flow Mode
	 * Valid range: [INT32_MIN .. INT32_MAX]
	 */
	int32_t tile_z;

	/**
	 * @brief X-direction padding values
	 *
	 * @details Interpretation varies by processing mode:
	 * - Raster Mode: pad_x[0] = left padding, pad_x[1] = right padding
	 * - Vertical Mining Mode: pad_x[0] = top padding, pad_x[1] = bottom padding
	 * Valid range for each element: [0 .. INT32_MAX]
	 */
	int32_t pad_x[2];

	/**
	 * @brief Y-direction padding values
	 *
	 * @details Interpretation varies by processing mode:
	 * - Raster Mode: pad_y[0] = top padding, pad_y[1] = bottom padding
	 * - Vertical Mining Mode: pad_y[0] = left padding, pad_y[1] = right padding
	 * Valid range for each element: [0 .. INT32_MAX]
	 */
	int32_t pad_y[2];

	/**
	 * @brief Grid size in X dimension (tiles per packet)
	 * Valid range: [1 .. UINT32_MAX]
	 */
	uint32_t grid_size_x;

	/**
	 * @brief Grid size in Y dimension (repeat count)
	 * Valid range: [1 .. UINT32_MAX]
	 */
	uint32_t grid_size_y;

	/**
	 * @brief Grid size in Z dimension for Tensor Data Flow
	 * Valid range: [1 .. UINT32_MAX]
	 */
	uint32_t grid_size_z;

	/**
	 * @brief Tile offset as specified in hardware sequencer header
	 * Valid range: [INT32_MIN .. INT32_MAX]
	 */
	int32_t grid_step_x;

	/**
	 * @brief Column/row offset as specified in hardware sequencer header
	 * Valid range: [INT32_MIN .. INT32_MAX]
	 */
	int32_t grid_step_y;

	/**
	 * @brief Repetition factor for head descriptor in hardware sequencer
	 * Valid range: [1 .. UINT32_MAX]
	 */
	uint32_t head_tile_count;

	/**
	 * @brief Flag indicating if hardware sequencer has split padding
	 * Valid values: true (has split padding), false (no split padding)
	 */
	bool is_split_padding;
};

/**
 * @brief Hardware sequencer frame information structure
 *
 * @details This structure represents the valid frame information including
 * start and end coordinates in three-dimensional space. It defines the
 * bounding box of the frame being processed by the hardware sequencer,
 * enabling proper frame boundary validation and processing.
 */
struct pva_hwseq_frame_info {
	/**
	 * @brief X-coordinate of frame start
	 * Valid range: [INT64_MIN .. INT64_MAX]
	 */
	int64_t start_x;

	/**
	 * @brief Y-coordinate of frame start
	 * Valid range: [INT64_MIN .. INT64_MAX]
	 */
	int64_t start_y;

	/**
	 * @brief Z-coordinate of frame start
	 * Valid range: [INT64_MIN .. INT64_MAX]
	 */
	int64_t start_z;

	/**
	 * @brief X-coordinate of frame end
	 * Valid range: [start_x .. INT64_MAX]
	 */
	int64_t end_x;

	/**
	 * @brief Y-coordinate of frame end
	 * Valid range: [start_y .. INT64_MAX]
	 */
	int64_t end_y;

	/**
	 * @brief Z-coordinate of frame end
	 * Valid range: [start_z .. INT64_MAX]
	 */
	int64_t end_z;
};

/**
 * @brief Hardware sequencer buffer structure
 *
 * @details This structure holds the hardware sequencer buffer data
 * as received from user space. It provides access to the raw sequencer
 * blob data and tracks the remaining bytes to be processed during
 * hardware sequencer parsing and validation operations.
 */
struct pva_hwseq_buffer {
	/**
	 * @brief Pointer to hardware sequencer blob data
	 * Valid value: non-null if bytes_left > 0
	 */
	const uint8_t *data;

	/**
	 * @brief Number of bytes remaining in the buffer
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t bytes_left;
};

/**
 * @brief Hardware sequence blob entry information structure
 *
 * @details This structure stores information about a hardware sequence blob entry,
 * including the associated DMA channel, hardware sequencer range, and frame count.
 * It provides the necessary context for processing hardware sequencer operations
 * within a specific DMA channel configuration.
 */
struct hw_seq_blob_entry {
	/**
	 * @brief Pointer to DMA channel containing this hardware sequencer blob
	 */
	struct pva_dma_channel const *ch;

	/**
	 * @brief Starting index of the hardware sequencer range
	 * Valid range: [0 .. UINT16_MAX]
	 */
	uint16_t hwseq_start;

	/**
	 * @brief Ending index of the hardware sequencer range
	 * Valid range: [hwseq_start .. UINT16_MAX]
	 */
	uint16_t hwseq_end;

	/**
	 * @brief Number of frames associated with the hardware sequencer
	 * Valid range: [1 .. UINT32_MAX]
	 */
	uint32_t num_frames;
};

/**
 * @brief Hardware sequencer private data structure
 *
 * @details This structure holds comprehensive private data for hardware sequencer
 * blob parsing and validation. It contains descriptor counts, tile information,
 * blob entry details, parsing state, mode flags, and references to related
 * DMA configuration data. This structure enables thorough validation and
 * processing of hardware sequencer operations while maintaining proper
 * abstraction from hardware-specific implementation details.
 */
struct pva_hwseq_priv {
	/**
	 * @brief Number of descriptors in the hardware sequencer blob
	 * Valid range: [1 .. UINT32_MAX]
	 */
	uint32_t desc_count;

	/**
	 * @brief Total number of tiles in the packet
	 *
	 * @details Sum total of descriptor repetition factors present in the
	 * hardware sequencer blob, representing the total tile processing load.
	 * Valid range: [1 .. UINT32_MAX]
	 */
	uint32_t tiles_per_packet;

	/**
	 * @brief Maximum tile X coordinate
	 * Valid range: [INT32_MIN .. INT32_MAX]
	 */
	int32_t max_tx;

	/**
	 * @brief Maximum tile Y coordinate
	 * Valid range: [INT32_MIN .. INT32_MAX]
	 */
	int32_t max_ty;

	/**
	 * @brief Hardware sequencer blob entry information
	 */
	struct hw_seq_blob_entry entry;

	/**
	 * @brief Hardware sequencer buffer for blob parsing
	 */
	struct pva_hwseq_buffer blob;

	/**
	 * @brief Flag indicating presence of split padding
	 * Valid values: true (split padding present), false (no split padding)
	 */
	bool is_split_padding;

	/**
	 * @brief Flag indicating scan mode type
	 *
	 * @details Indicates hardware sequencer scan mode:
	 * - true: Raster scan mode
	 * - false: Vertical mining mode
	 */
	bool is_raster_scan;

	/**
	 * @brief PVA hardware generation
	 *
	 * @details Hardware generation identifier for platform-specific
	 * behavior adaptation
	 * Valid values: @ref pva_hw_gen enumeration values
	 */
	enum pva_hw_gen hw_gen;

	/**
	 * @brief Pointer to DMA configuration header
	 */
	const struct pva_dma_config *dma_config;

	/**
	 * @brief Pointer to hardware sequencer header
	 */
	const struct pva_dma_hwseq_hdr *hdr;

	/**
	 * @brief Pointer to column/row header in hardware sequencer
	 */
	const struct pva_dma_hwseq_colrow_hdr *colrow;

	/**
	 * @brief Pointer to head descriptor in the hardware sequencer
	 */
	const struct pva_dma_descriptor *head_desc;

	/**
	 * @brief Pointer to tail descriptor in the hardware sequencer
	 */
	const struct pva_dma_descriptor *tail_desc;

	/**
	 * @brief Array of DMA descriptor entries from hardware sequencer blob
	 */
	struct pva_dma_hwseq_desc_entry dma_descs[2];

	/**
	 * @brief Pointer to calculated access sizes from hardware sequencer blob
	 */
	struct pva_kmd_dma_access *access_sizes;
};

/**
 * @brief Per-frame information for hardware sequencer processing
 *
 * @details This structure contains frame-specific information for hardware
 * sequencer operations, including sequence tile counts and VMEM tile
 * requirements per frame. It enables proper resource allocation and
 * validation for frame-based processing operations.
 */
struct pva_hwseq_per_frame_info {
	/**
	 * @brief Number of tiles in the sequence for this frame
	 * Valid range: [1 .. UINT32_MAX]
	 */
	uint32_t seq_tile_count;

	/**
	 * @brief Number of VMEM tiles required per frame
	 * Valid range: [1 .. UINT32_MAX]
	 */
	uint32_t vmem_tiles_per_frame;
};

/**
 * @brief Validate hardware sequencer configuration
 *
 * @details This function performs comprehensive validation of hardware sequencer
 * configurations including:
 * - Parsing and validating hardware sequencer blob structure
 * - Checking descriptor references and repetition factors
 * - Validating frame boundaries and addressing modes
 * - Computing memory access patterns and requirements
 * - Ensuring hardware constraints are satisfied
 * - Building descriptor usage masks for resource tracking
 * - Verifying grid parameters and tile arrangements
 *
 * The validation ensures that the hardware sequencer configuration is
 * safe for execution and will not violate hardware constraints or
 * memory protection boundaries. The function provides platform-agnostic
 * validation that can be adapted to different hardware implementations.
 *
 * @param[in]  dma_config         Pointer to DMA configuration containing hardware sequencer
 *                                Valid value: non-null
 * @param[in]  hw_consts          Pointer to hardware constants for validation
 *                                Valid value: non-null
 * @param[out] access_sizes       Array to store computed access size information
 *                                Valid value: non-null, min size PVA_MAX_NUM_DMA_DESC
 * @param[out] hw_dma_descs_mask  Bitmask for tracking hardware descriptor usage
 *                                Valid value: non-null
 *
 * @retval PVA_SUCCESS                  Hardware sequencer blob validated successfully
 * @retval PVA_ERR_HWSEQ_INVALID        Invalid hardware sequencer configuration
 * @retval PVA_ERANGE                   Hardware sequencer configuration exceeds bounds or limits
 * @retval PVA_INVAL                    Invalid descriptor reference in hardware sequencer
 * @retval PVA_BAD_PARAMETER_ERROR      Invalid grid parameters in hardware sequencer
 * @retval PVA_INVALID_RESOURCE         Invalid input parameters
 */
enum pva_error validate_hwseq(struct pva_dma_config const *dma_config,
			      struct pva_kmd_hw_constants const *hw_consts,
			      struct pva_kmd_dma_access *access_sizes,
			      uint64_t *hw_dma_descs_mask);

#endif
