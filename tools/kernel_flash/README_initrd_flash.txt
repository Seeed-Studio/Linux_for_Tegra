************************************************************************
                              Linux for Jetson
                             Flash using initrd
                                   README
************************************************************************
The NVIDIA Jetson Linux Package provides tools to flash the Jetson devices from
the host using recovery kernel initrd running on the target. This document
describes in detail the procedure for "flashing using initrd".

Requirements:
- This tool uses SSH to contact with the Jetson target and establish an NFS server on the host in order
  to transfer the file from the host to the target.  Make sure your firewall and network settings allows
  SSH to the Jetson target and NFS request from the Jetson target.
  (Note: starting from Jetson Thor, this is no longer required)
- This tool use IPv6. Make sure your kernel settings, firewall settings, and network settings allow IPv6
  (Note: starting from Jetson Thor, this is no longer required)
- Run this script to install the right dependencies:
      $ sudo tools/l4t_flash_prerequisites.sh # For Debian-based Linux
- For T234, the default storage size is 57GiB for both internal storage device and external storage device
- For T264, the default storage size is 57GiB for internal storage device and 234GiB for external storage device.


How to use:
- This tool supports T264 and T234 devices. You can use the -h option to find out what options this tool supports.
- Below are listed some sample workflows for initrd flashing.

Workflow 1: How to flash single devices in one step
Steps:
- Make sure you have only ONE device in recovery mode plugged in the host
- Run this command from the Linux_for_Tegra folder:
      $ sudo ./tools/kernel_flash/l4t_initrd_flash.sh <board-name> <rootdev>
  Where <board-name> and <rootdev> are similar to the corresponding variables used
  in the flash.sh command. (See more details in the official documentation's
  board name table).



Workflow 2: How to generate images first and flash the target later.
Steps:

With device connected (online mode):
- Make sure you have only ONE device in recovery mode plugged into the host
- Run this command from the Linux_for_Tegra folder to generate flash images:
$ sudo ./l4t_initrd_flash.sh --no-flash <board-name> <rootdev>

Without device connected (offline mode):
- Run this command from the Linux_for_Tegra folder to generate flash images:
$ sudo BOARDID=<BOARDID> FAB=<FAB> BOARDSKU=<BOARDSKU> BOARDREV=<BOARDREV> \
./l4t_initrd_flash.sh --no-flash <board-name> <rootdev>

