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

from ctypes import *
import sys
import os

if sys.platform.startswith("win"):
    libname = "libnvtopo.dll"
else:
    libname = "libnvtopo.so"

def _openlib_board_automation():
    abs_dir_path = os.path.split(os.path.abspath(__file__))[0]
    libfile = os.path.join(abs_dir_path, libname)
    dll = CDLL(libfile)
    return dll

def _openlib_nv_outdir():
    nv_outdir = os.getenv("NV_OUTDIR")
    abs_dir_path = os.path.join(nv_outdir, "nvidia/tools-private/libnvtopo/tmake/lib-desktop_64")
    libfile = os.path.join(abs_dir_path, libname)
    dll = CDLL(libfile)
    return dll

def _openlib_ldpath():
    dll = CDLL(libname)
    return dll

def _open_libnvtopo():
    try:
        # First try look for the library in board_automation.
        return _openlib_board_automation()
    except:
        try:
            # Second try if it is in $NV_OUTDIR
            return _openlib_nv_outdir()
        except:
            # Final try rely on $LD_LIBRARY_PATH
            try:
                return _openlib_ldpath()
            except:
                raise Exception("Unable to find %s. " % (libname))

libnvtopo = None

class nv_topo_device_info(Structure):
    _fields_ = [
            ('fw_major_version', c_ubyte),
            ('fw_minor_version', c_ubyte),
            ('nv_topo_family', c_ubyte),
            ('nv_topo_model', c_ubyte),
            ('interface_major_version', c_ubyte),
            ('interface_minor_version', c_ubyte),
            ('platform_lsb', c_ubyte),
            ('platform_msb', c_ubyte),
            ('special_feature_lsb', c_ubyte),
            ('special_feature_msb', c_ubyte),
            ('gpio_count', c_ubyte),
            ('serial_number', c_char*64),
            ('gpio_mask', c_uint32)
            ]

class nv_topo_device(Structure):
    _fields_ = [
            ('handle', c_uint64)
            ]

class nv_topo_i2c_config(Structure):
    _fields_ = [
            ('i2c_frequency_khz', c_uint32)
            ]

error_code_to_str = {
        1 : "NV_TOPO_ERROR_INVALID_ARG",
        2 : "NV_TOPO_ERROR_OPEN_DEVICE",
        3 : "NV_TOPO_ERROR_CLOSE_DEVICE",
        4 : "NV_TOPO_ERROR_INVALID_CMD",
        5 : "NV_TOPO_ERROR_INVALID_USB_PATH",
        6 : "NV_TOPO_ERROR_OPEN_USB_PATH",
        7 : "NV_TOPO_ERROR_TOO_LONG_BUS_PATH",
        8 : "NV_TOPO_ERROR_OPEN_BUS_PATH",
        9 : "NV_TOPO_ERROR_TOO_LONG_DEV_PATH",
        10 : "NV_TOPO_ERROR_INVALID_EP",
        11 : "NV_TOPO_ERROR_READ_DEVICE",
        12 : "NV_TOPO_ERROR_INVALID_RESPONSE",
        13 : "NV_TOPO_ERROR_READ_INPUT_REPORT",
        14 : "NV_TOPO_ERROR_READ_INPUT_REPORT_SIZE",
        15 : "NV_TOPO_ERROR_WRITE_OUTPUT_REPORT",
        16 : "NV_TOPO_ERROR_WRITE_OUTPUT_REPORT_SIZE",
        17 : "NV_TOPO_ERROR_RECONNECT_INTERFACE",
        18 : "NV_TOPO_ERROR_RELEASE_INTERFACE",
        19 : "NV_TOPO_ERROR_CLAIM_INTERFACE",
        20 : "NV_TOPO_ERROR_GET_DRIVER",
        21 : "NV_TOPO_ERROR_DISCONNECT_INTERFACE",
        22 : "NV_TOPO_ERROR_GET_STRING_DESCRIPTOR",
        23 : "NV_TOPO_ERROR_INVALID_GPIO_PIN",
        24 : "NV_TOPO_ERROR_MISMATCH_GPIO_PIN",
        25 : "NV_TOPO_ERROR_DEVICE_NOT_OPENED",
        26 : "NV_TOPO_ERROR_MAX_DEVICE_REACHED",
        27 : "NV_TOPO_ERROR_USB_BUS_PATH_CONSTRUCT",
        28 : "NV_TOPO_ERROR_USB_DEV_PATH_CONSTRUCT",
        29 : "NV_TOPO_ERROR_USB_DEV_BUSY",
        30 : "NV_TOPO_ERROR_DEVICE_ALREADY_OPENED"
        }

