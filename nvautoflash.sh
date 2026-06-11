#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2021-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

# Usage:
#
# sudo ./nvautoflash.sh <flash.sh options>  ---- auto detect & flash
# sudo ./nvautoflash.sh --print_boardid -------- print boardid & no flash
# sudo ./nvautoflash.sh -h|--help  ------------- print usage (no device required)
#

set -o pipefail;
set -o errtrace;
shopt -s extglob;
curdir=$(dirname "$0");
curdir=$(cd "${curdir}" && pwd);
scriptname=$(basename "$0");
cmdline="$(printf %q "${BASH_SOURCE[@]}")$( (($#)) && printf ' %q' "$@" )";
arguments="$*";
LDK_DIR="${curdir}";
BL_DIR="${LDK_DIR}/bootloader";
cvmname="cvm.bin";
bbdname="cvb.bin";
read_baseboard_eeprom="yes";
sbkpkc="false";
do_not_flash="false";
usb_instance="";

usage ()
{
	cat <<EOF
Usage:
  sudo ./${scriptname} [<flash.sh options>]    Auto-detect connected board and flash
  sudo ./${scriptname} --print_boardid         Print board ID and exit (no flash)

Options handled by ${scriptname}:
  -h, -?, --help     Show this message and exit (no RCM device required)

Other arguments are passed through to flash.sh (T234) or tools/kernel_flash/l4t_initrd_flash.sh (T264).
EOF
}

for __nvaf_arg in "$@"; do
	case "${__nvaf_arg}" in
	-h|-\?|--help)
		usage
		exit 0
		;;
	esac
done

findadev()
{
	# Splitting this line is too dangerous.
	local devpaths=($(find /sys/bus/usb/devices/usb*/ -name devnum -print0 | {
		local fn_devnum;
		local found=();
		while read -r -d "" fn_devnum; do
			local dir;
			local vendor;
			local product;
			local busnum;
			local devpath;
			local fn_busnum;
			local fn_devpath;
			dir="$(dirname "${fn_devnum}")";
			vendor="$(cat "${dir}/idVendor")";
			if [ "${vendor}" != "0955" ]; then
				continue
			fi;
			product="$(cat "${dir}/idProduct")";
			case "${product}" in
			"7023") ;;	# AGX Orin
			"7223") ;;	# SKU4 Orin
			"7323") ;;	# Orin NX 16GB
			"7423") ;;	# Orin NX 8GB
			"7523") ;;	# Orin Nano 8GB
			"7623") ;;	# Orin Nano 4GB
			"7026") ;;	# AGX Thor
			"7226") ;;	# AGX Thor T4000
			"7326") ;;	# AGX Thor prod fused
			*) continue ;;
			esac
			fn_busnum="${dir}/busnum";
			if [ ! -f "${fn_busnum}" ]; then
				continue;
			fi;
			fn_devpath="${dir}/devpath";
			if [ ! -f "${fn_devpath}" ]; then
				continue;
			fi;
			# Only include devices for which the DEVNAME exists. In a container
			# environment, the DEVNAME for this device may not have been mapped
			# in, which is the case for when a device is in recovery mode, but
			# that device is not mapped into the current container.
			devname=$(udevadm info --query=property "$dir" | grep DEVNAME | cut -d= -f2)
			if [ ! -e "$devname" ]; then
				continue
			fi

			busnum="$(cat "${fn_busnum}")";
			devpath="$(cat "${fn_devpath}")";

			found+=("${busnum}-${devpath}");
		done;
		echo "${found[@]}";
	}))
	echo "${#devpaths[@]}";
}

