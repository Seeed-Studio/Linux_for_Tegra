from __future__ import print_function

#
# SPDX-FileCopyrightText: Copyright (c) 2018-2025, NVIDIA Corporation.  All Rights Reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA Corporation is strictly prohibited.
#

import binascii
import hashlib
import json
import math
import struct
import os, sys
import subprocess
import re
import time
import traceback
import yaml

AES_128_HASH_BLOCK_LEN = 16
AES_256_HASH_BLOCK_LEN = 16

NV_RSA_MAX_KEY_SIZE = 512
NV_COORDINATE_SIZE  = 64

NV_ECC_SIG_STRUCT_SIZE = 96
NV_ECC521_SIG_STRUCT_SIZE = 136
ECC_KEY_SIZE = 32
ECC521_KEY_SIZE = 68
ED25519_KEY_SIZE = 32
ED25519_SIG_SIZE = 64
XMSS_KEY_SIZE = 132
XMSS_SIG_SIZE = 2832
RSA3K_KEY_SIZE = 384
PCP_SIZE = 384
IV_SIZE = 12

MAX_KEY_LIST = 3
MAX_PKC_KEY_COUNT = 16  # Max number of PKC keys for image signing on T264

# Mode Defines
NvTegraSign_FSKP    = 'FSKP'
NvTegraSign_SBK     = 'SBK'
NvTegraSign_PKC     = 'PKC'
NvTegraSign_ECC     = 'ECC'
NvTegraSign_ECC521  = 'ECC521'
NvTegraSign_ED25519 = 'EDDSA'
NvTegraSign_XMSS    = 'XMSS'
NvTegraSign_LIST    = 'LIST'

TegraOpenssl = 'tegraopenssl'
TegraSign_v3_Keystore = '/tmp/tegrasign_v3.keystore'

class ReadFlag:
    IGNORE  = 1
    ENFORCE = 2

class KdfType:
     CBC     = 0
     GCM     = 1

class KeyType:
    SBK     = 'SBK'
    SBK_HPSE= 'SBK_HPSE'
    SBK_SB  = 'SBK_SB'
    KEK0    = 'KEK0'
    FSKP_AK = 'FSKP_AK'
    FSKP_EK = 'FSKP_EK'
    FSKP_KDK= 'FSKP_KDK'
    FSKP    = 'FSKP'
    PKC     = 'PKC'
    ED25519 = 'EDDSA'
    ECC     = 'ECC'
    ECC521  = 'ECC521'
    XMSS    = 'XMSS'
    HSM     = 'HSM'     # This is for --key not defined
    HSM_L4T = 'HSM_L4T' # This is for L4T HSM signing mode
    UNKNOWN = 'UNKNOWN' # This is default init

class Token:
    def __init__(self, arr = None):
        self.buf = arr
        self.filename = None
        if (arr != None):
           self.buf = str_to_hex(arr) # Set the default value

    def parse(self, arg, is_str = True):
        if '=' in arg:
            # Format: context=context.bin
            sublist = arg.split('=')
            self.read(sublist[1])
        else:
            # Format: expect string in
            self.buf = str_to_hex(arg)

    def read(self, filename, flag = ReadFlag.ENFORCE):
        if (filename == None) and (flag == ReadFlag.IGNORE):
            return                    # Assume default value
        with open_file(filename, 'rb') as f:
            self.buf = f.read()
            self.filename = filename
    def set_buf(self, hexbuf):
        self.buf = hexbuf

    def get_hexbuf(self):
        return self.buf

    def get_strbuf(self):
        if self.buf == None:
            return None
        return hex_to_str(self.buf)

    def is_valid(self):
        if self.buf == None:
            return False
        return True

class PkcKey:
    def __init__(self):
        self.Sha = Sha._256
        self.PkcsVersion = 0
        self.PubKey = bytearray(NV_RSA_MAX_KEY_SIZE + 1)
        self.PrivKey = bytearray(NV_RSA_MAX_KEY_SIZE + 1)
        self.P = bytearray(NV_RSA_MAX_KEY_SIZE)
        self.Q = bytearray(NV_RSA_MAX_KEY_SIZE)

class ECKey:
    def __init__(self):
        self.PrivKey = 1
        self.Coordinate = bytearray(NV_COORDINATE_SIZE)

class EDKey:
    def __init__(self):
        self.PrimeD = bytearray(ED25519_KEY_SIZE)
        self.PubKey = bytearray(ED25519_KEY_SIZE)

