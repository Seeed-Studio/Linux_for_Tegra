#! /usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) 2021-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
# property and proprietary rights in and to this material, related
# documentation and any modifications thereto. Any use, reproduction,
# disclosure or distribution of this material and related documentation
# without an express license agreement from NVIDIA CORPORATION or
# its affiliates is strictly prohibited.

from __future__ import print_function
import importlib


# ----------------------------------------


# Copied from _targets._common for... reasons
import time
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


from libnvtopo_wrapper import *
nv_topo_init()


# ----------------------------------------


# nv_topo class abstracts access to various topo-controlled targets.
class nv_topo(object):
    """NV_TOPO"""
    def __init__(self, target='topo', **kwargs):
        _serial = kwargs.get('serial', None)
        _serial2 = kwargs.get('serial2', None)
        _index = kwargs.get('index', None)
        _secidx = kwargs.get('secidx', None)
        self.target = target
        try:
            _tname = self.target.lstrip('_')  # avoid importing certain files, here
            _tmodule = importlib.import_module("_targets." + _tname)
            self.required_topos = getattr(_tmodule, 'TOPOS_REQUIRED')
            self.instantiate = getattr(_tmodule, 'instantiate')
        except ImportError as e:
            print("NV_TOPO target: '" + self.target + "' is not supported (%s)"%e)
            sys.exit(1)
        except AttributeError as e:
            print("INTERNAL ERROR: could not import '" + self.target + "': %s" % e)
            sys.exit(2)
        devcount = NvTopoClass.get_device_count()
        if devcount < self.required_topos:
            raise Exception("Not enough TOPOs found for target '%s'! If the target is connected and is detected by host (in lsusb as device 0955:7045) then try using sudo or setup udev rule." % target)
        if _secidx is not None and self.required_topos < 2:
            raise Exception("'secidx' should not be given for targets that utilize only 1 TOPO.")
        if devcount > self.required_topos and (_serial==None and _index==None and _secidx==None):
            raise Exception("Too many (" +str(devcount) +
                    ") TOPOs detected for target '%s'! Serial number or index %sis required%s." % (target, "of primary " if self.required_topos == 2 else "", " (also secidx)" if self.required_topos == 2 else ""))
        self.topopri = None
        self.toposec = None
        if _serial is None:
            if _index is None:
                if devcount > self.required_topos:
                    raise Exception("Too many TOPOs present to infer primary index for target '%s'." % target)
                else:  # devcount == self.required_topos and self.required_topos == 2
                    _index = 1 if _secidx == 0 else 0  # inferring primary index
                    print("NOTE: inferring topo index %s for primary, based on USB device count of %s and secidx=%s" % (_index, devcount, _secidx))
            self.topopri = NvTopoClass.from_index(_index)
        else:
            assert _index is None, "Cannot give both 'index' (%s) and 'serial' (%s)" % (str(_index), _serial)
            self.topopri = NvTopoClass.from_serial(_serial)
        assert 1 <= self.required_topos <= 2, "Unexpected value for 'self.required_topos' (%d)" % self.required_topos
        if self.required_topos == 2:
            if _serial2 is None:
                if _secidx == None:
                    if devcount > self.required_topos:
                        raise Exception("Too many TOPOs present to infer secondary index for target '%s'." % target)
                    else:  # devcount == self.required_topos and self.required_topos == 2
                        _secidx = 1 if _index == 0 else 0  # inferring secondary index
                        print("NOTE: inferring topo index %d for secondary, based on USB device count of %d and primary index=%d" % (_secidx, devcount, _index))
                else:
                    assert _secidx != _index, "Arguments 'index' (%d) and 'secidx' (%d) cannot have the same value." % (_index, _secidx)
                self.toposec = NvTopoClass.from_index(_secidx)
            else:
                assert _secidx is None, "Cannot give both 'secidx' (%s) and 'serial2' (%s)" % (str(_secidx), _serial2)
                self.toposec = NvTopoClass.from_serial(_serial2)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        return False  # propagate Exceptions

    def _assert_multi_has_aid(func):
        def wrapper(self, *args, **kwargs):
            aid = kwargs.get('aid')
            assert self.required_topos <= 1 or (aid is not None), "The 'aid' argument is required for multi-TOPO targets"
            kwargs['aid'] = aid or 0  # turn None into 0 (now that we know None is acceptable)
            return func(self, *args, **kwargs)
        return wrapper

    @_assert_multi_has_aid
    def get_device_info(self, aid=None):
        with self._create_target() as t:
            return t.get_device_info(aid=aid)

    @_assert_multi_has_aid
    def reset_fw_normal(self, aid=None):
        with self._create_target() as t:
            t.reset_fw_normal(aid=aid)

    @_assert_multi_has_aid
    def reset_fw_bootloader(self, aid=None):
        with self._create_target() as t:
            t.reset_fw_bootloader(aid=aid)

    def target_reset(self, options=None):
        print("Issuing reset sequence via topo for '%s'..." % self.target)
        with self._create_target() as t:
            t.target_reset(options)

    def target_recovery_mode(self, options=None):
        print("Entering recovery mode via topo for '%s'..." % self.target)
        with self._create_target() as t:
            t.target_recovery_mode(options)
            print("Delaying for 2 seconds to let recovery USB device enumerate...")
            sleep_progress(2)
            print("Attempting to verify RCM entry...")
            print("Recovery status is now: %s" % t.recovery_status())

    def target_options_only(self, options=None):
        print("Applying options via topo for '%s'..." % self.target)
        with self._create_target() as t:
            t.apply_options(options)

    # This API will press the button for all TOPOs associated with the target
    def hold_button(self, button):
        with self._create_target() as t:
            t.hold_button(button)

    # This API will press the button for all TOPOs associated with the target
    def release_button(self, button):
        with self._create_target() as t:
            t.release_button(button)

    # This API will press the button for all TOPOs associated with the target
    def push_button(self, button, options=None):
        with self._create_target() as t:
            t.apply_options(options)
            if options is not None and hasattr(options, 'delay'):
                t.push_button(button, options.delay)
            else:
                t.push_button(button)

    def target_power_on(self, options=None):
        print("Issuing power-on sequence via topo...")
        with self._create_target() as t:
            t.target_power_on(options)

    def target_power_off(self):
        print("Issuing power-off sequence via topo...")
        with self._create_target() as t:
            t.target_power_off()

    @_assert_multi_has_aid
    def nvjtag_sel(self, enable, aid=None):
        io_name = "GPIO_NVJTAG_SEL"
        io_config = "OUT_OD"
        io_init_state = 'HIGH'
        io_state = 'HIGH'
        if enable:
            io_state = 0
        self.set_IO_config(io_name, io_config, io_init_state, aid)
        self.set_IO_value(io_name, io_state, aid)

    @_assert_multi_has_aid
    def nvdbug_sel(self, enable, aid=None):
        io_name = "GPIO_NVDBUG_SEL"
        io_config = "OUT_OD"
        io_init_state = 'HIGH'
        io_state = 'HIGH'
        if enable:
            io_state = 0
        self.set_IO_config(io_name, io_config, io_init_state, aid)
        self.set_IO_value(io_name, io_state, aid)

    def is_VDD_CORE_on(self):
        # No VDD info from TOPO so return void for all TOPO board.
        return

    def is_VDD_CPU_on(self):
        # No VDD info from TOPO so return void for all TOPO board.
        return

    @_assert_multi_has_aid
    def get_IO_names(self, aid=None):
        with self._create_target() as t:
            return t.get_IO_names(aid)

    @_assert_multi_has_aid
    def get_IO_value(self, io_name, aid=None):
        with self._create_target() as t:
            return t.get_IO_value(io_name, aid)

    @_assert_multi_has_aid
    def set_IO_value(self, io_name, value, aid=None):
        with self._create_target() as t:
            t.set_IO_value(io_name, value, aid)

    @_assert_multi_has_aid
    def get_IO_config(self, io_name, aid=None):
        with self._create_target() as t:
            return t.get_IO_config(io_name, aid)

    @_assert_multi_has_aid
    def set_IO_config(self, io_name, config, init, aid=None):
        with self._create_target() as t:
            t.set_IO_config(io_name, config, init, aid)

    @_assert_multi_has_aid
    def i2c_read(self, i2c_addr, rlen, aid=None):
        with self._create_target() as t:
            return t.i2c_read(i2c_addr, rlen, aid)

    @_assert_multi_has_aid
    def i2c_write(self, i2c_addr, wlen, wdata, aid=None):
        with self._create_target() as t:
            t.i2c_write(i2c_addr, wlen, wdata, aid)

    @_assert_multi_has_aid
    def i2c_write_read(self, i2c_addr, wlen, rlen, wdata, aid=None):
        with self._create_target() as t:
            return t.i2c_write_read(i2c_addr, wlen, rlen, wdata, aid)

    def target_status(self):
        with self._create_target() as t:
            print("Recovery status for '%s': %s" % (self.target, t.recovery_status()))
            t.print_status()

    def target_ec_reset(self):
         with self._create_target() as t:
            return t.target_ec_reset()

    def _create_target(self):
        return self.instantiate([self.topopri, self.toposec])

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
    with NvTopoClass.from_index(args.i) as topo:
        info = topo.get_device_info()
        print_device_info(info)