chk_baseboard_eeprom()
{
	# Splitting this line is too dangerous.
	local result=($(find /sys/bus/usb/devices/usb*/ -name devnum -print0 | {
		local fn_devnum;
		local found;
		while read -r -d "" fn_devnum; do
			local dir;
			local vendor;
			local product;
			dir="$(dirname "${fn_devnum}")";
			vendor="$(cat "${dir}/idVendor")";
			if [ "${vendor}" != "0955" ]; then
				continue
			fi;
			product="$(cat "${dir}/idProduct")";
			case "${product}" in
			"7023") found="no-23x"; ;;	# AGX Orin
			"7223") found="no-23x"; ;;	# SKU4 Orin
			"7323") found="no-23x"; ;;	# Orin NX 16GB
			"7423") found="no-23x"; ;;	# Orin NX 8GB
			"7523") found="no-23x"; ;;	# Orin Nano 8GB
			"7623") found="no-23x"; ;;	# Orin Nano 4GB
			"7026") found="yes-26x"; ;;	# AGX Thor
			"7226") found="yes-26x"; ;;	# AGX Thor T4000
			"7326") found="yes-26x"; ;;	# AGX Thor Prod
			*) continue ;;
			esac
		done;
		echo "${found}";
	}))
	echo "${result[0]}";
}

#
# read_ecid can be issued only once per boot recovery.
#
read_ecid ()
{
	local ECID;
	local rcmcmd;
	local inst_args="";

	if [ -f "${BL_DIR}/tegrarcm_v2" ]; then
		rcmcmd="tegrarcm_v2";
	else
		echo "Error: tegrarcm is missing." >&2;
		exit 1;
	fi;
	# The ${usb_instance} can be passed as one of environment variable.
	# In case there is no ${usb_instance} passed as one of environment
	# variable, the tegraflash tool takes the first instance of usb
	# connection.
	if [ -n "${usb_instance}" ]; then
		inst_args="--instance ${usb_instance}";
	fi;
	pushd "${BL_DIR}" > /dev/null 2>&1 || exit 2;
	# ${inst_args} is either null or  multi-token parameter which should
	# not be double quoted as single token parameter.

	if [[ "${read_baseboard_eeprom}" =~ 23x ]]; then
		ECID=$("${BL_DIR}/${rcmcmd}" ${inst_args} --uid --new_session --chip 0x23 | grep BR_CID | cut -d' ' -f2);
	elif [[ "${read_baseboard_eeprom}" =~ 26x ]]; then
		ECID=$("${BL_DIR}/${rcmcmd}" ${inst_args} --uid --new_session --chip 0x26 | grep BR_CID | cut -d' ' -f2);
	fi
	rm -f rcm_state
	popd > /dev/null 2>&1 || exit 2;
	echo "${ECID}";
}

parse_hwchipid ()
{
	local ECID="$1";

	local idval="";
	local hival="";
	idval="0x${ECID:6:2}";
	hival="${idval}";
	echo "${hival}";
}

parse_fuselevel ()
{
	local ECID="$1";
	local hwchipid="$2";
	local flval="";

	flval="${ECID:2:1}";
	if [ "${hwchipid}" = "0x23" ] || [ "${hwchipid}" = "0x26" ]; then
		flval="0x${ECID:2:1}";
		flval=$(printf %x "$((flval & 0x8))");
		if [ "${flval}" = "0" ]; then
			flval="fuselevel_nofuse";
		else
			flval="fuselevel_production";
		fi;
	fi;
	echo "${flval}";
}

parse_bootauth ()
{
	local ECID="$1";
	local hwchipid="$2";

	local flval="";
	local baval="";

	if [ "${hwchipid}" = "0x26" ]; then
		flval="0x${ECID:2:2}";
		flval=$(printf %x "$((flval & 0xe8))");
		# Bit 127 - ProductionMode
		# Bit 126 - SecurityMode
		# Bit 125 - OemKeyValid
		# Bit 123 - SBK
		case ${flval} in
		0|80) baval="NS"; ;;
		e0|a0) baval="PKC"; ;;
		e8|a8) baval="SBKPKC"; ;;
		*) echo "Error: Invalid fuse configuration 0x${flval}";
			exit 1;
		esac;
	elif [ "${hwchipid}" = "0x23" ]; then
		flval="0x${ECID:3:1}";
		flval=$(printf %x "$((flval + 0))");
		case ${flval} in
		0) baval="NS"; ;;
		# 1 - 3K RSA
		# 2 - ECDSA P-256
		# 3 - ECDSA P-512
		# 4 - ED25519
		# 5 - XMSS
		1|2|3|4|5) baval="PKC"; ;;
		# 9 - SBK + 3K RSA
		# a - SBK + ECDSA P-256
		# b - SBK + ECDSA P-512
		# c - SBK + ED25519
		# d - SBK + XMSS
		9|a|b|c|d) baval="SBKPKC"; ;;
		*) echo "Error: Invalid authentication 0x${flval}";
			exit 1;
		esac;
	fi;
	echo "${baval}";
}

