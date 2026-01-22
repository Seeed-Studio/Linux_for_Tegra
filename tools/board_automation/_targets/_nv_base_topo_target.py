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
from functools import wraps
import time
import atexit
import sys

from libnvtopo_wrapper import *
from ._common import *


# ----------------------------------------


BOLD='\033[1m'
RESET='\033[0m'

# Even though this script is nominally Python3, it gets used as a module by
#  scripts that are Python2.  So, we have to eschew things like `Enum`
class RCMState(object):
    _UNSUPPORTED  = (-1,    "UNSUPPORTED detection for this Tegra or host OS")
    _INCONSISTENT = (-2,    "INCONSISTENT recovery states among constituent TOPO aspects (too few nvidia RCM devices)")
    _UNKNOWN      = (None,  "UNKNOWN: on linux, try `"+BOLD+"sudo apt install python3-usb"+RESET+"` to fix")
    _AMBIGUOUS    = (0,     "AMBIGUOUS (too many nvidia RCM devices)")
    _DISABLED     = (False, "NOT IN RECOVERY MODE")
    _ENABLED      = (True,  "IN RECOVERY MODE")

    def __init__(self, value):
        self.value = value

    def __str__(self):
        return self.value[1]

    def __repr__(self):
        return self.value[0]

# Construct our fake 'Enum' values
RCMState.UNSUPPORTED = RCMState(RCMState._UNSUPPORTED)
RCMState.INCONSISTENT = RCMState(RCMState._INCONSISTENT)
RCMState.UNKNOWN = RCMState(RCMState._UNKNOWN)
RCMState.AMBIGUOUS = RCMState(RCMState._AMBIGUOUS)
RCMState.DISABLED = RCMState(RCMState._DISABLED)
RCMState.ENABLED = RCMState(RCMState._ENABLED)


def _is_singular_item(thing):
    return not isinstance(thing,(list, tuple, set, dict))

