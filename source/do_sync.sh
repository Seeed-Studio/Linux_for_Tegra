#!/usr/bin/expect
#vi  /etc/sudoers
#%sudo    ALL=(ALL) NOPASSWD: ALL

spawn  scp ./kernel_out/nvidia-oot/device-tree/platform/generic-dts/dtbs/tegra234-j501x-0000+p3701-0004-reserver.dtb  seeed@10.0.0.50:/tmp
expect "seeed@10.0.0.50's password"
send "seeed\r"
interact


spawn  scp ./kernel_out/nvidia-oot/device-tree/platform/generic-dts/dtbs/tegra234-j501x-0000+p3701-0005-reserver.dtb  seeed@10.0.0.50:/tmp
expect "seeed@10.0.0.50's password"
send "seeed\r"
interact

spawn  scp ./kernel_out/nvidia-oot/device-tree/platform/generic-dts/dtbs/tegra234-j501x-0000+p3701-0000-reserver.dtb  seeed@10.0.0.50:/tmp
expect "seeed@10.0.0.50's password"
send "seeed\r"
interact

spawn  scp ./kernel_out/nvidia-oot/device-tree/platform/generic-dts/dtbs/tegra234-j501x-0000+p3701-0004-reserver-gmsl.dtb  seeed@10.0.0.50:/tmp
expect "seeed@10.0.0.50's password"
send "seeed\r"
interact


spawn  scp ./kernel_out/nvidia-oot/device-tree/platform/generic-dts/dtbs/tegra234-j501x-0000+p3701-0005-reserver-gmsl.dtb  seeed@10.0.0.50:/tmp
expect "seeed@10.0.0.50's password"
send "seeed\r"
interact

spawn  scp ./kernel_out/nvidia-oot/device-tree/platform/generic-dts/dtbs/tegra234-j501x-0000+p3701-0000-reserver-gmsl.dtb  seeed@10.0.0.50:/tmp
expect "seeed@10.0.0.50's password"
send "seeed\r"
interact

spawn  scp ./kernel_out/kernel/kernel-jammy-src/arch/arm64/boot/Image  seeed@10.0.0.50:/tmp
expect "seeed@10.0.0.50's password"
send "seeed\r"
interact

spawn  ssh seeed@10.0.0.50 sudo cp   /tmp/tegra234-j501x-0000+p3701-000*  /boot
expect "seeed@10.0.0.50's password"
send "seeed\r"
interact

spawn  ssh seeed@10.0.0.50 sudo cp   /tmp/Image  /boot/Image-new
expect "seeed@10.0.0.50's password"
send "seeed\r"
interact

spawn  ssh seeed@10.0.0.50 sudo sync
expect "seeed@10.0.0.50's password"
send "seeed\r"
interact

spawn  ssh seeed@10.0.0.50 sudo reboot
expect "seeed@10.0.0.50's password"
send "seeed\r"
interact