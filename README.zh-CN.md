- [Linux\_for\_Tegra](#linux_for_tegra)
	- [支持的硬件](#支持的硬件)
	- [Getting Started](#getting-started)
	- [软件介绍](#软件介绍)
		- [目录结构](#目录结构)
		- [CI/CD](#cicd)
		- [seeed-linux-overlay](#seeed-linux-overlay)
	- [编译构建内核步骤](#编译构建内核步骤)
	- [总结](#总结)


# Linux_for_Tegra

此软件是Seeed Jetson reComputer && reServer等产品默认出货固件的源代码。它基于NV Jetpack 5.1.1构建而来，在这个基础上添加了硬件额外定制的硬件驱动和板子，方便用户二次开发自己的软件以及构建Jetson其他的系统，例如Yocto，buildroot等。



## 支持的硬件

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

请关注**reComputer**,**Industrial**, **reServer**等关键词，如果产品名称中只有reComputer，它代表的是基于NVIDIA Jetson DevKit的载板，产品的形态跟官方devkit是一样的。如果产品名称是reComputer + Industrial，它代表的是[这种](https://wiki.seeedstudio.com/reComputer_Industrial_Getting_Started/)形态的产品。如果产品名称是reServer + Industrial，它代表的是[这种](https://wiki.seeedstudio.com/reServer_Industrial_Getting_Started/)形态的产品。

## Getting Started
1. Download the latest Jetson Linux release package and sample file system for your Jetson developer kit from https://developer.nvidia.com/linux-tegra

2. Enter the following commands to untar the files and assemble the rootfs:
```
$ tar xf ${L4T_RELEASE_PACKAGE}
$ sudo tar xpf ${SAMPLE_FS_PACKAGE} -C Linux_for_Tegra/rootfs/
$ cd Linux_for_Tegra/
$ sudo ./apply_binaries.sh
$ sudo ./tools/l4t_flash_prerequisites.sh
```
3. Copy BSP file. for other product please refer to [CI/CD](#cicd).
```
cp extra_scripts/reserver_industrial/Image kernel/
cp extra_scripts/reserver_industrial/tegra234-p3767-0003-p3509-a02.dtb kernel/dtb/
cp extra_kernel_modules/lan743x.ko rootfs/lib/modules/5.10.104-tegra/kernel/drivers/net/ethernet/microchip/lan743x.ko
cp extra_kernel_modules/spi-tegra114.ko rootfs/lib/modules/5.10.104-tegra/kernel/drivers/spi/spi-tegra114.ko
cp extra_kernel_modules/8723du.ko rootfs/lib/modules/5.10.104-tegra/kernel/drivers/net/wireless/realtek/rtl8xxxu/
```

4. Ensure that your Jetson developer kit is configured and connected to your Linux host as described in Assumptions.


5. Confirm that the developer kit is in Force Recovery Mode by following the procedure To determine whether the developer kit is in force recovery mode.

6. Enter this command on your Linux host to install (flash) the Jetson release onto the Jetson developer kit.

```
sudo ./tools/kernel_flash/l4t_initrd_flash.sh --external-device nvme0n1p1 -c tools/kernel_flash/flash_l4t_nvme.xml -S 80GiB  -p "-c bootloader/t186ref/cfg/flash_t234_qspi.xml --no-systemimg" --network usb0  reserver-orin-industrial  external 
```
7. The Jetson developer kit automatically reboots when the installation process is complete. At this point your Jetson developer kit is operational. Follow the prompts on the display to set up a user account and log in.


## 软件介绍
这里从下面三个方面介绍这套软件的内容

### 目录结构
相比原始的Linux_for_Tegra，我们新增了下面文件夹和文件。
- extra_kernel_modules： 这里存放了，我们定制的内核模块binary文件。
- extra_scripts： 这里存放了，我们定制了内核以及在系统构建过程中的必要的shell脚本文件。

虽然Seeed基于Jetson的产品比较多，但是只用到了下面5个配置文件。详细对应关系请见[.gitlab-ci.yml](./.gitlab-ci.yml).
- recomputer-xavier-nx-industrial.conf
- reserver-orin-industrial.conf
- recomputer-orin.conf
- recomputer-orin-industrial.conf
- recomputer-xavier-nx-devkit.conf

### CI/CD
对于Seeed的Jetson的产品，都是使用SSD作为系统存储，因此固件都只是支持SSD启动。因为SSD烧录的特殊性，我们尝试过采用NVIDIA官方文档中通过USB在生产产线进行批量生产，但是生产效率特别低，无法满足大规模生产的需求。经过我们的研究，采用spiflash + ssd两阶段烧录是最合适的方式，在Seeed 目前的Jetson的生产过程固件的烧录步骤如下：
1. 烧录最小的qspiflash镜像
2. 安装SSD(**SSD已经通过专业SSD克隆服务，烧录了SSD镜像**)

CI/CD,由Seeed内部的Gitlab使用的服务，它自动的生产满足上述需求的固件，并存放在Samba文件服务器中。它的配置文件非常具有参考价值，可以帮助用户二次生产量产镜像，方便使用Seeed量产服务。下面以job-orin-nano-reserver-8g为例介绍，固件生产过程。
1. 标准的Gitlab CI字段。
```
job-orin-nano-reserver-8g:
    stage: orin-nano-reserver-8g
    when: on_success
    tags:
      - arm64-shell

    needs: [job-orin-nano-industrial-8g]
```
2. 安装rootfs，这是[NVIDIA Jetson文档](https://docs.nvidia.com/jetson/archives/r35.3.1/DeveloperGuide/text/IN/QuickStart.html#to-flash-the-jetson-developer-kit-operating-software)中介绍的标准步骤。[`Tegra_Linux_Sample-Root-Filesystem_R35.3.1_aarch64.tbz2`](https://developer.nvidia.com/downloads/embedded/l4t/r35_release_v3.1/release/tegra_linux_sample-root-filesystem_r35.3.1_aarch64.tbz2/) 来自NVIDIA文档。
```
    script:
         - id
         - export DATE_STR=$(TZ='Asia/Hong_Kong' date +%Y-%m-%d)
         - wget  http://192.168.1.77/jetson/Tegra_Linux_Sample-Root-Filesystem_R35.3.1_aarch64.tbz2 -q
         - tar xpf Tegra_Linux_Sample-Root-Filesystem_R35.3.1_aarch64.tbz2 -C rootfs
         - ./apply_binaries.sh
         - ./tools/l4t_flash_prerequisites.sh
```
3. 生成qspiflash固件，它很小，大概只有2G。在工厂生产通过USB线在1分钟内就能完成烧录。
```
         - sudo  BOARDID=3767 BOARDSKU=0003 FAB=RC1  BOARDREV=B.6   ./tools/kernel_flash/l4t_initrd_flash.sh   -p "-c bootloader/t186ref/cfg/flash_t234_qspi.xml --no-systemimg" --no-flash  --massflash 5  --network usb0  reserver-orin-industrial external 
         - mkdir deploy
         - mount -t cifs -o username=$SMB_USER,password=$SMB_PWD,vers=3.0,uid=1000,gid=1000,rw,file_mode=0664 //192.168.1.77/red_2t/jetson deploy
         - cp  mfi_reserver-orin-industrial.tar.gz  deploy/mfi_reserver-orin-nano-8g-industrial-qspiflash-5.1-35.3.1-$DATE_STR.tar.gz 
```
4. 这一步是固件的核心定制步骤，它会下面几件事情：
  
	- 安装nvidia-jetpack
	- 应用对应的BSP二进制文件
	- 清理工作文件夹
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
5. 生成量产需要mfi固件。Jetson Jetpack 5.x以后生成量产固件需要的参数非常多，这里的参数可以帮助后续对相关衍生产品。
```
         - sudo  BOARDID=3767 BOARDSKU=0003 FAB=RC1  BOARDREV=B.6   ./tools/kernel_flash/l4t_initrd_flash.sh --external-device nvme0n1p1 -c tools/kernel_flash/flash_l4t_nvme.xml -S 80GiB  -p "-c bootloader/t186ref/cfg/flash_t234_qspi.xml --no-systemimg" --no-flash  --massflash 5  --network usb0  reserver-orin-industrial  external 
         - cp  mfi_reserver-orin-industrial.tar.gz  deploy/mfi_reserver-orin-nano-8g-industrial-5.1-35.3.1-$DATE_STR.tar.gz 
         - umount deploy
```

请注意，这里**192.168.1.77**是Seeed内部的服务器，它具备Samba和Http文件服务器的功能。

### seeed-linux-overlay
对于Seeed Jetson reComputer 来说它在软件上跟官方devkit一样，我们只是将[p3509-a02+p3767-0000.conf](./p3509-a02+p3767-0000.conf)复制为[recomputer-orin.conf](./recomputer-orin.conf)，recomputer-xavier-nx-devkit.conf 复制于p3509-0000+p3668-0001-qspi-emmc.conf。

对于Seeed Jetson reComputer Industrial和reServer 90%定制的硬件的dts都可以用overlay来实现。但是少部分[硬件配置](https://github.com/Seeed-Studio/Linux_for_Tegra/blob/349f0c967f0ed12cdf39248335e0efaaab90381c/source/hardware/nvidia/platform/t23x/p3768/kernel-dts/tegra234-p3767-0000-p3509-a02.dts#L34-L83) 在原始的dts中没有根节点，无法通过overlay来实现。

这是reComputer和reServer用到的overlay文件。

- [xavier-nx-seeed-industry.dts](https://github.com/Seeed-Studio/seeed-linux-dtoverlays/blob/master/overlays/xavier-nx/xavier-nx-seeed-industry.dts)
- [orin-nx-seeed-reserver.dts](https://github.com/Seeed-Studio/seeed-linux-dtoverlays/blob/master/overlays/orin-nx/orin-nx-seeed-reserver.dts)
- [orin-nx-seeed-industry.dts](https://github.com/Seeed-Studio/seeed-linux-dtoverlays/blob/master/overlays/orin-nx/orin-nx-seeed-industry.dts)

对于seeed-linux-overlay编译过程在非常简单, 你可以在任何linux主机上进行编译，前提要安装`make`和`device-tree-compiler`两个软件包。
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

上述过程后会生产`orin-nx-seeed-industry.dtbo`, `orin-nx-seeed-reserver.dtbo`, `xavier-nx-seeed-industry.dtbo`三个文件，需要拷贝到`Linux_for_Tegra/kernel/dtb/`。 通过`OVERLAY_DTB_FILE=` 字段应用到conf文件中。例如`recomputer-orin-industrial.conf`。
```
source "${LDK_DIR}/p3767.conf.common";
OVERLAY_DTB_FILE="${OVERLAY_DTB_FILE},orin-nx-seeed-industry.dtbo";

...
```

## 编译构建内核步骤
建议先阅读[NVIDIA官方文档](https://docs.nvidia.com/jetson/archives/r35.3.1/DeveloperGuide/text/SD/Kernel/KernelCustomization.html). 我们开源了所有我们用到的linux源码，建议在高性能的PC或者服务器上进行这个步骤，如果在Jetson上这个编译，可能会耗费4个小时以上。
1. 下载[Bootlin Toolchain gcc 9.3 ](https://developer.download.nvidia.cn/embedded/L4T/bootlin/aarch64--glibc--stable-final.tar.gz)到/tmp/aarch64--glibc--stable-final.tar.gz.
2. 解压编译器，编译内核。
```
cd Linux_for_Tegra/source
mkdir gcc
tar xf /tmp/aarch64--glibc--stable-final.tar.gz -C gcc
export CROSS_COMPILE_AARCH64_PATH=`pwd`/gcc
export CROSS_COMPILE_AARCH64=`pwd`/gcc/bin/aarch64-buildroot-linux-gnu-
./nvbuild.sh
```
3. 编译完以后会生成`Image`和预编译的ko，dtb文件。
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
## 总结

此软件最大程度保留了原始`Linux_for_Tegra`的使用方法和特点，在这个基础上添加了我们自己的BSP和board。
