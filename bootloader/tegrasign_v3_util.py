from __future__ import print_function

#
# Copyright (c) 2018-2020, NVIDIA Corporation.  All Rights Reserved.
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA Corporation is strictly prohibited.
#

import struct
import os, sys
import subprocess
import re
import time

AES_128_HASH_BLOCK_LEN = 16
AES_256_HASH_BLOCK_LEN = 16

NV_RSA_MAX_KEY_SIZE = 512
NV_COORDINATE_SIZE  = 64

NV_ECC_SIG_STRUCT_SIZE = 96
ED25519_KEY_SIZE = 32
ED25519_SIG_SIZE = 64

MAX_KEY_LIST = 3

# Mode Defines
NvTegraSign_FSKP    = 'FSKP'
NvTegraSign_SBK     = 'SBK'
NvTegraSign_PKC     = 'PKC'
NvTegraSign_ECC     = 'ECC'
NvTegraSign_ED25519 = 'ED25519'

TegraOpenssl = 'tegraopenssl'

class NvTegraPkcsVersion:
     NvTegraPkcsV1_5 = 1
     NvTegraPkcsV2_1 = 2

class PkcKey:
    def __init__(self):
        self.PkcsVersion = 0
        self.PubKey = bytearray(NV_RSA_MAX_KEY_SIZE + 1)
        self.PrivKey = bytearray(NV_RSA_MAX_KEY_SIZE + 1)
        self.P = bytearray(NV_RSA_MAX_KEY_SIZE)
        self.Q = bytearray(NV_RSA_MAX_KEY_SIZE)
        #self.KeySize = 0

class ECKey:
    def __init__(self):
        self.PrivKey = 1
        self.Coordinate = bytearray(NV_COORDINATE_SIZE)
        #self.KeySize = 0

class EDKey:
    def __init__(self):
        self.PrimeD = bytearray(ED25519_KEY_SIZE)
        self.PubKey = bytearray(ED25519_KEY_SIZE)

class Key:
    def __init__(self):
        self.eckey = ECKey()
        self.edkey = EDKey()
        self.pkckey = PkcKey()
        self.aeskey = bytearray(16)

class SignKey:
    def __init__(self):
        self.mode = "Unknown"
        self.filename = "Unknown"
        self.key = Key()
        self.keysize = 16

class tegrasign_exception(Exception):
    def __init__(self, value):
        self.value = value

    def __str__(self):
        return repr(self.value)

cmd_environ = { }
start_time = time.time()
is_standalone = False
is_verbose = False


def isPython3():

    if sys.hexversion >= 0x3000000:
        return True

    return False

'''
If use_verbose is True and '--verbose' is set, then proceed to:
If tegrasign is invoked as standalone, do default print
Else it prints timestamp, to be aligned with tegraflash
'''
def info_print(string, use_verbose=False):
    global is_verbose
    if use_verbose and is_verbose == False:
        return

    if is_standalone:
        print(string)
    else:
        diff_time = time.time() - start_time
        print('[ %8.4f ] %s' % (diff_time, string))

def print_process(process, capture_log = False):

    print_time = True
    diff_time = time.time() - start_time
    log = ''

    while process.poll() is None:
        output = process.stdout.read(1)
        if capture_log:
            log += output.decode("utf-8")
        outputchar = output.decode('ascii')

        if outputchar == '\n' :
            diff_time = time.time() - start_time
            print_time = True
        elif outputchar == '\r' :
            print_time = True
        elif outputchar:
            if print_time and not is_standalone:
                print('[ %8.4f ] ' % diff_time, end='')
                print_time = False

        sys.stdout.write(outputchar)
        sys.stdout.flush()

    for string in process.communicate()[0].decode('utf-8').split('\n'):
        if capture_log and len(string) > 0:
            log += str(string)
            info_print(string)

    return log

def set_env(standalone, verbose):
    if standalone:
        global cmd_environ
        local_env = os.environ
        local_env["PATH"] += os.pathsep + os.path.dirname(os.path.realpath(__file__))
    else:
        # Delay import to avoid import clash
        from tegraflash_internal import cmd_environ
        local_env = cmd_environ
    cmd_environ = local_env

    global is_standalone
    is_standalone = standalone

    global is_verbose
    is_verbose = verbose

