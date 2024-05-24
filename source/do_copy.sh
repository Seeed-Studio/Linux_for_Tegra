#!/bin/bash

cp   ./kernel_out/nvidia-oot/device-tree/platform/generic-dts/dtbs/tegra234-j501x-0000+p3701-0000-reserver.dtb  ../kernel/dtb/
cp   ./kernel_out/nvidia-oot/device-tree/platform/generic-dts/dtbs/tegra234-j501x-0000+p3701-0004-reserver.dtb  ../kernel/dtb/
cp   ./kernel_out/nvidia-oot/device-tree/platform/generic-dts/dtbs/tegra234-j501x-0000+p3701-0005-reserver.dtb  ../kernel/dtb/
cp   ./kernel_out/nvidia-oot/device-tree/platform/generic-dts/dtbs/tegra234-j501x-0000+p3701-0000-reserver-gmsl.dtb  ../kernel/dtb/
cp   ./kernel_out/nvidia-oot/device-tree/platform/generic-dts/dtbs/tegra234-j501x-0000+p3701-0004-reserver-gmsl.dtb  ../kernel/dtb/
cp   ./kernel_out/nvidia-oot/device-tree/platform/generic-dts/dtbs/tegra234-j501x-0000+p3701-0005-reserver-gmsl.dtb  ../kernel/dtb/
cp  ./kernel_out/kernel/kernel-jammy-src/arch/arm64/boot/Image ../kernel/Image
