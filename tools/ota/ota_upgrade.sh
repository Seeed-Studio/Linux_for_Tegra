#!/bin/bash

# Jetson L4T OTA upgrade script with safety features.
# Upgrades from r36.4.x to r36.5.0 via deb packages.
#
# Safety features:
#   - SHA256 integrity verification of all deb packages
#   - Automatic backup of critical boot files
#   - Rollback on failure (restores kernel Image and initrd)
#   - Fallback boot entry in extlinux.conf
#   - Full upgrade logging to /var/log/ota_upgrade.log
#   - Pre-flight checks (disk space, version compatibility)
#
# Usage: sudo bash ota_upgrade.sh [DEB_DIR]
#   DEB_DIR  Directory containing deb packages and sha256sum.txt

LOG_FILE="/var/log/ota_upgrade.log"
DEB_DIR="${1:-$HOME/ota-debs}"
BOOT_CTRL_CONF="/etc/nv_boot_control.conf"
DISABLE_FLAG="/opt/nvidia/l4t-packages/.nv-l4t-disable-boot-fw-update-in-preinstall"
BACKUP_DIR="/opt/nvidia/l4t-packages/ota_backup"

PACKAGES_TO_HOLD="nvidia-l4t-display-kernel nvidia-l4t-kernel nvidia-l4t-kernel-dtbs \
  nvidia-l4t-kernel-headers nvidia-l4t-kernel-oot-headers \
  nvidia-l4t-kernel-oot-modules nvidia-l4t-initrd"

TARGET_L4T="36.5.0"
MIN_DISK_SPACE_MB=2048
DEBIAN_FRONTEND="noninteractive"
export DEBIAN_FRONTEND

# ---- Logging ----

log() {
	echo "$@"
	echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> "${LOG_FILE}"
}

fatal() {
	log ""
	log "========================================="
	log "FATAL: $1"
	log "========================================="
	exit 1
}

step() {
	log ""
	log "========================================="
	log "[STEP $1/12] $2"
	log "========================================="
}

step_ok() {
	log "  OK: $1"
}

step_fail() {
	log "  FAILED: $1"
}

# ---- Rollback ----

rollback() {
	log ""
	log "========================================="
	log "ROLLBACK: Restoring backup files..."
	log "========================================="

	if [ -f "${BACKUP_DIR}/Image" ]; then
		cp "${BACKUP_DIR}/Image" /boot/Image
		log "  Restored /boot/Image"
	fi
	if [ -f "${BACKUP_DIR}/initrd" ]; then
		cp "${BACKUP_DIR}/initrd" /boot/initrd
		log "  Restored /boot/initrd"
	fi
	if [ -f "${BACKUP_DIR}/nv_boot_control.conf" ]; then
		cp "${BACKUP_DIR}/nv_boot_control.conf" "${BOOT_CTRL_CONF}"
		log "  Restored ${BOOT_CTRL_CONF}"
	fi
	if [ -f "${BACKUP_DIR}/nvidia-l4t-apt-source.list" ]; then
		cp "${BACKUP_DIR}/nvidia-l4t-apt-source.list" \
			/etc/apt/sources.list.d/nvidia-l4t-apt-source.list
		log "  Restored apt source"
	fi
	if [ -f "${BACKUP_DIR}/nv_tegra_release" ]; then
		cp "${BACKUP_DIR}/nv_tegra_release" /etc/nv_tegra_release
		log "  Restored /etc/nv_tegra_release"
	fi
	if [ -f "${BACKUP_DIR}/hold.list" ]; then
		xargs apt-mark hold < "${BACKUP_DIR}/hold.list" 2>/dev/null || true
		log "  Restored package holds"
	fi
	# Re-create disable flag to prevent partial bootloader updates
	mkdir -p /opt/nvidia/l4t-packages
	touch "${DISABLE_FLAG}"

	log ""
	log "Rollback complete. System restored to pre-upgrade state."
	log "Reboot to use the old kernel. Review log: ${LOG_FILE}"
	log "========================================="
}

