/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef TEEC_SOC_PLUGIN_H
#define TEEC_SOC_PLUGIN_H

#include "teec-soc-plugin-os.h"
#include <linux/types.h>

/*
 *              TEE Client SOC Plugin HAL Interface Version Information
 * --------------------------------------------------------------------------------
 *  Revision    Changes/Additions
 *
 *  V0.1        Initial version.
 */

#define TEEC_SOC_PLUGIN_INTERFACE_MAJOR_VERSION	0
#define TEEC_SOC_PLUGIN_INTERFACE_MINOR_VERSION	1

/**
 * @file teec-soc-plugin.h
 * @brief Common communication interface for TEE (Trusted Execution Environment) client operations
 *
 * @b Description: TEE transport abstraction interface.
 */

/**
 * @defgroup teec_soc_plugin_hal TEEC_SOC_PLUGIN_HAL_API::Transport Interface
 *
 * The TEE Client SOC Plugin interface provides a polymorphic communication layer
 * that abstracts different transport mechanisms (e.g., Mailbox, IVC)
 * for communicating with the Trusted Execution Environment (TEE).
 * This interface is designed for cross-module usage where the main nvtzvault.ko module
 * calls functions exported by this SOC plugin module (teec-soc-plugin.ko).
 * The SOC plugin module must be loaded first to provide the transport implementation.
 *
 * The plugin implements the TeeClient function pointer interface that enables:
 * - Common nvtzvault.ko driver supporting multiple transport types
 * - Transport-specific implementation isolation from generic driver code
 * - Where required, transport selection based on device tree properties.
 *
 * Interface Usage Pattern:
 * 1. Initialize: teec_initialize_interface() -> teec_comms_init() -> teec_comms_reset()
 * 2. Communicate: teec_comms_acq_lock() -> teec_comms_send_msg() -> teec_comms_wait_event() ->
 *               teec_comms_read_msg() -> teec_comms_rel_lock()
 * 3. Cleanup: teec_comms_reset_memory() -> teec_comms_deinit()
 *
 * @{
 */

/**
 * @brief Error codes for TEE client operations
 *
 * This enum defines standardized error codes returned by all TEE communication
 * functions. The values are chosen to be consistent with TEE Client API standards
 * and provide specific error categorization for different failure scenarios.
 *
 * @note kernel_checkpatch: typedef required to conform to plugin interface
 */
typedef enum {
	/** Indicates the operation was successful. */
	TEE_CLIENT_STATUS_OK             = 0U,
	/** Indicates an unspecified error occurred. */
	TEE_CLIENT_STATUS_GENERIC_ERROR  = 15U,
	/** Indicates input parameters are invalid. */
	TEE_CLIENT_STATUS_BAD_PARAMETERS = 51U,
	/** Indicates the operation is invalid in its current state. */
	TEE_CLIENT_STATUS_BAD_STATE      = 60U,
	/** Indicates that there is no response from TOS */
	TEE_CLIENT_STATUS_NO_RESPONSE_FROM_TOS = 85U,
	/** Indicates that the LIST is Empty */
	TEE_CLIENT_STATUS_LIST_EMPTY      = 90U,
	/** Indicates the system ran out of resources. */
	TEE_CLIENT_STATUS_OUT_OF_MEMORY  = 102U,
	/** Indicates that there is Access Denied*/
	TEE_CLIENT_STATUS_ACCESS_DENIED = 105U,
	/** Indicates the TOS timeout. */
	TEE_CLIENT_STATUS_TOS_TIMEOUT  = 150U,
} TeeClientStatus;

/**
 * @defgroup required_interfaces
 *
 * Function pointer interface for transport-specific implementations.
 * These functions are populated by transport implementations (e.g., mailbox, IVC)
 * and called by the main nvtzvault.ko driver.
 *
 * @{
 */

