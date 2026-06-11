#! /usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
# property and proprietary rights in and to this material, related
# documentation and any modifications thereto. Any use, reproduction,
# disclosure or distribution of this material and related documentation
# without an express license agreement from NVIDIA CORPORATION or
# its affiliates is strictly prohibited.

from ctypes import *
from nvtopo import NvTopoClass
from ._common import *

# N.B. This file is implementation details of board_automation.

class TCA6408:
    I2C_7BIT_ADDR_DEFAULT = 0x20    # lsb-justified (for interop with libnvtopo.so)
    I2C_7BIT_ADDR_ALTERNATE = 0x21
    REG_INPUT  = 0x00
    REG_OUTPUT = 0x01
    REG_INVERT = 0x02
    REG_CONFIG = 0x03

    def __init__(self, topodev, i2c_addr=I2C_7BIT_ADDR_DEFAULT, cfgmask=0xFF, cfgdir=0xFF, cfgpol=0):
        assert cfgmask == (cfgmask & 0xFF)
        assert cfgdir == (cfgdir & 0xFF)
        assert cfgpol == (cfgpol & 0xFF)
        self.topodev = topodev
        self.i2c_addr = i2c_addr
        self._configure(cfgmask, cfgdir, cfgpol, verify=True)

    def _read_reg(self, reg):
        assert reg == (reg & 0xFF)
        wdata = (c_ubyte *1)()
        wdata[0] = reg
        nv_dbgprint("        Reading 8-bit value from register %s of I2C address %s..." % (str(reg), hex(self.i2c_addr)))
        rdata = self.topodev.i2c_write_read(self.i2c_addr, 1, 1, wdata)
        # Presumably, nv_topo would raise on NAK/timeout condition
        result = rdata[0] & 0xFF
        nv_dbgprint("            reg=%s, read=%s" % (str(reg), hex(result)))
        return result

    def _write_reg(self, reg, val, verify=False, prev=None):
        assert reg == (reg & 0xFF)
        assert val == (val & 0xFF)
        if verify and prev is None:
            prev = self._read_reg(reg)
        wdata = (c_ubyte *2)()
        wdata[0] = reg
        wdata[1] = val
        nv_dbgprint("        Writing 8-bit value %s to register %s of I2C address %s..." % (hex(val & 0xFF), str(reg), hex(self.i2c_addr)))
        self.topodev.i2c_write(self.i2c_addr, 2, wdata)
        if verify:
            confirm = self._read_reg(reg)
            nv_dbgprint("            Confirm reg=%s: prev=%s, written=%s, confirm=%s" % (str(reg), hex(prev & 0xFF), hex(val & 0xFF), hex(confirm & 0xFF)))
            return confirm == val
        return True  # presumably, nv_topo would raise on NAK/timeout condition

    def _rmw_reg(self, reg, mask, val, verify):
        assert reg == (reg & 0xFF)
        assert mask == (mask & 0xFF)
        assert val == (val & 0xFF)
        rmwtext = ""
        if mask:
            rmwtext = " (having applied read-mask %s)" % hex(~mask & 0xFF)
            prev = self._read_reg(reg)
            new = (val & mask) | (prev & (~mask & 0xFF))
        else:
            new = val
        nv_dbgprint("    Changing 8-bit value of register %s to %s%s..." % (str(reg), hex(new & 0xFF), rmwtext))
        return self._write_reg(reg, new, verify=verify, prev=prev)

    def _get_config(self):
        return self._read_reg(type(self).REG_CONFIG)

    def _set_config(self, mask, dir, verify=False):
        '''
        If a bit in this register is set to 1, the corresponding port pin is enabled as
        an input with a high-impedance output driver.  If a bit in this register is cleared
        to 0, the corresponding port pin is enabled as an output.
        '''
        if not self._rmw_reg(type(self).REG_CONFIG, mask, dir, verify):
            raise Exception("TCA6408: Failed to change configuration register value (attempted %s)" %hex(dir))

    def _get_polarities(self):
        return self._read_reg(type(self).REG_INVERT)

    def _set_polarities(self, mask, polarities, verify=False):
        '''
        If a bit in this register is set, the corresponding pin's polarity is inverted.
        If a bit in this register is cleared, the corresponding pin's original polarity is retained.
        '''
        if not self._rmw_reg(type(self).REG_INVERT, mask, polarities, verify):
            raise Exception("TCA6408: Failed to change polarity inversion register value (attempted %s)" % hex(polarities))

    def _configure(self, cfgmask, cfgdir, cfgpol, verify=False):
        nv_dbgprint("TCA6408: Configuring directions (%s) of these pins: %s..." % (hex(cfgdir), hex(cfgmask)))
        self._set_config(cfgmask, cfgdir, verify)
        nv_dbgprint("TCA6408: Configuring these pins to have inverted polarity: %s..." % hex(cfgpol & 0xFF))
        self._set_polarities(cfgmask, cfgpol, verify)

    def get_inputs(self):
        return self._read_reg(type(self).REG_INPUT)

    def set_inputs(self, values, verify=False):
        raise Exception("TCA6408: Unsupported (ineffective) write attempt to input register")

    def get_outputs(self):
        return self._read_reg(type(self).REG_OUTPUT)

    def set_outputs(self, values, mask=0, verify=False):
        '''
        Bit values in this register have no effect on pins defined as inputs
        '''
        nv_dbgprint("TCA6408: Changing output-pin states...")
        if not self._rmw_reg(type(self).REG_OUTPUT, mask, values, verify):
            raise Exception("TCA6408: Failed to change output register value (attempted %s)" % hex(values))


