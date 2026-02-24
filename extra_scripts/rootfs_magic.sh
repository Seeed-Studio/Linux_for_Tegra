#!/bin/bash
set -x
 
echo 'Acquire::http::Proxy "http://192.168.1.77:3142";' >> /etc/apt/apt.conf

apt-get update
apt install -y nvidia-jetpack
apt install -y pulseaudio-module-bluetooth
systemctl disable nvgetty.service
apt-get clean
rm /etc/apt/apt.conf

cat > /etc/apt/apt.conf.d/99disable-auto-updates << EOF
APT::Periodic::Update-Package-Lists "0";
APT::Periodic::Download-Upgradeable-Packages "0";
APT::Periodic::AutocleanInterval "0";
APT::Periodic::Unattended-Upgrade "0";
EOF
sudo apt-get remove --purge -y update-manager update-notifier
sudo apt-mark hold nvidia-l4t-display-kernel nvidia-l4t-kernel nvidia-l4t-kernel-dtbs nvidia-l4t-kernel-headers nvidia-l4t-kernel-oot-headers nvidia-l4t-kernel-oot-modules nvidia-l4t-initrd
