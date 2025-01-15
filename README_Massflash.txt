************************************************************************
                               Linux for Tegra
                                  Massflash
                                   README
************************************************************************

The NVIDIA Tegra Linux Package provides ``massflash'' tools to flash
multiple Jetson devices simultaneously. This document describes detailed
procedure of ``massflashing''.

The massflashing tool generates ``massflash blob'' in trusted environment.
The massflash blob is portable, encrypted, and signed binary firmware and
tool files, which are used to flash one or more Jetson devices simultaneously
in insecure place such as factory floor without revealing any SBK or PKC key
files in human readable form.


========================================================================
Building the Massflash Blob in Trusted Environment
========================================================================
There are 2 methods to build the massflash blob: ONLINE and OFFLINE.
The ONLINE method requires the target Jetson device attached to the host
and the OFFLINE method requires knowledge of actual specification of
target device.

  Building the Massflash Blob with ONLINE method
  ----------------------------------------------
   Building the massflash blob with ONLINE method requires:
   - Set up a X86 Linux host as the ``signing host'' in safe location.
     See ``L4T_quick_start_guide.txt'' for detailed host setup.
   - If and only if secureboot is required, take extra steps:
     See ``Installing the L4T Secureboot Package'' in README_secureboot.txt
     to install secureboot package.
     See ``Generating the RSA Key Pair'' in README_secureboot.txt
     to generate the RSA Key-pair.
     See ``Preparing the DK(KEK)/SBK/ODM Fuses'' in README_secureboot.txt
     to prepare the DK(KEK)/SBK/ODM keys.

   To generate the massflash blob with ONLINE method:

   - Enter the command `cd Linux_for_Tegra`.
   - connect one target Jetson device, and put it into RCM mode.
   - sudo ./nvmassflashgen.sh [<secureboot options>] <device name> mmcblk0p1
     See ``Signing and Flashing Boot Files in two steps'' in
     README_secureboot.txt for details of <secureboot options>.

   Examples for ONLINE massflash blob generation method:
     To generate clear massflash blob:
       sudo ./nvmassflashgen.sh <device name> mmcblk0p1

     To generate PKC signed massflash blob:
       sudo ./nvmassflashgen.sh -x <TegraID> -y PKC -u <PKC keyfile> <device name> mmcblk0p1

     Where `<device_name>` is one of supported jetson devices:
     jetson-agx-xavier-industrial, jetson-xavier-nx-devkit-tx2-nx,
     jetson-xavier-nx-devkit-emmc, jetson-nano-devkit-emmc,
     jetson-agx-xavier-devkit, jetson-tx2-devkit, jetson-tx2-devkit-tx2i,
     jetson-tx2-devkit-4gb, and jetson-tx1-devkit.

     NOTE: nvmassflashgen.sh generates massflash images named as:
           mfi_<device_name>.tbz2 for clear massflash blob,
           mfi_<device_name>_signed.tbz2 for PKC signed massflash blob,
           mfi_<device_name>_encrypt_signed.tbz2 for SBK+PKC massflash blob.

     NOTE: SBKPKC is supported only in OFFLINE mode by jetson-tx2-devkit,
           jetson-agx-xavier-industrial, jetson-xavier-nx-devkit-tx2-nx,
           jetson-xavier-nx-devkit-emmc, jetson-agx-xavier-devkit,
           jetson-tx2-devkit, jetson-tx2-devkit-tx2i, and
           jetson-tx2-devkit-4gb.

     NOTE: For detailed information about <key.pem>, <SBK file>, and
           <KEK file>, see README_secureboot.txt

  Building the Massflash Blob with OFFLINE method
  -----------------------------------------------
   Building the massflash blob with OFFLINE method requires:
   Same as ONLINE method. See ``Building the Massflash Blob with ONLINE
   method'' above.

   To generate the massflash blob with OFFLINE method:

   - Enter the command `cd Linux_for_Tegra`.
   - No actual jetson device attachment is necessary.
   - Just add ``BOARDID=<boardid> BOARDSKU=<sku> FAB=<fab> [BOARDREV=<rev>]
     FUSELEVEL=fuselevel_production'' in front of
     ``./nvmassflashgen.sh'' as in ONLINE method:
     sudo BOARDID=<boardid> BOARDSKU=<sku> FAB=<fab> [BOARDREV=<rev>] \
     FUSELEVEL=fuselevel_production \
     ./nvmassflashgen.sh [<secureboot options>] <device name> mmcblk0p1
     See ``Signing and Flashing Boot Files in two steps'' in
     README_secureboot.txt for details of <secureboot options>.
   Where actual values are:
                                      BOARDID  BOARDSKU  FAB  BOARDREV
     --------------------------------+--------+---------+----+---------
     jetson-agx-xavier-industrial     2888     0008      600  A.0
     jetson-xavier-nx-devkit-tx2-nx   3636     0001      100  B.0
     jetson-xavier-nx-devkit-emmc     3668     0001      100  N/A
     jetson-nano-devkit-emmc          3448     0002      400  N/A
     jetson-agx-xavier-devkit (16GB)  2888     0001      400  H.0
     jetson-agx-xavier-devkit (32GB)  2888     0004      400  K.0
     jetson-tx2-devkit                3310     1000      B02  N/A
     jetson-tx2-devkit-tx2i           3489     0000      300  A.0
     jetson-tx2-devkit-4gb            3489     0888      300  F.0
     jetson-tx1-devkit                2180     0000      400  N/A
     --------------------------------+--------+---------+----+---------


   NOTE: Only jetson-agx-xavier-devkit requires ``BOARDREV'' argument.

   NOTE: jetson-agx-xavier-devkit 16GB(SKU1) and 32GB(SKU4) are using
         same configuration. Only differences are BOARDSKU and BOARDREV.

   NOTE: All input and output are exactly same as ONLINE method.

   Examples for OFFLINE massflash blob generation method:
   To generate jetson-tx1-devkit clear massflash blob:
     sudo BOARDID=2180 BOARDSKU=0000 FAB=400 FUSELEVEL=fuselevel_production \
     ./nvmassflashgen.sh jetson-tx1-devkit mmcblk0p1

   To generate jetson-tx2-devkit-4gb clear massflash blob:
     sudo BOARDID=3489 BOARDSKU=0888 FAB=300 FUSELEVEL=fuselevel_production \
     ./nvmassflashgen.sh jetson-tx2-devkit-4gb mmcblk0p1

   To generate jetson-agx-xavier-devkit (32GB) clear massflash blob:
     sudo BOARDID=2888 BOARDSKU=0004 FAB=400 BOARDREV=K.0 \
     FUSELEVEL=fuselevel_production \
     ./nvmassflashgen.sh jetson-agx-xavier-devkit mmcblk0p1

   To generate jetson-agx-xavier-devkit (16GB) clear massflash blob:
     sudo BOARDID=2888 BOARDSKU=0001 FAB=400 BOARDREV=H.0 \
     FUSELEVEL=fuselevel_production \
     ./nvmassflashgen.sh jetson-agx-xavier-devkit mmcblk0p1

   To generate jetson-tx2-devkit-tx2i clear massflash blob:
     sudo BOARDID=3489 BOARDSKU=0000 FAB=300 FUSELEVEL=fuselevel_production \
     ./nvmassflashgen.sh jetson-tx2-devkit-tx2i mmcblk0p1

   To generate jetson-tx2-devkit PKC signed massflash blob:
     sudo BOARDID=3310 BOARDSKU=1000 FAB=B02 FUSELEVEL=fuselevel_production \
     ./nvmassflashgen.sh -x 0x18 -y PKC -u <pkc_keyfile> \
     jetson-tx2-devkit mmcblk0p1

   To generate jetson-agx-xavier-devkit (16GB) SBK encrypted + PKC signed massflash blob:
     sudo BOARDID=2888 BOARDSKU=0001 FAB=400 BOARDREV=H.0 \
     FUSELEVEL=fuselevel_production ./nvmassflashgen.sh -x 0x19 -y SBKPKC \
     -u <pkc_keyfile> -v <sbk_keyfile> jetson-agx-xavier-devkit mmcblk0p1

   To generate jetson-nano-devkit-emmc PKC signed massflash blob:
     sudo BOARDID=3448 BOARDSKU=0002 FAB=400 FUSELEVEL=fuselevel_production \
     ./nvmassflashgen.sh -x 0x21 -y PKC -u <pkc_keyfile> \
     jetson-nano-devkit-emmc mmcblk0p1

   To generate jetson-xavier-nx-devkit-emmc SBK encrypted PKC signed massflash blob:
     sudo BOARDID=3668 BOARDSKU=0001 FAB=100 BOARDREV=H.0 \
     FUSELEVEL=fuselevel_production ./nvmassflashgen.sh -x 0x19 -y SBKPKC \
     -u <pkc_keyfile> -v <sbk_keyfile> jetson-xavier-nx-devkit-emmc mmcblk0p1

