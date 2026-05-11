# Jetson L4T deb-based OTA Upgrade Guide

## Overview

Upgrade Jetson devices via deb packages (JetPack 6.2 / L4T 36.4.x → JetPack 6.2.2 / L4T 36.5.0).

Preserves user configuration and data, does not trigger the initial setup wizard.

## Step 1: Build deb packages on host

```bash
cd source
export CROSS_COMPILE=$(realpath ../..)/l4t-gcc/aarch64--glibc--stable-2022.08-1/bin/aarch64-buildroot-linux-gnu-
./build_debs.sh
```

Output goes to `ota_packages/` (7 packages):

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
scp ../ota_packages/*.deb tools/ota/ota_upgrade.sh nvidia@<JETSON_IP>:~/ota-debs/
```

## Step 3: Run upgrade on device

```bash
sudo bash ~/ota-debs/ota_upgrade.sh ~/ota-debs
```

The script handles all steps automatically:
- Unlock held packages
- Disable boot-fw partition writes
- Switch apt source to r36.5
- Install deb packages
- Upgrade userspace and JetPack
- Update UEFI firmware
- Re-lock packages
- Cleanup old kernel modules
- Prompt for reboot

## Step 4: Verify

```bash
uname -a                    # 5.15.185-tegra
cat /etc/nv_tegra_release   # R36, REVISION: 5.0
nvbootctrl dump-slots-info  # Current version: 36.5.0
dpkg -l | grep nvidia-jetpack  # 6.2.2
```

## Troubleshooting

- **A_kernel update FAILED**: `sudo touch /opt/nvidia/l4t-packages/.nv-l4t-disable-boot-fw-update-in-preinstall && sudo dpkg --configure -a`
- **Black screen after reboot**: `sudo apt-get install -y nvidia-l4t-3d-core nvidia-l4t-gbm`
- **File conflicts**: `sudo dpkg -i --force-overwrite ~/ota-debs/*.deb`
- **Dependency conflicts**: `sudo apt-get --fix-broken install -y`
- **Config file prompts**: Add `-o Dpkg::Options::='--force-confold'` to skip interactive prompts
- **bootloader "does not match any known boards"**: Verify TNSPEC was correctly modified in step 7a

## Manual upgrade

For advanced users who want step-by-step control, the manual procedure is:

```bash
# 1. Unlock
sudo apt-mark unhold \
  nvidia-l4t-display-kernel nvidia-l4t-kernel nvidia-l4t-kernel-dtbs \
  nvidia-l4t-kernel-headers nvidia-l4t-kernel-oot-headers \
  nvidia-l4t-kernel-oot-modules nvidia-l4t-initrd

# 2. Disable boot-fw writes
sudo mkdir -p /opt/nvidia/l4t-packages
sudo touch /opt/nvidia/l4t-packages/.nv-l4t-disable-boot-fw-update-in-preinstall

# 3. Switch apt source
sudo sed -i 's/r36\.4/r36.5/g' /etc/apt/sources.list.d/nvidia-l4t-apt-source.list
sudo apt-get update

# 4. Install debs
sudo dpkg -i --force-overwrite --force-depends ~/ota-debs/*.deb

# 5. Upgrade userspace
sudo apt-get -o Dpkg::Options::='--force-confold' \
  --fix-broken install -y --allow-change-held-packages

# 6. Dist-upgrade
sudo apt-get -o Dpkg::Options::='--force-confold' \
  dist-upgrade -y --allow-change-held-packages

# 7. UEFI update
sudo cp /etc/nv_boot_control.conf /etc/nv_boot_control.conf.bak
sudo sed -i 's/recomputer-orin-j401/jetson-orin-nano-devkit/' /etc/nv_boot_control.conf
sudo rm -f /opt/nvidia/l4t-packages/.nv-l4t-disable-boot-fw-update-in-preinstall
sudo apt-get install --reinstall -y --allow-downgrades nvidia-l4t-bootloader
sudo cp /etc/nv_boot_control.conf.bak /etc/nv_boot_control.conf
sudo rm -f /etc/nv_boot_control.conf.bak

# 8. JetPack (optional)
sudo apt-get install -y --allow-change-held-packages nvidia-jetpack

# 9. Re-lock
sudo apt-mark hold \
  nvidia-l4t-display-kernel nvidia-l4t-kernel nvidia-l4t-kernel-dtbs \
  nvidia-l4t-kernel-headers nvidia-l4t-kernel-oot-headers \
  nvidia-l4t-kernel-oot-modules nvidia-l4t-initrd

# 10. Reboot
sudo reboot
```

TNSPEC mapping for step 7 depends on product:
- reComputer J401/J101/Orin Nano series → `jetson-orin-nano-devkit`
- reComputer Industrial/A30/reServer series → `jetson-agx-orin-devkit`
