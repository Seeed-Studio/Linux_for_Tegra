**********************************************************************
NVIDIA Jetson Linux (L4T) OP-TEE Package
**********************************************************************

----------------------------------------------------------------------
Introduction
----------------------------------------------------------------------
This package contains the necessary files and instructions to build a
trusted OS image based on ATF and OP-TEE for these Jetson devices:
- Jetson Xavier NX
- Jetson AGX Xavier series
- Jetson AGX Orin series

----------------------------------------------------------------------
Prerequisites
----------------------------------------------------------------------
Please refer to the link below to install build prerequisites, e.g.
python3-pycryptodome and python3-pyelftools, in your build machine.

https://optee.readthedocs.io/en/latest/building/prerequisites.html

----------------------------------------------------------------------
The python cryptography issue
----------------------------------------------------------------------
When building OP-TEE sources, you may see a python error in sign_encrypt.py like:
"TypeError: public_key() missing 1 required positional argument: 'backend'"

This is because the "cryptography" python package in your build system is too old.
Running the following commands can fix the issue:

sudo apt remove python3-cryptography
pip3 install cryptography

----------------------------------------------------------------------
Placeholders used in this document
----------------------------------------------------------------------
This document uses a placeholder, "<platform>", to indicate Jetson platforms.
Its possible values are:
- 194
- 234
Choose the platform value according to your Jetson board to build different
trusted OS images and DTBs.

----------------------------------------------------------------------
Toolchain
----------------------------------------------------------------------
Download the toolchain from Jetson release page according to your L4T version:

https://developer.nvidia.com/embedded/jetson-linux-archive

Set environment variable CROSS_COMPILE_AARCH64_PATH to point to the aarch64
toolchain. For example, if the aarch64 toolchain directory is
/toolchain/aarch64--glibc--stable-2022.03-1/, then set
the CROSS_COMPILE_AARCH64_PATH with the command below.

export CROSS_COMPILE_AARCH64_PATH=/toolchain/aarch64--glibc--stable-2022.03-1

Then set environment variable CROSS_COMPILE_AARCH64 with the command
below.

export CROSS_COMPILE_AARCH64=/toolchain/aarch64--glibc--stable-2022.03-1/bin/aarch64-buildroot-linux-gnu-

----------------------------------------------------------------------
UEFI StMM image
----------------------------------------------------------------------
A UEFI StMM image is required when building OP-TEE. The image is usually at:

For the Jetson AGX Xavier series and the Jetson Xavier NX:
<Linux_for_Tegra>/bootloader/standalonemm_optee_t194.bin

For the Jetson AGX Orin series:
<Linux_for_Tegra>/bootloader/standalonemm_optee_t234.bin

Set the environment variable "UEFI_STMM_PATH" to let the OP-TEE build script
know where the image is:

export UEFI_STMM_PATH=<StMM image path>

----------------------------------------------------------------------
Building the OP-TEE source code
----------------------------------------------------------------------
Execute this command to build the OP-TEE source package:

./optee_src_build.sh -p t<platform>

----------------------------------------------------------------------
Building the OP-TEE dtb
----------------------------------------------------------------------
Execute this command to build OP-TEE dtb:

dtc -I dts -O dtb -o ./optee/tegra<platform>-optee.dtb ./optee/tegra<platform>-optee.dts

----------------------------------------------------------------------
Building the ATF source code with OP-TEE SPD
----------------------------------------------------------------------
1. Extract the ATF source package.
   mkdir atf_build
   tar -I lbzip2 -C atf_build -xpf atf_src.tbz2

2. Build the ATF source code:
   cd atf_build/arm-trusted-firmware
   make BUILD_BASE=./build \
       CROSS_COMPILE="${CROSS_COMPILE_AARCH64}" \
       DEBUG=0 LOG_LEVEL=20 PLAT=tegra SPD=opteed TARGET_SOC=t<platform> V=0
   cd ../..

----------------------------------------------------------------------
Generating the tos.img with ATF and OP-TEE images
----------------------------------------------------------------------
1. Get gen_tos_part_img.py. It's usually in the directory
   <Linux_for_Tegra>/nv_tegra/tos-scripts/ of BSP package.

2. Generate the tos.img with the commands:
   ./gen_tos_part_img.py \
       --monitor ./atf_build/arm-trusted-firmware/build/tegra/t<platform>/release/bl31.bin \
       --os ./optee/build/t<platform>/core/tee-raw.bin \
       --dtb ./optee/tegra<platform>-optee.dtb \
       --tostype optee \
       ./tos.img

----------------------------------------------------------------------
Verifying the Image
----------------------------------------------------------------------
To verify the image:
1. Replace the default TOS image file with the newly generated TOS
   image. The default TOS image file is located at:
   <Linux_for_Tegra>/bootloader/tos-optee_t<platform>.img

2. Perform either of these tasks:
   - Flash the system as normal.
     This is useful for flashing a new system or replacing the
     entire operating system.
   - Re-flash the TOS image using these partition flash commands:
       sudo ./flash.sh -k <TOS partition name> <your_board_conf_file> mmcblk0p1
       ex:
       sudo ./flash.sh -k secure-os jetson-xavier-nx-devkit mmcblk0p1
       sudo ./flash.sh -k A_secure-os jetson-agx-orin-devkit mmcblk0p1

3. Copy all the files under ./optee/install/t<platform> to the target.
