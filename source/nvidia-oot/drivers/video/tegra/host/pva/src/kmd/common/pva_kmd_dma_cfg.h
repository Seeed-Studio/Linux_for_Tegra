/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_DMA_CFG_H
#define PVA_KMD_DMA_CFG_H

#include "pva_kmd.h"
#include "pva_resource.h"

/**
 * @brief Mask to extract the GOB offset from the Surface address
 *
 * @details Mask to extract the GOB offset from the Surface address
 * Used for block linear DMA configuration processing.
 * Value: 0x3E00
 */
#define PVA_DMA_BL_GOB_OFFSET_MASK 0x3E00U

/**
 * @brief Right shift value for moving GOB offset to LSB
 *
 * @details Right shift value for moving GOB offset value extracted from surface address to LSB
 * Used in conjunction with PVA_DMA_BL_GOB_OFFSET_MASK for block linear addressing.
 * Value: 6
 */
#define PVA_DMA_BL_GOB_OFFSET_MASK_RSH 6U

/**
 * @brief Maximum descriptor ID value supported by hardware
 *
 * @details Maximum descriptor ID value supported by hardware
 * Used for DMA descriptor validation and range checking.
 * Value: 0x3F (63 decimal)
 */
#define MAX_DESC_ID 0x3FU

/**
 * @brief Enumeration of DMA frame replication modes
 *
 * @details Defines the different frame replication modes supported by the DMA engine.
 * Frame replication allows efficient broadcasting of data to multiple destinations
 * or processing paths, optimizing memory bandwidth usage for certain algorithms.
 */
enum pva_dma_frame_rep {
	/** @brief No replication - single copy operation */
	REPLICATION_NONE = 0,
	/** @brief Two-way replication - data copied to 2 destinations */
	REPLICATION_TWO_WAY,
	/** @brief Four-way replication - data copied to 4 destinations */
	REPLICATION_FOUR_WAY,
	/** @brief Eight-way replication - data copied to 8 destinations */
	REPLICATION_EIGHT_WAY,
	/** @brief Sixteen-way replication - data copied to 16 destinations */
	REPLICATION_SIXTEEN_WAY,
	/** @brief Thirty-two-way replication - data copied to 32 destinations */
	REPLICATION_THIRTYTWO_WAY,
	/** @brief Full replication - maximum supported replication */
	REPLICATION_FULL
};

/**
 * @brief DMA access range entry for validation
 *
 * @details This structure defines a memory access range with start and end addresses
 * used for DMA validation. It ensures that DMA operations do not access memory
 * outside of allocated and mapped regions, providing memory protection and
 * preventing buffer overruns.
 */
struct pva_kmd_dma_access_entry {
	/**
	 * @brief Starting address of the access range
	 * Valid range: [INT64_MIN .. INT64_MAX]
	 */
	int64_t start_addr;

	/**
	 * @brief Ending address of the access range (inclusive)
	 * Valid range: [start_addr .. INT64_MAX]
	 */
	int64_t end_addr;
};

/**
 * @brief Complete DMA access information for validation
 *
 * @details This structure contains access range information for all potential
 * DMA targets including source, primary destination, and secondary destination.
 * It is used during DMA configuration validation to ensure all memory accesses
 * are within valid ranges and properly bounded.
 */
struct pva_kmd_dma_access {
	/** @brief Source buffer access range */
	struct pva_kmd_dma_access_entry src;
	/** @brief Primary destination buffer access range */
	struct pva_kmd_dma_access_entry dst;
	/** @brief Secondary destination buffer access range */
	struct pva_kmd_dma_access_entry dst2;
};

struct pva_kmd_resource_table;
struct pva_kmd_hw_constants;

/**
 * @brief Auxiliary information for DMA resource management
 *
 * @details Auxiliary information needed for managing DMA resources including
 * holding references to DRAM buffers and VPU binaries used by DMA configuration,
 * and providing scratch buffers needed during DMA configuration loading.
 * This structure maintains resource lifetime and dependency tracking.
 */
struct pva_kmd_dma_resource_aux {
	/**
	 * @brief Pointer to the resource table for resource management
	 */
	struct pva_kmd_resource_table *res_table;

