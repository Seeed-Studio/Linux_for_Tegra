#!/bin/bash

# UEFI firmware update helper for Jetson devices.
# Temporarily changes TNSPEC to match NVIDIA devkit so the official
# nvidia-l4t-bootloader postinst can select and apply the correct capsule.
# Restores original board identity after capsule is queued.
#
# Usage: sudo ./update_uefi.sh

set -e

BOOT_CTRL_CONF="/etc/nv_boot_control.conf"
DISABLE_FLAG="/opt/nvidia/l4t-packages/.nv-l4t-disable-boot-fw-update-in-preinstall"

if [ "$(id -u)" -ne 0 ]; then
    echo "Error: Run as root (sudo)"
    exit 1
fi

if [ ! -f "${BOOT_CTRL_CONF}" ]; then
    echo "Error: ${BOOT_CTRL_CONF} not found"
    exit 1
fi

# Read current TNSPEC
ORIG_TNSPEC=$(awk '/^TNSPEC/ {print $2}' "${BOOT_CTRL_CONF}")
echo "Current TNSPEC: ${ORIG_TNSPEC}"

# Map Seeed board to NVIDIA devkit for capsule matching
DEVKIT=""
case "${ORIG_TNSPEC}" in
    *recomputer-orin-j401-*|*recomputer-orin-nano-*|*recomputer-orin-j101-*)
        DEVKIT="jetson-orin-nano-devkit"
        ;;
    *recomputer-industrial-*|*reserver-*|*recomputer-a30*)
        DEVKIT="jetson-agx-orin-devkit"
        ;;
    *)
        echo "Error: Unknown board in TNSPEC, cannot map to devkit"
        exit 1
        ;;
esac
echo "Mapping to devkit: ${DEVKIT}"

# Backup
cp "${BOOT_CTRL_CONF}" "${BOOT_CTRL_CONF}.bak"

# Remove disable flag so bootloader postinst can run
rm -f "${DISABLE_FLAG}"

# Replace Seeed board name with devkit name in TNSPEC
# e.g. recomputer-orin-j401 -> jetson-orin-nano-devkit
sed -i "s/recomputer-orin-j401/${DEVKIT}/g; s/recomputer-orin-nano/${DEVKIT}/g; s/recomputer-orin-j101/${DEVKIT}/g; s/recomputer-industrial/${DEVKIT}/g; s/reserver[a-z0-9-]*/${DEVKIT}/g; s/recomputer-a30/${DEVKIT}/g" "${BOOT_CTRL_CONF}"
echo "Modified TNSPEC: $(awk '/^TNSPEC/ {print $2}' "${BOOT_CTRL_CONF}")"

# Reinstall official bootloader package (triggers capsule update)
echo "Reinstalling official nvidia-l4t-bootloader..."
if apt-get install --reinstall -y --allow-downgrades nvidia-l4t-bootloader; then
    echo "Capsule update queued successfully."
else
    echo "WARNING: Bootloader reinstall failed. Check TNSPEC mapping."
fi

# Restore original config
cp "${BOOT_CTRL_CONF}.bak" "${BOOT_CTRL_CONF}"
rm -f "${BOOT_CTRL_CONF}.bak"
echo "Restored original TNSPEC: ${ORIG_TNSPEC}"

echo ""
echo "Reboot to apply UEFI firmware update."
echo "After reboot, verify with: nvbootctrl dump-slots-info"
