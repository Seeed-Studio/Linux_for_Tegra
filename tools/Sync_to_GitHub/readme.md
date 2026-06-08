- [Linux_for_Tegra](#Linux_for_Tegra)
    - [Supported hardware](#supported-hardware)
    - [Getting Started](#getting-started)
    - [Introduction to the software](#introduction-to-the-software)
        - [Directory structure](directory-structure)
        - [CI/CD](#cicd)
    - [Summary](#summary)

# Linux_for_Tegra

This software is the source code of the default shipping firmware of Seeed Jetson reComputer and other products. It is built on NVIDIA JetPack 7 (L4T R38.4.0). On this basis, additional hardware drivers and boards are added, which is convenient for users to develop their own software and build other Jetson systems, such as Yocto, buildroot, etc.

## Supported hardware

- jetson-agx-thor-devkit - [NVIDIA Jetson AGX Thor Devkit](https://developer.nvidia.com/embedded/jetson-agx-thor-developer-kit)
- recomputer-thor-carrier-j601 - [reComputer J601](https://www.seeedstudio.com/)

## Getting Started

1. Download and prepare the Linux_for_Tegra source code
```
wget https://developer.nvidia.com/downloads/embedded/l4t/r38_release_v4.0/release/Jetson_Linux_r38.4.0_aarch64.tbz2
tar xf Jetson_Linux_r38.4.0_aarch64.tbz2
```

2. Download and prepare sample root file system
```
wget https://developer.nvidia.com/downloads/embedded/l4t/r38_release_v4.0/release/Tegra_Linux_Sample-Root-Filesystem_r38.4.0_aarch64.tbz2
sudo tar xpf Tegra_Linux_Sample-Root-Filesystem_r38.4.0_aarch64.tbz2 -C Linux_for_Tegra/rootfs/
```

3. sync the source code for compiling
```
cd Linux_for_Tegra/source/
./source_sync.sh -t jetson_38.4.0
```
4. clone this repo and overwrite the original source code
```
cd ../..
mkdir -p github/Linux_for_Tegra
git clone https://github.com/Seeed-Studio/Linux_for_Tegra.git -b r38.4.0 --depth=1 github/Linux_for_Tegra
cp -r github/Linux_for_Tegra/* Linux_for_Tegra/
```

5. apply necessary changes to rootfs
```
cd Linux_for_Tegra
sudo ./apply_binaries.sh
```
* make sure system have required libraries
   ```
   sudo apt-get update
   sudo apt-get install build-essential flex bison libssl-dev
   sudo apt-get install sshpass
   sudo apt-get install abootimg
   sudo apt-get install nfs-kernel-server
   sudo apt-get install libxml2-utils
   ```
* if You do not have qemu, install it using command
   ```
   sudo apt-get install qemu-user-static
   ```

6. prepare work for kernel build
```
wget https://developer.nvidia.com/downloads/embedded/l4t/r38_release_v4.0/toolchain/aarch64--glibc--stable-2022.08-1.tar.bz2
mkdir -p l4t-gcc
tar xf aarch64--glibc--stable-2022.08-1.tar.bz2 -C ./l4t-gcc
export ARCH=arm64
export CROSS_COMPILE=`realpath .`/l4t-gcc/aarch64--glibc--stable-2022.08-1/bin/aarch64-buildroot-linux-gnu-
```

7. compile and build kernel
```
cd source
./nvbuild.sh
```

8. install new kernel dtbs and drivers
```
./do_copy.sh
export INSTALL_MOD_PATH=`realpath ../rootfs/`
./nvbuild.sh -i
```

9. flash the device (take recomputer-thor-carrier-j601 for example)
```
sudo ./tools/kernel_flash/l4t_initrd_flash.sh --external-device nvme0n1p1 \
  -c tools/kernel_flash/flash_l4t_t264_nvme.xml \
  -S 80GiB \
  -p "-c bootloader/flash_l4t_t264_qspi.xml" \
  --showlogs --network usb0 recomputer-thor-carrier-j601 external
```

## Introduction to the software

Here is the content of this software from the following three aspects

### Directory structure

Compared to the original Linux_for_Tegra, we have added the following folders and files.

- extra_scripts: here we store the kernel and the necessary shell script files for the system build process.

The configuration files used for Seeed products. For detailed correspondence, see [.gitlab-ci.yml](./.gitlab-ci.yml).

- jetson-agx-thor-devkit.conf
- recomputer-thor-carrier-j601.conf

### CI/CD

Seeed's Jetson products based on JetPack 7 use SSD as system storage, so the firmware supports SSD boot. Because of the particularity of SSD burning, we tried to use NVIDIA's official documentation to mass produce via USB on the production line, but the production efficiency was extremely low and could not meet the needs of mass production. After our research, two-stage burning using spiflash + ssd is the most appropriate way. The steps for burning firmware in Seeed's current Jetson production process are as follows:

1. Burn the smallest qspiflash image
2. Install the SSD (**SSD has passed the professional SSD cloning service and burned the SSD image**)

CI/CD, a service used by Seeed's internal Gitlab, automatically produces firmware that meets the above requirements and stores it in the Samba file server. Its configuration file is of great reference value and can help users re-produce mass production images and facilitate the use of Seeed mass production services. The following uses jetson-agx-thor-devkit as an example to introduce the firmware production process.

1. Standard Gitlab CI fields.

```
jetson-agx-thor-devkit:
    stage: jetson-agx-thor-devkit
    timeout: 3h
    rules:
      - if: '$CI_COMMIT_TAG'
        when: on_success
      - if: '$START_TASK == "jetson-agx-thor-devkit"'
        when: on_success
    tags:
      - x86-shell
    variables:
      CONFIG: "jetson-agx-thor-devkit"
      BOARDID: "3834"
      BOARDSKU: "0008"
      FAB: "400"
      BOARDREV: "G.5"
      CHIP_SKU: "00:00:00:A0"
```

2. Install rootfs, which is a standard procedure described in the NVIDIA documentation.

```
    script:
         - id
         - export DATE_STR=$(TZ='Asia/Hong_Kong' date +%Y-%m-%d)
         - wget http://192.168.1.77/jetson/Tegra_Linux_Sample-Root-Filesystem_r38.4.0_aarch64.tbz2 -q
         - tar xpf Tegra_Linux_Sample-Root-Filesystem_r38.4.0_aarch64.tbz2 -C rootfs
         - ./apply_binaries.sh
```

3. Build the kernel with Seeed modifications.

```
         - export ARCH=arm64
         - export CROSS_COMPILE=`realpath .`/l4t-gcc/aarch64--glibc--stable-2022.08-1/bin/aarch64-buildroot-linux-gnu-
         - cd source/
         - ln -sf ../../../../../../nvethernetrm nvidia-oot/drivers/net/ethernet/nvidia/nvethernet/nvethernetrm
         - ./nvbuild.sh
         - ./do_copy.sh
         - export INSTALL_MOD_PATH=`realpath ../rootfs/`
         - ./nvbuild.sh -i
```

4. Generate qspiflash firmware, which is small. In factory production, it can be burned quickly via USB cable.

```
         - sudo BOARDID=$BOARDID BOARDSKU=$BOARDSKU FAB=$FAB BOARDREV=$BOARDREV CHIP_SKU=$CHIP_SKU ./tools/kernel_flash/l4t_initrd_flash.sh --qspi-only --no-flash --massflash 5 --network usb0 $CONFIG external
```

5. Install NVIDIA JetPack and custom packages.

```
         - sed -i "s/<SOC>/t264/g" rootfs/etc/apt/sources.list.d/nvidia-l4t-apt-source.list
         - mount --bind /sys ./rootfs/sys
         - mount --bind /dev ./rootfs/dev
         - mount --bind /dev/pts ./rootfs/dev/pts
         - mount --bind /proc ./rootfs/proc
         - cp /usr/bin/qemu-aarch64-static rootfs/usr/bin/
         - cp extra_scripts/rootfs_magic.sh rootfs
         - chroot rootfs /rootfs_magic.sh || true
         - umount ./rootfs/sys
         - umount ./rootfs/dev/pts
         - umount ./rootfs/dev
         - umount ./rootfs/proc
         - rm rootfs/rootfs_magic.sh
         - rm rootfs/usr/bin/qemu-aarch64-static
```

6. Generate mass production mfi firmware (qspi + nvme).

```
         - sudo BOARDID=$BOARDID BOARDSKU=$BOARDSKU FAB=$FAB BOARDREV=$BOARDREV CHIP_SKU=$CHIP_SKU ./tools/kernel_flash/l4t_initrd_flash.sh --external-device nvme0n1p1 -c tools/kernel_flash/flash_l4t_t264_nvme.xml -S 80GiB -p "-c bootloader/flash_l4t_t264_qspi.xml --no-systemimg" --no-flash --massflash 5 --network usb0 $CONFIG external
```

Note that **192.168.1.77** is the internal server of Seeed, which functions as a Samba and Http file server.

## Summary

This software retains the usage and features of the original `Linux_for_Tegra` to the greatest extent, and adds our own BSP and board on this basis. It is designed for NVIDIA JetPack 7 (L4T R38.4.0) with support for the Jetson AGX Thor platform.
