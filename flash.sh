#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2011-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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


#
# flash.sh: Flash the target board.
#	    flash.sh performs the best in LDK release environment.
#
# Usage: Place the board in recovery mode and run:
#
#	flash.sh [options] <target_board> <root_device>
#
#	for more detail enter 'flash.sh -h'
#
# Examples:
# ./flash.sh <target_board> internal			- boot <target_board> from on-board device (eMMC/SDCARD)
# ./flash.sh <target_board> external			- boot <target_board> from external device
# ./flash.sh <target_board> mmcblk0p1			- boot <target_board> from eMMC
# ./flash.sh <target_board> mmcblk1p1			- boot <target_board> from SDCARD
# ./flash.sh <target_board> sda1			- boot <target_board> from USB device
# ./flash.sh <target_board> nvme0n1			- boot <target_board> from NVME storage device
# ./flash.sh -N <IPaddr>:/nfsroot <target_board> eth0	- boot <target_board> from NFS
# ./flash.sh -k LNX <target_board> mmcblk1p1		- update <target_board> kernel
# ./flash.sh -k EBT <target_board> mmcblk1p1		- update <target_board> bootloader
#
# Optional Environment Variables:
# BOARDID ---------------- Pass boardid to override EEPROM value
# BOARDREV --------------- Pass board_revision to override EEPROM value
# BOARDSKU --------------- Pass board_sku to override EEPROM value
# BOOTLOADER ------------- Bootloader binary to be flashed
# CFGFILE ---------------- Partition table configuration file to be used.
# CMDLINE ---------------- Target cmdline. See help for more information.
# DEVSECTSIZE ------------ Device Sector size. (default = 512Byte).
# DTBFILE ---------------- Device Tree file to be used.
# EMCFUSE_VALUE ---------- Value of emc fuse.
# FLASHAPP --------------- Flash application running in host machine.
# INITRD ----------------- Initrd image file to be flashed.
# KERNEL_IMAGE ----------- Linux kernel zImage file to be flashed.
# MTS -------------------- MTS file name such as mts_si.
# MTSPREBOOT ------------- MTS preboot file name such as mts_preboot_si.
# NFSARGS ---------------- Static Network assignments.
#			   <C-ipa>:<S-ipa>:<G-ipa>:<netmask>
# NFSROOT ---------------- NFSROOT i.e. <my IP addr>:/exported/rootfs_dir.
# NO_KERNEL_DTB ---------- Do not use kernel dtb.
# NO_RECOVERY_IMG -------- Do not create or re-create recovery.img
# NO_ROOTFS -------------- Do not create or re-create system.img
# ODMDATA ---------------- Odmdata to be used.
# ROOTFSSIZE ------------- Linux RootFS size (internal emmc/nand only).
# ROOTFS_DIR ------------- Linux RootFS directory name.
# SBKKEY ----------------- SBK key file to used to encrypt bootloader images.
# SPEFILE ---------------- SPE firmware file path such as bootloader/spe.bin.
# FAB -------------------- Target board's FAB ID.
# WB0BOOT ---------------- Warmboot code such as nvtbootwb0.bin
#
INFODIVIDER="\
###############################################################################\
";

chkerr ()
{
	if [ $? -ne 0 ]; then
		if [ "$1" != "" ]; then
			echo "$1";
		else
			echo "failed.";
		fi;
		exit 1;
	fi;
	if [ "$1" = "" ]; then
		echo "done.";
	fi;
}

pr_conf()
{
	echo "target_board=${target_board}";
	echo "target_rootdev=${target_rootdev}";
	echo "rootdev_type=${rootdev_type}";
	echo "rootfssize=${rootfssize}";
	echo "odmdata=${odmdata}";
	echo "flashapp=${flashapp}";
	echo "flasher=${flasher}";
	echo "bootloader=${bootloader}";
	echo "wb0boot=${wb0boot}";
	echo "mtspreboot=${mtspreboot}";
	echo "mts=${mts}";
	echo "bctfile=${bctfile}";
	echo "cfgfile=${cfgfile}";
	echo "kernel_fs=${kernel_fs}";
	echo "kernel_image=${kernel_image}";
	echo "dtbfile=${dtbfile}"
	echo "rootfs_dir=${rootfs_dir}";
	echo "nfsroot=${nfsroot}";
	echo "nfsargs=${nfsargs}";
	echo "kernelinitrd=${kernelinitrd}";
	echo "cmdline=${cmdline}";
	echo "boardid=${boardid}";
}

validateIP ()
{
	local ip=$1;
	local ret=1;

	if [[ $ip =~ ^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$ ]]; then
		OIFS=${IFS};
		IFS='.';
		ip=($ip);
		IFS=${OIFS};
		[[ ${ip[0]} -le 255 && ${ip[1]} -le 255 && \
		   ${ip[2]} -le 255 && ${ip[3]} -le 255 ]];
		ret=$?;
	fi;
	if [ ${ret} -ne 0 ]; then
		echo "Invalid IP address: $1";
		exit 1;
	fi;
}

netmasktbl=(\
	"255.255.255.252" \
	"255.255.255.248" \
	"255.255.255.240" \
	"255.255.255.224" \
	"255.255.255.192" \
	"255.255.255.128" \
	"255.255.255.0" \
	"255.255.254.0" \
	"255.255.252.0" \
	"255.255.248.0" \
	"255.255.240.0" \
	"255.255.224.0" \
	"255.255.192.0" \
	"255.255.128.0" \
	"255.255.0.0" \
	"255.254.0.0" \
	"255.252.0.0" \
	"255.248.0.0" \
	"255.240.0.0" \
	"255.224.0.0" \
	"255.192.0.0" \
	"255.128.0.0" \
	"255.0.0.0" \
);

