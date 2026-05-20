#!/bin/bash

# Package OTA deb files into a release tarball.
# Optionally upload to GitLab or GitHub Release.
#
# Usage: ./create_release.sh [VERSION] [OPTIONS]
#   VERSION              Release version tag (e.g., r36.5.0)
#   --upload gitlab      Upload to GitLab Release via API
#   --upload github      Upload to GitHub Release via gh CLI
#   -h, --help           Show help

set -e

REPO_DIR="$(cd "$(dirname "$(readlink -f "${0}")")/../.." && pwd)"
UPLOAD_TARGET=""
VERSION=""

usage() {
	echo "Usage: ./create_release.sh [VERSION] [OPTIONS]"
	echo "  VERSION              Release version tag (e.g., r36.5.0)"
	echo "  --upload gitlab      Upload to GitLab Release via API"
	echo "  --upload github      Upload to GitHub Release via gh CLI"
	echo "  -h, --help           Show help"
	exit 0
}

parse_args() {
	while [ $# -gt 0 ]; do
		case "${1}" in
			--upload)
				case "${2}" in
					gitlab) UPLOAD_TARGET="gitlab"; shift 2 ;;
					github) UPLOAD_TARGET="github"; shift 2 ;;
					*) UPLOAD_TARGET="github"; shift ;;
				esac
				;;
			-h|--help) usage ;;
			r36.*|v*) VERSION="${1}"; shift ;;
			*) echo "Error: Invalid option ${1}"; usage ;;
		esac
	done
	if [ -z "${VERSION}" ]; then
		VERSION="r36.5.0"
	fi
}

check_prereqs() {
	if [ ! -d "${REPO_DIR}/ota_packages" ]; then
		echo "Error: ${REPO_DIR}/ota_packages/ not found. Run build_debs.sh first."
		exit 1
	fi
	if [ -z "$(ls ${REPO_DIR}/ota_packages/*.deb 2>/dev/null)" ]; then
		echo "Error: No deb packages in ${REPO_DIR}/ota_packages/"
		exit 1
	fi
	if [ ! -f "${REPO_DIR}/ota_packages/sha256sum.txt" ]; then
		echo "Warning: sha256sum.txt not found. Run build_debs.sh to generate."
	fi
	if [ "${UPLOAD_TARGET}" = "github" ] && ! command -v gh &>/dev/null; then
		echo "Error: gh CLI not found. Install: https://cli.github.com/"
		exit 1
	fi
	if [ "${UPLOAD_TARGET}" = "gitlab" ] && [ -z "${CI_JOB_TOKEN}" ]; then
		echo "Error: CI_JOB_TOKEN not set. GitLab upload must run inside a CI pipeline."
		exit 1
	fi
}

