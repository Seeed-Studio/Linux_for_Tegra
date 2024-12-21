## Image download command
```
tar xjf  mfi_xxx-xxxx.tbz2
cd mfi_xxx-xxxx

sudo ./nvmflash.sh --showlogs
```

## CI usage
### CI code can meet the following compilation requirements.
* compile all the images.
* just compile images for specific carrier board.
* just compile images for specific main board with different RAM size.
* just compile image for specific main board with specific RAM size.
* compile custom image with parameters provided by user.

### Here is the sheet for the detail CI config:
|CI parameter(s) list | CI parameter(s) value | CI job(s) will run|
|----------------|-----------------------|-------------------|
|just create a tag| NONE| all the jobs |
|START_TASK|reserver-industrial-orin|reserver-industrial-orin-nano-4g-j401<br>reserver-industrial-orin-nano-8g-j401<br>reserver-industrial-orin-nx-16g-j401<br>reserver-industrial-orin-nx-8g-j401|
|START_TASK|reserver-industrial-orin-nano|reserver-industrial-orin-nano-4g-j401<br>reserver-industrial-orin-nano-8g-j401|
|START_TASK|reserver-industrial-orin-nx|reserver-industrial-orin-nx-16g-j401<br>reserver-industrial-orin-nx-8g-j401|
|START_TASK|reserver-industrial-orin-nano-4g-j401|reserver-industrial-orin-nano-4g-j401|
|START_TASK|reserver-industrial-orin-nano-8g-j401|reserver-industrial-orin-nano-8g-j401|

### Here is the demo config for reference if you want to compile a custom image
|CI parameter(s) list | CI parameter(s) value | CI job(s) will run|
|----------------|-----------------------|-------------------|
|IS_CUSTOM<br>CONFIG<br>BOARDID<br>BOARDSKU<br>FAB<br>BOARDREV<br>CHIP_SKU|IS_CUSTOM:1<br>CONFIG:reserver-agx-orin-j501x-gmsl<br>BOARDID:3701<br>BOARDSKU:0005<br>FAB:500<br>BOARDREV:M.0<br>CHIP_SKU:00:00:00:D0|custom-firmware|

## Firmware source tracing
### We can trace the source of a specific firmware.
#### Here is a example:
```
seeed@ubuntu:~$ cat /etc/nv_tegra_release
# R36 (release), REVISION: 4.0, GCID: 37537400, BOARD: generic, EABI: aarch64, DATE: Fri Sep 13 04:36:44 UTC 2024
# KERNEL_VARIANT: oot
TARGET_USERSPACE_LIB_DIR=nvidia
TARGET_USERSPACE_LIB_DIR_PATH=usr/lib/aarch64-linux-gnu/nvidia
# Seeed Image Name mfi_recomputer-orin-nx-8g-j401-6.1-36.4.0-2024-12-04.tar.gz
# branch R36.4.0
# commit ID 970b38cf3ccaf8ca57ca3a889acd91dc153e464b
```
#### It shows the firmware name which included this rootfs is `mfi_recomputer-orin-nx-8g-j401-6.1-36.4.0-2024-12-04.tar.gz`. The code branch name is `R36.4.0` and the commit hash is `970b38cf3ccaf8ca57ca3a889acd91dc153e464b`.
>>>>>>> 2c2cae3 (update README)