validateNETMASK ()
{
	local i;
	local nm=$1;
	for (( i=0; i<${#netmasktbl[@]}; i++ )); do
		if [ "${nm}" = ${netmasktbl[$i]} ]; then
			return 0;
		fi;
	done;
	echo "Error: Invalid netmask($1)";
	exit 1;
}

validateNFSargs ()
{
	local a=$2;

	OIFS=${IFS};
	IFS=':';
	a=($a);
	IFS=${OIFS};

	if [ ${#a[@]} -ne 4 ]; then
		echo "Error: Invalid nfsargs($2)";
		exit 1;
	fi;
	validateIP ${a[0]};
	if [ "${serverip}" = "" ]; then
		validateIP ${a[1]};
	fi;
	validateIP ${a[2]};
	validateNETMASK ${a[3]};
	if [ "$1" != "" ]; then
		eval "$1=$2";
	fi;
	return 0;
}

validateNFSroot ()
{
	if [ "$2" = "" ]; then
		return 1;
	fi;
	OIFS=${IFS};
	IFS=':';
	local var=$1;
	local a=($2);
	IFS=${OIFS};
	if [ ${#a[@]} -ne 2 ]; then
		echo "Error: Invalid nfsroot($2)";
		exit 1;
	fi;
	validateIP ${a[0]};
	if [[ "${a[1]}" != /* ]]; then
		echo "Error: Invalid nfsroot($2)";
		exit 1;
	fi;
	eval "${var}=$2";
	return 0;
}

usage ()
{
	state=$1;
	retval=$2;

	if [[ $state == allunknown ]]; then
		echo -e "
Usage: sudo ./flash.sh [options] <target_board> <rootdev>
  Where,
	target board: Valid target board name or 'autodetect'
	rootdev: Proper root device.";

	elif [[ $state == rootdevunknown ]]; then
		echo -e "
Usage: sudo ./flash.sh [options] ${target_board} <rootdev>
  Where,
    rootdev for ${target_board}:
	${ROOT_DEV}";

	else
		echo "
Usage: sudo ./flash.sh [options] ${target_board} ${target_rootdev}";
	fi;

	cat << EOF
    options:
        -b <emc_bctfile> ------ use the specified emc_bct file.
        -c <cfgfile> ---------- Flash partition table config file.
        -d <dtbfile> ---------- device tree file.
        -e <bpmp_file> -------- use the specified bpmp file.
        -f <flashapp> --------- Path to flash application (tegraflash.py)
        -g <bpmp_dtb_file> ---- use the specified bpmp dtb file
        -h -------------------- print this message.
        -i <enc rfs key file>-- key for disk encryption support.
        -k <partition id> ----- partition name or number specified in flash.cfg.
        -l <tbcdtbfile> ------- cpu bootloader dtb file.
        -m <mts preboot> ------ MTS preboot such as mts_preboot_si.
        -n <nfs args> --------- Static nfs network assignments
                                <Client IP>:<Server IP>:<Gateway IP>:<Netmask>
        -o <odmdata> ---------- ODM data.
        -r -------------------- skip building and reuse existing system.img.
        -u <PKC key file>------ PKC key or PKC keylist used for odm fused board.
                                This option is also used for cpu-bootloader signing in PV key feature.
        -v <SBK key file>------ Secure Boot Key (SBK) key used for ODM fused board.
        -w <wb0boot> ---------- warm boot binary such as nvtbootwb0.bin
        -x <tegraid> ---------- Tegra CHIPID.
        -B <boardid> ---------- BoardId.
        -C <cmdline> ---------- Kernel commandline arguments.
                                WARNING:
                                Each option in this kernel commandline gets
                                higher preference over the values set by
                                flash.sh. In case of NFS booting, this script
                                adds NFS booting related arguments, if -i option
                                is omitted.
        -F <uefi_bin_name> ---- UEFI binary name such as uefi_t26x_general.bin.
        -G <file name> -------- Read partition and save image to file.
        -I <initrd> ----------- initrd file. Null initrd is default.
        -K <kernel> ----------- Kernel image file such as zImage or Image.
        -L <bootloader> ------- Bootloader such as cboot.bin or u-boot-dtb.bin.
        -M <mts boot> --------- MTS boot file such as mts_si.
        -N <nfsroot> ---------- i.e. <my IP addr>:/my/exported/nfs/rootfs.
        -R <rootfs dir> ------- Sample rootfs directory.
        -S <size> ------------- Rootfs size in bytes. Valid only for internal
                                rootdev. KiB, MiB, GiB short hands are allowed,
                                for example, 1GiB means 1024 * 1024 * 1024 bytes.
        -T <ext num sectors> ---The number of the sectors of the external storage device.
                                The default value is 119537664 if this option is not set.
        -U <int num sectors> ---The number of the sectors of the internal storage device.
                                The default value is 119537664 if this option is not set.
        -Z -------------------- Print configurations and then exit.
        --no-flash ------------ perform all steps except physically flashing the board.
                                This will create a system.img.
                                If combined with --rcm-boot, the command will generate rcmboot_blob.
                                If combined with --read-info, the command will generate readinfocmd.txt.
        --external-device------ Generate flash images for external devices
        --sparseupdate--------- only flash partitions that have changed. Currently only support SPI flash memory
        --no-systemimg -------- Do not create or re-create system.img.
        --bup ----------------- Generate bootloader update payload(BUP).
        --single-image-bup <part name> Generate specified single image BUP, this must work with --bup.
        --bup-type <type> ----- Generate specific type bootloader update payload(BUP), such as bl or kernel.
        --multi-spec----------- Enable support for building multi-spec BUP.
        --clean-up------------- Clean up BUP buffer when multi-spec is enabled.
        --usb-instance <id> --- Specify the USB instance to connect to;
                                <id> = USB port path (e.g. 3-14).
        --no-root-check ------- Typical usage of this script require root permissions.
                                Pass this option to allow running the script as a
                                regular user, in which case only specific combinations
                                of command-line options will be functional.
        --uefi-keys <keys_conf> Specify UEFI keys configuration file.
        --rcm-boot ------------ Do RCM boot instead of physically flashing the board.
        --sign ---------------- Sign images and store them under "bootloader/signed"
                                directory. The board will not be physically flashed.
        --image --------------- Specify the image to be written into board.
        --boot-chain-flash <c>  Flash only a specific boot chain (ex. "A, "B", "all").
                                Defaults to "all", inputs are case insensitive.
                                Not suitable for production.
        --boot-chain-select <c> Specify booting chain (ex. "A" or "B") after the board is flashed.
                                Defaults to "A", inputs are case insensitive.
        --pv-crt -------------- The certificate for the key that is used to sign cpu_bootloader
        --with-systemimg ------ Generate system images also when using -k option
        --pv-enc <enc_key>----- The encryption key that is used to encrypt cpu_bootloader.
        --uefi-enc <uefi_enc_key> Key file (0x23: 32-byte) to encrypt UEFI payloads
        --uda-dir-------------- Directory to store user data that will be encrypted in UDA partition.
        --generic-passphrase -- Use generic passphrase for disk encryption.
        --disable-random-iv --- Disable generation of random IV, SALT1, SALT2 and DERSTR.
        --read-info ----------- Read and display board related info, fuse info (based on fuse_t234.xml),
                                and EEPROM content.
        --reuse-uuid ---------  Reuse uuid which is already generated first time.
        --qspi-only  ---------  Flash QSPI device only.
        --passthrough --------  Additional options to passthrough the tegraflash command
        --read-ramcode -------  Generate read_ramcode script in the flashcmd.txt command script.
        --gen-read-eeprom ----  Generate read_eeprom script with all signed binaries. User later can run
                                read_eeprom.sh from host to dump EEPROM content on a target device after
                                putting the target device in RCM mode. Since all binaries are signed already,
                                there is no key needed even the device has PKC fuse burned.
        --hsm ----------------  Enable HSM for signing (and encrypting) images. This option must be used with
                                -u option (image signing). -v or --pv-enc is optional (image encryption).
        --debug --------------  Enable debug mode, and enable '--keep' option to tegraflash.py
                                For now, the debug mode will enable saving of unencrypted and unsigned files
                                for UEFI encryption/signing debugging.
        --gen-systemimg-only -  Generate system.img only, program then exits.

EOF
	exit $retval;
}

setdflt ()
{
	local var="$1";
	if [ "${!var}" = "" ]; then
		eval "${var}=\"$2\"";
	fi;
}

setval ()
{
	local var="$1";
	local val="$2";
	if [ "${!val}" = "" ]; then
		echo "Error: missing $val not defined.";
		exit 1;
	fi;
	eval "${var}=\"${!val}\"";
}

mkfilesoft ()
{
	local var="$1";
	local varname="$1name";

	eval "${var}=\"$2\"";
	if [ "${!var}" = "" -o ! -f "${!var}" ]; then
		if [ "$3" != "" -a -f "$3" ]; then
			eval "${var}=\"$3\"";
		fi;
	fi;
	if [ "${!var}" != "" ]; then
		if [ ! -f "${!var}" ]; then
			eval "${var}=\"\"";
			eval "${varname}=\"\"";
			return 1;
		fi;
		eval "${var}=\"$(readlink -f "${!var}")\"";
		eval "${varname}=\"$(basename "${!var}")\"";
	fi;
	return 0;
}

mkfilepath ()
{
	local var="$1";
	local varname="$1name";

	eval "${var}=\"$2\"";
	setdflt "${var}" "$3";
	if [ "${!var}" != "" ]; then
		eval "${var}=\"$(readlink -f "${!var}")\"";
		if [ ! -f "${!var}" ]; then
			if [ -z "${IGNORE_BOARD_CONF_ERROR}" ]; then
				echo "Error: missing $var (${!var}).";
				usage allknown 1;
			fi;
			dd if=/dev/zero of="${!var}" bs=512 count=100;
		fi;
		eval "${varname}=\"$(basename "${!var}")\"";
	fi;
}

mkdirpath ()
{
	local var="$1";
	eval "${var}=$2";
	setdflt "$1" "$3";
	if [ "${!var}" != "" ]; then
		eval "${var}=\"$(readlink -f "${!var}")\"";
		if [ ! -d "${!var}" ]; then
			if [ -z "${IGNORE_BOARD_CONF_ERROR}" ]; then
				echo "Error: missing $var (${!var}).";
				usage allknown 1;
			fi;
			mkdir -p "${!var}";
		fi;
	fi;
}

getsize ()
{
	local var="$1";
	local val="$2";
	if [[ ${!val} != *[!0-9]* ]]; then
		eval "${var}=${!val}";
	elif [[ (${!val} == *KiB) && (${!val} != *[!0-9]*KiB) ]]; then
		eval "${var}=$(( ${!val%KiB} * 1024 ))";
	elif [[ (${!val} == *MiB) && (${!val} != *[!0-9]*MiB) ]]; then
		eval "${var}=$(( ${!val%MiB} * 1024 * 1024 ))";
	elif [[ (${!val} == *GiB) && (${!val} != *[!0-9]*GiB) ]]; then
		eval "${var}=$(( ${!val%GiB} * 1024 * 1024 * 1024))";
	else
		echo "Error: Invalid $1: ${!val}";
		exit 1;
	fi;
}

validatePartID ()
{
	local idx=0;
	declare -A cf;

	while read aline; do
		if [ "$aline" != "" ]; then
			arr=( $(echo $aline | tr '=' ' ') );
			if [ "${arr[1]}" == "name" ]; then
				if [ "${arr[3]}" == "id" ]; then
					cf[$idx,1]="${arr[2]}";
					cf[$idx,0]="${arr[4]}";
				else
					cf[$idx,0]="${arr[2]}";
				fi
				idx=$((idx+1));
			fi
		fi;
	done < $4;

	if [ "${arr[3]}" == "id" ]; then
		for ((i = 0; i < idx; i++)) do
			if [ "\"$3\"" = "${cf[$i,0]}" -o  \
			     "\"$3\"" = "${cf[$i,1]}" ]; then
				eval "$1=${cf[$i,0]}";
				eval "$2=${cf[$i,1]}";
			return 0;
			fi;
		done;
		echo "Error: invalid partition id ($3)";
		exit 1;
	else
		return 0;
	fi;
}

cp2local ()
{
	local src=$1;
	if [ "${!src}" = "" ]; then return 1; fi;
	if [ ! -f "${!src}" ]; then return 1; fi;
	if [ "$2" = "" ];      then return 1; fi;
	if [ -f "$2" -a "${!src}" = "$2" ]; then
		local sum1=$(sum "${!src}");
		local sum2=$(sum "$2");
		if [ "$sum1" = "$sum2" ]; then
			echo "Existing ${src}($2) reused.";
			return 0;
		fi;
	fi;
	echo -n "copying ${src}(${!src})... ";
	cp -f "${!src}" "$2";
	chkerr;
	return 0;
}

chsuffix ()
{
	local var="$1";
	local fname=$(basename "$2");
	local OIFS=${IFS};
	IFS='.';
	na=($fname);
	IFS=${OIFS};
	eval "${var}=${na[0]}.${3}";
}

pad_file_aligned ()
{
	local __file="$1";
	local __alignment="$2";
	local __padvalue="$3";
	local __padstring="";

	filesize=$(stat --format=%s "${__file}");
	rem=$(( filesize % __alignment ));
	if (( rem > 0 )); then
		rem=$(( __alignment - rem ));
		for ((i = 0 ; i < rem ; i++)); do
			__padstring+=${__padvalue};
		done;
		echo -e -n "${__padstring}" >> "${__file}";
	fi;
}

build_fsimg ()
{
	local __localsysfile="$1";
	local __fillpat="$2";
	local __rootfssize="$3";
	local __rootfs_type="$4";
	local __rootfs_dir="$5";
	local __cmdline="$6";
	local __do_sign="$7";
	local _extlinux_conf="extlinux/extlinux.conf"

	echo "Making ${__localsysfile}... ";

	local bcnt=$(( ${__rootfssize} / 512 ));
	local bcntdiv=$(( ${__rootfssize} % 512 ));
	if [ ${bcnt} -eq 0 -o ${bcntdiv} -ne 0 ]; then
		echo "Error: ${__rootfs_type} file system size has to be 512 bytes allign.";
		exit 1;
	fi
	if [ "${__rootfs_type}" != "FAT32" ] && [ ! -f "${__rootfs_dir}/boot/${_extlinux_conf}" ]; then
		# In case the rootfs is originated from foreign source,
		# just copy extlinux.conf to /boot to make the rest of
		# script happy.
		if [ -z "${IGNORE_BOARD_CONF_ERROR}" ]; then
			echo "${__rootfs_dir}/boot/${_extlinux_conf} is not found, exiting...";
			exit 1
		fi;
		mkdir -p "${__rootfs_dir}/boot/extlinux";
		cp "${BL_DIR}/extlinux.conf" "${__rootfs_dir}/boot/extlinux";
	fi
	if [ "${__fillpat}" != "" -a "${__fillpat}" != "0" ]; then
		local fc=$(printf '%d' ${__fillpat});
		local fillc=$(printf \\\\$(printf '%02o' $fc));
		< /dev/zero head -c ${__rootfssize} | tr '\000' ${fillc} > ${__localsysfile};
		chkerr "making ${__localsysfile} with fillpattern($fillc}) failed.";
	else
		truncate --size ${__rootfssize} ${__localsysfile};
		chkerr "making ${__localsysfile} with zero fillpattern failed.";
	fi;
	loop_dev="$(losetup --show -f "${__localsysfile}")";
	chkerr "mapping ${__localsysfile} to loop device failed.";
	if [ "${__rootfs_type}" = "FAT32" ]; then
		mkfs.msdos -I -F 32 "${loop_dev}" > /dev/null 2>&1;
	else
		mkfs -t ${__rootfs_type} "${loop_dev}" > /dev/null 2>&1;
	fi;
	chkerr "formating ${__rootfs_type} filesystem on ${__localsysfile} failed.";
	mkdir -p mnt;		chkerr "make $4 mount point failed.";
	mount "${loop_dev}" mnt;	chkerr "mount ${__localsysfile} failed.";
	mkdir -p mnt/boot/dtb;	chkerr "make ${__localsysfile}/boot/dtb failed.";
	cp -f "${kernel_fs}" mnt/boot;
	chkerr "Copying ${kernel_fs} failed.";
	_dtbfile=${__rootfs_dir}/boot/${dtbfilename};
	if [ -f "${_dtbfile}" ]; then
		cp -f "${_dtbfile}" "mnt/boot/dtb/${dtbfilename}";
		chkerr "populating ${_dtbfile} to ${__localsysfile}/boot/dtb failed.";
	fi;
	if [ "${__rootfs_type}" = "FAT32" ]; then
		touch -f mnt/boot/cmdline.txt > /dev/null 2>&1;
		chkerr "Creating cmdline.txt failed.";
		echo -n -e "${__cmdline}" >mnt/boot/cmdline.txt;
		chkerr "Writing cmdline.txt failed.";
	else
		pushd mnt > /dev/null 2>&1;
		echo -n -e "\tpopulating rootfs from ${__rootfs_dir} ... ";
		(cd "${__rootfs_dir}"; tar --xattrs --xattrs-include=* -cpf - *) \
			| tar --xattrs --xattrs-include=* -xpf - ;
		chkerr "Failed. Your APPSIZE might be too small.";

		# Populate extlinux.conf if "$cmdline" exists
		if [ "${__cmdline}" != "" ]; then
			# Add the "$cmdline" at the APPEND line if it does not exist.
			echo -n -e "\tpopulating /boot/${_extlinux_conf} ... ";
			rootfs_found=$(grep -cE "${__cmdline}" "./boot/${_extlinux_conf}");
			if [ "${rootfs_found}" == "0" ];then
				sed -i "/^[ \t]*APPEND/s|\$| ${__cmdline}|" "./boot/${_extlinux_conf}";
				chkerr;
			fi;
		fi;
		if [ "${__do_sign}" = "True" ]; then
			local kernel_fs_basename;
			kernel_fs_basename=$(basename "${kernel_fs}");
			echo -n -e "\tgenerating signed file of ${kernel_fs_basename} ... ";
			uefi_signimage "./boot/${kernel_fs_basename}" "${uefi_db_key}" "${uefi_db_cert}" "nosplit" "True"; chkerr;
			echo -n -e "\tgenerating sig file of ${dtbfilename} ... ";
			uefi_signimage "./boot/dtb/${dtbfilename}" "${uefi_db_key}" "${uefi_db_cert}" "split" "True"; chkerr;
			echo -n -e "\tgenerating sig file of initrd ... ";
			uefi_signimage "./boot/initrd" "${uefi_db_key}" "${uefi_db_cert}" "split" "True"; chkerr;
			echo -n -e "\tgenerating sig file of extlinux.conf ... ";
			# Signing tool will pad extlinux.conf with 0x80 to be 16-byte aligned.
			# This pad byte of 0x80 may cause some utilities fail to read the entire
			# extlinux.conf.
			# So, pad extlinux.conf to 16-byte aligned with linefeed.
			pad_file_aligned "./boot/${_extlinux_conf}" 16 "\x0a";
			# Note: no encryption to extlinux.conf file
			uefi_signimage "./boot/${_extlinux_conf}" "${uefi_db_key}" "${uefi_db_cert}" "split" "False"; chkerr;
		fi
		popd > /dev/null 2>&1;
	fi;
	echo -e -n "\tSync'ing ${__localsysfile} ... ";
	sync; sync; sleep 5;	# Give FileBrowser time to terminate gracefully.
	echo "done.";
	umount mnt > /dev/null 2>&1;
	losetup -d "${loop_dev}" > /dev/null 2>&1;
	rmdir mnt > /dev/null 2>&1;

	if [[ "${__fillpat}" != "" && -x mksparse && -z "${UNIFIED_FLASH}" ]]; then
		echo -e -n "\tConverting RAW image to Sparse image... ";
		mv -f ${__localsysfile} ${__localsysfile}.raw;
		if [ "${BLBlockSize}" != "" ]; then
			blblksizeoption="-b $BLBlockSize";
		fi;
		./mksparse ${blblksizeoption} --fillpattern=${__fillpat} ${__localsysfile}.raw ${__localsysfile}; chkerr;
	else
		cp "${__localsysfile}" "${__localsysfile}".raw;
	fi
	echo "${__localsysfile} built successfully. ";
}

get_fuse_level ()
{
	local rcmcmd;
	local inst_args="";
	local idval_1="";
	local idval_2="";
	local flval="";
	local baval="None";
	local flvar="$1";
	local hivar="$2";
	local bavar="$3";
	local minor="";

	if [ -f "${BL_DIR}/tegrarcm_v2" ]; then
		rcmcmd="tegrarcm_v2";
	elif [ -f "${BL_DIR}/tegrarcm" ]; then
		rcmcmd="tegrarcm";
	else
		echo "Error: tegrarcm is missing.";
		exit 1;
	fi;
	if [ "${CHIPID}" = "0x23" ]; then
		extra_args="--new_session --chip 0x23";
	elif [ "${CHIPID}" = "0x23 9" ]; then
		extra_args="--new_session --chip '0x23 9'";
	elif [ "${CHIPID}" = "0x26" ]; then
		extra_args="--new_session --chip '0x26'";
	fi;
	if [ -n "${usb_instance}" ]; then
		inst_args="--instance ${usb_instance}";
	fi;
	pushd "${BL_DIR}" > /dev/null 2>&1;
	ECID=$(./${rcmcmd} ${extra_args} ${inst_args} --uid | grep BR_CID | cut -d' ' -f2);
	popd > /dev/null 2>&1;
	if [ "${ECID}" != "" ] && [ "${ECID}" != 0x00000 ]; then
		idval_1="0x${ECID:3:2}";
		eval "${hivar}=\"${idval_1}\"";
		idval_2="0x${ECID:6:2}";
		if [ "${CHIPID}" = "0x23 9" ]; then
			minor="${ECID:8:1}";
		fi;

		flval="${ECID:2:1}";
		baval="";
		if [ "${idval_2}" = "0x26" ]; then
			local tmp_1;
			flval="0x${ECID:2:1}";
			flval=$(printf %x "$((flval & 0xe))");
			tmp_1=$(printf %2.2x "$((idval_1 & 0xf0))");
			flval="${flval}${tmp_1}";
			# Bit 127 - NV production mode
			# Bit 126 - OEM production mode
			# Bit 125 - OEM key valid = PKC
			# Bit 123 - encryption enable = SBK
			case ${flval} in
			000) flval="fuselevel_nofuse"; ;;
			800) flval="fuselevel_production"; baval="NS"; ;;
			e00) flval="fuselevel_production"; baval="PKC"; ;;
			e80) flval="fuselevel_production"; baval="SBKPKC"; ;;
			*) echo "Error: Invalid fuse configuration 0x${flval}";
				exit 1;
			esac;
			hwchipid="${idval_2}";
			if [ "${minor}" != "" ]; then
				hwchipid="${hwchipid} ${minor}";
			fi;
			hwchiprev="${ECID:5:1}";
		elif [ "${idval_2}" = "0x23" ] || [ "${idval_2}" = "0x23 9" ]; then
			local tmp_1;
			flval="0x${ECID:2:1}";
			flval=$(printf %x "$((flval & 0x8))");
			tmp_1=$(printf %2.2x "$((idval_1 & 0xf0))");
			flval="${flval}${tmp_1}";
			case ${flval} in
			000) flval="fuselevel_nofuse"; ;;
			800) flval="fuselevel_production"; baval="NS"; ;;
			# 810 - 3K RSA
			# 820 - ECDSA P-256
			# 830 - ECDSA P-512
			# 840 - ED25519
			# 850 - XMSS
			810|820|830|840|850)
				flval="fuselevel_production"; baval="PKC"; ;;
			# 890 - SBK + 3K RSA
			# 8a0 - SBK + ECDSA P-256
			# 8b0 - SBK + ECDSA P-512
			# 8c0 - SBK + ED25519
			# 8d0 - SBK + XMSS
			890|8a0|8b0|8c0|8d0)
				flval="fuselevel_production"; baval="SBKPKC"; ;;
			*) echo "Error: Invalid fuse configuration 0x${flval}";
				exit 1;
			esac;
			SKIPUID="--skipuid";
			hwchipid="${idval_2}";
			if [ "${minor}" != "" ]; then
				hwchipid="${hwchipid} ${minor}";
			fi;
			hwchiprev="${ECID:5:1}";
		elif [ "${idval_1}" = "0x21" ] || [ "${idval_1}" = "0x12" ] || \
			[ "${idval_1}" = "0x00" ] && [ "${idval_2}" = "0x21" ]; then
			case ${flval} in
			0|1|2) flval="fuselevel_nofuse"; ;;
			3)     flval="fuselevel_production"; ;;
			4)     flval="fuselevel_production"; baval="NS"; ;;
			5)     flval="fuselevel_production"; baval="SBK"; ;;
			6)     flval="fuselevel_production"; baval="PKC"; ;;
			*)     flval="fuselevel_unknown"; ;;
			esac;
			SKIPUID="--skipuid";
			if [ "${idval_1}" = "0x00" ]; then
				eval "${hivar}=\"${idval_2}\"";
			fi;
		else
			case ${flval} in
			0|1|2) flval="fuselevel_nofuse"; ;;
			8|c)   flval="fuselevel_production"; baval="NS"; ;;
			9|d)   flval="fuselevel_production"; baval="SBK"; ;;
			a)     flval="fuselevel_production"; baval="PKC"; ;;
			e)     flval="fuselevel_production"; baval="SBKPKC"; ;;
			*)     flval="fuselevel_unknown"; ;;
			esac;
		fi;
		eval "${flvar}=\"${flval}\"";
		eval "${bavar}=\"${baval}\"";
	else
		eval "${flvar}=\"\"";
		eval "${bavar}=\"\"";
		eval "${hivar}=\"\"";
	fi;
}

function get_full_path ()
{
	local val="$1";
	local result="$2";
	local fullpath;
	fullpath=$(readlink -f ${val});	# null if path is invalid
	if [ "${fullpath}" == "" ]; then
		echo "Invalid path/filename ${val}";
		exit 1;
	fi;
	eval "${result}=${fullpath}";
}

#
# Prepare common args for t234
#
t234_prepare_common_args ()
{
	local __args="";

	if [ -n "${usb_instance}" ]; then
		__args+="--instance ${usb_instance} ";
	fi;
	if [ "${CHIPMAJOR}" != "" ]; then
		__args+="--chip \"${CHIPID} ${CHIPMAJOR}\" ";
	else
		__args+="--chip \"${CHIPID}\" ";
	fi;
	__args+="--applet \"${LDK_DIR}/${SOSFILE}\" ";
	__args+="${SKIPUID} ";
	SKIPUID="";
	__args+="--cfg ${readinfofilename} "
	__args+="--dev_params ${emc_fuse_dev_paramsname} ";
	cp2local device_config "${BL_DIR}/${device_configname}"
	cp2local misc_config "${BL_DIR}/${misc_configname}"
	__args+="--device_config ${device_configname} --misc_config ${misc_configname} "
	if [ -f "${uphy_config}" ]; then
		cp2local uphy_config "${BL_DIR}/${uphy_configname}";
		__args+="--uphy_config ${uphy_configname} "
	fi;
	__args+="--bins \"mb2_applet ${mb2appletname}\" ";
	if [ "${keyfile}" != "" ]; then
		__args+="--key \"${keyfile}\" ";
	fi;
	if [ "${sbk_keyfile}" != "" ]; then
		__args+="--encrypt_key \"${sbk_keyfile}\" ";
		if [ "${disable_random_iv}" = "True" ]; then
			echo "Disable random IV"
			__args+="--disable_random_iv ";
		fi
	fi
	cp2local emc_fuse_dev_params "${BL_DIR}/${emc_fuse_dev_paramsname}";
	if [ "${fuselevel}" = "fuselevel_production" ]; then
		sed -i "s/preprod_dev_sign = <1>/preprod_dev_sign = <0>/" "${BL_DIR}/${emc_fuse_dev_paramsname}";
	fi
	if [ "${minratchet_config}" != "" ]; then
		cp2local minratchet_config "${BL_DIR}/${minratchet_configname}";
		__args+="--minratchet_config ${minratchet_configname} ";
	fi
	if [ "${hsm_enable}" -eq 1 ]; then
		check_hsm_support
		__args+="--hsm ";
	fi
	if [ "${debug_mode}" -eq 1 ]; then
		__args+="--keep ";
	fi
	eval "$1='${__args}'"
}

#
# Prepare common args for t264
#
t264_prepare_common_args ()
{
	local __args="";

	if [ -n "${usb_instance}" ]; then
		__args+="--instance ${usb_instance} ";
	fi;
	if [ "${CHIPMAJOR}" != "" ]; then
		__args+="--chip \"${CHIPID} ${CHIPMAJOR}\" ";
	else
		__args+="--chip \"${CHIPID}\" ";
	fi;
	__args+="--applet \"${LDK_DIR}/${SOSFILE}\" ";

	cp2local emc_fuse_dev_params "${BL_DIR}/${emc_fuse_dev_paramsname}";
	if [ "${fuselevel}" = "fuselevel_production" ]; then
		sed -i "s/preprod_dev_sign = <1>/preprod_dev_sign = <0>/" \
			"${BL_DIR}/${emc_fuse_dev_paramsname}";
	fi

	mkfilesoft rcmbootbctcfg    "${RCMBOOT_BCT_CFG}"    "";
	mkfilepath sdrambctfile      "${BCTFILE}"    "${TARGET_DIR}/BCT/${EMC_BCT}";
	mkfilesoft uphy_config    "${TARGET_DIR}/BCT/${UPHY_CONFIG}" "";
	mkfilesoft device_config  "${TARGET_DIR}/BCT/${DEVICE_CONFIG}" "";
	mkfilesoft misc_config    "${TARGET_DIR}/BCT/${MISC_CONFIG}" "";
	mkfilesoft pinmux_config  "${TARGET_DIR}/BCT/${PINMUX_CONFIG}" "";
	mkfilesoft gpioint_config  "${TARGET_DIR}/BCT/${GPIOINT_CONFIG}" "";
	mkfilesoft pmic_config    "${TARGET_DIR}/BCT/${PMIC_CONFIG}" "";
	mkfilesoft pmc_config     "${TARGET_DIR}/BCT/${PMC_CONFIG}" "";
	mkfilesoft deviceprod_config    "${TARGET_DIR}/BCT/${DEVICEPROD_CONFIG}" "";
	mkfilesoft prod_config    "${TARGET_DIR}/BCT/${PROD_CONFIG}" "";
	mkfilesoft scr_config     "${TARGET_DIR}/BCT/${SCR_CONFIG}" "";
	mkfilesoft wb0sdram	"${TARGET_DIR}/BCT/${WB0SDRAM_BCT}"	"";
	mkfilesoft minratchet_config "${TARGET_DIR}/BCT/${MINRATCHET_CONFIG}" "";
	mkfilesoft bootrom_config "${TARGET_DIR}/BCT/${BOOTROM_CONFIG}" "";
	mkfilesoft mb2bct_cfg        "${TARGET_DIR}/BCT/${MB2_BCT}" "";
	mkfilesoft bpmp_mem_config   "${TARGET_DIR}/BCT/${BPMP_MEM_CONFIG}"  "";

	diagcfg="diag_${rcmbootbctcfgname}"
	cp2local  rcmbootbctcfg "${BL_DIR}/${diagcfg}";
	cp2local sdrambctfile "${BL_DIR}/${sdrambctfilename}"
	cp2local uphy_config "${BL_DIR}/${uphy_configname}";
	cp2local device_config "${BL_DIR}/${device_configname}";
	cp2local misc_config "${BL_DIR}/${misc_configname}";
	cp2local pinmux_config "${BL_DIR}/${pinmux_configname}";
	cp2local gpioint_config "${BL_DIR}/${gpioint_configname}";
	cp2local pmic_config "${BL_DIR}/${pmic_configname}";
	cp2local pmc_config "${BL_DIR}/${pmc_configname}";
	cp2local deviceprod_config "${BL_DIR}/${deviceprod_configname}";
	cp2local prod_config "${BL_DIR}/${prod_configname}";
	cp2local scr_config "${BL_DIR}/${scr_configname}";
	cp2local mb2bct_cfg "${BL_DIR}/${mb2bct_cfgname}";
	cp2local wb0sdram  "${BL_DIR}/${wb0sdramname}";
	cp2local bootrom_config "${BL_DIR}/${bootrom_configname}";
	cp2local bpmp_mem_config "${BL_DIR}/${bpmp_mem_configname}";
	cp2local minratchet_config "${BL_DIR}/${minratchet_configname}";
	set_value_in_bct_cfg "${bpmp_mem_configname}" "brbct_cfg/bpmp_mem_cfg" "${BL_DIR}/${diagcfg}"
	set_value_in_bct_cfg "${bootrom_configname}" "brbct_cfg/brcommand" "${BL_DIR}/${diagcfg}"
	set_value_in_bct_cfg "${wb0sdramname}" "brbct_cfg/wb0sdram" "${BL_DIR}/${diagcfg}"
	set_value_in_bct_cfg "${minratchet_configname}" "brbct_cfg/minratchet" "${BL_DIR}/${diagcfg}"
	set_value_in_bct_cfg "${deviceprod_configname}" "brbct_cfg/deviceprod" "${BL_DIR}/${diagcfg}"
	set_value_in_bct_cfg "${prod_configname}" "brbct_cfg/prod" "${BL_DIR}/${diagcfg}"
	set_value_in_bct_cfg "${scr_configname}" "brbct_cfg/scr" "${BL_DIR}/${diagcfg}"
	set_value_in_bct_cfg "${mb2bct_cfgname}" "brbct_cfg/mb2bctcfg" "${BL_DIR}/${diagcfg}"
	set_value_in_bct_cfg "${uphy_configname}" "brbct_cfg/uphy" "${BL_DIR}/${diagcfg}"
	set_value_in_bct_cfg "${device_configname}" "brbct_cfg/device" "${BL_DIR}/${diagcfg}"
	set_value_in_bct_cfg "${misc_configname}" "brbct_cfg/misc" "${BL_DIR}/${diagcfg}"
	set_value_in_bct_cfg "${pinmux_configname}" "brbct_cfg/pinmux" "${BL_DIR}/${diagcfg}"
	set_value_in_bct_cfg "${gpioint_configname}" "brbct_cfg/gpioint" "${BL_DIR}/${diagcfg}"
	set_value_in_bct_cfg "${pmic_configname}" "brbct_cfg/pmic" "${BL_DIR}/${diagcfg}"
	set_value_in_bct_cfg "${pmc_configname}" "brbct_cfg/pmc" "${BL_DIR}/${diagcfg}"
	set_value_in_bct_cfg "${emc_fuse_dev_paramsname}" "brbct_cfg/dev_param" "${BL_DIR}/${diagcfg}"
	set_value_in_bct_cfg "${sdrambctfilename}" "brbct_cfg/sdram" "${BL_DIR}/${diagcfg}"

	__args+="--rcmboot_bct_cfg ${diagcfg} --rcmboot_pt_layout ${READINFOFILE} "

	if [ "${keyfile}" != "" ]; then
		__args+="--key \"${keyfile}\" ";
	fi;
	if [ "${sbk_keyfile}" != "" ]; then
		__args+="--encrypt_key \"${sbk_keyfile}\" ";
		if [ "${disable_random_iv}" = "True" ]; then
			echo "Disable random IV"
			__args+="--disable_random_iv ";
		fi
	fi;
	if [ "${hsm_enable}" -eq 1 ]; then
		check_hsm_support
		__args+="--hsm ";
	fi
	if [ "${pkc_keylistfile}" != "" ]; then
		__args+="--key_list \"${pkc_keylistfile}\" ";
	fi;
	if [ "${minratchet_config}" != "" ]; then
		__args+="--minratchet_config ${minratchet_configname} ";
	fi

	eval "$1='${__args}'"
}

t234_get_board_version ()
{
	local args="";
	local __board_id=$1;
	local __board_FAB=$2;
	local __board_sku=$3;
	local __board_revision=$4;
	local __emcfuse_bin=$5
	local __board_ramcode=$6
	local command;
	local boardid;
	local boardFAB;
	local boardsku;
	local boardrevision;
	local boardRamcode;

	t234_prepare_common_args args

	command=""
	if [ "${emcfuse}" != "" ]; then
		chsuffix emcfusebin "${emcfusename}" "bin";
		if [ -f "${BL_DIR}"/"${emcfusebin}" ]; then
			rm "${BL_DIR}"/"${emcfusebin}"
		fi;
		cp2local emcfuse "${BL_DIR}/${emcfusename}";
		command+="readfuses ${emcfusebin} ${emcfusename}; ";
	fi;
	cp2local emc_fuse_dev_params "${BL_DIR}/${emc_fuse_dev_paramsname}";
	if [ "${fuselevel}" = "fuselevel_production" ]; then
		sed -i "s/preprod_dev_sign = <1>/preprod_dev_sign = <0>/" "${BL_DIR}/${emc_fuse_dev_paramsname}";
	fi
	command+="dump eeprom cvm cvm.bin; dump try_custinfo custinfo_out.bin;"
	command+=" reboot recovery"
	args+="--cmd \"${command}\" ";
	local cmd="./tegraflash.py ${args}";
	pushd "${BL_DIR}" > /dev/null 2>&1;
	if [ -f "rcm_state" ]; then
		rm rcm_state;
	fi;
	echo "${cmd}";
	if [ "${read_info}" == "True" ] && [ "${no_flash}" -eq 1 ]; then
		# Expand $cmd to display Fuse Info and EEPROM content and save it to readinfocmd.txt
		readinfocmdfile="${BL_DIR}/readinfocmd.txt";
		echo "Saving readinfo command in ${readinfocmdfile}";
		prepare_readinfocmd "${cmd}" "${readinfocmdfile}";
		popd > /dev/null 2>&1 || exit;
		return
	fi
	eval "${cmd}";
	chkerr "Reading board information failed.";
	if [ "${SKIP_EEPROM_CHECK}" = "" ]; then
		if [[ -e "cvm.bin" ]]; then
			boardid=$(./chkbdinfo -i cvm.bin);
			boardFAB=$(./chkbdinfo -f cvm.bin);
			boardsku=$(./chkbdinfo -k cvm.bin);
			boardrevision=$(./chkbdinfo -r cvm.bin);
			chkerr "Parsing cvm.bin failed.";
		else
			echo "cvm.bin does not exist"
			exit 1
		fi
	fi;

	# Get ramcode from the board
	boardRamcode=$(./chkbdinfo -R chip_info.bin_bak);
	chkerr "Getting ramcode failed.";
	boardRamcode="$(echo "${boardRamcode}" | cut -d: -f 4)";
	boardRamcode="$((16#${boardRamcode} % 16))"

	if [ "${BOARDID}" != "" ]; then
		boardid="${BOARDID}";
	fi;

	if [ "${FAB}" != "" ]; then
		boardFAB="${FAB}";
	fi;

	if [ "${BOARDSKU}" != "" ]; then
		boardsku="${BOARDSKU}";
	fi;

	if [ "${BOARDREV}" != "" ]; then
		boardrevision="${BOARDREV}";
	fi;

	if [ "${RAMCODE_ID}" != "" ]; then
		boardRamcode="${RAMCODE_ID}";
	fi;

	popd > /dev/null 2>&1;
	eval ${__board_id}="${boardid}";
	eval ${__board_FAB}="${boardFAB}";
	eval ${__board_sku}="${boardsku}";
	eval ${__board_revision}="${boardrevision}";
	eval "${__emcfuse_bin}"="${emcfusebin}";
	custinfofilename="custinfo_out.bin"
	eval ${__board_ramcode}="${boardRamcode}";
}

t264_get_board_version ()
{
	local args="";
	local __board_id=$1;
	local __board_FAB=$2;
	local __board_sku=$3;
	local __board_revision=$4;
	local __emcfuse_bin=$5;
	local __board_ramcode=$6;
	local __platform_id=$7;
	local __boot_device=$8;
	local command;
	local boardid;
	local boardFAB;
	local boardsku;
	local boardrevision;
	local boardRamcode;
	local platformid;
	local bootdevice;

	t264_prepare_common_args args

	command=""
	if [ "${emcfuse}" != "" ]; then
		chsuffix emcfusebin "${emcfusename}" "bin";
		if [ -f "${BL_DIR}"/"${emcfusebin}" ]; then
			rm "${BL_DIR}"/"${emcfusebin}"
		fi;
		cp2local emcfuse "${BL_DIR}/${emcfusename}";
		command+="readfuses ${emcfusebin} ${emcfusename}; ";
	fi;

	command+="dump eeprom cvm cvm.bin; dump eeprom cvb cvb.bin; dump try_custinfo custinfo_out.bin; reboot recovery"
	args+="--cmd \"${command}\" ";
	local cmd="./tegraflash.py ${args}";
	pushd "${BL_DIR}" > /dev/null 2>&1;
	if [ "${read_info}" == "True" ] && [ "${no_flash}" -eq 1 ]; then
		# Expand $cmd to display Fuse Info and EEPROM content and save it to readinfocmd.txt
		readinfocmdfile="${BL_DIR}/readinfocmd.txt";
		echo "Saving readinfo command in ${readinfocmdfile}";
		prepare_readinfocmd "${cmd}" "${readinfocmdfile}";
		popd > /dev/null 2>&1 || exit;
		return
	fi;

	# proceed to dry-run for offline image generation
	if [ -z "${hwchipid}" ]; then
		cmd+=" --no_flash"
	fi
	if [ -f "rcm_state" ]; then
		rm rcm_state;
	fi;
	echo "${cmd}";
	eval "${cmd}";
	# return for dry-run scenario
	if [ -z "${hwchipid}" ]; then
		popd > /dev/null 2>&1;
		return
	fi
	chkerr "Reading board information failed.";
	if [ "${SKIP_EEPROM_CHECK}" = "" ]; then
		if [[ -e "cvm.bin" ]]; then
			boardid=$(./chkbdinfo -i cvm.bin);
			boardFAB=$(./chkbdinfo -f cvm.bin);
			boardsku=$(./chkbdinfo -k cvm.bin);
			boardrevision=$(./chkbdinfo -r cvm.bin);
			chkerr "Parsing cvm.bin failed.";
		else
			echo "cvm.bin does not exist"
			exit 1
		fi
		if [[ -e "cvb.bin" ]]; then
			platformid=$(./chkbdinfo -q cvb.bin);
			bootdevice=$(./chkbdinfo -Q cvb.bin);
			chkerr "Parsing board information failed.";
		else
			echo "cvb.bin does not exist"
		fi
	fi;

	# Get ramcode from the board
	boardRamcode=$(./chkbdinfo -R chip_info.bin_bak);
	chkerr "Getting ramcode failed.";
	boardRamcode="$(echo "${boardRamcode}" | cut -d: -f 4)";
	boardRamcode="$((16#${boardRamcode} % 16))"

	if [ "${BOARDID}" != "" ]; then
		boardid="${BOARDID}";
	fi;

	if [ "${FAB}" != "" ]; then
		boardFAB="${FAB}";
	fi;

	if [ "${BOARDSKU}" != "" ]; then
		boardsku="${BOARDSKU}";
	fi;

	if [ "${BOARDREV}" != "" ]; then
		boardrevision="${BOARDREV}";
	fi;

	if [ "${RAMCODE_ID}" != "" ]; then
		boardRamcode="${RAMCODE_ID}";
	fi;

	popd > /dev/null 2>&1;
	eval ${__board_id}="${boardid}";
	eval ${__board_FAB}="${boardFAB}";
	eval ${__board_sku}="${boardsku}";
	eval ${__board_revision}="${boardrevision}";
	eval "${__boot_device}"="${bootdevice}";
	eval "${__platform_id}"="${platformid}";
	eval "${__emcfuse_bin}"="${emcfusebin}";
	custinfofilename="custinfo_out.bin"
	eval ${__board_ramcode}="${boardRamcode}";
}

t23_9_get_board_version ()
{
	local args="";
	local __board_id=$1;
	local __board_FAB=$2;
	local __board_sku=$3;
	local __board_revision=$4;
	local __emcfuse_bin=$5;
	local __board_ramcode=$6;
	local __platform_id=$7;
	local __boot_device=$8;
	local command;
	local boardid;
	local boardFAB;
	local boardsku;
	local boardrevision;
	local boardRamcode;
	local platformid;
	local bootdevice;

	if [ -n "${usb_instance}" ]; then
		args+="--instance ${usb_instance} ";
	fi;
	if [ "${CHIPMAJOR}" != "" ]; then
		args+="--chip \"${CHIPID} ${CHIPMAJOR}\" ";
	else
		args+="--chip \"${CHIPID}\" ";
	fi;
	args+="--applet \"${LDK_DIR}/${SOSFILE}\" ";
	args+="${SKIPUID} ";
	SKIPUID="";
	command=""
	if [ "${emcfuse}" != "" ]; then
		chsuffix emcfusebin "${emcfusename}" "bin";
		if [ -f "${BL_DIR}"/"${emcfusebin}" ]; then
			rm "${BL_DIR}"/"${emcfusebin}"
		fi;
		cp2local emcfuse "${BL_DIR}/${emcfusename}";
		command+="readfuses ${emcfusebin} ${emcfusename}; ";
	fi;
	cp2local emc_fuse_dev_params "${BL_DIR}/${emc_fuse_dev_paramsname}";
	if [ "${fuselevel}" = "fuselevel_production" ]; then
		sed -i "s/preprod_dev_sign = <1>/preprod_dev_sign = <0>/" "${BL_DIR}/${emc_fuse_dev_paramsname}";
	fi
	args+="--cfg ${readinfofilename} "

	command+="dump eeprom cvm cvm.bin; dump try_custinfo custinfo_out.bin;"
	command+=" reboot recovery"
	args+="--dev_params ${emc_fuse_dev_paramsname} ";
	cp2local device_config "${BL_DIR}/${device_configname}"
	cp2local misc_config "${BL_DIR}/${misc_configname}"
	if [ -f "${uphy_config}" ]; then
		cp2local uphy_config "${BL_DIR}/${uphy_configname}";
		args+="--device_config ${device_configname} --misc_config ${misc_configname} --uphy_config ${uphy_configname} "
	else
		args+="--device_config ${device_configname} --misc_config ${misc_configname} "
	fi;
	args+="--bins \"mb2_applet ${mb2appletname}\" ";

	args+="--cmd \"${command}\" ";
	local cmd="./tegraflash.py ${args}";
	pushd "${BL_DIR}" > /dev/null 2>&1;
	if [ "${keyfile}" != "" ]; then
		cmd+="--key \"${keyfile}\" ";
	fi;
	if [ "${sbk_keyfile}" != "" ]; then
		cmd+="--encrypt_key \"${sbk_keyfile}\" ";
		if [ "${disable_random_iv}" = "True" ]; then
			echo "Disable random IV"
			cmd+="--disable_random_iv ";
		fi
	fi;

	if [ "${read_info}" == "True" ] && [ "${no_flash}" -eq 1 ]; then
		# Expand $cmd to display Fuse Info and EEPROM content and save it to readinfocmd.txt
		readinfocmdfile="${BL_DIR}/readinfocmd.txt";
		echo "Saving readinfo command in ${readinfocmdfile}";
		prepare_readinfocmd "${cmd}" "${readinfocmdfile}";
		popd > /dev/null 2>&1 || exit;
		return
	fi;

	# proceed to dry-run for offline image generation
	if [ -z "${hwchipid}" ]; then
		cmd+=" --no_flash"
	fi
	if [ -f "rcm_state" ]; then
		rm rcm_state;
	fi;
	echo "${cmd}";
	eval "${cmd}";
	# return for dry-run scenario
	if [ -z "${hwchipid}" ]; then
		popd > /dev/null 2>&1;
		return
	fi
	chkerr "Reading board information failed.";
	if [ "${SKIP_EEPROM_CHECK}" = "" ]; then
		boardid=$(./chkbdinfo -i cvm.bin);
		boardFAB=$(./chkbdinfo -f cvm.bin);
		boardsku=$(./chkbdinfo -k cvm.bin);
		boardrevision=$(./chkbdinfo -r cvm.bin);
		if [[ -e "cvb.bin" ]]; then
			platformid=$(./chkbdinfo -q cvb.bin);
			bootdevice=$(./chkbdinfo -Q cvb.bin);
		fi
		chkerr "Parsing board information failed.";
	fi;

	# Get ramcode from the board
	boardRamcode=$(./chkbdinfo -R chip_info.bin_bak);
	chkerr "Getting ramcode failed.";
	boardRamcode="$(echo "${boardRamcode}" | cut -d: -f 4)";
	boardRamcode="$((16#${boardRamcode} % 16))"

	if [ "${BOARDID}" != "" ]; then
		boardid="${BOARDID}";
	fi;

	if [ "${FAB}" != "" ]; then
		boardFAB="${FAB}";
	fi;

	if [ "${BOARDSKU}" != "" ]; then
		boardsku="${BOARDSKU}";
	fi;

	if [ "${BOARDREV}" != "" ]; then
		boardrevision="${BOARDREV}";
	fi;

	if [ "${RAMCODE_ID}" != "" ]; then
		boardRamcode="${RAMCODE_ID}";
	fi;

	popd > /dev/null 2>&1;
	eval ${__board_id}="${boardid}";
	eval ${__board_FAB}="${boardFAB}";
	eval ${__board_sku}="${boardsku}";
	eval ${__board_revision}="${boardrevision}";
	eval "${__boot_device}"="${bootdevice}";
	eval "${__platform_id}"="${platformid}";
	eval "${__emcfuse_bin}"="${emcfusebin}";
	custinfofilename="custinfo_out.bin"
	eval ${__board_ramcode}="${boardRamcode}";
}

get_board_version ()
{
	case "${CHIPID}" in
	"0x23")
		t234_get_board_version "$1" "$2" "$3" "$4" "$5" "$6"
		;;
	 "0x26" | "0x26 8")
		t264_get_board_version "$1" "$2" "$3" "$4" "$5" "$6" "$7" "$8"
		;;
	"0x23 9")
		t23_9_get_board_version "$1" "$2" "$3" "$4" "$5" "$6" "$7" "$8"
		;;
	*)
		echo "get_board_version(): CHIPID of ${CHIPID} is not supported"
		exit 1
		;;
	esac;
}

#
# Prepare readinfocmd and save it to readinfocmd.txt file
#
prepare_readinfocmd ()
{
	local cmd="$1";
	local cmd_file="$2";

	echo "rm cvm.bin;" > "${cmd_file}";
	{
		echo "${cmd};"
		echo "boardid=\$(./chkbdinfo -i cvm.bin);"
		echo "boardFAB=\$(./chkbdinfo -f cvm.bin);"
		echo "boardsku=\$(./chkbdinfo -k cvm.bin);"
		echo "boardrevision=\$(./chkbdinfo -r cvm.bin);"
		echo "boardRamcode=\$(./chkbdinfo -R chip_info.bin_bak);"
		echo "boardRamcode=\$(echo \${boardRamcode} | cut -d: -f 4);"
		echo "boardRamcode=\$((16#\${boardRamcode} % 16));"
		echo "chipSKU=\$(./chkbdinfo -C chip_info.bin_bak);"
		echo "echo '';"
		echo "echo '==== Board Spec. ====';"
		echo "echo BOARDID=\${boardid};"
		echo "echo FAB=\${boardFAB} ;"
		echo "echo BOARDSKU=\${boardsku};"
		echo "echo CHIP_SKU=\${chipSKU};"
		echo "echo RAMCODE_ID=\${boardRamcode};"
		echo "echo '';"
		echo "fusebin=${emcfusebin}"
		echo "if [ -n \"\${fusebin}\" ] && [ -f \"\${fusebin}\" ]; then"
		echo "    echo '==== Fuse Info ====';"
		echo "    cat \"\${fusebin}\";"
		echo "    echo '';"
		echo "fi"
		echo "echo '==== EEPROM content ====';"
		echo "xxd -g 1 cvm.bin;"
		echo "echo '';"
	} >> "${cmd_file}";
}

#
# Generate rcmdump_blob
#
function gen_rcmdump_blob ()
{
	local args="";
	local command="";

	if [ "${CHIPID}" = "0x26" ]; then
		t264_prepare_common_args args;
	elif [ "${CHIPID}" = "0x23" ]; then
		t234_prepare_common_args args;
	else
		echo "gen_rcmdump_blob is not supported for ${CHIPID}"
		return 1
	fi

	command="dump gen_blob"
	args+="--cmd \"${command}\" ";
	local cmd="./tegraflash.py ${args}";
	pushd "${BL_DIR}" > /dev/null 2>&1;
	if [ -f "rcm_state" ]; then
		rm rcm_state;
	fi;
	echo "${cmd}";
	eval "${cmd}";
	chkerr "gen_blob failed.";
	echo ""
	echo "rcmdump_blob is generated."
	echo ""
	cp tegrarcm_v2 rcmdump_blob/
	cp chkbdinfo rcmdump_blob/
	# create a tarball
	tar -cf rcmdump_blob.tar rcmdump_blob
	rm -rf rcmdump_blob
	tar -rf rcmdump_blob.tar read_ramcode.func
	if [ "${no_flash}" -eq 1 ] && [ "${read_info}" == "True" ]; then
		tar -rf rcmdump_blob.tar read_eeprom.func
		echo "read_eeprom scripts added to rcmdump_blob, program exiting ..."
		echo "";
		echo "To read eeprom, put the target in recovery mode:"
		echo "  $ cd bootloader";
		echo "  $ sudo ./read_eeprom.sh ${CHIPID}"
		echo "";
		exit 0
	fi
	popd > /dev/null 2>&1 || exit;
	return 0
}
#
# This function prepares a command script (a text string)
# to do read_ramcode command.
#
t264_set_read_ramcode_cmd()
{
	read_ramcode_cmd=$(cat << EOF
tar -xf rcmdump_blob.tar;
if [ \$? -ne 0 ]; then
    echo "Failed to extract rcmdump_blob.tar";
    exit 1;
fi;

source read_ramcode.func;

ramcode=${RAMCODE};
mem_rcm_file=${mem_rcm_file};
ramcode_mem_bct_file="";

# Read on-board ramcode from the target
read_ramcode 0x26;

# Set mem_bct binary based on the ramcode
t264_get_ramcode_mem_bct ramcode_mem_bct_file;
echo "ramcode_mem_bct_file=\${ramcode_mem_bct_file}"
echo "Copying \${ramcode_mem_bct_file} to membct_ramcode_file"
cp \${ramcode_mem_bct_file} membct_ramcode_file;


EOF
)
}

#
# EEPROM get board S/N .
#
boardinfo_trk ()
{
	local boardinforom;
	local boardpartnu;
	if [[ -e "${LDK_DIR}/nv_internal_trk.sh" &&
		-e "${BL_DIR}/chkbdinfo" &&
		-e "${BL_DIR}/cvm.bin" ]]; then
		pushd "${BL_DIR}" > /dev/null 2>&1;
		boardinforom=$(./chkbdinfo -a cvm.bin);
		boardpartnu=$(./chkbdinfo -p cvm.bin);
		if [[ "${boardinforom}" != "" ]] && [[ "${boardpartnu}" != "" ]]; then
			eval "PRODUCT_OUT=\"${LDK_DIR}\" \"${LDK_DIR}/nv_internal_trk.sh\" \"${boardinforom}\" \"${boardpartnu}\"";
		fi
		popd > /dev/null 2>&1;
	fi
}

#
# Read chip specifc details
#
get_chip_info_details ()
{
	local args="";
	local __chip_SKU=$1;
	local __chip_minor_revision_ID=$2;
	local __bootrom_revision_ID=$3;
	local chipSKU;
	local chipminorrevisionID;
	local bootromrevisionID;

	pushd "${BL_DIR}" > /dev/null 2>&1 || return;
	chipSKU=$(./chkbdinfo -C chip_info.bin_bak);
	chipminorrevisionID=$(./chkbdinfo -M chip_info.bin_bak);
	bootromrevisionID=$(./chkbdinfo -O chip_info.bin_bak);
	chkerr "Parsing chip_info.bin information failed.";

	popd > /dev/null 2>&1 || return;

	eval "${__chip_SKU}"="${chipSKU}";
	eval "${__chip_minor_revision_ID}"="${chipminorrevisionID}";
	eval "${__bootrom_revision_ID}"="${bootromrevisionID}";
}

#
# SoC Sanity Check:
#
chk_soc_sanity ()
{
	local mach_dir="generic";
	local socname="Unknown";
	local opmode="Unknown";
	local disk_enc="";

	#
	# Print Target Board Information:
	# NOTE: The list of board listed here may or may not be
	#	supported by the version of BSP(Board Support Package)
	#	that provides this copy of the script. This lists all
	#	of the publicly available Jetson developer platforms.
	#
	case ${hwchipid} in
	0x23) socname="Tegra 234" ;;
	"0x23 9") socname="Tegra 23 9" ;;
	0x26) socname="Tegra 264" ;;
	esac;

	case ${fuselevel} in
	fuselevel_nofuse) opmode="pre-production"; ;;
	fuselevel_production) opmode="production"; ;;
	esac;

	if [ ${disk_enc_enable} -eq 1 ]; then
		disk_enc="enabled";
	else
		disk_enc="disabled";
	fi;

	echo	"# Target Board Information:";
	echo -n "# Name: ${ext_target_board}, Board Family: ${target_board}, ";
	echo	"SoC: ${socname}, ";
	echo	"# OpMode: ${opmode}, Boot Authentication: ${bootauth}, ";
	echo	"# Disk encryption: ${disk_enc} ,";
	echo	"${INFODIVIDER}";

	if [ "${hwchipid}" != "" -a "${CHIPID}" != "" -a "${CHIPID}" != "${hwchipid}" ]; then
		echo -n "Error: The Actual SoC ID(${hwchipid}) ";
		echo -n "mismatches intended ${ext_target_board} ";
		echo "SoC ID(${CHIPID}).";
		exit 1;
	fi;

	if [ "${target_board}" != "${mach_dir}" ] && [ "${target_board}" != "${GENERIC}" ]; then
		echo -n "Error: The Actual board family (${mach_dir}) ";
		echo -n "mismatches intended ${ext_target_board} ";
		echo "board family(${target_board}).";
		exit 1;
	fi;

	if [ "${no_flash}" -eq 1 ]; then
		return
	fi

	if [ "${hwchipid}" = "" ]; then
		# Nothing to check against. Just let it go.
		echo "Error: probing the target board failed.";
		echo "       Make sure the target board is connected through ";
		echo "       USB port and is in recovery mode.";
		exit 1;
	fi;

	case ${bootauth} in
	PKC)
		if [ "${keyfile}" = "" ] || [ "${sbk_keyfile}" != "" ]; then
			echo -n "Error: Either RSA key file is not provided or SBK key ";
			echo "file is provided for PKC protected target board.";
			exit 1;
		fi;
		;;
	SBKPKC)
		if [ "${keyfile}" = "" ] || [ "${sbk_keyfile}" = "" ]; then
			echo -n "Error: Either RSA key file and/or SBK key file ";
			echo "is not provided for SBK and PKC protected target board.";
			exit 1;
		fi;
		;;
	SBK)
		echo "Error: L4T does not support SBK protected target board.";
		exit 1;
		;;
	NS)
		if [ "${keyfile}" != "" ] || [ "${sbk_keyfile}" != "" ]; then
			echo -n "Error: either RSA key file and/or SBK key file ";
			echo "are provided for none SBK and PKC protected target board.";
			exit 1;
		fi;
		;;
	*)
		if [ "${keyfile}" != "" ]; then
			echo -n "Error: The RSA key file is provided for ";
			echo "non-PKC protected target board.";
			exit 1;
		fi;
		;;
	esac;
}

function rootuuid_gen() {
	local root_id=$1
	local uuid=""
	local uuidgen_installed="$(which uuidgen || true)"

	if [ "${uuidgen_installed}" == "" ]; then
		echo "Error: uuidgen not installed! Please provide the UUID or install"
		echo "uuidgen. For example, to install uuidgen for Ubuntu distributions,"
		echo "execute the command 'sudo apt install uuid-runtime'. Otherwise a"
		echo "UUID can be provided by storing a UUID to the file"
		echo "${rootfsuuidfile} or ${rootfsuuidfile}_b if neabled ROOTFS_AB"
		usage allunknown 1;
	fi

	uuid="$(uuidgen)"
	setval "rootfsuuid${root_id}" uuid;

	echo "${uuid}" > "${rootfsuuidfile}${root_id}"
	echo "Generated UUID ${uuid} for mounting root APP${root_id} partition."
}

function rootuuid_chk_and_gen() {
	local root_id=$1
	local uuid=""
	local uuid_regex="([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})"

	if [ "${rcm_boot}" -eq 0 ] && [ "${reuse_uuid}" -eq 0 ] \
		&& [ -f "${rootfsuuidfile}${root_id}" ] \
		&& [ "${target_partname}" == "" ] && [ "${reuse_systemimg}" != "true" ] \
		&& [ "${no_systemimg}" -eq 0 ]; then
		rm "${rootfsuuidfile}${root_id}"
	fi

	# read UUID which stored in the file ${rootfsuuidfile} if the file exist
	if [ -f "${rootfsuuidfile}${root_id}" ]; then
		uuid="$(sed -nr "s/^${uuid_regex}$/\1/p" "${rootfsuuidfile}${root_id}")"

		if [ "${#uuid}" != "36" ]; then
			echo "File ${rootfsuuidfile}${root_id} contains invalid UUID!"
			usage allunknown 1;
		fi

		setval "rootfsuuid${root_id}" uuid;
		echo "Using UUID ${uuid} for mounting root APP${root_id} partition."
	fi

	# generate UUID if ${rootfsuuidfile} isn't present
	eval uuid='$'{"rootfsuuid${root_id}"}
	if [ "${uuid}" == "" ]; then
		rootuuid_gen "${root_id}"
	fi
}

function check_hsm_support {
	if [ ! -f "${keyfile}" ]; then
		echo "Error: keyfile ${keyfile} not found. Need -u option with --hsm option";
		exit 1;
	fi;
	if [ "${PVENC}" != "" ]; then
		echo "Error: HSM feature is not supported with --pv-enc options";
		exit 1;
	fi;
}

function rootuuid_restore {
	local ext="${1}"
	local _rootfsuuid="rootfsuuid${ext}"
	local _rootfsuuid_b

	rootuuid_chk_and_gen "${ext}"

	if [[ "${rootfs_ab}" == 1 ]]; then
		# get UUID for APP_b
		_rootfsuuid_b="rootfsuuid_b${ext}"
		rootuuid_chk_and_gen "_b${ext}"
	fi



	if [[ "${!_rootfsuuid}" == "" ]] || \
	   [[ "${rootfs_ab}" == 1 && "${!_rootfsuuid_b}" == "" ]]; then
		echo "No UUID found for root partition! If the root partition"
		echo "is not currently being mounted using a partition UUID,"
		echo "then flash the device by specifying the root device that"
		echo "was specified when previously flashing the entire system"
		echo "(eg. /dev/mmcblk0p1). Otherwise, to mount the root"
		echo "partition using a partition UUID please either:"
		echo "1. If you know the UUID for the root partition save it"
		echo "   to the file ${rootfsuuidfile},"
		echo "   or for root B partition save it"
		echo "   to the file  ${rootfsuuidfile}_b"
		echo "2. Re-flash entire system to generate a new UUID."
		usage allunknown 1;
	fi
}

function sysfile_exist {
	if [ ${disk_enc_enable} -eq 0 ]; then
		echo "Reusing existing ${localsysfile}... ";
		if [ ! -e "${localsysfile}" ]; then
			echo "file does not exist.";
			exit 1;
		fi;
		if [[ "${rootfs_ab}" == 1 ]]; then
			echo "Reusing existing ${localsysfile}.b... ";
			if [ ! -e "${localsysfile}_b" ]; then
				echo "file does not exist.";
				exit 1;
			fi;
		fi;
	else
		echo "Reusing existing ${localsysbootfile} & ${localsysencrootfile}... ";
		if [ ! -e "${localsysbootfile}" ] || [ ! -e "${localsysencrootfile}" ]; then
			echo "file does not exist.";
			exit 1;
		fi;
		if [[ "${rootfs_ab}" == 1 ]]; then
			echo "Reusing existing ${localsysbootfile_b} & ${localsysencrootfile_b}... ";
			if [ ! -e "${localsysbootfile_b}" ] || [ ! -e "${localsysencrootfile_b}" ]; then
				echo "file does not exist.";
				exit 1;
			fi;
		fi;
	fi;
	echo "done.";
}

function make_boot_image() {
	if [ -n "${BOOTIMG}" ]; then
		if ! [ -f "${BOOTIMG}" ]; then
			echo "Error: ${BOOTIMG} not found"
			exit 1;
		fi
		cp "${BOOTIMG}" "${localbootfile}"
		if [[ "${rootfs_ab}" == 1 ]]; then
			cp "${BOOTIMG}" "${localbootfile}_b"
		fi
		return
	fi
	echo -n "Making Boot image... ";
	if [ "${NULL_INITRD_IN_BOOTIMG}" != "" ]; then
		rm -f initrd;
		touch initrd;
	fi;
	ramdisk=initrd;
	if [[ "${rootfs_ab}" == 1 && "${disk_enc_enable}" == 1 ]]; then
		ramdisk_b=initrd_b;
	fi
	local _kernel_image="${kernel_image}"
	if [ "${rcm_boot}" -eq 1 ] && [[ "${_kernel_image}" == *.gz ]]; then
		local _tmp
		_tmp="$(mktemp)"
		gunzip -c "${_kernel_image}" > "${_tmp}"
		chkerr;
		_kernel_image=${_tmp}
	fi
	if [[ ${uefi_sign} == "True" ]]; then
		# Sign kernel_image before making boot.img
		cp "${_kernel_image}" "${_kernel_image}".save
		uefi_signimage "${_kernel_image}" "${uefi_db_key}" "${uefi_db_cert}" "nosplit" "False";
	fi
	./mkbootimg \
		--kernel "${_kernel_image}" \
		--ramdisk "${ramdisk}" \
		--board "${target_rootdev}" \
		--output "${localbootfile}" \
		--cmdline "${cmdline}" > /dev/null 2>&1;
	chkerr;
	# delete ${localbootfile}_b in case that it is left here from previous run
	rm -f "${localbootfile}_b"
	if [[ "${rootfs_ab}" == 1 ]]; then
		if [[ "${disk_enc_enable}" == 1 ]]; then
			./mkbootimg \
				--kernel "${_kernel_image}" \
				--ramdisk "${ramdisk_b}" \
				--board "${target_rootdev}" \
				--output "${localbootfile}_b" \
				--cmdline "${cmdline_b}" > /dev/null 2>&1;
			chkerr;
		else
			./mkbootimg \
				--kernel "${_kernel_image}" \
				--ramdisk "${ramdisk}" \
				--board "${target_rootdev}" \
				--output "${localbootfile}_b" \
				--cmdline "${cmdline_b}" > /dev/null 2>&1;
			chkerr;
		fi
	fi
	if [[ ${uefi_sign} == "True" ]]; then
		# Restore the unsigned kernel_image
		mv "${_kernel_image}".save "${_kernel_image}"
	fi
}

function check_xmllint() {
	# Usage:
	#	check_xmllint {__ret_value}
	local __ret_value="${1}"

	if [ -f "/usr/bin/xmllint" ]; then
		eval "${__ret_value}=\"/usr/bin/xmllint\""
	else
		if [ -z "${__XMLLINT_BIN}" ]; then
			echo "ERROR xmllint not found! To install - please run: " \
				"\"sudo apt-get install libxml2-utils\""
			exit 1
		fi;
	fi;
}

function set_value_in_bct_cfg() {
    local value="$1"
    local tag_name="$2"
    local xml_file="$3"

    # Check if xmlstarlet is installed
    if ! command -v xmlstarlet &> /dev/null; then
        echo "Error: xmlstarlet is not installed. Please install it first."
        return 1
    fi

    # Update the tag value
    xmlstarlet ed -L -u "/bct_cfg/$tag_name" -v "$value" "$xml_file"

    echo "Updated $tag_name to $value in $xml_file"
}

function replace_value_in_bct_cfg() {
	if [[ "${coldbootbctcfg}" != "" ]]; then
		cp2local coldbootbctcfg "${BL_DIR}/${coldbootbctcfgname}";
		cp "${BL_DIR}/${coldbootbctcfgname}" "${BL_DIR}/${localbctcfg}"
		BCTARGS+="--coldboot_bct_cfg ${localbctcfg} ";
	fi;
	if [[ "${rcmbootbctcfg}" != "" ]]; then
		cp2local rcmbootbctcfg "${BL_DIR}/${rcmbootbctcfgname}";
		cp "${BL_DIR}/${rcmbootbctcfgname}" "${BL_DIR}/${localrcmbootbctcfg}"
		BCTARGS+="--rcmboot_bct_cfg ${localrcmbootbctcfg} ";
	fi;

	if [ "${uphy_config}" != "" ]; then
		cp2local uphy_config "${BL_DIR}/${uphy_configname}";
		set_value_in_bct_cfg "${uphy_configname}" "brbct_cfg/uphy" "${localbctcfg}"
		set_value_in_bct_cfg "${uphy_configname}" "brbct_cfg/uphy" "${localrcmbootbctcfg}"

	fi;
	if [ "${minratchet_config}" != "" ]; then
		cp2local minratchet_config "${BL_DIR}/${minratchet_configname}";
		set_value_in_bct_cfg "${minratchet_configname}" "brbct_cfg/minratchet" "${localbctcfg}"
		set_value_in_bct_cfg "${minratchet_configname}" "brbct_cfg/minratchet" "${localrcmbootbctcfg}"
		BCTARGS+="--minratchet_config ${minratchet_configname} ";
	fi;
	if [ "${device_config}" != "" ]; then
		cp2local device_config "${BL_DIR}/${device_configname}";
		set_value_in_bct_cfg "${device_configname}" "brbct_cfg/device" "${localbctcfg}"
		set_value_in_bct_cfg "${device_configname}" "brbct_cfg/device" "${localrcmbootbctcfg}"

	fi;
	if [ "${misc_cold_boot_config}" != "" ]; then
		cp2local misc_cold_boot_config "${BL_DIR}/${misc_cold_boot_configname}";
		BCTARGS+="--misc_cold_boot_config ${misc_cold_boot_configname} ";
	fi;
	if [ "${misc_config}" != "" ]; then
		cp2local misc_config "${BL_DIR}/${misc_configname}";
		set_value_in_bct_cfg "${misc_configname}" "brbct_cfg/misc" "${localbctcfg}"
		set_value_in_bct_cfg "${misc_configname}" "brbct_cfg/misc" "${localrcmbootbctcfg}"
	fi;
	if [ "${pinmux_config}" != "" ]; then
		cp2local pinmux_config "${BL_DIR}/${pinmux_configname}";
		set_value_in_bct_cfg "${pinmux_configname}" "brbct_cfg/pinmux" "${localbctcfg}"
		set_value_in_bct_cfg "${pinmux_configname}" "brbct_cfg/pinmux" "${localrcmbootbctcfg}"
	fi;
	if [ "${gpioint_config}" != "" ]; then
		cp2local gpioint_config "${BL_DIR}/${gpioint_configname}";
		set_value_in_bct_cfg "${gpioint_configname}" "brbct_cfg/gpioint" "${localbctcfg}"
		set_value_in_bct_cfg "${gpioint_configname}" "brbct_cfg/gpioint" "${localrcmbootbctcfg}"
	fi;
	if [ "${pmic_config}" != "" ]; then
		cp2local pmic_config "${BL_DIR}/${pmic_configname}";
		set_value_in_bct_cfg "${pmic_configname}" "brbct_cfg/pmic" "${localbctcfg}"
		set_value_in_bct_cfg "${pmic_configname}" "brbct_cfg/pmic" "${localrcmbootbctcfg}"
	fi;
	if [ "${pmc_config}" != "" ]; then
		cp2local pmc_config "${BL_DIR}/${pmc_configname}";
		set_value_in_bct_cfg "${pmc_configname}" "brbct_cfg/pmc" "${localbctcfg}"
		set_value_in_bct_cfg "${pmc_configname}" "brbct_cfg/pmc" "${localrcmbootbctcfg}"
	fi;
	if [ "${deviceprod_config}" != "" ]; then
		cp2local deviceprod_config "${BL_DIR}/${deviceprod_configname}";
		set_value_in_bct_cfg "${deviceprod_configname}" "brbct_cfg/deviceprod" "${localbctcfg}"
		set_value_in_bct_cfg "${deviceprod_configname}" "brbct_cfg/deviceprod" "${localrcmbootbctcfg}"
	fi;
	if [ "${prod_config}" != "" ]; then
		cp2local prod_config "${BL_DIR}/${prod_configname}";
		set_value_in_bct_cfg "${prod_configname}" "brbct_cfg/prod" "${localbctcfg}"
		set_value_in_bct_cfg "${prod_configname}" "brbct_cfg/prod" "${localrcmbootbctcfg}"
	fi;
	if [ "${scr_config}" != "" ]; then
		cp2local scr_config "${BL_DIR}/${scr_configname}";
		set_value_in_bct_cfg "${scr_configname}" "brbct_cfg/scr" "${localbctcfg}"
		set_value_in_bct_cfg "${scr_configname}" "brbct_cfg/scr" "${localrcmbootbctcfg}"
	fi;
	if [ "${wb0sdram}" != "" ]; then
		cp2local wb0sdram  "${BL_DIR}/${wb0sdramname}";
		set_value_in_bct_cfg "${wb0sdramname}" "brbct_cfg/wb0sdram" "${localbctcfg}"
		set_value_in_bct_cfg "${wb0sdramname}" "brbct_cfg/wb0sdram" "${localrcmbootbctcfg}"
	fi;
	if [ "${scr_config_dts}" != "" ]; then
		cp2local scr_config_dts "${BL_DIR}/${scr_config_dtsname}";
	fi;
	if [ "${scr_cold_boot_config}" != "" ]; then
		cp2local scr_cold_boot_config "${BL_DIR}/${scr_cold_boot_configname}";
		BCTARGS+="--scr_cold_boot_config ${scr_cold_boot_configname} ";
	fi;
	if [ "${bootrom_config}" != "" ]; then
		cp2local bootrom_config "${BL_DIR}/${bootrom_configname}";
		set_value_in_bct_cfg "${bootrom_configname}" "brbct_cfg/brcommand" "${localbctcfg}"
		set_value_in_bct_cfg "${bootrom_configname}" "brbct_cfg/brcommand" "${localrcmbootbctcfg}"
	fi;
	if [ "${uphylane_config}" != "" ]; then
		cp2local uphylane_config "${BL_DIR}/${uphylane_configname}";
		BCTARGS+="--uphy ${uphylane_configname} ";
	fi
	if [ "${bpmp_mem_config}" != "" ]; then
		cp2local bpmp_mem_config "${BL_DIR}/${bpmp_mem_configname}";
		set_value_in_bct_cfg "${bpmp_mem_configname}" "brbct_cfg/bpmp_mem_cfg" "${localbctcfg}"
		set_value_in_bct_cfg "${bpmp_mem_configname}" "brbct_cfg/bpmp_mem_cfg" "${localrcmbootbctcfg}"
	fi
	# Process dev params for multiple boot chains
	process_dev_params
	if [ "${mb2bct_cfg}" != "" ]; then
		cp2local mb2bct_cfg "${BL_DIR}/${mb2bct_cfgname}";
		if [ -n "${pv_pub_key_hash}" ]; then
			mb2bct_cfg_with_pkh="$(basename "${mb2bct_cfgname}" .dts)_with_pkh.dts"
			cat > "${mb2bct_cfg_with_pkh}" <<EOF
/dts-v1/;
#include "${mb2bct_cfgname}"
/ { mb2-misc { cpubl_auth_key_delegation_enable = <1>; cpubl_auth_key_pcp_hash = <${pv_pub_key_hash}>;}; };
EOF
			mb2bct_cfgname="${mb2bct_cfg_with_pkh}"
		fi
		set_value_in_bct_cfg "${mb2bct_cfgname}" "brbct_cfg/mb2bctcfg" "${localbctcfg}"
		set_value_in_bct_cfg "${mb2bct_cfgname}" "brbct_cfg/mb2bctcfg" "${localrcmbootbctcfg}"
	fi;
	set_value_in_bct_cfg "${bctfilename}" "brbct_cfg/sdram" "${localbctcfg}"
	set_value_in_bct_cfg "${bctfilename}" "hpct_cfg/sdram" "${localbctcfg}"
	set_value_in_bct_cfg "${bctfilename}" "sbct_cfg/sdram" "${localbctcfg}"
	set_value_in_bct_cfg "${bctfilename}" "brbct_cfg/sdram" "${localrcmbootbctcfg}"
	set_value_in_bct_cfg "${bctfilename}" "hpct_cfg/sdram" "${localrcmbootbctcfg}"
	set_value_in_bct_cfg "${bctfilename}" "sbct_cfg/sdram" "${localrcmbootbctcfg}"

	set_value_in_bct_cfg "${hpsebl1filename}" "hpct_cfg/bl" "${localbctcfg}"
	set_value_in_bct_cfg "${hpsefwfilename}" "hpct_cfg/fw" "${localbctcfg}"
	set_value_in_bct_cfg "${hpserawfilename}" "hpct_cfg/raw_bin" "${localbctcfg}"
	set_value_in_bct_cfg "${sbbl1filename}" "sbct_cfg/bl" "${localbctcfg}"
	set_value_in_bct_cfg "${sbfwfilename}"   "sbct_cfg/fw" "${localbctcfg}"
	set_value_in_bct_cfg "${sbrawfilename}" "sbct_cfg/raw_bin" "${localbctcfg}"
	set_value_in_bct_cfg "${hpsebl1filename}" "hpct_cfg/bl" "${localrcmbootbctcfg}"
	set_value_in_bct_cfg "${hpsefwfilename}" "hpct_cfg/fw" "${localrcmbootbctcfg}"
	set_value_in_bct_cfg "${hpserawfilename}" "hpct_cfg/raw_bin" "${localrcmbootbctcfg}"
	set_value_in_bct_cfg "${sbbl1filename}" "sbct_cfg/bl" "${localrcmbootbctcfg}"
	set_value_in_bct_cfg "${sbfwfilename}"   "sbct_cfg/fw" "${localrcmbootbctcfg}"
	set_value_in_bct_cfg "${sbrawfilename}" "sbct_cfg/raw_bin" "${localrcmbootbctcfg}"

}

function get_value_from_PT_table() {
	# Usage:
	#	get_value_from_PT_table {__pt_name} \
	#	{__pt_node} \
	#	{__pt_file} \
	#	{__ret_value}
	local __XMLLINT_BIN="";
	local __pt_name="${1}";
	local __pt_node="${2}";
	local __pt_file="${3}";
	local __ret_value="${4}";
	local __node_val="";

	# Check xmllint
	check_xmllint __XMLLINT_BIN

	# Get node value
	__node_val="$(${__XMLLINT_BIN} --xpath "/partition_layout/device/partition[@name='${__pt_name}']/${__pt_node}/text()" ${__pt_file})";
	__node_val=$(echo ${__node_val} | sed -e 's/^[[:space:]]*//');

	eval "${__ret_value}=\"${__node_val}\"";
}

function get_dev_attr_from_PT_table() {
	# Usage:
	#	get_dev_attr_from_PT_table {__dev_attr} \
	#	{__dev_type} \
	#	{__pt_file} \
	#	{__ret_value}
	local __XMLLINT_BIN="";
	local __dev_attr="${1}";
	local __dev_type="${2}"
	local __pt_file="${3}";
	local __ret_value="${4}";
	local __attr_val="";

	# Check xmllint
	check_xmllint __XMLLINT_BIN

	# Get node value
	if [ "${__dev_type}" = "boot_dev" ]; then
		__attr_val="$(${__XMLLINT_BIN} --xpath "string((/partition_layout/device/@${__dev_attr})[1])" ${__pt_file})";
	elif [ "${__dev_type}" = "user_dev" ]; then
		__attr_val="$(${__XMLLINT_BIN} --xpath "string((/partition_layout/device/@${__dev_attr})[last()])" ${__pt_file})";
	else
		echo "Error: not support device type ${__dev_type}."
		return 1
	fi

	eval "${__ret_value}=\"${__attr_val}\"";
}

function partition_exists_in_PT_table() {
	# Usage:
	#	partition_exists_in_PT_table {__pt_name} {__pt_file}
	local __XMLLINT_BIN="";
	local __pt_name="${1}";
	local __pt_file="${2}";

	# Check xmllint
	check_xmllint __XMLLINT_BIN

	# Check whether node exists
	${__XMLLINT_BIN} --xpath "/partition_layout/device/partition[@name='${__pt_name}']" ${__pt_file} >/dev/null 2>&1
	return $?
}

# Get the value of a attribute in the element of a xml file and store it in a variable
# Input:
#   $1 - element_path: The XPath expression used to locate the element containing the desired attribute
#   $2 - xml_file
#   $3 - target_attribute: the attribute to retrieve
#   $4 - ret_var: the variable that will store the value of the attribute if found
#   $5 - filter_attribute (optional): the attribute to filter by
#   $6 - filter_value (optional): the value of the filter attribute to match
function get_value_in_xml() {
	local __XMLLINT_BIN=""
	local element_path="${1}"
	local xml_file="${2}"
	local target_attribute="${3}"
	local ret_var="${4}"
	local filter_attribute="${5}"
	local filter_value="${6}"
	local __xpath_expr
	local __node_val

	if [ ! -f "${xml_file}" ]; then
		echo "Error: File ${xml_file} does not exist"
		exit 1
	fi

	# Check xmllint
	check_xmllint __XMLLINT_BIN

	# If no filter attribute or filter value is provided, select the target attribute in the element path
	if [ -z "${filter_attribute}" ] || [ -z "${filter_value}" ]; then
		__xpath_expr="${element_path}/@${target_attribute}"
	else
		__xpath_expr="${element_path}[@${filter_attribute}='${filter_value}']/@${target_attribute}"
	fi
	__node_val=$(xmllint --xpath "string(${__xpath_expr})" "${xml_file}")

	eval "${ret_var}=\"${__node_val}\"";
}


function get_partition_attribute_in_PT_table() {
	# Usage:
	#	get_partition_attribute_in_PT_table {__pt_name} {__pt_file} {__pt_attr} {__ret_value}
	local __pt_name="${1}";
	local __pt_file="${2}";
	local __pt_attr="${3}"
	local __ret_value="${4}";
	local __node_val="";
	local __pt_path="/partition_layout/device/partition"
	local __pt_tagname="name"

	# Get value of attribute
	get_value_in_xml "${__pt_path}" "${__pt_file}" "${__pt_attr}" "${__ret_value}" "${__pt_tagname}" "${__pt_name}"
}

function update_value_in_PT_table() {
	# Usage:
	#       update_value_in_PT_table {__pt_name} {__pt_node} {__pt_file} {__value}
	local __pt_name="${1}";
	local __pt_node="${2}";
	local __pt_file="${3}";
	local __value="${4}";

	# Check if xmlstarlet is installed
	if ! command -v xmlstarlet &> /dev/null; then
		echo "Error: xmlstarlet is not installed. To install - please run: " \
				"\"sudo apt-get install xmlstarlet\"";
		exit 1;
	fi;

	# Update the node value
	if ! xmlstarlet ed -L -u "/partition_layout/device/partition[@name='${__pt_name}']/${__pt_node}" \
		-v "${__value}" "${__pt_file}"; then
		echo "Error: failed to update the ${__pt_node} for ${__pt_name} partition."
		exit 1;
	fi;

	echo "Updated ${__pt_node} for ${__pt_name} partition to ${__value} in ${__pt_file}";
}

function get_user_sector_size() {

	get_dev_attr_from_PT_table "sector_size" "user_dev" "${cfgfile}" user_sector_size

	if [ -z "${user_sector_size}" ]; then
		echo "Using default storage sector size (${DEVSECTSIZE})"
		user_sector_size=${DEVSECTSIZE}
	fi

	if [ "${user_sector_size}" != "${SECTOR_SIZE_512}" ] && [ "${user_sector_size}" != "${SECTOR_SIZE_4K}" ]; then
		echo "Error: not supported sector size ${user_sector_size} for user_dev."
		exit 1
	fi

	echo "Using user_dev sector size ${user_sector_size}"
}

# Sets the number of sectors for a user device based on its sector size.
function set_user_device_num_sectors() {
	local tmp_int
	local tmp_ext

	# Set the numer of sectors according to the default internal
	# storage size and external storage size defined in the
	# configuration file.
	tmp_int=$((INT_STORAGE_SIZE / user_sector_size))
	tmp_ext=$((EXT_STORAGE_SIZE / user_sector_size))

	ext_num_sectors="${EXT_NUM_SECTORS:-${tmp_ext}}"
	int_num_sectors="${INT_NUM_SECTORS:-${tmp_int}}"
}

# Extract public key from certificate and check if algo of
# PV authentication key is the same as PKC key
# Usage:
# extract_pub_key pkc_keyfile pv_pub_keycert pv_pubkeyfile
#	pkc_keyfile: pkc key file to sign all images except cpubl
#	pv_pub_keycert: pv public key certification
#	pv_pubkeyfile: write pv public key into pv_pubkeyfile
function extract_pub_key {
	local pkc_keyfile="${1}"
	local pv_key_cert="${2}"
	local result="${3}"
	local pv_key_algo_str=""

	pv_key_algo_str=$(openssl x509 -in "${pv_key_cert}" -text -noout | grep "Public Key Algorithm" | awk -F': ' '{print $2}')
	case "${pv_key_algo_str}" in
		"rsaEncryption")
			if openssl rsa -in "${pkc_keyfile}" -check -noout 2>/dev/null; then
				pv_key_algo="rsa"
				openssl x509 -in "${pv_key_cert}" -noout -modulus -out "${result}"
			else
				echo "Error: PV key algorithm is RSA which is not aligned with PKC key algorithm!"
				exit 1;
			fi
			;;

		"id-ecPublicKey")
			if openssl ec -in "${pkc_keyfile}" -check -noout 2>/dev/null; then
				pv_key_algo="ecc"
				openssl x509 -in "${pv_key_cert}" -pubkey -noout > tmp_pubkey
				openssl ec -in tmp_pubkey -pubin -text -noout -conv_form uncompressed > "${result}"
				rm tmp_pubkey
			else
				echo "Error: PV key algorithm is ECDSA which is not aligned with PKC key algorithm!"
				exit 1;
			fi
			;;

		*)
			echo "Error: Unknown/unsupported key algorithm: ${pv_key_algo_str}"
			exit 1
			;;
	esac
}

function create_fsimg {
	local do_sign="${2}";

	if [ "${disk_enc_enable}" -eq 0 ]; then
		local source_folder="${1}"
		APP_TAG+="-e s/APPFILE/${localsysfile}/ ";
		# this is set at a setval function
		if [ "${target_partname}" = "" ] || [ "${target_partname}" = "APP" ] || [ -n "${with_systemimg}" ]; then
			build_fsimg "${localsysfile}" "${fillpat}" \
				    "${rootfssize}" "${rootfs_type}" \
				    "${source_folder}" "${cmdline}" "${do_sign}";
		fi;

		if [[ "${rootfs_ab}" == 1 ]]; then
			# build fsimage for APP_b
			local sysfile=""

			# check if APP_b exist in layout file
			get_value_from_PT_table "APP_b" "filename" "${cfgfile}" sysfile
			if [ "${sysfile}" != "" ]; then
				if [ "${target_partname}" = "" ] || \
				   [ "${target_partname}" = "APP_b" ] || [ -n "${with_systemimg}" ]; then
					build_fsimg "${localsysfile}_b" "${fillpat}" \
						    "${rootfssize}" "${rootfs_type}" \
						    "${source_folder}" "${cmdline_b}" "${do_sign}";
				fi;
			fi;
		fi;
	else
		if [ "${target_partname}" = "" ] || [ "${target_partname}" = "APP" ] || [ -n "${with_systemimg}" ]; then
			build_boot_fsimg "${localsysbootfile}" "${fillpat}" \
					 "${bootfssize}" "${rootfs_type}" \
					 "${rootfs_dir}/boot" "${cmdline}" "${do_sign}";
		fi;
		if [ "${target_partname}" = "" ] || [ "${target_partname}" = "APP_ENC" ] || [ -n "${with_systemimg}" ]; then
			build_enc_root_fsimg "${localsysencrootfile}" "${fillpat}" \
					     "${encrootfssize}" "${rootfs_type}" \
					     "${rootfs_dir}" "${rootfsuuid_enc}" \
					     "${bootpartuuid}" "${ECID}" "${rootfsuuid_b_enc}" \
					     "${rootfsuuid}" "${partition_app_enc_enable}" \
					     "${gen_luks_password_kdf}";
		fi;

		if [[ "${rootfs_ab}" == 1 ]]; then
			if [ "${target_partname}" = "" ] || [ "${target_partname}" = "APP_b" ] || [ -n "${with_systemimg}" ]; then
				echo -e -n "\tpopulating initrd_b to rootfs... ";
				cp -f initrd_b "${rootfs_dir}/boot/initrd"; chkerr;
				build_boot_fsimg "${localsysbootfile_b}" "${fillpat}" \
						 "${bootfssize}" "${rootfs_type}" \
						 "${rootfs_dir}/boot" "${cmdline_b}" "${do_sign}";
			fi;
			if [ "${target_partname}" = "" ] || [ "${target_partname}" = "APP_ENC_b" ] || [ -n "${with_systemimg}" ]; then
				build_enc_root_fsimg "${localsysencrootfile_b}" "${fillpat}" \
						     "${encrootfssize}" "${rootfs_type}" \
						     "${rootfs_dir}" "${rootfsuuid_b_enc}" \
						     "${bootpartuuid_b}" "${ECID}" "${rootfsuuid_enc}" \
						     "${rootfsuuid_b}" "${partition_app_enc_b_enable}" \
						     "${gen_luks_password_kdf}";
			fi;
		fi;
	fi;
}

# Clean up any dangling mounts, devices and directories.
# Assuming below names/variables are used for temporary mount points.
#     ${esp_loop_dev} - Variable pointing to a loop device
#     ${esp_mnt_dir} -  Variable pointing to temporary directory used for mounts.
cleanup_espmount ()
{
	echo "Cleaning ESP mounts..";
	if findmnt "${esp_loop_dev}" > /dev/null 2>&1; then
		if ! umount "${esp_mnt_dir}" > /dev/null 2>&1; then
			echo "WARNING: umount ${esp_mnt_dir} failed."
		fi;
		if ! losetup -d "${esp_loop_dev}" > /dev/null 2>&1; then
			echo "WARNING: dettaching ${esp_loop_dev} failed."
		fi;
	fi;
	if [ -d "${esp_mnt_dir}" ]; then
		if ! rmdir "${esp_mnt_dir}" > /dev/null 2>&1; then
			echo "WARNING: Could not remove ${esp_mnt_dir} dir."
		fi;
	fi;
}

# Create EFI system partition image with UEFI L4T launcher application.
function create_espimage() {

	# esp partition does not exist
	if ! partition_exists_in_PT_table "esp" "${cfgfile}"; then
		echo "Skip generating EFI system partition image because esp partition does not exist.";
		return;
	fi

	local efi_file="$1";
	local esp_img_name="$2";
	local esp_sector_size="$3";
	local esp_fs_size="";
	get_value_from_PT_table "esp" "size" "${cfgfile}" esp_fs_size
	if [ "${esp_fs_size}" = "" ]; then
		echo "EFI system partition size is not set.";
		exit 1;
	fi
	echo "Using esp partition size ${esp_fs_size}";

	generate_empty_image "${esp_img_name}" "${esp_fs_size}" "${esp_sector_size}"
	trap cleanup_espmount EXIT
	# Copy the image for esp_alt partition
	cp -v "${esp_img_name}" "${esp_img_name}.alt"

	esp_loop_dev="$(losetup --show -f "${esp_img_name}")";
	chkerr "mapping ${esp_img_name} to loop device failed.";

	local esp_size_260M=$(( 260 * 1024 * 1024 ));

	if [ "${esp_sector_size}" -eq "${SECTOR_SIZE_4K}" ] && [ "${esp_fs_size}" -lt "${esp_size_260M}" ]; then
		echo "Using \"mkfs.vfat -I -S ${esp_sector_size} ${esp_loop_dev}\" to format esp.";
		mkfs.vfat -I -S "${esp_sector_size}" "${esp_loop_dev}" > /dev/null 2>&1;
	else
		echo "Using \"mkfs.vfat -I -F 32 -s 1 -S ${esp_sector_size} ${esp_loop_dev}\" to format esp.";
		mkfs.vfat -I -F 32 -s 1 -S "${esp_sector_size}" "${esp_loop_dev}" > /dev/null 2>&1;
	fi
	chkerr "Formatting FAT filesystem on ${esp_img_name} failed.";
	esp_mnt_dir="espmnt";
	mkdir -p "${esp_mnt_dir}"; chkerr "make ${esp_img_name} mount point failed.";
	mount "${esp_loop_dev}" "${esp_mnt_dir}"; chkerr "mount ${esp_img_name} failed.";
	mkdir -p "${esp_mnt_dir}/EFI/BOOT/"; chkerr "make ${esp_img_name}/EFI/BOOT failed.";
	cp -f "${efi_file}" "${esp_mnt_dir}/EFI/BOOT/BOOTAA64.efi";
	chkerr "Copying ${efi_file} to ${esp_mnt_dir}/EFI/BOOT/BOOTAA64.efi failed.";
	echo -e -n "\tSync'ing ${esp_img_name} ... ";
	sync; sync; sleep 5;	# Give FileBrowser time to terminate gracefully.
	echo "done.";
	umount "${esp_mnt_dir}" > /dev/null 2>&1;
	losetup -d "${esp_loop_dev}" > /dev/null 2>&1;
	rmdir "${esp_mnt_dir}" > /dev/null 2>&1;
	trap - EXIT
}

# Use compression to allow bpmp-fw-dtb to support more than 4 ram codes
function compress_bpmp_fw_dtb() {
	local bpmp_fw_dtb_file="${1}";
	local bpmp_fw_dtb_filename="${2}";
	local bpmp_fw_dtb_filename_base;
	local bpmp_fw_dtb_filename_ext;
	local lz4c_bin="lz4c";

	# Check for lz4c installation
	local lz4c_check=$(which "${lz4c_bin}");
	if [ -z "${lz4c_check}" ]; then
		echo "error: lz4c not found";
		echo "To install please run:";
		SYSTEM_VER="$(grep "DISTRIB_RELEASE" </etc/lsb-release | cut -d= -f 2 | sed 's/\.//')"
		if [ "${SYSTEM_VER}" -lt 2004 ]; then
			echo "  sudo apt-get install -y liblz4-tool";
		else
			echo "  sudo apt-get install -y lz4";
		fi
		exit 1;
	else
		echo "lz4c installed at ${lz4c_check}";
	fi

	# Compress bpmp-fw-dtb
	bpmp_fw_dtb_filename_base="${bpmp_fw_dtb_filename%%.*}";
	bpmp_fw_dtb_filename_ext="${bpmp_fw_dtb_filename#*.}";
	echo -e -n "Compressing ${bpmp_fw_dtb_file} ...\n";
	"${lz4c_bin}" -f "${bpmp_fw_dtb_file}" \
		"${BL_DIR}/${bpmp_fw_dtb_filename_base}_lz4.${bpmp_fw_dtb_filename_ext}" \
		1>/dev/null 2>&1;
	chkerr "Failed to compress ${bpmp_fw_dtb_file} with ${lz4c_bin}.";
	mkfilesoft bpfdtbfile "${BPFDTBFILE}" \
		"${BL_DIR}/${bpmp_fw_dtb_filename_base}_lz4.${bpmp_fw_dtb_filename_ext}";
}

function update_bpmp_fw_dtb() {
	local bpmp_fw_dtb_file="${1}";
	local bpmp_fw_dtb_filename="${2}";
	local clk_dtso_file="${3}";
	local clk_dtso_filename="${4}";
	local clk_dtbo_filename="${clk_dtso_filename%.*}.dtbo";

	# Assemble BPFDTB_FILE and CLKDTSO_FILE (if specified), otherwise skip
	if [ -n "${clk_dtso_file}" ]; then
		cp2local clk_dtso_file "${BL_DIR}/${clk_dtso_filename}";
		clk_dtbo_cmd="${DTC} -I dts -O dtb -o ${BL_DIR}/${clk_dtbo_filename} ${BL_DIR}/${clk_dtso_filename}";
		clk_dtbo_cmd+=" && ${FDTOVERLAY} -i ${bpmp_fw_dtb_file} -o ${bpmp_fw_dtb_file} ${BL_DIR}/${clk_dtbo_filename}";
		echo "${clk_dtbo_cmd}";
		eval "${clk_dtbo_cmd}";
		chkerr "Failed to apply clock overlay ${clk_dtbo_filename} to ${bpmp_fw_dtb_filename}.";
	fi;

}

function parse_partition_enc_attr(){

	if partition_exists_in_PT_table "APP_ENC" "${cfgfile}"; then
		get_attr_from_PT_table "APP_ENC" "encrypted" "${cfgfile}" partition_app_enc_enable;
		get_attr_from_PT_table "APP_ENC" "reencrypt" "${cfgfile}" partition_app_enc_reencrypt;
	else
		echo "APP_ENC does not exist on partition table."
		exit 1;
	fi;

	if [ "${rootfs_ab}" == 1 ]; then
		if partition_exists_in_PT_table "APP_ENC_b" "${cfgfile}"; then
			get_attr_from_PT_table "APP_ENC_b" "encrypted" "${cfgfile}" partition_app_enc_b_enable;
			get_attr_from_PT_table "APP_ENC_b" "reencrypt" "${cfgfile}" partition_app_enc_b_reencrypt;
		else
			echo "APP_ENC_b does not exist on partition table."
			exit 1;
		fi;

		if [[ "${partition_app_enc_enable}" != "${partition_app_enc_b_enable}" ]]; then
			echo "Attribute encrypted from APP_ENC and APP_ENC_b are not the same."
			exit 1;
		fi

		if [[ "${partition_app_enc_reencrypt}" != "${partition_app_enc_b_reencrypt}" ]]; then
			echo "Attribute reencrypt from APP_ENC and APP_ENC_b are not the same."
			exit 1;
		fi

		if [[ "${partition_app_enc_enable}" != "true" ]] && [[ "${partition_app_enc_reencrypt}" == "true" ]]; then
			echo "Disk re-encryption can be enabled only when disk encryption is enabled."
			exit 1;
		fi
	fi;
}

function uefi_encryptimage() {
	# call l4t_sign_image.sh to generate encrypted and signed image
	# The image is generated in the same folder of file path specified in --file
	# The encrypted image will replace the original image and will have a BCH.
	# The original image is saved as $image.unencrypted if ${debug_mode} is 1.
	local image="$1";
	local ftype="$2";
	local sign_key="$3";
	local enc_key="$4";
	if [ "${debug_mode}" -eq 1 ]; then
		cp "${image}" "${image}".unencrypted
	fi

	tmpdir=$(mktemp -d);
	mkdir -p "${tmpdir}" > /dev/null 2>&1;
	image_file=$(readlink -f "${image}");
	cp "${image_file}" "${tmpdir}/file"

	if [ "${CHIPID}" = "0x23" ]; then
		"${LDK_DIR}"/l4t_sign_image.sh \
			--file "${tmpdir}/file" --type "${ftype}" \
			--key "${sign_key}" --encrypt_key "${enc_key}" --chip "${tegraid}" --split True \
			--enable_user_kdk True;
	else
		echo "Error: Unsupported chip id ${CHIPID}"
		exit 1
	fi

	cat "${tmpdir}/file".sig "${tmpdir}/file" > "${tmpdir}/file".encrypted
	cp "${tmpdir}/file".encrypted "${image_file}"
	rm -rf "${tmpdir}"
}

function uefi_signimage() {
	local image="$1";
	local key="$2";
	local cert="$3";
	local mode="$4";
	local enable_enc="$5";
	local debug_option="";

	if [ "${debug_mode}" -eq 1 ]; then
		debug_option=" --debug"
	fi

	"${LDK_DIR}"/l4t_uefi_sign_image.sh \
			--image "${image}" \
			--key "${key}" \
			--cert "${cert}" \
			--mode "${mode}" \
			${debug_option}
	chkerr "ERROR: uefi_signimage failed.";

	if [ "${uefi_enc_key}" != "" ] && [ "${enable_enc}" == "True" ]; then
		uefi_encryptimage "${image}" "data" "${key}" "${uefi_enc_key}"
	fi
}

function limit_boot_chains() {
	# Limit flashing to only A_* or B_* partitions
	# (T234+) according to the "--boot-chain-flash" option.
	# This receives the flash partition layout through a pipe and empties
	# filename placeholders of partitions that we want to skip flashing.
	IFS=$'\n';
	while read line; do
		if [[ "${line}" == *"<partition "* ]]; then
			partition_name=$(echo "${line}" | sed -nE 's/.* name="([^"]*)".*/\1/p');
			if [ "${boot_chain_flash}" == "A" ]; then
				if [[ "${partition_name}" == "B_"* ]] || [[ "${partition_name}" == *"_b" ]]; then
					skip_partition=1;
				fi;
			elif [ "${boot_chain_flash}" == "B" ]; then
				# There is no "*_a" boot chain naming convention
				if [[ "${partition_name}" == "A_"* ]]; then
					skip_partition=1;
				fi;
			fi;
		elif [[ "${line}" == *"</partition>"* ]]; then
			skip_partition=;
		fi;

		if [[ "${line}" == *"<filename>"* ]] && [ "${skip_partition}" == 1 ]; then
			echo "${line}" | sed -E 's|(<filename>).*(</filename>)|\1 \2|g';
		else
			echo "${line}";
		fi;
	done;
}

function limit_qspi_device() {
	# Limit flashing QSPI device only
	local device_type=
	local skip_device=0
	IFS=$'\n';
	while read -r line; do
		if [ "${qspi_only}" != 1 ]; then
			echo "${line}"
			continue
		fi

		if [[ "${line}" == *"<device "* ]]; then
			# Remove the lines not for QSPI device
			device_type="$(echo "${line}" | grep -o "type=\"[a-z]\+\"" | cut -d= -f 2)"
			if [ "${device_type}" != "\"spi\"" ]; then
				skip_device=1
			fi
		fi;
		if [ "${skip_device}" == 0 ]; then
			echo "${line}"
		fi;
		if [[ "${line}" == *"</device>" ]]; then
			skip_device=0
		fi
	done;
}

function process_dev_params() {
	# Process dev params for multiple boot chains
	# The flahs.sh currently supports up to pass four dev params files
	# for boot chains A to D.
	# This function concatenates the name of each dev params file with
	# comma as the divider and finally pass this constructed string
	# to tegraflash.py through the parameter "--dev_params"
	local dev_params_i=;
	local dev_params_name=;
	local dev_params_a_flag="true";
	local bcrargsopt=""
	local bctargs=""

	for dev_params_i in "dev_params" "dev_params_b" "dev_params_c" "dev_params_d"
	do
		if [ "${!dev_params_i}" != "" ]; then
			# At least the dev params for chain A exists.
			if [ "${dev_params_a_flag}" == "true" ]; then
				bcrargsopt+="--dev_params ";
				dev_params_a_flag="false"
			else
				bctargs+=","
			fi
			dev_params_name="${dev_params_i}name";
			cp2local "${dev_params_i}" "${BL_DIR}/${!dev_params_name}";
			if [ "${fuselevel}" = "fuselevel_production" ]; then
				sed -i "s/preprod_dev_sign = <1>/preprod_dev_sign = <0>/" "${BL_DIR}/${!dev_params_name}";
			fi
			bctargs+="${!dev_params_name}";
		else
			# Only support continual chains starting from A, including A only, AB, ABC and ABCD.
			break;
		fi;
	done
	bctargs+=" "
	if [ "${dev_params_a_flag}" == "false" ]; then
		if [ "${CHIPID}" = "0x26" ]; then
			set_value_in_bct_cfg "${bctargs}" "brbct_cfg/dev_param" "${localbctcfg}"
			set_value_in_bct_cfg "${bctargs}" "brbct_cfg/dev_param" "${localrcmbootbctcfg}"
			set_value_in_bct_cfg "${bctargs}" "hpct_cfg/dev_param" "${localbctcfg}"
			set_value_in_bct_cfg "${bctargs}" "hpct_cfg/dev_param" "${localrcmbootbctcfg}"
			set_value_in_bct_cfg "${bctargs}" "sbct_cfg/dev_param" "${localbctcfg}"
			set_value_in_bct_cfg "${bctargs}" "sbct_cfg/dev_param" "${localrcmbootbctcfg}"
		else
			BCTARGS+="${bcrargsopt}${bctargs}"
		fi
	fi
}

function update_overlay_dtb() {
	# Update the overlay dtb
	# If the specified node exists and the passed node_property is not NULL,
	# update it with the passed node_property.
	# If the node exists but the passed node_property is NULL, delete the node.
	# If the specified node does not exist and the passed node_property is not NULL,
	# add the node with the passed node_property.
	# When adding/updating a node with the passed node_property, the parent_node_name
	# might be specified for the location of the passed node. if parent_node_name is not
	# specified, gNVIDIAPublicVariableGuid will be used.
	# Usage:
	#	update_overlay_dtb {dtb_file} {node_name} {node_property} [parent_node_name]
	local dtb_file="${1}"
	local node_name="${2}"
	local node_property="${3}"
	local parent_node_name="${4}"
	local dts_file=
	local dts_file_updated=

	if [ ! -f "${dtb_file}" ]; then
		echo "Error: specified ${dtb_file} is not found."
		exit 1
	fi

	if [ "${node_name}" == "" ]; then
		echo "Error: the name of node is not specified"
		exit 1
	fi

	# Convert .dtbo to .dts
	dts_file="${dtb_file%.*}"
	dts_file_updated="${dts_file}_updated.dts"
	${DTC} -I dtb -O dts -o "${dts_file_updated}" "${dtb_file}"

	# Determine the lines of including the matched node.
	local start_line=
	local end_line=
	start_line="$(sed -n '/'"${node_name}"'/=' "${dts_file_updated}")"
	if [ "${start_line}" == "" ] && [ "${node_property}" == "" ]; then
		# Return if the specified node to be deleted is not found
		echo "Warning: the specified node to be deleted does not exist"
		rm -f "${dts_file_updated}"
		return
	fi

	# Delete the lines including the matched node.
	if [ "${start_line}" != "" ]; then
		end_line="$(sed -n ''"${start_line}"',${/};/=}' "${dts_file_updated}" | head -n 1)"
		sed -i ''"${start_line}"','"${end_line}"'d' "${dts_file_updated}"
	fi

	# Insert the node into the converted dts if passed node property is not NULL.
	local node=
	if [ "${node_property}" != "" ]; then
		node="${node_name} ${node_property}"
		parent_node_name="${parent_node_name:-gNVIDIAPublicVariableGuid}"
		sed -i '/'"${parent_node_name}"' {/a '"${node}"'' "${dts_file_updated}"
	fi

	# Covert the updated .dts to .dtbo
	${DTC} -I dts -O dtb -o "${dtb_file}" "${dts_file_updated}"
	rm -f "${dts_file_updated}"
}

function process_l4t_conf_dtbo() {
	# Update the UEFI variables in L4TConfiguration.dtbo
	local a_node=

	# Update current BSP version to "uefi/fmp-lowest-supported-version" in L4TConfiguration.dtbo
	a_node=$( printf "= <0x%02x%02x%02x%02x>;" \
		$(( (BSP_BRANCH>>8)&0xFF )) $(( BSP_BRANCH&0xFF )) $(( BSP_MAJOR&0xFF )) $(( BSP_MINOR&0xFF )) )
	update_overlay_dtb "${L4T_CONF_DTBO}" "fmp-lowest-supported-version" "${a_node}" "uefi"

	# Update the value of "gNVIDIAPublicVariableGuid/RootsRedundancyLevel" if ROOTFS_AB is 1.
	if [ "${rootfs_ab}" == 1 ]; then
		a_node="{data = [ 01 00 00 00 ];runtime;locked;};"
		update_overlay_dtb "${L4T_CONF_DTBO}" "RootfsRedundancyLevel" "${a_node}"
	fi

	# Update the value of "gNVIDIAPublicVariableGuid/RootfsRetryCountMax" if
	# ROOTFS_RETRY_COUNT_MAX is set between 1 and 3.
	local retry_count_max=
	if [ -n "${ROOTFS_RETRY_COUNT_MAX}" ] \
		&& [ "${ROOTFS_RETRY_COUNT_MAX}" -ge 1 ] \
		&& [ "${ROOTFS_RETRY_COUNT_MAX}" -le 3 ]; then
		retry_count_max=$(printf '%02x' "${ROOTFS_RETRY_COUNT_MAX}")
		a_node="{data = [ ${retry_count_max} 00 00 00 ];runtime;locked;};"
		update_overlay_dtb "${L4T_CONF_DTBO}" "RootfsRetryCountMax" "${a_node}"
	fi

	# Call "set_boot_to_recovery" function if it is defined.
	# The "set_boot_to_recovery" function is defined in image-based OTA.
	if declare -F -f set_boot_to_recovery > /dev/null 2>&1; then
		set_boot_to_recovery
	fi

	if [ "${rcm_boot}" -eq 1 ]; then
		update_overlay_dtb "${L4T_CONF_DTBO}" "DefaultBootPriority" "{data = \"boot.img\";locked;};" "gNVIDIATokenSpaceGuid"
	fi
}

### Function to convert a key file to an ascii hex string by removing '0x' and ' '.
#
function convert_key_to_hex()
{
        result=$(sed -e 's/\(0x\| \)//g' "$1");
        echo "${result}";
}

### Function to check if a sd card partition config file is used and mismatch with the board
function check_device_mismatch() {
	local config_file="${1}"
	# An associative array with boardid as the key and an array of boardsku as the value
	# The value should be string that elements are seperate by spaces. e.g. ['3767']="0005 0100"
	# Example of Adding a new boardid with 2 skus:
	# declare -rA board_with_sdcard=(
	# 	['3767']="0005"
	# 	['3701']="0009 0010"
	# )
	declare -rA board_with_sdcard=(
		['3767']="0005"
	)

	# check if the filename contains "_sd_" or "_sd." pattern
	# check if the board is the one we want to check whether it supports sdcard
	if [[ "${config_file}" =~ _"sd"[_/\.] && -n "${board_with_sdcard["${BOARDID}"]}" ]]; then
		local mismatch_flag=true
		read -r -a sku_list <<< "${board_with_sdcard["${BOARDID}"]}"

		for sku in "${sku_list[@]}"; do
			if [[ "${sku}" == "${BOARDSKU}" ]]; then
				mismatch_flag=false
				break
			fi
		done

		if [[ "${mismatch_flag}" == true ]]; then
			echo "Error: Using a flash partition table config file of SD card, ${config_file}, but the target board does not have SD card"
			echo "To bypass this error, please add your board information to the board_with_sdcard array in this script."
			exit 1;
		fi
	fi
}

### Function to put board specification into a configuration file
function inject_board_spec_info() {
	echo "Copying nv_boot_control.conf to rootfs"
	mkdir -p "${rootfs_dir}/etc";
	cp -f "${BL_DIR}/nv_boot_control.conf" "${rootfs_dir}/etc"
	ota_boot_dev="/dev/mmcblk0boot0"
	ota_gpt_dev="/dev/mmcblk0boot1"
	if [ "${OTA_BOOT_DEVICE}" != "" ]; then
		ota_boot_dev="${OTA_BOOT_DEVICE}"
	fi;
	if [ "${OTA_GPT_DEVICE}" != "" ]; then
		ota_gpt_dev="${OTA_GPT_DEVICE}"
	fi
	sed -i '/TNSPEC/d' "${rootfs_dir}/etc/nv_boot_control.conf";
	sed -i "$ a TNSPEC ${spec}" "${rootfs_dir}/etc/nv_boot_control.conf";
	sed -i '/TEGRA_CHIPID/d' "${rootfs_dir}/etc/nv_boot_control.conf";
	sed -i "$ a TEGRA_CHIPID ${CHIPID}" "${rootfs_dir}/etc/nv_boot_control.conf";
	sed -i '/TEGRA_OTA_BOOT_DEVICE/d' "${rootfs_dir}/etc/nv_boot_control.conf";
	sed -i "$ a TEGRA_OTA_BOOT_DEVICE ${ota_boot_dev}" "${rootfs_dir}/etc/nv_boot_control.conf";
	sed -i '/TEGRA_OTA_GPT_DEVICE/d' "${rootfs_dir}/etc/nv_boot_control.conf";
	sed -i "$ a TEGRA_OTA_GPT_DEVICE ${ota_gpt_dev}" "${rootfs_dir}/etc/nv_boot_control.conf";

}

### Function to convert semicolon-separated string to YAML sequence
# Example input:
# convert_to_yaml_sequence "ENABLE_A;ENABLE_B" "4"
# Output:
#     ? "ENABLE_A"
#     ? "ENABLE_B"
function convert_to_yaml_sequence() {
	local input="${1}"
	local indent="${2}"
	local result=""

	IFS=';' read -r -a values <<< "${input}"

	for value in "${values[@]}"; do
		if [ -z "${value}" ]; then
			return
		fi
		result+="$(printf "%${indent}s" "")? \"${value}\"\n"
	done
	printf "%s" "${result}"
}

### Function to generate platform config overlay profile (bct preprocessor defines)
# Generates a YAML configuration file for BCT defines
# Input:
#   $1 - output filename
# Environment variables used:
#   BL_DIR - Base directory for output
#   *_BCT_DEFINES - Semicolon-separated list defined in the board config file
function generate_overlay_pcp() {
	local output_file="${1}"
	local overlay_pcp_prefix="bct_defines:\n  number_of_chains: 2\n"
	local overlay_pcp_content=""
	local defines_array=("COMMON_BCT_DEFINES" "RCMBOOT_BCT_DEFINES" "RCMFLASH_BCT_DEFINES" "COLDBOOT_BCT_DEFINES")
	local DEFINE_INDENT=4
	local CHAIN_INDENT=6

	for define_variable in "${defines_array[@]}"; do
		if [ -z "${!define_variable}" ]; then
			continue
		fi

		case $define_variable in
		COMMON_BCT_DEFINES)
			overlay_pcp_content+="  common_flags: &common_flags"
			common_flags="$(convert_to_yaml_sequence "${!define_variable}" "${DEFINE_INDENT}")"
			if [ -z "${common_flags}" ]; then
				overlay_pcp_content+=" {}\n"
			else
				overlay_pcp_content+="\n${common_flags}"
			fi
			;;
		RCMBOOT_BCT_DEFINES)
			overlay_pcp_content+="  rcm_boot:\n"
			overlay_pcp_content+="    <<: *common_flags\n"
			overlay_pcp_content+="$(convert_to_yaml_sequence "${!define_variable}" "${DEFINE_INDENT}")"
			;;
		RCMFLASH_BCT_DEFINES)
			overlay_pcp_content+="  rcm_flash:\n"
			overlay_pcp_content+="    <<: *common_flags\n"
			overlay_pcp_content+="$(convert_to_yaml_sequence "${!define_variable}" "${DEFINE_INDENT}")"
			;;
		COLDBOOT_BCT_DEFINES)
			overlay_pcp_content+="  chains:\n"
			overlay_pcp_content+="    chain_A:\n"
			overlay_pcp_content+="      <<: *common_flags\n"
			overlay_pcp_content+="$(convert_to_yaml_sequence "${!define_variable}" "${CHAIN_INDENT}")"
			overlay_pcp_content+="    chain_B:\n"
			overlay_pcp_content+="      <<: *common_flags\n"
			overlay_pcp_content+="$(convert_to_yaml_sequence "${!define_variable}" "${CHAIN_INDENT}")"
			;;
		esac

	done

	if [ -n "${overlay_pcp_content}" ]; then
		if [ -z "${COMMON_BCT_DEFINES}" ]; then
			overlay_pcp_prefix+="  common_flags: &common_flags {}\n"
		fi
		overlay_pcp_prefix+="${overlay_pcp_content}"
		echo -e "${overlay_pcp_prefix}" > "${BL_DIR}/${output_file}"
		BCTARGS+="--overlay_bct_flags_file ${output_file} ";
	fi
}

# Check if the format of the bct element in the keylist is valid
# Input:
#   $1 - keylist: the path to the XML file containing the keylist
# Output:
#   Exits with code 1 if the format is invalid
function check_pkc_keylist() {
	local keylist="${1}"
	local key_index=""
	local chip_id=""
	local __value=""
	local list_name="entry_list"
	local global_key_info_tagname="bct"

	get_value_in_xml "/${list_name}/${global_key_info_tagname}" "${keylist}" "pcp_file" __value
	get_value_in_xml "/${list_name}/${global_key_info_tagname}" "${keylist}" "pcps_file" __value
	get_value_in_xml "/${list_name}/${global_key_info_tagname}" "${keylist}" "pcps_hash_file" __value

	get_value_in_xml "/${list_name}/${global_key_info_tagname}" "${keylist}" "chip_id" chip_id
	if [[ "${chip_id}" != "0x260" ]]; then
		echo "Error: keylist is only support on t264"
		exit 1
	fi

	get_value_in_xml "/${list_name}/${global_key_info_tagname}" "${keylist}" "active_index" key_index
	if [[ "${key_index}" -lt 0 || "${key_index}" -gt 15 ]]; then
		echo "Error: key index ${key_index} in ${keylist} is out of range"
		exit 1
	fi
}

# Get the active PKC key from the keylist file and store it in a variable
# Input:
#   $1 - keylist: the path to the PKC keylist file
#   $2 - ret_var: the variable to store the active key indicated in the keylist
function get_active_key_from_keylist(){
	local keylist="${1}"
	local ret_var="${2}"
	local active_key_index
	local active_key

	check_pkc_keylist "${keylist}"

	get_value_in_xml "/entry_list/bct" "${keylist}" "active_index" active_key_index
	active_key="$(xmlstarlet sel -t -m "/entry_list/entry[@key_id='${active_key_index}']" -v "@key" -n "${keylist}")"
	if [ -z "${active_key}" ]; then
		echo "Error: Failed to get active_key name for index ${active_key_index}"
		exit 1
	fi

	# Create fake key for HSM flow
	if [ ! -f "${active_key}" ] && [ "${hsm_enable}" -eq 1 ]; then
		touch "${active_key}"
	fi

	eval "${ret_var}=\"${active_key}\"";
}

if [ $# -lt 2 ]; then
	usage allunknown 1;
fi;

nargs=$#;
target_rootdev=${!nargs};
nargs=$(($nargs-1));
ext_target_board=${!nargs};

# NV internal
_nvbrd_trk=0

# NV internal workflows may allow board autodetection
board_detection_helper="flash_board_detection.sh"
if [ -f "${board_detection_helper}" ]; then
	source "${board_detection_helper}"
	if [ "${ext_target_board}" == "autodetect" ]; then
		ext_target_board=$(get_target_board)
		if [ -z "${ext_target_board}" ]; then
			echo "Error: board autodetection failed, must pass a valid board name"
			usage allunknown 1;
		fi
	else
		check_target_board "${ext_target_board}"
	fi
elif [ "${ext_target_board}" == "autodetect" ]; then
	echo "Error: board autodetection not possible, must pass a valid board name"
	usage allunknown 1;
fi

if [ ! -r "${ext_target_board}.conf" ]; then
	echo "Error: Invalid target board - ${ext_target_board}.";
	usage allunknown 1;
fi

# set up LDK_DIR path
LDK_DIR=$(cd "$(dirname "$0")" && pwd);
LDK_DIR=$(readlink -f "${LDK_DIR}");

# set common print message for process_board_version()
print_board_version()
{
	local board_id="${1}";
	local board_version="${2}";
	local board_sku="${3}";
	local board_revision="${4}";
	local chiprev="${5}";

	echo "Board ID(${board_id}) version(${board_version}) sku(${board_sku}) revision(${board_revision})";
}

ext_target_board_canonical=$(readlink -e "${ext_target_board}".conf)
ext_target_board_canonical=$(basename "${ext_target_board_canonical}" .conf)
GENERIC="generic"
disk_enc_enable=0;
rootfs_ab=0;
NFS_ROOTFS_SIZE=209715200
source "${ext_target_board}.conf"

# Use the specified board family if it has.
if [ "${BOARD_FAMILY}" != "" ]; then
	target_board=${BOARD_FAMILY}
fi

# set up path variables
BL_DIR="${LDK_DIR}/bootloader";
TARGET_DIR="${BL_DIR}/${target_board}";
KERNEL_DIR="${LDK_DIR}/kernel";
export PATH="${KERNEL_DIR}:${PATH}:${BL_DIR}";		# preference on our DTC
if [ -z "${DTB_DIR}" ]; then
	DTB_DIR="${KERNEL_DIR}/dtb";
fi
if [ ! -d "${DTB_DIR}" ]; then
	echo "DTB_DIR = ${DTB_DIR}";
	echo "Error: directory does not exist.";
	exit 1;
fi
DTC="${KERNEL_DIR}/dtc";
FDTOVERLAY="${KERNEL_DIR}/fdtoverlay";
L4T_CONF_DTBO="${BL_DIR}/L4TConfiguration.dtbo"
if [ "${BINSARGS}" = "" -a "${BINS}" != "" ]; then			#COMPAT
	BINSARGS="--bins \"";						#COMPAT
fi;									#COMPAT

# Print BSP Info:
#
echo "${INFODIVIDER}";
echo "# L4T BSP Information:";
if [ -f "${LDK_DIR}/nv_tegra/bsp_version" ]; then
	source "${LDK_DIR}/nv_tegra/bsp_version"
	echo "# R${BSP_BRANCH} , REVISION: ${BSP_MAJOR}.${BSP_MINOR}"
elif [ -f "${LDK_DIR}/rootfs/etc/nv_tegra_release" ]; then
	head -n1 "${LDK_DIR}/rootfs/etc/nv_tegra_release" | \
		sed -e s/DATE:/\\n\#\ DATE:/;
else
	echo "# Unknown Release";
fi;

if [ -f "${LDK_DIR}/nv_tegra/user_version" ]; then
	source "${LDK_DIR}/nv_tegra/user_version"
	echo "# User release: ${USER_VERSION}"
	# Store user release version into rootfs/etc/user_release_version
	if [ -d "${LDK_DIR}/rootfs/etc/" ]; then
		echo -n "# User release: ${USER_VERSION}" >"${LDK_DIR}/rootfs/etc/user_release_version"
	fi
fi

echo "${INFODIVIDER}";

# Determine rootdev_type
#
rootdev_type="external";
if [[ "${target_rootdev}" == "internal" || "${target_rootdev}" == mmcblk0p* || \
      "${target_rootdev}" == ${BOOTDEV} ]]; then
	rootdev_type="internal";
	INITRD="";
	if [ ${disk_enc_enable} -eq 1 ] && [ "${target_rootdev}" != "internal" ]; then
		echo "Change device boot from ${target_rootdev} to internal"
		target_rootdev="internal";
	fi
elif [ "${target_rootdev}" = "eth0" -o "${target_rootdev}" = "eth1" ]; then
	rootdev_type="network";
	disk_enc_enable=0;
	rootfssize="${NFS_ROOTFS_SIZE}"
elif [[ "${target_rootdev}" != "external" && "${target_rootdev}" != mmcblk1p* && \
	"${target_rootdev}" != sd* && "${target_rootdev}" != nvme* ]]; then
	echo "Error: Invalid target rootdev($target_rootdev).";
	usage rootdevunknown 1;
fi;

if [[ "${rootdev_type}" == "external" && "${disk_enc_enable}" -eq 1 ]]; then
	echo "Error: When disk encryption is enabled, root device cannot be ${target_rootdev}"
	exit 1;
fi;

# Import disk encryption helper function
#
if [ ${disk_enc_enable} -eq 1 ]; then
	disk_encryption_helper_dir="${LDK_DIR}/tools/disk_encryption";
	if [ ! -f "${disk_encryption_helper_dir}/disk_encryption_helper.func" ]; then
		echo "Error: disk encryption is not supported."
		exit 1
	fi
	source "${disk_encryption_helper_dir}/disk_encryption_helper.func"
	# Determine the KDF based on the CHIPID
	gen_luks_password_kdf="";
	if [ "${CHIPID}" == "0x26" ]; then
		gen_luks_password_kdf="hmac-kdf"
	elif [ "${CHIPID}" == "0x23" ]; then
		gen_luks_password_kdf="cmac-kdf"
	else
		echo "Error: Disk encryption is not supported for ${CHIPID}."
		return 1;
	fi;
fi;

rootfsuuid="";
rootfsuuid_enc="";
rootfsuuid_ext="";
rootfsuuid_ext_enc="";
rootfsuuid_b="";
rootfsuuid_b_enc="";
rootfsuuid_b_ext=""
rootfsuuid_b_ext_enc="";
cmdline_b="";

rootfsuuidfile="${BL_DIR}/l4t-rootfs-uuid.txt"
read_part_name="";
rcm_boot=0;
no_root_check=0;
no_flash=0;
kdump_enable=0;
rapid_boot=0
external_device=0;
sparseupdate=0;
no_systemimg=0;
bup_blob=0;
single_bup_blob=0;
single_bup_name="";
bup_type="";
to_sign=0;
support_multi_spec=0;
clean_up=0;
generic_passphrase=0;
reuse_uuid=0;
PASSTHROUGH=""
_ODMDATA="";
_BPFFILE="";
_TBCDTB_FILE=""
with_systemimg=
OVERLAY_DTB_FILE="${OVERLAY_DTB_FILE},${ADDITIONAL_DTB_OVERLAY}"
boot_chain_flash="ALL";
boot_chain_select="A";
read_info="False";
qspi_only=0
gen_read_ramcode=0
set_size=0
hsm_enable=0
debug_mode=0
gen_systemimg_only=0
IGNORE_BOARD_CONF_ERROR="";
disable_random_iv="False";
keyfile=""
opstr+="b:c:d:e:f:g:h:i:k:l:m:n:o:ru:v:w:x:B:C:F:G:I:K:L:M:N:R:S:T:U:Z:-:";
while getopts "${opstr}" OPTION; do
	case $OPTION in
	b) BCTFILE=${OPTARG}; ;;
	c) CFGFILE=${OPTARG}; ;;
	d) DTBFILE=${OPTARG}; ;;
	e) _BPFFILE=${OPTARG}; ;;
	f) FLASHAPP=${OPTARG}; ;;
	g) BPFDTBFILE=${OPTARG}; ;;
	h) usage allunknown 0; ;;
	i) ENC_RFS_KEY=${OPTARG}; ;;
	k) target_partname=${OPTARG}; ;;	# cmdline only
	l) _TBCDTB_FILE=${OPTARG}; ;;	# cmdline only
	m) MTSPREBOOT=${OPTARG}; ;;
	n) NFSARGS=${OPTARG}; ;;
	o) _ODMDATA=${OPTARG}; ;;
	r) reuse_systemimg="true"; ;;		# cmdline only
	u) keyfile="${OPTARG}"; ;;
	v) SBKKEY=${OPTARG}; ;;
	w) WB0BOOT=${OPTARG}; ;;
	x) tegraid=${OPTARG}; ;;
	B) BOARDID=${OPTARG}; ;;
	C) CMDLINE="${OPTARG}"; ;;
	F) set_uefi_bins "${OPTARG}"; ;;
	G) read_part_name=${OPTARG}; ;;
	I) INITRD=${OPTARG}; ;;
	K) KERNEL_IMAGE=${OPTARG}; ;;
	L) BOOTLOADER=${OPTARG}; ;;
	M) MTS=${OPTARG}; ;;
	N) NFSROOT=${OPTARG}; ;;
	R) ROOTFS_DIR=${OPTARG}; ;;
	S) ROOTFSSIZE=${OPTARG};set_size=1; ;;
	T) EXT_NUM_SECTORS=${OPTARG}; ;;
	U) INT_NUM_SECTORS=${OPTARG}; ;;
	Z) zflag="true"; ;;			# cmdline only
	-) case ${OPTARG} in
	   no-root-check) no_root_check=1; ;;
	   no-flash) no_flash=1; ;;
	   kdump) kdump_enable=1; ;;
	   no-systemimg) no_systemimg=1; ;;
	   external-device) external_device=1; ;;
	   sparseupdate) sparseupdate=1; ;;
	   rcm-boot) rcm_boot=1; ;;
	   generic-passphrase) generic_passphrase=1; ;;
	   reuse-uuid) reuse_uuid=1; ;;
	   # allow payload generation to happen without sudo option
	   bup) no_root_check=1;bup_blob=1; ;;
	   single-image-bup)
		single_bup_blob=1;
		single_bup_name="${!OPTIND}";
		OPTIND=$(($OPTIND + 1));
		;;
	   bup-type)
		bup_type="${!OPTIND}";
		OPTIND=$(($OPTIND + 1));
		;;
	   sign) to_sign=1; ;;
	   multi-spec) support_multi_spec=1; ;;
	   clean-up) clean_up=1; ;;
	   usb-instance)
		usb_instance="${!OPTIND}";
		OPTIND=$(($OPTIND + 1));
		;;
	   image)
		write_image_name="${!OPTIND}";
		OPTIND=$(($OPTIND + 1));
		;;
		tosfile)
		TOSFILE="${!OPTIND}";
		OPTIND=$(($OPTIND + 1));
		;;
	   user_key)
		USERKEY="${!OPTIND}";
		OPTIND=$(($OPTIND + 1));
		;;
	   uda-dir)
		UDADIR="${!OPTIND}";
		OPTIND=$(($OPTIND + 1));
		;;
	   uefi-keys)
		UEFI_KEYS_CONF="${!OPTIND}";
		OPTIND=$(($OPTIND + 1));
		;;
	   uefi-enc)
		UEFI_ENC="${!OPTIND}";
		OPTIND=$(($OPTIND + 1));
		;;
	   pv-crt)
		PVCERT="${!OPTIND}";
		OPTIND=$(($OPTIND + 1));
		;;
	   pv-enc)
		PVENC="${!OPTIND}";
		OPTIND=$(($OPTIND + 1));
		;;
	   boot-chain-flash)
		boot_chain_flash="${!OPTIND}";
		# store user string in uppercase
		boot_chain_flash="${boot_chain_flash^^}"
		OPTIND=$(($OPTIND + 1));
		;;
	   boot-chain-select)
		boot_chain_select="${!OPTIND}";
		# store user string in uppercase
		boot_chain_select="${boot_chain_select^^}"
		OPTIND=$(($OPTIND + 1));
		;;
	   with-systemimg)
		with_systemimg=1
		;;
	   disable-random-iv) disable_random_iv="True"; ;;
	   read-info) read_info="True"; ;;
	   qspi-only) qspi_only=1; ;;
	   passthrough)
		PASSTHROUGH=" ${!OPTIND} ";
		OPTIND=$((OPTIND + 1));
		;;
       read-ramcode) gen_read_ramcode=1; ;;
	   gen-read-eeprom)
		gen_read_ramcode=1;
		no_flash=1;
		read_info="True";
		;;
	   hsm) hsm_enable=1; ;;
	   debug) debug_mode=1; ;;
	   gen-systemimg-only)
		gen_systemimg_only=1;
		IGNORE_BOARD_CONF_ERROR="True";
		;;
	   *) usage allunknown 1; ;;
	   esac;;
	*) usage allunknown 1; ;;
	esac;
