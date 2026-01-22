#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2021-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

readonly SD_EMMC_ONDEV="mmcblk0"
readonly UFS_ONDEV="sda"
readonly NVME_ONDEV="nvme0n1"
readonly INTERNAL_EMMCBOOT0_ONDEV="mmcblk0boot0"
readonly INTERNAL_EMMCBOOT1_ONDEV="mmcblk0boot1"

function wait_for_external_device()
{
	timeout="${timeout:-10}"
	for _ in $(seq "${timeout}"); do
		if [ -b "${external_device}" ]; then
			break
		fi
		sleep 1
	done
	if [ -b "${external_device}" ]; then
		if [ -n "${erase_all}" ]; then
			set +e
			[ -b "${external_device}" ] && blkdiscard -f "${external_device}"
			set -e
		fi
	else
		echo "Connection timeout: device ${external_device} is still not ready."
 	fi;
}

function convert_base()
{
	local convStr="0123456789abcdefghijklmnopqrstuvwxyz"
	local num
	local base="${2}"

	num=$(busybox bc <<< "${1}")

	# Note that the division will not give a negative result, so safe to use (( ))
	if (( $(busybox bc <<< "${num} < ${base}") )); then
		# return convStr[num]
		echo "${convStr:num:1}"
	else
		local mod
		local new_number
		local current_digit

		# return self.convertBase(num // b, b) + convStr[num % b]
		# return self.convertBase($new_number, $base) + convStr[$current_digit]
		mod=$(busybox bc <<< "${num} % ${base}")
		new_number=$(busybox bc <<< "${num} / ${base}")
		current_digit="${convStr:mod:1}"

		local next_digit
		next_digit=$(convert_base "${new_number}" "${base}")

		echo "${next_digit}${current_digit}"
	fi
}

function generate_adb_number()
{
	local l_ECID
	local decimal_ecid
	local l_Base32
	local adb_number

	l_ECID=$(/bin/bash /bin/nv_fuse_read.sh br_cid)
	l_ECID="${l_ECID#*0x}" # extract only the part after 0x
	l_ECID="${l_ECID^^}" # uppercase

	decimal_ecid=$(busybox bc <<< "obase=10; ibase=16; ${l_ECID}")
	l_Base32=$(convert_base "${decimal_ecid}" 32)
	adb_number="${l_Base32^^}" # l_Base32.upper()

	if [[ "${adb_number}" = "0" ]]; then
		adb_number="00000000000000000000"
	fi

	echo "${adb_number}"
}

function set_up_adb()
{
	ln -s /dev /dev/block
	modprobe -v dummy numdummies=1
	ip link add dummmy0 type dummy
	ip addr add 127.0.0.1/24 brd + dev dummy0 label dummy0:0
	# Mount configfs before making config change
	mount -t configfs none /sys/kernel/config

	mkdir -p /sys/kernel/config/usb_gadget/l4t
	echo 0x0955 > /sys/kernel/config/usb_gadget/l4t/idVendor
	echo 0x7100 > /sys/kernel/config/usb_gadget/l4t/idProduct
	echo 0x0002 > /sys/kernel/config/usb_gadget/l4t/bcdDevice
	echo 0x0200 > /sys/kernel/config/usb_gadget/l4t/bcdUSB
	mkdir -p /sys/kernel/config/usb_gadget/l4t/strings/0x409

	echo "NVIDIA" > /sys/kernel/config/usb_gadget/l4t/strings/0x409/manufacturer
	echo "FunctionFS gadget (adb)" > /sys/kernel/config/usb_gadget/l4t/strings/0x409/product
	# Set up Adb serial number
	adb_serial_num=$(generate_adb_number)
	echo "${adb_serial_num}" > /sys/kernel/config/usb_gadget/l4t/strings/0x409/serialnumber

	mkdir -p /sys/kernel/config/usb_gadget/l4t/functions/ffs.adb
	mkdir -p /dev/usb-ffs/adb
	mount -t functionfs adb /dev/usb-ffs/adb
	mkdir /sys/kernel/config/usb_gadget/l4t/configs/b.1
	mkdir /sys/kernel/config/usb_gadget/l4t/configs/b.1/strings/0x409
	echo "ffs1" > /sys/kernel/config/usb_gadget/l4t/configs/b.1/strings/0x409/configuration
	ln -s /sys/kernel/config/usb_gadget/l4t/functions/ffs.adb /sys/kernel/config/usb_gadget/l4t/configs/b.1/f2
	if [ -e /sys/class/usb_role/usb2-0-role-switch/role ]; then
		echo "device" > /sys/class/usb_role/usb2-0-role-switch/role
	fi
	sleep 1
	/bin/adbd64 &
	sleep 1
	if [ -e /sys/bus/usb/devices/usb2/power/control ]; then
		echo on > /sys/bus/usb/devices/usb2/power/control
	fi
	echo "${udc_dev}" > /sys/kernel/config/usb_gadget/l4t/UDC

}

