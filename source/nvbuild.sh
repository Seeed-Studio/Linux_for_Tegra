#!/bin/bash

set -e

SCRIPT_DIR="$(dirname $(readlink -f "${0}"))"
SCRIPT_NAME="$(basename "${0}")"
DO_INSTALL=0
ROOTFS_PATH=""

source "${SCRIPT_DIR}/nvcommon_build.sh"

function usage {
        cat <<EOM
Usage: ./${SCRIPT_NAME} [OPTIONS]
This script builds kernel sources in this directory.
OPTIONS:
        -h              Displays this help
        -o <outdir>     Creates kernel build output in <outdir>
        -i              Install kernel modules to rootfs (must combine with -r)
        -r <rootfs>     Root filesystem path to install modules to (used with -i)
EOM
}

function parse_input_param {
	while [ $# -gt 0 ]; do
		case ${1} in
			-h)
				usage
				exit 0
				;;
			-o)
				KERNEL_OUT_DIR="${2}"
				shift 2
				;;
			-i)
				DO_INSTALL=1
				shift 1
				;;
			-r)
				ROOTFS_PATH="${2}"
				shift 2
				;;
			*)
				echo "Error: Invalid option ${1}"
				usage
				exit 1
				;;
			esac
	done
}

function build_arm64_kernel_sources {
	kernel_version="${1}"
	echo "Building kernel-${kernel_version} sources"

	source_dir="${SCRIPT_DIR}/kernel/kernel-${kernel_version}/"
	config_file="tegra_defconfig"
	tegra_kernel_out="${source_dir}"

	if [ ! -z "${KERNEL_OUT_DIR}" ] ; then
		O_OPT=(O="${KERNEL_OUT_DIR}")
		tegra_kernel_out="${KERNEL_OUT_DIR}"
	else
		O_OPT=()
	fi

	"${MAKE_BIN}" -C "${source_dir}" ARCH=arm64 \
		LOCALVERSION="-tegra" \
		CROSS_COMPILE="${CROSS_COMPILE_AARCH64}" \
		"${O_OPT[@]}" "${config_file}"

	"${MAKE_BIN}" -C "${source_dir}" ARCH=arm64 \
		LOCALVERSION="-tegra" \
		CROSS_COMPILE="${CROSS_COMPILE_AARCH64}" \
		"${O_OPT[@]}" -j"${NPROC}" \
		--output-sync=target Image

	"${MAKE_BIN}" -C "${source_dir}" ARCH=arm64 \
		LOCALVERSION="-tegra" \
		CROSS_COMPILE="${CROSS_COMPILE_AARCH64}" \
		"${O_OPT[@]}" -j"${NPROC}" \
		--output-sync=target dtbs

	"${MAKE_BIN}" -C "${source_dir}" ARCH=arm64 \
		LOCALVERSION="-tegra" \
		CROSS_COMPILE="${CROSS_COMPILE_AARCH64}" \
		"${O_OPT[@]}" -j"${NPROC}" \
		--output-sync=target modules

	image="${tegra_kernel_out}/arch/arm64/boot/Image"
	if [ ! -f "${image}" ]; then
		echo "Error: Missing kernel image ${image}"
		exit 1
	fi

	# Optional: install modules to rootfs
	if [ "${DO_INSTALL}" -eq 1 ]; then
		if [ -z "${ROOTFS_PATH}" ]; then
			echo "Error: -i was specified but no -r <rootfs> given."
			exit 1
		fi
		echo "Installing kernel modules to ${ROOTFS_PATH}..."
		"${MAKE_BIN}" -C "${source_dir}" ARCH=arm64 \
			LOCALVERSION="-tegra" \
			CROSS_COMPILE="${CROSS_COMPILE_AARCH64}" \
			"${O_OPT[@]}" \
			INSTALL_MOD_PATH="${ROOTFS_PATH}" \
			modules_install
	fi

	echo "Kernel sources compiled successfully."
}

parse_input_param $@
build_arm64_kernel_sources "5.10"
