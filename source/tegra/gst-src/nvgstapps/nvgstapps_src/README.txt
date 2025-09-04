
This file explains the procedure to compile NvGstApps sources for hardfp(armhf)
ARM architecture.


--------------------------------------------------------------------------------
                            Prerequisites for nvgst-1.0 applications
--------------------------------------------------------------------------------
For nvgstcapture-1.0 and nvgstplayer-1.0 applications:

* You must install GStreamer-1.0 on the target board using apt-get, as follows:

  sudo apt-get install gstreamer1.0-tools gstreamer1.0-alsa \
     gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
     gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly \
     gstreamer1.0-libav libgstreamer1.0-dev


* Download or copy the nvgstapps_src.tbz2 file on device and untar it.
  tar -xpf nvgstapps_src.tbz2


* Compile nvgstapps with following procedures.
--------------------------------------------------------------------------------
                            Procedure to compile nvgstcapture-1.0:
--------------------------------------------------------------------------------

 On the target, execute the following commands:

 sudo apt-get install libgstreamer-plugins-base1.0-dev
 sudo apt-get install libegl1-mesa-dev
 sudo apt-get install libx11-dev libxext-dev

 As the above steps will overwrite the sym-links to the hardware accelerated libegl
 binary, to point back to the tegra version, execute the following commands:

 Export ARM application binary interface based on the Linux and ARM platform:
 Linux 64bit userspace support:
    export TEGRA_ARMABI=aarch64-linux-gnu
 Linux 32bit userspace and ARM hardfp support:
    export TEGRA_ARMABI=arm-linux-gnueabihf

 cd nvgstapps_src/nvgst_sample_apps/nvgstcapture-1.0
 gcc nvgstcapture.c nvgst_x11_common.c -o nvgstcapture-1.0 \
   $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-plugins-base-1.0 \
   gstreamer-pbutils-1.0 x11 xext gstreamer-video-1.0) -ldl

--------------------------------------------------------------------------------
                            Procedure to compile nvgstplayer-1.0:
--------------------------------------------------------------------------------

 On the target, execute the following commands:

 cd nvgstapps_src/nvgst_sample_apps/nvgstplayer-1.0
 gcc nvgstplayer.c nvgst_x11_common.c nvgst_asound_common.c -o nvgstplayer-1.0 \
   $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-plugins-base-1.0 \
   gstreamer-pbutils-1.0 gstreamer-video-1.0 x11 xext alsa)


* For nvgstcapture-1.0 usage, refer to
  nvgstapps_src/nvgst_sample_apps/nvgstcapture-1.0/nvgstcapture-1.0_README.txt

* For nvgstplayer-1.0 usage, refer to
  nvgstapps_src/nvgst_sample_apps/nvgstplayer-1.0/nvgstplayer-1.0_README.txt