class HSM:
    def __init__(self):
        self.type = KeyType.UNKNOWN
        self.algo_only = False
        self.magicid = None # This is for loading key specific to magic id
        self.revoke = False # This is for loading revoke key specific to magic id
        self.active_index = None # This is for active index
        self.key_id = None
        self.l4t_key_label = None # This is for L4T HSM signing mode

    def parse(self, arg_list, mode):
        # Take the mode as type when hsm is not explicitly defined
        if (arg_list == None) or (len(arg_list) == 0):
            self.type = mode
            return

        # Ensure arg_list is a list for proper iteration
        if isinstance(arg_list, str):
            # If a single string is passed, treat it as a single argument
            arg_list = [arg_list]

        # Parse the hsm flag for the mode explicitly
        for arg in arg_list:
            org_arg = arg # To avoid uppercase key label supported only in HSM_L4T
            arg = arg.upper()  # Only convert current argument to uppercase
            m = re.search(r'^[\d]+', arg)
            if m:
                self.active_index = m.group(0)
            if KeyType.FSKP_AK in arg:
                self.type = KeyType.FSKP_AK
            elif KeyType.FSKP_EK in arg:
                self.type = KeyType.FSKP_EK
            elif KeyType.FSKP_KDK in arg:
                self.type = KeyType.FSKP_KDK
            elif KeyType.FSKP in arg:
                self.type = KeyType.FSKP
            elif KeyType.KEK0 in arg:
                self.type = KeyType.KEK0
            elif KeyType.SBK in arg:
                self.type = KeyType.SBK
            elif 'RSA' in arg:
                self.type = KeyType.PKC
            elif KeyType.ED25519 in arg:
                self.type = KeyType.ED25519
            elif KeyType.ECC521 in arg:
                self.type = KeyType.ECC521
            elif KeyType.ECC in arg:
                self.type = KeyType.ECC
            elif KeyType.XMSS in arg:
                self.type = KeyType.XMSS
            elif arg.startswith(KeyType.HSM_L4T):
                # Enable SoftHSM mode for L4T HSM signing mode
                global is_softhsm_on
                is_softhsm_on = True

                # Extract the HSM key label from key name string
                # Support two formats:
                # 1. legacy: HSM_L4T (T234)
                # 2. new: HSM_L4T_<key_label>_<active_index> (T264)

                # Try new format first: HSM_L4T_<key_label>_<active_index>
                hsm_pattern = re.search(r'^' + KeyType.HSM_L4T + r'_([A-Z0-9]+)_(\d+)$', org_arg, re.IGNORECASE)
                if hsm_pattern:
                    index_value = int(hsm_pattern.group(2))
                    if index_value < 0 or index_value > 15:
                        raise tegrasign_exception('HSM L4T active_index out of range (0-15): %d in %s' % (index_value, arg))
                    self.l4t_key_label = hsm_pattern.group(1)  # Extract key_label (SIGN2)
                    self.active_index = hsm_pattern.group(2)   # Extract active_index (2)
                else:
                    # Try legacy format: HSM_L4T
                    hsm_legacy_pattern = re.search(r'^' + KeyType.HSM_L4T + r'$', arg)
                    if hsm_legacy_pattern:
                        self.l4t_key_label = KeyType.PKC  # Fixed key label "PKC" in legacy format
                        self.active_index = None   # No active index in legacy format
                    else:
                        raise tegrasign_exception('Invalid HSM L4T key name format: ' + arg + '. Expected: HSM_L4T or HSM_L4T_<key_label>_<index>')

                # Get the PKC key type from HSM directly
                # Support RSA3K, ECC P256 and P521 only
                from tegrasign_v3_softhsm import hsm_get_pkc_key_type
                pkc_key_type = hsm_get_pkc_key_type(self.l4t_key_label)
                info_print("[HSM] L4T HSM PKC key type: %s" % pkc_key_type)
                if pkc_key_type == 'RSA':
                    self.type = KeyType.PKC
                elif pkc_key_type == 'ECC':
                    self.type = KeyType.ECC
                elif pkc_key_type == 'ECC521':
                    self.type = KeyType.ECC521
                else:
                    raise tegrasign_exception('Unknown HSM PKC key type: ' + pkc_key_type)
            elif 'ALGO' in arg:
                self.algo_only = True
            elif 'REVOKE' in arg:
                self.revoke = True
            elif len(arg) == 4:
                self.magicid = arg
            else:
                raise tegrasign_exception('Unknown HSM type parsed: ' + arg)
        if self.magicid is not None and self.type is KeyType.UNKNOWN:
            raise tegrasign_exception('Unknown HSM type parsed: %s' %( ",".join(arg_list)))

    def get_type(self):
        return self.type

    def is_algo_only(self):
        return self.algo_only

    def is_fskp_mode(self):
        return self.type in [KeyType.FSKP_AK, KeyType.FSKP_EK, KeyType.FSKP_KDK, KeyType.FSKP]

class DeviceId:
    def __init__(self, arr = None):
        # In string format
        self.id = '0'
        self.major = '0'
        self.minor = '0'

    def parse(self, arg):
        try:
            # Possible formats: '<id><major>', '<id><major> <minor>', '<id> <major> <minor>'
            sublist = arg.strip().split(' ')
            if len(sublist) == 3:
                self.id = sublist[0]
                self.major = sublist[1]
                self.minor = sublist[2]
            elif len(sublist) == 2:
                self.id = sublist[0][:-1]
                self.major = sublist[0][-1]
                self.minor = sublist[1]
            elif len(sublist) == 1:
                self.id = sublist[0][:-1]
                self.major = sublist[0][-1]
            else:
                raise tegrasign_exception('Unknown \"%s\" parsed for chipid' %(arg))
        except Exception as e:
            raise tegrasign_exception('Unknown \"%s\" parsed for chipid due to %s' %(arg, e))

    def is_t234(self):
        return (self.id == '0x23' and self.major == '0')

    def is_t264(self):
        return self.id == '0x26'

    def chipid(self):
        return '%s%s' %(self.id, self.major)

    def chipid_all(self):
        return [self.id, self.major, self.minor]

