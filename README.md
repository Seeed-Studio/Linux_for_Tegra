- [Linux_for_Tegra](#Linux_for_Tegra)
    - [Getting Started](#getting-started)
    - [Summary](#summary)

# Linux_for_Tegra

This software is the source code of the default shipping firmware of Seeed Jetson products. It is built on NVIDIA Jetpack 4.6.3 On this basis, additional hardware drivers and boards are added, which is convenient for users to develop their own software and build other Jetson systems, such as Yocto, buildroot, etc.

## Getting Started

1. Download and rootfs source code
```
wget -O Tegra_Linux_Sample-Root-Filesystem_R32.7.3_aarch64.tbz2 "https://developer.nvidia.com/downloads/remeleasev73t210tegralinusample-root-filesystemr3273aarch64tbz2"
```
then
```
sudo tar xpf Tegra_Linux_Sample-Root-Filesystem_R32.7.3_aarch64.tbz2 -C rootfs/
```

2. apply necessary changes to rootfs
```
sudo ./apply_binaries.sh
```

3. package the image 
```
sudo BOARDID=3448 BOARDSKU=0002 FAB=400 FUSELEVEL=fuselevel_production ./nvmassflashgen.sh jetson-nano-devkit-emmc mmcblk0p1
```
This will generate a compressed file named ``mfi_jetson-nano-devkit-emmc.tbz2``


4. flash
```
tar -xpf mfi_jetson-nano-devkit-emmc.tbz2
sudo ./nvmflash.sh --showlogs
```