#!/usr/bin/env python

# Copyright (c) 2021-2022, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

from __future__ import print_function
from functools import wraps
from libnvtopo_wrapper import *
import time
import atexit
import sys
try:
    from supported_targets_all import supported_targets
except:
    from supported_targets import supported_targets

# Sleep for X seconds and print '.' to indicate every second elapsed.
def sleep_progress(second):
    if (second <= 1):
        time.sleep(second)
    else:
        while second > 0:
            s = min([1, second])
            second = second - s
            time.sleep(s)
            print(".", end="")
            sys.stdout.flush()
        print("")

nv_topo_init()

# NV_TOPO target class implements target specific sequence.
# This is the base implementation for NV_TOPO target.
class nv_topo_target:
    """NV_TOPO_TARGET"""

    def __init__(self, device):
        self.device = device
        self.buttons = {
                "ONKEY" : NV_TOPO_PIN["PWR_BTN_N"],
                "SYS_RESET" : NV_TOPO_PIN["SYS_RST_N"],
                "FORCE_RECOVERY" : NV_TOPO_PIN["FRC_REC_N"],
                "FORCE_OFF" : NV_TOPO_PIN["FRC_OFF_N"],
                }

        # Fill up the IO list based on the supported pins from FW.
        self.IOs = {}
        info = self.get_device_info()
        io_list = {
                "GPIO_PWR_BTN_N" : NV_TOPO_PIN["PWR_BTN_N"],
                "GPIO_SYS_RST_N" : NV_TOPO_PIN["SYS_RST_N"],
                "GPIO_FRC_REC_N" : NV_TOPO_PIN["FRC_REC_N"],
                "GPIO_FRC_OFF_N" : NV_TOPO_PIN["FRC_OFF_N"],
                "GPIO_NVDBUG_SEL" : NV_TOPO_PIN["NVDBUG_SEL"],
                "GPIO_NVJTAG_SEL" : NV_TOPO_PIN["NVJTAG_SEL"],
                "GPIO_MUX_SEL" : NV_TOPO_PIN["MUX_SEL"],
                "GPIO_MODULE_PWR_ON" : NV_TOPO_PIN["MODULE_PWR_ON"],
                "GPIO_VIN_PWR_ON" : NV_TOPO_PIN["VIN_PWR_ON"],
                "GPIO_PGOOD" : NV_TOPO_PIN["PGOOD"],
                "GPIO_ACOK" : NV_TOPO_PIN["ACOK"],
                }
        for name in io_list.keys():
            io_bit = io_list[name]
            io_exist = info.gpio_mask & (1 << io_bit)
            if io_exist != 0:
                self.IOs[name] = io_bit

    def get_device_info(self):
        return nv_topo_get_info(self.device)

    def reset_fw(self, type):
        nv_topo_reset(self.device, type)

    def target_reset(self, delay=1):
        self.push_button("SYS_RESET")

    def target_recovery_mode(self):
        # This is generic sequence to put the board into recovery for all topo boards.
        self.target_power_off()

        print("Putting the board into recovery. Please do not interrupt the process!")
        self.hold_button("FORCE_RECOVERY")
        sleep_progress(1)
        self.push_button("ONKEY")
        sleep_progress(1)
        self.release_button("FORCE_RECOVERY")
        print("Recovery mode done.")

    def enable_USB(self):
        raise Exception("Not Implemented yet")

    def disable_USB(self):
        raise Exception("Not Implemented yet")

    def hold_button(self, button):
        nv_topo_set_gpio_config(self.device, self.buttons[button], NV_TOPO_GPIO_CONFIG["OUT_OD"], NV_TOPO_GPIO_INIT_STATE["LOW"] )
        nv_topo_set_gpio_value(self.device, self.buttons[button], NV_TOPO_GPIO_VALUE["LOW"])

    def release_button(self, button):
        nv_topo_set_gpio_value(self.device, self.buttons[button], NV_TOPO_GPIO_VALUE["HIGH"])

    def push_button(self, button, delay=1):
        self.hold_button(button)
        sleep_progress(delay)
        self.release_button(button)

    def target_power_on(self):
        # This is generic sequence to turn on the board for all topo boards.
        print("Starting power on sequence. TOPO will power off the board then power it back on.")
        self.target_power_off()
        self.push_button("ONKEY")
        print("Power on sequence done.")

    def set_uid(self, uid):
        nv_topo_set_uid(self.device, uid)

    def get_uid(self):
        uid = nv_topo_get_uid(self.device)
        return uid

    def reset_uid_to_default(self):
        nv_topo_set_uid(self.device, "")

    def target_power_off(self):
        # This is generic sequence to turn off the board for all topo boards.
        print("Powering off the board. This process may take about 16 seconds. Please do not interrupt the process!")
        self.push_button("ONKEY", delay=12)
        self.push_button("FORCE_OFF")
	# From board team:
        # measurement shows 2.4s until VDD_5V0_CVM drops below 500mV, so 3
        # seconds delay would be safe including CVM reset.
        sleep_progress(3)
        print("Powering off done.")

    def get_IO_names(self):
        return self.IOs.keys()

    def get_IO(self, gpio):
        return nv_topo_get_gpio_value(self.device, self.IOs[gpio]).value

    def set_IO(self, io_name, value):
        nv_topo_set_gpio_value(self.device, self.IOs[io_name], value)

    def get_IO_config(self, io_name):
        return nv_topo_get_gpio_config(self.device, self.IOs[io_name]).value

    def set_IO_config(self, io_name, config, init_value):
        nv_topo_set_gpio_config(self.device, self.IOs[io_name], NV_TOPO_GPIO_CONFIG[config], init_value )