class PCP:
   def __init__(self):
       self.deviceid = DeviceId()
       self.signed_xml_file = None # This is the finsihed file
       self.pcp_file = 'pcp.bin'
       self.pcps_hash_file = 'pcps_hash.bin'

class KDF:
    def __init__(self):
        self.iv = Token('00000000000000000000000000000000') #Set default value
        self.iv_off = Token()
        self.iv_sz = Token()
        self.aad = Token()
        self.aad_off = Token()
        self.aad_sz = Token()
        self.salt1_off = Token()
        self.salt1_sz = Token()
        self.salt2_off = Token()
        self.salt2_sz = Token()
        self.tag = Token()
        self.verify = 0
        self.label = Token()
        self.label_off = Token()
        self.label_sz = Token()
        self.bl_label = Token('0000000000000000')
        self.fw_label = Token('0000000000000000')
        self.tz_label = Token('0000000000000000')
        self.gp_label = Token('0000000000000000')
        self.context = Token('0000000000000000')
        self.iv_off = Token()
        self.tag_off = Token()
        self.pay_off = Token()
        self.pay_sz = Token()
        self.signed_pay_off = Token()
        self.signed_pay_sz = Token()
        self.signed_dgt_off = Token()
        self.dgt_off = Token()
        self.deviceid = DeviceId()
        self.dk = None
        self.magicid = None
        self.type = KdfType.CBC
        self.msg = None
        self.flag = DerKey.DEV
        self.der_key = None
        self.der_root = None
        self.enc = None
        self.bootmode = None
        self.key_already_derived = False
        self.rdm_iv = False
        self.rdm_label = False
        self.rdm_salt1 = False
        self.rdm_salt2 = False
        self.compress = False
        self.meta_blob_sz = 0
        self.integrity_chk = False
        self.res_sign_off = Token()
        self.res_sign_sz = Token()
        self.res_sign_enc_off = Token()

    def parse_file(self, p_key, kdf_file, internal = None):
        with open(kdf_file) as f:
            params = yaml.safe_load(f)

        tokens = {'IV':'--iv', 'AAD':'--aad', 'VER':None, 'DERSTR':None, 'CHIPID':None,
                  'MAGICID':None, 'FLAG':None, 'BL_DERSTR':None, 'FW_DERSTR':None,
                  'TZ_DERSTR':None, 'GP_DERSTR':None, 'DERKEY':None, 'DERROOT': None, 'PAYLOAD_OFF':None,
                  'PAYLOAD_SZ':None, 'DIGEST_OFF':None, 'TAG_OFF':None, 'ENC':None, 'BOOTMODE':None,
                  'IV_OFF':None, 'IV_SZ':None, 'RANDOM':None,
                  'AAD_OFF':None, 'AAD_SZ':None, 'DERSTR_OFF':None, 'DERSTR_SZ':None,
                  'SALT1_OFF':None, 'SALT1_SZ':None, 'SALT2_OFF':None, 'SALT2_SZ':None,
                  'SIGNED_PAYLOAD_OFF':None, 'SIGNED_PAYLOAD_SZ':None, 'SIGNED_DIGEST_OFF':None,
                  'COMPRESS':None, 'METABLOBSIZE':None, 
                  'INTEGRITY_CHK':None, 'RES_SIGN_OFF':None, 'RES_SIGN_SZ':None, 'RES_SIGN_ENC_OFF':None}
        for token in tokens:
            if token in params:
                # Update the entries if they are found in internal dict
                if internal != None and tokens.get(token) in internal:
                    internal[tokens.get(token)] = params.get(token)
                if token == 'AAD':
                    self.aad.parse(params.get(token))
                elif token == 'AAD_OFF':
                    self.aad_off.parse(params.get(token))
                elif token == 'AAD_SZ':
                    self.aad_sz.parse(params.get(token))
                elif token == 'CHIPID':
                    self.deviceid.parse(params.get(token))
                elif token == 'FLAG':
                    flag_val = params.get(token).upper()
                    if flag_val == 'DEV':
                        self.flag = DerKey.DEV
                    elif flag_val == 'SBK_PT':
                        self.flag = DerKey.SBK_PT
                    elif flag_val == 'SBK_WRAP':
                        self.flag = DerKey.SBK_WRAP
                    else:
                        raise tegrasign_exception('Unknown %s parsed from %s' %(flag_val, kdf_file))
                elif token == 'VER':
                    self.context.parse(params.get(token))
                elif token == 'IV':
                    if params.get(token).lower() == 'random': # Will generate random iv later
                        self.rdm_iv = True
                    else:
                        self.iv.parse(params.get(token))
                elif token == 'IV_SZ':
                    self.iv_sz.parse(params.get(token))
                elif token == 'DERKEY':
                    self.dk = params.get(token)
                elif token == 'DERROOT':
                    self.der_root = params.get(token)
                elif token == 'DERSTR':
                    entry = params.get(token)
                    # Check if the random directive has been set
                    if self.rdm_label == True:
                        self.label.parse(entry)
                    else:
                        self.label.parse(entry)
                elif token == 'DERSTR_OFF':
                    self.label_off.parse(params.get(token))
                elif token == 'DERSTR_SZ':
                    self.label_sz.parse(params.get(token))
                elif token == 'SALT1_OFF':
                    self.salt1_off.parse(params.get(token))
                elif token == 'SALT1_SZ':
                    self.salt1_sz.parse(params.get(token))
                elif token == 'SALT2_OFF':
                    self.salt2_off.parse(params.get(token))
                elif token == 'SALT2_SZ':
                    self.salt2_sz.parse(params.get(token))
                elif token == 'BL_DERSTR':
                    self.bl_label.parse(params.get(token))
                elif token == 'FW_DERSTR':
                    self.fw_label.parse(params.get(token))
                elif token == 'TZ_DERSTR':
                    self.tz_label.parse(params.get(token))
                elif token == 'GP_DERSTR':
                    self.gp_label.parse(params.get(token))
                elif token == 'DIGEST_OFF':
                    self.dgt_off.parse(params.get(token))
                elif token == 'IV_OFF':
                    self.iv_off.parse(params.get(token))
                elif token == 'PAYLOAD_OFF':
                    self.pay_off.parse(params.get(token))
                elif token == 'PAYLOAD_SZ':
                    self.pay_sz.parse(params.get(token))
                elif token == 'SIGNED_PAYLOAD_OFF':
                    self.signed_pay_off.parse(params.get(token))
                elif token == 'SIGNED_PAYLOAD_SZ':
                    self.signed_pay_sz.parse(params.get(token))
                elif token == 'SIGNED_DIGEST_OFF':
                    self.signed_dgt_off.parse(params.get(token))
                elif token == 'TAG_OFF':
                    self.tag_off.parse(params.get(token))
                elif token == 'MAGICID':
                    self.magicid = params.get(token)
                elif token == 'ENC':
                    self.enc = params.get(token)
                elif token == 'BOOTMODE':
                    self.bootmode = params.get(token)
                elif token == 'RANDOM':
                    rdm_list = params.get(token).upper()
                    self.rdm_iv = True if 'IV' in rdm_list else False
                    self.rdm_label = True if 'DERSTR' in rdm_list else False
                    self.rdm_salt1 = True if 'SALT1' in rdm_list else False
                    self.rdm_salt2 = True if 'SALT2' in rdm_list else False
                elif token == 'IV_OFF':
                    self.iv_off.parse(params.get(token))
                elif token == 'COMPRESS':
                    self.compress = True if params.get(token) == 'TRUE' else False
                elif token == 'METABLOBSIZE':
                    self.meta_blob_sz = params.get(token)
                elif token == 'INTEGRITY_CHK':
                    self.integrity_chk = params.get(token)
                elif token == 'RES_SIGN_OFF':
                    self.res_sign_off.parse(params.get(token))
                elif token == 'RES_SIGN_SZ':
                    self.res_sign_sz.parse(params.get(token))
                elif token == 'RES_SIGN_ENC_OFF':
                    self.res_sign_enc_off.parse(params.get(token))

    def parse(self, p_key, internal):
        kdf_arg = internal['--kdf']
        # Format: ['context=context.bin', 'label=label.bin']
        for arg in kdf_arg:
            arg_low = arg.lower()
            if 'context=' in arg_low:
                self.context.parse(arg)
            elif 'label=' in arg_low:
                self.label.parse(arg)
            elif 'kdf_file=' in arg_low:
                kdf_file = arg.split('=')[1]
                self.parse_file(p_key, kdf_file, internal)
            elif "key_already_derived=true" == arg_low:
                self.key_already_derived = True
            else:
                raise tegrasign_exception('Unknown argument parsed ' + arg)
        p_key.src_file = internal['--file']
        p_key.filename = internal['--key']
        p_key.mode = NvTegraSign_FSKP
        if internal['--hsm']:
            p_key.mode = p_key.hsm.type
        else:
            p_key.hsm.type == KeyType.UNKNOWN
        if internal['--sign'] == None:
            internal["--enc"] = 'aesgcm'
        self.type = KdfType.GCM
        p_key.key.aeskey = str_to_hex('0123456789abcdef0123456789abcdef') # Preset so not to skip enc when doing is_zero_aes check

    def get_hexmsg(self):
        return str_to_hex(self.msg)

    def get_composed_msg(self, L, hex_label, hex_context=True):
        fixed_msg = ''
        # Append Label
        if self.label.get_hexbuf() is not None:
            if hex_label is True:
                fixed_msg = self.label.get_hexbuf()
            else:
                fixed_msg = self.label.get_strbuf()
        # Append '00' byte
        fixed_msg += '00'
        # Append Context
        if len(self.context.get_hexbuf()) == 0:
            fixed_msg += '00'
        else:
            if hex_context is True:
                fixed_msg += self.context.get_hexbuf()
            else:
                fixed_msg += self.context.get_strbuf()
        # Append 'L' bit string (byte aligned)
        Lbits = '%08x' % L
        fixed_msg += Lbits if len(Lbits) % 2 == 0 else '0' + Lbits

        rlen = 32
        self.msg = compose_msg_data(CtrLoc.BEFORE, compose_ctr(rlen, 1), fixed_msg)

    # This is for getting aad buffer mius the iv buffer
    def get_aad_noiv(self):
        aad = self.aad.get_strbuf()
        iv = self.iv.get_strbuf()
        return aad[0:len(aad) - len(iv)]

