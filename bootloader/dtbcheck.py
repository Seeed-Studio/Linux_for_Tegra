#!/usr/bin/env python3
#
# Copyright (c) 2019-2022, NVIDIA CORPORATION. All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

import argparse
import re
import struct
import sys
import zlib
from collections import OrderedDict

import t194
import t234

__dtbcheck_magic = 0x44544243 # DTBC
_dtb_header_fmt = '>10L'
_dtb_header_len = struct.calcsize(_dtb_header_fmt)
__dtb_header_off_totalsize = 4
__dtb_header_off_off_dt_struct = 8
__dtb_alignment = 64
__phandle_fmt = '>L'
__metadata_fmt = '>4L'
__metadata_len = struct.calcsize(__metadata_fmt)
__signature_fmt = '>L'
__signature_len = struct.calcsize(__signature_fmt)
__signature_mask = 0xFFFFFFFF

__machine = t194

__clk_min_rate = 0 # 0 Hz
__clk_max_rate = 6000000000 # 6 GHz
__regulator_voltage_min_uv = 450000 # 450 mV
__regulator_voltage_max_uv = 1300000 # 1300 mV
__thermal_min_temp = -60000 # -60 C
__thermal_max_temp = 127000 # 127 C

# Align value n to boundary b
def _align(n, b):
    return ((n + b - 1) // b) * b

class FdtHeader:
    __dtb_magic = 0xd00dfeed
    __dtb_version = 17
    __dtb_last_comp_version = 16

    def __init__(self):
        self.magic              = 0
        self.totalsize          = 0
        self.off_dt_struct      = 0
        self.off_dt_strings     = 0
        self.off_mem_rsvmap     = 0
        self.version            = 0
        self.last_comp_version  = 0
        self.boot_cpuid_phys    = 0
        self.size_dt_strings    = 0
        self.size_dt_struct     = 0

    def check(self, data):
        assert self.magic == self.__dtb_magic
        assert self.version == self.__dtb_version
        assert self.last_comp_version == self.__dtb_last_comp_version

        assert self.totalsize >= _dtb_header_len
        assert self.totalsize <= 2 ** 31

        assert self.off_dt_struct >= _dtb_header_len
        assert self.off_dt_struct + self.size_dt_struct <= self.totalsize
        assert self.off_dt_struct % 4 == 0

        assert self.off_dt_strings >= _dtb_header_len
        assert self.off_dt_strings + self.size_dt_strings <= self.totalsize

        assert self.off_mem_rsvmap >= _dtb_header_len
        assert self.off_mem_rsvmap <= self.totalsize
        assert self.off_mem_rsvmap % 8 == 0

        assert len(data) == self.totalsize

    def pad(self, size):
        self.totalsize += size

    def pack(self):
        data = struct.pack(_dtb_header_fmt,
                           self.magic, self.totalsize,
                           self.off_dt_struct, self.off_dt_strings,
                           self.off_mem_rsvmap, self.version,
                           self.last_comp_version, self.boot_cpuid_phys,
                           self.size_dt_strings, self.size_dt_struct)

        assert len(data) == _dtb_header_len

        return data

def __parse_header(data):
    header = FdtHeader()

    values = struct.unpack(_dtb_header_fmt, data)
    header.magic                = values[0]
    header.totalsize            = values[1]
    header.off_dt_struct        = values[2]
    header.off_dt_strings       = values[3]
    header.off_mem_rsvmap       = values[4]
    header.version              = values[5]
    header.last_comp_version    = values[6]
    header.boot_cpuid_phys      = values[7]
    header.size_dt_strings      = values[8]
    header.size_dt_struct       = values[9]

    return header

class FdtNode:
    def __init__(self, name, parent):
        self.name = name
        self.parent = parent
        self.children = OrderedDict()
        self.props = OrderedDict()
        self.phandles = {}
        self.depth = 0

        node = self
        while node.parent:
            node = node.parent
            self.depth += 1

    def path(self):
        if self.parent:
            p = self.parent.path()
            if p == '/':
                return '/{}'.format(self.name)
            else:
                return '{}/{}'.format(self.parent.path(), self.name)
        else:
            return '/'

    def get_by_phandle(self, phandle):
        # phandles are stored in root node
        node = self
        while node.parent:
            node = node.parent

        assert phandle in node.phandles, \
                "Invalid phandle '{}' at '{}'".format(phandle, self.path())

        return node.phandles[phandle]

    def __str__(self):
        name = self.name
        indent = ' ' * 4

        if not self.parent:
            name = '/'

        s = '{}{} {{\n'.format(indent * self.depth, name)

        for name, prop in self.props.items():
            s += '{}{} = <{}>;\n'.format(indent * (self.depth + 1),
                                         name, prop)

        for node in self.children.values():
            s += node.__str__()

        s += '{}}};\n'.format(indent * self.depth)

        return s

def __parse_string(data, offset):
    end = offset
    while data[end] not in (0, b'\x00'):
        end += 1

    return data[offset:end].decode()

def __parse_dt_struct(header, data, offset):
    FDT_BEGIN_NODE  = 0x00000001
    FDT_END_NODE    = 0x00000002
    FDT_PROP        = 0x00000003
    FDT_NOP         = 0x00000004
    FDT_END         = 0x00000009

    token_fmt = '>L'
    prop_fmt = '>2L'

    node_chars = '[0-9A-Za-z\+,\-\._]'
    node_pattern = re.compile('^{0:}+(@{0:}+)?$'.format(node_chars))

    prop_chars = '[0-9A-Za-z#\+,\-\.\?_]'
    prop_pattern = re.compile('^{}+$'.format(prop_chars))

    root = None
    node = None
    depth = 0

    while True:
        token = struct.unpack_from(token_fmt, data, offset)[0]
        offset += struct.calcsize(token_fmt)
        if token == FDT_BEGIN_NODE:
            name = __parse_string(data, offset)
            if root:
                assert node_pattern.match(name), \
                        "Invalid node name '{}'".format(name)

            offset = _align(offset + len(name) + 1, 4)
            n = FdtNode(name, node)
            if node:
                assert name not in node.children, \
                        "Duplicate node '{}'".format(name)
                node.children[name] = n
            node = n

            if not root:
                root = node

            depth += 1
        elif token == FDT_END_NODE:
            assert node, "FDT is corrupt"
            node = node.parent

            assert depth > 0, "FDT is corrupt"
            depth -= 1
        elif token == FDT_PROP:
            assert node, "FDT is corrupt"

            length, nameoff = struct.unpack_from(prop_fmt, data, offset)
            offset += struct.calcsize(prop_fmt)

            name = __parse_string(data, header.off_dt_strings + nameoff)
            assert prop_pattern.match(name), \
                    "Invalid property name '{}'".format(name)

            prop = data[offset:offset + length]
            offset = _align(offset + length, 4)
            assert name not in node.props, \
                    "Duplicate property '{}'".format(name)

            node.props[name] = prop

            if name == 'phandle':
                phandle = struct.unpack(__phandle_fmt, prop)[0]
                assert phandle not in root.phandles, \
                        "Duplicate phandle '{}'".format(phandle)
                root.phandles[phandle] = node
        elif token == FDT_NOP:
            pass
        elif token == FDT_END:
            assert depth == 0, "FDT is truncated"
            break
        else:
            assert False, "FDT is corrupt"

    return root

class FdtRule:
    def __init__(self, pattern, rule, subrules=[]):
        self.pattern = re.compile('^{}$'.format(pattern))
        self.rule = rule
        self.subrules = subrules

    def check(self, node):
        if self.pattern.match(node.name):
            self.rule(node)
            for name in node.children:
                child = node.children[name]
                for rule in self.subrules:
                    rule.check(child)

def __check_props(node, required=[], optional=[]):
    path = node.path()

    msg = "Node '{}' is missing required property '{}'"
    for name in required:
        assert name in node.props, msg.format(path, name)

    optional_patterns = [re.compile('^{}$'.format(x)) for x in optional]

    msg = "Node '{}' has unknown property '{}'"
    for name in node.props:
        in_optional = any([x.match(name) for x in optional_patterns])
        assert name in required or in_optional, msg.format(path, name)

def __check_children(node, required=[], optional=[]):
    path = node.path()

    msg = "Node '{}' is missing required child node '{}'"
    for name in required:
        assert name in node.children, msg.format(path, name)

    optional_patterns = [re.compile('^{}$'.format(x)) for x in optional]

    msg = "Node '{}' has unknown child node '{}'"
    for name in node.children:
        in_optional = any([x.match(name) for x in optional_patterns])
        assert name in required or in_optional, msg.format(path, name)

def __check_type(fmt, node, prop, func=lambda x: len(x) == 1, required=True):
    path = node.path()

    if required:
        msg = "Node '{}' is missing required property '{}'"
        assert prop in node.props, msg.format(path, prop)
    elif prop not in node.props:
        return ()

    type_fmt = '>{}'.format(fmt)
    type_size = struct.calcsize(type_fmt)

    data = node.props[prop]

    msg = "Node '{}' property '{}' has incorrect size"
    assert len(data) % type_size == 0, msg.format(path, prop)

    count = len(data) // type_size
    array_fmt = '>{}{}'.format(count, fmt)

    data = struct.unpack(array_fmt, data)

    msg = "Node '{}' property '{}' is invalid"
    assert func(data), msg.format(path, prop)

    return data

def __check_u8(*args, **kwargs):
    return __check_type('B', *args, **kwargs)

def __check_u32(*args, **kwargs):
    return __check_type('L', *args, **kwargs)

def __check_s32(*args, **kwargs):
    return __check_type('l', *args, **kwargs)

def __check_u64(*args, **kwargs):
    return __check_type('Q', *args, **kwargs)

def __check_float(*args, **kwargs):
    return __check_type('f', *args, **kwargs)

def __check_one(v, pred):
    return len(v) == 1 and pred(v[0])

def __check_arr(a, pred):
    for v in a:
        if not pred(v):
            return False
    return True

def __is_frequency_valid(frequency):
    return (len(frequency) == 1 and
            frequency[0] >= __clk_min_rate and
            frequency[0] <= __clk_max_rate)

def __is_voltage_valid(voltage):
    return (len(voltage) == 1 and
            voltage[0] >= __regulator_voltage_min_uv and
            voltage[0] <= __regulator_voltage_max_uv)

def __is_temperature_valid(temp):
    return (len(temp) == 1 and
            temp[0] >= __thermal_min_temp and
            temp[0] <= __thermal_max_temp)

def __is_clk_id_valid(clk_id):
    return len(clk_id) == 1 and clk_id[0] in __machine.clocks

def __is_rail_id_valid(rail_id):
    return len(rail_id) == 1 and rail_id[0] in __machine.rails

def __check_root(node):
    # BPMPDTB-ROOT-1
    __check_props(node)
    # BPMPDTB-ROOT-2
    __check_children(node, required=['sku'], optional=['.+'])

    has_external_memory = 'external-memory' in node.children
    has_emc_strap = 'emc-strap' in node.children

    # BPMPDTB-ROOT-3
    assert not (has_external_memory and has_emc_strap), \
        "Root node must not have both 'emc-strap' and 'external-memory' " \
        "child nodes"

def __check_adc(node):
    # BPMPDTB-ADC-1
    __check_props(node)

def __check_adc_node(node):
    # BPMPDTB-ADC-2
    __check_props(node, required=['adc-id'], optional=['vmon', 'cal'])
    # BPMPDTB-ADC-3
    __check_children(node)

    # BPMPDTB-ADC-4
    __check_u32(node, 'adc-id')

    # BPMPDTB-ADC-5
    __check_u32(node, 'vmon', func=lambda x: len(x) == 4, required=False)
    # BPMPDTB-ADC-6
    __check_u32(node, 'cal', func=lambda x: len(x) == 3, required=False)

def __check_aotag(node):
    # BPMPDTB-AOTAG-1
    __check_props(node,
            optional=['thermtrip', 'hsm_trip', 'enable_quad_bias'])
    # BPMPDTB-AOTAG-2
    __check_children(node)

    # BPMPDTB-AOTAG-3
    __check_s32(node, 'thermtrip', required=False)
    # BPMPDTB-AOTAG-4
    __check_s32(node, 'hsm_trip', required=False)
    # BPMPDTB-AOTAG-5
    __check_u32(node, 'enable_quad_bias', required=False)

def __check_avfs(node):
    # BPMPDTB-AVFS-1
    __check_props(node, optional=['clvc-period'])
    # BPMPDTB-AVFS-2
    __check_children(node, optional=['clock@.+', 'tuning'])

    # BPMPDTB-AVFS-3
    __check_u32(node, 'clvc-period', required=False)

def __check_avfs_clock(node):
    # BPMPDTB-AVFS-4
    __check_props(node,
            required=['clk-id', 'pdiv', 'mdiv', 'temp-ranges', 'vfgain'],
            optional=['vmin', 'coeffs', 'use-auto-skipper', 'max-frequency',
                'min-frequency', 'vcap-at-fmax', 'static-vfgain',
                'allow-auto-cc3', 'freq-list', 'vrev-off', 'vmin-off'])
    # BPMPDTB-AVFS-5
    __check_children(node, optional=['bin@.+', 'clvc', 'tuning'])

    # BPMPDTB-AVFS-6
    __check_u32(node, 'clk-id')
    # BPMPDTB-AVFS-7
    __check_u32(node, 'pdiv', func=lambda x:
                __check_one(x, lambda v: v in range(1, 32)))
    # BPMPDTB-AVFS-8
    __check_u32(node, 'mdiv', func=lambda x:
                __check_one(x, lambda v: v in range(1, 32)))

    # BPMPDTB-AVFS-9
    phandle = __check_u32(node, 'temp-ranges')
    temp_ranges = node.get_by_phandle(phandle[0])
    __check_temp_ranges(temp_ranges)

    # BPMPDTB-AVFS-10
    __check_u32(node, 'vfgain', func=lambda x:
                len(x) > 0 and len(x) % 6 == 0 and __check_avfs_vfgain(x))

    # BPMPDTB-AVFS-11
    __check_float(node, 'vmin', func=lambda x: len(x) == 15, required=False)
    # BPMPDTB-AVFS-12
    __check_float(node, 'coeffs', func=lambda x: len(x) == 10, required=False)
    # BPMPDTB-AVFS-13
    __check_u32(node, 'use-auto-skipper', required=False,
                func=lambda x: __check_one(x, lambda v: v in [0, 1]))
    # BPMPDTB-AVFS-14
    fmax = __check_u64(node, 'max-frequency', required=False,
                       func=lambda x:
                       __check_one(x, lambda v: v <= 10000000000))
    # BPMPDTB-AVFS-15
    fmin = __check_u64(node, 'min-frequency', required=False,
                       func=lambda x:
                       __check_one(x, lambda v: v <= 10000000000))
    # BPMPDTB-AVFS-16
    if fmin and fmax:
        assert fmax > fmin, \
            "Invalid frequency limits {}-{} at {}".format(fmin, fmax,
                                                          node.path())
    # BPMPDTB-AVFS-17
    __check_u32(node, 'vcap-at-fmax', required=False)
    # BPMPDTB-AVFS-18
    __check_u32(node, 'static-vfgain',
                func=lambda x:
                len(x) == 3 and __check_arr(x, lambda v: v in range(0, 16)),
                required=False)
    # BPMPDTB-AVFS-19
    __check_u32(node, 'allow-auto-cc3', required=False,
                func=lambda x: __check_one(x, lambda v: v in [0, 1]))
    # BPMPDTB-AVFS-20
    __check_u64(node, 'freq-list',
                func=lambda x:
                len(x) > 0 and __check_avfs_freq_list(x, fmin, fmax),
                required=False)
    # BPMPDTB-AVFS-21
    __check_s32(node, 'vrev-off', required=False,
                func=lambda x:
                __check_one(x, lambda v: v >= -1000000 and v <= 1000000))
    # BPMPDTB-AVFS-22
    __check_s32(node, 'vmin-off', required=False,
                func=lambda x:
                __check_one(x, lambda v: v >= -1000000 and v <= 1000000))

def __check_avfs_vfgain(x):
    num = len(x)
    vprev = -1
    for i in range(0, num // 6):
        for j in range(0, 5):
            if not x[i * 6 + j] in range(0, 16):
                return False
        vlim = x[i * 6 + 5]
        if vlim > 10000000 or vlim <= vprev:
            return False
        vprev = vlim
    return True

def __check_avfs_freq_list(x, fmin, fmax):
    fprev = -1
    for f in x:
        if f > 10000000000 or f <= fprev:
            return False
        fprev = f
    if fmin and not fmin[0] in x:
        return False
    if fmax and not fmax[0] in x:
        return False
    return True

def __check_avfs_clock_bin(node):
    # BPMPDTB-AVFS-23
    __check_props(node,
            required=['bin-bottom', 'coeffs'],
                  optional=['max-frequency', 'min-frequency', 'vmin'])
    # BPMPDTB-AVFS-24
    __check_children(node)

    # BPMPDTB-AVFS-25
    __check_u32(node, 'bin-bottom')
    # BPMPDTB-AVFS-26
    __check_float(node, 'coeffs', func=lambda x: len(x) == 10)

    # BPMPDTB-AVFS-27
    __check_u64(node, 'max-frequency', required=False)
    # BPMPDTB-AVFS-28
    __check_u64(node, 'min-frequency', required=False)
    # BPMPDTB-AVFS-29
    __check_float(node, 'vmin', func=lambda x: len(x) == 15, required=False)

def __check_avfs_clock_clvc(node):
    # BPMPDTB-AVFS-30
    __check_props(node,
            required=['max-uvreq-offset', 'min-error-threshold'],
            optional=['use-freq-clvc', 'use-uv-clvc', 'pgain', 'igain',
                'dgain', 'max-adj-step-uv', 'floor-uvreq', 'req-offset',
                'max-integral-error'])
    # BPMPDTB-AVFS-31
    __check_children(node)

    # BPMPDTB-AVFS-32
    __check_u32(node, 'max-uvreq-offset',
                func=lambda x: __check_one(x, lambda v: v <= 1000000))
    # BPMPDTB-AVFS-33
    __check_u32(node, 'min-error-threshold',
                func=lambda x: __check_one(x, lambda v: v <= 1000000000))

    # BPMPDTB-AVFS-34
    freq = __check_u32(node, 'use-freq-clvc', required=False)
    # BPMPDTB-AVFS-35
    uv = __check_u32(node, 'use-uv-clvc', required=False)
    # BPMPDTB-AVFS-36
    assert ((freq and freq[0] > 0 and not uv)
            or (uv and uv[0] > 0 and not freq)), \
            "Invalid use-freq-clvc/use-uv-clvc at {}".format(node.path())

    # BPMPDTB-AVFS-37
    __check_float(node, 'pgain', required=False,
                func=lambda x: __check_one(x, lambda v: v <= 1000))
    # BPMPDTB-AVFS-38
    __check_float(node, 'igain', required=False,
                func=lambda x: __check_one(x, lambda v: v <= 1000))
    # BPMPDTB-AVFS-39
    __check_float(node, 'dgain', required=False,
                func=lambda x: __check_one(x, lambda v: v <= 1000))
    # BPMPDTB-AVFS-40
    __check_u32(node, 'max-adj-step-uv', required=False,
                func=lambda x: __check_one(x, lambda v: v <= 1000000))
    # BPMPDTB-AVFS-41
    __check_u32(node, 'floor-uvreq', required=False)
    # BPMPDTB-AVFS-42
    __check_s32(node, 'req-offset', required=False,
                func=lambda x: __check_one(x, lambda v: v >= 0 and v <= 3))
    # BPMPDTB-AVFS-43
    __check_u64(node, 'max-integral-error', required=False,
                func=lambda x: __check_one(x, lambda v: v <= 2147483647))

def __check_avfs_clock_tuning(node):
    # BPMPDTB-AVFS-44
    __check_props(node,
            optional=['fll-init', 'fll-ldmem', 'fll-switch-ldmem', 'fll-ctrl',
                'pd-tune-logic', 'pd-tune-sram', 'frug-fast', 'frug-main',
                'sram-init', 'sram-threshold', 'sram-accu-num',
                'skp-ramp-rate', 'low-gain-accu-num', 'low-gain-threshold',
                'low-gain-init', 'pd-tune-lw'])
    # BPMPDTB-AVFS-45
    __check_children(node)

    # BPMPDTB-AVFS-46
    __check_u32(node, 'fll-init', required=False,
                func=lambda x: __check_one(x, lambda v: v <= 255))
    # BPMPDTB-AVFS-47
    __check_u32(node, 'fll-ldmem', required=False,
                func=lambda x: __check_one(x, lambda v: v <= 255))
    # BPMPDTB-AVFS-48
    __check_u32(node, 'fll-switch-ldmem', required=False,
                func=lambda x: __check_one(x, lambda v: v <= 31))
    # BPMPDTB-AVFS-49
    __check_u32(node, 'fll-ctrl', required=False)
    # BPMPDTB-AVFS-50
    __check_u32(node, 'pd-tune-logic', required=False,
                func=lambda x: __check_one(x, lambda v: v <= 7))
    # BPMPDTB-AVFS-51
    __check_u32(node, 'pd-tune-sram', required=False,
                func=lambda x: __check_one(x, lambda v: v <= 7))
    # BPMPDTB-AVFS-52
    __check_u32(node, 'frug-fast', required=False,
                func=lambda x: __check_one(x, lambda v: v <= 15))
    # BPMPDTB-AVFS-53
    __check_u32(node, 'frug-main', required=False,
                func=lambda x: __check_one(x, lambda v: v <= 15))
    # BPMPDTB-AVFS-54
    __check_u32(node, 'sram-init', required=False,
                func=lambda x: __check_one(x, lambda v: v <= 15))
    # BPMPDTB-AVFS-55
    __check_u32(node, 'sram-threshold', required=False,
                func=lambda x: __check_one(x, lambda v: v <= 15))
    # BPMPDTB-AVFS-56
    __check_u32(node, 'sram-accu-num', required=False,
                func=lambda x: __check_one(x, lambda v: v <= 15))
    # BPMPDTB-AVFS-57
    __check_u32(node, 'skp-ramp-rate', required=False,
                func=lambda x: __check_one(x, lambda v: v <= 31))
    # BPMPDTB-AVFS-58
    __check_u32(node, 'low-gain-accu-num', required=False,
                func=lambda x: __check_one(x, lambda v: v <= 15))
    # BPMPDTB-AVFS-59
    __check_u32(node, 'low-gain-threshold', required=False,
                func=lambda x: __check_one(x, lambda v: v <= 15))
    # BPMPDTB-AVFS-60
    __check_u32(node, 'low-gain-init', required=False,
                func=lambda x: __check_one(x, lambda v: v <= 15))
    # BPMPDTB-AVFS-61
    __check_u32(node, 'pd-tune-lw', required=False,
                func=lambda x: __check_one(x, lambda v: v <= 63))

def __check_avfs_tuning(node):
    # BPMPDTB-AVFS-62
    __check_props(node, optional=['ndiv-req-off'])
    # BPMPDTB-AVFS-63
    __check_children(node)

    # BPMPDTB-AVFS-64
    __check_u32(node, 'ndiv-req-off', required=False,
                func=lambda x: __check_one(x, lambda v: v <= 3))

def __check_clocks_t194(node):
    # BPMPDTB-CLOCKS-1
    __check_props(node, optional=['disable-unreferenced', 'default-masters'])
    # BPMPDTB-CLOCKS-2
    __check_children(node, optional=['clock@.+', 'init', 'lateinit'])

    # BPMPDTB-CLOCKS-3
    __check_u32(node, 'disable-unreferenced',
                func=lambda x: len(x) == 1 and x[0] in range(2),
                required=False)

    # BPMPDTB-CLOCKS-4
    __check_u32(node, 'default-masters',
                func=lambda x: len(x) == 1 and (x[0] & __machine.doorbells) == x[0],
                required=False)

def __check_clocks_init(node):
    # BPMPDTB-CLOCKS-5
    __check_children(node)

    path = node.path()
    msg = "Node '{}' property '{}' has invalid {}"

    # BPMPDTB-CLOCKS-6
    for name in node.props:
        pro = __check_u32(node, name, func=lambda x: len(x) == 4)

        clk = pro[0]
        parent = pro[1]
        rate = pro[2]
        ena = pro[3]

        assert __is_clk_id_valid((clk,)), msg.format(path, name, 'clk')
        assert not parent or parent in __machine.parent_clocks[clk], msg.format(path, name, 'parent')
        assert __is_frequency_valid((rate,)), msg.format(path, name, 'rate')
        assert ena & __machine.enablers == ena, msg.format(path, name, 'enable')

def __check_clocks_clock_t194(node):
    # BPMPDTB-CLOCKS-7
    __check_props(node,
            required=['clk-id'],
            optional=['allowed-parents', 'masters', 'mrq_rate_locked',
                'mrq_hide', 'pll_freq_table', 'allow_fractional_divider',
                'max-rate.*'])
    # BPMPDTB-CLOCKS-8
    __check_children(node)

    # BPMPDTB-CLOCKS-9
    clk = __check_u32(node, 'clk-id', func=__is_clk_id_valid)[0]

    # BPMPDTB-CLOCKS-10
    __check_u32(node, 'allowed-parents',
                func=lambda x: len(x) > 0 and all(j in __machine.parent_clocks[clk] for j in x),
                required=False)

    # BPMPDTB-CLOCKS-11
    __check_u32(node, 'masters',
                func=lambda x: len(x) == 1 and (x[0] & __machine.doorbells) == x[0],
                required=False)

    # BPMPDTB-CLOCKS-12
    __check_u32(node, 'mrq_rate_locked',
                func=lambda x: len(x) == 1 and x[0] in range(2),
                required=False)

    # BPMPDTB-CLOCKS-13
    __check_u32(node, 'mrq_hide',
                func=lambda x: len(x) == 1 and x[0] in range(2),
                required=False)

    # BPMPDTB-CLOCKS-14
    __check_u32(node, 'pll_freq_table',
                func=lambda x: len(x) > 0 and len(x) % 9 == 0 and clk in __machine.plls,
                required=False)

    # BPMPDTB-CLOCKS-15
    __check_u32(node, 'allow_fractional_divider',
                func=lambda x: len(x) == 1 and x[0] in range(2) and clk in __machine.frac_clocks,
                required=False)

    # BPMPDTB-CLOCKS-16
    for name in node.props:
        if name.startswith('max-rate'):
            __check_u64(node, name, func=__is_frequency_valid)

def __check_clocks(node):
    # BPMPDTB-CLOCKS-17
    __check_props(node, optional=['jtag-oist-control', 'default-acl', 'keep-unreferenced'])

    # BPMPDTB-CLOCKS-18
    __check_children(node, optional=['clock@.+', 'init', 'lateinit'])

    # BPMPDTB-CLOCKS-19
    __check_u32(node, 'jtag-oist-control',
                func=lambda x: len(x) == 1 and x[0] in range(2),
                required=False)

    # BPMPDTB-CLOCKS-20
    __check_u32(node, 'default-acl',
                func=lambda x: len(x) == 1 and (x[0] & __machine.doorbells) == x[0],
                required=False)

    # BPMPDTB-CLOCKS-21
    __check_u32(node, 'keep-unreferenced',
                func=lambda x: len(x) == 1 and x[0] in range(2),
                required=False)

def __check_clocks_clock(node):
    # BPMPDTB-CLOCKS-22
    __check_props(node,
            required=['clk-id'],
            optional=['acl', 'disable-spread', 'max-rate.*'])

    # BPMPDTB-CLOCKS-23
    __check_children(node)

    # BPMPDTB-CLOCKS-24
    clk = __check_u32(node, 'clk-id', func=__is_clk_id_valid)[0]

    # BPMPDTB-CLOCKS-25
    __check_u32(node, 'acl',
                func=lambda x: len(x) == 1 and (x[0] & __machine.doorbells) == x[0],
                required=False)

    # BPMPDTB-CLOCKS-26
    __check_u32(node, 'disable-spread',
                func=lambda x: len(x) == 1 and x[0] in range(2) and clk in __machine.plls,
                required=False)

    # BPMPDTB-CLOCKS-27
    for name in node.props:
        if name.startswith('max-rate'):
            __check_u64(node, name, func=__is_frequency_valid)

def __check_diagnostics(node):
    # BPMPDTB-DIAGNOSTICS-1
    __check_props(node, optional=['level'])
    # BPMPDTB-DIAGNOSTICS-2
    __check_children(node)

    # BPMPDTB-DIAGNOSTICS-3
    __check_u32(node, 'level', required=False)

def __check_dvfs_revision(node):
    # BPMPDTB-DVFS_REVISION-1
    __check_children(node)

    # BPMPDTB-DVFS_REVISION-2
    for name in node.props:
        __check_u32(node, name)

def __check_dvs(node):
    # BPMPDTB-DVS-1
    __check_props(node)
    # BPMPDTB-DVS-2
    __check_children(node, optional=['clock@.+', 'rail@.+'])

def __check_dvs_clock(node):
    # BPMPDTB-DVS-3
    __check_props(node, required=['clk-id'])
    # BPMPDTB-DVS-4
    __check_children(node, optional=['rail@.+'])

    # BPMPDTB-DVS-5
    __check_u32(node, 'clk-id',
                func=lambda x: len(x) == 1 and x[0] in __machine.clocks)

def __check_dvs_clock_rail(node):
    path = node.path()

    # BPMPDTB-DVS-6
    __check_props(node,
            required=['rail-id'],
            optional=['temp-ranges', 'vmin'])
    # BPMPDTB-DVS-7
    __check_children(node, optional=['opp@.+', 'bin@.+'])

    # BPMPDTB-DVS-8
    __check_u32(node, 'rail-id', func=__is_rail_id_valid)

    # BPMPDTB-DVS-9
    phandle = __check_u32(node, 'temp-ranges', required=False)
    has_temp_ranges = len(phandle) > 0

    if has_temp_ranges:
        temp_ranges = node.get_by_phandle(phandle[0])
        __check_temp_ranges(temp_ranges)

    # BPMPDTB-DVS-10
    __check_u32(node, 'vmin', func=__is_voltage_valid, required=False)

    opps = [y for x, y in node.children.items() if x.startswith('opp@')]
    bins = [y for x, y in node.children.items() if x.startswith('bin@')]

    has_opp = len(opps) > 0
    has_bin = len(bins) > 0

    # BPMPDTB-DVS-11
    assert has_opp != has_bin, \
            "Node '{}' must have either opp or bin child node".format(path)

def __check_dvs_clock_rail_opp(node):
    # BPMPDTB-DVS-12
    __check_props(node, required=['freq', 'cvb-coeffs'])
    # BPMPDTB-DVS-13
    __check_children(node)

    rail = node.parent
    phandle = __check_u32(rail, 'temp-ranges', required=False)
    has_temp_ranges = len(phandle) > 0

    if has_temp_ranges:
        cvb_coeffs_len = 15
    else:
        cvb_coeffs_len = 3

    # BPMPDTB-DVS-14
    __check_u64(node, 'freq', func=__is_frequency_valid)
    # BPMPDTB-DVS-15
    __check_float(node, 'cvb-coeffs', func=lambda x: len(x) == cvb_coeffs_len)

def __check_dvs_clock_rail_bin(node):
    path = node.path()

    # BPMPDTB-DVS-16
    __check_props(node,
            required=['bin-bottom'],
            optional=['coeffs', 'max-frequency', 'vmin'])
    # BPMPDTB-DVS-17
    __check_children(node, optional=['opp@.+'])

    # BPMPDTB-DVS-18
    __check_u32(node, 'bin-bottom')

    # BPMPDTB-DVS-19
    __check_float(node, 'coeffs',
                  func=lambda x: len(x) in (3, 10),
                  required=False)

    has_opp = any(x.startswith('opp@') for x in node.children)
    has_coeffs = 'coeffs' in node.props

    # BPMPDTB-DVS-20
    msg = "Node '{}' must have either opp child node or coeffs property"
    assert has_opp != has_coeffs, msg.format(path)

    if has_coeffs:
        # BPMPDTB-DVS-21
        __check_u64(node, 'max-frequency',
                    func=__is_frequency_valid,
                    required=False)
        # BPMPDTB-DVS-22
        __check_u32(node, 'vmin', func=__is_voltage_valid, required=False)
    else:
        # BPMPDTB-DVS-23
        msg = ("Node '{}' must have coeffs property with "
              "max-frequency and vmin properties")
        assert ('max-frequency' not in node.props and
                'vmin' not in node.props), msg.format(path)

def __check_dvs_clock_rail_bin_opp(node):
    # BPMPDTB-DVS-24
    __check_props(node, required=['freq', 'cvb-coeffs'])
    # BPMPDTB-DVS-25
    __check_children(node)

    rail = node.parent.parent
    phandle = __check_u32(rail, 'temp-ranges', required=False)
    has_temp_ranges = len(phandle) > 0

    if has_temp_ranges:
        cvb_coeffs_len = 15
    else:
        cvb_coeffs_len = 3

    # BPMPDTB-DVS-26
    __check_u64(node, 'freq', func=__is_frequency_valid)
    # BPMPDTB-DVS-27
    __check_float(node, 'cvb-coeffs', func=lambda x: len(x) == cvb_coeffs_len)

def __check_dvs_rail(node):
    # BPMPDTB-DVS-28
    __check_props(node,
            required=['rail-id'],
            optional=['vmin', 'temp-ranges'])
    # BPMPDTB-DVS-29
    __check_children(node, optional=['bin@.+'])

    # BPMPDTB-DVS-30
    __check_u32(node, 'rail-id', func=__is_rail_id_valid)

    # BPMPDTB-DVS-31
    phandle = __check_u32(node, 'temp-ranges', required=False)
    has_temp_ranges = len(phandle) > 0

    if has_temp_ranges:
        temp_ranges = node.get_by_phandle(phandle[0])
        __check_temp_ranges(temp_ranges)
        vmin_len = 5
    else:
        vmin_len = 1

    # BPMPDTB-DVS-32
    __check_u32(node, 'vmin',
            func=lambda x: (len(x) == vmin_len and
                            all(__is_voltage_valid((y,)) for y in x)),
            required=False)

def __check_dvs_rail_bin(node):
    __check_props(node, required=['bin-bottom'], optional=['vmin'])
    __check_children(node)

    rail = node.parent
    phandle = __check_u32(rail, 'temp-ranges', required=False)
    has_temp_ranges = len(phandle) > 0

    if has_temp_ranges:
        vmin_len = 5
    else:
        vmin_len = 1

    __check_u32(node, 'bin-bottom')
    __check_u32(node, 'vmin',
                func=lambda x: (len(x) == vmin_len and
                                all(__is_voltage_valid((y,)) for y in x)),
                required=False)

def __check_ec(node):
    # BPMPDTB-EC-1
    __check_props(node,
            optional=['ftti-budget', 'nsr-car-applied', 'default-masters'])

    # BPMPDTB-EC-2
    __check_u32(node, 'ftti-budget', required=False)
    # BPMPDTB-EC-3
    __check_u32(node, 'nsr-car-applied',
                func=lambda x: len(x) == 0,
                required=False)
    # BPMPDTB-EC-4
    __check_u32(node, 'default-masters', required=False)

def __check_ec_node(node):
    # BPMPDTB-EC-5
    __check_props(node, required=['hsm-id'], optional=['threshold', 'policy'])
    # BPMPDTB-EC-6
    __check_children(node)

    # BPMPDTB-EC-7
    __check_u32(node, 'hsm-id')

    # BPMPDTB-EC-8
    __check_u32(node, 'threshold', required=False)
    # BPMPDTB-EC-9
    __check_u32(node, 'policy',
                func=lambda x: len(x) > 0 and len(x) % 3 == 0,
                required=False)

def __check_select_first_compatible(node, x):
    num = len(x) // 2
    for i in range(0, num):
        code = x[i * 2]
        if code >= 16:
            return False
        phandle = x[i * 2 + 1]
        emc = node.get_by_phandle(phandle)
        __check_emc(emc)
    return True

def __check_select(node, x):
    for phandle in x:
        if phandle != 0: # some DTs..
            emc = node.get_by_phandle(phandle)
            __check_emc(emc)
    return True

# The top level 'compatible' for t194 was supposed to be nvidia,tegra19-emc
# the edt.py generates tables with nvidia,t19x-emc-table
# The compatible property for per frequency nodes is supposed to be
# 'nvidia,tegra??-emc-table' or 'nvidia,tegraXX-emc-table-derated'
# The name of the blob property is supposed to be 'nvidia,t??x-emc-table'
def __check_emc_blob(name, node):
    cbase = name
    bname = name
    if name == 'nvidia,tegra210b01-emc':
        cbase = name
        bname = 'nvidia,t210b01-emc-table'
    if name == 'nvidia,t19x-emc-table':
        cbase = 'nvidia,tegra19-emc'
        bname = name
    __check_props(node, required=['compatible', bname])
    c = __check_u8(node, 'compatible', func=lambda x: len(x) > 0)
    s = ''.join([chr(v) for v in c])
    regular = cbase + '-table\0'
    derated = cbase + '-table-derated\0'
    assert s in [regular, derated], \
        "Invalid 'compatible' at {}".format(node.path())
    if bname == 'nvidia,t19x-emc-table':
        __check_u8(node, bname, func=lambda x: len(x) >= 13404)

def __check_emc(node):
    __check_props(node, required=['compatible'], optional=['.+'])
    c = __check_u8(node, 'compatible', func=lambda x: len(x) > 0)
    s = ''.join([chr(v) for v in c])
    assert s in ['nvidia,tegra18-emc\0',
                 'nvidia,tegra210b01-emc\0',
                 'nvidia,t19x-emc-table\0'], \
                 "Invalid compatible {} at {}".format(s, node.path())
    s = s.rstrip('\0')
    if s in ['nvidia,tegra210b01-emc', 'nvidia,t19x-emc-table']:
        assert len(node.children) > 0, \
            "Subnodes missing at {}".format(node.path())
        for child in node.children.values():
            __check_emc_blob(s, child)
    __check_u8(node, 'basic-configuration', required=False,
               func=lambda x: len(x) == 4 or len(x) == 8)
    __check_u32(node, 'mr4_poll_temp', required=False,
                func=lambda x: len(x) == 2)
    __check_u32(node, 'mr4_policy', required=False,
                func=lambda x:
                len(x) == 8 and __check_arr(x, lambda v: v in [1, 2, 4, 5, 6]))

def __check_emc_strap(node):
    __check_props(node, optional=['select-first-compatible', 'select'])
    __check_children(node)

    c = __check_u32(node, 'select-first-compatible',
                    func=lambda x:
                    len(x) > 0 and len(x) % 2 == 0
                    and __check_select_first_compatible(node, x),
                    required=False)
    s = __check_u32(node, 'select',
                    func=lambda x: len(x) in (4, 16)
                    and __check_select(node, x),
                    required=False)
    assert (c and not s) or (s and not c), \
        "Either 'select-first-compatible or 'select' must be specified"

def __check_external_memory(node):
    __check_emc(node)

def __check_fmon(node):
    # BPMPDTB-FMON-1
    __check_props(node, optional=['default-masters'])
    # BPMPDTB-FMON-2
    __check_children(node, optional=['fmon@.+', 'vrefro'])

    # BPMPDTB-FMON-3
    __check_u32(node, 'default-masters', required=False)

def __check_fmon_fmon(node):
    # BPMPDTB-FMON-4
    __check_props(node,
            required=['clk-id'],
            optional=['ht-coeffs', 'lt-coeffs', 'rw-coeffs', 'fault-reports',
                'fault-actions', 'min-rate', 'max-rate'])
    # BPMPDTB-FMON-5
    __check_children(node)

    # BPMPDTB-FMON-6
    __check_u32(node, 'clk-id')

    # BPMPDTB-FMON-7
    __check_float(node, 'ht-coeffs',
                  func=lambda x: len(x) == 3,
                  required=False)
    # BPMPDTB-FMON-8
    __check_float(node, 'lt-coeffs',
                  func=lambda x: len(x) == 3,
                  required=False)
    # BPMPDTB-FMON-9
    __check_float(node, 'rw-coeffs',
                  func=lambda x: len(x) == 3,
                  required=False)
    # BPMPDTB-FMON-10
    __check_u32(node, 'fault-reports', required=False)
    # BPMPDTB-FMON-11
    __check_u32(node, 'fault-actions', required=False)
    # BPMPDTB-FMON-12
    __check_u64(node, 'min-rate', required=False)
    # BPMPDTB-FMON-13
    __check_u64(node, 'max-rate', required=False)

def __check_fmon_vrefro(node):
    # BPMPDTB-FMON-14
    __check_props(node, optional=['freq-adjust', 'rev-limit'])
    # BPMPDTB-FMON-15
    __check_children(node)

    # BPMPDTB-FMON-16
    __check_u32(node, 'freq-adjust', required=False)
    # BPMPDTB-FMON-17
    __check_u32(node, 'rev-limit', required=False)

def __check_fps(node):
    # BPMPDTB-FPS-1
    __check_props(node,
            optional=['sc7-sequence', 'sc8-sequence',
                'hibernate-late-sequence', 'resume-early-sequence',
                'sc7-exit-sequence', 'reboot-sequence', 'shutdown-sequence'])
    # BPMPDTB-FPS-2
    __check_children(node)

    # BPMPDTB-FPS-3
    for name in node.props:
        __check_u8(node, name, func=lambda x: len(x) > 0)

def __check_ftrace(node):
    # BPMPDTB-FTRACE-1
    __check_props(node, optional=['enable'])
    # BPMPDTB-FTRACE-2
    __check_children(node)

    # BPMPDTB-FTRACE-3
    __check_u32(node, 'enable', required=False)

def __check_fuse(node):
    # BPMPDTB-FUSE-1
    __check_props(node, optional=['sm-ecid', 'tsensor_coeff_sel'])
    # BPMPDTB-FUSE-2
    __check_children(node)

    # BPMPDTB-FUSE-3
    # Null terminated string of 25 hexadecimal characters
    __check_u8(node, 'sm-ecid', func=lambda x: len(x) == 26, required=False)
    # BPMPDTB-FUSE-4
    __check_u32(node, 'tsensor_coeff_sel', required=False)

def __i2c_bus_is_i2c5(node):
    return __check_u32(node, 'i2c-id')[0] == 5

def __check_i2c_busses(node):
    path = node.path()

    # BPMPDTB-I2C_BUSSES-1
    __check_props(node)

    # BPMPDTB-I2C_BUSSES-2
    assert len(node.children) > 0, "Node '{}' is empty".format(path)
    # BPMPDTB-I2C_BUSSES-3
    assert any(__i2c_bus_is_i2c5(x) for x in node.children.values()), \
        "Node '{}' is missing required I2C5 child node".format(path)

def __check_i2c_busses_node(node):
    # BPMPDTB-I2C_BUSSES-4
    __check_props(node,
            required=['i2c-id', 'controller-clock', 'bus-clock'],
            optional=['tlow', 'thigh'])
    # BPMPDTB-I2C_BUSSES-5
    __check_children(node, optional=['i2c_firewall_rules'])

    # BPMPDTB-I2C_BUSSES-6
    # i2c ids [1,10] are valid
    __check_u32(node, 'i2c-id',
                func=lambda x: len(x) == 1 and x[0] in range(1, 11))

    # BPMPDTB-I2C_BUSSES-7
    # Controller clock rate is at least 38.4 MHz for FM+
    __check_u32(node, 'controller-clock',
                func=lambda x: len(x) == 1 and x[0] >= 38400000)

    # BPMPDTB-I2C_BUSSES-8
    # Bus clock is at least 100 kHz, at most 1 MHz
    __check_u32(node, 'bus-clock',
                func=lambda x: (len(x) == 1 and
                                x[0] in range(10**5, 10**6 + 1)))

    # BPMPDTB-I2C_BUSSES-9
    # Heuristic check for tlow and thigh in [1,16]
    __check_u32(node, 'tlow',
                func=lambda x: len(x) == 1 and x[0] in range(1, 17),
                required=False)
    # BPMPDTB-I2C_BUSSES-10
    __check_u32(node, 'thigh',
                func=lambda x: len(x) == 1 and x[0] in range(1, 17),
                required=False)

def __check_i2c_fw_rules(node):
    # BPMPDTB-I2C_BUSSES-11
    __check_props(node)

def __check_i2c_fw_rules_node_t194(node):
    # BPMPDTB-I2C_BUSSES-12
    __check_props(node,
            required=['addr_range_low', 'addr_range_high'],
            optional=['action', 'addr_range_type'])
    # BPMPDTB-I2C_BUSSES-13
    __check_children(node)

    # BPMPDTB-I2C_BUSSES-14
    __check_u32(node, 'addr_range_low')
    # BPMPDTB-I2C_BUSSES-15
    __check_u32(node, 'addr_range_high')

    # BPMPDTB-I2C_BUSSES-16
    __check_u32(node, 'action', required=False)
    # BPMPDTB-I2C_BUSSES-17
    __check_u32(node, 'addr_range_type', required=False)

def __check_i2c_fw_rules_node_t23x(node):
    # BPMPDTB-I2C_BUSSES-12
    __check_props(node,
            required=['addr_range_low', 'addr_range_high'],
            optional=['action', 'addr_range_type', 'allow_reads'])
    # BPMPDTB-I2C_BUSSES-13
    __check_children(node)

    # BPMPDTB-I2C_BUSSES-14
    __check_u32(node, 'addr_range_low')
    # BPMPDTB-I2C_BUSSES-15
    __check_u32(node, 'addr_range_high')

    # BPMPDTB-I2C_BUSSES-16
    __check_u32(node, 'action', required=False)
    # BPMPDTB-I2C_BUSSES-17
    __check_u32(node, 'addr_range_type', required=False)
    # BPMPDTB-I2C_BUSSES-18
    __check_u32(node, 'allow_reads', required=False)

def __check_mail(node):
    # BPMPDTB-MAIL-1
    __check_props(node, required=['edition'], optional=['dbs'])
    # BPMPDTB-MAIL-2
    __check_children(node, optional=['acl'])

    # BPMPDTB-MAIL-3
    __check_u32(node, 'edition')

    # BPMPDTB-MAIL-4
    __check_u32(node, 'dbs', required=False)

def __check_mail_acl(node):
    pass

def __check_pdomains(node):
    # BPMPDTB-PDOMAINS-1
    # Allow no-powergate-on-boot to workaround bug 2649591
    __check_props(node, optional=['no-powergate-on-boot'])
    # BPMPDTB-PDOMAINS-2
    __check_children(node, optional=['domain@.+'])

    # BPMPDTB-PDOMAINS-3
    __check_u32(node, 'no-powergate-on-boot', required=False)

def __check_pdomains_domain(node):
    # BPMPDTB-PDOMAINS-4
    __check_props(node,
            required=['id'],
            optional=['always-off', 'always-on', 'initial', 'target'])
    # BPMPDTB-PDOMAINS-5
    __check_children(node)

    # BPMPDTB-PDOMAINS-6
    __check_u32(node, 'id')

    # BPMPDTB-PDOMAINS-7
    __check_u32(node, 'always-off', required=False)
    # BPMPDTB-PDOMAINS-8
    __check_u32(node, 'always-on', required=False)
    # BPMPDTB-PDOMAINS-9
    __check_u32(node, 'initial', required=False)
    # BPMPDTB-PDOMAINS-10
    __check_u32(node, 'target', required=False)

def __check_regulator_dummy(node):
    # BPMPDTB-REGULATORS-17
    __check_props(node,
            required=['compatible'],
            optional=['enable-gpio', 'linux,phandle', 'phandle'])
    # BPMPDTB-REGULATORS-18
    __check_children(node)

    # BPMPDTB-REGULATORS-19
    assert node.props['compatible'] == b'dummy\x00'

    # BPMPDTB-REGULATORS-20
    __check_u32(node, 'enable-gpio', required=False)
    # BPMPDTB-REGULATORS-21
    __check_u32(node, 'linux,phandle', required=False)
    # BPMPDTB-REGULATORS-22
    __check_u32(node, 'phandle', required=False)

def __check_regulator_fixed(node):
    # BPMPDTB-REGULATORS-23
    __check_props(node,
            required=['compatible'],
            optional=['step-uv', 'enable-gpio', 'linux,phandle', 'phandle'])
    # BPMPDTB-REGULATORS-24
    __check_children(node)

    # BPMPDTB-REGULATORS-25
    assert node.props['compatible'] == b'fixed\x00'

    # BPMPDTB-REGULATORS-26
    __check_u32(node, 'step-uv', required=False)
    # BPMPDTB-REGULATORS-27
    __check_u32(node, 'enable-gpio', required=False)
    # BPMPDTB-REGULATORS-28
    __check_u32(node, 'linux,phandle', required=False)
    # BPMPDTB-REGULATORS-29
    __check_u32(node, 'phandle', required=False)

def __check_regulator_ovr(node):
    # BPMPDTB-REGULATORS-30
    __check_props(node,
            required=['compatible', 'pwm-id', 'pwm-off-uv', 'pwm-on-uv'],
            optional=['pwm-rate', 'enable-gpio', 'linux,phandle', 'phandle'])
    # BPMPDTB-REGULATORS-31
    __check_children(node)

    # BPMPDTB-REGULATORS-32
    assert node.props['compatible'] == b'openvreg\x00'
    # BPMPDTB-REGULATORS-33
    __check_u32(node, 'pwm-id')
    # BPMPDTB-REGULATORS-34
    __check_u32(node, 'pwm-off-uv')
    # BPMPDTB-REGULATORS-35
    __check_u32(node, 'pwm-on-uv')

    # BPMPDTB-REGULATORS-36
    __check_u32(node, 'pwm-rate', required=False)
    # BPMPDTB-REGULATORS-37
    __check_u32(node, 'enable-gpio', required=False)
    # BPMPDTB-REGULATORS-38
    __check_u32(node, 'linux,phandle', required=False)
    # BPMPDTB-REGULATORS-39
    __check_u32(node, 'phandle', required=False)

def __check_regulator_vrs11(node):
    # BPMPDTB-REGULATORS-40
    __check_props(node,
            required=['compatible', 'pwm-id', 'slave-address', 'channel'],
            optional=['pwm-rate', 'enable-gpio', 'linux,phandle', 'phandle'])
    # BPMPDTB-REGULATORS-41
    __check_children(node)

    # BPMPDTB-REGULATORS-42
    assert node.props['compatible'] == b'vrs-11\x00' or node.props['compatible'] == b'vrs-11-quirks\x00'
    # BPMPDTB-REGULATORS-43
    __check_u32(node, 'pwm-id')

    # BPMPDTB-REGULATORS-44
    __check_u32(node, 'slave-address')

    # BPMPDTB-REGULATORS-45
    __check_u32(node, 'channel')

    # BPMPDTB-REGULATORS-46
    __check_u32(node, 'pwm-rate', required=False)
    # BPMPDTB-REGULATORS-47
    __check_u32(node, 'enable-gpio', required=False)
    # BPMPDTB-REGULATORS-48
    __check_u32(node, 'linux,phandle', required=False)
    # BPMPDTB-REGULATORS-49
    __check_u32(node, 'phandle', required=False)


def __check_regulator_dev(node):
    regulator_devs = {
            b'dummy\x00': __check_regulator_dummy,
            b'fixed\x00': __check_regulator_fixed,
            b'openvreg\x00': __check_regulator_ovr,
            b'vrs-11\x00': __check_regulator_vrs11,
            b'vrs-11-quirks\x00': __check_regulator_vrs11,
            }

    # BPMPDTB-REGULATORS-14
    assert 'compatible' in node.props, \
            "Invalid regulator device '{}'".format(node.path())

    # BPMPDTB-REGULATORS-15
    compatible = node.props['compatible']
    assert compatible in regulator_devs, \
            "Unknown regulator device '{}'".format(node.path())

    # BPMPDTB-REGULATORS-16
    regulator_devs[compatible](node)

def __check_regulators(node):
    # BPMPDTB-REGULATORS-1
    __check_props(node)

def __check_regulators_node(node):
    # BPMPDTB-REGULATORS-2
    __check_props(node,
            required=['rail-id', 'dev'],
            optional=['regulator-init-microvolt', 'regulator-min-microvolt',
                'regulator-max-microvolt', 'regulator-ramp-delay-const',
                'regulator-ramp-delay-linear', 'regulator-enable-ramp-delay',
                'regulator-disable-ramp-delay',
                'regulator-pre-disable-ramp-delay'])
    # BPMPDTB-REGULATORS-3
    __check_children(node)

    # BPMPDTB-REGULATORS-4
    __check_u32(node, 'rail-id', func=lambda x: len(x) > 0)

    # BPMPDTB-REGULATORS-5
    phandle = __check_u32(node, 'dev')
    dev = node.get_by_phandle(phandle[0])
    __check_regulator_dev(dev)

    # BPMPDTB-REGULATORS-6
    __check_u32(node, 'regulator-init-microvolt', required=False)
    # BPMPDTB-REGULATORS-7
    __check_u32(node, 'regulator-min-microvolt', required=False)
    # BPMPDTB-REGULATORS-8
    __check_u32(node, 'regulator-max-microvolt', required=False)
    # BPMPDTB-REGULATORS-9
    ramp_delay_const = __check_u32(node, 'regulator-ramp-delay-const',
                                   required=False)
    # BPMPDTB-REGULATORS-10
    ramp_delay_linear = __check_u32(node, 'regulator-ramp-delay-linear',
                                    required=False)
    # BPMPDTB-REGULATORS-11
    __check_u32(node, 'regulator-enable-ramp-delay', required=False)
    # BPMPDTB-REGULATORS-12
    __check_u32(node, 'regulator-disable-ramp-delay', required=False)
    # BPMPDTB-REGULATORS-13
    __check_u32(node, 'regulator-pre-disable-ramp-delay', required=False)

    total_ramp_delay = 0
    if len(ramp_delay_const) > 0:
        total_ramp_delay += ramp_delay_const[0]

    if len(ramp_delay_linear) > 0:
        total_ramp_delay += 300000 // ramp_delay_linear[0]

    # BPMPDTB-REGULATORS-40
    assert total_ramp_delay <= 10000, \
        "Total voltage ramp delay exceeds 10ms budget " \
        "({}us > 10000us):".format(total_ramp_delay)

def __check_reset(node):
    # BPMPDTB-RESET-1
    __check_props(node, optional=['default-masters'])
    # BPMPDTB-RESET-2
    __check_children(node, optional=['reset@.+'])

    # BPMPDTB-RESET-3
    __check_u32(node, 'default-masters', required=False)

def __check_reset_reset(node):
    # BPMPDTB-RESET-4
    __check_props(node, required=['reset-id'], optional=['masters'])
    # BPMPDTB-RESET-5
    __check_children(node)

    # BPMPDTB-RESET-6
    __check_u32(node, 'reset-id')

    # BPMPDTB-RESET-7
    __check_u32(node, 'masters', required=False)

def __check_rm(node):
    # BPMPDTB-RELIABILITY_MANAGEMENT-1
    __check_props(node)

def __check_rm_node(node):
    # BPMPDTB-RELIABILITY_MANAGEMENT-2
    __check_props(node,
            optional=['rail-id', 'unicap', 'voltage-caps', 'thermzone-id',
                'temp-offset', 'temp-offset-milli'])
    # BPMPDTB-RELIABILITY_MANAGEMENT-3
    __check_children(node)

    # BPMPDTB-RELIABILITY_MANAGEMENT-4
    __check_u32(node, 'rail-id', required=False)
    # BPMPDTB-RELIABILITY_MANAGEMENT-5
    __check_u32(node, 'unicap', required=False)
    # BPMPDTB-RELIABILITY_MANAGEMENT-6
    __check_u32(node, 'voltage-caps',
                func=lambda x: len(x) > 0 and len(x) % 3 == 0,
                required=False)
    # BPMPDTB-RELIABILITY_MANAGEMENT-7
    __check_u32(node, 'thermzone-id', required=False)
    # BPMPDTB-RELIABILITY_MANAGEMENT-8
    __check_u32(node, 'temp-offset', required=False)
    # BPMPDTB-RELIABILITY_MANAGEMENT-9
    __check_u32(node, 'temp-offset-milli', required=False)

def __check_serial(node):
    # BPMPDTB-SERIAL-1
    __check_props(node, optional=['port', 'log-level', 'has_input'])
    # BPMPDTB-SERIAL-2
    __check_children(node, optional=['combined-uart'])

    # BPMPDTB-SERIAL-3
    __check_u32(node, 'port', required=False)
    # BPMPDTB-SERIAL-4
    __check_u32(node, 'log-level', required=False)
    # BPMPDTB-SERIAL-5
    __check_u32(node, 'has_input', func=lambda x: len(x) == 0, required=False)

def __check_serial_combined_uart(node):
    # BPMPDTB-SERIAL-6
    __check_props(node, optional=['enabled'])
    # BPMPDTB-SERIAL-7
    __check_children(node)

    # BPMPDTB-SERIAL-8
    __check_u32(node, 'enabled', func=lambda x: len(x) == 0, required=False)

def __check_sku(node):
    # BPMPDTB-SKU-1
    __check_props(node)

    # BPMPDTB-SKU-2
    assert len(node.children) > 0, "Node '{}' is empty".format(node.path())

def __check_sku_node(node):
    # BPMPDTB-SKU-3
    __check_props(node, required=['valid-sku-ids'])
    # BPMPDTB-SKU-4
    __check_children(node)

    # BPMPDTB-SKU-5
    __check_u32(node, 'valid-sku-ids', func=lambda x: len(x) > 0)

def __check_soctherm(node):
    # BPMPDTB-SOCTHERM-1
    __check_props(node,
            optional=['force_fallback', 'fallback_offsets',
                'enable_quad_bias'])
    # BPMPDTB-SOCTHERM-2
    __check_children(node,
            optional=['thermtrip', 'hsmtrip', 'throttle', 'edp_oc',
            'throttlectl'])

    # BPMPDTB-SOCTHERM-3
    __check_u32(node, 'force_fallback',
                func=lambda x: len(x) > 0,
                required=False)
    # BPMPDTB-SOCTHERM-4
    __check_u32(node, 'fallback_offsets',
                func=lambda x: len(x) > 0 and len(x) % 3 == 0,
                required=False)
    # BPMPDTB-SOCTHERM-5
    __check_u32(node, 'enable_quad_bias', required=False)

def __check_soctherm_thermtrip(node):
    # BPMPDTB-SOCTHERM-6
    __check_children(node)

    # BPMPDTB-SOCTHERM-7
    for name in node.props:
        __check_s32(node, name, func=lambda x: len(x) == 2)

def __check_soctherm_hsmtrip(node):
    # BPMPDTB-SOCTHERM-8
    __check_children(node)

    # BPMPDTB-SOCTHERM-9
    for name in node.props:
        __check_s32(node, name, func=lambda x: len(x) == 2)

def __check_soctherm_throttle(node):
    # BPMPDTB-SOCTHERM-10
    __check_children(node)

    # BPMPDTB-SOCTHERM-11
    for name in node.props:
        __check_s32(node, name, func=lambda x: len(x) in (4, 7))

def __check_soctherm_edp_oc(node):
    # BPMPDTB-SOCTHERM-12
    __check_children(node)

    # BPMPDTB-SOCTHERM-13
    for name in node.props:
        __check_u32(node, name, func=lambda x: len(x) == 7)

def __check_soctherm_throttlectl(node):
    # BPMPDTB-SOCTHERM-14
    __check_children(node)

    # BPMPDTB-SOCTHERM-15
    for name in node.props:
        __check_s32(node, name, func=lambda x: len(x) == 4)

def __check_speedo(node):
    # BPMPDTB-SPEEDO-1
    __check_props(node)

def __check_speedo_node(node):
    # BPMPDTB-SPEEDO-2
    __check_props(node, required=['rail-id', 'offset'])
    # BPMPDTB-SPEEDO-3
    __check_children(node)

    # BPMPDTB-SPEEDO-4
    __check_u32(node, 'rail-id')
    # BPMPDTB-SPEEDO-5
    __check_s32(node, 'offset')

def __check_system_cfg(node):
    # BPMPDTB-SYSTEM_CFG-1
    __check_props(node, optional=['spe-enabled', 'osc-on-during-sc7',
                                    'sec-faults-rst-en'])
    # BPMPDTB-SYSTEM_CFG-2
    __check_children(node, optional=['sc7'])

    # BPMPDTB-SYSTEM_CFG-3
    __check_u32(node, 'spe-enabled', required=False)
    # BPMPDTB-SYSTEM_CFG-4
    __check_u32(node, 'osc-on-during-sc7', required=False)

def __check_system_cfg_sc7(node):
    # BPMPDTB-SYSTEM_CFG-5
    __check_props(node,
            optional=['wake-delay', 'pwrgood-timer',
                'osc-prepwr-timer', 'osc-postpwr-timer'])
    __check_children(node)

    # BPMPDTB-SYSTEM_CFG-6
    __check_u32(node, 'wake-delay', required=False)
    # BPMPDTB-SYSTEM_CFG-7
    __check_u32(node, 'pwrgood-timer', required=False)
    # BPMPDTB-SYSTEM_CFG-8
    __check_u32(node, 'osc-prepwr-timer', required=False)
    # BPMPDTB-SYSTEM_CFG-9
    __check_u32(node, 'osc-postpwr-timer', required=False)

def __check_temp_ranges(node):
    # BPMPDTB-TEMP_RANGES-1
    __check_props(node,
            required=['thermzone-id', 'tranges'],
            optional=['linux,phandle', 'phandle'])
    # BPMPDTB-TEMP_RANGES-2
    __check_children(node)

    # BPMPDTB-TEMP_RANGES-3
    __check_u32(node, 'thermzone-id')
    # BPMPDTB-TEMP_RANGES-4
    __check_s32(node, 'tranges', func=lambda x:
                len(x) == 10 and __check_temp_values(x))

    # BPMPDTB-TEMP_RANGES-5
    __check_u32(node, 'linux,phandle', required=False)
    # BPMPDTB-TEMP_RANGES-6
    __check_u32(node, 'phandle', required=False)

def __check_temp_values(x):
    if not all(__is_temperature_valid((y,)) for y in x):
        return False

    for i in range(0, 4):
        tlo = x[i * 2]
        thi = x[i * 2 + 1]
        if tlo >= thi:
            return False
        if i > 0:
            if tlo <= tlo_prev:
                return False # no increase in low limit
            if thi <= thi_prev:
                return False # no increase in high limit
            if tlo >= thi_prev:
                return False # no overlap
        tlo_prev = tlo
        thi_prev = thi
    tmin = x[0]
    tmax = x[3 * 2 + 1]
    tsafe_lo = x[4 * 2]
    tsafe_hi = x[4 * 2 + 1]
    if tsafe_lo != tmin:
        return False
    if tsafe_hi != tmax:
        return False
    return True

def __check_tj_max(node):
    # BPMPDTB-TJ_MAX-1
    __check_props(node, optional=['poll_period'])
    # BPMPDTB-TJ_MAX-2
    __check_children(node, required=['tz_list'])

    # BPMPDTB-TJ_MAX-3
    __check_u32(node, 'poll_period', required=False)

def __check_tj_max_tz_list(node):
    # BPMPDTB-TJ_MAX-4
    __check_children(node)

    # BPMPDTB-TJ_MAX-5
    for name in node.props:
        __check_s32(node, name, func=lambda x: len(x) == 2)

def __check_uphy(node):
    # BPMPDTB-UPHY-1
    __check_props(node,
            required=['pcie-xbar-config'],
            optional=['status', 'pcie-c0-endpoint-enable',
                'pcie-c0-endpoint-use-int-refclk', 'pcie-c4-endpoint-enable',
                'pcie-c4-endpoint-use-int-refclk', 'pcie-c5-endpoint-enable',
                'pcie-c5-endpoint-use-int-refclk', 'ufs-config',
                'sata-enable', 'nvhs-owner', 'force-ufs-init'])
    # BPMPDTB-UPHY-2
    __check_children(node, optional=['sata-fuse-override'])

    # BPMPDTB-UPHY-3
    __check_u8(node, 'pcie-xbar-config', func=lambda x: len(x) > 0)

    # BPMPDTB-UPHY-4
    __check_u8(node, 'status', func=lambda x: len(x) > 0, required=False)
    # BPMPDTB-UPHY-5
    __check_u32(node, 'pcie-c0-endpoint-enable',
                func=lambda x: len(x) == 0,
                required=False)
    # BPMPDTB-UPHY-6
    __check_u32(node, 'pcie-c0-endpoint-use-int-refclk',
                func=lambda x: len(x) == 0,
                required=False)
    # BPMPDTB-UPHY-7
    __check_u32(node, 'pcie-c4-endpoint-enable',
                func=lambda x: len(x) == 0,
                required=False)
    # BPMPDTB-UPHY-8
    __check_u32(node, 'pcie-c4-endpoint-use-int-refclk',
                func=lambda x: len(x) == 0,
                required=False)
    # BPMPDTB-UPHY-9
    __check_u32(node, 'pcie-c5-endpoint-enable',
                func=lambda x: len(x) == 0,
                required=False)
    # BPMPDTB-UPHY-10
    __check_u32(node, 'pcie-c5-endpoint-use-int-refclk',
                func=lambda x: len(x) == 0,
                required=False)
    # BPMPDTB-UPHY-11
    __check_u8(node, 'ufs-config', func=lambda x: len(x) > 0, required=False)
    # BPMPDTB-UPHY-12
    __check_u32(node, 'sata-enable',
                func=lambda x: len(x) == 0, required=False)
    # BPMPDTB-UPHY-13
    __check_u8(node, 'nvhs-owner', func=lambda x: len(x) > 0, required=False)
    # BPMPDTB-UPHY-14
    __check_u32(node, 'force-ufs-init',
                func=lambda x: len(x) == 0,
                required=False)

def __check_uphy_sata_fuse_override(node):
    # BPMPDTB-UPHY-15
    __check_props(node,
    optional=['tx-drv-amp-sel0', 'tx-drv-amp-sel1', 'tx-drv-post-sel0',
        'tx-drv-post-sel1'])
    # BPMPDTB-UPHY-16
    __check_children(node)

    # BPMPDTB-UPHY-17
    __check_u32(node, 'tx-drv-amp-sel0', required=False)
    # BPMPDTB-UPHY-18
    __check_u32(node, 'tx-drv-amp-sel1', required=False)
    # BPMPDTB-UPHY-19
    __check_u32(node, 'tx-drv-post-sel0', required=False)
    # BPMPDTB-UPHY-20
    __check_u32(node, 'tx-drv-post-sel1', required=False)

def __check_vrmon_max20480(node):
    # BPMPDTB-VRMON-17
    __check_props(node,
            required=['compatible', 'i2c-addr'],
            optional=['boot-config', 'linux,phandle', 'phandle'])
    # BPMPDTB-VRMON-18
    __check_children(node)

    # BPMPDTB-VRMON-19
    assert node.props['compatible'] == b'max20480\x00'
    # BPMPDTB-VRMON-20
    __check_u32(node, 'i2c-addr')

    # BPMPDTB-VRMON-21
    __check_u32(node, 'boot-config', required=False)
    # BPMPDTB-VRMON-22
    __check_u32(node, 'linux,phandle', required=False)
    # BPMPDTB-VRMON-23
    __check_u32(node, 'phandle', required=False)

def __check_vrmon_vrs12(node):
    # BPMPDTB-VRMON-24
    __check_props(node,
            required=['compatible', 'i2c-addr'],
            optional=['boot-config', 'linux,phandle', 'phandle'])
    # BPMPDTB-VRMON-25
    __check_children(node)

    # BPMPDTB-VRMON-26
    assert node.props['compatible'] == b'vrs12\x00'
    # BPMPDTB-VRMON-27
    __check_u32(node, 'i2c-addr')

    # BPMPDTB-VRMON-28
    __check_u32(node, 'boot-config', required=False)
    # BPMPDTB-VRMON-29
    __check_u32(node, 'linux,phandle', required=False)
    # BPMPDTB-VRMON-30
    __check_u32(node, 'phandle', required=False)

def __check_vrmon_dev(node):
    vrmon_devs = {
            b'max20480\x00': __check_vrmon_max20480,
            b'vrs12\x00': __check_vrmon_vrs12,
        }

    # BPMPDTB-VRMON-14
    assert 'compatible' in node.props, \
            "Invalid vrmon device '{}'".format(node.path())

    # BPMPDTB-VRMON-15
    compatible = node.props['compatible']
    assert compatible in vrmon_devs, \
            "Unknown vrmon device '{}'".format(node.path())

    # BPMPDTB-VRMON-16
    vrmon_devs[compatible](node)

def __check_vrmon(node):
    # BPMPDTB-VRMON-1
    __check_props(node)

def __check_vrmon_node(node):
    # BPMPDTB-VRMON-2
    __check_props(node,
            required=['rail-id', 'mon-dev', 'channel', 'underv-uv-min',
                'underv-uv-per-v', 'overv-uv-max', 'overv-uv-per-v'],
            optional=['underv-uv-offs', 'overv-uv-offs', 'ovshoot-uv-per-v'])
    # BPMPDTB-VRMON-3
    __check_children(node)

    # BPMPDTB-VRMON-4
    __check_u32(node, 'rail-id')

    # BPMPDTB-VRMON-5
    phandle = __check_u32(node, 'mon-dev')
    mon_dev = node.get_by_phandle(phandle[0])
    __check_vrmon_dev(mon_dev)

    # BPMPDTB-VRMON-6
    __check_u32(node, 'channel')
    # BPMPDTB-VRMON-7
    __check_u32(node, 'underv-uv-min')
    # BPMPDTB-VRMON-8
    __check_u32(node, 'underv-uv-per-v')
    # BPMPDTB-VRMON-9
    __check_u32(node, 'overv-uv-max')
    # BPMPDTB-VRMON-10
    __check_u32(node, 'overv-uv-per-v')

    # BPMPDTB-VRMON-11
    __check_u32(node, 'underv-uv-offs', required=False)
    # BPMPDTB-VRMON-12
    __check_u32(node, 'overv-uv-offs', required=False)
    # BPMPDTB-VRMON-13
    __check_u32(node, 'ovshoot-uv-per-v', required=False)

def __sign(version, chipid, majorrev, header, data):
    # Metadata contains major revision, chip ID, dtbcheck.py version and magic
    metadata = struct.pack(__metadata_fmt, majorrev, chipid,
                           version, __dtbcheck_magic)

    # Calculate lengths with metadata and signature
    unaligned_len = len(data) + __metadata_len + __signature_len
    aligned_len = _align(unaligned_len, __dtb_alignment)

    header.pad(aligned_len - len(data))

    header_data = header.pack()
    dtb_data = data[_dtb_header_len:]
    padding = b'\x00' * (aligned_len - unaligned_len)

    # Calculate CRC
    crc = zlib.crc32(header_data)
    crc = zlib.crc32(dtb_data, crc)
    crc = zlib.crc32(padding, crc)
    crc = zlib.crc32(metadata, crc)
    crc &= __signature_mask
    signature_data = struct.pack(__signature_fmt, crc)

    signed_data = header_data + dtb_data + padding + metadata + signature_data
    assert len(signed_data) == aligned_len

    header.check(signed_data)

    return signed_data

def __verify(version, chipid, majorrev, signed_data, unsigned_data):
    assert len(signed_data) % __dtb_alignment == 0

    metadata_signature_len = __metadata_len + __signature_len

    diff_len = len(signed_data) - len(unsigned_data)
    assert diff_len >= metadata_signature_len
    assert diff_len < metadata_signature_len + __dtb_alignment

    # Verify DTBs are the same except for totalsize
    assert (signed_data[:__dtb_header_off_totalsize] ==
            unsigned_data[:__dtb_header_off_totalsize])
    assert (signed_data[__dtb_header_off_off_dt_struct:-diff_len] ==
            unsigned_data[__dtb_header_off_off_dt_struct:])

    header_data = signed_data[:_dtb_header_len]
    dtb_data = signed_data[_dtb_header_len:-metadata_signature_len]
    metadata = signed_data[-metadata_signature_len:-__signature_len]
    signature_data = signed_data[-__signature_len:]

    # Verify major revision, chip ID, dtbcheck.py version and magic
    assert ((majorrev, chipid, version, __dtbcheck_magic) ==
            struct.unpack(__metadata_fmt, metadata))

    # Verify CRC
    crc = zlib.crc32(header_data)
    crc = zlib.crc32(dtb_data, crc)
    crc = zlib.crc32(metadata, crc)
    crc &= __signature_mask
    signature = struct.unpack(__signature_fmt, signature_data)[0]

    assert crc == signature

def __main():
    global __machine
    chips = {
        't194': t194,
        't234': t234,
        't239': None,
        'th500': None,
    }

    # dtbcheck.py version, chip ID, major revision
    chip_infos = {
        't194': (1, 0x19, 0x1),
        't234': (1, 0x23, 0x4),
        't239': (1, 0x23, 0x9),
        'th500': (1, 0x24, 0x1),
    }

    root_rules = {
        't194':
        FdtRule('', __check_root, subrules=[
            FdtRule('adc', __check_adc, subrules=[
                FdtRule('.+', __check_adc_node),
            ]),
            FdtRule('aotag', __check_aotag),
            FdtRule('avfs', __check_avfs, subrules=[
                FdtRule('clock@.+', __check_avfs_clock, subrules=[
                    FdtRule('bin@.+', __check_avfs_clock_bin),
                    FdtRule('clvc', __check_avfs_clock_clvc),
                    FdtRule('tuning', __check_avfs_clock_tuning),
                ]),
                FdtRule('tuning', __check_avfs_tuning),
            ]),
            FdtRule('clocks', __check_clocks_t194, subrules=[
                FdtRule('init', __check_clocks_init),
                FdtRule('lateinit', __check_clocks_init),
                FdtRule('clock@.+', __check_clocks_clock_t194),
            ]),
            FdtRule('diagnostics', __check_diagnostics),
            FdtRule('dvfs-revision', __check_dvfs_revision),
            FdtRule('dvs', __check_dvs, subrules=[
                FdtRule('clock@.+', __check_dvs_clock, subrules=[
                    FdtRule('rail@.+', __check_dvs_clock_rail, subrules=[
                        FdtRule('opp@.+', __check_dvs_clock_rail_opp),
                        FdtRule('bin@.+', __check_dvs_clock_rail_bin, subrules=[
                            FdtRule('opp@.+', __check_dvs_clock_rail_bin_opp),
                        ]),
                    ]),
                ]),
                FdtRule('rail@.+', __check_dvs_rail, subrules=[
                    FdtRule('bin@.+', __check_dvs_rail_bin),
                ]),
            ]),
            FdtRule('ec', __check_ec, subrules=[
                FdtRule('.+', __check_ec_node),
            ]),
            FdtRule('emc-strap', __check_emc_strap),
            FdtRule('external-memory', __check_external_memory),
            FdtRule('fmon', __check_fmon, subrules=[
                FdtRule('fmon@.+', __check_fmon_fmon),
                FdtRule('vrefro', __check_fmon_vrefro),
            ]),
            FdtRule('fps', __check_fps),
            FdtRule('ftrace', __check_ftrace),
            FdtRule('fuse', __check_fuse),
            FdtRule('i2c-busses', __check_i2c_busses, subrules=[
                FdtRule('.+', __check_i2c_busses_node, subrules=[
                    FdtRule('i2c_firewall_rules', __check_i2c_fw_rules, subrules=[
                        FdtRule('.+', __check_i2c_fw_rules_node_t194),
                    ]),
                ]),
            ]),
            FdtRule('mail', __check_mail, subrules=[
                FdtRule('acl', __check_mail_acl),
            ]),
            FdtRule('pdomains', __check_pdomains, subrules=[
                FdtRule('domain@.+', __check_pdomains_domain),
            ]),
            FdtRule('regulators', __check_regulators, subrules=[
                FdtRule('.+', __check_regulators_node),
            ]),
            FdtRule('reliability-management', __check_rm, subrules=[
                FdtRule('.+', __check_rm_node),
            ]),
            FdtRule('reset', __check_reset, subrules=[
                FdtRule('reset@.+', __check_reset_reset),
            ]),
            FdtRule('serial', __check_serial, subrules=[
                FdtRule('combined-uart', __check_serial_combined_uart),
            ]),
            FdtRule('sku', __check_sku, subrules=[
                FdtRule('.+', __check_sku_node),
            ]),
            FdtRule('soctherm', __check_soctherm, subrules=[
                FdtRule('thermtrip', __check_soctherm_thermtrip),
                FdtRule('hsmtrip', __check_soctherm_hsmtrip),
                FdtRule('throttle', __check_soctherm_throttle),
                FdtRule('edp_oc', __check_soctherm_edp_oc),
                FdtRule('throttlectl', __check_soctherm_throttlectl),
            ]),
            FdtRule('speedo', __check_speedo, subrules=[
                FdtRule('.+', __check_speedo_node),
            ]),
            FdtRule('system-cfg', __check_system_cfg, subrules=[
                FdtRule('sc7', __check_system_cfg_sc7),
            ]),
            FdtRule('tj_max', __check_tj_max, subrules=[
                FdtRule('tz_list', __check_tj_max_tz_list),
            ]),
            FdtRule('uphy', __check_uphy, subrules=[
                FdtRule('sata-fuse-override', __check_uphy_sata_fuse_override),
            ]),
            FdtRule('vrmon', __check_vrmon, subrules=[
                FdtRule('.+', __check_vrmon_node),
            ]),
        ]),
        't234':
        FdtRule('', __check_root, subrules=[
            FdtRule('clocks', __check_clocks, subrules=[
                FdtRule('init', __check_clocks_init),
                FdtRule('lateinit', __check_clocks_init),
                FdtRule('clock@.+', __check_clocks_clock),
            ]),
            FdtRule('diagnostics', __check_diagnostics),
            FdtRule('ec', __check_ec, subrules=[
                FdtRule('.+', __check_ec_node),
            ]),
            # TODO: implement t234 rules for emc nodes
            # FdtRule('emc-strap', __check_emc_strap),
            # FdtRule('external-memory', __check_external_memory),
            FdtRule('fps', __check_fps),
            FdtRule('ftrace', __check_ftrace),
            FdtRule('i2c-busses', __check_i2c_busses, subrules=[
                FdtRule('.+', __check_i2c_busses_node, subrules=[
                    FdtRule('i2c_firewall_rules', __check_i2c_fw_rules, subrules=[
                        FdtRule('.+', __check_i2c_fw_rules_node_t23x),
                    ]),
                ]),
            ]),
            FdtRule('mail', __check_mail, subrules=[
                FdtRule('acl', __check_mail_acl),
            ]),
            FdtRule('pdomains', __check_pdomains, subrules=[
                FdtRule('domain@.+', __check_pdomains_domain),
            ]),
            FdtRule('regulators', __check_regulators, subrules=[
                FdtRule('.+', __check_regulators_node),
            ]),
            # TODO: implement t234 rules for /reliability-management
            # FdtRule('reliability-management', __check_rm, subrules=[
            #     FdtRule('.+', __check_rm_node),
            # ]),
            FdtRule('reset', __check_reset, subrules=[
                FdtRule('reset@.+', __check_reset_reset),
            ]),
            FdtRule('serial', __check_serial, subrules=[
                FdtRule('combined-uart', __check_serial_combined_uart),
            ]),
            FdtRule('sku', __check_sku, subrules=[
                FdtRule('.+', __check_sku_node),
            ]),
            # TODO: implement t234 rulse for /soctherm
            # FdtRule('soctherm', __check_soctherm, subrules=[
            #     FdtRule('thermtrip', __check_soctherm_thermtrip),
            #     FdtRule('hsmtrip', __check_soctherm_hsmtrip),
            #     FdtRule('throttle', __check_soctherm_throttle),
            #     FdtRule('edp_oc', __check_soctherm_edp_oc),
            #     FdtRule('throttlectl', __check_soctherm_throttlectl),
            # ]),
            FdtRule('system-cfg', __check_system_cfg, subrules=[
                FdtRule('sc7', __check_system_cfg_sc7),
            ]),
            # TODO: implement t234 rules for /uphy
            # FdtRule('uphy', __check_uphy, subrules=[
            #     FdtRule('sata-fuse-override', __check_uphy_sata_fuse_override),
            # ]),
            # TODO: implement t234 rules for /vrmon
            # FdtRule('vrmon', __check_vrmon, subrules=[
            #     FdtRule('.+', __check_vrmon_node),
            # ]),
        ]),
        't239':
        FdtRule('', __check_root, subrules=[
            FdtRule('sku', __check_sku, subrules=[
                FdtRule('.+', __check_sku_node),
            ]),
        ]),
        'th500':
        FdtRule('', __check_root, subrules=[
            FdtRule('sku', __check_sku, subrules=[
                FdtRule('.+', __check_sku_node),
            ]),
        ]),
    }

    parser = argparse.ArgumentParser()
    parser.add_argument('-c', '--chip', choices=chips.keys(),
                        required=True)
    parser.add_argument('-o', '--output', type=argparse.FileType('wb'),
                        default=sys.stdout)
    parser.add_argument('input', type=argparse.FileType('rb'))

    args = parser.parse_args()

    __machine = chips[args.chip]

    infile = args.input
    if infile == sys.stdin and hasattr(sys.stdin, 'buffer'):
        infile = sys.stdin.buffer

    outfile = args.output
    if outfile == sys.stdout and hasattr(sys.stdout, 'buffer'):
        outfile = sys.stdout.buffer

    data = infile.read()

    assert len(data) >= _dtb_header_len

    # Validate DTB header
    header = __parse_header(data[:_dtb_header_len])
    header.check(data)

    # Parse and validate DTB struct
    root = __parse_dt_struct(header, data, header.off_dt_struct)

    # Validate DTB against rules
    rule = root_rules[args.chip]
    rule.check(root)

    version, chipid, majorrev = chip_infos[args.chip]

    # Sign and verify output
    signed_data = __sign(version, chipid, majorrev, header, data)
    __verify(version, chipid, majorrev, signed_data, data)

    outfile.write(signed_data)

if __name__ == '__main__':
    __main()