done

# if the user is not root, there is not point in going forward unless
# the user knows what he's doing.
if [ "${no_root_check}" != "1" ] && [ "${USER}" != "root" ]; then
	echo "flash.sh requires root privilege";
	exit 1;
fi

if [[ "${BUILD_SD_IMAGE}" == 1 && "${no_flash}" == 0 ]]; then
	echo "*** The option BUILD_SD_IMAGE must work with --no-flash flag. ***"
	echo "Exiting now...";
	exit 1;
fi;

if [ "${boot_chain_flash}" != "A" ] && [ "${boot_chain_flash}" != "B" ] && [ "${boot_chain_flash}" != "ALL" ]; then
	echo "Boot chain ${boot_chain_flash} not recognized. Valid options are A/a, B/b or all.";
	exit 1;
fi;

# Import python-pkcs11 module when HSM is used
if [ "${hsm_enable}" -eq 1 ]; then
	pkcs11_module=$(python3 -m pip show python-pkcs11)
	pkcs11_location=$(echo "${pkcs11_module}" | grep "Location:" | cut -d ' ' -f 2-)
	if [ "${pkcs11_module}" == "" ] || [ "${pkcs11_location}" == "" ]; then
		echo "Need to install python-pkcs11 module before using HSM."
		exit 1;
	fi;
	export PYTHONPATH="${pkcs11_location}:${PYTHONPATH}"
