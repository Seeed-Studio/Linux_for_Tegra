#!/usr/bin/python

# Copyright (c) 2015-2018, NVIDIA CORPORATION. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#
# Converts pinmux dts to cfg format
#

import sys
import datetime
import argparse

tool_version = "version 1.0"

def rmchars(s, chrset):
    for c in chrset:
        s = s.replace(c, '')
    return s

# Processes the string to remove unnecessary characters
def preprocess_string(line):
    line = rmchars(line,"\r\n")
    return rmchars(line," \n\t{")

def print_pinmux(addr, mask, value, comment, do_print_mask = False):
    if do_print_mask:
        print("pinmux.0x%08x.0x%08x = 0x%08x; # %s" % (addr, mask, value, comment))
    else:
        print("pinmux.0x%08x = 0x%08x; # %s" % (addr, value, comment))

# Processes gpio address file and returns dictionary which maps port
# to port address.
def process_gpio_address_file(f):
    gpio_port_addr_dict = {}
    for src_line in f.readlines():
            # Preprocess line
            line = rmchars(src_line, " \t\n\r").split(',')
            gpio_port_addr_dict[line[0]] = [line[1]] # "I" : ['0x2210800']
            # print(line[0],gpio_port_addr_dict[line[0]])
    return gpio_port_addr_dict

# Processes pinmux address file and returns two dictionaries. One
# dictionary maps full pin name to address (needed for processing
# pinmux dts file) and other maps pinname to address (needed for
# processing gpio dts file).
def process_address_file(f):
    pinmux_dict = {}
    pinmux_gpio_dict = {}
    for src_line in f.readlines():
            # Preprocess line - e.g. pex_l0_rst_n_pa0, PE0, RSVD1, RSVD2, RSVD3, 0x7024, 0
            line = rmchars(src_line, " \t\r\n").split(',')

            # Pinmux dict
            pinmux_dict[line[0]] = line[1:7] # "pex_l0_rst_n_pa0" : ['PE0','RSVD1','RSVD2','RSVD3','0x7024','0']
            # print(line[0],line[1:7])

            # GPIO dict
            pin_name = line[0].split('_')
            pin_name = pin_name[len(pin_name)-1]
            pin_name = pin_name[1:len(pin_name)]
            pinmux_gpio_dict[pin_name.upper()] = [line[0], line[5], line[6]] # "a0" : ['pex_l0_rst_n_pa0', '0x7024','0']
            # print(pin_name.upper(),pinmux_gpio_dict[pin_name.upper()])
    return (pinmux_dict, pinmux_gpio_dict)

# Process por value file and return dictionary which maps address to por value.
def process_por_val_file(f):
    por_val_dict = {}
    for src_line in f.readlines():
            line = rmchars(src_line, "\t\n").split(',')
            por_val_dict[line[0]] = line[1] # '0x022130a0': '0x12300000'
            # print (line[0], por_val_dict[line[0]]);
    return por_val_dict

