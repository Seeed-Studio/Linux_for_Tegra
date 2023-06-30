#
# Copyright (c) 2018-2023, NVIDIA Corporation.  All Rights Reserved.
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA Corporation is strictly prohibited.
#
import stat
import shutil
from xml.etree import ElementTree
from tegrasign_v3_util import *
import hashlib

def compute_sha(type, filename, offset, length, blockSize="0"):
    if type == 'sha256':
        return do_sha((256/8), filename, offset, length, blockSize)
    else:
        return do_sha((512/8), filename, offset, length, blockSize)

'''
Perform based on the node define
<sha digest_type="sha512" digest_file="br_bct_BR.sha" offset="68" length="4028" />
'''
def perform_sha(filename, shanode):
    if (shanode == None):
        return
    sha_type = shanode.get('digest_type')
    dest_file = shanode.get('digest_file')
    length = int (shanode.get('length') if shanode.get('length') else 0)
    offset = int (shanode.get('offset') if shanode.get('offset') else 0)

    compute_sha(sha_type, filename, offset, length)
    if not os.path.isfile(dest_file):
        raise tegrasign_exception('Could not find ' + dest_file)

'''
This parses the xml to sign list of specified files
Example xml file format
<file name="rcm_0.rcm" offset="1312" length="160" id="0" type="rcm" >
    <sbk encrypt="1" sign="1" encrypt_file="rcm_0_encrypt.rcm" hash="rcm_0.hash" ></sbk>
    <pkc signature="rcm_0.sig" signed_file="rcm_0_signed.rcm" ></pkc>
    <ec signature="rcm_0.sig" signed_file="rcm_0_signed.rcm" ></ec>
    <eddsa signature="rcm_0.sig" signed_file="rcm_0_signed.rcm" ></eddsa>
</file>
'''
def sign_files_internal(p_keylist, filenode, pkh, mont, sha_type, iv):

    filename = filenode.get('name')
    if filename == None:
        info_print('***Missing file name*** ')
        return exit_routine()

    sign_fh = open_file(filename, 'rb')
    buff_data = sign_fh.read()
    file_size = len(buff_data)
    sign_fh.close()

    length = int (filenode.get('length') if filenode.get('length') else 0)
    offset = int (filenode.get('offset') if filenode.get('offset') else 0)
    key_index = int (filenode.get('key_index') if filenode.get('key_index') else 0)

    length = length if length > 0 else file_size - offset
    offset = offset if offset > 0 else 0

    length=int(length)

    if file_size < offset:
      length = 0
      info_print('Warning: Offset %d is more than file Size %d for %s' % (offset, file_size, filename))
      return exit_routine()

    if (offset + length) > file_size:
      info_print('Warning: Offset %d + Length %d is greater than file Size %d for %s' % (offset, length, file_size, filename))
      return exit_routine()

    if key_index >= MAX_KEY_LIST:
        info_print('Warning: Key at index %d is not provided ' %(key_index))
        return exit_routine()

    buff_to_sign = buff_data[offset : offset + length]

    if p_keylist[key_index].mode == NvTegraSign_SBK:

        sbknode = filenode.find('sbk')
        if sbknode is None:
            info_print('sbk tag is not present.')
            return exit_routine()

        skip_enc = 0 if int(sbknode.get('encrypt')) >=1 else 1
        do_sign  = 1 if int(sbknode.get('sign')) >=1 else 0
        enc_file_name = sbknode.get('encrypt_file')
        hash_file_name = sbknode.get('hash')

        NumAesBlocks = int(length/AES_128_HASH_BLOCK_LEN)
        length = int(NumAesBlocks * AES_128_HASH_BLOCK_LEN)

        buff_hash = '0' * AES_128_HASH_BLOCK_LEN
        buff_enc = bytearray(buff_to_sign)

        if (skip_enc or is_zero_aes(p_keylist[key_index])):
            info_print('Skipping encryption: ' + filename, True)
        else:
            buff_enc = do_aes_cbc(buff_to_sign, length, p_keylist[key_index], iv)

        if do_sign:
            buff_hash = do_aes_cmac(buff_enc, length, p_keylist[key_index])

        buff_data = buff_data[0:int(offset)] + buff_enc + buff_data[int(offset) + int(length):]

        # save encryption to *_encrypt.* file
        enc_fh = open_file(enc_file_name, 'wb')
        write_file(enc_fh, buff_data)
        enc_fh.close()

        # save hash to *.hash file
        hash_fh = open_file(hash_file_name, 'wb')
        write_file(hash_fh, buff_hash)
        hash_fh.close()

        perform_sha(filename, filenode.find('sha'))

    elif p_keylist[key_index].mode == NvTegraSign_FSKP:

        sbknode = filenode.find('sbk')
        if sbknode is None:
            info_print('sbk tag is not present.')
            return exit_routine()

        skip_enc = 0 if int(sbknode.get('encrypt')) >=1 else 1
        do_sign  = 1 if int(sbknode.get('sign')) >=1 else 0
        enc_file_name = sbknode.get('encrypt_file')
        hash_file_name = sbknode.get('hash')

        NumAesBlocks = int(length/AES_256_HASH_BLOCK_LEN)
        length = int(NumAesBlocks*AES_256_HASH_BLOCK_LEN)

        buff_hash = "0" * AES_256_HASH_BLOCK_LEN
        buff_enc = bytearray(buff_to_sign)

        if (skip_enc or is_zero_aes(p_keylist[key_index])):
            info_print('Skipping encryption: ' + filename, True)
        else:
            buff_enc = do_aes_cbc(buff_to_sign, length, p_keylist[key_index], iv)

        if do_sign:
            buff_hash = do_aes_cmac(buff_enc, length, p_keylist[key_index])

        buff_data = buff_data[0:offset] + buff_enc + buff_data[offset + length:]

        # save encryption to *_encrypt.* file
        enc_fh = open_file(enc_file_name, 'wb')
        write_file(enc_fh, buff_data)
        enc_fh.close()

        # save hash to *.hash file
        hash_fh = open_file(hash_file_name, 'wb')
        write_file(hash_fh, buff_hash)
        hash_fh.close()

        perform_sha(filename, filenode.find('sha'))

    elif p_keylist[key_index].mode == NvTegraSign_ECC:

        ecnode = filenode.find('ec')
        if ecnode is None:
          info_print('ec tag is not present')
          return exit_routine()

        sig_file_name = ecnode.get('signature')
        signed_file_name = ecnode.get('signed_file')

        sig_data = do_ecc(buff_to_sign, length, p_keylist[key_index], pkh, sha_type)

        if sig_file_name:
            sig_fh = open_file(sig_file_name, 'wb')
            write_file(sig_fh, sig_data)
            sig_fh.close()
        else:
            info_print('Not saving signature')

        if signed_file_name:
            signed_fh = open_file(signed_file_name, 'wb')
            write_file(signed_fh, buff_data)
            signed_fh.close()
        else:
            info_print('Not saving signed file')

    elif p_keylist[key_index].mode == NvTegraSign_ED25519:

        ednode = filenode.find('eddsa')

        if ednode is None:
          info_print('eddsa tag is not present')
          return exit_routine()

        sig_file_name = ednode.get('signature')
        signed_file_name = ednode.get('signed_file')

        sig_data = do_ed25519(buff_to_sign, length, p_keylist[key_index], pkh)

        if sig_file_name:
            sig_fh = open_file(sig_file_name, 'wb')
            write_file(sig_fh, sig_data)
            sig_fh.close()
        else:
            info_print('Not saving signature')

        if signed_file_name:
            signed_fh = open_file(signed_file_name, 'wb')
            write_file(signed_fh, buff_data)
            signed_fh.close()
        else:
            info_print('Not saving signed file')

    elif p_keylist[key_index].mode == NvTegraSign_XMSS:
        ednode = filenode.find('xmss')

        if ednode is None:
          info_print('xmss tag is not present')
          exit_routine()

        sig_file_name = ednode.get('signature')
        signed_file_name = ednode.get('signed_file')

        sig_data = do_xmss(buff_to_sign, p_keylist[key_index], pkh)

        if sig_file_name:
            sig_fh = open_file(sig_file_name, 'wb')
            write_file(sig_fh, sig_data)
            sig_fh.close()
        else:
            info_print('Not saving signature')

        if signed_file_name:
            signed_fh = open_file(signed_file_name, 'wb')
            write_file(signed_fh, buff_data)
            signed_fh.close()
        else:
            info_print('Not saving signed file')

    else:

        pkcnode = filenode.find('pkc')
        if pkcnode is None:
          info_print('pkc tag is not present')
          return exit_routine()

        sig_file_name = pkcnode.get('signature')
        signed_file_name = pkcnode.get('signed_file')
        sig_data = do_rsa_pss(buff_to_sign, length, p_keylist[key_index], pkh, mont, sha_type)

        if sig_file_name:
            sig_fh = open_file(sig_file_name, 'wb')
            write_file(sig_fh, sig_data)
            sig_fh.close()
        else:
            info_print('Not saving signature')

        if signed_file_name:
            signed_fh = open_file(signed_file_name, 'wb')
            write_file(signed_fh, buff_data)
            signed_fh.close()
        else:
            info_print('Not saving signed file')
    return 0


def sign_files_in_list(p_keylist, internal):
    filelistname = internal["--list"]
    pkh, _, _= get_pkh_args(internal)
    mont = internal["--getmontgomeryvalues"]
    iv = internal["--iv"]
    sha_type = internal["--sha"]

    try:
        tree = ElementTree.parse(filelistname)

    except IOError:
        info_print('Cannot parse %s as a XML file' %(filelistname))
        return exit_routine()

    root = tree.getroot()

    for child in root:
        retVal = sign_files_internal(p_keylist, child, pkh, mont, sha_type, iv)
        if retVal != 0:
            return retVal

    # Add mode info
    root.set('mode', get_mode_str(p_keylist[0], False))

    # Prepend the following to the xml content
    comment = '<?xml version="1.0"?>\n<!-- Auto generated by tegrasign -->\n\n'
    if(isPython3()):
        xml_str = comment + ElementTree.tostring(root, encoding='unicode')
    else:
        xml_str = comment + ElementTree.tostring(root)

    # Generate *_signed.xml
    xml_fh = open_file(filelistname.replace('.xml', '_signed.xml'), 'w')
    write_file(xml_fh, xml_str)
    xml_fh.close()
    return 0