fi;

# Validate the option "--boot-chain-select"
# Only 2 boot chains (A and B) are supported currently
if [ "${boot_chain_select}" != "A" ] && [ "${boot_chain_select}" != "B" ]; then
	echo "Selected boot chain ${boot_chain_select} is invalid. Valid options are A/a or B/b.";
	exit 1;
fi;

# If "--boot-chain-flash" is not set to "all", make sure that
# the selected boot chain is the same as the flashed chain.
if [ "${boot_chain_flash}" != "ALL" ] && [ "${boot_chain_flash}" != "${boot_chain_select}" ]; then
	echo "Flashed chain (${boot_chain_flash}) is different from selected boot chain (${boot_chain_select}).";
	exit 1
fi;

# Both chains should be flashed if selected boot chain is "B"
if [ "${boot_chain_select}" == "B" ] && [ "${boot_chain_flash}" != "ALL" ]; then
	echo "Both chains should be flashed if selected boot chain is \"B\".";
	exit 1
fi;

# get key file if -u option provided
pkc_keylistfile=""
if [ "${keyfile}" != "" ]; then
	if [[ ! ${keyfile} =~ ^/ ]]; then
		keyfile=$(readlink -f "${keyfile}");
	fi;

	if [ "${CHIPID}" = "0x26" ]; then
		pkc_keylistfile="${keyfile}"
		get_active_key_from_keylist "${pkc_keylistfile}" keyfile
	fi

	if [ ! -f "${keyfile}" ]; then
		echo "Error: keyfile ${keyfile} not found";
		exit 1;
	fi;