# Process mandatory pinmux file and return dictionary which maps pinname to mandatory values
def process_mandatory_pinmux_file(f, pinmux_dict):
   mandatory_pin_dict = {}
   for src_line in f.readlines():
           if src_line[0] == '#':
                 continue
           line = rmchars(src_line, "\t\n").split(':')
           val = ['NA','NA','NA','NA','NA','NA']
           for i in range(1, len(line)):
                 fields = (line[i]).split('=')
                 if fields[0] == "nvidia,function":
                      pinmux_option = pinmux_dict[line[0]]
                      if fields[1] == pinmux_option[0].lower():
                             val[0] = '0'
                      elif fields[1] == pinmux_option[1].lower():
                             val[0] = '1'
                      elif fields[1] == pinmux_option[2].lower():
                             val[0] = '2'
                      elif fields[1] == pinmux_option[3].lower():
                             val[0] = '3'
                 elif fields[0] == "nvidia,pull":
                      if fields[1] == "<TEGRA_PIN_PULL_NONE>":
                             val[1] = '0'
                      elif fields[1] == "<TEGRA_PIN_PULL_DOWN>":
                             val[1] = '1'
                      elif fields[1] == "<TEGRA_PIN_PULL_UP>":
                             val[1] = '2'
                      elif fields[1] == "<TEGRA_PIN_RSVD>":
                             val[1] = '3'
                 elif fields[0] == "nvidia,tristate":
                      if fields[1] == "<TEGRA_PIN_ENABLE>":
                             val[2] = '1'
                      elif fields[1] == "<TEGRA_PIN_DISABLE>":
                             val[2] = '0'
                 elif fields[0] == "nvidia,enable-input":
                      if fields[1] == "<TEGRA_PIN_ENABLE>":
                             val[3] = '1'
                      elif fields[1] == "<TEGRA_PIN_DISABLE>":
                             val[3] = '0'
                 elif fields[0] == "nvidia,lpdr":
                      if fields[1] == "<TEGRA_PIN_ENABLE>":
                             val[4] = '1'
                      elif fields[1] == "<TEGRA_PIN_DISABLE>":
                             val[4] = '0'
                 elif fields[0] == "nvidia,lpbk":
                      if fields[1] == "<TEGRA_PIN_ENABLE>":
                             val[5] = '1'
                      elif fields[1] == "<TEGRA_PIN_DISABLE>":
                             val[5] = '0'
           if val[0] != 'NA':
                 mandatory_pin_dict[line[0]+"-"+val[0]] = val # 'cam_i2c_sda_po3-0': ['0', 'NA', '0', 'NA', '1', 'NA']
           else:
                 sys.stderr.write("ERROR: pin %s \"nvidia,function\" field is invalid in mandatory pinmux file" % (line[0]))
                 sys.exit(1)
           # print (line[0], mandatory_pin_dict[line[0]]);
   return mandatory_pin_dict

# Processes pmc bit information file and return dictionary which
# maps function to adress information.
def process_padinfo_file(f):
    pmc_dict = {}
    for src_line in f.readlines():
            # Preprocess line - e.g. ufs,1,1
            line = rmchars(src_line, " \n\r\t").split(',')

            # pmc dict
            pmc_dict[line[0]] = line[1:3]
            # print(line[0],pmc_dict[line[0]])
    return pmc_dict

