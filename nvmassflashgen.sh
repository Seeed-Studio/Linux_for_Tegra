#!/bin/bash

# Copyright (c) 2018-2022, NVIDIA CORPORATION.  All rights reserved.
#
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

hdrsize=$((LINENO - 2));
hdrtxt=`head -n ${hdrsize} $0`;
fdlock=200;
set -o pipefail;
set -o errtrace;
shopt -s extglob;
curdir=$(cd `dirname $0` && pwd);
nargs=$#;
nargs=$(($nargs-1));
ext_target_board=${!nargs};
if [ ! -r ${ext_target_board}.conf ]; then
	echo "Error: Invalid target board - ${ext_target_board}." >&2;
	exit 3;
fi
LDK_DIR=`readlink -f "${curdir}"`;
KERNEL_DIR="${LDK_DIR}/kernel";
export PATH="${KERNEL_DIR}:${PATH}";	# preference on our DTC
source ${ext_target_board}.conf;

BLDIR="bootloader";
FLASHCMD="flashcmd.txt";
MFGENCMD="mfgencmd.txt";
mfidir="mfi_${ext_target_board}";

gen_aflash_sh_v1()
{
	local aflash_txt=`cat << EOF

usbarg="--instance \\\$1";
cidarg="--chip AFARG_CHIPID";
rcmarg="--rcm AFARG_RCMARG";
bctarg="--bct AFARG_BCTARG";
dlbctarg="--download bct AFARG_DLBCTARG";
wrbctarg="--write BCT AFARG_WRBCTARG";
ebtarg="--download ebt AFARG_EBTARG 0 0";	# type file [loadaddr entry]
rp1arg="--download rp1 AFARG_RP1ARG 0 0";	# type file [loadaddr entry]
pttarg="--pt AFARG_PTTARG.bin";
bfsarg="--updatebfsinfo AFARG_PTTARG.bin";
storagefile="\\\$\\\$_storage_info.bin";

chkerr()
{
	if [ \\\$? -ne 0 ]; then
		echo "*** Error: \\\$1 failed." >&2;
		rm -f \\\${storagefile};
		exit 1;
	fi;
	echo "*** \\\$1 succeeded.";
}

execmd ()
{
	local banner="\\\$1";
	local cmd="\\\$2";
	local nochk="\\\$3";

	echo; echo "*** \\\${banner}";
	echo "\\\${cmd}";
	if [ "\\\${nochk}" != "" ]; then
		\\\${cmd};
		return;
	fi;
	\\\${cmd};
	chkerr "\\\${banner}";
}

curdir=\\\$(cd \\\`dirname \\\$0\\\` && pwd);
pushd \\\${curdir} > /dev/null 2>&1;

banner="Updating BFS information on BCT";
cmd="\\\${curdir}/tegrabct \\\${bctarg} \\\${cidarg} \\\${bfsarg}";
execmd "\\\${banner}" "\\\${cmd}";

banner="Boot Rom communication";
cmd="\\\${curdir}/tegrarcm \\\${usbarg} \\\${cidarg} \\\${rcmarg}";
execmd "\\\${banner}" "\\\${cmd}";

banner="Sending BCTs";
cmd="\\\${curdir}/tegrarcm \\\${usbarg} \\\${dlbctarg}";
execmd "\\\${banner}" "\\\${cmd}";

banner="Sending bootloader and pre-requisite binaries";
cmd="\\\${curdir}/tegrarcm \\\${usbarg} \\\${ebtarg} \\\${rp1arg}";
execmd "\\\${banner}" "\\\${cmd}";

banner="Booting Recovery";
cmd="\\\${curdir}/tegrarcm \\\${usbarg} --boot recovery";
execmd "\\\${banner}" "\\\${cmd}";

banner="Retrieving storage infomation";
cmd="\\\${curdir}/tegradevflash \\\${usbarg} ";
cmd+="--oem platformdetails storage \\\${storagefile}";
execmd "\\\${banner}" "\\\${cmd}";

banner="Flashing the device";
cmd="\\\${curdir}/tegradevflash \\\${usbarg} \\\${pttarg} ";
cmd+="--storageinfo \\\${storagefile} --create";
execmd "\\\${banner}" "\\\${cmd}";

banner="Writing the BCT";
cmd="\\\${curdir}/tegradevflash \\\${usbarg} \\\${wrbctarg}";
execmd "\\\${banner}" "\\\${cmd}";

banner="Rebooting the device";
cmd="\\\${curdir}/tegradevflash \\\${usbarg} --reboot coldboot";
execmd "\\\${banner}" "\\\${cmd}";

rm -f \\\${storagefile};

popd > /dev/null 2>&1;
exit 0;
EOF`;
	echo "${hdrtxt}" > nvaflash.sh;
	echo "${aflash_txt}" >> nvaflash.sh;
	chmod +x nvaflash.sh;

	local conv="";
	conv+="-e s/AFARG_RCMARG/${rcmarg}/g ";
	conv+="-e s/AFARG_BCTARG/${bctarg}/g ";
	conv+="-e s/AFARG_DLBCTARG/${dlbctarg}/g ";
	conv+="-e s/AFARG_WRBCTARG/${wrbctarg}/g ";
	conv+="-e s/AFARG_EBTARG/${ebtarg}/g ";
	conv+="-e s/AFARG_RP1ARG/${rp1arg}/g ";
	conv+="-e s/AFARG_PTTARG/${pttarg}/g ";
	sed -i ${conv} nvaflash.sh;
	if [ $? -ne 0 ]; then
		echo "Error: Setting up nvaflash.sh" >&2;
		exit 2;
	fi;
	fc=`cat nvaflash.sh | sed -e s/AFARG_CHIPID/"${cidarg}"/g`;
	echo "${fc}" > nvaflash.sh;
}

