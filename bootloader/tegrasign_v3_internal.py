#
# Copyright (c) 2018-2020, NVIDIA Corporation.  All Rights Reserved.
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA Corporation is strictly prohibited.
#
from tegrasign_v3_util import isPython3, write_file
import binascii
from xml.etree import ElementTree
from tegrasign_v3_util import *
import os

def is_hsm():

    if os.getenv('NV_ENABLE_HSM'):
        return bool(int(os.getenv('NV_ENABLE_HSM')))
    else:
        return False

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
def sign_files_internal(p_keylist, filenode, pkh, mont):

    filename = filenode.get('name')
    if filename == None:
        info_print('***Missing file name*** ')
        exit_routine()

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
      exit_routine()

    if (offset + length) > file_size:
      info_print('Warning: Offset %d + Length %d is greater than file Size %d for %s' % (offset, length, file_size, filename))
      exit_routine()

    if key_index >= MAX_KEY_LIST:
        info_print('Warning: Key at index %d is not provided ' %(key_index))
        exit_routine()

    buff_to_sign = buff_data[offset : offset + length]

    if p_keylist[key_index].mode == NvTegraSign_SBK:

        sbknode = filenode.find('sbk')
        if sbknode is None:
            info_print('sbk tag is not present.')
            exit_routine()

        skip_enc = 0 if int(sbknode.get('encrypt')) >=1 else 1
        do_sign  = 1 if int(sbknode.get('sign')) >=1 else 0
        enc_file_name = sbknode.get('encrypt_file')
        hash_file_name = sbknode.get('hash')

        NumAesBlocks = int(length/AES_128_HASH_BLOCK_LEN)
        length = int(NumAesBlocks * AES_128_HASH_BLOCK_LEN)

        buff_hash = '0' * AES_128_HASH_BLOCK_LEN
        buff_enc = bytearray(buff_to_sign)

        if skip_enc or is_zero_aes(p_keylist[key_index]):
            info_print('Skipping encryption: ' + filename, True)
        else:
            buff_enc = do_aes_cbc(buff_to_sign, length, p_keylist[key_index])

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

    elif p_keylist[key_index].mode == NvTegraSign_FSKP:

        sbknode = filenode.find('sbk')
        if sbknode is None:
            info_print('sbk tag is not present.')
            exit_routine()

        skip_enc = 0 if int(sbknode.get('encrypt')) >=1 else 1
        do_sign  = 1 if int(sbknode.get('sign')) >=1 else 0
        enc_file_name = sbknode.get('encrypt_file')
        hash_file_name = sbknode.get('hash')

        NumAesBlocks = int(length/AES_256_HASH_BLOCK_LEN)
        length = int(NumAesBlocks*AES_256_HASH_BLOCK_LEN)

        buff_hash = "0" * AES_256_HASH_BLOCK_LEN
        buff_enc = bytearray(buff_to_sign)

        if skip_enc or is_zero_aes(p_keylist[key_index]):
            info_print('Skipping encryption: ' + filename, True)
        else:
            buff_enc = do_aes_cbc(buff_to_sign, length, p_keylist[key_index])

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

    elif p_keylist[key_index].mode == NvTegraSign_ECC:

        ecnode = filenode.find('ec')
        if ecnode is None:
          info_print('ec tag is not present')
          exit_routine()

        sig_file_name = ecnode.get('signature')
        signed_file_name = ecnode.get('signed_file')

        sig_data = do_ecc(buff_to_sign, length, p_keylist[key_index], pkh)

        if sig_file_name:
            sig_fh = open_file(sig_file_name, 'wb')
            write_file(sig_fh, sig_data)
            sig_fh.close()
        else:
            info_print('Not saving Hash')

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
          exit_routine()

        sig_file_name = ednode.get('signature')
        signed_file_name = ednode.get('signed_file')

        sig_data = do_ed25519(buff_to_sign, length, p_keylist[key_index], pkh)

        if sig_file_name:
            sig_fh = open_file(sig_file_name, 'wb')
            write_file(sig_fh, sig_data)
            sig_fh.close()
        else:
            info_print('Not saving Hash')

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
          exit_routine()

        sig_file_name = pkcnode.get('signature')
        signed_file_name = pkcnode.get('signed_file')

        sig_data = do_rsa_pss(buff_to_sign, length, p_keylist[key_index], pkh, mont)

        if sig_file_name:
            sig_fh = open_file(sig_file_name, 'wb')
            write_file(sig_fh, sig_data)
            sig_fh.close()
        else:
            info_print('Not saving Hash')

        if signed_file_name:
            signed_fh = open_file(signed_file_name, 'wb')
            write_file(signed_fh, buff_data)
            signed_fh.close()
        else:
            info_print('Not saving signed file')


