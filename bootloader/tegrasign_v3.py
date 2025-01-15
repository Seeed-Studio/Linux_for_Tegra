#!/usr/bin/env python
#
# Copyright (c) 2018-2020, NVIDIA Corporation.  All Rights Reserved.
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA Corporation is strictly prohibited.
#

import argparse
import traceback
from tegrasign_v3_internal import *
from tegrasign_v3_util import *


def compute_sha(filename, offset, length):
    do_sha256(filename, offset, length)

def print_help():
    parser = argparse.ArgumentParser()
    parser.print_help()


def parse_cmdline(commandLine):
    ''' Parse command-line args. The argument order is important
    '''
    # Parse command-line arguments
    parser = argparse.ArgumentParser()
    parser.add_argument("--file",   help="specify a file containing data to be signed")
    parser.add_argument("--getmode",help="Print mode in file if given else on console", action='append', nargs='?')
    parser.add_argument("--getmontgomeryvalues", help="Save montgomery values")
    parser.add_argument("--key",    help="specify a file containing key", nargs='+')
    parser.add_argument("--length", help="specify the length of the data to be signed or omit to specify the entire data", default=0)
    parser.add_argument("--list",   help="specify a XML file that contains a list of files to be signed")
    parser.add_argument("--offset", help="specify the start of the data to be signed", default=0)
    parser.add_argument("--pubkeyhash", help="specify the file to save public key hash")
    parser.add_argument("--sha",    help="Compute sha hash for sha256")
    parser.add_argument("--skip_encryption", help="skip encryption in case of non-zero sbk", action='store_true')
    parser.add_argument("--verbose", help="Print verbose information", action='store_true')

    # print help if the # of args == 1
    if not len(commandLine) > 1:
        print_help()
        return False

    args = parser.parse_args(commandLine[1:])
    return args

'''
If pkh is filename and not 'None', then the pub key hash file will be generated
If mont is filename and not 'None', then the montgomery value file will be generated for rsa3k
'''
def extract_key(p_key, keyfilename, pkh, mont):

    if keyfilename == 'None' or keyfilename == None:
        p_key.mode = NvTegraSign_SBK
        info_print('Assuming zero filled SBK key')
        return 1

    else :
        try:
            key_fh = open(keyfilename, 'rb')
            key_buf = key_fh.read()
            BufSize = len(key_buf)

        except IOError:
            p_key.mode = NvTegraSign_SBK
            info_print('Assuming zero filled SBK key : not reading ' + keyfilename)
            return 1

        if extract_AES_key(key_buf, BufSize, p_key):
            p_key.filename = keyfilename
            return 1

        if is_PKC_key(keyfilename, p_key, pkh, mont) is True:
           p_key.mode = NvTegraSign_PKC
           p_key.filename = keyfilename
           return 1

        if is_ECC_key(keyfilename, p_key, pkh) is True:
           p_key.mode = NvTegraSign_ECC
           p_key.filename = keyfilename
           return 1

        if is_ED25519_key(keyfilename, p_key, pkh) is True:
           p_key.mode = NvTegraSign_ED25519
           p_key.filename = keyfilename
           return 1

    info_print('Invalid key format')
    return 0

'''
mode_val can be 'None' or file path to be created
If it's None, print the mode string to console
If it's file path, save the string to file
'''
def get_mode(mode_val, p_key):
    mode_str = get_mode_str(p_key, True)

    if mode_val[0] == None:
        info_print(mode_str)

    else:
        mode_file = ''.join(mode_val)
        mode_fh = open_file(mode_file, 'wb')

        if mode_fh:
            write_file(mode_fh, bytes(mode_str.encode("utf-8")))
            mode_fh.close()
        else:
            info_print('Cannot open %s for writing' %(mode_file))

'''
Since the public key hash file is created in do_rsa_pss/do_ecc() or is_pkc/ecc()
Here we only check for file existance and prints warning if not found
'''
def save_public_key_hash(p_key, filename):
    if (p_key.mode == NvTegraSign_PKC):
        if check_file(filename):
            info_print('Saving pkc public key in ' + filename)

    elif (p_key.mode == NvTegraSign_ECC or p_key.mode == NvTegraSign_ED25519):
        if check_file(filename):
            info_print('Saving public key in ' + filename)


'''
Since the montgomery values file is created in do_rsa_pss()
Here we only check for file existance and prints warning if not found
'''
def save_montgomery_values(p_key, filename):
    if p_key.mode == NvTegraSign_PKC and p_key.keysize >= 384:
        if check_file(filename):
            info_print('Saving Montgomery values in ' + filename)

