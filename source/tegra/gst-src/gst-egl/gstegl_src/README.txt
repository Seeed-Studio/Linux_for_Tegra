This package contains the code for libgstnvegl-1.0.so library

DEPENDENCIES:
------------
(1) EGL
(2) OPENGLES2
(3) Gstreamer >= 1.2.3
(4) X11, Xext

The above target machine dependencies can be obtained from any standard
distribution (like https://launchpad.net/ubuntu) or can be self-built.

Above required packages can also be installed using the following command:

sudo apt-get install \
	autoconf \
	automake \
	autopoint \
	autotools-dev \
	gtk-doc-tools \
	libegl1-mesa-dev \
	libgles2-mesa-dev \
	libgstreamer1.0-dev \
	libgstreamer-plugins-base1.0-dev \
	libgtk2.0-dev \
	libtool \
	libx11-dev \
	libxext-dev \
	pkg-config

This package was built against the (above) libs present in Ubuntu 16.04.

INSTALLATION:
-------------
1) Untar the package and enter the dir
2) ./autogen.sh
3) ./configure
4) make
5) sudo make install

Note:
Pass the appropriate flags and toolchain path to "configure" for tha ARM build.