class Key:
    def __init__(self):
        self.eckey = ECKey()
        self.edkey = EDKey()
        self.pkckey = PkcKey()
        self.aeskey = bytearray(16)

class Sha:
    _512 = 1
    _256 = 0

class DerKey:
    DEV            = '1'
    NV             = '2'
    DEVPDS         = '3'
    NVPDS          = '4'
    SBK_PT         = '5'
    SBK_WRAP       = '6'

class KdfArg:
    IV     = 0
    AAD    = 1
    TAG    = 2
    SRC    = 3
    FLAG   = 4
    DKSTR  = 5 # derivation string for DK
    DKVER  = 6 # version for DK
    BLSTR  = 7 # derivation label for BL_KDK
    FWSTR  = 8 # derivation label for FW_KDK
    TZSTR  = 9 # derivation label for TZ_KDK
    GPSTR  = 10# derivation label for GP_KDK

class RanArr:
    def __init__(self):
        self.count = 1 # Default to 1
        self.size = 0
        self.buf = None

class SignKey:
    def __init__(self):
        self.mode = "Unknown"
        self.filename = "Unknown"
        self.key = Key()
        self.keysize = 16
        self.kdf = KDF()
        self.hsm = HSM()
        self.pcp = PCP()
        self.ran = RanArr()
        self.len = 0
        self.off = 0
        self.pk_file = None
        self.src_file = None
        self.src_buf = None
        self.src_size = 0
        self.block_size = "0"

    def parse_random(self, opts, filename):
        if filename != None:
            self.filename = filename
        if len(opts) > 1:
            self.ran.count = int(opts[1])
        self.ran.size = int(opts[0])

    def parse_hsm(self, hsm, mode):
        self.hsm.parse(hsm, mode)

    def parse(self, src_file, length, offset, block_size):
        self.src_file = src_file
        self.block_size = block_size
        (self.src_buf, self.src_size, self.len, self.off) = check_len_off(src_file, length, offset)

    def validate_hsmmode(self):
        if (self.hsm.type == self.mode):
            return
        # SBK and KEK0 are considered the same mode
        if (self.hsm.type in [KeyType.SBK, KeyType.KEK0]) and (self.mode == NvTegraSign_SBK):
            return

        # FSKP* are considered the same mode
        if (self.hsm.type in [KeyType.FSKP_AK, KeyType.FSKP_EK, KeyType.FSKP_KDK, KeyType.FSKP]) and (self.mode == NvTegraSign_FSKP):
            return

        if (self.hsm.algo_only):
            return
        raise tegrasign_exception('HSM mode ' + self.hsm.type + ' specified does not match --key mode type ' + self.mode)

    def get_sign_buf(self):
        return self.src_buf[self.off : self.off + self.len]

