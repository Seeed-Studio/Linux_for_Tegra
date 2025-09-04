This package contains the code for libgstjpeg.so library

DEPENDENCIES:
------------
(1) Gstreamer >= 1.2.3

The above dependencies can be obtained from any standard distribution
(like https://launchpad.net/ubuntu) or can be self-built.
This package was built against the (above) libs present in Ubuntu 16.04.
Please note that below instructions will build libgstjpeg.so for aarch64.

1. Pre-Reqs

Install the required packages as below:

sudo apt-get install autotools-dev autoconf autopoint libtool build-essential
sudo apt-get install gstreamer1.0-tools gstreamer-1.0 gstreamer1.0-plugins-base
sudo apt-get install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev
sudo apt-get install libjpeg-dev

INSTALLATION:
-------------
1) Untar the package on target at $HOME and enter the dir
        tar -xjvf gstjpeg_src.tbz2
        cd gstjpeg_src/gst-jpeg/gst-jpeg-1.0/
2) export NOCONFIGURE=true
3) export CFLAGS="-I$HOME/gstjpeg_src/nv_headers -DUSE_TARGET_TEGRA"
4) ./autogen.sh
5) ./configure
6) make
7) sudo cp -i ext/jpeg/.libs/libgstjpeg.so /usr/lib/aarch64-linux-gnu/gstreamer-1.0/
8) cd /usr/lib/aarch64-linux-gnu/
9) Link libgstjpeg.so to Nvidia libnvjpeg.so
        sudo ln -sf tegra/libnvjpeg.so libjpeg.so
        sudo ln -sf tegra/libnvjpeg.so libjpeg.so.8