	/**
	 * @brief Resource ID of the VPU binary used by this DMA configuration
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t vpu_bin_res_id;

	/**
	 * @brief Number of DRAM resources referenced by this DMA configuration
	 * Valid range: [0 .. PVA_KMD_MAX_NUM_DMA_DRAM_SLOTS]
	 */
	uint32_t dram_res_count;

	/**
	 * @brief Array of DRAM buffer resource IDs statically referenced by DMA configuration
	 *
	 * @details DRAM buffers statically referenced by the DMA configuration
	 */
	uint32_t static_dram_res_ids[PVA_KMD_MAX_NUM_DMA_DRAM_SLOTS];
};

/**
 * @brief Scratch buffers for DMA configuration processing
 *
 * @details Scratch buffers needed during DMA configuration loading that don't fit on stack.
 * These buffers provide temporary storage for parsing, validation, and conversion
 * operations during DMA configuration setup. They help avoid stack overflow issues
 * and provide efficient memory management for DMA operations.
 */
struct pva_kmd_dma_scratch_buffer {
	/**
	 * @brief Static slot definitions for firmware
	 */
	struct pva_fw_dma_slot static_slots[PVA_KMD_MAX_NUM_DMA_SLOTS];

	/**
	 * @brief Static relocation information for firmware
	 */
	struct pva_fw_dma_reloc static_relocs[PVA_KMD_MAX_NUM_DMA_SLOTS];

	/**
	 * @brief Access size validation information for each descriptor
	 */
	struct pva_kmd_dma_access access_sizes[PVA_MAX_NUM_DMA_DESC];

	/**
	 * @brief Bitmask for hardware DMA descriptors tracking
	 */
	uint64_t hw_dma_descs_mask[((PVA_MAX_NUM_DMA_DESC / 64ULL) + 1ULL)];
};

/**
 * @brief Parse and validate user-provided DMA configuration
 *
 * @details This function performs the following operations:
 * - Parses the user-provided DMA configuration data structure
 * - Validates configuration parameters against hardware constants
 * - Converts user format to internal representation for processing
 * - Checks descriptor counts, slot assignments, and parameter ranges
 * - Ensures configuration compatibility with hardware capabilities
 * - Prepares configuration for further validation and loading steps
 *
 * The parsing includes validation of DMA descriptors, slot configurations,
 * addressing modes, and other parameters required for safe DMA operation.
 * Any invalid parameters or unsupported configurations will result in
 * appropriate error codes.
 *
 * @param[in] dma_cfg_hdr    Pointer to user DMA configuration header
 *                           Valid value: non-null
 * @param[in] dma_config_size Size of the DMA configuration data in bytes
 *                           Valid range: [sizeof(header) .. UINT32_MAX]
 * @param[out] out_cfg       Pointer to parsed DMA configuration output
 *                           Valid value: non-null
 * @param[in] hw_consts      Pointer to hardware constants for validation
 *                           Valid value: non-null
 * @param[in] skip_validation Skip DMA validation if true
 *                           Valid value: true, false
 *
 * @retval PVA_SUCCESS                  DMA configuration parsed successfully
 * @retval PVA_INVALID_DMA_CONFIG       Invalid configuration parameters
 * @retval PVA_NOT_IMPL                 Unsupported DMA configuration feature
 * @retval PVA_INVAL                    Invalid input parameters
 */
enum pva_error pva_kmd_parse_dma_config(
	const struct pva_ops_dma_config_register *dma_cfg_hdr,
	uint32_t dma_config_size, struct pva_dma_config *out_cfg,
	struct pva_kmd_hw_constants const *hw_consts, bool skip_validation);

/**
 * @brief Acquire references to resources used by DMA configuration
 *
 * @details This function performs the following operations:
 * - Identifies all resources (DRAM buffers, VPU binaries) used by DMA config
 * - Acquires reference counts for these resources to prevent premature cleanup
 * - Updates the auxiliary structure with resource tracking information
 * - Ensures resource lifetime extends through DMA configuration usage
 * - Provides dependency management for proper resource cleanup ordering
 *
 * This function must be called before using a DMA configuration to ensure
 * all referenced resources remain valid. The references should be released
 * using @ref pva_kmd_unload_dma_config_unsafe() when the configuration is
 * no longer needed.
 *
 * @param[in] dma_cfg       Pointer to parsed DMA configuration
 *                          Valid value: non-null
 * @param[in, out] dma_aux  Pointer to DMA auxiliary structure to update
 *                          Valid value: non-null
 *
 * @retval PVA_SUCCESS              Resources acquired successfully
 * @retval PVA_INVALID_RESOURCE     Resource in invalid state for acquisition
 * @retval PVA_INVAL                Invalid configuration or auxiliary pointer
 */