def sign_single_file(p_key, internal):
    filename = internal["--file"]
    offset = internal["--offset"]
    length = internal["--length"]
    enc_type = internal["--enc"]
    sign_type = internal["--sign"]
    pkh, _, _ = get_pkh_args(internal)
    mont = internal["--getmontgomeryvalues"]
    iv = internal["--iv"]
    aad = internal["--aad"]
    tag = internal["--tag"]
    verify = internal["--verify"]
    sha512 = internal["--sha"]
    verbose = internal["--verbose"]

    with open(filename, 'rb') as f:
        buff_data = bytearray(f.read())
        file_size = len(buff_data)

    offset = offset if offset > 0 else 0
    length = length if length > 0 else file_size - offset

    if file_size < offset:
      length = 0
      info_print('Warning: Offset %d is more than file Size %d for %s' % (offset, file_size, filename))
      return exit_routine()

    if (offset + length) > file_size:
      info_print('Warning: Offset %d + Length %d is greater than file Size %d for %s' % (offset, length, file_size, filename))
      return exit_routine()

    buff_to_sign = buff_data[offset : offset + length]

    if p_key.mode == NvTegraSign_SBK:

        NumAesBlocks = int(length / AES_128_HASH_BLOCK_LEN)
        length = int(NumAesBlocks * AES_128_HASH_BLOCK_LEN)

        buff_hash = "0" * AES_128_HASH_BLOCK_LEN
        buff_enc = bytearray(buff_to_sign)
        is_hash = False

        if ((enc_type == None or enc_type == 'None') or is_zero_aes(p_key)):
            info_print('Skipping encryption: ' + filename, True)
        elif (enc_type == 'aescbc'):
            buff_enc = do_aes_cbc(buff_to_sign, length, p_key, iv)
        elif (enc_type == 'aesgcm'):
            buff_enc = do_aes_gcm(buff_to_sign, length, p_key, iv, aad, tag, verify, verbose)
            tag_file_name = os.path.splitext(filename)[0] + '.tag'
            with open(tag_file_name, 'wb') as f:
                f.write(p_key.kdf.tag.get_hexbuf())

        if sign_type == 'hmacsha256':
            buff_hash = do_hmac_sha256(buff_enc, length, p_key)
            is_hash = True
        elif enc_type != 'aesgcm': # cmac is not for aesgcm
            buff_hash = do_aes_cmac(buff_enc, length, p_key)
            is_hash = True

        buff_data = buff_data[0:offset] + buff_enc + buff_data[offset + length:]

        # save encryption to *_encrypt.* file
        enc_file_name = os.path.splitext(filename)[0] + '_encrypt' + os.path.splitext(filename)[1] # ie. rcm_0_encrypt.rcm
        enc_fh = open_file(enc_file_name, 'wb')
        write_file(enc_fh, buff_data)
        enc_fh.close()

        if is_hash == True:
            # save hash to *.hash file
            hash_file_name = os.path.splitext(filename)[0] + '.hash'
            hash_fh = open_file(hash_file_name, 'wb')
            write_file(hash_fh, buff_hash)
            hash_fh.close()

    elif p_key.mode == NvTegraSign_FSKP:

        NumAesBlocks = int(length/AES_256_HASH_BLOCK_LEN)
        length = int(NumAesBlocks*AES_256_HASH_BLOCK_LEN)

        buff_hash = "0" * AES_256_HASH_BLOCK_LEN
        buff_enc = bytearray(buff_to_sign)
        is_hash = False

        if not is_hsm() and ((enc_type == None or enc_type == 'None') or is_zero_aes(p_key)):
            info_print('Skipping encryption: ' + filename, True)
        elif (enc_type == 'aescbc'):
            buff_enc = do_aes_cbc(buff_to_sign, length, p_key, iv)
        elif (enc_type == 'aesgcm'):
            buff_enc = do_aes_gcm(buff_to_sign, length, p_key, iv, aad, tag, verify, verbose)
            tag_file_name = os.path.splitext(filename)[0] + '.tag'
            with open(tag_file_name, 'wb') as f:
                f.write(p_key.kdf.tag.get_hexbuf())

        if sign_type == 'hmacsha256':
            buff_hash = do_hmac_sha256(buff_enc, length, p_key)
            is_hash = True
        elif enc_type != 'aesgcm': # cmac is not for aesgcm
            buff_hash = do_aes_cmac(buff_enc, length, p_key)
            is_hash = True

        buff_data = buff_data[0:offset] + buff_enc + buff_data[offset + length:]

        # save encryption to *_encrypt.* file
        enc_file_name = os.path.splitext(filename)[0] + '_encrypt' + os.path.splitext(filename)[1] # ie. rcm_0_encrypt.rcm
        enc_fh = open_file(enc_file_name, 'wb')
        write_file(enc_fh, buff_data)
        enc_fh.close()

        if is_hash == True:
            # save hash to *.hash file
            hash_file_name = os.path.splitext(filename)[0] + '.hash'
            hash_fh = open_file(hash_file_name, 'wb')
            write_file(hash_fh, buff_hash)
            hash_fh.close()

    elif p_key.mode == NvTegraSign_ECC:

        sig_data = do_ecc(buff_to_sign, length, p_key, pkh, sha512)

        sig_file_name = os.path.splitext(filename)[0] + '.sig'
        sig_fh = open_file(sig_file_name, 'wb')
        write_file(sig_fh, sig_data)
        sig_fh.close()

    elif p_key.mode == NvTegraSign_ED25519:

        sig_data = do_ed25519(buff_to_sign, length, p_key, pkh)
        sig_file_name = os.path.splitext(filename)[0] + '.sig'
        sig_fh = open_file(sig_file_name, 'wb')
        write_file(sig_fh, sig_data)
        sig_fh.close()

    elif p_key.mode == NvTegraSign_XMSS:
        sig_data = do_xmss(buff_to_sign, p_key, pkh)
        sig_file_name = os.path.splitext(filename)[0] + '.sig'
        sig_fh = open_file(sig_file_name, 'wb')
        write_file(sig_fh, sig_data)
        sig_fh.close()

    else:

        sig_data = do_rsa_pss(buff_to_sign, length, p_key, pkh, mont, sha512)
        sig_file_name = os.path.splitext(filename)[0] + '.sig'
        sig_fh = open_file(sig_file_name, 'wb')
        write_file(sig_fh, sig_data)
        sig_fh.close()
    return 0

def do_aes_cmac(buff_to_sign, length, p_key):
    buff_sig = "0" * 16 # note cmac will always return 128bit

    base_name =  script_dir + 'v3_cmac_' + pid
    raw_name = base_name + '.raw'
    result_name = base_name + '.out'
    raw_file = open_file(raw_name, 'wb')

    key_bytes = len(binascii.hexlify(p_key.key.aeskey))/2
    keysize_bytes = int_2byte_cnt(p_key.keysize)
    len_bytes = int_2byte_cnt(length)
    sign_bytes = len(buff_to_sign)
    sig_bytes = len(buff_sig)
    result_bytes = len(result_name) + 1

    # to write to file
    # order: sizes then data for: key, keysize, length, buff_to_sign, buff_sig, result_name
    num_list = [key_bytes, keysize_bytes, len_bytes, sign_bytes, sig_bytes, result_bytes]
    for num in num_list:
        arr = int_2bytes(4, num)
        write_file(raw_file, arr)

    write_file(raw_file, p_key.key.aeskey) #aeskey already in byte array format
    arr = int_2bytes(keysize_bytes, p_key.keysize)
    write_file(raw_file, arr)
    arr = int_2bytes(len_bytes, length)
    write_file(raw_file, arr)

    write_file(raw_file, bytes(buff_to_sign))

    write_file(raw_file, buff_sig.encode("utf-8"))
    write_file(raw_file, result_name.encode("utf-8"))
    nullarr = bytearray(1)
    nullarr[0] = 0          # need this null for char*
    write_file(raw_file, nullarr)
    raw_file.close()

    command = exec_file(TegraOpenssl)
    command.extend(['--aescmac', raw_name])

    ret_str = run_command(command)

    if check_file(result_name):
        result_fh = open_file(result_name, 'rb')
        buff_sig = result_fh.read()
        result_fh.close()
        os.remove(result_name)

    os.remove(raw_name)
    return buff_sig

def do_hmac_sha256(buff_to_sign, length, p_key):
    buff_dgst = "0" * 32 # note hmac-sha256 will always return 256bit

    if is_hsm():
        from tegrasign_v3_hsm import do_hmac_sha256_hsm
        return do_hmac_sha256_hsm(buff_to_sign, p_key)

    base_name = script_dir + 'v3_hmacsha_' + pid
    raw_name = base_name + '.raw'
    result_name = base_name + '.out'
    raw_file = open_file(raw_name, 'wb')

    key_bytes = len(binascii.hexlify(p_key.key.aeskey))/2
    keysize_bytes = int_2byte_cnt(p_key.keysize)
    len_bytes = int_2byte_cnt(length)
    hash_bytes = len(buff_to_sign)
    dgst_bytes = len(buff_dgst)
    result_bytes = len(result_name) + 1

    # to write to file
    # order: sizes then data for: key, keysize, length, buff_to_sign, buff_dgst, result_name
    num_list = [key_bytes, keysize_bytes, len_bytes, hash_bytes, dgst_bytes, result_bytes]
    for num in num_list:
        arr = int_2bytes(4, num)
        write_file(raw_file, arr)

    write_file(raw_file, p_key.key.aeskey) #aeskey already in byte array format
    arr = int_2bytes(keysize_bytes, p_key.keysize)
    write_file(raw_file, arr)
    arr = int_2bytes(len_bytes, length)
    write_file(raw_file, arr)

    write_file(raw_file, bytes(buff_to_sign))
    write_file(raw_file, buff_dgst.encode("utf-8"))
    write_file(raw_file, result_name.encode("utf-8"))
    nullarr = bytearray(1)
    nullarr[0] = 0          # need this null for char*
    write_file(raw_file, nullarr)
    raw_file.close()

    command = exec_file(TegraOpenssl)
    command.extend(['--hmacsha256', raw_name])

    ret_str = run_command(command)

    if check_file(result_name):
        result_fh = open_file(result_name, 'rb')
        buff_dgst = result_fh.read()
        result_fh.close()
        os.remove(result_name)

    os.remove(raw_name)
    return buff_dgst

