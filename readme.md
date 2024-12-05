## Image download command
```
sudo tar xpf  mfi_xxx-xxxx.tar.gz
cd mfi_xxx-xxxx

sudo ./tools/kernel_flash/l4t_initrd_flash.sh --flash-only --massflash 1 --network usb0  --showlogs
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

