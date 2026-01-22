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
import time
import sys
import os
import platform

g_topodbg = os.getenv('NV_TOPODBG') or None
def nv_dbgprint(*args, **kwargs):
    if g_topodbg:
        print(*args, **kwargs)


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


# Even though this script is nominally Python3, it gets used as a module by
#  scripts that are Python2.  So, we have to eschew things like `Enum`
class HostOS(object):
    _UNSUPPORTED = (0, "UNSUPPORTED (supports Linux and Windows only)")
    _LINUX       = (1, "LINUX")
    _WINDOWS     = (2, "WINDOWS")
    def __init__(self, value):
        self.value = value
    def __str__(self):
        return self.value[1]
    def __repr__(self):
        self.value[0]
# Construct our fake 'Enum' values
HostOS.UNSUPPORTED = HostOS(HostOS._UNSUPPORTED)
HostOS.LINUX = HostOS(HostOS._LINUX)
HostOS.WINDOWS = HostOS(HostOS._WINDOWS)

def get_host_os():
    os_type = platform.system()
    if os_type == "Windows":
        return HostOS.WINDOWS
    elif os_type == "Linux":
        return HostOS.LINUX
    return HostOS.UNSUPPORTED
g_host_os = get_host_os()


g_linux_sans_pyusb = False   # initially assume not linux, or else has pyusb
print("NOTE: Host OS detected to be %s"%str(g_host_os))
if g_host_os == HostOS.LINUX:
    try:
        import usb.core
    except ImportError as e:
        print("NOTE: usb.core not found; this is OK, but disables recovery detection", file=sys.stderr)
        g_linux_sans_pyusb = True


def get_nvidia_rcm_devices(vid, pid, pidmask=0xFFFF):
    host_os = get_host_os()
    if host_os == HostOS.LINUX:
        return _get_usb_devices__linux(vid, pid, pidmask)
    elif host_os == HostOS.WINDOWS:
        return _get_usb_devices__windows(vid, pid, pidmask)
    else:
        raise NotImplementedError("Cannot enumerate USB devices due to host OS being %s"%str(host_os))


def _get_usb_devices__linux(vid, pid, pidmask):
    assert (g_host_os == HostOS.LINUX) and not g_linux_sans_pyusb, "Shouldn't get here if not linux, or else missing usb.core"
    nvdevs = usb.core.find(find_all=True, idVendor=vid)
    # `nvdevs` is a dynamic iterable, we want to return a static list
    # content doesn't particularly matter; callers use the list's (non)emptiness as (True)False
    return list(dev for dev in nvdevs if (dev.idProduct & pidmask) == (pid & pidmask))