NV_TOPO_RESET_TYPE = {
        "NORMAL" : 0,
        "ENTER_BOOTLOADER": 1
        }
NV_TOPO_GPIO_CONFIG = {
        "IN" : 0,
        "OUT_PP" : 1,
        "OUT_OD" : 2
        }
NV_TOPO_GPIO_INIT_STATE = {
        "LOW" : 0,
        "HIGH" : 1
        }
NV_TOPO_GPIO_VALUE = {
        "LOW" : 0,
        "HIGH" : 1
        }
NV_TOPO_PIN = {
        "PWR_BTN_N" : 0,
        "SYS_RST_N" : 1,
        "FRC_REC_N" : 2,
        "FRC_OFF_N" : 3,
        "NVDBUG_SEL" : 4,
        "NVJTAG_SEL" : 5,
        "MUX_SEL" : 6,
        "MODULE_PWR_ON" : 7,
        "VIN_PWR_ON" : 8,
        "PGOOD" : 9,
        "ACOK" : 10,
        "GPIO1" : 11,
        "GPIO2" : 12,
        "GPIO3" : 13,
        "GPIO4" : 14
        }
NV_TOPO_MAX_UID_LEN = 28

def lib_init():
    global libnvtopo
    libnvtopo = _open_libnvtopo()
    libnvtopo.nv_topo_init.restype = c_int
    libnvtopo.nv_topo_exit.restype = c_int
    libnvtopo.nv_topo_get_lib_version.restype = c_char_p

    libnvtopo.nv_topo_get_devices.restype = c_int
    libnvtopo.nv_topo_get_devices.argtypes = [POINTER(c_uint32), POINTER(nv_topo_device)]

    libnvtopo.nv_topo_get_info.restype = c_int
    libnvtopo.nv_topo_get_info.argtypes = [ nv_topo_device, POINTER(nv_topo_device_info)]

    libnvtopo.nv_topo_open.restype = c_int
    libnvtopo.nv_topo_open.argtypes = [nv_topo_device]

    libnvtopo.nv_topo_close.restype = c_int
    libnvtopo.nv_topo_close.argtypes = [nv_topo_device]

    libnvtopo.nv_topo_reset.restype = c_int
    libnvtopo.nv_topo_reset.argtypes = [nv_topo_device, c_uint32]

    libnvtopo.nv_topo_get_uid.restype = c_int
    libnvtopo.nv_topo_get_uid.argtypes = [nv_topo_device, c_char_p, c_uint32]

    libnvtopo.nv_topo_set_uid.restype = c_int
    libnvtopo.nv_topo_set_uid.argtypes = [nv_topo_device, c_char_p]

    libnvtopo.nv_topo_get_gpio_config.restype = c_int
    libnvtopo.nv_topo_get_gpio_config.argtypes = [nv_topo_device, c_uint32, POINTER(c_uint32)]

    libnvtopo.nv_topo_set_gpio_config.restype = c_int
    libnvtopo.nv_topo_set_gpio_config.argtypes = [nv_topo_device, c_uint32, c_uint32, c_uint32]

    libnvtopo.nv_topo_get_gpio_value.restype = c_int
    libnvtopo.nv_topo_get_gpio_value.argtypes = [nv_topo_device, c_uint32, POINTER(c_uint32)]

    libnvtopo.nv_topo_set_gpio_value.restype = c_int
    libnvtopo.nv_topo_set_gpio_value.argtypes = [nv_topo_device, c_uint32, c_uint32]

    libnvtopo.nv_topo_get_i2c_config.restype = c_int
    libnvtopo.nv_topo_get_i2c_config.argtypes = [nv_topo_device, POINTER(nv_topo_i2c_config)]

    libnvtopo.nv_topo_set_i2c_config.restype = c_int
    libnvtopo.nv_topo_set_i2c_config.argtypes = [nv_topo_device, POINTER(nv_topo_i2c_config)]

    libnvtopo.nv_topo_i2c_read.restype = c_int
    libnvtopo.nv_topo_i2c_read.argtypes = [nv_topo_device, c_uint32, c_uint32, POINTER(c_uint8)]

    libnvtopo.nv_topo_i2c_write.restype = c_int
    libnvtopo.nv_topo_i2c_write.argtypes = [nv_topo_device, c_uint32, c_uint32, POINTER(c_uint8)]

    libnvtopo.nv_topo_i2c_write_read.restype = c_int
    libnvtopo.nv_topo_i2c_write_read.argtypes = \
        [nv_topo_device, c_uint32, c_uint32, c_uint32, POINTER(c_uint8), POINTER(c_uint8)]

