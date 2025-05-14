- [Linux_for_Tegra](#Linux_for_Tegra)
    - [Getting Started](#getting-started)
    - [Summary](#summary)

# Linux_for_Tegra

This software is the source code of the default shipping firmware of Seeed Jetson products. It is built on NVIDIA Jetpack 4.6.6 On this basis, additional hardware drivers and boards are added, which is convenient for users to develop their own software and build other Jetson systems, such as Yocto, buildroot, etc.

## Getting Started

1. Download and rootfs source code
```
wget https://developer.nvidia.com/downloads/embedded/l4t/r32_release_v7.6/t210/tegra_linux_sample-root-filesystem_r32.7.6_aarch64.tbz2
```
then
```
rm -rf rootfs/*
sudo tar xpf tegra_linux_sample-root-filesystem_r32.7.6_aarch64.tbz2 -C rootfs/
```

2. apply necessary changes to rootfs
```
sudo ./apply_binaries.sh
```

3. package the image for qspi & nvme flash
sudo BOARDID=3448 BOARDSKU=0002 FAB=400 FUSELEVEL=fuselevel_production ./nvmassflashgen.sh jetson-nano-devkit-emmc mmcblk0p1