enum pva_error
pva_kmd_dma_use_resources(struct pva_dma_config const *dma_cfg,
			  struct pva_kmd_dma_resource_aux *dma_aux);

/**
 * @brief Validate DMA configuration against hardware constraints
 *
 * @details This function performs the following operations:
 * - Validates DMA configuration parameters against hardware capabilities
 * - Checks descriptor limits, slot usage, and addressing constraints
 * - Computes access ranges for memory validation
 * - Builds hardware descriptor usage masks for tracking
 * - Ensures configuration will execute safely on hardware
 * - Verifies memory access patterns and alignment requirements
 *
 * The validation covers all aspects of DMA operation including transfer
 * sizes, addressing modes, descriptor linking, and hardware resource usage.
 * This comprehensive validation prevents runtime errors and ensures
 * reliable DMA operation.
 *
 * @param[in] dma_cfg           Pointer to DMA configuration to validate
 *                              Valid value: non-null
 * @param[in] hw_consts         Pointer to hardware constants for validation
 *                              Valid value: non-null
 * @param[out] access_sizes     Array to store computed access size information
 *                              Valid value: non-null, min size PVA_MAX_NUM_DMA_DESC
 * @param[out] hw_dma_descs_mask Bitmask for tracking hardware descriptor usage
 *                              Valid value: non-null
 *
 * @retval PVA_SUCCESS                  DMA configuration validated successfully
 * @retval PVA_INVALID_DMA_CONFIG       Configuration violates hardware constraints
 * @retval PVA_ENOSPC                   Configuration exceeds hardware limits
 * @retval PVA_INVAL                    Invalid configuration or output pointers
 */
enum pva_error
pva_kmd_validate_dma_config(struct pva_dma_config const *dma_cfg,
			    struct pva_kmd_hw_constants const *hw_consts,
			    struct pva_kmd_dma_access *access_sizes,
			    uint64_t *hw_dma_descs_mask);

/**
 * @brief Compute memory access patterns for DMA configuration
 *
 * @details This function performs the following operations:
 * - Analyzes DMA descriptors to determine memory access patterns
 * - Computes source and destination address ranges for each operation
 * - Builds hardware descriptor usage masks for resource tracking
 * - Calculates total memory footprint and access requirements
 * - Provides information needed for memory validation and protection
 * - Determines optimal memory layout and access strategies
 *
 * The computed access information is used for memory validation, SMMU
 * programming, and ensuring DMA operations remain within allocated
 * buffer boundaries. This analysis is critical for memory protection
 * and system stability.
 *
 * @param[in] dma_cfg           Pointer to DMA configuration to analyze
 *                              Valid value: non-null
 * @param[out] access_sizes     Array to store computed access information
 *                              Valid value: non-null, min size PVA_MAX_NUM_DMA_DESC
 * @param[out] hw_dma_descs_mask Bitmask for tracking hardware descriptor usage
 *                              Valid value: non-null
 *
 * @retval PVA_SUCCESS              Access patterns computed successfully
 * @retval PVA_INVALID_DMA_CONFIG   Invalid DMA configuration for analysis
 * @retval PVA_INVAL                Invalid configuration or output pointers
 */
enum pva_error
pva_kmd_compute_dma_access(struct pva_dma_config const *dma_cfg,
			   struct pva_kmd_dma_access *access_sizes,
			   uint64_t *hw_dma_descs_mask);