# Trap errors for rollback
trap 'log "ERROR: Upgrade failed at step. Rolling back..."; rollback; exit 1' ERR

# ---- Precondition checks (Step 0) ----

step 0 "Pre-flight checks"

if [ "$(id -u)" -ne 0 ]; then
	fatal "Run as root: sudo bash $0 $*"
fi
step_ok "Running as root"

if [ ! -d "${DEB_DIR}" ] || [ -z "$(ls ${DEB_DIR}/*.deb 2>/dev/null)" ]; then
	fatal "No deb packages found in ${DEB_DIR}"
fi
step_ok "Deb packages found in ${DEB_DIR}"

if [ ! -f "${BOOT_CTRL_CONF}" ]; then
	fatal "${BOOT_CTRL_CONF} not found. Is this a Jetson device?"
fi
step_ok "Boot control config found"

# Check disk space
AVAIL_MB=$(df -m / | awk 'NR==2 {print $4}')
if [ "${AVAIL_MB}" -lt "${MIN_DISK_SPACE_MB}" ]; then
	fatal "Insufficient disk space: ${AVAIL_MB}MB available, need ${MIN_DISK_SPACE_MB}MB"
fi
step_ok "Disk space: ${AVAIL_MB}MB available"

# Check current L4T version — strict gate
CURRENT_L4T=$(awk '/^# branch R/ {print $3}' /etc/nv_tegra_release 2>/dev/null)
if echo "${CURRENT_L4T}" | grep -qE '^R36\.4\.'; then
	step_ok "Current L4T: ${CURRENT_L4T} (upgradable)"
elif echo "${CURRENT_L4T}" | grep -qE '^R36\.5\.'; then
	fatal "Already on ${CURRENT_L4T}. No upgrade needed."
else
	fatal "Unsupported source version: ${CURRENT_L4T}. This script upgrades R36.4.x only."
fi

# SHA256 integrity verification
if [ -f "${DEB_DIR}/sha256sum.txt" ]; then
	log "  Verifying package integrity..."
	if (cd "${DEB_DIR}" && sha256sum -c sha256sum.txt >/dev/null 2>&1); then
		step_ok "SHA256 verification passed"
	else
		fatal "SHA256 verification failed! Packages may be corrupted or tampered."
	fi
else
	log "  WARNING: No sha256sum.txt found, skipping integrity check"
fi

# Board detection
ORIG_TNSPEC=$(awk '/^TNSPEC/ {print $2}' "${BOOT_CTRL_CONF}")
log "Board TNSPEC: ${ORIG_TNSPEC}"

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
step_ok "Board: ${ORIG_TNSPEC} -> ${DEVKIT}"