fi;

# get sbk key file if -v option provided
sbk_keyfile="";
if [ "${SBKKEY}" != "" ]; then
	# when sbk key is present, pkc key must be present
	if [ "${keyfile}" = "" ]; then
		echo "Error: missing PKC key; try -u";
		exit 1;
	fi;

	sbk_keyfile=$(readlink -f "${SBKKEY}");
	if [ ! -f "${sbk_keyfile}" ]; then
		echo "Error: keyfile ${sbk_keyfile} not found";
		exit 1;
	fi;
fi;

pv_pub_key_hash=""
if [ "${PVCERT}" != "" ]; then
	if [ "${keyfile}" = "" ] || [ ! -f "${keyfile}" ]; then
		echo "Error: missing PKC key; try -u";
		exit 1;
	fi;
	pv_certfile=$(readlink -f "${PVCERT}");
	if [ ! -f "${pv_certfile}" ]; then
		echo "Error: certificate file ${pv_certfile} not found";
		exit 1;
	fi;

	pushd "$BL_DIR" > /dev/null 2>&1;
	certpubkey=$(mktemp)
	certpkh=$(mktemp)
	pv_key_algo=""

	extract_pub_key "${keyfile}" "${pv_certfile}" "${certpubkey}"

	./tegrasign_v3.py --pubkeyhash "${certpubkey}" "${certpkh}" "${pv_key_algo}" --sha sha512
	pv_pub_key_hash="$(xxd -p -e "${certpkh}" | cut -c 11-46 | tr -d ' ' | tr -d '\n' | sed 's/.\{8\}/0x& /g')"
	rm -rf "${certpubkey}" "${certpkh}"
	popd > /dev/null 2>&1;
fi;

pv_enc_key="";
pv_enc_content="0 0 0 0 0 0 0 0"
if [ "${PVENC}" != "" ]; then
	if [ "${CHIPID}" != "0x23" ]; then
		echo "Error: PV key encryption feature is not supported on this platform";
		exit 1;
	fi;

	pv_enc_key=$(readlink -f "${PVENC}");
	if [ ! -f "${pv_enc_key}" ]; then
		echo "Error: keyfile ${pv_enc_key} not found";
		exit 1;
	fi;
	pv_enc_content=$(<"$pv_enc_key")
fi;

uefi_secure_dtbo=""
# get uefi keys config file if --uefi-keys option is provided
if [ "${UEFI_KEYS_CONF}" != "" ]; then
	uefi_keys_conf=$(readlink -f "${UEFI_KEYS_CONF}");
	if [ ! -f "${uefi_keys_conf}" ]; then
		echo "Error: UEFI keys conf file ${uefi_keys_conf} not found";
		exit 1;
	fi;
	source "${uefi_keys_conf}"
	# cd to ${uefi_keys_conf}'s directory
	uefi_keys_conf_dir=$(dirname "${uefi_keys_conf}")
	pushd "${uefi_keys_conf_dir}" > /dev/null 2>&1 || exit 1
	mkfilesoft uefi_db_key  "${UEFI_DB_1_KEY_FILE}" ""
	mkfilesoft uefi_db_cert  "${UEFI_DB_1_CERT_FILE}" ""
	# Copy the dtbo file
	if [ ${bup_blob} -eq 1 ]; then
		uefi_secure_dtbo="UefiUpdateSecurityKeys.dtbo"
	else
		uefi_secure_dtbo="UefiDefaultSecurityKeys.dtbo"
	fi

	if [ ! -f "${uefi_secure_dtbo}" ]; then
		echo "Error: ${uefi_secure_dtbo} not found"
		echo "Run 'gen_uefi_keys_dts.sh <keys_conf>' first"
		exit 1;
	fi
	cp  "${uefi_secure_dtbo}" "${LDK_DIR}"/bootloader/"${uefi_secure_dtbo}"
	# Add the dtbo to OVERLAY_DTB
	OVERLAY_DTB_FILE="${OVERLAY_DTB_FILE}","${uefi_secure_dtbo}"
	popd  > /dev/null 2>&1 || exit 1
fi;

