#!/bin/bash
set -x
 
echo 'Acquire::http::Proxy "http://192.168.1.77:3142";' >> /etc/apt/apt.conf

apt-get update
apt install -y nvidia-jetpack
apt install -y pulseaudio-module-bluetooth
systemctl disable nvgetty.service
apt-get clean
rm /etc/apt/apt.conf

dpkg -i \
  /glib/libglib2.0-0_2.72.4-0ubuntu2.4~portwell1_arm64.deb \
  /glib/libglib2.0-bin_2.72.4-0ubuntu2.4~portwell1_arm64.deb \
  /glib/libglib2.0-data_2.72.4-0ubuntu2.4~portwell1_all.deb

dpkg -i \
  /glib/libglib2.0-dev_2.72.4-0ubuntu2.4~portwell1_arm64.deb \
  /glib/libglib2.0-dev-bin_2.72.4-0ubuntu2.4~portwell1_arm64.deb \
  /glib/libglib2.0-doc_2.72.4-0ubuntu2.4~portwell1_all.deb \
  /glib/libglib2.0-tests_2.72.4-0ubuntu2.4~portwell1_arm64.deb

sudo apt-mark hold nvidia-l4t-display-kernel nvidia-l4t-kernel nvidia-l4t-kernel-dtbs nvidia-l4t-kernel-headers nvidia-l4t-kernel-oot-headers nvidia-l4t-kernel-oot-modules