- Put the device in recovery mode again
- Run this command from the Linux_for_Tegra folder:
      $ sudo ./l4t_initrd_flash.sh --flash-only <board-name> <rootdev>
  Where <board-name> and <rootdev> are similar to the corresponding variables
  used in the flash.sh command. (See more details in the official
  documentation's board name table).

For the value of the environment variables, please refer to the table at the bottom of this file.




Workflow 3: How to flash to an external storage:
Requirements
To flash to an externally connected storage device, you need to create your own
partition config xml file for the external device. For information about how to
do this, see the 'External Storage Device Partition' section in the developer
guide.

There are three examples xml files in the tools/kernel_flash folder. These
examples assume that the attached external storage is 64 gibibytes and above:
- flash_l4t_external.xml contains both the rootfs, kernel and kernel-dtb on the
  external storage device.
- flash_l4t_nvme_rootfs_enc.xml is a sample partition configuration that is used for
  disk encryption feature on external storage.
- flash_l4t_nvme_rootfs_ab.xml is a sample partition configuration that is used for the
  rootfs ab feature on external storage.

To flash, run this command from the Linux_for_Tegra folder:
$ sudo ADDITIONAL_DTB_OVERLAY_OPT=<opt> ./l4t_initrd_flash.sh --external-device <external-device> \
      -c <external-partition-layout> \
      [ --external-only ] \
      <board-name> <rootdev>
Where:
- <board-name> and <rootdev> variables are similar to those that are used for
  flash.sh. (See more details in the official documentation's board name
  table).
- <root-dev> can be set to "mmcblk0p1" or "internal" for booting from internal
  device or "external", "sda1" or "nvme0n1p1" for booting from external device.
  If your external device's external partition layout has "APP" partition,
  specifying here "nvme0n1p1" will generate the rootfs boot commandline:
  root=/dev/nvme0n1p1. If <rootdev> is internal or external, the tool will
  generate rootfs commandline: root=PARTUUID=...
  Note: For the Jetson units with dual NVMe, <rootdev> must be internal or external,
  so that root=PARTUUID=... is used in the kernel commandline.
- <external-partition-layout> is the partition layout for the external storage
  device in XML format.
- <external-device> is the name of the external storage device you want to flash
  as it appears in the '/dev/' folder (i.e nvme0n1p1, nvme1n1p1, sda1).
- Use --external-only to flash only the external storage device.
  If you do not provide the "--external-only" option, the command will flash both internal and
  external storage devices.
- Use --network <netargs> if you want the flash process to happen through Ethernet protocol
  instead of USB protocol. Ethernet protocol is more reliable than USB protocol
  for external devices like USB.
  <netargs> can be "usb0" when flashing using ethernet protocol through the usb
  flashing cable or "eth0:<target-ip>/<subnet>:<host-ip>" when flashing using
  ethernet protocol through the RJ45 cable.
- (Optional) Declare ADDITIONAL_DTB_OVERLAY_OPT=<opt> where <opt> can be BootOrderNvme.dtbo.
  This allows UEFI to prioritize booting from NVMe SSD. <opt> can also be BootOrderUsb.dtbo, which
  allows UEFI to prioritize booting from the USB storage drive


Example usage:
Flash an NVMe SSD and use APP partition on it as root filesystem
sudo ADDITIONAL_DTB_OVERLAY_OPT="BootOrderNvme.dtbo" ./l4t_initrd_flash.sh --external-device nvme0n1p1 [ -c ./tools/kernel_flash/flash_l4t_t264_nvme.xml ]  --showlogs  jetson-agx-thor-devkit internal

Flash USB-connected storage use APP partition on it as root filesystem
sudo ADDITIONAL_DTB_OVERLAY_OPT="BootOrderUsb.dtbo" ./l4t_initrd_flash.sh --external-device sda1 [ -c ./tools/kernel_flash/flash_l4t_t264_nvme.xml ] --showlogs  jetson-agx-thor-devkit internal


Initrd flash depends on --external-device options and the last parameter <rootdev>
to generate the correct images.

When a Jetson has both internal and external storage devices like Jetson AGX Orin, the initrd flash tool will flash the full root filesystem both on the external storage device and the internal storage device.
If you dont want this to happen, please use the following command:

sudo ADDITIONAL_DTB_OVERLAY_OPT="BootOrderNvme.dtbo" ./l4t_initrd_flash.sh --external-device nvme0n1p1 -c ./tools/kernel_flash/flash_l4t_external.xml --showlogs  jetson-agx-orin-devkit external



Workflow 4: ROOTFS_AB support and boot from external device:
ROOTFS_AB is supported by setting the ROOTFS_AB environment variable to 1. For
example:
sudo ROOTFS_AB=1 ./l4t_initrd_flash.sh \
      --external-device nvme0n1 \
      [ -c ./tools/kernel_flash/flash_l4t_t264_nvme_rootfs_ab.xml ] \
      jetson-agx-thor-devkit \
      external





Workflow 5: Secureboot
With Secureboot package installed, you can flash PKC fused or SBKPKC fused
Jetson. For example:
$ sudo ./l4t_initrd_flash.sh \
      -u pkckey.pem \
      -v sbk.key \
      [-p "--user_key user.key" ] \
      --external-device nvme0n1 \
      [ -c ./tools/kernel_flash/flash_l4t_t264_nvme.xml ] \
      jetson-agx-thor-devkit \
      external





Workflow 6: Initrd Massflash
Initrd Massflash works with workflow 3,4,5. Initrd massflash also requires you to do the massflash
in two steps.

First, generate massflash package using options --no-flash and --massflash <x> and --network usb0
Where <x> is the highest possible number of devices to be flashed concurrently.

Both online mode and offline mode are supported (Details can be seen in workflow 2).
In the example below, we use offline mode to create a flashing environment that is
capable of flashing 5 devices concurrently.

For Orin:
$ sudo BOARDID=<BOARDID> FAB=<FAB> BOARDSKU=<BOARDSKU> BOARDREV=<BOARDREV>
./tools/kernel_flash/l4t_initrd_flash.sh --no-flash --massflash 5 --network usb0 jetson-agx-orin-devkit mmcblk0p1

For Thor:
$ sudo BOARDID=<BOARDID> FAB=<FAB> BOARDSKU=<BOARDSKU> BOARDREV=<BOARDREV>
./tools/kernel_flash/l4t_initrd_flash.sh --no-flash --massflash 5 --network usb0 jetson-agx-thor-devkit mmcblk0p1

(For the value of BOARDID, FAB, BOARDSKU and BOARDREV, please refer to the table at the bottom of this file.)


Second,
- Connect all 5 Jetson devices to the flashing hosts.
(Make sure all devices are in exactly the same hardware revision similar to the requirement in
README_Massflash.txt )
- Put all of connected Jetsons into RCM mode.
- Run:

For Orin
$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --flash-only --massflash 5 --network usb0 jetson-agx-orin-devkit
(Optionally add --showlogs to show all of the log)

For Thor
$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --flash-only --massflash 5 --network usb0 jetson-agx-thor-devkit
(Optionally add --showlogs to show all of the log)

Note:
the actual number of connected devices can be less than the maximum number
of devices the package can support.


Tips:
- The tool also provides the --keep option to keep the flash
  environment, and the --reuse options to reuse the flash environment to make
  massflash run faster:

  Massflash the first time.
  $ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --flash-only --massflash 5 --network usb0 --keep jetson-agx-orin-devkit

  Massflash the second time.
  $ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --flash-only --massflash 5 --network usb0 --reuse jetson-agx-orin-devkit

- Use ionice to make the flash process the highest I/O priority in the system.
  $ sudo ionice -c 1 -n 0 ./tools/kernel_flash/l4t_initrd_flash.sh --flash-only --network usb0 --massflash 5 jetson-agx-orin-devkit






Workflow 7: Secure initrd Massflash

Here are the steps to flash in unsecure factory floor.

First, generate a massflash package using the --no-flash and --massflash <x>
options, and specify the neccessary keys using the -u and -v options, where <x>
is the highest possible number of devices to be flashed concurrently. In the
example below, we create a flashing environment in online mode that is
capable of flashing 5 devices concurrently.

$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh -u <pkckey> [-v <sbkkey>] --no-flash --massflash 5 jetson-agx-thor-devkit internal
$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh -u <pkckey> [-v <sbkkey>] --no-flash --massflash 5 jetson-agx-orin-devkit internal


The tool generates a tarball called mfi_<target-board>.tar.gz that contains all
the minimal binaries needed to flash in an unsecure environment. Download this
tarball to the unsafe environment, and untar the tarball to create a flashing
environment. For examples,
$ scp mfi_<target-board>.tar.gz <factory-host-ip>:<factory-host-dir>
...
Untar on a factory host machine:
$ sudo tar xpfv mfi_<target-board>.tar.gz

Second, perform this procedure:
- Connect the Jetson devices to the flashing hosts.
  (Make sure all devices are in exactly the same hardware revision similar to
  the requirement in README_Massflash.txt )
- Put all of connected Jetsons into RCM mode.
- Run:
$ cd mfi_<target-board>
For Orin
$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --flash-only --massflash 5 --network usb0 jetson-agx-orin-devkit
(Optionally add --showlogs to show all of the log)

For Thor
$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --flash-only --massflash 5 --network usb0 jetson-agx-thor-devkit
(Optionally add --showlogs to show all of the log)






Workflow 8: Flash inidividual partition

Initrd flash has an option to flash individual partitions based on the index file.
When running initrd flash, index files are generated under tools/kernel_flash/images
based on the partition configuration layout xml (images/internal/flash.idx for internal storage,
images/external/flash.idx for external storage). Using "-k" option, initrd flash can flash one
partition based on the partition label specified in the index file.

Examples:
For flashing eks partition on internal device:
$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh -k eks jetson-agx-thor-devkit internal


For flashing kernel-dtb partition on external device:
$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh \
  --external-device nvme0n1p1 \
  -c ./tools/kernel_flash/flash_l4t_external.xml \
  -k kernel-dtb --external-only jetson-agx-thor-devkit internal


Workflow 9: Disk encryption support on external device

For disk encryption for external device on Jetson AGX Orin, you can flash the external
device with the below command:

- Run this command from the Linux_for_Tegra folder:
$ sudo ROOTFS_ENC=1 ./tools/kernel_flash/l4t_initrd_flash.sh --external-device <external-device> \
      -c <external-partition-layout> \
      [-p "-i encryption.key" ] --external-only \
      jetson-agx-thor-devkit external

Where:
- all the parameters are the same as above.
- <external-partition-layout> is the external storage partition layout containing
APP, APP_ENC and UDA encrypted partition. In this folder, flash_l4t_nvme_rootfs_enc.xml
is provided as an example.





Workflow 10: Generate images for internal device and external device seperately
then flash

The flashing tool supports a three-step process: "to generate images for an
internal device, then generate them for an external device, then flash.
This is enabled by using the "append" option. Four examples below show how it
works.

Example 1: Generate a normal root filesystem configuration for the internal device
, then generate an encrypted root filesystem for the external device, then flash

1. Put the device into recovery mode, then generate a normal root
filesystem for the internal device:
$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --reuse --no-flash jetson-agx-orin-devkit internal
(Or if you want to generate the image offline, then you can use:
$ sudo BOARDID=3701 BOARDSKU=0004 FAB=000 ./tools/kernel_flash/l4t_initrd_flash.sh --no-flash --reuse jetson-agx-orin-devkit internal
)

2. Put the device into recovery mode, then generate an encrypted
filesystem for the external device:
$ sudo ROOTFS_ENC=1 ./tools/kernel_flash/l4t_initrd_flash.sh --no-flash \
            --external-device nvme0n1p1 \
            -c ./tools/kernel_flash/flash_l4t_nvme_rootfs_enc.xml \
            --external-only --append jetson-agx-orin-devkit external
(Or if you want to generate the image offline, then you can use:
$ sudo BOARDID=3701 BOARDSKU=0004 FAB=000 ROOTFS_ENC=1 CHIP_SKU="00:00:00:D0" ./tools/kernel_flash/l4t_initrd_flash.sh --no-flash \
            --external-device nvme0n1p1 \
            -c ./tools/kernel_flash/flash_l4t_nvme_rootfs_enc.xml \
            --external-only --append jetson-agx-orin-devkit external
)


3. Put the device into recovery mode, then flash both images:
$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --flash-only jetson-agx-orin-devkit


Example 2: In this example, you want to boot Jetson Orin Nano from an
attached NVMe SSD.

1. Put the device into recovery mode, then generate qspi only images
for the internal device:
$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --no-flash --reuse jetson-orin-nano-devkit internal


2. Put the device into recovery mode, then generate a normal
filesystem for the external device:
$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --no-flash \
            --external-device nvme0n1p1 \
            -c ./tools/kernel_flash/flash_l4t_external.xml \
            --external-only --append jetson-orin-nano-devkit external

3. Put the device into recovery mode, then flash both images:
$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --flash-only


Example 3: we create a massflash package with encrypted internal image and
normal external image with the --append option

1. Put the device into recovery mode, then generate encrypted rootfs
images for the internal device:
$ sudo ROOTFS_ENC=1 ./tools/kernel_flash/l4t_initrd_flash.sh --no-flash --reuse jetson-orin-nano-devkit internal

2. Put the device into recovery mode, then generate a normal
filesystem for the external device, and create a massflash package capable of
flashing two devices simultaneously:

$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --no-flash \
            --external-device nvme0n1p1 \
            -c ./tools/kernel_flash/flash_l4t_external.xml \
            --external-only --massflash 2 --append jetson-orin-nano-devkit external

3. Put two devices into recovery mode, then flash two devices:
$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --flash-only --massflash 2 jetson-orin-nano-devkit


Example 4: Generate an encrypted root filesystem configuration for the internal device
, then generate an encrypted root filesystem for the external device, then flash

1. Put the device into recovery mode, then generate an encrypted root
filesystem for the internal device:
$ sudo ROOTFS_ENC=1 ./tools/kernel_flash/l4t_initrd_flash.sh --no-flash --reuse jetson-agx-orin-devkit internal

Second step: Put the device into recovery mode, then generate an encrypted
filesystem for the external device:
$ sudo ROOTFS_ENC=1 ./tools/kernel_flash/l4t_initrd_flash.sh --no-flash \
            --external-device nvme0n1p1 \
            -c ./tools/kernel_flash/flash_l4t_nvme_rootfs_enc.xml \
            --external-only --append jetson-agx-orin-devkit external

Third step: Put the device into recovery mode, then flash both images:
$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --flash-only

Workflow 11: Manually generate a bootable external storage device:

You can manually generate a bootable external storage such as NVMe SSD, SD card or USB using this tool.
When a Jetson in recovery mode is connected, use the following command:

$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --direct <extdev_on_host> \
      -c <external-partition-layout> \
      --external-device <extdev_on_target> \
      [ -p <options> ] \
      <boardname> external

where
     <extdev_on_host> is the external device /dev node name as it appears on the host. For examples,
     if you plug in a USB on your PC, and it appears as /dev/sdb, then <exdev_on_host> will be sdb

     <extdev_on_target> is "nvme0n1p1" for NVMe SSD, "sda1" for USB or mmcblk1p1 for SD card

     <external-partition-layout> is the partition layout for the external storage device in XML format.
     You can use ./tools/kernel_flash/flash_l4t_external.xml as an example.

     <options> (optional) is any other option you use when generating the external storage device.
     For examples, specify -p "-C kmemleak" if you want to add kernel option "kmemleak"

If no Jetson in recovery mode is connected, please specify these env variables when running the flash command:
sudo BOARDID=<BOARDID> FAB=<FAB> BOARDSKU=<BOARDSKU> BOARDREV=<BOARDREV>
 ./tools/kernel_flash/l4t_initrd_flash.sh ...

For the value of these, please refer to the table at the bottom of this file.



Appendix:

Environment variables value table:

#                                       BOARDID  BOARDSKU  FAB  BOARDREV
#    ----------------------------------+--------+---------+----+---------+
#    jetson-agx-orin-devkit               3701     0001      TS1  C.2
#    jetson-agx-orin-devkit               3701     0000      TS4  A.0
#    holoscan-devkit                      3701     0002      TS1  A.0
#    jetson-agx-orin-devkit               3701     0004      TS4  A.0
#    jetson-agx-orin-devkit (64GB)        3701     0005
#    jetson-agx-orin-devkit-industrial    3701     0008
#    jetson-orin-nano-devkit (NX 16GB)    3767     0000
#    jetson-orin-nano-devkit (NX 8GB)     3767     0001
#    jetson-orin-nano-devkit (NX 16GB)    3767     0002
#    jetson-orin-nano-devkit (Nano 8GB)   3767     0003
#    jetson-orin-nano-devkit (Nano 4GB)   3767     0004
#    jetson-orin-nano-devkit (Nano 8GB)   3767     0005
#    jetson-agx-thor-devkit (Thor 128GB)  3834     0008
#    ----------------------------------+--------+---------+----+---------+
#


# Optional Environment Variables:
# EXTOPTIONS ------------------- flash options when generating flash image for external devices
# FLASHING_KERNEL -------------- define the path of the initrd image used for running the flashing process
# ADDITIONAL_DTB_OVERLAY_OPT --- define the path of the additional dtb overlay file


# Board Configuration Variables:
# EXTERNAL_PT_LAYOUT ----------- Default partition layout for external storage device
# FLASHING_CONFIG_FILE --------- Board configuration to generate flashing image
# EXTERNAL_DEVICE -------------- Default external storage device
# _FLASHING_KERNEL ------------- The boot initrd used for flashing. For example, you can use
# ------------------------------ _FLASHING_KERNEL="${LDK_DIR}/unified_flash/tools/flashtools/flashing_kernel/initramfs/t264/boot_flashing.img"
# _FLASHING_KERNEL_CMDLINE ----- the kernel commandline used for flashing kernel
# MAX_MASSFLASH ---------------- Default number of devices can be flashed at the same time when using massflash tool
# INTERNAL_PT_LAYOUT ----------- Default partition layout for internal storage device