# GPIO dts file processing apis
def process_gpio_file(f, pinmux_gpio_dict, gpio_addr_dict, pinmux_non_aon_addr, pinmux_aon_addr, do_print_mask = False):
    gpio_list = []
    line = f.readline()
    while line:
        line = rmchars(preprocess_string(line), "<=")
        if line == "gpio-input":
            gpio_type = 1
            print("#### Pinmux for gpio-input pins ####")
        elif line == "gpio-output-low":
            gpio_type = 2
            print("#### Pinmux for gpio-output-low pins ####")
        elif line == "gpio-output-high":
            gpio_type = 3
            print("#### Pinmux for gpio-output-high pins ####")
        else:
            line = f.readline()
            continue
        # Got node to process
        while 1:
            val = 0
            line = f.readline()
            line = preprocess_string(line)
            line = line.replace("TEGRA_GPIO(","")
            line = line.replace(")","")
            if line == ">;":
                break
            line = line.split(",")
            port = line[0]
            pin_no = int(line[1])

            # Enable GPIO in GPIO config register (bit 0 at offset 0x0)
            port_addr = int(gpio_addr_dict[port][0], 0) # Got port address
            pin_addr = port_addr + (0x20*pin_no)
            cfg_addr = pin_addr + 0x0
            out_ctrl_addr = pin_addr + 0xc
            out_val_addr = pin_addr + 0x10
            if gpio_type == 1: # gpio-input
                # CFG: set bit 0, unset bit 1 at offset 0x0
                cfg_val = 0x1
            elif gpio_type == 2: # gpio-ouput-low
                # CFG: set bit 0, set bit 1 at offset 0x0
                cfg_val = 0x3
                # CTRL: unset bit 0  at offset 0xc
                ctrl_val = 0x0 # DRIVEN
                # OUT:  set bit 0 at offset 0x10
                val = 0x0     # OUT low
            elif gpio_type == 3: # gpio-ouput-high
                # CFG: set bit 0, set bit 1 at offset 0x0
                cfg_val = 0x3
                # CTRL: unset bit 0  at offset 0xc
                ctrl_val = 0x0 # DRIVEN
                # OUT:  set bit 0 at offset 0x10
                val = 0x1     # OUT high
            else:
                print("Invalid gpio type")
            mask = 0x03
            ctrl_mask = 0x01
            cfg_mask = 0x03

            print_pinmux(cfg_addr, cfg_mask, cfg_val, "CONFIG %s%s" % (port, line[1]), do_print_mask)
            # print ("pinmux.0x%08x.0x%08x = 0x%08x; # CONFIG %s%s" % (cfg_addr, cfg_mask, cfg_val, port, line[1]))
            if gpio_type == 2 or gpio_type == 3:
                print_pinmux(out_ctrl_addr, ctrl_mask, ctrl_val, "CONTROL %s%s" % (port, line[1]), do_print_mask)
                # print("pinmux.0x%08x.0x%08x = 0x%08x; # CONTROL %s%s" % (out_ctrl_addr, ctrl_mask, ctrl_val, port, line[1]))
                print_pinmux(out_val_addr, mask, val, "OUTPUT %s%s" % (port, line[1]), do_print_mask)
                # print("pinmux.0x%08x.0x%08x = 0x%08x; # OUTPUT %s%s" % (out_val_addr, mask, val, port, line[1]))

            # Setting pin to GPIO in pinmux config register (bit 10 at offset 0x0 to zero)
            pin_attr = pinmux_gpio_dict[line[0]+line[1]]
            if pin_attr[2] == '0':
                addr = pinmux_non_aon_addr+int(pin_attr[1], 0)
            elif pin_attr[2] == '1':
                addr = pinmux_aon_addr+int(pin_attr[1], 0)
            else:
                print("Error no domain information")
            # print pinmux_gpio_dict[line[0]+line[1]]
            print_pinmux(addr, mask, 0, "GPIO %s" % pin_attr[0], do_print_mask)
            # print("pinmux.0x%08x.0x%08x = 0x%08x; # GPIO %s" % (addr, mask, 0, pin_attr[0]))
            gpio_list.append(pin_attr[0])
        line = f.readline()
    print("")
    return gpio_list

def mandatory_pinmux_check(mandatory_pinmux_dict, pin_name, por_val):
    mask = [0x3, 0xc, 0x10, 0x40, 0x100, 0x20 ]
    shift = [0, 2, 4, 6, 8, 5]
    fields = ['nvidia,function', 'nvidia,pull', 'nvidia,tristate',  'nvidia,enable-input', 'nvidia,lpdr', 'nvidia,lpbk']

    field_val = ((por_val & mask[0]) >> shift[0])
    mandatory_pin_name = pin_name+"-"+str(field_val)
    if mandatory_pin_name in mandatory_pinmux_dict:
        attrs = mandatory_pinmux_dict[mandatory_pin_name]
        for i in range(1, len(mask)):
             if attrs[i] != 'NA':
                 field_val = ((por_val & mask[i]) >> shift[i])
                 if field_val != int(attrs[i]):
                      sys.stderr.write("ERROR: pin %s(0x%08x) field %s(0x%08x) is not matching, val = 0x%02x expected = 0x%02x\n" % (pin_name, por_val, fields[i], mask[i], field_val, int(attrs[i])))
    return

