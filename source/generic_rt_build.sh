#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2017-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: GPL-2.0-only
#
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
#
# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.

# The script enables/disables PREEMPT RT in the kernel source.
# - usage:
#         generic_rt_build.sh enable; #for applying RT kernel
#         generic_rt_build.sh disable; #for reverting RT kernel

SCRIPT_DIR="$(dirname "$(readlink -f "${0}")")"
source "${SCRIPT_DIR}/kernel_src_build_env.sh"

any_failure=0
enable_rt()
{
	if [ -f "${SCRIPT_DIR}"/kernel/"${KERNEL_SRC_DIR}"/arch/arm64/configs/.orig.defconfig ]; then
		echo "PREEMPT RT config is already applied to the kernel!"
	else
		#make temporary copy of the defconfig file
		cp "${SCRIPT_DIR}"/kernel/"${KERNEL_SRC_DIR}"/arch/arm64/configs/defconfig\
			"${SCRIPT_DIR}"/kernel/"${KERNEL_SRC_DIR}"/arch/arm64/configs/.orig.defconfig

		#make temporary copy of the tegra_prod_defconfig file
		cp "${SCRIPT_DIR}"/kernel/"${KERNEL_SRC_DIR}"/arch/arm64/configs/tegra_prod_defconfig\
			"${SCRIPT_DIR}"/kernel/"${KERNEL_SRC_DIR}"/arch/arm64/configs/.orig.tegra_prod_defconfig

		if [ -d "${SCRIPT_DIR}"/kernel/"${KERNEL_SRC_DIR}"/rt-patches ]; then
			file_list=$(find "${SCRIPT_DIR}"/kernel/"${KERNEL_SRC_DIR}"/rt-patches -name \*.patch -type f | sort)
			for p in $file_list; do
				# set flag in case of failure and continue
				patch -s -d .. -p1 < "$p" || any_failure=1
				if [[ ${any_failure} -eq 1 ]] ; then
					echo "failed patching ${p}"
					return;
				fi
			done
			echo "The PREEMPT RT patches have been successfully applied!"
		fi

		#make temporary copy of the defconfig file
		cp -f "${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/defconfig"\
			"${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/.updated.defconfig"

		#make temporary copy of the tegra_prod_defconfig file
		cp -f "${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/tegra_prod_defconfig"\
			"${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/.updated.tegra_prod_defconfig"

		"${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/scripts/config" --file "${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/.updated.defconfig"\
			--enable PREEMPT_RT  --disable DEBUG_PREEMPT\
			--disable KVM\
			--enable EMBEDDED\
			--enable NAMESPACES\
			--disable CPU_IDLE_TEGRA18X\
			--disable CPU_FREQ_GOV_INTERACTIVE\
			--disable CPU_FREQ_TIMES \
			--disable FAIR_GROUP_SCHED || any_failure=1

		"${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/scripts/config" --file "${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/.updated.tegra_prod_defconfig"\
			--enable PREEMPT_RT  --disable DEBUG_PREEMPT\
			--disable KVM\
			--enable EMBEDDED\
			--enable NAMESPACES\
			--disable CPU_IDLE_TEGRA18X\
			--disable CPU_FREQ_GOV_INTERACTIVE\
			--disable CPU_FREQ_TIMES \
			--disable FAIR_GROUP_SCHED || any_failure=1

		[[ -f "${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/defconfig" ]] && rm "${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/defconfig"
		[[ -f "${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/tegra_defconfig" ]] && rm "${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/tegra_defconfig"
		[[ -f "${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/tegra_prod_defconfig" ]] && rm "${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/tegra_prod_defconfig"

		cp -fnrs "${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/.updated.defconfig"\
				"${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/defconfig"
		ln -s "${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/defconfig"\
				"${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/tegra_defconfig"
		cp -fnrs "${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/.updated.tegra_prod_defconfig"\
				"${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/tegra_prod_defconfig"

		echo "PREEMPT RT config is set successfully!"
	fi
}

disable_rt()
{
	if [ -f "${SCRIPT_DIR}"/kernel/"${KERNEL_SRC_DIR}"/arch/arm64/configs/.orig.defconfig ]; then
		if [ -d "${SCRIPT_DIR}"/kernel/"${KERNEL_SRC_DIR}"/rt-patches ]; then
			file_list=$(find "${SCRIPT_DIR}"/kernel/"${KERNEL_SRC_DIR}"/rt-patches -name \*.patch -type f | sort -r)
			for p in $file_list; do
				# set flag in case of failure and continue
				patch -s -R -d .. -p1 < "$p" || any_failure=1
			done
			echo "The PREEMPT RT patches have been successfully reverted!"
		fi

		rm "${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/defconfig"
		rm "${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/tegra_defconfig"
		rm "${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/tegra_prod_defconfig"
		cp "${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/.orig.defconfig"\
			"${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/defconfig"
		ln -s "defconfig"\
			"${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/tegra_defconfig"
		cp "${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/.orig.tegra_prod_defconfig"\
			"${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/tegra_prod_defconfig"

		rm -rf "${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/.orig.defconfig"
		rm -rf "${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/.updated.defconfig"
		rm -rf "${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/.orig.tegra_prod_defconfig"
		rm -rf "${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/configs/.updated.tegra_prod_defconfig"
		echo "PREEMPT RT config is disabled successfully!"
	else
		echo "PREEMPT RT config not applied to the kernel!"
	fi
}

usage()
{
	echo Usages:
	echo 1. "${0}" enable : Enable RT kernel
	echo 2. "${0}" disable : Disable RT kernel
	any_failure=1
}

# script starts from here
dir_run_from=$(dirname "${0}")
pushd "$dir_run_from" &>/dev/null || exit
if [ "$1" == "enable" ]; then
	echo "Enable RT kernel"
	enable_rt
elif [ "$1" == "disable" ]; then
	echo "Disable RT kernel"
	disable_rt
else
	echo "Wrong argument"
	usage
fi

popd &>/dev/null || exit

exit $any_failure