def nv_topo_init():
    lib_init()
    ret = libnvtopo.nv_topo_init()
    if ret != 0:
        raise Exception("nv_topo_init failed with return value: "+error_code_to_str[ret])

def nv_topo_exit():
    ret = libnvtopo.nv_topo_exit()
    if ret != 0:
        raise Exception("nv_topo_exit failed with return value: "+error_code_to_str[ret])

def nv_topo_get_lib_version():
    return str(libnvtopo.nv_topo_get_lib_version())

def nv_topo_get_devices():
    cnt = c_uint32(0)
    ret = libnvtopo.nv_topo_get_devices(byref(cnt),None)
    if ret != 0:
        raise Exception("nv_topo_get_devices failed with return value: "+error_code_to_str[ret])
    devices = (nv_topo_device * cnt.value)()
    ret = libnvtopo.nv_topo_get_devices(byref(cnt),devices)
    if ret != 0:
        raise Exception("nv_topo_get_devices failed with return value: "+error_code_to_str[ret])
    return cnt,devices

def nv_topo_get_info(device):
    devInfo = nv_topo_device_info()
    ret = libnvtopo.nv_topo_get_info(device, byref(devInfo))
    if ret != 0:
        raise Exception("nv_topo_get_info failed with return value: "+error_code_to_str[ret])
    return devInfo

def nv_topo_open(device):
    ret = libnvtopo.nv_topo_open(device)
    if ret != 0:
        raise Exception("nv_topo_open failed with return value: "+error_code_to_str[ret])

def nv_topo_close(device):
    ret = libnvtopo.nv_topo_close(device)
    if ret != 0:
        raise Exception("nv_topo_close failed with return value: "+error_code_to_str[ret])

def nv_topo_reset(device, reset_type):
    ret = libnvtopo.nv_topo_reset(device, reset_type)
    if ret != 0:
        raise Exception("nv_topo_reset failed with return value: "+error_code_to_str[ret])

def nv_topo_get_gpio_config(device, pin):
    config = c_uint32(0)
    ret = libnvtopo.nv_topo_get_gpio_config(device, pin, byref(config))
    if ret != 0:
        raise Exception("nv_topo_get_gpio_config failed with return value: "+error_code_to_str[ret])
    return config

def nv_topo_set_gpio_config(device, pin, config, initial_state):
    ret = libnvtopo.nv_topo_set_gpio_config(device, pin, config, initial_state)
    if ret != 0:
        raise Exception("nv_topo_set_gpio_config failed with return value: "+error_code_to_str[ret])

def nv_topo_get_gpio_value(device, pin):
    value = c_uint32(0)
    ret = libnvtopo.nv_topo_get_gpio_value(device, pin, byref(value))
    if ret != 0:
        raise Exception("nv_topo_get_gpio_value failed with return value: "+error_code_to_str[ret])
    return value

def nv_topo_set_gpio_value(device, pin, value):
    ret = libnvtopo.nv_topo_set_gpio_value(device, pin, value)
    if ret != 0:
        raise Exception("nv_topo_set_gpio_value failed with return value: "+error_code_to_str[ret])

def nv_topo_set_uid(device, uid):
    ret = libnvtopo.nv_topo_set_uid(device, c_char_p(uid.encode('utf-8')))
    if ret != 0:
        raise Exception("nv_topo_set_uid failed with return value: "+error_code_to_str[ret])

def nv_topo_get_uid(device):
    buff = create_string_buffer(NV_TOPO_MAX_UID_LEN+1)
    ret = libnvtopo.nv_topo_get_uid(device, buff, NV_TOPO_MAX_UID_LEN)
    if ret != 0:
        raise Exception("nv_topo_get_uid failed with return value: "+error_code_to_str[ret])
    return str(buff.value)

def nv_topo_set_i2c_config(device, i2c_config):
    ret = libnvtopo.nv_topo_set_i2c_config(device, byref(i2c_config))
    if ret != 0:
        raise Exception("nv_topo_set_i2c_config failed with return value: "+error_code_to_str[ret])