def sign_files_in_list(p_keylist, filelistname, pkh, mont):

    try:
        tree = ElementTree.parse(filelistname)

    except IOError:
        info_print('Cannot parse %s as a XML file' %(filelistname))
        exit_routine()

    root = tree.getroot()

    for child in root:
        sign_files_internal(p_keylist, child, pkh, mont)

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


def sign_single_file(p_key, filename, offset, length, skip_enc, do_sign, pkh, mont):
    sign_fh = open_file(filename, 'rb')
    buff_data = sign_fh.read()
    file_size = len(buff_data)
    sign_fh.close()

    offset = offset if offset > 0 else 0
    length = length if length > 0 else file_size - offset

    if file_size < offset:
      length = 0
      info_print('Warning: Offset %d is more than file Size %d for %s' % (offset, file_size, filename))
      exit_routine()

    if (offset + length) > file_size:
      info_print('Warning: Offset %d + Length %d is greater than file Size %d for %s' % (offset, length, file_size, filename))
      exit_routine()

    buff_to_sign = buff_data[offset : offset + length]

    if p_key.mode == NvTegraSign_SBK:

        NumAesBlocks = int(length / AES_128_HASH_BLOCK_LEN)
        length = int(NumAesBlocks * AES_128_HASH_BLOCK_LEN)

        buff_hash = "0" * AES_128_HASH_BLOCK_LEN
        buff_enc = bytearray(buff_to_sign)

        if skip_enc or is_zero_aes(p_key):
            info_print('Skipping encryption: ' + filename, True)
        else:
            buff_enc = do_aes_cbc(buff_to_sign, length, p_key)

        if do_sign:
            buff_hash = do_aes_cmac(buff_enc, length, p_key)

        buff_data = buff_data[0:offset] + buff_enc + buff_data[offset + length:]

        # save encryption to *_encrypt.* file
        enc_file_name = os.path.splitext(filename)[0] + '_encrypt' + os.path.splitext(filename)[1] # ie. rcm_0_encrypt.rcm
        enc_fh = open_file(enc_file_name, 'wb')
        write_file(enc_fh, buff_data)
        enc_fh.close()

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

        if skip_enc or is_zero_aes(p_key):
            info_print('Skipping encryption: ' + filename, True)
        else:
            buff_enc = do_aes_cbc(buff_to_sign, length, p_key)

        if do_sign:
            buff_hash = do_aes_cmac(buff_enc, length, p_key)

        buff_data = buff_data[0:offset] + buff_enc + buff_data[offset + length:]

        # save encryption to *_encrypt.* file
        enc_file_name = os.path.splitext(filename)[0] + '_encrypt' + os.path.splitext(filename)[1] # ie. rcm_0_encrypt.rcm
        enc_fh = open_file(enc_file_name, 'wb')
        write_file(enc_fh, buff_data)
        enc_fh.close()

        # save hash to *.hash file
        hash_file_name = os.path.splitext(filename)[0] + '.hash'
        hash_fh = open_file(hash_file_name, 'wb')
        write_file(hash_fh, buff_hash)
        hash_fh.close()

    elif p_key.mode == NvTegraSign_ECC:

        sig_data = do_ecc(buff_to_sign, length, p_key, pkh)

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

    else:

        sig_data = do_rsa_pss(buff_to_sign, length, p_key, pkh, mont)

        sig_file_name = os.path.splitext(filename)[0] + '.sig'
        sig_fh = open_file(sig_file_name, 'wb')
        write_file(sig_fh, sig_data)
        sig_fh.close()

