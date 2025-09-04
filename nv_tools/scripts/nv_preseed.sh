#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2021 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: MIT
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
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

# This script creates pre-configuration file which will be used when
# running OEM configuration automatic mode.

set -e

function usage()
{
	echo "Usage:"
	echo "${script_name} -u <username> [-p <password>]"
	echo "${script_name} -h"
	echo "  -u | --username - username for new user account"
	echo "  -p | --password - hash data of password for new user account"
	echo "  -h | --help - print this usage"
	exit 1
}

function parse_args()
{
	while [ -n "${1}" ]; do
		case "${1}" in
		-h | --help)
			usage;;
		-u | --username)
			[ -n "${2}" ] || usage || echo "ERROR: Not enough parameters"
			user_name="${2}"
			shift 2
			;;
		-p | --password)
			[ -n "${2}" ] || usage || echo "ERROR: Not enough parameters"
			user_pass="${2}"
			shift 2
			;;
		*)
			echo "ERROR: Invalid parameter. Exiting..."
			usage
			exit 1
			;;
		esac
	done
}

function generate_password()
{
	read -s -p "Password for L4T new user ${user_name}: " password
	user_pass=$(/usr/bin/mkpasswd -m sha-256 "${password}")
	echo ""
}

function check_pre_req()
{
	this_user="$(whoami)"
	if [ "${this_user}" != "root" ]; then
		echo "ERROR: please run as sudo or root user" > /dev/stderr
		usage
		exit 1
	fi

	if [ ! -d "${rfs_dir}" ]; then
		echo "ERROR: ${rfs_dir} directory not found" > /dev/stderr
		usage
	fi

	if [ ! -e "/usr/bin/mkpasswd" ]; then
		echo "ERROR: please run \"apt install whois\" to install required package" > /dev/stderr
		usage
	fi

	if [ -z "${user_name}" ]; then
		echo "ERROR: please specify username parameter" > /dev/stderr
		usage
	fi

	if [ -n "${user_name}" ] && [ -z "${user_pass}" ]; then
		generate_password
	fi
}

function append_locale()
{
	if [ -f "${sys_locale_file}" ]; then
		locale=$(sed 's/\"//g' "${sys_locale_file}" | awk -F'=' '/LANG=/ {print $2}')
	fi

	if [ -z "${locale}" ]; then
		locale="${def_locale}"
	fi

	echo "d-i debian-installer/locale string ${locale}" >> "${preseed_cfg}"
}

function append_layoutcode()
{
	if [ -f "${sys_keyboard_file}" ]; then
		layoutcode=$(sed 's/\"//g' "${sys_keyboard_file}" | awk -F "=" '/XKBLAYOUT=/ {print $2}')
	fi

	if [ -z "${layoutcode}" ]; then
		layoutcode="${def_layoutcode}"
	fi

	echo "d-i keyboard-configuration/layoutcode string ${layoutcode}" >> "${preseed_cfg}"
}

function append_timezone()
{
	if [ -f "${sys_timezone_file}" ]; then
		timezone=$(cat "${sys_timezone_file}")
	fi

	if [ -z "${timezone}" ]; then
		timezone="${def_timezone}"
	fi

	echo "d-i time/zone string ${timezone}" >> "${preseed_cfg}"
}

function append_user_account()
{
	sudo tee -a "${preseed_cfg}" > /dev/null <<EOT
d-i passwd/user-fullname string ${user_name}
d-i passwd/username string ${user_name}
d-i passwd/user-password-crypted password ${user_pass}
EOT
}

function create_preseed_cfg()
{
	if [ -e "${preseed_cfg}" ]; then
		rm "${preseed_cfg}"
	fi

	append_locale
	append_layoutcode
	append_timezone
	append_user_account

	echo "d-i oem-config/late_command string rm /nv_preseed.cfg" >> "${preseed_cfg}"
}

script_name="$(basename "${0}")"
l4t_nv_tools_scripts_dir="$(cd "$(dirname "${0}")" && pwd)"
l4t_nv_tools_dir="${l4t_nv_tools_scripts_dir%/*}"
l4t_dir="${l4t_nv_tools_dir%/*}"
rfs_dir="${l4t_dir}/rootfs"
preseed_cfg="${rfs_dir}/nv_preseed.cfg"

user_name=""
user_pass=""
def_locale="en_US.UTF-8"
def_layoutcode="us"
def_timezone="US/Eastern"

sys_locale_file="/etc/default/locale"
sys_keyboard_file="/etc/default/keyboard"
sys_timezone_file="/etc/timezone"

parse_args "$@"
check_pre_req
create_preseed_cfg
