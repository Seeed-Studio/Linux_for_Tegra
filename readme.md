- [Linux_for_Tegra](#linux_for_tegra)
    - [Supported hardware](#supported-hardware)
    - [Getting Started](#getting-started)
    - [Introduction to the software](#introduction-to-the-software)
        - [Directory structure](#directory-structure)
        - [CI/CD](#cicd)
    - [Summary](#summary)

# Linux_for_Tegra

This software is the source code of the default shipping firmware of Seeed Jetson reComputer, reServer and other products. It is built on NVIDIA JetPack 7.2 / Jetson Linux R39.2.0. On this basis, additional hardware drivers and boards are added, which is convenient for users to develop their own software and build other Jetson systems, such as Yocto, Buildroot, etc.

## Supported hardware

This branch includes Seeed carrier board support for Jetson Orin and Jetson Thor platforms. The exact CI build matrix and board configuration mapping are maintained in [`.gitlab-ci.yml`](./.gitlab-ci.yml).

- reComputer Thor carrier series
- reComputer and reComputer Industrial Orin Nano / Orin NX series
- reServer Industrial Orin Nano / Orin NX series
- reComputer Mini Orin series
- reComputer Super Orin series
- reComputer Rugged Orin series
- reComputer Robotics Orin series
- reServer AGX Orin J501 series
- reComputer Mini AGX Orin series
- reComputer Robotics AGX Orin series
- Seeed AGX Orin kit

Please pay attention to keywords such as **reComputer**, **Industrial**, **reServer**, **Rugged**, **Robotics**, etc. If there is only reComputer in the product name, it represents a carrier board based on NVIDIA Jetson DevKit. If the product name contains Industrial, Rugged, Robotics or reServer, it represents the corresponding Seeed carrier-board product family.

## Getting Started

1. Download and prepare the Linux_for_Tegra source code.

```
wget https://developer.nvidia.com/downloads/embedded/l4t/r39_release_v2.0/release/Jetson_Linux_r39.2.0_aarch64.tbz2
tar xf Jetson_Linux_r39.2.0_aarch64.tbz2
```

2. Download and prepare the sample root file system.

```
wget https://developer.nvidia.com/downloads/embedded/l4t/r39_release_v2.0/release/Tegra_Linux_Sample-Root-Filesystem_r39.2.0_aarch64.tbz2
sudo tar xpf Tegra_Linux_Sample-Root-Filesystem_r39.2.0_aarch64.tbz2 -C Linux_for_Tegra/rootfs/
```

3. Sync the source code for compiling.

```
cd Linux_for_Tegra/source/
./source_sync.sh -t jetson_39.2
```

4. Clone this repo and overwrite the original source code.

```
cd ../..
mkdir -p github/Linux_for_Tegra
git clone https://github.com/Seeed-Studio/Linux_for_Tegra.git -b r39.2.0 --depth=1 github/Linux_for_Tegra
cp -r github/Linux_for_Tegra/* Linux_for_Tegra/
```

5. Apply necessary changes to rootfs.

```
cd Linux_for_Tegra
sudo ./apply_binaries.sh
```

Make sure the build host has the required packages installed.

```
sudo apt-get update
sudo apt-get install build-essential flex bison libssl-dev
sudo apt-get install sshpass abootimg nfs-kernel-server libxml2-utils
sudo apt-get install qemu-user-static
```

6. Prepare the work environment for kernel build.

```
export ARCH=arm64
```

JetPack 7 uses the `source/kernel/kernel-noble` kernel source tree. The repository provides build helper scripts under `source/`.

7. Compile and build the kernel.

```
cd source
./nvbuild.sh
```

8. Install new kernel, dtbs, drivers and Seeed overlays.

```
./do_copy.sh
export INSTALL_MOD_PATH=`realpath ../rootfs/`
./nvbuild.sh -i
cd ..

# Copy Seeed camera/GMSL overlays into rootfs/boot.
# Do not skip this step: these DTBO files are needed by the flashed system.
cp kernel/dtb/tegra234-seeed-gmsl* rootfs/boot/ 2>/dev/null || true
cp kernel/dtb/tegra234-seeed-orbbec-335lg-overlay.dtbo rootfs/boot/ 2>/dev/null || true
cp kernel/dtb/tegra234-p3767-camera-p3768-imx219-dual-seeed.dtbo rootfs/boot/ 2>/dev/null || true
cp kernel/dtb/tegra234-p3767-camera-p3768-imx219-quad-seeed.dtbo rootfs/boot/ 2>/dev/null || true
cp kernel/dtb/tegra234-p3767-camera-p3768-imx477-dual-seeed.dtbo rootfs/boot/ 2>/dev/null || true
cp kernel/dtb/tegra234-p3767-camera-p3768-imx219-imx477.dtbo rootfs/boot/ 2>/dev/null || true
cp kernel/dtb/tegra234-p3767-camera-p3768-imx477-imx219.dtbo rootfs/boot/ 2>/dev/null || true

# Rebuild initrd after installing kernel modules and overlay files.
./tools/l4t_update_initrd.sh
```

9. Flash the device. The following command takes `recomputer-orin-j401` as an example.

```
sudo ./tools/kernel_flash/l4t_initrd_flash.sh --external-device nvme0n1p1 -c tools/kernel_flash/flash_l4t_t234_nvme.xml -p "-c bootloader/generic/cfg/flash_t234_qspi.xml" --showlogs --network usb0 recomputer-orin-j401 internal
```

For Thor carrier boards, use the matching Thor board configuration and flash XML from this repository.

```
sudo ./tools/kernel_flash/l4t_initrd_flash.sh --external-device nvme0n1p1 -c tools/kernel_flash/flash_l4t_t264_nvme.xml -p "-c bootloader/flash_l4t_t264_qspi.xml" --showlogs --network usb0 recomputer-thor-carrier-j6015 internal
```

**Note:** For more flashing methods, please follow Seeed wiki documents for the corresponding product family.

## Introduction to the software

Here is the content of this software from the following aspects.

### Directory structure

Compared to the original Linux_for_Tegra, we have added the following folders and files.

- `extra_scripts`: Kernel and rootfs helper scripts used by the system build process.
- `source/do_copy.sh`: Helper script for copying built kernel Image, DTBs and generated artifacts back into Linux_for_Tegra.
- `source/kernel/kernel-noble`: JP7 kernel source tree.
- `.gitlab-ci.yml`: Seeed internal firmware build matrix and production image flow.

Although Seeed has many products based on Jetson, the board configuration files are shared by product families. For detailed correspondence, see [`.gitlab-ci.yml`](./.gitlab-ci.yml).

### CI/CD

Seeed's Jetson products usually use SSD as system storage, so the firmware mainly targets SSD boot. Because of the particularity of SSD flashing, Seeed production uses a two-stage flashing flow with QSPI flash plus SSD image.

The steps for burning firmware in Seeed's current Jetson production process are as follows:

1. Burn the smallest QSPI flash image.
2. Install the SSD. The SSD image is generated and cloned before installation.

CI/CD is used by Seeed's internal GitLab to automatically produce firmware that meets the above requirements and store it in the Samba file server. Its configuration is a useful reference for reproducing mass production images and using Seeed mass production services.

1. Standard GitLab CI fields.

```
job-name:
    stage: product-stage
    when: on_success
    tags:
      - arm64-shell
```

2. Install rootfs, which is a standard procedure described in the NVIDIA documentation.

```
    script:
         - export DATE_STR=$(TZ='Asia/Hong_Kong' date +%Y-%m-%d)
         - wget http://192.168.1.77/jetson/Tegra_Linux_Sample-Root-Filesystem_r39.2.0_aarch64.tbz2 -q
         - tar xpf Tegra_Linux_Sample-Root-Filesystem_r39.2.0_aarch64.tbz2 -C rootfs
         - ./apply_binaries.sh
         - ./tools/l4t_flash_prerequisites.sh
```

3. Generate QSPI flash firmware. In factory production, the QSPI image can be flashed quickly through USB.

```
         - sudo BOARDID=$BOARDID BOARDSKU=$BOARDSKU FAB=$FAB BOARDREV=$BOARDREV CHIP_SKU=$CHIP_SKU ./tools/kernel_flash/l4t_initrd_flash.sh --qspi-only --no-flash --massflash 5 --network usb0 $CONFIG external
         - mkdir deploy
         - mount -t cifs -o username=$SMB_USER,password=$SMB_PWD,vers=3.0,uid=1000,gid=1000,rw,file_mode=0664 //192.168.1.77/red_2t/jetson deploy
         - cp mfi_$CONFIG.tar.gz deploy/mfi_$CI_JOB_STAGE-qspiflash-$JETPACKVER-$JETSONVER-$DATE_STR.tar.gz || true
```

4. Customize the rootfs. This step can install JetPack components, apply BSP binaries and clean temporary build files.

```
- mount --bind /sys ./rootfs/sys
- mount --bind /dev ./rootfs/dev
- mount --bind /dev/pts ./rootfs/dev/pts
- mount --bind /proc ./rootfs/proc
- cp /usr/bin/qemu-aarch64-static rootfs/usr/bin/
- cp extra_scripts/rootfs_magic.sh rootfs
- chroot rootfs /rootfs_magic.sh
- umount ./rootfs/sys
- umount ./rootfs/dev/pts
- umount ./rootfs/dev
- umount ./rootfs/proc
- rm rootfs/rootfs_magic.sh
- rm rootfs/usr/bin/qemu-aarch64-static
```

5. Generate the mass production MFI firmware.

For Orin:

```
- sudo BOARDID=$BOARDID BOARDSKU=$BOARDSKU FAB=$FAB BOARDREV=$BOARDREV CHIP_SKU=$CHIP_SKU ./tools/kernel_flash/l4t_initrd_flash.sh --external-device nvme0n1p1 -c tools/kernel_flash/flash_l4t_nvme.xml -S 80GiB -p "-c bootloader/generic/cfg/flash_t234_qspi.xml --no-systemimg" --no-flash --massflash 5 --network usb0 $CONFIG external
```

For Thor:

```
- sudo BOARDID=$BOARDID BOARDSKU=$BOARDSKU FAB=$FAB BOARDREV=$BOARDREV CHIP_SKU=$CHIP_SKU ./tools/kernel_flash/l4t_initrd_flash.sh --external-device nvme0n1p1 -c tools/kernel_flash/flash_l4t_t264_nvme.xml -S 80GiB -p "-c bootloader/flash_l4t_t264_qspi.xml --no-systemimg" --no-flash --massflash 5 --network usb0 $CONFIG external
```

Then copy the generated package to the deployment directory.

```
- cp mfi_$CONFIG.tar.gz deploy/mfi_$CI_JOB_STAGE-$JETPACKVER-$JETSONVER-$DATE_STR.tar.gz || true
- umount deploy
```

Note that **192.168.1.77** is the internal server of Seeed, which functions as a Samba and HTTP file server.

## Summary

This software retains the usage and features of the original `Linux_for_Tegra` to the greatest extent, and adds Seeed BSP, board configuration, camera/GMSL support and production build flow on this basis.
