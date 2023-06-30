#!/bin/bash

# Copyright (c) 2020-2023, NVIDIA CORPORATION. All rights reserved.
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
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

# Usage: ./l4t_create_images_for_kernel_flash.sh -N <IPaddr>:/nfsroot <board-name> \
# <rootdev>
# This script creates the flash images and copy it to the correct locations. You
# might want to disable hung_task_panic as flashing to external storage could
# take a long time by using 'echo 0 > /proc/sys/kernel/hung_task_panic'
set -e

function cleanup()
{
	if [ -n "${tmp_dir}" ] && findmnt -M "${tmp_dir}" > /dev/null; then
		umount "${tmp_dir}"
	fi

	if [ -d "${tmp_dir}" ]; then
		rm -rf "${tmp_dir}"
	fi

}

trap cleanup EXIT

function usage()
{
	echo -e "
Usage: $0 -N <IPaddr>:/nfsroot [-u <keyfile>] <board-name> <rootdev>
Where,
    -u <PKC key file>            PKC key used for odm fused board.
    -v <SBK key file>            SBK key used for encryptions
    -N <IPaddr>:/nfsroot         Indicate where the IP and the location of the NFS root file system
    -n nfsargs                   Static nfs network assignments
                                 <Client IP>:<Server IP>:<Gateway IP>:<Netmask>
    -p <option>                  Pass options to flash.sh when generating the image for internal storage
    <board-name>                 Indicate which board to use.
    <rootdev>                    Indicate what root device to use
    --no-flash                   Stop the flash script from flashing the target to NFS
    --flash-only                 Flash to NFS root file system without creating the image
    --external-device <dev>      Generate and/or flash images for the indicated external storage
                                 device. If this is used, -c option must be specified.
    --external-only              Skip generating internal storage images
    --usb-instance               Specify the usb port where the flashing cable is plugged (i.e 1-3)
    -c <config file>             The partition layout for the external storage device.
    -S <size>                    External APP partition size in bytes. KiB, MiB, GiB short hands are allowed,
                                 for example, 1GiB means 1024 * 1024 * 1024 bytes. (optional)
    -t                           Skip to generate flash package tarball. This option can be used
                                 for host that is also used for NFS host.
    --user_key <key_file>        User provided key file (16-byte) to encrypt user images, like kernel, kernel-dtb and initrd.
                                 If user_key is specified, SBK key (-v) has to be specified.
                                 For now, user_key file must contain all 0's.
    --pv-crt                     User provided key to sign cpu_bootloader



With --external-device options specified, the supported values for <dev> are
    nvme0n1
    sda

Examples:
    1) Flash jetson-xavier:
        sudo ./l4t_create_images_for_kernel_flash.sh -N 192.168.0.21:/data/nfsroot jetson-xavier mmcblk0p1

    2) Flash jetson-xavier and the attached external storage device:

        sudo ./l4t_create_images_for_kernel_flash.sh --external-device nvme0n1
            -c external_storage_layout.xml
            -S 5120000000
            -N 192.168.0.21:/data/nfsroot
            jetson-xavier
            mmcblk0p1

The results of the command is:
a) Generate images for external device based on external_storage_layout.xml and
set the size of the APP to ~ 5GB (5120000000 bytes)

b) Generate images for internal device based on the given <board-name> and
<rootdev>. More specifically, it internally uses the following command:
    ./flash.sh --no-flash --sign jetson-xavier mmcblk0p1

c) Then the device is RCM boot to NFS

The uuid of boot partition for external storage device is generated each time
and saved in Linux_for_Tegra/bootloader/l4t-rootfs-uuid.txt.ext.

	"; echo;
	exit 1
}


