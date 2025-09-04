# Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#  * Neither the name of NVIDIA CORPORATION nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Steps to compile the "gst-nvarguscamera" sources natively:

1) Install gstreamer related packages on target using the command:

	sudo apt-get install libgstreamer1.0-dev \
		gstreamer1.0-plugins-base \
		gstreamer1.0-plugins-good \
		libgstreamer-plugins-base1.0-dev \
		libegl1-mesa-dev

2) Install "jetson_multimedia_api" package from latest Jetpack release.

3) Download and extract the package "gst-nvarguscamera_src.tbz2" as follow:

	tar -I lbzip2 -xvf gst-nvarguscamera_src.tbz2

3) Run the following commands to build and install "libgstnvarguscamerasrc.so":
	cd "gst-nvarguscamera"
	make
	make install
	or
	DEST_DIR=<dir> make install

  Note: "make install" will copy library "libgstnvarguscamerasrc.so"
  into "/usr/lib/aarch64-linux-gnu/gstreamer-1.0" directory.