# Pinmux dts file processing apis
def process_attr(pin_name, src_line, pinmux_option, val, mask, opt_str):

    line = src_line.split('=')
    line[1] = rmchars(line[1], '";')

    if line[0] == "nvidia,function": # nvidia,function (1:0)
        if line[1] == pinmux_option[0].lower():
            val = val | ((0x0) << 0)
            opt_str += pinmux_option[0].lower()+", "
        elif line[1] == pinmux_option[1].lower():
            val = val | ((0x1) << 0)
            opt_str += pinmux_option[1].lower()+", "
        elif line[1] == pinmux_option[2].lower():
            val = val | ((0x2) << 0)
            opt_str += pinmux_option[2].lower()+", "
        elif line[1] == pinmux_option[3].lower():
            val = val | ((0x3) << 0)
            opt_str += pinmux_option[3].lower()+", "
        else:
            sys.stderr.write("ERROR: pin %s has invalid \"nvidia,function\" = \"%s\" option\n" %(pin_name, line[1]))
        mask = mask | (0x03 << 0)
    elif line[0] == "nvidia,pull": # nvidia,pull  (3:2)
        if line[1] == "<TEGRA_PIN_PULL_DOWN>":
            val = val | ((0x1) << 2)
            opt_str += "pull-down, "
        elif line[1] == "<TEGRA_PIN_PULL_UP>":
            val = val | ((0x2) << 2)
            opt_str += "pull-up, "
        elif line[1] == "<TEGRA_PIN_RSVD>":
            val = val | ((0x3) << 2)
            opt_str += "pull-rsvd, "
        mask = mask | (0x03 << 2)
    elif line[0] == "nvidia,tristate":
        if line[1] == "<TEGRA_PIN_ENABLE>": # nvidia,tristate (4:4)
            val = val | ((0x1) << 4)
            opt_str += "tristate-enable, "
        else:
            opt_str += "tristate-disable, "
        mask = mask | (0x01 << 4)
    elif line[0] == "nvidia,io-high-voltage":
        if line[1] == "<TEGRA_PIN_ENABLE>": # nvidia,io-high-voltage (5:5)
            val = val | ((0x1) << 5)
            opt_str += ", io_high_voltage-enable"
        else:
            opt_str += ", io_high_voltage-disable"
        mask = mask | (0x01 << 5)
    elif line[0] == "nvidia,loopback":
        if line[1] == "<TEGRA_PIN_ENABLE>": # nvidia,loopback (5:5)
            val = val | ((0x1) << 5)
            opt_str += ", loopback-enable"
        else:
            opt_str += ", loopback-disable"
        mask = mask | (0x01 << 5)
    elif line[0] == "nvidia,enable-input":
        if line[1] == "<TEGRA_PIN_ENABLE>": # nvidia,enable-input (6:6)
            val = val | ((0x1) << 6)
            opt_str += "input-enable"
        else:
            opt_str += "input-disable"
        mask = mask | (0x01 << 6)
    elif line[0] == "nvidia,lpdr":
        if line[1] == "<TEGRA_PIN_ENABLE>": # nvidia,lpdr (8:8)
            val = val | ((0x1) << 8)
            opt_str += ", lpdr-enable"
        else:
            opt_str += ", lpdr-disable"
        mask = mask | (0x01 << 8)
    else:
        pass

#   print ("# %s: val = %x, mask = %x" % (line[0], val, mask))
    return (val, mask, opt_str)

