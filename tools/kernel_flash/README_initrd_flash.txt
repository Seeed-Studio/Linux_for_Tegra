************************************************************************
                              Linux for Jetson
                             Flash using initrd
                                   README
************************************************************************
The NVIDIA Jetson Linux Package provides tools to flash the Jetson devices from
the host using recovery kernel initrd running on the target. This document
describes in detail the procedure for "flashing using initrd".

Requirements:
- This tool makes use of USB mass storage during flashing; therefore,
  automount of new external storage device needs to be disabled temporarily
  during flashing. On most distributions of Debian-based Linux, you can do this
  using the following command:
      $ systemctl stop udisks2.service
- Run this script to install the right dependencies:
      $ sudo tools/l4t_flash_prerequisites.sh # For Debian-based Linux

How to use:
- This tool does not support size discovery for internal emmc/sdcard. Therefore,
  you might need to change the "num_sectors" field in the config file under
  bootloader/t186ref/cfg if the default "num_sectors" is incompatible. You must
  change "num_sectors" so that num_sectors * sector_size is equal to or smaller
  the size of the internal emmc/sd card of your Jetson.
- This tool supports T194 and T234 devices. You can use the -h option to find out what options this tool supports.
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
$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --no-flash <board-name> <rootdev>

Without device connected (offline mode):
- Run this command from the Linux_for_Tegra folder to generate flash images:
$ sudo BOARDID=<BOARDID> FAB=<FAB> BOARDSKU=<BOARDSKU> BOARDREV=<BOARDREV> \
./tools/kernel_flash/l4t_initrd_flash.sh --no-flash <board-name> <rootdev>