class tegrasign_exception(Exception):
    def __init__(self, value):
        self.value = value

    def __str__(self):
        return repr(self.value)

AAD_0_96 = ('00' * 12)

def xor_hex(a, b):
    if len(a) > len(b):
        return "".join(["%x" % (int(x,16) ^ int(y,16)) for (x, y) in zip(a[:len(b)], b)])
    else:
        return "".join(["%x" % (int(x,16) ^ int(y,16)) for (x, y) in zip(a, b[:len(a)])])

def manifest_xor_offset(manifest, offset):
    if len(manifest) < len(offset):
        raise tegrasign_exception('Invalid Manifest length %d < Offset length %d ' % (len(manifest), len(offset)))
    return xor_hex(manifest[:len(offset)], offset) + manifest[len(offset):]

'''
Enumerate for Counter location: Before/Middle/After
'''
class CtrLoc:
    BEFORE = 1
    MIDDLE = 2
    AFTER = 3

def compose_ctr(rlen, i):
    if rlen == 8:
        return ('%02x' % i)
    elif rlen == 16:
        return ('%04x' % i)
    elif rlen == 24:
        return ('%06x' % i)
    else:
        return ('%08x' % i)

def compose_msg_data(order, cntr, data):
    if order == CtrLoc.BEFORE:
        return cntr + data
    elif order == CtrLoc.AFTER:
        return data + cntr
    else:
        raise tegrasign_exception('Counter location in the middle of input message is not implemented yet!')