class nv_base_topo_target(object):
    """NV_BASE_TOPO_TARGET
        This is a container that generalizes
        topo targets as having 1..N topos.  In a multi-TOPO
        target, this script calls each constituent TOPO
        an 'aspect'.

        This class also implements the traditional physical
        buttons typically found on a TOPO module (albeit
        by default will press/release the same button on
        all aspects)

        It also implements the sequences defined for a
        legacy, generic TOPO target (albeit by default
        will perform those sequences on all aspects.)
    """
    def __init__(self, topos):
        self.entered = False
        if _is_singular_item(topos) and isinstance(topos, NvTopoClass):
            topos = [topos]  # convert to list
        assert isinstance(topos, list), "Argument 'topos' must be a single NvTopoClass or a list of NvTopoClass objects"
        assert len(topos) > 0, "Argument 'topos' must provide at least one NvTopoClass object"
        self.aspects = topos

    def __del__(self):
        if self.entered:
            for topo in self.aspects:
                topo.close()

    def __enter__(self):
        self.entered = True
        for topo in self.aspects:
            topo.open()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        for topo in self.aspects:
            topo.close()
        self.entered = False
        return False  # indicates that exceptions should propagate

    def get_device_info(self, aid):
        dev_infos = []
        for topo in self.aspects:
            dev_infos.append(topo.get_device_info())
        return dev_infos

    def reset_fw_normal(self, aid):
        self.aspects[aid].reset_fw(NV_TOPO_RESET_TYPE["NORMAL"])

    def reset_fw_bootloader(self, aid):
        self.aspects[aid].reset_fw(NV_TOPO_RESET_TYPE["ENTER_BOOTLOADER"])

    def i2c_read(self, i2c_addr, rlen, aid):
        # Explicit aspect ID is required for I2C transactions; cannot default
        #  TODO: but if ever useful, we could spec `None` to mean all (but still not default)
        return self.aspects[aid].i2c_read(i2c_addr, rlen)

    def i2c_write(self, i2c_addr, wlen, wdata, aid):
        # Explicit aspect ID is required for I2C transactions; cannot default
        #  TODO: but if ever useful, we could spec `None` to mean all (but still not default)
        self.aspects[aid].i2c_write(i2c_addr, wlen, wdata)

    def i2c_write_read(self, i2c_addr, wlen, rlen, wdata, aid):
        # Explicit aspect ID is required for I2C transactions; cannot default
        #  TODO: but if ever useful, we could spec `None` to mean all (but still not default)
        return self.aspects[aid].i2c_write_read(i2c_addr, wlen, rlen, wdata)

    def get_IO_names(self, aid):
        # Explicit aspect ID is required for retrieving GPIO names; cannot default
        return self.aspects[aid].get_IO_names()

    def get_IO_value(self, pin_name, aid):
        # Explicit aspect ID is required for retrieving GPIO values; cannot default
        return self.aspects[aid].get_IO_value(pin_name)

    def set_IO_value(self, pin_name, value, aid):
        # Explicit aspect ID is required for changing GPIO values; cannot default
        #  TODO: but if ever useful, we could spec `None` to mean all (but still not default)
        return self.aspects[aid].set_IO_value(pin_name, value)

    def get_IO_config(self, pin_name, aid):
        # Explicit aspect ID is required for retrieving GPIO config; cannot default
        return self.aspects[aid].get_IO_config(pin_name)

    def set_IO_config(self, pin_name, config, init, aid):
        # Explicit aspect ID is required for changing GPIO config; cannot default
        #  TODO: but if ever useful, we could spec `None` to mean all (but still not default)
        return self.aspects[aid].set_IO_config(pin_name, config, init)

    def hold_button(self, button, aid=None):
        # If no aspect ID given, then all associated TOPOs will have their buttons held
        aspect_nums = range(len(self.aspects)) if aid is None else [aid]
        for aid in aspect_nums:
            if 1 < len(aspect_nums):
                nv_dbgprint("--- Holding %s for aspect %d..." % (button, aid))
            self.aspects[aid].hold_button(button)

    def release_button(self, button, aid=None):
        # If no aspect ID given, then all associated TOPOs will have their buttons released
        aspect_nums = range(len(self.aspects)) if aid is None else [aid]
        for aid in aspect_nums:
            if 1 < len(aspect_nums):
                nv_dbgprint("--- Releasing %s for aspect %d..." % (button, aid))
            self.aspects[aid].release_button(button)

    def push_button(self, button, delay=1, aid=None):
        self.hold_button(button, aid=aid)
        if (delay > 0):
            sleep_progress(delay)
        self.release_button(button, aid=aid)

    def target_reset(self, options=None, aid=None):
        # N.B. aid==None will cause buttons to be pressed on each associated TOPO
        if options is not None and hasattr(options, 'delay'):
            self.push_button("SYS_RESET", delay=options.delay, aid=aid)
        else:
            self.push_button("SYS_RESET", aid=aid)

    def target_power_on(self, options=None, aid=None):
        # This is sequence to turn on the board for all generic topo boards.
        print("Starting power on sequence. TOPO will power off the board then power it back on.")
        self.target_power_off(aid=aid)
        self.push_button("ONKEY", aid=aid)
        print("Power on sequence done.")

    def target_power_off(self, aid=None):
        # This is sequence to turn off the board for all generic topo boards.
        print("Powering off the board. This process may take about 19 seconds. Please do not interrupt the process!")
        self.push_button("ONKEY", delay=12, aid=aid)
        # Similar delay like below after turning off the board.
        sleep_progress(3)
        self.push_button("FORCE_OFF", aid=aid)
        # From board team:
        # measurement shows 2.4s until VDD_5V0_CVM drops below 500mV, so 3
        # seconds delay would be safe including CVM reset.
        sleep_progress(3)
        print("Powering off done.")

    def _recovery_mode(self, button, aid=None):
        # If no aspect ID given, then all associated TOPOs will be given this sequence
        self.hold_button("FORCE_RECOVERY", aid=aid)
        sleep_progress(1)
        self.push_button(button, aid=aid)
        sleep_progress(1)
        self.release_button("FORCE_RECOVERY", aid=aid)

    def target_recovery_mode(self, options=None, aid=None):
        self.target_power_off(aid=aid)
        print("Putting the board into recovery. Please do not interrupt the process!")
        self._recovery_mode("ONKEY", aid=aid)
        print("Recovery mode done.")

    def recovery_status(self, vid=0x0955, pid=0x7020, pidmask=None):
        # N.B.  I dont know how to associate particular instances
        #  of matching usbdevs to aspects.  So, we dont even
        #  try here; we just report ENABLED if len(found) has
        #  the expected value
        if pid == None:
            return RCMState.UNSUPPORTED
        if not pidmask:
            pidmask = 0xF0FF # appropriate default pidmask value for APX, but not others (e.g., UARTs)
        if g_linux_sans_pyusb:
            return RCMState.UNKNOWN
        nv_dbgprint("  Checking for vid=%s, pid=%s, pidmask=%s" % (hex(vid),hex(pid),hex(pidmask)))
        found = get_nvidia_rcm_devices(vid, pid, pidmask)
        if len(found) == 0:
            return RCMState.DISABLED
        elif len(found) < len(self.aspects):
            return RCMState.INCONSISTENT
        elif len(found) > len(self.aspects):
            return RCMState.AMBIGUOUS
        else:
            return RCMState.ENABLED

    def print_status(self):
        for topo in self.aspects:
            _hdr = "FOR TOPO-ASPECT '%s':" % topo.name
            print("\n%s\n%s" % (_hdr, '-'*len(_hdr)))
            for gpio in sorted([x for x in topo.get_IO_names() if "GPIO" in x]):
                print("    " + gpio + " is %d" % topo.get_IO_value(gpio))
        print("")

    def apply_options(self, options=None):
        pass


__all__ = ['nv_dbgprint', 'sleep_progress', 'RCMState', 'nv_base_topo_target']

