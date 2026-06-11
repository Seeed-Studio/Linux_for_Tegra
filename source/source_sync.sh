#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2012-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#
# This script syncs NVIDIA's version of:
# 1. the kernel source
# 2. other related repositories
# from nv-tegra, NVIDIA's public git repository.
#
# The script provides options to sync to a specific tag
# so that the binaries shipped with a release can be replicated.
#
# By default, the script uses shallow cloning (--depth 1) for faster downloads
# and smaller disk usage. This is suitable for most use cases where you only
# need the specific tag version.
#
# Usage:
# By default it will download all the listed sources and sync to the latest
# public release tag.
# ./source_sync.sh
#
# Use the -t <TAG> option to provide a specific TAG to be used to sync all the
# sources.
# Use the -k option to download only the kernel and device tree repos.
# Use the -o option to download only non-kernel repos.
# Use the -s option to perform a shallow clone instead of full clone.
#
# The script automatically detects the latest public release tag (with "jetson_"
# prefix) when no tag is specified.
# For detailed usage information run with -h option.
#

function InitVars {
	# source dir
	LDK_DIR=$(cd $(dirname $0) && pwd)
	# script name
	SCRIPT_NAME=$(basename $0)
	# exit on error on sync
	EOE=0
	# after processing SOURCE_INFO
	NSOURCES=0
	declare -a SOURCE_INFO_PROCESSED
	# download all?
	DALL=1
	SHALLOW_CLONE=false
	# Reference repository used for server detection and tag determination
	REFERENCE_REPO="device/hardware/nvidia/tegra-public-dts.git"
	# Tag prefix to select latest tag from a specific release series.
	TAG_PREFIX="jetson_39"
	# Detect and set git server
	GIT_SERVER=$(GetGitServer "${REFERENCE_REPO}")
	# info about sources.
	# NOTE: nvethrnetrm.git should be listed after "linux-nv-oot.git" due to nesting of sync path
	SOURCE_INFO="
k:build/nvidia-public:${GIT_SERVER}/kernel/build/nvidia-public.git:
k:dtc-src/1.4.5:${GIT_SERVER}/3rdparty/dtc-src/1.4.5.git:
k:hardware/nvidia/t23x/nv-public:${GIT_SERVER}/device/hardware/nvidia/t23x-public-dts.git:
k:hardware/nvidia/t264/nv-public:${GIT_SERVER}/device/hardware/nvidia/t264-public-dts.git:
k:hardware/nvidia/tegra/nv-public:${GIT_SERVER}/device/hardware/nvidia/tegra-public-dts.git:
k:hwpm:${GIT_SERVER}/linux-hwpm.git:
k:kernel/kernel-noble:${GIT_SERVER}/3rdparty/canonical/linux-noble.git:
k:nvdisplay:${GIT_SERVER}/tegra/kernel-src/nv-kernel-display-driver.git:
k:nvethernetrm:${GIT_SERVER}/kernel/nvethernetrm.git:
k:nvgpu:${GIT_SERVER}/tegra/kernel-src/linux-nvgpu.git:
k:nvidia-oot:${GIT_SERVER}/linux-nv-oot.git:
k:unifiedgpudisp:${GIT_SERVER}/tegra/kernel-src/nv-unified-gpu-display-driver.git:
o:tegra/ada-src/adaruntime:${GIT_SERVER}/tegra/ada-src/adaruntime.git:
o:tegra/argus-cam-libav/argus_cam_libavencoder:${GIT_SERVER}/tegra/argus-cam-libav/argus_cam_libavencoder.git:
o:tegra/cuda-src/nvsample_cudaprocess:${GIT_SERVER}/tegra/cuda-src/nvsample_cudaprocess.git:
o:tegra/gfx-src/nv-xconfig:${GIT_SERVER}/tegra/gfx-src/nv-xconfig.git:
o:tegra/gst-src/gst-egl:${GIT_SERVER}/tegra/gst-src/gst-egl.git:
o:tegra/gst-src/gst-jpeg:${GIT_SERVER}/tegra/gst-src/gst-jpeg.git:
o:tegra/gst-src/gst-nvarguscamera:${GIT_SERVER}/tegra/gst-src/gst-nvarguscamera.git:
o:tegra/gst-src/gst-nvcompositor:${GIT_SERVER}/tegra/gst-src/gst-nvcompositor.git:
o:tegra/gst-src/gst-nvipcpipeline:${GIT_SERVER}/tegra/gst-src/gst-nvipcpipeline.git:
o:tegra/gst-src/gst-nvsiplcamera:${GIT_SERVER}/tegra/gst-src/gst-nvsiplcamera.git:
o:tegra/gst-src/gst-nvtee:${GIT_SERVER}/tegra/gst-src/gst-nvtee.git:
o:tegra/gst-src/gst-nvunixfd:${GIT_SERVER}/tegra/gst-src/gst-nvunixfd.git:
o:tegra/gst-src/gst-nvv4l2camera:${GIT_SERVER}/tegra/gst-src/gst-nvv4l2camera.git:
o:tegra/gst-src/gst-nvvidconv:${GIT_SERVER}/tegra/gst-src/gst-nvvidconv.git:
o:tegra/gst-src/gst-nvvideo4linux2:${GIT_SERVER}/tegra/gst-src/gst-nvvideo4linux2.git:
o:tegra/gst-src/libgstnvcustomhelper:${GIT_SERVER}/tegra/gst-src/libgstnvcustomhelper.git:
o:tegra/gst-src/libgstnvdrmvideosink:${GIT_SERVER}/tegra/gst-src/libgstnvdrmvideosink.git:
o:tegra/gst-src/libgstnvvideosinks:${GIT_SERVER}/tegra/gst-src/libgstnvvideosinks.git:
o:tegra/gst-src/nvgstapps:${GIT_SERVER}/tegra/gst-src/nvgstapps.git:
o:tegra/gst-src/opencv_gst_samples:${GIT_SERVER}/tegra/gst-src/opencv_gst_samples.git:
o:tegra/hafnium-src/hafnium:${GIT_SERVER}/tegra/hafnium-src/hafnium.git:
o:tegra/nv-sci-src/nvsci_headers:${GIT_SERVER}/tegra/nv-sci-src/nvsci_headers.git:
o:tegra/nv-sci-src/nvsci_samples:${GIT_SERVER}/tegra/nv-sci-src/nvsci_samples.git:
o:tegra/openwfd-src/openwfd_headers:${GIT_SERVER}/tegra/openwfd-src/openwfd_headers.git:
o:tegra/openwfd-src/openwfd_samples:${GIT_SERVER}/tegra/openwfd-src/openwfd_samples.git:
o:tegra/optee-src/atf:${GIT_SERVER}/tegra/optee-src/atf.git:
o:tegra/optee-src/atf_t264:${GIT_SERVER}/tegra/optee-src/atf_t264.git:
o:tegra/optee-src/nv-optee:${GIT_SERVER}/tegra/optee-src/nv-optee.git:
o:tegra/spe-src/spe-freertos-bsp:${GIT_SERVER}/tegra/spe-src/spe-freertos-bsp.git:
o:tegra/v4l2-src/libv4l2_nvargus:${GIT_SERVER}/tegra/v4l2-src/libv4l2_nvargus.git:
o:tegra/v4l2-src/v4l2_libs:${GIT_SERVER}/tegra/v4l2-src/v4l2_libs.git:
o:tegra/webrtc-app-src/argus-camera-app:${GIT_SERVER}/tegra/webrtc-app-src/argus-camera-app.git:
"
}

