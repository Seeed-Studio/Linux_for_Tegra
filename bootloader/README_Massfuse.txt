************************************************************************
                               Linux for Tegra
                                  Massfuse
                                   README
************************************************************************

The NVIDIA Tegra Secureboot Package provides ``massfuse'' tools to fuse
multiple Jetson devices simultaneously. This document describes detailed
procedure of ``massfusing''. Refer to ``Secureboot'' section of the BSP
documentation for detailed definition of fuse and security.

The massfusing tool generates ``massfuse blob'', which is a collection of
portable binary fuse configuration and tool files. The massfuse blob is
generated in relatively safer place such as HQ and used to fuse one or
more Jetson devices simultaneously in a place such as factory floor
without revealing any SBK or PKC key files in human readable form.

NOTE: Even though there is no human readable SBK or PKC key file in the
massfuse blob, the usage of massfuse blob does not guarantee 100% security.


========================================================================
Building the Massfuse Blob in Trusted Environment
========================================================================
There are 2 methods to build the massfuse blob: ONLINE and OFFLINE.
The ONLINE method requires the target Jetson device attached to the host
and the OFFLINE method requires knowledge of actual specification of
target device.

  Building the Massfuse Blob with ONLINE method
  ---------------------------------------------
   Building the massfuse blob with ONLINE method requires:
   - Set up a X86 Linux host as the ``key host'' in safe location.
   - Generate the PKC and SBK Key
   - If necessary, prepare the KEK/SBK/ODM fuses

   See ``Secureboot'' section of the BSP documentation for details.

   To generate the massfuse blob with ONLINE method:

   - Enter the command `cd Linux_for_Tegra`.
   - connect one target Jetson device, and put it into RCM mode.
   - ./nvmassfusegen.sh <odm fuse options> <device_name>

   Examples for ONLINE massfuse blob generation method:
   For t23x,
     To fuse clear devices with .xml template file:
       sudo ./nvmassfusegen.sh -i <chip_id> -X <template file> <device_name>

     Where `<device_name>` is one of supported jetson devices:
     jetson-agx-orin

     NOTE: L4T supports only .xml template file based fusing for
           t23x boards.

     NOTE: For detailed information about .xml template file,
           see ``Secureboot'' section of BSP documentation.

   For t19x,
     To fuse clear devices with PKC HASH from .pem file:
       sudo ./nvmassfusegen.sh -i <chip_id> -p -k <key.pem> <device_name>

     To fuse clear devices with SBK key and PKC HASH:
       sudo ./nvmassfusegen.sh -i <chip_id> -p -k <key.pem> \
       -S <SBK file> <device_name>

     Where `<device_name>` is one of supported jetson devices:
     jetson-agx-xavier-industrial, jetson-xavier-nx-devkit-emmc, and
     jetson-agx-xavier-devkit

     NOTE: The portable massfuse blob is named as:
           mfuse_<device_name>.tbz2 for non-secureboot,
           mfuse_<device_name>_signed.tbz2 for PKC secureboot,
           mfuse_<device_name>_encrypt_signed.tbz2 for SBKPKC secureboot.

     NOTE: For detailed information about <key.pem>, <SBK file>, and
           <KEK file>, see ``Secureboot'' section of BSP documentation.

  Building the Massfuse Blob with OFFLINE method
  ----------------------------------------------
   Building the massfuse blob with OFFLINE method requires:
   Same as ONLINE method. See ``Building the Massfuse Blob with ONLINE
   method'' above.

   To generate the massfuse blob with OFFLINE method:

   - Enter the command `cd Linux_for_Tegra`.
   - No actual jetson device attachment is necessary.
   - Just add ``BOARDID=<bdid> BOARDSKU=<bdsku> FAB=<fab> BOARDREV=<bdrev>
     FUSELEVEL=fuselevel_production CHIPREV=<chiprev> [CHIP_SKU=<chipsku>]''
     in front of ``./nvmassfusegen.sh'' as in ONLINE method:
     BOARDID=<boardid> BOARDSKU=<sku> FAB=<fab> \
     FUSELEVEL=fuselevel_production ./nvmassfusegen.sh \
     <odm fuse options> <device_name>
   Where actual values are:
                                    bdid  bdsku  fab  bdrev  chiprev  chipsku
   --------------------------------+-----+------+----+------+--------+--------
   jetson-orin-nx-devkit-16gb (SKU0)3701  0000   500  J.0    1        D0
   jetson-agx-orin-devkit           3701  0000   500  J.0    1        D0
   jetson-agx-xavier-industrial     2888  0008   600  A.0    2        N/A
   jetson-xavier-nx-devkit-emmc     3668  0001   100  N/A    2        N/A
   jetson-agx-xavier-devkit (16GB)  2888  0001   400  H.0    2        N/A
   jetson-agx-xavier-devkit (32GB)  2888  0004   400  K.0    2        N/A
   --------------------------------+-----+------+----+------+--------+--------

   NOTE: All input and output are exactly same as ONLINE method.

   Examples for OFFLINE massfuse blob generation method:
   For t23x,
     To fuse with .xml template file:
       sudo BOARDID=3701 BOARDSKU=0000 FAB=500 BOARDREV=J.0 \
       FUSELEVEL=fuselevel_production  CHIPREV=1 CHIP_SKU=D0 \
       ./nvmassfusegen.sh -i 0x23 --auth NS -X <template.xml> \
       jetson-agx-orin-devkit

   For t19x,
     To fuse SBK key and PKC HASH:
       sudo BOARDID=2888 BOARDSKU=0001 FAB=400 BOARDREV=H.0 \
       FUSELEVEL=fuselevel_production CHIPREV=2 ./nvmassfusegen.sh -i 0x19 \
       --auth NS -p -k <key.pem> [--KEK{0-2} <KEK file>] -S <SBK file> \
       jetson-agx-xavier-devkit        # For AGX 16GB
       sudo BOARDID=2888 BOARDSKU=0004 FAB=400 BOARDREV=K.0 \
       FUSELEVEL=fuselevel_production CHIPREV=2 ./nvmassfusegen.sh -i 0x19 \
       --auth NS -p -k <key.pem> [--KEK{0-2} <KEK file>] -S <SBK file> \
       jetson-agx-xavier-devkit        # For AGX 32GB


