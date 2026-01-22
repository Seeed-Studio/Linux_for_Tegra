#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: MIT
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

# Generate a tarball for QSPI flash
#
set -e

QSPI_IMAGE_NAME="qspi.img"
RCMBOOT_BLOB_DIR="bootloader/rcmboot_blob"
# The initrd contains the qspi image
FLASH_QSPI_INITRD="flash_qspi_initrd"

function init_var() {
	# Flash Mode (Default: Linux)
	FLASH_MODE="${FLASH_MODE:-Linux}"
	if [ "${FLASH_MODE}" = "Linux" ]; then
		HOST_TYPE="host"
	elif [ "${FLASH_MODE}" = "BMC" ]; then
		HOST_TYPE="bmc"
	elif [ "${FLASH_MODE}" = "Windows" ]; then
		echo "Error: Windows flash mode is not supported currently." >&2
		exit 1
	else
		echo "Error: Unsupported flash mode ${FLASH_MODE}." >&2
		exit 1
	fi
	echo "Using FLASH_MODE: ${FLASH_MODE}, and HOST_TYPE: ${HOST_TYPE}"

	COMMON_CMD_ARGS=()
	if [ "${PKC_KEY_FILE}" != "" ] && [ -f "${PKC_KEY_FILE}" ]; then
		COMMON_CMD_ARGS+=("-u" "${PKC_KEY_FILE}")
	fi
	if [ "${SBK_KEY_FILE}" != "" ] && [ -f "${SBK_KEY_FILE}" ]; then
		COMMON_CMD_ARGS+=("-v" "${SBK_KEY_FILE}")
	fi

	QSPI_BOARDCFG="${BOARD_NAME}"
	echo "Using QSPI Board Config: ${QSPI_BOARDCFG}"

	SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
	L4T_DIR=$(readlink -f "${SCRIPT_DIR}/../..")
	BOOTLOADER_DIR="${L4T_DIR}/bootloader"
	TOOLS_DIR="${L4T_DIR}/tools"
	ROOTFS_DIR="${L4T_DIR}/rootfs"
	_FLASHING_UEFI=$(LDK_DIR=${L4T_DIR}; source "${LDK_DIR}/${QSPI_BOARDCFG}.conf" > /dev/null;echo "${_FLASHING_UEFI}")

	# The dir where the files are archived
	ARCHIVE_DIR="${HOST_TYPE}flash"
	echo "Using Archive dir: ${ARCHIVE_DIR}"
}

function generate_qspi_image() {
	echo "Generating QSPI image..."

	local cmd_args=("${COMMON_CMD_ARGS[@]}")
	cmd_args+=("-p" "${FLASH_OPTIONS}")

	echo ./tools/l4t_generate_qspi_images.sh "${cmd_args[@]}" "${QSPI_BOARDCFG}"
	./tools/l4t_generate_qspi_images.sh "${cmd_args[@]}" "${QSPI_BOARDCFG}"

	mv "./bootloader/${QSPI_BOARDCFG}.qspi.img" "./bootloader/${QSPI_IMAGE_NAME}"
}

function prepare_initrd() {
	echo "Preparing initrd..."
	TEMP_INITRD_DIR=$(mktemp -d)
	pushd "${TEMP_INITRD_DIR}" > /dev/null || exit 1

	# Extract the clean initrd from ./Linux_for_Tegra/bootloader/l4t_initrd.img
	#
	gunzip -c "${BOOTLOADER_DIR}/l4t_initrd.img" | cpio -i

	# Copy all the binary
	#
	cp "${BOOTLOADER_DIR}/${QSPI_IMAGE_NAME}" .
	cp "${TOOLS_DIR}/ota_tools/version_upgrade/init" .
	cp "${TOOLS_DIR}/ota_tools/version_upgrade/nv_ota_"* bin/
	cp "${TOOLS_DIR}/qspi_flash/kernel_flash_qspi.sh" bin/nv_recovery.sh

	# Following commands are used by kernel_flash_qspi.sh
	#
	cp "${ROOTFS_DIR}/usr/sbin/mtd_debug" bin/
	cp "${ROOTFS_DIR}/usr/sbin/flash_erase" bin/
	cp "${ROOTFS_DIR}/usr/bin/cut" bin/
	cp "${ROOTFS_DIR}/usr/bin/sync" bin/

	pushd "${ROOTFS_DIR}" > /dev/null || exit 1
	local kernel_modules_dir
	# Get the kernel modules dir. For example: usr/lib/modules/5.15.148-tegra/
	kernel_modules_dir=$(find usr/lib/modules -type d -name "*-tegra")
	popd > /dev/null || exit 1

	mkdir -p "${kernel_modules_dir}/kernel/drivers/spi"
	if [ -d "${ROOTFS_DIR}/${kernel_modules_dir}/updates/drivers/spi/" ]; then
		cp "${ROOTFS_DIR}/${kernel_modules_dir}/updates/drivers/spi/"* "${kernel_modules_dir}/kernel/drivers/spi"
	else
		echo "Error: No spi driver for tegra found!" >&2
		exit 1
	fi

	# Package the initrd
	#
	find . | cpio -H newc -o | gzip -9 -n >"${L4T_DIR}/${FLASH_QSPI_INITRD}"

	popd > /dev/null || exit 1
}

