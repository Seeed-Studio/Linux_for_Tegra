                    README

v4l2_libs_src.tbz2 has following structure:
- include
- libv4l2
- libv4lconvert
- v4l2libs_README.txt
- LICENSE.v4l2libs

This tar contains sources for following two libraries:
- libnvv4l2
- libnvv4lconvert

1) Steps for building libnvv4lconvert:

    cd "libv4lconvert"
    make
    sudo make install

    Above steps will build and install libnvv4lconvert.so at
    /usr/lib/aarch64-linux-gnu/tegra

2) Steps for building libnvv4l2:

    Build and install libnvv4lconvert as mentioned above.
    cd "libv4l2"
    make
    sudo make install

    Above steps will build and install libnvv4l2.so at
    /usr/lib/aarch64-linux-gnu/tegra