gen_aflash_sh_v2()
{
	local aflash_txt=`cat << EOF

usbarg="--instance \\\$1";
cidarg="--chip AFARG_CHIPID";
rcmarg="--rcm AFARG_RCMARG";
rcmsfarg="AFARG_RCMSFARG";
pttarg="--pt AFARG_PTTARG.bin";
storagefile="storage_info.bin";

dlbrbctarg="--download bct_bootrom AFARG_DLBRBCTARG";
wrbrbctarg="--write BCT AFARG_WRBRBCTARG";

mb1bctarg="AFARG_DLMB1BCTARG";
dlmb1bctarg="--download bct_mb1 AFARG_DLMB1BCTARG";
wrmb1bctarg="--write MB1_BCT AFARG_WRMB1BCTARG";
wrmb1bctbarg="--write MB1_BCT_b AFARG_WRMB1BCTARG";

membctarg="AFARG_MEMBCTARG";
dlmembctarg="--download bct_mem AFARG_DLMEMBCTARG";
wrmembctarg="--write MEM_BCT AFARG_WRMEMBCTARG";
wrmembctbarg="--write MEM_BCT_b AFARG_WRMEMBCTARG";

memcbctarg="AFARG_MEMCBCTARG";

chkerr()
{
	if [ \\\$? -ne 0 ]; then
		echo "*** Error: \\\$1 failed." >&2;
		exit 1;
	fi;
	echo "*** \\\$1 succeeded.";
}

execmd ()
{
	local banner="\\\$1";
	local cmd="\\\$2";
	local nochk="\\\$3";

	echo; echo "*** \\\${banner}";
	echo "\\\${cmd}";
	if [ "\\\${nochk}" != "" ]; then
		\\\${cmd};
		return;
	fi;
	\\\${cmd};
	chkerr "\\\${banner}";
}

curdir=\\\$(cd \\\`dirname \\\$0\\\` && pwd);
pushd \\\${curdir} > /dev/null 2>&1;

banner="Boot Rom communication";
cmd="\\\${curdir}/tegrarcm_v2 \\\${usbarg} \\\${cidarg} ";
if [ "\\\${rcmsfarg}" != "" ]; then
	cmd+="--rcm \\\${rcmsfarg} ";
fi;
cmd+="\\\${rcmarg} ";
execmd "\\\${banner}" "\\\${cmd}";

# SBK+PKC has the worst BL state transition delay up to 2 sec
sleep 2;

banner="Checking applet";
cmd="\\\${curdir}/tegrarcm_v2 \\\${usbarg} --isapplet";
execmd "\\\${banner}" "\\\${cmd}";

banner="Sending BCTs";
cmd="\\\${curdir}/tegrarcm_v2 \\\${usbarg} \\\${dlbrbctarg} ";
if [ "\\\${mb1bctarg}" != "" ]; then
	cmd+="\\\${dlmb1bctarg} ";
fi;
if [ "\\\${membctarg}" != "" ]; then
	cmd+="\\\${dlmembctarg}";
fi;
execmd "\\\${banner}" "\\\${cmd}";

banner="Sending bootloader and pre-requisite binaries";
cmd="\\\${curdir}/tegrarcm_v2 \\\${usbarg} --download blob blob.bin";
execmd "\\\${banner}" "\\\${cmd}";

banner="Booting Recovery";
cmd="\\\${curdir}/tegrarcm_v2 \\\${usbarg} --boot recovery";
execmd "\\\${banner}" "\\\${cmd}";

banner="Checking applet";
cmd="\\\${curdir}/tegrarcm_v2 \\\${usbarg} --isapplet";
execmd "\\\${banner}" "\\\${cmd}" "1";

sleep 3;
banner="Checking CPU bootloader";
cmd="\\\${curdir}/tegradevflash_v2 \\\${usbarg} --iscpubl";
execmd "\\\${banner}" "\\\${cmd}";

exec ${fdlock}>aflash.lock;
flock ${fdlock};
if [ \\\$(ls -1 mbr_* gpt_* 2>&1 | wc -l) != "4" ]; then
	banner="Retrieving storage infomation";
	cmd="\\\${curdir}/tegradevflash_v2 \\\${usbarg} ";
	cmd+="--oem platformdetails storage \\\${storagefile}";
	execmd "\\\${banner}" "\\\${cmd}";

	banner="Generating GPT";
	cmd="\\\${curdir}/tegraparser_v2 --storageinfo \\\${storagefile} ";
	cmd+="--generategpt \\\${pttarg}";
	execmd "\\\${banner}" "\\\${cmd}";
fi;
flock -u ${fdlock};

banner="Flashing the device";
cmd="\\\${curdir}/tegradevflash_v2 \\\${usbarg} \\\${pttarg} --create";
execmd "\\\${banner}" "\\\${cmd}";

banner="Writing the BR BCT";
cmd="\\\${curdir}/tegradevflash_v2 \\\${usbarg} \\\${wrbrbctarg}";
execmd "\\\${banner}" "\\\${cmd}";


if [ "\\\${mb1bctarg}" != "" ]; then
	banner="Writing coldboot MB1 BCT";
	cmd="\\\${curdir}/tegradevflash_v2 \\\${usbarg} \\\${wrmb1bctarg}";
	execmd "\\\${banner}" "\\\${cmd}";

	banner="Writing coldboot MB1_B BCT";
	cmd="\\\${curdir}/tegradevflash_v2 \\\${usbarg} \\\${wrmb1bctbarg}";
	execmd "\\\${banner}" "\\\${cmd}";
fi;

if [ "\\\${membctarg}" != "" ]; then
	banner="Writing the MEM BCT";
	cmd="\\\${curdir}/tegradevflash_v2 \\\${usbarg} \\\${wrmembctarg}";
	execmd "\\\${banner}" "\\\${cmd}";

	banner="Writing the MEM BCT B";
	cmd="\\\${curdir}/tegradevflash_v2 \\\${usbarg} \\\${wrmembctbarg}";
	execmd "\\\${banner}" "\\\${cmd}";
fi;

if [ "\\\${POWEROFF}" -eq 1 ]; then
	banner="Poweroff the device";
	cmd="\\\${curdir}/tegradevflash_v2 \\\${usbarg} --reboot poweroff";
else
	banner="Rebooting the device";
	cmd="\\\${curdir}/tegradevflash_v2 \\\${usbarg} --reboot coldboot";
fi
execmd "\\\${banner}" "\\\${cmd}";

popd > /dev/null 2>&1;
exit 0;
EOF`;
	echo "${hdrtxt}" > nvaflash.sh;
	echo "${aflash_txt}" >> nvaflash.sh;
	chmod +x nvaflash.sh;

	local conv="";
	conv+="-e s/AFARG_RCMSFARG/${rcmsfarg}/g ";
	conv+="-e s/AFARG_RCMARG/${rcmarg}/g ";
	conv+="-e s/AFARG_PTTARG/${pttarg}/g ";

	conv+="-e s/AFARG_DLBRBCTARG/${dlbrbctarg}/g ";
	conv+="-e s/AFARG_WRBRBCTARG/${dlbrbctarg}/g ";

	conv+="-e s/AFARG_DLMB1BCTARG/${dlmb1bctarg}/g ";
	conv+="-e s/AFARG_WRMB1BCTARG/${wrmb1bctarg}/g ";

	conv+="-e s/AFARG_MEMBCTARG/${membctarg}/g ";
	conv+="-e s/AFARG_DLMEMBCTARG/${dlmembctarg}/g ";
	conv+="-e s/AFARG_WRMEMBCTARG/${wrmembctarg}/g ";
	conv+="-e s/AFARG_MEMCBCTARG/${memcbctarg}/g ";

	sed -i ${conv} nvaflash.sh;
	if [ $? -ne 0 ]; then
		echo "Error: Setting up nvaflash.sh" >&2;
		exit 4;
	fi;
	fc=`cat nvaflash.sh | sed -e s/AFARG_CHIPID/"${cidarg}"/g`;
	echo "${fc}" > nvaflash.sh;
}

