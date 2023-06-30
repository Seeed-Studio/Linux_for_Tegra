#!/bin/bash

# Copyright (c) 2015-2023, NVIDIA CORPORATION.  All rights reserved.
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

#
# odmfuse.sh: Fuse the target board.
#           odmfuse performs the best in L4T fuse environment.
#
# Usage: Place the board in recovery mode and run:
#
#       ./odmfuse.sh -i <TegraID> [options] TargetBoard
#
#       for more detail enter 'odmfuse.sh -h'
#
# Examples for Jetson AGX Xavier:
#   1. Burn fuse with PKC, SBK and KEK2 without SecurityMode:
#      ./odmfuse.sh -i 0x19 -k <Key file> -S <SBK file> --KEK2 <KEK2 file> jetson-agx-xavier-devkit
#   2. Burn fuse with PKC and KEK2 without SecurityMode:
#      ./odmfuse.sh -i 0x19 -k <Key file> --KEK2 <KEK2 file> jetson-agx-xavier-devkit
#   3. Burn fuse with PKC and SBK with SecurityMode:
#      ./odmfuse.sh -i 0x19 -p -k <Key file> -S <SBK file> jetson-agx-xavier-devkit
#

validateMaster ()
{
	if [[ "$1" =~ ^[^@]{1,}@[^@]{1,}$ ]]; then
		return 0;
	fi;
	echo "Error: dbmaster is not in <user>@<db server> format.";
	exit 1;
}