========================================================================
Burning the Massfuse Blob
========================================================================
Burning the massfuse blob in untrusted environment requires:
- Set up one or more X86 Linux hosts as ``fusing hosts''.
  The fusing hosts do not require any L4T BSP installation.
- Use the following procedure to burn the fuses of one or more jetson
  devices simultaneously.
- Following procedure must be performed on each fusing hosts.

1. Download mfuse_<device_name>.tbz2 to each fusing host.

   Example:
   ubuntu@ahost:~$ scp loginname@<key host ipaddr>:Linux_for_Tegra/mfuse_jetson-agx-orin-devkit.tbz2
   loginname@<master host ipaddr?'s password:
   mfuse_jetson_agx-orin-devkit.tbz2        100% 1024KB   1.0MB/s   00:00

2. Untar mfuse_<device_name>.tbz2 image:

   Example:
   - tar xvjf mfuse_jetson-agx-orin-devkit.tbz2

3. Change directory to the massfuse blob directory.

   Example:
   - cd mfuse_jetson-agx-orin-devkit

4. Fuse multiple Jetson devices simultaneously:

   - Connect the Jetson devices to fusing hosts.
     (Make sure all devices are in exactly the same hardware revision as
     prepared in ``Building Massfuse Blob'' section above: Especially
     SKU, FAB, BOARDREV, etc... )
   - Put all of connected Jetsons into RCM mode.
   - Enter: `sudo ./nvmfuse.sh [--showlogs]`

     NOTE: nvmfuse.sh saves all massfusing logs in mfuselogs
           directory in mfuse_<device_name>[[_encrypt]_signed].
           Each log name has following format:
           ``<hostname>_<timestamp>_<pid>_fuse_<USB_path>.log''

     NOTE: This procedure can be repeated and all the boards burned
           with same massfuse blob have exactly same fuse configurations.
