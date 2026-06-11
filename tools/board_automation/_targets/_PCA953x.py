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

import sys
from ctypes import *
from nvtopo import NvTopoClass
from ._common import *

# N.B. This file is implementation details of board_automation.

class _PCA953x:
    REG_INPUT  = 0x00
    REG_OUTPUT = 0x02
    REG_INVERT = 0x04
    REG_CONFIG = 0x06

    def __init__(self, topodev, i2c_addr, cfgmask16=0xFFFF, cfgdir16=0xFFFF, cfgpol16=0):
        assert cfgmask16 == (cfgmask16 & 0xFFFF)
        assert cfgdir16 == (cfgdir16 & 0xFFFF)
        assert cfgpol16 == (cfgpol16 & 0xFFFF)
        self.topodev = topodev
        self.i2c_addr = i2c_addr
        self._configure(cfgmask16, cfgdir16, cfgpol16, verify=True)

    def _read_reg16(self, reg):
        assert reg == (reg & 0xFF)
        wdata = (c_ubyte *1)()
        wdata[0] = reg & 0xFF
        nv_dbgprint("        Reading 16-bit value from register %s of I2C address %s..." % (str(reg), hex(self.i2c_addr)))
        rdata = self.topodev.i2c_write_read(self.i2c_addr, 1, 2, wdata)
        # Presumably, nv_topo would raise on NAK/timeout condition
        result = (rdata[1] << 8) | rdata[0]
        nv_dbgprint("            reg=%s, read=%s" % (str(reg), hex(result)))
        return result

    def _write_reg16(self, reg, val16, verify=False, prev16=None):
        assert reg == (reg & 0xFF)
        assert val16 == (val16 & 0xFFFF)
        if verify and prev16 is None:
            prev16 = self._read_reg16(reg)
        wdata = (c_ubyte *3)()
        wdata[0] = reg
        wdata[1] = (val16 >> 0) & 0xFF
        wdata[2] = (val16 >> 8) & 0xFF
        nv_dbgprint("        Writing 16-bit value %s to register %s of I2C address %s..." % (hex(val16 & 0xFFFF), str(reg), hex(self.i2c_addr)))
        self.topodev.i2c_write(self.i2c_addr, 3, wdata)
        if verify:
            confirm = self._read_reg16(reg)
            nv_dbgprint("            Confirm reg=%s: prev=%s, written=%s, confirm=%s" % (str(reg), hex(prev16 & 0xFFFF), hex(val16 & 0xFFFF), hex(confirm & 0xFFFF)))
            return confirm == val16
        return True  # presumably, nv_topo would raise on NAK/timeout condition

    def _rmw_reg16(self, reg, mask16, val16, verify):
        rmwtext = ""
        if mask16:
            rmwtext = " (having applied read-mask %s)" % hex(~mask16 & 0xFFFF)
            prev16 = self._read_reg16(reg)
            new16 = (val16 & mask16) | (prev16 & (~mask16 & 0xFFFF))
        else:
            prev16 = None
            new16 = val16
        nv_dbgprint("    Changing 16-bit value of register %s to %s%s..." % (str(reg), hex(new16 & 0xFFFF), rmwtext))
        return self._write_reg16(reg, new16, verify=verify, prev16=prev16)

    def _get_config(self):
        return self._read_reg16(type(self).REG_CONFIG)

    def _set_config(self, mask16, dir16, verify):
        '''
        If a bit in this register is set to 1, the corresponding port pin is enabled as
        an input with a high-impedance output driver.  If a bit in this register is cleared
        to 0, the corresponding port pin is enabled as an output.
        '''
        if not self._rmw_reg16(type(self).REG_CONFIG, mask16, dir16, verify):
            raise Exception("PCA9535: Failed to change configuration register value (attempted %s)" % hex(dir16), file=sys.stderr)

    def _get_polarities(self):
        return self._read_reg16(type(self).REG_INVERT)

    def _set_polarities(self, mask16, pol16, verify):
        '''
        If a bit in this register is set, the corresponding pin's polarity is inverted.
        If a bit in this register is cleared, the corresponding pin's original polarity is retained.
        '''
        if not self._rmw_reg16(type(self).REG_INVERT, mask16, pol16, verify):
            raise Exception("PCA9535: Failed to change polarity inversion register value (attempted %s)" % hex(pol16), file=sys.stderr)

    def _configure(self, cfgmask16, cfgdir16, cfgpol16, verify=False):
        nv_dbgprint("PCA9535: Configuring directions (%s) of these pins: %s..." % (hex(cfgdir16), hex(cfgmask16)))
        cfgmask16 = cfgmask16 if cfgmask16 else 0xFFFF
        self._set_config(cfgmask16, cfgdir16, verify)
        nv_dbgprint("PCA9535: Configuring these pins (%s) to have polarity: %s..." % (hex(cfgmask16), hex(cfgmask16 & cfgpol16 & 0xFFFF)))
        self._set_polarities(cfgmask16, cfgpol16, verify)

    def get_inputs(self):
        return self._read_reg16(type(self).REG_INPUT)

    def set_inputs(self, values16, mask16=0, verify=False):
        raise Exception("PCA9535: Ineffective write attempt to input register")

    def get_outputs(self):
        return self._read_reg16(type(self).REG_OUTPUT)

    def set_outputs(self, values16, mask16=0, verify=False):
        '''
        Bit values in this register have no effect on pins defined as inputs
        '''
        nv_dbgprint("PCA9535: Changing output-pin states...")
        if not self._rmw_reg16(type(self).REG_OUTPUT, mask16, values16, verify):
            raise Exception("PCA9535: Failed to change output register value (attempted %s)" % hex(new16), file=sys.stderr)


class PCA9535(_PCA953x):
    I2C_7BIT_ADDR_DEFAULT = 0x25    # lsb-justified (for interop with libnvtopo.so)
    def __init__(self, topodev, i2c_addr=I2C_7BIT_ADDR_DEFAULT, **kwargs):
        assert i2c_addr >= 0x20 and i2c_addr <= 0x27
        super().__init__(topodev, i2c_addr, **kwargs)


class PCA9539(_PCA953x):
    I2C_7BIT_ADDR_DEFAULT = 0x74    # lsb-justified (for interop with libnvtopo.so)
    def __init__(self, topodev, i2c_addr=I2C_7BIT_ADDR_DEFAULT, **kwargs):
        assert i2c_addr >= 0x74 and i2c_addr <= 0x77
        super().__init__(topodev, i2c_addr, **kwargs)