def do_aes_cmac_hsm(buf, p_key):

    tmpf_in = 'tmp_aes_cmac.in'
    tmpf_out = 'tmp_aes_cmac.mac'

    with open_file(tmpf_in, 'wb') as f:
        write_file(f, buf)

    if p_key.mode == NvTegraSign_SBK:
        # For now, Zero SBK is assumed for AES-CMAC
        key = '00000000000000000000000000000000'
        cipher = 'aes-128-cbc'
    else:
        info_print('[HSM] do_aes_cmac_hsm: read an AES key filename=%s' % p_key.filename)
        # FIXME read AES key content
        #       same content as what's included at p_key.key.aeskey
        with open_file(p_key.filename, 'rb') as f:

            # TODO: command HSM to perform AES CBC with a correct key
            key_ = f.read()
            if key_[:2] == b'0x':
                # The key below is just concatenation of hex literals in fskp.key
                # key format is printable 0x123456578 0x9abcdef0 ...
                key = key_.decode().strip().replace('0x', '').replace(' ', '')
            else:
                # key format is in a binary sequence
                key = binascii.hexlify(key_).decode('ascii')

        cipher = 'aes-256-cbc'

    runcmd = 'openssl dgst -mac cmac -macopt cipher:%s -macopt hexkey:%s -binary -out %s %s' % (cipher, key, tmpf_out, tmpf_in)
    info_print('[HSM] calling %s' % runcmd)
    try:
        subprocess.check_call(runcmd, shell=True)
    except subprocess.CalledProcessError:
        print("[HSM] ERROR: failure in running %s" % runcmd)
        exit_routine()
    finally:
        os.remove(tmpf_in)

    with open_file(tmpf_out, 'rb') as f:
        cmac = f.read()

    os.remove(tmpf_out)

    info_print('[HSM] aes cmac is done... return')

    return cmac

def do_aes_cmac(buff_to_sign, length, p_key):
    buff_sig = "0" * 16 # note cmac will always return 128bit

    if is_hsm():
        return do_aes_cmac_hsm(buff_to_sign, p_key)

    current_dir_path = os.path.dirname(os.path.realpath(__file__))
    raw_name = current_dir_path + '/aescmac_raw.bin'
    result_name = current_dir_path + '/aescmac_out.bin'
    raw_file = open_file(raw_name, 'wb')

    key_bytes = len(binascii.hexlify(p_key.key.aeskey))/2
    keysize_bytes = int_2byte_cnt(p_key.keysize)
    len_bytes = int_2byte_cnt(length)
    sign_bytes = len(buff_to_sign)
    sig_bytes = len(buff_sig)
    result_bytes = len(result_name) + 1

    # to write to file
    # order: sizes then data for: key, keysize, length, buff_to_sign, buff_sig, result_name

    arr = int_2bytes(4, key_bytes)
    write_file(raw_file, arr)
    arr = int_2bytes(4, keysize_bytes)
    write_file(raw_file, arr)
    arr = int_2bytes(4, len_bytes)
    write_file(raw_file, arr)
    arr = int_2bytes(4, sign_bytes)
    write_file(raw_file, arr)
    arr = int_2bytes(4, sig_bytes)
    write_file(raw_file, arr)
    arr = int_2bytes(4, result_bytes)
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
    command.extend(['--aesCmac', raw_name])

    ret_str = run_command(command)

    if check_file(result_name):
        result_fh = open_file(result_name, 'rb')
        buff_sig = result_fh.read()
        result_fh.close()
        os.remove(result_name)

    os.remove(raw_name)
    return buff_sig

def do_aes_cbc_hsm(buf, p_key):

    tmpf_in = 'tmp_aes_cbc.in'
    tmpf_out = 'tmp_aes_cbc.enc'

    # FIXME: replace a below std openssl aes-cbc with HSM FSKP
    #        put the key label or slot into the name with dummy key info
    #        e.g.) aes128_slot0_key0.key

    with open_file(tmpf_in, 'wb') as f:
        write_file(f, buf)

    with open_file(p_key.filename, 'rb') as f:
        info_print('[HSM] do_aes_cbc_hsm: AES key filename=%s' % p_key.filename)

        # FIXME: command HSM to perform AES CBC with a correct key
        #        read the same key value as what's included at p_key.key.aeskey
        key_ = f.read()
        if key_[:2] == b'0x':
            # key format is printable 0x123456578 0x9abcdef0 ...
            key = key_.decode().strip().replace('0x', '').replace(' ', '')
        else:
            # key format is in a binary sequence
            key = binascii.hexlify(key_).decode('ascii')
    # NOTE: IV = 0
    iv  = '00000000000000000000000000000000'

    runcmd = "openssl enc -e -aes-256-cbc -nopad -in %s -out %s -K %s -iv %s" % (tmpf_in, tmpf_out, key, iv)
    info_print('[HSM] calling %s' % runcmd)
    try:
        subprocess.check_call(runcmd, shell=True)
    except subprocess.CalledProcessError:
        print("[HSM] ERROR: failure in running %s" % runcmd)
        exit_routine()
    finally:
        os.remove(tmpf_in)

    with open_file(tmpf_out, 'rb') as f:
        buf_enc = f.read()

    os.remove(tmpf_out)

    info_print('[HSM] aes-cbc-256 is done... return')

    return buf_enc