def _cmd_set_uid(args):
    with NvTopoClass.from_index(args.i) as topo:
        topo.set_uid(args.n)

def _cmd_reset_uid(args):
    with NvTopoClass.from_index(args.i) as topo:
        topo.reset_uid_to_default()

def _cmd_fw_version(args):
    with NvTopoClass.from_index(args.i) as topo:
        dev_info = topo.get_device_info()
        print("%d.%02d" % (dev_info.fw_major_version, dev_info.fw_minor_version))

def _cmd_reset_fw(args):
    with NvTopoClass.from_index(args.i) as topo:
        topo.reset_fw(NV_TOPO_RESET_TYPE["NORMAL"])

def _cmd_reset_fw_bootloader(args):
    with NvTopoClass.from_index(args.i) as topo:
        topo.reset_fw(NV_TOPO_RESET_TYPE["ENTER_BOOTLOADER"])

def _cmd_get_io_names(args):
    with NvTopoClass.from_index(args.i) as topo:
        io_names = topo.get_IO_names()
        for n in io_names:
            print(n)

def _cmd_get_io_config(args):
    with NvTopoClass.from_index(args.i) as topo:
        io_config = topo.get_IO_config(args.n)
        found = 0
        for key in NV_TOPO_GPIO_CONFIG.keys():
            if NV_TOPO_GPIO_CONFIG[key] == io_config:
                print(key)
                found = 1
        if found == 0:
            print("Unknown config: %d" % io_config)

def _cmd_set_io_config(args):
    with NvTopoClass.from_index(args.i) as topo:
        topo.set_IO_config(args.n, args.c, args.v)

def _cmd_get_io_value(args):
    with NvTopoClass.from_index(args.i) as topo:
        io_val = topo.get_IO_value(args.n)
        print(io_val)

def _cmd_set_io_value(args):
    with NvTopoClass.from_index(args.i) as topo:
        topo.set_IO_value(args.n, args.v)

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

    "reset_fw" : {
        "help" : "reset TOPO FW",
        "arguments" : [
            {
                "flag" : "-i",
                "data" : {"action" : 'store', "help" : "index of the board in the list", "required" : True, "type" : int, "required" : True}
            }
        ],
        "cmd_fn" : _cmd_reset_fw
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
                "data" : {"action" : 'store', "help" : "GPIO state (LOW / HIGH)", "required" : True}
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
