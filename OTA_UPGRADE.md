# Jetson L4T deb-based OTA Upgrade Guide

## Overview

Upgrade Jetson devices via deb packages (JetPack 6.2 / L4T 36.4.x → JetPack 6.2.2 / L4T 36.5.0).

Preserves user configuration and data, does not trigger the initial setup wizard.

**Supported boards**: reComputer J401, J40mini, J101, Industrial, reServer series, Jetson Orin Nano Developer Kit (J3011). One package works for all boards.

## Step 1: Build deb packages on host

```bash
cd source
export CROSS_COMPILE=$(realpath ../..)/l4t-gcc/aarch64--glibc--stable-2022.08-1/bin/aarch64-buildroot-linux-gnu-
./build_debs.sh
```

Output goes to `ota_packages/` (7 packages + sha256sum.txt):

| Package | Version | Contents |
|---|---|---|
| nvidia-l4t-kernel | 5.15.185-tegra-36.5.0-\<ts\> | Kernel Image + in-tree modules |
| nvidia-l4t-kernel-dtbs | 5.15.185-tegra-36.5.0-\<ts\> | DTB/DTBO files |
| nvidia-l4t-kernel-headers | 5.15.185-tegra-36.5.0-\<ts\> | Kernel headers |
| nvidia-l4t-kernel-oot-modules | 5.15.185-tegra-36.5.0-\<ts\> | Out-of-tree modules |
| nvidia-l4t-kernel-oot-headers | 5.15.185-tegra-36.5.0-\<ts\> | OOT headers |
| nvidia-l4t-display-kernel | 5.15.185-tegra-36.5.0-\<ts\> | Display driver modules |
| nvidia-l4t-initrd | 36.5.0-\<ts\> | initrd |

## Step 2: Transfer to device

```bash
scp ../ota_packages/*.deb ../ota_packages/sha256sum.txt tools/ota/ota_upgrade.sh nvidia@<JETSON_IP>:~/ota-debs/
```

## Step 3: Run upgrade on device

```bash
sudo bash ~/ota-debs/ota_upgrade.sh ~/ota-debs
```

The script handles all steps automatically:
- Pre-flight checks (disk space, version compatibility, SHA256 verification)
- Backup critical files (kernel Image, initrd, boot config)
- Unlock held packages
- Switch apt source to r36.5
- Install deb packages
- Upgrade userspace and JetPack
- Update UEFI firmware
- Re-lock packages
- Cleanup old kernel modules
- Add fallback boot entry
- Prompt for reboot

## Step 4: Verify

```bash
uname -a                    # 5.15.185-tegra
cat /etc/nv_tegra_release   # R36, REVISION: 5.0
nvbootctrl dump-slots-info  # Current version: 36.5.0
dpkg -l | grep nvidia-jetpack  # 6.2.2
```

## Safety features

- **SHA256 verification**: All deb packages are verified against `sha256sum.txt` before installation
- **Automatic backup**: Kernel Image, initrd, and boot config are backed up before upgrade
- **Automatic rollback**: If upgrade fails, backup files are restored automatically
- **Fallback boot**: A backup kernel entry is added to extlinux.conf for manual recovery
- **Full logging**: All operations are logged to `/var/log/ota_upgrade.log`

## Recovery

If the device fails to boot after upgrade:
1. Hold any key during boot to enter the UEFI boot menu
2. Select "backup kernel (pre-upgrade)" to boot with the old kernel
3. Investigate the log at `/var/log/ota_upgrade.log`

If upgrade fails mid-process:
- The script automatically restores backup files
- Reboot to return to the pre-upgrade state
- Check the log for details

## Release packaging

To create a distributable release package:

```bash
# Build packages
cd source && ./build_debs.sh

# Create release tarball
cd .. && ./tools/ota/create_release.sh r36.5.0

# Optionally upload to GitHub
./tools/ota/create_release.sh r36.5.0 --upload
```

This produces:
- `ota-r36.5.0.tar.gz` — all deb packages + upgrade script + docs
- `ota-r36.5.0.tar.gz.sha256` — integrity checksum

### Download and upgrade (for end users)

```bash
# Download
wget https://github.com/Seeed-Studio/Linux_for_Tegra/releases/download/r36.5.0/ota-r36.5.0.tar.gz
wget https://github.com/Seeed-Studio/Linux_for_Tegra/releases/download/r36.5.0/ota-r36.5.0.tar.gz.sha256

# Verify integrity
sha256sum -c ota-r36.5.0.tar.gz.sha256

# Extract and upgrade
tar xzf ota-r36.5.0.tar.gz
sudo bash ota-r36.5.0/ota_upgrade.sh ota-r36.5.0
```

## Troubleshooting

- **A_kernel update FAILED**: `sudo touch /opt/nvidia/l4t-packages/.nv-l4t-disable-boot-fw-update-in-preinstall && sudo dpkg --configure -a`
- **Black screen after reboot**: Select "backup kernel" from boot menu, then: `sudo apt-get install -y nvidia-l4t-3d-core nvidia-l4t-gbm`
- **File conflicts**: `sudo dpkg -i --force-overwrite ~/ota-debs/*.deb`
- **Dependency conflicts**: `sudo apt-get --fix-broken install -y`
- **Config file prompts**: Add `-o Dpkg::Options::='--force-confold'` to skip interactive prompts
- **bootloader "does not match any known boards"**: Verify TNSPEC was correctly modified in step 8
- **Upgrade log**: `/var/log/ota_upgrade.log`
