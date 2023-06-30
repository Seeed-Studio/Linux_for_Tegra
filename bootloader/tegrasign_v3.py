#!/usr/bin/env python3
#
# Copyright (c) 2018-2023, NVIDIA Corporation.  All Rights Reserved.
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA Corporation is strictly prohibited.
#

import argparse
from tegrasign_v3_internal import *
from tegrasign_v3_util import *

parser = {};

internal = {
            "--file"   : None,
            "--getmode": None,
            "--getmontgomeryvalues" : None,
            "--key"    : None,
            "--length" : None,
            "--list"   : None,
            "--offset" : None,
            "--pubkeyhash": None,
            "--sha"    : None,
            "--enc"    : None,
            "--iv"     : None,
            "--aad"    : None,
            "--tag"    : None,
            "--sign"   : None,
            "--verify" : None,
            "--verbose": None,
            "--random" : None,
            "--block"  : "0"
          }

def clear_internal():
    internal["--file"] = None
    internal["--getmode"] = None
    internal["--getmontgomeryvalues"] = None
    internal["--key"] = None
    internal["--length"] = None
    internal["--list"] = None
    internal["--offset"] = None
    internal["--pubkeyhash"] = None
    internal["--sha"] = None
    internal["--enc"] = None
    internal["--iv"] = None
    internal["--aad"] = None
    internal["--tag"] = None
    internal["--sign"] = None
    internal["--verify"] = None
    internal["--verbose"] = None
    internal["--block"] = "0"


def print_help():
    parser.print_help()


def parse_cmdline(commandLine):
    ''' Parse command-line args. The argument order is important
    '''
    # Parse command-line arguments
    global parser
    parser = argparse.ArgumentParser()
    parser.add_argument("--file",   help="Specify a file containing data to be signed")
    parser.add_argument("--getmode",help="Print mode in file if given else on console", action='append', nargs='?', metavar='FILE')
    parser.add_argument("--getmontgomeryvalues", help="Save montgomery values to file", metavar='FILE')
    parser.add_argument("--hsm",    help="Invoke HSM-mode call with key type specified: [ |sbk|kek0|fskp_ak|fskp_ek|fskp_kdk|fskp|rsa|eddsa|algo]", nargs='*', metavar='HSM')
    parser.add_argument("--iv",     help="Specify iv for cipher aescbc, aesgcm by using 'random', file containing IV, or hex string without '0x'.", metavar='IV', default=None)
    parser.add_argument("--kdf",    help="Perform KDF and encryption. <label=label.bin> <context=context.bin> <kdf_file=kdf.yaml> <key_already_derived=boolean>",  nargs='+', metavar='KDF')
    parser.add_argument("--key",    help="Specify a file containing key", metavar='FILE')
    parser.add_argument("--length", help="Specify the length of the data to be signed or omit to specify the entire data", default=0)
    parser.add_argument("--list",   help="Specify a XML file that contains a list of files to be signed", metavar='FILE',)
    parser.add_argument("--offset", help="Specify the start of the data to be signed", default=0)
    parser.add_argument("--pubkeyhash", help="Specify the files to save public key and hash. 1) <public key> [hash file] when --key <private key> is used. 2) <public key> <hash file> <rsa|eddsa> when --key <private key> is not used. Note in this case the public key is specific format and the key type flag is required", metavar='FILE', nargs='*')
    parser.add_argument("--sha",    help="Compute sha hash for sha256 or sha512", choices=['sha256', 'sha512'])
    parser.add_argument("--enc", help="Set encryption, or skip in case of non-zero sbk", choices=[None, 'skip', 'aescbc', 'aesgcm'])
    parser.add_argument("--aad", help="Specify aad for aesgcm using 'random' or hex string without '0x'", metavar='AAD', default=0)
    parser.add_argument("--tag", help="Specify tag, hex string without '0x', for aesgcm verification. Use with '--verify'", metavar='TAG', default=0)
    parser.add_argument("--sign",    help="Sign using aescmac or hmacsha256", choices=['aescmac', 'hmacsha256'])
    parser.add_argument("--verify", help="Specify the file containing data to be verified. Current support is aesgcm only", metavar='VERIFY', default=0)
    parser.add_argument("--verbose", help="Print verbose information", action='store_true')
    parser.add_argument("--random", help="Generate random strings of <byte_size><count> and save to --file <file>", nargs='+')
    parser.add_argument("--block", help="Perform encryption or SHA in blocks of specified size", default="0")

    # print help if the # of args == 1
    if not len(commandLine) > 0:
        print_help()
        return False

    args = parser.parse_args(commandLine)
    return args