def do_aes_cbc(buff_to_enc, length, p_key, iv):

    buff_sig = "0" * 16
    base_name = script_dir + 'v3_cbc_' + pid
    raw_name = base_name + '.raw'
    result_name = base_name + '.out'
    raw_file = open_file(raw_name, 'wb')

    key_bytes = len(binascii.hexlify(p_key.key.aeskey))/2
    keysize_bytes = int_2byte_cnt(p_key.keysize)
    len_bytes = int_2byte_cnt(length)
    enc_bytes = len(buff_to_enc)
    dest_bytes = int(length)
    result_bytes = len(result_name) + 1
    buff_dest = "0" * dest_bytes
    if (type(iv) == str) or (type(iv) == bytearray):
        iv_bytes = len(binascii.hexlify(iv))/2
    else:
        iv_bytes = 0;

    # to write to file
    # order: sizes then data for: key, keysize, length, buff_to_enc, buff_dest, result_name, iv
    num_list = [key_bytes, keysize_bytes, len_bytes, enc_bytes, dest_bytes, result_bytes, iv_bytes]
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
    if (iv != None):
        write_file(raw_file, bytes(iv))
    raw_file.close()

    command = exec_file(TegraOpenssl)
    command.extend(['--aescbc', raw_name])

    ret_str = run_command(command)

    if check_file(result_name):
        result_fh = open_file(result_name, 'rb')
        buff_sig = result_fh.read()
        result_fh.close()
        os.remove(result_name)

    os.remove(raw_name)
    return buff_sig

def do_rsa_pss(buff_to_sign, length, p_key, pkhfile, montfile, sha512):
    p_key.key.pkckey.Sha = sha512
    if is_hsm():
        from tegrasign_v3_hsm import do_rsa_pss_hsm
        return do_rsa_pss_hsm(buff_to_sign, p_key)

    buff_sig = "0" * p_key.keysize
    base_name =  script_dir + 'v3_rsa_' + pid
    raw_name = base_name + '.raw'
    result_name = base_name + '.out'

    raw_file = open_file(raw_name, 'wb')

    filename_bytes = len(p_key.filename) + 1 # to account for 0x0
    len_bytes = int_2byte_cnt(length)
    sign_bytes = len(buff_to_sign)
    sig_bytes = len(buff_sig)
    pkh_bytes = 0 if pkhfile == None else (len(pkhfile) + 1)
    mont_bytes = 0 if montfile == None else (len(montfile) + 1)
    result_bytes = len(result_name) + 1

    # order: sizes then data for: file name, length, buff_to_sign, buff_sig, pkhfile, montfile, result_name, sha512
    num_list = [filename_bytes, len_bytes, sign_bytes, sig_bytes, pkh_bytes, mont_bytes, result_bytes, sha512]
    for num in num_list:
        arr = int_2bytes(4, num)
        write_file(raw_file, arr)

    write_file(raw_file, bytes(p_key.filename.encode("utf-8")))
    nullarr = bytearray(1)
    nullarr[0] = 0          # need this null for char*

    write_file(raw_file, nullarr)
    arr = int_2bytes(len_bytes, length)
    write_file(raw_file, arr)

    write_file(raw_file, buff_to_sign)
    write_file(raw_file, bytes(buff_sig.encode("utf-8")))

    if (pkh_bytes > 0):
        write_file(raw_file, bytes(pkhfile.encode("utf-8")))
        write_file(raw_file, nullarr)

    if (mont_bytes > 0):
        write_file(raw_file, bytes(montfile.encode("utf-8")))
        write_file(raw_file, nullarr)

    write_file(raw_file, bytes(result_name.encode("utf-8")))
    write_file(raw_file, nullarr)

    raw_file.close()

    command = exec_file(TegraOpenssl)
    command.extend(['--rsa', raw_name])

    ret_str = run_command(command)

    if check_file(result_name):
        result_fh = open_file(result_name, 'rb')
        buff_sig = result_fh.read()
        result_fh.close()
        os.remove(result_name)

    os.remove(raw_name)
    return buff_sig

def do_ecc(buff_to_sign, length, p_key, pkhfile, sha512):

    buff_sig = "0" * p_key.keysize

    base_name =  script_dir + 'v3_ecc_' + pid
    raw_name = base_name + '.raw'
    result_name = base_name + '.out'
    raw_file = open_file(raw_name, 'wb')

    filename_bytes = len(p_key.filename) + 1 # to account for 0x0
    len_bytes = int_2byte_cnt(length)
    sign_bytes = len(buff_to_sign)
    sig_bytes = len(buff_sig)
    pkh_bytes = 0 if pkhfile == None else (len(pkhfile) + 1)
    result_bytes = len(result_name) + 1

    # order: sizes then data for: file name, length, buff_to_sign, buff_sig, pkhfile, result_name, sha512
    num_list = [filename_bytes, len_bytes, sign_bytes, sig_bytes, pkh_bytes, result_bytes, sha512]
    for num in num_list:
        arr = int_2bytes(4, num)
        write_file(raw_file, arr)

    write_file(raw_file, bytes(p_key.filename.encode("utf-8")))
    nullarr = bytearray(1)
    nullarr[0] = 0          # need this null for char*
    write_file(raw_file, nullarr)

    arr = int_2bytes(len_bytes, length)
    write_file(raw_file, arr)

    write_file(raw_file, buff_to_sign)
    write_file(raw_file, bytes(buff_sig.encode("utf-8")))

    if (pkh_bytes > 0):
        write_file(raw_file, bytes(pkhfile.encode("utf-8")))
        write_file(raw_file, nullarr)

    write_file(raw_file, bytes(result_name.encode("utf-8")))
    write_file(raw_file, nullarr)

    raw_file.close()

    command = exec_file(TegraOpenssl)
    command.extend(['--ecc', raw_name])

    ret_str = run_command(command)

    if check_file(result_name):
        result_fh = open_file(result_name, 'rb')
        buff_sig = result_fh.read()
        result_fh.close()
        os.remove(result_name)

    os.remove(raw_name)
    return buff_sig

def do_ed25519(buff_to_sign, length, p_key, pkhfile):

    if is_hsm():
        from tegrasign_v3_hsm import do_ed25519_hsm
        return do_ed25519_hsm(buff_to_sign, p_key)

    buff_sig = "0" * p_key.keysize

    base_name =  script_dir + 'v3_eddsa_' + pid
    raw_name = base_name + '.raw'
    result_name = base_name + '.out'
    raw_file = open_file(raw_name, 'wb')

    filename_bytes = len(p_key.filename) + 1 # to account for 0x0
    len_bytes = int_2byte_cnt(length)
    sign_bytes = len(buff_to_sign)
    sig_bytes = len(buff_sig)
    pkh_bytes = 0 if pkhfile == None else (len(pkhfile) + 1)
    result_bytes = len(result_name) + 1

    # order: sizes then data for: file name, length, buff_to_sign, buff_sig, pkhfile, result_name
    num_list = [filename_bytes, len_bytes, sign_bytes, sig_bytes, pkh_bytes, result_bytes]
    for num in num_list:
        arr = int_2bytes(4, num)
        write_file(raw_file, arr)

    write_file(raw_file, bytes(p_key.filename.encode("utf-8")))
    nullarr = bytearray(1)
    nullarr[0] = 0          # need this null for char*
    write_file(raw_file, nullarr)

    arr = int_2bytes(len_bytes, length)
    write_file(raw_file, arr)

    write_file(raw_file, buff_to_sign)
    write_file(raw_file, bytes(buff_sig.encode("utf-8")))

    if (pkh_bytes > 0):
        write_file(raw_file, bytes(pkhfile.encode("utf-8")))
        write_file(raw_file, nullarr)

    write_file(raw_file, bytes(result_name.encode("utf-8")))
    write_file(raw_file, nullarr)

    raw_file.close()

    command = exec_file(TegraOpenssl)
    command.extend(['--ed25519', raw_name])

    ret_str = run_command(command)

    if check_file(result_name):
        result_fh = open_file(result_name, 'rb')
        buff_sig = result_fh.read()
        result_fh.close()
        os.remove(result_name)

    os.remove(raw_name)
    return buff_sig