/**
 * @brief Collect relocation information for firmware DMA configuration
 *
 * @details This function performs the following operations:
 * - Extracts static and dynamic slot information from DMA configuration
 * - Builds relocation tables for firmware address patching
 * - Separates static slots (bound at load time) from dynamic slots
 * - Generates firmware-compatible slot and relocation structures
 * - Maps descriptor channels to slots for proper address resolution
 * - Prepares data needed for firmware DMA setup and execution
 *
 * The relocation information enables firmware to properly patch addresses
 * in DMA descriptors at runtime, supporting both static (pre-bound) and
 * dynamic (runtime-bound) memory slots for flexible DMA operation.
 *
 * @param[in] dma_cfg           Pointer to DMA configuration
 *                              Valid value: non-null
 * @param[in] access_sizes      Array of access size information
 *                              Valid value: non-null
 * @param[out] out_static_slots Array to store static slot information
 *                              Valid value: non-null, min size num_static_slots
 * @param[in] num_static_slots  Number of static slots to process
 *                              Valid range: [0 .. PVA_KMD_MAX_NUM_DMA_SLOTS]
 * @param[out] out_static_relocs Array to store static relocation information
 *                              Valid value: non-null
 * @param[out] out_dyn_slots    Array to store dynamic slot information
 *                              Valid value: non-null, min size num_dyn_slots
 * @param[in] num_dyn_slots     Number of dynamic slots to process
 *                              Valid range: [0 .. PVA_KMD_MAX_NUM_DMA_SLOTS]
 * @param[out] out_dyn_relocs   Array to store dynamic relocation information
 *                              Valid value: non-null
 * @param[in] desc_to_ch        Array mapping descriptors to channels
 *                              Valid value: non-null
 */
void pva_kmd_collect_relocs(struct pva_dma_config const *dma_cfg,
			    struct pva_kmd_dma_access const *access_sizes,
			    struct pva_fw_dma_slot *out_static_slots,
			    uint16_t num_static_slots,
			    struct pva_fw_dma_reloc *out_static_relocs,
			    struct pva_fw_dma_slot *out_dyn_slots,
			    uint16_t num_dyn_slots,
			    struct pva_fw_dma_reloc *out_dyn_relocs,
			    uint8_t const *desc_to_ch);

/**
 * @brief Bind static buffers to the DMA configuration
 *
 * @details This function performs the following operations:
 * - Edits @ref pva_dma_config in-place to replace offset fields with final addresses
 * - Resolves static buffer addresses from resource table entries
 * - Validates that DMA configuration does not access static buffers out of range
 * - Updates firmware DMA configuration with bound address information
 * - Ensures memory protection by validating all access patterns
 * - Prepares configuration for safe execution with static buffer bindings
 *
 * When binding static buffers, this function modifies the DMA configuration
 * to embed actual memory addresses, enabling efficient firmware execution
 * without runtime address resolution for static resources.
 *
 * @param[in, out] fw_dma_cfg     Pointer to firmware DMA configuration to update
 *                                Valid value: non-null
 * @param[in] dma_aux             Pointer to DMA auxiliary resource information
 *                                Valid value: non-null
 * @param[in] static_slots        Array of static slot definitions
 *                                Valid value: non-null
 * @param[in] num_static_slots    Number of static slots to bind
 *                                Valid range: [0 .. PVA_KMD_MAX_NUM_DMA_SLOTS]
 * @param[in] static_relocs       Array of static relocation information
 *                                Valid value: non-null
 * @param[in] static_bindings     Array of static binding specifications
 *                                Valid value: non-null
 * @param[in] num_static_bindings Number of static bindings to process
 *                                Valid range: [0 .. UINT32_MAX]
 *
 * @retval PVA_SUCCESS                  Static buffers bound successfully
 * @retval PVA_INVALID_BINDING          Invalid binding specification
 * @retval PVA_BUF_OUT_OF_RANGE         Buffer access would exceed valid range
 * @retval PVA_INVALID_RESOURCE         Resource in invalid state for binding
 */
enum pva_error pva_kmd_bind_static_buffers(
	struct pva_dma_config_resource *fw_dma_cfg,
	struct pva_kmd_dma_resource_aux *dma_aux,
	struct pva_fw_dma_slot const *static_slots, uint16_t num_static_slots,
	struct pva_fw_dma_reloc const *static_relocs,
	struct pva_dma_static_binding const *static_bindings,
	uint32_t num_static_bindings);

