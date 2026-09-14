/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_VPU_OCD_H
#define PVA_KMD_VPU_OCD_H

/**
 * @brief Maximum number of data access operations in VPU OCD interface
 *
 * @details This constant defines the maximum number of data elements that can
 * be accessed in a single VPU on-chip debug operation. It limits the size of
 * data arrays in VPU OCD parameter structures to ensure bounded memory usage
 * and predictable operation timing.
 */
#define VPU_OCD_MAX_NUM_DATA_ACCESS 7U

#if PVA_ENABLE_VPU_OCD == 1
/**
 * @brief VPU on-chip debug write operation parameters
 *
 * @details This structure contains parameters for VPU on-chip debug write
 * operations. It includes the instruction to execute and the data to be
 * written during the debug operation. The structure allows for multiple
 * data elements to be written in a single operation for efficiency.
 */
struct pva_vpu_ocd_write_param {
	/**
	 * @brief Debug instruction to execute
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t instr;

	/**
	 * @brief Number of data elements to write
	 * Valid range: [0 .. VPU_OCD_MAX_NUM_DATA_ACCESS]
	 */
	uint32_t n_write;

	/**
	 * @brief Array of data elements to write
	 * Valid range for each element: [0 .. UINT32_MAX]
	 */
	uint32_t data[VPU_OCD_MAX_NUM_DATA_ACCESS];
};

/**
 * @brief VPU on-chip debug read operation parameters
 *
 * @details This structure contains parameters for VPU on-chip debug read
 * operations. It specifies the number of data elements to read and provides
 * storage for the retrieved data. The structure supports reading multiple
 * data elements in a single operation for efficiency.
 */
struct pva_vpu_ocd_read_param {
	/**
	 * @brief Number of data elements to read
	 * Valid range: [0 .. VPU_OCD_MAX_NUM_DATA_ACCESS]
	 */
	uint32_t n_read;

	/**
	 * @brief Array to store read data elements
	 * Valid range for each element: [0 .. UINT32_MAX]
	 */
	uint32_t data[VPU_OCD_MAX_NUM_DATA_ACCESS];
};

/**
 * @brief Read data from VPU on-chip debug (OCD) interface
 *
 * @details This function performs read operations from the VPU's on-chip
 * debug interface. It provides access to VPU debug registers, internal state,
 * and memory for debugging and development purposes. The function reads data
 * from the specified offset within the VPU debug space and copies it to the
 * provided buffer.
 *
 * @param[in]  dev       Pointer to @ref pva_kmd_device structure
 *                       Valid value: non-null, must be initialized
 * @param[in]  file_data Pointer to file-specific data context
 *                       Valid value: platform-specific pointer or NULL
 * @param[out] data      Buffer to store read data
 *                       Valid value: non-null, must have capacity >= size
 * @param[in]  offset    Byte offset within VPU debug space to read from
 *                       Valid range: [0 .. VPU_DEBUG_SPACE_SIZE-1]
 * @param[in]  size      Number of bytes to read
 *                       Valid range: [1 .. remaining_space_from_offset]
 *
 * @retval >=0                      Number of bytes successfully read
 * @retval PVA_INVAL                Invalid argument provided
 * @retval PVA_INTERNAL             Device not in proper state for debug access
 * @retval PVA_EACCES               Debug access not permitted
 * @retval PVA_TIMEDOUT             Timeout during debug operation
 * @retval PVA_INTERNAL             Hardware error during debug access
 */
int64_t pva_kmd_vpu_ocd_read(struct pva_kmd_device *dev, void *file_data,
			     uint8_t *data, uint64_t offset, uint64_t size);