def do_xmss(buff_to_sign, p_key, pkh):
    # public key file name is xmss-sha256_20.pub
    raw_name = script_dir + 'v3_xmss_' + pid + '.raw'
    buff_sign = None;

    key_file = p_key.filename
    cache_file = key_file + '.cache'
    pub_file = os.path.splitext(os.path.basename(key_file))[0] + '.pub'
    result_name = raw_name + '.sig'
    xmss_exe = 'xmss-sign'

    if (check_file(xmss_exe) == False):
        raise tegrasign_exception('Can not find %s for signing' % (xmss_exe))

    # If any of the three files does not exist, invoke to regenrate key pair
    if (check_file(cache_file) == False or check_file(key_file) == False
        or check_file(pub_file) == False):
        info_print('Regenerating XMSS key pair')
        if (check_file(cache_file) == True):
            os.remove(cache_file)
        if (check_file(key_file) == True):
            os.remove(key_file)
        if (check_file(pub_file) == True):
            os.remove(pub_file)
        # Invoke cmd to regenerate: ./xmss-sign generate --privkey private-key --pubkey public-key
        command = exec_file(xmss_exe)
        command.extend(['generate --privkey ' + key_file + ' --pubkey ' + pub_file]);
        ret_str = run_command(command)
    status = os.stat(key_file)
    mask = oct(status.st_mode)[-3:]

    if (status.st_mode & stat.S_IWOTH) or (status.st_mode & stat.S_IXOTH) or (status.st_mode & stat.S_IROTH):
        info_print(key_file + ' file mode needs to be modified, mask: ' + mask)
        new_mode = stat.S_IMODE(os.lstat(key_file).st_mode)
        new_mode = new_mode & 0o770   # get rid of other mode, so resulting in: o=
        try:
            os.chmod(key_file, new_mode)
            info_print('Hit exception when changing the mode: ' + oct(new_mode)[-3:] )
        except Exception as e:
            info_print('Hit exception when changing the mode: ' + oct(new_mode)[-3:] + str(e))
    with open_file(raw_name, 'wb') as raw_file:
        write_file(raw_file, bytes(buff_to_sign))

    if pkh:
        shutil.copyfile(pub_file, pkh)

    # Generate the signature in file named '$raw_name'.sig
    command = exec_file(xmss_exe)
    command.extend(['sign'])
    command.extend(['-f', raw_name])
    command.extend(['--privkey', key_file])
    command.extend(['-o', result_name])
    ret_str = run_command(command)

    if check_file(result_name):
        with open_file(result_name, 'rb') as result_fh:
            buff_sig = result_fh.read()
        os.remove(result_name)
    return buff_sig

def do_sha(sha_cnt, filename, offset, length, blockSize):

    sha_fh = open_file(filename, 'rb')
    buff_data = sha_fh.read()
    sha_fh.close()

    file_size = len(buff_data)
    length = length if length > 0 else file_size - offset
    offset = offset if offset > 0 else 0

    if file_size < offset:
      length = 0
      info_print('Warning: Offset %d is more than file Size %d for %s' % (offset, file_size, filename))
      return exit_routine()

    if (offset + length) > file_size:
      info_print('Warning: Offset %d + Length %d is greater than file Size %d for %s' % (offset, length, file_size, filename))
      return exit_routine()

    buff_to_hash = buff_data[offset : offset + length]
    buff_hash = "0" * int(sha_cnt)
    len_bytes = int_2byte_cnt(length)
    base_name = script_dir + 'v3_' + os.path.splitext(os.path.basename(filename))[0] + '_' + pid
    hash_file_name = os.path.splitext(filename)[0] + '.sha'
    hash_file_bytes = len(hash_file_name) + 1

    # to write to raw file
    raw_name =  base_name + '.raw'
    raw_file = open_file(raw_name, 'wb')

    # order: sizes then data for: length, buff_to_hash, buff_hash, hash_file_name
    num_list = [len_bytes, length, sha_cnt, hash_file_bytes]
    for num in num_list:
        arr = int_2bytes(4, num)
        write_file(raw_file, arr)

    arr = int_2bytes(len_bytes, length)
    write_file(raw_file, arr)

    write_file(raw_file, bytes(buff_to_hash))
    write_file(raw_file, bytes(buff_hash.encode("utf-8")))
    nullarr = bytearray(1)
    nullarr[0] = 0          # need this null for char*
    write_file(raw_file, bytes(hash_file_name.encode("utf-8")))
    write_file(raw_file, nullarr)

    raw_file.close()

    command = exec_file(TegraOpenssl)
    command.extend(['--sha', raw_name])
    if blockSize != "0":
        command.extend(['--block', blockSize])

    ret_str = run_command(command)
    if check_file(hash_file_name):
        info_print('Sha saved in ' + hash_file_name)

    os.remove(raw_name)
    return hash_file_name

def extract_AES_key(pBuffer, p_key):

    # Process the content as binary format
    if not b'0' in pBuffer:
        byte_cnt = len(pBuffer)

        if byte_cnt == 16:
            p_key.keysize = byte_cnt
            p_key.key.aeskey = pBuffer
            p_key.mode = NvTegraSign_SBK
            info_print('Key is a SBK key')
            info_print('Key Size is 16 bytes')
            return 1

        elif byte_cnt == 32:
            p_key.keysize = byte_cnt
            p_key.key.aeskey = pBuffer
            p_key.mode = NvTegraSign_FSKP
            info_print('Key Size is 32 bytes')
            return 1
        else:
            info_print('Not an AES key', True)
            return 0

    try:
        # Process the content as string format
        list_of_elements = [ (number).replace("0x", "") for number in pBuffer[:].decode().replace("\n", " ").split(" ") ]

        key_str = list_of_elements[0]

        for element in list_of_elements[1:]:
            key_str = key_str + element

        key_str_length = len(key_str.strip())
        if key_str_length == 32:
            p_key.mode = NvTegraSign_SBK
            info_print('Key is a SBK key')
            info_print('Key Size is 16 bytes')

        elif key_str_length == 64:
            p_key.mode = NvTegraSign_FSKP
            info_print('Key Size is 32 bytes')

        else:
            info_print('Not an AES key', True)
            return 0

        key = str_to_hex(key_str)

        p_key.keysize = int(key_str_length/2)

        p_key.key.aeskey = bytearray(key)

        return 1

    except UnicodeDecodeError:
        # key format is in a binary sequence
        byte_cnt = len(pBuffer)

        if byte_cnt == 16:
            p_key.keysize = byte_cnt
            p_key.key.aeskey = pBuffer
            p_key.mode = NvTegraSign_SBK
            info_print('Key is a SBK key')
            info_print('Key Size is 16 bytes')
            return 1

        elif byte_cnt == 32:
            p_key.keysize = byte_cnt
            p_key.key.aeskey = pBuffer
            p_key.mode = NvTegraSign_FSKP
            info_print('Key Size is 32 bytes')
            return 1
    info_print('Not an AES key', True)
    return 0

def is_PKC_key(keyfilename, p_key, pkh, mont):

    command = exec_file(TegraOpenssl)
    pubkeyfile = 'v3_pub_keyhash'
    temp_copy = 0

    # pack the arguments
    if pkh and mont:
        if is_hsm():
            from tegrasign_v3_hsm import get_rsa_mod_hsm, get_rsa_mont_hsm
            return get_rsa_mod_hsm(p_key, pkh) and get_rsa_mont_hsm(p_key, mont)

        command.extend(['--isPkcKey', keyfilename, pkh, mont])
    elif pkh:
        if is_hsm():
            from tegrasign_v3_hsm import get_rsa_mod_hsm
            return get_rsa_mod_hsm(p_key, pkh)
        command.extend(['--isPkcKey', keyfilename, pkh])
    elif mont:
        if is_hsm():
            from tegrasign_v3_hsm import get_rsa_mont_hsm
            return get_rsa_mont_hsm(p_key, mont)

        command.extend(['--isPkcKey', keyfilename, pubkeyfile, mont])
        temp_copy = 1
    else:
        if is_hsm():
            from tegrasign_v3_hsm import get_rsa_mod_hsm
            return get_rsa_mod_hsm(p_key)
        command.extend(['--isPkcKey', keyfilename])

    ret_str = run_command(command)

    if temp_copy==1:
        os.remove(pubkeyfile)

    if not is_ret_ok(ret_str):
        return False

    # scan the return string for decimal value
    m = re.search('Key size is (\d+)', ret_str)
    if m:
        p_key.keysize = int(m.group(1))
        if (p_key.keysize > 0) and (p_key.keysize < NV_RSA_MAX_KEY_SIZE):
            return True
    return False

def is_ECC_key(keyfilename, p_key, pkh):
    if is_hsm():
        return False #TODO: Not supported

    command = exec_file(TegraOpenssl)

    if pkh == None:
        command.extend(['--isEccKey', keyfilename])
    else:
        command.extend(['--isEccKey', keyfilename, pkh])

    ret_str = run_command(command)

    if is_ret_ok(ret_str):
        # See if the key is p521
        if '521' in ret_str:
            p_key.keysize = NV_ECC521_SIG_STRUCT_SIZE
        else:
            p_key.keysize = NV_ECC_SIG_STRUCT_SIZE
        return True
    return False

def is_ED25519_key(keyfilename, p_key, pkh):

    if is_hsm():
        from tegrasign_v3_hsm import get_ed25519_pub_hsm
        return get_ed25519_pub_hsm(p_key, pkh)

    command = exec_file(TegraOpenssl)

    if pkh == None:
        command.extend(['--isEd25519Key', keyfilename])
    else:
        command.extend(['--isEd25519Key', keyfilename, pkh])

    ret_str = run_command(command)
    if is_ret_ok(ret_str):
        p_key.keysize = ED25519_SIG_SIZE
        return True
    return False

def is_xmss_key(keyfilename, p_key, pkh):

    file_size = os.path.getsize(keyfilename)

    if (file_size == XMSS_KEY_SIZE):
        p_key.keysize = XMSS_KEY_SIZE
        info_print('Assuming XMSS key')
        pub_file = os.path.splitext(os.path.basename(keyfilename))[0] + '.pub'
        if pkh and check_file(pub_file):
            # Duplicating the file because we need to pass that back to the caller
            shutil.copyfile(pub_file, pkh)
        return True
    return False

def do_kdf_kdf2(kdk, kdd, label = None, context = None, HexLabel = False):

    msgStr = get_composed_msg(label,context, 256, HexLabel, True)

    internal = SignKey()
    if kdd == None:
        internal.key.aeskey = str_to_hex(kdk)
    else:
        internal.key.aeskey = str_to_hex(kdk+kdd)

    internal.keysize = len(internal.key.aeskey)
    msg = str_to_hex(msgStr)

    return do_hmac_sha256(msg, len(msg), internal)

