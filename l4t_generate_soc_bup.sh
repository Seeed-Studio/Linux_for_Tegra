#!/bin/bash

# Copyright (c) 2018-2023, NVIDIA CORPORATION.  All rights reserved.
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#  * Neither the name of NVIDIA CORPORATION nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# This script generates bootloader and kernel multi-specification BUP
# update payloads for Jetson boards

set -e

function usage()
{
	if [ -n "${1}" ]; then
		echo "${1}"
		echo ""
	fi

	echo "This script generates bootloader and kernel payloads based on the"
	echo "specifications listed in the vars at the end this script and the"
	echo "supplied <target_soc>."
	echo ""
	echo "Usage:"
	echo "	${script_name} [-h|--help] [-u <key_file>] [-v <SBK key file>] [-b <board>] [-p <option>] [-f <boardspec file>] [-e <boardspec entry>] <target_soc>"
	echo "	-h|--help      Displays this help prompt."
	echo "	-u <PKC key file> Optional RSA key file for signing binaries."
	echo "	-v <SBK key file> Optional Secure Boot Key (SBK) key used for ODM fused board."
	echo "                 This option must used with PKC key, and only support for t19x."
	echo "	-p <option>    Pass options to flash.sh when generating the BUP."
	echo "	<target_soc>   Must be \"t19x\", \"t23x\"."
	echo "                 Must be compatible with current build environment."
	echo "                 (t19x under t186ref)"
	echo "	-b <board>     This option specifies one particular board for corresponding target_soc."
	echo "                 Supported boards are:"
	echo "                     For \"t19x\": \"jetson-xavier-maxn\", \"jetson-xavier-slvs-ec\","
	echo "                                   \"jetson-xavier-nx-devkit\", \"jetson-xavier-nx-devkit-emmc\","
	echo "                                   \"clara-agx-xavier-devkit\", \"jetson-agx-xavier-industrial\";"
	echo "                                   \"jetson-agx-xavier-devkit\", \"jetson-agx-xavier-industrial-mxn\";"
	echo "                     For \"t23x\": \"jetson-agx-orin-devkit\", \"holoscan-devkit\","
	echo "                                   \"jetson-orin-nano-devkit\";"
	echo "	-f <boardspec file> This option specifies user provided board spec file."
	echo "	-e <boardspec entry> This option specifies board spec entry in user provided board spec file, like:"
	echo "                     \"jetson_tx2_devkit_ota_emmc_r28_3_spec\" in the \"ota_board_specs.conf\""
	echo "	-d|--debug     Keep intermediate files during create payload"
	echo ""
	echo "Examples:"
	echo "	${script_name} t19x"
	echo "	(generates bl, kernel multi-spec payloads for t19x SoC)"
	echo ""
	echo "	${script_name} -b jetson-xavier-nx-devkit-emmc t19x"
	echo "	(generates bl, kernel multi-spec payloads for t19x SoC, Jetson Xavier NX devkit)"
	echo ""
	echo "	${script_name} -u pkc.key -v sbk.key -p \"--user_key user.key\" t19x"
	echo "	(generates secured bl, kernel multi-spec payloads for t19x SoC)"
	echo ""
	echo "Notes:"
	echo "	- generates bl_only_payload, kernel_only_payload for t19x"
	echo "	- payloads are under \"Linux_for_Tegra/bootloader/payloads_\""
	echo ""
	echo "These generated payloads are consumed by nv_update_engine on the target"
	echo "to update bootloader and kernel partitions on the device."

	exit 1
}

function check_pre_req()
{
	if [ -z "$1" ]; then
		usage "Error. Arguments required"
	fi

	while [ -n "${1}" ]; do
		case "${1}" in
			-h | --help)
				usage
				;;
			-u)
				[ -n "${2}" ] || usage "Not enough parameters"
				build_bup_script_opts+="-u \"${2}\" "
				shift 2
				;;
			-v)
				[ -n "${2}" ] || usage "Not enough parameters"
				build_bup_script_opts+="-v \"${2}\" "
				shift 2
				;;
			t19x)
				target_soc_name="t19x"
				if [ -z "${board_spec_entry}" ]; then
					board_spec_entry="t19x_spec"
				fi
				shift 1
				;;
			t23x)
				target_soc_name="t23x"
				if [ -z "${board_spec_entry}" ]; then
					board_spec_entry="t23x_spec"
				fi
				shift 1
				;;
			-b)
				[ -n "${2}" ] || usage "Not enough parameters"
				target_board="${2}"
				shift 2
				;;
			-d | --debug)
				dbg_payloads=1
				shift 1
				;;
			-p)
				[ -n "${2}" ] || usage "Not enough parameters"
				build_bup_script_opts+="${2} "
				shift 2
				;;
			-e)
				[ -n "${2}" ] || usage "Not enough parameters"
				board_spec_entry="${2}"
				shift 2
				;;
			-f)
				[ -n "${2}" ] || usage "Not enough parameters"
				board_spec_file="${2}"
				shift 2
				;;
			*)
				usage "Error. Unknown option or target SoC: ${1}"
				;;
		esac
	done

	if [ ! -f "${build_bup_script}" ]; then
		echo "Error. ${build_bup_script} not found" > /dev/stderr
		usage
	fi

	# apply user specified board spec file and board spec entry
	if [ -n "${board_spec_file}" ] && [ -n "${board_spec_entry}" ]; then
		if [ ! -f "${board_spec_file}" ]; then
			echo "Error. Specified file ${board_spec_file} is not found"
			usage
		fi
		source "${board_spec_file}"
		target_soc="${board_spec_entry}[@]"
		target_soc=("${!target_soc}")
	elif [ -n "${board_spec_file}" ] || [ -n "${board_spec_entry}" ]; then
		echo "Error. The -f option and -e option must be applied together"
		usage
	fi

	if [ "${target_soc[0]}" == "" ]; then
		echo "Error. Unknown target soc: NULL"
		usage
	fi

	# default generate all boards in target_soc.
	if [ "${target_board}" == "all" ]; then
		return 0
	fi

	# check paramter if not default case
	local board_found=0
	for soc in "${target_soc[@]}"; do
		eval "${soc}"
		if [ "${board}" == "${target_board}" ]; then
			board_found=1
			break
		fi
	done

	if [ "${board_found}" -ne 1 ]; then
		usage "Error. Unknown target board: ${target_board}"
	fi
}

