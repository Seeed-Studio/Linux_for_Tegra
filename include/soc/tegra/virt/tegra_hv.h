/*
 * SPDX-License-Identifier: GPL-2.0-only
 * SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef TEGRA_HV_H
#define TEGRA_HV_H

/**
 * @defgroup hypervisor_ivc_framework Hypervisor IVC Framework
 * @{
 */

#include <linux/types.h>
#include <soc/tegra/virt/syscalls.h>

/** @brief Supports trap notification to peer end via MSI irqs */
#define SUPPORTS_TRAP_MSI_NOTIFICATION

/** @brief Page size of IVC meta data queried from hypervisor */
#define IVC_INFO_PAGE_SIZE 65536

/** @brief Maximum Guest VM count */
#define MAX_NUM_GUESTS		16U
/** @brief The maximum number of IVC queues supported by the PCT. */
#define PCT_MAX_NUM_IVC_QUEUES	512U
/** @brief The maximum number of mempools supported by the PCT. */
#define PCT_MAX_NUM_MEMPOOLS	120U

/** @brief structure representing guest IVC area info */
struct tegra_hv_guest_area {
	/** @brief IO remapped shared memory address */
	uintptr_t shmem;
	/** @brief length of shared memory */
	size_t length;
};

/** @brief structure representing IVC notification info */
struct tegra_hv_notify_info {
	/** @brief trap region base virtual address */
	uintptr_t trap_region_base_va;
	/** @brief trap region base IPA */
	uint64_t trap_region_base_ipa;
	/** @brief trap region end IPA */
	uint64_t trap_region_end_ipa;
	/** @brief trap region size */
	uint64_t trap_region_size;
	/** @brief MSI region base virtual address */
	uintptr_t msi_region_base_va;
	/** @brief MSI region base IPA */
	uint64_t msi_region_base_ipa;
	/** @brief MSI region end IPA */
	uint64_t msi_region_end_ipa;
	/** @brief MSI region size */
	uint64_t msi_region_size;
};

/** @brief structure representing IVC layout */
struct tegra_hv_ivc_layout {
	/** @brief pointer to IVC info page */
	const struct ivc_info_page *info;
	/** @brief pointer to guest IVC area info array */
	struct tegra_hv_guest_area *guest_ivc_info;
	/** @brief notification info */
	struct tegra_hv_notify_info notify;
};

/** @brief structure representing mempool cookie in hypervisor driver */
struct tegra_hv_ivm_cookie {
	/** @brief mempool base ipa address */
	uint64_t ipa;
	/** @brief mempool size */
	uint64_t size;
	/** @brief vmid of the peer */
	unsigned int peer_vmid;
	/** @brief reserved */
	void *reserved;
};