'''
If pk is filename and not 'None', then the pub key file will be generated
If mont is filename and not 'None', then the montgomery value file will be generated for rsa3k
'''
def extract_key(p_key, keyfilename, internal):

    pk, _, _ = get_pkh_args(internal)

    mont = internal["--getmontgomeryvalues"]

    # Handles 'None' first
    if keyfilename == 'None':
        if (p_key.hsm.type == KeyType.FSKP):
            p_key.mode = NvTegraSign_FSKP
            p_key.key.aeskey = bytearray(16) # not supporting 32B
            info_print('Assuming zero filled SBK key')
        else:
            p_key.mode = NvTegraSign_SBK
            p_key.key.aeskey = bytearray(16)
            info_print('Assuming zero filled SBK key')
        p_key.filename = keyfilename
        return 1
    # Handle HSM route next
    elif p_key.hsm.type != KeyType.UNKNOWN:
        p_key.filename = keyfilename

        if p_key.hsm.type in [KeyType.SBK, KeyType.KEK0]:
            p_key.mode = NvTegraSign_SBK
            return 1

        elif p_key.hsm.is_fskp_mode():
            p_key.mode = NvTegraSign_FSKP
            return 1

        elif is_PKC_key(keyfilename, p_key, pk, mont) is True:
           p_key.mode = NvTegraSign_PKC
           return 1

        # Note: HSM for ECC does not exist, so native route is used
        if is_ECC_key(keyfilename, p_key, pk) is True:
           p_key.mode = NvTegraSign_ECC
           return 1

        elif is_ED25519_key(keyfilename, p_key, pk) is True:
           p_key.mode = NvTegraSign_ED25519
           return 1

    else:
        try:
            key_fh = open(keyfilename, 'rb')
            key_buf = key_fh.read()
            BufSize = len(key_buf)
            p_key.filename = keyfilename
            key_fh.close()

        except IOError:
            p_key.mode = NvTegraSign_SBK
            info_print('Assuming zero filled SBK key : not reading ' + keyfilename)
            return 1

        if extract_AES_key(key_buf, p_key):
            p_key.filename = keyfilename
            return 1

        if is_PKC_key(keyfilename, p_key, pk, mont) is True:
           p_key.mode = NvTegraSign_PKC
           p_key.filename = keyfilename
           return 1

        if is_ECC_key(keyfilename, p_key, pk) is True:
           p_key.mode = NvTegraSign_ECC
           p_key.filename = keyfilename
           return 1

        if is_ED25519_key(keyfilename, p_key, pk) is True:
           p_key.mode = NvTegraSign_ED25519
           p_key.filename = keyfilename
           return 1

        if is_xmss_key(keyfilename, p_key, pk) is True:
           p_key.mode = NvTegraSign_XMSS
           p_key.filename = keyfilename
           return 1

    info_print('Invalid key format')
    return 0