/**
 * @brief Provide API version of the plugin interface
 *
 * This will be used by nvtzvault driver to perform version compatibility checks
 * and provide backward compatibility support.
 *
 * @param [out] major_version Major version
 * @param [out] minor_version Minor version
 *
 * @return none
 *
 * @usage
 * - Interface type: Required
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: Yes
 *   - Async/Sync: Sync
 * - Required privileges: Kernel context
 * - API group
 *   - Initialization: Yes
 *   - Run time: No
 *   - De-initialization: No
 * - Security considerations: N/A
 * - Safety considerations: N/A
 * - Performance considerations: N/A
 * - Additional considerations: N/A
 */
void teec_soc_get_interface_version(uint32_t *major_version, uint32_t *minor_version);

/**
 * @brief Main TEE client communication interface structure
 *
 * This structure provides a polymorphic interface for TEE communication operations.
 * It contains function pointers that are populated by specific transport implementations
 * at runtime, enabling the same client code to work with different underlying
 * transport mechanisms (e.g. mailbox, IVC, etc.).
 *
 * The interface supports the complete lifecycle of TEE communication:
 * - Initialization and configuration
 * - Message sending and receiving with offset-based streaming
 * - Memory management and validation
 * - Synchronization and locking
 * - Cleanup and deinitialization
 *
 * @note Transport-specific implementation details are documented with respective implementation.
 *
 * kernel_checkpatch: typedef required to conform to plugin interface
 */
