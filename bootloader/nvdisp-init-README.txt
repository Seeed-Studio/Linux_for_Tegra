NV Display Init

A subset of the T19x CBoot code is used to do HW display init prior to
UEFI load, so that UEFI can then move a BMP file to the T19x framebuffer
to do boot splash on T194 AGX, XNX and JAXi boards.

The nvdisp-init code is taken from the R32.7.1 CBoot source archive that
NVIDIA releases to the public, and patched to remove unneeded code and
add the ability to use the CPU-BL binary after display init and execute
the UEFI bootloader. Nothing in the flash map files (flash*.xml)
changes, as the combined nvdisp-init+UEFI binary still fits within the
4MB partition on eMMC on QSPI. Two minor changes to the common config
files (pXXXX.conf.common) for AGX/XNX/JAXi are made to allow the flash
tools to merge the two binaries into one & flash it as the CPU-BL.

To reproduce the nvdisp-init.bin binary:

1) Take the R32.7.1 T19x CBoot source tarball from that release and unpack it
   as per the CBoot README.
   - $ mkdir cboot
   - $ tar -xjf DO_NOT_DISTRIBUTE_cboot_src_t19x.tbz2 -C cboot

2) Apply the nvdisp-init.patch from the BSP.
   - $ cp <l4t-tree>/l4t/prebuilts/nvdisp-init/nvdisp-init.patch cboot
   - $ cd cboot
   - $ patch -p1 <nvdisp-init.patch

3) Build lk.bin as per the CBoot source README.
   - $ export CROSS_COMPILE=<l4t-tree>/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android-
   - $ export TEGRA_TOP=$PWD
   - $ export TOP=$PWD
   - $ make -C ./bootloader/partner/t18x/cboot PROJECT=t194 TOOLCHAIN_PREFIX="${CROSS_COMPILE}" DEBUG=2 BUILDROOT="${PWD}"/out NV_TARGET_BOARD=t194ref NV_BUILD_SYSTEM_TYPE=l4t NOECHO=@

   * The resulting out/build-t194/lk.bin file will be 208744 bytes, pad lk.bin out to 384KB using
     'truncate --size=393216 out/build-t194/lk.bin'
   * The md5sum of padded lk.bin is 5858f091034f9c13be579bef696016d5.
   * That lk.bin binary is then copied to the BSP 'bootloader' directory as
     nvdisp-init.bin.

To reverse this process, and skip display init and revert to a boot flow
without boot splash/display init on T194 boards, complete the following steps:

1) Change the TBCFILE= line in p3668.conf.common and p2972-0000.conf.common to
   point to uefi_jetson.bin, for example:
    TBCFILE="bootloader/uefi_jetson.bin";
2) Remove the UEFIFILE= line from the above files.
3) Edit flash.sh, and remove the following lines:
    if [ "${CHIPID}" = "0x19" ] and [ ${UEFIFILE} != "" ]; then
    echo "NVDISP+UEFI in ${TBCFILE} .."
    truncate --size=393216 "${TBCFILE}"
    cat "${UEFIFILE}" >> "${TBCFILE}"
    fi;

4) Delete nvdisp-init.bin from your BSP Linux_for_Tegra/bootloader directory.
5) Reflash your board.
   Now only UEFI will be run as the CPU-BL and no boot splash BMP will appear
   on HDMI.

