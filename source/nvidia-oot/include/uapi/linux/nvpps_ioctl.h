/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

/**
 * @page page_nvpps_interface NVPPS Driver External Interface Documentation
 * @section timesync_overview Overview
 *
 * The NVIDIA Pulse Per Second (NVPPS) driver provides high-precision
 * timestamping capabilities for Tegra platforms. This driver supports
 * GPIO-based and TIMER-based event generation and capture with nanosecond
 * precision timing correlation between PTP (Precision Time Protocol) and
 * TSC (Time Stamp Counter) time domains.
 *
 * @defgroup timesync_access_control Access Control
 *
 * Description of access requirements:
 * - Device file: /dev/nvpps[N] where N is the device instance number
 * - Required permissions: Read/write access to the device file
 * - Privilege level: Standard user space application privileges
 * - Thread safety: All IOCTLs are thread-safe when used on separate file descriptors
 *
 */

/**
 * @defgroup timesync_user_api_group NvPPS user APIs/IOCTL
 * @{
 */

/**
 * @brief Get NVPPS driver and API version information
 *
 * @param[out] version Pointer to struct nvpps_version where version information
 *   will be stored. The memory for this structure shall be allocated by the caller.
 *
 * @return
 * - 0 for success
 * - -EFAULT if user pointer is invalid
 *
 * Behaviour of IOCTL and user expectations:
 * - This IOCTL returns both driver version and API version information
 * - Driver version indicates the implementation version
 * - API version indicates the interface compatibility level
 * - Version information is read-only and constant for a given driver build
 *
 * @pre Device file must be opened successfully
 *
 * @post None
 *
 * @usage
 * - Allowed context for the IOCTL call
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: Yes
 *   - Async/Sync: Sync
 * - Required Privileges:
 *   - Caller must be part of group nvpps
 * - API Group
 *   - Init: Yes
 *   - Runtime: Yes
 *   - De-Init: Yes
 *
 */

#define NVPPS_GETVERSION	_IOR('p', 0x1, struct nvpps_version *)

/**
 * @brief Get current NVPPS driver configuration parameters
 *
 * @param[out] params Pointer to struct nvpps_params where current configuration
 *   will be stored. The memory for this structure shall be allocated by the caller.
 *
 * @return
 * - 0 for success
 * - -EFAULT if user pointer is invalid
 *
 * Behaviour of IOCTL and user expectations:
 * - Returns the current event mode (GPIO or Timer based)
 * - Returns the current TSC mode (nanoseconds or counter values)
 *
 * @pre Device file must be opened successfully
 *
 * @post None
 *
 * @usage
 * - Allowed context for the IOCTL call
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: Yes
 *   - Async/Sync: Sync
 * - Required Privileges:
 *   - Caller must be part of group nvpps
 * - API Group
 *   - Init: Yes
 *   - Runtime: Yes
 *   - De-Init: Yes
 *
 */

#define NVPPS_GETPARAMS		_IOR('p', 0x2, struct nvpps_params *)

/**
 * @brief Set NVPPS driver configuration parameters
 *
 * @param[in] params Pointer to struct nvpps_params containing desired configuration to be set.
 *   The memory for this structure shall be allocated and initialized by the caller.
 *
 * @return
 * - 0 for success
 * - -EFAULT if user pointer is invalid
 * - -EINVAL for invalid parameter values
 * - -ERANGE in Timer mode case, if there is overflow when calculating next timeout
 * - -ve value for anyother error from kernel calls eg: devm_request_irq
 *
 * Behaviour of IOCTL and user expectations:
 * - Sets the event mode: NVPPS_MODE_GPIO (0x01) or NVPPS_MODE_TIMER (0x02)
 * - Sets the TSC mode: NVPPS_TSC_NSEC (0) or NVPPS_TSC_COUNTER (1)
 * - Invalid mode requests will be rejected
 * - GPIO mode requires valid GPIO pin configuration in device tree
 *
 * @pre
 * - Device file must be opened successfully
 * - No active PPS event capture should be in progress
 *
 * @post
 * - It is recommended to ensure that at least one full event generation cycle has completed
 *   before invoking NVPPS_SETPARAMS again, in order to prevent loss of timestamp capture events
 *
 * @usage
 * - Allowed context for the IOCTL call
 *   - Signal handler: No
 *   - Thread-safe: No
 *   - Re-entrant: No (configuration changes are serialized)
 *   - Async/Sync: Sync
 * - Required Privileges:
 *   - Caller must be part of group nvpps
 * - API Group
 *   - Init: Yes
 *   - Runtime: Yes
 *   - De-Init: Yes
 *
 */