function create_payloads()
{
	local ret_msg=""
	local ret_val=0
	local fail_count=0
	local bup_type_opt=""

	local spec_tmp="${target_soc[0]}"
	eval "${spec_tmp}"
	local cmd_clean="FAB=${fab} BOARDID=${boardid} BOARDSKU=${boardsku} \
		BOARDREV=${boardrev} CHIPREV=${chiprev} FUSELEVEL=\"fuselevel_production\" \
		${build_bup_script} --clean-up ${board} ${rootdev}"
	if eval "${cmd_clean}"; then
		ret_msg+="\r\nSUCCESS: cleaned up BUP tmp files prior to payload creation"
	else
		ret_msg+="\r\nFAILURE: error cleaning BUP tmp files"
		ret_val=1
		((++fail_count))
	fi

	for spec in "${target_soc[@]}"; do
		eval "${spec}"

		if [ "${fuselevel_s}" == "0" ]; then
			fuselevel="fuselevel_nofuse";
		else
			fuselevel="fuselevel_production";
		fi

		# include specified boards of target_soc
		# "all": defalut all boards supported by a specific target_soc
		if [[ "all" != "${target_board}" && "${board}" != "${target_board}" ]]; then
			continue;
		fi

		if [[ "${bup_type}" != "" ]]; then
			bup_type_opt="--bup-type ${bup_type}"
		else
			bup_type_opt=""
		fi

		local cmd="FAB=${fab} BOARDID=${boardid} BOARDSKU=${boardsku} \
			BOARDREV=${boardrev} FUSELEVEL=${fuselevel} CHIPREV=${chiprev} \
			${build_bup_script} ${build_bup_script_opts} ${bup_type_opt} \
			${board} ${rootdev}"

		echo -e "${cmd}\r\n"
		if eval "${cmd}"; then
			ret_msg+="\r\nSUCCESS: created payload for config \"${spec}\""
		else
			ret_msg+="\r\nFAILURE: no payload made for config \"${spec}\""
			ret_val=1
			((++fail_count))
		fi
	done

	if [ "${dbg_payloads}" -eq 0 ]; then
		if eval "${cmd_clean}"; then
			ret_msg+="\r\nSUCCESS: cleaned up BUP tmp files after payload creation"
		else
			ret_msg+="\r\nFAILURE: error cleaning BUP tmp files"
			ret_val=1
			((++fail_count))
		fi
	fi

	echo "${target_soc_name} payload generation complete with ${fail_count} failure(s)"
	echo -e "${ret_msg}"
	if [ "${ret_val}" -eq 0 ]; then
		local check_cmd="${bup_generator_script} -c ${bootloader_dir}/payloads_${target_soc_name}/bl_only_payload; \
				${bup_generator_script} -c ${bootloader_dir}/payloads_${target_soc_name}/kernel_only_payload"
		if eval "${check_cmd}"; then
			echo -e "\r\nSUCCESS: contents of the BUP"
		else
			echo -e "\r\nFAILURE: can't print out contents of the BUP"
			ret_val=1
		fi
	fi
	exit "${ret_val}"

}

script_name="$(basename "${0}")"
script_file_name="$(echo "${script_name}" | cut -f 1 -d '.')"
l4t_dir="$(cd "$(dirname "${0}")" && pwd)"
build_bup_script="${l4t_dir}/build_l4t_bup.sh"
build_bup_script_opts="--multi-spec "
target_soc_name=""
target_board="all"
dbg_payloads=0
bootloader_dir="${l4t_dir}/bootloader"
bup_generator_script="${bootloader_dir}/BUP_generator.py"
board_spec_entry=
board_spec_file="jetson_board_spec.cfg"

echo ""
echo "Generate Multi-Spec BUP Tool"
echo ""

check_pre_req "${@}"
create_payloads
