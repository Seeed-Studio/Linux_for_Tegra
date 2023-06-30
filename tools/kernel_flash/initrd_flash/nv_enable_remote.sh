#!/bin/bash

# Copyright (c) 2021-2022, NVIDIA CORPORATION.  All rights reserved.
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
			[ -b "${external_device}" ] && blkdiscard "${external_device}"
			set -e
		fi
	else
		echo "Connection timeout: device ${external_device} is still not ready."
 	fi;
}

function set_up_usb_device_mode()
(
	set -e
	modprobe -v qspi_mtd
	modprobe -v spi-tegra210-qspi
	modprobe -v pwm-fan
	if [ -f /initrd_flash.cfg ]; then
		external_device=""
		erase_all=""
		instance=""
		nfsnet=""
		targetip=""
		timeout=""
		gateway=""
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
	for _ in $(seq 60); do
		udc_dev_t210=700d0000.xudc
		if [ -e "/sys/class/udc/${udc_dev_t210}" ]; then
			udc_dev="${udc_dev_t210}"
			break
		fi
		udc_dev_t186=3550000.xudc
		if [ -e "/sys/class/udc/${udc_dev_t186}" ]; then
			udc_dev="${udc_dev_t186}"
			break
		fi
		sleep 1
	done
	if [ "${udc_dev}" == "" ]; then
		echo No known UDC device found
		return 1
	fi


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

	cfg_str="${cfg_str}+RNDIS+L4T${instance}"
	func=functions/rndis.usb0
	mkdir -p "${func}"
	ln -sf "${func}" "${cfg}"

	echo 1 > os_desc/use
	echo 0xcd > os_desc/b_vendor_code
	echo MSFT100 > os_desc/qw_sign
	echo RNDIS > "${func}/os_desc/interface.rndis/compatible_id"
	echo 5162001 > "${func}/os_desc/interface.rndis/sub_compatible_id"
	ln -sf "${cfg}" os_desc

	# Parse configuration. `instance` is used to differentiate different device
	if [ -f /initrd_flash.cfg ]; then
		if [ -n "${erase_all}" ]; then
			set +e
			[ -b /dev/mmcblk0 ] && blkdiscard /dev/mmcblk0
			[ -b /dev/mmcblk0boot0 ] && blkdiscard /dev/mmcblk0boot0
			[ -b /dev/mmcblk0boot1 ] && blkdiscard /dev/mmcblk0boot1
			set -e
		fi
	fi

	# Now we create a USB MSC interface. This interface contains upto 4 lun devices
	# The first three luns exposes internal emmc. The forth lun exposes the external
	# device
	# The format of the inquiry_string is
	# First 8 characters are flashing target storage device identifier.
	# The following table connects the identifier to the dev node
	# +------------+--------------------+
	# | identifier |           dev node |
	# +------------+--------------------+
	# | mmc0       |            mmcblk0 |
	# +------------+--------------------+
	# | mmc0b0     |      mmcblk0boot0  |
	# +------------+--------------------+
	# | mmc0b1     |      mmcblk0boot1  |
	# +------------+--------------------+
	# | ext0       | ${external_device} |
	# +------------+--------------------+

	# The last 16 characters are the target device identifier on flashing host
	cfg_str="${cfg_str}+UMS"
	func=functions/mass_storage.0
	mkdir -p "${func}"
	printf "%-8s%-16s" "mmc0" "${instance}" > "${func}/lun.0/inquiry_string"
	if [ -b /dev/mmcblk0 ]; then
		echo "/dev/mmcblk0" > "${func}/lun.0/file"
		echo "Add mmcblk0"
	fi

	mkdir -p "${func}/lun.1"
	printf "%-8s%-16s" "mmc0b0" "${instance}" > "${func}/lun.1/inquiry_string"
	if [ -b /dev/mmcblk0boot0 ]; then
		# for NX SD, mmcblk0boot0/1 is not present. So in that case, it will not
		# need to export it. For other devices, we don't need to fail here in
		# initrd and let the script on the flash side do the error handling.
		echo "/dev/mmcblk0boot0" > "${func}/lun.1/file"
		echo "Add mmcblk0boot0"
	fi

	mkdir -p "${func}/lun.2"
	printf "%-8s%-16s" "mmc0b1" "${instance}" > "${func}/lun.2/inquiry_string"
	if [ -b /dev/mmcblk0boot1 ]; then
		echo "/dev/mmcblk0boot1" > "${func}/lun.2/file"
		echo "Add mmcblk0boot1"
	fi

	if [ -n "${external_device}" ]; then
		mkdir -p "${func}/lun.3"
		printf "%-8s%-16s" "ext0" "${instance}" > "${func}/lun.3/inquiry_string"
		if [ -b "${external_device}" ]; then
			echo "${external_device}" > "${func}/lun.3/file"
			echo "Add ${external_device}"
		else
			echo "Connection timeout: device ${external_device} is still not ready."
 		fi;
	fi

	ln -sf "${func}" "${cfg}"

	mkdir -p "${cfg}/strings/0x409"
	# :1 in the variable expansion strips the first character from the value. This
	# removes the unwanted leading + sign. This simplifies the logic to construct
	# $cfg_str above; it can always add a leading delimiter rather than only doing
	# so unless the string is previously empty.
	echo "${cfg_str:1}" > "${cfg}/strings/0x409/configuration"

	echo "${udc_dev}" > UDC

	# enable rndis0 for usb device mode
	/bin/ip link set dev "$(cat functions/rndis.usb0/ifname)" up
	/bin/ip a add fe80::1 dev "$(cat functions/rndis.usb0/ifname)"
	/bin/ip a add fc00:1:1:"${instance}"::2/64 dev "$(cat functions/rndis.usb0/ifname)"
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
	if [ -f /sys/block/mmcblk0boot0/force_ro ]; then
		echo 0 > /sys/block/mmcblk0boot0/force_ro
	fi
	if [ -f /sys/block/mmcblk0boot1/force_ro ]; then
		echo 0 > /sys/block/mmcblk0boot1/force_ro
	fi

	set_up_usb_device_mode

	# Enable sshd
	local pts_dir="/dev/pts"
	if [ ! -d "${pts_dir}" ];then
		mkdir "${pts_dir}"
	fi
	mount "${pts_dir}"
	mkdir -p /run/sshd
	/bin/sshd -E /tmp/sshd.log
	return 0
)
