#!/bin/bash

# Copyright (c) 2016-2020, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA Corporation and its licensors retain all intellectual property and
# proprietary rights in and to this software and related documentation.  Any
# use, reproduction, disclosure or distribution of this software and related
# documentation without an express license agreement from NVIDIA Corporation
# is strictly prohibited.

# This script generates bmp.blob for the product passed as argument
# The config_file from the respective product folder is used to generate the blob
#set -x

function compress_blob()
{
	if ! command -v ${_compress_tool} >/dev/null 2>&1; then
		echo 1>&2 "can't find '${_compress_tool}', skip compressing blob."
		return
	fi

	_blob_file=$1
	_blob_size=`cat ${_blob_file} | wc -c`
	_compressed_blob=${_blob_file}.lz4

	head -c ${_blob_header_len} ${_blob_file} > ${_compressed_blob}

	_blob_size=$((${_blob_size} - ${_blob_header_len}))
	_cmd="${_compress_tool} -l -c1 -f stdin >> ${_compressed_blob}"
	tail -c ${_blob_size} ${_blob_file} | eval ${_cmd}

	# update blob size(4Bytes) in header
	_compressed_size=`cat ${_compressed_blob} | wc -c`

	local _i=${_blob_size_pos}
	local _end=$((${_i} + 4))
	local _byte=0
	while [[ ${_i} -lt ${_end} ]]
	do
		_byte=$((${_compressed_size} % 256))
		_byte=$(printf %x ${_byte})
		printf "\x${_byte}" | dd conv=notrunc of=${_compressed_blob} bs=1 seek=${_i}
		_compressed_size=$((${_compressed_size} / 256))
		_i=$(($_i + 1))
	done

	rm ${_blob_file}
	mv ${_compressed_blob} ${_blob_file}

	return
}


# Sanity checks
if [[ $# -ne 4  && $# -ne 5 ]]; then
    echo 1>&2 "usage: $0 <chip> <product config file> <path to blob_generator> [ <path to lz4c> ] <output file>"
    exit 1
fi

_chip=$1
_config=$2
_blob_generator=$3

if [[ $# -eq 4 ]]; then
	_compress_tool=
	_output=$4
elif [[ $# -eq 5 ]]; then
	_compress_tool=$4
	_output=$5
fi

if [[ ! -r "${_config}" ]]; then
    echo 1>&2 "$0: can't find product configuration file '${_config}'."
    exit 1
fi
if [[ ! -x "${_blob_generator}" ]]; then
    echo 1>&2 "$0: can't find blob_generator binary '${_blob_generator}'."
    exit 1
fi

set -e
rm -f ${_output}

_list=""
_config_dir=$(dirname ${_config})
while read _line
do
	_line="${_config_dir}/${_line}"
	_list="${_list}${_line} "
done < ${_config}

_cmd="${_blob_generator} -t bmp -e \"${_list}\""

# WAR: provide backward compatibility with t21x products, bmp.blob generated
#      for t21x can be parsed by newer products, but not the opposite
[[ ${_chip} = t21* ]] && { _cmd="${_cmd} -v 0"; }

eval ${_cmd}

if [[ -n ${_compress_tool} ]]; then
	# note: below 2 parameters should be synchronous with blob header in ${_blob_generator}
	_blob_size_pos=20
	_header_len_offset=24

	_blob_header_len=$(od -An -j ${_header_len_offset} -N 4 -t d4 bmp.blob)
	_blob_header_len=`echo ${_blob_header_len}`
	compress_blob bmp.blob
fi

# move generated file from source tree to build directory
mv bmp.blob ${_output}

echo "$0: Success! bmp.blob is in '${_output}'."
exit 0