def do_aes_cbc(buff_to_enc, length, p_key):

    if is_hsm():
        return do_aes_cbc_hsm(buff_to_enc, p_key)

    current_dir_path = os.path.dirname(os.path.realpath(__file__))
    raw_name = current_dir_path + '/aescbc_raw.bin'
    result_name = current_dir_path + '/aescbc_out.bin'
    raw_file = open_file(raw_name, 'wb')

    key_bytes = len(binascii.hexlify(p_key.key.aeskey))/2
    keysize_bytes = int_2byte_cnt(p_key.keysize)
    len_bytes = int_2byte_cnt(length)
    enc_bytes = len(buff_to_enc)
    dest_bytes = int(length)
    result_bytes = len(result_name) + 1
    buff_dest = "0" * int(dest_bytes)
    info_print (dest_bytes)

    # to write to file
    # order: sizes then data for: key, keysize, length, buff_to_enc, buff_dest, result_name

    arr = int_2bytes(4, int(key_bytes))
    write_file(raw_file, arr)
    arr = int_2bytes(4, keysize_bytes)
    write_file(raw_file, arr)
    arr = int_2bytes(4, len_bytes)
    write_file(raw_file, arr)
    arr = int_2bytes(4, enc_bytes)
    write_file(raw_file, arr)
    arr = int_2bytes(4, dest_bytes)
    write_file(raw_file, arr)
    arr = int_2bytes(4, result_bytes)
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
    raw_file.close()

    command = exec_file(TegraOpenssl)
    command.extend(['--aesCbc', raw_name])

    ret_str = run_command(command)

    if check_file(result_name):
        result_fh = open_file(result_name, 'rb')
        buff_sig = result_fh.read()
        result_fh.close()
        os.remove(result_name)

    os.remove(raw_name)
    return buff_sig

def do_rsa_pss_hsm(buf, p_key):

    tmpf_in = 'tmp_rsa_pss.in'
    tmpf_out = 'tmp_rsa_pss.sig'
    tmpf_hash = 'tmp_sha256.hash'
    priv_keyf = p_key.filename

    with open_file(tmpf_in, 'wb') as f:
        write_file(f, buf)

    # rsa_pss_saltlen:-1 means the same length of hash (sha256) here
    # single line execution:
    # runcmd = "openssl dgst -sha256 -sigopt rsa_padding_mode:pss -sigopt rsa_pss_saltlen:-1 -sign %s -out %s %s" % (priv_keyf, tmpf_out, tmpf_in)

    # two separate line execution with intermediate sha256 output
    runcmd1 = "openssl dgst -sha256 -binary -out %s %s" % (tmpf_hash, tmpf_in)
    runcmd2 = "openssl pkeyutl -sign -pkeyopt rsa_padding_mode:pss -pkeyopt rsa_pss_saltlen:-1 -pkeyopt digest:sha256 -in %s -out %s -inkey %s" % (tmpf_hash, tmpf_out, priv_keyf)
    info_print('[HSM] calling %s\n%s' % (runcmd1, runcmd2))
    try:
        subprocess.check_call(runcmd1, shell=True)
        subprocess.check_call(runcmd2, shell=True)
    except subprocess.CalledProcessError:
        print("[HSM] ERROR: failure in running %s, %s" % (runcmd1, runcmd2))
        exit_rotune()
    finally:
        os.remove(tmpf_in)

    with open_file(tmpf_out, 'rb') as f:
        sig_data = swapbytes(bytearray(f.read()))

    os.remove(tmpf_hash)
    os.remove(tmpf_out)

    info_print('[HSM] rsa-pss routine is done... return')

    return sig_data