def get_composed_msg(label, ctx, L, hex_label, hex_context=True):
    fixed_msg = ''
    # Append Label
    if label is not None:
        if hex_label is True:
            fixed_msg = label
        else:
            fixed_msg = literal_to_hex(label)
    # Append '00' byte
    fixed_msg += '00'
    # Append Context
    if len(ctx) == 0:
        fixed_msg += '00'
    else:
        if hex_context is True:
            fixed_msg += ctx
        else:
            fixed_msg += literal_to_hex(ctx) #ctx.encode('utf-8').hex()
    # Append 'L' bit string (byte aligned)
    Lbits = '%08x' % L
    fixed_msg += Lbits if len(Lbits) % 2 == 0 else '0' + Lbits

    rlen = 32
    return compose_msg_data(CtrLoc.BEFORE, compose_ctr(rlen, 1), fixed_msg)

cmd_environ = { }
start_time = time.time()
is_standalone = False
is_verbose = False
is_hsm_on = False
is_softhsm_on = False
script_dir= os.path.dirname(os.path.realpath(__file__)) + os.sep
bin_dir = script_dir
pid = str(os.getpid())

def isPython3():

    if sys.hexversion >= 0x3000000:
        return True

    return False

'''
To generate and return a bytearray of random numbers for the given count length
'''
def random_gen(count):
    # generate bytearray of random numbers for the given count length
    if is_hsm() and is_softhsm():
        internal = SignKey()
        internal.ran.size = count
        internal.ran.count = 1
        from tegrasign_v3_softhsm import do_random_hsm
        do_random_hsm(internal)
        return internal.ran.buf
    return os.urandom(count)


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
        print('%s' %(string))
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
            # In case if timestamp is already printed above, it's skipped here.
            if print_time == False:
                print('%s' %(string))
                print_time = True
            else:
                info_print(string)

    return log

def set_env(standalone, verbose, hsm = False, path = None, softhsm = False):
    global cmd_environ
    local_env = os.environ

    if standalone:
        local_env["PATH"] += os.pathsep + os.path.dirname(os.path.realpath(__file__))
    else:
        # Check to see if signing is invoked by GVS and set env var for XMSS
        if ('TEST_LOG_DIR' in local_env) and 'vrl' in local_env['TEST_LOG_DIR']:
            local_env['XMSS_SIGN_CHECK_KEY_PERMISSION'] = 'no'
    # Add the file location to the $PATH
    if 'PATH' in local_env:
        local_env['PATH'] += os.pathsep + os.path.dirname(os.path.realpath(__file__))
    cmd_environ = local_env

    global is_standalone
    is_standalone = standalone

    global is_verbose
    is_verbose = verbose

    global is_hsm_on
    is_hsm_on = hsm

    global is_softhsm_on
    is_softhsm_on = softhsm

    if path != None:
        global bin_dir
        bin_dir = path

def getEnviornmetalVariable(var):
    environVar = None
    try:
        environVar = os.environ[var]
    except:
        info_print("Environmental variable " + var + " not set")

    return environVar


'''
Returns the flag indicating HSM mode or not
'''
def is_hsm():
    global is_hsm_on
    return is_hsm_on

def is_softhsm():
    global is_softhsm_on
    return is_softhsm_on

def run_command(cmd, enable_print=True):

    log = ''
    if is_verbose == True:
        info_print(' '.join(cmd))

    use_shell = (sys.platform == 'win32')

    process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, shell=use_shell, env=cmd_environ)

    if enable_print == True:
        log = print_process(process, enable_print)
    return_code = process.wait()
    if return_code != 0:
        raise tegrasign_exception('Return value = ' + str(return_code) +
                ' . Command = ' + ' '.join(cmd))

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
Find the file in the searchable paths
'''
def search_file(filename):
    path_list = os.getenv("PATH").split(os.pathsep)
    for path in path_list:
        file_path = os.path.join(path, filename)
        if os.path.isfile(file_path):
            return file_path
    return filename

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
Executes the path of the binary that is found
'''
def exec_file(name):
    bin_name = name
    if sys.platform in {'win32', 'cygwin'}:
        bin_name = name + '.exe'

    if not os.path.isfile(bin_name):
        bin_name = bin_dir + bin_name
        if not os.path.isfile(bin_name):
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
    elif pKey.mode == NvTegraSign_ECC521:
        mode_str = 'ec521'
    elif pKey.mode == NvTegraSign_ED25519:
        mode_str = 'eddsa'
    elif pKey.mode == NvTegraSign_XMSS:
        mode_str = 'xmss'
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

'''
Convert hex string to hex array
'''
def str_to_hex(text):
    if(isPython3()):
        hex_arr = binascii.unhexlify(text.strip())
    else:
        hex_arr = text.strip().decode("hex")
    return hex_arr