def nv_topo_get_i2c_config(device):
    i2c_config = nv_topo_i2c_config()
    ret = libnvtopo.nv_topo_get_i2c_config(device, byref(i2c_config))
    if ret != 0:
        raise Exception("nv_topo_get_i2c_config failed with return value: "+error_code_to_str[ret])
    return i2c_config

def nv_topo_i2c_read(device, i2c_addr, read_size):
    buff = (c_ubyte * read_size)()
    ret = libnvtopo.nv_topo_i2c_read(device, i2c_addr, read_size, buff)
    if ret != 0:
        raise Exception("nv_topo_i2c_read failed with return value: "+error_code_to_str[ret])
    return buff

def nv_topo_i2c_write(device, i2c_addr, msg_len, msg_buff):
    ret = libnvtopo.nv_topo_i2c_write(device, i2c_addr, msg_len, msg_buff)
    if ret != 0:
        raise Exception("nv_topo_i2c_write failed with return value: "+error_code_to_str[ret])

def nv_topo_i2c_write_read(device, i2c_addr, write_size, read_size, write_buff):
    read_buff = (c_ubyte * read_size)()
    ret = libnvtopo.nv_topo_i2c_write_read(
            device, i2c_addr, write_size, read_size, write_buff, read_buff)
    if ret != 0:
        raise Exception("nv_topo_i2c_write_read failed with return value: "+error_code_to_str[ret])
    return read_buff


# ----------------------------------------