function GetGitServer {
	local REFERENCE_REPO="$1"
	# Git servers in priority order (will try in order until one works).
	# Note that a colon is needed in the URL when we extract the DNLOAD
	# field elsewhere using the "cut" command.
	local GIT_SERVERS=("https://gitlab.com/nvidia/nv-tegra" "git://nv-tegra.nvidia.com")

	echo "Detecting available git server..." >&2

	for server in "${GIT_SERVERS[@]}"; do
		echo "Testing server: ${server}..." >&2
		if git ls-remote --exit-code "${server}/${REFERENCE_REPO}" \
			> /dev/null 2>&1; then
			echo "Using git server: ${server}" >&2
			echo "${server}"
			return 0
		fi
	done

	# No server is available
	echo "ERROR: No git server is available!" >&2
	echo "Tried servers: ${GIT_SERVERS[*]}" >&2
	exit 1
}

function Usages {
	local ScriptName=$1
	local LINE
	local OP
	local DESC
	local PROCESSED=()
	local INDENT="                    "

	echo "Use: $1 [options]"
	echo ""
	echo "General options:"
	echo "     -h         : Help"
	echo "     -e         : Exit on sync error"
	echo "     -d [DIR]   : Root of source is DIR"
	echo "     -t [TAG]   : Git tag that will be used to sync all the sources"
	echo "     -s         : Perform shallow clone instead of full clone"
	echo ""
	echo "By default, all sources are downloaded and synced to the latest public"
	echo "release tag."
	echo "Only specified sources are downloaded and synced to the specified TAG,"
	echo "if one or more of the following options are mentioned."
	echo ""
	echo "$SOURCE_INFO" | while read LINE; do
		if [ ! -z "$LINE" ]; then
			OP=$(echo "$LINE" | cut -f 1 -d ':')
			DESC=$(echo "$LINE" | cut -f 2 -d ':')
			if [[ ! " ${PROCESSED[@]} " =~ " ${OP} " ]]; then
				echo "     -${OP}         : Download sources for "
				PROCESSED+=("${OP}")
			fi
			echo "${INDENT}${DESC}"
		fi
	done
	echo ""
}