#
# Regardless of TegraID, all HEX inputs(KEK, SBK) should be in
# single Big Endian format. This routine should not only check the
# input format but also convert to LE format.
#
chkhash ()
{
	local keyname=$1;
	local keylen=$2;
	local keystr="${!keyname}";
	local resid=0;

	# 1. Check single HEX format.
	if [[ ${keystr} =~ ^0[xX] ]]; then
		keystr=${keystr:2};
	fi;
	if [ "${keylen}" = "" ]; then
		keylen=${#keystr};
	else
		keylen=$(( keylen * 2 ));
	fi;
	if [ ${keylen} -ne ${#keystr} ]; then
		echo "Error: The length of ${keystr} = ${#keystr} != ${keylen}";
		exit 1;
	fi;
	resid=$((keylen % 8));
	if [ $resid -ne 0 ]; then
		echo "Error: ${keyname} length is not modulo 32bit";
		exit 1;
	fi;

	if [[ "${keystr}" = *[^[:alnum:]]* ]]; then
		echo "Error: ${keyname} has non-alphanumeric value";
		exit 1;
	fi;

	if [[ "${keystr}" =~ [g-zG-Z] ]]; then
		echo "Error: ${keyname} is not in HEX format.";
		exit 1;
	fi;
}

#
# Convert Little Endian hashes to single Big Endian HEX.
#
convhash ()
{
	local le="";
	local be="";
	local keyname=$1;
	local keylen=$2;
	local keystr="${!keyname}";

	# 1. Consolidate multiple hash tokens into single HEX format.
	if [[ ${keystr} =~ ^0[xX] ]]; then
		le=${keystr:2};
	fi;
	local i=${#le}
	while [ $i -gt 0 ]; do
		i=$((i - 2));
		be+="${le:$i:2}"
	done;

	# 2. Set the global variable with new value.
	eval "${keyname}=0x\"${be}\"";
}

factory_overlay_gen ()
{
	local fusecmd="$1";
	local fuseconf="$2";
	local tmpdir="";
	local workdir="";
	local cmdline="";
	local opts;
	local opt;
	local optnum;
	local i;
	local j;
	local files;
	local file;
	local filenum;
	local folder;

	pushd "${BL_DIR}" >& /dev/null || exit;
	tmpdir="$(mktemp -d factory-overlay-tmpdir-XXXX)";
	if [ ! -d "${tmpdir}" ]; then
		echo "Error: Create temporary directory failed.";
		popd > /dev/null 2>&1 || exit;
		exit 1;
	fi;
	workdir="${tmpdir}/bootloader";
	mkdir "${workdir}";

	echo "*** Start preparing fuse configuration ... ";
	local fusecmdfile="fusecmd.sh"
	echo "#!/bin/bash" >  "${fusecmdfile}";
	if [ "${tid}" != "0x19" ]; then
		echo "export PATH=.:\${PATH}" >> "${fusecmdfile}";
	fi;
	echo "eval '${fusecmd}'" >> "${fusecmdfile}";
	chmod +x "${fusecmdfile}";
	mv "${fusecmdfile}" "${workdir}";

	cp -f "${fuseconf}" "${workdir}";
	rm -f ./*.raw;

	if [ "${tid}" = "0x23" ] || [ "${tid}" = "0x19" ]; then
		cp tegrabct_v2 "${workdir}";
		cp tegradevflash_v2 "${workdir}";
		cp tegrahost_v2 "${workdir}";
		cp tegraparser_v2 "${workdir}";
		cp tegrarcm_v2 "${workdir}";
		cp tegrasign_v3*.py "${workdir}";
		cp tegraopenssl "${workdir}";
		if [ "${tid}" = "0x19" ]; then
			cp sw_memcfg_overlay.pl "${workdir}";
		else
			cp "${KERNEL_DIR}/dtc" "${workdir}";
		fi;

		# Parsing the command line of tegraflash.py, to get all files that tegraflash.py and
		# tegraflash_internal.py needs so copy them to the working directory.
		cmdline=$(echo "${fusecmd}" | sed -e s/\;/\ /g -e s/\"//g);
		cmdline=$(echo "${cmdline}" | sed -e s/\ \ */\ /g);
		opts=($cmdline);
		optnum=${#opts[@]};
		for (( i=0; i < optnum; )); do
			opt="${opts[$i]}";
			opt=$(echo "${opt}" | sed -e s/\,/\ /g);
			files=($opt);
			filenum=${#files[@]};
			for (( j=0; j < filenum; )); do
				file="${files[$j]}";
				if [ -f "${file}" ]; then
					folder=$(dirname "${file}");
					if [ "${folder}" != "." ]; then
						mkdir -p "${workdir}/${folder}";
					fi;
					cp "${file}" "${workdir}/${folder}";
				fi;
				j=$((j+1));
			done;
			i=$((i+1));
		done;
		cp -r -t "${workdir}" ./*.py pyfdt/ ./*.h ./*.dtsi ./*.dtb  ./*.dts
		# If the flash.xml appears in the cmdline, that means we need not only files listed
		# in the cmdline, but also files in flash.xml. So parsing the flash.xml here.
		if [ -f "${workdir}/flash.xml" ]; then
			sed -i $'s/\t/    /g' "${workdir}/flash.xml";
			opt=$(sed -n /^\ *\<filename/p "${workdir}"/flash.xml|sed -e s/^\ *//g -e s/\ \ */\ /g|cut -d" " -f 2);
			# Note: using "${files}" in the for loop is wrong because the double quotation marks
			# make the ${files} a single string with carriage returns
			files=($opt);
			for (( i=0; i<${#files[@]}; i++ )); do
				file="${files[$i]}";
				if [ "${file}" = "bmp.blob" ] || [ "${file}" = "system.img" ] ||
				   [ "${file}" = "slot_metadata.bin" ]; then
					continue;
				fi;
				if [ ! -f "${workdir}/${file}" ] && [ -f "${file}" ]; then
					cp "${file}" "${workdir}";
				fi;
			done;
		fi;
	else
		echo "Error: Not supported yet.";
		popd > /dev/null 2>&1 || exit;
		rm -rf "${tmpdir}";
		exit 1;
	fi;

	pushd "${tmpdir}" >& /dev/null || exit;
	tar cjf fuseblob.tbz2 bootloader;
	mv fuseblob.tbz2 "${LDK_DIR}";
	popd > /dev/null 2>&1 || exit;

	rm -rf "${tmpdir}";
	popd > /dev/null 2>&1 || exit;
	echo "*** done.";
}

usage ()
{
	cat << EOF
Usage:
  ./odmfuse.sh -i <TegraID> [options] TargetBoard

  Where options are,
    -c <CryptoType> ---------- Set the crypto type(obsolete). Please use "--auth" instead.
    -d <0xXXXX> -------------- sets sec_boot_dev_cfg=0xXXXX&0x3fff.
    -i <TegraID> ------------- tegra ID: 0x23-Orin, 0x19-Xavier
    -j ----------------------- Keep jtag enabled (obsolete). Jtag by default is enabled.
                               Jtag can be disabled by using "--disable-jtag" option.
                               Jtag can't be re-enabled once the jtag disable fuse bit is burned.
    -k <KeyFile> ------------- 2048 bit RSA private KEY file for Xavier.
                               3072 bit RSA private KEY file for Orin.
    -l <0xXXX> --------------- sets odm_lock=0xX. (4 bits)
    -p ----------------------- sets production mode.
    -r <0xXX> ---------------- sets sw_reserved=0xXX.
    -S <SBK file> ------------ 128bit Secure Boot Key file in HEX format.
    -X <fuse XML file> ------- fuse configuration XML file to burn.
                               For detail, refer to "Secureboot" section of
                               L4T BSP documentation.
    --auth ------------------- Set the current authentication type of the board. Possible values:
                               NS -- No authentication, PKC - PKC is enabled, SBKPKC - SBK and PKC are enabled.
                               This option is only needed in offline mode, namely option "--noburn" is set.
    --noburn ----------------- Prepare fuse blob without actual burning.
    --test ------------------- No fuses will be really burned, for test purpose.
    --force ------------------ For fuses that have been burned, force to burn it again.
    --disable-jtag ----------- Burn the jtag-disable fuse. You can't re-enable it after it is burned.
    --KEK0 <Key file> -------- 128bit Key Encryption Key file in HEX format.
    --KEK1 <Key file> -------- 128bit Key Encryption Key file in HEX format.
    --KEK2 <Key file> -------- 128bit Key Encryption Key file in HEX format.
    --KEK256 <Key file> ------ 256bit Key Encryption Key file in HEX format.
    --odm_reserved[0-7] ------ sets 32bit ReservedOdm[0-7]. (Input=0xXXXXXXXX)
    --debug_authentication --- Set arm_debug_authentication=0xXX&0x1f. (5 bits)
    --odm_id ----------------- Set odm_id=0xXXXXXXXXXXXXXXXX. (64 bits)
                               High 32 bits: odm_id[0]. Low 32 bits: odm_id[1].
EOF
}

check_ctype ()
{
	if [ "${__ctype}" != "" ]; then
		echo;
		echo "The option -c is obsolete now. Please use \"--auth\".";
		echo;
	fi;
	if [ "${running_mode}" = "${MODE_OFFLINE}" ]; then
		if [ "${Ctype}" = "" ]; then
			echo "*** Error: --auth is missing.";
			exit 1;
		fi;
		if [ "${Ctype}" != "NS" ] && [ "${Ctype}" != "PKC" ] && \
			[ "${Ctype}" != "SBKPKC" ]; then
			echo "*** Error: illegal --auth type. (valid types = \"NS\" or \"PKC\" or \"SBKPKC\")";
			exit 1;
		fi;
	fi;
}

get_running_mode ()
{
	if [ ${noburn} -eq 0 ]; then
		running_mode="${MODE_ONLINE}";
		return;
	fi;

	if [ "${tid}" = "0x23" ]; then
		if [ "${FAB}" != "" ] && [ "${BOARDID}" != "" ] && \
			[ "${BOARDSKU}" != "" ] && [ "${BOARDREV}" != "" ] && \
			[ "${CHIPREV}" != "" ] && [ "${CHIP_SKU}" != "" ]; then
			running_mode="${MODE_OFFLINE}";
		else
			echo
			echo "Odmfuse requires variable FAB, BOARDID, BOARDSKU, BOARDREV, CHIPREV, and CHIP_SKU in order to run in the offline mode."
			echo "Otherwise odmfuse needs to access on board EEPROM. Make sure the board is in recovery mode."
			echo
			running_mode="${MODE_HYBRID}";
		fi;
	elif [ "${tid}" = "0x19" ]; then
		if [ "${FAB}" != "" ] && [ "${BOARDID}" != "" ] && \
			[ "${BOARDSKU}" != "" ] && [ "${BOARDREV}" != "" ] && \
			[ "${CHIPREV}" != "" ]; then
			running_mode="${MODE_OFFLINE}";
		else
			echo
			echo "Odmfuse requires variable FAB, BOARDID, BOARDSKU, BOARDREV, and CHIPREV in order to run in the offline mode."
			echo "Otherwise odmfuse needs to access on board EEPROM. Make sure the board is in recovery mode."
			echo
			running_mode="${MODE_HYBRID}";
		fi;
	else
		echo "*** Error: Unsupported Tegra ID ${tid}.";
		exit 1;
	fi;
}

sanitize_inputs ()
{
	local kekmsg="Error: Key Encryption Key is supported only for Xavier";
	if [ "${tid}" = "" ]; then
		echo "*** Error: Tegra ID is missing.";
		usage;
	fi;
	if [ "${tid}" != "0x23" ] && [ "${tid}" != "0x19" ]; then
		echo "*** Error: Unsupported Tegra ID ${tid}.";
		exit 1;
	fi;

	get_running_mode;
	check_ctype;
	if [ "${running_mode}" = "${MODE_OFFLINE}" ]; then
		check_sbk_pkc "${Ctype}" "${KEYFILE}" "${SBKFILE}";
	fi;

	if [ "${KEK0FILE}" != "" ]; then
		if [ "${tid}" != "0x19" ]; then
			echo "${kekmsg}";
			exit 1;
		fi;
		if [ ! -f "${KEK0FILE}" ]; then
			echo "*** Error: ${KEK0FILE} doesn't exits.";
			exit 1;
		fi;
		KEK0FILE=$(readlink -f "${KEK0FILE}");
	fi;

	if [ "${KEK1FILE}" != "" ]; then
		if [ "${tid}" != "0x19" ]; then
			echo "${kekmsg}";
			exit 1;
		fi;
		if [ ! -f "${KEK1FILE}" ]; then
			echo "*** Error: ${KEK1FILE} doesn't exits.";
			exit 1;
		fi;
		KEK1FILE=$(readlink -f "${KEK1FILE}");
	fi;

	if [ "${KEK2FILE}" != "" ]; then
		if [ "${tid}" != "0x19" ]; then
			echo "${kekmsg}";
			exit 1;
		fi;
		if [ ! -f "${KEK2FILE}" ]; then
			echo "*** Error: ${KEK2FILE} doesn't exits.";
			exit 1;
		fi;
		KEK2FILE=$(readlink -f "${KEK2FILE}");
	fi;

	if [ "${KEK256FILE}" != "" ]; then
		if [ "${tid}" != "0x19" ]; then
			echo "${kekmsg}";
			exit 1;
		fi;
		if [ ! -f "${KEK256FILE}" ]; then
			echo "*** Error: ${KEK256FILE} doesn't exits.";
			exit 1;
		fi;
		KEK256FILE=$(readlink -f "${KEK256FILE}");
	fi;

	if [ "${KEYFILE}" != "" ]; then
		if [ ! -f "${KEYFILE}" ]; then
			echo "*** Error: ${KEYFILE} doesn't exits.";
			exit 1;
		fi;
		KEYFILE=$(readlink -f "${KEYFILE}");
	fi;

	if [ "${SBKFILE}" != "" ]; then
		if [ ! -f "${SBKFILE}" ]; then
			echo "*** Error: ${SBKFILE} doesn't exits.";
			exit 1;
		fi;
		SBKFILE=$(readlink -f "${SBKFILE}");
	fi;
}

check_keks ()
{
	local name=$1;
	local value_old=$2;
	local value_new="$3";
	local tmp="";
	local slice_old="";
	local slice_new="";
	local -i i;
	local -i j;
	local -i len;

	if [ "${name}" = "KEK256" ]; then
		len=64;
	else
		len=32;
	fi;
	if [ -n "${value_new}" ]; then
		value_new=$(cat "${value_new}");
		tmp=$(echo "${value_new}" | cut -c1-2);
		if [ "${tmp}" = "0x" ] || [ "${tmp}" = "0X" ]; then
			value_new=$(echo "${value_new}" | cut -c3-);
		fi;
		i=${#value_new};
		if ((i < len)); then
			for ((j = 1; j <= len - i; j++)); do
				value_new="0"${value_new};
			done;
		fi;
	fi;
	tmp=$(echo "${value_old}" | sed 's/^0*//');
	if [ -n "${tmp}" ] && [ -n "${value_new}" ]; then
		if [ ${force} -eq 1 ]; then
			for ((i = 1; i <= len; i = i + 8)); do
				j=$((i + 7));
				slice_old=$(echo "${value_old}" | cut -c${i}-${j});
				slice_old="0x$(echo "obase=16; ibase=16; ${slice_old}" | bc)";
				slice_new=$(echo "${value_new}" | cut -c${i}-${j});
				slice_new="0x$(echo "obase=16; ibase=16; ${slice_new}" | bc)";
				tmp=$((slice_old & slice_new));
				if ((tmp != slice_old)); then
					echo "${name}: you can't reset bits from 1 to 0. You're changing 0x${value_old} to 0x${value_new}.";
					return 1;
				fi;
			done;
		else
			echo "The ${name} has been burned. You can't burn it again.";
			echo "Add --force if you really want to do it.";
			return 1;
		fi;
	fi;

	return 0;
}

check_fuse_values ()
{
	local __auth=$1;
	local tmp="";
	local odm_lock_and="";
	local odm_old_name="";
	local odm_new_name="";
	local num="";
	local i;
	declare -i mask=0;
	if [ "${tid}" = "0x19" ]; then
		for f in "${FUSES_KEYS[@]}"; do
			tmp=$(cat "${FUSE_READ_OUTPUT}" |grep "${f}:"|cut -d" " -f2);
			eval "__fuse_check_${f}=${tmp}";
		done;
		for f in "${FUSES_MANUFACTURING[@]}"; do
			tmp=$(cat "${FUSE_READ_OUTPUT}" |grep "${f}:"|cut -d" " -f2);
			eval "__fuse_check_${f}=0x$(echo "obase=16; ibase=16; ${tmp}" | bc)";
		done;
	elif [ "${tid}" = "0x23" ]; then
		for f in "${FUSES_KEYS_T234[@]}"; do
			tmp=$(cat "${FUSE_READ_OUTPUT}" |grep "${f}:"|cut -d" " -f2);
			eval "__fuse_check_${f}=${tmp}";
		done;
		for f in "${FUSES_MANUFACTURING_T234[@]}"; do
			tmp=$(cat "${FUSE_READ_OUTPUT}" |grep "${f}:"|cut -d" " -f2);
			eval "__fuse_check_${f}=0x$(echo "obase=16; ibase=16; ${tmp}" | bc)";
		done;
	fi


	for f in "${FUSES_ODM_RESERVED[@]}"; do
		tmp=$(cat "${FUSE_READ_OUTPUT}" |grep "${f}:"|cut -d" " -f2);
		eval "__fuse_check_${f}=0x$(echo "obase=16; ibase=16; ${tmp}" | bc)";
	done;
	if [ "${tid}" = "0x19" ]; then
		for f in "${FUSES_T19X_EXTRA_ODM[@]}"; do
			tmp=$(cat "${FUSE_READ_OUTPUT}" |grep "${f}:"|cut -d" " -f2);
			eval "__fuse_check_${f}=0x$(echo "obase=16; ibase=16; ${tmp}" | bc)";
		done;
	fi;

	if (( __fuse_check_SecurityMode > 0)); then
		if [ -n "${KEK0FILE}" ] || [ -n "${KEK1FILE}" ] || [ -n "${KEK2FILE}" ] ||
		   [ -n "${KEK256FILE}" ] || [ -n "${BootDevCfg}" ] || [ ${jtag_disable} -eq 1 ] ||
		   [ -n "${set_productionmode}" ] || [ -n "${sw_reserved}" ] ||
		   [ -n "${arm_debug_auth}" ] || [ -n "${odm_id}" ]; then
			echo "SecurityMode is burned, you can't burn any manufacturing fuses now.";
			return 1;
		fi;
		if [ "${__auth}" = "NS" ]; then
			if [ -n "${KEYFILE}" ] || [ -n "${SBKFILE}" ]; then
				echo "SecurityMode is burned, you can't burn PKC or SBK now.";
				return 1;
			fi;
		fi;
	fi;
	# We don't need to check PKC and SBK here.
	# If PKC is burned, then the KEYFILE users provide is for signing the images
	# If SBK is burned, then the SBKFILE users provide is for encrypting the images
	# If both PKC and SBK are not burned, then the KEYFILE and SBKFILE users provide are for burning
	check_keks "KEK0" "${__fuse_check_Kek0}" "${KEK0FILE}" || { return 1; };
	check_keks "KEK1" "${__fuse_check_Kek1}" "${KEK1FILE}" || { return 1; };
	check_keks "KEK2" "${__fuse_check_Kek2}" "${KEK2FILE}" || { return 1; };
	check_keks "KEK256" "${__fuse_check_Kek256}" "${KEK256FILE}" || { return 1; };

	if [ ${jtag_disable} -eq 1 ] && ((__fuse_check_JtagDisable > 0)); then
		echo "The jtag-disable fuse bit has been burned. You can't burn it again.";
		return 1;
	fi;
	if [ -n "${sw_reserved}" ]; then
		tmp=$((__fuse_check_SwReserved & sw_reserved));
		if ((tmp != __fuse_check_SwReserved)); then
			echo "SwReserved: you can't reset bits from 1 to 0. You're changing ${__fuse_check_SwReserved} to ${sw_reserved}.";
			return 1;
		fi;
	fi;
	if [ -n "${arm_debug_auth}" ]; then
		tmp=$((__fuse_check_DebugAuthentication & arm_debug_auth));
		if ((tmp != __fuse_check_DebugAuthentication)); then
			echo -n "DebugAuthentication: you can't reset bits from 1 to 0. ";
			echo "You're changing ${__fuse_check_DebugAuthentication} to ${arm_debug_auth}.";
			return 1;
		fi;
	fi;
	if [ -n "${odm_id}" ]; then
		tmp=$((__fuse_check_OdmId & odm_id));
		if ((tmp != __fuse_check_OdmId)); then
			echo "OdmId: you can't reset bits from 1 to 0. You're changing ${__fuse_check_OdmId} to ${odm_id}.";
			return 1;
		fi;
	fi;

	if [ -n "${odm_lock}" ]; then
		tmp=$((__fuse_check_OdmLock & odm_lock));
		if ((tmp != __fuse_check_OdmLock)); then
			echo "OdmLock: you can't reset bits from 1 to 0. You're changing ${__fuse_check_OdmLock} to ${odm_lock}.";
			return 1;
		fi;
	fi;
	local odm_reserved_fuses=("${FUSES_ODM_RESERVED[@]}");
	for (( i=0; i<${#odm_reserved_fuses[@]}; i++ )); do
		f="${odm_reserved_fuses[$i]}";
		if [[ "${f}" = ReservedOdm* ]]; then
			num=${f##ReservedOdm};
			odm_new_name="odm_reserved${num}";
			if [ -n "${!odm_new_name}" ]; then
				if ((num < 4)); then
					mask=$(( 1 << num ));
					odm_lock_and=$((__fuse_check_OdmLock & mask));
					if ((odm_lock_and > 0)); then
						echo "OdmLock[${num}] has been set. You can't burn ${odm_new_name} again.";
						return 1;
					fi;
				fi;

				odm_old_name="__fuse_check_${f}";
				tmp=$((${!odm_old_name} & ${!odm_new_name}));
				if ((tmp != ${!odm_old_name})); then
					echo "${odm_new_name}: you can't reset bits from 1 to 0. You're changing ${!odm_old_name} to ${!odm_new_name}.";
					return 1;
				fi;
			fi;
		fi;
	done;

	return 0;
}

get_pkc_keytype ()
{
	local tegraid="$1";
	local keyfile="$2";
	local ksize="";
	local ktype="";
	local lst;

	lst=$(ssh-keygen -l -f "${keyfile}" 2>&1);
	if [ $? -ne 0 ]; then
		echo "";
		return;
	fi;
	ksize=$(echo "${lst}" | awk '{print $1}');
	ktype=$(echo "${lst,,}" | rev | awk '{print $1}' | rev | tr -d "()");

	# T19x supports both rsa-2k and rsa-3k.
	# T23x supports rsa-3k only.
	# ecdsa is only supported by T23x.
	case ${ktype} in
	rsa)	case ${ksize} in
		2048)	if [ "${tegraid}" = "0x19" ]; then
				echo "rsa-2k";
				return;
			fi;
			;;
		3072)	echo "rsa-3k";
			return;
			;;
		esac;
		;;
	ecdsa)	if [ "${tegraid}" = "0x23" ]; then
			case ${ksize} in
			256)	echo "ecp256";
				return;
				;;
			521)	echo "ecp521";
				return;
				;;
			esac;
		fi;
		;;
	esac;
	echo "";
}

generate_key_hash_v3 ()
{
	local keyfile="${1}";
	local aline;
	local tegrasign="${BL_DIR}/tegrasign_v3.py";
	local tmpfiles="ppp hhh";
	local signparam="--pubkeyhash ${tmpfiles} --key ${keyfile}";

	aline=$(${tegrasign} ${signparam} | grep "tegra-fuse");
	rm -f "${tmpfiles}";
	echo "${aline}" | rev | awk '{print $1}' | rev;
}

generate_key_hash_19x ()
{
	local keytype="$1";
	local keyfile="$2";
	local tmpdir;
	local result;
	local tegraopenssl="${BL_DIR}"/tegraopenssl;

	result="";
	tmpdir="$(mktemp -d generate-key-hash-tmpdir-XXXX)";
	pushd "${tmpdir}" >& /dev/null || exit;
	openssl rsa -in "${keyfile}" -noout -modulus -out key.mod;
	cat key.mod | sed s/^Modulus=// > key.mod.tmp;
	xxd -r -p key.mod.tmp key.mod.tmp.bin;
	if [ "${keytype}" = "rsa-2k" ]; then
		objcopy -I binary --reverse-byte=256 key.mod.tmp.bin key.mod.bin;
	else
		${tegraopenssl} --isPkcKey "${keyfile}" key.hash mont.bin >& /dev/null;
		objcopy -I binary --reverse-byte=384 key.mod.tmp.bin key.mod.bin;
	fi;
	dd if=/dev/zero of=buffer.bin bs=1 count=1216 >& /dev/null;
	dd if=key.mod.bin conv=notrunc of=buffer.bin bs=1 >& /dev/null;
	if [ "${keytype}" = "rsa-3k" ]; then
		dd if=mont.bin conv=notrunc of=buffer.bin bs=1 seek=384 >& /dev/null;
	fi;
	result=$(openssl dgst -sha256 -hex buffer.bin|cut -d" " -f2);
	popd >& /dev/null || exit;
	rm -rf "${tmpdir}";
	echo "0x${result}";
}

insert_odm_reserved ()
{
	local idx=$1;
	local fusecfg=$2;
	local var="odm_reserved${idx}";

	if [ "${!var}" = "" ]; then
		echo "0";
		return;
	fi;
	echo -n "<fuse name=\"ReservedOdm${idx}\" " >> "${fusecfg}";
	echo "size=\"4\" value=\"${!var}\" />" >> "${fusecfg}";
	echo "1";
}

check_required_tools ()
{
	local i;
	local tools=("bc" "xxd" "objcopy" "openssl");
	for (( i=0; i<${#tools[@]}; i++ )); do
		t="${tools[$i]}";
		if ! command -v "${t}" >& /dev/null; then
			echo "Command \"${t}\" cannot be found. Install it before continue.";
			return 1;
		fi;
	done;
	return 0;
}

check_required_tools || { exit 1; };

cd "$(dirname "$0")" || exit 1;
source ./odmfuse.func;
LDK_DIR="$(pwd)";
LDK_DIR=$(readlink -f "${LDK_DIR}");
BL_DIR="${LDK_DIR}/bootloader";

# For T194 platforms
#   The variable Ctype saves the value of the "-c" option originally.
#   We use it to save the value of option "--auth" now because the option "-c" has been obsoleted.
#   For offline mode, $Ctype is mandatory because we have no way to know the authentication status of the board.
#   For online and hybrid modes, $Ctype is optional because we always get the authentication status
#   of the board and save it to $bootauth. If users set $Ctype, we'll compare it with $bootauth and
#   fail the execution if they don't match.
Ctype="";
__ctype="";
noburn=0;
testmode=0;
force=0;
jtag_disable=0;
running_mode="";
arm_debug_auth="";
odm_id="";
while getopts "hc:d:i:jk:l:pr:s:S:X:-:" OPTION
do
	case $OPTION in
	h) usage; ;;
	c) __ctype=${OPTARG}; ;;
	d) BootDevCfg="${OPTARG}"; ;;
	i) tid="${OPTARG}"; ;;
	j)
		echo;
		echo "The option -j is obsolete now. Jtag by default is enabled.";
		echo "Please use \"--disable-jtag\" option if you want to burn the jtag-disable fuse.";
		echo "Jtag can't be re-enabled once the jtag-disable fuse bit is burned.";
		echo;
	;;
	k) KEYFILE="${OPTARG}"; ;;
	l) odm_lock="${OPTARG}"; ;;
	p) set_productionmode="yes"; ;;
	r) sw_reserved="${OPTARG}"; ;;
	s) export sku="${OPTARG}"; ;;
	S) SBKFILE="${OPTARG}"; ;;
	X) XFILE="${OPTARG}"; ;;
	-) case ${OPTARG} in
	   noburn) noburn=1; ;;
	   test) testmode=1; ;;
	   force) force=1; ;;
	   disable-jtag) jtag_disable=1; ;;
	   auth) Ctype="${!OPTIND}"; OPTIND=$((OPTIND+1));;
	   KEK0) KEK0FILE="${!OPTIND}"; OPTIND=$((OPTIND+1));;
	   KEK1) KEK1FILE="${!OPTIND}"; OPTIND=$((OPTIND+1));;
	   KEK2) KEK2FILE="${!OPTIND}"; OPTIND=$((OPTIND+1));;
	   KEK256) KEK256FILE="${!OPTIND}"; OPTIND=$((OPTIND+1));;
	   odm_reserved0) export odm_reserved0="${!OPTIND}"; OPTIND=$((OPTIND+1));;
	   odm_reserved1) export odm_reserved1="${!OPTIND}"; OPTIND=$((OPTIND+1));;
	   odm_reserved2) export odm_reserved2="${!OPTIND}"; OPTIND=$((OPTIND+1));;
	   odm_reserved3) export odm_reserved3="${!OPTIND}"; OPTIND=$((OPTIND+1));;
	   odm_reserved4) export odm_reserved4="${!OPTIND}"; OPTIND=$((OPTIND+1));;
	   odm_reserved5) export odm_reserved5="${!OPTIND}"; OPTIND=$((OPTIND+1));;
	   odm_reserved6) export odm_reserved6="${!OPTIND}"; OPTIND=$((OPTIND+1));;
	   odm_reserved7) export odm_reserved7="${!OPTIND}"; OPTIND=$((OPTIND+1));;
	   debug_authentication) arm_debug_auth="${!OPTIND}"; OPTIND=$((OPTIND+1));;
	   odm_id) odm_id="${!OPTIND}"; OPTIND=$((OPTIND+1));;
	   *) usage; ;;
	   esac;;
	*) usage; ;;
	esac