/**
 * @brief          Checks whether platform supports virtualization or not
 *
 * @retval         true If platform supports virtualization else false.
 *
 * @pre
 *                 - Tegra hypervisor driver should have been initialized.
 *
 * @post
 *                 - Client can take runtime decision in driver if OS is running in native environment or in virtualized environment.
 *
 * @usage
 *                 - Call this function to check if OS is running in native environment or in virtualized environment.
 *                 - Allowed context for the API call
 *                   - Interrupt handler: Yes
 *                   - Signal handler: N/A
 *                   - Thread-safe: Yes
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
bool is_tegra_hypervisor_mode(void);

/**
 * @brief          Reserve a mempool for use
 * @param[in]      id Id of the requested mempool.
 *
 * @retval         ivck Returns a cookie representing the mempool on success, otherwise an ERR_PTR.
 *
 * @pre
 *                 - Tegra hypervisor driver should have been initialized.
 *                 - This API should be invoked on virtual/hypervisor environment only.
 *
 * @post
 *                 - Reserved mempool will be available for I/O operations.
 *
 * @usage
 *                 - Mempool will be available for data transfer with peer end.
 *                 - Allowed context for the API call
 *                   - Interrupt handler: Yes
 *                   - Signal handler: N/A
 *                   - Thread-safe: No
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
struct tegra_hv_ivm_cookie *tegra_hv_mempool_reserve(unsigned int id);

/**
 * @brief          Release a reserved mempool
 * @param[in]      ivck IVC cookie of the queue
 *
 * @retval         ret 0 On success or a negative error code otherwise.
 *
 * @pre
 *                 - Tegra hypervisor driver should have been initialized.
 *                 - This API should be invoked on virtual/hypervisor environment only.
 *
 * @post
 *                 - Unreserved mempool will not be available for I/O operations.
 *
 * @usage
 *                 - Mempool will not be available for data transfer available for data transfer.
 *                 - Allowed context for the API call
 *                   - Interrupt handler: Yes
 *                   - Signal handler: N/A
 *                   - Thread-safe: No
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
int tegra_hv_mempool_unreserve(struct tegra_hv_ivm_cookie *ivck);

/**
 * @brief          Query IVC meta data from hypervisor
 *
 * @retval         ptr To ivc_info_page structure having ivc meta data info
 *                 shared by hypervisor
 *
 * @pre
 *                 - Tegra hypervisor driver should have been initialized.
 *                 - This API should be invoked on virtual/hypervisor environment only.
 *
 * @post
 *                 - IVC meta data will be retuned to client
 *
 * @usage
 *                 - Allowed context for the API call
 *                   - Interrupt handler: Yes
 *                   - Signal handler: N/A
 *                   - Thread-safe: No
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
const struct ivc_info_page *tegra_hv_get_ivc_info(void);

/**
 * @brief          Query vmid of the GOS VM from hypervisor
 *
 * @retval         vmid On success else -ve value
 *
 * @pre
 *                 - Tegra hypervisor driver should have been initialized.
 *                 - This API should be invoked on virtual/hypervisor environment only.
 *
 * @post
 *                 - Guest VM ID will be returned.
 *
 * @usage
 *                 - Allowed context for the API call
 *                   - Interrupt handler: Yes
 *                   - Signal handler: N/A
 *                   - Thread-safe: No
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
int tegra_hv_get_vmid(void);

/**
 * @brief          Reserve an IVC queue by ID
 * @param[in]      id Queue ID to reserve
 *
 * @retval         true If the queue was successfully reserved
 * @retval         false If already reserved or if the queue doesn't exist
 *
 * @pre
 *                 - Tegra hypervisor driver should have been initialized.
 *                 - This API should be invoked on virtual/hypervisor environment only.
 *
 * @post
 *                 - Queue will be marked reserved with reserved_by_external flag set
 *
 * @usage
 *                 - Used by tegra_hv_nvscicom to prevent double reservation of queues.
 *                 - Allowed context for the API call
 *                   - Interrupt handler: No
 *                   - Signal handler: N/A
 *                   - Thread-safe: Yes
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
bool tegra_hv_ivc_reserve_id(uint32_t id);

/**
 * @brief          Unreserve an IVC queue by ID
 * @param[in]      id Queue ID to unreserve
 *
 * @pre
 *                 - Tegra hypervisor driver should have been initialized.
 *                 - This API should be invoked on virtual/hypervisor environment only.
 *                 - Queue should have been reserved via tegra_hv_ivc_reserve_id.
 *
 * @post
 *                 - Queue reservation will be cleared if reserved_by_external flag was set
 *
 * @usage
 *                 - Used by tegra_hv_nvscicom to release the reservation.
 *                 - Allowed context for the API call
 *                   - Interrupt handler: No
 *                   - Signal handler: N/A
 *                   - Thread-safe: Yes
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
void tegra_hv_ivc_unreserve_id(uint32_t id);

/**
 * @brief          Initialize IVC layout structure
 * @param[in,out]  layout Pointer to layout structure to initialize
 * @param[in]      read_ivc_info Function pointer to read IVC info page address
 *
 * @retval         0 On success, negative error code on failure
 *
 * @pre
 *                 - layout must be a valid pointer
 *                 - read_ivc_info must be a valid function pointer
 *
 * @post
 *                 - layout structure will be initialized with IVC info
 *
 * @usage
 *                 - Allowed context for the API call
 *                   - Interrupt handler: No
 *                   - Signal handler: N/A
 *                   - Thread-safe: No
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: Yes
 *                   - Runtime: No
 *                   - De-Init: No
 */
int tegra_hv_ivc_layout_init(struct tegra_hv_ivc_layout *layout,
			     int (*read_ivc_info)(uint64_t *ivc_info_page_pa));

/**
 * @brief          Release IVC layout resources
 * @param[in]      layout Pointer to layout structure to release
 *
 * @pre
 *                 - layout should have been initialized via tegra_hv_ivc_layout_init
 *
 * @post
 *                 - All resources in layout will be released
 *
 * @usage
 *                 - Allowed context for the API call
 *                   - Interrupt handler: No
 *                   - Signal handler: N/A
 *                   - Thread-safe: No
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: No
 *                   - De-Init: Yes
 */
void tegra_hv_ivc_layout_release(struct tegra_hv_ivc_layout *layout);

/**
 * @brief          Get maximum queue ID from IVC info
 * @param[in]      info Pointer to IVC info page
 *
 * @retval         Maximum queue ID found in IVC info page
 *
 * @pre
 *                 - info must be a valid pointer to IVC info page
 *
 * @usage
 *                 - Allowed context for the API call
 *                   - Interrupt handler: Yes
 *                   - Signal handler: N/A
 *                   - Thread-safe: Yes
 *                   - Async/Sync: Sync
 *                   - Re-entrant: Yes
 *                 - API Group
 *                   - Init: Yes
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
uint32_t tegra_hv_get_max_qid(const struct ivc_info_page *info);

/**
 * @brief          Add interrupts property to device node
 * @param[in]      dev Device node to add property to
 * @param[in]      info Pointer to IVC info page
 * @param[in]      prop Property structure to use (caller must keep alive)
 *
 * @retval         0 On success, negative error code on failure
 *
 * @pre
 *                 - dev, info, prop must be valid pointers
 *
 * @post
 *                 - Interrupts property will be added to device node
 *
 * @usage
 *                 - Allowed context for the API call
 *                   - Interrupt handler: No
 *                   - Signal handler: N/A
 *                   - Thread-safe: No
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: Yes
 *                   - Runtime: No
 *                   - De-Init: No
 */
int tegra_hv_add_ivc_interrupts(struct device_node *dev,
				const struct ivc_info_page *info,
				struct property *prop);

/** @} */

#endif /* TEGRA_HV_H */