function ProcessSwitch {
	local SWITCH="$1"
	local TAG="$2"
	local i
	local found=0

	for ((i=0; i < NSOURCES; i++)); do
		local OP=$(echo "${SOURCE_INFO_PROCESSED[i]}" | cut -f 1 -d ':')
		if [ "-${OP}" == "$SWITCH" ]; then
			SOURCE_INFO_PROCESSED[i]="${SOURCE_INFO_PROCESSED[i]}${TAG}:y"
			DALL=0
			found=1
		fi
	done

	if [ "$found" == 1 ]; then
		return 0
	fi

	echo "Terminating... wrong switch: ${SWITCH}" >&2
	Usages "$SCRIPT_NAME"
	exit 1
}

function DownloadAndSync {
	local WHAT_SOURCE="$1"
	local LDK_SOURCE_DIR="$2"
	local REPO_URL="$3"
	local TAG="$4"
	local OPT="$5"
	local clone_args=""
	local fetch_args=""
	# Set git arguments for shallow clone if enabled
	if [ "${SHALLOW_CLONE}" = true ]; then
		clone_args="--depth 1 --single-branch --no-tags"
		fetch_args="--depth 1 origin tag ${TAG}"
	else
		fetch_args="--all --tags"
	fi

	# If directory exists, verify it's a git repo
	if [ -d "${LDK_SOURCE_DIR}" ]; then
		if ! git -C "${LDK_SOURCE_DIR}" rev-parse --git-dir >/dev/null 2>&1; then
			echo "Directory ${LDK_SOURCE_DIR} exists but is not a git " \
				"repository. Please remove it first."
			return 1
		fi

		# Check if this is a shallow clone that needs conversion
		if [ "${SHALLOW_CLONE}" = false ]; then
			IS_SHALLOW=$(git -C "${LDK_SOURCE_DIR}" rev-parse --is-shallow-repository)
			if [ "${IS_SHALLOW}" = "true" ]; then
				echo "Converting shallow clone to full clone for ${WHAT_SOURCE}..."
				if ! git -C "${LDK_SOURCE_DIR}" fetch --unshallow > \
					/dev/null 2>&1; then
					echo "Failed to convert shallow clone to full clone for " \
						"${WHAT_SOURCE}"
					return 1
				fi
			fi
		fi

	else
		# Clone repository if it doesn't exist
		echo "Cloning ${WHAT_SOURCE} repository ..."
		if ! git clone ${clone_args} "${REPO_URL}" "${LDK_SOURCE_DIR}" > \
			/dev/null 2>&1; then
			echo "Failed to clone ${WHAT_SOURCE} repository"
			return 1
		fi
	fi

	# Fetch tag
	echo "Fetching tag ${TAG} for ${WHAT_SOURCE} ..."
	if ! git -C "${LDK_SOURCE_DIR}" fetch ${fetch_args} > /dev/null 2>&1; then
		echo "Failed to fetch tag ${TAG} for ${WHAT_SOURCE}"
		return 1
	fi

	# Checkout to the tag
	echo "Checking out tag ${TAG} for ${WHAT_SOURCE} ..."
	if ! git -C "${LDK_SOURCE_DIR}" checkout -b \
		"mybranch_$(date +%Y-%m-%d-%s)" "${TAG}" > /dev/null 2>&1; then
		echo "Failed to checkout tag ${TAG} for ${WHAT_SOURCE}"
		return 1
	fi

	echo "Successfully synced ${WHAT_SOURCE} to ${LDK_SOURCE_DIR} with " \
		"tag ${TAG}"
	echo ""
	return 0
}