/**
 * @brief Convert user DMA configuration to firmware format
 *
 * @details This function performs the following operations:
 * - Converts parsed DMA configuration to firmware-compatible format
 * - Serializes configuration data for efficient firmware consumption
 * - Calculates the size of configuration data needed in firmware memory
 * - Optimizes configuration layout for firmware execution performance
 * - Handles hardware-specific formatting and alignment requirements
 * - Prepares configuration for transfer to firmware execution environment
 *
 * The firmware format is optimized for efficient parsing and execution
 * by the PVA firmware, with proper alignment and layout for direct
 * hardware register programming.
 *
 * @param[in] dma_cfg                     Pointer to parsed DMA configuration
 *                                        Valid value: non-null
 * @param[out] fw_dma_config              Buffer for firmware DMA configuration output
 *                                        Valid value: non-null, sufficient size
 * @param[out] out_fw_fetch_size          Pointer to store firmware fetch size
 *                                        Valid value: non-null
 * @param[in] support_hwseq_frame_linking Whether hardware sequence frame linking is supported
 *                                        Valid values: true, false
 */
void pva_kmd_write_fw_dma_config(struct pva_dma_config const *dma_cfg,
				 void *fw_dma_config,
				 uint32_t *out_fw_fetch_size,
				 bool support_hwseq_frame_linking);

/**
 * @brief Load DMA configuration into firmware format
 *
 * @details This function performs comprehensive DMA configuration loading including:
 * - Validates the DMA configuration against hardware constraints
 * - Binds static resources (buffers) and embeds their addresses in firmware configuration
 * - Holds references to DRAM buffers and VPU binaries used by the configuration
 * - Converts the DMA configuration into optimized firmware format
 * - Computes memory requirements and access patterns
 * - Ensures proper resource lifetime management and dependency tracking
 * - Provides complete configuration ready for firmware execution
 *
 * This function integrates all aspects of DMA configuration processing
 * into a single operation, producing a validated and optimized configuration
 * that can be safely executed by the firmware.
 *
 * @param[in] resource_table     Pointer to resource table for the context
 *                               Valid value: non-null, must be initialized
 * @param[in] dma_cfg_hdr        Pointer to user DMA configuration header
 *                               Valid value: non-null
 * @param[in] dma_config_size    Size of the dma_config buffer in bytes
 *                               Valid range: [sizeof(header) .. UINT32_MAX]
 * @param[in, out] dma_aux       Pointer to auxiliary information for DMA loading
 *                               Valid value: non-null
 * @param[out] fw_dma_cfg        Output buffer for firmware DMA configuration
 *                               Valid value: non-null, sufficient size
 * @param[out] out_fw_fetch_size Pointer to store size of configuration to fetch into TCM
 *                               Valid value: non-null
 *
 * @retval PVA_SUCCESS                  DMA configuration loaded successfully
 * @retval PVA_INVALID_DMA_CONFIG       Invalid or unsupported DMA configuration
 * @retval PVA_INVALID_RESOURCE         Resource in invalid state for loading
 * @retval PVA_NOMEM                    Failed to allocate required memory
 * @retval PVA_BUF_OUT_OF_RANGE         Configuration would violate memory protection
 */
enum pva_error pva_kmd_load_dma_config(
	struct pva_kmd_resource_table *resource_table,
	const struct pva_ops_dma_config_register *dma_cfg_hdr,
	uint32_t dma_config_size, struct pva_kmd_dma_resource_aux *dma_aux,
	void *fw_dma_cfg, uint32_t *out_fw_fetch_size, bool priv);

/**
 * @brief Unload DMA configuration and release associated resources (unsafe version)
 *
 * @details This function performs the following operations:
 * - Releases all resource references held by the DMA configuration
 * - Cleans up auxiliary resource tracking information
 * - Frees memory allocations associated with the configuration
 * - Invalidates the DMA auxiliary structure for future use
 * - Does not perform thread-safe locking (caller's responsibility)
 *
 * This function is not thread-safe and requires external synchronization.
 * It should be called when a DMA configuration is no longer needed to
 * ensure proper resource cleanup and prevent memory leaks.
 *
 * @param[in, out] dma_aux  Pointer to DMA auxiliary structure to clean up
 *                          Valid value: non-null, must have been initialized
 */
void pva_kmd_unload_dma_config_unsafe(struct pva_kmd_dma_resource_aux *dma_aux);
#endif // PVA_KMD_DMA_CFG_H
