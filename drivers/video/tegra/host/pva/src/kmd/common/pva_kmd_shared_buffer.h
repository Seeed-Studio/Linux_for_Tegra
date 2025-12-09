/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_SHARED_BUFFER_H
#define PVA_KMD_SHARED_BUFFER_H

#include "pva_kmd_device.h"

/**
 * @brief Function pointer type for processing shared buffer elements
 *
 * @details This function pointer type defines the signature for callback
 * functions used to process individual elements from shared buffers. The
 * callback is invoked for each element that needs to be processed, allowing
 * different interfaces to handle their specific element types appropriately.
 *
 * @param[in] context    Pointer to context data for the processing operation
 *                       Valid value: can be null if no context needed
 * @param[in] interface  Interface identifier for the shared buffer
 *                       Valid range: [0 .. UINT8_MAX]
 * @param[in] element    Pointer to the element data to be processed
 *                       Valid value: non-null
 *
 * @retval PVA_SUCCESS  Element processed successfully
 * @retval error_code   Processing failed with specific error
 */
typedef enum pva_error (*shared_buffer_process_element_cb)(void *context,
							   uint8_t interface,
							   uint8_t *element);

/**
 * @brief Function pointer type for shared buffer locking operations
 *
 * @details This function pointer type defines the signature for callback
 * functions used to lock and unlock shared buffers. These callbacks provide
 * synchronization mechanisms to ensure thread-safe access to shared buffer
 * data structures between KMD and firmware operations.
 *
 * @param[in, out] pva  Pointer to @ref pva_kmd_device structure
 *                      Valid value: non-null
 * @param[in] interface Identifier for the shared buffer interface
 *                      Valid range: [0 .. UINT8_MAX]
 */
typedef void (*shared_buffer_lock_cb)(struct pva_kmd_device *pva,
				      uint8_t interface);

/**
 * @brief Structure for managing shared buffer communication between KMD and firmware
 *
 * @details This structure manages a shared buffer used for communication between
 * the KMD and firmware. The buffer consists of a header located in shared DRAM
 * memory that both KMD and firmware can access, along with local KMD fields
 * for bookkeeping and callback management. The structure supports element-based
 * communication with configurable processing and synchronization callbacks.
 */
struct pva_kmd_shared_buffer {
	/**
	 * @brief Pointer to shared buffer header in DRAM memory
	 *
	 * @details Only 'header' is in shared DRAM memory accessible by both KMD and firmware
	 */
	struct pva_fw_shared_buffer_header *header;

	/**
	 * @brief Pointer to beginning of buffer contents in DRAM
	 *
	 * @details 'body' tracks the beginning of buffer contents in DRAM
	 */
	uint8_t *body;

	/**
	 * @brief Callback function for processing buffer elements
	 *
	 * @details 'process_cb' callback is used to process elements in the buffer
	 */
	shared_buffer_process_element_cb process_cb;

	/**
	 * @brief Callback function for acquiring buffer lock
	 *
	 * @details 'lock_cb' callback is used to lock the buffer for thread safety
	 */
	shared_buffer_lock_cb lock_cb;

	/**
	 * @brief Callback function for releasing buffer lock
	 *
	 * @details 'unlock_cb' callback is used to unlock the buffer
	 */
	shared_buffer_lock_cb unlock_cb;

	/**
	 * @brief Device memory allocation tracking for the buffer
	 *
	 * @details 'resource_memory' is used to track the memory allocated for the buffer
	 */
	struct pva_kmd_device_memory *resource_memory;

	/**
	 * @brief Offset of buffer within the allocated resource
	 *
	 * @details 'resource_offset' is used to track offset of buffer in 'resource_id'
	 * Valid range: [0 .. UINT32_MAX]
	 */
	uint32_t resource_offset;
};

/**
 * @brief Initialize a shared buffer for KMD-firmware communication
 *
 * @details This function performs the following operations:
 * - Allocates device memory for the shared buffer and its header
 * - Initializes the buffer header with specified element and buffer sizes
 * - Sets up the buffer structure with provided callback functions
 * - Configures the buffer for the specified interface identifier
 * - Registers the buffer memory with the device's resource management system
 * - Establishes proper synchronization mechanisms for thread-safe access
 * - Prepares the buffer for element-based communication with firmware
 *
 * The initialized buffer can be used for bidirectional communication between
 * KMD and firmware, with elements processed using the provided callback
 * functions. The buffer must be deinitialized using @ref pva_kmd_shared_buffer_deinit()
 * when no longer needed.
 *
 * @param[in, out] pva          Pointer to @ref pva_kmd_device structure
 *                              Valid value: non-null
 * @param[in] interface         Interface identifier for this shared buffer
 *                              Valid range: [0 .. PVA_MAX_NUM_CCQ-1]
 * @param[in] element_size      Size of each element in bytes
 *                              Valid range: [1 .. UINT32_MAX]
 * @param[in] buffer_size       Total buffer size in bytes
 *                              Valid range: [element_size .. UINT32_MAX]
 * @param[in] lock_cb           Callback function for acquiring buffer lock
 *                              Valid value: non-null
 * @param[in] unlock_cb         Callback function for releasing buffer lock
 *                              Valid value: non-null
 *
 * @retval PVA_SUCCESS                  Buffer initialized successfully
 * @retval PVA_NOMEM                    Failed to allocate buffer memory
 * @retval PVA_INVAL                    Invalid parameters or callback pointers
 * @retval PVA_INTERNAL                 Failed to register buffer with resource system
 */
