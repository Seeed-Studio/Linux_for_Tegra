 #!/bin/bash
echo 'Acquire::http::Proxy "http://192.168.1.77:3142";' >> /etc/apt/apt.conf

apt-get update
apt install -y nvidia-jetpack pulseaudio-module-bluetooth
systemctl disabled nvgetty.service
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