def run_command(cmd, enable_print=True):

    log = ''
    if is_verbose == True:
        info_print(' '.join(cmd))

    use_shell = False
    if sys.platform == 'win32':
        use_shell = True

    process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, shell=use_shell, env=cmd_environ)

    if enable_print == True:
        log = print_process(process, enable_print)
    return_code = process.wait()

    if return_code != 0:
        raise tegrasign_exception('Return value ' + str(return_code) +
                '\nCommand ' + ' '.join(cmd))

    return log

'''
Master exit routine to terminate tegrasign.
'''
def exit_routine():
    info_print ('********* Error. Quitting. *********')
    global is_standalone
    if is_standalone:
        sys.exit(1)
    else:
        return 1

'''
Opens a file and returns a file handle or None if fail
'''
def open_file(file_name, attrib):
    file_handle = None

    try:
        file_handle = open(file_name,attrib)
    except IOError:
        info_print("Cannot open %s with attribute %s\n" %(file_name,attrib))
        exit_routine()

    return file_handle

'''
Write data to given file handle
'''
def write_file(file_handle, data):
    try:
        file_handle.write(data)
    except IOError:
        info_print("Cannot write %s \n" %(file_name))

'''
Executes the name as a binary file
'''
def exec_file(name):
    bin_name = name

    if sys.platform == 'win32' or sys.platform == 'cygwin':
        bin_name = name + '.exe'

    use_shell = False
    if sys.platform == 'win32':
        use_shell = True

    try:
        subprocess.Popen([bin_name], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, shell=use_shell, env=cmd_environ)
    except Exception as e:
        raise tegrasign_exception('Could not find ' + bin_name)

    return [bin_name]

'''
Checks to see if filename is a file, if it is not, prints warning msg
'''
def check_file(filename):
    if not os.path.isfile(filename):
        info_print('Warning: %s is not found' %(filename))
        return False

    return True

'''
Returns a str for the given mode. For xml tag, zerosbk and sbk both returns 'sbk'
For mode.txt, zerosbk will return 'zerosbk' instead of 'sbk'
'''
def get_mode_str(pKey, is_modetxt):
    if pKey.mode == NvTegraSign_PKC:
        mode_str = 'pkc'
    elif pKey.mode == NvTegraSign_ECC:
        mode_str = 'ec'
    elif pKey.mode == NvTegraSign_ED25519:
        mode_str = 'eddsa'
    else:
        if (is_modetxt and is_zero_aes(pKey)):
            mode_str = 'zerosbk'
        else:
            mode_str = 'sbk'

    return mode_str

'''
Returns a byte array specified by n size using the input value
'''
def int_2bytes(n, val):
    n=int(n)
    val=int(val)
    arr = bytearray(int(n))
    for i in range(n-1):
        arr[i] = val & 0xFF
        val >>= 8

    arr[n-1] = val & 0xFF

    return bytes(arr)

'''
Returns the number of bytes for the given integer
'''
def int_2byte_cnt(val):
    val = int(val)
    h = hex(val)
    n_cnt = len(str(h)) - 2 # account for '0x'

    if (n_cnt %2 == 0):
        return n_cnt/2

    return n_cnt/2 + 1

'''
Checks the return string to make sure it does contain: 'Valid'
'''
def is_ret_ok(ret_str):

    if "Valid" in ret_str or "Key size is " in ret_str:
        return True
    return False


'''
Checks to see if the given aes key is a string of zeros or not
'''
def is_zero_aes(p_key):
    for b in p_key.key.aeskey:
        if b != 0:
            return False

    return True

'''
Swap a mutable bytearray into little-endian format that Tegra expects
'''
def swapbytes(a):

    n = len(a)
    if n % 4 != 0:
        return None

    for i in range(0, int(n/2)):
        a[i], a[n-i-1] = a[n-i-1], a[i]

    return a