uefi_enc_key="";
uefi_enc_content="0 0 0 0 0 0 0 0"
if [ "${UEFI_ENC}" != "" ]; then
    if [ "${rcm_boot}" -eq 1 ]; then
        echo "Error: '--uefi-enc' option is not allowed in '--rcm-boot' mode.";
        exit 1;
    fi
	if [ "${UEFI_KEYS_CONF}" == "" ]; then
		echo "Error: '--uefi-enc' option needs '--uefi-keys' option specified to work "
		exit 1;
	fi
	uefi_enc_key=$(readlink -f "${UEFI_ENC}");
	if [ ! -f "${uefi_enc_key}" ]; then
		echo "Error: keyfile ${uefi_enc_key} not found";
		exit 1;
	fi;
	uefi_enc_content=$(<"$uefi_enc_key")
	echo "uefi_enc_content= ${uefi_enc_content}"
	uefi_enc_hex=$(convert_key_to_hex "${uefi_enc_key}")
	key_len=${#uefi_enc_hex}
	if [ "${CHIPID}" = "0x23" ]; then
		if [ "${key_len}" != 64 ]; then
			echo "Error: key size has to be 64"
			exit 1;
		fi
	fi
fi;

# get enc rfs key file if -i option provided
enc_rfs_keyfile="";
if [ "${ENC_RFS_KEY}" != "" ]; then
	enc_rfs_keyfile=$(readlink -f "${ENC_RFS_KEY}");
	if [ ! -f "${enc_rfs_keyfile}" ]; then
		echo "Error: keyfile ${enc_rfs_keyfile} not found";
		exit 1;
	fi;
fi;

ECID="";
# get the fuse level and update the data accordingly
fuselevel="";
hwchipid="";
hwchiprev="${CHIPREV}";
if [ "${hwchiprev}" = "" ]; then
	hwchiprev="0";
fi;
bootauth="";

get_fuse_level fuselevel hwchipid bootauth;
# fuselevel_unknown or empty will be handled as fuselevel_production
if [ "${fuselevel}" = "fuselevel_unknown" ] || [ "${fuselevel}" = "" ]; then
	fuselevel="${DFLT_FUSELEVEL:-fuselevel_production}";
fi;
fuselevel="${FUSELEVEL:-${fuselevel}}"

declare -F -f process_fuse_level > /dev/null 2>&1;
if [ $? -eq 0 ]; then
	process_fuse_level "${fuselevel}";
fi;

#
# Handle -G option for reading partition image to file
#
if [ "${read_part_name}" != "" ]; then
	# Exit if no -k option
	if [ "${target_partname}" = "" ]; then
		echo "Error: missing -k option to specify partition name";
		exit 1;
	fi
	# Exit if --image option is provided for write partition
	if [ "${write_image_name}" != "" ]; then
		echo "Error: not support to write partition while reading partition";
		exit 1;
	fi
	# Exit if path is invalid
	get_full_path ${read_part_name} read_part_name;
fi;

#
# Handle --image option for writing image to specified partition
#
if [ "${write_image_name}" != "" ]; then
	# Exit if no -k option
	if [ "${target_partname}" = "" ]; then
		echo "Error: missing -k option to specify partition name";
		exit 1;
	fi
	# Exit if file does not exist
	if [ ! -f "${write_image_name}" ]; then
		echo "Error: ${write_image_name} does not exist";
		exit 1;
	fi;
	# Exit if path is invalid
	get_full_path ${write_image_name} write_image_name;
fi;

# SoC Sanity Check
chk_soc_sanity;

###########################################################################
# System mandatory vars for dumping eeprom:
#
mkfilesoft emc_fuse_dev_params     "${TARGET_DIR}/BCT/${EMC_FUSE_DEV_PARAMS}" "";
mkfilesoft emcfuse	"${EMCFUSE}"	"${BL_DIR}/${EMCFUSE}";
mkfilesoft device_config  "${TARGET_DIR}/BCT/${DEVICE_CONFIG}" "";
mkfilesoft misc_config    "${TARGET_DIR}/BCT/${MISC_CONFIG}" "";
mkfilesoft uphy_config    "${TARGET_DIR}/BCT/${UPHY_CONFIG}" "";
if [ "${CHIPID}" = "0x23" ] || [ "${CHIPID}" = "0x23 9" ]; then
	# READINFOFILE must be defined for 0x23 boards in order to dump its eeprom.
	mkfilepath readinfofile    "${READINFOFILE}" "";
fi
mkfilesoft mb2applet    "${APPLET}"	 "";
mkfilesoft minratchet_config    "${TARGET_DIR}/BCT/${MINRATCHET_CONFIG}" "";

# read-ramcode is only supported on 0x234 or 0x264 (not on 0x239 or 0x268)
if [ ${gen_read_ramcode} -eq 1 ]; then
	if [ "${CHIPID}" = "0x23 9" ] || [ "${CHIPID}" = "0x26 8" ]; then
		echo "Ignore --read-ramcode option for 0x239 or 0x268 chip"
		gen_read_ramcode=0;
	else
		# Generate rcmdump_blob if --read-ramcode option is set.
		rm -rf "${BL_DIR}"/rcmdump_blob
		gen_rcmdump_blob
		chkerr "ERROR: failed to generate rcmdump_blob";
	fi
fi

# get the board version and update the data accordingly
# Note: skip getting board version for SIMULATION targets
if declare -F -f process_board_version > /dev/null 2>&1; then
	if [ -z "${CONF_SIMULATION}" ]; then
		board_FAB="${FAB}";
		board_id="${BOARDID}";
		board_sku="${BOARDSKU}";
		board_revision="${BOARDREV}"
		board_ramcode="${RAMCODE_ID}";
		platform_id="";
		boot_device="";

		if [ "${FORCE_ACCESS}" != "" ] || [ "${board_FAB}" == "" ] || [ ${read_info} == "True" ]; then
			# target has to be connected once comes here (except it is to do "read-info");
			if [ "${CHIPID}" = "0x23" ] && [ "${hwchipid}" == "" ] && [ ${read_info} == "False" ]; then
				# Exit if there is no target connected and no board spec presented (represented by ${FAB}).
				echo "Error: No Board Spec. and no target connected, exit."
				exit 1;
			fi
			if [ "${hwchipid}" != "" ] || [ ${read_info} == "True" ] \
				|| { [[ -z "${hwchipid}" && "${CHIPID}" == "0x26" && "${rcm_boot}" -eq 0 ]]; }; then
				get_board_version board_id board_FAB board_sku board_revision emcfuse_bin \
					board_ramcode platform_id boot_device;
				_nvbrd_trk=1;
				BOARDID="${board_id}";
				BOARDSKU="${board_sku}";
				FAB="${board_FAB}";
				BOARDREV="${board_revision}";
				RAMCODE_ID="${board_ramcode}"
			fi;
		fi;
		process_board_version "${board_id}" "${board_FAB}" "${board_sku}" "${board_revision}" "${hwchiprev}" "${BL_DIR}/cvm.bin" "${platform_id}" "${boot_device}";
	fi;
fi;

# Update the "RAMCODE" based on RAMCODE_ID that is set either as env variable or
# set by ramcode read from board.
# The "RAMCODE" later is passed to tegralflash.py.
if [ "${RAMCODE_ID}" != "" ]; then
	RAMCODE="${RAMCODE_ID}";
else
	# RAMCODE_ID is empty:
	# For non-BUP generation, if RAMCODE (should be specified in board config file) is also empty,
	# terminate the program.
	# Note: Apply this RAMCODE check only for t264 for now.
	if [ ${bup_blob} -eq 0 ] && [ "${CHIPID}" = "0x26" ] && [ "${RAMCODE}" == "" ]; then
		echo "ERROR: Neither RAMCODE_ID nor RAMCODE is specified."
		echo "       Program terminates."
		echo ""
		echo "Note: RAMCODE_ID is specified in command line."
		echo "      RAMCODE is specified in board config file."
		exit 1
	fi
fi;

ramcode_file="${BL_DIR}/ramcode.txt"
echo "${RAMCODE}" > "$ramcode_file"
echo "RAMCODE is ${RAMCODE}"

if declare -F -f process_chip_sku_version > /dev/null 2>&1; then
	chip_SKU="${CHIP_SKU}";
	chip_minor_revision_ID="${CHIP_MINOR}";
	bootrom_revision_ID="${BOOTROM_ID}";
	if [ "${FORCE_ACCESS}" != "" ] || [ "${chip_SKU}" == "" ]; then
		if [ "${hwchipid}" != "" ]; then
			get_chip_info_details chip_SKU chip_minor_revision_ID bootrom_revision_ID;
			CHIP_SKU="${chip_SKU}";
			CHIP_MINOR="${chip_minor_revision_ID}";
			BOOTROM_ID="${bootrom_revision_ID}";
		fi
	fi;
	process_chip_sku_version "${chip_SKU}" "${chip_minor_revision_ID}" "${bootrom_revision_ID}" "${RAMCODE}" "${fuselevel}" "${board_FAB}";
fi;

if [ ${read_info} == "True" ]; then
	if [ ${no_flash} -eq 1 ]; then
		echo "*** no-flash flag enabled. Exiting now... *** ";
		echo "";
		echo "To read board info, put the target in recovery mode."
		echo "cd bootloader";
		echo "sudo bash readinfocmd.txt"
		echo "";
		exit 0;
	fi;
	echo "ECID is ${ECID}"
	echo "";
	echo "==== Fuse Info (${BL_DIR}/${emcfusebin}) ====";
	cat "${BL_DIR}"/"${emcfusebin}";
	echo "";
	echo "==== EEPROM content (${BL_DIR}/cvm.bin) ====";
	xxd -g 1 "${BL_DIR}/cvm.bin";
	echo "";
	exit 0;
else
	{
		echo "ECID=${ECID}"
		echo "FUSELEVEL=${fuselevel}"
		echo "BOARDID=${board_id}"
		echo "BOARDSKU=${board_sku}"
		echo "FAB=${board_FAB}"
		echo "BOARDREV=${board_revision}"
		echo "RAMCODE_ID=${board_ramcode}"
		echo "CHIP_SKU=${chip_SKU}"
		echo "CHIP_MINOR=${chip_minor_revision_ID}"
		echo "BOOTROM_ID=${bootrom_revision_ID}"
	} > "${BL_DIR}/ecid.bin"
fi

# This variable is defined in the board config file
emcfuse_value="${EMCFUSE_VALUE}"
if declare -F -f update_flash_args > /dev/null 2>&1; then
	# this variable is defined in get_board_version function
	update_flash_args "${emcfuse_bin}" "${emcfuse_value}"
fi

if declare -F -f update_emmc_cfg_for_bup > /dev/null 2>&1; then
	update_emmc_cfg_for_bup "${bup_type}"
fi

# convert fuselevel to digit string
if [ "${fuselevel}" == "fuselevel_nofuse" ]; then
	fuselevel_s="0";
else
	fuselevel_s="1";
fi;

# Set board spec: BOARD_ID-FAB-BOARDSKU-BOARDREV-NV_PRODUCTION-CHIP_REV-BOARD_NAME-
spec="${BOARDID}-${FAB}-${BOARDSKU}-${BOARDREV}-${fuselevel_s}-${hwchiprev}-${ext_target_board}-";
# Make sure spec length is less than maximum supported by BUP (128)
MAX_SPEC_LEN=128

if ((${#spec} > ${MAX_SPEC_LEN})); then
	echo "Error: spec length exceeds ${MAX_SPEC_LEN}, ${spec}(len=${#spec})"
	exit 1
fi;

# get board SN and Part
if [ ${_nvbrd_trk} -ne 0 ]; then
	timeout 10s cat <(boardinfo_trk);
fi;

###########################################################################
# System default values: should be defined AFTER target_board value.
#
ROOTFS_TYPE="${ROOTFS_TYPE:-ext4}";
DEVSECTSIZE="${DEVSECTSIZE:-512}";		# default sector size = 512
BOOTPARTLIMIT="${BOOTPARTLIMIT:-10485760}";	# 1MiB limit
ACR_TYPE="${ACR_TYPE:-acr-debug}";		# default is acr-debug
fillpat="${FSFILLPATTERN:-0}";			# no cmdline: default=0
no_rootfs="${NO_ROOTFS:-0}" 			# default is 0
no_recovery_img="${NO_RECOVERY_IMG:-0}"
no_esp_img="${NO_ESP_IMG:-0}"
no_pva="${NO_PVA:-0}"
user_sector_size=""
SECTOR_SIZE_512=512
SECTOR_SIZE_4K=4096
ext_num_sectors=""
int_num_sectors=""
boardid="${BOARDID}";
if [ "${tegraid}" = "" ]; then
	tegraid="${CHIPID}";
fi;

if [ -z "${DFLT_KERNEL}" ]; then
	DFLT_KERNEL=${KERNEL_DIR}/Image;
else
	basekernel=$(basename "${DFLT_KERNEL}");
	if [ "${DFLT_KERNEL}" = "${basekernel}" ]; then
		DFLT_KERNEL="${KERNEL_DIR}/${DFLT_KERNEL}";
	fi;
fi;
if [ -z "${DFLT_KERNEL_FS}" ]; then
	DFLT_KERNEL_FS=${DFLT_KERNEL};
fi;
if [ -z "${DFLT_KERNEL_IMAGE}" ]; then
	DFLT_KERNEL_IMAGE=${DFLT_KERNEL};
fi;

# as ODMDATA can be overwritten by process_board_version, overwrite with user specified option here.
if [ -n "${_ODMDATA}" ]; then
	# used by setval function below
	ODMDATA=${_ODMDATA};
fi

# as BPFFILE can be overwritten by prcoess_chip_sku_version, overwrite with user specified option here.
if [ -n "${_BPFFILE}" ]; then
	# used by setval function below
	BPFFILE=${_BPFFILE};
fi

# as TBCDTB_FILE can be overwritten by prcoess_chip_sku_version, overwrite with user specified option here.
if [ -n "${_TBCDTB_FILE}" ]; then
	# used by setval function below
	TBCDTB_FILE=${_TBCDTB_FILE};
fi


###########################################################################
# System mandatory vars:
#
setval     rootfs_type	ROOTFS_TYPE;
setval     devsectsize	DEVSECTSIZE;
getsize    rootfssize	ROOTFSSIZE;	# .conf mandatory
getsize    recrootfssize RECROOTFSSIZE;
mkfilepath flashapp	"${FLASHAPP}"	"${BL_DIR}/tegraflash.py";
mkfilepath bootloader	"${BOOTLOADER}"	"${BL_DIR}/cboot.bin";
mkdirpath  rootfs_dir	"${ROOTFS_DIR}"	"${LDK_DIR}/rootfs";
mkfilepath kernel_image	"$KERNEL_IMAGE" "${DFLT_KERNEL_IMAGE}";
mkfilepath kernel_fs	"$KERNEL_IMAGE" "${DFLT_KERNEL_FS}";
mkfilepath bctfile	"${BCTFILE}"	"${TARGET_DIR}/BCT/${EMC_BCT}";
if [ "${CHIPID}" = "0x26" ]; then
	mkfilepath coldbootbctcfg   "${COLDBOOT_BCT_CFG}"   "";
	mkfilepath rcmbootbctcfg    "${RCMBOOT_BCT_CFG}"    "";
	mkfilepath coldbootptlayout "${COLDBOOT_PT_LAYOUT}" "";
	mkfilepath rcmbootptlayout  "${RCMBOOT_PT_LAYOUT}"  "";
fi
if [ ${rcm_boot} -ne 0 ]; then
	CFGFILE=${RCMBOOT_PT_LAYOUT:-"${CFGFILE}"}
elif [ ${bup_blob} -ne 0 ] || [ ${to_sign} -ne 0 ]; then
	CFGFILE=${CFGFILE:-"${COLDBOOT_PT_LAYOUT}"}
fi

mkfilepath cfgfile	"${CFGFILE}"	"${TARGET_DIR}/cfg/${EMMC_CFG}";

get_user_sector_size

set_user_device_num_sectors

if [[ "${bup_blob}" -eq 0 && "${rcm_boot}" -eq 0 && "${qspi_only}" -eq 0 ]]; then
    check_device_mismatch "${cfgfile}"
fi
if [ "${NO_KERNEL_DTB}" = "1" ]; then
	mkfilepath dtbfile	"${DTBFILE}"	"${BL_DIR}/${DTB_FILE}";
else
	mkfilepath dtbfile      "${DTBFILE}"    "${DTB_DIR}/${DTB_FILE}";
fi;

odmdata=${ODMDATA}
mkfilesoft kernelinitrd	"${INITRD}"	"${BL_DIR}/l4t_initrd.img";
mkfilesoft wb0boot	"${WB0BOOT}"	"${TARGET_DIR}/nvtbootwb0.bin";
mkfilesoft mb2_rf	"${MB2_RF}"	"";
mkfilesoft psc_rf	"${PSC_RF}"	"";
mkfilesoft cpu_bootloader	"${BOOTLOADER}"	"";
mkfilesoft mtspreboot	"${MTSPREBOOT}"	"${BL_DIR}/mts_preboot_si";
mkfilesoft ist_ucode	"${IST_UCODE}"	"";
mkfilesoft ist_bpmpfw	"${IST_BPMPFW}"	"";
mkfilesoft ist_config	"${IST_ICTBIN}"	"";
mkfilesoft ist_testimg	"${IST_TESTIMG}"	"";
mkfilesoft ist_rtinfo	"${IST_RTINFO}"	"";
mkfilesoft ist_rtid	"${IST_RTID}"	"";
mkfilesoft ist_rdram	"${IST_RDRAM}"	"";
mkfilesoft mcepreboot	"${MTS_MCE}"	"${BL_DIR}/${MTS_MCE}";
mkfilesoft mtsproper	"${MTSPROPER}"	"${BL_DIR}/${MTSPROPER}";
mkfilesoft mts		"${MTS}"	"${BL_DIR}/mts_si";
mkfilesoft mb1file	"${MB1FILE}"	"${BL_DIR}/mb1_prod.bin";
if [ "${BPFFILE}" != "" -a \
	"${BPFBASEFILE}" != "" -a "${BPFBASEDTBFILE}" != "" -a \
	-f "${BPFBASEFILE}" -a -f "${BPFBASEDTBFILE}" ]; then
	cat "${BPFBASEFILE}" "${BPFBASEDTBFILE}" > "${BPFFILE}";
fi;
mkfilesoft bpffile	"${FORCEBPFFILE:-"${BPFFILE}"}"	"${BL_DIR}/bpmp.bin";
mkfilesoft fsifwfile	"${FSIFWFILE}"	"";
mkfilesoft bpfdtbfile	"${BPFDTBFILE}" "${TARGET_DIR}/${BPFDTB_FILE}";
mkfilesoft clkdtsofile	"${CLKDTSOFILE}" "${TARGET_DIR}/${CLKDTSO_FILE}";
if [ "${bpfdtbfile}" = "" -a "${BPMPDTB_FILE}" != "" ]; then		#COMPAT
	mkfilesoft bpfdtbfile	"${BL_DIR}/${BPMPDTB_FILE}"	"";	#COMPAT
fi;									#COMPAT
mkfilesoft nctfile	"${NCTFILE}"	"${TARGET_DIR}/cfg/${NCT_FILE}";
mkfilesoft tosfile	"${TOSFILE}"	"${TARGET_DIR}/tos.img";
mkfilesoft eksfile	"${EKSFILE}"	"${TARGET_DIR}/eks.img";
mkfilesoft fbfile	"${FBFILE}"	"${BL_DIR}/${FBFILE}";
mkfilesoft bcffile	"${BCFFILE}"	"";
mkfilesoft sosfile	"${SOSFILE}"	"";
mkfilesoft mb2blfile	"${MB2BLFILE}"	"";
mkfilesoft dcefile	"${DCE}"	"";
mkfilesoft xusbfile	"${XUSBFILE}"	"";
mkfilesoft pvafile	"${PVAFILE}"	"";
mkfilesoft nvdecfile	"${NVHOSTNVDEC}"	"";
mkfilesoft custinfofile	"${DFLT_CUSTOMER_INFO}"	"${BL_DIR}/${custinfofilename}";
mkfilesoft camerafw	"${CAMERAFW}"	"";
mkfilesoft apefile	"${APEFILE}"	"${BL_DIR}/adsp-fw.bin";
mkfilesoft aonadspfw	"${AON_ADSPFW}"	"";
mkfilesoft adsp0fw	"${ADSP0FW}"	"";
mkfilesoft adsp1fw	"${ADSP1FW}"	"";
mkfilesoft igbfw	"${IGBFW}"	"";
mkfilesoft atffw	"${ATFFW}"	"";
mkfilesoft hafniumfwfile "${HAFNIUM_FW}"	"";
mkfilesoft rce1fwfile	"${RCE1FW}"	"";
mkfilesoft spefile	"${SPEFILE}"	"${BL_DIR}/spe.bin";
mkfilesoft drameccfile  "${DRAMECCFILE}" "";

function update_badpage_binary() {
	echo -e "$1" | dd of="${BL_DIR}/badpage.bin" bs=1 seek="$2" count="$3" conv=notrunc &> /dev/null
}

function badpage_bch_update() {
	update_badpage_binary 'NVDA' 0 4
	update_badpage_binary '\x01' 4064 1
	update_badpage_binary 'BINF' 5120 4
}

if [ "${BADPAGEFILE}" != "" ]; then
	if [ ! -f "${BL_DIR}/badpage.bin" ]; then
		echo "creating dummy ${BL_DIR}/badpage.bin"
		dd if=/dev/zero of="${BL_DIR}/badpage.bin" bs=512 count=16;
	else
		echo "reusing existing ${BL_DIR}/badpage.bin"
		# Clear BCH Header
		dd if=/dev/zero of="${BL_DIR}/badpage.bin" bs=512 seek=0 count=16;
	fi;
	badpage_bch_update;
	mkfilesoft badpagefile	"${BADPAGEFILE}"	"${BL_DIR}/badpage.bin";
fi;

mkfilesoft uphy_config    "${TARGET_DIR}/BCT/${UPHY_CONFIG}" "";
mkfilesoft device_config  "${TARGET_DIR}/BCT/${DEVICE_CONFIG}" "";
mkfilesoft misc_cold_boot_config    "${TARGET_DIR}/BCT/${MISC_COLD_BOOT_CONFIG}" "";
mkfilesoft misc_config    "${TARGET_DIR}/BCT/${MISC_CONFIG}" "";
mkfilesoft mb2bct_config  "${TARGET_DIR}/BCT/${MB2BCT_CONFIG}" "";
mkfilesoft pinmux_config  "${TARGET_DIR}/BCT/${PINMUX_CONFIG}" "";
mkfilesoft gpioint_config  "${TARGET_DIR}/BCT/${GPIOINT_CONFIG}" "";
mkfilesoft pmic_config    "${TARGET_DIR}/BCT/${PMIC_CONFIG}" "";
mkfilesoft pmc_config     "${TARGET_DIR}/BCT/${PMC_CONFIG}" "";
mkfilesoft deviceprod_config    "${TARGET_DIR}/BCT/${DEVICEPROD_CONFIG}" "";
mkfilesoft prod_config    "${TARGET_DIR}/BCT/${PROD_CONFIG}" "";
mkfilesoft scr_config     "${TARGET_DIR}/BCT/${SCR_CONFIG}" "";
mkfilesoft scr_config_dts     "${TARGET_DIR}/BCT/${FIREWALL_CONFIG_DTSI}" "";
mkfilesoft scr_cold_boot_config     "${TARGET_DIR}/BCT/${SCR_COLD_BOOT_CONFIG}" "";
mkfilesoft bpmp_mem_config           "${TARGET_DIR}/BCT/${BPMP_MEM_CONFIG}"         "";
mkfilesoft uphylane_config "${BL_DIR}/${UPHYLANE_CONFIG}" ""
# Support up to four boot chains A, B, C and D.
mkfilesoft dev_params     "${TARGET_DIR}/BCT/${DEV_PARAMS}" "";
mkfilesoft dev_params_b   "${TARGET_DIR}/BCT/${DEV_PARAMS_B}" "";
mkfilesoft dev_params_c     "${TARGET_DIR}/BCT/${DEV_PARAMS_C}" "";
mkfilesoft dev_params_d   "${TARGET_DIR}/BCT/${DEV_PARAMS_D}" "";
mkfilesoft bootrom_config "${TARGET_DIR}/BCT/${BOOTROM_CONFIG}" "";
mkfilesoft soft_fuses     "${TARGET_DIR}/BCT/${SOFT_FUSES}" "";
mkfilesoft mb2bct_cfg        "${TARGET_DIR}/BCT/${MB2_BCT}" "";
mkfilesoft wb0sdram	"${TARGET_DIR}/BCT/${WB0SDRAM_BCT}"	"";
mkfilesoft tbcfile	"${TBCFILE}"	 "";
mkfilesoft rcm_tbcfile	"${RCM_TBCFILE}"	 "";
mkfilesoft nvkeyfile    "${NVKEY_FILE}"	 "";
mkfilesoft nvencryptkeyfile "${NVENCRYPTKEY_FILE}"	 "";
mkfilesoft pscfwfile    "${PSCFW}"	 "";
mkfilesoft mtsmcefile    "${MTSMCEFILE}"	 "";
mkfilesoft pscbl1file   "${PSCBL1FILE}"	 "";
mkfilesoft tscfwfile    "${TSECFW}"	 "";
mkfilesoft hpsebl1file "${HPSEBL1FILE}"      "";
mkfilesoft hpsefwfile  "${HPSEFW}"      "";
mkfilesoft hpserawfile "${HPSERAW}"      "";
mkfilesoft sbbl1file   "${SBBL1FILE}"      "";
mkfilesoft sbfwfile    "${SBFW}"      "";
mkfilesoft sbrawfile   "${SBRAW}"      "";
mkfilesoft bct_flags_file     "${TARGET_DIR}/bctflags/${BCT_FLAGS_FILE}" "";
mkfilesoft tbcdtbfile	"${TBCDTB_FILE}" "${DTB_DIR}/${TBCDTB_FILE}";


if [ "${target_partname}" != "" ]; then
	if [ "$(xmlstarlet sel -t -v "//device[partition/@name='${target_partname}']/@type" "${localcfgfile}")" = "spi" ]; then
		qspi_only=1
	fi
	if [[ "${target_partname}" != APP* ]]; then
		no_systemimg=1
	fi
	if [[ "${target_partname}" != esp* ]]; then
		no_esp_img=1
	fi
fi

if [[ ("${target_partname}" == "" || "${target_partname}" == "recovery-dtb" || -n "${with_systemimg}" ) \
	&& "${bup_blob}" -eq 0 ]]; then
	if [ -f "${DTB_DIR}/${DTB_FILE}" ]; then
		echo "Copy "${DTB_DIR}/${DTB_FILE} to "${DTB_DIR}/${DTB_FILE}.rec"
		cp "${DTB_DIR}/${DTB_FILE}" "${DTB_DIR}/${DTB_FILE}.rec"
		# RECDTB_FILE is defined in board configuration file
		mkfilepath recdtbfile   "${RECDTB_FILE}" "${DTB_DIR}/${DTB_FILE}.rec";
	fi
fi

# Parse attribute of partition encryption status.
partition_app_enc_enable="false"
partition_app_enc_b_enable="false"
partition_app_enc_reencrypt="false"
partition_app_enc_b_reencrypt="false"
app_partition_exist="false"

if partition_exists_in_PT_table "APP" "${cfgfile}"; then
	app_partition_exist="true"
	if [ "${disk_enc_enable}" -eq 1 ]; then
		parse_partition_enc_attr;
	fi
fi

if [ "${rootdev_type}" = "network" ]; then
	if [ "${NFSROOT}" = "" -a "${NFSARGS}" = "" ]; then
		echo "Error: network argument(s) missing.";
		usage allknown 1;
	fi;
	if [ "${NFSROOT}" != "" ]; then
		validateNFSroot nfsroot "${NFSROOT}";
	fi;
	if [ "${NFSARGS}" != "" ]; then
		validateNFSargs nfsargs "${NFSARGS}";
	fi;
	if [ "${nfsroot}" != "" ]; then
		nfsdargs="root=/dev/nfs rw netdevwait";
		cmdline+="${nfsdargs} ";
		if [ "${nfsargs}" != "" ]; then
			nfsiargs="ip=${nfsargs}";
			nfsiargs+="::${target_rootdev}:off";
		else
			nfsiargs="ip=:::::${target_rootdev}:on";
		fi;
		cmdline+="${nfsiargs} ";
		cmdline+="nfsroot=${nfsroot} ";
	fi;
elif [ "${target_rootdev}" == "internal" ] || \
     [ "${target_rootdev}" == "external" ] || \
     [[ "${rootfs_ab}" == 1 ]]; then
	# For 'internal' and 'external' target root devices,
	# or enabled ROOTFS_AB=1, always use the UUID stored in the file
	# ${rootfsuuidfile} or ${rootfsuuidfile}_b if present.
	# If this file is not present, then try to generate one.
	_tmp_uuid="";

	if [ "${target_rootdev}" == "external" ] || \
	[ "${external_device}" -eq 1 ]; then
		rootuuid_restore "_ext"
		_tmp_uuid="${rootfsuuid_ext}";
	else
		rootuuid_restore ""
		_tmp_uuid="${rootfsuuid}";
	fi

	if [ ${disk_enc_enable} -eq 1 ];  then
		if [[ "${partition_app_enc_enable}" == "true" ]]; then
			# The encrypted fs UUID of the rootdev.
			if [ "${external_device}" -eq 1 ]; then
				rootuuid_restore "_ext_enc"
				_tmp_uuid="${rootfsuuid_ext_enc}";

				bootpartuuid_restore "_ext"
				rootfsuuid_enc="${rootfsuuid_ext_enc}"
				rootfsuuid_b_enc="${rootfsuuid_b_ext_enc}"
				# These variables are set in disk_encryption_helper.func
				bootpartuuid="${bootpartuuid_ext}"
				# These variables are set in disk_encryption_helper.func
				bootpartuuid_b="${bootpartuuid_b_ext}"
			else
				rootuuid_restore "_enc";
				_tmp_uuid="${rootfsuuid_enc}";
				bootpartuuid_restore;
			fi
			cmdline+="root=UUID=${_tmp_uuid} rw rootwait rootfstype=ext4 "
		else
			if [ "${external_device}" -eq 1 ]; then
				bootpartuuid_restore "_ext"
				# These variables are set in disk_encryption_helper.func
				bootpartuuid="${bootpartuuid_ext}"
				# These variables are set in disk_encryption_helper.func
				bootpartuuid_b="${bootpartuuid_b_ext}"
			else
				bootpartuuid_restore;
			fi
			cmdline+="root=PARTUUID=${_tmp_uuid} rw rootwait rootfstype=ext4 "
		fi;
	else
		cmdline+="root=PARTUUID=${_tmp_uuid} rw rootwait rootfstype=ext4 "
	fi;
else
	cmdline+="root=/dev/${target_rootdev} rw rootwait rootfstype=ext4 "
fi;

if [ "${CMDLINE_ADD}" != "" ]; then
	cmdline+="${CMDLINE_ADD} ";
fi;

if [ "${CMDLINE}" != "" ]; then
	for string in ${CMDLINE}; do
		lcl_str=$(echo $string | sed "s|\(.*\)=.*|\1|")
		if [[ "${cmdline}" =~ $lcl_str ]]; then
			cmdline=$(echo "$cmdline" | sed "s|$lcl_str=[0-9a-zA-Z:/,]*||g")
		fi
		cmdline+="${string} ";
	done
fi;

##########################################################################
if [ "${zflag}" == "true" ]; then
	pr_conf;
	exit 0;
fi;
##########################################################################

pushd "$BL_DIR" > /dev/null 2>&1;

# BCT_CFG_FILE
localbctcfg="bct_cfg.xml"
localrcmbootbctcfg="rcmboot_bct_cfg.xml"

### Localize files and build TAGS ########################################
# BCT_TAG:::
#
cp2local bctfile "${BL_DIR}/${bctfilename}";
if [ "${CHIPID}" = "0x26" ]; then
	# Build up BCT parameters:
	replace_value_in_bct_cfg

	if [ "${nvkeyfile}" != "" ]; then
		cp2local nvkeyfile "${BL_DIR}/${nvkeyfilename}";
		BCTARGS+="--nv_key ${nvkeyfilename} ";
	fi;
	if [ "${nvencryptkeyfile}" != "" ]; then
		cp2local nvencryptkeyfile "${BL_DIR}/${nvencryptkeyfilename}";
		BCTARGS+="--nvencrypt_key ${nvencryptkeyfilename} ";
	fi;
	if [ "${pscfwfile}" != "" ]; then
		cp2local pscfwfile "${BL_DIR}/${pscfwfilename}";
	fi;
	if [ "${pscbl1file}" != "" ]; then
		cp2local pscbl1file "${BL_DIR}/${pscbl1filename}";
	fi;
	if [ "${hpsebl1file}" != "" ]; then
		cp2local hpsebl1file "${BL_DIR}/${hpsebl1filename}";
	fi;
	if [ "${sbbl1file}" != "" ]; then
		cp2local sbbl1file "${BL_DIR}/${sbbl1filename}";
	fi;
	if [ "${hpsefwfile}" != "" ]; then
		cp2local hpsefwfile "${BL_DIR}/${hpsefwfilename}";
        fi;
	if [ "${sbfwfile}" != "" ]; then
		cp2local sbfwfile "${BL_DIR}/${sbfwfilename}";
	fi;
	if [ "${hpserawfile}" != "" ]; then
		cp2local hpserawfile "${BL_DIR}/${hpserawfilename}";
	fi;
	if [ "${sbrawfile}" != "" ]; then
		cp2local sbrawfile "${BL_DIR}/${sbrawfilename}";
	fi;

	if [ "${mtsmcefile}" != "" ]; then
		cp2local mtsmcefile "${BL_DIR}/${mtsmcefilename}";
	fi;
	if [ "${tscfwfile}" != "" ]; then
		cp2local tscfwfile "${BL_DIR}/${tscfwfilename}";
	fi;
	if [ "${hafniumfwfile}" != "" ]; then
		cp2local hafniumfwfile "${BL_DIR}/${hafniumfwfilename}";
	fi;
	if [ "${rce1fwfile}" != "" ]; then
		cp2local rce1fwfile "${BL_DIR}/${rce1fwfilename}";
	fi;
	if [ "${mb2applet}" != "" ]; then
		cp2local mb2applet "${BL_DIR}/${mb2appletname}";
	fi;
	if [ "${bct_flags_file}" != "" ]; then
		cp2local bct_flags_file "${BL_DIR}/${bct_flags_filename}";
		BCTARGS+="--bct_flags_file ${bct_flags_filename} ";
	fi;
	generate_overlay_pcp "platform_config_profile_overlay.yaml"

elif [ "${BINSARGS}" != "" ]; then
	# Build up BCT parameters:

	if [ "${uphy_config}" != "" ]; then
		cp2local uphy_config "${BL_DIR}/${uphy_configname}";
		BCTARGS+="--uphy_config ${uphy_configname} ";
	fi;
	if [ "${minratchet_config}" != "" ]; then
		cp2local minratchet_config "${BL_DIR}/${minratchet_configname}";
		BCTARGS+="--minratchet_config ${minratchet_configname} ";
	fi;
	if [ "${device_config}" != "" ]; then
		cp2local device_config "${BL_DIR}/${device_configname}";
		BCTARGS+="--device_config ${device_configname} ";
	fi;
	if [ "${misc_cold_boot_config}" != "" ]; then
		cp2local misc_cold_boot_config "${BL_DIR}/${misc_cold_boot_configname}";
		BCTARGS+="--misc_cold_boot_config ${misc_cold_boot_configname} ";
	fi;
	if [ "${misc_config}" != "" ]; then
		cp2local misc_config "${BL_DIR}/${misc_configname}";
		BCTARGS+="--misc_config ${misc_configname} ";
	fi;
	if [ "${mb2bct_config}" != "" ]; then
		cp2local mb2bct_config "${BL_DIR}/${mb2bct_configname}";
	fi;
	if [ "${pinmux_config}" != "" ]; then
		cp2local pinmux_config "${BL_DIR}/${pinmux_configname}";
		BCTARGS+="--pinmux_config ${pinmux_configname} ";
	fi;
	if [ "${gpioint_config}" != "" ]; then
		cp2local gpioint_config "${BL_DIR}/${gpioint_configname}";
		BCTARGS+="--gpioint_config ${gpioint_configname} ";
	fi;
	if [ "${pmic_config}" != "" ]; then
		cp2local pmic_config "${BL_DIR}/${pmic_configname}";
		BCTARGS+="--pmic_config ${pmic_configname} ";
	fi;
	if [ "${pmc_config}" != "" ]; then
		cp2local pmc_config "${BL_DIR}/${pmc_configname}";
		BCTARGS+="--pmc_config ${pmc_configname} ";
	fi;
	if [ "${deviceprod_config}" != "" ]; then
		cp2local deviceprod_config "${BL_DIR}/${deviceprod_configname}";
		BCTARGS+="--deviceprod_config ${deviceprod_configname} ";
	fi;
	if [ "${prod_config}" != "" ]; then
		cp2local prod_config "${BL_DIR}/${prod_configname}";
		BCTARGS+="--prod_config ${prod_configname} ";
	fi;
	if [ "${scr_config}" != "" ]; then
		cp2local scr_config "${BL_DIR}/${scr_configname}";
		BCTARGS+="--scr_config ${scr_configname} ";
	fi;
	if [ "${wb0sdram}" != "" ]; then
		cp2local wb0sdram  "${BL_DIR}/${wb0sdramname}";
		BCTARGS+="--wb0sdram_config ${wb0sdramname} ";
	fi;
	if [ "${scr_config_dts}" != "" ]; then
		cp2local scr_config_dts "${BL_DIR}/${scr_config_dtsname}";
	fi;
	if [ "${scr_cold_boot_config}" != "" ]; then
		cp2local scr_cold_boot_config "${BL_DIR}/${scr_cold_boot_configname}";
		BCTARGS+="--scr_cold_boot_config ${scr_cold_boot_configname} ";
	fi;
	if [ "${bootrom_config}" != "" ]; then
		cp2local bootrom_config "${BL_DIR}/${bootrom_configname}";
		BCTARGS+="--br_cmd_config ${bootrom_configname} ";
	fi;
	if [ "${uphylane_config}" != "" ]; then
		cp2local uphylane_config "${BL_DIR}/${uphylane_configname}";
		BCTARGS+="--uphy ${uphylane_configname} ";
	fi
	# Process dev params for multiple boot chains
	process_dev_params
	if [ "${mb2bct_cfg}" != "" ]; then
		cp2local mb2bct_cfg "${BL_DIR}/${mb2bct_cfgname}";
		if [ -n "${pv_pub_key_hash}" ]; then
			mb2bct_cfg_with_pkh="$(basename "${mb2bct_cfgname}" .dts)_with_pkh.dts"
			cat > "${mb2bct_cfg_with_pkh}" <<EOF
/dts-v1/;
#include "${mb2bct_cfgname}"
/ { mb2-misc { cpubl_auth_key_delegation_enable = <1>; cpubl_auth_key_pcp_hash = <${pv_pub_key_hash}>; cpubl_enc_key = <${pv_enc_content}>;}; };
EOF
			mb2bct_cfgname="${mb2bct_cfg_with_pkh}"
		fi
		BCTARGS+="--mb2bct_cfg ${mb2bct_cfgname} ";

	fi;
	if [ "${nvkeyfile}" != "" ]; then
		cp2local nvkeyfile "${BL_DIR}/${nvkeyfilename}";
		BCTARGS+="--nv_key ${nvkeyfilename} ";
	fi;
	if [ "${nvencryptkeyfile}" != "" ]; then
		cp2local nvencryptkeyfile "${BL_DIR}/${nvencryptkeyfilename}";
		BCTARGS+="--nvencrypt_key ${nvencryptkeyfilename} ";
	fi;
	if [ "${pscfwfile}" != "" ]; then
		cp2local pscfwfile "${BL_DIR}/${pscfwfilename}";
		if [ "${BINSARGS}" != "" ]; then
			BINSARGS+="psc_fw ${pscfwfilename}; ";
		fi
	fi;
	if [ "${pscbl1file}" != "" ]; then
		cp2local pscbl1file "${BL_DIR}/${pscbl1filename}";
	fi;
	if [ "${mtsmcefile}" != "" ]; then
		cp2local mtsmcefile "${BL_DIR}/${mtsmcefilename}";
		if [ "${BINSARGS}" != "" ]; then
			BINSARGS+="mts_mce ${mtsmcefilename}; ";
		fi
	fi;
	if [ "${tscfwfile}" != "" ]; then
		cp2local tscfwfile "${BL_DIR}/${tscfwfilename}";
		if [ "${BINSARGS}" != "" ]; then
			BINSARGS+="tsec_fw ${tscfwfilename}; ";
		fi
	fi;
	if [ "${mb2applet}" != "" ]; then
		cp2local mb2applet "${BL_DIR}/${mb2appletname}";
		if [ "${BINSARGS}" != "" ]; then
			BINSARGS+="mb2_applet ${mb2appletname}; ";
		fi
	fi;
	if [ "${BCT}" = "" ]; then
		BCT="--sdram_config";
	fi;
elif [ "${BCT}" = "" ]; then
	BCT="--bct";
fi;

# check cases where system.img is not needed
if [ ${bup_blob} -eq 1 ] || [ ${rcm_boot} -eq 1 ] \
	|| [ "${no_rootfs}" -eq 1 ] \
	|| [ "${no_systemimg}" -eq 1 ] \
	|| [ "${read_part_name}" != "" ] || [ "${qspi_only}" == 1 ]; then
	skip_systemimg="true";
fi;

if [ -n "${with_systemimg}" ]; then
	skip_systemimg="";
fi

# APP partition does not exist
# If ${gen_systemimg_only} is set, no need to check if APP partitions exists
if [ ${gen_systemimg_only} -eq 0  ]; then
    if ! partition_exists_in_PT_table "APP" "${cfgfile}"; then
        skip_systemimg="true";
    fi;
fi

# UDA_TAG:
#
# Create the UDA encrypted disk image if the attribuate "encrypted" is true.
if [ ${disk_enc_enable} -eq 1 ]; then
	uda_image="user_data_encrypted.img";
	uda_dir="";
	if [ "${external_device}" -eq 1 ]; then
		udaext="_ext";
		uda_image="${uda_image}${udaext}";
	fi
	if [ "${reuse_systemimg}" = "true" ] || [ "${skip_systemimg}" = "true" ]; then
		echo "Skip generating encrypted UDA partition";
	else
		# Delete the UDA encrypted disk image if it exists
		if [ -f "${uda_image}" ]; then
			rm -f "${uda_image}"
		fi
		if [ "${UDADIR}" != "" ]; then
			uda_dir="${LDK_DIR}/${UDADIR}"
		fi
		create_enc_user_disk "UDA" "${cfgfile}" "${fillpat}" "${rootfs_type}" "${ECID}" "${uda_dir}" \
				     "${uda_image}" "${udaext}" "${gen_luks_password_kdf}";
	fi
fi;
if [ "${uda_image}" != "" ] && [ -f "${uda_image}" ]; then
	UDA_TAG+="-e s/UDA_FILE/${uda_image}/ ";
else
	UDA_TAG+="-e /UDA_FILE/d ";
fi;


# LNX_TAG:
#
localbootfile=boot.img;
rm -f initrd; touch initrd;
if [[ "${rootfs_ab}" == 1 && "${disk_enc_enable}" == 1 ]]; then
	rm -f initrd_b; touch initrd_b;
fi;
if [ ${rcm_boot} -eq 1 ]; then
	if [ "${kernelinitrd}" = "" ]; then
		kernelinitrd="l4t_initrd.img"
	fi;
fi;
do_initrd_update="true"
if [[ "${reuse_systemimg}" == "true" || ( "${skip_systemimg}" = "true" && "${rcm_boot}" -eq 0 ) ]]; then
	do_initrd_update="false"
fi

# Kernel image must be present in the rootfs before updating the initrd,
# as there are cases where the rootfs has no kernel image before the update
echo -e -n "\tpopulating kernel to rootfs... ";
mkdir -p "${rootfs_dir}/boot" > /dev/null 2>&1;
echo "Copying ${kernel_fs} to ${rootfs_dir}/boot"
cp -f "${kernel_fs}" "${rootfs_dir}/boot"; chkerr;

if [ "$kernelinitrd" != "" -a -f "$kernelinitrd" ]; then
	if [[ "${do_initrd_update}" == "true" ]]; then
		# Update the initrd in BSP and rootfs
		update_initrd_args=""
		if [ -n "${INITRD_LIST_FILES}" ]; then
			update_initrd_args="--list-files ${INITRD_LIST_FILES}"
		fi
		update_initrd_cmd="${LDK_DIR}/tools/l4t_update_initrd.sh ${update_initrd_args}"
		eval "${update_initrd_cmd}"
		chkerr;
	fi

	echo -n "copying initrd(${kernelinitrd})... ";
	cp -f "${kernelinitrd}" initrd;
	if [[ "${rootfs_ab}" == 1 && "${disk_enc_enable}" == 1 ]]; then
		cp -f "${kernelinitrd}" initrd_b;
	fi;
	chkerr;

	# Update initrd for LUKS disk encryption support
	if [ ${disk_enc_enable} -eq 1 ]; then
		# Prepare the needed binaries
		prepare_luks_bin_list "${LDK_DIR}" "${rootfs_dir}" luks_bin_list
		luks_bin_list+=("/sbin/cryptsetup" "/usr/sbin/nvluks-srv-app");

		# Prepare the initrd
		initrddir="${BL_DIR}";
		tempinitrd="${initrddir}/initrd";
		tempinitrddir="${initrddir}/temp";
		if [ ! -d "${tempinitrddir}" ]; then
			mkdir -p "${tempinitrddir}";
		fi;
		pushd "${tempinitrddir}" > /dev/null 2>&1;
		prepare_luks_initrd "${tempinitrd}" "${rootfs_dir}" \
					"${rootfsuuid_enc}" "${rootfsuuid_b_enc}" \
					"${partition_app_enc_enable}" "${partition_app_enc_reencrypt}"  \
					"${luks_bin_list[@]}"
		popd > /dev/null 2>&1;
		chkerr;

		if [[ "${rootfs_ab}" == 1 ]]; then
			rm -rf ${tempinitrddir}/*;
			tempinitrd="${initrddir}/initrd_b";
			pushd "${tempinitrddir}" > /dev/null 2>&1;
			prepare_luks_initrd "${tempinitrd}" "${rootfs_dir}" \
						"${rootfsuuid_b_enc}" "${rootfsuuid_enc}" \
						"${partition_app_enc_enable}" "${partition_app_enc_reencrypt}" \
						"${luks_bin_list[@]}"
			popd > /dev/null 2>&1;
			chkerr;
		fi;

		# Clean up
		rm -rf "${tempinitrddir}";
	fi;
fi;

# Fix the external storage layout using kernel/kernel-dtb partition
# Update them to A_kernel/B_kernel/A_kernel-dtb/B_kernel-dtb for t23x.
if [ "${CHIPID}" = "0x23" ]; then
	LNX_TAG+="-e s/name=\"kernel\"/name=\"A_kernel\"/ ";
	LNX_TAG+="-e s/name=\"kernel_b\"/name=\"B_kernel\"/ ";
else
	LNX_TAG+="-e s/LNXNAME/kernel/ ";
fi
LNX_TAG+="-e s/LNXSIZE/104857600/ ";
# Handle where kernel image is specified by -k and --image options
if [ "${write_image_name}" != "" ]; then
	if [ "${target_partname}" = "LNX" ] || [ "${target_partname}" = "kernel" ] \
		|| [ "${target_partname}" = "kernel_b" ]; then
		kernel_image="${write_image_name}";
		write_image_name="";
	fi
fi

if [ "${INITRD_IN_BOOTIMG}" = "yes" ]; then
	ramdisk=initrd;
else
	ramdisk="/dev/null"
fi

if [[ "${rootfs_ab}" == 1 ]]; then
	if [[ "${partition_app_enc_b_enable}" == "true" ]]; then
		if [ "${target_rootdev}" == "external" ] || \
			[ "${external_device}" -eq 1 ]; then
			cmdline_b="${cmdline//${rootfsuuid_ext_enc}/${rootfsuuid_b_ext_enc}}"
		else
			if [[ "${partition_app_enc_b_enable}" == "true" ]]; then
				cmdline_b="${cmdline//${rootfsuuid_enc}/${rootfsuuid_b_enc}}"
			else
				cmdline_b="${cmdline//${rootfsuuid}/${rootfsuuid_b}}"
			fi
		fi
	else
		if [ "${target_rootdev}" == "external" ] || \
			[ "${external_device}" -eq 1 ]; then
			cmdline_b="${cmdline//${rootfsuuid_ext}/${rootfsuuid_b_ext}}"
		else
			cmdline_b="${cmdline//${rootfsuuid}/${rootfsuuid_b}}"
		fi
	fi
fi

uefi_sign="False"

if [[ ${UEFIBL} =~ "uefi" ]] || [[ ${TBCFILE} =~ "uefi" ]]; then
	echo "bl is uefi"
	if [ "${UEFI_KEYS_CONF}" != "" ]; then
		echo "will sign uefi payloads"
		uefi_sign="True"
	fi;
fi

make_boot_image

# boot.img is ready:

# For T26x and T23x, generate encrypted/signed file of boot.img in a temp folder.
if [ "${tegraid}" = "0x23" ] || [ "${tegraid}" = "0x26" ]; then
	if [[ ${uefi_sign} == "False" ]]; then
		echo "Not signing of boot.img"
	else
		temp_user_dir="temp_user_dir";
		rm -rf "${temp_user_dir}" > /dev/null 2>&1;
		mkdir -p "${temp_user_dir}"; chkerr "failed to mkdir ${temp_user_dir}";
		cp ${localbootfile} ${temp_user_dir}/bootimg > /dev/null 2>&1;
		if [ -f "${localbootfile}_b" ]; then
			cp "${localbootfile}_b" "${temp_user_dir}"/bootimg_b > /dev/null 2>&1;
		fi
		pushd ${temp_user_dir} > /dev/null 2>&1 || exit 1;
		# Set "append" (to append .sig file)
		uefi_signimage "bootimg" "${uefi_db_key}" "${uefi_db_cert}" "append" "True";
		# Save a copy (.unsigned) before overwritten
		cp ../"${localbootfile}" ../"${localbootfile}".unsigned;
		cp "bootimg" ../"${localbootfile}"
		if [ -f "bootimg_b" ]; then
			uefi_signimage "bootimg_b" "${uefi_db_key}" "${uefi_db_cert}" "append" "True";
			cp ../"${localbootfile}_b" ../"${localbootfile}_b".unsigned;
			cp "bootimg_b" ../"${localbootfile}_b"
		fi
		popd > /dev/null 2>&1 || exit 1;
	fi
fi

LNX_TAG+="-e s/LNXFILE/${localbootfile}/ ";

# Generate an empty image filled by 0xff
function generate_empty_image() {
	local image="${1}";
	local image_size="${2}";
	local sector_size="${3}";
	local block_count=;
	local remaining_size=;

	block_count=$((image_size / sector_size));
	remaining_size=$((image_size % sector_size));

	if [ "${block_count}" -eq 0 ] || [ "${remaining_size}" -ne 0 ]; then
		echo "Error: Generate zero image failed, the image size is not a multiple of ${sector_size}";
		exit 1;
	fi

	if ! dd if=/dev/zero bs="${sector_size}" count="${block_count}" | tr '\000' '\377' > "${image}"; then
		echo "Error: fail to generate ${image} filled by \"0xff\""
		exit 1;
	fi
	return;
}

# Build recovery image and dtb
# recovery.img set to 100M, and leave 512KB for
# recovery-dtb and 512KB for kernel-bootctrl
REC_SIZE_DEF=104857600
RECROOTFS_SIZE_DEF=314572800
REC_TAG+="-e s/RECNAME/recovery/ ";
REC_TAG+="-e s/RECSIZE/${REC_SIZE_DEF}/ "
RECDTB_TAG+="-e s/RECDTB-NAME/recovery-dtb/ ";
BOOTCTRL_TAG+="-e s/BOOTCTRLNAME/kernel-bootctrl/ ";
if [ "${no_recovery_img}" -eq 0 ] \
	&& { [ "${target_partname}" == "" ] || [ "${target_partname}" == "recovery" ] || [ "${target_partname}" == "recovery-dtb" ] || [ -n "${with_systemimg}" ]; } \
	&& [ ${bup_blob} -eq 0 ]; then
	make_recovery_script="${LDK_DIR}/tools/ota_tools/version_upgrade/ota_make_recovery_img_dtb.sh"
	if [ -f "${make_recovery_script}" ];then
		source "${make_recovery_script}"

		if [ "${target_partname}" == "" ] || [ "${target_partname}" == "recovery" ] || [ -n "${with_systemimg}" ]; then
			localrecfile="recovery.img"
			if [[ ${uefi_sign} == "True" ]]; then
				# Sign kernel_fs before making recovery image
				cp "${kernel_fs}" "${kernel_fs}".save
				uefi_signimage "${kernel_fs}" "${uefi_db_key}" "${uefi_db_cert}" "nosplit" "False";
				ota_make_recovery_img "${LDK_DIR}" "${kernel_fs}" "${kernelinitrd}" "${localrecfile}" "${tegraid}" "${disk_enc_enable}"
				# Restore the unsigned kernel_fs
				mv "${kernel_fs}".save "${kernel_fs}"
				# Sign recovery.img
				cp "${localrecfile}" "${localrecfile}".unsigned
				uefi_signimage "${localrecfile}" "${uefi_db_key}" "${uefi_db_cert}" "append" "True";
			else
				# Create unsigned recovery.img
				ota_make_recovery_img "${LDK_DIR}" "${kernel_fs}" "${kernelinitrd}" "${localrecfile}" "${tegraid}" "${disk_enc_enable}"
			fi
			REC_TAG+="-e s/RECFILE/${localrecfile}/ ";
			if partition_exists_in_PT_table "RECNAME_alt" "${cfgfile}"; then
				# Generate the image for recovery_alt partition
				generate_empty_image "${localrecfile}.alt" "${REC_SIZE_DEF}" "${user_sector_size}"
				REC_TAG+="-e s/REC_ALT_FILE/${localrecfile}.alt/ ";
			fi
		else
			REC_TAG+="-e /RECFILE/d ";
			REC_TAG+="-e /REC_ALT_FILE/d ";
		fi

		if [ "${target_partname}" == "" ] || [ "${target_partname}" == "recovery-dtb" ] || [ -n "${with_systemimg}" ]; then
			if [ "${recdtbfile}" != "" ] && [ -f "${recdtbfile}" ];then
				ota_make_recovery_dtb "${LDK_DIR}" "${recdtbfilename}"
				# Sign ${recdtbfile}" if uefi_sign is true
				if [[ ${uefi_sign} == "True" ]]; then
					cp "${recdtbfilename}" "${recdtbfilename}".unsigned
					uefi_signimage "${recdtbfilename}" "${uefi_db_key}" "${uefi_db_cert}" "append" "True";
				fi
				RECDTB_TAG+="-e s/RECDTB-FILE/${recdtbfilename}/ ";#COMPAT
				RECDTB_TAG+="-e s/RECDTB_FILE/${recdtbfilename}/ ";
				if partition_exists_in_PT_table "RECDTB-NAME_alt" "${cfgfile}"; then
					# Get the size of the recovery-dtb_alt partition
					get_value_from_PT_table "RECDTB-NAME_alt" "size" "${cfgfile}" rec_dtb_alt_size;
					if [ "${rec_dtb_alt_size}" == "" ]; then
						echo "Error: failed to get the size of recovery-dtb_alt partition";
						exit 1;
					fi
					# Generate the image for recovery-dtb_alt partition
					generate_empty_image "${recdtbfilename}.alt" "${rec_dtb_alt_size}" "${user_sector_size}"
					REC_TAG+="-e s/RECDTB_ALT_FILE/${recdtbfilename}.alt/ ";
				fi
			else
				echo "Recovery dtb file is missing"
				exit 1
			fi
		else
			RECDTB_TAG+="-e /RECDTB-FILE/d ";#COMPAT
			RECDTB_TAG+="-e /RECDTB_FILE/d ";
			RECDTB_TAG+="-e /RECDTB_ALT_FILE/d ";
		fi
	else
		REC_TAG+="-e /RECFILE/d ";
		REC_TAG+="-e /REC_ALT_FILE/d ";
		RECDTB_TAG+="-e /RECDTB-FILE/d ";#COMPAT
		RECDTB_TAG+="-e /RECDTB_FILE/d ";
		RECDTB_TAG+="-e /RECDTB_ALT_FILE/d ";
	fi

	if [ "${MSI_EMMC_OFFSET}" != "" ]; then
		REC_TAG+="-e s/MSI_EMMC_OFFSET/${MSI_EMMC_OFFSET}/ "
	fi

	if [ "${MSI_QSPI_OFFSET}" != "" ]; then
		REC_TAG+="-e s/MSI_QSPI_OFFSET/${MSI_QSPI_OFFSET}/ "
	fi

	# BOOTCTRL_TAG: Kernel boot control metadata
	#
	BOOTCTRL_TAG+="-e s/BOOTCTRL-FILE/kernel_bootctrl.bin/ ";
	BOOTCTRL_FILE_SIZE=20
	# make a dummpy kernel_bootctrl.bin for generating index file for OTA
	dd if=/dev/zero of=kernel_bootctrl.bin bs=1 count=${BOOTCTRL_FILE_SIZE}
else
	REC_TAG+="-e /RECFILE/d ";
	REC_TAG+="-e /REC_ALT_FILE/d ";
	RECDTB_TAG+="-e /RECDTB-FILE/d ";#COMPAT
	RECDTB_TAG+="-e /RECDTB_FILE/d ";
	RECDTB_TAG+="-e /RECDTB_ALT_FILE/d ";
	BOOTCTRL_TAG+="-e /BOOTCTRL-FILE/d ";
fi
# RECROOTFS partition size is set to 300MiB if RECROOTFSSIZE is not set
if [ -z "${recrootfssize}" ];then
	RECROOTFS_TAG="-e s/RECROOTFSSIZE/${RECROOTFS_SIZE_DEF}/ ";
else
	RECROOTFS_TAG="-e s/RECROOTFSSIZE/${recrootfssize}/ ";
fi

# NCT_TAG:
#
if [ "${bcffile}" != "" ]; then
	cp2local bcffile "${BL_DIR}/${bcffilename}";
	NCTARGS+="--boardconfig ${bcffilename} ";
	NCT_TAG+="-e /NCTFILE/d ";
	NCT_TAG+="-e s/NCTTYPE/data/ ";
elif [ "${boardid}" != "" ]; then
	: # Do nothing
elif [ "${nctfile}" != "" ]; then
	cp2local nctfile "${BL_DIR}/${nctfilename}";
	NCT_TAG+="-e s/NCTFILE/${nctfilename}/ ";
	NCT_TAG+="-e s/NCTTYPE/config_table/ ";
	NCTARGS+="--nct ${nctfilename} ";
else
	NCT_TAG+="-e /NCTFILE/d ";
	NCT_TAG+="-e s/NCTTYPE/data/ ";
fi;

# VER_TAG:
#
if [ "${VERFILENAME}" != "" ]; then
	# NV1: VersionID,ReleaseString
	# NV2: VersionID,ReleaseString,BoardID
	# NV3: VersionID,ReleaseString,BoardID,Timestamp,CRC32
	# NV4: VersionID,ReleaseString,BoardID,Timestamp,Version32,CRC32
	echo "NV4" > "${VERFILENAME}";	# Version file format number
	if [ -f "${LDK_DIR}/nv_tegra/bsp_version" ]; then
		echo "# R${BSP_BRANCH} , REVISION: ${BSP_MAJOR}.${BSP_MINOR}" >> "${VERFILENAME}";
		# Note: default text values for variables are treated as zeros
		BSP_VERSION32=$( printf "0x%x" $(( (BSP_BRANCH<<16) | (BSP_MAJOR<<8) | BSP_MINOR )) )
	else
		head -n 1 "${rootfs_dir}/etc/nv_tegra_release" >> "${VERFILENAME}";
		BSP_VERSION32="0x00000000";
	fi;
	echo "BOARDID=${BOARDID} BOARDSKU=${BOARDSKU} FAB=${FAB}" >> "${VERFILENAME}";
	VER_TIMESTAMP="$(date -R)"
	TIMESTAMP=$(date -d "$VER_TIMESTAMP" +%Y%m%d%H%M%S)
	echo "${TIMESTAMP}" >> "${VERFILENAME}";
	echo "${BSP_VERSION32}" >> "${VERFILENAME}";
	CRC32=$( python3 -c 'import zlib; print("%X"%(zlib.crc32(open("'"${VERFILENAME}"'", "rb").read()) & 0xFFFFFFFF))' )
	if [ "${CRC32}" == "" ]; then
		echo "Failed to calculate version CRC!"
		exit 1
	fi
	NUM_BYTES=$( wc -c "${VERFILENAME}" | awk '{print $1}' )
	echo "BYTES:${NUM_BYTES} CRC32:${CRC32}" >> "${VERFILENAME}";
	VER_TAG+="-e s/VERFILE/${VERFILENAME}/ ";
else
	VER_TAG+="-e /VERFILE/d ";
fi;

# SOS_TAG: recovery
#
if [ "${sosfile}" != "" ]; then
	cp2local sosfile "${BL_DIR}/${sosfilename}";
	SOSARGS+="--applet ${sosfilename} ";
	SOS_TAG+="-e s/SOSFILE/${sosfilename}/ ";

fi;

# MB2BL_TAG:== tboot_recovery
#
if [ "${mb2blfile}" != "" ]; then
	cp2local mb2blfile "${BL_DIR}/${mb2blfilename}";
	NVC_TAG+="-e s/MB2BLFILE/${mb2blfilename}/ ";
	if [ "${BINSARGS}" != "" ]; then
		BINSARGS+="mb2_bootloader ${mb2blfilename}; ";
	fi;
fi;
if [ "${xusbfile}" != "" ]; then
	cp2local xusbfile "${BL_DIR}/${xusbfilename}";
	NVC_TAG+="-e s/XUSB_FW/${xusbfilename}/ ";
	if [ "${BINSARGS}" != "" ]; then
		BINSARGS+="xusb_fw ${xusbfilename}; ";
	fi;
else
	NVC_TAG+="-e s/XUSB_FW// ";
fi

if [ "${pvafile}" != "" ]; then
	cp2local pvafile "${BL_DIR}/${pvafilename}";
	NVC_TAG+="-e s/PVA_FILE/${pvafilename}/ ";
	if [ "${BINSARGS}" != "" ]; then
		BINSARGS+="pva_fw ${pvafilename}; ";
	fi;
else
	NVC_TAG+="-e s/PVA_FILE// "
fi
if [ "${dcefile}" != "" ]; then
	cp2local dcefile "${BL_DIR}/${dcefilename}";
	NVC_TAG+="-e s/DCE/${dcefilename}/ ";
	if [ "${BINSARGS}" != "" ]; then
		BINSARGS+="dce_fw ${dcefilename}; ";
	fi;
else
	NVC_TAG+="-e s/DCE/${xusbfilename}/ ";
fi
if [ "${nvdecfile}" != "" ]; then
	cp2local nvdecfile  "${BL_DIR}/${nvdecfilename}";
	NVC_TAG+="-e s/NVHOSTNVDEC/${nvdecfilename}/ ";
	if [ "${BINSARGS}" != "" ]; then
		BINSARGS+="nvdec ${nvdecfilename}; ";
	fi;
else
	NVC_TAG+="-e s/NVHOSTNVDEC// ";
fi
if [ "${psc_rf}" != "" ]; then
	cp2local psc_rf "${BL_DIR}/${psc_rfname}";
	NVC_TAG+="-e s/PSCRF_IMAGE/${psc_rfname}/ ";
else
	NVC_TAG+="-e s/PSCRF_IMAGE// ";
fi
if [ "${mb2_rf}" != "" ]; then
	cp2local mb2_rf "${BL_DIR}/${mb2_rfname}";
	NVC_TAG+="-e s/MB2RF_IMAGE/${mb2_rfname}/ ";
else
	NVC_TAG+="-e s/MB2RF_IMAGE// ";
fi
if [ "${pscbl1file}" != "" ]; then
	NVC_TAG+="-e s/PSCBL1FILE/${pscbl1filename}/ ";
else
	NVC_TAG+="-e s/PSCBL1FILE// ";
fi
if [ "${tscfwfile}" != "" ]; then
	NVC_TAG+="-e s/TSECFW/${tscfwfilename}/ ";
else
	NVC_TAG+="-e s/TSECFW// ";
fi
if [ "${mtsmcefile}" != "" ]; then
	NVC_TAG+="-e s/MCE_IMAGE/${mtsmcefilename}/ ";
else
	NVC_TAG+="-e s/MCE_IMAGE// ";
fi
if [ "${pscfwfile}" != "" ]; then
	NVC_TAG+="-e s/PSCFW/${pscfwfilename}/ ";
else
	NVC_TAG+="-e s/PSCFW// ";
fi

if [ "${hafniumfwfile}" != "" ]; then
	NVC_TAG+="-e s/HAFNIUM_FW/${hafniumfwfilename}/ ";
else
	NVC_TAG+="-e s/HAFNIUM_FW// ";
fi
if [ "${rce1fwfile}" != "" ]; then
	NVC_TAG+="-e s/RCE1FW/${rce1fwfilename}/ ";
else
	NVC_TAG+="-e s/RCE1FW// ";
fi


# IST_TAG:
#
if [ "${ist_ucode}" != "" ]; then
	cp2local ist_ucode "${BL_DIR}/${ist_ucodename}";
	IST_TAG+="-e s/IST_UCODE/${ist_ucodename}/ ";
else
	IST_TAG+="-e /IST_UCODE/d ";
fi;
if [ "${ist_bpmpfw}" != "" ]; then
	cp2local ist_bpmpfw "${BL_DIR}/${ist_bpmpfwname}";
	IST_TAG+="-e s/IST_BPMPFW/${ist_bpmpfwname}/ ";
else
	IST_TAG+="-e /IST_BPMPFW/d ";
fi;
if [ "${ist_config}" != "" ]; then
	cp2local ist_config "${BL_DIR}/${ist_configname}";
	IST_TAG+="-e s/IST_ICTBIN/${ist_configname}/ ";
else
	IST_TAG+="-e /IST_ICTBIN/d ";
fi;
if [ "${ist_testimg}" != "" ]; then
	cp2local ist_testimg "${BL_DIR}/${ist_testimgname}";
	IST_TAG+="-e s/IST_TESTIMG/${ist_testimgname}/ ";
else
	IST_TAG+="-e /IST_TESTIMG/d ";
fi;
if [ "${ist_rtinfo}" != "" ]; then
	cp2local ist_rtinfo "${BL_DIR}/${ist_rtinfoname}";
	IST_TAG+="-e s/IST_RTINFO/${ist_rtinfoname}/ ";
else
	IST_TAG+="-e /IST_RTINFO/d ";
fi;
if [ "${ist_rtid}" != "" ]; then
	cp2local ist_rtid "${BL_DIR}/${ist_rtidname}";
	IST_TAG+="-e s/IST_RTID/${ist_rtidname}/ ";
else
	IST_TAG+="-e /IST_RTID/d ";
fi;
if declare -F -f update_rist_ufs_partition > /dev/null 2>&1; then
	update_rist_ufs_partition "${ist_rdram}" "${ist_rdramname}" "${BL_DIR}"
fi;

# MPB_TAG:
#
if [ "${mtspreboot}" != "" ]; then
	cp2local mtspreboot "${BL_DIR}/${mtsprebootname}";
	MPB_TAG+="-e s/MXB/MPB/ ";
	MPB_TAG+="-e s/MPBNAME/mts-preboot/ ";
	MPB_TAG+="-e s/MPBTYPE/mts_preboot/ ";
	MPB_TAG+="-e s/MPBFILE/${mtsprebootname}/ ";
	if [ "${BINSARGS}" != "" ]; then
		BINSARGS+="mts_preboot ${mtsprebootname}; ";
	else
		MTSARGS+="--preboot ${mtsprebootname} ";
	fi;
else
	MPB_TAG+="-e s/MPBTYPE/data/ ";
	MPB_TAG+="-e /MPBFILE/d ";
fi;

# MBP_TAG:
#
if [ "${mts}" != "" ]; then
	cp2local mts "${BL_DIR}/${mtsname}";
	MBP_TAG+="-e s/MXP/MBP/ ";
	MBP_TAG+="-e s/MBPNAME/mts-bootpack/ ";
	MBP_TAG+="-e s/MBPTYPE/mts_bootpack/ ";
	MBP_TAG+="-e s/MBPFILE/${mtsname}/ ";
	if [ "${BINSARGS}" != "" ]; then
		BINSARGS+="mts_bootpack ${mtsname}; ";
	else
		MTSARGS+="--bootpack ${mtsname} ";
	fi;
else
	MBP_TAG+="-e s/MBPTYPE/data/ ";
	MBP_TAG+="-e /MBPFILE/d ";
fi;

# MB1_TAG:
#
if [ "${mb1file}" != "" ]; then
	cp2local mb1file "${BL_DIR}/${mb1filename}";
	MB1_TAG+="-e s/MB1NAME/mb1/ ";
	MB1_TAG+="-e s/MB1TYPE/mb1_bootloader/ ";
	MB1_TAG+="-e s/MB1FILE/${mb1filename}/ ";
else
	MB1_TAG+="-e s/MB1TYPE/data/ ";
	MB1_TAG+="-e /MB1FILE/d ";
fi;

# BPF_TAG:
#
if [ "${bpffile}" != "" ]; then
	cp2local bpffile "${BL_DIR}/${bpffilename}";
	BPF_TAG+="-e s/BXF/BPF/ ";
	BPF_TAG+="-e s/BPFNAME/bpmp-fw/ ";
	BPF_TAG+="-e s/BPFFILE/${bpffilename}/ ";
	BPF_TAG+="-e s/BPFSIGN/true/ ";
	if [ "${BINSARGS}" != "" ]; then
		BINSARGS+="bpmp_fw ${bpffilename}; ";
	fi;
else
	BPF_TAG+="-e /BPFFILE/d ";
	BPF_TAG+="-e s/BPFSIGN/false/ ";
fi;

# FSIFW_TAG:
#
if [ "${fsifwfile}" != "" ]; then
	cp2local fsifwfile "${BL_DIR}/${fsifwfilename}";
	FSIFW_TAG+="-e s/FSIFW/${fsifwfilename}/ ";
	if [ "${BINSARGS}" != "" ]; then
		BINSARGS+="fsi_fw ${fsifwfilename}; ";
	fi

else
	FSIFW_TAG+="-e /FSIFW/d ";
fi;

# BPFDTB_TAG:
if [ "${bpfdtbfile}" != "" ]; then
	update_bpmp_fw_dtb "${bpfdtbfile}" "${bpfdtbfilename}" "${clkdtsofile}" "${clkdtsofilename}";
	cp2local bpfdtbfile "${BL_DIR}/${bpfdtbfilename}";

	BPFDTB_TAG+="-e s/BPFDTB-NAME/bpmp-fw-dtb/ ";
	BPFDTB_TAG+="-e s/BPFDTB-FILE/${bpfdtbfilename}/ ";
	BPFDTB_TAG+="-e s/BPFDTB_FILE/${bpfdtbfilename}/ ";
	BPFDTB_TAG+="-e s/BPMPDTB-SIGN/true/ ";
	BPFDTB_TAG+="-e s/BPMPDTB/${bpfdtbfilename}/ ";			#COMPAT
	BPFDTB_TAG+="-e s/BXF-DTB/BPF-DTB/ ";				#COMPAT
	if [ "${BINSARGS}" != "" ]; then
		BINSARGS+="bpmp_fw_dtb ${bpfdtbfilename}; ";
	fi;
else
	BPFDTB_TAG+="-e /BPFDTB-FILE/d ";				#COMPAT
	BPFDTB_TAG+="-e /BPFDTB_FILE/d ";
	BPFDTB_TAG+="-e s/BPMPDTB-SIGN/false/ ";
fi;

# SCE_TAG:
if [ "${camerafw}" != "" ]; then
	cp2local camerafw "${BL_DIR}/${camerafwname}";
	SCE_TAG+="-e s/CAMERAFW/${camerafwname}/ ";
	if [ "${BINSARGS}" != "" ]; then
		BINSARGS+="rce_fw ${camerafwname}; ";
	fi;
else
	SCE_TAG+="-e /CAMERAFW/d ";
fi;

# APE_TAG:
if [ "${apefile}" != "" ]; then
	cp2local apefile "${BL_DIR}/${apefilename}";
	APE_TAG+="-e s/APENAME/ape-fw/ ";
	APE_TAG+="-e s/APETYPE/ape_fw/ ";
	APE_TAG+="-e s/APEFILE/${apefilename}/ ";
	if [ "${BINSARGS}" != "" -a "${CHIPID}" = "0x23" -o "${CHIPID}" = "0x23 9" ]; then
	     BINSARGS+="ape_fw ${apefilename}; ";
	fi;

else
	APE_TAG+="-e s/APETYPE/data/ ";
	APE_TAG+="-e /APEFILE/d ";
fi;

# AON_TAG:
if [ "${aonadspfw}" != "" ]; then
	cp2local aonadspfw "${BL_DIR}/${aonadspfwname}";
	AON_TAG+="-e s/AON_ADSPNAME/aon-fw/ ";
	AON_TAG+="-e s/AON_ADSPTYPE/aon_fw/ ";
	AON_TAG+="-e s/AON_ADSPFILE/${aonadspfwname}/ ";
	AON_TAG+="-e s/AON_ADSPFW/${aonadspfwname}/ ";
	if [ "${BINSARGS}" != "" -a "${CHIPID}" = "0x23" -o "${CHIPID}" = "0x23 9" ]; then
	     BINSARGS+="aon_fw ${aonadspfwname}; ";
	fi;
else
	AON_TAG+="-e s/AON_ADSPTYPE/data/ ";
	AON_TAG+="-e /AON_ADSPFILE/d ";
	AON_TAG+="-e /AON_ADSPFW/d ";
fi;

# ADSP0_TAG:
if [ "${adsp0fw}" != "" ]; then
	cp2local adsp0fw "${BL_DIR}/${adsp0fwname}";
	ADSP0_TAG+="-e s/ADSP0NAME/adsp-fw/ ";
	ADSP0_TAG+="-e s/ADSP0TYPE/adsp_fw/ ";
	ADSP0_TAG+="-e s/ADSP0FWFILE/${adsp0fwname}/ ";
	ADSP0_TAG+="-e s/ADSP0FW/${adsp0fwname}/ ";
	if [ "${BINSARGS}" != "" -a "${CHIPID}" = "0x23" -o "${CHIPID}" = "0x23 9" ]; then
	     BINSARGS+="adsp0_fw ${adsp0fwname}; ";
	fi;
else
	ADSP0_TAG+="-e s/ADSP0FWTYPE/data/ ";
	ADSP0_TAG+="-e /ADSP0FWFILE/d ";
	ADSP0_TAG+="-e /ADSP0FW/d ";
fi;

# ADSP1_TAG:
if [ "${adsp1fw}" != "" ]; then
	cp2local adsp1fw "${BL_DIR}/${adsp1fwname}";
	ADSP1_TAG+="-e s/ADSP1NAME/adsp-fw/ ";
	ADSP1_TAG+="-e s/ADSP1TYPE/adsp_fw/ ";
	ADSP1_TAG+="-e s/ADSP1FWFILE/${adsp1fwname}/ ";
	ADSP1_TAG+="-e s/ADSP1FW/${adsp1fwname}/ ";
	if [ "${BINSARGS}" != "" -a "${CHIPID}" = "0x23" -o "${CHIPID}" = "0x23 9" ]; then
	     BINSARGS+="adsp1_fw ${adsp1fwname}; ";
	fi;
else
	ADSP1_TAG+="-e s/ADSP1FWTYPE/data/ ";
	ADSP1_TAG+="-e /ADSP1FWFILE/d ";
	ADSP1_TAG+="-e /ADSP1FW/d ";
fi;

# IGBFW_TAG:
if [ "${igbfw}" != "" ]; then
	cp2local igbfw "${BL_DIR}/${igbfwname}";
	IGBFW_TAG+="-e s/IGBFWNAME/igb-fw/ ";
	IGBFW_TAG+="-e s/IGBFWTYPE/igb_fw/ ";
	IGBFW_TAG+="-e s/IGBFWFILE/${igbfwname}/ ";
	IGBFW_TAG+="-e s/IGBFW/${igbfwname}/ ";
	if [ "${BINSARGS}" != "" -a "${CHIPID}" = "0x23" -o "${CHIPID}" = "0x23 9" ]; then
	     BINSARGS+="igb_fw ${igbfwname}; ";
	fi;
else
	IGBFW_TAG+="-e s/IGBFWTYPE/data/ ";
	IGBFW_TAG+="-e /IGBFWFILE/d ";
	IGBFW_TAG+="-e /IGBFW/d ";
fi;

# ATFFW_TAG:
if [ "${atffw}" != "" ]; then
	cp2local atffw "${BL_DIR}/${atffwname}";
	ATFFW_TAG+="-e s/ATF_FWNAME/atf/ ";
	ATFFW_TAG+="-e s/ATF_FWTYPE/atf/ ";
	ATFFW_TAG+="-e s/ATF_FWFILE/${atffwname}/ ";
	ATFFW_TAG+="-e s/ATF_FW/${atffwname}/ ";
	if [ "${BINSARGS}" != "" -a "${CHIPID}" = "0x23" -o "${CHIPID}" = "0x23 9" ]; then
	     BINSARGS+="atf ${atffwname}; ";
	fi;
else
	ATFFW_TAG+="-e s/ATF_FWTYPE/data/ ";
	ATFFW_TAG+="-e /ATF_FWFILE/d ";
	ATFFW_TAG+="-e /ATF_FW/d ";
fi;

# SPE_TAG:
if [ "${spefile}" != "" ]; then
	cp2local spefile "${BL_DIR}/${spefilename}";
	SPE_TAG+="-e s/SPENAME/spe-fw/ ";
	SPE_TAG+="-e s/SPETYPE/spe_fw/ ";
	SPE_TAG+="-e s/SPEFILE/${spefilename}/ ";
	if [ "${BINSARGS}" != "" -a "${CHIPID}" = "0x23" -o "${CHIPID}" = "0x23 9" ]; then
	     BINSARGS+="spe_fw ${spefilename}; ";
	fi;

else
	SPE_TAG+="-e s/SPETYPE/data/ ";
	SPE_TAG+="-e /SPEFILE/d ";
fi;

# DRAMECC_TAG:
if [ "${drameccfile}" != "" ]; then
	cp2local drameccfile "${BL_DIR}/${drameccfilename}";
	DRAMECC_TAG+="-e s/DRAMECCNAME/dram-ecc-fw/ ";
	DRAMECC_TAG+="-e s/DRAMECCTYPE/dram_ecc/ ";
	DRAMECC_TAG+="-e s/DRAMECCFILE/${drameccfilename}/ ";
else
	DRAMECC_TAG+="-e s/DRAMECCTYPE/data/ ";
	DRAMECC_TAG+="-e /DRAMECCFILE/d ";
fi;

# BADPAGE_TAG:
if [ "${badpagefile}" != "" ]; then
	cp2local badpagefile "${BL_DIR}/${badpagefilename}";
	BADPAGE_TAG+="-e s/BADPAGENAME/bad-page/ ";
	BADPAGE_TAG+="-e s/BADPAGETYPE/black_list_info/ ";
	BADPAGE_TAG+="-e s/BADPAGEFILE/${badpagefilename}/ ";
else
	BADPAGE_TAG+="-e s/BADPAGETYPE/data/ ";
	BADPAGE_TAG+="-e /BADPAGEFILE/d ";
fi;


# WB0_TAG:
#
if [ "${wb0boot}" != "" ]; then
	cp2local wb0boot "${BL_DIR}/${wb0bootname}";
	WB0_TAG+="-e s/WX0/WB0/ ";
	WB0_TAG+="-e s/SC7NAME/sc7/ ";
	WB0_TAG+="-e s/WB0TYPE/WB0/ ";
	WB0_TAG+="-e s/WB0FILE/${wb0bootname}/ ";
	WB0_TAG+="-e s/WB0BOOT/${wb0bootname}/ ";
else
	WB0_TAG+="-e s/WB0TYPE/data/ ";
	WB0_TAG+="-e /WB0FILE/d ";
fi;

# TOS_TAG:
#
if [ "${tosfile}" != "" ]; then
	cp2local tosfile "${BL_DIR}/${tosfilename}";
	TOS_TAG+="-e s/TXS/TOS/ ";
	TOS_TAG+="-e s/TOSNAME/secure-os/ ";
	TOS_TAG+="-e s/TOSFILE/${tosfilename}/ ";
	if [ "${BINSARGS}" != "" ]; then
		BINSARGS+="tos ${tosfilename}; ";
	fi;
else
	TOS_TAG+="-e /TOSFILE/d ";
fi;

# EKS_TAG:
#
EKS_TAG+="-e s/EXS/EKS/ ";
if [ "${eksfile}" != "" ]; then
	cp2local eksfile "${BL_DIR}/${eksfilename}";
	EKS_TAG+="-e s/EKSFILE/${eksfilename}/ ";
	if [ "${BINSARGS}" != "" ]; then
		BINSARGS+="eks ${eksfilename}; ";
	fi;
else
	EKS_TAG+="-e /EKSFILE/d ";
fi;

# FB_TAG:
#
if [ "${fbfile}" != "" ]; then
	chsuffix fbfilebin ${fbfilename} "bin";
	cp2local fbfile "${BL_DIR}/${fbfilename}";
	FB_TAG+="-e s/FBFILE/${fbfilebin}/ ";
	FB_TAG+="-e s/FX/FB/ ";
	FB_TAG+="-e s/FBNAME/fusebypass/ ";
	FB_TAG+="-e s/FBTYPE/fuse_bypass/ ";
	FB_TAG+="-e s/FBSIGN/true/ ";
	if [[ "${fuselevel}" = "fuselevel_nofuse" ]]; then
		FBOTPS="--fb ${fbfilebin} "
		FBARGS+="${FBOTPS}"
		FBCMD="parse fusebypass ${fbfilename} "
		FBARGS+="--cmd \"${FBCMD} ";
		FBSKU_ID="${FBSKU_ID:-misc}"
	else
		FBOTPS=""
		# nv fused board: skip --fb xxx and "parse fusebypass xxx.xml <FBSKU_ID>"
		FBARGS+="--cmd \"";
	fi
	if [ "${CHIPID}" = "0x26" ]; then
		if [[ "${fuselevel}" = "fuselevel_nofuse" ]]; then
			FBCMD+="${FBSKU_ID}; "
			FBARGS+="${FBSKU_ID}; ";
		else
			FBCMD=""
		fi
		if [ ${rcm_boot} -ne 0 ]; then
			FBARGS+="${NVSIGN_CMD} rcmboot\" ";
		elif [ ${bup_blob} -ne 0 ] || [ ${to_sign} -ne 0 ]; then
			FBARGS+="${NVSIGN_CMD} sign\" ";
		else
			FBARGS+="${NVSIGN_CMD} flash; reboot\" ";
		fi;
	elif [ "${CHIPID}" = "0x23" ] || [ "${CHIPID}" = "0x23 9" ]; then
		if [[ "${fuselevel}" = "fuselevel_nofuse" ]]; then
			FBCMD+="${FBSKU_ID}; "
			FBARGS+="${FBSKU_ID}; ";
		else
			FBCMD=""
		fi
		if [ ${bup_blob} -ne 0 ] || [ ${to_sign} -ne 0 ]; then
			FBARGS+="sign\" ";
		elif [ ${rcm_boot} -ne 0 ]; then
			FBARGS+="rcmboot\" ";
		else
			FBARGS+="flash; reboot\" ";
		fi;
		BINSARGS+="kernel boot.img; "
		BINSARGS+="kernel_dtb ${DTB_FILE}; "
	fi;
else
	FB_TAG+="-e s/FBTYPE/data/ ";
	FB_TAG+="-e s/FBSIGN/false/ ";
	FB_TAG+="-e /FBFILE/d ";
	if [ ${rcm_boot} -ne 0 ]; then
		if [ -n "${BINSARGS}" ]; then
			BINSARGS+="kernel boot.img; "
			BINSARGS+="kernel_dtb ${DTB_FILE}; "
		fi
		FBARGS+="--cmd \"${NVSIGN_CMD} rcmboot\" ";
	elif [ ${bup_blob} -ne 0 ] || [ ${to_sign} -ne 0 ]; then
		FBARGS+="--cmd \"${NVSIGN_CMD} sign\" ";
	else
		FBARGS+="--cmd \"flash; reboot\" ";
	fi
fi;

# soft_fuse:
#
if [ "${soft_fuses}" != "" ]; then
	cp2local soft_fuses "${BL_DIR}/${soft_fusesname}";
	NV_ARGS+="--soft_fuses ${soft_fusesname} ";
fi;

# DTB_TAG: Kernel DTB
#
if [ "${dtbfile}" != "" ]; then
	cp2local dtbfile "${BL_DIR}/${dtbfilename}";
	cp "${BL_DIR}/${dtbfilename}" "${BL_DIR}/kernel_${dtbfilename}"
	dtbfilename="kernel_${dtbfilename}";

	DTB_TAG+="-e s/DXB/DTB/ ";
	if [ "${CHIPID}" = "0x23" ]; then
		DTB_TAG+="-e s/name=\"kernel-dtb\"/name=\"A_kernel-dtb\"/ ";
		DTB_TAG+="-e s/name=\"kernel-dtb_b\"/name=\"B_kernel-dtb\"/ ";
	else
		DTB_TAG+="-e s/KERNELDTB-NAME/kernel-dtb/ ";
	fi
	DTB_TAG+="-e s/DTBFILE/${dtbfilename}/ ";
	DTB_TAG+="-e s/KERNELDTB-FILE/${dtbfilename}/ ";
	DTB_TAG+="-e s/DTB_FILE/${dtbfilename}/ ";			#COMPAT
	DTBARGS+="--bldtb ${tbcdtbfilename} ";
else
	DTB_TAG+="-e /DTBFILE/d ";
	DTB_TAG+="-e /KERNELDTB-FILE/d ";
fi;

# inject board spec info into nv_boot_control.conf
inject_board_spec_info

# APP_TAG: RootFS
#

if [ ${disk_enc_enable} -eq 0 ]; then
	localsysfile=system.img;
	APP_TAG+="-e s/APPSIZE/${rootfssize}/ ";

	if [[ "${external_device}" == 1 ]]; then
		# If APPUUID and APPUUID_b exist, replace them with uuid accordingly.
		# Make sure the "APPUUID_b" is replaced before replacing "APPUUID".
		APP_TAG+="-e s/APPUUID_b/${rootfsuuid_b_ext}/ ";
		APP_TAG+="-e s/APPUUID/${rootfsuuid_ext}/ ";
	elif [[ "${target_rootdev}" == "internal" || "${rootfs_ab}" == 1 ]]; then
		# If APPUUID and APPUUID_b exist, replace them with uuid accordingly.
		# Make sure the "APPUUID_b" is replaced before replacing "APPUUID".
		APP_TAG+="-e s/APPUUID_b/${rootfsuuid_b}/ ";
		APP_TAG+="-e s/APPUUID/${rootfsuuid}/ ";
	else
		APP_TAG+="-e s/APPUUID// ";
	fi
elif [ "${app_partition_exist}" = "true" ]; then
	localsysbootfile="";
	localsysencrootfile="";
	bootfssize="";
	encrootfssize="";

	get_value_from_PT_table "APP" "filename" "${cfgfile}" localsysbootfile
	get_value_from_PT_table "APP_ENC" "filename" "${cfgfile}" localsysencrootfile
	get_value_from_PT_table "APP" "size" "${cfgfile}" bootfssize
	encrootfssize=$((${rootfssize}-${bootfssize}));

	if [[ "${rootfs_ab}" == 1 ]]; then
		localsysbootfile_b="";
		localsysencrootfile_b="";

		get_value_from_PT_table "APP_b" "filename" "${cfgfile}" localsysbootfile_b
		get_value_from_PT_table "APP_ENC_b" "filename" "${cfgfile}" localsysencrootfile_b

		APP_TAG+="-e s/APP_ENC_SIZE_b/${encrootfssize}/ ";
		if [[ "${external_device}" == 1 ]]; then
			APP_TAG+="-e s/APPUUID_b/${bootpartuuid_b_ext}/ ";
			if [[ "${partition_app_enc_b_enable}" == "true" ]]; then
				APP_TAG+="-e s/APP_ENC_UUID_b/${rootfsuuid_b_ext_enc}/ ";
			else
				APP_TAG+="-e s/APP_ENC_UUID_b/${rootfsuuid_b_ext}/ ";
			fi
		else
			APP_TAG+="-e s/APPUUID_b/${bootpartuuid_b}/ ";
			if [[ "${partition_app_enc_b_enable}" == "true" ]]; then
				APP_TAG+="-e s/APP_ENC_UUID_b/${rootfsuuid_b_enc}/ ";
			else
				APP_TAG+="-e s/APP_ENC_UUID_b/${rootfsuuid_b}/ ";
			fi
		fi
	fi;

	APP_TAG+="-e s/APP_ENC_SIZE/${encrootfssize}/ ";
	if [[ "${external_device}" == 1 ]]; then
		APP_TAG+="-e s/APPUUID/${bootpartuuid_ext}/ ";
		if [[ "${partition_app_enc_enable}" == "true" ]]; then
			APP_TAG+="-e s/APP_ENC_UUID/${rootfsuuid_ext_enc}/ ";
		else
			APP_TAG+="-e s/APP_ENC_UUID/${rootfsuuid_ext}/ ";
		fi
	else
		APP_TAG+="-e s/APPUUID/${bootpartuuid}/ ";
		if [[ "${partition_app_enc_enable}" == "true" ]]; then
			APP_TAG+="-e s/APP_ENC_UUID/${rootfsuuid_enc}/ ";
		else
			APP_TAG+="-e s/APP_ENC_UUID/${rootfsuuid}/ ";
		fi
	fi
fi;

# At this stage, the kernel dtb in $BL_DIR folder has the "bootargs=" added,
# it can be saved to rootfs.
#
if [ "${reuse_systemimg}" = "true" ] || [ "${skip_systemimg}" = "true" ]; then
	if [ ${disk_enc_enable} -eq 0 ]; then
		APP_TAG+="-e s/APPFILE/${localsysfile}/ ";
	fi;
	if [ "${skip_systemimg}" != "true" ]; then
		sysfile_exist
	else
		if [ ${disk_enc_enable} -eq 0 ]; then
			echo "Skip generating ${localsysfile}";
		else
			echo "Skip generating ${localsysbootfile} & ${localsysencrootfile}";
		fi;
	fi;
elif [ "${rootdev_type}" = "internal" ]; then
	echo -e -n "\tpopulating initrd to rootfs... ";
	cp -f initrd "${rootfs_dir}/boot"; chkerr;
	echo -e -n "\tpopulating ${dtbfilename} to rootfs... ";
	cp -f "${dtbfilename}" "${rootfs_dir}/boot"; chkerr;
	create_fsimg "${rootfs_dir}" ${uefi_sign};
elif [ "${rootdev_type}" = "network" -o "${rootdev_type}" = "external" ]; then
	APP_TAG+="-e s/APPFILE/${localsysfile}/ ";
	echo "generating ${localsysfile} for booting... ";
	tmpdir=$(mktemp -d);
	mkdir -p "${tmpdir}/boot/extlinux" > /dev/null 2>&1;
	cp -f "${rootfs_dir}/boot/extlinux/extlinux.conf" "${tmpdir}/boot/extlinux" > /dev/null 2>&1;
	cp -f "${kernel_fs}" "${tmpdir}/boot" > /dev/null 2>&1;
	cp -f "${dtbfilename}" "${tmpdir}/boot" > /dev/null 2>&1;
	cp -f initrd "${tmpdir}/boot" > /dev/null 2>&1;
	create_fsimg "${tmpdir}" ${uefi_sign};
else
	APP_TAG+="-e /system.img/d ";
	APP_TAG+="-e /APPFILE/d ";
fi;
if [ ${gen_systemimg_only} -eq 1 ]; then
	echo "system.img has been generated."
	exit 0
fi

# We can append signature of dtb file to the dtb file.
if [ "${tegraid}" = "0x23" ] || [ "${tegraid}" = "0x26" ]; then
	if [[ ${uefi_sign} == "False" ]]; then
		echo "Not signing of kernel-dtb"
	else
		# Generate .sig file in a temp folder
		cp "${dtbfilename}" ${temp_user_dir}/kerneldtb > /dev/null 2>&1;
		pushd ${temp_user_dir} > /dev/null 2>&1 || exit 1;
		if [ "${rootfs_ab}" == 1 ]; then
			cp kerneldtb kerneldtb_b
		fi
		# Generate dtb's .sig file
		# Set "append" to append the sig file
		uefi_signimage "kerneldtb" "${uefi_db_key}" "${uefi_db_cert}" "append" "True";
		# save the original as .unsigned
		cp ../"${dtbfilename}" ../"${dtbfilename}".unsigned
		cp "kerneldtb" ../"${dtbfilename}"
		if [ "${rootfs_ab}" == 1 ]; then
			uefi_signimage "kerneldtb_b" "${uefi_db_key}" "${uefi_db_cert}" "append" "True";
			# save the original as .unsigned
			cp ../"${dtbfilename}_b" ../"${dtbfilename}_b".unsigned
			cp "kerneldtb_b" ../"${dtbfilename}_b"
		fi
		popd > /dev/null 2>&1 || exit 1;
	fi
fi

# TBC_TAG:== EBT
#
if [ "${tbcfile}" != "" ]; then
	cp2local tbcfile "${BL_DIR}/${tbcfilename}";
	TBC_TAG+="-e s/TXC/TBC/ ";
	TBC_TAG+="-e s/TBCNAME/cpu-bootloader/ ";
	TBC_TAG+="-e s/TBCTYPE/bootloader/ ";
	TBC_TAG+="-e s/TBCFILE/${tbcfilename}/ ";
else
	TBC_TAG+="-e s/TBCTYPE/data/ ";
	TBC_TAG+="-e /TBCFILE/d ";
fi;

# This override is needed since rcm_tbcfile is indirectly assigned by mkfilesoft().
# The value comes from the board conf file (i.e. RCM_TBCFILE).
if [ "${rcm_tbcfile}" != "" ]; then
	# This override is needed since rcm_tbcfilename is indirectly assigned by cp2local().
	cp2local rcm_tbcfile "${BL_DIR}/${rcm_tbcfilename}";
fi
# ESP_TAG:== Efi System Partition
#
l4t_launcher="${L4T_LAUNCHER:-BOOTAA64.efi}"
localespfile=esp.img;
if [ "${no_esp_img}" = "1" ] || [ "${UEFI_OS_LAUNCHER}" = "0" ] \
	|| [ "${bup_blob}" = "1" ] || [ "${skip_systemimg}" = "true" ] \
	|| { [ "${reuse_systemimg}" = "true" ] && [ -f ${localespfile} ]; }; then
	echo "Skip generating EFI system partition image.";
	ESP_TAG+="-e /ESP_FILE/d ";
	ESP_TAG+="-e /ESP_ALT_FILE/d ";
else
	# Sign BOOTAA64.efi if uefi_sign is true
	if [[ ${uefi_sign} == "True" ]]; then
		# Save the original BOOTAA64.efi as BOOTAA64.efi.unsigned
		cp "${l4t_launcher}" "${l4t_launcher}".unsigned
		echo "Signing " "${l4t_launcher}" "..."
		uefi_signimage "${l4t_launcher}" "${uefi_db_key}" "${uefi_db_cert}" "nosplit" "False"; chkerr
		create_espimage "${l4t_launcher}" "${localespfile}" "${user_sector_size}";
		cp "${l4t_launcher}" "${l4t_launcher}".signed
		# Restore the original (BOOTAA64.efi.unsigned) BOOTAA64.efi
		cp "${l4t_launcher}".unsigned "${l4t_launcher}"
	else
		create_espimage "${l4t_launcher}" "${localespfile}" "${user_sector_size}";
	fi

	ESP_TAG+="-e s/ESP_FILE/${localespfile}/ ";
	ESP_TAG+="-e s/ESP_ALT_FILE/${localespfile}.alt/ ";
fi;

# TBCDTB_TAG:== Bootloader with DTB
#
if [ "${tbcdtbfile}" != "" ]; then
	cp2local tbcdtbfile "${BL_DIR}/${tbcdtbfilename}";
	TBCDTB_TAG+="-e s/TBCDTB-NAME/bootloader-dtb/ ";
	TBCDTB_TAG+="-e s/TBCDTB-FILE/${UEFIBL}/ ";
else
	TBCDTB_TAG+="-e s/TBCTYPE/data/ ";
	TBCDTB_TAG+="-e /TBCDTB-FILE/d ";
fi;

# EFI_TAG: Minimum FAT32 partition size is 64MiB (== 1 FAT cluster)
#
localefifile=efi.img;
efifs_size=$(( 64 * 1024 * 1024 ));
EFI_TAG+="-e s/EFISIZE/${efifs_size}/ ";
# bootloadername is programmatically generated
if [ "${bootloadername}" = "uefi.bin" ]; then
	build_fsimg $localefifile "" $efifs_size "FAT32" "" "$cmdline" "False";
	EFI_TAG+="-e s/EXI/EFI/ ";
	EFI_TAG+="-e s/EFIFILE/${localefifile}/ ";
else
	EFI_TAG+="-e /EFIFILE/d ";
fi;

# EXTDEV_TAG
#
EXTDEV_TAG+="-e s/EXT_NUM_SECTORS/${ext_num_sectors}/ ";

# INTDEV_TAG
#
INTDEV_TAG+="-e s/INT_NUM_SECTORS/${int_num_sectors}/ ";

# CFG:
#
if [[ ${cfgfile} =~ \.xml$ ]]; then
	localcfgfile=flash.xml;
else
	localcfgfile=flash.cfg;
fi;
echo -n "copying cfgfile(${cfgfile}) to ${localcfgfile}... ";
if [ "${BINSARGS}" != "" ]; then
	# Close BINSARGS before get used for the first time.
	BINSARGS+="\"";
	BINSCONV+="-e s/\"[[:space:]]*/\"/ ";
	BINSCONV+="-e s/\;[[:space:]]*\"/\"/ ";
	BINSARGS=$(echo "${BINSARGS}" | sed ${BINSCONV});
fi;
CFGCONV+="${EBT_TAG} ";
CFGCONV+="${LNX_TAG} ";
CFGCONV+="${SOS_TAG} ";
CFGCONV+="${NCT_TAG} ";
CFGCONV+="${VER_TAG} ";
CFGCONV+="${NVC_TAG} ";
CFGCONV+="${MB2BL_TAG} ";
CFGCONV+="${MPB_TAG} ";
CFGCONV+="${IST_TAG} ";
CFGCONV+="${MBP_TAG} ";
CFGCONV+="${MB1_TAG} ";
CFGCONV+="${BPFDTB_TAG} ";
CFGCONV+="${BPF_TAG} ";
CFGCONV+="${SCE_TAG} ";
CFGCONV+="${SPE_TAG} ";
CFGCONV+="${DRAMECC_TAG} ";
CFGCONV+="${BADPAGE_TAG} ";
CFGCONV+="${TOS_TAG} ";
CFGCONV+="${EKS_TAG} ";
CFGCONV+="${FB_TAG}  ";
CFGCONV+="${WB0_TAG} ";
CFGCONV+="${APP_TAG} ";
CFGCONV+="${EFI_TAG} ";
CFGCONV+="${RECDTB_TAG} ";
CFGCONV+="${DTB_TAG} ";
CFGCONV+="${TBCDTB_TAG} ";
CFGCONV+="${TBC_TAG} ";
CFGCONV+="${GPT_TAG} ";
CFGCONV+="${CBOOTOPTION_TAG} ";
CFGCONV+="${ESP_TAG} ";
CFGCONV+="${REC_TAG} ";
CFGCONV+="${BOOTCTRL_TAG} ";
CFGCONV+="${RECROOTFS_TAG} ";
CFGCONV+="${CUSTINFO_TAG} ";
CFGCONV+="${APE_TAG} ";
CFGCONV+="${AON_TAG} ";
CFGCONV+="${ADSP0_TAG} ";
CFGCONV+="${ADSP1_TAG} ";
CFGCONV+="${IGBFW_TAG} ";
CFGCONV+="${ATFFW_TAG} ";
CFGCONV+="${FSIFW_TAG} ";
CFGCONV+="${UDA_TAG} ";
CFGCONV+="${EXTDEV_TAG} ";
CFGCONV+="${INTDEV_TAG} ";


# CFGCONV is a flat string holding a bunch of flags for sed, so should not be
# quoted. Maybe need to convert CFGCONV to a list so that we can quote this.
# For now, though, disable SC2086.
cat "${cfgfile}" | limit_boot_chains | limit_qspi_device | sed ${CFGCONV} > ${localcfgfile}; chkerr;

# Set APP partition's <allocation_attribute> to 0x8
# if the "-S" option is specified
if [ "${set_size}" = "1" ]; then
	update_value_in_PT_table "APP" "allocation_attribute" \
		"${localcfgfile}" "0x8"
fi

# FLASH:
#
cp2local flasher	"${BL_DIR}/${flashername}";
cp2local flashapp	"${BL_DIR}/${flashappname}";

if [[ "${target_partname}" != "" ]] && [[ -z "${UNIFIED_FLASH}" || "${UNIFIED_FLASH}" -eq 0 ]]; then
	validatePartID target_partid target_partname $target_partname $localcfgfile;
	tmp_updateid="[${target_partname}]";
	need_sign=0;
	additional_args=""
	signtype="encrypt";
	if [ "${bootauth}" = "PKC" ] || [ "${bootauth}" = "SBKPKC" ]; then
		signtype="signed";
	fi;
	case ${target_partname} in
	BCT) target_partfile="${bctfilename}";
		if [ -n "${write_image_name}" ]; then
			additional_args+="--bct_cold_boot ${write_image_name}"
			write_image_name="";
		fi;
	     ;;
	*mb2 | mb2_b) target_partfile="${mb2blfilename}";
	     need_sign=1;
	     ;;
	*mb2rf | mb2rf_b) target_partfile="${mb2_rfname}";
	     need_sign=1;
	     ;;
	*bpmp-fw | bpmp-fw_b)
	     target_partfile="${bpffilename}";
	     need_sign=1;
	     ;;
	*bpmp-fw-dtb | bpmp-fw-dtb_b)
	     target_partfile="${bpfdtbfilename}";
	     need_sign=1;
	     ;;
	EBT) target_partfile="${bootloadername}"; need_sign=1; ;;
	*cpu-bootloader | cpu-bootloader_b)
	     target_partfile="${tbcfilename}";
	     need_sign=1;
	     ;;
	bootloader-dtb | bootloader-dtb_b)
	     target_partfile="${tbcdtbfilename}";
	     need_sign=1;
	     ;;
	*secure-os | secure-os_b)
	     target_partfile="${tosfilename}";
	     need_sign=1;
	     ;;
	*eks) target_partfile="${eksfilename}";
	     need_sign=1;
	     ;;
	LNX) target_partfile="${localbootfile}";
		pre_cmds="write DTB ${dtbfilename}; ";
		;;
	*kernel | kernel_b)
		target_partfile="${localbootfile}";
		if [ "${tegraid}" != "0x23" ]; then
			need_sign=1;
		fi;
		if [[ "${rootfs_ab}" == 1  && ${target_partname} == "kernel_b" ]]; then
			target_partfile="${localbootfile}_b";
		fi;
		;;
	*kernel-dtb | kernel-dtb_b) target_partfile="${dtbfilename}";
		if [ "${tegraid}" != "0x23" ]; then
			need_sign=1;
		fi;
		;;
	*fsi-fw) target_partfile="${fsifwfilename}";
			need_sign=1;
		;;
	NCT) target_partfile="${nctfilename}"; ;;
	SOS) target_partfile="${sosfilename}"; ;;
	MPB) target_partfile="${mtsprebootname}"; ;;
	MBP) target_partfile="${mtsname}"; ;;
	BPF) target_partfile="${bpffilename}"; ;;
	APP)
		if [ ${disk_enc_enable} -eq 0 ]; then
			target_partfile="${localsysfile}";
		else
			target_partfile="${localsysbootfile}";
		fi;
		;;
	APP_b)
		if [ ${rootfs_ab} -eq 0 ]; then
			echo "*** Update APP_b is not supported. ***";
			echo "*** Set ROOTFS_AB=1 to enable APP_b. ***";
			exit 1;
		elif [ ${rootfs_ab} -eq 1 ] && [ ${disk_enc_enable} -eq 0 ]; then
			target_partfile="${localsysfile}_b";
		elif [ ${rootfs_ab} -eq 1 ] && [ ${disk_enc_enable} -eq 1 ]; then
			target_partfile="${localsysbootfile_b}";
		fi;
		;;
	APP_ENC)
		if [ ${disk_enc_enable} -eq 0 ]; then
			echo "*** Update APP_ENC is not supported. ***";
			echo "*** Set ROOTFS_ENC=1 to enable APP_ENC. ***";
			exit 1;
		else
			target_partfile="${localsysencrootfile}";
		fi
		;;
	APP_ENC_b)
		if [ ${disk_enc_enable} -eq 0 ] || [ ${rootfs_ab} -eq 0 ]; then
			echo "*** Update APP_ENC_b is not supported. ***";
			echo "*** Set ROOTFS_AB=1 & ROOTFS_ENC=1 to enable APP_ENC_b. ***";
			exit 1;
		else
			target_partfile="${localsysencrootfile_b}";
		fi
		;;
	DTB|RP1) target_partfile="${dtbfilename}";
		need_sign=1;
		;;
	EFI) target_partfile="${localefifile}"; ;;
	TOS) target_partfile="${tosfilename}"; ;;
	EKS) target_partfile="${eksfilename}"; ;;
	FB)  target_partfile="${fbfilename}"; ;;
	WB0) target_partfile="${wb0bootname}"; ;;
	*mb1 | mb1_b)
		if [ "${CHIPID}" = "0x23" ]; then
	     		need_sign=1;
		fi
	     target_partfile="${mb1filename}";
		 ;;
	*rce-fw | rce-fw_b)
	     target_partfile="${camerafwname}";
	     need_sign=1;
	     ;;
	A_psc_bl1)
	     target_partfile="${pscbl1filename}";
	     need_sign=1;
	     ;;
	A_psc-fw)
	     target_partfile="${pscfwfilename}";
	     need_sign=1;
	     ;;
	A_tsec-fw)
	     target_partfile="${tscfwfilename}";
	     need_sign=1;
	     ;;
	A_nvdec)
	     target_partfile="${nvdecfilename}";
	     need_sign=1;
	     ;;
	A_dce-fw)
	     target_partfile="${dcefilename}";
	     need_sign=1;
	     ;;
	mts-preboot | *mts-mce | mts-proper | \
	mts-preboot_b | mts-mce_b | mts-proper_b | \
	*adsp-fw | extended-can-fw | \
	A_aon-fw | B_aon-fw | \
	*adsp0-fw | *adsp1-fw | *igb_fw | *atf | *hafnium | \
	adsp-fw_b | extended-can-fw_b | \
	fusebypass)
	     # For partitions that do not have default image, user must provide
	     # the image to be flashed
	     if [ "${read_part_name}" = "" ] && [ "${write_image_name}" = "" ]; then
	         echo -n "*** Error: missing ${target_partname} image. ";
	         echo "Use option --image to specify the image to be flashed.";
	         exit 1;
	     fi;
	     need_sign=1;
	     ;;
	xusb-fw | xusb-fw_b | BMP | BMP_b)
	     if [ "${read_part_name}" = "" ] && [ "${write_image_name}" = "" ]; then
	         echo -n "*** Error: missing ${target_partname} image. ";
	         echo "Use option --image to specify the image to be flashed.";
	         exit 1;
	     fi;
	     ;;
	*MB1_BCT | MB1_BCT_b)
		# use the name hard coded by tegraflash.py
		if [ "${read_part_name}" = "" ]; then
			write_image_name="signed/mb1_cold_boot_bct_MB1_sigheader.bct.${signtype}"
		fi;
		need_sign=1;
		;;
	#
	# Comment out sc7 support. It is found that sc7 sigheader is different and it needs special handling
	# See 200617500
	#
	*sc7 | sc7_b) target_partfile="${wb0bootname}";
		need_sign=1;
		;;
	*spe-fw | spe-fw_b)
	     target_partfile="${spefilename}";
	     need_sign=1;
	     ;;
	esp)
		target_partfile="${localespfile}";
		if [ ${no_flash} -eq 1 ] && [ "${read_part_name}" = "" ]; then
			echo "*** esp image is generated at ${BL_DIR}/${localespfile}"
			exit 0
		fi
		;;
	*)   echo "*** Update ${tmp_updateid} is not supported. ***";
	     exit 1; ;;
	esac;
	if [ "${read_part_name}" != "" ]; then
		# Read partition
		target_partfile="${read_part_name}";
		echo "*** Reading ${tmp_updateid} and storing to ${target_partfile} ***";
	else
		# Write partition
		if [ "${write_image_name}" != "" ]; then
			# write partition with image provided in command line
			target_partfile="${write_image_name}";
		fi;
		if [ ${no_flash} -eq 1 ]; then
			echo "*** Signing ${target_partfile} ***";
		else
			echo "*** Updating ${tmp_updateid} with ${target_partfile} ***";
		fi;
	fi;
	if [ "${FLASHARGS}" = "" ]; then
		if [ "${CHIPID}" = "0x23" ] || [ "${CHIPID}" = "0x23 9" ]  || [ "${CHIPID}" = "0x26" ]; then
			if [ "${RCM_UEFIBL}" != "" ]; then
				flashername=${RCM_UEFIBL};
			else
				flashername=${UEFIBL};
			fi
			FLASHARGS+=" --bl ${flashername} ";
		else
			FLASHARGS="--bl ${flashername} ${DTBARGS} ";
		fi;
		if [ "${CHIPMAJOR}" != "" ]; then
			FLASHARGS+="--chip \"${tegraid} ${CHIPMAJOR}\" ";
		else
			FLASHARGS+="--chip \"${tegraid}\" ";
		fi;
		FLASHARGS+="--applet ${sosfilename} ";
	fi;
	if [ "${CHIPID}" = "0x23" ]; then
		FLASHARGS+="$BCT ${bctfilename} ";
	fi

	if [ ${need_sign} -eq 1 ]; then
		pf_dir="$(dirname "${target_partfile}")";
		if [ "${read_part_name}" != "" ]; then
			mkdir -p "${pf_dir}/signed" > /dev/null 2>&1;
		fi;
	fi

	FLASHARGS+="${BCTARGS}${NV_ARGS} ";
	if [ "${CHIPID}" = "0x26" ]; then
		FLASHARGS+="--coldboot_pt_layout  ${localcfgfile}"
	else
		FLASHARGS+="--cfg  ${localcfgfile} ${BINSARGS} ";
	fi
	if [ -n "${odmdata}" ]; then
		FLASHARGS+=" --odmdata ${odmdata} ";
	fi
	if [ "${CHIPID}" = "0x23" ] || [ "${CHIPID}" = "0x26" ] && [ "${OVERLAY_DTB_FILE}" != "" ]; then
		echo "${OVERLAY_DTB_FILE}" | while read -d, line || [[ -n $line ]]; do
			mkfilesoft overlay_dtb	"${DTB_DIR}/${line}" "";
			cp2local overlay_dtb "${BL_DIR}/${overlay_dtbname}";
		done
		# Process the L4TConfiguration.dtbo before flashing if it exists
		if [ -f "${L4T_CONF_DTBO}" ]; then
			process_l4t_conf_dtbo
		fi
		FLASHARGS+=" --overlay_dtb ${OVERLAY_DTB_FILE} ";
	fi
	if [ "${CHIPID}" = "0x23" ] || [ "${CHIPID}" = "0x26" ] && [ "${DCE_OVERLAY_DTB_FILE}" != "" ]; then
		echo "${DCE_OVERLAY_DTB_FILE}" | while read -d, line || [[ -n $line ]]; do
		mkfilesoft overlay_dtb	"${line}" "${DTB_DIR}/${line}";
		cp2local overlay_dtb "${BL_DIR}/${overlay_dtbname}";
		done
		FLASHARGS+=" --dce_overlay_dtb ${DCE_OVERLAY_DTB_FILE} ";
	fi
	if [[ ${uefi_sign} == "True" ]]; then
		FLASHARGS+=" --rawkerneldtb ${dtbfilename}.unsigned ";
	else
		FLASHARGS+=" --rawkerneldtb ${dtbfilename} ";
	fi
	FLASHARGS+="--bldtb ${tbcdtbfilename} ";
	FLASHARGS+="--concat_cpubl_bldtb --cpubl ${tbcfilename} ";

	if [ "${rcm_tbcfile}" != "" ]; then
		FLASHARGS+="--cpubl_rcm ${rcm_tbcfilename} ";
	fi

	target_parttype=""
	get_partition_attribute_in_PT_table "${target_partname}" "${localcfgfile}" "type" target_parttype
	FLASHARGS+=" --cmd \"";
	FLASHARGS+="${pre_cmds}";
	if [ "${read_part_name}" != "" ]; then
		FLASHARGS+="read ${target_partname} ${target_partfile}\" ";
	else
		# if target_partfile is not specified, exit with error message
		if [ "${target_partfile}" = "" ];then
			echo "*** Error: the file for writing ${target_partname} or signing is not specified. ****"
			exit 1
		fi
		if [ ${no_flash} -eq 1 ]; then
			if [ "${target_partname}" == "BCT" ]; then
				# Signing BCT needs all neccessary flashing args.
				FLASHARGS="$(echo "${FLASHARGS}" | sed -e "s/--cmd \".*/--cmd \"sign ${target_partfile} BCT\" /")";
			elif [[ "${CHIPID}" = "0x23" || "${CHIPID}" = "0x26" ]]; then
				# Cpubl needs to concat dtb files before signing
				FLASHARGS="--chip ${CHIPID} "
				if [[ "${target_partname}" == "A_cpu-bootloader" ||
					"${target_partname}" == "B_cpu-bootloader" ]]; then
					FLASHARGS+="--overlay_dtb ${OVERLAY_DTB_FILE} --bldtb ${tbcdtbfilename} --concat_cpubl_bldtb --cpubl ${target_partfile} "
					FLASHARGS+="--cmd \"sign ${target_partfile} bootloader_stage2 ${target_partname}\" ";
				else
					FLASHARGS+="--cmd \"sign ${target_partfile} ${target_parttype}\" "
				fi;
			else
				FLASHARGS="--chip ${CHIPID} "
				FLASHARGS+="--cmd \"sign ${target_partfile} ${target_parttype}\" "
			fi
			# tegraflash add check to detect if cold boot or rcm boot bct cfg was passed on t264.
			# without this information, exception triggers.
			if [ "${CHIPID}" = "0x26" ]; then
				FLASHARGS+="${BCTARGS} "
			fi
		else
			# Only issue erase command for QSPI device.
			# The sdmmc erase/trim operation may corrupt other partitions.
			# See 200565454 and 200615787
			if [[ "${ext_target_board_canonical}" == "p3509-0000+p3668"* ||
				"${ext_target_board_canonical}" == "p3448-0000-sd"* ||
				"${ext_target_board_canonical}" == "p3449-0000+p3448-0000-qspi"* ||
				"${ext_target_board_canonical}" == "p3448-0000-max-spi"* ]]; then
				# issue an erase command before write
				FLASHARGS+="erase ${target_partname}; ";
			fi

			if [ ${need_sign} -eq 1 ]; then
				# special handling for MB1_BCT
				if [[ "${target_partname}" == *MB1_BCT ]] ||
					[ "${target_partname}" = "MB1_BCT_b" ]; then
					FLASHARGS+="sign; write ";
				else
					FLASHARGS+="signwrite ";
				fi;
			else
				FLASHARGS+="write ";
			fi
			FLASHARGS+="${target_partname} ${target_partfile}; ";
			FLASHARGS+="reboot\" ";
		fi
	fi
	FLASHARGS+="${SKIPUID} ";
	if [ -n "${usb_instance}" ]; then
		FLASHARGS+="--instance ${usb_instance} ";
	fi;
	# Use PKC from HSM means all keys are in HSM
	if [ "${hsm_enable}" -eq 1 ]; then
		check_hsm_support
		FLASHARGS+="--hsm ";
	fi
	# Add keyfile if provided
	if [ "${keyfile}" != "" ]; then
		FLASHARGS+="--key \"${keyfile}\" ";
	fi;
	if [[ "${CHIPID}" = "0x23" || "${CHIPID}" = "0x26" ]] &&
		[[ "${target_partname}" == "A_cpu-bootloader" || "${target_partname}" == "B_cpu-bootloader" ]] &&
		[ "${pv_enc_key}" != "" ] && [ -f "${pv_enc_key}" ]; then
			FLASHARGS+="--encrypt_key \"${pv_enc_key}\" "
			FLASHARGS+="--enable_user_kdk "
	elif [ "${sbk_keyfile}" != "" ]; then
		FLASHARGS+="--encrypt_key \"${sbk_keyfile}\" ";
	fi
	if [ "${pkc_keylistfile}" != "" ]; then
		FLASHARGS+="--key_list \"${pkc_keylistfile}\" ";
	fi;
	if [ "${disable_random_iv}" = "True" ]; then
		echo "Disable random IV"
		FLASHARGS+="--disable_random_iv ";
	fi
	if [ "${RAMCODE}" != "" ]; then
		FLASHARGS+="--ramcode ${RAMCODE} ";
	fi;
	# Need to write BCT backup partition if it exists
	if partition_exists_in_PT_table "BCT-boot-chain_backup" "${cfgfile}"; then
		FLASHARGS+="--bct_backup ";
	fi;
	# Need to check if the partition is compressed
	target_partcompress=""
	get_partition_attribute_in_PT_table "${target_partname}" "${localcfgfile}" "compress" target_partcompress
	if [ "${target_partcompress}" == "true" ]; then
		FLASHARGS+="--compress  ${target_parttype} ";
	fi;
	# Flash BCT for booting from chain A or chain B
	FLASHARGS+=" --boot_chain ${boot_chain_select} ";
	# Flash PVA partition based on variable NO_PVA
	FLASHARGS+=" --no_pva ${no_pva} ";
	FLASHARGS+="${additional_args} ";
	if [ "${debug_mode}" -eq 1 ]; then
		FLASHARGS+="--keep ";
	fi
	echo "./${flashappname} ${FLASHARGS}";
	cmd="./${flashappname} ${FLASHARGS}";
	if [ -f "rcm_state" ]; then
		rm rcm_state;
	fi;
	if ! [[ ${need_sign} -eq 0 && ${no_flash} -eq 1 ]]; then
		eval ${cmd};
		chkerr "Failed to flash/read ${target_board}.";
	fi
	if [ "${read_part_name}" != "" ]; then
		#
		# Save signed image with .signed extension,
		#
		if [ ${need_sign} -eq 1 ]; then
			mv -f "${target_partfile}" "${target_partfile}.signed";
			dd if="${target_partfile}.signed" of="${target_partfile}" \
				skip=1
		fi;
		echo "*** The ${tmp_updateid} has been read successfully. ***";
		if [ "${target_partname}" = "APP" -a -x mksparse ]; then
			echo -e -n "\tConverting RAW image to Sparse image... ";
			mv -f ${target_partfile} ${target_partfile}.raw;
			./mksparse --fillpattern=0 ${target_partfile}.raw ${target_partfile};
		fi;
	else
		if [ ${no_flash} -eq 1 ]; then
			echo "*** ${target_partfile} has been signed successfully. ***";
		else
			echo "*** The ${tmp_updateid} has been updated successfully. ***";
		fi;
	fi;
	exit 0;
