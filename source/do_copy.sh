#!/bin/bash

cp ./kernel_out/kernel-devicetree/generic-dts/dtbs/tegra234-j401-p3768-0000+p3767-0001-recomputer.dtb ../kernel/dtb/

cp  ./kernel_out/kernel/kernel-jammy-src/arch/arm64/boot/Image ../kernel/Image
