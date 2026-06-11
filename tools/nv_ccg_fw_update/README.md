# nv_ccg_fw_update tool

## Overview

This is a CLI tool for updating the firmware of CCG8 devices over I2C. It supports
checking the current firmware version and flashing new firmware. Currently, only
x86_64 Linux hosts are supported.

**[CAUTION]**: Use this tool with caution. Incorrect usage may lead to bricking the device.
- Do not interrupt the firmware update process or remove the power once it has started.
- After a successful firmware update, you may need to manually manually unplug all cables
(all USBC cables &DC power cables) and replug them to complete the update process.
- Only one device can be updated at a time. Do not connect multiple devices to the host when using this tool.

## Usage

This is how you can use the tool:

```
Usage: nv_ccg_fw_update [OPTIONS]
Options:
  -i, --i2c <i2c>           Specify the I2C interface (e.g., I2C2)
  -v, --ver                 Check and print firmware version
      --fw1 <path>          Specify the path to FW1 binary
      --fw2 <path>          Specify the path to FW2 binary
  -h, --help                Display this help message
```

To check the firmware version.

```
$ nv_ccg_fw_update -i I2C2 -v
```

To update the firmware.

```
$ nv_ccg_fw_update -i I2C2 --fw1 fw1.cyacd2 --fw2 fw2.cyacd2
```

## Known Issues

### I2C read failure when using USBC power supply
You may encounter the following message if using USBC power supply on your device, and running the tool:

```
Reset CCG device...
[ERROR] I2C read failed with error code 15
[ERROR] Failed to read interrupt register
[ERROR] Timeout. No interrupt received
```

The issue is: If use DC power supply (microfit_2x2pin) and we send reset command
to CCG8, it can tell whether reset is complete and shows succeed. However, if use
USBC power supply and we send reset command to CCG8, it can't tell whether
reset is complete.

If you encounter this error, wait until the process exits, then try unplugging and
replugging all Type-C cables to manually reset, and then run the tool again to
confirm the firmware version. If the firmware version is updated successfully,
it means the firmware update process is successful.

### Incorrect firmware version after update failure

When using incorrect firmware to update, the device will enter bootloader but fail to
update. After that you may see incorrect firmware version because it doesn't exit
bootloader properly.

```
$ nv_ccg_fw_update -i I2C2 -v
FW1 Version Tag: 00.00
FW2 Version Tag: 0f.80
```

If either FW version shows 00.00, it indicates PD controller is in FW updating mode.
User can either continue updating FW or unplug all power cables to restore previous
firmware.
