# Firmware compilation process:
The readme file here shows the overall process of building a firmware package. For some boards, you may need to copy additional device trees or driver files to the rootfs file to build the firmware package properly. Therefore, before executing the following content, please open the `.gitlab-ci.yml` file, find the carrier board you want, and then build the firmware package according to the `.gitlab-ci.yml` content.

## 1. Download the official BSP

    wget https://developer.nvidia.com/downloads/embedded/l4t/r35_release_v5.0/release/jetson_linux_r35.5.0_aarch64.tbz2

    tar -xjvf Jetson_Linux_R35.5.0_aarch64.tbz2 -c xxx

## 2. Overwrite Seeed's BSP with the official BSP

    git clone -b r35.5.0 https://github.com/Seeed-Studio/Linux_for_Tegra.git

    cp -rf Linux_for_Tegra/* xxx/Linux_for_Tegra/

## 3. Prepare to start compiling

    cd xxx/Linux_for_Tegra/

    wget https://developer.nvidia.com/downloads/embedded/l4t/r35_release_v5.0/release/tegra_linux_sample-root-filesystem_r35.5.0_aarch64.tbz2

    sudo tar xpf tegra_linux_sample-root-filesystem_r35.5.0_aarch64.tbz2 -C rootfs

    sudo ./apply_binaries.sh

    wget https://developer.nvidia.com/embedded/jetson-linux/bootlin-toolchain-gcc-93

    mkdir -p l4t-gcc

    tar xf bootlin-toolchain-gcc-93 -C ./l4t-gcc

    export ARCH=arm64

    export CROSS_COMPILE_AARCH64_PATH=`realpath .`/l4t-gcc

    cd source/

    export INSTALL_MOD_PATH=`realpath ../rootfs/`

    ./nvbuild.sh -i -r ${INSTALL_MOD_PATH}

    ./do_copy.sh

    cd ..

    sudo sed -i "s/<SOC>/t194/g" rootfs/etc/apt/sources.list.d/nvidia-l4t-apt-source.list

## 4. Add some drivers of seeed && Pre-installed nvidia-jetpack
    mount --bind /sys ./rootfs/sys

    mount --bind /dev ./rootfs/dev

    mount --bind /dev/pts ./rootfs/dev/pts

    mount --bind /proc ./rootfs/proc

    cp /usr/bin/qemu-aarch64-static rootfs/usr/bin/

    cp extra_scripts/rootfs_magic.sh rootfs

    chroot rootfs /rootfs_magic.sh

    cp extra_scripts/recomputer_industrial/xavier-nx-seeed-industry.dtbo  kernel/dtb/
   
    KER_NUM=`basename $(find ./rootfs/lib/modules -mindepth 1 -maxdepth 1 -type d)`
   
    mkdir -p rootfs/lib/modules/$KER_NUM/kernel/drivers/net/wireless/realtek/rtw88
   
    cp extra_kernel_modules/rtl8723du/*.ko rootfs/lib/modules/$KER_NUM/kernel/drivers/net/wireless/realtek/rtw88/
   
    sudo chroot rootfs depmod -a  $KER_NUM

    umount ./rootfs/sys

    umount ./rootfs/dev/pts

    umount ./rootfs/dev

    umount ./rootfs/proc

    rm rootfs/rootfs_magic.sh
    
    rm rootfs/usr/bin/qemu-aarch64-static
## 5. Make package

### 5.1 For xavier-nx :
    sudo ADDITIONAL_DTB_OVERLAY_OPT="BootOrderNvme.dtbo"  BOARDID=3668 BOARDSKU=0003 FAB=301  BOARDREV=F.0 ./tools/kernel_flash/l4t_initrd_flash.sh --external-device nvme0n1p1 -c tools/kernel_flash/flash_l4t_nvme.xml -S 80GiB  -p "-c bootloader/t186ref/cfg/flash_l4t_t194_qspi_p3668.xml --no-systemimg"   --no-flash  --massflash 5 --network usb0 recomputer-xavier-nx-industrial external

### 5.2 For others:
    sudo  BOARDID=3767 BOARDSKU=0004 FAB=300  BOARDREV=N.2  CHIP_SKU=00:00:00:D6  ./tools/kernel_flash/l4t_initrd_flash.sh --external-device nvme0n1p1 -c tools/kernel_flash/flash_l4t_nvme.xml -S 80GiB  -p "-c bootloader/t186ref/cfg/flash_t234_qspi.xml --no-systemimg" --no-flash  --massflash 5  --network usb0  recomputer-orin  external 
 
Different modules have different BOARDID BOARDSKU FAB BOARDREV CHIP_SKU parameters. You can find them on the official website. Different carrier boards correspond to different configuration files, such as ``recomputer-orin / recomputer-xavier-nx-industrial``, etc. Select the corresponding parameters according to your carrier board.

# OTA
According to NVIDIA's official tool description, the OTA process is mainly divided into two steps:
- Generate OTA package
- OTA on Jetson devices
## Generate OTA package
We provide a `start_generate_ota_pkg.sh` script to easily generate OTA packages. The following is an example of generating an OTA package:
1.Prepare the `BASE_BSP` source code. `BASE_BSP` is the `Linux_for_Tegra` source code before executing OTA. You can download it on our github
2.Prepare the source code of `TARGET_BSP`. `TARGET_BSP` is the `Linux_for_Tegra` source code after executing OTA. You can download it from our github. After downloading, be sure to set up the rootfs and the system configuration you want. For details, see the readme of the corresponding version.
3.Go to the `Linux_for_Tegra/tools/ota_tools` directory and execute `start_generate_ota_pkg.sh`. It will ask you to enter the path of `BASE_BSP`, the path of `TARGET_BSP`, the name of the carrier board to be OTA, and the jectapck version number before OTA. Then it will generate `ota_payload_package.tar.gz` in the bootloader directory.

```
cd ~/JP5.1.3/Linux_for_Tegra/tools/ota_tools/
./start_generate_ota_pkg.sh

=== OTA Environment Initialization ===
Enter q at any prompt to quit

Enter BASE_BSP path: /home/pss/mydisk/D/jetson/JP5.1.1/Linux_for_Tegra
Enter TARGET_BSP path: /home/pss/mydisk/D/jetson/JP5.1.3/Linux_for_Tegra
Enter target_board name: recomputer-orin-j40mini
Enter bsp_version (Rmm-n): R35-3
...
...
SUCCESS: generate OTA package at "/home/pss/mydisk/D/jetson/JP5.1.3/Linux_for_Tegra/bootloader/recomputer-orin-j40mini/ota_payload_package.tar.gz"

```

## OTA on Jetson devices
1.Install dependent software
```
sudo apt-get update
sudo apt-get install efibootmgr nvme-cli
```
2.Create a directory to store the files generated during the OTA update process and set the `WORKDIR` environment variable to the full pathname of this directory.
```
mkdir temp
export WORKDIR=/home/user/temp/
```
3.Put the OTA tool in the ${WORKDIR}/Linux_for_Tegra/tools/ directory. This OTA tool can copy the tools on the host, for example:
```
cd ${WORKDIR} 
mkdir -p Linux_for_Tegra/tools/
cd ${WORKDIR}/Linux_for_Tegra/tools/
scp -r pss@192.168.100.200:/home/pss/mydisk/D/jetson/JP5.1.3/Linux_for_Tegra/tools/ota_tools .
```
4.Create a /ota/ directory and place the `ota_payload_package.tar.gz` OTA payload package in the /ota/ directory.
```
mkdir /ota/
cd /ota/
sudo scp pss@192.168.100.200:/home/pss/mydisk/D/jetson/JP5.1.3/Linux_for_Tegra/bootloader/recomputer-orin-j40mini/ota_payload_package.tar.gz .
```
5.Start OTA. 
```
cd ${WORKDIR}/Linux_for_Tegra/tools/ota_tools/version_upgrade
sudo ./nv_ota_start.sh /ota/ota_payload_package.tar.gz

```
If there is no error after executing here, restart Jetson.

## Keep the files before OTA
After OTA is completed, the system will be replaced with the system in `TARGET_BSP`, and the files in the original board will be lost. If you want to keep some files in the original system, please use the `nv_ota_preserve_data.sh` script in the OTA tool, which is located in the `Linux_for_Tegra/tools/ota_tools/version_upgrade/` directory. This script reads the content of `ota_backup_files_list.txt` and selects the files to be saved by editing the `ota_backup_files_list.txt` file:

```
# All the files or directories should be listed with absolute path
# Example:
 etc/passwd
 opt/nvidia
 ...
```
