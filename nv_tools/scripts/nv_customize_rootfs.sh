#!/bin/bash

# Copyright (c) 2020-2022, NVIDIA CORPORATION. All rights reserved.
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

# This script contains function to apply NVIDIA customization to root
# file system
#

# $1 - Path to rootfs
function nv_customize_rootfs {
	LDK_ROOTFS_DIR="${1}"
	if [ ! -d "${LDK_ROOTFS_DIR}" ]; then
		echo "Error: ${LDK_ROOTFS_DIR} does not exist!"
		exit 1
	fi
	ARM_ABI_DIR=

	if [ -d "${LDK_ROOTFS_DIR}/usr/lib/arm-linux-gnueabihf/tegra" ]; then
		ARM_ABI_DIR_ABS="usr/lib/arm-linux-gnueabihf"
	elif [ -d "${LDK_ROOTFS_DIR}/usr/lib/arm-linux-gnueabi/tegra" ]; then
		ARM_ABI_DIR_ABS="usr/lib/arm-linux-gnueabi"
	elif [ -d "${LDK_ROOTFS_DIR}/usr/lib/aarch64-linux-gnu/tegra" ]; then
		ARM_ABI_DIR_ABS="usr/lib/aarch64-linux-gnu"
	else
		echo "Error: None of Hardfp/Softfp Tegra libs found"
		exit 4
	fi

	ARM_ABI_DIR="${LDK_ROOTFS_DIR}/${ARM_ABI_DIR_ABS}"
	ARM_ABI_TEGRA_DIR="${ARM_ABI_DIR}/tegra"
	install ${INSTALL_ROOT_OPTS} -m 0755 -d \
		"${LDK_ROOTFS_DIR}/etc/systemd/system/multi-user.target.wants"
	pushd "${LDK_ROOTFS_DIR}/etc/systemd/system/multi-user.target.wants" \
		> /dev/null 2>&1
	if [ -h "isc-dhcp-server.service" ]; then
		rm -f "isc-dhcp-server.service"
	fi
	if [ -h "isc-dhcp-server6.service" ]; then
		rm -f "isc-dhcp-server6.service"
	fi
	popd > /dev/null

	# Enable Unity by default for better user experience [2332219]
	if [ -d "${LDK_ROOTFS_DIR}/usr/share/xsessions" ]; then
		pushd "${LDK_ROOTFS_DIR}/usr/share/xsessions" > /dev/null 2>&1
		if [ -f "ubuntu.desktop" ] && [ -f "unity.desktop" ]; then
			echo "Rename ubuntu.desktop --> ux-ubuntu.desktop"
			mv "ubuntu.desktop" "ux-ubuntu.desktop"
		fi
		if [ -f "openbox.desktop" ]; then
			mv "openbox.desktop" "ux-openbox.desktop"
		fi
		if [ -f "LXDE.desktop" ]; then
			mv "LXDE.desktop" "ux-LXDE.desktop"
		fi
		popd > /dev/null
	fi

	# gdm3 is not starting the wayland-session unless we
	# create a softlink to ubuntu-wayland.desktop [200781472]
	if [ -d "${LDK_ROOTFS_DIR}/usr/share/wayland-sessions" ]; then
		pushd "${LDK_ROOTFS_DIR}/usr/share/wayland-sessions" > /dev/null 2>&1
		if [ ! -f "ubuntu.desktop" ] && [ -f "ubuntu-wayland.desktop" ]; then
			ln -s ubuntu-wayland.desktop ubuntu.desktop
		fi
		popd > /dev/null
	fi

	if [ -e "${LDK_ROOTFS_DIR}/usr/share/lightdm/lightdm.conf.d/50-ubuntu.conf" ] ; then
		grep -q -F 'allow-guest=false' \
			"${LDK_ROOTFS_DIR}/usr/share/lightdm/lightdm.conf.d/50-ubuntu.conf" \
			|| echo 'allow-guest=false' \
			>> "${LDK_ROOTFS_DIR}/usr/share/lightdm/lightdm.conf.d/50-ubuntu.conf"
	fi

	# Disabling NetworkManager-wait-online.service for Bug 200290321
	echo "Disabling NetworkManager-wait-online.service"
	if [ -h "${LDK_ROOTFS_DIR}/etc/systemd/system/network-online.target.wants/NetworkManager-wait-online.service" ]; then
		rm "${LDK_ROOTFS_DIR}/etc/systemd/system/network-online.target.wants/NetworkManager-wait-online.service"
	fi

	echo "Disable the ondemand service by changing the runlevels to 'K'"
	for file in "${LDK_ROOTFS_DIR}"/etc/rc[0-9].d/; do
		if [ -f "${file}"/S*ondemand ]; then
			mv "${file}"/S*ondemand "${file}/K01ondemand"
		fi
	done

	# Remove the spawning of ondemand service
	if [ -h "${LDK_ROOTFS_DIR}/etc/systemd/system/multi-user.target.wants/ondemand.service" ]; then
		rm -f "${LDK_ROOTFS_DIR}/etc/systemd/system/multi-user.target.wants/ondemand.service"
	fi

	# If default target does not exist and if rootfs contains gdm, set default to nv-oem-config target
	if [ ! -e "${LDK_ROOTFS_DIR}/etc/systemd/system/default.target" ] && \
	   [ -d "${LDK_ROOTFS_DIR}/etc/gdm3/" ]; then
		mkdir -p "${LDK_ROOTFS_DIR}/etc/systemd/system/nv-oem-config.target.wants"
		pushd "${LDK_ROOTFS_DIR}/etc/systemd/system/nv-oem-config.target.wants" \
			> /dev/null 2>&1
		ln -sf "/lib/systemd/system/nv-oem-config.service" \
			"nv-oem-config.service"
		ln -sf "/etc/systemd/system/nvfb-early.service" \
			"nvfb-early.service"
		ln -sf "/etc/systemd/system/nvpower.service" \
			"nvpower.service"
		ln -sf "/etc/systemd/system/nvfancontrol.service" \
			"nvfancontrol.service"
		ln -sf "/etc/systemd/system/nvpmodel.service" \
			"nvpmodel.service"
		popd > /dev/null 2>&1
		pushd "${LDK_ROOTFS_DIR}/etc/systemd/system" > /dev/null 2>&1
		ln -sf /lib/systemd/system/nv-oem-config.target \
			nv-oem-config.target
		ln -sf nv-oem-config.target default.target
		popd > /dev/null 2>&1

		extra_groups="EXTRA_GROUPS=\"audio gdm gpio i2c lightdm render video weston-launch\""
		sed -i "/\<EXTRA_GROUPS\>=/ s/^.*/${extra_groups}/" \
			"${LDK_ROOTFS_DIR}/etc/adduser.conf"
		sed -i "/\<ADD_EXTRA_GROUPS\>=/ s/^.*/ADD_EXTRA_GROUPS=1/" \
			"${LDK_ROOTFS_DIR}/etc/adduser.conf"
	fi

	if [ -e "${LDK_ROOTFS_DIR}/etc/gdm3/custom.conf" ]; then
		sed -i "/WaylandEnable=false/ s/^#//" \
			"${LDK_ROOTFS_DIR}/etc/gdm3/custom.conf"
	fi

	# Disable unattended upgrade
	if [ -e "${LDK_ROOTFS_DIR}/etc/apt/apt.conf.d/20auto-upgrades" ]; then
		sed -i "s/Unattended-Upgrade \"1\"/Unattended-Upgrade \"0\"/" \
			"${LDK_ROOTFS_DIR}/etc/apt/apt.conf.d/20auto-upgrades"
	fi

	# Disable release upgrade
	if [ -e "${LDK_ROOTFS_DIR}/etc/update-motd.d/91-release-upgrade" ]; then
		rm -f "${LDK_ROOTFS_DIR}/etc/update-motd.d/91-release-upgrade"
	fi
	if [ -e "${LDK_ROOTFS_DIR}/etc/update-manager/release-upgrades" ]; then
		sed -i "s/Prompt=lts/Prompt=never/" \
			"${LDK_ROOTFS_DIR}/etc/update-manager/release-upgrades"
	fi

	# Set LXDE as default LightDM user session
	if [ -e "${LDK_ROOTFS_DIR}/etc/lightdm/lightdm.conf.d/50-nvidia.conf" ] && \
		[ -e "${LDK_ROOTFS_DIR}/usr/share/xsessions/ux-LXDE.desktop" ]; then
		grep -q -F 'user-session=ux-LXDE' \
			"${LDK_ROOTFS_DIR}/etc/lightdm/lightdm.conf.d/50-nvidia.conf" \
			|| sed -i '1 auser-session=ux-LXDE' \
			"${LDK_ROOTFS_DIR}/etc/lightdm/lightdm.conf.d/50-nvidia.conf"
	fi

	# Set lightdm-gtk-greeter as default login greeter for LightDM
	if [ -e "${LDK_ROOTFS_DIR}/etc/lightdm/lightdm.conf.d/50-nvidia.conf" ] && \
		[ -e "${LDK_ROOTFS_DIR}/usr/sbin/lightdm-gtk-greeter" ]; then
		grep -q -F 'greeter-session=lightdm-gtk-greeter' \
			"${LDK_ROOTFS_DIR}/etc/lightdm/lightdm.conf.d/50-nvidia.conf" \
			|| sed -i '1 agreeter-session=lightdm-gtk-greeter' \
			"${LDK_ROOTFS_DIR}/etc/lightdm/lightdm.conf.d/50-nvidia.conf"
	fi

	# Set XScreensaver default mode as blank
	if [ -e "${LDK_ROOTFS_DIR}/etc/X11/app-defaults/XScreenSaver" ]; then
		sed -i "s/random/blank/" \
			"$(readlink -f "${LDK_ROOTFS_DIR}/etc/X11/app-defaults/XScreenSaver")"
	fi

	# Skip Livepatch setting in gnome-initial-setup
	if [ -e "${LDK_ROOTFS_DIR}/usr/lib/gnome-initial-setup/vendor.conf" ]; then
		sed -i "s/language;/language;livepatch;/" \
			"${LDK_ROOTFS_DIR}/usr/lib/gnome-initial-setup/vendor.conf"
	fi
}

nv_customize_rootfs "${1}"