typedef struct TeeClient {
	/**
	 * Private data pointer for transport-specific implementation
	 * This opaque pointer contains transport-specific context and configuration data
	 *
	 * @note Implementation-specific structure details are documented in respective
	 *       transport implementation (e.g., MbTeeComm in teec-soc-mailbox.c)
	 */
	void *tee_comms_priv;

	/**
	 * @brief Initialize transport communication hardware resources
	 *
	 * Initializes transport-specific hardware resources including memory mapping,
	 * interrupt handlers, and synchronization mechanisms. This function is called
	 * after interface setup to prepare the transport for communication operations.
	 *
	 * @param [in] tee_comms_priv Pointer to transport-specific communication structure
	 * @param [in] pconfig Configuration data specific to transport implementation
	 *
	 * @return TeeClientStatus status code
	 * @retval TEE_CLIENT_STATUS_OK Initialization successful
	 * @retval TEE_CLIENT_STATUS_BAD_PARAMETERS Invalid input parameters
	 * @retval TEE_CLIENT_STATUS_BAD_STATE Transport already initialized or invalid state
	 * @retval TEE_CLIENT_STATUS_GENERIC_ERROR Transport resource initialization failures
	 *
	 * @usage
	 * - Interface type: Required
	 * - Allowed context for the API call
	 *   - Interrupt handler: No
	 *   - Signal handler: No
	 *   - Thread-safe: No
	 *   - Re-entrant: No
	 *   - Async/Sync: Sync
	 * - Required privileges: Kernel context
	 * - API group
	 *   - Initialization: Yes
	 *   - Run time: No
	 *   - De-initialization: No
	 * - Security considerations: N/A
	 * - Safety considerations: N/A
	 * - Performance considerations: N/A
	 * - Additional considerations: N/A
	 */
	TeeClientStatus (*teec_comms_init)(void *tee_comms_priv, void *pconfig);

	/**
	 * @brief Reset transport communication channel to clean state
	 *
	 * Resets the transport communication channel to a clean operational state.
	 * This function is typically called during initialization and system resume
	 * scenarios to ensure proper transport operation after state changes.
	 * It is designed for channel reset scenarios.
	 *
	 * @param [in] tee_comms_priv Pointer to transport-specific communication structure
	 *
	 * @return TeeClientStatus status code
	 * @retval TEE_CLIENT_STATUS_OK Reset successful (no-op for some transports)
	 * @retval TEE_CLIENT_STATUS_BAD_PARAMETERS Invalid tee_comms_priv pointer or not initialized
	 *
	 * @usage
	 * - Interface type: Required
	 * - Allowed context for the API call
	 *   - Interrupt handler: No
	 *   - Signal handler: No
	 *   - Thread-safe: No
	 *   - Re-entrant: No
	 *   - Async/Sync: Sync
	 * - Required privileges: Kernel context
	 * - API group
	 *   - Initialization: Yes
	 *   - Run time: Yes
	 *   - De-initialization: No
	 * - Security considerations: N/A
	 * - Safety considerations: N/A
	 * - Performance considerations: N/A
	 * - Additional considerations: N/A
	 */
	TeeClientStatus (*teec_comms_reset)(void *tee_comms_priv);

	/**
	 * @brief Reset and clear transport-specific memory regions
	 *
	 * Clears and resets transport-specific memory regions and control structures.
	 * This function is typically called during deinitialization to ensure clean
	 * state and proper resource cleanup.
	 *
	 * @param [in] tee_comms_priv Pointer to transport-specific communication structure
	 *
	 * @return TeeClientStatus status code
	 * @retval TEE_CLIENT_STATUS_OK Memory reset successful
	 * @retval TEE_CLIENT_STATUS_BAD_STATE Invalid tee_comms_priv pointer or not initialized
	 *
	 * @usage
	 * - Interface type: Required
	 * - Allowed context for the API call
	 *   - Interrupt handler: No
	 *   - Signal handler: No
	 *   - Thread-safe: No
	 *   - Re-entrant: No
	 *   - Async/Sync: Sync
	 * - Required privileges: Kernel context
	 * - API group
	 *   - Initialization: No
	 *   - Run time: No
	 *   - De-initialization: Yes
	 * - Security considerations: N/A
	 * - Safety considerations: N/A
	 * - Performance considerations: N/A
	 * - Additional considerations: N/A
	 */
	TeeClientStatus (*teec_comms_reset_memory)(void *tee_comms_priv);

	/**
	 * @brief Transmit message data to TEE using transport-specific protocol
	 *
	 * Transmits request message data to the TEE using the configured transport mechanism.
	 * This function validates parameters, checks transport availability, and initiates
	 * the transport-specific transmission procedure.
	 *
	 * @param [in] tee_comms_priv Pointer to transport-specific communication structure
	 * @param [in] data Pointer to message data buffer to transmit
	 * @param [in] size Message data size in bytes
	 *
	 * @return TeeClientStatus status code
	 * @retval TEE_CLIENT_STATUS_OK Message sent successfully
	 * @retval TEE_CLIENT_STATUS_BAD_PARAMETERS Invalid data pointer or size
	 * @retval TEE_CLIENT_STATUS_BAD_STATE Transport not ready or busy
	 * @retval TEE_CLIENT_STATUS_GENERIC_ERROR Size overflow or transmission failure
	 *
	 * @usage
	 * - Interface type: Required
	 * - Allowed context for the API call
	 *   - Interrupt handler: No
	 *   - Signal handler: No
	 *   - Thread-safe: No
	 *   - Re-entrant: No
	 *   - Async/Sync: Sync
	 * - Required privileges: Kernel context
	 * - API group
	 *   - Initialization: No
	 *   - Run time: Yes
	 *   - De-initialization: No
	 * - Security considerations: N/A
	 * - Safety considerations: N/A
	 * - Performance considerations: N/A
	 * - Additional considerations: N/A
	 */
	TeeClientStatus (*teec_comms_send_msg)(void *tee_comms_priv, void *data, size_t size);

	/**
	 * @brief Wait for TEE response completion with configurable timeout
	 *
	 * Waits for transport-specific communication events indicating TEE response
	 * readiness. The implementation uses transport-appropriate synchronization
	 * mechanisms (e.g. interrupts, completion objects, polling, etc.).
	 *
	 * @param [in] tee_comms_priv Pointer to transport-specific communication structure
	 * @param [in] timeout_val Timeout value in milliseconds (0 = non-blocking check)
	 *
	 * @return TeeClientStatus status code
	 * @retval TEE_CLIENT_STATUS_OK Response ready for reading
	 * @retval TEE_CLIENT_STATUS_BAD_PARAMETERS Invalid timeout value (< 0)
	 * @retval TEE_CLIENT_STATUS_BAD_STATE Transport not initialized
	 * @retval TEE_CLIENT_STATUS_TOS_TIMEOUT Timeout expired waiting for response
	 *
	 * @usage
	 * - Interface type: Required
	 * - Allowed context for the API call
	 *   - Interrupt handler: No
	 *   - Signal handler: No
	 *   - Thread-safe: No
	 *   - Re-entrant: No
	 *   - Async/Sync: Sync
	 * - Required privileges: Kernel context
	 * - API group
	 *   - Initialization: No
	 *   - Run time: Yes
	 *   - De-initialization: No
	 * - Security considerations: N/A
	 * - Safety considerations: N/A
	 * - Performance considerations: N/A
	 * - Additional considerations: N/A
	 */
	TeeClientStatus (*teec_comms_wait_event)(void *tee_comms_priv, int64_t timeout_val);

	/**
	 * @brief Read TEE response message data with validation
	 *
	 * Reads response message data from the TEE using the configured transport mechanism.
	 * This function should be called after successful teec_comms_wait_event() to retrieve response
	 * data and perform transport-specific validation.
	 *
	 * @param [in] tee_comms_priv Pointer to transport-specific communication structure
	 * @param [out] data Pointer to buffer where response data will be stored
	 * @param [in] size Data size to read in bytes
	 *
	 * @return TeeClientStatus status code
	 * @retval TEE_CLIENT_STATUS_OK Response data read successfully
	 * @retval TEE_CLIENT_STATUS_BAD_PARAMETERS Invalid data pointer, size, or buffer size mismatch
	 * @retval TEE_CLIENT_STATUS_BAD_STATE Transport not initialized
	 * @retval TEE_CLIENT_STATUS_GENERIC_ERROR TEE operation errors or response validation failures
	 *
	 * @usage
	 * - Interface type: Required
	 * - Allowed context for the API call
	 *   - Interrupt handler: No
	 *   - Signal handler: No
	 *   - Thread-safe: No
	 *   - Re-entrant: No
	 *   - Async/Sync: Sync
	 * - Required privileges: Kernel context
	 * - API group
	 *   - Initialization: No
	 *   - Run time: Yes
	 *   - De-initialization: No
	 * - Security considerations: N/A
	 * - Safety considerations: N/A
	 * - Performance considerations: N/A
	 * - Additional considerations: N/A
	 */
	TeeClientStatus (*teec_comms_read_msg)(void *tee_comms_priv, void *data, size_t size);

	/**
	 * @brief Acquire transport synchronization for operation sequence
	 *
	 * Acquires transport-specific synchronization mechanism to ensure proper
	 * operation ordering. Must be paired with teec_comms_rel_lock() to release.
	 * Synchronization strategy is implementation dependent.
	 *
	 * @param [in] tee_comms_priv Pointer to transport-specific communication structure
	 *
	 * @return TeeClientStatus status code
	 * @retval TEE_CLIENT_STATUS_OK Lock acquired successfully
	 * @retval TEE_CLIENT_STATUS_BAD_PARAMETERS Invalid tee_comms_priv pointer
	 * @retval TEE_CLIENT_STATUS_BAD_STATE Transport not initialized
	 *
	 * @usage
	 * - Interface type: Required
	 * - Allowed context for the API call
	 *   - Interrupt handler: No
	 *   - Signal handler: No
	 *   - Thread-safe: No
	 *   - Re-entrant: No
	 *   - Async/Sync: Sync
	 * - Required privileges: Kernel context
	 * - API group
	 *   - Initialization: No
	 *   - Run time: Yes
	 *   - De-initialization: No
	 * - Security considerations: N/A
	 * - Safety considerations: N/A
	 * - Performance considerations: N/A
	 * - Additional considerations: N/A
	 */
	TeeClientStatus (*teec_comms_acq_lock)(void *tee_comms_priv);

	/**
	 * @brief Release transport synchronization mechanism
	 *
	 * Releases transport synchronization previously acquired by teec_comms_acq_lock().
	 * This allows subsequent operations to proceed according to transport-specific
	 * synchronization policy.
	 *
	 * @param [in] tee_comms_priv Pointer to transport-specific communication structure
	 *
	 * @return TeeClientStatus status code
	 * @retval TEE_CLIENT_STATUS_OK Lock released successfully
	 * @retval TEE_CLIENT_STATUS_BAD_PARAMETERS Invalid tee_comms_priv pointer
	 * @retval TEE_CLIENT_STATUS_BAD_STATE Transport not initialized
	 *
	 * @usage
	 * - Interface type: Required
	 * - Allowed context for the API call
	 *   - Interrupt handler: No
	 *   - Signal handler: No
	 *   - Thread-safe: No
	 *   - Re-entrant: No
	 *   - Async/Sync: Sync
	 * - Required privileges: Kernel context
	 * - API group
	 *   - Initialization: No
	 *   - Run time: Yes
	 *   - De-initialization: No
	 * - Security considerations: N/A
	 * - Safety considerations: N/A
	 * - Performance considerations: N/A
	 * - Additional considerations: N/A
	 */
	TeeClientStatus (*teec_comms_rel_lock)(void *tee_comms_priv);

	/**
	 * @brief Deinitialize and cleanup transport communication interface
	 *
	 * Performs complete cleanup of transport-specific resources including hardware
	 * resource unmapping, memory deallocation, and synchronization mechanism cleanup.
	 * After calling this function, the interface is fully torn down.
	 * To resume communication, teec_initialize_interface() must be called
	 * again before teec_comms_init() - not just teec_comms_init() alone.
	 *
	 * @param [in] tee_comms_priv Pointer to transport-specific communication structure
	 *
	 * @return TeeClientStatus status code
	 * @retval TEE_CLIENT_STATUS_OK Deinitialization successful
	 * @retval TEE_CLIENT_STATUS_BAD_PARAMETERS Invalid tee_comms_priv pointer
	 * @retval TEE_CLIENT_STATUS_BAD_STATE Transport in inconsistent state
	 *
	 * @usage
	 * - Interface type: Required
	 * - Allowed context for the API call
	 *   - Interrupt handler: No
	 *   - Signal handler: No
	 *   - Thread-safe: No
	 *   - Re-entrant: No
	 *   - Async/Sync: Sync
	 * - Required privileges: Kernel context
	 * - API group
	 *   - Initialization: No
	 *   - Run time: No
	 *   - De-initialization: Yes
	 * - Security considerations: N/A
	 * - Safety considerations: N/A
	 * - Performance considerations: N/A
	 * - Additional considerations: N/A
	 */
	TeeClientStatus (*teec_comms_deinit)(void *tee_comms_priv);
} TeeClient;

