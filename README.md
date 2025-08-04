- [Linux_for_Tegra](#Linux_for_Tegra)
    - [Getting Started](#getting-started)
    - [Summary](#summary)

# Linux_for_Tegra

This software is the source code of the default shipping firmware of Seeed Jetson products. It is built on NVIDIA Jetpack 4.6.6 On this basis, additional hardware drivers and boards are added, which is convenient for users to develop their own software and build other Jetson systems, such as Yocto, buildroot, etc.

# Update PCN
NVIDIA's Jetson modules may periodically update their DDR or EMMC chips. This often results in a PCN patch. You'll need to apply this patch to the source code and rebuild the firmware for the latest Jetson module to work properly. Jetpack 4 is quite old and may require a PCN update. Here's how to update the PCN:
1. Access the PCN Center
```
https://developer.nvidia.com/embedded/jetson-pcn-center
```
2. Download the PCN package for the corresponding module (using PCN211181 as an example).
```
wget https://developer.nvidia.com/downloads/embedded/L4T/r32_Release_v7.5/overlay_32.7.5_PCN211181.tbz2
```
3. Extract the archive to the source code directory
```
sudo tar -xpf overlay_32.7.5_PCN211181.tbz2 -C xxx/Linux_for_Tegra
```

## Getting Started

1. Download and rootfs source code
```
wget https://developer.nvidia.com/downloads/embedded/l4t/r32_release_v7.6/t210/tegra_linux_sample-root-filesystem_r32.7.6_aarch64.tbz2
```
then
```
sudo tar xpf tegra_linux_sample-root-filesystem_r32.7.6_aarch64.tbz2 -C rootfs/
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