function set_up_usb_device_mode()
(

	# Mount configfs before making config change
	mount -t configfs none /sys/kernel/config

	mkdir -p /sys/kernel/config/usb_gadget/l4t

	cd /sys/kernel/config/usb_gadget/l4t

	# If this script is modified outside NVIDIA, the idVendor and idProduct values
	# MUST be replaced with appropriate vendor-specific values.
	echo 0x0955 > idVendor
	echo 0x7035 > idProduct
	# BCD value. Each nibble should be 0..9. 0x1234 represents version 12.3.4.
	echo 0x0001 > bcdDevice

	# Informs Windows that this device is a composite device, i.e. it implements
	# multiple separate protocols/devices.
	echo 0xEF > bDeviceClass
	echo 0x02 > bDeviceSubClass
	echo 0x01 > bDeviceProtocol

	mkdir -p strings/0x409
	if [ -e "/proc/device-tree/serial-number" ]; then
		cat /proc/device-tree/serial-number > strings/0x409/serialnumber
	else
		echo "0" > strings/0x409/serialnumber
	fi

	# If this script is modified outside NVIDIA, the manufacturer and product values
	# MUST be replaced with appropriate vendor-specific values.
	echo "NVIDIA" > strings/0x409/manufacturer
	echo "Linux for Tegra" > strings/0x409/product

	cfg=configs/c.1
	mkdir -p "${cfg}"
	cfg_str=""

	cfg_str="${cfg_str}+NCM+L4T${instance}"
	func=functions/ncm.usb0
	mkdir -p "${func}"
	ln -sf "${func}" "${cfg}"


	# Parse configuration. `instance` is used to differentiate different device
	if [ -f /initrd_flash.cfg ]; then
		if [ -n "${erase_all}" ]; then
			set +e
			[ -b /dev/${SD_EMMC_ONDEV} ] && blkdiscard -f /dev/${SD_EMMC_ONDEV}
			[ -b /dev/${NVME_ONDEV} ] && blkdiscard -f /dev/${NVME_ONDEV}
			[ -b /dev/${UFS_ONDEV} ] && blkdiscard -f /dev/${UFS_ONDEV}
			[ -b /dev/${INTERNAL_EMMCBOOT0_ONDEV} ] && blkdiscard -f /dev/${INTERNAL_EMMCBOOT0_ONDEV}
			[ -b /dev/${INTERNAL_EMMCBOOT1_ONDEV} ] && blkdiscard -f /dev/${INTERNAL_EMMCBOOT1_ONDEV}
			set -e
		fi
	fi



	mkdir -p "${cfg}/strings/0x409"
	# :1 in the variable expansion strips the first character from the value. This
	# removes the unwanted leading + sign. This simplifies the logic to construct
	# $cfg_str above; it can always add a leading delimiter rather than only doing
	# so unless the string is previously empty.
	echo "${cfg_str:1}" > "${cfg}/strings/0x409/configuration"

	if [ -e /sys/bus/usb/devices/usb2/power/control ]; then
		echo on > /sys/bus/usb/devices/usb2/power/control
	fi
	echo "${udc_dev}" > UDC

	# enable ncm for usb device mode
	/bin/ip link set dev "$(cat functions/ncm.usb0/ifname)" up
	/bin/ip a add fe80::1 dev "$(cat functions/ncm.usb0/ifname)"
	/bin/ip a add fc00:1:1:"${instance}"::2/64 dev "$(cat functions/ncm.usb0/ifname)"
	if [ -n "${boot_rootfs}" ]; then
		/bin/ip a add 192.168.55.1/24 dev "$(cat functions/ncm.usb0/ifname)"
	fi
	if [ -e /sys/class/usb_role/usb2-0-role-switch/role ]; then
		echo "device" > /sys/class/usb_role/usb2-0-role-switch/role
	fi
)