'''
Convert literal to hex array
'''
def literal_to_hex(literal):
    if(isPython3()):
        hex_arr = literal.encode('utf-8').hex()
    else:
        hex_arr = literal.strip().encode("hex")
    return hex_arr

'''
Convert hex array to string
'''
def hex_to_str(arr):
    return binascii.hexlify(arr).decode("utf8")

'''
Check length and offset of the file
'''
def check_len_off(filename, length, offset):
    with open(filename, 'rb') as f:
        file_buf = bytearray(f.read())
        file_size = len(file_buf)

    if (type(length) == str):
        length = int(length)

    if (type(offset) == str):
        offset = int(offset)

    length = length if length > 0 else file_size - offset
    offset = offset if offset > 0 else 0

    if file_size < offset:
        length = 0
        info_print('Warning: Offset %d is more than file Size %d for %s' % (offset, file_size, filename))
        exit_routine()

    if (offset + length) > file_size:
        info_print('Warning: Offset %d + Length %d is greater than file Size %d for %s' % (offset, length, file_size, filename))
        exit_routine()

    return (file_buf, file_size, length, offset)

'''
Parse the --pubkeyhash arguments
'''
def get_pkh_args(internal):
    pk = None
    phk = None
    mode = None
    if (type(internal["--pubkeyhash"]) == list):
        len_ = 0 if (internal["--pubkeyhash"] == None) else len(internal["--pubkeyhash"])
        if len_ > 2:
            mode = internal["--pubkeyhash"][2]
        if len_ > 1:
            phk = internal["--pubkeyhash"][1]
        if len_ > 0:
            pk = internal["--pubkeyhash"][0]
    else:
        # Pass in as string, then only define for pk
        pk = internal["--pubkeyhash"]
    if mode != None and len(mode) > 0:
        mode_list = {'rsa': NvTegraSign_PKC, 'ecc': NvTegraSign_ECC, 'ecc521': NvTegraSign_ECC521,
            'eddsa': NvTegraSign_ED25519, 'xmss': NvTegraSign_XMSS}
        mode = mode_list.get(mode.lower(), None)

    return pk, phk, mode

'''
Prints the public key hash in tegra-fuse format
'''
def print_pcp(arr):
    arr_str = hex_to_str(arr)
    info_print('tegra-fuse format (big-endian): 0x%s' %(arr_str))

'''
Prints the public key hash in the fuse setting format for fusebypass
'''
def print_pcp_entries(arr, mode):
    arr_str = hex_to_str(arr)
    n = len(arr_str)

    if n % 4 != 0:
        return
    lines = ''
    entry_line = ''

    # Process 4 bytes at a time
    info_print('vdk fuse bypass format:')
    for i in range(0, int(n/8)):
        line = 'FUSE_PUBLIC_KEY%i=0x%s ' %(i, hex_to_str(swapbytes(bytearray(arr[i*4:(i+1)*4]))))

        lines += line
        entry_line += line
        if (i+1) %4 == 0:
            if mode == NvTegraSign_PKC:
                info_print('        self.pkc += " %s"' %(entry_line))
            elif mode == NvTegraSign_ECC:
                info_print('        self.ecdsa += " %s"' %(entry_line))
            elif mode == NvTegraSign_ED25519:
                info_print('        self.eddsa += " %s"' %(entry_line))
            elif mode == NvTegraSign_XMSS:
                info_print('        self.xmss += " %s"' %(entry_line))
            else:
                info_print('        self.pcp += " %s"' %(entry_line))
            entry_line = ''

    lines = ''
    info_print('fuse bypass format:')
    for i in range(0, int(n/8)):
        info_print('FAB_ENTRY(PUBLIC_KEY%i, PUBLIC_KEY%i, 0x%s),' %(i, i, hex_to_str(swapbytes(bytearray(arr[i*4:(i+1)*4])))))

    print('dts format: [',  end=' ')
    for i in range(0, int(n/2)):
        print(hex_to_str((bytearray(arr[i:(i+1)]))), end=' ')
    print("];")

# Generates unique key id based on
# in_key_id - string key ID ("SBK_BCT_KDK")
# in_label - derivation label (like "BCTB", "00000000000000000000000000000000")
#             to be used as input for the KDF. Type: string of hex bytes.
# in_context - derivation context (like "01010000", can be empty - "")
#               to be used as input for the KDF. Type: string of hex bytes.
# in_kdk - needed for DK key name generation
# Returns:
#     unq_in_key_id - md5sum(SBK_BCT_KDK_00000000000000000000000000000000_)
#                     = SBK_BCT_KDK_605fd4af08edf08a57dd3a86dee8b8ae"
def create_unique(in_key_id, in_label, in_context, in_kdk_stem = None):
    prefix = in_key_id
    if in_context == '':
        if in_kdk_stem == None:
            stem = '%s_%s_' %(in_key_id, in_label)
        else:
            stem = '%s_%s_%s_' %(in_key_id, in_kdk_stem, in_label)
    else:
        if in_kdk_stem == None:
            stem = '%s_%s_%s' %(in_key_id, in_label, in_context)
        else:
            stem = '%s_%s_%s_%s' %(in_key_id, in_kdk_stem, in_label, in_context)
    unq_in_key_id = '%s_%s' %(in_key_id, hashlib.md5(stem.encode('utf-8')).hexdigest())
    return unq_in_key_id