'''
Based on the mode passed in, parse the public key of the expected format
    For RSA: openssl pem format which starts with 'Modulus='
    For EDDSA: openssl der format
and create tegra-style pubkey file for save_public_key_hash() usage
'''
def extract_pubkey(p_key, internal):
    pk, pkh, mode = get_pkh_args(internal)
    p_key.mode = mode
    p_key.pk_file = pk + '_v3'
    pk_filesize = os.path.getsize(pk)

    # Parse public key format
    if p_key.mode == NvTegraSign_PKC:
        if (pk_filesize == RSA3K_KEY_SIZE):
            # Assume the pk passed in is from tegrasign
            p_key.pk_file = pk
            return 1
        # Note: pubkey.pem format expected is created from:
        # openssl rsa -in rsa_priv_3k_pem.pem -pubout -modulus  > pubkey.pem
        # So modulus info can be extracted
        with open(pk, 'r') as fr, open(p_key.pk_file, 'wb') as fw:
            lines = fr.read()[len('Modulus='):]
            lines = lines.split('\n')[0]
            pub_bytes = swapbytes(bytearray(str_to_hex(lines)))
            fw.write(pub_bytes)

    elif p_key.mode == NvTegraSign_ED25519:
        if (pk_filesize == ED25519_KEY_SIZE):
            # Assume the pk passed in is from tegrasign
            p_key.pk_file = pk
            return 1

        # Note: pubkey.der format expected is created from:
        # openssl pkey -in edopenssl_v3.pem  -pubout -outform DER  > pubkey.der
        with open(pk, 'rb') as fr, open(p_key.pk_file, 'wb') as fw:
            lines = bytearray(fr.read())
            pub_bytes = lines[12:]
            fw.write(pub_bytes)
    else:
        info_print('Non-supported key format: %s' %(pk))
        return 0

    return 1
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
Since the public key file is created in do_rsa_pss/do_ecc() or is_pkc/ecc()
Here we only check for file existance and prints warning if not found
'''
def save_public_key(p_key, internal):
    pk, _, _ = get_pkh_args(internal)
    if check_file(pk):
        if p_key.mode == NvTegraSign_PKC:
            info_print('Saving pkc public key in ' + pk)
        else:
            info_print('Saving public key in ' + pk + ' for ' + p_key.mode)

'''
The public key file is created by tegrasign_v3.py or passed in via cmd argument
of the known format, see extract_pubkey() for format
Compute and print the hash value
'''
def save_public_key_hash(p_key, internal):
    pk, pkh, mode = get_pkh_args(internal)
    if pkh == None:
        return 1

    if p_key.pk_file != None:
        pk = p_key.pk_file
    pcp_file = 'pcp.bin'
    with open(pk, 'rb') as fr, open(pcp_file, 'wb') as fw:
        pub_bytes = bytearray(fr.read())
        keysize = len(pub_bytes)
        buf = bytearray(PCP_SIZE)
        buf[0:keysize] = pub_bytes[:]
        fw.write(buf)
    if check_file(pcp_file) == False:
        return 0

    sha_file = compute_sha('sha512', pcp_file, 0, PCP_SIZE)
    os.rename(sha_file, pkh)
    with open(pkh, 'rb') as f:
        sha_bytes = f.read()
        print_pcp(sha_bytes)
        if internal['--verbose']:
            print_pcp_entries(sha_bytes, p_key.mode)
        os.remove(pcp_file)
        return 1
    return 0
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
def print_args(internal):

    if __name__=='__main__':
        return

    try:
        argstr = 'tegrasign_v3.py'
        if internal["--aad"] != 0:
            argstr += ' --aad ' + internal["--aad"]
        if internal["--enc"]:
            argstr += ' --enc ' + internal["--enc"]
        if internal["--iv"] != 0:
            argstr += ' --iv ' + internal["--iv"]
        if internal["--file"]:
            argstr += ' --file ' + internal["--file"]
        if internal["--key"]:
            if isinstance(internal["--key"], list):
                argstr += ' --key ' + ' '.join(internal["--key"])
            else:
                argstr += ' --key ' + internal["--key"]
        if internal["--length"]:
            argstr += ' --length ' + internal["--length"]
        if internal["--list"]:
            argstr += ' --list ' + internal["--list"]
        if internal["--kdf"]:
            argstr += ' --kdf ' + ' '.join(internal["--kdf"])

        if internal["--getmode"]:
            # check to see if it's a list
            if isinstance(internal["--getmode"], list):
                if internal["--getmode"][0]:
                    argstr += ' --getmode ' + internal["--getmode"][0]
                else:
                    argstr += ' --getmode '
            else:
                argstr += ' --getmode ' + internal["--getmode"]
        if internal["--getmontgomeryvalues"]:
            argstr += ' --getmontgomeryvalues ' + internal["--getmontgomeryvalues"]
        if internal["--offset"]:
            argstr += ' --offset ' + internal["--offset"]
        if internal["--pubkeyhash"]:
            argstr += ' --pubkeyhash ' + internal["--pubkeyhash"]
        if internal["--sha"]:
            argstr += ' --sha ' + internal["--sha"]
        if internal["--sign"] != None:
            argstr += ' --sign ' + internal["--sign"]
        if internal["--tag"] != 0:
            argstr += ' --tag ' + internal["--tag"]
        if internal["--verify"] != 0:
            argstr += ' --verify ' + internal["--verify"]
        info_print(argstr)

    except Exception as e:
        info_print('Encounter exception when printing argument list')
        info_print(e.message)

def tegrasign(args_file, args_getmode, args_getmont, args_key, args_length, args_list, args_offset, args_pubkeyhash, args_sha, args_enc, args_verbose=False, args_iv=0, args_aad=0, args_tag=0, args_sign=None, args_verify=0, args_kdf=None, args_hsm=None, args_ran=None, args_block='0'):

    internal["--file"] = args_file
    internal["--getmode"] = args_getmode
    internal["--getmontgomeryvalues"] = args_getmont
    internal["--key"] = args_key
    internal["--length"] = args_length
    internal["--list"] = args_list
    internal["--offset"] = args_offset
    internal["--pubkeyhash"] = args_pubkeyhash
    internal["--sha"] = args_sha
    internal["--enc"] = args_enc
    internal["--iv"] = args_iv
    internal["--aad"] = args_aad
    internal["--tag"] = args_tag
    internal["--sign"] = args_sign
    internal["--verify"] = args_verify
    internal["--verbose"] = args_verbose
    internal["--kdf"] = args_kdf
    internal["--hsm"] = args_hsm
    internal["--random"] = args_ran
    internal["--block"] = args_block

    print_args(internal)

    set_env(__name__=='__main__', args_verbose, args_hsm != None, None)

    try:
        is_kdf_file = (internal["--kdf"] != None) and ('kdf_file' in ''.join(internal["--kdf"]).lower())

        if internal["--key"] or internal["--hsm"] or is_kdf_file:
            is_key_list = True

            if isinstance(internal["--key"], list):
                keyfile_count = len(internal["--key"])
            else:
                is_key_list = False
                keyfile_count = 1

            # Check key count
            if (keyfile_count > MAX_KEY_LIST):
                info_print('--key has ' + str(len(internal["--key"])) + ' arguments which exceeds ' + str(MAX_KEY_LIST))
                return exit_routine()

            p_keylist = [ SignKey() for i in range(keyfile_count)]

            # Extract each key only if it is in a list
            if is_key_list:
                for i in range(keyfile_count):
                    if extract_key(p_keylist[i], internal["--key"][i], internal) == 0:
                        return exit_routine()
            else:
                if internal["--hsm"]:
                    # Set the hsm type before extracting keys
                    p_keylist[0].parse_hsm(internal["--hsm"], KeyType.HSM)
                if internal["--kdf"]:
                    p_keylist[0].kdf.parse(p_keylist[0], internal)
                if not is_kdf_file and (extract_key(p_keylist[0], internal["--key"], internal) == 0):
                    return exit_routine()

                if not internal["--hsm"]:
                    # After extracting key, set the key types that have --key provided
                    p_keylist[0].parse_hsm(internal["--hsm"], p_keylist[0].mode)
                    if args_key and (p_keylist[0].hsm.type != KeyType.HSM):
                        # It is possible that the right keytype is found, but not with the intended key path, under HSM mode
                        # so it is best that we extract again with right key path to obtain correct info before moving on
                        if extract_key(p_keylist[0], internal["--key"], internal) == 0:
                            return exit_routine()
                p_keylist[0].validate_hsmmode()

            # Check key mode is the same for all keys
            for i in range(1, keyfile_count):
                if p_keylist[i].mode != p_keylist[i-1].mode:
                    raise tegrasign_exception('key[' + str(i) + '].mode = ' + p_keylist[i].mode + ' which does not match key[' \
                        + str(i-1) + '].mode = ' + p_keylist[i-1].mode)

            if internal["--getmode"]:
                get_mode(internal["--getmode"], p_keylist[0])

            if internal["--sha"]:
                # Change to boolean for passing into API
                if internal["--sha"] == 'sha512':
                    internal["--sha"] = Sha._512
                else:
                    internal["--sha"] = Sha._256
            else:
                # Set Sha256 as the default mode
                internal["--sha"] = Sha._256

            if (type(internal["--iv"]) == str):
                if (internal["--iv"] == 'random'):
                    internal["--iv"] = random_gen(16);
                    info_print("--iv %s" %(hex_to_str(internal["--iv"])))
                    # Store the iv to file
                    if internal["--file"]:
                        iv_file = os.path.splitext(internal["--file"])[0] + '.iv'
                    else:
                        iv_file = 'random.iv'
                    iv_fh = open_file(iv_file, 'wb')
                    if iv_fh:
                        write_file(iv_fh, bytes(internal["--iv"]))
                        iv_fh.close()
                        p_keylist[0].kdf.iv.read(iv_file, ReadFlag.IGNORE)
                    else:
                        info_print('Cannot open %s for writing' %(iv_file))
                        return exit_routine()
                elif os.path.exists(internal["--iv"]):
                    # Assume passing in iv in file
                    p_keylist[0].kdf.iv.read(internal["--iv"], ReadFlag.IGNORE)
                    iv_fh = open_file(internal["--iv"], 'rb')
                    internal["--iv"] = iv_fh.read()
                    iv_fh.close()
                else:
                    temp = str_to_hex(internal["--iv"])
                    internal["--iv"] = bytearray(temp) #convert str to bytearray
                    p_keylist[0].kdf.iv.set_buf(internal["--iv"])

            if (type(internal["--aad"]) == str):
                if (internal["--aad"] == 'random'):
                    internal["--aad"] = random_gen(16);
                    info_print("--aad %s" %(binascii.hexlify(internal["--aad"])))
                else:
                    temp = str_to_hex(internal["--aad"])
                    internal["--aad"] = bytearray(temp) #convert str to bytearray
                p_keylist[0].kdf.aad.set_buf(internal["--aad"])

            if (type(internal["--tag"]) == str):
                temp = str_to_hex(internal["--tag"])
                internal["--tag"] = bytearray(temp) #convert str to bytearray
            elif (type(args_tag) == int):
                internal["--tag"] = bytearray(16)
            p_keylist[0].kdf.tag.set_buf(internal["--tag"])
            if internal["--verify"] != 0:
                p_keylist[0].kdf.verify = internal["--verify"]

            if internal["--list"]:
                # This shall generate signatures and update input xml list.
                retVal = sign_files_in_list(p_keylist, internal)
                if retVal !=0:
                    return retVal

            elif internal["--file"]:
                length = -1
                offset = 0
                if (internal["--sign"] == None):
                    internal["--sign"] = 'aescmac'
                    if (internal["--enc"] == None):
                        internal["--enc"] = 'aescbc' #set as default only if mac is not defined


                if internal["--length"]:
                    length = int(internal["--length"])

                if internal["--offset"]:
                    offset = int(internal["--offset"])
                internal["--length"] = length
                internal["--offset"] = offset

                p_keylist[0].parse(internal["--file"], internal["--length"], internal["--offset"], internal["--block"])

                if internal["--kdf"]:
                    kdf_arg_len = len(internal["--kdf"])
                    if (kdf_arg_len == 1):
                        # kdf_list = [iv1, aad1, tag1, src, DerKey.DEV, der_str, ver, psc_bl, psc_fw]
                        with open(p_keylist[0].src_file, 'rb') as f:
                            org_src = bytearray(f.read())
                            src = org_src[p_keylist[0].off:p_keylist[0].off+p_keylist[0].len]

                        kdf_list = [p_keylist[0].kdf.iv.get_hexbuf(), p_keylist[0].kdf.aad.get_hexbuf(), p_keylist[0].kdf.tag.get_hexbuf(), \
                                    src, p_keylist[0].kdf.flag, p_keylist[0].kdf.label.get_hexbuf(), p_keylist[0].kdf.context.get_hexbuf(), \
                                    p_keylist[0].kdf.bl_label.get_hexbuf(), p_keylist[0].kdf.fw_label.get_hexbuf()]
                        if (do_key_derivation(p_keylist[0], kdf_list, internal["--block"]) != True):
                            return exit_routine()
                        if (p_keylist[0].kdf.enc == 'OEM' or p_keylist[0].kdf.enc == 'USER_KDK'):
                            return True
                        fileNm, fileExt = os.path.splitext(p_keylist[0].src_file)
                        enc_file = fileNm + '_encrypt' + fileExt
                        tag_file = fileNm + '.tag'
                        with open(tag_file, 'wb') as f:
                            f.write(kdf_list[KdfArg.TAG][:])
                        with open(enc_file, 'wb') as f:
                            org_src[p_keylist[0].off:p_keylist[0].off+p_keylist[0].len] = kdf_list[KdfArg.SRC][:]
                            f.write(org_src)
                            return True

                    elif (kdf_arg_len >= 2):
                        if internal["--enc"] == 'aesgcm':
                            return do_derive_aesgcm(p_keylist[0], internal)
                        elif internal["--sign"] == 'hmacsha256':
                            return do_derive_hmacsha(p_keylist[0])
                        return do_derive_cbc(p_keylist[0])
                else:
                    retVal = sign_single_file(p_keylist[0], internal)
                    if retVal !=0:
                        return retVal


            if internal["--pubkeyhash"]:
                # Check to see if pkh is a list and has more than 2 elements
                if type(internal["--pubkeyhash"]) == list and len(internal["--pubkeyhash"]) > 2:
                    info_print('%s flag is not needed since --key is used' %(internal["--pubkeyhash"][2]))
                    print_help()
                else:
                    save_public_key(p_keylist[0], internal)
                    save_public_key_hash(p_keylist[0], internal)

            if internal["--getmontgomeryvalues"]:
                save_montgomery_values(p_keylist[0], internal["--getmontgomeryvalues"])

        else:
            if internal["--random"] != None:
                p_key = SignKey()
                p_key.parse_random(internal["--random"], internal["--file"])
                do_random(p_key)

            elif internal["--pubkeyhash"] and type(internal["--pubkeyhash"]) == list:
                p_key = SignKey()
                # Extract pubkey if algo is specified, else take in as-is
                if len(internal["--pubkeyhash"]) == 3:
                    extract_pubkey(p_key, internal)
                    save_public_key_hash(p_key, internal) #chia
                else:
                    info_print('--pubkeyhash flag is missing %d argument(s) since --key is not used'
                        %(len(internal["--pubkeyhash"])-3))
                    print_help()
            elif internal["--sha"] in ['sha256', 'sha512']:
                length = -1
                offset = 0

                if internal["--length"]:
                    length = int(internal["--length"])

                if internal["--offset"]:
                    offset = int(internal["--offset"])

                internal["--length"] = length
                internal["--offset"] = offset

                if internal["--file"]:
                    compute_sha(internal["--sha"], internal["--file"], internal["--offset"], internal["--length"], internal["--block"])
                else:
                    print_help()

            else:
                print_help()
        # This is confusing since they are mixing booleans and integrers
        # but if we got here it is a success return 0
        return 0
    except Exception as e:
        info_print(traceback.format_exc())
        info_print('Encounter exception when signing')
        info_print(e)
        return exit_routine()

def main(commandLineArgs):
    args = parse_cmdline(commandLineArgs)

    if not args is False:
        retVal = tegrasign(args.file, args.getmode, args.getmontgomeryvalues, args.key, args.length,
            args.list, args.offset, args.pubkeyhash, args.sha, args.enc, args.verbose, args.iv, args.aad, args.tag, args.sign, args.verify, args.kdf, args.hsm, args.random, args.block)
        return retVal
    return 1

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
--enc
--verbose : optional, this is not enabled from tegraflash, standalone can be enabled
'''
if __name__=='__main__':
    main(sys.argv[1:])