class NvTopoClass(object):
    """NvTopoClass
        This is a class interface to the low-level,
        free-function API also provided by libnvtopo_wrapper

        It provides resource management, and also a thin
        abstraction for the physical buttons present
        on a TOPO board.

        Client code needs to carefully manage the lifetime
        of this object, particularly with respect to
        deferred destruction by the GC.  This means
        either explicitly calling open/close, or
        else using `with` blocks on instances of
        this class.
    """
    # Mapping of GPIO names to corresponding TOPO pins
    GPIOS = {
            "GPIO_PWR_BTN_N"    : NV_TOPO_PIN["PWR_BTN_N"],
            "GPIO_SYS_RST_N"    : NV_TOPO_PIN["SYS_RST_N"],
            "GPIO_FRC_REC_N"    : NV_TOPO_PIN["FRC_REC_N"],
            "GPIO_FRC_OFF_N"    : NV_TOPO_PIN["FRC_OFF_N"],
            "GPIO_NVDBUG_SEL"   : NV_TOPO_PIN["NVDBUG_SEL"],
            "GPIO_NVJTAG_SEL"   : NV_TOPO_PIN["NVJTAG_SEL"],
            "GPIO_MUX_SEL"      : NV_TOPO_PIN["MUX_SEL"],
            "GPIO_MODULE_PWR_ON": NV_TOPO_PIN["MODULE_PWR_ON"],
            "GPIO_VIN_PWR_ON"   : NV_TOPO_PIN["VIN_PWR_ON"],
            "GPIO_PGOOD"        : NV_TOPO_PIN["PGOOD"],
            "GPIO_ACOK"         : NV_TOPO_PIN["ACOK"],
            "GPIO_GPIO1"        : NV_TOPO_PIN["GPIO1"],
            "GPIO_GPIO2"        : NV_TOPO_PIN["GPIO2"],
            "GPIO_GPIO3"        : NV_TOPO_PIN["GPIO3"],
            "GPIO_GPIO4"        : NV_TOPO_PIN["GPIO4"],
            }
    BUTTONS = {  # Mapping of physical button names to corresponding TOPO gpio names
            "ONKEY"         : "GPIO_PWR_BTN_N",
            "SYS_RESET"     : "GPIO_SYS_RST_N",
            "FORCE_RECOVERY": "GPIO_FRC_REC_N",
            "FORCE_OFF"     : "GPIO_FRC_OFF_N",
            }

    def __init__(self, device, name=None, ref=None):
        self.device = device
        self.name = name or 'topo'
        self.ref = ref or '??'
        self.opened = False

    def __del__(self):
        if self.opened:
            self.close()

    @staticmethod
    def get_device_count():
        _count,_ = nv_topo_get_devices()
        _count = _count.value   # because this was a c_uint()
        return _count

    @staticmethod
    def from_index(index=None, name=None):
        _count,_topodevs = nv_topo_get_devices()
        _count = _count.value   # because this was a c_uint()
        assert _count > 0, "No TOPOs were found"
        assert (index is not None) or (_count == 1), "Index required when there are multiple TOPOs"
        index = index if (index is not None) else 0
        assert index < _count, "Index must be between 0 and %d" % (_count - 1)
        return NvTopoClass(_topodevs[index], name=name, ref="index %d"%index)

    @staticmethod
    def from_serial(serial, name=None):
        _count,_topodevs = nv_topo_get_devices()
        _count = _count.value
        assert _count > 0, "No TOPOs were found"
        _index = None
        for i in range(_count):
            di = nv_topo_get_info(_topodevs[i])
            if di.serial_number.decode("utf-8") == serial:
                _index = i
                break
        assert _index is not None, "Serial '%s' not found" % serial
        return NvTopoClass(_topodevs[_index], name=name, ref="serial '%s'"%serial)

    def open(self):
        print("Opening communication channel for %s" % self.name)
        nv_topo_open(self.device)
        self.opened = True
        self.IOs = dict()
        info = self.get_device_info()
        for name in type(self).GPIOS.keys():
            io_bit = type(self).GPIOS[name]
            gpio_present = info.gpio_mask & (1 << io_bit)
            if gpio_present:
                self.IOs[name] = io_bit

    def close(self):
        print("Closing communication channel for %s" % self.name)
        nv_topo_close(self.device)
        self.opened = False

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
        return False  # indicates that exceptions should propagate

    def __del__(self):
        if self.opened:
            self.close()
        self.device = None

    def _assert_is_opened(func):
        def wrapper(self, *args, **kwargs):
            assert self.opened, "NvTopoClass instance must be opened first"
            return func(self, *args, **kwargs)
        return wrapper

    @_assert_is_opened
    def get_device_info(self):
        return nv_topo_get_info(self.device)

    @_assert_is_opened
    def get_IO_names(self):
        return self.IOs.keys()

    @_assert_is_opened
    def get_IO_value(self, pin_name):
        return nv_topo_get_gpio_value(self.device, self.IOs[pin_name]).value

    @_assert_is_opened
    def set_IO_value(self, pin_name, value):
        nv_topo_set_gpio_value(self.device, self.IOs[pin_name], NV_TOPO_GPIO_VALUE[value])

    @_assert_is_opened
    def get_IO_config(self, pin_name):
        return nv_topo_get_gpio_config(self.device, self.IOs[pin_name]).value

    @_assert_is_opened
    def set_IO_config(self, pin_name, config, init):
        nv_topo_set_gpio_config(self.device, self.IOs[pin_name], NV_TOPO_GPIO_CONFIG[config], NV_TOPO_GPIO_INIT_STATE[init])

    @_assert_is_opened
    def get_I2C_config(self):
        return nv_topo_get_i2c_config(self.device)

    @_assert_is_opened
    def set_I2C_config(self, i2c_config):
        nv_topo_set_i2c_config(self.device, i2c_config)

    @_assert_is_opened
    def i2c_read(self, i2c_addr, rlen):
        return nv_topo_i2c_read(self.device, i2c_addr, rlen)

    @_assert_is_opened
    def i2c_write(self, i2c_addr, wlen, wdata):
        nv_topo_i2c_write(self.device, i2c_addr, wlen, wdata)

    @_assert_is_opened
    def i2c_write_read(self, i2c_addr, wlen, rlen, wdata):
        return nv_topo_i2c_write_read(self.device, i2c_addr, wlen, rlen, wdata)

    @_assert_is_opened
    def reset_fw(self, type):
        nv_topo_reset(self.device, type)

    @_assert_is_opened
    def set_uid(self, uid):
        nv_topo_set_uid(self.device, uid)

    @_assert_is_opened
    def get_uid(self):
        uid = nv_topo_get_uid(self.device)
        return uid

    @_assert_is_opened
    def reset_uid_to_default(self):
        nv_topo_set_uid(self.device, "")

    @_assert_is_opened
    def hold_button(self, button):
        self.set_IO_config(type(self).BUTTONS[button], "OUT_OD", "LOW")
        self.set_IO_value(type(self).BUTTONS[button], "LOW")

    @_assert_is_opened
    def release_button(self, button):
        # Here we assume that a prior hold_button call configured the pin appropriately
        self.set_IO_value(type(self).BUTTONS[button], "HIGH")