function generate_host_flash_images() {
	# Generate images for flash QSPI
	# The built artficates are in $RCMBOOT_BLOB_DIR
	#
	local env_args="BOOTDEV=initrd RAMCODE_ID=${RAMCODE_ID} NO_RECOVERY_IMG=1 NO_ESP_IMG=1 ROOTFS_AB= ROOTFS_ENC= "
	local cmd_rcm_args=("${COMMON_CMD_ARGS[@]}")
	cmd_rcm_args+=("-I" "./${FLASH_QSPI_INITRD}")
	cmd_rcm_args+=("${FLASH_OPTIONS}")
	cmd_rcm_args+=("--no-root-check" "--no-flash" "--no-systemimg" "--rcm-boot" "--read-ramcode")
	cmd_rcm_args+=("-C" "console=ttyTCU0,115200")
	if [ -n "${_FLASHING_UEFI}" ]; then
		cmd_rcm_args+=("-F" "${_FLASHING_UEFI}")
	fi
	cmd="${env_args} ./flash.sh ${cmd_rcm_args[*]} ${QSPI_BOARDCFG} initrd"
	echo -e "${cmd}\r\n"
	if ! eval "${cmd}"; then
		echo "FAILURE: ${cmd}" >&2
		exit 1
	fi
}

function get_build_version() {
	# Get ${BSP_VERSION}
	local bsp_version_file="${L4T_DIR}/nv_tegra/bsp_version"

	if [ -f "${bsp_version_file}" ]; then
		source "${bsp_version_file}"
	else
		echo "Error: Unknown Release." >&2
		exit 1
	fi;

	echo "R${BSP_VERSION}"
}

function get_tarball_name() {
	local build_ver="${1}"
	local TARBALL_NAME="${HOST_TYPE}_tegraflash"
	if [ -n "${build_ver}" ]; then
		TARBALL_NAME="${TARBALL_NAME}_${build_ver}"
	fi

	echo "${TARBALL_NAME}"
}

function create_host_flash_tarball() {
	echo "Creating ${HOST_TYPE} flash tarball..."

	if [ "${FLASH_MODE}" = "BMC" ]; then
		# Get an arm32 bit tegrarcm_v2 from a pre-build package
		if [ -f "../arm_tegraflash.tbz2" ]; then
			ARM32_TOOL="arm_tegraflash.tbz2"
		else
			for f in ../arm_tegraflash_*.tbz2; do
				[ -e "$f" ] && ARM32_TOOL="${f}" || echo "ARM32_TOOL does not exist" || exit 1
				break
			done
		fi
		tar -xvf "${ARM32_TOOL}" -C "${RCMBOOT_BLOB_DIR}" Linux_for_Tegra/bootloader/tegrarcm_v2
		cp "${RCMBOOT_BLOB_DIR}"/Linux_for_Tegra/bootloader/tegrarcm_v2 "${RCMBOOT_BLOB_DIR}"
		rm -rf "${RCMBOOT_BLOB_DIR}"/Linux_for_Tegra
		# Get the bmc flash script
		cp tools/bmc_flash/bmcflashqspi.sh "${RCMBOOT_BLOB_DIR}"
	elif [ "${FLASH_MODE}" = "Linux" ]; then
		# Get x86-64 linux build tegrarcm_v2
		cp bootloader/tegrarcm_v2 "${RCMBOOT_BLOB_DIR}"
		# Get the host flash script
		cp tools/qspi_flash/host_flash_qspi.sh "${RCMBOOT_BLOB_DIR}"
	fi

	mkdir -p "${ARCHIVE_DIR}"
	cp "${RCMBOOT_BLOB_DIR}/"* "${ARCHIVE_DIR}"

	# Save version info
	BUILD_VER="$(get_build_version)"
	echo "Build version = ${BUILD_VER}"
	if [ -n "${BUILD_VER}" ]; then
		echo -n "${BUILD_VER}-" > "${ARCHIVE_DIR}/version.txt"
	fi
	echo -n "$(date +%Y.%m-%d)" >> "${ARCHIVE_DIR}/version.txt"

	# Build final host flashing tarball
	TARBALL_NAME="$(get_tarball_name "${BUILD_VER}")"
	tar -cvf "${TARBALL_NAME}.tbz2" "./${ARCHIVE_DIR}"

	echo "Generated ${TARBALL_NAME} tarball successfully."
}