def do_rsa_pss(buff_to_sign, length, p_key, pkhfile, montfile):

    if is_hsm():
        return do_rsa_pss_hsm(buff_to_sign, p_key)

    buff_sig = "0" * p_key.keysize

    current_dir_path = os.path.dirname(os.path.realpath(__file__))
    raw_name = current_dir_path + '/rsa_raw.bin'
    result_name = current_dir_path + '/rsa_out.bin'
    raw_file = open_file(raw_name, 'wb')

    filename_bytes = len(p_key.filename) + 1 # to account for 0x0
    len_bytes = int_2byte_cnt(length)
    sign_bytes = len(buff_to_sign)

    sig_bytes = len(buff_sig)
    pkh_bytes = 0 if pkhfile == None else (len(pkhfile) + 1)
    mont_bytes = 0 if montfile == None else (len(montfile) + 1)
    result_bytes = len(result_name) + 1

    # order: sizes then data for: file name, length, buff_to_sign, buff_sig, pkhfile, montfile, result_name
    arr = int_2bytes(4, filename_bytes)
    write_file(raw_file, arr)
    arr = int_2bytes(4, len_bytes)
    write_file(raw_file, arr)
    arr = int_2bytes(4, sign_bytes)
    write_file(raw_file, arr)
    arr = int_2bytes(4, sig_bytes)
    write_file(raw_file, arr)

    arr = int_2bytes(4, pkh_bytes)
    write_file(raw_file, arr)
    arr = int_2bytes(4, mont_bytes)
    write_file(raw_file, arr)
    arr = int_2bytes(4, result_bytes)
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

def do_ecc(buff_to_sign, length, p_key, pkhfile):

    buff_sig = "0" * p_key.keysize

    current_dir_path = os.path.dirname(os.path.realpath(__file__))
    raw_name = current_dir_path + '/ecc_raw.bin'
    result_name = current_dir_path + '/ecc_out.bin'
    raw_file = open_file(raw_name, 'wb')

    filename_bytes = len(p_key.filename) + 1 # to account for 0x0
    len_bytes = int_2byte_cnt(length)
    sign_bytes = len(buff_to_sign)
    sig_bytes = len(buff_sig)
    pkh_bytes = 0 if pkhfile == None else (len(pkhfile) + 1)
    result_bytes = len(result_name) + 1

    # order: sizes then data for: file name, length, buff_to_sign, buff_sig, pkhfile, result_name
    arr = int_2bytes(4, filename_bytes)
    write_file(raw_file, arr)
    arr = int_2bytes(4, len_bytes)
    write_file(raw_file, arr)
    arr = int_2bytes(4, sign_bytes)
    write_file(raw_file, arr)
    arr = int_2bytes(4, sig_bytes)
    write_file(raw_file, arr)

    arr = int_2bytes(4, pkh_bytes)
    write_file(raw_file, arr)
    arr = int_2bytes(4, result_bytes)
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

    buff_sig = "0" * p_key.keysize

    current_dir_path = os.path.dirname(os.path.realpath(__file__))
    raw_name = current_dir_path + '/ed_raw.bin'
    result_name = current_dir_path + '/ed_out.bin'
    raw_file = open_file(raw_name, 'wb')

    filename_bytes = len(p_key.filename) + 1 # to account for 0x0
    len_bytes = int_2byte_cnt(length)
    sign_bytes = len(buff_to_sign)
    sig_bytes = len(buff_sig)
    pkh_bytes = 0 if pkhfile == None else (len(pkhfile) + 1)
    result_bytes = len(result_name) + 1

    # order: sizes then data for: file name, length, buff_to_sign, buff_sig, pkhfile, result_name
    arr = int_2bytes(4, filename_bytes)
    write_file(raw_file, arr)
    arr = int_2bytes(4, len_bytes)
    write_file(raw_file, arr)
    arr = int_2bytes(4, sign_bytes)
    write_file(raw_file, arr)
    arr = int_2bytes(4, sig_bytes)
    write_file(raw_file, arr)

    arr = int_2bytes(4, pkh_bytes)
    write_file(raw_file, arr)
    arr = int_2bytes(4, result_bytes)
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

