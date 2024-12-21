#!/bin/bash

# Copyright (c) 2021-2022, NVIDIA CORPORATION.  All rights reserved.
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
bbdname="bbd.bin";
read_baseboard_eeprom="yes";
sbkpkc="false";
do_not_flash="false";
usb_instance="";

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
			"7721") ;;	# TX1
			"7f21") ;;	# NANO
			"7018") ;;	# TX2i
			"7c18") ;;	# TX2
			"7019") ;;	# AGX
			"7819") ;;	# AGXi
			"7919") ;;	# AGXi
			"7e19") ;;	# NX
			"7418") ;;	# TX2 4GB
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
			"7721") found="yes"; ;;	# TX1
			"7f21") found="yes"; ;;	# NANO
			"7018") found="yes"; ;;	# TX2i
			"7c18") found="yes"; ;;	# TX2
			"7019") found="yes"; ;;	# AGX
			"7819") found="yes"; ;;	# AGXi
			"7919") found="yes"; ;;	# AGXi
			"7e19") found="no"; ;;	# NX
			"7418") found="yes"; ;;	# TX2 4GB
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
	elif [ -f "${BL_DIR}/tegrarcm" ]; then
		rcmcmd="tegrarcm";
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
	ECID=$("${BL_DIR}/${rcmcmd}" ${inst_args} --uid | grep BR_CID | cut -d' ' -f2);
	popd > /dev/null 2>&1 || exit 2;
	echo "${ECID}";
}

parse_hwchipid ()
{
	local ECID="$1";

	local idval_1="";
	local idval_2="";
	local hival="";

	idval_1="0x${ECID:3:2}";
	idval_2="0x${ECID:6:2}";

	hival="${idval_1}";
	if [ "${hival}" = "0x21" ] || [ "${hival}" = "0x12" ] || \
		[ "${hival}" = "0x00" ] && [ "${hival}" = "0x21" ]; then
		if [ "${hival}" = "0x00" ]; then
			hival="${idval_2}";
		fi;
	elif [ "${hival}" = "0x80" ]; then
		if [ "${idval_2}" = "0x19" ]; then
			hival="${idval_2}";
		fi;
	fi;
	echo "${hival}";
}

parse_fuselevel ()
{
	local ECID="$1";
	local hwchipid="$2";
	local flval="";

	flval="${ECID:2:1}";
	if [ "${hwchipid}" = "0x21" ]; then
		case ${flval} in
		0|1|2)	flval="fuselevel_nofuse"; ;;
		3)	flval="fuselevel_production"; ;;
		4)	flval="fuselevel_production"; ;;
		5)	flval="fuselevel_production"; ;;
		6)	flval="fuselevel_production"; ;;
		*)	flval="fuselevel_unknown"; ;;
		esac;
	elif [ "${hwchipid}" = "0x19" ]; then
		case ${flval} in
		0|1|2)	flval="fuselevel_nofuse"; ;;
		8)	flval="fuselevel_production"; ;;
		9)	flval="fuselevel_production"; ;;
		d)	flval="fuselevel_production"; ;;
		*)	flval="fuselevel_unknown"; ;;
		esac;
	else
		case ${flval} in
		0|1|2)	flval="fuselevel_nofuse"; ;;
		8|c)	flval="fuselevel_production"; ;;
		9|d)	flval="fuselevel_production"; ;;
		a)	flval="fuselevel_production"; ;;
		e)	flval="fuselevel_production"; ;;
		*)	flval="fuselevel_unknown"; ;;
		esac;
	fi;
	echo "${flval}";
}

parse_bootauth ()
{
	local ECID="$1";
	local hwchipid="$2";

	local idval_1="";
	local idval_2="";
	local flval="";
	local baval="";

	flval="${ECID:2:1}";
	if [ "${hwchipid}" = "0x21" ]; then
		case ${flval} in
		4) baval="NS"; ;;
		5) baval="SBK"; ;;
		6) baval="PKC"; ;;
		esac;
	elif [ "${hwchipid}" = "0x19" ]; then
		case ${flval} in
		8)     flval="fuselevel_production"; baval="NS"; ;;
		# 9: 2K RSA, a: 3K RSA
		9|a)   flval="fuselevel_production"; baval="PKC"; ;;
		# d: 2K RSA+SBK, e: 3K RSA+SBK
		d|e)     flval="fuselevel_production"; baval="SBKPKC"; ;;
		esac;
	else
		case ${flval} in
		8|c)	baval="NS"; ;;
		9|d)	baval="SBK"; ;;
		a)	baval="PKC"; ;;
		e)	baval="SBKPKC"; ;;
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
# After "reboot recovery", ECID read has to preceed any RCM operation.
#
read_eeprom ()
{
	local args="";
	local tegraid=$1;
	local cvmcmd="dump eeprom boardinfo ${cvmname}";
	local bbdcmd="dump eeprom baseinfo ${bbdname}";
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

	if [ "${read_baseboard_eeprom}" = "yes" ]; then
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

	if [ "${tegraid}" = "0x19" ]; then
		args+="--applet \"${BL_DIR}/mb1_t194_prod.bin\" ";
		local soft_fusesname="tegra194-mb1-soft-fuses-l4t.cfg";
		cp -f "${TARGET_DIR}/BCT/${soft_fusesname}" "${BL_DIR}";
		chkerr "$?" "Copying ${soft_fusesname}";
		args+="--soft_fuses ${soft_fusesname} "
		args+="--bins \"mb2_applet nvtboot_applet_t194.bin\" ";
		command+="; reboot recovery";
	elif [ "${tegraid}" = "0x18" ]; then
		args+="--applet \"${BL_DIR}/mb1_recovery_prod.bin\" ";
		command+="; reboot recovery";
	else
		args+="--applet \"${BL_DIR}/nvtboot_recovery.bin\" ";
	fi;
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
	echo "${cmd}";
	eval "${cmd}";
	chkerr "$?" "Reading board information";

	popd > /dev/null 2>&1 || exit 2;
}