#define NVPPS_SETPARAMS		_IOW('p', 0x3, struct nvpps_params *)

/**
 * @brief Get precision timestamped PPS event data
 *
 * @param[out] event Pointer to struct nvpps_timeevent where event data
 *   will be stored. The memory for this structure shall be allocated by the caller.
 *
 * @return
 * - 0 for success
 * - -EFAULT if user pointer is invalid
 *
 * Behaviour of IOCTL and user expectations:
 * - Returns high-precision timestamps correlated between PTP and TSC domains
 * - Event number (evt_nb) increments for each captured event
 * - TSC timestamp value interpretation depends on tsc_mode setting
 * - PTP timestamps are in nanoseconds since epoch
 * - IRQ latency measurement included for timing analysis
 * - Secondary PTP timestamp available on supported platforms
 * - If failure to read concurrent time from PTP interface will result in
 *   corresponding value being set as 0
 *
 * @pre
 * - Device file must be opened successfully
 * - Valid configuration must be set via NVPPS_SETPARAMS
 * - PPS signal source must be connected (for GPIO mode)
 * - PTP client & server daemon should be running on primary & secondary network interfaces
 *   This is needed because only after starting PTP client & server dameons on primary &
 *   secondary interfaces respectively, the tegra network drivers register its interface to read
 *   concurrent PTP-TSC timestamps of the PTP network interface. If this IOCTL is excersized before
 *   starting the PTP dameons, then the timestamp fields will be set to 0 values and an error msg to
 *   indicate PTP is not running on Primary and/or secondary network interface is shown in the
 *   kernel dmesg log.
 *
 * @post None
 *
 * @usage
 * - Allowed context for the IOCTL call
 *   - Signal handler: No
 *   - Thread-safe: Yes (each file descriptor maintains separate event queue)
 *   - Re-entrant: Yes
 *   - Async/Sync: Sync (blocking) or Async (non-blocking with poll/select)
 * - Required Privileges:
 *   - Caller must be part of group nvpps
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 *
 */

#define NVPPS_GETEVENT		_IOR('p', 0x4, struct nvpps_timeevent *)

/**
 * @brief Get kernel and hardware PTP timestamps
 *
 * @param[in,out] timestamp Pointer to struct nvpps_timestamp_struct.
 *   Input: clockid field must be set to desired clock source Identifier
 *   Output: kernel_ts and hw_ptp_ts fields populated with timestamps
 *   The memory for this structure shall be allocated by the caller.
 *
 * @return
 * - 0 for success
 * - -EFAULT if user pointer is invalid
 * - -EINVAL if clockid is invalid or unsupported
 * - -ERANGE if timestamp values exceed representable range
 * - -ve value if anyother error from Kernel calls
 *
 * Behaviour of IOCTL and user expectations:
 * - Provides timestamps of kernel clock and hardware PTP clock
 * - Supports multiple clock sources via clockid parameter
 * - Hardware PTP timestamp represents the actual PTP clock time in Primary PTP interface
 * - Kernel timestamp represents corresponding system MONOTONIC or
 *   REALTIME time based on clockid passed
 * - Extra fields reserved for future extensions
 *
 * @pre
 * - Device file must be opened successfully
 * - PTP hardware must be initialized and available
 * - Valid values for clockid (CLOCK_MONOTONIC or CLOCK_REALTIME) must be specified
 * - PTP client daemon should be running on primary interfaces.
 *   This is needed because only after starting PTP client daemon on the primary PTP interface
 *   the tegra network driver registers its interface to read PTP time from primary PTP interface.
 *   If this IOCTL is excersized before starting the PTP client daemon, then the value returned in PTP
 *   timestamp field will be set to 0 and an error msg is shown in the kernel dmesg log.
 *
 * @post None
 *
 * @usage
 * - Allowed context for the IOCTL call
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required Privileges:
 *   - Caller must be part of group nvpps
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 *
 */

