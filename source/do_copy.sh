#!/bin/bash
cp ./kernel_out/kernel-devicetree/generic-dts/dtbs/tegra264-p4071-0000+p3834-0008-nv.dtb ../kernel/dtb/
cp ./kernel_out/kernel-devicetree/generic-dts/dtbs/tegra264-p4071-0000+p3834-0008-recomputer-carrier.dtb ../kernel/dtb/
cp ./kernel_out/kernel-devicetree/generic-dts/dtbs/tegra264-p4071-0000+p3834-0000-recomputer-carrier.dtb ../kernel/dtb/
cp ./kernel_out/kernel-devicetree/generic-dts/dtbs/tegra264-p4071-camera-* ../kernel/dtb/

cp  ./kernel_out/kernel/kernel-noble/arch/arm64/boot/Image ../kernel/Image