========================================================================
Flashing the Massflash Blob in Untrusted Environment
========================================================================
Flashing the massflash blob in untrusted environment requires:
- Set up one or more X86 Linux host as the ``flashing host''.
  The flashing hosts do not require any L4T BSP installation.
- Use the following procedure to flash one or more jetson devices
  simultaneously.
- Following procedure must be performed on each flashing hosts.

1. Download mfi_<jetson_device>[[_encrypt]_signed].tbz2

   Example:
   ubuntu@ahost:~$ scp loginname@<master host ipaddr>:Linux_for_Tegra/mfi_jetson_tx1_signed.tbz2
   loginname@<master host ipaddr?'s password:
   mfi_jetson_tx1_signed.tbz2              100% 1024KB   1.0MB/s   00:00

2. Untar mfi_<jetson_device>[[_encrypt]_signed].tbz2 image:

   Example:
   - tar xvjf mfi_jetson_tx1_signed.tbz2

3. Change directory to the massflash blob directory.

   Example:
   - cd mfi_jetson_tx1_signed

4. Flash multiple TX1s simultaneously:

   - Connect the Jetson devices to the flashing hosts.
     (Make sure all devices are in exactly the same hardware revision as
     prepared in ``Building Massflash Blob'' section above: Especially
     SKU, FAB, BOARDREV, etc... )
   - Put all of connected Jetsons into RCM mode.
   - Enter: `sudo ./nvmflash.sh [--showlogs]`

   NOTE: nvmflash.sh saves all massflashing logs in mfilogs
         directory in mfi_<jetson_device>[[_encrypt]_signed].
         Each log name has following format:
         ``<hostname>_<timestamp>_<pid>_flash_<USB_path>.log''

   NOTE: This procedure can be repeated and all the boards flashed
         with same massflash blob have exactly same L4T firmware version.

   NOTE: The final performance (i.e. how many simultaneous flashing
         each flashing host can take) is solely depending on USB
         configuration of each flashing host.
