#!/bin/bash

# Copyright (c) 2019-2020, NVIDIA Corporation.  All Rights Reserved.
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA Corporation is strictly prohibited.

# This is a script to generate the SD card flashable image for
# jetson-nano, jetson-nano-2gb-devkit and jetson-xavier-nx-devkit platforms

set -e

function usage()
{
	if [ -n "${1}" ]; then
		echo "${1}"
	fi

	echo "Usage:"
	echo "${script_name} -o <sd_blob_name> -b <board> -r <revision>"
	echo "	sd_blob_name	- valid file name"
	echo "	board		- board name. Supported boards are:"
	echo "			   jetson-nano"
	echo "			   jetson-nano-2gb-devkit"
	echo "			   jetson-xavier-nx-devkit"
	echo "	revision	- SKU revision number"
	echo "			   jetson-nano: 100/200/300 for A01/A02/B00"
	echo "			   jetson-nano-2gb-devkit: default"
	echo "			   jetson-xavier-nx-devkit: default"
	echo "Example:"
	echo "${script_name} -o sd-blob.img -b jetson-nano -r 300"
	echo "${script_name} -o sd-blob.img -b jetson-xavier-nx-devkit"
	exit 1
}

function cleanup() {
	set +e
	if [ -n "${tmpdir}" ]; then
		umount "${tmpdir}"
		rmdir "${tmpdir}"
	fi

	if [ -n "${loop_dev}" ]; then
		losetup -d "${loop_dev}"
	fi
}
trap cleanup EXIT

function check_revision()
{
	case "${board}" in
	jetson-nano)
		case "${rev}" in
		"100" | "200" | "300")
			;;
		*)
			usage "Incorrect Revision - Supported revisions - 100, 200, 300"
			;;
		esac
		;;
	jetson-xavier-nx-devkit)
		rev="000"
		;;
	jetson-nano-2gb-devkit)
		rev="300"
		;;
	esac
}

function check_pre_req()
{
	this_user="$(whoami)"
	if [ "${this_user}" != "root" ]; then
		echo "ERROR: This script requires root privilege" > /dev/stderr
		usage
		exit 1
	fi

	while [ -n "${1}" ]; do
		case "${1}" in
		-h | --help)
			usage
			;;
		-b | --board)
			[ -n "${2}" ] || usage "Not enough parameters"
			board="${2}"
			shift 2
			;;
		-o | --outname)
			[ -n "${2}" ] || usage "Not enough parameters"
			sd_blob_name="${2}"
			shift 2
			;;
		-r | --revision)
			[ -n "${2}" ] || usage "Not enough parameters"
			rev="${2}"
			shift 2
			;;
		*)
			usage "Unknown option: ${1}"
			;;
		esac
	done

	if [ "${board}" == "" ]; then
		echo "ERROR: Invalid board name" > /dev/stderr
		usage
	else
		case "${board}" in
		jetson-nano)
			boardid="3448"
			target="jetson-nano-qspi-sd"
			storage="sdcard"
			;;
		jetson-nano-2gb-devkit)
			boardid="3448"
			boardsku="0003"
			target="jetson-nano-2gb-devkit"
			storage="sdcard"
			;;
		jetson-xavier-nx-devkit)
			boardid="3668"
			target="jetson-xavier-nx-devkit"
			storage="sdcard"
			;;
		*)
			usage "Unknown board: ${board}"
			;;
		esac
	fi

	check_revision

	if [ "${sd_blob_name}" == "" ]; then
		echo "ERROR: Invalid SD blob image name" > /dev/stderr
		usage
	fi

	if [ ! -f "${l4t_dir}/flash.sh" ]; then
		echo "ERROR: ${l4t_dir}/flash.sh is not found" > /dev/stderr
		usage
	fi

	if [ ! -f "${l4t_tools_dir}/nvptparser.py" ]; then
		echo "ERROR: ${l4t_tools_dir}/nvptparser.py is not found" > /dev/stderr
		usage
	fi

	if [ ! -d "${bootloader_dir}" ]; then
		echo "ERROR: ${bootloader_dir} directory not found" > /dev/stderr
		usage
	fi

	if [ ! -d "${rfs_dir}" ]; then
		echo "ERROR: ${rfs_dir} directory not found" > /dev/stderr
		usage
	fi
}

function create_raw_image()
{
	# Calulate raw image size by accumulating partition size with 1MB (2048-sector * 512) round up and plus 2MB for GPTs
	sd_blob_size=$("${l4t_tools_dir}/nvptparser.py" "${signed_image_dir}/${signed_cfg}" "${storage}" | awk -F'[=;]' '{sum += (int($6 / (2048 * 512)) + 1)} END {printf "%dM\n", sum + 2}')
	echo "${script_name} - creating ${sd_blob_name} of ${sd_blob_size}..."
	dd if=/dev/zero of="${sd_blob_name}" bs=1 count=0 seek="${sd_blob_size}"
}