function generate_hostflash_for_qspi() {

	# Build Tegra QSPI image - 64MB in size
	#
	#	Create a tarball for flash QSPI image. The artifact located at Linux_for_Tegra/tegraflash_<VERSION>.tbz2
	#	Note:
	#		<VERSION> is matching release version. For example, R36.4.0
	#

	generate_qspi_image
	prepare_initrd
	generate_host_flash_images
	create_host_flash_tarball
}

function cleanup() {
	echo "Cleaning up..."
	rm -rf "${TEMP_INITRD_DIR}" "${ARCHIVE_DIR}" "${RCMBOOT_BLOB_DIR}" "${FLASH_QSPI_INITRD}"
}


function parse_args() {
	if ! OPTIONS=$(getopt -o u:v:p:f:h --long flash-mode:,help -- "$@"); then
		echo "Error: failed to parse options." >&2
		show_usage
		exit 1
	fi

	eval set -- "$OPTIONS"

	while true; do
		case "$1" in
			-u) PKC_KEY_FILE="$2"; shift 2 ;;
			-v) SBK_KEY_FILE="$2"; shift 2 ;;
			-p) FLASH_OPTIONS="$2"; shift 2 ;;
			-f|--flash-mode) FLASH_MODE="$2"; shift 2 ;;
			-h|--help) show_usage; exit 0 ;;
			--) shift; break ;;
			*) echo "Error: Invalid option: $1" >&2; show_usage; exit 1 ;;
		esac
	done

	# Check for remaining arguments
	if [[ $# -ne 1 ]]; then
		echo "Error: Invalid number of arguments." >&2
		show_usage
		exit 1
	fi

	# Set the $BOARD_NAME argument
	BOARD_NAME="$1"
}

function show_usage() {
	echo "Usage: [env={value},...] $0 [-u <PKC key file>] [-v <SBK key file>] [-p <options>] [-f <flash-mode>] <board-name>"
cat <<EOF
	Where,
		<board-name>	Indicate to generate QSPI flash for this board.

	Options:
	-u <PKC key file>
		PKC key used for odm fused board
	-v <SBK key file>
		Secure Boot Key (SBK) key used for ODM fused board
	-p <options>
		Options directly passed to flash.sh
	--flash-mode|-f <mode>
		Specify the flash host (options: Linux, BMC, Windows; default: Linux)
	--help|-h
		Show this help message

	Example:
		1. Generate QSPI image for the connected IGX Orin Devkit
			${SCRIPT_NAME} --flash-mode BMC igx-orin-devkit
		2. Generate QSPI image for the connected Jetson AGX Thor Devkit
			${SCRIPT_NAME} jetson-agx-thor-devkit
		3. Generate QSPI image for the disconnected IGX Orin Devkit
			BOARDID=3701 FAB=000 BOARDSKU=0008 CHIP_SKU=00:00:00:90 ${SCRIPT_NAME} --flash-mode BMC igx-orin-devkit
		4. Generate QSPI image for the disconnected Jetson AGX Thor Devkit
			BOARDID=3834 FAB=000 BOARDSKU=0008 CHIP_SKU=00:00:00:A0 ${SCRIPT_NAME} jetson-agx-thor-devkit
		5. Generate QSPI image for the connected Orin Nano/NX
			${SCRIPT_NAME} jetson-orin-nano-devkit
		6. Generate QSPI image for the disconnected Orin Nano SD card (SKU5)
			BOARDID=3767 FAB=300 BOARDSKU=0005 CHIP_SKU=00:00:00:D5 ${SCRIPT_NAME} jetson-orin-nano-devkit
		7. Generate QSPI image for the disconnected Orin NX NVMe (SKU1)
			BOARDID=3767 FAB=300 BOARDSKU=0001 CHIP_SKU=00:00:00:D4 ${SCRIPT_NAME} jetson-orin-nano-devkit
		8. Generate QSPI image signed by \"rsa_key.pem\" and encrypted by \"sbk.key\" for the disconnected Orin Nano SD card
			BOARDID=3767 FAB=300 BOARDSKU=0005 CHIP_SKU=00:00:00:D5 ${SCRIPT_NAME} -u rsa_key.pem -v sbk.key jetson-orin-nano-devkit
EOF
}

if [ "$(id -u)" -ne 0 ]; then
	echo "Error: This script requires root privilege." >&2
	exit 1
fi

SCRIPT_NAME=$(basename "$(readlink -f "$0")")

parse_args "$@"

init_var

trap cleanup EXIT
echo "Starting QSPI flash generation..."
# Change to the Linux_for_tegra directory (two levels up from the script)
pushd "${L4T_DIR}" > /dev/null || exit 1
generate_hostflash_for_qspi
popd > /dev/null || exit 1

echo "Ending QSPI flash generation."
exit 0