def do_kdf_params_t234(dk, params, kdf_list):
    # Note some kdf is using string operation, some are hex operation
    is_hex = True
    is_str = False
    L = 256
    basic_params = params['BASIC']

    # Derive the key relationship: dk -> kdk -> *_dec_kdk
    dk_params = params['DK'][dk]
    dk_ctx = {
        'KDK'     : dk_params['KDK'],
        'Label'   : hex_to_str(kdf_list[KdfArg.DKSTR]), # Note this is passed in
        'Context' : hex_to_str(kdf_list[KdfArg.DKVER]), # Note this is passed in
    }

    dk_ctx['Msg'] = get_composed_msg(dk_ctx['Label'], dk_ctx['Context'], L, is_hex)

    kdk_params = params['KDK'][dk_ctx['KDK']]
    kdk_to_use = kdk_params['KDK']
    kdk_ctx = {
        'KDK'   : kdk_to_use,
        'Label' : kdk_params["Label"],
    }
    kdk_ctx['Msg'] = get_composed_msg(kdk_ctx['Label'], '', L, is_str)

    bl_dec_kdk_ctx = {}
    fw_dec_kdk_ctx = {}

    # Check if bl_dec_kdk is defined for this dk
    if '_ROM_DEC_KDK' not in kdk_to_use:
        bl_dec_kdk_params = params['DEC_KDK'][kdk_ctx['KDK']]
        kdk_to_use = bl_dec_kdk_params['KDK']
        bl_dec_kdk_ctx = {
            'KDK'   : kdk_to_use,
            'Label' : hex_to_str(kdf_list[KdfArg.BLSTR]),  # Note this is passed in
        }

        bl_dec_kdk_ctx['Msg'] = get_composed_msg(bl_dec_kdk_ctx['Label'], '', L, is_hex)

        # Check if fw_dec_kdk is defined for this dk
        if '_ROM_DEC_KDK' not in kdk_to_use:
            fw_dec_kdk_params = params['DEC_KDK'][bl_dec_kdk_ctx['KDK']]
            kdk_to_use = fw_dec_kdk_params['KDK']
            fw_dec_kdk_ctx = {
                "KDK"   : kdk_to_use,
                "Label" : hex_to_str(kdf_list[KdfArg.FWSTR]),  # Note this is passed in
            }

            fw_dec_kdk_ctx['Msg'] = get_composed_msg(fw_dec_kdk_ctx['Label'], '', L, is_hex)
        else:
            fw_dec_kdk_ctx['Msg'] = None

    else:
        bl_dec_kdk_ctx['Msg'] = None
        fw_dec_kdk_ctx['Msg'] = None

    dec_kdk_params = params['DEC_KDK'][kdk_to_use]

    dec_kdk_ctx = {
        'KDK'   : basic_params[dec_kdk_params['KDK']],
        'KDD'   : basic_params[dec_kdk_params['KDD']],
        'Label' : dec_kdk_params['Label'],
    }

    dec_kdk_ctx["Msg"] = get_composed_msg(dec_kdk_ctx['Label'], '', L, is_str)

    # Pop the elements that are no longer needed
    while (len(kdf_list) > KdfArg.FLAG):
        kdf_list.pop()

    return ([dec_kdk_ctx['KDK'] + dec_kdk_ctx['KDD'], dec_kdk_ctx["Msg"],
            bl_dec_kdk_ctx["Msg"], kdk_ctx["Msg"], dk_ctx["Msg"]])

def do_kdf(params_slist, kdf_list):
    base_name = script_dir + 'v3_kdf_' + pid
    raw_name = base_name + '.raw'
    result_name = base_name + '.tag'
    raw_file = open_file(raw_name, 'wb')

    # to write to file
    # order: sizes then data for: deckdk_kdkkdd, deckdk_msg, (bl_deckdk_msg), kdk_msg, dk_msg, iv, aad, tag, src, result_name

    for param in params_slist:
        if param == None:
            arr = int_2bytes(4, 0)
        else:
            arr = int_2bytes(4, len(str_to_hex(param)))
        write_file(raw_file, arr)

    for kdf in kdf_list:
        arr = int_2bytes(4, len(kdf))
        write_file(raw_file, arr)

    arr = int_2bytes(4, len(result_name) + 1)
    write_file(raw_file, arr)

    for param in params_slist:
        if param != None:
            write_file(raw_file, str_to_hex(param))

    for kdf in kdf_list:
        write_file(raw_file, kdf)

    write_file(raw_file, result_name.encode("utf-8"))
    nullarr = bytearray(1)
    nullarr[0] = 0          # need this null for char*
    write_file(raw_file, nullarr)
    raw_file.close()

    command = exec_file(TegraOpenssl)
    command.extend(['--kdf', raw_name])

    ret_str = run_command(command)

    if check_file(result_name):
        result_fh = open_file(result_name, 'rb')
        buff_dgst = result_fh.read()
        kdf_list[KdfArg.TAG] = buff_dgst[:]

        with open(raw_name, 'rb') as f:
            buff_data = bytearray(f.read())
            src_bytes = len(kdf_list[KdfArg.SRC])
            result_bytes = len(result_name) + 1
            payload_offset = len(buff_data) - src_bytes - result_bytes
            kdf_list[KdfArg.SRC] = buff_data[payload_offset:payload_offset+src_bytes]
        result_fh.close()
        os.remove(raw_name)
        os.remove(result_name)
        return True
    os.remove(raw_name)
    return False

def do_derive_dk(dk, params, kdf_list, p_key):
    dk_list = params['DK']

    if dk in dk_list:
        if p_key.kdf.deviceid.is_t234() == True:
            params_slist = do_kdf_params_t234(dk, params, kdf_list)
        else:
            from tegrasign_v3_nvkey_load import do_kdf_params
            return do_kdf_params(dk, params, kdf_list)

        return do_kdf(params_slist, kdf_list)
    raise tegrasign_exception('Can not derive %s' % (dk))

def do_kdf_params_oem_t234(dk, params, kdf_list, p_key):
    # Note some kdf is using string operation, some are hex operation
    is_hex = True
    is_str = False
    L = 256
    basic_params = params['BASIC']

    dk_params = params['DK'][dk]
    dk_ctx = {
        "KDK" : dk_params['KDK'],
        'Label'   : p_key.kdf.label.get_strbuf(),
        'Context' : p_key.kdf.context.get_strbuf(),
    }

    dk_ctx["Msg"] = get_composed_msg(dk_ctx['Label'], dk_ctx['Context'], L, is_hex)

    kdk_params = params['KDK'][dk_ctx['KDK']]
    kdk_to_use = kdk_params['KDK']
    kdk_upstream = kdk_to_use
    kdk_ctx = {
        "KDK" : kdk_to_use,
        "Label" : kdk_params["Label"],
    }

    kdk_ctx['Msg'] = get_composed_msg(kdk_ctx['Label'], '', L, is_str)
    bl_kdk_ctx = {}
    fw_kdk_ctx = {}
    gp_kdk_ctx = {}
    gpto_kdk_ctx = {}
    tz_kdk_ctx = {}
    bl_kdk_ctx['Msg'] = None
    fw_kdk_ctx['Msg'] = None
    gp_kdk_ctx['Msg'] = None
    gpto_kdk_ctx['Msg'] = None
    tz_kdk_ctx['Msg'] = None
    count = 5
    while kdk_to_use in ['SBK_NVMB_KDK', 'SBK_TZ_KDK', 'SBK_GP_KDK', 'SBK_GP_TOSB_KDK', 'SBK_FW_KDK'] and (count>0):
        # Check if sbk_bl_kdk is defined for this dk
        if 'SBK_NVMB_KDK' in kdk_to_use:
            bl_kdk_params = params['KDK'][kdk_to_use]
            kdk_to_use = bl_kdk_params['KDK']

            if p_key.kdf.deviceid.is_t234() == True:
                bl_kdk_ctx = {
                    'KDK'   : kdk_to_use,
                    'Label' : p_key.kdf.bl_label.get_strbuf(),
                }
                bl_kdk_ctx['Msg'] = get_composed_msg(bl_kdk_ctx['Label'], '', L, is_hex)
            else:
                bl_kdk_ctx = {
                    'KDK'   : kdk_to_use,
                    'Label' : bl_kdk_params['Label'],
                    'Context' : p_key.kdf.bl_label.get_strbuf(),
                }
                bl_kdk_ctx['Msg'] = get_composed_msg(bl_kdk_ctx['Label'], bl_kdk_ctx['Context'], L, False)

        elif 'SBK_TZ_KDK' in kdk_to_use:
            tz_kdk_params = params['KDK'][kdk_to_use]
            kdk_to_use = tz_kdk_params['KDK']
            tz_kdk_ctx = {
                'KDK'   : kdk_to_use,
                'Label' : p_key.kdf.tz_label.get_strbuf()
            }

            tz_kdk_ctx['Msg'] = get_composed_msg(tz_kdk_ctx['Label'], '', L, is_hex)

        elif 'SBK_GP_KDK' in kdk_to_use:
            gp_kdk_params = params['KDK'][kdk_to_use]
            kdk_to_use = gp_kdk_params['KDK']
            gp_kdk_ctx = {
                'KDK'   : kdk_to_use,
                'Label' : p_key.kdf.gp_label.get_strbuf()
            }

            gp_kdk_ctx['Msg'] = get_composed_msg(gp_kdk_ctx['Label'], '', L, is_hex)

        elif 'SBK_GP_TOSB_KDK' in kdk_to_use:
            gpto_kdk_params = params['KDK'][kdk_to_use]
            kdk_to_use = gpto_kdk_params['KDK']
            gpto_kdk_ctx = {
                'KDK'   : kdk_to_use,
                'Label' : '544F5342', # 'TOSB'
            }

            gpto_kdk_ctx['Msg'] = get_composed_msg(gpto_kdk_ctx['Label'], '', L, is_hex)

        # Check if sbk_fw_kdk is defined for this dk
        elif 'SBK_FW_KDK' in kdk_to_use:
            fw_kdk_params = params['KDK'][kdk_to_use]
            kdk_to_use = fw_kdk_params['KDK']
            fw_kdk_ctx = {
                "KDK"   : kdk_to_use,
                "Label" : p_key.kdf.fw_label.get_strbuf(),
            }

            fw_kdk_ctx['Msg'] = get_composed_msg(fw_kdk_ctx['Label'], '', L, is_hex)

        count = count - 1

    aes_params = params['AES'][kdk_to_use]
    aes_iv = manifest_xor_offset(basic_params[aes_params['IV']], aes_params["Offset"])
    aes_aad = aes_params['Manifest'] + AAD_0_96
    aes_tag = bytes(16)

    dec_kdk_params = params['DEC_KDK'][aes_params['KDK']]

    dec_kdk_ctx = {
        'KDK'   : basic_params[dec_kdk_params['KDK']],
        'KDD'   : basic_params[dec_kdk_params['KDD']],
        "Label" : dec_kdk_params["Label"],
    }

    dec_kdk_ctx["Msg"] = get_composed_msg(dec_kdk_ctx['Label'], '', L, is_str)

    # Pop the elements that are no longer needed
    while (len(kdf_list) > KdfArg.DKSTR):
        kdf_list.pop()

    # Replace sbk key str if the sbk key file is found
    sbk_keystr = aes_params["Plain"]
    if p_key.filename != None and os.path.exists(p_key.filename):
        with open(p_key.filename, 'rb') as f:
            key_buf = bytearray(f.read())
            if extract_AES_key(key_buf, p_key):
                sbk_keystr = hex_to_str(p_key.key.aeskey)

    return [dec_kdk_ctx['KDK'] + dec_kdk_ctx['KDD'], aes_iv,  aes_aad, sbk_keystr, dec_kdk_ctx['Msg'],
            bl_kdk_ctx['Msg'], tz_kdk_ctx['Msg'], gp_kdk_ctx['Msg'], gpto_kdk_ctx['Msg'],  kdk_ctx['Msg'], dk_ctx['Msg']]

