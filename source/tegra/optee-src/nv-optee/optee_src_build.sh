#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2021-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# This script builds optee sources in the current working directory.
# Usage: ./${SCRIPT_NAME}.sh [OPTIONS]
set -e

# shellcheck disable=SC2046
SCRIPT_DIR="$(dirname $(readlink -f "${0}"))"
SCRIPT_NAME="$(basename "${0}")"

source "${SCRIPT_DIR}/nvcommon_build.sh"

function usage {
        cat <<EOM
Usage: ./${SCRIPT_NAME} [OPTIONS]
This script builds optee sources in this directory.
It supports following options.
OPTIONS:
        -p                  Target platform. Possible values: t194|t234
        -t                  Enable fTPM build
        -h                  Displays this help
EOM
}

function check_pre_req {
	check_vars "CROSS_COMPILE_AARCH64_PATH"
	CROSS_COMPILE_AARCH64="${CROSS_COMPILE_AARCH64:-${CROSS_COMPILE_AARCH64_PATH}/bin/aarch64-buildroot-linux-gnu-}"

	if [ ! -f "${CROSS_COMPILE_AARCH64}gcc" ]; then
		echo " path ${CROSS_COMPILE_AARCH64}gcc does not exist"
		exit 1
	fi

	if [ ! -f "${UEFI_STMM_PATH}" ]; then
		echo "UEFI_STMM_PATH is not defined or the path is invalid"
		exit 1
	fi
}