gen_aflash_sh()
{
	local tfv=$1;
	case ${tfv} in
	1) gen_aflash_sh_v1; ;;
	2) gen_aflash_sh_v2; ;;
	*) echo "Error: Unknown tegraflash version" >&2; exit 5; ;;
	esac;
}

gen_mflash_sh()
{
	local mflash_txt=`cat << EOF

curdir=\\\$(cd \\\`dirname \\\$0\\\` && pwd);
showlogs=0;
if [ "\\\$1" = "--showlogs" ]; then
	showlogs=1;
fi;

# Find devices to flash
devpaths=(\\\$(find /sys/bus/usb/devices/usb*/ \\\\
		-name devnum -print0 | {
	found=()
	while read -r -d "" fn_devnum; do
		dir="\\\$(dirname "\\\${fn_devnum}")"
		vendor="\\\$(cat "\\\${dir}/idVendor")"
		if [ "\\\${vendor}" != "0955" ]; then
			continue
		fi
		product="\\\$(cat "\\\${dir}/idProduct")"
		case "\\\${product}" in
		"7721") ;;
		"7f21") ;;
		"7018") ;;
		"7c18") ;;
		"7121") ;;
		"7019") ;;
		"7819") ;;
		"7919") ;;
		"7e19") ;;
		"7418") ;;
		*)
			continue
			;;
		esac
		fn_busnum="\\\${dir}/busnum"
		if [ ! -f "\\\${fn_busnum}" ]; then
			continue
		fi
		fn_devpath="\\\${dir}/devpath"
		if [ ! -f "\\\${fn_devpath}" ]; then
			continue
		fi
		busnum="\\\$(cat "\\\${fn_busnum}")"
		devpath="\\\$(cat "\\\${fn_devpath}")"
		found+=("\\\${busnum}-\\\${devpath}")
	done
	echo "\\\${found[@]}"
}))

# Exit if no devices to flash
if [ \\\${#devpaths[@]} -eq 0 ]; then
	echo "No devices to flash" >&2
	exit 6
fi

# Create a folder for saving log
mkdir -p mfilogs;
pid="\\\$\\\$"
ts=\\\`date +%Y%m%d-%H%M%S\\\`;

# Clear old gpt crufts
rm -f mbr_* gpt_*;

# Flash all devices in background
flash_pids=()
for devpath in "\\\${devpaths[@]}"; do
	fn_log="mfilogs/\\\${ts}_\\\${pid}_flash_\\\${devpath}.log"
	cmd="\\\${curdir}/nvaflash.sh \\\${devpath}";
	\\\${cmd} > "\\\${fn_log}" 2>&1 &
	flash_pid="\\\$!";
	flash_pids+=("\\\${flash_pid}")
	echo "Start flashing device: \\\${devpath}, PID: \\\${flash_pid}";
	if [ \\\${showlogs} -eq 1 ]; then
		gnome-terminal -e "tail -f \\\${fn_log}" -t \\\${fn_log} > /dev/null 2>&1 &
	fi;
done

# Wait until all flash processes done
failure=0
while true; do
	running=0
	if [ \\\${showlogs} -ne 1 ]; then
		echo -n "Ongoing processes:"
	fi;
	new_flash_pids=()
	for flash_pid in "\\\${flash_pids[@]}"; do
		if [ -e "/proc/\\\${flash_pid}" ]; then
			if [ \\\${showlogs} -ne 1 ]; then
				echo -n " \\\${flash_pid}"
			fi;
			running=\\\$((\\\${running} + 1))
			new_flash_pids+=("\\\${flash_pid}")
		else
			wait "\\\${flash_pid}" || failure=1
		fi
	done
	if [ \\\${showlogs} -ne 1 ]; then
		echo
	fi;
	if [ \\\${running} -eq 0 ]; then
		break
	fi
	flash_pids=("\\\${new_flash_pids[@]}")
	sleep 5
done

if [ \\\${failure} -ne 0 ]; then
	echo "Flash complete (WITH FAILURES)" >&2;
	exit 7
fi

echo "Flash complete (SUCCESS)"
EOF`;
	echo "${hdrtxt}" > nvmflash.sh;
	echo "${mflash_txt}" >> nvmflash.sh;
	chmod +x nvmflash.sh;
}

