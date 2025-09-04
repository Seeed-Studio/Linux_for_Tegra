# Firmware compilation process:
The firmware generation process can refer to the `.gitlab.cl` file, which is the process of automatically generating firmware by the SEEED internal server.

## 1. Download the official BSP

    wget https://developer.nvidia.com/downloads/embedded/l4t/r35_release_v6.1/release/jetson_linux_r35.6.1_aarch64.tbz2

    tar -xjvf jetson_linux_r35.6.1_aarch64.tbz2 -c xxx

## 2. Overwrite Seeed's BSP with the official BSP

    git clone -b r35.6.1 https://github.com/Seeed-Studio/Linux_for_Tegra.git

    cp -rf Linux_for_Tegra/* xxx/Linux_for_Tegra/

## 3. Prepare to start compiling

    cd xxx/Linux_for_Tegra/

    wget https://developer.nvidia.com/downloads/embedded/l4t/r35_release_v6.1/tegra_linux_sample-root-filesystem_r35.6.1_aarch64.tbz2

    sudo tar xpf tegra_linux_sample-root-filesystem_r35.6.1_aarch64.tbz2 -C rootfs

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