def do_sha256(fileName, offset, length):

    sha_fh = open_file(fileName, 'rb')
    buff_data = sha_fh.read()
    sha_fh.close()

    file_size = len(buff_data)
    length = length if length > 0 else file_size - offset
    offset = offset if offset > 0 else 0

    if file_size < offset:
      length = 0
      info_print('Warning: Offset %d is more than file Size %d for %s' % (offset, file_size, filename))
      exit_routine()

    if (offset + length) > file_size:
      info_print('Warning: Offset %d + Length %d is greater than file Size %d for %s' % (offset, length, file_size, filename))
      exit_routine()

    buff_to_hash = buff_data[offset : offset + length]
    sha_cnt = (256/8)
    buff_hash = "0" * int(sha_cnt)
    len_bytes = int_2byte_cnt(length)
    hash_file_name = os.path.splitext(fileName)[0] + '.sha'
    hash_file_bytes = len(hash_file_name) + 1

    # to write to raw file
    current_dir_path = os.path.dirname(os.path.realpath(__file__))
    raw_name = current_dir_path + '/sha_raw.bin'
    raw_file = open_file(raw_name, 'wb')

    # order: sizes then data for: length, buff_to_hash, buff_hash, hash_file_name
    arr = int_2bytes(4, len_bytes)
    write_file(raw_file, bytes(arr))
    arr = int_2bytes(4, length)
    write_file(raw_file, bytes(arr))
    arr = int_2bytes(4, sha_cnt)
    write_file(raw_file, bytes(arr))
    arr = int_2bytes(4, hash_file_bytes)
    write_file(raw_file, bytes(arr))

    arr = int_2bytes(len_bytes, length)
    write_file(raw_file, bytes(arr))

    write_file(raw_file, bytes(buff_to_hash))
    write_file(raw_file, bytes(buff_hash.encode("utf-8")))
    nullarr = bytearray(1)
    nullarr[0] = 0          # need this null for char*
    write_file(raw_file, bytes(hash_file_name.encode("utf-8")))
    write_file(raw_file, bytes(nullarr))

    raw_file.close()

    command = exec_file(TegraOpenssl)
    command.extend(['--sha', raw_name])

    ret_str = run_command(command)
    if check_file(hash_file_name):
        info_print('Sha saved in ' + hash_file_name)

    os.remove(raw_name)


def extract_AES_key(pBuffer, BufSize, p_key):

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

    if(isPython3()):
        key = binascii.unhexlify(key_str.strip())
    else:
        key = key_str.strip().decode("hex")

    p_key.keysize = int(key_str_length/2)

    p_key.key.aeskey = bytearray(key)

    return 1

def get_rsa_mod_hsm(priv_keyf, key_size, pub_modf=None):

    runcmd = 'openssl rsa -in %s -modulus -noout' % (priv_keyf)
    info_print('[HSM] calling %s' % runcmd)
    try:
        output = subprocess.check_output(runcmd, shell=True).decode("utf-8")
    except subprocess.CalledProcessError:
        print("[HSM] ERROR: failure in running %s" % runcmd)
        exit_routine()
    # Check if the output is 'Modulus=963E...'
    if not output.startswith('Modulus='):
        return False

    rsa_n_bin = swapbytes(bytearray(binascii.unhexlify(output.strip()[len('Modulus='):])))
    key_size = len(rsa_n_bin)
    if pub_modf:
        with open_file(pub_modf, 'wb') as f:
            write_file(f, rsa_n_bin)

    info_print('[HSM] Done - get_rsa_modulus_hsm')

    return True

def is_PKC_key(keyfilename, p_key, pkh, mont):

    command = exec_file(TegraOpenssl)
    pubkeyfile = 'v3_pub_keyhash'
    temp_copy = 0

    # pack the arguments
    if pkh and mont:
        command.extend(['--isPkcKey', keyfilename, pkh, mont])
    elif pkh:
        if is_hsm():
            return get_rsa_mod_hsm(keyfilename, p_key.keysize, pkh)
        command.extend(['--isPkcKey', keyfilename, pkh])
    elif mont:
        command.extend(['--isPkcKey', keyfilename, pubkeyfile, mont])
        temp_copy = 1
    else:
        if is_hsm():
            return get_rsa_mod_hsm(keyfilename, p_key.keysize)
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

    command = exec_file(TegraOpenssl)

    if pkh == None:
        command.extend(['--isEccKey', keyfilename])
    else:
        command.extend(['--isEccKey', keyfilename, pkh])

    ret_str = run_command(command)
    if is_ret_ok(ret_str):
        p_key.keysize = NV_ECC_SIG_STRUCT_SIZE
        return True
    return False

def is_ED25519_key(keyfilename, p_key, pkh):

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
