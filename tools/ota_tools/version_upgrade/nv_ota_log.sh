#!/bin/bash

# Copyright (c) 2019-2021, NVIDIA CORPORATION.  All rights reserved.
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

# This is a script to enable the log in the OTA process
_OTA_LOG_FILE=
_TMP_OTA_LOG_FILE=/tmp/ota_log.tmp
_ENABLE_CONSOLE_LOG=1

init_ota_log()
{
	local log_dir_path=$1

	if [ "${log_dir_path}" = "" ];then
		echo "Usage: init_ota_log <log dir path>"
		return 1
	fi

	if [ ! -d "${log_dir_path}" ];then
		echo "Creating log dir at ${log_dir_path}"
		mkdir "${log_dir_path}"
		if [ $? -ne 0 ];then
			echo "Failed to create log dir ${log_dir_path}"
			return 1
		fi
	fi

	local log_ts=`date +%Y%m%d-%H%M%S`
	_OTA_LOG_FILE=${log_dir_path}/ota_${log_ts}.log
	echo "Create log file at ${_OTA_LOG_FILE}"
	echo "### OTA log ###" >${_OTA_LOG_FILE}
	return 0
}

ota_log()
{
	local message=$1
	local ota_log_file=${_OTA_LOG_FILE}
	local tmp_ota_log_file=${_TMP_OTA_LOG_FILE}

	if [ "${ota_log_file}" != "" -a -f "${ota_log_file}" ];then
		echo "${message}" >>${ota_log_file}
	else
		echo "${message}" >>${tmp_ota_log_file}
	fi

	if [ ${_ENABLE_CONSOLE_LOG} -eq 1 ];then
		echo "${message}"
	fi
}

get_ota_log_file()
{
	local ota_log_file=${_OTA_LOG_FILE}
	local tmp_ota_log_file=${_TMP_OTA_LOG_FILE}

	if [ "${ota_log_file}" != "" -a -f "${ota_log_file}" ];then
		echo -n "${ota_log_file}"
	else
		echo -n "${tmp_ota_log_file}"
	fi

}