getidx()
{
	local i;
	local f="$1";
	local s="$2";
	shift; shift;
	local a=($@);

	for (( i=0; i<${#a[@]}; i++ )); do
		if [ "$f" != "${a[$i]}" ]; then
			continue;
		fi;
		i=$(( i+1 ));
		if [ "${s}" != "" ]; then
			if [ "$s" != ${a[$i]} ]; then
				continue;
			fi;
			i=$(( i+1 ));
		fi;
		return $i;
	done;
	echo "Error: $f $s not found" >&2;
	exit 8;
}

chkidx()
{
	local i;
	local f="$1";
	shift;
	local a=($@);

	for (( i=0; i<${#a[@]}; i++ )); do
		if [ "$f" != "${a[$i]}" ]; then
			continue;
		fi;
		return 0;
	done;
	return 1;
}

chext ()
{
	local i;
	local var="$1";
	local fname=`basename "$2"`;
	local OIFS=${IFS};
	IFS='.';
	na=($fname);
	IFS=${OIFS};
	local nasize=${#na[@]};
	if [ $nasize -lt 2 ]; then
		echo "Error: invalid file name: ${fname}" >&2;
		exit 9;
	fi;
	na[$((nasize-1))]=${3};
	local newname="";
	for (( i=0; i < ${nasize}; i++ )); do
		newname+="${na[$i]}";
		if [ $i -lt $((nasize-1)) ]; then
			newname+=".";
		fi;
	done;
	eval "${var}=${newname}";
}

gen_param()
{
	local tf_v=1;
	local len;
	local binsidx;
	local a=`echo "$1" | sed -e s/\"//g -e s/\;//g`;
	local OIFS=${IFS};
	IFS=' ';
	a=($a);
	IFS=$OIFS;

	blobxmltxt="";
	getidx "--bl" "" "${a[@]}";
	ebtarg="${a[$?]}";

	getidx "--chip" "" "${a[@]}";
	cidarg="${a[$?]}";
	len=$(expr length "${cidarg}");
	if [ ${len} -le 4 ]; then
		tgid="${cidarg}";
		if [ "${CHIPMAJOR}" != "" ]; then
			cidarg="${cidarg} ${CHIPMAJOR}";
		else
			local addchipmajor="true";
			local relfile="${LDK_DIR}/rootfs/etc/nv_tegra_release";
			if [ -f "${relfile}" ]; then
				rel=`head -n 1 "${relfile}"`;
				rel=`echo "${rel}" | awk -F ' ' '{print $2}'`;
				if [ "${rel}" \< "R32" ]; then
					addchipmajor="false";
				fi;
			fi;
			if [ "${addchipmajor}" = "true" ]; then
				cidarg="${cidarg} 0";
			fi;
		fi;
	else
		tgid=`echo "${cidarg}" | awk -F ' ' '{print $1}'`;
	fi;
	if [ "${tgid}" != "0x21" ]; then
		tf_v=2;
	fi;

	getidx "--applet" "" "${a[@]}";
	rcmarg="${a[$?]}";
	if [ "${rcmarg}" = "nvtboot_recovery.bin" -o \
		"${rcmarg}" = "mb1_recovery_prod.bin" -o \
		"${rcmarg}" = "mb1_t194_prod.bin" ]; then
		rcmarg="rcm_list_signed.xml";
	fi;

	getidx "--cfg" "" "${a[@]}";
	pttarg="${a[$?]}";

	chkidx "--bins" "${a[@]}";
	if [ $? -eq 0 ]; then
		getidx "--bins" "" "${a[@]}";
		binsidx=$?;
	else
		chkidx "--bin" "${a[@]}";
		if [ $? -eq 0 ]; then
			getidx "--bin" "" "${a[@]}";
			binsidx=$?;
		fi;
	fi;

	if [ ${tf_v} -eq 1 ]; then
		# Tegraflash V1
		# BCT params
		getidx "--bct" "" "${a[@]}";
		local bctfilename="${a[$?]}";
		chext bctfilename ${bctfilename} "bct";
		bctarg="${bctfilename}";
		dlbctarg="${bctfilename}";
		wrbctarg="${bctfilename}";

		# BIN params
		getidx "--bldtb" "" "${a[@]}";
		rp1arg="${a[$?]}";
		bfsarg="${pttarg}";
		return ${tf_v};
	fi;

	# Tegraflash V2
	# BCT params
	chkidx "--bct" "${a[@]}";
	if [ $? -eq 0 ]; then
		getidx "--bct" "" "${a[@]}";
		bctarg="${a[$?]}";
		dlbrbctarg="${bctarg}";
	else
		dlbrbctarg="br_bct_BR.bct";
	fi;
	wrbrbctarg="${dlbrbctarg}";

	chkidx "--applet_softfuse" "${a[@]}";
	if [ $? -eq 0 ]; then
		getidx "--applet_softfuse" "" "${a[@]}";
		rcmsfarg="${a[$?]}";
	fi;

	chkidx "--mb1_bct" "${a[@]}";
	if [ $? -eq 0 ]; then
		getidx "--mb1_bct" "" "${a[@]}";
		mb1bctarg="${a[$?]}";
		dlmb1bctarg="${mb1bctarg}";
	else
		dlmb1bctarg="mb1_bct_MB1_sigheader.bct";
		chkidx "--key" "${a[@]}";
		if [ $? -eq 0 ]; then
			dlmb1bctarg+=".signed";
		else
			dlmb1bctarg+=".encrypt";
		fi;
	fi;

	chkidx "--mb1_cold_boot_bct" "${a[@]}";
	if [ $? -eq 0 ]; then
		getidx "--mb1_cold_boot_bct" "" "${a[@]}";
		mb1cbctarg="${a[$?]}";
		wrmb1bctarg="${mb1cbctarg}";
	else
		wrmb1bctarg="mb1_cold_boot_bct_MB1_sigheader.bct";
		getidx "--cmd" "" "${a[@]}";
		if [[ ${a[$?]} =~ secureflash ]]; then
			if [ "${sbkpkc}" = "true" ]; then
				wrmb1bctarg+=".encrypt";
			fi;
			wrmb1bctarg+=".signed";
		else
			wrmb1bctarg+=".encrypt";
		fi;
	fi;

	if [ "${tgid}" = "0x19" ]; then
		chkidx "--mem_bct" "${a[@]}";
		if [ $? -eq 0 ]; then
			getidx "--mem_bct" "" "${a[@]}";
			membctarg="${a[$?]}";
			dlmembctarg="${membctarg}";
		else
			dlmembctarg="mem_rcm_sigheader.bct";
			getidx "--cmd" "" "${a[@]}";
			if [[ ${a[$?]} =~ secureflash ]]; then
				dlmembctarg+=".signed";
			else
				dlmembctarg+=".encrypt";
			fi;
			if [ "${tgid}" = "0x19" ]; then
				membctarg="${dlmembctarg}";
			fi;
		fi;

		chkidx "--mem_bct_cold_boot" "${a[@]}";
		if [ $? -eq 0 ]; then
			getidx "--mem_bct_cold_boot" "" "${a[@]}";
			memcbctarg="${a[$?]}";
			wrmembctarg="${memcbctarg}";
		else
			wrmembctarg="mem_coldboot_sigheader.bct";
			getidx "--cmd" "" "${a[@]}";
			if [[ ${a[$?]} =~ secureflash ]]; then
				wrmembctarg+=".signed";
			else
				wrmembctarg+=".encrypt";
			fi;
		fi;
	fi;

	# BIN params
	blobxmltxt+="<file_list mode=\"blob\">";
	blobxmltxt+="<!--Auto generated by tegraflash.py-->";
	blobfiles="";
	# The first entry is EBT binary.
	if [ "${bctarg}" != "" -a "${mb1bctarg}" != "" ]; then
		blobxmltxt+="<file name=\"";
		blobxmltxt+="${ebtarg}\" ";
		blobfiles+="${ebtarg} ";
		blobxmltxt+="type=\"bootloader\" />";
		for (( i=${binsidx}; i<${#a[@]}; i++ )); do
			if [[ ${a[$i]} =~ ^\-\- ]]; then
				break;
			fi;
			blobxmltxt+="<file name=\"";
			blobxmltxt+="${a[$((i+1))]}\" ";
			blobfiles+="${a[$((i+1))]} ";
			blobxmltxt+="type=\"${a[$i]}\" />";
			i=$((i+1));
		done;
	else
		local na=`echo "${ebtarg}" | cut -d'.' -f1`;
		local suf=`echo "${ebtarg}" | cut -d'.' -f2`;
		blobxmltxt+="<file name=\"";
		blobxmltxt+="${na}_sigheader.${suf}.encrypt\" ";
		blobfiles+="${na}_sigheader.${suf}.encrypt ";
		blobxmltxt+="type=\"bootloader\" />";
		for (( i=${binsidx}; i<${#a[@]}; i++ )); do
			if [ "${a[$i]}" = "kernel" -o \
				"${a[$i]}" = "kernel_dtb" ]; then
				i=$((i+1));
				continue;
			fi;
			if [[ ${a[$i]} =~ ^\-\- ]]; then
				break;
			fi;
			na=`echo "${a[$((i+1))]}" | cut -d'.' -f1`;
			suf=`echo "${a[$((i+1))]}" | cut -d'.' -f2`;
			blobxmltxt+="<file name=\"";
			blobxmltxt+="${na}_sigheader.${suf}.encrypt\" ";
			blobfiles+="${na}_sigheader.${suf}.encrypt ";
			blobxmltxt+="type=\"${a[$i]}\" />";
			i=$((i+1));
		done;
	fi;
	blobxmltxt+="</file_list>";
	return ${tf_v};
}

clean_mfd()
{
	rm -f *.sh *.sb *.hash *.sig *.txt *.tmp *.func *.raw;
	local i;
	local lst=`ls -1`;
	local a=($lst);
	for (( i=0; i<${#a[@]}; i++ )); do
		if [ -L "${a[$i]}" ]; then
			rm -f "${a[$i]}";
			cp -f "../${a[$i]}" "${a[$i]}";
		fi;
	done;
}

findadev()
{
	local devpaths=($(find /sys/bus/usb/devices/usb*/ -name devnum -print0 | {
		local fn_devnum;
		local found=();
		while read -r -d "" fn_devnum; do
			local dir="$(dirname "${fn_devnum}")";
			local vendor="$(cat "${dir}/idVendor")";
			if [ "${vendor}" != "0955" ]; then
				continue
			fi;
			local product="$(cat "${dir}/idProduct")";
			case "${product}" in
			"7721") ;;
			"7f21") ;;
			"7018") ;;
			"7c18") ;;
			"7121") ;;
			"7019") ;;
			"7819") ;;
			"7919") ;;
			"7e19") ;;
			"7418") ;;
			*) continue ;;
			esac
			local fn_busnum="${dir}/busnum";
			if [ ! -f "${fn_busnum}" ]; then
				continue;
			fi;
			local fn_devpath="${dir}/devpath";
			if [ ! -f "${fn_devpath}" ]; then
				continue;
			fi;
			local busnum="$(cat "${fn_busnum}")";
			local devpath="$(cat "${fn_devpath}")";
			found+=("${busnum}-${devpath}");
		done;
		echo "${found[@]}";
	}))
	echo "${#devpaths[@]} Jetson devices in RCM mode. USB: ${devpaths[@]}";
	return "${#devpaths[@]}";
}

#
#                                      BOARDID  BOARDSKU  FAB  BOARDREV
#     --------------------------------+--------+---------+----+---------
#     jetson-agx-xavier-industrial     2888     0008      600  A.0
#     jetson-xavier-nx-devkit-tx2-nx   3636     0001      100  B.0
#     jetson-xavier-nx-devkit-emmc     3668     0001      100  N/A
#     jetson-nano-devkit-emmc          3448     0002      200  N/A
#     jetson-agx-xavier-devkit (16GB)  2888     0001      400  H.0
#     jetson-agx-xavier-devkit (32GB)  2888     0004      400  K.0
#     jetson-tx2-devkit                3310     1000      B02  N/A
#     jetson-tx2-devkit-tx2i           3489     0000      300  A.0
#     jetson-tx2-devkit-4gb            3489     0888      300  F.0
#     jetson-tx1-devkit                2180     0000      400  N/A
#     --------------------------------+--------+---------+----+---------
#
cfgtab=(\
	"jetson-agx-xavier-industrial"		"2888"	"0008" \
	"jetson-xavier-nx-devkit-tx2-nx"	"3636"	"0001" \
	"jetson-xavier-nx-devkit-emmc"		"3668"	"0001" \
	"jetson-nano-devkit-emmc"		"3448"	"0002" \
	"jetson-agx-xavier-devkit (16GB)"	"2888"	"0001" \
	"jetson-agx-xavier-devkit (32GB)"	"2888"	"0004" \
	"jetson-tx2-devkit"			"3310"	"1000" \
	"jetson-tx2-devkit-tx2i"		"3489"	"0000" \
	"jetson-tx2-devkit-4gb"			"3489"	"0888" \
	"jetson-tx1-devkit"			"2180"	"0000" \
);

chk_support()
{
	local BOARDID="$1";
	local BOARDSKU="$2";
	local i;
	for (( i=0; i<${#cfgtab[@]}; )); do
		if [ "${BOARDID}" = "${cfgtab[$((i + 1))]}" ] && \
			[ "${BOARDSKU}" = "${cfgtab[$((i + 2))]}" ]; then
			return;
		fi;
		i=$((i + 3));
	done;
	echo "Warning: Unofficial board configuration has been requested.";
	echo "         BOARDID = ${BOARDID}, BOARDSKU = ${BOARDSKU}";
	echo "         Will continue and give it a try.";
}

create_signdir ()
{
	local i;
	local v;
	local f;
	local tfv=$2;
	local l=(\
		"ebtarg" \
		"bctarg" \
		"rp1arg" \
		"rcmarg" \
		"pttarg" \
		);

	if [ "$1" = "" ]; then
		echo "Error: Null sign directory name." >&2;
		exit 10;
	fi;
	rm -rf "$1";
	mkdir "$1";
	pushd "$1" > /dev/null 2>&1;
	for (( i=0; i<${#l[@]}; i++ )); do
		v=${l[$i]};
		if [ ${tfv} -gt 1 -a "${!v}" = "" ]; then
			continue;
		fi;

		if [ ! -f ../${!v} ]; then
			echo "Error: ../${!v} does not exist" >&2;
			exit 11;
		fi;
		echo -n "copying ${!v} ... ";
		cp -f ../${!v} .;
		if [ $? -eq 0 ]; then
			echo "succeeded."
		else
			echo "failed."
			exit 12;
		fi;
		if [ "${v}" = "pttarg" ]; then
			if [ ${tfv} -gt 1 ]; then
				if [ ! -f "../${!v}.bin" ] || \
					[ "${sbkpkc}" = "true" ] || \
					[ "${pkc}" = "true" ]; then
					#
					# SBK/PKC still has stale file names
					# in secureflash.xml.bin. So, the binary
					# secureflash.xml.bin must be recreated
					# from secureflash.xml.
					#
					pushd .. > /dev/null 2>&1;
					cp -f ${!v} ${!v}.tmp;
					./tegraparser_v2 --pt ${!v}.tmp;
					popd > /dev/null 2>&1;
				fi;
			fi;
			echo -n "copying ${!v}.bin ... ";
			cp -f ../${!v}.bin .;
			if [ $? -eq 0 ]; then
				echo "succeeded."
			else
				echo "failed."
				exit 13;
			fi;
		fi;
	done;

	local lst=`grep "filename>" ${pttarg} | sed -e s/\<filename\>//g -e s?\</filename\>??g -e s?\</partition\>??g`;
	local a=($lst);
	for (( i=0; i<${#a[@]}; i++ )); do
		if [ -f "${a[$i]}" ]; then
			continue;
		fi;
		#
		# clear && PKC generates mbr & gpt, but SBK+PKC
		# does not generate mbr & gpt. However, both mbr & gpt
		# are re-generated by nvaflash.sh during actual flashing time.
		# So, no need to copy.
		#
		if [[ "${a[$i]}" =~ ^mbr_ ]] || [[ "${a[$i]}" =~ ^gpt_ ]]; then
			continue;
		fi;
		if [ ! -f "../${a[$i]}" ]; then
			echo "Error: ${a[$i]} does not exist." >&2;
			exit 14;
		fi;
		echo -n "copying ${a[$i]} ... ";
		cp ../${a[$i]} .;
		if [ $? -eq 0 ]; then
			echo "succeeded."
		else
			echo "failed."
			exit 15;
		fi;
	done;

	if [ "${blobfiles}" != "" ]; then
		for f in ${blobfiles} ; do
			if [ -f ${f} ]; then
				continue;
			fi;
			echo -n "copying ${f} ... ";
			cp ../${f} .;
			if [ $? -eq 0 ]; then
				echo "succeeded."
			else
				echo "failed."
				exit 16;
			fi;
		done;
	fi;

	if [ ${tfv} = 2 -a "${tgid}" = "0x19" ]; then
		local q="mem_rcm_sigheader.bct.signed";
		if [ -f ../${q} ]; then
			echo -n "copying ${q} ... ";
			cp ../${q} .;
			if [ $? -eq 0 ]; then
				echo "succeeded."
			else
				echo "failed."
				exit 17;
			fi;
		fi;
	fi;

	popd > /dev/null 2>&1;
}

fill_mfidir ()
{
	local i;
	local l;
	local tf_v1=(\
		"nvmflash.sh" \
		"nvaflash.sh" \
		"tegrarcm" \
		"tegrabct" \
		"tegradevflash" \
		);
	local tf_v2=(\
		"nvmflash.sh" \
		"nvaflash.sh" \
		"tegrarcm_v2" \
		"tegrabct_v2" \
		"tegradevflash_v2" \
		"tegrahost_v2" \
		"tegraparser_v2" \
		);
	local opt=(\
		"${rcmsfarg}" \
		"emmc_bootblob_ver.txt" \
		"qspi_bootblob_ver.txt" \
		"${mb1bctarg}" \
		"${mb1cbctarg}" \
		"${memcbctarg}" \
		"${membctarg}" \
		);
	if [ $1 -eq 2 ]; then
		l=( ${tf_v2[@]} );
	else
		l=( ${tf_v1[@]} );
	fi;

	if [ "$2" = "" ]; then
		echo "Error: Null MFI directory." >&2;
		exit 18;
	fi;
	if [ ! -d "$2" ]; then
		echo "Error: MFI directory ($2) does not exists." >&2;
		exit 19;
	fi;
	pushd "$2" > /dev/null 2>&1;

	clean_mfd;
	local lst=`ls -1`;
	local a=($lst);
	for (( i=0; i<${#a[@]}; i++ )); do
		if [ -L "${a[$i]}" ]; then
			rm -f "${a[$i]}";
			echo -n "copying ${a[$i]} ... ";
			cp -f "../${a[$i]}" .;
			if [ $? -eq 0 ]; then
				echo "succeeded."
			else
				echo "failed."
				exit 20;
			fi;
		fi;
	done;

	for (( i=0; i<${#l[@]}; i++ )); do
		if [ ! -f ${l[$i]} ]; then
			echo -n "copying ${l[$i]} ... ";
			cp -f ../${l[$i]} .;
			if [ $? -eq 0 ]; then
				echo "succeeded."
			else
				echo "failed."
				exit 21;
			fi;
		fi;
	done;

	for (( i=0; i<${#opt[@]}; i++ )); do
		if [ "${opt[$i]}" = "" ]; then
			continue;
		fi;
		if [ ! -f ${opt[$i]} -a -f ../${opt[$i]} ]; then
			echo -n "copying ${opt[$i]} ... ";
			cp -f ../${opt[$i]} .;
			if [ $? -eq 0 ]; then
				echo "succeeded."
			else
				echo "failed."
				exit 22;
			fi;
		fi;
	done;

	if [ "${blobxmltxt}" != "" ]; then
		local tid=`echo "${cidarg}" | cut -d' ' -f1`;
		echo "${blobxmltxt}" > blob.xml;
		./tegrahost_v2 --chip ${tid} --generateblob blob.xml blob.bin;
	fi;
	popd > /dev/null 2>&1;
}

if [[ "$@" =~ -u ]]; then
	pkc="true";
	if [[ "$@" =~ -v ]]; then
		sbkpkc="true";
	fi;
fi;

cat << EOF
================================================================================
|| Generate Massflash Image in the master host:                               ||
|| Requires the Jetson connected in RCM mode.                                 ||
================================================================================
EOF

ndev=0;
if [ "${BOARDID}" = "" -o "${BOARDSKU}" = "" -o \
	"${FAB}" = "" -o "${FUSELEVEL}" = "" ]; then
	findadev;
	ndev=$?;
	if [ $ndev -ne 1 ]; then
		if [ $ndev -gt 1 ]; then
			echo "*** Too many Jetson devices found." >&2;
		else
			echo "*** Error: No Jetson device found." >&2;
		fi;
		echo "Connect 1 Jetson in RCM mode and rerun $0 $*" >&2;
		exit 23;
	fi;
else
	echo "Generating massflash command without Jetson device connected:";
	echo "    BOARDID=${BOARDID} BOARDSKU=${BOARDSKU} FAB=${FAB} BOARDREV=${BOARDREV} FUSELEVEL=${FUSELEVEL}";
fi;
chk_support "${BOARDID}" "${BOARDSKU}"

cat << EOF
+-------------------------------------------------------------------------------
| Step 1: Generate Command File
+-------------------------------------------------------------------------------
EOF

rm -f ${BLDIR}/flash.xml.tmp;		# Remove old cruft
BOARDID=${BOARDID} BOARDSKU=${BOARDSKU} FAB=${FAB} BOARDREV=${BOARDREV} FUSELEVEL=${FUSELEVEL} ${curdir}/flash.sh --no-flash $@
if [ $? -ne 0 ]; then
	echo "*** ERROR: ${FLASHCMD} generation failed." >&2;
	exit 24;
fi;
if [ ! -f "${BLDIR}/${FLASHCMD}" ]; then
	echo "*** Error: command file generation failed." >&2;
	exit 25;
fi;
cmd=`cat ${BLDIR}/${FLASHCMD}`;
gen_param "${cmd}";
tfvers=$?;

pushd ${BLDIR} > /dev/null 2>&1;
if [[ ${cmd} =~ secureflash ]]; then
	cat << EOF
+-------------------------------------------------------------------------------
| Step 2: Extract Signed Binaries
+-------------------------------------------------------------------------------
EOF
	if [[ ${ebtarg} =~ encrypt.signed$ ]]; then
		mfidir="${mfidir}_encrypt_signed";
	else
		mfidir="${mfidir}_signed";
	fi;
	signdir="mfitmp";
	create_signdir "${signdir}" ${tfvers};
else
	cat << EOF
+-------------------------------------------------------------------------------
| Step 2: Sign Binaries
+-------------------------------------------------------------------------------
EOF

	cmdconv="-e s/flash\;// -e s/reboot/sign/";
	cmd=`echo "${cmd}" | sed ${cmdconv}`;

	echo "${cmd} --keep --skipuid" > ${MFGENCMD};
	cat ${MFGENCMD};
	bash ${MFGENCMD} 2>&1 | tee mfi.log;
	if [ $? -ne 0 ]; then
		echo "Error: Signing binaries failed." >&2;
		exit 27;
	fi;

	tok=`grep "Keep temporary directory" mfi.log`;
	if [ "${tok}" = "" ]; then
		echo "Error: signing binaries failed." >&2;
		exit 28;
	fi;
	signdir=`echo "${tok}" | awk -F ' ' '{print $4}'`;
	signdir=`basename ${signdir}`;
fi;

cat << EOF
+-------------------------------------------------------------------------------
| Step 3: Generate Mass-flash scripts
+-------------------------------------------------------------------------------
EOF
rm -rf ${mfidir};
mv ${signdir} ${mfidir};
gen_mflash_sh;
gen_aflash_sh ${tfvers};

cat << EOF
+-------------------------------------------------------------------------------
| Step 4: Generate Mass-flash image tarball
+-------------------------------------------------------------------------------
EOF

fill_mfidir ${tfvers} "${mfidir}";
mfitarball="${mfidir}.tbz2";
tar cvjf ../${mfitarball} ${mfidir};
popd > /dev/null 2>&1;

echo "\
********************************************************************************
*** Mass Flashing tarball ${mfitarball} is ready.
********************************************************************************
    1. Download ${mfitarball} to each flashing hosts.
    2. Untar ${mfitarball}. ( tar xvjf ${mfitarball} )
    3. cd ${mfidir}
    4. Connect Jetson boards(${ext_target_board} only) and put them in RCM mode.
    5. ./nvmflash.sh
";