create_release() {
	local pkg_dir="${REPO_DIR}/ota_packages"
	local staging="/tmp/ota-${VERSION}"
	local output="${REPO_DIR}/ota_packages/ota-${VERSION}.tar.gz"

	rm -rf "${staging}"
	mkdir -p "${staging}"

	# Copy latest deb packages (pick the most recent timestamp if multiple)
	for pkg in nvidia-l4t-kernel nvidia-l4t-kernel-dtbs nvidia-l4t-kernel-headers \
		nvidia-l4t-kernel-oot-modules nvidia-l4t-kernel-oot-headers \
		nvidia-l4t-display-kernel nvidia-l4t-initrd; do
		latest=$(ls -t "${pkg_dir}/${pkg}"_*.deb 2>/dev/null | head -1)
		if [ -n "${latest}" ]; then
			cp "${latest}" "${staging}/"
		fi
	done

	# Copy sha256sum.txt
	if [ -f "${pkg_dir}/sha256sum.txt" ]; then
		cp "${pkg_dir}/sha256sum.txt" "${staging}/"
	fi

	# Copy upgrade script and docs
	cp "${REPO_DIR}/tools/ota/ota_upgrade.sh" "${staging}/"
	cp "${REPO_DIR}/OTA_UPGRADE.md" "${staging}/"

	# Count packages
	count=$(ls "${staging}"/*.deb 2>/dev/null | wc -l)
	if [ "${count}" -lt 7 ]; then
		echo "Warning: Only ${count} deb packages found (expected 7)"
	fi

	# Create tarball
	cd /tmp
	tar czf "${output}" "ota-${VERSION}/"
	rm -rf "${staging}"

	# Generate sha256 for the tarball
	sha256sum "${output}" > "${output}.sha256"

	echo ""
	echo "========================================="
	echo "Release package created"
	echo "========================================="
	echo "File:     ${output}"
	echo "Size:     $(du -sh "${output}" | cut -f1)"
	echo "SHA256:   $(cat "${output}.sha256")"
	echo ""
	ls -lh "${output}"
	echo "========================================="
}

upload_gitlab() {
	local pkg_dir="${REPO_DIR}/ota_packages"
	local tarball="${pkg_dir}/ota-${VERSION}.tar.gz"
	local sha_file="${tarball}.sha256"
	local api="${CI_API_V4_URL}/projects/${CI_PROJECT_ID}"
	local pkg_base="${api}/packages/generic/ota-upgrade/${VERSION}"

	echo ""
	echo "Uploading to GitLab Package Registry..."

	# Upload tarball
	echo "  Uploading ota-${VERSION}.tar.gz..."
	curl --fail --silent --show-error \
		--header "JOB-TOKEN: ${CI_JOB_TOKEN}" \
		--upload-file "${tarball}" \
		"${pkg_base}/ota-${VERSION}.tar.gz"

	# Upload sha256
	echo "  Uploading ota-${VERSION}.tar.gz.sha256..."
	curl --fail --silent --show-error \
		--header "JOB-TOKEN: ${CI_JOB_TOKEN}" \
		--upload-file "${sha_file}" \
		"${pkg_base}/ota-${VERSION}.tar.gz.sha256"

	echo ""
	echo "Creating GitLab Release..."

	# Write release description
	local desc_file
	desc_file="$(mktemp)"
	cat > "${desc_file}" <<DESC
## OTA Incremental Upgrade ${VERSION}

### Supported boards
- reComputer J401 / J40mini / J101
- reComputer Industrial / Rugged / Super
- reServer series
- Jetson Orin Nano Developer Kit (J3011)
- reComputer Mini AGX Orin (J501)

### Quick start
\`\`\`bash
# Download from this release page
# Verify integrity
sha256sum -c ota-${VERSION}.tar.gz.sha256

# Extract and upgrade
tar xzf ota-${VERSION}.tar.gz
sudo bash ota-${VERSION}/ota_upgrade.sh ota-${VERSION}
\`\`\`

### Safety features
- SHA256 package integrity verification
- Automatic backup and rollback on failure
- Fallback boot entry for recovery
- Full upgrade logging
DESC

	# Build JSON payload with python (available on CI runner)
	local release_json
	release_json="$(mktemp)"
	python3 -c "
import json, sys
with open('${desc_file}') as f:
    desc = f.read()
data = {
    'tag_name': '${VERSION}',
    'name': 'OTA Upgrade ${VERSION}',
    'description': desc,
    'assets': {
        'links': [
            {
                'name': 'ota-${VERSION}.tar.gz',
                'url': '${pkg_base}/ota-${VERSION}.tar.gz',
                'link_type': 'package'
            },
            {
                'name': 'ota-${VERSION}.tar.gz.sha256',
                'url': '${pkg_base}/ota-${VERSION}.tar.gz.sha256',
                'link_type': 'other'
            }
        ]
    }
}
json.dump(data, sys.stdout)
" > "${release_json}"

	curl --fail --silent --show-error \
		--request POST \
		--header "JOB-TOKEN: ${CI_JOB_TOKEN}" \
		--header "Content-Type: application/json" \
		--data @"${release_json}" \
		"${api}/releases"

	rm -f "${desc_file}" "${release_json}"

	echo ""
	echo "GitLab Release published: ${CI_PROJECT_URL}/-/releases/${VERSION}"
}

upload_github() {
	local pkg_dir="${REPO_DIR}/ota_packages"
	local tarball="${pkg_dir}/ota-${VERSION}.tar.gz"

	echo ""
	echo "Uploading to GitHub Release..."

	local notes_header
	notes_header="$(mktemp)"
	cat > "${notes_header}" <<HEADER
## OTA Incremental Upgrade ${VERSION}

### Supported boards
- reComputer J401 / J40mini / J101
- reComputer Industrial / Rugged / Super
- reServer series
- Jetson Orin Nano Developer Kit (J3011)
- reComputer Mini AGX Orin (J501)

### Quick start
\`\`\`bash
# 1. Download and extract
wget https://github.com/Seeed-Studio/Linux_for_Tegra/releases/download/${VERSION}/ota-${VERSION}.tar.gz
wget https://github.com/Seeed-Studio/Linux_for_Tegra/releases/download/${VERSION}/ota-${VERSION}.tar.gz.sha256

# 2. Verify integrity
sha256sum -c ota-${VERSION}.tar.gz.sha256

# 3. Extract and upgrade
tar xzf ota-${VERSION}.tar.gz
sudo bash ota-${VERSION}/ota_upgrade.sh ota-${VERSION}
\`\`\`

### Safety features
- SHA256 package integrity verification
- Automatic backup and rollback on failure
- Fallback boot entry for recovery
- Full upgrade logging

---

HEADER

	gh release create "${VERSION}" \
		"${tarball}" \
		"${tarball}.sha256" \
		--repo Seeed-Studio/Linux_for_Tegra \
		--title "OTA Upgrade ${VERSION}" \
		--generate-notes \
		--notes-file "${notes_header}"

	rm -f "${notes_header}"

	echo ""
	echo "Release published: https://github.com/Seeed-Studio/Linux_for_Tegra/releases/tag/${VERSION}"
}

parse_args "$@"
check_prereqs
create_release
if [ "${UPLOAD_TARGET}" = "gitlab" ]; then
	upload_gitlab
elif [ "${UPLOAD_TARGET}" = "github" ]; then
	upload_github
fi
