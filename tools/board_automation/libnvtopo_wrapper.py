# Copyright (c) 2021-2022, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

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
        "PWR_BTN_N" : 0x0,
        "SYS_RST_N" : 0x1,
        "FRC_REC_N" : 0x2,
        "FRC_OFF_N" : 0x3,
        "NVDBUG_SEL" : 0x4,
        "NVJTAG_SEL" : 0x5,
        "MUX_SEL" : 0x6,
        "MODULE_PWR_ON" : 0x7,
        "VIN_PWR_ON" : 0x8,
        "PGOOD" : 0x9,
        "ACOK" : 0xA,
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