def do_kdf_params_oem(dk, params, kdf_list, p_key):
    # Note some kdf is using string operation, some are hex operation
    is_hex = True
    is_str = False
    L = 256
    basic_params = params['BASIC']

    dk_params = params['DK'][dk]
    dk_ctx = {
        "KDK" : dk_params['KDK'],
        'Label'   : p_key.kdf.label.get_strbuf(),
        'Context' : p_key.kdf.context.get_strbuf(),
    }

    dk_ctx["Msg"] = get_composed_msg(dk_ctx['Label'], dk_ctx['Context'], L, is_hex)

    kdk_params = params['KDK'][dk_ctx['KDK']]
    kdk_to_use = kdk_params['KDK']
    kdk_upstream = kdk_to_use
    kdk_ctx = {
        "KDK" : kdk_to_use,
        "Label" : kdk_params["Label"],
    }

    kdk_ctx['Msg'] = get_composed_msg(kdk_ctx['Label'], '', L, is_str)
    bl_kdk_ctx = {}
    fw_kdk_ctx = {}
    gp_kdk_ctx = {}
    gpto_kdk_ctx = {}
    tz_kdk_ctx = {}
    rcm_kdk_ctx = {}
    bl_kdk_ctx['Msg'] = None
    fw_kdk_ctx['Msg'] = None
    gp_kdk_ctx['Msg'] = None
    gpto_kdk_ctx['Msg'] = None
    tz_kdk_ctx['Msg'] = None
    rcm_kdk_ctx['Msg'] = None

    sbk_list = ['SBK_NVMB_KDK', 'SBK_TZ_KDK', 'SBK_GP_KDK', 'SBK_GP_TOSB_KDK', 'SBK_FW_KDK']
    count = len(sbk_list)
    while kdk_to_use in sbk_list and (count>0):
        # Check if sbk_bl_kdk is defined for this dk
        if 'SBK_NVMB_KDK' in kdk_to_use:
            bl_kdk_params = params['KDK'][kdk_to_use]
            kdk_to_use = bl_kdk_params['KDK']

            if p_key.kdf.deviceid.is_t234() == True:
                bl_kdk_ctx = {
                    'KDK'   : kdk_to_use,
                    'Label' : p_key.kdf.bl_label.get_strbuf(),
                }
                bl_kdk_ctx['Msg'] = get_composed_msg(bl_kdk_ctx['Label'], '', L, is_hex)
            else:
                bl_kdk_ctx = {
                    'KDK'   : kdk_to_use,
                    'Label' : bl_kdk_params['Label'],
                    'Context' : p_key.kdf.bl_label.get_strbuf(),
                }
                bl_kdk_ctx['Msg'] = get_composed_msg(bl_kdk_ctx['Label'], bl_kdk_ctx['Context'], L, False)

        elif 'SBK_TZ_KDK' in kdk_to_use:
            tz_kdk_params = params['KDK'][kdk_to_use]
            kdk_to_use = tz_kdk_params['KDK']
            tz_kdk_ctx = {
                'KDK'   : kdk_to_use,
                'Label' : p_key.kdf.tz_label.get_strbuf()
            }

            tz_kdk_ctx['Msg'] = get_composed_msg(tz_kdk_ctx['Label'], '', L, is_hex)

        elif 'SBK_GP_KDK' in kdk_to_use:
            gp_kdk_params = params['KDK'][kdk_to_use]
            kdk_to_use = gp_kdk_params['KDK']
            gp_kdk_ctx = {
                'KDK'   : kdk_to_use,
                'Label' : p_key.kdf.gp_label.get_strbuf()
            }

            gp_kdk_ctx['Msg'] = get_composed_msg(gp_kdk_ctx['Label'], '', L, is_hex)

        elif 'SBK_GP_TOSB_KDK' in kdk_to_use:
            gpto_kdk_params = params['KDK'][kdk_to_use]
            kdk_to_use = gpto_kdk_params['KDK']
            gpto_kdk_ctx = {
                'KDK'   : kdk_to_use,
                'Label' : '544F5342', # 'TOSB'
            }

            gpto_kdk_ctx['Msg'] = get_composed_msg(gpto_kdk_ctx['Label'], '', L, is_hex)

        # Check if sbk_fw_kdk is defined for this dk
        elif 'SBK_FW_KDK' in kdk_to_use:
            fw_kdk_params = params['KDK'][kdk_to_use]
            kdk_to_use = fw_kdk_params['KDK']
            fw_kdk_ctx = {
                "KDK"   : kdk_to_use,
                "Label" : p_key.kdf.fw_label.get_strbuf(),
            }

            fw_kdk_ctx['Msg'] = get_composed_msg(fw_kdk_ctx['Label'], '', L, is_hex)

        count = count - 1

    aes_params = params['AES'][kdk_to_use]
    aes_iv = manifest_xor_offset(basic_params[aes_params['IV']], aes_params["Offset"])
    aes_aad = aes_params['Manifest'] + AAD_0_96
    aes_tag = bytes(16)

    dec_kdk_params = params['DEC_KDK'][aes_params['KDK']]

    dec_kdk_ctx = {
        'KDK'   : basic_params[dec_kdk_params['KDK']],
        'KDD'   : basic_params[dec_kdk_params['KDD']],
        "Label" : dec_kdk_params["Label"],
    }

    dec_kdk_ctx["Msg"] = get_composed_msg(dec_kdk_ctx['Label'], '', L, is_str)

    # Pop the elements that are no longer needed
    while (len(kdf_list) > KdfArg.DKSTR):
        kdf_list.pop()

    # Replace sbk key str if the sbk key file is found
    sbk_keystr = aes_params["Plain"]
    if p_key.filename != None and os.path.exists(p_key.filename):
        with open(p_key.filename, 'rb') as f:
            key_buf = bytearray(f.read())
            if extract_AES_key(key_buf, p_key):
                sbk_keystr = hex_to_str(p_key.key.aeskey)

    return [dec_kdk_ctx['KDK'] + dec_kdk_ctx['KDD'], aes_iv,  aes_aad, sbk_keystr, dec_kdk_ctx['Msg'],
            bl_kdk_ctx['Msg'], tz_kdk_ctx['Msg'], gp_kdk_ctx['Msg'], gpto_kdk_ctx['Msg'],  kdk_ctx['Msg'], dk_ctx['Msg']]