def process_pinmux_file(f, por_val_dict, mandatory_pin_dict, dict, non_aon_addr, aon_addr, gpio_list, do_print_mask = False):
    line = f.readline()
    while line:
        line = preprocess_string(line)
        if line == "pinmux_default:common":
            print("#### Pinmux for used pins ####")
        elif line == "pinmux_unused_lowpower:unused_lowpower":
            print("#### Pinmux for unused pins for low-power configuration ####")
        # print(line)
        # Got node to process
        if line in dict:
            # Fill pinmux and address.
            pin_name = line
            pin_attr = dict[line]
            opt_str = ""
            # print(line, pin_attr)
            if pin_attr[5] == '0':
                addr = non_aon_addr+int(pin_attr[4], 0)
            elif pin_attr[5] == '1':
                addr = aon_addr+int(pin_attr[4], 0)
            else:
                print("Error no domain information")
            mask = (1<<10)
            if pin_name in gpio_list:
                is_gpio = 1
                val = (0<<10)
            else:
                is_gpio = 0
                val = (1<<10)

            # Handle POR value
            por_val = 0x0
            hex_addr = "{0:#0{1}x}".format(addr,10)
            if hex_addr in por_val_dict:
                # print "%s : %s" % (hex_addr, por_val_dict[hex_addr])
                por_val = int(por_val_dict[hex_addr], 0)

            # Process the pin node attributes
            while 1:
                line = f.readline()
                line = preprocess_string(line)
                if line != "};":
                    val, mask, opt_str = process_attr(pin_name, line, pin_attr[0:4], val, mask, opt_str)
                    # print(line)
                else:
                    break
                if do_print_mask:
                    if val == 0 and mask == 0:
                        continue

                    por_val &= ~mask;
                    por_val |= val & mask;

                    if not is_gpio:
                         mandatory_pinmux_check(mandatory_pin_dict, pin_name, por_val)
                    print_pinmux(addr, mask, por_val, "%s: %s" % (pin_name, opt_str), do_print_mask = True)
                    mask = 0
                    val = 0
                    opt_str = ""

            if not do_print_mask:
                por_val &= ~mask;
                por_val |= val & mask;
                if not is_gpio:
                     mandatory_pinmux_check(mandatory_pin_dict, pin_name, por_val)
                print_pinmux(addr, mask, por_val, "%s: %s" % (pin_name, opt_str), do_print_mask = False)
            # print("pinmux.0x%08x.0x%08x = 0x%08x; # %s: %s" % (addr, mask, val, pin_name, opt_str))
        line = f.readline()
    f.close()
    return

# Pad dts file processing apis
def process_pad_file(f, dict, reg1_addr, reg2_addr):
    line = f.readline()
    val1 = 0x70
    val2 = 0x53
    while line:
        line = preprocess_string(line)
        if line in dict:
            attr = dict[line]
            nxt_line = f.readline()
            nxt_line = preprocess_string(nxt_line)
            nxt_line = nxt_line.split('=')
            nxt_line = nxt_line[1].replace(";","")
            if attr[0] == '1': # 1v8 reg
                if nxt_line == "<IO_PAD_VOLTAGE_1_2V>":
                    val1 = val1 & ~(1 << int(attr[1]))
                elif nxt_line == "<IO_PAD_VOLTAGE_1_8V>":
                    val1 = val1 | (1 << int(attr[1]))
            elif attr[0] == '2': # 3V3 reg
                if nxt_line == "<IO_PAD_VOLTAGE_1_8V>":
                    val2 = val2 & ~(1 << int(attr[1]))
                elif nxt_line == "<IO_PAD_VOLTAGE_3_3V>":
                    val2 = val2 | (1 << int(attr[1]))
            else:
                print("Error no proper reg information")
            out_str = ""
            if nxt_line == "<IO_PAD_VOLTAGE_1_2V>":
                out_str = "1.2V"
            elif nxt_line == "<IO_PAD_VOLTAGE_1_8V>":
                out_str = "1.8V"
            elif nxt_line == "<IO_PAD_VOLTAGE_3_3V>":
                out_str = "3.3V"
            # print(line,attr,nxt_line)
            print("# %-10s : %s" % (line.upper(), out_str))
        line = f.readline()
    print("pad-voltage.major = 1;")
    print("pad-voltage.minor = 0;")
    print("pad-voltage.0x%08x = 0x%08x; # %s" % (reg1_addr, val1, "PMC_IMPL_E_18V_PWR_0"))
    print("pad-voltage.0x%08x = 0x%08x; # %s" % (reg2_addr, val2, "PMC_IMPL_E_33V_PWR_0"))

