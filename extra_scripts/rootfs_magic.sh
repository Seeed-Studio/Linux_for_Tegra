 #!/bin/bash
 set -x

echo 'Acquire::http::Proxy "http://192.168.1.77:3142";' >> /etc/apt/apt.conf

apt-get update
apt install -y nvidia-jetpack 
apt install -y pulseaudio-module-bluetooth
systemctl disable nvgetty.service
apt-get clean
rm /etc/apt/apt.conf