# calls for offset, then enc, then do sha and returns
def do_kdf_oem_enc(kdf_list, p_key, blockSize):

    p_key.kdf.flag = DerKey.SBK_PT
    file_base, file_ext = os.path.splitext(p_key.src_file)
    temp_stem = file_base + '_tmp'
    enc_file = temp_stem  + file_ext
    final_file = file_base + '_encrypt'  + file_ext
    kdf_yaml = 'kdf_args_%s.yaml' %(temp_stem)
    shutil.copyfile(p_key.src_file, enc_file)

    command = exec_file(TegraOpenssl)
    command.extend(['--chip'])
    command.extend(p_key.kdf.deviceid.chipid_all())
    if p_key.kdf.bootmode != None and p_key.kdf.bootmode.upper() == 'RCM':
        command.extend(['--isRcmBoot'])
    command.extend(['--oem_encrypt', enc_file, kdf_yaml])

    ret_str = run_command(command)
    with open(enc_file, 'rb') as f:
        src = bytearray(f.read())

    patt = 'kdf_args_' + temp_stem + '.yaml'
    pattd = re.compile('kdf_args_' + temp_stem + '(\d).yaml')
    contents = os.listdir('.')

    for f in contents:
        md = pattd.match(f)

        if (md and len(md.groups()) > 0) or (f == patt):
            p_key.kdf.parse_file(p_key, f)
            dk, params = load_params_oem(p_key)
            dk_list = params['DK']
            if dk not in dk_list:
                raise tegrasign_exception('Can not derive %s' % (dk))

            pay_off = int.from_bytes(p_key.kdf.pay_off.get_hexbuf(),  "little")
            pay_sz = int.from_bytes(p_key.kdf.pay_sz.get_hexbuf(),  "little")
            kdf_list = [p_key.kdf.iv.get_hexbuf(), p_key.kdf.aad.get_hexbuf(), p_key.kdf.tag.get_hexbuf(), \
                    src[pay_off:pay_off+pay_sz], p_key.kdf.flag, p_key.kdf.label.get_hexbuf(), p_key.kdf.context.get_hexbuf(), \
                    p_key.kdf.bl_label.get_hexbuf(), p_key.kdf.fw_label.get_hexbuf()]

            if p_key.kdf.deviceid.is_t234() == True:
                params_slist = do_kdf_params_oem_t234(dk, params, kdf_list, p_key)
            else:
                params_slist = do_kdf_params_oem(dk, params, kdf_list, p_key)

            # if user kdk is enabled, only params_slist[10] (DkMsg) is required.
            if p_key.kdf.enc == 'USER_KDK':
                do_kdf_with_user_kdk(params_slist[10], kdf_list, p_key, blockSize)
            else:
                if (do_kdf_oem(params_slist, kdf_list, blockSize) == False):
                    return False
            tag_off = int.from_bytes(p_key.kdf.tag_off.get_hexbuf(),  "little")
            # pad the tag and encrypted buffer
            src[pay_off:pay_off+pay_sz] = kdf_list[KdfArg.SRC][:]
            src[tag_off:tag_off+len(kdf_list[KdfArg.TAG])] = kdf_list[KdfArg.TAG]
            if md:
                enc_file = temp_stem + str(md.group(1)) + '_encrypt'
            else:
                enc_file = temp_stem + '_encrypt'
            with open(enc_file, 'wb') as enc_f:
                enc_f.write(src)

            enc_file_sha = compute_sha('sha512', enc_file, pay_off, pay_sz)

            with open(enc_file_sha, 'rb') as enc_f:
                dgt_buf = bytearray(enc_f.read())
                dgt_off = int.from_bytes(p_key.kdf.dgt_off.get_hexbuf(),  "little")
                src[dgt_off:dgt_off+len(dgt_buf[:])] = dgt_buf[:]
                kdf_list[KdfArg.SRC] = src[:]
            os.remove(f)
            os.remove(enc_file)
            os.remove(enc_file_sha)
    with open(final_file, 'wb') as f:
        f.write(src)
    return True

def do_kdf_with_user_kdk(DkMsgStr, kdf_list, p_key, blockSize):
    DkMsg = str_to_hex(DkMsgStr)
    p_key.key.aeskey = do_hmac_sha256(DkMsg, len(DkMsg), p_key)
    p_key.block_size = int(blockSize)

    iv  = p_key.kdf.iv.get_hexbuf()
    aad  = p_key.kdf.aad.get_hexbuf()
    tag  = p_key.kdf.tag.get_hexbuf()

    if (type(p_key.kdf.verify) == int):
        verify_bytes = p_key.kdf.verify
    else:
        verify_bytes = len(p_key.kdf.verify) + 1

    if p_key.block_size == 0:
        kdf_list[KdfArg.SRC] = do_aes_gcm(kdf_list[KdfArg.SRC], len(kdf_list[KdfArg.SRC]), p_key, iv, aad, tag, verify_bytes, True)
        kdf_list[KdfArg.TAG] = p_key.kdf.tag.get_hexbuf()
        return;

    # Use block_size to do metablob encryption
    iv_off = 0
    iv_size = 12
    tag_off = 12
    tag_size = 16
    dgt_off = 28
    dgt_size = 64
    blob_sz = iv_size + tag_size + dgt_size

    last_blk_len = p_key.len % p_key.block_size
    blk_cnt = int(p_key.len / p_key.block_size) + (1 if (last_blk_len != 0) else 0)
    blob_alloc_sz = blob_sz * (blk_cnt + 1)
    blob_buf = bytearray(blob_alloc_sz)

    # Get ramdom strings
    p_key.ran.size = iv_size
    p_key.ran.count = blk_cnt - 1 # Use p_key's IV for the first itereation
    p_key.Sha = Sha._512
    do_random(p_key)

    # write blk_cnt to the first blob
    blob_buf[0:4] = int_2bytes(4, blk_cnt)

    for i in range(blk_cnt):
        if i+1 == blk_cnt:
            p_key.len = last_blk_len
        else:
            p_key.len = p_key.block_size

        start = i * p_key.block_size
        end = start + p_key.len

        kdf_list[KdfArg.SRC][start:end] = do_aes_gcm(kdf_list[KdfArg.SRC][start:end], p_key.len, p_key, iv, aad, tag, verify_bytes, True)
        buff = bytearray(hashlib.sha512(kdf_list[KdfArg.SRC][start:end]).digest())

        # Start writing to the 2nd blob b/c first blob has the blk_cnt
        start = (i+1) * blob_sz
        end = start + iv_size
        blob_buf[start+iv_off:start+iv_off+iv_size] = p_key.kdf.iv.get_hexbuf()
        blob_buf[start+tag_off:start+tag_off+tag_size] = p_key.kdf.tag.get_hexbuf()
        blob_buf[start+dgt_off:start+dgt_off+dgt_size] = buff[:]

        # Get iv for the next itereation
        start = i * iv_size
        end = start + iv_size
        p_key.kdf.iv.set_buf(p_key.ran.buf[start:end])

    p_key.len = len(p_key.src_buf)
    # Save blob to the tag field
    p_key.kdf.tag.set_buf(blob_buf)

def do_kdf_oem(params_slist, kdf_list, blockSize):
    if is_hsm():
        from tegrasign_v3_hsm import do_kdf_oem_hsm
        p_key = SignKey()
        p_key.hsm.type = KeyType.SBK
        p_key.kdf.flag = kdf_list[KdfArg.FLAG]
        p_key.kdf.iv.set_buf(kdf_list[KdfArg.IV])
        p_key.kdf.aad.set_buf(kdf_list[KdfArg.AAD])
        p_key.kdf.tag.set_buf(kdf_list[KdfArg.TAG])
        p_key.src_buf = kdf_list[KdfArg.SRC]
        p_key.block_size = int(blockSize)
        if do_kdf_oem_hsm(params_slist, p_key) == True:
            kdf_list[KdfArg.SRC] = p_key.src_buf
            kdf_list[KdfArg.TAG] = p_key.kdf.tag.get_hexbuf()
            return True
        return False

    base_name = script_dir + 'v3_aeskdf_' + pid
    raw_name = base_name + '.raw'
    result_name = base_name + '.tag'
    raw_file = open_file(raw_name, 'wb')

    # to write to file
    # order: sizes then data for: deckdk_kdkkdd, deckdk_iv, deckdk_aad, sbk_plain, deckdk_msg, tzkdk_msg, gpkdk_msg,
    #        gptokdk_msg, kdk_msg, dk_msg, iv, aad, tag, src, flag, result_name

    for param in params_slist:
        if param == None:
            arr = int_2bytes(4, 0)
        else:
            arr = int_2bytes(4, len(str_to_hex(param)))
        write_file(raw_file, arr)

    for kdf in kdf_list:
        arr = int_2bytes(4, len(kdf))
        write_file(raw_file, arr)

    arr = int_2bytes(4, len(result_name) + 1)
    write_file(raw_file, arr)

    for param in params_slist:
        if param != None:
            write_file(raw_file, str_to_hex(param))

    for kdf in kdf_list:
        if (type(kdf) == str) and (len(kdf) == 1): # handles flag that is a 1-char str
            arr = int_2bytes(1, ord(kdf))
            write_file(raw_file, arr)
        else:
             write_file(raw_file, kdf)

    write_file(raw_file, result_name.encode("utf-8"))
    nullarr = bytearray(1)
    nullarr[0] = 0          # need this null for char*
    write_file(raw_file, nullarr)
    raw_file.close()

    command = exec_file(TegraOpenssl)
    command.extend(['--kdfoem', raw_name])
    if blockSize  != "0":
        command.extend(['--block', str(blockSize)])

    ret_str = run_command(command)

    if check_file(result_name):
        result_fh = open_file(result_name, 'rb')
        buff_dgst = result_fh.read()
        kdf_list[KdfArg.TAG] = buff_dgst[:]

        with open(raw_name, 'rb') as f:
            buff_data = bytearray(f.read())
            src_bytes = len(kdf_list[KdfArg.SRC])
            flg_bytes = len(kdf_list[KdfArg.FLAG])
            result_bytes = len(result_name) + 1
            payload_offset = len(buff_data) - src_bytes - result_bytes - flg_bytes
            kdf_list[KdfArg.SRC] = buff_data[payload_offset:payload_offset+src_bytes]
        result_fh.close()
        os.remove(result_name)
        os.remove(raw_name)
        return True
    os.remove(raw_name)
    return False

def do_derive_dk_oem(dk, params, kdf_list, p_key, blockSize):
    dk_list = params['DK']

    if dk in dk_list:
        if p_key.kdf.deviceid.is_t234() == True:
            params_slist = do_kdf_params_oem_t234(dk, params, kdf_list, p_key)
        else:
            from tegrasign_v3_nvkey_load import do_kdf_params_oem
            params_slist = do_kdf_params_oem(dk, params, kdf_list, p_key)

        return do_kdf_oem(params_slist, kdf_list, blockSize)
    raise tegrasign_exception('Can not derive %s' % (dk))