'''
   verify_opt: if = 1 to decrypt the buffer
                  = 0 means no verify/decryption involved
               if is a str type, indicades a file path, then decrypt the file and compare with original
'''
def do_aes_gcm(buff_to_enc, length, p_key, iv, aad, tag, verify_opt, verbose):
    if is_hsm():
        do_aes_gcm_hsm = import_function('do_aes_gcm_hsm')
        return do_aes_gcm_hsm(buff_to_enc, p_key, iv, aad)
    base_name = script_dir + 'v3_gcm_' + pid
    raw_name = base_name + '.raw'
    result_name = base_name + '.out'
    # check tag and verify_file are both defined
    if (type(tag) == int and type(verify_opt) == str):
        raise tegrasign_exception('--tag and --verify must both be specified')

    raw_file = open_file(raw_name, 'wb')

    key_bytes = len(binascii.hexlify(p_key.key.aeskey))/2
    keysize_bytes = int_2byte_cnt(p_key.keysize)
    len_bytes = int_2byte_cnt(length)
    enc_bytes = len(buff_to_enc)
    dest_bytes = int(length)
    result_bytes = len(result_name) + 1
    if (type(iv) == type(None)):
        iv_bytes = 0
    else:
        iv_bytes = len(binascii.hexlify(iv))/2
    if (isinstance(aad, int)):
        aad_bytes = 0
    else:
        aad_bytes = len(binascii.hexlify(aad))/2

    if (isinstance(tag, int)):
        tag_bytes = 0
    else:
        tag_bytes = len(binascii.hexlify(tag))/2

    if (type(verify_opt) == int):
        verify_bytes = verify_opt
    else:
        verify_bytes = len(verify_opt) + 1

    buff_dest = "0" * dest_bytes

    # to write to file
    # order: sizes then data for: key, length, buff_to_enc, buff_dest, result_name, iv, aad, tag, file

    num_list = [key_bytes, keysize_bytes, len_bytes, enc_bytes, dest_bytes, result_bytes, iv_bytes, aad_bytes, tag_bytes, verify_bytes]
    for num in num_list:
        arr = int_2bytes(4, num)
        write_file(raw_file, arr)

    write_file(raw_file, p_key.key.aeskey)
    arr = int_2bytes(keysize_bytes, p_key.keysize)
    write_file(raw_file, arr)
    arr = int_2bytes(len_bytes, length)
    write_file(raw_file, arr)
    write_file(raw_file, bytes(buff_to_enc))
    write_file(raw_file, buff_dest.encode("utf-8"))
    write_file(raw_file, result_name.encode("utf-8"))
    nullarr = bytearray(1)
    nullarr[0] = 0          # need this null for char*
    write_file(raw_file, nullarr)
    if iv_bytes > 0:
        write_file(raw_file, bytes(iv))
    if aad_bytes > 0:
        write_file(raw_file, bytes(aad))
    if tag_bytes > 0:
        write_file(raw_file, bytes(tag))
    if verify_bytes > 0:
        write_file(raw_file, verify_opt.encode("utf-8"))
        nullarr = bytearray(1)
        nullarr[0] = 0          # need this null for char*
    raw_file.close()

    command = exec_file(TegraOpenssl)
    command.extend(['--aesgcm', raw_name])
    if (verbose != 0):
        command.extend(['--verbose'])

    ret_str = run_command(command)
    if (isinstance(tag, int) == False) and (type(verify_opt) == str):
        info_print ('********* Verification complete. Quitting. *********')
        sys.exit(1)

    else:
        if check_file(result_name):
            result_fh = open_file(result_name, 'rb')
            buff_sig = result_fh.read() # Return data to caller
            result_fh.close()
            os.remove(result_name)
        start = ret_str.find('tag')
        tag_str_len = 4;
        if (start > 0) and not (isinstance(tag, int)):
            if tag_bytes > 0:
                end = start + tag_str_len + int(tag_bytes * 2)
                tag[:] = str_to_hex(ret_str[start+tag_str_len:end])
            else:
                end = len(ret_str)
            p_key.kdf.tag.set_buf(str_to_hex(ret_str[start+tag_str_len:end]))
    os.remove(raw_name)
    return buff_sig

'''
Dynamic loading function from tegrasign_v3_hsm.py or tegrasign_v3_softhsm.py
'''
def import_function(function_name):
    if is_hsm() and is_softhsm():
        module_name = 'tegrasign_v3_softhsm'
    elif is_hsm() and not is_softhsm():
        module_name = 'tegrasign_v3_hsm'
    else:
       raise tegrasign_exception('Error: cannot import HSM function since HSM feature is not enabled')

    module = __import__(module_name, fromlist=[function_name])
    return getattr(module, function_name)