#define NVPPS_GETTIMESTAMP	_IOWR('p', 0x5, struct nvpps_timestamp_struct *)

/** @} */

/**
 * @defgroup timesync_data_structures_group Data Structures
 * @{
 */

#ifndef __UAPI_NVPPS_IOCTL_H__
#define __UAPI_NVPPS_IOCTL_H__

#include <linux/types.h>
#include <linux/ioctl.h>

/**
 * @brief structure to hold Driver version info
 *
 */
struct _version {
	__u32	major;		/**< Driver major version */
	__u32	minor;		/**< Driver minor version */
};

/**
 * @brief structure to hold API version info
 *
 */
struct _api {
	__u32	major;		/**< API major version */
	__u32	minor;		/**< API minor version */
};

/**
 * @brief Driver & API Version information structure
 *
 * Contains both driver version and API version information.
 * Used with NVPPS_GETVERSION IOCTL.
 */
struct nvpps_version {
	struct _version version;	/**< Driver version info struct */
	struct _api api;			/**< API version info struct */
};

#define NVPPS_VERSION_MAJOR	0
#define NVPPS_VERSION_MINOR	2
#define NVPPS_API_MAJOR		0
#define NVPPS_API_MINOR         4

/**
 * @brief Configuration parameters structure
 *
 * Contains event mode and TSC mode configuration.
 * Used with NVPPS_GETPARAMS and NVPPS_SETPARAMS IOCTLs.
 */
struct nvpps_params {
	__u32	evt_mode;	/**< Event mode: GPIO or Timer */
	__u32	tsc_mode;	/**< TSC mode: Nanoseconds or Counter */
};

/* Event mode definitions */
#define NVPPS_MODE_GPIO		0x01	/**< GPIO-based PPS event capture */
#define NVPPS_MODE_TIMER	0x02	/**< Timer-based PPS event generation */

/* TSC mode definitions */
#define NVPPS_TSC_NSEC		0	/**< TSC reported in nanoseconds */
#define NVPPS_TSC_COUNTER	1	/**< TSC reported as raw counter value */

/**
 * @brief PPS time event structure
 *
 * Contains concurrent PTP-TSC timestamps from Primary, Secondary & TSC domains
 * along with the other related info about the captured event
 * Used with NVPPS_GETEVENT IOCTL.
 */
struct nvpps_timeevent {
	__u32	evt_nb;		/**< Event sequence number. Range: 0 - UINT32_MAX */
	__u64	tsc;		/**< TSC timestamp (ns or counter based on mode) */
	__u64	ptp;		/**< Primary PTP timestamp in nanoseconds */
	__u64	secondary_ptp;	/**< Secondary PTP timestamp in nanoseconds */
	__u64	tsc_res_ns;	/**< TSC resolution in nanoseconds */
	__u32	evt_mode;	/**< Event mode used for this capture */
	__u32	tsc_mode;	/**< TSC mode used for this capture */
	__u64	irq_latency;	/**< Interrupt latency in nanoseconds */
};

#ifndef _LINUX_TIME64_H
typedef __s64 time64_t;
typedef __u64 timeu64_t;

/**
 * @brief structure to hold precise timestamp data
 *
 */
struct timespec64 {
	time64_t	tv_sec;		/**< seconds */
	long		tv_nsec;	/**< nanoseconds */
};
#endif

/**
 * @brief Structure to hold primary PTP & kernel timestamps
 *
 * Contains kernel and hardware PTP timestamps.
 * Used with NVPPS_GETTIMESTAMP IOCTL.
 */
struct nvpps_timestamp_struct {
	clockid_t	clockid;	/**< Input: Kernel clock source identifier. Valid values include CLOCK_MONOTONIC or CLOCK_REALTIME (defined by linux kernel) */
	struct timespec64 kernel_ts;	/**< Output: Kernel timestamp */
	struct timespec64 hw_ptp_ts;	/**< Output: Hardware PTP timestamp */
	__u64		extra[2];	/**< Reserved for future use */
};


#endif /* __UAPI_NVPPS_IOCTL_H__ */
/** @} */
