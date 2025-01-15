#!/bin/bash

# Copyright (c) 2020-2021, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
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

# This is a helper script to repackage an existing L4T Debian package.

set -e

WORKING_DIR="$(cd "$(dirname "${0}")" && pwd)"
REPACK_DIR=$(mktemp -d)
METADATA_DIR="${REPACK_DIR}/DEBIAN"
default_custom="default0"
comment="Custom version."
injections=()
custom=
maintainer=

function cleanup()
{
	rm -rf "${REPACK_DIR}"
}

function usage()
{
	ret=0
	if [ -n "${1}" ]; then
		echo "${1}"
		ret=1
	fi
cat <<EOF
Usage: ${0##*/} [-d <pkg>=<ver>,...] [-f <file>] [-h] [-i <src>:<dst>[:<perm>]] [-m <msg>]
                  [-n <maintainer>] [-v <ver>] package.deb

  -d update the version number of a particular dependent package. The version number is specified
     in the format <pkg>=<version>. Multiple packages can be specified with commas.
  -f specify an injection file. Each line in the file represents a regular file to be injected and
     should comply with the format: <source>:<destination>[:<permission>]. File permission must be
     specified for a new file.
  -h show this help.
  -i specify a particular file to be injected. The file must be specified in the format mentioned
     in '-f' otpion. Multiple files can be specified with multiple '-i' options.
  -m specify a comment line to be added in the changelog. The default is "Custom version.".
  -n specify maintainer info in the format: "full name <email-address>". This option is mandatory.
  -v specify a custom version string. The string will be appended to the L4T package version
     with a "+". The default is "default0".
EOF
	exit "${ret}"
}

function init()
{
	if [ -z "${package}" ]; then
		usage "ERROR: No package specified"
	fi
	if [ -z "${maintainer}" ]; then
		usage "ERROR: maintainer is mandatory"
	fi

	package_name=$(dpkg -f "${package}" "Package")
	original_version=$(dpkg -f "${package}" "Version")
	if [ -n "${custom}" ]; then
		updated_version="${original_version%%+*}+${custom}"
	else
		custom_version=$(cut -d '+' -s -f 2 <<< "${original_version}")
		if [ -z "${custom_version}" ]; then
			updated_version="${original_version}+${default_custom}"
		elif [[ "${original_version}" =~ ^[^+]*\+[^0-9]+[0-9]+$ ]]; then
			# automtically increase the numeric portion if the version is "<l4t_version>+<string><N>"
			updated_version=$(sed -r 's/^(.*[^0-9])([0-9]+)$/echo "\1"$((\2+1))/e' <<< "${original_version}")
		else
			usage "ERROR: Need to specify a custom version string"
		fi
	fi
}

function unpack()
{
	echo "Unpacking ${package}"
	dpkg-deb -R "${package}" "${REPACK_DIR}"
}

function inject_files()
{
	echo "Injecting files..."
	for file in "${injections[@]}"; do
		IFS=":" read -r src dst perm <<< "${file}"
		dst="${REPACK_DIR}${dst}"
		mode=
		if [ -n "${perm}" ]; then
			mode="${perm}"
		else
			if [ -e "${dst}" ]; then
				mode=$(stat -c %a "${dst}")
			else
				usage "ERROR: No permission specified for ${dst/${REPACK_DIR}/}"
			fi
		fi
		install -Dv -m "${mode}" "${src}" "${dst}"
	done
}

function update_version()
{
	echo "Updating version to ${updated_version}"
	sed -ri "s/(^Version:) (.*)$/\1 ${updated_version}/" "${METADATA_DIR}/control"
}

function update_dependencies()
{
	echo "Updating dependencies"
	exist_depends=()
	depends=$(dpkg -f "${package}" "Depends" | sed 's/, /,/g')
	if [ -n "${depends}" ]; then
		IFS=',' read -r -a exist_depends <<< "${depends}"
	fi

	IFS=',' read -r -a deps <<< "${dependencies}"
	for item in "${deps[@]}"; do
		[[ "${item}" =~ .*=.* ]] || usage "ERROR: Invalid dependency format: ${item}"
		pkg="${item%%=*}"
		ver="${item##*=}"
		i=0
		found="false"
		for i in "${!exist_depends[@]}"; do
			IFS=" " read -r dep_pkg dep_ver <<< "${exist_depends[$i]}"
			if [[ "${dep_pkg}" == "${pkg}" ]]; then
				if [[ "${dep_ver}" =~ \(=\ .*\) ]]; then
					exist_depends[$i]="${pkg} (= ${ver})"
					found="true"
					break
				else
					echo "ERROR: Attempting to update an non-equal dependency: ${exist_depends[$i]}"
					exit 1
				fi
			fi
		done
		if [ "${found}" != "true" ]; then
			echo "ERROR: Attempting to update an non-existant dependency: ${item}"
			exit 1
		fi
	done
	depends=$(IFS=,; echo "${exist_depends[*]}" | sed 's/,/, /g')
	sed -ri "s/^(Depends:) (.*)$/\1 ${depends}/" "${METADATA_DIR}/control"
}

function update_changelog()
{
	echo "Updating the changelog"
	changelog=$(mktemp)
	date=$(date -R)
cat <<EOF > "${changelog}"
${package_name} ($updated_version) stable; urgency=low

  * ${comment}

 -- ${maintainer}  ${date}

EOF
	original="${REPACK_DIR}/usr/share/doc/${package_name}/changelog.Debian.gz"
	zcat "${original}" >> "${changelog}"
	gzip -9nf -c "${changelog}" > "${original}"
}

function recalc_md5sum()
{
	echo "Recalulating md5sum"
	pushd "${REPACK_DIR}" > /dev/null
	# All files in DEBIAN/ and all conffiles are omitted from the md5sums file per dh_md5sums
	find . -type f ! -path "./${METADATA_DIR##*/}/*" ! -path "./etc/*" | LC_ALL=C sort | xargs md5sum | \
		sed -e 's@\./@ @' > "${METADATA_DIR}/md5sums"
	popd > /dev/null
}

# Calculating the installed size based on the algorithm used in dpkg-gencontrol
function recalc_installed_size()
{
	echo "Recalulating the installed size"
	installed_size=0
	list=($(find "${REPACK_DIR}" \( -type f -o -type l \) \
		! -path "*/${METADATA_DIR##*/}/control" ! -path "*/${METADATA_DIR##*/}/md5sums"))
	for file in "${list[@]}"; do
		size=$(stat -c %s "${file}")
		((installed_size+=(${size}+1023)/1024))
	done

	((installed_size+=$(find "${REPACK_DIR}" ! \( -type f -o -type l \) | wc -l)))
	sed -ri "s/(^Installed-Size:) ([0-9]*)$/\1 ${installed_size}/" "${METADATA_DIR}/control"
}

function repack()
{
	echo "Repacking"
	pkg=$(basename "${package}")
	fakeroot dpkg -b "${REPACK_DIR}" "${WORKING_DIR}/${pkg/$original_version/$updated_version}"
}

while getopts d:f:hi:m:n:v: opt; do
	case "${opt}" in
		d) dependencies=${OPTARG};;
		f) injections+=($(cat ${OPTARG}));;
		h) usage;;
		i) injections+=(${OPTARG});;
		m) comment=${OPTARG};;
		n) maintainer=${OPTARG};;
		v) custom=${OPTARG};;
		*) usage "Unknown option";;
	esac
done

shift $((OPTIND-1))
if [ $# -gt 1 ]; then
	usage "ERROR: Too many non-option arguments"
fi
package="${1}"

trap cleanup EXIT
init
unpack
if [ "${#injections[@]}" -gt 0 ]; then
	inject_files
fi
update_version
update_dependencies
update_changelog
recalc_md5sum
recalc_installed_size
repack