- Put the device in recovery mode again
- Run this command from the Linux_for_Tegra folder:
      $ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --flash-only <board-name> <rootdev>
  Where <board-name> and <rootdev> are similar to the corresponding variables
  used in the flash.sh command. (See more details in the official
  documentation's board name table).

For the value of the environment variables, please refer to the table at the bottom of this file.




Workflow 3: How to flash to an external storage:
Requirements
To flash to an externally connected storage device, you need to create your own
partition config xml file for the external device. For information about how to
do this, see the 'External Storage Device Partition' section in the developer
guide. Especially note that you will need to change the "num_sectors" field of
the partition config xml file to match your external storage device, as Initrd
flash does not support size discovery. You must change "num_sectors" so that
num_sectors * sector_size is equal to or smaller the size of your external
storage device. And for all types of external device, the device "type" needs to
be "nvme".

There are three examples xml files in the tools/kernel_flash folder. These
examples assume that the attached external storage is 64 gibibytes and above:

- flash_l4t_external.xml contains both the rootfs, kernel and kernel-dtb on the
  external storage device.
- flash_l4t_nvme_rootfs_enc.xml is a sample partition configuration that is used for
  disk encryption feature on external storage.
- flash_l4t_nvme_rootfs_ab.xml is a sample partition configuration that is used for the
  rootfs ab feature on external storage.

To flash, run this command from the Linux_for_Tegra folder:
$ sudo ADDITIONAL_DTB_OVERLAY_OPT=<opt> ./tools/kernel_flash/l4t_initrd_flash.sh --external-device <external-device> \
      -c <external-partition-layout> \
      [ --external-only ] \
      [ -S <APP-size> ] \
      [ --network <netargs> ] <board-name> <rootdev>
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
- <external-partition-layout> is the partition layout for the external storage
  device in XML format.
- <external-device> is the name of the external storage device you want to flash
  as it appears in the '/dev/' folder (i.e nvme0n1, sda).
- <APP-size> is the size of the partition that contains the operating system in bytes.
  KiB, MiB, GiB shorthand are allowed, for example, 1GiB means 1024 * 1024 *
  1024 bytes. This size cannot be bigger than "num_sectors" * "sector_size"
  specified in the <external-partition-layout> and must be small enough to fit
  other partitions in the partition layout.
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
Flash an NVMe SSD and use APP partition on it as root filesystem:
sudo ADDITIONAL_DTB_OVERLAY_OPT="BootOrderNvme.dtbo" ./tools/kernel_flash/l4t_initrd_flash.sh --external-device nvme0n1p1 -c ./tools/kernel_flash/flash_l4t_external.xml  --showlogs  jetson-xavier nvme0n1p1

Flash USB-connected storage use APP partition on it as root filesystem:
sudo ADDITIONAL_DTB_OVERLAY_OPT="BootOrderUsb.dtbo" ./tools/kernel_flash/l4t_initrd_flash.sh --external-device sda1 -c ./tools/kernel_flash/flash_l4t_external.xml  --showlogs  jetson-xavier mmcblk0p1

Flash an NVMe SSD and use the partition UUID (that is specified in l4t-rootfs-uuid.txt_ext) as the root filesystem:
sudo ADDITIONAL_DTB_OVERLAY_OPT="BootOrderNvme.dtbo" ./tools/kernel_flash/l4t_initrd_flash.sh --external-device nvme0n1p1 -c ./tools/kernel_flash/flash_l4t_external.xml  --showlogs  jetson-xavier external



Initrd flash depends on --external-device options and the last parameter <rootdev>
to generate the correct images. The following combinations are supported:
+-------------------+-----------------+-------------------------------------------------------+
| --external-device |       <rootdev> | Results                                               |
+-------------------+-----------------+-------------------------------------------------------+
| nvme*n*p* / sda*  |        internal | External device contains full root filesystem with    |
|                   |                 | kernel commandline: rootfs=PARTUUID=<external-uuid>   |
|                   |                 |                                                       |
|                   |                 | Internal device contains full root filesystem with    |
|                   |                 | kernel commandline: rootfs=PARTUUID=<internal-uuid>   |
+-------------------+-----------------+-------------------------------------------------------+
| nvme*n*p* / sda*  | nvme0n*p* / sd* | External device  contains full root filesystem with   |
|                   |                 | with kernel commandline rootfs=/dev/nvme0n1p1         |
|                   |                 |                                                       |
|                   |                 | Internal device contains minimal filesystem with     |
|                   |                 | kernel command line rootfs=/dev/nvme0n1p1             |
+-------------------+-----------------+-------------------------------------------------------+
| nvme*n*p* / sda*  |       mmcblk0p1 | External device  contains full root filesystem with   |
|                   |                 | with kernel commandline rootfs=/dev/nvme0n1p1         |
|                   |                 |                                                       |
|                   |                 | Internal device contains full filesystem with     |
|                   |                 | kernel command line rootfs=/dev/mmcblk0p1             |
+-------------------+-----------------+-------------------------------------------------------+
| nvme*n*p* / sda*  |        external | External device contains full root filesystem with    |
|                   |                 | kernel commandline: rootfs=PARTUUID=<external-uuid>   |
|                   |                 |                                                       |
|                   |                 | Internal device contains minimal root filesystem with |
|                   |                 | kernel commandline: rootfs=PARTUUID=<external-uuid>   |
+-------------------+-----------------+-------------------------------------------------------+





Workflow 4: How to flash to device with internal qspi and an external storage device:
Some Jetson devices like Jetson Orin NX and Jetson Xavier NX have an internal QSPI and an external
storage device, which flash.sh may not have support flashing yet. In this case you can use the
following commands:

For a device with internal QSPI and external NVMe:
sudo ./tools/kernel_flash/l4t_initrd_flash.sh --external-device nvme0n1p1 \
      -c tools/kernel_flash/flash_l4t_external.xml \
      -p "-c bootloader/t186ref/cfg/flash_t234_qspi.xml --no-systemimg" --network usb0 \
      <board> external


For a device with internal QSPI and external USB storage:
sudo ./tools/kernel_flash/l4t_initrd_flash.sh --external-device sda1 \
      -c tools/kernel_flash/flash_l4t_external.xml \
      -p "-c bootloader/t186ref/cfg/flash_t234_qspi.xml --no-systemimg" --network usb0 \
      <board> external




Workflow 5: ROOTFS_AB support and boot from external device:
ROOTFS_AB is supported by setting the ROOTFS_AB environment variable to 1. For
example:
sudo ROOTFS_AB=1 ./tools/kernel_flash/l4t_initrd_flash.sh \
      --external-device nvme0n1 \
      -S 8GiB \
      -c ./tools/kernel_flash/flash_l4t_nvme_rootfs_ab.xml \
      jetson-xavier \
      external





Workflow 6: Secureboot
With Secureboot package installed, you can flash PKC fused or SBKPKC fused
Jetson. For example:
$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh \
      -u pkckey.pem \
      -v sbk.key \
      [-p "--user_key user.key" ] \
      --external-device nvme0n1 \
      -S 8GiB \
      -c ./tools/kernel_flash/flash_l4t_external.xml \
      jetson-xavier \
      external





Workflow 7: Initrd Massflash
Initrd Massflash works with workflow 3,4,5. Initrd massflash also requires you to do the massflash
in two steps.

First, generate massflash package using options --no-flash and --massflash <x> and --network usb0
Where <x> is the highest possible number of devices to be flashed concurrently.

Both online mode and offline mode are supported (Details can be seen in workflow 2).
In the example below, we use online mode to create a flashing environment that is
capable of flashing 5 devices concurrently.

$ sudo BOARDID=<BOARDID> FAB=<FAB> BOARDSKU=<BOARDSKU> BOARDREV=<BOARDREV>
./tools/kernel_flash/l4t_initrd_flash.sh --no-flash --massflash 5 --network usb0 jetson-xavier-nx-devkit-emmc mmcblk0p1

(For the value of BOARDID, FAB, BOARDSKU and BOARDREV, please refer to the table at the bottom of this file.)


Second,
- Connect all 5 Jetson devices to the flashing hosts.
(Make sure all devices are in exactly the same hardware revision similar to the requirement in
README_Massflash.txt )
- Put all of connected Jetsons into RCM mode.
- Run:
$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --flash-only --massflash 5 --network usb0
(Optionally add --showlogs to show all of the log)

Note:
the actual number of connected devices can be less than the maximum number
of devices the package can support.


Tips:
- The tool also provides the --keep option to keep the flash
  environment, and the --reuse options to reuse the flash environment to make
  massflash run faster:

  Massflash the first time.
  $ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --flash-only --massflash 5 --network usb0 --keep

  Massflash the second time.
  $ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --flash-only --massflash 5 --network usb0 --reuse

- Use ionice to make the flash process the highest I/O priority in the system.
  $ sudo ionice -c 1 -n 0 ./tools/kernel_flash/l4t_initrd_flash.sh --flash-only --network usb0 --massflash 5






Workflow 8: Secure initrd Massflash

Here are the steps to flash in unsecure factory floor.

First, generate a massflash package using the --no-flash and --massflash <x>
options, and specify the neccessary keys using the -u and -v options, where <x>
is the highest possible number of devices to be flashed concurrently. In the
example below, we create a flashing environment in online mode that is
capable of flashing 5 devices concurrently.

$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh -u <pkckey> [-v <sbkkey>] --no-flash --massflash 5 jetson-xavier-nx-devkit-emmc mmcblk0p1

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
$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --flash-only --massflash 5
  (Optionally add --showlogs to show all of the log)






Workflow 9: Flash inidividual partition

Initrd flash has an option to flash individual partitions based on the index file.
When running initrd flash, index files are generated under tools/kernel_flash/images
based on the partition configuration layout xml (images/internal/flash.idx for internal storage,
images/external/flash.idx for external storage). Using "-k" option, initrd flash can flash one
partition based on the partition label specified in the index file.

Examples:
For flashing eks partition on internal device:
$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh -k eks jetson-xavier mmcblk0p1


For flashing kernel-dtb partition on external device:
$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh \
  --external-device nvme0n1p1 \
  -c ./tools/kernel_flash/flash_l4t_external.xml \
  -k kernel-dtb --external-only jetson-xavier mmcblk0p1


Workflow 10: Disk encryption support on external device

For disk encryption for external device on Jetson Xavier, you can flash the external
device with the below command:

- Run this command from the Linux_for_Tegra folder:
$ sudo ROOTFS_ENC=1 ./tools/kernel_flash/l4t_initrd_flash.sh --external-device <external-device> \
      -c <external-partition-layout> \
      [-p "-i encryption.key" ] --external-only \
      -S <APP-size> jetson-xavier external

Where:
- all the parameters are the same as above.
- <external-partition-layout> is the external storage partition layout containing
APP, APP_ENC and UDA encrypted partition. In this folder, flash_l4t_nvme_rootfs_enc.xml
is provided as an example.





Workflow 11: Generate images for internal device and external device seperately
then flash

The flashing tool supports a three-step process: "to generate images for an
internal device, then generate them for an external device, then flash.
This is enabled by using the "append" option. Four examples below show how it
works.

Example 1: Generate a normal root filesystem configuration for the internal device
, then generate an encrypted root filesystem for the external device, then flash

1. Put the device into recovery mode, then generate a normal root
filesystem for the internal device:
$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --no-flash jetson-xavier internal
(Or if you want to generate the image offline, then you can use:
$ sudo BOARDID=2888 BOARDSKU=004 FAB=400 ./tools/kernel_flash/l4t_initrd_flash.sh --no-flash jetson-xavier internal
)

2. Put the device into recovery mode, then generate an encrypted
filesystem for the external device:
$ sudo ROOTFS_ENC=1 ./tools/kernel_flash/l4t_initrd_flash.sh --no-flash \
            --external-device nvme0n1p1 \
            -S 8GiB -c ./tools/kernel_flash/flash_l4t_nvme_rootfs_enc.xml \
            --external-only --append jetson-xavier external
(Or if you want to generate the image offline, then you can use:
$ sudo BOARDID=2888 BOARDSKU=0004 FAB=400 ./tools/kernel_flash/l4t_initrd_flash.sh --no-flash \
            --external-device nvme0n1p1 \
            -S 8GiB -c ./tools/kernel_flash/flash_l4t_nvme_rootfs_enc.xml \
            --external-only --append jetson-xavier external
)


3. Put the device into recovery mode, then flash both images:
$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --flash-only


Example 2: In this example, you want to boot Jetson Xavier NX SD from an
attached NVMe SSD. The SD card does not need to be plugged in. You can also
apply this if you don't want to use the emmc on the Jetson Xavier NX emmc.

1. Put the device into recovery mode, then generate qspi only images
for the internal device:
$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --no-flash jetson-xavier-nx-devkit-qspi internal

Note: The board name given here is not jetson-xavier-nx-devkit or
jetson-xavier-nx-devkit-emmc so that no SD card or eMMC images are generated.


2. Put the device into recovery mode, then generate a normal
filesystem for the external device:
$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --no-flash \
            --external-device nvme0n1p1 \
            -c ./tools/kernel_flash/flash_l4t_external.xml \
            --external-only --append jetson-xavier-nx-devkit external

3. Put the device into recovery mode, then flash both images:
$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --flash-only


Example 3: we create a massflash package with encrypted internal image and
normal external image with the --append option

1. Put the device into recovery mode, then generate encrypted rootfs
images for the internal device:
$ sudo ROOTFS_ENC=1 ./tools/kernel_flash/l4t_initrd_flash.sh --no-flash jetson-xavier internal

2. Put the device into recovery mode, then generate a normal
filesystem for the external device, and create a massflash package capable of
flashing two devices simultaneously:

$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --no-flash \
            --external-device nvme0n1p1 \
            -S 8GiB -c ./tools/kernel_flash/flash_l4t_external.xml \
            --external-only --massflash 2 --append jetson-xavier external

3. Put two devices into recovery mode, then flash two devices:
$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --flash-only --massflash 2


Example 4: Generate an encrypted root filesystem configuration for the internal device
, then generate an encrypted root filesystem for the external device, then flash

1. Put the device into recovery mode, then generate an encrypted root
filesystem for the internal device:
$ sudo ROOTFS_ENC=1 ./tools/kernel_flash/l4t_initrd_flash.sh --no-flash jetson-xavier internal

Second step: Put the device into recovery mode, then generate an encrypted
filesystem for the external device:
$ sudo ROOTFS_ENC=1 ./tools/kernel_flash/l4t_initrd_flash.sh --no-flash \
            --external-device nvme0n1p1 \
            -S 8GiB -c ./tools/kernel_flash/flash_l4t_nvme_rootfs_enc.xml \
            --external-only --append jetson-xavier external

Third step: Put the device into recovery mode, then flash both images:
$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --flash-only

Workflow 12: Manually generate a bootable external storage device:

You can manually generate a bootable external storage such as NVMe SSD, SD card or USB using this tool.
When a Jetson in recovery mode is connected, use the following command:

$ sudo ./tools/kernel_flash/l4t_initrd_flash.sh --direct <extdev_on_host> \
      -c <external-partition-layout> \
      --external-device <extdev_on_target> \
      [ -p <options> ] \
      [ -S <rootfssize> ] \
      <boardname> external

where
     <extdev_on_host> is the external device /dev node name as it appears on the host. For examples,
     if you plug in a USB on your PC, and it appears as /dev/sdb, then <exdev_on_host> will be sdb

     <extdev_on_target> is "nvme0n1p1" for NVMe SSD, "sda1" for USB or mmcblk1p1 for SD card

     <external-partition-layout> is the partition layout for the external storage device in XML format.
     You can use ./tools/kernel_flash/flash_l4t_external.xml as an example.

     <rootfssize> (optional) is the size of APP partition on the external storage device. Note that this is different
     from the total size of the external storage device, which is defined by num_sectors field in
     <external-partition-layout>

     <options> (optional) is any other option you use when generating the external storage device.
     For examples, specify -p "-C kmemleak" if you want to add kernel option "kmemleak"

If no Jetson in recovery mode is connected, please specify these env variables when running the flash command:
sudo BOARDID=<BOARDID> FAB=<FAB> BOARDSKU=<BOARDSKU> BOARDREV=<BOARDREV>
 ./tools/kernel_flash/l4t_initrd_flash.sh ...

For the value of these, please refer to the table at the bottom of this file.



Appendix:

Environment variables value table:

#
#                                     BOARDID  BOARDSKU  FAB  BOARDREV
#    --------------------------------+--------+---------+----+---------
#    jetson-agx-xavier-industrial     2888     0008      600  A.0
#    clara-agx-xavier-devkit"         3900     0000      001  C.0
#    jetson-xavier-nx-devkit          3668     0000      100  N/A
#    jetson-xavier-nx-devkit-emmc     3668     0001      100  N/A
#    jetson-xavier-nx-devkit-emmc     3668     0003      N/A  N/A
#    jetson-agx-xavier-devkit (16GB)  2888     0001      400  H.0
#    jetson-agx-xavier-devkit (32GB)  2888     0004      400  K.0
#    jetson-agx-orin-devkit           3701     0001      TS1  C.2
#    jetson-agx-orin-devkit           3701     0000      TS4  A.0
#    jetson-agx-xavier-devkit (64GB)  2888     0005      402  B.0
#    holoscan-devkit                  3701     0002      TS1  A.0
#    jetson-agx-orin-devkit           3701     0004      TS4  A.0
#    --------------------------------+--------+---------+----+---------

Other environment variables:
EXTOPTIONS: flash option when generating flash image for external devices
ADDITIONAL_DTB_OVERLAY_OPT: Add additional overlay dtbs for UEFI when generating flash image