function SetTag {
	# If a specific tag is provided by the user, use it
	if [ -n "${TAG}" ]; then
		echo "Using user provided tag ${TAG} for source sync"
		return
	fi

	# Otherwise, get the latest tag from the reference repository
	echo "No user provided tag.. getting latest tag from reference repository"
	local REPO_URL="${GIT_SERVER}/${REFERENCE_REPO}"
	local LATEST_TAG=""
	local TAG_FILTER_PATTERN="refs/tags/${TAG_PREFIX}[0-9.]*$"

	# Use git ls-remote to get tags without cloning
	# Filter for "TAG_PREFIX" tags to find the latest public release tag
	LATEST_TAG=$(git ls-remote --tags "${REPO_URL}" | \
		grep -o "${TAG_FILTER_PATTERN}" | \
		sed 's|refs/tags/||g' | \
		sort -rV | \
		head -n 1)

	if [ -z "${LATEST_TAG}" ]; then
		echo "ERROR: No tags found in reference repository ${REPO_URL}!"
		echo "Unable to determine latest tag.. exiting"
		exit 1
	fi

	echo "Using latest tag ${LATEST_TAG} for source sync"
	TAG="${LATEST_TAG}"
}

# verify that git is installed
if  ! which git > /dev/null  ; then
  echo "ERROR: git is not installed. If your linux distro is 10.04 or later,"
  echo "git can be installed by 'sudo apt-get install git-core'."
  exit 1
fi

# Initialize global variables
InitVars

# prepare processing ....
GETOPT=":ehd:t:s"

OIFS="$IFS"
IFS=$(echo -en "\n\b")
SOURCE_INFO_PROCESSED=($(echo "$SOURCE_INFO"))
IFS="$OIFS"
NSOURCES=${#SOURCE_INFO_PROCESSED[*]}

# Track processed options to avoid duplicates
PROCESSED_OPS=()
for ((i=0; i < NSOURCES; i++)); do
	OP=$(echo "${SOURCE_INFO_PROCESSED[i]}" | cut -f 1 -d ':')
	# Check if this option has already been added
	if [[ ! " ${PROCESSED_OPS[*]} " =~ \ ${OP}\  ]]; then
		GETOPT="${GETOPT}${OP}"
		PROCESSED_OPS+=("${OP}")
	fi
done

# parse the command line first
while getopts "$GETOPT" opt; do
	case $opt in
		d)
			case $OPTARG in
				-[A-Za-z]*)
					Usages "$SCRIPT_NAME"
					exit 1
					;;
				*)
					LDK_DIR="$OPTARG"
					;;
			esac
			;;
		e)
			EOE=1
			;;
		s)
			SHALLOW_CLONE=true
			;;
		h)
			Usages "$SCRIPT_NAME"
			exit 1
			;;
		t)
			case $OPTARG in
				-[A-Za-z]*)
					Usages "$SCRIPT_NAME"
					exit 1
					;;
				*)
					TAG="$OPTARG"
					;;
			esac
			;;
		[A-Za-z])
			ProcessSwitch "-$opt" "$OPTARG"
			;;
		:)
			Usages "$SCRIPT_NAME"
			exit 1
			;;
		\?)
			echo "Terminating... wrong switch: $@" >&2
			Usages "$SCRIPT_NAME"
			exit 1
			;;
	esac
done
shift $((OPTIND-1))

# Set the tag for the sources
SetTag
echo "Tag is set to ${TAG}"
GRET=0
for ((i=0; i < NSOURCES; i++)); do
	OPT=$(echo "${SOURCE_INFO_PROCESSED[i]}" | cut -f 1 -d ':')
	WHAT=$(echo "${SOURCE_INFO_PROCESSED[i]}" | cut -f 2 -d ':')
	REPO=$(echo "${SOURCE_INFO_PROCESSED[i]}" | cut -f 3-4 -d ':')
	DNLOAD=$(echo "${SOURCE_INFO_PROCESSED[i]}" | cut -f 6 -d ':')

	if [ $DALL -eq 1 -o "${DNLOAD}" = "y" ]; then
		DownloadAndSync "$WHAT" "${LDK_DIR}/${WHAT}" "${REPO}" "${TAG}" "${OPT}"
		tRET=$?
		let GRET=GRET+tRET
		if [ $tRET -ne 0 -a $EOE -eq 1 ]; then
			exit $tRET
		fi
	fi
done

# Define path as variable to avoid duplication
NVETHERNET_PATH="${LDK_DIR}/nvidia-oot/drivers/net/ethernet/nvidia/nvethernet"
if [ -d "${NVETHERNET_PATH}" ]; then
	ln -sf ../../../../../../nvethernetrm "${NVETHERNET_PATH}/nvethernetrm"
fi

exit $GRET