/**
 * @brief Initialize transport interface and populate TeeClient structure with transport functions
 *
 * Sets up TeeClient structure with all required function pointers for TEE communication operations.
 *
 * Initialization sequence:
 * 1. teec_initialize_interface() - populate function pointers
 * 2. teec_comms_init() - initialize hardware resources
 * 3. teec_comms_reset() - reset transport to clean state
 *
 * @param [in] tee_priv Pointer to TeeClient structure to populate with transport functions
 * @param [in] comms_os_params OS-specific configuration data required for transport initialization
 *
 * @return TeeClientStatus status code
 * @retval TEE_CLIENT_STATUS_OK Interface initialization successful
 * @retval TEE_CLIENT_STATUS_BAD_PARAMETERS Invalid input parameters or missing configuration
 * @retval TEE_CLIENT_STATUS_OUT_OF_MEMORY Memory allocation failure
 *
 * @usage
 * - Interface type: Required
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: No
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: Kernel context
 * - API group
 *   - Initialization: Yes
 *   - Run time: No
 *   - De-initialization: No
 * - Security considerations: N/A
 * - Safety considerations: N/A
 * - Performance considerations: N/A
 * - Additional considerations: N/A
 */
TeeClientStatus teec_initialize_interface(TeeClient *tee_priv, CommsParamsOS comms_os_params);

/** @} */

/** @} */

#endif /* TEEC_SOC_PLUGIN_H */