fi;

# Init flash args
FLASHARGS="";

if [ -n "${usb_instance}" ]; then
	FLASHARGS+="--instance ${usb_instance} ";
fi;
if [ "${RCM_UEFIBL}" != "" ]; then
	FLASHARGS+=" --bl ${RCM_UEFIBL} ";
else
	FLASHARGS+=" --bl ${UEFIBL} ";
fi

if [ -n "${odmdata}" ]; then
	FLASHARGS+=" --odmdata ${odmdata} ";
fi
# The key handling below applies only for T264 rcm-boot
if [ "${keyfile}" != "" ]; then
	FLASHARGS+="--key \"${keyfile}\" ";
fi
if [ "${pkc_keylistfile}" != "" ]; then
	FLASHARGS+="--key_list \"${pkc_keylistfile}\" ";
fi
if [ "${sbk_keyfile}" != "" ]; then
	FLASHARGS+="--encrypt_key \"${sbk_keyfile}\" ";
fi
if [ "${OVERLAY_DTB_FILE}" != "" ]; then
	echo "${OVERLAY_DTB_FILE}" | while read -d, line || [[ -n $line ]]; do
		mkfilesoft overlay_dtb "${DTB_DIR}/${line}" "";
		cp2local overlay_dtb "${BL_DIR}/${overlay_dtbname}";
	done
	# Process the L4TConfiguration.dtbo before flashing if it exists
	if [ -f "${L4T_CONF_DTBO}" ]; then
		process_l4t_conf_dtbo
	fi
	FLASHARGS+=" --overlay_dtb ${OVERLAY_DTB_FILE} ";
