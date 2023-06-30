************************************************************************
                               Linux for Tegra
                                  Autoflash
                                   README
************************************************************************

The NVIDIA Tegra Linux Package provides ``autoflash'' tools to abstract
the actual Jetson device names from users. This document describes
detailed procedure of ``autoflashing''.

The autoflashing tool, nvautoflash.sh, is a wrapper shell script around
the conventional flash.sh. The nvautoflash.sh takes exactly same set of
options as the flash.sh does except the <device name> and <boot device>
which are 2 last mandatory parameters for flash.sh. The nvautoflash.sh
figures out <device name> and <boot device>, and passes them to flash.sh
automatically.


========================================================================
Autoflash requirements
========================================================================
The nature of automatic detection of device name and boot device imposes
following requirements:

  1. ONLINE flashing mode:
     Since the autoflash need to fetch board information, the target
     device connected in RCM mode is mandatory.

  2. Single device restriction:
     The autoflash restricts the number of device connection to 1.
     Multiple device connections may confuse not only autoflash but
     also users.

  3. Single step operation:
     The autoflash restricts the number of flashing steps to 1. It
     does not generates any blobs to be used later.

  4. Default boot device restriction:
     As opposed to flash.sh allows auxiliary boot device such as Ethernet
     or USB stick, the autoflash restricts the boot device to the default
     boot media such as emmc or sdcard.

========================================================================
Supported devices
========================================================================
The autoflash supports all shipped Jetson products:

                                      BOARDID  BOARDSKU  TegraID
     --------------------------------+--------+---------+---------
     jetson-agx-xavier-industrial     2888     0008      0x19
     jetson-xavier-nx-devkit-tx2-nx   3636     0001      0x18
     clara-agx-xavier-devkit          3900     0000      0x19
     jetson-xavier-nx-devkit          3668     0000      0x19
     jetson-xavier-nx-devkit-emmc     3668     0001      0x19
     jetson-nano-devkit               3448     0000      0x21
     jetson-nano-devkit-emmc          3448     0002      0x21
     jetson-nano-2gb-devkit           3448     0003      0x21
     jetson-agx-xavier-devkit (16GB)  2888     0001      0x19
     jetson-agx-xavier-devkit (32GB)  2888     0004      0x19
     jetson-tx2-devkit                3310     1000      0x18
     jetson-tx2-devkit-tx2i           3489     0000      0x18
     jetson-tx2-devkit-4gb            3489     0888      0x18
     jetson-tx1-devkit                2180     0000      0x21
     --------------------------------+--------+---------+---------

========================================================================
Usage examples
========================================================================
  For standard default flashing:
    sudo ./nvautoflash.sh

  For PKC authentication flashing:
       sudo ./nvautoflash.sh -x <TegraID> -y PKC -u <PKC keyfile>

  NOTE: Since SBKPKC is supported only in OFFLINE mode, it is not supported
        by autoflash.

  NOTE: For detailed information about <PKC keyfile>,
        see README_secureboot.txt
