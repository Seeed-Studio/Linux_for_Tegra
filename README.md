- [Linux_for_Tegra](#Linux_for_Tegra)
    - [Supported hardware](#supported-hardware)
    - [Getting Started](#getting-started)
    - [Introduction to the software](#introduction-to-the-software)
        - [Directory structure](directory-structure)
        - [CI/CD](#cicd)
    - [Summary](#summary)

# Linux_for_Tegra

This software is the source code of the default shipping firmware of Seeed Jetson reComputer, reServer and other products. It is built on NVIDIA Jetpack 6.0. On this basis, additional hardware drivers and boards are added, which is convenient for users to develop their own software and build other Jetson systems, such as Yocto, buildroot, etc.

## Supported hardware 

- orin-nx-industrial-8g - [reComputer Industrial J4011](https://www.seeedstudio.com/reComputer-Industrial-J4011-p-5681.html)
- orin-nx-reserver-8g - [reServer Industrial J4011](https://www.seeedstudio.com/reServer-industrial-J4011-p-5748.html)
- orin-nx-devkit-8g - [reComputer J4011](https://www.seeedstudio.com/reComputer-J4011-p-5585.html)
- orin-nx-industrial-16g - [reComputer Industrial J4012](https://www.seeedstudio.com/reComputer-Industrial-J4012-p-5684.html)
- orin-nx-reserver-16g - [reServer Industrial J4012](https://www.seeedstudio.com/reServer-industrial-J4012-p-5747.html)
- orin-nx-devkit-16g - [reComputer J4012](https://www.seeedstudio.com/reComputer-J4012-p-5586.html)
- orin-nano-industrial-8g - [reComputer Industrial J3011](https://www.seeedstudio.com/reComputer-Industrial-J3011-p-5682.html)
- orin-nano-reserver-8g - [reServer Industrial J3011](https://www.seeedstudio.com/reServer-industrial-J3011-p-5750.html)
- orin-nano-devkit-8g - [reComputer J3011](https://www.seeedstudio.com/reComputer-J3011-p-5590.html)
- orin-nano-industrial-4g - [reComputer Industrial J3010](https://www.seeedstudio.com/reComputer-Industrial-J3010-p-5686.html)
- orin-nano-reserver-4g - [reServer Industrial J3010](https://www.seeedstudio.com/reServer-industrial-J3010-p-5749.html)
- orin-nano-devkit-4g - [reComputer J3010](https://www.seeedstudio.com/reComputer-J3010-p-5589.html)

Please pay attention to keywords such as **reComputer**, **Industrial**, **reServer**, etc. If there is only reComputer in the product name, it represents a carrier board based on NVIDIA Jetson DevKit. The form of the product is the same as the official devkit. If the product name is reComputer + Industrial, it represents [this](https://wiki.seeedstudio.com/reComputer_Industrial_Getting_Started/) form of product. If the product name is reServer + Industrial, it represents [this](https://wiki.seeedstudio.com/reServer_Industrial_Getting_Started/) form of product.

## Getting Started

1. Download and prepare the Linux_for_Tegra source code
```
wget https://developer.nvidia.com/downloads/embedded/l4t/r36_release_v3.0/release/jetson_linux_r36.3.0_aarch64.tbz2
tar xf jetson_linux_r36.3.0_aarch64.tbz2
```

2. Download and prepare sample root file system
```
wget https://developer.nvidia.com/downloads/embedded/l4t/r36_release_v3.0/release/tegra_linux_sample-root-filesystem_r36.3.0_aarch64.tbz2
sudo tar xpf tegra_linux_sample-root-filesystem_r36.3.0_aarch64.tbz2 -C Linux_for_Tegra/rootfs/
```

3. sync the source code for compiling
```
cd Linux_for_Tegra/source/
./source_sync.sh -t jetson_36.3
```
4. clone this repo and overwrite the original source code
```
cd ../..
mkdir -p github/Linux_for_Tegra
git clone https://github.com/Seeed-Studio/Linux_for_Tegra.git -b r36.3.0 --depth=1 github/Linux_for_Tegra
cp -r github/Linux_for_Tegra/* Linux_for_Tegra/
```

5. apply necessary changes to rootfs
```
cd Linux_for_Tegra
sudo ./apply_binaries.sh
```
make sure system have required libraries (ex: ubuntu-20)
```
apt-get update && apt-get install build-essential flex bison libssl-dev
```

6. prepare work for kernel build
```
wget https://developer.nvidia.com/downloads/embedded/l4t/r36_release_v3.0/toolchain/aarch64--glibc--stable-2022.08-1.tar.bz2
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

9. flash the device(take recomputer-orin-j401 for example)
```
sudo ./tools/kernel_flash/l4t_initrd_flash.sh --external-device nvme0n1p1   -c tools/kernel_flash/flash_l4t_t234_nvme.xml -p "-c bootloader/generic/cfg/flash_t234_qspi.xml"   --showlogs --network usb0 recomputer-orin-j401 internal
```

**Note:** For more flashing methods, please follow our [reComputer Industrial](https://wiki.seeedstudio.com/reComputer_Industrial_Getting_Started/#different-methods-of-flashing) and [reServer Industrial](https://wiki.seeedstudio.com/reServer_Industrial_Getting_Started/#different-methods-of-flashing) wiki documents.

## Introduction to the software

Here is the content of this software from the following three aspects

### Directory structure

Compared to the original Linux_for_Tegra, we have added the following folders and files.

- extra_scripts: Here we store the kernel and the necessary shell script files for the system build process.

Although Seeed has many products based on Jetson, only the following five configuration files are used. For detailed correspondence, see .[gitlab-ci.yml](./.gitlab-ci.yml)..

- recomputer-industrial-orin-j201.conf
- recomputer-orin-j401.conf
- reserver-agx-orin-j501x.conf
- reserver-agx-orin-j501x-gmsl.conf
- reserver-industrial-orin-j401.conf

### CI/CD

Seeed's Jetson products all use SSD as system storage, so the firmware only supports SSD boot. Because of the particularity of SSD burning, we tried to use NVIDIA's official documentation to mass produce via USB on the production line, but the production efficiency was extremely low and could not meet the needs of mass production. After our research, two-stage burning using spiflash + ssd is the most appropriate way. The steps for burning firmware in Seeed’s current Jetson production process are as follows:

1. Burn the smallest qspiflash image
2. Install the SSD (**SSD has passed the professional SSD cloning service and burned the SSD image**)

CI/CD, a service used by Seeed's internal Gitlab, automatically produces firmware that meets the above requirements and stores it in the Samba file server. Its configuration file is of great reference value and can help users re-produce mass production images and facilitate the use of Seeed mass production services. The following uses job-orin-nano-reserver-8g as an example to introduce the firmware production process.

1. Standard Gitlab CI fields.

```
job-orin-nano-reserver-8g:
    stage: orin-nano-reserver-8g
    when: on_success
    tags:
      - arm64-shell

    needs: [job-orin-nano-industrial-8g]
```

2. Install rootfs, which is a standard procedure described in the NVIDIA documentation.

```
    script:
         - id
         - export DATE_STR=$(TZ='Asia/Hong_Kong' date +%Y-%m-%d)
         - wget  http://192.168.1.77/jetson/tegra_linux_sample-root-filesystem_r36.3.0_aarch64.tbz2 -q
         - tar xpf tegra_linux_sample-root-filesystem_r36.3.0_aarch64.tbz2 -C rootfs
         - ./apply_binaries.sh
         - ./tools/l4t_flash_prerequisites.sh
```

3. Generate qspiflash firmware, which is small, probably only 2GB. In factory production, it can be burned in 1 minute via USB cable.

```
         - sudo BOARDID=$BOARDID BOARDSKU=$BOARDSKU FAB=$FAB BOARDREV=$BOARDREV CHIP_SKU=$CHIP_SKU ./tools/kernel_flash/l4t_initrd_flash.sh -p "-c bootloader/generic/cfg/flash_t234_qspi.xml --no-systemimg" --no-flash --massflash 5 --network usb0 $CONFIG external
         - mkdir deploy
         - mount -t cifs -o username=$SMB_USER,password=$SMB_PWD,vers=3.0,uid=1000,gid=1000,rw,file_mode=0664 //192.168.1.77/red_2t/jetson deploy
         - cp mfi_$CONFIG.tar.gz deploy/mfi_$CI_JOB_STAGE-qspiflash-6.0-36.3.0-$DATE_STR.tar.gz || true
```

4. This step is the core customization step of the firmware, which will do the following:

- Install NVIDIA-Jetpack
- Apply the corresponding BSP binary
- Clean up Work Folders

```
- sed -i "s/<SOC>/t234/g" rootfs/etc/apt/sources.list.d/nvidia-l4t-apt-source.list
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

5.Generating mass production requires mfi firmware. There are many parameters required to generate mass production firmware after NVIDIA Jetpack 6.x. The parameters here can help with subsequent related derivative products.

```
- sudo BOARDID=$BOARDID BOARDSKU=$BOARDSKU FAB=$FAB BOARDREV=$BOARDREV CHIP_SKU=$CHIP_SKU ./tools/kernel_flash/l4t_initrd_flash.sh --external-device nvme0n1p1 -c tools/kernel_flash/flash_l4t_nvme.xml -S 80GiB -p "-c bootloader/generic/cfg/flash_t234_qspi.xml --no-systemimg" --no-flash --massflash 5 --network usb0 $CONFIG external
- cp mfi_$CONFIG.tar.gz deploy/mfi_$CI_JOB_STAGE-6.0-36.3.0-$DATE_STR.tar.gz || true 
- umount deploy
```

Note that **192.168.1.77** is the internal server of Seeed, which functions as a Samba and Http file server.

## Summary

This software retains the usage and features of the original `Linux_for_Tegra` to the greatest extent, and adds our own BSP and board on this basis.