# This function validates whether the IP given is valid
function validateIP()
{
	local ip="${1}";
	local ret=1;

	if [[ $ip =~ ^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$ ]]; then
		OIFS=${IFS};
		IFS='.';
		read -r -a ip <<< "${ip}";
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

function isuuid()
{
	local uuid_regex
	uuid_regex="([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})"

	local
	rootfsuuid=$(echo "$1" | sed -nr "s/^${uuid_regex}$/\1/p")

	if [ "${#rootfsuuid}" != "36" ]; then
		return 1
	fi

	return
}

# This function validates whether the nfsroot given is valid
function validateNFSroot()
{
	if [ "$3" = "" ]; then
		return 1;
	fi;
	OIFS=${IFS};
	IFS=':';
	local nfsroot="${1}";
	local ip_addr="${2}"
	read -r -a a <<< "${3}";
	IFS=${OIFS};
	if [ ${#a[@]} -ne 2 ]; then
		echo "${#a[@]}"
		echo "Error: Invalid nfsroot(${3})";
		exit 1;
	fi;
	validateIP "${a[0]}";
	if [[ "${a[1]}" != /* ]]; then
		echo "Error: Invalid nfsroot(${3})";
		exit 1;
	fi;
	eval "${ip_addr}=${a[0]}";
	eval "${nfsroot}=${a[1]}";
	return 0;
}

function get_value_from_PT_table()
{
	# Usage:
	#	get_value_from_PT_table {__pt_name} \
	#	{__pt_node} \
	#	{__pt_file} \
	local __pt_name="${1}";
	local __pt_node="${2}";
	local __pt_file="${3}";
	local __node_val="";


	# Get node value
	__node_val="$(xmllint --xpath "/partition_layout/device/partition[@name='${__pt_name}']/${__pt_node}/text()" "${__pt_file}")";
	__node_val=$(echo "${__node_val}" | xargs echo);

	echo "${__node_val}";
}

function is_valid_root_for_external()
{
	[[ "${1}" =~ ^internal|external|nvme[0-9]+n[0-9]+(p[0-9]+)|sd[a-z]+[0-9]+|mmcblk[1-9][0-9]*p[0-9]+$ ]]
}

# This function generate a folder that contains everything neccessary to flash
function generate_flash_images()
{
	echo "Create folder to store images to flash"
	if [ -z "${append}" ]; then
		rm -rf "${NFS_IMAGES_DIR}"
		mkdir -p "${NFS_IMAGES_DIR}/${INTERNAL}"
		mkdir -p "${NFS_IMAGES_DIR}/${EXTERNAL}"
		chmod 755 "${NFS_IMAGES_DIR}"
		chmod 755 "${NFS_IMAGES_DIR}/${INTERNAL}"
		chmod 755 "${NFS_IMAGES_DIR}/${EXTERNAL}"
	fi


	# generate all the images needed for flashing
	if [ "${external_only}" = "0" ]; then
		echo "Generate image for internal storage devices"
		if ! generate_signed_images "${OPTIONS}" 0 "${target_rootdev}"; then
			echo "Error: failed to generate images"
			exit 1
		fi

		# relocate the images we just create to the designated folder
		if ! package_images "0"; then
			echo "Error: failed to relocate images to ${NFS_IMAGES_DIR}"
			exit 1
		fi
	fi

	# If flashing to external device, generate external device image here
	if [ -n "${external_device}" ]; then
		echo "Generate image for external storage devices"
		local root;
		root=${external_device};
		if is_valid_root_for_external "${target_rootdev}"; then
			root=${target_rootdev};
		elif ! is_valid_root_for_external "${root}"; then
			echo "External device is ${root} with no partition specified."
			echo "Use \"internal\" as root device when generating images for external device"
			root="internal"
		fi

		# generate all the images needed for flashing external device
		if ! generate_signed_images "${EXTOPTIONS}" "1" "${root}"; then
			echo "Error: Failed to generate images for external device"
			exit 1
		fi

		# relocate the images we just create to the designated folder
		if ! package_images "1"; then
			echo "Error: failed to relocate images to ${NFS_IMAGES_DIR}"
			exit 1
		fi

	fi

	echo "Copy flash script to ${NFS_IMAGES_DIR}"
	cp -afv "${L4T_NFSFLASH_DIR}/${KERNEL_FLASH_SCRIPT}" "${NFS_IMAGES_DIR}"

	# The code below generates the sample flash from nfs systemd service that
	# automatically runs the flash script once the system boot up. Only for
	# testing
	if [ "${TEST}" = "1" ]; then
		copy_service_to_output
	fi

}

function copy_service_to_output()
{
	cp -afv "${L4T_NFSFLASH_DIR}/nv-l4t-flash-from-nfs.service" \
	"${NFS_IMAGES_DIR}"
	sed -i "s/\${board_name}/${target_board}/g" \
	"${NFS_IMAGES_DIR}/nv-l4t-flash-from-nfs.service"
}

function check_prereq()
{
	# Check xmllint
	if ! command -v xmllint &> /dev/null; then
		echo "ERROR xmllint not found! To install - please run: " \
			"\"sudo apt-get install libxml2-utils\""
		exit 1
	fi;
}

# This function finds all the images mentioned in the flash index file and puts
# it in a folder. Pass 1 to package external images, 0 to package internal images
function package_images()
{

	local external="${1}"
	local dest_dir=${NFS_IMAGES_DIR}
	local ext=
	if [ "${external}" = "1" ]; then
		ext="_ext"
		dest_dir="${dest_dir}/${EXTERNAL}"
	else
		dest_dir="${dest_dir}/${INTERNAL}"
	fi

	if [ ! -f "${FLASH_INDEX_FILE}" ]; then
		echo "Error: ${FLASH_INDEX_FILE} is not found"
		return 1
	fi

	if [ ! -f "${FLASH_XML_FILE}" ]; then
		echo "Error: ${FLASH_XML_FILE} is not found"
		return 1
	fi

	cp -avf "${FLASH_INDEX_FILE}" "${dest_dir}/flash.idx"

	readarray index_array < "${FLASH_INDEX_FILE}"
	echo "Flash index file is ${FLASH_INDEX_FILE}"

	lines_num=${#index_array[@]}
	echo "Number of lines is $lines_num"

	max_index=$((lines_num - 1))
	echo "max_index=${max_index}"

	for i in $(seq 0 ${max_index})
	do
		local item="${index_array[$i]}"

		local file_name
		file_name=$(echo "${item}" | cut -d, -f 5 | sed 's/^ //g' -)
		local part_name
		part_name=$(echo "${item}" | cut -d, -f 2 | sed 's/^ //g' - | cut -d: \
		-f 3)

		# need to create tarball for APP and APP_b partition
		if [ "${part_name}" = "APP" ] || [ "${part_name}" = "APP_b" ]; then

			localsysbootfile=$(get_value_from_PT_table "${part_name}" "filename" \
				"${FLASH_XML_FILE}")

			echo "Copying ${part_name} image into " \
			"${dest_dir}/${localsysbootfile}.raw"

			if [ "${sparse_mode}" = "1" ]; then
				cp "${BOOTLOADER_DIR}/${localsysbootfile}" "${dest_dir}/${localsysbootfile}"
			else
				tmp_dir=$(mktemp -d -t ci-XXXXXXXXXX)

				# Try to convert system.img.raw to tar format
				mount -o loop "${BOOTLOADER_DIR}/${localsysbootfile}.raw" "${tmp_dir}"

				# create tar images of the system.img and its sha1sum
				tar --xattrs -cpf "${dest_dir}/${localsysbootfile}" \
				"${COMMON_TAR_OPTIONS[@]}" -C "${tmp_dir}" .
			fi

			sha1sum "${dest_dir}/${localsysbootfile}" | cut -f 1 -d ' ' \
			> "${dest_dir}/${localsysbootfile}.sha1sum"

			var="${part_name}${ext}"
			echo -e "${var}=${localsysbootfile}" >> "${dest_dir}/flash.cfg"

			# Clean up
			if [ "${sparse_mode}" != "1" ]; then
				umount "${tmp_dir}"
				rm -rf "${tmp_dir}"
			fi
			continue
		fi

		if [ -z "${file_name}" ]; then
			echo "Warning: skip writing ${part_name} partition as no image is \
specified"
			continue
		fi

		# Try searching image in the "ENCRYPTED_SIGNED_DIR" directory and
		# then in "BOOTLOADER_DIR" directory
		local part_image_file="${ENCRYPTED_SIGNED_DIR}/${file_name}"
		if [ ! -f "${part_image_file}" ]; then
			part_image_file="${BOOTLOADER_DIR}/${file_name}"
			if [ ! -f "${part_image_file}" ]; then
				echo "Error: image for partition ${part_name} is not found at "\
				"${part_image_file}"
				return 1
			fi
		fi

		# Copy the image we found or generated into the designated folder
		echo "Copying ${part_image_file} "\
					"${dest_dir}/$(basename "${part_image_file}")"
		cp -avf "${part_image_file}" \
		"${dest_dir}/$(basename "${part_image_file}")"

	done

	# Generate the flash configuration to be included in the flash package
	{
		echo -e "external_device=${external_device}"
		echo -e "CHIPID=${CHIPID}"
	} >> "${dest_dir}/flash.cfg"

}

function get_images_dir()
{
	local CHIPID
	local sbk_keyfile
	CHIPID=$(LDK_DIR=${LINUX_BASE_DIR}; source "${LDK_DIR}/${target_board}.conf";echo "${CHIPID}")
	# use by odmsign_get_folder
	sbk_keyfile="${SBK_KEY}"
	if [ -f "${BOOTLOADER_DIR}/odmsign.func" ]; then
		echo "$(source "${BOOTLOADER_DIR}/odmsign.func"; odmsign_get_folder)"
	else
		echo "signed"
	fi

}

# This function issues a command to flash.sh to generate all the neccessary
# images. Needs two arguments:
#       options: flash options to pass to flash.sh
#       external: 1 to generate signed images for external images, 0 for
# internal images
#       rootdev:  rootdev device to flash to
function generate_signed_images()
{
	local options="${1}"
	local external="${2}"
	local rootdev="${3}"
	local board_arg=

	local cmd_arg="--no-flash --sign "
	if [ "${external}" = "1" ]; then
		cmd_arg+="--external-device -c \"${config_file}\" "
		if [ -n "${external_size}" ]; then
			cmd_arg+="-S \"${external_size}\" "
		fi
		board_arg="BOOTDEV=${rootdev} "
	fi
	board_arg+="ADDITIONAL_DTB_OVERLAY=\"${ADDITIONAL_DTB_OVERLAY_OPT}\" "
	if [ -n "${KEY_FILE}" ] && [ -f "${KEY_FILE}" ]; then
		cmd_arg+="-u \"${KEY_FILE}\" "
	fi

	if [ -n "${SBK_KEY}" ] && [ -f "${SBK_KEY}" ]; then
		cmd_arg+="-v \"${SBK_KEY}\" "
	fi

	if [ -n "${user_key}" ]; then
		cmd_arg+="--user_key \"${user_key}\" "
	fi

		if [ -n "${pv_crt}" ]; then
		cmd_arg+="--pv-crt \"${pv_crt}\" "
	fi

	if [ -n "${usb_instance}" ]; then
		cmd_arg+="--usb-instance \"${usb_instance}\" "
	fi

	cmd_arg+="${options} ${target_board} ${rootdev}"

	cmd="${board_arg} ${LINUX_BASE_DIR}/flash.sh ${cmd_arg}"
	export BOARDID
	export FAB
	export BOARDSKU
	export BOARDREV
	export CHIP_SKU
	export RAMCODE_ID
	export RAMCODE
	echo "Generate images to be flashed"
	echo -e "${cmd}\r\n"
	eval "${cmd}"
	return $?
}

# This function generates a tarball containing the flash package
function generate_tarball()
{
	local tmp_dir
	tmp_dir=$(mktemp -d -t ci-XXXXXXXXXX)
	mkdir -p "${tmp_dir}"
	move_flash_package_to "${tmp_dir}"
	TAR_IMAGE="${L4T_NFSFLASH_DIR}/nv_flash_from_nfs_image.tbz2"
	pushd "${tmp_dir}"
	tar jcvf "${TAR_IMAGE}" -- *
	popd
	echo -e "\n\nGenerated a tar package at ${TAR_IMAGE}. Copy this package on \
to the NFS root filesystem that will be used by your target to continue the \
flashing process."
	rm -rf "${tmp_dir}"
}

# This function move the flash package to the specified location
function move_flash_package_to()
{
	local destination="${1}/images_to_flash/${target_board}"
	if [ -d "${destination}" ]; then
		echo -e "\nFolder ${destination} already exists. This tool requires \
that this folder has not existed before. Please remove this folder, or backup \
and then remove it."
		exit 1
	fi

	echo "Copy images to NFS rootfs"
	mkdir -p "${destination}"
	chmod a+rx -R "${1}/images_to_flash/"
	cmd="cp -avxf ${NFS_IMAGES_DIR}/* ${destination}"
	# to be able to use wildcard
	eval "${cmd}"
}

L4T_NFSFLASH_DIR="$(cd "$(dirname "${0}")" && pwd)"
L4T_TOOLS_DIR="${L4T_NFSFLASH_DIR%/*}"
LINUX_BASE_DIR="${L4T_TOOLS_DIR%/*}"
BOOTLOADER_DIR="${LINUX_BASE_DIR}/bootloader"
NFS_IMAGES_DIR="${L4T_NFSFLASH_DIR}/images"
COMMON_TAR_OPTIONS=("--checkpoint=10000" "--warning=no-timestamp" \
"--numeric-owner")
KERNEL_FLASH_SCRIPT=l4t_flash_from_kernel.sh
no_flash=0
flash_only=0
OPTIONS=""
not_generate_tar=0
nargs=$#;
target_rootdev=${!nargs};
nargs=$((nargs-1));
target_board=${!nargs};
external_device=""
config_file=""
external_size=""
append=""
nfsargs=""
external_only=0
EXTOPTIONS="${EXTOPTIONS}"
source "${L4T_NFSFLASH_DIR}"/l4t_kernel_flash_vars.func

if [ "${USER}" != "root" ]; then
	echo "${0} requires root privilege";
	exit 1;
fi

if [ $# -lt 3 ]; then
	usage;
fi;

opstr+="N:n:u:tp:v:c:-:sS:"
while getopts "${opstr}" OPTION; do
	case $OPTION in
	c) config_file=${OPTARG}; ;;
	n) nfsargs=${OPTARG}; ;;
	p) OPTIONS=${OPTARG}; ;;
	t) not_generate_tar=1; ;;
	u) KEY_FILE=${OPTARG}; ;;
	v) SBK_KEY=${OPTARG}; ;;
	N) NFSROOT=${OPTARG}; ;;
	S) external_size=${OPTARG}; ;;
	-) case ${OPTARG} in
	   append) append=1; ;;
	   no-flash) no_flash=1; ;;
	   flash-only) flash_only=1; ;;
	   external-only) external_only=1; ;;
	   external-device)
	    external_device="${!OPTIND}";
	    OPTIND=$((OPTIND + 1));
	   ;;
	   sparse) sparse_mode=1; ;;
	   usb-instance)
		usb_instance="${!OPTIND}";
		OPTIND=$((OPTIND + 1));
		;;
	   user_key)
		user_key="${!OPTIND}";
		OPTIND=$((OPTIND + 1));
		;;
	   pv-crt)
		pv_crt="${!OPTIND}";
		OPTIND=$((OPTIND + 1));
		;;
	   *) usage ;;
	   esac;;
	*)
	   usage
	   ;;
	esac;
done
if [ "${external_only}" = "1" ]; then
	EXTOPTIONS="${OPTIONS}"
fi
ENCRYPTED_SIGNED_DIR="${BOOTLOADER_DIR}/$(get_images_dir)"
FLASH_INDEX_FILE="${ENCRYPTED_SIGNED_DIR}/flash.idx"
INTERNAL="internal"
EXTERNAL="external"
FLASH_XML_FILE="${ENCRYPTED_SIGNED_DIR}/flash.xml.tmp"
CHIPID=$(LDK_DIR=${LINUX_BASE_DIR}; source "${LDK_DIR}/${target_board}.conf";echo "${CHIPID}")


check_prereq

if [ -n "${external_device}" ]; then
	if [ -z "${config_file}" ]; then
		usage
	fi
	if [[ ! "${external_device}" =~ ^nvme[0-9]+n[0-9]+(p[0-9]+)?$ \
	&& ! "${external_device}" =~ ^sd[a-z]+[0-9]*
	&& ! "${external_device}" =~ ^mmcblk[1-9][0-9]*(p[0-9]+)?$ ]]; then
		echo "${external_device} is not a supported external storage device"
		exit 1
	fi
fi

if [ ! -f "${LINUX_BASE_DIR}/flash.sh" ]; then
	echo "Error: ${LINUX_BASE_DIR}/flash.sh is not found"
	exit 1
fi

# Validate nfs settings
if ! validateNFSroot NFS_LOCATION IP_ADDR "${NFSROOT}"; then
	if [ "${no_flash}" = "0" ]; then
		usage
	fi
fi

# Generate flash images if it is not flash only
if [ "${flash_only}" = "0" ]; then

	# Generate the flash package here
	generate_flash_images

	# Skip tarball generation if not_generate_tar = 1
	if [ "${not_generate_tar}" = "0" ] ; then
		generate_tarball
	fi

	if [ "${no_flash}" = "0" ]; then
		# Find out if NFS host is the same as current host, place the flash package
		# under NFS root.
		# IP_ADDR is defined in validateNFSroot
		if ! grep -q "${IP_ADDR}" <<< "$(hostname -I)"; then
			echo "NFS root fs is not on the host system. Skip moving the flash \
		package to NFS root"
		else
			NFS_LOCATION="$(readlink -f "${NFS_LOCATION}")"
			move_flash_package_to "${NFS_LOCATION}"
		fi
	fi
fi

# Do rcm-boot into NFS if there is no --no-flash option specified
if [ "${no_flash}" = "0" ]; then
	echo "Flash the device to NFS"
	if [ -n "${nfsargs}" ]; then
		cmd_arg="-n ${nfsargs} "
	fi

	if [ -n "${KEY_FILE}" ] && [ -f "${KEY_FILE}" ]; then
		cmd_arg+="-u \"${KEY_FILE}\" "
	fi

	if [ -n "${SBK_KEY}" ] && [ -f "${SBK_KEY}" ]; then
		cmd_arg+="-v \"${SBK_KEY}\" "
	fi

	if [ -n "${usb_instance}" ]; then
		cmd_arg+="--usb-instance \"${usb_instance}\" "
	fi

	cmd_arg+="-N ${NFSROOT} --rcm-boot ${target_board} eth0"
	cmd="USE_UBOOT=0 INITRD_IN_BOOTIMG=yes ${LINUX_BASE_DIR}/flash.sh ${cmd_arg}"
	echo -e "${cmd}\r\n"
	eval "${cmd}"
fi

echo "Success"