def map_bin_to_dk_oem(p_key, params):
    if p_key.kdf.dk != None:
        return p_key.kdf.dk

    magicid = p_key.kdf.magicid
    if magicid != None:
        # To find the DK for this magic id
        kdk_params = params.get('KDK')
        dk_params = params.get('DK')
        for kdk in kdk_params:
            kdk_val = kdk_params.get(kdk)
            if len(kdk_val) == 1:
                continue
            if kdk_val['Label'] == magicid:
                for dk in dk_params:
                   if (kdk == dk_params.get(dk)['KDK']):
                       return dk

    enc_file = p_key.src_file
    basename = os.path.splitext(os.path.basename(enc_file))[0].lower()
    ext = os.path.splitext(os.path.basename(enc_file))[1].lower()

    if 'bpmp' in basename and 'ist' in basename:
        return 'SBK_BPMP_IST_DK'

    if ('bpmp' in basename) and ('.dtb' == ext):
        return 'SBK_BPMP_DTB_DK'

    if 'ape' in basename:
        return 'SBK_APE_DK'

    if 'applet' in basename:
        return 'SBK_BPMP_MB2_DK'

    if 'bpmp' in basename:
        return 'SBK_BPMP_FW_DK'

    if 'br_bct' in basename:
        return 'SBK_BCT_DK'

    if 'cpurf' in basename:
        return 'SBK_MB2_RF_DK'

    if 'dce' in basename:
        return 'SBK_DCE_DK'

    if 'eks' in basename:
        return 'SBK_EKS_DK'

    if 'fsi' in basename:
        return 'SBK_FSI_DK'

    if 'ist' in basename and 'config' in basename: # This is IST-CONFIG
        return 'SBK_IST_CONFIG_DK'

    if 'ist' in basename and 'ucode' in basename:  # This is IST-UCODE (Key ON/OFF IST)
        return 'SBK_IST_UCODE_DK'

    if 'oist' in basename and 'ucode' in basename: # This is CCPLEX-IST-UCODE
        return 'SBK_CCPLEX_IST_DK'

    if 'mb1_bct' in basename or ('mb1' in basename and 'bct' in basename):
        return 'SBK_MB1BCT_DK'

    if 'mb1' in basename:
        return 'SBK_MB1_DK'

    if 'mb2_bct' in basename:
        return 'SBK_MB2BCT_DK'
    # MB2RF will also have mb2 in base name, so check additionally with magic-id
    if 'mb2' in basename and magicid == 'MB2B':
        return 'SBK_MB2_DK'

    if 'mce' in basename:
        return 'SBK_MCE_DK'

    if 'mem' in basename and ('.bct' == ext):
        if '0' in basename:
            return 'SBK_MEMBCT0_DK'
        elif '1' in basename:
            return 'SBK_MEMBCT1_DK'
        elif '2' in basename:
            return 'SBK_MEMBCT2_DK'
        elif '3' in basename:
            return 'SBK_MEMBCT3_DK'

    if 'nvdec' in basename:
        return 'SBK_NVDEC_DK'

    if 'psc_bl' in basename:
        return 'SBK_BL1_DK'

    if 'pscfw' in basename:
        return 'SBK_PSCFW_PKG_DK'

    if 'psc_rf' in basename:
        return 'SBK_PSC_RF_DK'

    if 'rce' in basename:
        return 'SBK_RCE_DK'

    if 'sc7' in basename:
        return 'SBK_SC7_RF_DK'

    if 'sce' in basename:
        return 'SBK_SCE_DK'

    if 'spe' in basename:
        return 'SBK_SPE_DK'

    if 'tz' in basename and 'vault' in basename:
        return 'SBK_TZ_VAULT_DK'

    if 'tos' in basename:
        return 'SBK_TOSB_DK'

    if 'tsec' in basename:
        return 'SBK_TSEC_DK'

    if 'uefi' and 'jetson' in basename:
        return 'SBK_CPU_BL_DK'

    if 'xusb'in basename:
        return 'SBK_XUSB_DK'

    if 'os' in basename or 'hv' in basename:
        return 'SBK_OS_DK'

    raise tegrasign_exception('Can not identify the key choice for %s' % (enc_file))

def load_params_oem(p_key):
    if p_key.kdf.deviceid.is_t234() == True:
        import yaml
        cfg_file = 'tegrasign_v3_oemkey.yaml'
        if os.path.exists(cfg_file) == False:
            cfg_file = script_dir + 'tegrasign_v3_oemkey.yaml'
        with open(cfg_file) as f:
            params = yaml.safe_load(f)
        chipid = p_key.kdf.deviceid.chipid()
        dk = map_bin_to_dk_oem(p_key, params['DER_OEM'][chipid])
        return dk, params['DER_OEM'][chipid]

    else:
        from tegrasign_v3_nvkey_load import load_params_oem_stage
        params = load_params_oem_stage(p_key)
        dk = map_bin_to_dk_oem(p_key, params)
        return dk, params

def do_derive_cbc(p_key):
    # Note some kdf is using string operation
    is_hex = False
    L = 128 # key length in bits

    p_key.kdf.get_composed_msg(L, is_hex, is_hex)

    current_dir_path = os.path.dirname(os.path.realpath(__file__)) + '/'
    base_name = current_dir_path + 'v3_kdfcbc_'
    raw_name = base_name + '.raw'
    raw_file = open_file(raw_name, 'wb')
    filename = p_key.src_file

    result_name = os.path.splitext(filename)[0] + '_encrypt' + os.path.splitext(filename)[1]

    # to write to file
    # order: sizes then data for: msg, iv, src, result_name
    kdf_list = [p_key.kdf.get_hexmsg(), p_key.key.aeskey, p_key.kdf.iv.get_hexbuf(), p_key.get_sign_buf()]

    for kdf in kdf_list:
        if kdf == None:
            arr = int_2bytes(4, 0)
        else:
            arr = int_2bytes(4, len(kdf))
        write_file(raw_file, arr)

    arr = int_2bytes(4, len(result_name) + 1)
    write_file(raw_file, arr)

    for kdf in kdf_list:
        if kdf != None:
             write_file(raw_file, kdf)

    nullarr = bytearray(1)
    nullarr[0] = 0          # need this null for char*

    write_file(raw_file, result_name.encode("utf-8"))
    write_file(raw_file, nullarr)
    raw_file.close()

    command = exec_file(TegraOpenssl)
    command.extend(['--kdfcbc', raw_name])

    ret_str = run_command(command)
    os.remove(raw_name)

    if check_file(result_name):
        return True
    return False

def do_random(p_key):
    if is_hsm():
        from tegrasign_v3_hsm import do_random_hsm
        do_random_hsm(p_key)
    else:
        p_key.ran.buf = bytearray(p_key.ran.size * p_key.ran.count)
        for i in range(p_key.ran.count):
            buf = random_gen(p_key.ran.size)
            start = i * p_key.ran.size
            p_key.ran.buf[start:start+p_key.ran.size] = buf[:]
        info_print('Generated random strings: %s ' %(hex_to_str(p_key.ran.buf)))

    if p_key.filename != 'Unknown':
        with open(p_key.filename, "wb") as f:
            f.write(p_key.ran.buf)

def do_derive_hmacsha(p_key):
    if is_hsm():
        from tegrasign_v3_hsm import do_derive_hmacsha_hsm
        buff_hash = do_derive_hmacsha_hsm(p_key.get_sign_buf(), p_key)
    else:
        key = do_kdf_kdf2(hex_to_str(p_key.key.aeskey), None, p_key.kdf.label.get_strbuf(), p_key.kdf.context.get_strbuf(), True)
        backup = p_key
        if not p_key.kdf.key_already_derived:
            backup.key.aeskey = key
        # Uncomment to generate prederived key
        # with open("hmac_derived_key", "wb") as f:
        #     f.write(key)
        buff_hash = do_hmac_sha256(p_key.get_sign_buf(), p_key.len, backup)

    # save hash to *.hash file
    hash_file_name = os.path.splitext(p_key.src_file)[0] + '.hash'
    with open(hash_file_name, "wb") as f:
        f.write(buff_hash)

def do_derive_aesgcm(p_key, internal):
    if is_hsm():
        from tegrasign_v3_hsm import do_derive_aesgcm_hsm
        buff_enc = do_derive_aesgcm_hsm(p_key.get_sign_buf(), p_key)
    else:
        key = do_kdf_kdf2(hex_to_str(p_key.key.aeskey), None, p_key.kdf.label.get_strbuf(), p_key.kdf.context.get_strbuf(), True)
        backup = p_key
        if not p_key.kdf.key_already_derived:
            backup.key.aeskey = key
        # Uncomment to generate prederived key
        # with open("aes_derived_key", "wb") as f:
        #     f.write(key)
        buff_enc = do_aes_gcm(p_key.get_sign_buf(), p_key.len, backup, internal["--iv"], internal["--aad"], internal["--tag"],
            internal["--verify"], internal["--verbose"])

    with open(p_key.src_file, 'rb') as f:
        buff_data = bytearray(f.read())

    buff_data = buff_data[0:p_key.off] + buff_enc + buff_data[p_key.off + p_key.len:]

    enc_file_name = os.path.splitext(p_key.src_file)[0] + '_encrypt' + os.path.splitext(p_key.src_file)[1]
    with open(enc_file_name, 'wb') as f:
        f.write(buff_data)

    tag_file_name = os.path.splitext(p_key.src_file)[0] + '.tag'
    with open(tag_file_name, 'wb') as f:
        f.write(p_key.kdf.tag.get_hexbuf())

'''
Perform key operation and pad back values for tag & src if successful
'''
def do_key_derivation(p_key, kdf_list, blockSize):
    try:
        info_print('Perform key derivation on ' + p_key.src_file)

        if p_key.kdf.enc == 'OEM' or p_key.kdf.enc == 'USER_KDK':
            return do_kdf_oem_enc(kdf_list, p_key, blockSize)

        elif (kdf_list[KdfArg.FLAG] <= DerKey.NVPDS):
            from tegrasign_v3_nvkey_load import load_params
            dk, params = load_params(p_key)
            return do_derive_dk(dk, params, kdf_list, p_key)

        dk, params = load_params_oem(p_key)
        return do_derive_dk_oem(dk, params, kdf_list, p_key, blockSize)

    except ImportError as e:
        raise tegrasign_exception('Please check setup. Could not find ' + str(e))

    except Exception as e:
        info_print(traceback.format_exc())
        raise tegrasign_exception("Unknown %s requested for key derivation encryption. Error %s" %(p_key.src_file, str(e)))