'''
Print the arguments if the invocation is not done by standalone
'''
def print_args(file_val, mode_val, mont_val, key_val, len_val, list_val, \
        offset_val, pkh_val, sha_val, skip_enc_val):

    if __name__=='__main__':
        return

    try:
        str = 'tegrasign_v3.py'
        if file_val:
            str += ' --file ' + file_val
        if mode_val:
            # check to see if it's a list
            if isinstance(mode_val, list):
                if mode_val[0]:
                    str += ' --getmode ' + mode_val[0]
                else:
                    str += ' --getmode'
            else:
                str += ' --getmode ' + mode_val
        if mont_val:
            str += ' --getmontgomeryvalues ' + mont_val
        if key_val:
            if isinstance(key_val, list):
                str += ' --key ' + ' '.join(key_val)
            else:
                str += ' --key ' + key_val
        if len_val:
            str += ' --length ' + len_val
        if list_val:
            str += ' --list ' + list_val
        if offset_val:
            str += ' --offset ' + offset_val
        if pkh_val:
            str += ' --pubkeyhash ' + pkh_val
        if sha_val:
            str += ' --sha ' + sha_val
        if skip_enc_val:
            str += ' --skip_encryption'

        info_print(str)

    except Exception as e:
        info_print('Encounter exception when printing argument list:' + e.message)

def tegrasign(args_file, args_getmode, args_getmont, args_key, args_length, args_list, args_offset, args_pubkeyhash, args_sha, args_skip_enc, args_verbose=False):

    print_args(args_file, args_getmode, args_getmont, args_key, args_length, args_list, args_offset, args_pubkeyhash, args_sha, args_skip_enc)

    set_env(__name__=='__main__', args_verbose)

    try:
        if args_key:
            is_key_list = True

            if isinstance(args_key, list):
                keyfile_count = len(args_key)
            else:
                is_key_list = False
                keyfile_count = 1

            # Check key count
            if (keyfile_count > MAX_KEY_LIST):
                info_print('--key has ' + str(len(args_key)) + ' arguments which exceeds ' + str(MAX_KEY_LIST))
                exit_routine()

            p_keylist = [ SignKey() for i in range(keyfile_count)]

            # Extract each key only if it is in a list
            if is_key_list:
                for i in range(keyfile_count):
                    if extract_key(p_keylist[i], args_key[i], args_pubkeyhash, args_getmont) == 0:
                        exit_routine()
            else:
                if extract_key(p_keylist[0], args_key, args_pubkeyhash, args_getmont) == 0:
                    exit_routine()

            # Check key mode is the same for all keys
            for i in range(1, keyfile_count):
                if p_keylist[i].mode != p_keylist[i-1].mode:
                    info_print('key[' + str(i) + '].mode = ' + p_keylist[i].mode + ' which does not match key['+ str(i-1) + '].mode = ' + p_keylist[i-1].mode)
                    exit_routine()

            if args_getmode:
                get_mode(args_getmode, p_keylist[0])

            if args_list:
                sign_files_in_list(p_keylist, args_list, args_pubkeyhash, args_getmont)

            elif args_file:
                length = -1
                offset = 0
                skip_enc = 0
                do_sign = True

                if args_length:
                    length = int(args_length)

                if args_offset:
                    offset = int(args_offset)

                if args_skip_enc:
                    skip_enc = 1

                sign_single_file(p_keylist[0], args_file, offset, length, skip_enc, do_sign, args_pubkeyhash, args_getmont)

            #else:
            #    print_help()

            if args_pubkeyhash:
                save_public_key_hash(p_keylist[0], args_pubkeyhash)

            if args_getmont:
                save_montgomery_values(p_keylist[0], args_getmont)

        else:
            if args_sha == 'sha256':
                length = -1
                offset = 0
                skip_enc = 0

                if args_length:
                    length = int(args_length)

                if args_offset:
                    offset = int(args_offset)

                if args_file:
                    compute_sha(args_file, offset, length)
                else:
                    print_help()

            else:
                print_help()
        # This is confusing since they are mixing booleans and integrers
        # but if we got here it is a success return 0
        return 0
    except Exception as e:
        print(traceback.format_exc())
        info_print('Encounter exception when signing: ' + str(e))
        exit_routine()

'''
Argument List Order:
--file
--getmode : can be a list: [None] or ['file'] or string: mode.txt
--getmontgomeryvalues
--key     : can be a list of files or a string
--length
--list
--offset
--pubkeyhash
--sha
--skip_encryption
--verbose : optional, this is not enabled from tegraflash, standalone can be enabled
'''
if __name__=='__main__':

    args = parse_cmdline(sys.argv)

    if not args is False:
        tegrasign(args.file, args.getmode, args.getmontgomeryvalues, args.key, args.length, args.list, args.offset, args.pubkeyhash, args.sha, args.skip_encryption, args.verbose)