done

sanitize_inputs;

shift $((OPTIND - 1));
if [ $# -ne 1 ]; then
	usage;
fi;
nargs=$#;
ext_target_board=${!nargs};
if [ ! -r ${ext_target_board}.conf ]; then
	echo -n "Error: Invalid target board - ";
	echo "${ext_target_board}";
	exit 1;
fi;

# set up environments:
source ${ext_target_board}.conf
TARGET_DIR="${BL_DIR}/${target_board}";
KERNEL_DIR="${LDK_DIR}/kernel";
export DTB_DIR="${KERNEL_DIR}/dtb";
export PATH="${KERNEL_DIR}:${PATH}";
odmfuse_init "${tid}" "${usb_instance}" "${CHIPMAJOR}" "${BL_DIR}" "${TARGET_DIR}" "${LDK_DIR}" "${SBKFILE}" "${KEYFILE}";

if [ "${running_mode}" != "${MODE_OFFLINE}" ]; then
	# The fuse read function does 3 things:
	# (1) Call get_fuse_level to get the "bootauth" of the board
	# (2) Dump the eeprom of the board so that we can make fuse args
	# (3) Send nvtboot binaries to the board then read fuse values
	if ! read_fuse_values bootauth CMDARGS; then
		echo "Error: read fuse from board failed.";
		exit 1;
	fi;
	if [ "${Ctype}" != "" ] && [ "${Ctype}" != "${bootauth}" ]; then
		echo "Error: wrong \"--auth\" option is set. The board's authentication type is: ${bootauth}.";
		exit 1;
	fi;

	pushd "${BL_DIR}" >& /dev/null || exit;
	if ! check_fuse_values "${bootauth}"; then
		exit 1;
	fi;
	popd >& /dev/null || exit;
else
	fuselevel="${FUSELEVEL:-fuselevel_production}";
	hwchipid="${tid}";
	bootauth="None";
	hwchiprev="0";

	if declare -F -f process_fuse_level > /dev/null 2>&1; then
		process_fuse_level "${fuselevel}";
	fi;

	bd_ver="${FAB}";
	bd_id="${BOARDID}";
	bd_sku="${BOARDSKU}";
	bd_rev="${BOARDREV}";
	if [ "${CHIPREV}" != "" ]; then
		hwchiprev="${CHIPREV}";
	fi;

	# process the board version and update the data accordingly
	if declare -F -f process_board_version > /dev/null 2>&1; then
		process_board_version "${bd_id}" "${bd_ver}" "${bd_sku}" "${bd_rev}" "${hwchiprev}";
	fi;

	# process the chip sku version and update the data accordingly
	if declare -F -f process_chip_sku_version > /dev/null 2>&1; then
		chip_SKU="${CHIP_SKU}";
       		chip_minor_revision_ID="${CHIP_MINOR}";
       		bootrom_revision_ID="${BOOTROM_ID}";
		ramcode_ID="${RAMCODE_ID:-0}";
		process_chip_sku_version "${chip_SKU}" "${chip_minor_revision_ID}" "${bootrom_revision_ID}" "${ramcode_ID}" "${fuselevel}" "${bd_ver}";
	fi;
	if declare -F -f update_flash_args > /dev/null 2>&1; then
		emcfuse_value="${EMCFUSE_VALUE:-c}"
		#this variable is defined in get_board_version function
		#shellcheck disable=SC2154
		update_flash_args "" "${emcfuse_value}";
	fi;
	prepare_cmdargs "${Ctype}" CMDARGS;
	echo "${dev_paramsname}"
	echo "${mb2_dev_paramsname}"
	if [ "${tid}" = "0x23" ]; then
		CMDARGS="${CMDARGS/${dev_paramsname}/${mb2_dev_paramsname}}"
		sed -i "s/preprod_dev_sign = <1>/preprod_dev_sign = <0>/" "${BL_DIR}/${mb2_dev_paramsname}"
	fi;
fi;

if [ "${XFILE}" != "" ]; then
	if [ "${BootDevCfg}" != "" ] || \
		[ ${jtag_disable} -eq 1 ] || \
		[ "${odm_lock}" != "" ] || \
		[ "${set_productionmode}" != "" ] || \
		[ "${sw_reserved}" != "" ] || \
		[ "${SBKFILE}" != "" ]; then
		usage;
	fi;
	if [ ! -f "${XFILE}" ]; then
		echo "*** Error: ${XFILE} doesn't exits.";
		exit 1;
	fi;
	XFILE=$(readlink -f "${XFILE}");
	pushd bootloader > /dev/null 2>&1 || exit;

	fusecfgname=$(mktemp -u fuse_XXXXXX.xml);
	fusecfg="${fusecfgname}";
	cp -f "${XFILE}" "${fusecfgname}" > /dev/null 2>&1;
	if [ "${testmode}" -eq 1 ]; then
		fusecfg="dummy ${fusecfg}"
	fi
	if [ "${tid}" = "0x23" ]; then
		generate_fskp_blob "${fusecfgname}" "CMDARGS"
	fi

	echo "*** Start fusing from fuse configuration ... ";
	if [ "${tid}" = "0x23" ]; then
		fcmd="./tegraflash.py ${CMDARGS} --cpubl uefi_jetson.bin --concat_cpubl_bldtb --bl uefi_jetson_with_dtb.bin ";
	elif [ "${tid}" = "0x19" ]; then
		fcmd="./tegraflash.py ${CMDARGS} ";
	fi;
	if [ "${RAMCODE}" != "" ]; then
		fcmd+=" --ramcode ${RAMCODE} ";
	fi;
	fcmd+="--cmd \"burnfuses ${fusecfg}\"";

	if [ "${noburn}" -eq 1 ]; then
		factory_overlay_gen "${fcmd}" "${fusecfgname}";
		rm -f "${fusecfgname}";
		exit $?;
	fi;
	echo "${fcmd}";
	eval "${fcmd}";
	if [ $? -ne 0 ]; then
		echo "Fuse failed.";
		rm -f "${fusecfgname}";
		exit 1;
	fi;
	rm -f "${fusecfgname}";
	popd > /dev/null 2>&1 || exit;
	exit 0;
fi;

pushd bootloader > /dev/null 2>&1 || exit;
hash="";
pkc_size="";
if [ -f "${KEYFILE}" ]; then
	echo -n "*** Calculating HASH from keyfile ${KEYFILE} ... ";
	pkc_keytype=$(get_pkc_keytype "${tid}" "${KEYFILE}");
	if [ "${pkc_keytype}" = "" ]; then
		echo "Error: Unsupported or unprotected key";
		ssh-keygen -l -f "${KEYFILE}";
		exit 1;
	fi;
	if [ "${tid}" = "0x23" ]; then
		hash=$(generate_key_hash_v3 "${KEYFILE}");
	else
		hash=$(generate_key_hash_19x "${pkc_keytype}" "${KEYFILE}");
	fi;
	echo "done";
	echo "PKC HASH: ${hash}";
	pkc_size=$((${#hash} - 2));
	pkc_size=$((pkc_size / 2));
fi;

if [ "${KEK0FILE}" != "" ]; then
	kek0=$(cat "${KEK0FILE}");
	chkhash "kek0" 16;
fi;

if [ "${KEK1FILE}" != "" ]; then
	kek1=$(cat "${KEK1FILE}");
	chkhash "kek1" 16;
fi;

if [ "${KEK2FILE}" != "" ]; then
	kek2=$(cat "${KEK2FILE}");
	chkhash "kek2" 16;
fi;

if [ "${KEK256FILE}" != "" ]; then
	kek256=$(cat "${KEK256FILE}");
	chkhash "kek256" 32;
fi;

if [ "${SBKFILE}" != "" ]; then
	SBK="$(cat "${SBKFILE}" | sed -e s/^[[:space:]]*// -e s/[[:space:]]0x//g -e s/[[:space:]]*//g)";
	sbk_size=$((${#SBK} - 2));
	sbk_size=$((sbk_size / 2));
	sbbits=$((sbk_size * 8));
	case ${sbk_size} in
	16)	if [ "${tid}" = "0x23" ]; then
			echo "Error: ${sbbits}bit SBK is unsupported by T23x.";
			exit 1;
		fi;
		;;
	32)	if [ "${tid}" = "0x19" ]; then
			echo "Error: ${sbbits}bit SBK is unsupported by T19x.";
			exit 1;
		fi;
		;;
	*)	echo "Error: Unsupported SBK keysize ${sbk_size}";
		exit 1;
		;;
	esac;
	chkhash "SBK" ${sbk_size};
fi;

echo -n "*** Generating fuse configuration ... ";
pkc_hash="";
fusecfg="odmfuse_pkc.xml";
if [ "${Ctype}" = "NS" ] || [ "${bootauth}" = "NS" ]; then
	pkc_hash="${hash}";
fi;

rm -f ${fusecfg};
fusecnt=0
magicid="0x45535546";	# BigEndian format
echo -n "<genericfuse " >> ${fusecfg};
echo    "MagicId=\"${magicid}\" version=\"1.0.0\">" >> ${fusecfg};
if [ "${BootDevCfg}" != "" ]; then
	echo -n "<fuse name=\"SecBootDeviceSelect\" " >> ${fusecfg};
	echo    "size=\"4\" value=\"${BootDevCfg}\" />" >> ${fusecfg};
	fusecnt=$((fusecnt + 1));
fi;
if [ ${jtag_disable} -eq 1 ]; then
	if [ "${tid}" = "0x23" ]; then
		echo -n "<fuse name=\"ArmJtagDisable\" " >> ${fusecfg};
	else
		echo -n "<fuse name=\"JtagDisable\" " >> ${fusecfg};
	fi;
	echo    "size=\"4\" value=\"0x1\" />" >> ${fusecfg};
	fusecnt=$((fusecnt + 1));
fi;

fusecnt=$((fusecnt + $(insert_odm_reserved 0 "${fusecfg}") ));
fusecnt=$((fusecnt + $(insert_odm_reserved 1 "${fusecfg}") ));
fusecnt=$((fusecnt + $(insert_odm_reserved 2 "${fusecfg}") ));
fusecnt=$((fusecnt + $(insert_odm_reserved 3 "${fusecfg}") ));
fusecnt=$((fusecnt + $(insert_odm_reserved 4 "${fusecfg}") ));
fusecnt=$((fusecnt + $(insert_odm_reserved 5 "${fusecfg}") ));
fusecnt=$((fusecnt + $(insert_odm_reserved 6 "${fusecfg}") ));
fusecnt=$((fusecnt + $(insert_odm_reserved 7 "${fusecfg}") ));
if [ "${odm_lock}" != "" ]; then
	echo -n "<fuse name=\"OdmLock\" " >> ${fusecfg};
	echo    "size=\"4\" value=\"${odm_lock}\" />" >> ${fusecfg};
	fusecnt=$((fusecnt + 1));
fi;

if [ "${arm_debug_auth}" != "" ]; then
	if [ "${tid}" != "0x19" ]; then
		echo "Error: arm_debug_auth is not verified yet for ${tid}.";
		exit 1;
	fi;
	echo -n "<fuse name=\"DebugAuthentication\" " >> ${fusecfg};
	echo "size=\"4\" value=\"${arm_debug_auth}\" />" >> ${fusecfg};
	fusecnt=$((fusecnt + 1));
fi;

if [ "${odm_id}" != "" ]; then
	echo -n "<fuse name=\"OdmId\" " >> ${fusecfg};
	echo "size=\"8\" value=\"${odm_id}\" />" >> ${fusecfg};
	fusecnt=$((fusecnt + 1));
fi;
if [ "${sw_reserved}" != "" ]; then
	echo -n "<fuse name=\"SwReserved\" " >> ${fusecfg};
	echo "size=\"4\" value=\"${sw_reserved}\" />" >> ${fusecfg};
	fusecnt=$((fusecnt + 1));
fi;
if [ "${SBK}" != "" ]; then
	if [ "${bootauth}" = "NS" ] || [ "${Ctype}" = "NS" ]; then
		echo -n "<fuse name=\"SecureBootKey\" " >> ${fusecfg};
		echo    "size=\"${sbk_size}\" value=\"${SBK}\" />"  >> ${fusecfg};
		fusecnt=$((fusecnt + 1));
	fi;
fi;
if [ "${kek0}" != "" ]; then
	echo -n "<fuse name=\"Kek0\" " >> ${fusecfg};
	echo    "size=\"16\" value=\"${kek0}\" />"  >> ${fusecfg};
	fusecnt=$((fusecnt + 1));
fi;
if [ "${kek1}" != "" ]; then
	echo -n "<fuse name=\"Kek1\" " >> ${fusecfg};
	echo    "size=\"16\" value=\"${kek1}\" />"  >> ${fusecfg};
	fusecnt=$((fusecnt + 1));
fi;
if [ "${kek2}" != "" ]; then
	echo -n "<fuse name=\"Kek2\" " >> ${fusecfg};
	echo    "size=\"16\" value=\"${kek2}\" />"  >> ${fusecfg};
	fusecnt=$((fusecnt + 1));
fi;
if [ "${kek256}" != "" ]; then
	echo -n "<fuse name=\"Kek256\" " >> ${fusecfg};
	echo    "size=\"32\" value=\"${kek256}\" />"  >> ${fusecfg};
	fusecnt=$((fusecnt + 1));
fi;
if [ "${pkc_hash}" != "" ]; then
	echo -n "<fuse name=\"PublicKeyHash\" size=\"${pkc_size}\" " >> ${fusecfg};
	echo    "value=\"${pkc_hash}\" />" >> ${fusecfg};
	fusecnt=$((fusecnt + 1));
	echo -n "<fuse name=" >> ${fusecfg};
	echo -n "\"BootSecurityInfo\" " >> ${fusecfg};
	case ${pkc_keytype} in
	rsa-2k)	if [ "${SBK}" != "" ]; then
			bsi="0x5";
		else
			bsi="0x1";
		fi;
		;;
	rsa-3k)	if [ "${tid}" = "0x19" ]; then
			if [ "${SBK}" != "" ]; then
				bsi="0x6";
			else
				bsi="0x2";
			fi;
		else
			if [ "${SBK}" != "" ]; then
				bsi="0x209";
			else
				bsi="0x201";
			fi;
		fi;
		;;
	ecp256)	if [ "${SBK}" != "" ]; then
			bsi="0x20a";
		else
			bsi="0x202";
		fi;
		;;
	ecp521)	if [ "${SBK}" != "" ]; then
			bsi="0x20b";
		else
			bsi="0x203";
		fi;
		;;
	*)	echo "Error: authetication ${pkc_keytype} not supported.";
		exit 1;
		;;
	esac;
	echo "size=\"4\" value=\"${bsi}\" />"  >> ${fusecfg};
	fusecnt=$((fusecnt + 1));
