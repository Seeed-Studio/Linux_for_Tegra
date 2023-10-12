- [Linux_for_Tegra](#Linux_for_Tegra)
    - [Supported hardware](#supported-hardware)
    - [Getting Started](#getting-started)
    - [Introduction to the software](#introduction-to-the-software)
        - [Directory structure](directory-structure)
        - [CI/CD](#cicd)
        - [seeed-linux-overlay](#seeed-linux-overlay)
    - [Compile the build kernel](#compile-and-build-kernel)
    - [Summary](#summary)

# Linux_for_Tegra

This software is the source code of the default shipping firmware of Seeed Jetson reComputer, reServer and other products. It is built on NVIDIA Jetpack 5.1.1. On this basis, additional hardware drivers and boards are added, which is convenient for users to develop their own software and build other Jetson systems, such as Yocto, buildroot, etc.

## Supported hardware 

- xavier-nx-industrial-16g - [reComputer Industrial J2012](https://www.seeedstudio.com/reComputer-Industrial-J2012-p-5685.html)
- xavier-nx-industrial-8g - [reComputer Industrial J2011](https://www.seeedstudio.com/reComputer-Industrial-J2011-p-5683.html)
- xavier-nx-devkit-16g - [reComputer J2022](https://www.seeedstudio.com/reComputer-J2022-p-5497.html)
- xavier-nx-devkit-8g - [reComputer J2021](https://www.seeedstudio.com/reComputer-J2021-p-5438.html)
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

1. Download the latest Jetson Linux release package and sample file system for your Jetson device from https://developer.nvidia.com/linux-tegra

2. Enter the following commands to untar the files and assemble the rootfs:

```
$ tar xf ${L4T_RELEASE_PACKAGE}
$ sudo tar xpf ${SAMPLE_FS_PACKAGE} -C Linux_for_Tegra/rootfs/
$ cd Linux_for_Tegra/
$ sudo ./apply_binaries.sh
$ sudo ./tools/l4t_flash_prerequisites.sh
```

3. Copy BSP file. for other products, please refer to [CI/CD](#cicd).

```
cp extra_scripts/reserver_industrial/Image kernel/
cp extra_scripts/reserver_industrial/tegra234-p3767-0003-p3509-a02.dtb kernel/dtb/
cp extra_kernel_modules/lan743x.ko rootfs/lib/modules/5.10.104-tegra/kernel/drivers/net/ethernet/microchip/lan743x.ko
cp extra_kernel_modules/spi-tegra114.ko rootfs/lib/modules/5.10.104-tegra/kernel/drivers/spi/spi-tegra114.ko
cp extra_kernel_modules/8723du.ko rootfs/lib/modules/5.10.104-tegra/kernel/drivers/net/wireless/realtek/rtl8xxxu/
```

4. Ensure that your Jetson device is configured and connected to your Linux host.

5. Confirm that the Jetson device is in Force Recovery Mode.

6. Enter this command on your Linux host to install (flash) the Jetson release onto the Jetson device.

```
sudo ./tools/kernel_flash/l4t_initrd_flash.sh --external-device nvme0n1p1 -c tools/kernel_flash/flash_l4t_nvme.xml -S 80GiB  -p "-c bootloader/t186ref/cfg/flash_t234_qspi.xml --no-systemimg" --network usb0  reserver-orin-industrial external 
```

7. The Jetson device automatically reboots when the installation process is complete. At this point your Jetson device is operational. Follow the prompts on the display to set up a user account and log in.

## Introduction to the software

Here is the content of this software from the following three aspects

### Directory structure

Compared to the original Linux_for_Tegra, we have added the following folders and files.

- extra_kernel_modules: Here is the binary file for our custom kernel module.
- extra_scripts: Here we store the kernel and the necessary shell script files for the system build process.

Although Seeed has many products based on Jetson, only the following five configuration files are used. For detailed correspondence, see .[gitlab-ci.yml](./.gitlab-ci.yml)..

- recomputer-xavier-nx-industrial.conf
- reserv-orin-industrial.conf
- recomputer-orin.conf
- recomputer-orin-industrial.conf
- recomputer-xavier-nx-devkit.conf

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

2. Install rootfs, which is a standard procedure described in [the NVIDIA Jetson documentation](https://docs.nvidia.com/jetson/archives/r35.3.1/DeveloperGuide/text/IN/QuickStart.html#to-flash-the-jetson-developer-kit-operating-software). [`Tegra_Linux_Sample-Root-Filesystem_R35.3.1_aarch64.tbz2`](https://developer.nvidia.com/downloads/embedded/l4t/r35_release_v3.1/release/tegra_linux_sample-root-filesystem_r35.3.1_aarch64.tbz2/) from the NVIDIA documentation.


```
    script:
         - id
         - export DATE_STR=$(TZ='Asia/Hong_Kong' date +%Y-%m-%d)
         - wget  http://192.168.1.77/jetson/Tegra_Linux_Sample-Root-Filesystem_R35.3.1_aarch64.tbz2 -q
         - tar xpf Tegra_Linux_Sample-Root-Filesystem_R35.3.1_aarch64.tbz2 -C rootfs
         - ./apply_binaries.sh
         - ./tools/l4t_flash_prerequisites.sh
```

3. Generate qspiflash firmware, which is small, probably only 2GB. In factory production, it can be burned in 1 minute via USB cable.

```
         - sudo  BOARDID=3767 BOARDSKU=0003 FAB=RC1  BOARDREV=B.6   ./tools/kernel_flash/l4t_initrd_flash.sh   -p "-c bootloader/t186ref/cfg/flash_t234_qspi.xml --no-systemimg" --no-flash  --massflash 5  --network usb0  reserver-orin-industrial external 
         - mkdir deploy
         - mount -t cifs -o username=$SMB_USER,password=$SMB_PWD,vers=3.0,uid=1000,gid=1000,rw,file_mode=0664 //192.168.1.77/red_2t/jetson deploy
         - cp  mfi_reserver-orin-industrial.tar.gz  deploy/mfi_reserver-orin-nano-8g-industrial-qspiflash-5.1-35.3.1-$DATE_STR.tar.gz 
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
         - cp extra_scripts/reserver_industrial/Image kernel/
         - cp extra_scripts/reserver_industrial/tegra234-p3767-0003-p3509-a02.dtb kernel/dtb/
         - cp extra_kernel_modules/lan743x.ko rootfs/lib/modules/5.10.104-tegra/kernel/drivers/net/ethernet/microchip/lan743x.ko
         - cp extra_kernel_modules/spi-tegra114.ko rootfs/lib/modules/5.10.104-tegra/kernel/drivers/spi/spi-tegra114.ko
         - cp extra_kernel_modules/8723du.ko rootfs/lib/modules/5.10.104-tegra/kernel/drivers/net/wireless/realtek/rtl8xxxu/8723du.ko
         - cp extra_kernel_modules/tpm/ rootfs/lib/modules/5.10.104-tegra/kernel/drivers/char/ -rf
         - chroot rootfs /rootfs_magic.sh
         - umount ./rootfs/sys
         - umount ./rootfs/dev/pts
         - umount ./rootfs/dev
         - umount ./rootfs/proc
         - rm rootfs/rootfs_magic.sh
         - rm rootfs/usr/bin/qemu-aarch64-static
```

5.Generating mass production requires mfi firmware. There are many parameters required to generate mass production firmware after NVIDIA Jetpack 5.x. The parameters here can help with subsequent related derivative products.

```
         - sudo  BOARDID=3767 BOARDSKU=0003 FAB=RC1  BOARDREV=B.6   ./tools/kernel_flash/l4t_initrd_flash.sh --external-device nvme0n1p1 -c tools/kernel_flash/flash_l4t_nvme.xml -S 80GiB  -p "-c bootloader/t186ref/cfg/flash_t234_qspi.xml --no-systemimg" --no-flash  --massflash 5  --network usb0  reserver-orin-industrial  external 
         - cp  mfi_reserver-orin-industrial.tar.gz  deploy/mfi_reserver-orin-nano-8g-industrial-5.1-35.3.1-$DATE_STR.tar.gz 
         - umount deploy
```

Note that **192.168.1.77** is the internal server of Seeed, which functions as a Samba and Http file server.

### seeed-linux-overlay

For Seeed Jetson reComputer it is the same as the official devkit in software, we just copy [p3509-a02+p3767-0000.conf](./p3509-a02+p3767-0000.conf) to [recomputer-orin.conf ](./recomputer-orin.conf), recomputer-xavier-nx-devkit.conf is copied from p3509-0000+p3668-0001-qspi-emmc.conf.

For Seeed Jetson reComputer Industrial and reServer, 90% of customized hardware dts can be implemented using overlay. But for a small number of [hardware configurations](https://github.com/Seeed-Studio/Linux_for_Tegra/blob/349f0c967f0ed12cdf39248335e0efaaab90381c/source/hardware/nvidia/platform/t23x/p3768/kernel-dts/tegra234-p3767-0000-p3509-a02.dts#L34-L83), there is no root node in the original dts and cannot be achieved through overlay.

This is the overlay file used by reComputer and reServer.

- [xavier-nx-seeed-industry.dts](https://github.com/Seeed-Studio/seeed-linux-dtoverlays/blob/master/overlays/xavier-nx/xavier-nx-seeed-industry.dts)
- [orin-nx-seeed-reserver.dts](https://github.com/Seeed-Studio/seeed-linux-dtoverlays/blob/master/overlays/orin-nx/orin-nx-seeed-reserver.dts)
- [orin-nx-seeed-industry.dts](https://github.com/Seeed-Studio/seeed-linux-dtoverlays/blob/master/overlays/orin-nx/orin-nx-seeed-industry.dts)

The compilation process for seeed-linux-overlay is very simple. You can compile on any Linux host. The prerequisite is to install the two software packages `make` and `device-tree-compiler`.

```
#Step 1: Clone this repo:
git clone https://github.com/Seeed-Studio/seeed-linux-dtoverlays
cd seeed-linux-dtoverlays

Step 2: Compile the code
#On Jetson Orin NX
make all_orin-nx

#On Jetson Xavier NX
make all_xavier-nx
```

After the above process, three files `orin-nx-seeed-industry.dtbo`, `orin-nx-seeed-reserver.dtbo`, `xavier-nx-seeed-industry.dtbo` will be produced, which need to be copied to `Linux_for_Tegra/kernel/dtb/`. Applied to the conf file via the `OVERLAY_DTB_FILE=` field. For example `recomputer-orin-industrial.conf`.

```
source "${LDK_DIR}/p3767.conf.common";
OVERLAY_DTB_FILE="${OVERLAY_DTB_FILE},orin-nx-seeed-industry.dtbo";

...
```

## Compile and build kernel

It is recommended to read the [official NVIDIA documentation](https://docs.nvidia.com/jetson/archives/r35.3.1/DeveloperGuide/text/SD/Kernel/KernelCustomization.html) first. We open source all the Linux source code we use. It is recommended to perform this step on a high-performance PC or server. If compiled on Jetson, it may take more than 4 hours.

1. Download [Bootlin Toolchain gcc 9.3](https://developer.download.nvidia.cn/embedded/L4T/bootlin/aarch64--glibc--stable-final.tar.gz) to /tmp/aarch64--glibc--stable-final.tar.gz.

2. Unzip the compiler and compile the kernel.

```
cd Linux_for_Tegra/source
mkdir gcc
tar xf /tmp/aarch64--glibc--stable-final.tar.gz -C gcc
export CROSS_COMPILE_AARCH64_PATH=`pwd`/gcc
export CROSS_COMPILE_AARCH64=`pwd`/gcc/bin/aarch64-buildroot-linux-gnu-
./nvbuild.sh
```

3. After compilation, `Image` and precompiled ko and dtb files will be generated.

```
...
  LD [M]  sound/soc/generic/snd-soc-audio-graph-card.ko
  LD [M]  sound/soc/generic/snd-soc-simple-card-utils.ko
  LD [M]  sound/soc/tegra-virt-alt/snd-soc-tegra-virt-t210ref-pcm.ko
  LD [M]  sound/soc/generic/snd-soc-simple-card.ko
  LD [M]  sound/soc/tegra-virt-alt/snd-soc-tegra210-virt-alt-admaif.ko
  LD [M]  sound/soc/tegra-virt-alt/snd-soc-tegra210-virt-alt-adsp.ko
  LD [M]  sound/soc/tegra/snd-soc-tegra-audio-graph-card.ko
  LD [M]  sound/soc/tegra/snd-soc-tegra-machine-driver.ko
  LD [M]  sound/soc/tegra/snd-soc-tegra-pcm.ko
  LD [M]  sound/soc/tegra/snd-soc-tegra-utils.ko
  LD [M]  sound/soc/tegra/snd-soc-tegra186-arad.ko
  LD [M]  sound/soc/tegra/snd-soc-tegra186-dspk.ko
  LD [M]  sound/soc/tegra/snd-soc-tegra186-asrc.ko
  LD [M]  sound/soc/tegra/snd-soc-tegra20-spdif.ko
  LD [M]  sound/soc/tegra/snd-soc-tegra210-admaif.ko
  LD [M]  sound/soc/tegra/snd-soc-tegra210-adsp.ko
  LD [M]  sound/soc/tegra/snd-soc-tegra210-adx.ko
  LD [M]  sound/soc/tegra/snd-soc-tegra210-afc.ko
  LD [M]  sound/soc/tegra/snd-soc-tegra210-ahub.ko
  LD [M]  sound/soc/tegra/snd-soc-tegra210-dmic.ko
  LD [M]  sound/soc/tegra/snd-soc-tegra210-amx.ko
  LD [M]  sound/soc/tegra/snd-soc-tegra210-i2s.ko
  LD [M]  sound/soc/tegra/snd-soc-tegra210-iqc.ko
  LD [M]  sound/soc/tegra/snd-soc-tegra210-mixer.ko
  LD [M]  sound/soc/tegra/snd-soc-tegra210-mvc.ko
  LD [M]  sound/soc/tegra/snd-soc-tegra210-ope.ko
  LD [M]  sound/soc/tegra/snd-soc-tegra210-sfc.ko
  LD [M]  sound/tegra-safety-audio/safety-i2s.ko
Kernel sources compiled successfully.
```

## Summary

This software retains the usage and features of the original `Linux_for_Tegra` to the greatest extent, and adds our own BSP and board on this basis.