function create_signed_images()
{
	echo "${script_name} - creating signed images"

	pushd "${l4t_dir}"
	# rootfs size = rfs_dir size + extra 10% for ext4 metadata and safety margin
	rootfs_size=$(du -ms "${rfs_dir}" | awk '{print $1}')
	rootfs_size=$((rootfs_size + (rootfs_size / 10)))

	# Generate signed images
	BOARDID="${boardid}" BOARDSKU="${boardsku}" FAB="${rev}" BUILD_SD_IMAGE=1 "${l4t_dir}/flash.sh" "--no-flash" "--sign" "-S" "${rootfs_size}MiB" "${target}" "mmcblk0p1"
	popd

	if [ ! -f "${bootloader_dir}/flashcmd.txt" ]; then
		echo "ERROR: ${bootloader_dir}/flashcmd.txt not found" > /dev/stderr
		exit 1
	fi

	if [ ! -d "${signed_image_dir}" ]; then
		echo "ERROR: ${bootloader_dir}/signed directory not found" > /dev/stderr
		exit 1
	fi

	chipid=$(sed -nr 's/.*chip ([^ ]*).*/\1/p' "${bootloader_dir}/flashcmd.txt")
	if [ "${chipid}" = "0x21" ]; then
		signed_cfg="flash.xml"
	else
		signed_cfg="flash.xml.tmp"
	fi

	if [ ! -f "${signed_image_dir}/${signed_cfg}" ]; then
		echo "ERROR: ${signed_image_dir}/${signed_cfg} not found" > /dev/stderr
		exit 1
	fi
}

function create_partitions()
{
	echo "${script_name} - create partitions"

	partitions=($("${l4t_tools_dir}/nvptparser.py" "${signed_image_dir}/${signed_cfg}" "${storage}"))
	part_type=8300 # Linux Filesystem

	sgdisk -og "${sd_blob_name}"
	for part in "${partitions[@]}"; do
		eval "${part}"
		if [ "${part_name}" = "master_boot_record" ]; then
			continue
		fi
		part_size=$((${part_size} / 512)) # convert to sectors
		sgdisk -n "${part_num}":0:+"${part_size}" \
			-c "${part_num}":"${part_name}" \
			-t "${part_num}":"${part_type}" "${sd_blob_name}"
	done
}

function write_partitions()
{
	echo "${script_name} - write partitions"
	loop_dev="$(losetup --show -f -P "${sd_blob_name}")"

	for part in "${partitions[@]}"; do
		eval "${part}"
		target_file=""
		if [ "${part_name}" = "APP" ]; then
			target_file="${bootloader_dir}/${part_file}.raw"
		elif [ -e "${signed_image_dir}/${part_file}" ]; then
			target_file="${signed_image_dir}/${part_file}"
		elif [ -e "${bootloader_dir}/${part_file}" ]; then
			target_file="${bootloader_dir}/${part_file}"
		fi

		if [ "${part_name}" = "master_boot_record" ]; then
			dd conv=notrunc if="${signed_image_dir}/${part_file}" of="${sd_blob_name}" bs="${part_size}" count=1
			continue
		fi

		if [ "${target_file}" != "" ] && [ "${part_file}" != "" ]; then
			echo "${script_name} - writing ${target_file}"
			sudo dd if="${target_file}" of="${loop_dev}p${part_num}"
		fi
	done

	losetup -d "${loop_dev}"
	loop_dev=""
}

boardsku=""
sd_blob_name=""
sd_blob_size=""
script_name="$(basename "${0}")"
l4t_tools_dir="$(cd "$(dirname "${0}")" && pwd)"
l4t_dir="${l4t_tools_dir%/*}"
if [ -z "${ROOTFS_DIR}" ]; then
	rfs_dir="${l4t_dir}/rootfs"
else
	rfs_dir="${ROOTFS_DIR}"
fi
bootloader_dir="${l4t_dir}/bootloader"
signed_image_dir="${bootloader_dir}/signed"
loop_dev=""
tmpdir=""

echo "********************************************"
echo "     Jetson Disk Image Creation Tool     "
echo "********************************************"

check_pre_req "${@}"
create_signed_images
create_raw_image
create_partitions
write_partitions

echo "********************************************"
echo "   Jetson Disk Image Creation Complete   "
echo "********************************************"