enable_remote_access()
(
	set -e
	echo "enable remote access"

	mkdir -p /var/run

	# Enable editing mmcblk0bootx
	if [ -f /sys/block/${INTERNAL_EMMCBOOT0_ONDEV}/force_ro ]; then
		echo 0 > /sys/block/${INTERNAL_EMMCBOOT0_ONDEV}/force_ro
	fi
	if [ -f /sys/block/${INTERNAL_EMMCBOOT1_ONDEV}/force_ro ]; then
		echo 0 > /sys/block/${INTERNAL_EMMCBOOT1_ONDEV}/force_ro
	fi

	modprobe -v phy_tegra194_p2u
	modprobe -v pcie_tegra194
	modprobe -v nvme
	modprobe -v tegra-bpmp-thermal
	modprobe -v pwm-tegra
	modprobe -v pwm-fan
	modprobe -v libcomposite
	modprobe -v typec
	modprobe -v ucsi-ccg
	modprobe -v tegra-xudc
	modprobe -v uas
	modprobe -v ipv6
	# Use nvethernet.ko (downstream) if present,
	# else use dwmac-tegra.ko (upstream)
	modprobe -v nvethernet || modprobe -v dwmac-tegra
	modprobe -v spi-tegra210-quad

	#optional modules
	set +e
	modprobe -v mods
	set -e

	# sleep to ensure the above modprobe completed registering with the kernel
	sleep 1

	if [ -f /initrd_flash.cfg ]; then
		external_device=""
		erase_all=""
		instance=""
		nfsnet=""
		targetip=""
		timeout=""
		gateway=""
		adb=""
		boot_rootfs=""
		initrd_only=""
		source /initrd_flash.cfg
		if [ -n "${external_device}" ]; then
			wait_for_external_device
		fi
		if [ "${nfsnet}" = "eth0" ]; then
			/bin/ip link set dev "${nfsnet}" up
			/bin/ip a add "${targetip}" dev "${nfsnet}"
			if [ -n "${gateway}" ]; then
				/bin/ip route add default via "${gateway}" dev "${nfsnet}"
			fi
		fi
	fi

	# find UDC device for usb device mode
	udc_dev=""
	known_udc_dev1=3550000.usb
	known_udc_dev2=a808670000.usb
	for _ in $(seq 5); do
		echo "Finding UDC"
		if [ -e "/sys/class/udc/${known_udc_dev1}" ]; then
			udc_dev="${known_udc_dev1}"
			break
		fi
		if [ -e "/sys/class/udc/${known_udc_dev2}" ]; then
			udc_dev="${known_udc_dev2}"
			break
		fi
		sleep 1
	done
	if [ "${udc_dev}" == "" ]; then
		echo No known UDC device found
		return 1
	fi

	if [ -n "${adb}" ]; then
		set_up_adb
		return $?
	fi
	set_up_usb_device_mode
	if [ -z "${boot_rootfs}" ] || [ -n "${initrd_only}" ]; then
		# Enable sshd
		local pts_dir="/dev/pts"
		if [ ! -d "${pts_dir}" ];then
			mkdir "${pts_dir}"
		fi
		mount "${pts_dir}"
		mkdir -p /run/sshd
		/bin/sshd -E /tmp/sshd.log
	fi
	return 0
)
