#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA CORPORATION, its affiliates and licensors retain all
# intellectual property and proprietary rights in and to this
# material, related documentation and any modifications thereto.
# Any use, reproduction, disclosure or distribution of this material
# and related documentation without an express license agreement from
# NVIDIA CORPORATION or its affiliates is strictly prohibited.

# This script resets all USB host controllers to resolve flashing
# issues related to USB devices.

reset_usb_host_controllers() {
	if [[ "${EUID}" != "0" ]]; then
		echo "ERROR: This script must be run as root!" >&2
		exit 1
	fi

	local reset_failed=0
	local reset_success=0

	# Loop through all USB host controllers (xhci, ehci, ohci)
	for hci in /sys/bus/pci/drivers/?hci_hcd; do
		if [ ! -d "${hci}" ]; then
			echo "WARN: No USB host controllers found" >&2
			continue
		fi

		if ! cd "${hci}"; then
			echo "ERROR: Failed to change directory to ${hci}" >&2
			((reset_failed++))
			continue
		fi

		echo "INFO: Resetting devices from ${hci}..."

		local devices=(????:??:??.?)
		if [ "${devices[0]}" = "????:??:??.?" ]; then
			echo "WARN: No devices found in ${hci}" >&2
			continue
		fi

		for device in "${devices[@]}"; do
			echo "INFO: Unbinding ${device}..."
			if ! echo -n "${device}" >unbind; then
				echo "ERROR: Failed to unbind ${device}" >&2
				((reset_failed++))
				continue
			fi

			sleep 0.5 # Sanity sleep between unbind and bind

			echo "INFO: Binding ${device}..."
			if ! echo -n "${device}" >bind; then
				echo "ERROR: Failed to bind ${device}" >&2
				((reset_failed++))
				continue
			fi
			((reset_success++))
		done
	done

	if [ "${reset_success}" -gt 0 ]; then
		echo -n "INFO: Waiting 10 seconds for devices to reset..."
		sleep 10
		echo "done!"
	fi

	echo "INFO: Successfully reset ${reset_success} device(s)"
	if [ "${reset_failed}" -gt 0 ]; then
		echo "WARN: Failed to reset ${reset_failed} device(s)" >&2
		return 1
	fi

	return 0
}

echo "INFO: Executing USB host controller(s) reset script..."
reset_usb_host_controllers
