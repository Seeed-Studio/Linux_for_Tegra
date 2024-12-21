=======================================================================
                        README-RAPID-BOOT on Jetson-Nano
                          Linux for Tegra
=======================================================================

This README file provides details on NVIDIA Jetson Nano platform.

Follows these steps:
1. Enter the command:
   $ cd Linux_for_Tegra
2. If you don't want USB device mode support, make the following change to
   disable USB device mode. This saves about 2.5 seconds of boot time.

File-name: rootfs/etc/udev/rules.d/99-nv-l4t-usb-device-mode.rules
-ACTION=="change" KERNEL=="android0" SUBSYSTEM=="android_usb" RUN+="/opt/nvidia/l4t-usb-device-mode/nv-l4t-usb-device-mode-state-change.sh"
+#ACTION=="change" KERNEL=="android0" SUBSYSTEM=="android_usb" RUN+="/opt/nvidia/l4t-usb-device-mode/nv-l4t-usb-device-mode-state-change.sh"

3. Enter this command to flash the target for rapid boot:
   $ sudo ./flash.sh --rapid-boot -C "quiet " jetson-nano-emc mmcblk0p1

4. After the target is flashed and Linux boots to the command line prompt
   for the first time, the system runs background processes
   [e.g usb-device mode settings etc] These processes takes about 45 seconds
   to complete after the prompt appears. Do not reboot the target until the
   process has finished running. After subsequent boots you may reboot as
   soon as the command line prompt appears.
