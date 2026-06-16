#!/bin/bash
# do_copy.sh - deploy the freshly built kernel Image and device-tree blobs
# into ../kernel/ so that flash.sh / l4t_initrd_flash.sh pick them up.
#
# Combined from the R36.5.0 (Orin / tegra234) and R38.4.0 (Thor / tegra264)
# do_copy.sh, adapted to the JP7.2 (L4T r39) build output layout:
#   - device trees : kernel_out/build/nvidia-public/devicetree/generic-dtbs/
#                    (r36/r38 used kernel_out/kernel-devicetree/generic-dts/dtbs/)
#   - kernel Image : kernel_out/kernel/kernel-noble/arch/arm64/boot/Image
#                    (r36 used kernel-jammy-src; JP7 uses kernel-noble)
#
# nvbuild.sh produces device trees for every supported board (both tegra234
# and tegra264) in one run, so we copy all produced .dtb/.dtbo here; flash.sh
# selects the one matching the board .conf at flash time. Missing files are
# tolerated so a partial / single-SoC build does not abort this script.

DTB_SRC="./kernel_out/build/nvidia-public/devicetree/generic-dtbs"
DTB_DST="../kernel/dtb"
IMG_SRC="./kernel_out/kernel/kernel-noble/arch/arm64/boot/Image"
IMG_DST="../kernel/Image"

mkdir -p "$DTB_DST"

# board device trees + overlays (Thor t264 and Orin t234)
cp -f "$DTB_SRC"/*.dtb  "$DTB_DST"/ 2>/dev/null || true
cp -f "$DTB_SRC"/*.dtbo "$DTB_DST"/ 2>/dev/null || true

# kernel Image
cp -f "$IMG_SRC" "$IMG_DST"
