###############################################################################
#
# Copyright (c) 2018, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA Corporation is strictly prohibited.
#
###############################################################################

Steps to compile the "gst-nvvideo4linux2" sources natively:

1) Install gstreamer related packages on target using the command:

	sudo apt-get install libgstreamer1.0-dev \
		gstreamer1.0-plugins-base \
		gstreamer1.0-plugins-good \
		libgstreamer-plugins-base1.0-dev \
		libv4l-dev \
		libegl1-mesa-dev

2) Download and extract the package "gst-nvvideo4linux_src.tbz2" as follow:

	tar -I lbzip2 -xvf gst-nvvideo4linux2_src.tbz2

3) Run the following commands to build and install "libgstnvvideo4linux2.so":
	make
	make install
	or
	DEST_DIR=<dir> make install

  Note: For Jetson, "make install" will copy library "libgstnvvideo4linux2.so"
  into "/usr/lib/aarch64-linux-gnu/gstreamer-1.0" directory. For x86 platforms,
  make install will copy the library "libgstnvvideo4linux2.so" into
  /opt/nvidia/deepstream/deepstream-4.0/lib/gst-plugins