#
#                                     BOARDID  BOARDSKU  FAB  BOARDREV
#    --------------------------------+--------+---------+----+---------
#    jetson-agx-xavier-industrial     2888     0008      600  A.0
#    jetson-xavier-nx-devkit-tx2-nx   3636     0001      100  B.0
#    clara-agx-xavier-devkit"         3900     0000      001  C.0
#    jetson-xavier-nx-devkit          3668     0000      100  N/A
#    jetson-xavier-nx-devkit-emmc     3668     0001      100  N/A
#    jetson-xavier-nx-devkit-emmc     3668     0003      N/A  N/A
#    jetson-nano-devkit               3448     0000      200  N/A
#    jetson-nano-devkit-emmc          3448     0002      400  N/A
#    jetson-nano-2gb-devkit           3448     0003      300  N/A
#    jetson-agx-xavier-devkit (16GB)  2888     0001      400  H.0
#    jetson-agx-xavier-devkit (32GB)  2888     0004      400  K.0
#    jetson-agx-xavier-devkit (64GB)  2888     0005      402  B.0
#    jetson-tx2-devkit                3310     1000      B02  N/A
#    jetson-tx2-devkit-tx2i           3489     0000      300  A.0
#    jetson-tx2-devkit-4gb            3489     0888      300  F.0
#    jetson-tx1-devkit                2180     0000      400  N/A
#    --------------------------------+--------+---------+----+---------
#
cfgtab=(\
	"jetson-agx-xavier-industrial"		"mmcblk0p1"	"2888"	"0008" \
	"jetson-xavier-nx-devkit-tx2-nx"	"mmcblk0p1"	"3636"	"0001" \
	"clara-agx-xavier-devkit"		"mmcblk0p1"	"3900"	"0000" \
	"jetson-xavier-nx-devkit"		"mmcblk0p1"	"3668"	"0000" \
	"jetson-xavier-nx-devkit-emmc"		"mmcblk0p1"	"3668"	"0001" \
	"jetson-xavier-nx-devkit-emmc"		"mmcblk0p1"	"3668"	"0003" \
	"jetson-nano-devkit"			"mmcblk0p1"	"3448"	"0000" \
	"jetson-nano-devkit-emmc"		"mmcblk0p1"	"3448"	"0002" \
	"jetson-nano-2gb-devkit"		"mmcblk0p1"	"3448"	"0003" \
	"jetson-agx-xavier-devkit"		"mmcblk0p1"	"2888"	"0001" \
	"jetson-agx-xavier-devkit"		"mmcblk0p1"	"2888"	"0004" \
	"jetson-agx-xavier-devkit"		"mmcblk0p1"	"2888"	"0005" \
	"jetson-tx2-devkit"			"mmcblk0p1"	"3310"	"1000" \
	"jetson-tx2-devkit-tx2i"		"mmcblk0p1"	"3489"	"0000" \
	"jetson-tx2-devkit-4gb"			"mmcblk0p1"	"3489"	"0888" \
	"jetson-tx1-devkit"			"mmcblk0p1"	"2180"	"1000" \
);

findconf ()
{
	local BOARDID="$1";
	local BOARDSKU="$2";
	local i;
	for (( i=0; i<${#cfgtab[@]}; )); do
		if [ "${BOARDID}" = "${cfgtab[$((i + 2))]}" ] && \
			[ "${BOARDSKU}" = "${cfgtab[$((i + 3))]}" ]; then
			echo "${cfgtab[$i]}";
			return;
		fi;
		i=$((i + 4));
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
		i=$((i + 4));
	done;
}

parse_eeprom ()
{
	local exefile="$1";
	local infofile="$2";

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

if [ "${hwchipid}" = "0x19" ]; then
	TARGET_DIR="${BL_DIR}/t186ref";
elif [ "${hwchipid}" = "0x18" ]; then
	TARGET_DIR="${BL_DIR}/t186ref";
elif [ "${hwchipid}" = "0x21" ]; then
	TARGET_DIR="${BL_DIR}/t210ref";
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
if [ -f "${BL_DIR}/${bbdname}" ]; then
	echo "Parsing baseboard EEPROM:";
	parse_eeprom "${BL_DIR}/chkbdinfo" "${BL_DIR}/${bbdname}";
	target_board=$(findconf "${BOARDID}" "${BOARDSKU}");
fi;
if [ -z "${target_board}" ] && [ -f "${BL_DIR}/${cvmname}" ]; then
	echo "Parsing module EEPROM:";
	parse_eeprom "${BL_DIR}/chkbdinfo" "${BL_DIR}/${cvmname}";
	target_board=$(findconf "${BOARDID}" "${BOARDSKU}");
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
cmd="\"${curdir}/flash.sh\" ${arguments} ${target_board} ${bootdev}";
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