enum pva_error pva_kmd_shared_buffer_init(struct pva_kmd_device *pva,
					  uint8_t interface,
					  uint32_t element_size,
					  uint32_t buffer_size,
					  shared_buffer_lock_cb lock_cb,
					  shared_buffer_lock_cb unlock_cb);

/**
 * @brief Deinitialize a shared buffer and free associated resources
 *
 * @details This function performs the following operations:
 * - Ensures the buffer is properly synchronized and no operations are pending
 * - Unregisters the buffer memory from the device's resource management system
 * - Frees the allocated device memory for buffer and header
 * - Cleans up the buffer structure and callback associations
 * - Invalidates the buffer for the specified interface identifier
 * - Ensures proper cleanup of synchronization mechanisms
 *
 * After calling this function, the shared buffer becomes invalid and cannot
 * be used for communication. Any pending operations should be completed
 * before calling this function to avoid resource leaks or synchronization issues.
 *
 * @param[in, out] pva      Pointer to @ref pva_kmd_device structure
 *                          Valid value: non-null
 * @param[in] interface     Interface identifier for the buffer to deinitialize
 *                          Valid range: [0 .. PVA_MAX_NUM_CCQ-1]
 *
 * @retval PVA_SUCCESS                Buffer deinitialized successfully
 * @retval PVA_INVAL                  Invalid interface or device pointer
 * @retval PVA_AGAIN                  Buffer still has pending operations
 */
enum pva_error pva_kmd_shared_buffer_deinit(struct pva_kmd_device *pva,
					    uint8_t interface);

/**
 * @brief Process pending elements in the shared buffer
 *
 * @details This function performs the following operations:
 * - Acquires the buffer lock using the configured lock callback
 * - Checks for new elements available in the shared buffer
 * - Processes each available element using the configured process callback
 * - Updates buffer pointers to mark elements as consumed
 * - Handles buffer wraparound and overflow conditions appropriately
 * - Releases the buffer lock using the configured unlock callback
 * - Maintains proper synchronization between KMD and firmware
 *
 * This function is typically called in response to notifications from
 * firmware indicating that new data is available in the shared buffer.
 * The processing continues until all available elements have been handled.
 *
 * @param[in] pva_dev       Pointer to PVA device structure
 *                          Valid value: non-null
 * @param[in] interface     Interface identifier for the buffer to process
 *                          Valid range: [0 .. PVA_MAX_NUM_CCQ-1]
 */
void pva_kmd_shared_buffer_process(void *pva_dev, uint8_t interface);

/**
 * @brief Bind a processing handler to a shared buffer interface
 *
 * @details This function performs the following operations:
 * - Associates a processing callback with the specified shared buffer interface
 * - Stores the provided context data for use during element processing
 * - Configures the buffer to use the specified handler for incoming elements
 * - Validates that the interface and handler are properly configured
 * - Updates internal state to enable processing for this interface
 *
 * The bound handler will be called for each element that needs processing
 * on the specified interface. The context data is passed to the handler
 * to provide any necessary state or configuration information.
 *
 * @param[in] pva_dev       Pointer to PVA device structure
 *                          Valid value: non-null
 * @param[in] interface     Interface identifier for the buffer
 *                          Valid range: [0 .. PVA_MAX_NUM_CCQ-1]
 * @param[in] data          Context data to pass to the processing handler
 *                          Valid value: can be null if no context needed
 *
 * @retval PVA_SUCCESS              Handler bound successfully
 * @retval PVA_INVAL                Invalid interface or device pointer
 * @retval PVA_INTERNAL             Buffer not properly initialized
 */
enum pva_error pva_kmd_bind_shared_buffer_handler(void *pva_dev,
						  uint8_t interface,
						  void *data);

/**
 * @brief Release the processing handler from a shared buffer interface
 *
 * @details This function performs the following operations:
 * - Removes the processing callback association from the specified interface
 * - Clears any stored context data for the interface
 * - Disables element processing for the interface
 * - Ensures proper cleanup of handler-related resources
 * - Updates internal state to reflect the handler removal
 *
 * After calling this function, the shared buffer will no longer process
 * elements for the specified interface until a new handler is bound using
 * @ref pva_kmd_bind_shared_buffer_handler().
 *
 * @param[in] pva_dev       Pointer to PVA device structure
 *                          Valid value: non-null
 * @param[in] interface     Interface identifier for the buffer
 *                          Valid range: [0 .. PVA_MAX_NUM_CCQ-1]
 */
void pva_kmd_release_shared_buffer_handler(void *pva_dev, uint8_t interface);
#endif