class nv_orin_slt_target(nv_topo_target):
    """NV_SLT_TARGET"""

    # From board team:
    # measurement shows 2.4s until VDD_5V0_CVM drops below 500mV, so 3
    # seconds delay would be safe including CVM reset. However the delay could
    # be different on another board, so making the delay configurable.
    def __init__(self, device, off_delay=3):
        nv_topo_target.__init__(self, device)
        self.off_delay = off_delay

    def target_power_off(self):
        # On SLT, the power on button will actually toggle the power on and off.
        # So the board might be in unknown state if we do not keep track the
        # power on history.
        print("Powering off the board.")
        self.push_button("FORCE_OFF")
        sleep_progress(self.off_delay)
        print("Powering off done.")

# nvtoop class abstracts access to NV_TOPO target device.
class nv_topo:
    """NV_TOPO"""

    supported_targets = supported_targets['nv_topo']

    def __init__(self, target= 'topo', serial_number=None, index=None):
        self.dev_cnt, self.devices = nv_topo_get_devices()
        self.dev_cnt = self.dev_cnt.value
        self.target = target
        self.open_count = 0
        if self.dev_cnt < 1:
            raise Exception("No debugger found! If the target is connected and is detected by host (in lsusb as device 0955:7045) then try using sudo or setup udev rule.")
        if self.dev_cnt > 1 and serial_number==None and index==None:
            raise Exception("Multiple (" +str(self.dev_cnt) +
                    ") debuggers detected! Serial number or index is required.")
        self.device = None
        if serial_number is None:
            if (index is None):
                index = 0
            elif (index < 0 or index >= self.dev_cnt):
                raise Exception("Invalid index " + str(index) +
                    ". TOPO device count: " + str(self.dev_cnt))
            self.device = self.devices[index]
        else:
            for i in range(self.dev_cnt):
                di = nv_topo_get_info(self.devices[i])
                if di.serial_number.decode("utf-8") == serial_number:
                    self.device = self.devices[i]
            if self.device == None:
                raise Exception("No device with serial "+serial_number+" found.")

    def __del__(self):
        if self.open_count > 0:
            nv_topo_close(self.device)

    def open(self):
        self.open_count += 1
        if self.open_count == 1:
            nv_topo_open(self.device)
            self._create_target(self.target)

    def close(self):
        self.open_count -= 1
        if self.open_count == 0:
            nv_topo_close(self.device)
        elif self.open_count < 0:
            self.open_count = 0
            raise Exception("TOPO mismatch open/close count")

    def open_close_ctx(f):
        @wraps(f)
        def wrapper(wrapped_self, *args, **kwds):
            ret = 0
            ex = 0
            try:
              wrapped_self.open()
              ret = f(wrapped_self, *args, **kwds)
            except Exception as e:
              ex = e
            finally:
              wrapped_self.close()
            if ex != 0:
              raise ex
            return ret
        return wrapper

    @open_close_ctx
    def get_device_info(self):
        return self.nv_topo_target.get_device_info()

    @open_close_ctx
    def reset_fw_bootloader(self):
        self.nv_topo_target.reset_fw(NV_TOPO_RESET_TYPE["ENTER_BOOTLOADER"])

    @open_close_ctx
    def target_reset(self, delay=1):
        self.nv_topo_target.target_reset(delay)

    @open_close_ctx
    def target_recovery_mode(self):
        self.nv_topo_target.target_recovery_mode()

    @open_close_ctx
    def enable_USB(self):
        self.nv_topo_target.enable_USB()

    @open_close_ctx
    def disable_USB(self):
        self.nv_topo_target.disable_USB()

    @open_close_ctx
    def hold_button(self, button):
        self.nv_topo_target.hold_button(button)

    @open_close_ctx
    def release_button(self, button):
        self.nv_topo_target.release_button(button)

    @open_close_ctx
    def push_button(self, button, delay=1):
        self.nv_topo_target.push_button(button, delay)

    @open_close_ctx
    def target_power_on(self):
        self.nv_topo_target.target_power_on()

    @open_close_ctx
    def target_power_off(self):
        self.nv_topo_target.target_power_off()

    @open_close_ctx
    def set_uid(self, uid):
        self.nv_topo_target.set_uid(uid)

    @open_close_ctx
    def get_uid(self):
        return self.nv_topo_target.get_uid()

    @open_close_ctx
    def reset_uid_to_default(self):
        self.nv_topo_target.reset_uid_to_default()

    @open_close_ctx
    def nvjtag_sel(self, enable):
        io_name = "GPIO_NVJTAG_SEL"
        io_config = "OUT_OD"
        io_init_state = 1
        io_state = 1
        if enable:
            io_state = 0
        self.set_IO_config(io_name, io_config, io_init_state)
        self.set_IO(io_name, io_state)

    @open_close_ctx
    def nvdbug_sel(self, enable):
        io_name = "GPIO_NVDBUG_SEL"
        io_config = "OUT_OD"
        io_init_state = 1
        io_state = 1
        if enable:
            io_state = 0
        self.set_IO_config(io_name, io_config, io_init_state)
        self.set_IO(io_name, io_state)

    def is_VDD_CORE_on(self):
        # No VDD info from TOPO so return void for all TOPO board.
        return

    def is_VDD_CPU_on(self):
        # No VDD info from TOPO so return void for all TOPO board.
        return

    @open_close_ctx
    def get_IO_names(self):
        return self.nv_topo_target.get_IO_names()

    @open_close_ctx
    def get_IO(self, gpio):
        return self.nv_topo_target.get_IO(gpio)

    @open_close_ctx
    def set_IO(self, io_name, value):
        self.nv_topo_target.set_IO(io_name, value)

    @open_close_ctx
    def get_IO_config(self, io_name):
        return self.nv_topo_target.get_IO_config(io_name)

    @open_close_ctx
    def set_IO_config(self, io_name, config, init_value):
        self.nv_topo_target.set_IO_config(io_name, config, init_value)

    def _create_target(self, target):
        if target == "topo":
            self.nv_topo_target = nv_topo_target(self.device)
        elif target == "orin-slt":
            self.nv_topo_target = nv_orin_slt_target(self.device)
        elif target == "concord":
            self.nv_topo_target = nv_topo_target(self.device)
        else:
            raise Exception("NV_TOPO target: " + target + " is not supported")