chkerr()
{
	if [ "$1" != 0 ]; then
		echo "--- Error: $2 failed." >&2;
		exit 14;
	fi;
	echo "--- $2 succeeded.";
}

getidx()
{
	local i;
	local f="$1";
	local s="$2";
	shift; shift;
	# The each individual arguments better be splitted.
	local a=($@);

	for (( i=0; i<${#a[@]}; i++ )); do
		if [ "$f" != "${a[$i]}" ]; then
			continue;
		fi;
		i=$(( i+1 ));
		if [ "${s}" != "" ]; then
			if [ "$s" != "${a[$i]}" ]; then
				continue;
			fi;
			i=$(( i+1 ));
		fi;
		echo "$i";
		return 0;
	done;
	echo "Error: $f $s not found" >&2;
	exit 3;
}

chkidx()
{
	local i;
	local f="$1";
	shift;
	# The each individual arguments better be splitted.
	local a=($@);

	for (( i=0; i<${#a[@]}; i++ )); do
		if [ "$f" != "${a[$i]}" ]; then
			continue;
		fi;
		return 0;
	done;
	return 1;
}

extract_args ()
{
	local idx;
	local astr="${cmdline}";
	local OIFS=${IFS};
	IFS=' ';
	# The astr better be splitted to build proper array of tokens.
	local a=($astr);
	IFS=$OIFS;

	if chkidx "--usb-instance" "${a[@]}"; then
		idx=$(getidx "--usb-instance" "" "${a[@]}");
		usb_instance="${a[$idx]}";
	fi;
}

#
# XXX: Read chip specifc details
#
get_chip_info_details ()
{
        local args="";
        local __chip_SKU=$1;
        local __chip_minor_revision_ID=$2;
        local __bootrom_revision_ID=$3;
        local __ramcode_ID=$4;
        local chipSKU;
        local chipminorrevisionID;
        local bootromrevisionID;
        local ramcodeID;

        pushd "${BL_DIR}" > /dev/null 2>&1 || return;
        chipSKU=$(./chkbdinfo -C chip_info.bin_bak);
        chipminorrevisionID=$(./chkbdinfo -M chip_info.bin_bak);
        bootromrevisionID=$(./chkbdinfo -O chip_info.bin_bak);
        ramcodeID=$(./chkbdinfo -R chip_info.bin_bak);
        chkerr "$?" "Parsing chip_info.bin information";

        popd > /dev/null 2>&1 || return;

        eval "${__chip_SKU}"="${chipSKU}";
        eval "${__chip_minor_revision_ID}"="${chipminorrevisionID}";
        eval "${__bootrom_revision_ID}"="${bootromrevisionID}";
        eval "${__ramcode_ID}"="${ramcodeID}";
}

function set_value_in_bct_cfg() {
    local value="$1"
    local tag_name="$2"
    local xml_file="$3"

    # Check if xmlstarlet is installed
    if ! command -v xmlstarlet &> /dev/null; then
        echo "Error: xmlstarlet is not installed. Please install by running:"
        echo "sudo apt install xmlstarlet"
        return 1
    fi

    # Update the tag value
    xmlstarlet ed -L -u "/bct_cfg/$tag_name" -v "$value" "$xml_file"

    echo "Updated $tag_name to $value in $xml_file"
}


#
# After "reboot recovery", ECID read has to preceed any RCM operation.
#
read_eeprom ()
{
	local args="";
	local tegraid=$1;
	local module="boardinfo"
	if [ "${tegraid}" = "0x23" ] || [ "${tegraid}" = "0x26" ]; then
		module="cvm"
	fi
	local cvmcmd="dump eeprom ${module} ${cvmname}";
	local bbdcmd="dump eeprom cvb ${bbdname}";
	local command="${cvmcmd}";
	local keyfile="";
	local sbk_keyfile="";
	local idx;
	local astr="${cmdline}";
	local OIFS=${IFS};
	IFS=' ';
	# The astr better be splitted to build proper array of tokens.
	local a=($astr);
	IFS=$OIFS;

	if [[ "${read_baseboard_eeprom}" =~ yes ]]; then
		command+="; ${bbdcmd}";
	fi;
	if chkidx "-u" "${a[@]}"; then
		idx=$(getidx "-u" "" "${a[@]}");
		keyfile=$(readlink -f "${a[$idx]}");
	fi;
	if chkidx "-v" "${a[@]}"; then
		idx=$(getidx "-v" "" "${a[@]}");
		sbk_keyfile=$(readlink -f "${a[$idx]}");
	fi;

	if [ "${CHIPMAJOR}" != "" ]; then
		args+="--chip \"${tegraid} ${CHIPMAJOR}\" ";
	else
		args+="--chip ${tegraid} ";
	fi;
	if [ -n "${usb_instance}" ]; then
		args+="--instance ${usb_instance} ";
	fi;

	if [ "${tegraid}" = "0x23" ]; then
		args+="--applet \"${BL_DIR}/mb1_t234_prod.bin\" "
		emc_fuse_dev_params="tegra234-br-bct-diag-boot.dts";
		cp -f "${TARGET_DIR}/BCT/${emc_fuse_dev_params}" "${BL_DIR}/${emc_fuse_dev_params}";
		sed -i "s/preprod_dev_sign = <1>/preprod_dev_sign = <0>/" "${BL_DIR}/${emc_fuse_dev_params}";
		args+="--cfg readinfo_t234_min_prod.xml "
		command+="; reboot recovery"
		args+="--dev_params ${emc_fuse_dev_params} ";
		device_config="tegra234-mb1-bct-device-p3701-0000.dts"
		misc_config="tegra234-mb1-bct-misc-p3701-0000.dts"
		cp -f "${TARGET_DIR}/BCT/${device_config}" "${BL_DIR}/${device_config}"
		cp -f "${TARGET_DIR}/BCT/${misc_config}" "${BL_DIR}/${misc_config}"
		args+="--device_config ${device_config} --misc_config ${misc_config} "
		mb2applet=applet_t234.bin
		args+="--bins \"mb2_applet ${mb2applet}\" ";
	elif [ "${tegraid}" = "0x26" ]; then
		args+="--applet \"${BL_DIR}/applet_t264.bin\" "
		emc_fuse_dev_params="tegra264-br-bct-diag-boot.dts";
		cp -f "${TARGET_DIR}/BCT/${emc_fuse_dev_params}" "${BL_DIR}/${emc_fuse_dev_params}";
		sed -i "s/preprod_dev_sign = <1>/preprod_dev_sign = <0>/" "${BL_DIR}/${emc_fuse_dev_params}";
		rcmbootbctcfgname="flash_l4t_t264_bct_cfg.xml"
		diagcfg="diag_${rcmbootbctcfgname}"
		cp -f "${BL_DIR}/${rcmbootbctcfgname}" "${BL_DIR}/${diagcfg}"

		device_config="tegra264-mb1-bct-device-common-l4t.dts"
		pinmux_config="tegra264-mb1-bct-pinmux-p3834-xxxx-p3971-0000-b02.dts"
		misc_config="tegra264-mb1-bct-misc-p3834-xxxx-p3971-0000.dts"
		sdram_config="tegra264-p3834-0008-sdram-bct-l4t.dts"
		cp -f "${TARGET_DIR}/BCT/${device_config}" "${BL_DIR}/${device_config}"
		cp -f "${TARGET_DIR}/BCT/${misc_config}" "${BL_DIR}/${misc_config}"
		cp -f "${TARGET_DIR}/BCT/${pinmux_config}" "${BL_DIR}/${pinmux_config}"
		cp -f "${TARGET_DIR}/BCT/${sdram_config}" "${BL_DIR}/${sdram_config}"
		set_value_in_bct_cfg "${device_config}" "brbct_cfg/device" "${BL_DIR}/${diagcfg}"
		set_value_in_bct_cfg "${misc_config}" "brbct_cfg/misc" "${BL_DIR}/${diagcfg}"
		set_value_in_bct_cfg "${pinmux_config}" "brbct_cfg/pinmux" "${BL_DIR}/${diagcfg}"
		set_value_in_bct_cfg "${emc_fuse_dev_params}" "brbct_cfg/dev_param" "${BL_DIR}/${diagcfg}"
		set_value_in_bct_cfg "${sdram_config}" "brbct_cfg/sdram" "${BL_DIR}/${diagcfg}"

		args+="--rcmboot_pt_layout readinfo_t264_min_prod.xml --rcmboot_bct_cfg ${diagcfg} "
		command+="; reboot recovery"

	fi
	args+="--skipuid ";
	args+="--cmd \"${command}\" ";
	local cmd="\"${BL_DIR}/tegraflash.py\" ${args}";
	pushd "${BL_DIR}" > /dev/null 2>&1 || exit 2;
	rm -f "${cvmname}" "${bbdname}";
	if [ "${keyfile}" != "" ]; then
		cmd+="--key \"${keyfile}\" ";
	fi;
	if [ "${sbk_keyfile}" != "" ]; then
		cmd+="--encrypt_key \"${sbk_keyfile}\" ";
	fi;
	if [ -f "rcm_state" ]; then
		rm rcm_state;
	fi;
	echo "${cmd}";
	eval "${cmd}";
	chkerr "$?" "Reading board information";

	popd > /dev/null 2>&1 || exit 2;
}

#
#                                       BOARDID  BOARDSKU  FAB  BOARDREV  BASEBOARDID
#    ----------------------------------+--------+---------+----+---------+-----------+
#    jetson-agx-orin-devkit               3701     0001      TS1  C.2
#    jetson-agx-orin-devkit               3701     0000      TS4  A.0
#    holoscan-devkit                      3701     0002      TS1  A.0
#    jetson-agx-orin-devkit               3701     0004      TS4  A.0
#    jetson-agx-orin-devkit (64GB)        3701     0005
#    jetson-agx-orin-devkit-industrial    3701     0008
#    jetson-orin-nano-devkit (NX 16GB)    3767     0000
#    jetson-orin-nano-devkit (NX 8GB)     3767     0001
#    jetson-orin-nano-devkit (NX 16GB)    3767     0002
#    jetson-orin-nano-devkit (Nano 8GB)   3767     0003
#    jetson-orin-nano-devkit (Nano 4GB)   3767     0004
#    jetson-orin-nano-devkit (Nano 8GB)   3767     0005
#    p3834-0008-p3971-0000 (Thor 128GB)   3834     0008                      3971
#    p3834-0005-p3971-0000 (Thor 128GB)   3834     0005                      3971
#    jetson-agx-thor-devkit (Thor 128GB)  3834     0008                      4071
#    jetson-agx-thor-t4000  (Thor 64GB)   3834     0000                      4071
#    ----------------------------------+--------+---------+----+---------+-----------+
#
cfgtab=(\
	"jetson-agx-orin-devkit"			"internal"	"3701"	"0001" "" \
	"jetson-agx-orin-devkit"			"internal"	"3701"	"0000" "" \
	"holoscan-devkit"					"internal"	"3701"	"0002" "" \
	"jetson-agx-orin-devkit"			"internal"	"3701"	"0004" "" \
	"jetson-agx-orin-devkit"			"internal"	"3701"	"0005" "" \
	"jetson-orin-nano-devkit"			"internal"	"3767"	"0000" "" \
	"jetson-orin-nano-devkit"			"internal"	"3767"	"0001" "" \
	"jetson-orin-nano-devkit"			"internal"	"3767"	"0002" "" \
	"jetson-orin-nano-devkit"			"internal"	"3767"	"0003" "" \
	"jetson-orin-nano-devkit"			"internal"	"3767"	"0004" "" \
	"jetson-orin-nano-devkit-super"		"internal"	"3767"	"0005" "" \
	"jetson-agx-orin-devkit-industrial"	"internal" 	"3701"	"0008" "" \
	"p3834-0008-p3971-0000-nvme"			"	external"	"3834"	"0008" "3971" \
	"p3834-0005-p3971-0000"				"external"	"3834"	"0005" "3971" \
	"jetson-agx-thor-devkit"			"external"	"3834"	"0008" "4071" \
	"jetson-agx-thor-t4000"				"external"	"3834"	"0000" "4071" \

);

findconf ()
{
	local BOARDID="$1";
	local BOARDSKU="$2";
	local i;
	for (( i=0; i<${#cfgtab[@]}; )); do
		if [ "${BOARDID}" = "${cfgtab[$((i + 2))]}" ] && \
			[ "${BOARDSKU}" = "${cfgtab[$((i + 3))]}" ] && \
			[ "${BASEBOARDID}" = "${cfgtab[$((i + 4))]}" ]; then
			echo "${cfgtab[$i]}";
			return;
		fi;
		i=$((i + 5));
	done;
}

find_bootdev ()
{
	local BOARDID="$1";
	local BOARDSKU="$2";
	local i;
	for (( i=0; i<${#cfgtab[@]}; )); do
		if [ "${BOARDID}" = "${cfgtab[$((i + 2))]}" ]; then
			if [ "${BOARDSKU}" = "${cfgtab[$((i + 3))]}" ]; then
				echo "${cfgtab[$((i + 1))]}";
				return;
			fi;
		fi;
		i=$((i + 5));
	done;
}

parse_eeprom ()
{
	local exefile="$1";
	local infofile="$2";
	local cvb="${3}"

	if [ -n "${cvb}" ]; then
		BASEBOARDID=$("${exefile}" -i "${infofile}");
		BASEBOARDID=$(echo "${BASEBOARDID}" | sed -e s/^[[:space:]]*// -e s/[[:space:]]*$//);
		chkerr "$?" "Parsing baseboard ID (${BASEBOARDID})";
		return
	fi

	BOARDID=$("${exefile}" -i "${infofile}");
	BOARDID=$(echo "${BOARDID}" | sed -e s/^[[:space:]]*// -e s/[[:space:]]*$//);
	chkerr "$?" "Parsing board ID (${BOARDID})";

	FAB=$("${exefile}" -f "${infofile}");
	FAB=$(echo "${FAB}" | sed -e s/^[[:space:]]*// -e s/[[:space:]]*$//);
	chkerr "$?" "Parsing board version (${FAB})";

	BOARDSKU=$("${exefile}" -k "${infofile}");
	BOARDSKU=$(echo "${BOARDSKU}" | sed -e s/^[[:space:]]*// -e s/[[:space:]]*$//);
	chkerr "$?" "Parsing board SKU (${BOARDSKU})";

	BOARDREV=$("${exefile}" -r "${infofile}");
	BOARDREV=$(echo "${BOARDREV}" | sed -e s/^[[:space:]]*// -e s/[[:space:]]*$//);
	chkerr "$?" "Parsing board REV (${BOARDREV})";
}

echo -n "*** Checking ONLINE mode ... ";
if [ "${BOARDID}" != "" ] || [ "${BOARDSKU}" != "" ] || \
	[ "${FAB}" != "" ] || [ "${FUSELEVEL}" != "" ]; then
	echo >&2;
	echo "*** Error: ${scriptname} runs only in ONLINE mode." >&2;
	echo "Do not pass BOARDID, BOARDSKU, FAB, and FUSELEVEL" >&2;
	exit 4;
fi;
echo "OK.";

echo -n "*** Checking target board connection ... ";
ndev=$(findadev);
echo "${ndev} connections found.";
extract_args;
if [ "${ndev}" = "0" ]; then
	echo "*** Error: No Jetson device found." >&2;
	exit 5;
fi;
if [ "${ndev}" != "1" ] && [ -z "${usb_instance}" ]; then
	echo "*** Error: Too many Jetson devices found." >&2;
	echo "Connect 1 Jetson in RCM mode and rerun ${cmdline}" >&2;
	exit 6;
fi;
read_baseboard_eeprom=$(chk_baseboard_eeprom);

echo -n "*** Reading ECID ... ";
ECID=$(read_ecid);
if [ "${ECID}" = "" ]; then
	echo "*** Error: ECID read failed." >&2;
	echo "Put the target board in RCM mode and retry." >&2;
	exit 7;
fi;
hwchipid=$(parse_hwchipid "${ECID}");
FUSELEVEL=$(parse_fuselevel "${ECID}" "${hwchipid}");
bootauth=$(parse_bootauth "${ECID}" "${hwchipid}");
echo "FUSELEVEL=${FUSELEVEL} hwchipid=${hwchipid} bootauth=${bootauth}";

if [ "${hwchipid}" = "0x23" ] || [ "${hwchipid}" = "0x26" ]; then
	TARGET_DIR="${BL_DIR}/generic";
else
	echo "*** Error: Unsupported Tegra SoC ID ${hwchipid} found." >&2;
	echo "Terminating." >&2.
	exit 8;
fi;
if [ ! -d "${TARGET_DIR}" ]; then
	echo "*** Error: ${TARGET_DIR} not found." >&2;
	echo "Set up proper BSP and try again." >&2;
	exit 9;
fi;

if [[ "${arguments}" =~ "-u" ]] && [[ "${arguments}" =~ "-v" ]]; then
	sbkpkc="true";
fi;

echo -n "*** Reading EEPROM ... ";
read_eeprom "${hwchipid}";
get_chip_info_details chip_SKU chip_minor_revision_ID bootrom_revision_ID ramcode_ID;
echo "Chip SKU(${chip_SKU}) ramcode(${ramcode_ID})"

if [ -z "${target_board}" ] && [ -f "${BL_DIR}/${cvmname}" ]; then
	echo "Parsing module EEPROM:";
	parse_eeprom "${BL_DIR}/chkbdinfo" "${BL_DIR}/${cvmname}";
	target_board=$(findconf "${BOARDID}" "${BOARDSKU}");
fi;
if [ -f "${BL_DIR}/${bbdname}" ]; then
	echo "Parsing baseboard EEPROM:";
	parse_eeprom "${BL_DIR}/chkbdinfo" "${BL_DIR}/${bbdname}" 1;
	target_board=$(findconf "${BOARDID}" "${BOARDSKU}" "${BASEBOARDID}");
fi;

if [ -z "${target_board}" ]; then
	echo "Error: Target board not found." >&2;
	exit 10;
fi;
echo "${target_board} found.";
if [[ "${arguments}" =~ "--print_boardid" ]]; then
	exit 0;
fi;

echo -n "*** Finding boot device ... ";
bootdev=$(find_bootdev "${BOARDID}" "${BOARDSKU}");
if [ -z "${bootdev}" ]; then
	echo "Error: Boot device not found(BOARDID=${BOARDID}, BOARDSKU=${BOARDSKU})" >&2;
	exit 11;
fi;
echo "Boot device ${bootdev} found.";

#
# Call out flash.sh
#
if [ "${sbkpkc}" = "true" ]; then
	if [[ "${arguments}" =~ "--no-flash" ]]; then
		do_not_flash="true";
	else
		arguments+=" --no-flash";
	fi;
fi;
if  [ "${hwchipid}" = "0x26" ]; then
	cmd="EXTOPTIONS=\"${arguments}\" \"${curdir}/tools/kernel_flash/l4t_initrd_flash.sh\" -p \"${arguments}\" ${target_board} ${bootdev}";
else
	cmd="ADDITIONAL_DTB_OVERLAY="BootOrderEmmc.dtbo" \"${curdir}/flash.sh\" ${arguments} ${target_board} ${bootdev}";
fi
echo "Wait 30 seconds for host machine to get Jetson Recovery USB"
sleep 30
echo "${cmd}";
if ! eval "${cmd}"; then
	echo "*** ERROR: flashing failed." >&2;
	exit 12;
fi;
if [ "${sbkpkc}" = "true" ] && [ "${do_not_flash}" != "true" ]; then
	cd "${BL_DIR}" || exit 2;
	if ! bash flashcmd.txt; then
		echo "*** ERROR: flashing signed binary failed." >&2;
		exit 13;
	fi;
fi;
exit 0;