def main(argv0, argv):
    pad = argparse.ArgumentParser('pad', add_help = False)
    pad.add_argument('pad_info_file', type = argparse.FileType('r'))
    pad.add_argument('pad_dts_file', type = argparse.FileType('r'))
    pad.add_argument('version')
    pad.add_argument('--mask', help = 'Show masks in output', action = 'store_true')
    pad.add_argument('--help', help = 'This help', action = 'store_true')
    pad.add_argument('--version', action='version', version= 'pinmux-dts2cfg.py ' + tool_version)
    pinmux = argparse.ArgumentParser('pinmux', add_help = False)
    pinmux.add_argument('--pinmux', action='store_true')
    pinmux.add_argument('address_info_file', type = argparse.FileType('r'))
    pinmux.add_argument('gpio_address_file', type = argparse.FileType('r'))
    pinmux.add_argument('por_val_file', type = argparse.FileType('r'))
    pinmux.add_argument('--mandatory_pinmux_file', type = argparse.FileType('r'))
    pinmux.add_argument('pinmux_dts_file', type = argparse.FileType('r'))
    pinmux.add_argument('gpio_dts_file', type = argparse.FileType('r'))
    pinmux.add_argument('version')
    pinmux.add_argument('--mask', help = 'Show masks in output', action = 'store_true')
    pinmux.add_argument('--help', help = 'This help', action = 'store_true')
    pinmux.add_argument('--version', action='version', version= 'pinmux-dts2cfg.py ' + tool_version)

    if argv[0] == "--pad": # processing pad files
        parser = pad
        argv = argv[1:]
    else:
        parser = pinmux

    v = parser.parse_args(argv)
    if v.help:
        print ("Usage: %s [--pad] parameters...." % argv0)
        pad.print_help()
        print ("")
        pinmux.print_help()
        sys.exit(0)

    now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
    if parser == pad:
        print ("##")
        print ("## CFG version %s" % v.version)
        print ("## Input pad file name: %s" % v.pad_dts_file.name)
        print ("## Generation date: %s" % now)
        print ("## PLEASE DO NOT EDIT THIS FILE")
        print ("## This is autogenerated file using the script pinmux-dts2cfg.py")
        print ("##")
        reg1_addr = 0x0c36003c
        reg2_addr = 0x0c360040
        pad_dict = process_padinfo_file(v.pad_info_file)
        process_pad_file(v.pad_dts_file, pad_dict, reg1_addr, reg2_addr)
    else:
        print ("##")
        print ("## Pinmux version %s" % v.version)
        print ("## Input pinmux file name: %s" % v.pinmux_dts_file.name)
        print ("## Input gpio file name: %s" % v.gpio_dts_file.name)
        print ("## Generation date: %s" % now)
        print ("## PLEASE DO NOT EDIT THIS FILE")
        print ("## This is autogenerated file using the script pinmux-dts2cfg.py")
        print ("##")
        print ("pinmux.major = 1;")
        print ("pinmux.minor = 0;")
        pinmux_non_aon_addr = 0x2430000
        pinmux_aon_addr = 0xc300000
        pinmux_dict, pinmux_gpio_dict = process_address_file(v.address_info_file)
        gpio_port_addr_dict = process_gpio_address_file(v.gpio_address_file)
        por_val_dict = process_por_val_file(v.por_val_file)
        if v.mandatory_pinmux_file:
                mandatory_pin_dict = process_mandatory_pinmux_file(v.mandatory_pinmux_file, pinmux_dict)
        else:
                mandatory_pin_dict = {}
        gpio_list = process_gpio_file(v.gpio_dts_file, pinmux_gpio_dict, gpio_port_addr_dict, pinmux_non_aon_addr, pinmux_aon_addr, v.mask)
        process_pinmux_file(v.pinmux_dts_file, por_val_dict, mandatory_pin_dict, pinmux_dict, pinmux_non_aon_addr, pinmux_aon_addr, gpio_list, v.mask)

if __name__ == "__main__":
    main(sys.argv[0], sys.argv[1:])
