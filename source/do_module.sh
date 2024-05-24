#!/usr/bin/expect


spawn ./kernel_out/kernel/kernel-jammy-src/scripts/sign-file sha512  /tmp/rootfs/signing_key.pem /tmp/rootfs/signing_key.x509 ./kernel_out/nvidia-oot/drivers/media/platform/tegra/camera/tegra-camera.ko
interact

spawn ./kernel_out/kernel/kernel-jammy-src/scripts/sign-file sha512  /tmp/rootfs/signing_key.pem /tmp/rootfs/signing_key.x509 ./kernel_out/nvidia-oot/drivers/platform/tegra/rtcpu/ivc-bus.ko



spawn  scp  ./kernel_out/nvidia-oot/drivers/media/platform/tegra/camera/tegra-camera.ko  seeed@10.0.0.50:/tmp
expect "seeed@10.0.0.50's password"
send "seeed\r"
interact

spawn  ssh seeed@10.0.0.50 sudo cp   /tmp/tegra-camera.ko  /usr/lib/modules/5.15.136-tegra/updates/drivers/media/platform/tegra/camera/tegra-camera.ko
expect "seeed@10.0.0.50's password"
send "seeed\r"
interact



spawn  scp  ./kernel_out/nvidia-oot/drivers/platform/tegra/rtcpu/ivc-bus.ko  seeed@10.0.0.50:/tmp
expect "seeed@10.0.0.50's password"
send "seeed\r"
interact

spawn  ssh seeed@10.0.0.50 sudo cp   /tmp/ivc-bus.ko  /usr/lib/modules/5.15.136-tegra/updates/drivers/platform/tegra/rtcpu/ivc-bus.ko
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