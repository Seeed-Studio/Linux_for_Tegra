#!/bin/bash

# One-shot Jetson L4T OTA upgrade script.
# Upgrades from r36.4.x to r36.5.0 via deb packages.
#
# Usage: sudo bash ota_upgrade.sh [DEB_DIR]
#   DEB_DIR  Directory containing the deb packages (default: ~/ota-debs)

set -e

DEB_DIR="${1:-$HOME/ota-debs}"
BOOT_CTRL_CONF="/etc/nv_boot_control.conf"
DISABLE_FLAG="/opt/nvidia/l4t-packages/.nv-l4t-disable-boot-fw-update-in-preinstall"
APT_OPTS="-o Dpkg::Options::='--force-confold'"

PACKAGES_TO_HOLD="nvidia-l4t-display-kernel nvidia-l4t-kernel nvidia-l4t-kernel-dtbs \
  nvidia-l4t-kernel-headers nvidia-l4t-kernel-oot-headers \
  nvidia-l4t-kernel-oot-modules nvidia-l4t-initrd"

fatal() {
	echo ""
	echo "========================================="
	echo "FATAL: $1"
	echo "========================================="
	exit 1
}

step() {
	echo ""
	echo "========================================="
	echo "[STEP $1/10] $2"
	echo "========================================="
}

# ---- Precondition checks ----

if [ "$(id -u)" -ne 0 ]; then
	fatal "Run as root: sudo bash $0 $*"
fi

if [ ! -d "${DEB_DIR}" ] || [ -z "$(ls ${DEB_DIR}/*.deb 2>/dev/null)" ]; then
	fatal "No deb packages found in ${DEB_DIR}"
fi

if [ ! -f "${BOOT_CTRL_CONF}" ]; then
	fatal "${BOOT_CTRL_CONF} not found. Is this a Jetson device?"
fi

ORIG_TNSPEC=$(awk '/^TNSPEC/ {print $2}' "${BOOT_CTRL_CONF}")
echo "Board TNSPEC: ${ORIG_TNSPEC}"

# Map board to NVIDIA devkit for UEFI capsule matching
DEVKIT=""
case "${ORIG_TNSPEC}" in
	*recomputer-orin-j401-*|*recomputer-orin-nano-*|*recomputer-orin-j101-*)
		DEVKIT="jetson-orin-nano-devkit"
		;;
	*recomputer-industrial-*|*reserver-*|*recomputer-a30*)
		DEVKIT="jetson-agx-orin-devkit"
		;;
	*)
		fatal "Unknown board in TNSPEC: ${ORIG_TNSPEC}"
		;;
esac
echo "UEFI devkit mapping: ${DEVKIT}"

echo ""
echo "Deb packages:"
ls -lh "${DEB_DIR}"/*.deb

echo ""
echo "========================================="
echo "Jetson L4T OTA Upgrade"
echo "========================================="
echo "Deb dir:  ${DEB_DIR}"
echo "Board:    ${ORIG_TNSPEC}"
echo "Devkit:   ${DEVKIT}"
echo ""
read -p "Press Enter to start upgrade, or Ctrl+C to cancel..."

# ---- Step 1: Unlock held packages ----

step 1 "Unlocking held packages"
apt-mark unhold ${PACKAGES_TO_HOLD} || true

# ---- Step 2: Disable boot-fw partition writes ----

step 2 "Disabling boot-fw partition writes"
mkdir -p /opt/nvidia/l4t-packages
touch "${DISABLE_FLAG}"

# ---- Step 3: Switch apt source ----

step 3 "Switching apt source to r36.5"
sed -i 's/r36\.4/r36.5/g' /etc/apt/sources.list.d/nvidia-l4t-apt-source.list
apt-get update

# ---- Step 4: Install deb packages ----

step 4 "Installing deb packages"
dpkg -i --force-overwrite --force-depends "${DEB_DIR}"/*.deb

# ---- Step 5: Fix dependencies and upgrade userspace ----

step 5 "Upgrading userspace packages (this may take several minutes)"
apt-get ${APT_OPTS} --fix-broken install -y --allow-change-held-packages

# ---- Step 6: Dist-upgrade remaining packages ----

step 6 "Dist-upgrading remaining packages"
apt-get ${APT_OPTS} dist-upgrade -y --allow-change-held-packages

# ---- Step 7: Update UEFI firmware ----

step 7 "Updating UEFI firmware"
cp "${BOOT_CTRL_CONF}" "${BOOT_CTRL_CONF}.bak"
rm -f "${DISABLE_FLAG}"

# Swap TNSPEC to devkit
sed -i "s/recomputer-orin-j401/${DEVKIT}/g; s/recomputer-orin-nano/${DEVKIT}/g; \
  s/recomputer-orin-j101/${DEVKIT}/g; s/recomputer-industrial/${DEVKIT}/g; \
  s/reserver[a-z0-9-]*/${DEVKIT}/g; s/recomputer-a30/${DEVKIT}/g" "${BOOT_CTRL_CONF}"

echo "  TNSPEC -> $(awk '/^TNSPEC/ {print $2}' "${BOOT_CTRL_CONF}")"
apt-get install --reinstall -y --allow-downgrades nvidia-l4t-bootloader

# Restore original TNSPEC
cp "${BOOT_CTRL_CONF}.bak" "${BOOT_CTRL_CONF}"
rm -f "${BOOT_CTRL_CONF}.bak"
echo "  TNSPEC restored: ${ORIG_TNSPEC}"

# ---- Step 8: Install JetPack (optional) ----

step 8 "Installing JetPack full package"
apt-get install -y --allow-change-held-packages nvidia-jetpack || \
	echo "  WARNING: JetPack install failed, continuing..."

# ---- Step 9: Re-lock packages ----

step 9 "Re-locking packages"
apt-mark hold ${PACKAGES_TO_HOLD}

# ---- Step 10: Summary ----

step 10 "Upgrade complete"
echo ""
echo "Kernel:  $(uname -r)"
echo "L4T:     $(awk '/REVISION/ {print $2}' /etc/nv_tegra_release 2>/dev/null || echo 'check after reboot')"
echo "JetPack: $(dpkg -l nvidia-jetpack 2>/dev/null | awk '/^ii/ {print $3}' || echo 'check after reboot')"
echo ""
echo "========================================="
echo "UEFI firmware update is pending."
echo "Reboot now to apply all changes."
echo "========================================="
echo ""
read -p "Reboot now? [Y/n] " REPLY
if [ -z "${REPLY}" ] || [ "${REPLY}" = "Y" ] || [ "${REPLY}" = "y" ]; then
	reboot
else
	echo "Reboot manually when ready: sudo reboot"
fi