def print_device_info(devInfo):
    print("fw_major_version: ", devInfo.fw_major_version)
    print("fw_minor_version: ", devInfo.fw_minor_version)
    print("nv_topo_family: ", devInfo.nv_topo_family)
    print("nv_topo_model: ", devInfo.nv_topo_model)
    print("interface_major_version: ", devInfo.interface_major_version)
    print("interface_minor_version: ", devInfo.interface_minor_version)
    print("platform_lsb: ", devInfo.platform_lsb)
    print("platform_msb: ", devInfo.platform_msb)
    print("special_feature_lsb: ", devInfo.special_feature_lsb)
    print("special_feature_msb: ", devInfo.special_feature_msb)
    print("gpio_count: ", devInfo.gpio_count)
    print("gpio_mask: 0x%x" % devInfo.gpio_mask)
    print("serial_number: ", devInfo.serial_number)

def _cmd_list(args):
    c, d = nv_topo_get_devices()
    print("Index\tSerial Number")
    for i in range(c.value):
        di = nv_topo_get_info(d[i])
        print(str(i) + "\t" + str(di.serial_number.decode('UTF-8')))

def _cmd_dev_info(args):
    m = nv_topo("topo", None, args.i)
    info = m.get_device_info()
    print_device_info(info)

def _cmd_set_uid(args):
    m = nv_topo("topo", None, args.i)
    m.set_uid(args.n)