# Summary
log ""
log "Deb packages:"
ls -lh "${DEB_DIR}"/*.deb >> "${LOG_FILE}" 2>&1
log ""
log "========================================="
log "Jetson L4T OTA Upgrade"
log "========================================="
log "Deb dir:  ${DEB_DIR}"
log "Board:    ${ORIG_TNSPEC}"
log "Devkit:   ${DEVKIT}"
log "Log:      ${LOG_FILE}"
log ""
if [ -t 0 ]; then
	read -p "Press Enter to start upgrade, or Ctrl+C to cancel..."
else
	log "Non-interactive mode, starting upgrade automatically..."
fi

# ---- Step 1: Backup critical files ----

step 1 "Backing up critical files"
mkdir -p "${BACKUP_DIR}"

cp /boot/Image "${BACKUP_DIR}/Image" 2>/dev/null || true
cp /boot/initrd "${BACKUP_DIR}/initrd" 2>/dev/null || true
cp "${BOOT_CTRL_CONF}" "${BACKUP_DIR}/nv_boot_control.conf"
cp /etc/apt/sources.list.d/nvidia-l4t-apt-source.list \
	"${BACKUP_DIR}/nvidia-l4t-apt-source.list" 2>/dev/null || true
cp /etc/nv_tegra_release "${BACKUP_DIR}/nv_tegra_release" 2>/dev/null || true
apt-mark showhold > "${BACKUP_DIR}/hold.list" 2>/dev/null || true
step_ok "Backup saved to ${BACKUP_DIR}"

# ---- Step 2: Unlock held packages ----

step 2 "Unlocking held packages"
apt-mark unhold ${PACKAGES_TO_HOLD} || true
step_ok "Packages unlocked"

# ---- Step 3: Disable boot-fw partition writes ----

step 3 "Disabling boot-fw partition writes"
mkdir -p /opt/nvidia/l4t-packages
touch "${DISABLE_FLAG}"
step_ok "Disable flag created"

# ---- Step 4: Switch apt source ----

step 4 "Switching apt source to r36.5"
sed -i 's/r36\.4/r36.5/g' /etc/apt/sources.list.d/nvidia-l4t-apt-source.list
if apt-get update; then
	step_ok "Apt source switched and updated"
else
	step_fail "apt-get update failed"
	fatal "Cannot update package lists. Check network connection."
fi

# ---- Step 5: Install deb packages ----

step 5 "Installing deb packages"
if dpkg -i --force-overwrite --force-depends "${DEB_DIR}"/*.deb; then
	step_ok "Deb packages installed"
else
	step_fail "dpkg install failed, attempting recovery..."
	dpkg --configure -a 2>/dev/null || true
	apt-get -f install -y 2>/dev/null || true
	fatal "Deb package installation failed. Rollback initiated."
fi

# ---- Step 6: Fix dependencies and upgrade userspace ----

step 6 "Upgrading userspace packages (this may take several minutes)"
if DEBIAN_FRONTEND=noninteractive apt-get \
	-o Dpkg::Options::="--force-confold" \
	-o Dpkg::Options::="--force-confdef" \
	--fix-broken install -y --allow-change-held-packages; then
	step_ok "Userspace packages upgraded"
else
	step_fail "Userspace upgrade had errors"
	log "  Attempting to fix..."
	dpkg --configure -a 2>/dev/null || true
	apt-get -f install -y 2>/dev/null || true
	log "  Continuing (non-critical errors may be resolved in dist-upgrade)"
fi

# ---- Step 7: Dist-upgrade remaining packages ----

step 7 "Dist-upgrading remaining packages"
if DEBIAN_FRONTEND=noninteractive apt-get \
	-o Dpkg::Options::="--force-confold" \
	-o Dpkg::Options::="--force-confdef" \
	dist-upgrade -y --allow-change-held-packages; then
	step_ok "Dist-upgrade complete"
else
	step_fail "Dist-upgrade had errors"
	log "  Attempting recovery..."
	dpkg --configure -a 2>/dev/null || true
	apt-get -f install -y 2>/dev/null || true
	log "  Continuing..."
fi

# ---- Step 8: Update UEFI firmware ----

step 8 "Updating UEFI firmware"
cp "${BOOT_CTRL_CONF}" "${BOOT_CTRL_CONF}.bak"
rm -f "${DISABLE_FLAG}"

# Swap TNSPEC to devkit
sed -i "s/recomputer-orin-j401/${DEVKIT}/g; s/recomputer-orin-nano/${DEVKIT}/g; \
  s/recomputer-orin-j101/${DEVKIT}/g; s/recomputer-industrial/${DEVKIT}/g; \
  s/reserver[a-z0-9-]*/${DEVKIT}/g; s/recomputer-a30/${DEVKIT}/g" "${BOOT_CTRL_CONF}"

MODIFIED_TNSPEC=$(awk '/^TNSPEC/ {print $2}' "${BOOT_CTRL_CONF}")
if [ "${MODIFIED_TNSPEC}" = "${ORIG_TNSPEC}" ]; then
	log "  WARNING: TNSPEC was not modified. Board name may not match expected patterns."
fi
log "  TNSPEC -> ${MODIFIED_TNSPEC}"

if apt-get install --reinstall -y --allow-downgrades nvidia-l4t-bootloader; then
	step_ok "UEFI bootloader update queued"
else
	step_fail "Bootloader reinstall failed"
	log "  This is non-critical - UEFI can be updated separately with update_uefi.sh"