fi;
if [ "${set_productionmode}" != "" ]; then
	echo -n "<fuse name=\"SecurityMode\" " >> ${fusecfg};
	echo    "size=\"4\" value=\"0x1\" />"  >> ${fusecfg};
	fusecnt=$((fusecnt + 1));
fi;
echo "</genericfuse>" >> ${fusecfg};
echo "done.";

if [ "${fusecnt}" -eq 0 ]; then
	echo "*** No fuse bit specified. Terminating.";
	exit 0;
fi;

cp "${fusecfg}" "${fusecfg}.sav";

if [ "${tid}" = "0x23" ]; then
	generate_fskp_blob "${fusecfg}" "CMDARGS"
fi

fcmd="./tegraflash.py ${CMDARGS} ";
if [ "${RAMCODE}" != "" ]; then
	fcmd+=" --ramcode ${RAMCODE} ";
fi;

# For T234 and T194, tegraflash_internal.py always reboots to recovery
# when fuse burning is done. So we don't add the "reboot recovery" in
# the tegraflash command here.
if [ ${testmode} -eq 1 ]; then
	echo "Test mode: using dummy so no fuses will be burned.";
	fcmd+="--cmd \"burnfuses dummy ${fusecfg}\"";
else
	fcmd+="--cmd \"burnfuses ${fusecfg}\"";
fi;

if [ "${noburn}" -eq 1 ]; then
	factory_overlay_gen "${fcmd}" "${fusecfg}";
	exit $?;
fi;

echo "*** Start fusing ... ";
echo "${fcmd}";
eval "${fcmd}";
if [ $? -ne 0 ]; then
	echo "failed.";
	exit 1;
fi;
echo "*** The fuse configuration is saved in bootloader/${fusecfg}";
echo "*** The ODM fuse has been burned successfully.";
popd > /dev/null 2>&1 || exit;
echo "*** done.";
exit 0;