def _get_usb_devices__windows(vid, pid, pidmask):
    import ctypes as ct
    from ctypes import wintypes as win
    kernel32 = ct.windll.kernel32

    setupapi = ct.WinDLL('setupapi')
    ULONG_PTR = win.WPARAM

    DIGCF_DEFAULT         =  0x00000001
    DIGCF_PRESENT         =  0x00000002
    DIGCF_ALLCLASSES      =  0x00000004
    DIGCF_PROFILE         =  0x00000008
    DIGCF_DEVICEINTERFACE =  0x00000010

    SPDRP_FRIENDLYNAME = 0x0000000C
    SPDRP_DEVICEDESC = 0x00000000

    def get_last_error():
        FORMAT_MESSAGE_FROM_SYSTEM = 0x00001000
        error_code = ct.GetLastError()
        buffer = ct.create_unicode_buffer(256)
        kernel32.FormatMessageW(
            FORMAT_MESSAGE_FROM_SYSTEM,
            None,
            error_code,
            0,
            buffer,
            len(buffer),
            None
        )
        return error_code, buffer.value

    class GUID(ct.Structure):
        _fields_ = (('Data1', ct.c_ulong),
                    ('Data2', ct.c_ushort),
                    ('Data3', ct.c_ushort),
                    ('Data4', ct.c_ubyte * 8))

    class SP_DEVINFO_DATA(ct.Structure):
        _fields_ = (('cbSize', win.DWORD),
                    ('ClassGuid', GUID),
                    ('DevInst', win.DWORD),
                    ('Reserved', win.WPARAM))  #ULONG_PTR))
        def __init__(self):
            self.cbSize = ct.sizeof(SP_DEVINFO_DATA)

    PDWORD = ct.POINTER(win.DWORD)
    PBYTE = ct.POINTER(win.BYTE)
    class HDEVINFO(win.HANDLE):
        pass

    setupapi.SetupDiGetClassDevsW.argtypes = ct.POINTER(GUID), win.PWCHAR, win.HWND, win.DWORD
    setupapi.SetupDiGetClassDevsW.restype = HDEVINFO
    setupapi.SetupDiEnumDeviceInfo.argtypes = HDEVINFO, win.DWORD, ct.POINTER(SP_DEVINFO_DATA)
    setupapi.SetupDiEnumDeviceInfo.restype = win.BOOL
    setupapi.SetupDiDestroyDeviceInfoList.argtypes = HDEVINFO,
    setupapi.SetupDiDestroyDeviceInfoList.restype = win.BOOL
    setupapi.SetupDiGetDeviceRegistryPropertyW.argtypes = HDEVINFO, ct.POINTER(SP_DEVINFO_DATA), win.DWORD, PDWORD, PBYTE, win.DWORD, PDWORD
    setupapi.SetupDiGetDeviceRegistryPropertyW.restype = win.BOOL
    ERR_DATA_INVALID = 13

    def _get_reg_string_prop(devinfoset, devinfo, prop):
        REG_SZ = 1
        prop_reg_data_type = win.DWORD()
        desc = (win.WCHAR * 256)()
        descsz = win.DWORD(ct.sizeof(desc))
        if setupapi.SetupDiGetDeviceRegistryPropertyW(
            devinfoset,
            ct.byref(devinfo),
            prop,
            ct.byref(prop_reg_data_type),
            ct.cast(desc, ct.POINTER(win.BYTE)),
            descsz,
            ct.byref(descsz)
        ):
            return desc.value if prop_reg_data_type.value == REG_SZ else "???"
        return ""   # normally should verify GetLastError() is ERR_DATA_INVALID (13), but don't care here?

    matches = []
    vidstr = "VID_"+format(vid, 'X').zfill(4)
    devinfo = SP_DEVINFO_DATA()
    hDevInfoSet = setupapi.SetupDiGetClassDevsW(None, "USB", None, DIGCF_PRESENT | DIGCF_ALLCLASSES)
    try:
        devidx = 0
        while setupapi.SetupDiEnumDeviceInfo(hDevInfoSet, devidx, ct.byref(devinfo)):
            buf = (win.CHAR * 1024)()
            bufsz = win.DWORD(ct.sizeof(buf))
            if setupapi.SetupDiGetDeviceInstanceIdA(hDevInfoSet, ct.byref(devinfo), buf, bufsz, ct.byref(bufsz)):
                instname = buf.value.decode('utf-8')
                if vidstr in instname:
                    pidpos = instname.find('PID_')
                    if pidpos == -1:
                        raise ValueError("Malformed USB instance name '%s'"%instname)
                    pidstr = instname[pidpos+4:pidpos+8]
                    pidval = int(pidstr,16)
                    if (pidval & pidmask) == (pid & pidmask):
                        desc1 = _get_reg_string_prop(hDevInfoSet, devinfo, SPDRP_FRIENDLYNAME)
                        desc2 = _get_reg_string_prop(hDevInfoSet, devinfo, SPDRP_DEVICEDESC)
                        matches.append((instname, desc1, desc2))
            devidx += 1
        ERROR_NO_MORE_ITEMS = 259
        if ct.GetLastError() != 0 and ct.GetLastError() != ERROR_NO_MORE_ITEMS:
            errcode, errmsg = get_last_error()
            print("SetupDiEnumDeviceInfo failed with error: %s - %s"%(errcode, errmsg), file=sys.stderr)
    finally:
        setupapi.SetupDiDestroyDeviceInfoList(hDevInfoSet)
    nv_dbgprint("%s :: matches : %s"%(_get_usb_devices__windows.__name__, matches))
    return matches