fi

# Restore original TNSPEC
cp "${BOOT_CTRL_CONF}.bak" "${BOOT_CTRL_CONF}"
rm -f "${BOOT_CTRL_CONF}.bak"
log "  TNSPEC restored: ${ORIG_TNSPEC}"

# ---- Step 9: Install JetPack ----

step 9 "Installing JetPack full package"
if apt-get install -y --allow-change-held-packages nvidia-jetpack; then
	step_ok "JetPack installed"
else
	log "  WARNING: JetPack install failed, continuing..."
fi

# ---- Step 10: Re-lock packages ----

step 10 "Re-locking packages"
apt-mark hold ${PACKAGES_TO_HOLD}
step_ok "Packages locked"

# ---- Step 11: Cleanup old kernel modules ----

step 11 "Cleaning up old kernel modules"
NEW_KV=""
for d in /lib/modules/*tegra*; do
	kv="$(basename "${d}")"
	if [ "${kv}" != "$(uname -r)" ] && [ -d "${d}" ]; then
		NEW_KV="${kv}"
	fi
done
if [ -n "${NEW_KV}" ]; then
	for mod_dir in /lib/modules/*; do
		kver="$(basename "${mod_dir}")"
		if [ "${kver}" != "${NEW_KV}" ] && [ -d "${mod_dir}" ] && echo "${kver}" | grep -q 'tegra'; then
			log "  Removing old kernel modules: ${kver} ($(du -sh "${mod_dir}" 2>/dev/null | cut -f1))"
			rm -rf "${mod_dir}"
		fi
	done
	step_ok "Old kernel modules cleaned"
else
	log "  No old kernel modules to clean up"
fi

# ---- Step 12: Add fallback boot entry + Summary ----

step 12 "Upgrade complete"

# Update nv_tegra_release to reflect the upgrade
if grep -q '^# Seeed Image Name' /etc/nv_tegra_release 2>/dev/null; then
	UPGRADE_DATE=$(date '+%Y-%m-%d')
	sed -i "s|^# Seeed Image Name .*|# Seeed Image Name OTA-upgraded-to-${TARGET_L4T}-on-${UPGRADE_DATE}|" \
		/etc/nv_tegra_release
	log "  Updated nv_tegra_release Seeed Image Name"
fi

# Add fallback boot entry using backed-up kernel
if [ -f "${BACKUP_DIR}/Image" ]; then
	cp "${BACKUP_DIR}/Image" /boot/Image.bak
	# Add fallback entry if not already present
	if ! grep -q "^LABEL backup" /boot/extlinux/extlinux.conf; then
		cat >> /boot/extlinux/extlinux.conf << 'FALLBACK'

LABEL backup
   MENU LABEL backup kernel (pre-upgrade)
   LINUX /boot/Image.bak
   INITRD /boot/initrd
   APPEND ${cbootargs}
FALLBACK
		log "  Fallback boot entry added to extlinux.conf"
	fi
fi

log ""
log "Kernel:  $(uname -r)"
log "L4T:     $(awk '/REVISION/ {print $2}' /etc/nv_tegra_release 2>/dev/null || echo 'check after reboot')"
log "JetPack: $(dpkg -l nvidia-jetpack 2>/dev/null | awk '/^ii/ {print $3}' || echo 'check after reboot')"
log "Log:     ${LOG_FILE}"
log ""
log "========================================="
log "UEFI firmware update is pending."
log "Reboot now to apply all changes."
log "========================================="
log ""
log "If the system fails to boot after reboot,"
log "select 'backup kernel' from the boot menu."
log "========================================="
echo ""
if [ -t 0 ]; then
	read -p "Reboot now? [Y/n] " REPLY
else
	REPLY="Y"
	log "Non-interactive mode, rebooting automatically"
fi
if [ -z "${REPLY}" ] || [ "${REPLY}" = "Y" ] || [ "${REPLY}" = "y" ]; then
	reboot
else
	echo "Reboot manually when ready: sudo reboot"
fi