def _cmd_reset_uid(args):
    m = nv_topo("topo", None, args.i)
    m.reset_uid_to_default()

def _cmd_fw_version(args):
    m = nv_topo("topo", None, args.i)
    dev_info = m.get_device_info()
    print("%d.%02d" % (dev_info.fw_major_version, dev_info.fw_minor_version))

def _cmd_reset_fw_bootloader(args):
    m = nv_topo("topo", None, args.i)
    m.reset_fw_bootloader()

def _cmd_get_io_names(args):
    m = nv_topo("topo", None, args.i)
    io_names = m.get_IO_names()
    for n in io_names:
        print(n)

def _cmd_get_io_config(args):
    m = nv_topo("topo", None, args.i)
    io_config = m.get_IO_config(args.n)
    found = 0
    for key in NV_TOPO_GPIO_CONFIG.keys():
        if NV_TOPO_GPIO_CONFIG[key] == io_config:
            print(key)
            found = 1
    if found == 0:
        print("Unknown config: %d" % io_config)

def _cmd_set_io_config(args):
    m = nv_topo("topo", None, args.i)
    m.set_IO_config(args.n, args.c, args.v)

def _cmd_get_io_value(args):
    m = nv_topo("topo", None, args.i)
    io_val = m.get_IO(args.n)
    print(io_val)

def _cmd_set_io_value(args):
    m = nv_topo("topo", None, args.i)
    m.set_IO(args.n, args.v)