/**
 * @brief Write data to VPU on-chip debug (OCD) interface
 *
 * @details This function performs write operations to the VPU's on-chip
 * debug interface. It provides access to write to VPU debug registers,
 * internal state, and memory for debugging and development purposes. The
 * function writes data from the provided buffer to the specified offset
 * within the VPU debug space.
 *
 * @param[in] dev       Pointer to @ref pva_kmd_device structure
 *                      Valid value: non-null, must be initialized
 * @param[in] file_data Pointer to file-specific data context
 *                      Valid value: platform-specific pointer or NULL
 * @param[in] data      Buffer containing data to write
 *                      Valid value: non-null, must contain valid data
 * @param[in] offset    Byte offset within VPU debug space to write to
 *                      Valid range: [0 .. VPU_DEBUG_SPACE_SIZE-1]
 * @param[in] size      Number of bytes to write
 *                      Valid range: [1 .. remaining_space_from_offset]
 *
 * @retval >=0                      Number of bytes successfully written
 * @retval PVA_INVAL                Invalid argument provided
 * @retval PVA_INTERNAL             Device not in proper state for debug access
 * @retval PVA_EACCES               Debug access not permitted
 * @retval PVA_TIMEDOUT             Timeout during debug operation
 * @retval PVA_INTERNAL             Hardware error during debug access
 */
int64_t pva_kmd_vpu_ocd_write(struct pva_kmd_device *dev, void *file_data,
			      const uint8_t *data, uint64_t offset,
			      uint64_t size);

/**
 * @brief Open VPU on-chip debug (OCD) interface for access
 *
 * @details This function initializes and opens the VPU's on-chip debug
 * interface for subsequent debug operations. It performs necessary hardware
 * setup, validation checks, and prepares the debug interface for read/write
 * operations. This function must be called before any VPU debug access
 * operations can be performed.
 *
 * @param[in] dev Pointer to @ref pva_kmd_device structure
 *                Valid value: non-null, must be initialized
 *
 * @retval PVA_SUCCESS              VPU OCD interface opened successfully
 * @retval PVA_INVAL                Invalid device pointer provided
 * @retval PVA_INTERNAL             Device not in proper state for debug access
 * @retval PVA_EACCES               Debug access not permitted
 * @retval PVA_AGAIN                Debug interface already in use
 * @retval PVA_INTERNAL             Hardware initialization failure
 */
int pva_kmd_vpu_ocd_open(struct pva_kmd_device *dev);

/**
 * @brief Release VPU on-chip debug (OCD) interface and cleanup resources
 *
 * @details This function releases the VPU's on-chip debug interface and
 * performs necessary cleanup operations. It ensures proper shutdown of the
 * debug interface, releases any allocated resources, and restores the device
 * to normal operation mode. This function should be called when VPU debug
 * access is no longer needed.
 *
 * @param[in] dev Pointer to @ref pva_kmd_device structure
 *                Valid value: non-null, must be initialized
 *
 * @retval PVA_SUCCESS              VPU OCD interface released successfully
 * @retval PVA_INVAL                Invalid device pointer provided
 * @retval PVA_INTERNAL             Device not in proper state for release
 * @retval PVA_INTERNAL             Hardware shutdown failure
 */
int pva_kmd_vpu_ocd_release(struct pva_kmd_device *dev);

/* Initialize VPU OCD debugfs nodes */
enum pva_error pva_kmd_vpu_ocd_init_debugfs(struct pva_kmd_device *pva);

#else /* PVA_ENABLE_VPU_OCD */

/* Dummy struct definition when VPU OCD is disabled */
struct pva_vpu_ocd_write_param {
	uint32_t dummy;
};

/* Dummy inline functions when VPU OCD is disabled */
static inline int64_t pva_kmd_vpu_ocd_read(struct pva_kmd_device *dev,
					   void *file_data, uint8_t *data,
					   uint64_t offset, uint64_t size)
{
	(void)dev;
	(void)file_data;
	(void)data;
	(void)offset;
	(void)size;
	return -1;
}

static inline int64_t pva_kmd_vpu_ocd_write(struct pva_kmd_device *dev,
					    void *file_data,
					    const uint8_t *data,
					    uint64_t offset, uint64_t size)
{
	(void)dev;
	(void)file_data;
	(void)data;
	(void)offset;
	(void)size;
	return -1;
}

static inline int pva_kmd_vpu_ocd_open(struct pva_kmd_device *dev)
{
	(void)dev;
	return -1;
}

static inline int pva_kmd_vpu_ocd_release(struct pva_kmd_device *dev)
{
	(void)dev;
	return -1;
}

/* Dummy initialization function when VPU OCD is disabled */
static inline enum pva_error
pva_kmd_vpu_ocd_init_debugfs(struct pva_kmd_device *pva)
{
	(void)pva;
	return PVA_SUCCESS;
}

#endif /* PVA_ENABLE_VPU_OCD */

#endif