fi
if [ "${DCE_OVERLAY_DTB_FILE}" != "" ]; then
	echo "${DCE_OVERLAY_DTB_FILE}" | while read -d, line || [[ -n $line ]]; do
		mkfilesoft overlay_dtb	"${line}" "${DTB_DIR}/${line}";
		cp2local overlay_dtb "${BL_DIR}/${overlay_dtbname}";
	done
	FLASHARGS+=" --dce_overlay_dtb ${DCE_OVERLAY_DTB_FILE} ";
fi
if [[ ${uefi_sign} == "True" ]]; then
	FLASHARGS+=" --rawkerneldtb ${dtbfilename}.unsigned ";
else
	FLASHARGS+=" --rawkerneldtb ${dtbfilename} ";
fi
FLASHARGS+=" ${PASSTHROUGH}${DTBARGS}${MTSARGS}${SOSARGS}${NCTARGS}${FBARGS}${NV_ARGS} ";

if [[ -n "${rcmbootptlayout}" && ${rcm_boot} -ne 0 ]] ; then
	FLASHARGS+="--rcmboot_pt_layout ${localcfgfile} ";
elif [[ -n "${coldbootptlayout}" && ( ${bup_blob} -ne 0  || ${to_sign} -ne 0 )  ]] ; then
	FLASHARGS+="--coldboot_pt_layout ${localcfgfile} ";
else
	FLASHARGS+="--cfg ${localcfgfile} ";
fi
if [ "${CHIPMAJOR}" != "" ]; then
	FLASHARGS+="--chip \"${tegraid} ${CHIPMAJOR}\" ";
else
	FLASHARGS+="--chip \"${tegraid}\" ";
fi;
FLASHARGS+="--concat_cpubl_bldtb --cpubl ${tbcfilename} ";
if [ "${rcm_tbcfile}" != "" ]; then
	FLASHARGS+="--cpubl_rcm ${rcm_tbcfilename} ";
fi
if [ -f "${BL_DIR}/${custinfofilename}" ]; then
	FLASHARGS+=" --cust_info ${custinfofilename} "
fi

FLASHARGS+="${BCTARGS} ";
FLASHARGS+="${BINSARGS} ";
FLASHARGS+="${SKIPUID} ";

if [ "${CHIPID}" = "0x23" ] || [ "${CHIPID}" = "0x23 9" ]; then
	FLASHARGS+="${BCT} ${bctfilename} ";

fi;

if [ "$TRIM_BPMP_DTB" = "true" ]; then
	FLASHARGS+="--trim_bpmp_dtb ";
fi;

if [ "${external_device}" -eq 1 ] && [[ "${target_rootdev}" = "internal" || "${target_rootdev}" == "${BOOTDEV}" || "${target_rootdev}" = "eth0" ]]; then
	FLASHARGS+="--external_device ";
fi

if [ "${sparseupdate}" -eq 1 ]; then
	FLASHARGS+=" --sparseupdate "
fi

# Use PKC from HSM means all keys are in HSM
if [ "${hsm_enable}" -eq 1 ]; then
	check_hsm_support
	FLASHARGS+="--hsm ";
fi

# Need to write BCT backup partition if it exists
if partition_exists_in_PT_table "BCT-boot-chain_backup" "${cfgfile}"; then
	FLASHARGS+=" --bct_backup ";
fi;

# Flash BCT for booting from chain A or chain B
FLASHARGS+=" --boot_chain ${boot_chain_select} ";

# Flash PVA partition based on variable NO_PVA
FLASHARGS+=" --no_pva ${no_pva} ";

if [ ${clean_up} -eq 0 ]; then
	if [ ! -f odmsign.func ]; then
		echo "ERROR: missing odmsign.func file. Exitting"
	    exit 1
	fi

	source odmsign.func;

	# Let odmsign_ext handle encryption/signing if PKC or SBK keys are specified.
	# For signing or flashing with signed images, odmsign_ext will execute and exit from there.
	# If it returns with non-0 status, exit.
	# If it returns with 0 status, let the main (flash.sh) finish the flashing.
	odmsign_ext;
	if [ $? -ne 0 ]; then
		exit 1;
	fi
fi

# Add for t264
# Because the key handling inside odmsign_ext will not be called
if [ "${CHIPID}" = "0x26" ]; then
	if [ "${keyfile}" != "" ]; then
		FLASHARGS+="--key \"${keyfile}\" ";
	fi;
	if [ "${sbk_keyfile}" != "" ]; then
		FLASHARGS+="--encrypt_key \"${sbk_keyfile}\" ";
	fi;
fi

if [ "${debug_mode}" -eq 1 ]; then
	FLASHARGS+="--keep ";
fi

if [ ${gen_read_ramcode} -eq 1 ]; then
	tegraflash_cmd="./${flashappname} ${FLASHARGS}";

	flashcmd=$(cat << EOF
tar -xf rcmdump_blob.tar
if [ \$? -ne 0 ]; then
    echo "Failed to extract rcmdump_blob.tar"
    exit 1
fi

source read_ramcode.func;

ramcode=${RAMCODE};

# Read on-board ramcode from the target
read_ramcode;

${tegraflash_cmd} --ramcode \${ramcode}
EOF
)

else
	if [ "${RAMCODE}" != "" ]; then
		FLASHARGS+=" --ramcode ${RAMCODE} ";
	fi;
	flashcmd="./${flashappname} ${FLASHARGS}";
fi

echo "${flashcmd}";
flashcmdfile="${BL_DIR}/flashcmd.txt";
echo "saving flash command in ${flashcmdfile}";
echo "${flashcmd}" > "${flashcmdfile}";
# Remove --skipuid flag for running flash command tegraflash.py directly
sed -i 's/--skipuid//g' "${flashcmdfile}"

# For Windows flashing or rcmboot
sata_boot_ext="sb"
rcm_boot_ext="rb"
kernel_dtb_file="kernel_dtb_filename.txt"
if [ ${rcm_boot} -eq 0 ]; then
	flashargfile="${BL_DIR}/flash_parameters.txt";
	cp -f ${localbootfile} ${localbootfile}.${sata_boot_ext};
	chkerr "Failed to copy boot image file ${localbootfile}.";
	cp -f ${localcfgfile} ${localcfgfile}.${sata_boot_ext};
	chkerr "Failed to copy partition layout file ${localcfgfile}.";
	cp -f ${dtbfilename} ${dtbfilename}.${sata_boot_ext};
	chkerr "Failed to copy kernel dtb file ${dtbfilename}.";
else
	flashargfile="${BL_DIR}/rcmboot_parameters.txt";
	cp -f ${localbootfile} ${localbootfile}.${rcm_boot_ext};
	chkerr "Failed to copy boot image file ${localbootfile}.";
	cp -f initrd initrd.${rcm_boot_ext};
	chkerr "Failed to copy initrd image file initrd.";
fi;
echo "${FLASHARGS}" > "${flashargfile}";
# Remove --skipuid flag for running flash command tegraflash.py directly
sed -i 's/--skipuid//g' "${flashargfile}"

# generate batch command for Windows flashing
flash_win_file="${BL_DIR}/flash_win.bat";
flash_win_cmd="python .\\win_tools\\${flashappname}"
echo "saving Windows flash command to ${flash_win_file}";
echo -n "${flash_win_cmd} " > "${flash_win_file}";
cat "${flashargfile}" >> "${flash_win_file}"

# generate bootloader update payload (BUP)
if [ ${bup_blob} -ne 0 ]; then
	bup_gen="${BL_DIR}/l4t_bup_gen.func"
	if [ -f "${bup_gen}" ]; then
		source "${bup_gen}"
		echo "*** Sign and generate BUP... *** ";
		if [ "${BOARDID}" = "" ]; then
			echo "Error: BOARDID is missing. BOARDID can be either set by "\
				"environment variable BOARDID or by reading from on-board "\
				"EEPROM."
			exit 1
		fi
		if [ "${fuselevel}" = "" ]; then
			echo "Error: fuselevel is missing."
			exit 1
		fi;
		if [ "${FAB}" = "" ]; then
			echo "Error: FAB # is missing."
			exit 1
		fi;
		l4t_bup_gen "${flashcmd}" "${spec}" "${fuselevel}" "${target_board}" \
			"${keyfile}" "${sbk_keyfile}" "${CHIPID}"
	else
		echo ""
		echo "Error: Missing ${bup_gen}"
		echo ""
		exit 1
	fi;
	exit 0;
fi;

if [ ${no_flash} -ne 0 ]; then
	if [ "${rcm_boot}" -eq 0 ]; then
		echo "*** no-flash flag enabled. Exiting now... *** ";
		exit 0;
	else
		echo "${flashcmd} --no_flash";
		eval "${flashcmd} --no_flash";
		if [ "${gen_read_ramcode}" -eq 1 ] && [ "${CHIPID}" = 0x26 ]; then
			rcmbootcmd_file="rcmboot_blob/rcmbootcmd.txt";
			tegrarcm_cmd=$(cat ${rcmbootcmd_file})
			mem_rcm_file=$(echo "${tegrarcm_cmd}" | awk -F"bct_mem " '/bct_mem /{print $2}' | cut -f1 -d' ');

			t264_set_read_ramcode_cmd;

			# Replace the specific bct_mem file (based on ramcode) in ${tegrarcm_cmd} with a generic filename:
			tegrarcm_cmd=${tegrarcm_cmd/${mem_rcm_file}/membct_ramcode_file};

			# Add ${read_ramcode_cmd} to the beginning of tegrarcm_cmd:
			rcmboot_cmd="${read_ramcode_cmd}";
			rcmboot_cmd+="\n\n";
			rcmboot_cmd+=${tegrarcm_cmd};
			rcmboot_cmd=$(printf "${rcmboot_cmd}");

			echo "Adding read_ramcode codes to ${rcmbootcmd_file}";
			echo "${rcmboot_cmd}" > "${rcmbootcmd_file}";
			# rcmdump_blob.tar is needed to do read_ramcode stuff.
			# Copy rcmdump_blob.tar to rcmboot_blob.
			cp rcmdump_blob.tar rcmboot_blob/;
		fi
		exit 0;
	fi
fi

echo "*** Flashing target device started. ***"
eval "${flashcmd}";
chkerr "Failed flashing ${target_board}.";
echo "*** The target ${target_board} has been flashed successfully. ***"
if [ "${rootdev_type}" = "internal" ]; then
	echo "Reset the board to boot from internal eMMC.";
elif [ "${rootdev_type}" = "network" ]; then
	if [ "${nfsroot}" != "" ]; then
		echo -n "Make target nfsroot(${nfsroot}) exported ";
		echo "on the network and reset the board to boot";
	else
		echo -n "Make the target nfsroot exported on the ";
		echo -n "network, configure your own DHCP server ";
		echo -n "with \"option-root=<nfsroot export path>;\" ";
		echo "properly and reset the board to boot";
	fi;
else
	echo -n "Make the target filesystem available to the device ";
	echo -n "and reset the board to boot from external ";
	echo "${target_rootdev}.";
fi;
echo;
exit 0;

# vi: ts=8 sw=8 noexpandtab