_commands = {
    "list" : {
        "help" : "list all available boards",
        "arguments" : [],
        "cmd_fn" : _cmd_list
    },

    "dev_info" : {
        "help" : "list all available boards",
        "arguments" : [
            {
                "flag" : "-i",
                "data" : {"action" : 'store', "help" : "index of the board in the list", "required" : True, "type" : int, "required" : True}
            },
        ],
        "cmd_fn" : _cmd_dev_info
    },

    "set_uid" : {
        "help" : "set board serial number",
        "arguments" : [
            {
                "flag" : "-i",
                "data" : {"action" : 'store', "help" : "index of the board in the list", "required" : True, "type" : int, "required" : True}
            },
            {
                "flag" : "-n",
                "data" : {"action" : 'store', "help" : "new serial number", "required" : True}
            }
        ],
        "cmd_fn" : _cmd_set_uid
    },

    "reset_uid" : {
        "help" : "reset board serial number",
        "arguments" : [
            {
                "flag" : "-i",
                "data" : {"action" : 'store', "help" : "index of the board in the list", "required" : True, "type" : int, "required" : True}
            }
        ],
        "cmd_fn" : _cmd_reset_uid
    },

    "fw_version" : {
        "help" : "get FW version",
        "arguments" : [
            {
                "flag" : "-i",
                "data" : {"action" : 'store', "help" : "index of the board in the list", "required" : True, "type" : int, "required" : True}
            }
        ],
        "cmd_fn" : _cmd_fw_version
    },

    "reset_fw_bootloader" : {
        "help" : "reset FW to bootloader mode",
        "arguments" : [
            {
                "flag" : "-i",
                "data" : {"action" : 'store', "help" : "index of the board in the list", "required" : True, "type" : int, "required" : True}
            }
        ],
        "cmd_fn" : _cmd_reset_fw_bootloader
    },

    "get_io_names" : {
        "help" : "get GPIO names",
        "arguments" : [
            {
                "flag" : "-i",
                "data" : {"action" : 'store', "help" : "index of the board in the list", "required" : True, "type" : int, "required" : True}
            }
        ],
        "cmd_fn" : _cmd_get_io_names
    },

    "get_io_config" : {
        "help" : "get GPIO config (input, output (push-pull), or open-drain)",
        "arguments" : [
            {
                "flag" : "-i",
                "data" : {"action" : 'store', "help" : "index of the board in the list", "required" : True, "type" : int, "required" : True}
            },
            {
                "flag" : "-n",
                "data" : {"action" : 'store', "help" : "GPIO name", "required" : True}
            }
        ],
        "cmd_fn" : _cmd_get_io_config
    },

    "set_io_config" : {
        "help" : "configure GPIO as input, output (push-pull), or open-drain",
        "arguments" : [
            {
                "flag" : "-i",
                "data" : {"action" : 'store', "help" : "index of the board in the list", "required" : True, "type" : int, "required" : True}
            },
            {
                "flag" : "-n",
                "data" : {"action" : 'store', "help" : "GPIO name", "required" : True}
            },
            {
                "flag" : "-c",
                "data" : {"action" : 'store', "help" : "GPIO config (IN / OUT_PP / OUT_OD)", "required" : True}
            },
            {
                "flag" : "-v",
                "data" : {"action" : 'store', "help" : "GPIO state (0 or 1)", "type" : int, "required" : True}
            }
        ],
        "cmd_fn" : _cmd_set_io_config
    },

    "get_io_value" : {
        "help" : "get GPIO state",
        "arguments" : [
            {
                "flag" : "-i",
                "data" : {"action" : 'store', "help" : "index of the board in the list", "required" : True, "type" : int, "required" : True}
            },
            {
                "flag" : "-n",
                "data" : {"action" : 'store', "help" : "GPIO name", "required" : True}
            }
        ],
        "cmd_fn" : _cmd_get_io_value
    },

    "set_io_value" : {
        "help" : "set GPIO state",
        "arguments" : [
            {
                "flag" : "-i",
                "data" : {"action" : 'store', "help" : "index of the board in the list", "required" : True, "type" : int, "required" : True}
            },
            {
                "flag" : "-n",
                "data" : {"action" : 'store', "help" : "GPIO name", "required" : True}
            },
            {
                "flag" : "-v",
                "data" : {"action" : 'store', "help" : "GPIO state (0 or 1)", "type" : int, "required" : True}
            }
        ],
        "cmd_fn" : _cmd_set_io_value
    }
}

def _parse_argument():
    import argparse
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest='command')

    for cmd_name in _commands.keys():
        cmd = _commands[cmd_name]
        cmd_parser = subparsers.add_parser(cmd_name, help=cmd["help"])
        for arg in cmd["arguments"]:
            cmd_parser.add_argument(arg["flag"], **arg["data"])

    args = parser.parse_args()
    return args

if __name__=="__main__" :
    args = _parse_argument()
    try:
        _commands[args.command]["cmd_fn"](args)
    except:
        raise
