#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2020-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

	# Disabling NetworkManager-wait-online.service for Bug 200290321
	echo "Disabling NetworkManager-wait-online.service"
	if [ -h "${LDK_ROOTFS_DIR}/etc/systemd/system/network-online.target.wants/NetworkManager-wait-online.service" ]; then
		rm "${LDK_ROOTFS_DIR}/etc/systemd/system/network-online.target.wants/NetworkManager-wait-online.service"
		ln -sf "/dev/null" "${LDK_ROOTFS_DIR}/etc/systemd/system/NetworkManager-wait-online.service"
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

	# Don't create default user via cloud-init
	if [ -e "${LDK_ROOTFS_DIR}/etc/cloud/cloud.cfg" ]; then
		sed -i -e "/users:/s/^/#/" -e "/- default/s/^/#/" \
			"${LDK_ROOTFS_DIR}/etc/cloud/cloud.cfg"
	fi

	# Use NetworkManager instead of netplan
	if [ -e "${LDK_ROOTFS_DIR}/lib/systemd/system/NetworkManager.service" ] && \
	   [ -d "${LDK_ROOTFS_DIR}/etc/netplan" ]; then
		cat > "${LDK_ROOTFS_DIR}/etc/netplan/01-network-manager-all.yaml" << ENDOFHERE
# Let NetworkManager manage all devices on this system
network:
  version: 2
  renderer: NetworkManager
ENDOFHERE
	fi

	if [ -d "${LDK_ROOTFS_DIR}/etc/cloud/cloud.cfg.d" ]; then
		echo "network: {config: disabled}" > \
			"${LDK_ROOTFS_DIR}/etc/cloud/cloud.cfg.d/99-disable-cloudinit-network.cfg"
	fi

	# If default target does not exist and if rootfs contains gdm, set default to nv-oobe target
	if [ ! -e "${LDK_ROOTFS_DIR}/etc/systemd/system/default.target" ] && \
	   [ -d "${LDK_ROOTFS_DIR}/etc/gdm3/" ]; then
		mkdir -p "${LDK_ROOTFS_DIR}/etc/systemd/system/nv-oobe.target.wants"
		pushd "${LDK_ROOTFS_DIR}/etc/systemd/system/nv-oobe.target.wants" \
			> /dev/null 2>&1
		ln -sf "/lib/systemd/system/nv-oobe.service" \
			"nv-oobe.service"
		ln -sf "/etc/systemd/system/nvpower.service" \
			"nvpower.service"
		ln -sf "/etc/systemd/system/nvfancontrol.service" \
			"nvfancontrol.service"
		ln -sf "/etc/systemd/system/nvpmodel.service" \
			"nvpmodel.service"
		popd > /dev/null 2>&1
		pushd "${LDK_ROOTFS_DIR}/etc/systemd/system" > /dev/null 2>&1
		ln -sf /lib/systemd/system/nv-oobe.target \
			nv-oobe.target
		ln -sf nv-oobe.target default.target
		popd > /dev/null 2>&1

		extra_groups="EXTRA_GROUPS=\"audio gdm gpio i2c render video weston-launch\""
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

	# Do not access pam_lastlog.so because the file was removed from Ubuntu 24.04
	if [ -e "${LDK_ROOTFS_DIR}/etc/pam.d/login" ]; then
		sed -i "/pam_lastlog.so/ s/^/#/" "${LDK_ROOTFS_DIR}/etc/pam.d/login"
	fi

	# Disable systemd-sysupdate.timer and systemd-sysupdate-reboot.timer
	if [ -e "${LDK_ROOTFS_DIR}/usr/lib/systemd/system/systemd-sysupdate.timer" ]; then
		ln -sf "/dev/null" "${LDK_ROOTFS_DIR}/etc/systemd/system/systemd-sysupdate.timer"
		ln -sf "/dev/null" "${LDK_ROOTFS_DIR}/etc/systemd/system/systemd-sysupdate-reboot.timer"
	fi

	# Disable dnsmasq.service because the default port conflicts with systemd-resolved.service
	if [ -h "${LDK_ROOTFS_DIR}/etc/systemd/system/multi-user.target.wants/dnsmasq.service" ]; then
		rm -f "${LDK_ROOTFS_DIR}/etc/systemd/system/multi-user.target.wants/dnsmasq.service"
	fi
}

nv_customize_rootfs "${1}"