function build_optee_sources {
	echo "Building ${TARGET_PLATFORM} optee sources..."

	# execute building steps
	local source_dir="${SCRIPT_DIR}/optee/optee_os"
	local build_dir="${SCRIPT_DIR}/optee/build/${TARGET_PLATFORM}"
	local install_dir="${SCRIPT_DIR}/optee/install/${TARGET_PLATFORM}"
	local ccc_prebuilt;
	local ftpm_config;
	local optee_config;
	local early_tas;
	local bins=()

	rm -rvf "${build_dir}" "${install_dir}"
	mkdir -vp "${build_dir}" "${install_dir}"

	if [ "${TARGET_PLATFORM}" == "t194" ]; then
		ccc_prebuilt="";
		early_tas="${build_dir}/early_ta/cpubl-payload-dec/0e35e2c9-b329-4ad9-a2f5-8ca9bbbd7713.stripped.elf \
			${build_dir}/early_ta/luks-srv/b83d14a8-7128-49df-9624-35f14f65ca6c.stripped.elf"
	fi
	if [ "${TARGET_PLATFORM}" == "t234" ]; then
		ccc_prebuilt="${source_dir}/prebuilt/t234/libcommon_crypto.a";
		early_tas="${build_dir}/early_ta/cpubl-payload-dec/0e35e2c9-b329-4ad9-a2f5-8ca9bbbd7713.stripped.elf \
			${build_dir}/early_ta/luks-srv/b83d14a8-7128-49df-9624-35f14f65ca6c.stripped.elf"

		if [ "${ENABLE_FTPM_BUILD}" == "yes" ]; then
			optee_config="CFG_CORE_TPM_EVENT_LOG=y \
				CFG_REE_STATE=y \
				CFG_JETSON_FTPM_HELPER_PTA=y"
			early_tas="${build_dir}/early_ta/cpubl-payload-dec/0e35e2c9-b329-4ad9-a2f5-8ca9bbbd7713.stripped.elf \
				${build_dir}/early_ta/ftpm-helper/a6a3a74a-77cb-433a-990c-1dfb8a3fbc4c.stripped.elf \
				${build_dir}/early_ta/luks-srv/b83d14a8-7128-49df-9624-35f14f65ca6c.stripped.elf \
				${build_dir}/early_ta/ms-tpm/bc50d971-d4c9-42c4-82cb-343fb7f37896.stripped.elf"
		fi
	fi

	# ${optee_config} could be blank here, so we can't quote it,
	# otherwise the shell will pass a blank argument to ${MAKE_BIN}.
	# shellcheck disable=SC2086
	"${MAKE_BIN}" -C "${source_dir}" \
		PLATFORM="tegra" \
		PLATFORM_FLAVOR="${TARGET_PLATFORM}" \
		CROSS_COMPILE64="${CROSS_COMPILE_AARCH64}" \
		PYTHON3="${PYTHON3_PATH}" \
		NV_CCC_PREBUILT="${ccc_prebuilt}" \
		${optee_config} \
		O="${build_dir}/" \
		-j"${NPROC}"
	bins+=("${build_dir}/core/tee-raw.bin")

	source_dir="${SCRIPT_DIR}/optee/optee_client"
	"${MAKE_BIN}" -C "${source_dir}" \
		CROSS_COMPILE="${CROSS_COMPILE_AARCH64}" \
		PYTHON3="${PYTHON3_PATH}" \
		TA_DEV_KIT_DIR="${build_dir}/export-ta_arm64/" \
		O="${build_dir}/" \
		DESTDIR="${install_dir}" \
		-j"${NPROC}"
	bins+=("${install_dir}/usr/sbin/tee-supplicant")

	source_dir="${SCRIPT_DIR}/optee/samples"
	"${MAKE_BIN}" -C "${source_dir}" \
		CROSS_COMPILE="${CROSS_COMPILE_AARCH64}" \
		PYTHON3="${PYTHON3_PATH}" \
		TA_DEV_KIT_DIR="${build_dir}/export-ta_arm64/" \
		OPTEE_CLIENT_EXPORT="${install_dir}/usr" \
		O="${build_dir}/" \
		-j"${NPROC}"
	bins+=("${install_dir}/usr/sbin/nvhwkey-app")
	bins+=("${install_dir}/usr/sbin/nvluks-srv-app")

	if [ "${TARGET_PLATFORM}" == "t234" ] && [ "${ENABLE_FTPM_BUILD}" == "yes" ]; then
		source_dir="${SCRIPT_DIR}/optee/samples/ms-tpm-20-ref/Samples/ARM32-FirmwareTPM/optee_ta"
		ftpm_config="CFG_TA_MEASURED_BOOT=y \
			     CFG_USE_PLATFORM_EPS=y"
		# shellcheck disable=SC2086
		"${MAKE_BIN}" -C "${source_dir}" \
			CROSS_COMPILE="${CROSS_COMPILE_AARCH64}" \
			PYTHON3="${PYTHON3_PATH}" \
			TA_DEV_KIT_DIR="${build_dir}/export-ta_arm64/" \
			OPTEE_CLIENT_EXPORT="${install_dir}/usr" \
			OPTEE_OS_DIR="${SCRIPT_DIR}/optee/optee_os" \
			${ftpm_config} \
			O="${build_dir}/" \
			-j"${NPROC}"
		bins+=("${install_dir}/usr/sbin/nvftpm-helper-app")
	fi

	# Re-build OP-TEE with early TA
	source_dir="${SCRIPT_DIR}/optee/optee_os"
	# shellcheck disable=SC2086
	"${MAKE_BIN}" -C "${source_dir}" \
		PLATFORM="tegra" \
		PLATFORM_FLAVOR="${TARGET_PLATFORM}" \
		CROSS_COMPILE64="${CROSS_COMPILE_AARCH64}" \
		PYTHON3="${PYTHON3_PATH}" \
		CFG_WITH_STMM_SP=y \
		CFG_STMM_PATH="${UEFI_STMM_PATH}" \
		NV_CCC_PREBUILT="${ccc_prebuilt}" \
		${optee_config} \
		O="${build_dir}/" \
		EARLY_TA_PATHS="${early_tas}" \
		-j"${NPROC}"

	source_dir="${SCRIPT_DIR}/optee/optee_test"
	"${MAKE_BIN}" -C "${source_dir}" \
		CROSS_COMPILE="${CROSS_COMPILE_AARCH64}" \
		CC="${CROSS_COMPILE_AARCH64}gcc" \
		PYTHON3="${PYTHON3_PATH}" \
		TA_DEV_KIT_DIR="${build_dir}/export-ta_arm64/" \
		OPTEE_CLIENT_EXPORT="${install_dir}/usr" \
		O="${build_dir}/" \
		-j"${NPROC}"
	"${MAKE_BIN}" -C "${source_dir}" \
		TA_DEV_KIT_DIR="${build_dir}/export-ta_arm64/" \
		OPTEE_CLIENT_EXPORT="${install_dir}/usr" \
		DESTDIR="${install_dir}" \
		O="${build_dir}/" \
		install
	bins+=("${install_dir}/bin/xtest")
	for bin in "${bins[@]}"; do
		if [ ! -f "${bin}" ]; then
			echo "Error: Missing output binary ${bin}"
			exit 1
		fi
	done

	echo "optee sources compiled successfully."
}

TARGET_PLATFORM=""
ENABLE_FTPM_BUILD=""
while getopts "hp:t" OPTION
do
	case $OPTION in
		p) TARGET_PLATFORM="${OPTARG}"; ;;
		t) ENABLE_FTPM_BUILD="yes"; ;;
		h)
			usage
			exit 0
		;;
		*)
			usage
			exit 1
		;;
        esac
done

if [ "${TARGET_PLATFORM}" = "" ]; then
	echo "The platform value is missing. Use \"-p\" to set it."
	exit 1
fi
if [ "${TARGET_PLATFORM}" != "t194" ] && [ "${TARGET_PLATFORM}" != "t234" ]; then
	echo "The platform value is wrong. Supported platform values: t194|t234."
	exit 1
fi

check_pre_req
build_optee_sources
